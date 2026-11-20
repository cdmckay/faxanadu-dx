/* Damage numbers. USA 57dd23d1, chipx86 byte-exact disassembly.
 * Every hit that takes HP shows how much, as a small number in the HUD's own
 * digits that floats up from where the hit landed and fades.
 *
 * The amounts come from three sites, which only observe:
 *   - The hero's weapon: Player_HitSpriteWithWeapon's @_updateSpriteHP
 *     ($8891, bank 14), LDA CurrentSprites_HP,X, SEC, SBC Temp_00. Temp_00
 *     holds the hit's damage (the weapon, the Pendant's quarter, the Glove)
 *     and X the sprite.
 *   - The hero's magic: Player_HitEnemyWithMagic's @_reduceEnemyHP ($81FD,
 *     bank 14), the same code, with the spell's damage after the Magical Rod
 *     and the enemy's defense. A spell the enemy shrugs off leaves 0.
 *   - The hero's HP: Player_ReduceHP (fixed bank), at its SEC ($C091) after
 *     LDA Player_HP_L. Every loss of HP goes through it (an enemy's or a
 *     shot's touch, a shot on the shield, Sugata's flash, the Poison), with
 *     Arg_PlayerHealthDelta, whole HP and 256ths after armor, still to take.
 *   Sugata's reflection (fax_sugata.c) runs @_reduceEnemyHP in a committed
 *   call, where no hook runs, so it reports itself (fax_damage_enemy_hit).
 *   A hit the game stops (iframes, Ointment, Block, a shield that takes all
 *   of a shot) never reaches these sites, and one that takes 0 shows nothing.
 *   A kill shows the hit's whole damage, which may be more than was left.
 *
 * Where: an enemy's number starts centered over the box the weapon checks
 * (CurrentSprites_XPos_Full and _YPos, the top-left, and its entity's
 * MAYBE_SPRITE_EXTENTS, $B407), the hero's over him (16 by 32), just above
 * the top. A number belongs to the room it landed in: with Seamless on it
 * stays put in the world as the camera moves (fax_world_room_origin), and in
 * the original view it shows only on its own screen, and not while the
 * screen scrolls. Numbers in one room never overlap: a new one stays where
 * its hit landed and moves any it would touch out of its way, and as every
 * number rises at the same pace, they stay apart.
 *
 * Look: the digit tiles $30-$39 the HUD draws gold and experience with
 * (bank 10, HUD_TILES_START), their color-3 pixels in sprite palette 1, the
 * shield's ($00 $25 $30 in every area): white ($30) for damage dealt and pink
 * ($25) for damage taken, outlined in black ($0F). They hold still while a
 * box or menu is open or the game is paused, and hide while a box is open.
 * While the hero's HP reads 0 (from a killing hit until he comes back) they
 * keep below the strip under the HUD where the zone names banner tells what
 * killed him. Drawn at present time, after Sugata's band and before the
 * banner.
 *
 * The numbers live in this feature's save-state record. See
 * docs/GAME_INTERNALS.md, "Damage numbers". */
#include "fax_damage.h"
#include "fax_world.h"
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

#ifndef FAX_DAMAGE_LOG
#define FAX_DAMAGE_LOG(...) fprintf(stderr, __VA_ARGS__)
#endif

extern uint32_t hw_palette_argb[512];   /* the host's NES colors, emphasis in bits 6-8 */

