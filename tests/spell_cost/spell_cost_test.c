/* Unit tests for Spell cost (fax_spell_cost.c), without the emulator or a
 * ROM. It runs against a model of the machine: CPU RAM (Player_MP,
 * SelectedMagic, the bar's width, the status bar's palette), a cart image
 * whose bank 14 holds MAGIC_COSTS where the game keeps it, palette RAM and
 * the PPU mask the status bar's lines were drawn with, the host's NES colors,
 * Seamless's layout, and a picture with the M: bar drawn in it as the game
 * draws it. Each check is on the pixels a player would see. */
#include <stdio.h>
static char logged[128];
#define FAX_SPELL_COST_LOG(...) snprintf(logged, sizeof logged, __VA_ARGS__)
#include "fax_spell_cost.c"

static unsigned checks;
#define CHECK(test) do{++checks;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(1);}}while(0)

/* ---- the machine ---- */
enum { HUD_X = 92 };                        /* Seamless's status bar band at 424 wide, with the Elixir left of it */
enum { W = 424, H = 240, BAR = HUD_X + 24, MAX = 80 };
enum { DELUGE, THUNDER, FIRE, DEATH, TILTE, NO_SPELL = 0xFF };
static const uint8_t COSTS[5] = { 2, 4, 6, 10, 16 };
static uint8_t ram[0x800];
static uint8_t cart[16 * 16384];
static size_t cart_size = sizeof cart;
static uint8_t palette_ram[32];
static uint8_t mask = 0x1E;                 /* PPUMASK: background and sprites on, no emphasis */
static bool hud_up = true;
static int world_x = HUD_X;
static uint32_t native[256 * 240];
static int presents;
uint32_t hw_palette_argb[512];

bool fax_text_hud_showing(void) { return hud_up; }
int fax_world_hud_x(int width) { return width <= 256 ? 0 : world_x; }
uint8_t cyc_mod_peek(uint16_t addr) { return addr < 0x2000 ? ram[addr & 0x7FF] : 0; }
size_t cyc_mod_copy_cart_payload(uint8_t *out, size_t capacity) {
    if (out && capacity >= cart_size) memcpy(out, cart, cart_size);
    return cart_size;
}
uint8_t cyc_render_line_mask(int y) { CHECK(y >= 16 && y < 32); return mask; }   /* a status bar line */
uint8_t cyc_render_palette(int e) { return palette_ram[e & 31]; }
const uint32_t *cyc_render_present(int *w, int *h) { ++presents; *w = 256; *h = 240; return native; }

static NESModActivationCallback reset_cb, plugin_cb;
int nes_mod_register_reset_callback(NESModActivationCallback cb) { reset_cb = cb; return 1; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    CHECK(!strcmp(id, "faxanadu.spell-cost"));
    plugin_cb = cb;
    return 1;
}

#define BACKDROP 0xFF123456u                /* what Seamless drew around the status bar: anything */
static uint32_t color(uint8_t nes) { return hw_palette_argb[nes]; }
/* A color as the PPU draws it with the status bar's mask: its greyscale and emphasis. */
static uint32_t drawn(uint8_t nes) { return hw_palette_argb[((nes & (mask & 1 ? 0x30 : 0x3F)) | (mask >> 5) << 6) & 511]; }

/* Bank 14 as the game has it: MAGIC_COSTS at $B7A9. */
static void make_cart(void) {
    memset(cart, 0xEA, sizeof cart);
    cart_size = sizeof cart;
    memcpy(cart + 14 * 16384 + 0x37A9, COSTS, sizeof COSTS);
}

/* Eolis's status bar palette (UI_AttributeDataIndex 3): black, the HP bar's
 * pink, the MP bar's green, white. */
static void eolis_palette(void) {
    static const uint8_t hud[4] = { 0x0F, 0x15, 0x19, 0x30 };
    for (int p = 0; p < 4; ++p) memcpy(palette_ram + p * 4, hud, 4);
    ram[0x038D] = 3;
}

