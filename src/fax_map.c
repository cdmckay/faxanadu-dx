/* The map. USA 57dd23d1, chipx86 byte-exact disassembly.
 *
 * What the hero has seen: every screen he has stood on outside (once it has
 * settled, so in both world views), the ladders and drops he has taken (a
 * move to the screen a scroll link puts above or below, or the Trunk's
 * climb), and the doors he has used. A door is found from where he was and
 * where he is: to another screen of the area, a door on the screen he left
 * that leads there (and the one he arrives at, leading back); to another
 * region, its doors on both screens; into a building, the door on the
 * screen he went in from (Area_SavedArea, Area_SavedScreen) that leads to it,
 * which a respawn or a Guru travel journey also sets, so a temple he wakes
 * in counts. Guru travel's visited Gurus count as their temples' doors.
 *
 * The Map button (M, or LT on a controller; MapKey and MapButton in
 * config.ini [Game]) opens it during play, through the game's own pause:
 * at GameLoop_CheckPauseGame's BEQ ($E02F), which returns when Start is not
 * new, Z is cleared, so the game pauses as for Start (Game_PausedState 1, the
 * music held, every feature holding as it does for a pause). In the pause
 * loop's BEQ ($E040), which waits another frame unless Start is new, it reads
 * the pad: Left and Right turn the page, and B, Start or Map close it, Z then
 * cleared so the game unpauses as for Start; otherwise Z is set, and the game
 * waits. The game's own code does the pausing; the hooks only steer the two
 * branches. Pressed during the game's own pause, it opens there.
 *
 * It is drawn over the presented picture (after every other feature, and
 * after Seamless composes its own), in the game's text box frame and font
 * (fax_text.c) and fixed NES colors: a box per screen seen, joined to its
 * neighbours seen beside it, and to those above and below by a ladder he has
 * taken; the current screen blinks; and a mark for each door used, at its
 * place on the screen: Gurus, shops, other buildings, and doors to towers,
 * castles and regions. Seen sets are named fields of the file in play
 * (fax_save.h), one an area. See docs/GAME_INTERNALS.md, "The map". */
#include "fax_map.h"
#include "fax_map_layout.h"
#include "fax_save.h"
#include "fax_text.h"
#include "fax_travel.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#ifdef CYC_WITH_SDL
#include "cyc_input.h"
#include "cyc_tcp.h"
#include <SDL.h>
#endif
#include <stdlib.h>
#include <string.h>

#ifndef FAX_MAP_LOG
#define FAX_MAP_LOG(...) fprintf(stderr, __VA_ARGS__)
#endif

extern uint32_t hw_palette_argb[512];   /* the host's NES colors, emphasis in bits 6-8 */

/* RAM */
#define SCREEN_READY_STATE      0x000Eu   /* 0 once a screen is set up */
#define INTERRUPT_HANDLERS_ON   0x0013u   /* Game_InterruptHandlersEnabled */
#define JOY1_CHANGED            0x0019u   /* newly pressed: A $80, B $40, Select $20, Start $10, Up $08, Down $04,
                                             Left $02, Right $01 */
#define AREA_CURRENT_AREA       0x0024u
#define SCREEN_SCROLL_DIRECTION 0x0054u   /* $FF unless scrolling to the next screen */
#define AREA_CURRENT_SCREEN     0x0063u
#define GAME_PAUSED_STATE       0x0120u   /* nonzero while the game is paused */
#define TEXTBOX_CLOSED          0x020Cu   /* 0 while a text box or the menu is open */
#define AREA_SAVED_AREA         0x03D5u   /* where a building's way out leads */
#define AREA_SAVED_SCREEN       0x03D6u
/* Fixed bank: GameLoop_CheckPauseGame ($E02B): LDA Joy1_ChangedButtonMask, AND #$10, BEQ RETURN_E02A,
 * LDA #$01, STA Game_PausedState, then @_waitForUnpause ($E036): JSR WaitForNextFrame, JSR Sprites_FlipRanges,
 * LDA Joy1_ChangedButtonMask, AND #$10, BEQ @_waitForUnpause, LDA #$00, STA Game_PausedState, RTS. Both
 * instructions after the BEQs load A, N and Z again, and nothing reads C or V, so Z (and A) alone steer them. */
#define PAUSE_CHECK 0xE02Fu   /* BEQ RETURN_E02A: Start not new, no pause */
#define PAUSE_LOOP  0xE040u   /* BEQ @_waitForUnpause: Start not new, another frame paused */

#define BUTTON_B     0x40u
#define BUTTON_START 0x10u
#define BUTTON_LEFT  0x02u
#define BUTTON_RIGHT 0x01u
#define P_Z          0x02u

/* The temples' interiors (area 4), by temple number; see "Temples and spawning". */
static const uint8_t temple_interior[8] = { 0x02, 0x0B, 0x10, 0x1E, 0x23, 0x2B, 0x33, 0x3C };

