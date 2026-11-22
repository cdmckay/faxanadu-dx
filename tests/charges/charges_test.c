/* Unit tests for the recharging items on the status bar (fax_charges.c),
 * without the emulator or a ROM. The row runs against a model of the machine:
 * Recharging items' answer for each item (fax_recharge_charge), CPU RAM, a
 * cart image whose bank 10 holds the items' tile numbers and pictures where
 * the game keeps them, palette RAM and the PPU mask the frame was drawn with,
 * the host's NES colors, and Seamless's layout. Each check is on the pixels a
 * player would see. */
#include <stdio.h>
static char logged[128];
#define FAX_CHARGES_LOG(...) snprintf(logged, sizeof logged, __VA_ARGS__)
#include "fax_charges.c"

static unsigned checks;
#define CHECK(test) do{++checks;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(1);}}while(0)

/* ---- the machine ---- */
static uint8_t ram[0x800];
static uint8_t cart[16 * 16384];
static size_t cart_size = sizeof cart;
static uint8_t palette_ram[32];
static uint8_t mask = 0x1E;                 /* PPUMASK: background and sprites on, no emphasis */
static FaxCharge charge_now[FAX_RECHARGE_ITEMS];
static bool box_up, hud_up = true;
static int world_x = 52;                    /* Seamless's native column 0 at 424 wide, with the row */
static uint32_t native[256 * 240];
static int presents;
uint32_t hw_palette_argb[512];

FaxCharge fax_recharge_charge(int item) { return item >= 0 && item < FAX_RECHARGE_ITEMS ? charge_now[item] : FAX_CHARGE_NONE; }
bool fax_text_box_showing(void) { return box_up; }
bool fax_text_hud_showing(void) { return hud_up; }
int fax_world_native_x(int width) { return width <= 256 ? 0 : world_x; }
uint8_t cyc_mod_peek(uint16_t addr) { return addr < 0x2000 ? ram[addr & 0x7FF] : 0; }
size_t cyc_mod_copy_cart_payload(uint8_t *out, size_t capacity) {
    if (out && capacity >= cart_size) memcpy(out, cart, cart_size);
    return cart_size;
}
uint8_t cyc_render_line_mask(int y) { CHECK(y >= 16 && y < 32); return mask; }   /* a status bar line */
uint8_t cyc_render_palette(int e) { return palette_ram[e & 31]; }
const uint32_t *cyc_render_present(int *w, int *h) { ++presents; *w = 256; *h = 240; return native; }

static NESModActivationCallback reset_cb, plugin_cb;
static NESModSavestateGet save_get;
static NESModSavestateSet save_set;
static NESModSavestateValidate save_validate;
int nes_mod_register_reset_callback(NESModActivationCallback cb) { reset_cb = cb; return 1; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    CHECK(!strcmp(id, "faxanadu.charges"));
    plugin_cb = cb;
    return 1;
}
int nes_mod_register_savestate_hook(const char *id, NESModSavestateGet get, NESModSavestateSet set) {
    CHECK(!strcmp(id, "faxanadu.charges"));
    save_get = get; save_set = set;
    return 1;
}
int nes_mod_register_savestate_validator(const char *id, NESModSavestateValidate v) {
    CHECK(!strcmp(id, "faxanadu.charges"));
    save_validate = v;
    return 1;
}

enum { BOOTS = FAX_RECHARGE_BOOTS, GLASS = FAX_RECHARGE_GLASS, ELIXIR = FAX_RECHARGE_ELIXIR };
enum { W = 424, H = 240, ROW_X = 52 + 256, TOP = 16 };
#define NONE FAX_CHARGE_NONE
#define READY FAX_CHARGE_READY
#define USED FAX_CHARGE_USED
#define BACKDROP 0xFF123456u                /* what Seamless drew there: the black band, or anything */

/* Bank 10 as the game has it: ITEM_TILEMAP_INDEXES ($B4E4) gives each item
 * four tile numbers, and each tile is 16 bytes from $8500. The pictures here
 * are made up, a different one for each item, with every pixel value. */
