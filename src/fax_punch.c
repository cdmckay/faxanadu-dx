/* Punches. USA 57dd23d1, chipx86 byte-exact disassembly.
 * Before his first weapon the hero swings bare-handed (Player_CheckHandleAttack
 * checks only climbing), and nothing lands: Player_HitSpriteWithWeapon ($8804,
 * bank 14) loads Player_CurWeapon and returns on BMI when it is $FF, no
 * weapon, and Player_DrawWeapon ($B7D6) returns the same way before it places
 * the weapon and loads its reach. A new game starts unarmed, and a weapon in
 * hand can't be sold or taken off, so this is only ever before the first.
 *
 * With the remaster a punch is a weapon of its own, the fist: 2 damage, half
 * the Hand Dagger's 4, the Glove adding 1, half its 2, and the Hand Dagger's
 * reach as the remaster serves it (the Taller dagger's, fax_dagger.c). Three
 * hooks in bank 14, none shared:
 *
 *   - $B7DE, Player_DrawWeapon's BMI RETURN_B7D5 after LDA Player_CurWeapon.
 *     Without a weapon, while a swing lasts, it places the fist and loads its
 *     reach as the routine would place and load the Hand Dagger's (which
 *     Player_GetNormalizedWeaponID already stands in for no weapon): the
 *     position from PTR_ARRAY_PRG14__b89f's dagger tables, with or without a
 *     shield, by pose and facing, in 16 bits against the screen; the Y from
 *     PTR_ARRAY_PRG14__b90f's; the reach from MAYBE_WEAPON_RANGES. Off the
 *     screen only the X is this frame's, as for a weapon. Nothing is drawn:
 *     the BMI returns. Outside a swing nothing is placed, since no hit can
 *     land then (Player_HitSpriteWithWeapon checks Player_Flags first) and the
 *     box is read only by Sprites_IsSpriteOutOfWeaponRange.
 *   - $8817, Player_HitSpriteWithWeapon's BMI RETURN_87CA after the same
 *     load: N cleared, so the routine goes on to the enemy check and the
 *     reach test (through Seams lose nothing's 16-bit X, fax_seams.c), and on
 *     a hit to the sound, the hit counter and the damage, with Y $FF from
 *     LDY Player_CurWeapon ($886C).
 *   - $888F, the Glove's STA Temp_00, after ADC WEAPON_TO_GLOVE_STRENGTH,Y,
 *     which for Y $FF read the byte at $89C6, code: A becomes Temp_00 plus
 *     the fist's 1.
 *
 * The fist's strength comes in at @_checkGlove ($8884), where the Pendant fix
 * works out every weapon's damage from WEAPON_STRENGTHS by Y; for the fist it
 * takes FAX_PUNCH_STRENGTH instead of $B8A4, which is a pointer byte. The
 * Pendant's quarter of 2 is 0. From @_updateSpriteHP on, a punch is any hit:
 * the damage number, the kill, its experience and MP. Townsfolk are still
 * never hit (SPRITE_CATEGORIES_BY_ENTITY). Everything is the machine's, so
 * there is nothing to save. See docs/GAME_INTERNALS.md, "Punches". */
#include "fax_punch.h"
#include "fax_dagger.h"
#include "cyc_mod.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#ifdef CYC_WITH_SDL
#include "cyc_tcp.h"
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Bank 14 */
#define DRAW_NO_WEAPON   0xB7DEu   /* Player_DrawWeapon's BMI RETURN_B7D5, after LDA Player_CurWeapon ($B7DB) */
#define HIT_NO_WEAPON    0x8817u   /* Player_HitSpriteWithWeapon's BMI RETURN_87CA, after LDA Player_CurWeapon
                                      ($8814). Falling through: LDX CurrentSpriteIndex, LDY ..., LDA
                                      SPRITE_CATEGORIES_BY_ENTITY,Y */
#define GLOVE_STORE      0x888Fu   /* STA Temp_00, after LDA Temp_00, CLC, ADC WEAPON_TO_GLOVE_STRENGTH,Y ($8889).
                                      Then @_updateSpriteHP ($8891): LDA CurrentSprites_HP,X, SEC, SBC Temp_00 */
#define WEAPON_RANGES    0xB897u   /* MAYBE_WEAPON_RANGES: (X, Y) per weapon, the Hand Dagger's first: 08 04 */
#define WEAPON_X_TABLES  0xB89Fu   /* PTR_ARRAY_PRG14__b89f: per weapon, without and with a shield, the X offset
                                      by pose and facing (left, right). The dagger's: $B8AF, $B8BF */
