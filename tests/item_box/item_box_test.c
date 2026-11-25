/* Unit tests for the item box lit while its item runs (fax_item_box.c),
 * without the emulator or a ROM. It runs against a model of the machine: CPU
 * RAM (SelectedItem, Player_StatusFlag, DurationHourGlass, the status bar's
 * palette), palette RAM and the PPU mask each of the band's lines was drawn
 * with, the tiles the last frame showed in the status bar's rows, the host's
 * NES colors, and Seamless's layout. Each check is on the pixels a player
 * would see. */
#include <stdio.h>
#include "fax_item_box.c"

static unsigned checks;
#define CHECK(test) do{++checks;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(1);}}while(0)

/* ---- the machine ---- */
enum { HUD_X = 92 };                        /* Seamless's status bar band at 424 wide, with the Elixir left of it */
enum { W = 424, H = 240 };
enum { HOUR_GLASS = 0x0D, WING_BOOTS = 0x0F, MATTOCK = 0x09, NO_ITEM = 0xFF };
static uint8_t ram[0x800];
static uint8_t palette_ram[32];
static uint8_t masks[240];                  /* PPUMASK each line was drawn with */
static unsigned tiles[30][32];              /* the background tiles the last frame showed */
static bool hud_up = true;
static int world_x = HUD_X;
static uint32_t native[256 * 240];
static int presents;
uint32_t hw_palette_argb[512];

bool fax_text_hud_showing(void) { return hud_up; }
unsigned fax_text_shown_tile(int col, int row) {
    return col >= 0 && col < 32 && row >= 0 && row < 30 ? tiles[row][col] : FAX_TEXT_NO_TILE;
}
int fax_world_hud_x(int width) { return width <= 256 ? 0 : world_x; }
uint8_t cyc_mod_peek(uint16_t addr) { return addr < 0x2000 ? ram[addr & 0x7FF] : 0; }
uint8_t cyc_render_line_mask(int y) { CHECK(y >= 0 && y < 32); return masks[y]; }   /* a line of the band */
uint8_t cyc_render_palette(int e) { return palette_ram[e & 31]; }
const uint32_t *cyc_render_present(int *w, int *h) { ++presents; *w = 256; *h = 240; return native; }

static NESModActivationCallback reset_cb, plugin_cb;
int nes_mod_register_reset_callback(NESModActivationCallback cb) { reset_cb = cb; return 1; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    CHECK(!strcmp(id, "faxanadu.item-box"));
    plugin_cb = cb;
    return 1;
}

/* A color as the PPU draws it on line y: the line's greyscale and emphasis. Every
 * index of the host's colors is told apart here. */
static uint32_t drawn(uint8_t nes, int y) {
    unsigned m = masks[y];
    return hw_palette_argb[((nes & (m & 1 ? 0x30 : 0x3F)) | (m >> 5) << 6) & 511] | 0xFF000000u;
}

/* The status bar as UI_DrawHUDSprites leaves it: the box's [ ] tiles in
 * columns 27 and 30 of rows 2 and 3, and "P:" (fax_text_hud_showing's own
 * check is the model's hud_up). The palette: Eolis's, sub-palette 3 (black,
 * pink, green, white), the others different so the right one is read. */
static void power_on(bool on) {
    reset_cb();
    memset(ram, 0, sizeof ram);
    ram[0x038D] = 3;                        /* UI_AttributeDataIndex */
    ram[0x03C1] = NO_ITEM;                  /* SelectedItem */
    ram[0x042A] = 0xFF;                     /* DurationHourGlass: not running */
    static const uint8_t hud[4] = { 0x0F, 0x15, 0x19, 0x30 };
    for (int p = 0; p < 4; ++p) {
        palette_ram[p * 4] = 0x0F;
        palette_ram[p * 4 + 1] = (uint8_t)(0x01 + p);
        palette_ram[p * 4 + 2] = (uint8_t)(0x11 + p);
        palette_ram[p * 4 + 3] = (uint8_t)(0x21 + p);
    }
    memcpy(palette_ram + 12, hud, 4);
    memset(masks, 0x1E, sizeof masks);      /* background and sprites on, no emphasis */
    memset(tiles, 0, sizeof tiles);
    tiles[2][27] = 0x2C; tiles[3][27] = 0x2D; tiles[2][30] = 0x2E; tiles[3][30] = 0x2F;
    hud_up = true; world_x = HUD_X;
    if (on) plugin_cb();
}

/* Seamless's picture: every pixel its own value, so any pixel the feature
 * touched shows, and the ones it left are what Seamless drew. */
static uint32_t seamless[W * H];
static uint32_t was(int x, int y) { return 0xFE000000u | (uint32_t)(y * W + x); }
static const uint32_t *present(int *w, int *h) {
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) seamless[y * W + x] = was(x, y);
    *w = W; *h = H;
    return fax_item_box_present(seamless, w, h);
}
static const uint32_t *present_now(void) { int w, h; return present(&w, &h); }

/* The box lit at band x `x0` of a `w`-wide picture `px`: the frame in `gold`,
 * the rest of the [ and ] tiles the backdrop's black, and every other pixel
 * what it was (`orig`, or Seamless's). */