static void power_on(bool on) {
    reset_cb();
    memset(ram, 0, sizeof ram);
    ram[0x04EE] = 10;                       /* UI_MPAndHPBarWidth, as UI_DrawHUDSprites sets it */
    ram[0x03C0] = NO_SPELL;
    eolis_palette();
    mask = 0x1E;
    hud_up = true; world_x = HUD_X;
    loaded = failed = false;
    make_cart();
    if (on) plugin_cb();
}

/* The picture Seamless presents, with the M: bar drawn for `mp` as the game
 * draws it at x (UI_SetHUDBarValue's tiles: the border's white on lines 17
 * and 22, the fill's value 2 on lines 18-21, value 0 past it, and the end
 * tile's white line after the 80 pixels). */
static uint32_t seamless[W * H];
static void draw_bar(uint32_t *px, int w, int x, unsigned mp) {
    unsigned p = ram[0x038D] & 3u;
    for (int i = 0; i <= MAX; ++i)
        for (int y = 17; y <= 22; ++y) {
            uint32_t c = i == MAX || y == 17 || y == 22 ? drawn(palette_ram[p * 4 + 3])
                       : (unsigned)i < mp ? drawn(palette_ram[p * 4 + 2]) : drawn(palette_ram[0]);
            px[y * w + x + i] = c;
        }
}
static unsigned bar_shows;                  /* the MP the bar was last drawn for (Player_SetMP) */
static void set_mp(unsigned mp) { ram[0x039A] = (uint8_t)mp; bar_shows = mp; }   /* the game's way: drawn */
static void poke_mp(unsigned mp) { ram[0x039A] = (uint8_t)mp; }                   /* not drawn yet */
static void select_spell(uint8_t s) { ram[0x03C0] = s; }

static void model(uint32_t *px) {
    for (int i = 0; i < W * H; ++i) px[i] = BACKDROP;
    draw_bar(px, W, BAR, bar_shows);
}
static const uint32_t *present(int *w, int *h) {
    model(seamless);
    *w = W; *h = H;
    return fax_spell_cost_present(seamless, w, h);
}
static bool untouched(const uint32_t *px) { return px == seamless; }

/* The picture a player should see: the model's, with the bar's fill pixels
 * from <= i < to in `shade` where the bar shows fill. */
static uint32_t want[W * H];
static bool shows(const uint32_t *px, unsigned from, unsigned to, uint8_t shade) {
    model(want);
    for (int y = 18; y < 22; ++y)
        for (unsigned i = from; i < to; ++i)
            if (want[y * W + BAR + (int)i] == drawn(palette_ram[(ram[0x038D] & 3) * 4 + 2]))
                want[y * W + BAR + (int)i] = drawn(shade);
    return !memcmp(px, want, sizeof want);
}

/* ---- tests ---- */
static void registers(void) {
    CHECK(reset_cb && plugin_cb);
}

