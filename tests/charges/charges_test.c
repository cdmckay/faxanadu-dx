/* Unit tests for the Elixir on the status bar (fax_charges.c), without the
 * emulator or a ROM. It runs against a model of the machine: Recharging
 * items' answer for each item (fax_recharge_charge), CPU RAM, a cart image
 * whose bank 10 holds the items' tile numbers and pictures where the game
 * keeps them, palette RAM and the PPU mask the frame was drawn with, the
 * host's NES colors, and Seamless's layout. Each check is on the pixels a
 * player would see. */
#include <stdio.h>
static char logged[128];
#define FAX_CHARGES_LOG(...) snprintf(logged, sizeof logged, __VA_ARGS__)
#include "fax_charges.c"

static unsigned checks;
#define CHECK(test)                                                                                                    \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (!(test)) {                                                                                                 \
            fprintf(stderr, "FAIL line %u: %s\n", __LINE__, #test);                                                    \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)

/* ---- the machine ---- */
enum { HUD_X = 92 }; /* Seamless's status bar band at 424 wide, with the Elixir left of it */
static uint8_t ram[0x800];
static uint8_t cart[16 * 16384];
static size_t cart_size = sizeof cart;
static uint8_t palette_ram[32];
static uint8_t mask = 0x1E; /* PPUMASK: background and sprites on, no emphasis */
static FaxCharge charge_now[FAX_RECHARGE_ITEMS];
static bool box_up, hud_up = true;
static int world_x = HUD_X;
static uint32_t native[256 * 240];
static int presents;
uint32_t hw_palette_argb[512];

FaxCharge fax_recharge_charge(int item) {
    return item >= 0 && item < FAX_RECHARGE_ITEMS ? charge_now[item] : FAX_CHARGE_NONE;
}
bool fax_text_box_showing(void) { return box_up; }
bool fax_text_hud_showing(void) { return hud_up; }
int fax_world_hud_x(int width) { return width <= 256 ? 0 : world_x; }
uint8_t cyc_mod_peek(uint16_t addr) { return addr < 0x2000 ? ram[addr & 0x7FF] : 0; }
size_t cyc_mod_copy_cart_payload(uint8_t *out, size_t capacity) {
    if (out && capacity >= cart_size) memcpy(out, cart, cart_size);
    return cart_size;
}
uint8_t cyc_render_line_mask(int y) {
    CHECK(y >= 16 && y < 32);
    return mask;
} /* a status bar line */
uint8_t cyc_render_palette(int e) { return palette_ram[e & 31]; }
const uint32_t *cyc_render_present(int *w, int *h) {
    ++presents;
    *w = 256;
    *h = 240;
    return native;
}

static NESModActivationCallback reset_cb, plugin_cb;
static NESModSavestateGet save_get;
static NESModSavestateSet save_set;
static NESModSavestateValidate save_validate;
int nes_mod_register_reset_callback(NESModActivationCallback cb) {
    reset_cb = cb;
    return 1;
}
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    CHECK(!strcmp(id, "faxanadu.charges"));
    plugin_cb = cb;
    return 1;
}
int nes_mod_register_savestate_hook(const char *id, NESModSavestateGet get, NESModSavestateSet set) {
    CHECK(!strcmp(id, "faxanadu.charges"));
    save_get = get;
    save_set = set;
    return 1;
}
int nes_mod_register_savestate_validator(const char *id, NESModSavestateValidate v) {
    CHECK(!strcmp(id, "faxanadu.charges"));
    save_validate = v;
    return 1;
}

enum { BOOTS = FAX_RECHARGE_BOOTS, GLASS = FAX_RECHARGE_GLASS, ELIXIR = FAX_RECHARGE_ELIXIR };
enum { W = 424, H = 240, X = HUD_X - 16, TOP = 16 }; /* the Elixir: against the band's left edge, rows 16-31 */
#define NONE     FAX_CHARGE_NONE
#define READY    FAX_CHARGE_READY
#define USED     FAX_CHARGE_USED
#define BACKDROP 0xFF123456u /* what Seamless drew there: the black band, or anything */