static bool lit_at(const uint32_t *px, int w, int h, int x0, uint8_t gold, uint32_t (*orig)(int, int)) {
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int c = x - x0;
            bool bracket = y >= 16 && y < 32 && ((c >= 216 && c < 224) || (c >= 240 && c < 248));
            uint32_t want = fax_item_box_on_frame(c, y) ? drawn(gold, y)
                            : bracket                   ? drawn(palette_ram[0], y)
                                                        : orig(x, y);
            if (px[y * w + x] != want) {
                fprintf(stderr, "at %d, %d (band %d): %08X, not %08X\n", x, y, c, px[y * w + x], want);
                return false;
            }
        }
    return true;
}
static bool lit(const uint32_t *px) { return lit_at(px, W, H, HUD_X, 0x27, was); }
static bool unlit(const uint32_t *px) { return px == seamless && !shown && frame_x == -1; }

static void select_item(uint8_t item) { ram[0x03C1] = item; }
static void boots(bool on) { ram[0x00A5] = on ? 0x80 | 0x03 : 0x03; }   /* $80 among the other status bits */
static void glass(uint8_t seconds) { ram[0x042A] = seconds; }

/* ---- the rules ---- */

static void test_running(void) {
    /* The box's own item, and only it. */
    CHECK(fax_item_box_running(WING_BOOTS, 0x80, 0xFF));
    CHECK(fax_item_box_running(WING_BOOTS, 0x83, 0xFF));
    CHECK(!fax_item_box_running(WING_BOOTS, 0x7F, 0xFF));          /* every other status bit: not flying */
    CHECK(!fax_item_box_running(WING_BOOTS, 0x00, 0x05));          /* the Hour Glass freezing, the boots shown */
    for (unsigned s = 0; s < 0x80; ++s) CHECK(fax_item_box_running(HOUR_GLASS, 0x00, (uint8_t)s));
    CHECK(fax_item_box_running(HOUR_GLASS, 0x80, 0x00));          /* LimitItemDurations' 0: still freezing */
    CHECK(!fax_item_box_running(HOUR_GLASS, 0x80, 0xFF));         /* the boots flying, the Hour Glass shown */
    CHECK(!fax_item_box_running(HOUR_GLASS, 0x00, 0x80));
    for (unsigned item = 0; item < 0x100; ++item)
        if (item != WING_BOOTS && item != HOUR_GLASS) CHECK(!fax_item_box_running((uint8_t)item, 0xFF, 0x00));
}

static void test_gold(void) {
    CHECK(fax_item_box_gold(0x30) == 0x27);   /* every region's white */
    CHECK(fax_item_box_gold(0x20) == 0x27);   /* a fade's first stage: still white */
    CHECK(fax_item_box_gold(0x10) == 0x17);
    CHECK(fax_item_box_gold(0x00) == 0x07);
    CHECK(fax_item_box_gold(0x0F) == 0x0F);   /* black: the fade's end */
    CHECK(fax_item_box_gold(0x1D) == 0x0F && fax_item_box_gold(0x0D) == 0x0F && fax_item_box_gold(0x3E) == 0x0F);
    CHECK(fax_item_box_gold(0x70) == 0x27);   /* palette RAM's top bits are not the color's */
}

static void test_frame(void) {
    unsigned n = 0;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 256; ++x) n += fax_item_box_on_frame(x, y);
    CHECK(n == 26 + 26 + 2 * 2 * 15);       /* top and bottom 26 wide, sides 2 wide over the 15 lines between */
    CHECK(fax_item_box_on_frame(219, 15) && fax_item_box_on_frame(244, 31) && fax_item_box_on_frame(220, 23));
    CHECK(fax_item_box_on_frame(243, 16) && !fax_item_box_on_frame(221, 16) && !fax_item_box_on_frame(242, 30));
    /* No pixel of the item's picture (x 224-239, lines 16-30: every item's is 15 lines or fewer). */
    for (int y = 16; y <= 30; ++y)
        for (int x = 224; x < 240; ++x) CHECK(!fax_item_box_on_frame(x, y));
    /* The brackets' own uprights (x 220 and 243, lines 17-30) are under the sides. */
    for (int y = 17; y <= 30; ++y) CHECK(fax_item_box_on_frame(220, y) && fax_item_box_on_frame(243, y));
}

/* ---- the machine ---- */

static void test_off(void) {
    power_on(false);
    select_item(WING_BOOTS); boots(true);
    int w = W, h = H;
    CHECK(fax_item_box_present(seamless, &w, &h) == seamless && !shown);
    presents = 0;
    CHECK(fax_item_box_present(NULL, &w, &h) == NULL && presents == 0);   /* the original: nothing asked of the host */
    power_on(true);
    select_item(WING_BOOTS); boots(true);
    CHECK(lit(present_now()));
    reset_cb();                             /* a power-on or reset: off until the plugin activates again */
    CHECK(unlit(present_now()));
}