static void spans(void) {
    FaxSpellCostSpan s;
    /* Enough MP: the cost's pixels at the fill's end. */
    for (int k = 0; k < 5; ++k) {
        s = fax_spell_cost_span(40, (uint8_t)k, COSTS, MAX);
        CHECK(s.kind == FAX_SPELL_COST_CAST && s.from == 40 - COSTS[k] && s.to == 40);
    }
    /* Exactly the cost: it goes off (Player_ReduceMP's SBC leaves no borrow), and takes the whole fill. */
    s = fax_spell_cost_span(6, FIRE, COSTS, MAX);
    CHECK(s.kind == FAX_SPELL_COST_CAST && s.from == 0 && s.to == 6);
    /* Less than the cost: the whole fill, darker. */
    s = fax_spell_cost_span(8, TILTE, COSTS, MAX);
    CHECK(s.kind == FAX_SPELL_COST_SHORT && s.from == 0 && s.to == 8);
    s = fax_spell_cost_span(9, DEATH, COSTS, MAX);
    CHECK(s.kind == FAX_SPELL_COST_SHORT && s.from == 0 && s.to == 9);
    s = fax_spell_cost_span(1, DELUGE, COSTS, MAX);
    CHECK(s.kind == FAX_SPELL_COST_SHORT && s.from == 0 && s.to == 1);
    /* No MP, no fill: nothing to draw. */
    s = fax_spell_cost_span(0, TILTE, COSTS, MAX);
    CHECK(s.kind == FAX_SPELL_COST_NONE && s.to == 0);
    /* No spell selected ($FF), or anything Player_CastMagic would not cast (5 and up): the bar as it is. */
    static const uint8_t none[] = { NO_SPELL, 5, 6, 0x80, 0xFE };
    for (unsigned i = 0; i < sizeof none; ++i) CHECK(fax_spell_cost_span(40, none[i], COSTS, MAX).kind == FAX_SPELL_COST_NONE);
    /* Full: the last pixels of all 80. */
    s = fax_spell_cost_span(80, TILTE, COSTS, MAX);
    CHECK(s.kind == FAX_SPELL_COST_CAST && s.from == 64 && s.to == 80);
    /* More than the bar holds (a poke past Player_SetMP's cap): the game draws 80, and the cost ends there. */
    s = fax_spell_cost_span(90, DELUGE, COSTS, MAX);
    CHECK(s.kind == FAX_SPELL_COST_CAST && s.from == 78 && s.to == 80);
    CHECK(fax_spell_cost_span(40, DELUGE, COSTS, 0).kind == FAX_SPELL_COST_NONE);   /* no bar */
}

static void shades(void) {
    /* A row up and a row down, as the game's fades step: the bar's greens. */
    CHECK(fax_spell_cost_lighter(0x19) == 0x29 && fax_spell_cost_darker(0x19) == 0x09);
    CHECK(fax_spell_cost_lighter(0x1A) == 0x2A && fax_spell_cost_darker(0x1A) == 0x0A);
    for (uint8_t hue = 0; hue <= 0x0C; ++hue) {
        CHECK(fax_spell_cost_lighter(hue) == (0x10 | hue) && fax_spell_cost_lighter(0x10 | hue) == (0x20 | hue));
        CHECK(fax_spell_cost_lighter(0x20 | hue) == (0x30 | hue) && fax_spell_cost_lighter(0x30 | hue) == 0x30);
        CHECK(fax_spell_cost_darker(0x30 | hue) == (0x20 | hue) && fax_spell_cost_darker(0x20 | hue) == (0x10 | hue));
        CHECK(fax_spell_cost_darker(0x10 | hue) == hue && fax_spell_cost_darker(hue) == 0x0F);
    }
    /* The greys of hue $D: light to dark grey, and dark grey down to black. */
    CHECK(fax_spell_cost_lighter(0x2D) == 0x3D && fax_spell_cost_darker(0x3D) == 0x2D && fax_spell_cost_darker(0x2D) == 0x0F);
    /* The blacks stay black. */
    static const uint8_t blacks[] = { 0x0D, 0x1D, 0x0E, 0x1E, 0x2E, 0x3E, 0x0F, 0x1F, 0x2F, 0x3F };
    for (unsigned i = 0; i < sizeof blacks; ++i)
        CHECK(fax_spell_cost_lighter(blacks[i]) == 0x0F && fax_spell_cost_darker(blacks[i]) == 0x0F);
}