/* The defaults as SDL numbers them: SDL_SCANCODE_M, and the left trigger
 * (SDL_CONTROLLER_AXIS_TRIGGERLEFT, 4) pressed, in the host's binding encoding
 * (cyc_input.h CYC_PAD_AXIS: 100 + axis * 2 + positive). */
enum { DEFAULT_KEY = 16, DEFAULT_PAD = 100 + 4 * 2 + 1, AXIS_PRESS = 16384 };
enum { OPEN_WINDOW = 30 };   /* frames a press waits for the main loop's pause check */

/* ---- what the hero has seen ---- */

enum { SET = (FAX_MAP_SCREENS + 7) / 8, DOOR_SET = FAX_MAP_DOORS / 8 };
typedef struct {
    uint8_t seen[FAX_MAP_AREAS][SET];   /* screens stood on */
    uint8_t up[FAX_MAP_AREAS][SET];     /* left upward: a ladder, or the Trunk's climb */
    uint8_t down[FAX_MAP_AREAS][SET];   /* left downward: a ladder or a drop */
    uint8_t doors[FAX_MAP_AREAS][DOOR_SET];   /* door locations used, by their place in the area's list */
} Visits;

static bool enabled, failed, decoded;
static FaxMapAtlas atlas;
static Visits v;
static struct { uint8_t area, screen, valid; } last;   /* the place seen last, outside or in */
static uint8_t gurus_merged;                           /* Guru travel's visited Gurus already counted */
static bool open, closing;
static int shown = -1;                                 /* the page on display */
static uint64_t frames, want;                          /* a press waits until frame `want` */
static unsigned opens, closes, turns, presses, age;
static char label[80] = "Map: M / LT";

static uint8_t q(uint16_t a) { return cyc_mod_peek(a); }
static bool bit(const uint8_t *set, unsigned i) { return set[i / 8] >> (i % 8) & 1; }
static void set_bit(uint8_t *set, unsigned i) { set[i / 8] |= (uint8_t)(1u << (i % 8)); }

static bool valid_place(unsigned area, unsigned screen) {
    return area < FAX_MAP_AREAS && area != FAX_MAP_BUILDINGS && screen < atlas.area[area].count;
}
static void see(unsigned area, unsigned screen) { if (valid_place(area, screen)) set_bit(v.seen[area], screen); }
static void use_door(unsigned area, unsigned door) { set_bit(v.doors[area], door); }

/* A building entered from `area`'s `screen`: the door there that leads in. */
static void entered(unsigned area, unsigned screen, unsigned building) {
    if (!valid_place(area, screen)) return;
    const FaxMapArea *a = &atlas.area[area];
    for (unsigned i = 0; i < a->door_count; ++i)
        if (a->door[i].kind == FAX_MAP_DOOR_BUILDING && a->door[i].screen == screen && a->door[i].to == building) {
            use_door(area, i);
            see(area, screen);
        }
}

/* From one place outside to another. */
static void moved(unsigned from_area, unsigned from, unsigned area, unsigned to) {
    if (from_area == area) {
        const FaxMapArea *a = &atlas.area[area];
        for (unsigned d = 0; d < 4; ++d) {
            if (a->link[from][d] != to) continue;
            if (d == FAX_MAP_UP) set_bit(v.up[area], from);
            if (d == FAX_MAP_DOWN) set_bit(v.down[area], from);
            return;   /* walked, climbed or fell: no door */
        }
        if (area == FAX_MAP_TRUNK) {
            if (atlas.climb[from][0] == to) { set_bit(v.up[area], from); return; }
            if (atlas.climb[from][1] == to) { set_bit(v.down[area], from); return; }
        }
        for (unsigned i = 0; i < a->door_count; ++i) {
            const FaxMapDoor *d = &a->door[i];
            if (d->kind != FAX_MAP_DOOR_SCREEN) continue;
            if ((d->screen == from && d->to == to) || (d->screen == to && d->to == from)) use_door(area, i);
        }
        return;
    }
    if (from_area == FAX_MAP_TOWNS || area == FAX_MAP_TOWNS) return;   /* a town's edge */
    const FaxMapArea *a = &atlas.area[from_area], *b = &atlas.area[area];
    for (unsigned i = 0; i < a->door_count; ++i)
        if (a->door[i].kind == FAX_MAP_DOOR_REGION && a->door[i].screen == from) use_door(from_area, i);
    for (unsigned i = 0; i < b->door_count; ++i)
        if (b->door[i].kind == FAX_MAP_DOOR_REGION && b->door[i].screen == to) use_door(area, i);
}

/* Guru travel's visited Gurus: their temples' doors and screens. Eolis's
 * counts as visited from the start of every game, so it is left to the walk. */