/* Bank 10 as the game has it: ITEM_TILEMAP_INDEXES ($B4E4) gives each item
 * four tile numbers, and each tile is 16 bytes from $8500. The pictures here
 * are made up, a different one for each item, with every pixel value, so a
 * picture drawn is the Elixir's and no other's. */
static const uint8_t ITEM[3] = { 0x0F, 0x0D, 0x12 }; /* ITEMINVENTORY_WING_BOOTS, _HOUR_GLASS, _ELIXIR */
static const uint8_t TILE[3][4] = { { 0x22, 0x23, 0x24, 0x25 },
                                    { 0x1A, 0x1B, 0x1C, 0x1D },
                                    { 0x34, 0x35, 0x36, 0x37 } };
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
/* The pixel value (0-3) of the Elixir's picture at (x, y), decoded here from the cart. */
static unsigned value_at(int x, int y) {
    const uint8_t *tile = bank10() + 0x0500 + TILE[ELIXIR][(y / 8) * 2 + x / 8] * 16;
    int r = y % 8, c = x % 8;
    return (tile[r] >> (7 - c) & 1u) | (tile[r + 8] >> (7 - c) & 1u) << 1;
}

static void power_on(bool on) {
    reset_cb();
    memset(ram, 0, sizeof ram);
    ram[0x038D] = 3;                                            /* UI_AttributeDataIndex: Eolis's status bar */
    static const uint8_t eolis[4] = { 0x0F, 0x1A, 0x16, 0x30 }; /* black, green, pink, white */
    for (int p = 0; p < 4; ++p) memcpy(palette_ram + p * 4, eolis, 4);
    palette_ram[12 + 1] = 0x1A;
    palette_ram[12 + 2] = 0x16;
    palette_ram[12 + 3] = 0x30;
    mask = 0x1E;
    memset(charge_now, 0, sizeof charge_now);
    box_up = false;
    hud_up = true;
    world_x = HUD_X;
    loaded = failed = false;
    if (on) plugin_cb();
}
static void frame(void) {
    ++ram[0x1A];
    fax_charges_frame_end();
} /* the NMI's InterruptCounter, then the frame's end */
static void frames(int n) {
    while (n--) frame();
}

static uint32_t seamless[W * H];
static const uint32_t *present(int *w, int *h) {
    for (int i = 0; i < W * H; ++i) seamless[i] = BACKDROP;
    *w = W;
    *h = H;
    return fax_charges_present(seamless, w, h);
}
static uint32_t color(uint8_t nes) { return hw_palette_argb[nes]; }
/* The Elixir at x of a picture `px`, W wide: its picture in `inks` (by pixel value), the backdrop where it is clear. */
static bool drawn_at(const uint32_t *px, int x, const uint32_t inks[4]) {
    for (int y = 0; y < 16; ++y)
        for (int c = 0; c < 16; ++c) {
            unsigned v = value_at(c, y);
            uint32_t want = v ? inks[v] : BACKDROP;
            if (px[(TOP + y) * W + x + c] != want) return false;
        }
    return true;
}
static bool drawn(const uint32_t *px, const uint32_t inks[4]) { return drawn_at(px, X, inks); }
/* Nothing but the Elixir's place changed: the status bar and the world are
 * Seamless's, nothing is past the item box, and nothing else is drawn (the
 * Wing Boots and the Hour Glass are not). */
static bool only_the_elixir(const uint32_t *px) {
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            bool place = y >= TOP && y < TOP + 16 && x >= X && x < X + 16;
            if (!place && px[y * W + x] != BACKDROP) return false;
        }
    return true;
}
static bool untouched(const uint32_t *px) { return px == seamless; }
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

/* ---- tests ---- */
static void registers(void) {
    CHECK(reset_cb && plugin_cb && save_get && save_set && save_validate);
    power_on(false);
    CHECK(fax_charges_width() == 0); /* off: Seamless keeps the status bar centred */
    power_on(true);
    CHECK(fax_charges_width() == FAX_CHARGES_ITEM && FAX_CHARGES_ITEM == 16);
}