static void each_spell(void) {
    power_on(true);
    int w, h;
    set_mp(40);
    /* No spell selected: the picture is Seamless's, untouched. */
    const uint32_t *px = present(&w, &h);
    CHECK(untouched(px) && !shown && bar_x == BAR && last.kind == FAX_SPELL_COST_NONE);
    CHECK(loaded && !memcmp(costs, COSTS, sizeof costs));   /* MAGIC_COSTS, from the cart's bank 14 */
    /* Each spell: its cost's pixels at the fill's end a step lighter, $29, and nothing else changed. */
    for (int k = 0; k < 5; ++k) {
        select_spell((uint8_t)k);
        px = present(&w, &h);
        CHECK(!untouched(px) && w == W && h == H && shown && shows(px, 40u - COSTS[k], 40, 0x29));
        CHECK(last.kind == FAX_SPELL_COST_CAST && fill_nes == 0x19 && shade_nes == 0x29);
    }
    /* Not enough MP: the whole fill a step darker, $09. */
    set_mp(8);
    for (int k = 3; k < 5; ++k) {
        select_spell((uint8_t)k);
        px = present(&w, &h);
        CHECK(shown && shows(px, 0, 8, 0x09) && last.kind == FAX_SPELL_COST_SHORT && shade_nes == 0x09);
    }
    select_spell(FIRE);
    CHECK(shows(present(&w, &h), 2, 8, 0x29));
    /* Exactly the cost: lighter, all of it. */
    set_mp(6);
    CHECK(shows(present(&w, &h), 0, 6, 0x29));
    /* Empty: nothing to draw. */
    set_mp(0);
    select_spell(TILTE);
    CHECK(untouched(present(&w, &h)) && !shown);
    /* Full. */
    set_mp(80);
    CHECK(shows(present(&w, &h), 64, 80, 0x29));
    /* No spell again: untouched. */
    select_spell(NO_SPELL);
    CHECK(untouched(present(&w, &h)));
}

static void tracks_mp(void) {
    power_on(true);
    int w, h;
    set_mp(40);
    select_spell(FIRE);
    CHECK(shows(present(&w, &h), 34, 40, 0x29));
    /* A cast: Player_MP is 34 a frame before the game's bar is: the span is
     * 28-33 at once, and the bar's 34-39 stay its green until it is redrawn. */
    poke_mp(34);
    const uint32_t *px = present(&w, &h);
    CHECK(shows(px, 28, 34, 0x29) && last.from == 28 && last.to == 34);
    CHECK(px[19 * W + BAR + 36] == color(0x19));
    set_mp(34);
    CHECK(shows(present(&w, &h), 28, 34, 0x29));
    /* MP back (a kill, a shop) before the bar is redrawn: pixel 34 is not
     * fill yet, so it stays empty, and the span is 29-34. */
    poke_mp(35);
    px = present(&w, &h);
    CHECK(shows(px, 29, 35, 0x29) && px[19 * W + BAR + 34] == color(0x0F));
    set_mp(35);
    CHECK(shows(present(&w, &h), 29, 35, 0x29));
    /* Down to less than the cost: darker, every present after. */
    set_mp(5);
    CHECK(shows(present(&w, &h), 0, 5, 0x09));
    /* Anything else in the span that is not the bar's fill (a sprite over it) stays. */
    set_mp(40);
    model(seamless);
    seamless[19 * W + BAR + 37] = 0xFFABCDEFu;
    w = W; h = H;
    px = fax_spell_cost_present(seamless, &w, &h);
    CHECK(px[19 * W + BAR + 37] == 0xFFABCDEFu && px[19 * W + BAR + 36] == color(0x29) && px[18 * W + BAR + 37] == color(0x29));
}

static void palettes(void) {
    power_on(true);
    int w, h;
    set_mp(40);
    select_spell(DEATH);
    /* Another screen's palette (UI_AttributeDataIndex 0, the HUD's green $1A in palette 14). */
    ram[0x038D] = 0;
    palette_ram[2] = 0x1A;
    CHECK(shows(present(&w, &h), 30, 40, 0x2A) && fill_nes == 0x1A);
    set_mp(4);
    CHECK(shows(present(&w, &h), 0, 4, 0x0A));
    eolis_palette();
    set_mp(40);
    /* The frame's emphasis and greyscale, as the PPU drew the status bar's lines. */
    mask = 0x1E | 0x20 | 0x01;
    const uint32_t *px = present(&w, &h);
    CHECK(shown && shows(px, 30, 40, 0x29));
    CHECK(px[19 * W + BAR + 35] == hw_palette_argb[0x20 | 64] && px[19 * W + BAR + 25] == hw_palette_argb[0x10 | 64]);
    mask = 0x1E;
    /* A fill as black as the empty bar (the screen faded out): nothing. */
    palette_ram[12 + 2] = 0x0F;
    CHECK(untouched(present(&w, &h)));
    palette_ram[12 + 2] = 0x19;
    /* A fill that a step darker would be black (the first stage of a fade, $09): left as it is. */
    palette_ram[12 + 2] = 0x09;
    set_mp(4);
    CHECK(untouched(present(&w, &h)));
    set_mp(40);
    CHECK(shows(present(&w, &h), 30, 40, 0x19));   /* lighter still shows */
    eolis_palette();
}

