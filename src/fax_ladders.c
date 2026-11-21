/* Ladder fixes. USA 57dd23d1, chipx86 byte-exact disassembly.
 * Two bugs of the original's ladders (#36):
 *
 * - The top of a ladder. Player_CheckHandleClimbUp ($E301) moves the hero up
 *   whenever the block over his head is passable; it never asks whether he is
 *   still on the ladder. At a ladder's top block, the step that lifts his feet
 *   out of it leaves no climbable block at his head (X + 7, Y) or his feet
 *   (X + 7, Y + $1F), so the next update Player_CheckIfOnLadder clears can
 *   climb and climbing, he falls 8 pixels, and the ladder catches him. With Up
 *   held he bobs for as long as it is held: about 11 updates up, one drop, the
 *   pose flicking out of the climb on every drop.
 *   The hook at the BNE after the step's passable check ($E30C) takes the
 *   branch to the routine's RTS, as a solid block above does, when the step
 *   would take him off the ladder (the game's own Player_CheckIfOnLadder, a
 *   pixel up, in a scratch copy of the machine). It clears climbing, as the
 *   drop did: he stays at the top in the pose the original's catch leaves him
 *   in, and A jumps from there, as it did after a drop. Not while flying (the
 *   Wing Boots with A held use the same routine to rise), nor at the screen's
 *   top row, where the game's own step goes on to the screen above.
 *
 * - The fountain's ladder (the Retranslation's "fountain ladder bugfix"). Trunk
 *   $28's screen event, ScreenEvents_HandlePathToMasconEvent ($EF69), runs from
 *   GameLoop_RunScreenEventHandlers late in the first main-loop pass on the
 *   screen. Once the path to Mascon is open (Quests $20) it clears the
 *   fountain's stone and drops the ladder: blocks written into ScreenBuffer
 *   and the nametable. But GameLoop_UpdatePlayer runs earlier in that pass, and
 *   if it walks him off the screen, Area_ScrollTo has already loaded the next
 *   screen ($27, the only neighbour) into ScreenBuffer: the event then puts
 *   the ladder (column 2, rows 2-8, through $27's floor) and two blocks of air
 *   (row 5 and 6 of column 7, in $27's floor) there instead.
 *   The hook at the handler's entry skips it, as its own "not yet" path does
 *   (CurrentScreen_SpecialEventID $FF, RTS), unless Trunk $28 is the screen
 *   loaded. The event comes back the next time the hero enters $28.
 *
 * chipx86's "bad can-climb ladder check" (LDA #$03 for LDX in
 * Player_HandleVertLadderOrJump) never changes the outcome on this ROM, so
 * there is no fix for it here. See docs/GAME_INTERNALS.md, "Ladders". */
#include "fax_ladders.h"
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
#define CLIMB_UP_BRANCH   0xE30Cu  /* Player_CheckHandleClimbUp's BNE @_return, after LDA Player_Flags,
                                      AND #$DA, STA Player_Flags, LDX #$02, JSR Player_CheckIfPassable:
                                      Z clear = a block over his head */
#define FOUNTAIN_EVENT    0xEF69u  /* ScreenEvents_HandlePathToMasconEvent: LDA Quests, AND #$20, BEQ */
#define CHECK_IF_ON_LADDER 0xE752u /* Player_CheckIfOnLadder: sets or clears can climb from the blocks at
                                      his head and feet; clears climbing with it */
/* RAM */
#define JOY1_BUTTONS      0x0016u  /* Joy1_ButtonMask */
#define AREA              0x0024u  /* Area_CurrentArea */
#define SCREEN            0x0063u  /* Area_CurrentScreen */
#define PLAYER_POS_Y      0x00A1u
#define PLAYER_FLAGS      0x00A4u
#define PLAYER_STATUS     0x00A5u  /* Player_StatusFlag */
#define SPECIAL_EVENT     0x042Eu  /* CurrentScreen_SpecialEventID */
#define A_BUTTON          0x80u
#define CAN_CLIMB         0x08u    /* Player_Flags */
#define CLIMBING          0x10u
#define WING_BOOTS        0x80u    /* Player_StatusFlag */
#define TRUNK             1u
#define FOUNTAIN_SCREEN   0x28u    /* Trunk $28, the dried fountain and the gate to the Mist */
#define NO_EVENT          0xFFu
#define P_Z               0x02u