static void greys(void) {
    /* The blacks stay black; a color takes the grey a step darker than its
     * brightness; a grey goes a step darker, down to dark grey, never lighter. */
    static const uint8_t blacks[] = { 0x0D, 0x1D, 0x0E, 0x1E, 0x2E, 0x3E, 0x0F, 0x1F, 0x2F, 0x3F };
    for (unsigned i = 0; i < sizeof blacks; ++i) CHECK(fax_charges_grey(blacks[i]) == 0x0F);
    for (uint8_t hue = 1; hue <= 0x0C; ++hue) {
        CHECK(fax_charges_grey(hue) == 0x2D);        /* dark colors */
        CHECK(fax_charges_grey(0x10 | hue) == 0x2D); /* the status bar's green $1A and pink $16 */
        CHECK(fax_charges_grey(0x20 | hue) == 0x00);
        CHECK(fax_charges_grey(0x30 | hue) == 0x10);
    }
    CHECK(fax_charges_grey(0x30) == 0x10 && fax_charges_grey(0x20) == 0x10); /* white: light grey */
    CHECK(fax_charges_grey(0x10) == 0x00 && fax_charges_grey(0x3D) == 0x00);
    CHECK(fax_charges_grey(0x00) == 0x2D && fax_charges_grey(0x2D) == 0x2D);
}

static void the_three_states(void) {
    power_on(true);
    make_cart();
    int w, h;
    /* Not his: the picture is Seamless's, untouched, but the Elixir's place is kept. */
    frames(2);
    const uint32_t *px = present(&w, &h);
    CHECK(untouched(px) && elixir_x == X && !shown);
    /* Charged: in the status bar's colours, against the band's left edge, and nothing else drawn. */
    charge_now[ELIXIR] = READY;
    frame();
    px = present(&w, &h);
    CHECK(!untouched(px) && w == W && h == H && shown && drawn(px, ready_inks()) && only_the_elixir(px));
    /* Spent: grey. */
    charge_now[ELIXIR] = USED;
    px = present(&w, &h);
    CHECK(drawn(px, grey_inks()) && only_the_elixir(px));
    /* Not his again (the Spring of Trunk took it): nothing. */
    charge_now[ELIXIR] = NONE;
    frame();
    CHECK(untouched(present(&w, &h)) && !shown);
    /* The Wing Boots and the Hour Glass, ready or in use, are never drawn, and change nothing of the Elixir's. */
    for (int boots = NONE; boots <= USED; ++boots)
        for (int glass = NONE; glass <= USED; ++glass) {
            charge_now[BOOTS] = (FaxCharge)boots;
            charge_now[GLASS] = (FaxCharge)glass;
            charge_now[ELIXIR] = NONE;
            frame();
            CHECK(untouched(present(&w, &h)));
            charge_now[ELIXIR] = READY;
            frame();
            px = present(&w, &h);
            CHECK(drawn(px, ready_inks()) && only_the_elixir(px) && t.blink == 0);
        }
    /* The ready colors are the status bar's palette: Eolis's green, pink and white here. */
    CHECK(ready_inks()[1] == color(0x1A) && ready_inks()[2] == color(0x16) && ready_inks()[3] == color(0x30));
    CHECK(grey_inks()[1] == color(0x2D) && grey_inks()[2] == color(0x2D) && grey_inks()[3] == color(0x10));
    /* Another screen's palette (UI_AttributeDataIndex 1). */
    ram[0x038D] = 1;
    palette_ram[4 + 1] = 0x06;
    palette_ram[4 + 2] = 0x27;
    palette_ram[4 + 3] = 0x3C;
    px = present(&w, &h);
    uint32_t other[4] = { 0, color(0x06), color(0x27), color(0x3C) };
    CHECK(drawn(px, other));
    ram[0x038D] = 3;
    /* The frame's emphasis and greyscale, as the PPU drew the status bar's lines. */
    mask = 0x1E | 0x20 | 0x01;
    px = present(&w, &h);
    uint32_t tinted[4] = { 0, hw_palette_argb[0x10 | 64], hw_palette_argb[0x10 | 64], hw_palette_argb[0x30 | 64] };
    CHECK(drawn(px, tinted));
    mask = 0x1E;
}