static void merge_gurus(void) {
    uint8_t gurus = fax_travel_visited() & (uint8_t)~1u;
    if (gurus == gurus_merged) return;
    for (unsigned t = 1; t < 8; ++t) {
        if (!(gurus >> t & 1)) continue;
        for (unsigned area = 0; area < FAX_MAP_AREAS; ++area) {
            const FaxMapArea *a = &atlas.area[area];
            for (unsigned i = 0; i < a->door_count; ++i)
                if (a->door[i].kind == FAX_MAP_DOOR_BUILDING && a->door[i].palette == FAX_MAP_TEMPLE &&
                    a->door[i].to == temple_interior[t]) entered(area, a->door[i].screen, temple_interior[t]);
        }
    }
    gurus_merged = gurus;
}

static bool settled(void) {
    return q(SCREEN_READY_STATE) == 0 && q(INTERRUPT_HANDLERS_ON) && q(SCREEN_SCROLL_DIRECTION) == 0xFF;
}

/* Where the hero is, once the screen has settled. */
static void observe(void) {
    unsigned area = q(AREA_CURRENT_AREA), screen = q(AREA_CURRENT_SCREEN);
    if (area == FAX_MAP_BUILDINGS) {
        entered(q(AREA_SAVED_AREA), q(AREA_SAVED_SCREEN), screen);
    } else if (valid_place(area, screen)) {
        see(area, screen);
        if (last.valid && last.area != FAX_MAP_BUILDINGS && (last.area != area || last.screen != screen) &&
            valid_place(last.area, last.screen))
            moved(last.area, last.screen, area, screen);
    } else {
        return;
    }
    last.area = (uint8_t)area; last.screen = (uint8_t)screen; last.valid = 1;
}

static bool decode(void) {
    if (decoded) return true;
    if (failed) return false;
    size_t need = cyc_mod_copy_cart_payload(NULL, 0);
    uint8_t *prg = need >= 16u * 0x4000u ? malloc(need) : NULL;
    bool ok = prg && cyc_mod_copy_cart_payload(prg, need) == need && fax_map_decode(&atlas, prg, need) &&
              (fax_text_loaded() || fax_text_load());
    free(prg);
    if (!ok) {
        FAX_MAP_LOG("[map] the cart's area tables are not the original's; the map is off\n");
        failed = true;
        return false;
    }
    decoded = true;
    return true;
}

/* ---- the pages ---- */

/* The place the hero is at: in a building, the screen he went in from. */
static bool hero_place(unsigned *area, unsigned *screen) {
    *area = q(AREA_CURRENT_AREA); *screen = q(AREA_CURRENT_SCREEN);
    if (*area == FAX_MAP_BUILDINGS) { *area = q(AREA_SAVED_AREA); *screen = q(AREA_SAVED_SCREEN); }
    return valid_place(*area, *screen);
}
static int hero_page(void) {
    unsigned area, screen;
    return hero_place(&area, &screen) ? fax_map_page_of(&atlas, area, screen) : -1;
}
static bool page_seen(int p) {
    const FaxMapPage *page = &atlas.page[p];
    for (unsigned i = 0; i < page->count; ++i)
        if (bit(v.seen[page->cell[i].area], page->cell[i].screen)) return true;
    return false;
}
static void turn(int dir) {
    for (int k = 1; k < FAX_MAP_PAGES; ++k) {
        int p = ((shown + dir * k) % FAX_MAP_PAGES + FAX_MAP_PAGES) % FAX_MAP_PAGES;
        if (page_seen(p)) { if (p != shown) { shown = p; ++turns; } return; }
    }
}
static unsigned pages_seen(void) {
    unsigned n = 0;
    for (int p = 0; p < FAX_MAP_PAGES; ++p) n += page_seen(p);
    return n;
}

/* ---- the hooks ---- */

static void steer(bool fall_through) {   /* the BEQ: on past it (Z clear), or taken (Z set) */
    CycModRegs regs;
    cyc_mod_regs(&regs);
    if (fall_through) { regs.a = BUTTON_START; regs.p = (uint8_t)(regs.p & ~P_Z); }
    else { regs.a = 0; regs.p = (uint8_t)(regs.p | P_Z); }
    cyc_mod_set_regs(&regs);
}
static void show(void) {
    want = 0;
    int p = hero_page();
    if (p < 0 || !page_seen(p)) { for (p = 0; p < FAX_MAP_PAGES && !page_seen(p); ++p) {} }
    if (p >= FAX_MAP_PAGES) return;
    shown = p;
    open = true; closing = false; age = 0;
    ++opens;
}

/* GameLoop_CheckPauseGame's BEQ: a press of Map pauses as Start does. */
static int check_hook(uint16_t addr) {
    (void)addr;
    if (!enabled || cyc_mod_isolated() || !want || !decoded) return 0;
    show();
    if (open) steer(true);
    return 0;
}

