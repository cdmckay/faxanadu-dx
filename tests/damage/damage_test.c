/* Unit tests for damage numbers (fax_damage.c), without the emulator or a ROM.
 * The feature runs against Block's model of the machine (../block/model.c):
 * the game's own code around its three sites, as the disassembly has it (the
 * weapon's and magic's LDA CurrentSprites_HP,X, SEC, SBC Temp_00, and
 * Player_ReduceHP's SEC, SBC Arg_PlayerHealthDelta_L), the frames' ends, and
 * the picture: a native one and a Seamless canvas, with digit tiles of our
 * own in a cart image's bank 10. */
#define FAX_DAMAGE_LOG(...) ((void)0)
#include "fax_damage.c"
#include "model.h"

/* fax_text.c's test for a box: TextBox_Closed 0 and a box's frame on screen.
 * A screen's tile can leave TextBox_Closed 0 with none (Forepaw's screen 2). */
#define TEXTBOX_CLOSED 0x020Cu
static bool frame_shown;
bool fax_text_box_showing(void) { return ram[TEXTBOX_CLOSED] == 0 && frame_shown; }
static void box(bool open) { ram[TEXTBOX_CLOSED] = !open; frame_shown = open; }

/* RAM the model's game code uses */
#define SPRITE_HP   0x0344u
#define SPRITE_FLAGS 0x02DCu
enum { ZOZURA = 0x47, SUGATA = 0x18, KING_GRIEVE = 0x3C, NO_ROOM = -9999 };
enum { WHITE = 0x30, PINK = 0x25 };

/* ---- the picture ---- */
uint32_t hw_palette_argb[512];
static uint8_t palette_ram[32];
uint32_t cyc_render_color(int entry) { return hw_palette_argb[palette_ram[entry & 31] & 63] | 0xFF000000u; }
uint8_t cyc_render_line_mask(int y) { (void)y; return 0x1E; }   /* no emphasis, no greyscale */
static uint32_t native[256 * 240], world[640 * 360];
const uint32_t *cyc_render_present(int *width, int *height) { *width = 256; *height = 240; return native; }
int cyc_render_native_origin(int width) { return (width - 256) / 2; }
static bool world_on;
static unsigned world_area;
static int room_x[16], room_y[16];
bool fax_world_room_origin(int area, int room, int *x, int *y) {
    if (!world_on || area != (int)world_area || room < 0 || room >= 16 || room_x[room] == NO_ROOM) return false;
    *x = room_x[room]; *y = room_y[room];
    return true;
}
#define BG 0xFF404040u
#define INK_WHITE 0xFFF0F0F0u
#define INK_PINK 0xFFF05898u
#define EDGE 0xFF000000u

/* The cart: bank 10's HUD tiles hold our own digits, each a different glyph in
 * color 3 with one color-1 pixel (as the HUD's 5 has), and bank 14's extents. */
static uint8_t payload[16 * 0x4000];
size_t cyc_mod_copy_cart_payload(uint8_t *out, size_t capacity) {
    if (out && capacity >= sizeof payload) memcpy(out, payload, sizeof payload);
    return sizeof payload;
}
static const uint8_t *tile(unsigned d) { return payload + 10 * 0x4000 + 0x140 + (0x30 + d) * 16; }
static void make_cart(void) {
    memset(payload, 0, sizeof payload);
    for (unsigned d = 0; d < 10; ++d) {
        uint8_t *t = payload + 10 * 0x4000 + 0x140 + (0x30 + d) * 16;
        for (unsigned r = 0; r < 7; ++r) {
            uint8_t row = (uint8_t)(((r + d) % 3 ? 0x82 : 0xFE) ^ (r == d % 7 ? 0x28 : 0));
            t[r] = t[r + 8] = row;
        }
        t[3] |= 0x01 << (d % 4);          /* a color-1 pixel: not ink */
        t[3 + 8] &= (uint8_t)~(0x01 << (d % 4));
    }
}
static bool ink_at(unsigned d, int r, int c) {   /* the tile's color 3 */
    const uint8_t *t = tile(d);
    return r >= 0 && r < 8 && c >= 0 && c < 8 && (t[r] >> (7 - c) & t[r + 8] >> (7 - c) & 1);
}
/* What the number at first-glyph (x, y) puts at (px, py): 2 ink, 1 outline, 0 nothing. */
static int expected(unsigned amount, int x, int y, int px, int py) {
    char text[4];
    int n = snprintf(text, sizeof text, "%u", amount);
    bool near = false;
    for (int d = 0; d < n; ++d)
        for (int dr = -1; dr <= 1; ++dr)
            for (int dc = -1; dc <= 1; ++dc) {
                bool hit = ink_at((unsigned)(text[d] - '0'), py - y + dr, px - x - d * 8 + dc);
                if (hit && !dr && !dc) return 2;
                near |= hit;
            }
    return near ? 1 : 0;
}

