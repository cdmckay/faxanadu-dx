/* Eolis's edge: the shafts at Eolis's right edge closed (#134). USA 57dd23d1,
 * chipx86 byte-exact disassembly.
 *
 * Eolis $00 and $08 have no screen to their right, and their right wall
 * (column 14, rows 3-11, block $0E) stands one block short of the edge: column
 * 15 is open from the sky ($01) down the back of the town wall ($5A, $54) and
 * through the floor ($5F, row 11) to the bottom of the screen. Flown over the
 * wall and let go, the hero drops down it to Y $C0 (Player_SetPosAtBottomEdge,
 * no screen below), under the floor, where A, Up and Right do nothing and no
 * enemy comes. With the Wing Boots running on MP and MP only from kills and
 * Gurus (fax_recharge.c, fax_mana.c), a flight that runs out over it left him
 * there until a reset, as in the original (docs/GAME_INTERNALS.md,
 * "Softlocks").
 *
 * - The column stands solid. A hook at ScreenBuffer_IsBlockImpassable's
 *   entry ($E87C, LDY ScreenBuffer,X) answers "impassable" for column 15,
 *   rows 0-11, of those two screens (fax_shaft_solid): A = 1, Y = the solid
 *   property, N and Z as the routine's own LDA leaves them, and it returns as
 *   its RTS would, as for the wall beside it. Every check of the screen's own
 *   blocks goes through there: the hero's sideways moves
 *   (Area_CanPlayerMoveAtY), his ground and head checks
 *   (Area_CanMoveImmediatelyRight), a climb's sideways check, and the
 *   sprites' and spells' moves. So flying he stops at the wall's edge (X 224)
 *   and lands on its top, and nothing walks or falls into the column. The
 *   blocks themselves stay, so nothing looks different: $5A, $54 and $5F are
 *   the back of the town wall that other Eolis screens show as background
 *   (block properties are the area's, so they can't change), and $01 is the
 *   sky. Checks across a screen's edge read LastColumnLeftScreen and the like
 *   through Area_IsBlockImpassable ($E87F), past the site, and these screens
 *   have nothing to their right, alone or in Seamless.
 * - Out of the shaft. A save state from before can hold him in it: in column
 *   15 of $00 or $08 (X past 224), or under Eolis's floor (Y 145 to $C0). The
 *   floor (row 11) is solid on every Eolis screen but for that column, so
 *   only the shaft leads under it, and from $08 the open row beneath takes
 *   him left into $01 (in Seamless as far as $06). A hook at Game_MainLoop's
 *   JSR GameLoop_UpdatePlayer ($DB53), before he moves each frame, puts him
 *   standing on the floor (Y 144) where he is, or next to the wall (X 208)
 *   on $00 and $08 (fax_shaft_rescue). Rows 9 and 10, where he then stands,
 *   are clear wherever the open row leads.
 *
 * The solid column keeps no state, so it also runs in Seamless's isolated
 * rooms (a neighbour's townsfolk see the same wall); the rescue moves only
 * the live hero. What each did, outside them, is counted for the TCP command.
 * No ROM, no read overrides, no save-state record. */
#include "fax_shaft.h"
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

/* Fixed bank */
#define IS_BLOCK_IMPASSABLE 0xE87Cu  /* ScreenBuffer_IsBlockImpassable: LDY ScreenBuffer,X, JSR
                                        Area_GetBlockProperty, TAY, LDA BLOCK_PROPERTY_IMPASSABLE_MAP,Y, RTS */
#define UPDATE_PLAYER_CALL  0xDB53u  /* Game_MainLoop: JSR GameLoop_UpdatePlayer, after
                                        JSR Screen_ResetSpritesForGamePlay */
/* RAM */
#define AREA_CURRENT        0x0024u  /* Area_CurrentArea */
#define SCREEN_CURRENT      0x0063u  /* Area_CurrentScreen */
#define PLAYER_X            0x009Eu  /* Player_PosX_Block */
#define PLAYER_Y            0x00A1u  /* Player_PosY */
/* Values */
#define AREA_EOLIS          0x00u
#define EDGE_COLUMN         15u      /* the open column */
#define FLOOR_ROW           11u      /* Eolis's floor; the column stands solid down to it */
#define PROPERTY_SOLID      0x01u    /* BLOCK_PROPERTY_IMPASSABLE_MAP[1] = 1 */
#define WALL_X              224u     /* column 14: past it he overlaps column 15 */
#define BESIDE_WALL_X       208u     /* column 13, the floor next to the wall */
#define FLOOR_Y             144u     /* standing on row 11 (Y + 32 = its top) */
#define BOTTOM_Y            0xC0u    /* Player_SetPosAtBottomEdge */
#define FLAG_N              0x80u
#define FLAG_Z              0x02u