/* The pause loop's BEQ, once a frame while paused: the map's buttons. */
static int loop_hook(uint16_t addr) {
    (void)addr;
    if (!enabled || cyc_mod_isolated() || !decoded) return 0;
    if (!open) {
        if (want) { show(); if (open) steer(false); }   /* pressed during the game's own pause */
        return 0;
    }
    uint8_t changed = q(JOY1_CHANGED);
    bool close = closing || (changed & (BUTTON_B | BUTTON_START));
    closing = false;
    if (!close && (changed & BUTTON_LEFT)) turn(-1);
    else if (!close && (changed & BUTTON_RIGHT)) turn(1);
    if (close) { open = false; ++closes; }
    steer(close);
    return 0;
}

void fax_map_frame_end(void) {
    if (!enabled) return;
    ++frames;
    if (want && (frames >= want || q(TEXTBOX_CLOSED) == 0)) want = 0;
    if (!decode()) return;
    if (open) ++age;
    merge_gurus();
    if (settled() && !q(GAME_PAUSED_STATE)) observe();
}

bool fax_map_showing(void) { return enabled && open; }

static void press(void) {
    if (!enabled) return;
    ++presses;
    if (open) closing = true;
    else want = frames + OPEN_WINDOW;
}

/* ---- the save file: one field an area ---- */

static const char *const field[FAX_MAP_AREAS] = {
    "map.eolis", "map.trunk", "map.mist", "map.towns", NULL, "map.branch", "map.dartmoor", "map.zenis",
};

/* A set as hex digits, the first holding items 0-3 (item 0 its lowest bit). */
static char *put_set(char *out, const uint8_t *set, unsigned count) {
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < count; i += 4) *out++ = hex[(set[i / 8] >> (i % 8)) & 15];
    *out = 0;
    return out;
}
static const char *get_set(const char *in, uint8_t *set, unsigned count, unsigned bytes) {
    memset(set, 0, bytes);
    while (*in == ' ') ++in;
    for (unsigned i = 0; *in && *in != ' '; ++in, i += 4) {
        unsigned d = *in >= '0' && *in <= '9' ? (unsigned)(*in - '0') : *in >= 'a' && *in <= 'f' ? (unsigned)(*in - 'a' + 10) : 16;
        if (d > 15) return NULL;
        for (unsigned b = 0; b < 4; ++b) if (d >> b & 1 && i + b < count) set_bit(set, i + b);
    }
    return in;
}

/* "seen up down doors". False when the area has nothing to keep. */
static bool format_area(unsigned area, char out[FAX_SAVE_VALUE]) {
    if (!field[area]) return false;
    unsigned n = atlas.area[area].count, doors = atlas.area[area].door_count;
    bool any = false;
    for (unsigned i = 0; i < SET; ++i) any |= v.seen[area][i] != 0;
    for (unsigned i = 0; i < DOOR_SET; ++i) any |= v.doors[area][i] != 0;
    if (!any) return false;
    char *p = put_set(out, v.seen[area], n);
    *p++ = ' '; p = put_set(p, v.up[area], n);
    *p++ = ' '; p = put_set(p, v.down[area], n);
    *p++ = ' '; put_set(p, v.doors[area], doors);
    return true;
}
static bool parse_area(unsigned area, const char *text) {
    memset(v.seen[area], 0, SET); memset(v.up[area], 0, SET); memset(v.down[area], 0, SET);
    memset(v.doors[area], 0, DOOR_SET);
    if (!text) return true;
    unsigned n = atlas.area[area].count, doors = atlas.area[area].door_count;
    Visits got = { 0 };
    const char *p = get_set(text, got.seen[area], n, SET);
    if (p) p = get_set(p, got.up[area], n, SET);
    if (p) p = get_set(p, got.down[area], n, SET);
    if (p) p = get_set(p, got.doors[area], doors, DOOR_SET);
    if (!p) return false;   /* damaged: forget the area rather than guess */
    memcpy(v.seen[area], got.seen[area], SET); memcpy(v.up[area], got.up[area], SET);
    memcpy(v.down[area], got.down[area], SET); memcpy(v.doors[area], got.doors[area], DOOR_SET);
    return true;
}

static void forget(void) {
    memset(&v, 0, sizeof v);
    last.valid = 0;
    gurus_merged = 0;
}

static void on_save(FaxSaveEvent event) {
    if (!enabled || !decode()) return;
    if (event == FAX_SAVE_NEW_GAME) { forget(); return; }
    for (unsigned area = 0; area < FAX_MAP_AREAS; ++area) {
        if (!field[area]) continue;
        if (event == FAX_SAVE_CONTINUED) {
            if (!parse_area(area, fax_save_get(field[area])))
                FAX_MAP_LOG("[map] %s is damaged; that area's map starts over\n", field[area]);
        } else {
            char text[FAX_SAVE_VALUE];
            fax_save_set(field[area], format_area(area, text) ? text : NULL);
        }
    }
    if (event == FAX_SAVE_CONTINUED) { last.valid = 0; gurus_merged = 0; }
}

