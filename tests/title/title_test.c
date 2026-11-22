/* Unit tests for REMASTERED on the title (fax_title.c, with fax_text.c),
 * without the emulator or a ROM. The feature runs against a model of the
 * machine: a cart image holding glyphs, a palette, the title's picture as the
 * game draws it (the logo on rows 16-143, START and CONTINUE from row 160),
 * and the title's loop, which calls the hook once a frame. */
#include <stdio.h>
static char logged[128];
#define FAX_TITLE_LOG(...) snprintf(logged, sizeof logged, __VA_ARGS__)
#include "fax_text.c"
#include "fax_title.c"

static unsigned checks;
#define CHECK(test) do{++checks;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(1);}}while(0)

/* ---- the machine ---- */
enum { BLACK = 0xFF000000u, LOGO = 0xFF845024u, TEXT = 0xFFFFFFFFu };
static uint8_t ram[0x800];
static uint8_t cart[16 * 16384];
static uint32_t native[256 * 240];
static int presents;

uint8_t cyc_mod_peek(uint16_t addr) { return addr < 0x2000 ? ram[addr & 0x7FF] : 0; }
size_t cyc_mod_copy_cart_payload(uint8_t *out, size_t capacity) {
    if (out && capacity >= sizeof cart) memcpy(out, cart, sizeof cart);
    return sizeof cart;
}
uint16_t cyc_render_line_bg_table(int y) { (void)y; return 0x1000; }
uint8_t cyc_render_chr(uint16_t a) { (void)a; return 0; }
/* fax_text.c's nametable reads, which the title does not use. */
int cyc_render_line_scroll_x(int y) { (void)y; return 0; }
int cyc_render_line_scroll_y(int y) { return y; }
uint8_t cyc_render_nametable(uint16_t a) { (void)a; return 0; }
/* Palette entry e as a color whose blue byte is e, so a pixel tells its entry. */
uint32_t cyc_render_color(int e) { return 0xFF000000u | 0x00400000u | (uint32_t)(e & 31); }
const uint32_t *cyc_render_present(int *w, int *h) { ++presents; *w = 256; *h = 240; return native; }
static int origin = -1;   /* what a compositor declared, or -1: centered */
int cyc_render_native_origin(int width) { return origin >= 0 ? origin : width <= 256 ? 0 : (width - 256) / 2; }

/* Registration. */
static NESModFunctionEntryCallback hook;
static uint16_t hook_addr;
static int hook_on = -1;
int nes_mod_register_function_entry_plugin(const char *id, uint16_t addr, NESModFunctionEntryCallback cb) {
    if (strcmp(id, "faxanadu.title-remastered.loop")) return 0;
    hook = cb; hook_addr = addr;
    return 1;
}
int nes_mod_set_function_hook_enabled(const char *id, int on) {
    if (strcmp(id, "faxanadu.title-remastered.loop")) return 0;
    hook_on = on;
    return 1;
}
int nes_mod_register_reset_callback(NESModActivationCallback cb) { return cb == deactivate; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    return !strcmp(id, "faxanadu.title-remastered") && cb == activate;
}
int nes_mod_register_savestate_hook(const char *id, NESModSavestateGet get, NESModSavestateSet set) {
    return !strcmp(id, "faxanadu.title-remastered") && get == state_get && set == state_set;
}
int nes_mod_register_savestate_validator(const char *id, NESModSavestateValidate v) {
    return !strcmp(id, "faxanadu.title-remastered") && v == state_validate;
}

/* The font: each glyph a pattern on rows 0-6, as the game's capitals are. */
static void font(void) {
    memset(cart, 0xFF, sizeof cart);
    uint8_t *glyph = cart + 13 * 16384;
    for (int c = 0x20; c < 0x80; ++c)
        for (int r = 0; r < 8; ++r) glyph[(c - 0x20) * 8 + r] = c == ' ' || r == 7 ? 0 : (uint8_t)(c ^ (r * 0x11));
    fax_text_unload();
}
/* The title as StartScreen_Draw leaves it, in a picture `w` x `h` with the
 * native one at column x0: the logo and border on rows 16-143, START and
 * CONTINUE on 160-166, the notices on 184-222, black between. */
