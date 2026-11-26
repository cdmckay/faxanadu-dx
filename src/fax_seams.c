/* Seams lose nothing: slice 0 of combat across Seamless's seams (#166,
 * docs/SEAMLESS-COMBAT.md). USA 57dd23d1, chipx86 byte-exact disassembly.
 *
 * Two rules, both in bank 14's sprite loop (Sprites_UpdateAll, $8000):
 *
 *   - **A neighbouring room's copy touches nothing.** Seamless runs each
 *     neighbour's actors on an isolated copy of the machine, against a hero
 *     projected to that room's edge (fax_world.c's shadow()). His
 *     invincibility there keeps enemies and their shots off him, but
 *     CurrentSprite_CheckHitPlayer ($890A) sends every touch to its
 *     category's handler without looking at it: a coin or bread at the edge
 *     was taken (Player_HandleTouchBreadOrCoin, $8968) and its gold or HP
 *     thrown away with the copy's RAM, and an item there
 *     (Player_HandleTouchItem, $88FB) or a person who talks when touched
 *     (Player_HandleTouchNPC) started a script that waits for frames, which
 *     an isolated call cannot run: the call failed, Seamless dropped the
 *     room's tick, and the room stood still while he stood beside it. A hook
 *     at the routine's entry returns at once in those ticks
 *     (fax_world_ticking()), so the live game is the only place the hero is
 *     touched. The coin waits until he walks into its room, which is then
 *     the live game's.
 *
 *   - **The weapon's reach in 16 bits** (the original's bug). Player_DrawWeapon
 *     ($B7D6) places the weapon in 16 bits, PlayerPosXPlus10 ($CE) low and
 *     Maybe_Something_WeaponPosX ($CF) high, against the screen's
 *     Screen_Maybe_ScrollXCounter ($9F); Sprites_IsSpriteOutOfWeaponRange
 *     ($88CB) then compares only $CE with the sprite's X, in 8 bits. So a
 *     swing whose box reaches past the screen's edge, or wraps past 255 with
 *     the reach added, hits a monster at the other edge: from Eolis $04's x
 *     4 facing left, the Long Sword kills a Zozura at its x 236, 232 pixels
 *     behind him. A hook at the routine's entry works the X test out in 16
 *     bits, and when the 8-bit test would hit only by wrapping round the
 *     screen, returns "out of range" itself, as the X test's failure leaves
 *     it; otherwise the original runs. The Y test can't wrap inside a room
 *     (its 208 lines leave the weapon's box below 256), so it is left alone.
 *
 * Both hooks are part of the remaster: with it off, the original keeps its
 * ghost hit. Nothing is kept but counts for the TCP command, so there is no
 * save-state record. See docs/GAME_INTERNALS.md, "Weapon reach" and
 * "Features and what they use". */
#include "fax_seams.h"
#include "fax_world.h"
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
#define OUT_OF_WEAPON_RANGE   0x88CBu   /* Sprites_IsSpriteOutOfWeaponRange: LDA CurrentSprites_Entities,X, ASL A,
                                           TAY, LDA MAYBE_SPRITE_EXTENTS,Y, CLC, ADC Maybe_WeaponRange_X, STA Temp_00 */
#define CHECK_HIT_PLAYER      0x890Au   /* CurrentSprite_CheckHitPlayer: LDX CurrentSpriteIndex,
                                           LDA CurrentSprites_Entities,X, CMP #$FF, BEQ RETURN_8909, JSR ... */
#define SPRITE_EXTENTS        0xB407u   /* MAYBE_SPRITE_EXTENTS: (EX, EY) per entity */
/* RAM */
#define TEMP_00               0x0000u   /* the X test's limit, EX + the reach */
#define SCROLL_X              0x009Fu   /* Screen_Maybe_ScrollXCounter: the screen's high byte of X */
#define WEAPON_X              0x00CEu   /* PlayerPosXPlus10: the weapon's X, low byte */
#define WEAPON_X_HIGH         0x00CFu   /* Maybe_Something_WeaponPosX: its high byte */
#define WEAPON_RANGE_X        0x00D2u   /* Maybe_WeaponRange_X */
#define ENTITIES              0x02CCu   /* CurrentSprites_Entities, 8 slots */
#define CURRENT_SPRITE_INDEX  0x0378u   /* CurrentSpriteIndex */
#define SPRITE_X              0x037Fu   /* Something_Maybe_WeaponDistanceX: the caller's copy of the sprite's X */
/* Flags */
enum { FLAG_C = 0x01, FLAG_Z = 0x02, FLAG_V = 0x40, FLAG_N = 0x80 };

static bool enabled;
static unsigned skipped;                 /* CheckHitPlayer calls skipped in neighbours' ticks */
static unsigned refused;                 /* weapon matches refused: they hit only by wrapping (live game) */
static int last_weapon_x = 0x7FFF;       /* the last refused one's weapon X, in 16 bits from the screen's left */
static unsigned last_sprite_x, last_entity = 0xFF;

/* The weapon's X in 16 bits from the screen's left edge: its low byte, and
 * its high byte against the screen's (-1 left of the screen, 1 right of it). */
static int weapon_x16(uint8_t low, uint8_t high, uint8_t screen) {
    return (int)(int8_t)(uint8_t)(high - screen) * 256 + low;
}

/* Sprites_IsSpriteOutOfWeaponRange's X test, as the game does it in 8 bits:
 * in range when (weapon x + reach - sprite x) mod 256 < extent + reach. */