static const uint8_t ITEM[3] = { 0x0F, 0x0D, 0x12 };              /* ITEMINVENTORY_WING_BOOTS, _HOUR_GLASS, _ELIXIR */
static const uint8_t TILE[3][4] = { { 0x22, 0x23, 0x24, 0x25 }, { 0x1A, 0x1B, 0x1C, 0x1D }, { 0x34, 0x35, 0x36, 0x37 } };
static uint8_t *bank10(void) { return cart + 10 * 16384; }
static void make_cart(void) {
    memset(cart, 0, sizeof cart);
    cart_size = sizeof cart;
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 4; ++k) {
            bank10()[0xB4E4 - 0x8000 + ITEM[i] * 4 + k] = TILE[i][k];
            uint8_t *tile = bank10() + 0x0500 + TILE[i][k] * 16;
            for (int b = 0; b < 16; ++b) tile[b] = (uint8_t)(0x5A ^ (i * 37 + k * 11 + b * 7) ^ (b < 8 ? 0x0F : 0xF0));
        }
}
/* The pixel value (0-3) of item i's picture at (x, y), decoded here from the cart. */
static unsigned value_at(int i, int x, int y) {
    const uint8_t *tile = bank10() + 0x0500 + TILE[i][(y / 8) * 2 + x / 8] * 16;
    int r = y % 8, c = x % 8;
    return (tile[r] >> (7 - c) & 1u) | (tile[r + 8] >> (7 - c) & 1u) << 1;
}

static void power_on(bool on) {
    reset_cb();
    memset(ram, 0, sizeof ram);
    ram[0x038D] = 3;                        /* UI_AttributeDataIndex: Eolis's status bar */
    static const uint8_t eolis[4] = { 0x0F, 0x1A, 0x16, 0x30 };   /* black, green, pink, white */
    for (int p = 0; p < 4; ++p) memcpy(palette_ram + p * 4, eolis, 4);
    palette_ram[12 + 1] = 0x1A; palette_ram[12 + 2] = 0x16; palette_ram[12 + 3] = 0x30;
    mask = 0x1E;
    memset(charge_now, 0, sizeof charge_now);
    box_up = false; hud_up = true; world_x = 52;
    loaded = failed = false;
    if (on) plugin_cb();
}
static void frame(void) { ++ram[0x1A]; fax_charges_frame_end(); }   /* the NMI's InterruptCounter, then the frame's end */
static void frames(int n) { while (n--) frame(); }

static uint32_t seamless[W * H];
static const uint32_t *present(int *w, int *h) {
    for (int i = 0; i < W * H; ++i) seamless[i] = BACKDROP;
    *w = W; *h = H;
    return fax_charges_present(seamless, w, h);
}
static uint32_t color(uint8_t nes) { return hw_palette_argb[nes] ; }
/* Item i in slot i of a picture `px`, W wide: its picture in `inks` (by pixel value), the backdrop where it is clear. */
static bool drawn(const uint32_t *px, int i, const uint32_t inks[4]) {
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            unsigned v = value_at(i, x, y);
            uint32_t want = v ? inks[v] : BACKDROP;
            if (px[(TOP + y) * W + ROW_X + i * FAX_CHARGES_SLOT + x] != want) return false;
        }
    return true;
}
static bool empty(const uint32_t *px, int i) {
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < FAX_CHARGES_SLOT && ROW_X + i * FAX_CHARGES_SLOT + x < W; ++x)
            if (px[(TOP + y) * W + ROW_X + i * FAX_CHARGES_SLOT + x] != BACKDROP) return false;
    return true;
}
static const uint32_t *ready_inks(void) {
    static uint32_t inks[4];
    for (int v = 1; v < 4; ++v) inks[v] = color(palette_ram[12 + v]);
    return inks;
}
static const uint32_t *grey_inks(void) {
    static uint32_t inks[4];
    for (int v = 1; v < 4; ++v) inks[v] = color(fax_charges_grey(palette_ram[12 + v]));
    return inks;
}
/* Nothing but the row's slots changed: the status bar and the world are Seamless's. */
static bool only_the_row(const uint32_t *px) {
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            bool row = y >= TOP && y < TOP + 16 && x >= ROW_X && x < ROW_X + FAX_CHARGES_ROW;
            if (!row && px[y * W + x] != BACKDROP) return false;
        }
    return true;
}