static void title_picture(uint32_t *px, int w, int h, int x0) {
    for (int i = 0; i < w * h; ++i) px[i] = BLACK;
    for (int y = 16; y < 144; ++y) for (int x = 16; x < 240; ++x) px[y * w + x0 + x] = LOGO;
    for (int y = 160; y < 167; ++y) for (int x = 52; x < 191; ++x) px[y * w + x0 + x] = TEXT;
    for (int y = 184; y < 223; ++y) for (int x = 32; x < 231; ++x) px[y * w + x0 + x] = TEXT;
}

/* One machine frame: the title's loop runs (or not), then the frame ends. */
static void frame(bool loop) {
    if (loop && hook_on == 1) CHECK(hook(TITLE_LOOP) == 0);   /* it only observes */
    fax_title_frame_end();
}
static void power_on(void) { deactivate(); fax_title_power_on(); activate(); }

/* ---- the tests ---- */

static void registration(void) {
    CHECK(hook && hook_addr == 0xFC7A);   /* @_afterCheckHandleInputFarJump */
    deactivate();
    CHECK(hook_on == 0);
    activate();
    CHECK(hook_on == 1);
}

/* The word: REMASTERED in the font, centered under the logo, in palette entry
 * 3, and nothing else of the picture changed. */
static void the_word(void) {
    font(); power_on(); title_picture(native, 256, 240, 0);
    frame(true);
    int w = 0, h = 0;
    presents = 0;
    const uint32_t *out = fax_title_present(NULL, &w, &h);
    CHECK(out && out != native && presents == 1 && w == 256 && h == 240);
    int x0 = 88, y0 = 148;   /* 80 wide, centered on the logo (16-239); between rows 143 and 160 */
    CHECK(fax_text_width(WORD) == 80 && !strcmp(WORD, "REMASTERED"));
    int top = 240, bottom = -1, left = 256, right = -1;
    for (int y = 0; y < 240; ++y)
        for (int x = 0; x < 256; ++x) {
            uint32_t was = native[y * 256 + x], is = out[y * 256 + x];
            bool inside = x >= x0 && x < x0 + 80 && y >= y0 && y < y0 + 8;
            if (!inside) { CHECK(is == was); continue; }
            uint8_t bits = cart[13 * 16384 + (WORD[(x - x0) / 8] - 0x20) * 8 + (y - y0)];
            bool on = bits >> (7 - (x - x0) % 8) & 1;
            CHECK(was == BLACK);   /* only rows the title leaves black */
            CHECK(is == (on ? cyc_render_color(3) : BLACK));
            if (on) {
                if (y < top) top = y;
                if (y > bottom) bottom = y;
                if (x < left) left = x;
                if (x > right) right = x;
            }
        }
    /* Clear of the logo above and of the title's choices (START and CONTINUE,
     * NEW GAME and CONTINUE, or a file pick, all from y 160) below, by 4 rows
     * or more. */
    CHECK(top >= 148 && bottom <= 155 && top - 143 > 4 && 160 - bottom > 4);
    CHECK(left >= 88 && right <= 167 && (left - 16) - (239 - right) <= 8 && (239 - right) - (left - 16) <= 8);
    CHECK(fax_title_present(native, &w, &h) != native);   /* a composed picture is drawn over too */
}