/* RAM */
#define TEMP_00                  0x0000u   /* the hit's damage, at both enemy sites */
#define AREA_CURRENT_AREA        0x0024u
#define SCREEN_SCROLL_DIRECTION  0x0054u   /* $FF unless scrolling to the next screen */
#define AREA_CURRENT_SCREEN      0x0063u
#define PLAYER_POS_X             0x009Eu   /* Player_PosX_Block: the hero's left; 16 wide */
#define PLAYER_POS_Y             0x00A1u   /* his top; 32 tall */
#define CURRENT_SPRITES_XPOS     0x00BAu   /* CurrentSprites_XPos_Full: a sprite's box's left, 8 slots */
#define CURRENT_SPRITES_YPOS     0x00C2u   /* its top */
#define CURRENT_ROM_BANK         0x0100u
#define GAME_PAUSED_STATE        0x0120u   /* 1 while Start has the game paused */
#define TEXTBOX_CLOSED           0x020Cu   /* 0 while a text box or the menu is open */
#define CURRENT_SPRITES_ENTITIES 0x02CCu
#define PLAYER_HP_U              0x0431u
#define HEALTH_DELTA_L           0x04BCu   /* Arg_PlayerHealthDelta_L: 256ths of HP */
#define HEALTH_DELTA_U           0x04BDu   /* Arg_PlayerHealthDelta_U: whole HP */
/* Bank 14 */
#define MAGIC_REDUCE_HP   0x81FDu   /* @_reduceEnemyHP: LDA CurrentSprites_HP,X, SEC, SBC Temp_00, STA, BCS */
#define WEAPON_REDUCE_HP  0x8891u   /* @_updateSpriteHP: LDA CurrentSprites_HP,X, SEC, SBC Temp_00, STA, BCS */
#define SPRITE_EXTENTS    0xB407u   /* MAYBE_SPRITE_EXTENTS: width, height, by entity */
/* Fixed bank */
#define HERO_REDUCE_HP    0xC091u   /* Player_ReduceHP's SEC, after LDA Player_HP_L; SBC Arg_PlayerHealthDelta_L */
/* Bank 10 */
#define HUD_TILES_START   0x8140u   /* UI_DrawHUDSprites copies these to PPU $1000: tile t at + t * 16 */

enum { HUD_BANK = 10, GAME_BANK = 14, BANK_SIZE = 16384, DIGIT_0 = 0x30 };   /* the HUD's digits are ASCII tiles */
enum { SHIELD_PALETTE = 1, DEALT_COLOR = 3, TAKEN_COLOR = 2, BLACK = 0x0F };
enum { HUD = 32, ROOM_W = 256, ENTITIES = 0x65, SLOTS = 8, HERO_W = 16, HERO_H = 32 };
enum { CELL = 8, GLYPH_H = 7, ABOVE = 8 };   /* a digit's glyph is 7 by 7; a number starts 8 above the top */
enum { GAP = 2 };                            /* rows between two outlines: one apart, one for the pace's rounding */
enum { BANNER_TOP = 8, BANNER_H = 3 * CELL };  /* the zone names banner's box, below the HUD */
enum { DEALT, TAKEN };

typedef struct {
    uint8_t kind, amount, area, room, age;
    int16_t x, y;            /* the first digit's top-left when it appeared, in its room's pixels */
    int16_t lift;            /* pixels moved up (down if negative) out of newer numbers' way */
    uint8_t tx, ty, tw, th;  /* the box the hit landed on */
} Number;

static bool enabled, failed, loaded;
static Number numbers[FAX_DAMAGE_MAX];   /* oldest first */
static unsigned count;
static struct { unsigned weapon, magic, hero, reflected, nothing, isolated, crowded; } seen;
static struct { bool shown; int x, y; } drawn[FAX_DAMAGE_MAX];   /* where the last picture drew each */
static uint8_t digits[10][16];
static uint32_t *canvas;
static size_t capacity;

static uint8_t q(uint16_t addr) { return cyc_mod_peek(addr); }

/* ---- the rules ---- */

int fax_damage_rise(unsigned age) { return (int)(age / FAX_DAMAGE_STEP); }
unsigned fax_damage_alpha(unsigned age) {
    enum { SOLID = FAX_DAMAGE_LIFE - FAX_DAMAGE_FADE };
    if (age < SOLID) return 255;
    if (age < FAX_DAMAGE_LIFE) return (FAX_DAMAGE_LIFE - age) * 255u / (FAX_DAMAGE_FADE + 1);
    return 0;
}
unsigned fax_damage_hero_amount(uint8_t whole, uint8_t fraction) {
    unsigned n = whole + (fraction >= 0x80);
    if (!n && fraction) n = 1;
    return n > 255 ? 255 : n;
}

static int digit_count(unsigned amount) { return amount >= 100 ? 3 : amount >= 10 ? 2 : 1; }
static int width_of(unsigned amount) { return digit_count(amount) * CELL - 1; }   /* the last column is blank */
static int top_of(const Number *n) { return n->y - n->lift - fax_damage_rise(n->age); }