/* ---- the game's code ---- */
static void place(unsigned slot, unsigned entity, uint8_t x, uint8_t y, uint8_t hp) {
    ram[CURRENT_SPRITES_ENTITIES + slot] = (uint8_t)entity;
    ram[CURRENT_SPRITES_XPOS + slot] = x;
    ram[CURRENT_SPRITES_YPOS + slot] = y;
    ram[SPRITE_HP + slot] = hp;
}
/* @_updateSpriteHP ($8891) or @_reduceEnemyHP ($81FD), X the sprite: the
 * site, then LDA CurrentSprites_HP,X, SEC, SBC Temp_00, STA. */
static void enemy_takes(uint16_t site, const char *id, unsigned slot, uint8_t damage) {
    ram[TEMP_00] = damage;
    cpu.x = (uint8_t)slot;
    CHECK(SITES(site, id));
    ram[SPRITE_HP + slot] = (uint8_t)(ram[SPRITE_HP + slot] - damage);
}
static void sword(unsigned slot, uint8_t damage) { enemy_takes(WEAPON_REDUCE_HP, "faxanadu.damage-numbers.weapon", slot, damage); }
static void spell(unsigned slot, uint8_t damage) { enemy_takes(MAGIC_REDUCE_HP, "faxanadu.damage-numbers.magic", slot, damage); }
/* Player_ReduceHP: LDA Player_HP_L, the site at SEC, SBC Arg_PlayerHealthDelta_L, ... */
static void hero_takes(uint8_t whole, uint8_t fraction) {
    ram[HEALTH_DELTA_U] = whole;
    ram[HEALTH_DELTA_L] = fraction;
    CHECK(SITES(HERO_REDUCE_HP, "faxanadu.damage-numbers.hero"));
    unsigned hp = ram[PLAYER_HP_U] * 256u + ram[0x0432], delta = whole * 256u + fraction;
    hp = hp > delta ? hp - delta : 0;
    ram[PLAYER_HP_U] = (uint8_t)(hp >> 8); ram[0x0432] = (uint8_t)hp;
}
static void frames(unsigned n) { while (n--) fax_damage_frame_end(); }

static void setup(void) {
    model_reset();
    memset(ram, 0, sizeof ram);
    cpu.s = 0xF0;
    isolated_scope = false;
    mapped = 14;
    model_activate("faxanadu.damage-numbers");
    ram[CURRENT_ROM_BANK] = 14;
    ram[AREA_CURRENT_AREA] = 0; ram[AREA_CURRENT_SCREEN] = 4;
    box(false); ram[SCREEN_SCROLL_DIRECTION] = 0xFF;
    ram[PLAYER_HP_U] = 80;
    ram[PLAYER_POS_X] = 192; ram[PLAYER_POS_Y] = 144;
    for (unsigned s = 0; s < SLOTS; ++s) ram[CURRENT_SPRITES_ENTITIES + s] = 0xFF;
    world_on = false;
    for (int r = 0; r < 16; ++r) room_x[r] = room_y[r] = NO_ROOM;
}

/* ---- the tests ---- */