static void where_it_shows(void) {
    power_on(true);
    make_cart();
    int w, h;
    charge_now[BOOTS] = charge_now[GLASS] = charge_now[ELIXIR] = READY;
    frame();
    /* The stock view: the engine's 256-wide picture has no room left of the status bar. */
    int before = presents;
    w = h = 0;
    CHECK(fax_charges_present(NULL, &w, &h) == NULL && presents == before + 1 && elixir_x == -1);
    /* Against the status bar's left edge, wherever Seamless put it. */
    world_x = 84;
    const uint32_t *px = present(&w, &h);
    CHECK(elixir_x == 84 - 16 && drawn_at(px, 84 - 16, ready_inks()) && px[TOP * W + 84] == BACKDROP);
    world_x = HUD_X;
    px = present(&w, &h);
    CHECK(elixir_x == X && drawn(px, ready_inks()));
    /* No room left of it in a narrower picture (Seamless's atlas of a narrow area, its band centred). */
    static uint32_t narrow[260 * 240];
    for (int i = 0; i < 260 * 240; ++i) narrow[i] = BACKDROP;
    w = 260;
    h = 240;
    world_x = 2;
    CHECK(fax_charges_present(narrow, &w, &h) == narrow && elixir_x == -1);
    world_x = HUD_X;
    /* Only beside a status bar that shows: not on the title, nor with the screen off. */
    hud_up = false;
    CHECK(untouched(present(&w, &h)) && !shown);
    hud_up = true;
    CHECK(!untouched(present(&w, &h)));
    /* A text box or the menu up: the status bar stays, so the Elixir does. */
    box_up = true;
    CHECK(!untouched(present(&w, &h)));
    box_up = false;
    /* Off (the original game): nothing, and nothing kept. */
    power_on(false);
    charge_now[ELIXIR] = READY;
    frame();
    charge_now[ELIXIR] = USED;
    frames(3);
    CHECK(untouched(present(&w, &h)));
    CHECK(t.blink == 0 && t.last == UNKNOWN);
}

static bool lit_now(void) {
    int w, h;
    present(&w, &h);
    return shown;
}