/* Eolis's screens whose right edge is the open column: outside the gate
 * (where the opening plays) and the gate to the Trunk. */
static bool shaft_screen(uint8_t screen) { return screen == 0x00u || screen == 0x08u; }

bool fax_shaft_solid(uint8_t area, uint8_t screen, uint8_t index) {
    return area == AREA_EOLIS && shaft_screen(screen) && (index & 15u) == EDGE_COLUMN && index >> 4 <= FLOOR_ROW;
}

bool fax_shaft_rescue(uint8_t area, uint8_t screen, uint8_t x, uint8_t y, uint8_t *to_x, uint8_t *to_y) {
    if (area != AREA_EOLIS) return false;
    bool in_column = shaft_screen(screen) && x > WALL_X;
    bool under_floor = y > FLOOR_Y && y <= BOTTOM_Y;
    if (!in_column && !under_floor) return false;
    *to_x = shaft_screen(screen) && x > BESIDE_WALL_X ? (uint8_t)BESIDE_WALL_X : x;
    *to_y = FLOOR_Y;
    return true;
}

static bool enabled;
static unsigned solid, rescued;          /* checks answered solid and heroes put back, outside isolation */
static uint8_t from_screen = 0xFF, from_x, from_y;   /* the last hero put back: where he was */

/* ScreenBuffer_IsBlockImpassable's entry: X is the block's index. */
static int solid_hook(uint16_t addr) {
    (void)addr;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    if (!fax_shaft_solid(cyc_mod_peek(AREA_CURRENT), cyc_mod_peek(SCREEN_CURRENT), regs.x)) return 0;
    regs.a = 1;                          /* BLOCK_PROPERTY_IMPASSABLE_MAP[PROPERTY_SOLID] */
    regs.y = PROPERTY_SOLID;             /* the routine's TAY leaves the property in Y */
    regs.p = (uint8_t)(regs.p & ~(FLAG_N | FLAG_Z));
    cyc_mod_set_regs(&regs);
    if (!cyc_mod_isolated()) ++solid;
    return 1;
}

/* Game_MainLoop, before GameLoop_UpdatePlayer: a hero in the shaft or under
 * the floor goes back onto it. */
static int rescue_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) return 0;
    uint8_t screen = cyc_mod_peek(SCREEN_CURRENT), x = cyc_mod_peek(PLAYER_X), y = cyc_mod_peek(PLAYER_Y);
    uint8_t to_x, to_y;
    if (!fax_shaft_rescue(cyc_mod_peek(AREA_CURRENT), screen, x, y, &to_x, &to_y)) return 0;
    cyc_mod_poke(PLAYER_X, to_x);
    cyc_mod_poke(PLAYER_Y, to_y);
    from_screen = screen;
    from_x = x;
    from_y = y;
    ++rescued;
    return 0;
}

static const char *const hook_ids[] = { "faxanadu.eolis-shaft.solid", "faxanadu.eolis-shaft.rescue" };
#define HOOKS (sizeof hook_ids / sizeof *hook_ids)

static void set_hooks(int on) {
    for (size_t i = 0; i < HOOKS; ++i) nes_mod_set_function_hook_enabled(hook_ids[i], on);
}
static void deactivate(void) {
    enabled = false;
    solid = rescued = 0;
    from_screen = 0xFF;
    from_x = from_y = 0;
    set_hooks(0);
}
static void activate(void) {
    enabled = true;
    set_hooks(1);
    nes_mod_set_local_only("Eolis's edge", 1);
}

#ifdef CYC_WITH_SDL
static void shaft_state(int id, const char *line) {
    (void)line;
    char fields[160];
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"solid\":%u,\"rescued\":%u,\"from_screen\":%u,\"from_x\":%u,\"from_y\":%u",
             enabled ? "true" : "false", solid, rescued, (unsigned)from_screen, (unsigned)from_x,
             (unsigned)from_y);
    cyc_tcp_ok(id, fields);
}
void fax_shaft_tcp_setup(void) {
    cyc_tcp_register("eolis_shaft_state",
                     "Eolis's edge: whether on, checks answered solid in the edge column, heroes put back on the "
                     "floor, and where the last one was (from_screen, from_x, from_y)",
                     shaft_state);
}
#else
void fax_shaft_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_eolis_shaft) {
    if (!nes_mod_register_function_entry_plugin(hook_ids[0], IS_BLOCK_IMPASSABLE, solid_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[1], UPDATE_PLAYER_CALL, rescue_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.eolis-shaft", activate)) abort();
}
