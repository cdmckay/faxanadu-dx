/* Speaker names. USA 57dd23d1, chipx86 byte-exact disassembly.
 * Every interaction script starts with an entity byte; $80 and up picks a
 * portrait, and IScripts_Begin then has IScripts_LoadPortraitTiles store it,
 * less its top bit, in IScript_PortraitID ($03C7), before it frames the
 * portrait (TextBox_SetCoordsForPortrait: tiles 2,14, 8 x 10) and the
 * dialogue box beside it (TextBox_SetCoordsForNPC: 10,16, 20 x 8). The ID
 * stays until IScriptAction_EndScript's IScripts_ClearPortraitImage sets it to
 * $FF, just before the boxes are cleared, so it holds through every message,
 * question, shop list and Guru travel line in between. Scripts with a
 * portrait only run in buildings (AREA_BUILDINGS).
 *
 * While the portrait's frame is on screen, this draws the speaker's name on a
 * tab above the dialogue box, at its left edge, in the game's own font and
 * frame (fax_text.c). A shop's list (Shop_Draw: 10,8, 20 x 20) covers the
 * dialogue box and rises above it, so the tab moves up onto the list while it
 * is open. The names are the dialogue's where it names someone, and the King
 * and the Gurus are "of" their town: the zone table (fax_zone.c) at the
 * building's door, or for a temple its town by temple number. No hooks, no
 * ROM, nothing written to the machine: it reads RAM and the nametable when the
 * picture is presented, so it keeps no state, and a save state shows what it
 * did when saved. See docs/GAME_INTERNALS.md, "Portraits" and "The text box". */
#include "fax_speaker.h"
#include "fax_text.h"
#include "fax_zone.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "mod_runtime.h"
#ifdef CYC_WITH_SDL
#include "cyc_tcp.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef FAX_SPEAKER_LOG
#define FAX_SPEAKER_LOG(...) fprintf(stderr, __VA_ARGS__)
#endif

/* RAM */
#define AREA_CURRENT_AREA   0x0024u
#define AREA_CURRENT_SCREEN 0x0063u /* the building's number, in AREA_BUILDINGS */
#define TEXTBOX_X           0x0208u /* TextBox_X, TextBox_Y: the box last opened or closed, in tiles */
#define TEXTBOX_Y           0x0209u
#define ISCRIPT_PORTRAIT_ID 0x03C7u /* the entity byte & $7F while a portrait shows; $FF none */
#define AREA_SAVED_AREA     0x03D5u /* Game_EnterBuilding: where the hero stood, at the door */
#define AREA_SAVED_SCREEN   0x03D6u

/* In tiles of the 256 x 240 picture. */
enum { PORTRAIT_X = 2, PORTRAIT_Y = 14, PORTRAIT_W = 8, PORTRAIT_H = 10 }; /* TextBox_SetCoordsForPortrait */
enum { DIALOGUE_X = 10, DIALOGUE_Y = 16, DIALOGUE_W = 20 }; /* TextBox_SetCoordsForNPC, with a portrait */
enum { HUD_ROWS = 4, TAB_ROWS = 3 };                        /* a tab: its frame around one line */
/* TextBox_Open's frame corner tiles */
enum { CORNER_TOP_LEFT = 1, CORNER_TOP_RIGHT = 3, CORNER_BOTTOM_LEFT = 5, CORNER_BOTTOM_RIGHT = 6 };

/* By portrait. The Pink Shirt Guy's and the Smoking Guy's portraits are in
 * the ROM, but no script shows them. */
static const char *const titles[FAX_PORTRAITS] = {
    [FAX_PORTRAIT_KING] = "King",                     /* $33 "The King is waiting for you." */
    [FAX_PORTRAIT_GURU] = "Guru",                     /* $94 "The Guru of Conflate has..." */
    [FAX_PORTRAIT_MARTIAL_ARTIST] = "Martial Artist", /* $19 "a martial art lesson" */
    [FAX_PORTRAIT_MAGICIAN] = "Magician",             /* $15 "I shall give magic", $17 "I'll teach you" */
    [FAX_PORTRAIT_DOCTOR] = "Doctor",                 /* $45 "Our doctor can help." */
    [FAX_PORTRAIT_NURSE] = "Nurse",                   /* in the hospitals, beside the Doctor */
    [FAX_PORTRAIT_MEAT_SALESMAN] = "Meat Seller",     /* $07-$0D "Dried meat to bring energy" */
    [FAX_PORTRAIT_TOOLS_SALESMAN] = "Tool Seller",    /* $0F "I sell tools." */
    [FAX_PORTRAIT_KEY_SALESMAN] = "Key Seller",       /* $0E "I sell keys." */
};

