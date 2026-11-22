/* DX on the title. USA 57dd23d1, chipx86 byte-exact disassembly.
 *
 * The word sits under the FAXANADU logo, centered on it, in the title's own
 * letters and the color the logo's palette draws its highlights in. The
 * letters come from the game's dialogue font (fax_text.c): its A to Z are the
 * title's letter tiles (CHR_LETTERS, bank 10 $8240, one color), pixel for
 * pixel. The title's picture is left as the game drew it: the word
 * goes over the presented picture, in rows the title leaves black (y 148-154,
 * between the logo's last row, 143, and the title's choices, from 160: START
 * and CONTINUE, NEW GAME and CONTINUE with the remaster, or a file pick).
 *
 * The title is up while Game_ShowStartScreen's loop runs: once a frame it
 * far-calls StartScreen_CheckHandleInput (which Save files steers), then
 * reads the pad at @_afterCheckHandleInputFarJump ($FC7A). A hook there only
 * notes that the loop ran; the word shows on the frames it did. Start leaves
 * the loop for the intro or a saved game (the original's mantra screen),
 * and the word goes with it.
 * No ROM, nothing written to the machine.
 * See docs/GAME_INTERNALS.md, "The title screen". */
#include "fax_title.h"
#include "fax_text.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#ifdef CYC_WITH_SDL
#include "cyc_tcp.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef FAX_TITLE_LOG
#define FAX_TITLE_LOG(...) fprintf(stderr, __VA_ARGS__)
#endif

/* Fixed bank */
#define TITLE_LOOP 0xFC7Au   /* Game_ShowStartScreen's @_afterCheckHandleInputFarJump: LDA Joy1_ChangedButtonMask */

static const char WORD[] = "DX";
enum {
    NATIVE_W = 256, NATIVE_H = 240,
    LOGO_LEFT = 16, LOGO_W = 224,   /* the logo and its border: tiles 2-29 of rows 2-17 (StartScreen_Draw, $2042) */
    WORD_Y = 148,                   /* the logo's last row is 143; the title's choices start at 160 */
    WORD_COLOR = 3,                 /* palette RAM: the logo's palette ($0F $05 $18 $38), its light yellow */
};

static bool enabled, failed;
static uint8_t seen = 255;          /* frames since the title's loop last ran */
static unsigned shown_frames;       /* presents with the word, since power-on */
static int drawn_x = -1, drawn_y = -1, drawn_w;   /* where the last present put it, and the picture's width */
static uint32_t *canvas;
static size_t capacity;

/* The word's top-left in a presented picture `width` wide: centered under the
 * logo, wherever the native picture sits in it. */
static void word_at(int width, int *x, int *y) {
    *x = cyc_render_native_origin(width) + LOGO_LEFT + (LOGO_W - fax_text_width(WORD)) / 2;
    *y = WORD_Y;
}
static bool title_up(void) { return enabled && !failed && seen <= 1; }

/* Game_ShowStartScreen's loop, once a frame while the title is up. */
static int loop_hook(uint16_t addr) {
    (void)addr;
    if (enabled) seen = 0;
    return 0;
}

void fax_title_power_on(void) { seen = 255; }
void fax_title_frame_end(void) {
    if (seen < 255) ++seen;
}

const uint32_t *fax_title_present(const uint32_t *picture, int *w, int *h) {
    if (!title_up()) return picture;
    if (!fax_text_loaded() && !fax_text_load()) {
        FAX_TITLE_LOG("[title] cannot read the game's font; DX is off\n");
        failed = true;
        return picture;
    }
    int width = 0, height = 0;
    const uint32_t *from = picture;
    if (from) width = *w, height = *h;
    else from = cyc_render_present(&width, &height);
    /* A picture smaller than the native one has the title scaled into it. */
    if (!from || width < NATIVE_W || height < NATIVE_H) return picture;
    size_t n = (size_t)width * (size_t)height;
    if (n > capacity) {
        uint32_t *grown = realloc(canvas, n * sizeof *grown);
        if (!grown) return picture;
        canvas = grown; capacity = n;
    }
    memcpy(canvas, from, n * sizeof *canvas);
    int x, y;
    word_at(width, &x, &y);
    fax_text_draw_color(canvas, width, height, x, y, WORD, cyc_render_color(WORD_COLOR), 255);
    ++shown_frames;
    drawn_x = x; drawn_y = y; drawn_w = width;
    *w = width; *h = height;
    return canvas;
}

/* Save states carry whether the title is up, so a state loaded while paused
 * shows the word exactly when its picture is the title. */
enum { RECORD = 2, VERSION = 1 };
static int state_get(uint8_t *buf, int cap) {
    if (cap < RECORD) return -1;
    buf[0] = VERSION;
    buf[1] = seen;
    return RECORD;
}
static int state_validate(const uint8_t *buf, int len) {
    return len == 0 || (len == RECORD && buf[0] == VERSION);
}
static int state_set(const uint8_t *buf, int len) {
    if (!state_validate(buf, len)) return 0;
    seen = len ? buf[1] : 255;   /* saved without the feature: wait for the loop */
    return 1;
}

#ifdef CYC_WITH_SDL
static void title_state(int id, const char *line) {
    (void)line;
    char fields[256];
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"font\":%s,\"shown\":%s,\"seen\":%u,\"word\":\"%s\",\"x\":%d,\"y\":%d,"
             "\"width\":%d,\"color\":%d,\"argb\":%lu,\"presents\":%u",
             enabled ? "true" : "false", fax_text_loaded() ? "true" : "false", title_up() ? "true" : "false", seen,
             WORD, drawn_x, drawn_y, drawn_w, WORD_COLOR, (unsigned long)cyc_render_color(WORD_COLOR), shown_frames);
    cyc_tcp_ok(id, fields);
}
void fax_title_tcp_setup(void) {
    cyc_tcp_register("title_state", "DX on the title: whether it shows, and where", title_state);
}
#else
void fax_title_tcp_setup(void) {}
#endif

static void deactivate(void) {
    enabled = failed = false;
    seen = 255;
    shown_frames = 0;
    drawn_x = drawn_y = -1; drawn_w = 0;
    nes_mod_set_function_hook_enabled("faxanadu.title-remastered.loop", 0);
}
static void activate(void) {
    enabled = true;
    nes_mod_set_function_hook_enabled("faxanadu.title-remastered.loop", 1);
}

/* The plugin, hook site and record keep their names from before the project
 * was Faxanadu DX (#116): save states know the record by its name. */
NES_MOD_CONSTRUCTOR(register_title) {
    if (!nes_mod_register_function_entry_plugin("faxanadu.title-remastered.loop", TITLE_LOOP, loop_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.title-remastered", activate) ||
        !nes_mod_register_savestate_hook("faxanadu.title-remastered", state_get, state_set) ||
        !nes_mod_register_savestate_validator("faxanadu.title-remastered", state_validate)) abort();
}
