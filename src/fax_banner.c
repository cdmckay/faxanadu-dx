/* Zone names. USA 57dd23d1, chipx86 byte-exact disassembly.
 * Entering a different zone (fax_zone.c) shows its name below the HUD for
 * about two seconds, in the game's own text box and font (fax_text.c), then
 * fades. It only watches RAM at the end of each frame and draws over the
 * presented picture: no hooks, no ROM, nothing written to the machine.
 *
 * A zone is read once a screen has settled. Buildings belong to no zone, so
 * stepping out of a shop returns to the zone already named and shows nothing.
 * A respawn or a Guru travel journey lands in a temple whose door leads
 * somewhere the hero has not just been (Area_SavedArea and Area_SavedScreen,
 * which Game_EnterBuilding sets from where the hero stood); that forgets the
 * last name, so walking out shows the town's. Two zones with one name (the
 * Dartmoor region and its town) show it once. The banner holds still while a
 * text box or menu is open or the game is paused, and is hidden behind boxes:
 * a box is open by what the last frame showed (fax_text_box_showing), since
 * TextBox_Closed alone reads 0 with no box up on some screens. Slain by
 * (fax_slain.c) draws its line in the same box with fax_banner_draw.
 * See docs/GAME_INTERNALS.md, "Areas, screens and zones" and "The text box". */
#include "fax_banner.h"
#include "fax_text.h"
#include "fax_world.h"
#include "fax_zone.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "mod_runtime.h"
#include "mod_savestate.h"
#ifdef CYC_WITH_SDL
#include "cyc_tcp.h"
#endif
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef FAX_BANNER_LOG
#define FAX_BANNER_LOG(...) fprintf(stderr, __VA_ARGS__)
#endif

/* RAM */
#define SCREEN_READY_STATE      0x000Eu   /* 0 once a screen is set up */
#define INTERRUPT_HANDLERS_ON   0x0013u   /* Game_InterruptHandlersEnabled */
#define AREA_CURRENT_AREA       0x0024u
#define SCREEN_SCROLL_DIRECTION 0x0054u   /* $FF unless scrolling to the next screen */
#define AREA_CURRENT_SCREEN     0x0063u
#define GAME_PAUSED_STATE       0x0120u   /* 1 while Start has the game paused */
#define AREA_SAVED_AREA         0x03D5u   /* where a building's way out leads */
#define AREA_SAVED_SCREEN       0x03D6u

/* Frames: fade in, hold, fade out. */
enum { FADE_IN = 8, HOLD = 120, FADE_OUT = 32, SHOWN = FADE_IN + HOLD + FADE_OUT };
enum { HUD = 32, PAD = 1 };       /* the HUD's height; a blank cell either side of the name */

typedef struct {
    int8_t here;       /* the zone the hero is in; FAX_ZONE_NONE in a building */
    int8_t named;      /* the zone whose name was shown last */
    int8_t showing;    /* the zone on the banner */
    uint8_t age;       /* frames the banner has been up */
    uint8_t out_area, out_screen, out_known;   /* the last place outside a building */
} Track;

static bool enabled, failed;
static Track t = { FAX_ZONE_NONE, FAX_ZONE_NONE, FAX_ZONE_NONE, 0, 0, 0, 0 };
static unsigned banners;                       /* names shown since power-on */
static const char *last_name;
static uint32_t *canvas;
static size_t capacity;

static uint8_t q(uint16_t addr) { return cyc_mod_peek(addr); }

static void forget(void) {
    t = (Track){ FAX_ZONE_NONE, FAX_ZONE_NONE, FAX_ZONE_NONE, 0, 0, 0, 0 };
}
void fax_banner_reset(void) {
    forget();
    banners = 0;
    last_name = NULL;
}