static bool hits_8bit(uint8_t weapon_x, uint8_t reach, uint8_t sprite_x, uint8_t extent) {
    return (uint8_t)(weapon_x + reach - sprite_x) < (uint8_t)(extent + reach);
}

/* Whether the 8-bit X test hits only by wrapping round the screen: the same
 * sum in 16 bits is outside 0-255, so the box and the sprite are a screen
 * apart. A sprite's X is always on the screen (0-255). */
static bool wraps(int weapon_x, uint8_t reach, uint8_t sprite_x, uint8_t extent) {
    int d = weapon_x + reach - sprite_x;
    return hits_8bit((uint8_t)weapon_x, reach, sprite_x, extent) && (d < 0 || d > 255);
}

/* Sprites_IsSpriteOutOfWeaponRange's entry. X is the sprite's slot; its X is
 * in Something_Maybe_WeaponDistanceX (Player_HitSpriteWithWeapon stores it
 * just before the JSR). Bank 14 is mapped, so a peek reads the extents the
 * routine would. A refusal leaves what the X test's failure leaves (A the
 * 8-bit difference, Y the entity times 2, Temp_00 the limit, the flags of
 * its SBC and CMP) but with C set: out of range. The one caller, at $8838,
 * reads only C (BCC, else JMP @_return). */
static int reach_hook(uint16_t addr) {
    (void)addr;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    uint8_t entity = cyc_mod_peek((uint16_t)(ENTITIES + (regs.x & 7u)));
    uint8_t index = (uint8_t)(entity << 1);
    uint8_t extent = cyc_mod_peek((uint16_t)(SPRITE_EXTENTS + index));
    uint8_t reach = cyc_mod_peek(WEAPON_RANGE_X), sprite_x = cyc_mod_peek(SPRITE_X);
    int weapon_x = weapon_x16(cyc_mod_peek(WEAPON_X), cyc_mod_peek(WEAPON_X_HIGH), cyc_mod_peek(SCROLL_X));
    if (!wraps(weapon_x, reach, sprite_x, extent)) return 0;   /* the original decides */
    uint8_t limit = (uint8_t)(extent + reach), sum = (uint8_t)(weapon_x + reach);
    uint8_t difference = (uint8_t)(sum - sprite_x), compare = (uint8_t)(difference - limit);
    uint8_t p = (uint8_t)(regs.p & ~(FLAG_C | FLAG_Z | FLAG_V | FLAG_N));
    if ((sum ^ sprite_x) & (sum ^ difference) & 0x80) p |= FLAG_V;      /* SBC's overflow */
    if (compare & 0x80) p |= FLAG_N;                                       /* CMP's N; never Z: difference < limit */
    regs.a = difference;
    regs.y = index;
    regs.p = (uint8_t)(p | FLAG_C);
    cyc_mod_poke(TEMP_00, limit);
    cyc_mod_set_regs(&regs);
    if (!cyc_mod_isolated()) {          /* a neighbour's copy never swings (shadow()), but count only the live game */
        ++refused;
        last_weapon_x = weapon_x;
        last_sprite_x = sprite_x;
        last_entity = entity;
    }
    return 1;
}

/* CurrentSprite_CheckHitPlayer's entry: in a neighbouring room's tick the
 * routine returns at once, with X as its first instruction (LDX
 * CurrentSpriteIndex) leaves it; CurrentSprite_UpdateState, next in the
 * loop, loads X again anyway. Anywhere else the original runs. */
static int touch_hook(uint16_t addr) {
    (void)addr;
    if (!cyc_mod_isolated() || !fax_world_ticking()) return 0;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    regs.x = cyc_mod_peek(CURRENT_SPRITE_INDEX);
    regs.p = (uint8_t)((regs.p & ~(FLAG_Z | FLAG_N)) | (regs.x ? 0 : FLAG_Z) | (regs.x & FLAG_N));
    cyc_mod_set_regs(&regs);
    ++skipped;
    return 1;
}

static const char *const reach_id = "faxanadu.seams.reach";
static const char *const touch_id = "faxanadu.seams.touch";

static void deactivate(void) {
    enabled = false;
    skipped = refused = 0;
    last_weapon_x = 0x7FFF;
    last_sprite_x = 0;
    last_entity = 0xFF;
    nes_mod_set_function_hook_enabled(reach_id, 0);
    nes_mod_set_function_hook_enabled(touch_id, 0);
}
static void activate(void) {
    enabled = true;
    nes_mod_set_function_hook_enabled(reach_id, 1);
    nes_mod_set_function_hook_enabled(touch_id, 1);
    nes_mod_set_local_only("Seams lose nothing", 1);
}

#ifdef CYC_WITH_SDL
static void seams_state(int id, const char *line) {
    (void)line;
    char fields[200];
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"skipped\":%u,\"refused\":%u,\"last_weapon_x\":%d,\"last_sprite_x\":%u,\"last_entity\":%u",
             enabled ? "true" : "false", skipped, refused, last_weapon_x, last_sprite_x, last_entity);
    cyc_tcp_ok(id, fields);
}
void fax_seams_tcp_setup(void) {
    cyc_tcp_register("seams_state",
                     "Seams lose nothing: touches skipped in neighbouring rooms' copies, and weapon hits refused "
                     "for wrapping round the screen (the last one's weapon X in 16 bits, sprite X and entity)",
                     seams_state);
}
#else
void fax_seams_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_seams) {
    if (!nes_mod_register_function_entry_plugin(reach_id, OUT_OF_WEAPON_RANGE, reach_hook) ||
        !nes_mod_register_function_entry_plugin(touch_id, CHECK_HIT_PLAYER, touch_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.seams", activate)) abort();
}