/* Two numbers in a room whose outlines come within GAP rows of each other. */
static bool crowds(const Number *a, const Number *b) {
    if (a->area != b->area || a->room != b->room) return false;
    int ax = a->x - 1, aw = width_of(a->amount) + 2, bx = b->x - 1, bw = width_of(b->amount) + 2;
    if (ax >= bx + bw || bx >= ax + aw) return false;
    int at = top_of(a) - 1, bt = top_of(b) - 1, h = GLYPH_H + 2;
    return at < bt + h + GAP && bt < at + h + GAP;
}

/* The newest number stays where its hit landed. Each older one, newest
 * first, moves away from any newer one it crowds: up if it was higher (as an
 * older one at the same place is), else down, as far as it takes. */
static void make_room(void) {
    enum { STEP = GLYPH_H + 2 + GAP };
    for (int i = (int)count - 2; i >= 0; --i) {
        Number *o = &numbers[i];
        int dir = 0;
        for (unsigned tries = 0; tries < 2 * FAX_DAMAGE_MAX; ++tries) {
            const Number *in_way = NULL;
            for (unsigned j = (unsigned)i + 1; j < count && !in_way; ++j)
                if (crowds(o, &numbers[j])) in_way = &numbers[j];
            if (!in_way) break;
            if (!dir) { dir = top_of(o) <= top_of(in_way) ? 1 : -1; ++seen.crowded; }
            int to = dir > 0 ? top_of(in_way) - STEP : top_of(in_way) + STEP;
            o->lift = (int16_t)(o->lift + top_of(o) - to);
        }
    }
}

static void spawn(unsigned kind, unsigned amount, int tx, int ty, int tw, int th) {
    if (!amount) { ++seen.nothing; return; }
    if (amount > 255) amount = 255;
    if (count == FAX_DAMAGE_MAX) memmove(numbers, numbers + 1, --count * sizeof *numbers);
    int w = width_of(amount), x = tx + tw / 2 - w / 2;
    if (x < 1) x = 1;
    if (x > ROOM_W - 1 - w) x = ROOM_W - 1 - w;
    numbers[count++] = (Number){ (uint8_t)kind, (uint8_t)amount, q(AREA_CURRENT_AREA), q(AREA_CURRENT_SCREEN), 0,
                                 (int16_t)x, (int16_t)(ty - ABOVE), 0, (uint8_t)tx, (uint8_t)ty, (uint8_t)tw,
                                 (uint8_t)th };
    make_room();
}

/* An enemy in sprite slot `slot` takes `amount`: over its box. */
static void enemy_hit(unsigned slot, unsigned amount) {
    slot &= SLOTS - 1;
    unsigned entity = q((uint16_t)(CURRENT_SPRITES_ENTITIES + slot));
    int w = 16, h = 16;
    if (entity < ENTITIES && q(CURRENT_ROM_BANK) == GAME_BANK) {
        unsigned ew = q((uint16_t)(SPRITE_EXTENTS + entity * 2)), eh = q((uint16_t)(SPRITE_EXTENTS + entity * 2 + 1));
        if (ew && ew <= 64) w = (int)ew;
        if (eh && eh <= 64) h = (int)eh;
    }
    spawn(DEALT, amount, q((uint16_t)(CURRENT_SPRITES_XPOS + slot)), q((uint16_t)(CURRENT_SPRITES_YPOS + slot)), w, h);
}

void fax_damage_enemy_hit(unsigned slot, unsigned amount) {
    if (!enabled || cyc_mod_isolated()) return;
    ++seen.reflected;
    enemy_hit(slot, amount);
}

/* ---- the hooks ---- */

static int enemy_hook(uint16_t addr) {
    if (!enabled) return 0;
    if (cyc_mod_isolated()) { ++seen.isolated; return 0; }   /* Seamless's neighboring rooms */
    CycModRegs regs;
    cyc_mod_regs(&regs);
    if (addr == WEAPON_REDUCE_HP) ++seen.weapon; else ++seen.magic;
    enemy_hit(regs.x, q(TEMP_00));
    return 0;
}

static int hero_hook(uint16_t addr) {
    (void)addr;
    if (!enabled) return 0;
    if (cyc_mod_isolated()) { ++seen.isolated; return 0; }
    ++seen.hero;
    spawn(TAKEN, fax_damage_hero_amount(q(HEALTH_DELTA_U), q(HEALTH_DELTA_L)), q(PLAYER_POS_X), q(PLAYER_POS_Y),
          HERO_W, HERO_H);
    return 0;
}