static void blinks_when_used(void) {
    power_on(true);
    make_cart();
    charge_now[ELIXIR] = READY;
    frames(5);
    CHECK(t.blink == 0 && lit_now());
    /* The revive: spent at once, and blinking through "I've used Elixir."
     * (the count holds while the box is up) and FAX_CHARGES_BLINK frames after. */
    charge_now[ELIXIR] = USED;
    box_up = true;
    frame();
    CHECK(t.blink == FAX_CHARGES_BLINK);
    unsigned on = 0, off = 0;
    for (int n = 0; n < 300; ++n) {
        frame();
        int w, h;
        const uint32_t *px = present(&w, &h);
        if (shown) {
            ++on;
            CHECK(drawn(px, grey_inks()));
        } else {
            ++off;
            CHECK(untouched(px));
        }
        CHECK(shown == !(ram[0x1A] & FAX_CHARGES_BLINK_OFF));
    }
    CHECK(on + off == 300 && on >= 144 && off >= 144 && t.blink == FAX_CHARGES_BLINK); /* 8 on, 8 off, held */
    box_up = false;
    int left = 0;
    while (t.blink) {
        frame();
        ++left;
    }
    CHECK(left == FAX_CHARGES_BLINK);
    for (int n = 0; n < 64; ++n) {
        frame();
        CHECK(lit_now());
    } /* then steady grey */
    /* Charged again (a Guru): no blink, in its colors at once, even mid-blink. */
    charge_now[ELIXIR] = READY;
    frame();
    charge_now[ELIXIR] = USED;
    frames(3);
    CHECK(t.blink > 0);
    charge_now[ELIXIR] = READY;
    frame();
    CHECK(t.blink == 0 && lit_now());
    /* The Wing Boots or the Hour Glass used: the Elixir does not blink. */
    charge_now[BOOTS] = charge_now[GLASS] = READY;
    frame();
    charge_now[BOOTS] = charge_now[GLASS] = USED;
    frame();
    CHECK(t.blink == 0 && lit_now());
    /* Spent from not his (a state loaded, a file continued): no blink. */
    charge_now[ELIXIR] = NONE;
    frame();
    charge_now[ELIXIR] = USED;
    frame();
    CHECK(t.blink == 0);
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
    CHECK(n == 3 && rec[0] == 2 && rec[1] == USED && rec[2] == FAX_CHARGES_BLINK - 10);
    CHECK(save_get(rec, 2) == -1);
    frames(40);
    CHECK(save_validate(rec, n) && save_set(rec, n) && t.blink == FAX_CHARGES_BLINK - 10 && t.last == USED);
    /* Records it refuses: another length or version, a charge or blink out of range. */
    uint8_t bad[7];
    memcpy(bad, rec, 3);
    bad[0] = 3;
    CHECK(!save_validate(bad, 3) && !save_set(bad, 3));
    memcpy(bad, rec, 3);
    bad[1] = 3;
    CHECK(!save_validate(bad, 3));
    memcpy(bad, rec, 3);
    bad[2] = FAX_CHARGES_BLINK + 1;
    CHECK(!save_validate(bad, 3));
    CHECK(!save_validate(rec, 2) && !save_validate(rec, 4));
    memcpy(bad, rec, 3);
    bad[1] = UNKNOWN;
    CHECK(save_validate(bad, 3));
    /* A version 1 record (#115's row: the three items' states, then their
     * blinks, the Wing Boots, the Hour Glass, the Elixir) loads the Elixir's. */
    const uint8_t row[7] = { 1, READY, USED, USED, 0, 40, 30 };
    CHECK(save_validate(row, 7) && save_set(row, 7) && t.last == USED && t.blink == 30);
    memcpy(bad, row, 7);
    bad[0] = 2;
    CHECK(!save_validate(bad, 7));
    memcpy(bad, row, 7);
    bad[2] = 3;
    CHECK(!save_validate(bad, 7));
    memcpy(bad, row, 7);
    bad[4] = FAX_CHARGES_BLINK + 1;
    CHECK(!save_validate(bad, 7));
    /* A state saved without the record: nothing blinks, and the next frame
     * only learns where the Elixir stands (spent, it does not start blinking). */
    CHECK(save_validate(NULL, 0) && save_set(NULL, 0));
    CHECK(t.blink == 0 && t.last == UNKNOWN);
    frame();
    CHECK(t.blink == 0 && t.last == USED);
}

static void no_picture(void) {
    power_on(true);
    make_cart();
    cart_size = 10 * 16384; /* no bank 10 */
    charge_now[ELIXIR] = READY;
    int w, h;
    logged[0] = 0;
    CHECK(untouched(present(&w, &h)) && failed && strstr(logged, "cannot read the Elixir's picture"));
    cart_size = sizeof cart;
    CHECK(untouched(present(&w, &h))); /* off until the next power-on */
    power_on(true);
    charge_now[ELIXIR] = READY;
    for (int k = 0; k < 4; ++k)
        memset(bank10() + 0x0500 + TILE[ELIXIR][k] * 16, 0, 16); /* a blank picture: not the game's */
    CHECK(untouched(present(&w, &h)) && failed);
}

int main(void) {
    for (int i = 0; i < 512; ++i) hw_palette_argb[i] = 0xFF000000u | (uint32_t)i << 8; /* a color tells its index */
    registers();
    greys();
    the_three_states();
    where_it_shows();
    blinks_when_used();
    save_states();
    no_picture();
    printf("charges_test: %u checks passed\n", checks);
    return 0;
}