static void the_rules(void) {
    CHECK(fax_damage_rise(0) == 0 && fax_damage_rise(2) == 0 && fax_damage_rise(3) == 1);
    CHECK(fax_damage_rise(45) == 15 && fax_damage_rise(48) == 16);    /* about 16 pixels in about 3/4 s */
    CHECK(fax_damage_alpha(0) == 255 && fax_damage_alpha(FAX_DAMAGE_LIFE - FAX_DAMAGE_FADE - 1) == 255);
    for (unsigned a = FAX_DAMAGE_LIFE - FAX_DAMAGE_FADE; a < FAX_DAMAGE_LIFE; ++a)
        CHECK(fax_damage_alpha(a) < fax_damage_alpha(a - 1) && fax_damage_alpha(a) > 0);
    CHECK(fax_damage_alpha(FAX_DAMAGE_LIFE) == 0);
    CHECK(fax_damage_hero_amount(10, 0) == 10 && fax_damage_hero_amount(2, 0x7F) == 2);
    CHECK(fax_damage_hero_amount(2, 0x80) == 3 && fax_damage_hero_amount(0, 0x01) == 1);
    CHECK(fax_damage_hero_amount(0, 0) == 0 && fax_damage_hero_amount(255, 0xFF) == 255);
}

static void weapon_and_magic_hits(void) {
    setup();
    rom[14][SPRITE_EXTENTS - 0x8000 + ZOZURA * 2] = 16; rom[14][SPRITE_EXTENTS - 0x8000 + ZOZURA * 2 + 1] = 16;
    place(7, ZOZURA, 120, 160, 9);
    sword(7, 8);
    CHECK(count == 1 && seen.weapon == 1 && ram[SPRITE_HP + 7] == 1);
    Number *n = &numbers[0];
    CHECK(n->kind == DEALT && n->amount == 8 && n->area == 0 && n->room == 4 && n->age == 0);
    CHECK(n->x == 120 + 8 - 3 && n->y == 160 - ABOVE && n->lift == 0);           /* centered, 8 above its top */
    CHECK(n->tx == 120 && n->ty == 160 && n->tw == 16 && n->th == 16 && n->slot == 7);
    /* The kill: 1 HP left, the whole hit shows. */
    sword(7, 8);
    CHECK(count == 2 && numbers[1].amount == 8 && ram[SPRITE_HP + 7] == 0xF9);
    /* Magic: the spell's damage; one the enemy shrugs off (0) shows nothing. */
    setup();
    place(6, ZOZURA, 60, 160, 9);
    spell(6, 0);
    CHECK(count == 0 && seen.magic == 1 && seen.nothing == 1);
    spell(6, 6);
    CHECK(count == 1 && numbers[0].amount == 6 && numbers[0].kind == DEALT && seen.magic == 2);
    /* A wide enemy's box, from its entity's extents: centered over all of it. */
    setup();
    rom[14][SPRITE_EXTENTS - 0x8000 + KING_GRIEVE * 2] = 64; rom[14][SPRITE_EXTENTS - 0x8000 + KING_GRIEVE * 2 + 1] = 16;
    place(3, KING_GRIEVE, 100, 80, 200);
    sword(3, 20);
    CHECK(numbers[0].x == 100 + 32 - 7 && numbers[0].tw == 64 && numbers[0].th == 16 && numbers[0].amount == 20);
    /* No extents to read (an entity past the table, or bank 14 not mapped): 16 by 16. */
    place(2, 0x70, 100, 80, 200);
    sword(2, 9);
    CHECK(numbers[1].tw == 16 && numbers[1].th == 16 && numbers[1].x == 100 + 8 - 3);
    ram[CURRENT_ROM_BANK] = 12;
    sword(3, 9);
    CHECK(numbers[2].tw == 16 && numbers[2].x == 100 + 8 - 3);
    /* Three digits. */
    sword(3, 200);
    CHECK(numbers[3].amount == 200 && numbers[3].x == 100 + 8 - 11);
    /* At a screen's edge, kept on it. */
    place(1, ZOZURA, 250, 80, 9);
    sword(1, 8);
    CHECK(numbers[4].x == 256 - 1 - 7);
}

static void the_hero_hit(void) {
    setup();
    hero_takes(7, 0);
    CHECK(count == 1 && seen.hero == 1 && ram[PLAYER_HP_U] == 73);
    Number *n = &numbers[0];
    CHECK(n->kind == TAKEN && n->amount == 7 && n->x == 192 + 8 - 3 && n->y == 144 - ABOVE);
    CHECK(n->tx == 192 && n->ty == 144 && n->tw == 16 && n->th == 32 && n->slot == HERO);
    hero_takes(2, 0x40);                 /* armor's quarter off: 2.25, rounded */
    hero_takes(1, 0xC0);                 /* 1.75 */
    hero_takes(0, 0x20);                 /* an eighth: still a hit */
    CHECK(count == 4 && numbers[1].amount == 2 && numbers[2].amount == 2 && numbers[3].amount == 1);
    hero_takes(0, 0);                    /* nothing taken */
    CHECK(count == 4 && seen.nothing == 1);
    /* A killing hit shows its whole damage. */
    ram[PLAYER_HP_U] = 3; ram[0x0432] = 0;
    hero_takes(10, 0);
    CHECK(count == 5 && numbers[4].amount == 10 && ram[PLAYER_HP_U] == 0);
}