/* ---- each frame ---- */

/* Held while a box or menu is open or the game is paused; gone at the end of
 * its life or once the hero is in another area (a building, a region's door). */
void fax_damage_frame_end(void) {
    if (!enabled || !count) return;
    unsigned area = q(AREA_CURRENT_AREA), kept = 0;
    bool held = !q(TEXTBOX_CLOSED) || q(GAME_PAUSED_STATE);
    for (unsigned i = 0; i < count; ++i) {
        Number n = numbers[i];
        if (n.area != area) continue;
        if (!held && ++n.age >= FAX_DAMAGE_LIFE) continue;
        numbers[kept++] = n;
    }
    count = kept;
}

/* ---- the picture ---- */

/* The HUD's digit tiles, from the cart: the ones it draws gold with. */
static bool load_digits(void) {
    size_t need = cyc_mod_copy_cart_payload(NULL, 0);
    if (need < (size_t)(HUD_BANK + 1) * BANK_SIZE) return false;
    uint8_t *payload = malloc(need);
    if (!payload) return false;
    bool ok = cyc_mod_copy_cart_payload(payload, need) == need;
    if (ok) memcpy(digits, payload + HUD_BANK * BANK_SIZE + (HUD_TILES_START - 0x8000u) + DIGIT_0 * 16, sizeof digits);
    free(payload);
    for (int d = 0; ok && d < 10; ++d) {   /* each a different glyph in color 3 */
        bool ink = false;
        for (int r = 0; r < 8; ++r) ink |= (digits[d][r] & digits[d][r + 8]) != 0;
        ok = ink && (d == 0 || memcmp(digits[d], digits[d - 1], 16));
    }
    return ok;
}

static uint32_t blend(uint32_t under, uint32_t over, unsigned alpha) {
    if (alpha >= 255) return over;
    uint32_t out = 0xFF000000u;
    for (int shift = 0; shift < 24; shift += 8) {
        unsigned u = under >> shift & 0xFF, o = over >> shift & 0xFF;
        out |= (uint32_t)((o * alpha + u * (255 - alpha) + 127) / 255) << shift;
    }
    return out;
}

typedef struct { uint32_t *px; int w, h, left, right, top; } Canvas;

/* `amount` with its first glyph's top-left at (x, y): each digit's color-3
 * pixels in `ink`, every pixel next to one (diagonals too) in `edge`. Clipped
 * to the canvas's columns [left, right) and rows from `top`. */
static void draw_number(const Canvas *c, int x, int y, unsigned amount, uint32_t ink, uint32_t edge, unsigned alpha) {
    enum { MW = 3 * CELL + 2, MH = CELL + 2 };
    uint8_t mask[MH][MW];
    memset(mask, 0, sizeof mask);
    char text[4];
    int n = snprintf(text, sizeof text, "%u", amount);
    for (int d = 0; d < n && d < 3; ++d) {
        const uint8_t *t = digits[text[d] - '0'];
        for (int r = 0; r < CELL; ++r)
            for (int b = 0; b < CELL; ++b)
                if (t[r] >> (7 - b) & t[r + 8] >> (7 - b) & 1) mask[1 + r][1 + d * CELL + b] = 2;
    }
    for (int r = 0; r < MH; ++r)
        for (int b = 0; b < MW; ++b) {
            if (mask[r][b] != 2) continue;
            for (int dr = -1; dr <= 1; ++dr)
                for (int db = -1; db <= 1; ++db) {
                    int rr = r + dr, bb = b + db;
                    if (rr >= 0 && rr < MH && bb >= 0 && bb < MW && !mask[rr][bb]) mask[rr][bb] = 1;
                }
        }
    for (int r = 0; r < MH; ++r) {
        int py = y - 1 + r;
        if (py < c->top || py < 0 || py >= c->h) continue;
        for (int b = 0; b < MW; ++b) {
            int px = x - 1 + b;
            if (!mask[r][b] || px < c->left || px >= c->right || px < 0 || px >= c->w) continue;
            uint32_t *p = c->px + (size_t)py * (size_t)c->w + (size_t)px;
            *p = blend(*p, mask[r][b] == 2 ? ink : edge, alpha);
        }
    }
}