/* ---- drawing ---- */

/* NES colors */
enum {
    BLACK = 0x0F, ROOM = 0x02, ROOM_WALL = 0x22, TOWN = 0x08, TOWN_WALL = 0x28, HERE = 0x16,
    GURU = 0x30, SHOP = 0x2A, HOUSE = 0x26, DOOR = 0x38,
};
enum { MARK_GURU, MARK_SHOP, MARK_HOUSE, MARK_DOOR, MARKS };
static const uint8_t mark_color[MARKS] = { GURU, SHOP, HOUSE, DOOR };
static const char *const mark_name[MARKS] = { "Guru", "Shop", "House", "Door" };
/* 3 x 3 pixels, a row a byte's low 3 bits, the left pixel highest */
static const uint8_t mark_shape[MARKS][3] = {
    { 2, 7, 2 },   /* a cross */
    { 7, 7, 7 },   /* a block */
    { 2, 7, 5 },   /* a roof on two walls */
    { 6, 6, 6 },   /* a door */
};

/* Where the map sits in the native picture: the frame is the whole 256 x 240,
 * the page's name on row 2, its grid in 240 x 176 from y 32, the key on row 27. */
enum { NATIVE_W = 256, NATIVE_H = 240, GRID_X = 8, GRID_Y = 32, GRID_W = 240, GRID_H = 176, TITLE_Y = 16,
       KEY_Y = 216, BLINK = 16 };
static const uint8_t cell_sizes[][2] = { { 16, 13 }, { 14, 11 }, { 12, 10 }, { 10, 8 }, { 8, 7 }, { 6, 5 } };

typedef struct { int16_t x, y, w, h; uint8_t area, screen, seen; } Drawn;
static Drawn drawn[FAX_MAP_CELLS];
static unsigned drawn_count;
static int drawn_page = -1, drawn_cw, drawn_ch;
static uint32_t *canvas;
static size_t capacity;

static uint32_t nes(unsigned c) { return hw_palette_argb[c & 63] | 0xFF000000u; }
static void fill(uint32_t *px, int w, int h, int x, int y, int rw, int rh, uint32_t color) {
    for (int j = y < 0 ? 0 : y; j < y + rh && j < h; ++j)
        for (int i = x < 0 ? 0 : x; i < x + rw && i < w; ++i) px[(size_t)j * (size_t)w + (size_t)i] = color;
}
static void mark(uint32_t *px, int w, int h, int x, int y, unsigned kind, int scale) {
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            if (mark_shape[kind][r] >> (2 - c) & 1) fill(px, w, h, x + c * scale, y + r * scale, scale, scale, nes(mark_color[kind]));
}
static unsigned mark_of(const FaxMapDoor *d) {
    if (d->kind != FAX_MAP_DOOR_BUILDING) return MARK_DOOR;
    if (d->palette == FAX_MAP_TEMPLE) return MARK_GURU;
    if (d->palette == FAX_MAP_TOOL_SHOP || d->palette == FAX_MAP_KEY_SHOP) return MARK_SHOP;
    return MARK_HOUSE;
}
static const Drawn *drawn_at(unsigned area, unsigned screen) {
    for (unsigned i = 0; i < drawn_count; ++i)
        if (drawn[i].area == area && drawn[i].screen == screen) return &drawn[i];
    return NULL;
}