static void not_in_isolation_or_off(void) {
    setup();
    place(7, ZOZURA, 120, 160, 9);
    isolated_scope = true;                /* Seamless's neighboring rooms */
    sword(7, 8);
    hero_takes(7, 0);
    fax_damage_enemy_hit(7, 12);
    CHECK(count == 0 && seen.isolated == 2 && seen.reflected == 0);
    isolated_scope = false;
    model_reset();                        /* the remaster off */
    CHECK(!hook_enabled("faxanadu.damage-numbers.weapon") && !hook_enabled("faxanadu.damage-numbers.hero"));
    CHECK(!hook_enabled("faxanadu.damage-numbers.magic"));
    sword(7, 8);
    hero_takes(7, 0);
    fax_damage_enemy_hit(7, 12);
    CHECK(count == 0);
    fax_damage_frame_end();
    CHECK(fax_damage_present(NULL, &(int){0}, &(int){0}) == NULL);
}

static void sugatas_reflection(void) {
    setup();
    rom[14][SPRITE_EXTENTS - 0x8000 + SUGATA * 2] = 16; rom[14][SPRITE_EXTENTS - 0x8000 + SUGATA * 2 + 1] = 32;
    place(6, SUGATA, 120, 128, 22);
    fax_damage_enemy_hit(6, 12);
    CHECK(count == 1 && seen.reflected == 1 && numbers[0].amount == 12 && numbers[0].kind == DEALT);
    CHECK(numbers[0].x == 120 + 8 - 7 && numbers[0].y == 128 - ABOVE && numbers[0].th == 32);
}

static void the_float(void) {
    setup();
    place(7, ZOZURA, 120, 160, 60);
    sword(7, 8);
    for (unsigned f = 1; f < FAX_DAMAGE_LIFE; ++f) {
        frames(1);
        CHECK(count == 1 && numbers[0].age == f && top_of(&numbers[0]) == 152 - (int)f / 3 && numbers[0].x == 125);
    }
    CHECK(top_of(&numbers[0]) == 152 - 19);
    frames(1);
    CHECK(count == 0);                   /* gone after FAX_DAMAGE_LIFE frames */
    /* Held while a box or menu is open, or the game is paused; not while a
     * screen's tile leaves TextBox_Closed 0 with no box. */
    sword(7, 8);
    frames(5);
    box(true);
    frames(30);
    CHECK(numbers[0].age == 5);
    box(false); ram[GAME_PAUSED_STATE] = 0x81;   /* $81 once the sound engine has muted */
    frames(30);
    CHECK(numbers[0].age == 5);
    ram[GAME_PAUSED_STATE] = 0;
    frames(1);
    CHECK(numbers[0].age == 6);
    ram[TEXTBOX_CLOSED] = 0;
    frames(1);
    CHECK(numbers[0].age == 7);
    box(false);
    /* Another room of the area keeps it (Seamless shows it there); another area does not. */
    ram[AREA_CURRENT_SCREEN] = 3;
    frames(1);
    CHECK(count == 1 && numbers[0].age == 8);
    ram[AREA_CURRENT_AREA] = 4;          /* into a building */
    frames(1);
    CHECK(count == 0);
}

static bool crowded(void) {
    for (unsigned i = 0; i < count; ++i)
        for (unsigned j = i + 1; j < count; ++j)
            if (crowds(&numbers[i], &numbers[j])) return true;
    return false;
}