#define WEAPON_Y_TABLES  0xB90Fu   /* PTR_ARRAY_PRG14__b90f: per weapon, the Y offset by pose. The dagger's: $B917 */
#define ATTACK_POSES     0xB97Bu   /* BYTE_ARRAY_PRG14__b97b: the poses of a swing's phases, 4 5 6 */
/* RAM */
#define TEMP_00          0x0000u   /* the hit's damage, so far */
#define HERO_X           0x009Eu   /* Player_PosX_Block */
#define SCROLL_X         0x009Fu   /* Screen_Maybe_ScrollXCounter: the screen's high byte of X */
#define HERO_Y           0x00A1u   /* Player_PosY */
#define SCROLL_Y         0x00A2u   /* Player_Something_ScrollPosY: the high byte Player_DrawWeapon adds to */
#define PLAYER_FLAGS     0x00A4u
#define ARMOR_INDEX      0x00A7u   /* Player_ArmorLookupIndex: bit 0, a shield */
#define ATTACK_PHASE     0x00AEu   /* Player_AttackPhaseAnimFrame: 0, 1, 2 */
#define WEAPON_X         0x00CEu   /* PlayerPosXPlus10, low byte */
#define WEAPON_X_HIGH    0x00CFu   /* Maybe_Something_WeaponPosX: its high byte */
#define WEAPON_Y         0x00D0u   /* Maybe_Something_WeaponPosY */
#define WEAPON_Y_HIGH    0x00D1u   /* DrawWeapon_Unused_00D1: its high byte, which nothing reads */
#define REACH_X          0x00D2u   /* Maybe_WeaponRange_X */
#define REACH_Y          0x00D3u   /* Maybe_WeaponRange_Y */
/* Player_Flags */
#define JUMPING          0x01u
#define FALLING          0x04u
#define CAN_CLIMB        0x08u
#define CLIMBING         0x10u
#define FACING_RIGHT     0x40u
#define ATTACKING        0x80u
#define SHIELD           0x01u
#define CLIMB_POSE       7u
#define P_N              0x80u
enum { DAGGER = 0 };

/* Where the fist is this frame and how far it reaches: what Player_DrawWeapon
 * stores for the Hand Dagger. On the screen when its X's high byte is the
 * screen's; off it the routine stores only the X. */
typedef struct {
    uint8_t x, x_high, y, y_high, reach_x, reach_y;
    bool on_screen;
} FistBox;

static bool enabled;
static unsigned placed, gloved;     /* frames the fist was placed, Glove bonuses given (live game) */
static FistBox last;

static uint8_t peek(uint16_t addr) { return cyc_mod_peek(addr); }
static uint16_t peek16(uint16_t addr) { return (uint16_t)(peek(addr) | peek((uint16_t)(addr + 1)) << 8); }

/* Player_IsClimbing, as the game has it (fax_falling.c's climbing()). */
static bool climbing(uint8_t flags, uint8_t x) {
    return (flags & CAN_CLIMB) && ((x & 0x0F) == 0 || !(flags & FALLING)) && (flags & CLIMBING);
}

/* The pose Maybe_Player_CalcAnimFrame ($B927) gives an attacking hero: the
 * jump's branch and the walk's both go to the swing's, by its phase, and only
 * a climbing hero, who can't start a swing, gets the climb's. */
static uint8_t attack_pose(uint8_t flags, uint8_t x, uint8_t phase) {
    if (!(flags & JUMPING) && climbing(flags, x)) return CLIMB_POSE;
    return peek((uint16_t)(ATTACK_POSES + phase));
}

/* Player_DrawWeapon's arithmetic for the Hand Dagger, in an attacking
 * hero's pose. Bank 14 is mapped when it runs, so the tables are the ROM's;
 * the Y reach is the Taller dagger's served byte, which a peek would not see. */
static FistBox fist_box(void) {
    uint8_t flags = peek(PLAYER_FLAGS), x = peek(HERO_X), scroll = peek(SCROLL_X);
    uint8_t pose = attack_pose(flags, x, peek(ATTACK_PHASE));
    unsigned table = (DAGGER << 1 | (peek(ARMOR_INDEX) & SHIELD)) << 1;   /* ASL, ORA, ASL */
    uint16_t xs = peek16((uint16_t)(WEAPON_X_TABLES + table));
    int8_t offset = (int8_t)peek((uint16_t)(xs + pose * 2 + (flags & FACING_RIGHT ? 1 : 0)));
    unsigned sum = (unsigned)x + (uint8_t)offset;                       /* Player_CalcValueAndFFForNeg's */
    FistBox box = last;
    box.x = (uint8_t)sum;
    box.x_high = (uint8_t)(scroll + (offset < 0 ? 0xFF : 0) + (sum >> 8));
    box.on_screen = box.x_high == scroll;
    if (!box.on_screen) return box;
    uint16_t ys = peek16(WEAPON_Y_TABLES + DAGGER * 2);
    unsigned y = (unsigned)peek(HERO_Y) + peek((uint16_t)(ys + pose));
    box.y = (uint8_t)y;
    box.y_high = (uint8_t)(peek(SCROLL_Y) + (y >> 8));
    box.reach_x = peek(WEAPON_RANGES + DAGGER * 2);
    box.reach_y = fax_dagger_reach_y();
    return box;
}