/* DEST_SCREEN_FOR_TEMPLE_SPAWN: each temple's building, by temple. The
 * Mascon Guru's door opens on the Mist, but he is Mascon's (Guru travel). */
static const struct {
    uint8_t building;
    int8_t zone;
} temples[] = {
    { 0x02, FAX_ZONE_EOLIS },    { 0x0B, FAX_ZONE_APOLUNE },       { 0x10, FAX_ZONE_FOREPAW },
    { 0x1E, FAX_ZONE_MASCON },   { 0x23, FAX_ZONE_VICTIM },        { 0x2B, FAX_ZONE_CONFLATE },
    { 0x33, FAX_ZONE_DAYBREAK }, { 0x3C, FAX_ZONE_DARTMOOR_TOWN },
};

/* A zone as a place someone is "of": a town or region by its name, a tower or
 * a castle by its own ("Castle of Fraternal": Fraternal). */
static const char *place(int zone) {
    const FaxZone *z = fax_zone(zone);
    if (!z) return NULL;
    const char *of = strstr(z->name, " of ");
    return (z->kind == FAX_ZONE_TOWER || z->kind == FAX_ZONE_CASTLE) && of ? of + 4 : z->name;
}

bool fax_speaker_name(unsigned portrait, unsigned building, int zone, char out[FAX_SPEAKER_NAME_MAX + 1]) {
    if (portrait >= FAX_PORTRAITS || !titles[portrait]) return false;
    const char *at = NULL;
    if (portrait == FAX_PORTRAIT_GURU)
        for (size_t i = 0; i < sizeof temples / sizeof temples[0]; ++i)
            if (temples[i].building == building) zone = temples[i].zone;
    if (portrait == FAX_PORTRAIT_KING || portrait == FAX_PORTRAIT_GURU) at = place(zone);
    if (at) snprintf(out, FAX_SPEAKER_NAME_MAX + 1, "%s of %s", titles[portrait], at);
    else snprintf(out, FAX_SPEAKER_NAME_MAX + 1, "%s", titles[portrait]);
    return true;
}

/* ---- the machine ---- */
static bool enabled, failed;
static uint32_t *canvas;
static size_t capacity;

static uint8_t q(uint16_t addr) { return cyc_mod_peek(addr); }

/* TextBox_Open draws a frame top row first and TextBox_Close clears it top
 * row first, so all four corners (in the last frame: fax_text_shown_tile)
 * mean the whole frame is up. */
static bool portrait_box_shown(void) {
    enum { RIGHT = PORTRAIT_X + PORTRAIT_W - 1, BOTTOM = PORTRAIT_Y + PORTRAIT_H - 1 };
    return fax_text_shown_tile(PORTRAIT_X, PORTRAIT_Y) == CORNER_TOP_LEFT &&
           fax_text_shown_tile(RIGHT, PORTRAIT_Y) == CORNER_TOP_RIGHT &&
           fax_text_shown_tile(PORTRAIT_X, BOTTOM) == CORNER_BOTTOM_LEFT &&
           fax_text_shown_tile(RIGHT, BOTTOM) == CORNER_BOTTOM_RIGHT;
}

typedef struct {
    char name[FAX_SPEAKER_NAME_MAX + 1];
    int col, row; /* the tab's top-left cell */
    int pad;      /* blank cells either side of the name: 1, or 0 for a name that would not fit */
} Tab;
static int tab_cols(const Tab *t) { return (int)strlen(t->name) + 2 * t->pad; } /* inside the frame */

/* Who is speaking, if anyone, by RAM alone. */
static bool speaker(char name[FAX_SPEAKER_NAME_MAX + 1]) {
    if (q(AREA_CURRENT_AREA) != FAX_AREA_BUILDINGS) return false; /* also the title, where RAM is 0 */
    int zone = fax_zone_at(q(AREA_SAVED_AREA), q(AREA_SAVED_SCREEN));
    return fax_speaker_name(q(ISCRIPT_PORTRAIT_ID), q(AREA_CURRENT_SCREEN), zone, name);
}