static void never_on_top_of_each_other(void) {
    enum { SPACED = GLYPH_H + 2 + GAP };
    setup();
    place(7, ZOZURA, 120, 160, 200);
    place(6, ZOZURA, 116, 160, 200);
    /* Two in one frame (one swing, both Zozura): the newer stays at its hit, the older moves up. */
    sword(7, 8);
    sword(6, 8);
    CHECK(count == 2 && seen.crowded == 1);
    CHECK(numbers[1].lift == 0 && top_of(&numbers[1]) == 152 && top_of(&numbers[0]) == 152 - SPACED);
    for (unsigned f = 0; count == 2; ++f) {   /* apart every frame, as both rise at one pace */
        CHECK(!crowded());
        frames(1);
    }
    /* Six frames apart at one spot: the older, risen 2, moves up the rest of the way. */
    setup();
    place(7, ZOZURA, 120, 160, 200);
    sword(7, 8);
    frames(6);
    sword(7, 8);
    CHECK(top_of(&numbers[1]) == 152 && top_of(&numbers[0]) == 152 - SPACED && numbers[0].lift == SPACED - 2);
    /* A third: the second moves up, and the first above it. */
    frames(4);
    sword(7, 8);
    CHECK(count == 3 && !crowded() && top_of(&numbers[2]) == 152);
    CHECK(top_of(&numbers[1]) == 152 - SPACED && top_of(&numbers[0]) == 152 - 2 * SPACED);
    while (count) { CHECK(!crowded()); frames(1); }
    /* One already lower than the new one (an enemy's over the hero's head) moves down. */
    setup();
    place(5, ZOZURA, 186, 150, 200);     /* its number's top at 142 */
    sword(5, 8);
    ram[PLAYER_POS_Y] = 146;             /* his, at 138: just above */
    hero_takes(7, 0);
    CHECK(top_of(&numbers[1]) == 138 && top_of(&numbers[0]) == 138 + SPACED && numbers[0].lift < 0 && !crowded());
    /* Side by side, or in different rooms, they leave each other alone. */
    setup();
    place(7, ZOZURA, 100, 160, 200);
    place(6, ZOZURA, 60, 160, 200);
    sword(7, 8);
    sword(6, 8);
    CHECK(numbers[0].lift == 0 && numbers[1].lift == 0 && seen.crowded == 0);
    ram[AREA_CURRENT_SCREEN] = 5;
    sword(7, 8);
    CHECK(numbers[0].lift == 0 && numbers[2].lift == 0 && seen.crowded == 0);
    /* At most FAX_DAMAGE_MAX: a new one takes the oldest's place. */
    setup();
    place(7, ZOZURA, 120, 160, 200);
    for (unsigned i = 1; i <= FAX_DAMAGE_MAX + 1; ++i) sword(7, (uint8_t)i);
    CHECK(count == FAX_DAMAGE_MAX && numbers[0].amount == 2 && numbers[FAX_DAMAGE_MAX - 1].amount == FAX_DAMAGE_MAX + 1);
}

/* ---- the picture ---- */

static void colors(void) {
    for (int e = 0; e < 512; ++e) hw_palette_argb[e] = 0xFF101010u;
    hw_palette_argb[WHITE] = INK_WHITE; hw_palette_argb[PINK] = INK_PINK; hw_palette_argb[BLACK] = EDGE;
    memset(palette_ram, 0x0F, sizeof palette_ram);
    palette_ram[16 + 4 + 1] = 0x00; palette_ram[16 + 4 + 2] = PINK; palette_ram[16 + 4 + 3] = WHITE;   /* the shield's */
}
static void fill(uint32_t *px, size_t n) { for (size_t i = 0; i < n; ++i) px[i] = BG; }
static uint32_t blended(uint32_t over, unsigned alpha) { return blend(BG, over, alpha); }

/* The picture `got` (w wide) is `under` but for each number: ink, outline,
 * blended at its alpha, at `at` (its first glyph's top-left), clipped to
 * rows from `top` and columns [left, right). */
typedef struct { unsigned amount; int x, y; uint32_t ink; unsigned alpha; } Drawn;
static void same_but(const uint32_t *got, int w, int h, const Drawn *d, unsigned n, int top, int left, int right) {
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint32_t want = BG;
            for (unsigned i = 0; i < n; ++i) {
                int v = y >= top && x >= left && x < right ? expected(d[i].amount, d[i].x, d[i].y, x, y) : 0;
                if (v) want = blended(v == 2 ? d[i].ink : EDGE, d[i].alpha);
            }
            if (got[(size_t)y * w + x] != want) {
                fprintf(stderr, "FAIL pixel (%d, %d): %08X, expected %08X\n", x, y, got[(size_t)y * w + x], want);
                exit(1);
            }
            ++checks;
        }
}