/* Player_DrawWeapon's BMI after LDA Player_CurWeapon: N set, no weapon. */
static int draw_hook(uint16_t addr) {
    (void)addr;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    if (!(regs.p & P_N) || !(peek(PLAYER_FLAGS) & ATTACKING)) return 0;
    FistBox box = fist_box();
    cyc_mod_poke(WEAPON_X, box.x);
    cyc_mod_poke(WEAPON_X_HIGH, box.x_high);
    if (box.on_screen) {
        cyc_mod_poke(WEAPON_Y, box.y);
        cyc_mod_poke(WEAPON_Y_HIGH, box.y_high);
        cyc_mod_poke(REACH_X, box.reach_x);
        cyc_mod_poke(REACH_Y, box.reach_y);
    }
    if (!cyc_mod_isolated()) {
        ++placed;
        last = box;
    }
    return 0;                       /* the BMI returns: nothing to draw */
}

/* Player_HitSpriteWithWeapon's BMI after the same load: the next instruction
 * (LDX) sets N and Z again, so clearing N alone sends the fist on. */
static int hit_hook(uint16_t addr) {
    (void)addr;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    if (!(regs.p & P_N)) return 0;
    regs.p = (uint8_t)(regs.p & ~P_N);
    cyc_mod_set_regs(&regs);
    return 0;
}

/* The Glove's STA Temp_00, reached only while the Glove runs. Y is still
 * Player_CurWeapon. Nothing reads the ADC's flags: @_updateSpriteHP's LDA and
 * SEC set N, Z and C again before they are read, and nothing reads V. */
static int glove_hook(uint16_t addr) {
    (void)addr;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    if (!(regs.y & FAX_PUNCH_NO_WEAPON)) return 0;
    regs.a = (uint8_t)(peek(TEMP_00) + FAX_PUNCH_GLOVE);
    cyc_mod_set_regs(&regs);
    if (!cyc_mod_isolated()) ++gloved;
    return 0;
}

static const char *const hook_ids[] = {
    "faxanadu.punches.draw", "faxanadu.punches.hit", "faxanadu.punches.glove",
};
enum { HOOKS = sizeof hook_ids / sizeof hook_ids[0] };

static void deactivate(void) {
    enabled = false;
    placed = gloved = 0;
    last = (FistBox){0};
    for (unsigned i = 0; i < HOOKS; ++i) nes_mod_set_function_hook_enabled(hook_ids[i], 0);
}
static void activate(void) {
    enabled = true;
    for (unsigned i = 0; i < HOOKS; ++i) nes_mod_set_function_hook_enabled(hook_ids[i], 1);
    nes_mod_set_local_only("Punches", 1);   /* a peer without it would play other rules */
}

#ifdef CYC_WITH_SDL
static void punches_state(int id, const char *line) {
    (void)line;
    char fields[224];
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"placed\":%u,\"gloved\":%u,\"x\":%u,\"x_high\":%u,\"y\":%u,\"reach_x\":%u,"
             "\"reach_y\":%u,\"on_screen\":%s,\"strength\":%u,\"glove\":%u",
             enabled ? "true" : "false", placed, gloved, last.x, last.x_high, last.y, last.reach_x, last.reach_y,
             last.on_screen ? "true" : "false", FAX_PUNCH_STRENGTH, FAX_PUNCH_GLOVE);
    cyc_tcp_ok(id, fields);
}
void fax_punch_tcp_setup(void) {
    cyc_tcp_register("punches_state",
                     "Punches: frames the fist was placed and Glove bonuses given, and the fist's last box",
                     punches_state);
}
#else
void fax_punch_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_punches) {
    if (!nes_mod_register_function_entry_plugin(hook_ids[0], DRAW_NO_WEAPON, draw_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[1], HIT_NO_WEAPON, hit_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[2], GLOVE_STORE, glove_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.punches", activate)) abort();
}