static bool enabled;
static unsigned tops, events;      /* climbs stopped at a top; fountain events kept off another screen */

static uint8_t peek(uint16_t addr) { return cyc_mod_peek(addr); }

/* Whether the hero would still be on a ladder a pixel higher: the game's own
 * Player_CheckIfOnLadder, in a scratch copy of the machine. */
static bool on_ladder_at(uint8_t y) {
    CycModRegs regs;
    cyc_mod_regs(&regs);
    if (!cyc_mod_isolate_begin()) return true;
    cyc_mod_poke(PLAYER_POS_Y, y);
    bool ok = cyc_mod_call(CHECK_IF_ON_LADDER, &regs) && (peek(PLAYER_FLAGS) & CAN_CLIMB);
    cyc_mod_isolate_end();
    return ok;
}

/* The climb's passable check. With nothing over his head (Z set), a step that
 * would leave the ladder becomes the solid block's early return. */
static int climb_up_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) return 0;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    if (!(regs.p & P_Z)) return 0;                                         /* a block above: the game stops him */
    if ((peek(PLAYER_STATUS) & WING_BOOTS) && (peek(JOY1_BUTTONS) & A_BUTTON)) return 0;   /* flying */
    uint8_t y = peek(PLAYER_POS_Y);
    if (y == 0 || on_ladder_at((uint8_t)(y - 1))) return 0;
    cyc_mod_poke(PLAYER_FLAGS, (uint8_t)(peek(PLAYER_FLAGS) & ~CLIMBING));
    regs.a = 1;
    regs.p = (uint8_t)(regs.p & ~P_Z);
    cyc_mod_set_regs(&regs);
    ++tops;
    return 0;
}

/* The fountain's event: only on the screen it belongs to. */
static int fountain_hook(uint16_t addr) {
    (void)addr;
    if (peek(AREA) == TRUNK && peek(SCREEN) == FOUNTAIN_SCREEN) return 0;
    cyc_mod_poke(SPECIAL_EVENT, NO_EVENT);
    if (!cyc_mod_isolated()) ++events;
    return 1;
}

static const char *const hook_ids[] = {"faxanadu.ladder-fixes.climb-top", "faxanadu.ladder-fixes.fountain"};
static void hooks(int on) {
    for (unsigned i = 0; i < sizeof hook_ids / sizeof hook_ids[0]; i++) nes_mod_set_function_hook_enabled(hook_ids[i], on);
}
static void deactivate(void) {
    enabled = false;
    tops = events = 0;
    hooks(0);
}
static void activate(void) {
    enabled = true;
    hooks(1);
    nes_mod_set_local_only("Ladder fixes", 1);
}

#ifdef CYC_WITH_SDL
static void ladders_state(int id, const char *line) {
    (void)line;
    char fields[96];
    snprintf(fields, sizeof fields, "\"enabled\":%s,\"tops\":%u,\"events\":%u", enabled ? "true" : "false", tops,
             events);
    cyc_tcp_ok(id, fields);
}
void fax_ladders_tcp_setup(void) {
    cyc_tcp_register("ladders_state", "Ladder fixes: climbs stopped at a ladder's top, fountain events kept off "
                                      "another screen", ladders_state);
}
#else
void fax_ladders_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_ladders) {
    if (!nes_mod_register_function_entry_plugin(hook_ids[0], CLIMB_UP_BRANCH, climb_up_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[1], FOUNTAIN_EVENT, fountain_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.ladder-fixes", activate)) abort();
}