static void draw_page(uint32_t *px, int w, int h, int ox, int oy) {
    const FaxMapPage *page = &atlas.page[shown];
    unsigned k = 0;
    while (k + 1 < sizeof cell_sizes / sizeof cell_sizes[0] &&
           (page->w * cell_sizes[k][0] > GRID_W || page->h * cell_sizes[k][1] > GRID_H)) ++k;
    int cw = cell_sizes[k][0], ch = cell_sizes[k][1];
    int gx = ox + GRID_X + (GRID_W - page->w * cw) / 2, gy = oy + GRID_Y + (GRID_H - page->h * ch) / 2;
    unsigned here_area = 0, here_screen = 0;
    bool here = hero_place(&here_area, &here_screen) && (age / BLINK) % 2 == 0;
    drawn_count = 0; drawn_page = shown; drawn_cw = cw; drawn_ch = ch;
    /* The boxes: walls, then the inside. */
    for (unsigned i = 0; i < page->count; ++i) {
        const FaxMapCell *c = &page->cell[i];
        bool seen = bit(v.seen[c->area], c->screen);
        Drawn *d = &drawn[drawn_count++];
        *d = (Drawn){ (int16_t)(gx + c->x * cw), (int16_t)(gy + c->y * ch), (int16_t)(cw - 1), (int16_t)(ch - 1),
                      c->area, c->screen, seen };
        if (!seen) continue;
        bool town = c->area == FAX_MAP_TOWNS;
        bool blink = here && c->area == here_area && c->screen == here_screen;
        fill(px, w, h, d->x, d->y, d->w, d->h, nes(town ? TOWN_WALL : ROOM_WALL));
        fill(px, w, h, d->x + 1, d->y + 1, d->w - 2, d->h - 2, nes(blink ? HERE : town ? TOWN : ROOM));
    }
    /* The ways between them: beside, open; above and below, a ladder he has taken. */
    for (unsigned i = 0; i < drawn_count; ++i) {
        const Drawn *a = &drawn[i];
        if (!a->seen) continue;
        unsigned ta, ts;
        const Drawn *b;
        if (fax_map_joined(&atlas, a->area, a->screen, FAX_MAP_RIGHT, &ta, &ts) && (b = drawn_at(ta, ts)) &&
            b->seen && b->x == a->x + cw && b->y == a->y) {
            bool blink = here && a->area == here_area && a->screen == here_screen;
            bool blink_b = here && b->area == here_area && b->screen == here_screen;
            fill(px, w, h, a->x + a->w - 1, a->y + 1, 2, a->h - 2,
                 nes(blink ? HERE : a->area == FAX_MAP_TOWNS ? TOWN : ROOM));
            fill(px, w, h, b->x, b->y + 1, 1, b->h - 2, nes(blink_b ? HERE : b->area == FAX_MAP_TOWNS ? TOWN : ROOM));
        }
        if (fax_map_joined(&atlas, a->area, a->screen, FAX_MAP_DOWN, &ta, &ts) && (b = drawn_at(ta, ts)) &&
            b->seen && b->x == a->x && b->y == a->y + ch &&
            (bit(v.down[a->area], a->screen) || bit(v.up[b->area], b->screen))) {
            int gap = cw >= 12 ? 4 : 2, x = a->x + (a->w - gap) / 2;
            bool blink = here && a->area == here_area && a->screen == here_screen;
            fill(px, w, h, x, a->y + a->h - 1, gap, 2, nes(blink ? HERE : ROOM));
            bool blink_b = here && b->area == here_area && b->screen == here_screen;
            fill(px, w, h, x, b->y, gap, 1, nes(blink_b ? HERE : ROOM));
        }
    }
    /* The doors used, at their place on their screen. */
    for (unsigned i = 0; i < drawn_count; ++i) {
        const Drawn *c = &drawn[i];
        if (!c->seen || c->w < 6 || c->h < 5) continue;
        const FaxMapArea *a = &atlas.area[c->area];
        for (unsigned j = 0; j < a->door_count; ++j) {
            const FaxMapDoor *d = &a->door[j];
            if (d->screen != c->screen || !bit(v.doors[c->area], j)) continue;
            int bx = d->block & 15, by = d->block >> 4;   /* 16 x 13 blocks */
            int x = c->x + 1 + bx * (c->w - 5) / 15, y = c->y + 1 + (by > 12 ? 12 : by) * (c->h - 5) / 12;
            mark(px, w, h, x, y, mark_of(d), 1);
        }
    }
}

static void draw(uint32_t *px, int w, int h) {
    fill(px, w, h, 0, 0, w, h, nes(BLACK));
    int ox = (w - NATIVE_W) / 2, oy = (h - NATIVE_H) / 2;
    if (oy < 0) oy = 0;
    fax_text_box(px, w, h, ox, oy, NATIVE_W / FAX_TEXT_CELL - 2, NATIVE_H / FAX_TEXT_CELL - 2, 255);
    const char *name = atlas.page[shown].name;
    fax_text_draw(px, w, h, ox + (NATIVE_W - fax_text_width(name)) / 2, oy + TITLE_Y, name, 255);
    if (pages_seen() > 1) {   /* the font's triangles: ~ points left, } right */
        fax_text_draw(px, w, h, ox + 2 * FAX_TEXT_CELL, oy + TITLE_Y, "~", 255);
        fax_text_draw(px, w, h, ox + NATIVE_W - 3 * FAX_TEXT_CELL, oy + TITLE_Y, "}", 255);
    }
    draw_page(px, w, h, ox, oy);
    /* The key: each mark at twice its size, then its name. */
    int width = 0;
    for (unsigned k = 0; k < MARKS; ++k) width += 6 + 3 + fax_text_width(mark_name[k]) + (k + 1 < MARKS ? 12 : 0);
    int x = ox + (NATIVE_W - width) / 2;
    for (unsigned k = 0; k < MARKS; ++k) {
        mark(px, w, h, x, oy + KEY_Y + 1, k, 2);
        x += 9;
        fax_text_draw(px, w, h, x, oy + KEY_Y, mark_name[k], 255);
        x += fax_text_width(mark_name[k]) + 12;
    }
}