/* ---- tests ---- */
static void registers(void) {
    CHECK(reset_cb && plugin_cb && save_get && save_set && save_validate);
    power_on(false);
    CHECK(fax_charges_row() == 0);                 /* off: Seamless keeps the status bar centred */
    power_on(true);
    CHECK(fax_charges_row() == FAX_CHARGES_ROW && FAX_CHARGES_ROW == 64);
}

static void greys(void) {
    /* The blacks stay black; a color takes the grey a step darker than its
     * brightness; a grey goes a step darker, down to dark grey, never lighter. */
    static const uint8_t blacks[] = { 0x0D, 0x1D, 0x0E, 0x1E, 0x2E, 0x3E, 0x0F, 0x1F, 0x2F, 0x3F };
    for (unsigned i = 0; i < sizeof blacks; ++i) CHECK(fax_charges_grey(blacks[i]) == 0x0F);
    for (uint8_t hue = 1; hue <= 0x0C; ++hue) {
        CHECK(fax_charges_grey(hue) == 0x2D);              /* dark colors */
        CHECK(fax_charges_grey(0x10 | hue) == 0x2D);       /* the status bar's green $1A and pink $16 */
        CHECK(fax_charges_grey(0x20 | hue) == 0x00);
        CHECK(fax_charges_grey(0x30 | hue) == 0x10);
    }
    CHECK(fax_charges_grey(0x30) == 0x10 && fax_charges_grey(0x20) == 0x10);   /* white: light grey */
    CHECK(fax_charges_grey(0x10) == 0x00 && fax_charges_grey(0x3D) == 0x00);
    CHECK(fax_charges_grey(0x00) == 0x2D && fax_charges_grey(0x2D) == 0x2D);
}

static void the_three_states(void) {
    power_on(true);
    make_cart();
    int w, h;
    /* Nothing of his: the picture is Seamless's, untouched. */
    frames(2);
    const uint32_t *px = present(&w, &h);
    CHECK(px == seamless && row_x == ROW_X && !shown[0] && !shown[1] && !shown[2]);
    /* Ready, used, not his: each in its own slot, whatever the others are. */
    charge_now[BOOTS] = READY; charge_now[GLASS] = USED; charge_now[ELIXIR] = NONE;
    frame();
    px = present(&w, &h);
    CHECK(px != seamless && w == W && h == H && only_the_row(px));
    CHECK(drawn(px, BOOTS, ready_inks()) && drawn(px, GLASS, grey_inks()) && empty(px, ELIXIR));
    CHECK(shown[BOOTS] && shown[GLASS] && !shown[ELIXIR]);
    charge_now[BOOTS] = NONE; charge_now[GLASS] = READY; charge_now[ELIXIR] = USED;
    px = present(&w, &h);
    CHECK(empty(px, BOOTS) && drawn(px, GLASS, ready_inks()) && drawn(px, ELIXIR, grey_inks()) && only_the_row(px));
    charge_now[BOOTS] = READY; charge_now[ELIXIR] = READY;
    px = present(&w, &h);
    for (int i = 0; i < 3; ++i) CHECK(drawn(px, i, ready_inks()));
    /* The ready colors are the status bar's palette: Eolis's green, pink and white here. */
    CHECK(ready_inks()[1] == color(0x1A) && ready_inks()[2] == color(0x16) && ready_inks()[3] == color(0x30));
    CHECK(grey_inks()[1] == color(0x2D) && grey_inks()[2] == color(0x2D) && grey_inks()[3] == color(0x10));
    /* Another screen's palette (UI_AttributeDataIndex 1). */
    ram[0x038D] = 1;
    palette_ram[4 + 1] = 0x06; palette_ram[4 + 2] = 0x27; palette_ram[4 + 3] = 0x3C;
    px = present(&w, &h);
    uint32_t other[4] = { 0, color(0x06), color(0x27), color(0x3C) };
    for (int i = 0; i < 3; ++i) CHECK(drawn(px, i, other));
    ram[0x038D] = 3;
    /* The frame's emphasis and greyscale, as the PPU drew the status bar's lines. */
    mask = 0x1E | 0x20 | 0x01;
    px = present(&w, &h);
    uint32_t tinted[4] = { 0, hw_palette_argb[0x10 | 64], hw_palette_argb[0x10 | 64], hw_palette_argb[0x30 | 64] };
    CHECK(drawn(px, BOOTS, tinted));
    mask = 0x1E;
}