static void the_native_picture(void) {
    setup();
    colors();
    fill(native, 256 * 240);
    loaded = false;
    place(7, ZOZURA, 120, 160, 200);
    sword(7, 8);
    hero_takes(7, 0);
    sword(7, 128);
    frames(9);
    int w = 0, h = 0;
    const uint32_t *got = fax_damage_present(NULL, &w, &h);
    CHECK(got && got != native && w == 256 && h == 240 && loaded);
    Drawn d[3] = {
        { 8, 125, HUD + top_of(&numbers[0]), INK_WHITE, 255 },
        { 7, 197, HUD + top_of(&numbers[1]), INK_PINK, 255 },
        { 128, 120 + 8 - 11, HUD + top_of(&numbers[2]), INK_WHITE, 255 },
    };
    same_but(got, w, h, d, 3, HUD, 0, 256);
    for (unsigned i = 0; i < 3; ++i) CHECK(drawn[i].shown && drawn[i].x == d[i].x && drawn[i].y == d[i].y);
    for (unsigned i = 0; i < 256 * 240; ++i) CHECK(native[i] == BG);   /* the engine's own picture is untouched */
    /* Fading. */
    frames(FAX_DAMAGE_LIFE - FAX_DAMAGE_FADE + 3 - 9);
    got = fax_damage_present(NULL, &w, &h);
    unsigned alpha = fax_damage_alpha(numbers[0].age);
    CHECK(alpha > 0 && alpha < 255);
    for (unsigned i = 0; i < 3; ++i) { d[i].y = HUD + top_of(&numbers[i]); d[i].alpha = alpha; }
    same_but(got, w, h, d, 3, HUD, 0, 256);
    /* None while a box is open or the screen scrolls, or in another room;
     * a screen's tile in TextBox_Closed is no box. */
    ram[TEXTBOX_CLOSED] = 0;
    CHECK(fax_damage_present(NULL, &w, &h) != NULL && drawn[0].shown);
    box(true);
    CHECK(fax_damage_present(NULL, &w, &h) == NULL && !drawn[0].shown);
    box(false); ram[SCREEN_SCROLL_DIRECTION] = 1;
    CHECK(fax_damage_present(NULL, &w, &h) == NULL);
    ram[SCREEN_SCROLL_DIRECTION] = 0xFF; ram[AREA_CURRENT_SCREEN] = 3;
    CHECK(fax_damage_present(NULL, &w, &h) == NULL);
    /* Never over the HUD. */
    setup();
    place(7, ZOZURA, 120, 4, 200);       /* its number's top 4 above the room */
    sword(7, 8);
    got = fax_damage_present(NULL, &w, &h);
    Drawn top = { 8, 125, HUD - 4, INK_WHITE, 255 };
    same_but(got, w, h, &top, 1, HUD, 0, 256);
    /* While he lies dead (HP 0), below the strip where the banner says what killed him. */
    setup();
    ram[PLAYER_POS_Y] = 8;
    hero_takes(80, 0);
    CHECK(ram[PLAYER_HP_U] == 0);
    got = fax_damage_present(NULL, &w, &h);
    int floor_y = HUD + BANNER_TOP + BANNER_H + 1;
    CHECK(drawn[0].y == floor_y);
    Drawn dead = { 80, 192 + 8 - 7, floor_y, INK_PINK, 255 };
    same_but(got, w, h, &dead, 1, HUD, 0, 256);
    for (int y = HUD + BANNER_TOP; y < HUD + BANNER_TOP + BANNER_H; ++y)
        for (int x = 0; x < 256; ++x) CHECK(got[y * 256 + x] == BG);
    /* The digits cannot be read: off, without drawing. */
    setup();
    loaded = false;
    memset(payload + 10 * 0x4000, 0, 0x4000);
    sword(7, 8);
    CHECK(fax_damage_present(NULL, &w, &h) == NULL && failed);
    frames(1);
    CHECK(fax_damage_present(NULL, &w, &h) == NULL);
    make_cart();
}