const uint32_t *fax_map_present(const uint32_t *picture, int *w, int *h) {
    if (!enabled || !open || !decoded || shown < 0) return picture;
    int width = 0, height = 0;
    const uint32_t *from = picture;
    if (from) width = *w, height = *h;
    else from = cyc_render_present(&width, &height);
    if (!from || width <= 0 || height <= 0) return picture;
    size_t n = (size_t)width * (size_t)height;
    if (n > capacity) {
        uint32_t *grown = realloc(canvas, n * sizeof *grown);
        if (!grown) return picture;
        canvas = grown; capacity = n;
    }
    draw(canvas, width, height);
    *w = width; *h = height;
    return canvas;
}

/* ---- save states: everything above ---- */

enum { VERSION = 1, RECORD = 1 + sizeof(Visits) + 3 + 1 + 4 + 2 };
static int state_get(uint8_t *buf, int cap) {
    if (cap < (int)RECORD) return -1;
    uint8_t *p = buf;
    *p++ = VERSION;
    memcpy(p, &v, sizeof v); p += sizeof v;
    *p++ = last.area; *p++ = last.screen; *p++ = last.valid;
    *p++ = gurus_merged;
    *p++ = open; *p++ = (uint8_t)shown; *p++ = (uint8_t)(age & 0xFF); *p++ = (uint8_t)(want > frames ? want - frames : 0);
    *p++ = 0; *p++ = 0;
    return (int)(p - buf);
}
static int state_validate(const uint8_t *buf, int len) {
    if (len == 0) return 1;
    if (len != (int)RECORD || buf[0] != VERSION) return 0;
    const uint8_t *p = buf + 1 + sizeof(Visits);
    return p[0] < FAX_MAP_AREAS && p[2] <= 1 && p[4] <= 1 && (p[5] == 0xFF || p[5] < FAX_MAP_PAGES) && (!p[4] || p[5] != 0xFF);
}
static int state_set(const uint8_t *buf, int len) {
    if (!state_validate(buf, len)) return 0;
    if (!len) { forget(); open = closing = false; shown = -1; want = 0; return 1; }
    const uint8_t *p = buf + 1;
    memcpy(&v, p, sizeof v); p += sizeof v;
    last.area = p[0]; last.screen = p[1]; last.valid = p[2];
    gurus_merged = p[3];
    open = p[4]; shown = p[5] == 0xFF ? -1 : p[5]; age = p[6]; want = p[7] ? frames + p[7] : 0;
    closing = false;
    return 1;
}

/* ---- the bindings (config.ini [Game]) ---- */

const char *fax_map_controls_label(void) { return label; }

#ifdef CYC_WITH_SDL
static int key = DEFAULT_KEY, pad = DEFAULT_PAD;   /* 0: unbound */
static bool axis_down;

static void describe(void) {   /* as the host shows its own shortcuts: "M / LT" */
    char hint[64] = "unbound";
    CycBinding both = { key, pad };
    if (key || pad) cyc_binding_hint(&both, hint, sizeof hint);
    snprintf(label, sizeof label, "Map: %s", hint);
}

bool fax_map_load_setting(const char *name, const char *value) {
    int got;
    if (!strcmp(name, "MapKey")) {
        if (cyc_key_parse(value, &got)) key = got;
        else fprintf(stderr, "[map] MapKey: SDL has no key named \"%s\"; keeping the default\n", value);
    } else if (!strcmp(name, "MapButton")) {
        if (cyc_pad_parse(value, &got) && !CYC_PAD_IS_COMBO(got)) pad = got;
        else fprintf(stderr, "[map] MapButton: \"%s\" is not one controller button or axis; keeping the default\n", value);
    } else {
        return false;
    }
    describe();
    return true;
}
void fax_map_save_settings(FILE *f) {
    char k[32], p[48];
    cyc_key_text(key, k, sizeof k);
    cyc_pad_text(pad, p, sizeof p);
    fprintf(f, "MapKey = %s\nMapButton = %s\n", k, p);
}

void fax_map_event(const void *ev) {
    const SDL_Event *e = ev;
    if (e->type == SDL_KEYDOWN) {
        if (key && !e->key.repeat && e->key.keysym.scancode == key) press();
    } else if (e->type == SDL_CONTROLLERBUTTONDOWN) {
        if (CYC_PAD_IS_BUTTON(pad) && e->cbutton.button == pad - 1) press();
    } else if (e->type == SDL_CONTROLLERAXISMOTION && CYC_PAD_IS_AXIS(pad) && e->caxis.axis == (pad - 100) / 2) {
        int value = (pad - 100) & 1 ? e->caxis.value : -e->caxis.value;
        bool down = value >= AXIS_PRESS;
        if (down && !axis_down) press();
        axis_down = down;
    }
}