/* The tab to show now: the speaker's, while the portrait's frame is up. */
static bool tab_now(Tab *t) {
    if (!speaker(t->name) || !portrait_box_shown()) return false;
    /* On the dialogue box, or on a shop's list while it covers it. */
    unsigned bottom = DIALOGUE_Y, x = q(TEXTBOX_X), y = q(TEXTBOX_Y);
    if (x == DIALOGUE_X && y >= HUD_ROWS + TAB_ROWS && y < DIALOGUE_Y) bottom = y;
    t->col = DIALOGUE_X;
    t->row = (int)bottom - TAB_ROWS;
    t->pad = (int)strlen(t->name) + 4 <= DIALOGUE_W ? 1 : 0;
    return true;
}

/* Where native pixel (x, y) is in a presented picture w x h: as it is, or
 * scaled to fit and centered, as Seamless shows a building (fax_world.c). */
static void to_picture(int w, int h, int *x, int *y) {
    int nw = w < 256 ? w : 256, nh = 240 * nw / 256;
    if (nh > h) {
        nh = h;
        nw = 256 * nh / 240;
    }
    *x = (w - nw) / 2 + *x * nw / 256;
    *y = *y * nh / 240;
}

const uint32_t *fax_speaker_present(const uint32_t *picture, int *w, int *h) {
    Tab tab;
    if (!enabled || failed || !tab_now(&tab)) return picture;
    if (!fax_text_loaded() && !fax_text_load()) {
        FAX_SPEAKER_LOG("[speaker names] cannot read the game's font; speaker names are off\n");
        failed = true;
        return picture;
    }
    int width = 0, height = 0;
    const uint32_t *from = picture;
    if (from) width = *w, height = *h;
    else from = cyc_render_present(&width, &height);
    if (!from || width <= 0 || height <= 0) return picture;
    size_t n = (size_t)width * (size_t)height;
    if (n > capacity) {
        uint32_t *grown = realloc(canvas, n * sizeof *grown);
        if (!grown) return picture;
        canvas = grown;
        capacity = n;
    }
    memcpy(canvas, from, n * sizeof *canvas);
    int x = tab.col * FAX_TEXT_CELL, y = tab.row * FAX_TEXT_CELL;
    to_picture(width, height, &x, &y);
    fax_text_box(canvas, width, height, x, y, tab_cols(&tab), 1, 255);
    fax_text_draw(canvas, width, height, x + (1 + tab.pad) * FAX_TEXT_CELL, y + FAX_TEXT_CELL, tab.name, 255);
    *w = width;
    *h = height;
    return canvas;
}

#ifdef CYC_WITH_SDL
static void speaker_state(int id, const char *line) {
    (void)line;
    char fields[384], name[FAX_SPEAKER_NAME_MAX + 1];
    Tab tab;
    bool named = speaker(name), shown = enabled && !failed && tab_now(&tab);
    const FaxZone *door = fax_zone(fax_zone_at(q(AREA_SAVED_AREA), q(AREA_SAVED_SCREEN)));
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"font\":%s,\"area\":%u,\"building\":%u,\"portrait\":%u,\"door\":%s%s%s,"
             "\"name\":%s%s%s,\"box\":%s,\"shown\":%s,\"col\":%d,\"row\":%d,\"cols\":%d",
             enabled ? "true" : "false", fax_text_loaded() ? "true" : "false", q(AREA_CURRENT_AREA),
             q(AREA_CURRENT_SCREEN), q(ISCRIPT_PORTRAIT_ID), door ? "\"" : "", door ? door->name : "null",
             door ? "\"" : "", named ? "\"" : "", named ? name : "null", named ? "\"" : "",
             portrait_box_shown() ? "true" : "false", shown ? "true" : "false", shown ? tab.col : -1,
             shown ? tab.row : -1, shown ? tab_cols(&tab) + 2 : 0);
    cyc_tcp_ok(id, fields);
}
void fax_speaker_tcp_setup(void) {
    cyc_tcp_register("speaker_state", "Speaker names: the portrait speaking, its name, and the tab on screen",
                     speaker_state);
}
#else
void fax_speaker_tcp_setup(void) {}
#endif

static void deactivate(void) { enabled = failed = false; }
static void activate(void) { enabled = true; }

NES_MOD_CONSTRUCTOR(register_speaker_names) {
    if (!nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.speaker-names", activate))
        abort();
}