const uint32_t *fax_damage_present(const uint32_t *picture, int *w, int *h) {
    memset(drawn, 0, sizeof drawn);
    if (!enabled || failed || !count || !q(TEXTBOX_CLOSED)) return picture;
    if (!loaded && !(loaded = load_digits())) {
        FAX_DAMAGE_LOG("[damage numbers] cannot read the HUD's digits; damage numbers are off\n");
        failed = true;
        return picture;
    }
    int width = 0, height = 0, left, right;
    const uint32_t *from = picture;
    bool world = from != NULL;
    if (world) {
        width = *w; height = *h;
        left = 0; right = width;
    } else {
        if (q(SCREEN_SCROLL_DIRECTION) != 0xFF) return picture;   /* the screen slides away under them */
        from = cyc_render_present(&width, &height);
        if (!from) return picture;
        left = cyc_render_native_origin(width); right = left + ROOM_W;
    }
    if (width <= 0 || height <= 0) return picture;
    /* While he lies dead, below the banner's box (as fax_banner.c places it). */
    int hud = width < 256 ? HUD * width / 256 : HUD, floor_y = hud + BANNER_TOP + BANNER_H + 1;
    bool dying = q(PLAYER_HP_U) == 0;
    unsigned area = q(AREA_CURRENT_AREA), screen = q(AREA_CURRENT_SCREEN);
    unsigned mask = cyc_render_line_mask(239);
    uint32_t ink[2] = { cyc_render_color(16 + 4 * SHIELD_PALETTE + DEALT_COLOR),
                        cyc_render_color(16 + 4 * SHIELD_PALETTE + TAKEN_COLOR) };
    uint32_t edge = hw_palette_argb[(BLACK | (mask >> 5) << 6) & 511] | 0xFF000000u;
    Canvas c = { NULL, width, height, left, right, HUD };
    for (unsigned i = 0; i < count; ++i) {
        const Number *n = &numbers[i];
        unsigned alpha = fax_damage_alpha(n->age);
        int ox, oy;
        if (n->area != area || !alpha) continue;
        if (world) {
            if (!fax_world_room_origin(n->area, n->room, &ox, &oy)) continue;
        } else {
            if (n->room != screen) continue;
            ox = left; oy = HUD;
        }
        int x = ox + n->x, y = oy + top_of(n);
        if (dying && y < floor_y) y = floor_y;
        if (!c.px) {
            size_t size = (size_t)width * (size_t)height;
            if (size > capacity) {
                uint32_t *grown = realloc(canvas, size * sizeof *grown);
                if (!grown) return picture;
                canvas = grown; capacity = size;
            }
            memcpy(canvas, from, size * sizeof *canvas);
            c.px = canvas;
        }
        draw_number(&c, x, y, n->amount, ink[n->kind], edge, alpha);
        drawn[i].shown = true; drawn[i].x = x; drawn[i].y = y;
    }
    if (!c.px) return picture;
    *w = width; *h = height;
    return canvas;
}

/* ---- the machine's record ---- */

enum { VERSION = 1, ENTRY = 15, RECORD_MAX = 2 + FAX_DAMAGE_MAX * ENTRY };
static void put16(uint8_t *p, int v) { p[0] = (uint8_t)v; p[1] = (uint8_t)((unsigned)v >> 8); }
static int16_t get16(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }
static int state_get(uint8_t *buf, int cap) {
    int len = 2 + (int)count * ENTRY;
    if (cap < len) return -1;
    buf[0] = VERSION; buf[1] = (uint8_t)count;
    for (unsigned i = 0; i < count; ++i) {
        const Number *n = &numbers[i];
        uint8_t *p = buf + 2 + i * ENTRY;
        p[0] = n->kind; p[1] = n->amount; p[2] = n->area; p[3] = n->room; p[4] = n->age;
        put16(p + 5, n->x); put16(p + 7, n->y); put16(p + 9, n->lift);
        p[11] = n->tx; p[12] = n->ty; p[13] = n->tw; p[14] = n->th;
    }
    return len;
}
static int state_validate(const uint8_t *buf, int len) {
    if (len == 0) return 1;
    if (len < 2 || buf[0] != VERSION || buf[1] > FAX_DAMAGE_MAX || len != 2 + buf[1] * ENTRY) return 0;
    for (unsigned i = 0; i < buf[1]; ++i) {
        const uint8_t *p = buf + 2 + i * ENTRY;
        if (p[0] > TAKEN || !p[1] || p[4] >= FAX_DAMAGE_LIFE) return 0;
    }
    return 1;
}
static int state_set(const uint8_t *buf, int len) {
    if (!state_validate(buf, len)) return 0;
    count = len && enabled ? buf[1] : 0;   /* a state saved with the remaster on, loaded with it off: none */
    for (unsigned i = 0; i < count; ++i) {
        const uint8_t *p = buf + 2 + i * ENTRY;
        numbers[i] = (Number){ p[0], p[1], p[2], p[3], p[4], get16(p + 5), get16(p + 7), get16(p + 9),
                               p[11], p[12], p[13], p[14] };
    }
    return 1;
}