/* The game shows a screen it has finished loading and is not scrolling. */
static bool settled(void) {
    return q(SCREEN_READY_STATE) == 0 && q(INTERRUPT_HANDLERS_ON) && q(SCREEN_SCROLL_DIRECTION) == 0xFF;
}
static bool held(void) { return fax_text_box_showing() || q(GAME_PAUSED_STATE) != 0; }

static unsigned alpha_at(unsigned age) {
    if (age < FADE_IN) return (age + 1) * 255u / FADE_IN;
    if (age < FADE_IN + HOLD) return 255;
    if (age < SHOWN) return (SHOWN - age) * 255u / (FADE_OUT + 1);
    return 0;
}

static void arrive(int zone) {
    t.here = (int8_t)zone;
    if (zone == FAX_ZONE_NONE) { t.showing = FAX_ZONE_NONE; return; }   /* a building: put it away */
    const char *name = fax_zone(zone)->name;
    bool same = t.named != FAX_ZONE_NONE && !strcmp(fax_zone(t.named)->name, name);
    t.named = (int8_t)zone;
    if (same) return;
    t.showing = (int8_t)zone;
    t.age = 0;
    ++banners;
    last_name = name;
}

/* Where the hero is, once the screen has settled. */
static void observe(void) {
    unsigned area = q(AREA_CURRENT_AREA), screen = q(AREA_CURRENT_SCREEN);
    int zone = FAX_ZONE_NONE;
    if (area == FAX_AREA_BUILDINGS) {
        uint8_t back_area = q(AREA_SAVED_AREA), back_screen = q(AREA_SAVED_SCREEN);
        /* A door leads back where the hero stood; a respawn or a journey does not. */
        if (t.out_known && (back_area != t.out_area || back_screen != t.out_screen)) t.named = FAX_ZONE_NONE;
        t.out_area = back_area; t.out_screen = back_screen;
    } else {
        t.out_area = (uint8_t)area; t.out_screen = (uint8_t)screen;
        zone = fax_zone_at(area, screen);
    }
    t.out_known = 1;
    if (zone != t.here) arrive(zone);
}

void fax_banner_step(void) {
    if (!enabled || failed) return;
    if (!fax_text_loaded() && !fax_text_load()) {
        FAX_BANNER_LOG("[zone names] cannot read the game's font; zone names are off\n");
        failed = true;
        return;
    }
    if (settled()) observe();
    if (t.showing != FAX_ZONE_NONE && !held() && ++t.age >= SHOWN) t.showing = FAX_ZONE_NONE;
}

const uint32_t *fax_banner_draw(const uint32_t *picture, int *w, int *h, const char *line, unsigned alpha) {
    if (!alpha || !line || !fax_text_loaded()) return picture;
    int width = 0, height = 0;
    const uint32_t *from = picture;
    if (from) width = *w, height = *h;
    else from = cyc_render_present(&width, &height);
    if (!from || width <= 0 || height <= 0) return picture;
    size_t n = (size_t)width * (size_t)height;
    if (from != canvas) {   /* a banner already drawn this frame is drawn over */
        if (n > capacity) {
            uint32_t *grown = realloc(canvas, n * sizeof *grown);
            if (!grown) return picture;
            canvas = grown; capacity = n;
        }
        memcpy(canvas, from, n * sizeof *canvas);
    }
    /* Below the HUD (scaled down with the picture if it is under 256 wide),
     * centered under it: in Seamless, where it put the native columns. */
    int cols = (int)strlen(line) + 2 * PAD;
    int box_w = (cols + 2) * FAX_TEXT_CELL;
    int hud = width < 256 ? HUD * width / 256 : HUD;
    int x = width < 256 ? (width - box_w) / 2 : fax_world_native_x(width) + (256 - box_w) / 2, y = hud + FAX_TEXT_CELL;
    fax_text_box(canvas, width, height, x, y, cols, 1, alpha);
    fax_text_draw(canvas, width, height, x + (1 + PAD) * FAX_TEXT_CELL, y + FAX_TEXT_CELL, line, alpha);
    *w = width; *h = height;
    return canvas;
}