static void where_it_shows(void) {
    power_on(true);
    make_cart();
    int w, h;
    charge_now[BOOTS] = charge_now[GLASS] = charge_now[ELIXIR] = READY;
    frame();
    /* The stock view: the engine's 256-wide picture has no room for the row. */
    int before = presents;
    w = h = 0;
    CHECK(fax_charges_present(NULL, &w, &h) == NULL && presents == before + 1 && row_x == -1);
    /* Past the status bar's right edge, wherever Seamless put it. */
    world_x = 84;
    const uint32_t *px = present(&w, &h);
    CHECK(row_x == 84 + 256 && px[TOP * W + 84 + 256 - 1] == BACKDROP);
    world_x = 52;
    px = present(&w, &h);
    CHECK(row_x == ROW_X && drawn(px, ELIXIR, ready_inks()));
    /* No room past it in a narrower picture (Seamless's atlas of a narrow area). */
    static uint32_t narrow[300 * 240];
    for (int i = 0; i < 300 * 240; ++i) narrow[i] = BACKDROP;
    w = 300; h = 240; world_x = 22;
    CHECK(fax_charges_present(narrow, &w, &h) == narrow);
    world_x = 52;
    /* Only beside a status bar that shows: not on the title, nor with the screen off. */
    hud_up = false;
    CHECK(present(&w, &h) == seamless && !shown[0]);
    hud_up = true;
    CHECK(present(&w, &h) != seamless);
    /* A text box or the menu up: the status bar stays, so the row does. */
    box_up = true;
    CHECK(present(&w, &h) != seamless);
    box_up = false;
    /* Off (the original game): nothing, and nothing kept. */
    power_on(false);
    charge_now[BOOTS] = READY;
    frames(3);
    CHECK(present(&w, &h) == seamless);
    CHECK(t.blink[BOOTS] == 0 && t.last[BOOTS] == UNKNOWN);
}

static bool lit_now(int i) { int w, h; present(&w, &h); return shown[i]; }

static void blinks_when_used(void) {
    power_on(true);
    make_cart();
    charge_now[ELIXIR] = READY;
    frames(5);
    CHECK(t.blink[ELIXIR] == 0 && lit_now(ELIXIR));
    /* The revive: spent at once, and blinking through "I've used Elixir."
     * (the count holds while the box is up) and FAX_CHARGES_BLINK frames after. */
    charge_now[ELIXIR] = USED;
    box_up = true;
    frame();
    CHECK(t.blink[ELIXIR] == FAX_CHARGES_BLINK);
    unsigned on = 0, off = 0;
    for (int n = 0; n < 300; ++n) {
        frame();
        int w, h;
        const uint32_t *px = present(&w, &h);
        if (shown[ELIXIR]) { ++on; CHECK(drawn(px, ELIXIR, grey_inks())); }
        else { ++off; CHECK(empty(px, ELIXIR)); }
        CHECK(shown[ELIXIR] == !(ram[0x1A] & FAX_CHARGES_BLINK_OFF));
    }
    CHECK(on + off == 300 && on >= 144 && off >= 144 && t.blink[ELIXIR] == FAX_CHARGES_BLINK);   /* 8 on, 8 off, held */
    box_up = false;
    int left = 0;
    while (t.blink[ELIXIR]) { frame(); ++left; }
    CHECK(left == FAX_CHARGES_BLINK);
    for (int n = 0; n < 64; ++n) { frame(); CHECK(lit_now(ELIXIR)); }   /* then steady grey */
    /* Ready again (a Guru): no blink, in its colors at once, even mid-blink. */
    charge_now[ELIXIR] = READY;
    frame();
    charge_now[ELIXIR] = USED;
    frames(3);
    CHECK(t.blink[ELIXIR] > 0);
    charge_now[ELIXIR] = READY;
    frame();
    CHECK(t.blink[ELIXIR] == 0 && lit_now(ELIXIR));
    /* Each item blinks on its own: the Wing Boots used, the Hour Glass still ready. */
    charge_now[BOOTS] = charge_now[GLASS] = READY;
    frame();
    charge_now[BOOTS] = USED;
    frame();
    CHECK(t.blink[BOOTS] == FAX_CHARGES_BLINK && t.blink[GLASS] == 0 && t.blink[ELIXIR] == 0);
    /* Used from not his (a state loaded, a file continued): no blink. */
    charge_now[GLASS] = NONE;
    frame();
    charge_now[GLASS] = USED;
    frame();
    CHECK(t.blink[GLASS] == 0);
}