/* Only while the title's loop runs: the frame it ran, and no other. */
static void only_on_the_title(void) {
    font(); power_on(); title_picture(native, 256, 240, 0);
    int w = 256, h = 240;
    CHECK(fax_title_present(native, &w, &h) == native);   /* power-on: not yet */
    frame(false);
    CHECK(fax_title_present(native, &w, &h) == native);
    frame(true);
    CHECK(fax_title_present(native, &w, &h) != native);
    CHECK(fax_title_present(native, &w, &h) != native);   /* every present of that frame */
    frame(true);
    CHECK(fax_title_present(native, &w, &h) != native);
    frame(false);                                         /* Start: the intro, or a saved game */
    CHECK(fax_title_present(native, &w, &h) == native);
    for (int i = 0; i < 300; ++i) frame(false);
    CHECK(fax_title_present(native, &w, &h) == native && seen == 255);
    /* Off: the hook is off, and nothing shows even if it were called. */
    frame(true);
    deactivate();
    CHECK(hook_on == 0 && fax_title_present(native, &w, &h) == native);
    CHECK(hook(TITLE_LOOP) == 0);
    fax_title_frame_end();
    CHECK(fax_title_present(native, &w, &h) == native);
    activate();
}

/* Wider pictures (Seamless, the wide modes): under the logo wherever the
 * native picture sits; a picture smaller than the native one is left alone. */
static void wider_pictures(void) {
    font(); power_on();
    static uint32_t wide[512 * 320];
    title_picture(wide, 512, 320, 128);
    frame(true);
    int w = 512, h = 320;
    const uint32_t *out = fax_title_present(wide, &w, &h);
    CHECK(out != wide && w == 512 && h == 320);
    int changed = 0, minx = 512;
    for (int i = 0; i < 512 * 320; ++i)
        if (out[i] != wide[i]) { ++changed; if (i % 512 < minx) minx = i % 512; CHECK(i / 512 >= 148 && i / 512 < 156); }
    CHECK(changed > 0 && minx >= 128 + 88 && minx < 128 + 96);
    origin = 40;   /* a compositor that put the native picture elsewhere */
    title_picture(wide, 512, 320, 40);
    out = fax_title_present(wide, &w, &h);
    minx = 512;
    for (int i = 0; i < 512 * 320; ++i) if (out[i] != wide[i] && i % 512 < minx) minx = i % 512;
    CHECK(minx >= 40 + 88 && minx < 40 + 96);
    origin = -1;
    w = 200; h = 188;   /* scaled down into a small window */
    CHECK(fax_title_present(wide, &w, &h) == wide && w == 200 && h == 188);
}

static void save_states(void) {
    font(); power_on();
    uint8_t rec[8];
    CHECK(state_get(rec, 1) == -1);
    frame(true);
    int n = state_get(rec, sizeof rec);
    CHECK(n == 2 && rec[0] == 1 && rec[1] == 1 && state_validate(rec, n));
    for (int i = 0; i < 5; ++i) frame(false);
    int w = 256, h = 240;
    title_picture(native, 256, 240, 0);
    CHECK(fax_title_present(native, &w, &h) == native);
    CHECK(state_set(rec, n) == 1);   /* a state saved on the title: the word, before a frame runs */
    CHECK(fax_title_present(native, &w, &h) != native);
    CHECK(state_set(rec, 0) == 1);   /* saved without the feature: wait for the loop */
    CHECK(fax_title_present(native, &w, &h) == native);
    rec[0] = 2;
    CHECK(!state_validate(rec, n) && state_set(rec, n) == 0);
    CHECK(!state_validate(rec, 3));
}

static void without_the_font(void) {
    memset(cart, 0, sizeof cart);   /* no glyphs: no letters */
    fax_text_unload();
    power_on();
    logged[0] = 0;
    frame(true);
    int w = 256, h = 240;
    CHECK(fax_title_present(native, &w, &h) == native && failed && strstr(logged, "font"));
    frame(true);
    CHECK(fax_title_present(native, &w, &h) == native);
}

int main(void) {
    registration();
    the_word();
    only_on_the_title();
    wider_pictures();
    save_states();
    without_the_font();
    printf("title_test: %u checks passed\n", checks);
    return 0;
}