void fax_banner_hide(void) { t.showing = FAX_ZONE_NONE; }
bool fax_banner_busy(void) { return enabled && !failed && t.showing != FAX_ZONE_NONE; }

const uint32_t *fax_banner_present(const uint32_t *picture, int *w, int *h) {
    if (!enabled || failed || t.showing == FAX_ZONE_NONE || fax_text_box_showing()) return picture;
    return fax_banner_draw(picture, w, h, fax_zone(t.showing)->name, alpha_at(t.age));
}

/* Save states carry the banner, so loading one resumes it as it was. */
enum { RECORD = 8, VERSION = 1 };
static bool zone_ok(int8_t z) { return z == FAX_ZONE_NONE || (z >= 0 && z < FAX_ZONE_COUNT); }
static int state_get(uint8_t *buf, int cap) {
    if (cap < RECORD) return -1;
    const uint8_t rec[RECORD] = { VERSION, (uint8_t)t.here, (uint8_t)t.named, (uint8_t)t.showing, t.age,
                                  t.out_area, t.out_screen, t.out_known };
    memcpy(buf, rec, RECORD);
    return RECORD;
}
static int state_validate(const uint8_t *buf, int len) {
    return len == 0 || (len == RECORD && buf[0] == VERSION && zone_ok((int8_t)buf[1]) && zone_ok((int8_t)buf[2]) &&
                        zone_ok((int8_t)buf[3]) && buf[4] <= SHOWN && buf[7] <= 1);
}
static int state_set(const uint8_t *buf, int len) {
    if (!state_validate(buf, len)) return 0;
    if (!len) { forget(); return 1; }   /* saved without zone names: name the zone again */
    t = (Track){ (int8_t)buf[1], (int8_t)buf[2], (int8_t)buf[3], buf[4], buf[5], buf[6], buf[7] };
    return 1;
}

#ifdef CYC_WITH_SDL
static void zone_state(int id, const char *line) {
    (void)line;
    char fields[512];
    const FaxZone *here = fax_zone(t.here), *showing = fax_zone(t.showing);
    unsigned area = q(AREA_CURRENT_AREA), screen = q(AREA_CURRENT_SCREEN);
    const FaxZone *at = fax_zone(fax_zone_at(area, screen));
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"font\":%s,\"area\":%u,\"screen\":%u,\"zone\":%s%s%s,\"here\":%s%s%s,"
             "\"showing\":%s%s%s,\"alpha\":%u,\"age\":%u,\"banners\":%u,\"last\":%s%s%s,\"box\":%s",
             enabled ? "true" : "false", fax_text_loaded() ? "true" : "false", area, screen,
             at ? "\"" : "", at ? at->name : "null", at ? "\"" : "",
             here ? "\"" : "", here ? here->name : "null", here ? "\"" : "",
             showing ? "\"" : "", showing ? showing->name : "null", showing ? "\"" : "",
             showing ? alpha_at(t.age) : 0u, t.age, banners,
             last_name ? "\"" : "", last_name ? last_name : "null", last_name ? "\"" : "",
             fax_text_box_showing() ? "true" : "false");
    cyc_tcp_ok(id, fields);
}
void fax_banner_tcp_setup(void) {
    cyc_tcp_register("zone_state", "Zone names: the zone here, the banner showing, names shown so far, a box open",
                     zone_state);
}
#else
void fax_banner_tcp_setup(void) {}
#endif

static void deactivate(void) {
    enabled = failed = false;
    fax_banner_reset();
}
static void activate(void) { enabled = true; }

NES_MOD_CONSTRUCTOR(register_zone_names) {
    if (!nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.zone-names", activate) ||
        !nes_mod_register_savestate_hook("faxanadu.zone-names", state_get, state_set) ||
        !nes_mod_register_savestate_validator("faxanadu.zone-names", state_validate)) abort();
}