static void the_seamless_picture(void) {
    enum { W = 640, H = 360 };
    setup();
    colors();
    fill(world, W * H);
    world_on = true; world_area = 0;
    room_x[4] = -70; room_y[4] = 60;     /* the active room, the camera on the hero */
    room_x[3] = 186; room_y[3] = 60;     /* its right neighbor */
    place(7, ZOZURA, 120, 160, 200);
    sword(7, 8);
    ram[AREA_CURRENT_SCREEN] = 3;        /* across the seam: the number stays where it landed */
    ram[PLAYER_POS_X] = 4; ram[PLAYER_POS_Y] = 144;
    hero_takes(5, 0);
    frames(4);
    int w = W, h = H;
    const uint32_t *got = fax_damage_present(world, &w, &h);
    CHECK(got && got != world && w == W && h == H);
    Drawn d[2] = { { 8, -70 + 125, 60 + top_of(&numbers[0]), INK_WHITE, 255 },
                   { 5, 186 + 9, 60 + top_of(&numbers[1]), INK_PINK, 255 } };
    same_but(got, w, h, d, 2, HUD, 0, W);
    CHECK(drawn[0].x == d[0].x && drawn[0].y == d[0].y && drawn[1].x == d[1].x);
    /* A room off the map, or a picture without the world: nothing. */
    room_x[4] = room_x[3] = NO_ROOM;
    CHECK(fax_damage_present(world, &w, &h) == world);
    world_on = false;
    CHECK(fax_damage_present(world, &w, &h) == world);
}

static void the_record(void) {
    setup();
    place(7, ZOZURA, 120, 160, 200);
    sword(7, 8);
    frames(2);
    sword(7, 99);
    hero_takes(3, 0);
    frames(5);
    Number before[FAX_DAMAGE_MAX];
    unsigned had = count;
    memcpy(before, numbers, sizeof before);
    uint8_t buf[512];
    int len = model_state_get("faxanadu.damage-numbers", buf, sizeof buf);
    CHECK(len == 2 + 3 * ENTRY && buf[0] == VERSION && buf[1] == 3);
    frames(FAX_DAMAGE_LIFE);
    CHECK(count == 0);
    CHECK(model_state_set("faxanadu.damage-numbers", buf, len));
    CHECK(count == had && !memcmp(before, numbers, had * sizeof *numbers));
    CHECK(numbers[0].lift != 0);         /* the lift too */
    /* Saved without the feature: none. Malformed: refused. */
    CHECK(model_state_set("faxanadu.damage-numbers", buf, 0) && count == 0);
    uint8_t bad[512];
    memcpy(bad, buf, (size_t)len); bad[0] = 2;
    CHECK(!model_state_set("faxanadu.damage-numbers", bad, len));
    CHECK(!model_state_set("faxanadu.damage-numbers", buf, len - 1));
    memcpy(bad, buf, (size_t)len); bad[2] = 2;                                /* a kind */
    CHECK(!model_state_set("faxanadu.damage-numbers", bad, len));
    memcpy(bad, buf, (size_t)len); bad[3] = 0;                                /* an amount */
    CHECK(!model_state_set("faxanadu.damage-numbers", bad, len));
    memcpy(bad, buf, (size_t)len); bad[6] = FAX_DAMAGE_LIFE;                  /* an age */
    CHECK(!model_state_set("faxanadu.damage-numbers", bad, len));
    memcpy(bad, buf, (size_t)len); bad[2 + 15] = 8;                           /* a slot */
    CHECK(!model_state_set("faxanadu.damage-numbers", bad, len));
    memcpy(bad, buf, (size_t)len); bad[1] = FAX_DAMAGE_MAX + 1;
    CHECK(!model_state_set("faxanadu.damage-numbers", bad, len));
    /* Loaded with the remaster off: none. */
    model_reset();
    CHECK(model_state_set("faxanadu.damage-numbers", buf, len) && count == 0);
}

int main(void) {
    make_cart();
    the_rules();
    weapon_and_magic_hits();
    the_hero_hit();
    not_in_isolation_or_off();
    sugatas_reflection();
    the_float();
    never_on_top_of_each_other();
    the_native_picture();
    the_seamless_picture();
    the_record();
    printf("damage_test: %u checks passed\n", checks);
    return 0;
}