static void save_states(void) {
    power_on(true);
    make_cart();
    charge_now[ELIXIR] = READY;
    frame();
    charge_now[ELIXIR] = USED;
    frames(11);
    uint8_t rec[16];
    int n = save_get(rec, sizeof rec);
    CHECK(n == 7 && rec[0] == 1 && rec[1 + ELIXIR] == USED && rec[4 + ELIXIR] == FAX_CHARGES_BLINK - 10);
    CHECK(save_get(rec, 6) == -1);
    frames(40);
    CHECK(save_validate(rec, n) && save_set(rec, n) && t.blink[ELIXIR] == FAX_CHARGES_BLINK - 10);
    /* Records it refuses: another length or version, a charge or blink out of range. */
    uint8_t bad[7];
    memcpy(bad, rec, 7); bad[0] = 2;                       CHECK(!save_validate(bad, 7) && !save_set(bad, 7));
    memcpy(bad, rec, 7); bad[1] = 3;                       CHECK(!save_validate(bad, 7));
    memcpy(bad, rec, 7); bad[4] = FAX_CHARGES_BLINK + 1;   CHECK(!save_validate(bad, 7));
    CHECK(!save_validate(rec, 6));
    memcpy(bad, rec, 7); bad[1] = UNKNOWN;                 CHECK(save_validate(bad, 7));
    /* A state saved without the row: nothing blinks, and the next frame only
     * learns where each item stands (a used one does not start blinking). */
    CHECK(save_validate(NULL, 0) && save_set(NULL, 0));
    CHECK(t.blink[ELIXIR] == 0 && t.last[ELIXIR] == UNKNOWN);
    frame();
    CHECK(t.blink[ELIXIR] == 0 && t.last[ELIXIR] == USED);
}

static void no_pictures(void) {
    power_on(true);
    make_cart();
    cart_size = 10 * 16384;                 /* no bank 10 */
    charge_now[BOOTS] = READY;
    int w, h;
    logged[0] = 0;
    CHECK(present(&w, &h) == seamless && failed && strstr(logged, "cannot read the items' pictures"));
    cart_size = sizeof cart;
    CHECK(present(&w, &h) == seamless);     /* off until the next power-on */
    power_on(true);
    memset(bank10() + 0x0500, 0, 0x1000);   /* blank pictures: not the game's */
    CHECK(present(&w, &h) == seamless && failed);
}

int main(void) {
    for (int i = 0; i < 512; ++i) hw_palette_argb[i] = 0xFF000000u | (uint32_t)i << 8;   /* a color tells its index */
    registers();
    greys();
    the_three_states();
    where_it_shows();
    blinks_when_used();
    save_states();
    no_pictures();
    printf("charges_test: %u checks passed\n", checks);
    return 0;
}
