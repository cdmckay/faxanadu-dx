/* Holding on to ladders. USA 57dd23d1, chipx86 byte-exact disassembly.
 * In the original a hit on a ladder knocks the hero off it (the 40th
 * Anniversary Edition's "knocked down while climbing a ladder"): for the three
 * updates of knockback, Player_CheckHandleJump ($E174) hands the update to
 * Player_HandleKnockback ($E153), which sets Player_Flags to his facing bit
 * alone, dropping can climb, climbing and the rest, and pushes him 8 pixels an
 * update away from the hit (Player_TryMoveLeft or Right, with
 * Player_UpdatePosFromKnockback's speed). 24 pixels takes him clear of the
 * ladder, and he falls. Only a wall behind him keeps him on it.
 *
 * The hook at Player_HandleKnockback's entry returns at once, so he keeps his
 * place and his hold, when he is on a ladder in the air: can climb, not
 * jumping, and no ground under him (the game's own down check,
 * Player_CheckIfPassable with X = 3, in a scratch copy of the machine). The
 * hit still takes its HP and gives its invincibility, he turns to face it as
 * the hit set, and the d-pad does nothing for those three updates, as in any
 * knockback; Up and Down still climb. On the ground, in front of a ladder or
 * not, knockback is the game's. So is its first update in a jump past a
 * ladder, which ends the jump; if that leaves him in front of the ladder, he
 * holds on from the next. See docs/GAME_INTERNALS.md, "Ladders". */
#include "fax_ladder_hold.h"
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

/* Bank 15 */
/* Player_HandleKnockback: LDA Player_Flags, AND #$40, PHA, EOR #$40,
 * STA Player_Flags, JSR Player_KnockbackHoriz */
#define HANDLE_KNOCKBACK  0xE153u
#define CHECK_IF_PASSABLE 0xE6C8u  /* Player_CheckIfPassable: X = 0 left, 1 right, 2 up, 3 down; A = 0 passable */
/* RAM */
#define PLAYER_FLAGS      0x00A4u
#define JUMPING           0x01u    /* Player_Flags */
#define CAN_CLIMB         0x08u
#define DOWN              3u

static bool enabled;
static unsigned held;              /* knockback updates skipped on a ladder */

/* Whether the block under his feet is passable, by the game's own check. */
static bool in_the_air(void) {
    CycModRegs regs;
    cyc_mod_regs(&regs);
    if (!cyc_mod_isolate_begin()) return false;
    regs.x = DOWN;
    bool air = cyc_mod_call(CHECK_IF_PASSABLE, &regs) && regs.a == 0;
    cyc_mod_isolate_end();
    return air;
}

static int knockback_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) return 0;
    uint8_t flags = cyc_mod_peek(PLAYER_FLAGS);
    if (!(flags & CAN_CLIMB) || (flags & JUMPING) || !in_the_air()) return 0;
    ++held;
    return 1;
}

static const char *const hook_id = "faxanadu.ladder-hold.knockback";
static void deactivate(void) {
    enabled = false;
    held = 0;
    nes_mod_set_function_hook_enabled(hook_id, 0);
}
static void activate(void) {
    enabled = true;
    nes_mod_set_function_hook_enabled(hook_id, 1);
    nes_mod_set_local_only("Holding on to ladders", 1);
}

#ifdef CYC_WITH_SDL
static void ladder_hold_state(int id, const char *line) {
    (void)line;
    char fields[64];
    snprintf(fields, sizeof fields, "\"enabled\":%s,\"held\":%u", enabled ? "true" : "false", held);
    cyc_tcp_ok(id, fields);
}
void fax_ladder_hold_tcp_setup(void) {
    cyc_tcp_register("ladder_hold_state", "Holding on to ladders: knockback updates skipped on a ladder",
                     ladder_hold_state);
}
#else
void fax_ladder_hold_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_ladder_hold) {
    if (!nes_mod_register_function_entry_plugin(hook_id, HANDLE_KNOCKBACK, knockback_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.ladder-hold", activate)) abort();
}