static void tcp_set(char *out, size_t cap, const uint8_t *set, unsigned count) {
    char text[FAX_SAVE_VALUE];
    put_set(text, set, count);
    snprintf(out, cap, "\"%s\"", text);
}
static void map_state(int id, const char *line) {
    (void)line;
    static char fields[16384];
    size_t n = 0;
    unsigned area = 0, screen = 0;
    bool placed = decoded && hero_place(&area, &screen);
    n += (size_t)snprintf(fields + n, sizeof fields - n,
                          "\"enabled\":%s,\"decoded\":%s,\"open\":%s,\"page\":%d,\"page_name\":%s%s%s,"
                          "\"here\":[%d,%d],\"here_page\":%d,\"pages_seen\":%u,\"opens\":%u,\"closes\":%u,"
                          "\"turns\":%u,\"presses\":%u,\"binding\":\"%s\",\"paused\":%u",
                          enabled ? "true" : "false", decoded ? "true" : "false", open ? "true" : "false", shown,
                          shown >= 0 ? "\"" : "", shown >= 0 && decoded ? atlas.page[shown].name : "null",
                          shown >= 0 ? "\"" : "", placed ? (int)area : -1, placed ? (int)screen : -1,
                          placed ? fax_map_page_of(&atlas, area, screen) : -1, decoded ? pages_seen() : 0u, opens,
                          closes, turns, presses, label + 5, q(GAME_PAUSED_STATE));
    if (decoded) {
        const char *names[] = { "seen", "up", "down", "doors" };
        for (unsigned k = 0; k < 4; ++k) {
            n += (size_t)snprintf(fields + n, sizeof fields - n, ",\"%s\":[", names[k]);
            for (unsigned a = 0; a < FAX_MAP_AREAS; ++a) {
                char text[FAX_SAVE_VALUE + 4];
                const uint8_t *set = k == 0 ? v.seen[a] : k == 1 ? v.up[a] : k == 2 ? v.down[a] : v.doors[a];
                tcp_set(text, sizeof text, set, k == 3 ? atlas.area[a].door_count : atlas.area[a].count);
                n += (size_t)snprintf(fields + n, sizeof fields - n, "%s%s", a ? "," : "", text);
            }
            n += (size_t)snprintf(fields + n, sizeof fields - n, "]");
        }
        n += (size_t)snprintf(fields + n, sizeof fields - n, ",\"drawn_page\":%d,\"cell\":[%d,%d],\"boxes\":[",
                              drawn_page, drawn_cw, drawn_ch);
        for (unsigned i = 0; i < drawn_count && n < sizeof fields - 64; ++i)
            n += (size_t)snprintf(fields + n, sizeof fields - n, "%s[%u,%u,%d,%d,%d,%d,%u]", i ? "," : "",
                                  drawn[i].area, drawn[i].screen, drawn[i].x, drawn[i].y, drawn[i].w, drawn[i].h,
                                  drawn[i].seen);
        n += (size_t)snprintf(fields + n, sizeof fields - n, "],\"layout\":[");
        for (int p = 0; p < FAX_MAP_PAGES && n < sizeof fields - 64; ++p)
            n += (size_t)snprintf(fields + n, sizeof fields - n, "%s[\"%s\",%u,%u,%u]", p ? "," : "",
                                  atlas.page[p].name, atlas.page[p].w, atlas.page[p].h, atlas.page[p].count);
        n += (size_t)snprintf(fields + n, sizeof fields - n, "]");
    }
    cyc_tcp_ok(id, fields);
}
void fax_map_tcp_setup(void) {
    cyc_tcp_register("map_state",
                     "Map: open, the page shown, the screens seen, ladders and doors used, and the boxes drawn", map_state);
}
#else
bool fax_map_load_setting(const char *name, const char *value) { (void)name; (void)value; return false; }
void fax_map_save_settings(FILE *f) { (void)f; }
void fax_map_event(const void *ev) { (void)ev; }
void fax_map_tcp_setup(void) {}
#endif

static const char *const hook_ids[] = { "faxanadu.map.check", "faxanadu.map.loop" };
static void hooks(int on) {
    for (unsigned i = 0; i < sizeof hook_ids / sizeof hook_ids[0]; ++i) nes_mod_set_function_hook_enabled(hook_ids[i], on);
}
static void deactivate(void) {
    enabled = false;
    open = closing = false;
    shown = -1; want = 0;
    opens = closes = turns = presses = age = 0;
    forget();
    hooks(0);
}
static void activate(void) {
    enabled = true;
    hooks(1);
    nes_mod_set_local_only("Map", 1);   /* it pauses the game: host input a peer would not see */
}

NES_MOD_CONSTRUCTOR(register_map) {
    if (!nes_mod_register_function_entry_plugin(hook_ids[0], PAUSE_CHECK, check_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[1], PAUSE_LOOP, loop_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.map", activate) ||
        !nes_mod_register_savestate_hook("faxanadu.map", state_get, state_set) ||
        !nes_mod_register_savestate_validator("faxanadu.map", state_validate) ||
        !fax_save_listen(on_save)) abort();
}