/* ---- TCP ---- */

#ifdef CYC_WITH_SDL
static void damage_state(int id, const char *line) {
    (void)line;
    char out[4096];
    int at = snprintf(out, sizeof out,
                      "\"enabled\":%s,\"digits\":%s,\"live\":%u,\"weapon\":%u,\"magic\":%u,\"hero\":%u,\"reflected\":%u,"
                      "\"nothing\":%u,\"isolated\":%u,\"crowded\":%u,\"numbers\":[",
                      enabled ? "true" : "false", loaded ? "true" : "false", count, seen.weapon, seen.magic, seen.hero,
                      seen.reflected, seen.nothing, seen.isolated, seen.crowded);
    for (unsigned i = 0; i < count && at > 0 && at < (int)sizeof out; ++i) {
        const Number *n = &numbers[i];
        char where[32] = "null";
        if (drawn[i].shown) snprintf(where, sizeof where, "[%d,%d]", drawn[i].x, drawn[i].y);
        at += snprintf(out + at, sizeof out - (size_t)at,
                       "%s{\"kind\":\"%s\",\"amount\":%u,\"area\":%u,\"room\":%u,\"age\":%u,\"x\":%d,\"y\":%d,"
                       "\"w\":%d,\"h\":%d,\"lift\":%d,\"target\":[%u,%u,%u,%u],\"alpha\":%u,\"drawn\":%s}",
                       i ? "," : "", n->kind == TAKEN ? "taken" : "dealt", n->amount, n->area, n->room, n->age, n->x,
                       top_of(n), width_of(n->amount), GLYPH_H, n->lift, n->tx, n->ty, n->tw, n->th,
                       fax_damage_alpha(n->age), where);
    }
    if (at > 0 && at < (int)sizeof out - 2) snprintf(out + at, sizeof out - (size_t)at, "]");
    cyc_tcp_ok(id, out);
}
void fax_damage_tcp_setup(void) {
    cyc_tcp_register("damage_state", "Damage numbers: each number showing (room pixels, and where the last picture "
                     "drew it), and the hits seen", damage_state);
}
#else
void fax_damage_tcp_setup(void) {}
#endif

static const char *const hook_ids[] = {
    "faxanadu.damage-numbers.weapon", "faxanadu.damage-numbers.magic", "faxanadu.damage-numbers.hero" };
static void hooks(int on) {
    for (unsigned i = 0; i < sizeof hook_ids / sizeof hook_ids[0]; ++i) nes_mod_set_function_hook_enabled(hook_ids[i], on);
}
static void deactivate(void) {
    enabled = failed = false;
    count = 0;
    memset(&seen, 0, sizeof seen);
    memset(drawn, 0, sizeof drawn);
    hooks(0);
}
static void activate(void) {
    enabled = true;
    hooks(1);
}

NES_MOD_CONSTRUCTOR(register_damage_numbers) {
    if (!nes_mod_register_function_entry_plugin(hook_ids[0], WEAPON_REDUCE_HP, enemy_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[1], MAGIC_REDUCE_HP, enemy_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[2], HERO_REDUCE_HP, hero_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.damage-numbers", activate) ||
        !nes_mod_register_savestate_hook("faxanadu.damage-numbers", state_get, state_set) ||
        !nes_mod_register_savestate_validator("faxanadu.damage-numbers", state_validate)) abort();
}