static void where_it_shows(void) {
    power_on(true);
    int w, h;
    set_mp(40);
    select_spell(THUNDER);
    /* Wherever Seamless put the status bar band. */
    world_x = 84;
    model(seamless);
    for (int y = 17; y <= 22; ++y) for (int i = 0; i <= MAX; ++i) seamless[y * W + BAR + i] = BACKDROP;
    draw_bar(seamless, W, 84 + 24, 40);
    w = W; h = H;
    const uint32_t *px = fax_spell_cost_present(seamless, &w, &h);
    CHECK(bar_x == 84 + 24 && px[19 * W + 84 + 24 + 37] == color(0x29) && px[19 * W + 84 + 24 + 35] == color(0x19));
    world_x = HUD_X;
    /* The engine's own 256-wide picture (Seamless drew nothing): the bar at x 24. */
    for (int i = 0; i < 256 * 240; ++i) native[i] = BACKDROP;
    draw_bar(native, 256, 24, 40);
    int before = presents;
    w = h = 0;
    px = fax_spell_cost_present(NULL, &w, &h);
    CHECK(presents == before + 1 && w == 256 && h == 240 && bar_x == 24 && px != native);
    CHECK(px[19 * 256 + 24 + 36] == color(0x29) && px[19 * 256 + 24 + 35] == color(0x19));
    /* No room for the bar in a narrower picture: nothing. */
    static uint32_t narrow[100 * 240];
    for (int i = 0; i < 100 * 240; ++i) narrow[i] = BACKDROP;
    w = 100; h = 240; world_x = 2;
    CHECK(fax_spell_cost_present(narrow, &w, &h) == narrow && bar_x == -1);
    world_x = HUD_X;
    /* Only while the status bar shows: not on the title, nor with the screen off. */
    hud_up = false;
    CHECK(untouched(present(&w, &h)) && !shown);
    hud_up = true;
    CHECK(!untouched(present(&w, &h)));
    /* Off (the original game): nothing. */
    power_on(false);
    set_mp(40);
    select_spell(THUNDER);
    CHECK(untouched(present(&w, &h)) && !shown && bar_x == -1);
}

static void no_costs(void) {
    power_on(true);
    int w, h;
    set_mp(40);
    select_spell(FIRE);
    cart_size = 14 * 16384;                 /* no bank 14 */
    logged[0] = 0;
    CHECK(untouched(present(&w, &h)) && failed && strstr(logged, "cannot read MAGIC_COSTS"));
    cart_size = sizeof cart;
    CHECK(untouched(present(&w, &h)));      /* off until the next power-on */
    power_on(true);
    set_mp(40);
    select_spell(FIRE);
    CHECK(!untouched(present(&w, &h)));
    /* Not the game's costs (a blank table): off. */
    power_on(true);
    memset(cart + 14 * 16384 + 0x37A9, 0, 5);
    set_mp(40);
    select_spell(FIRE);
    CHECK(untouched(present(&w, &h)) && failed);
}

int main(void) {
    for (int i = 0; i < 512; ++i) hw_palette_argb[i] = 0xFF000000u | (uint32_t)i << 8;   /* a color tells its index */
    registers();
    spans();
    shades();
    each_spell();
    tracks_mp();
    palettes();
    where_it_shows();
    no_costs();
    printf("spell_cost_test: %u checks passed\n", checks);
    return 0;
}