static void test_boots(void) {
    power_on(true);
    select_item(WING_BOOTS);
    CHECK(unlit(present_now()));            /* his, not flying */
    boots(true);
    const uint32_t *px = present_now();
    CHECK(px != seamless && lit(px) && shown && frame_x == HUD_X + 219 && gold_nes == 0x27 && white_nes == 0x30);
    boots(false);                           /* the frame the flight ends */
    CHECK(unlit(present_now()));
    boots(true);
    select_item(HOUR_GLASS);                /* the Hour Glass shown while the boots fly */
    CHECK(unlit(present_now()));
    select_item(NO_ITEM);
    CHECK(unlit(present_now()));
    select_item(MATTOCK);
    glass(9);
    CHECK(unlit(present_now()));
}

static void test_glass(void) {
    power_on(true);
    select_item(HOUR_GLASS);
    CHECK(unlit(present_now()));
    glass(15);
    CHECK(lit(present_now()));
    glass(0);                               /* its last second, or a boss's screen's LimitItemDurations */
    CHECK(lit(present_now()));
    glass(0xFF);                            /* the frame it wraps: "The power of the Hour Glass is gone." */
    CHECK(unlit(present_now()));
    glass(12);
    select_item(WING_BOOTS);                /* the boots shown while the Hour Glass freezes */
    CHECK(unlit(present_now()));
    boots(true);                            /* both running: the boots shown, lit */
    CHECK(lit(present_now()));
}

static void test_status_bar(void) {
    power_on(true);
    select_item(WING_BOOTS); boots(true);
    hud_up = false;                         /* the title, the ending, the screen off */
    CHECK(unlit(present_now()));
    hud_up = true;
    tiles[3][30] = 0x00;                    /* the box is not where UI_DrawHUDSprites puts it */
    CHECK(unlit(present_now()));
    tiles[3][30] = 0x2F;
    CHECK(lit(present_now()));
    world_x = 84;                           /* the band wherever Seamless put it */
    CHECK(lit_at(present_now(), W, H, 84, 0x27, was));
    world_x = W - 255;                      /* a band that would not fit */
    CHECK(unlit(present_now()));
    /* The stock 256-wide picture, the engine's own. */
    world_x = HUD_X;
    for (int i = 0; i < 256 * 240; ++i) native[i] = 0xFD000000u | (uint32_t)i;
    int w = 0, h = 0;
    presents = 0;
    const uint32_t *px = fax_item_box_present(NULL, &w, &h);
    CHECK(presents == 1 && w == 256 && h == 240 && px != native);
    for (int y = 0; y < 240; ++y)
        for (int x = 0; x < 256; ++x) {
            bool bracket = y >= 16 && y < 32 && ((x >= 216 && x < 224) || (x >= 240 && x < 248));
            uint32_t want = fax_item_box_on_frame(x, y) ? drawn(0x27, y)
                            : bracket                   ? drawn(0x0F, y)
                                                        : 0xFD000000u | (uint32_t)(y * 256 + x);
            CHECK(px[y * 256 + x] == want);
        }
    /* Too short a picture to hold the band. */
    w = W; h = 31;
    CHECK(fax_item_box_present(seamless, &w, &h) == seamless);
}

static void test_palette(void) {
    power_on(true);
    select_item(HOUR_GLASS); glass(7);
    /* The status bar's sub-palette, UI_AttributeDataIndex's: the white read from it. */
    ram[0x038D] = 2;
    palette_ram[2 * 4 + 3] = 0x30;
    CHECK(lit(present_now()) && white_nes == 0x30);
    palette_ram[2 * 4 + 3] = 0x10;          /* a fade-out's second stage: light grey, the gold a row down */
    CHECK(lit_at(present_now(), W, H, HUD_X, 0x17, was) && gold_nes == 0x17);
    palette_ram[2 * 4 + 3] = 0x00;
    CHECK(lit_at(present_now(), W, H, HUD_X, 0x07, was));
    palette_ram[2 * 4 + 3] = 0x0F;          /* faded to black: the bar is gone, and nothing is lit */
    CHECK(unlit(present_now()));
    ram[0x038D] = 0x83;                     /* only its two low bits */
    CHECK(lit(present_now()));
    /* The backdrop the brackets are erased with is palette RAM's first color. */
    palette_ram[0] = 0x2D;
    CHECK(lit(present_now()));
    palette_ram[0] = 0x0F;
    /* Each line as it was drawn: emphasis on one, greyscale on another. */
    masks[20] = 0x1E | 0x20 | 0x80;
    masks[15] = 0x1E | 0x01;
    const uint32_t *px = present_now();
    CHECK(lit(px));
    CHECK(px[20 * W + HUD_X + 219] == hw_palette_argb[0x27 | (0x5 << 6)]);   /* the side, emphasized */
    CHECK(px[15 * W + HUD_X + 230] == hw_palette_argb[0x20]);               /* the top, $27 in greyscale */
}

int main(void) {
    for (int i = 0; i < 512; ++i) hw_palette_argb[i] = 0xFF000000u | (uint32_t)i << 8 | 0x5Au;
    test_running();
    test_gold();
    test_frame();
    test_off();
    test_boots();
    test_glass();
    test_status_bar();
    test_palette();
    printf("item box: %u checks passed\n", checks);
    return 0;
}
