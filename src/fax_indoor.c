/* Equip indoors: equip from the menu, and hold and swing the weapon, inside
 * buildings. USA 57dd23d1, chipx86 byte-exact disassembly.
 *
 * Inside a building (Area_CurrentArea 4: the shops, houses, temples and the
 * King's room, and a few rooms in the Mist and Dartmoor that hold enemies) the
 * original keeps the weapon out of the hero's hand three ways:
 *
 * - Game_EnterBuilding ($DE06) begins LDA #$FF, STA Player_CurWeapon.
 *   Game_ExitBuilding puts SelectedWeapon back on the way out.
 * - Player_SetWeapon ($EDEC), in area 4, only stores SelectedWeapon: no
 *   Player_CurWeapon and no redraw.
 * - The Select menu (PlayerMenu_HandleInventoryMenuInput, bank 12) refuses
 *   every choice in area 4 with the invalid-choice sound, so no weapon, armor,
 *   shield, spell or item can be equipped there.
 *
 * Nothing else is gated: the hero swings indoors in the original, bare-handed,
 * because Player_DrawWeapon and Player_HitSpriteWithWeapon do nothing without
 * Player_CurWeapon. With the weapon in hand, the game's own code draws it and
 * wields it. Its tiles load to PPU $0340-$03FF, which no building's sprites
 * use (theirs load from $0900). The sword hits only enemies and bosses
 * (SPRITE_CATEGORIES_BY_ENTITY 0 and 7), never townsfolk (2), so a swing at a
 * shopkeeper or a Guru does nothing. The enemies in the rooms that hold them
 * now take the sword as well as magic, which the original never stopped.
 *
 * Three hooks, none shared:
 * - $DE0B, in Game_EnterBuilding right after the clear: Player_CurWeapon =
 *   SelectedWeapon, before Screen_SetupNew loads the hero's tiles
 *   (Player_DrawSpriteImmediately). Every way into a building passes there: a
 *   door, a respawn or Guru travel's journey (Player_Spawn), and the ending's
 *   walk to the King.
 * - $EDF1, and $8B88 in bank 12: each area check's BEQ. Clearing Z skips the
 *   refusal, and the routine goes on as it does outdoors.
 * Nothing is kept: the weapon in hand is the machine's, and save states carry
 * it. See docs/GAME_INTERNALS.md. */
#include "fax_indoor.h"
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
/* Game_EnterBuilding's LDA Areas_DefaultMusic, after LDA #$FF,
 * STA Player_CurWeapon at $DE06 */
#define ENTER_BUILDING    0xDE0Bu
/* Player_SetWeapon's BEQ @_cannotEquip, after LDA Area_CurrentArea,
 * CMP #$04. Falling through: PLA, STA SelectedWeapon,
 * STA Player_CurWeapon, JSR Player_DrawSprite */
#define SET_WEAPON_CHECK  0xEDF1u
/* Bank 12 */
/* PlayerMenu_HandleInventoryMenuInput's BEQ PlayerMenu_HandleInvalidChoice,
 * after LDA Area_CurrentArea, CMP #$04 at @_hasInput ($8B83). Falling
 * through: LDA PlayerMenu_SelectedInventory and the outdoor checks */
#define MENU_CHECK        0x8B88u
/* RAM */
#define AREA              0x0024u  /* Area_CurrentArea: 4 in a building */
#define SELECTED_WEAPON   0x03BDu
#define PLAYER_CUR_WEAPON 0x03C8u  /* The weapon in hand, $FF for none */
#define P_Z               0x02u

static bool enabled;
static unsigned entered, weapons_set, menu_choices;  /* times each site sent the game on as outdoors */

/* Into a building: the weapon stays in hand. SelectedWeapon is $FF without
 * one, which is the original's value here. */
static int enter_hook(uint16_t addr) {
    (void)addr;
    cyc_mod_poke(PLAYER_CUR_WEAPON, cyc_mod_peek(SELECTED_WEAPON));
    if (!cyc_mod_isolated()) ++entered;
    return 0;
}

/* An area check's BEQ, where Z set means a building. Both next instructions
 * (PLA at $EDF3, LDA at $8B8A) set A, N and Z, and C is set again before
 * anything reads it, so Z alone is changed. */
static bool go_on(void) {
    CycModRegs regs;
    cyc_mod_regs(&regs);
    if (!(regs.p & P_Z)) return false;
    regs.p = (uint8_t)(regs.p & ~P_Z);
    cyc_mod_set_regs(&regs);
    return true;
}
static int set_weapon_hook(uint16_t addr) {
    (void)addr;
    if (go_on() && !cyc_mod_isolated()) ++weapons_set;
    return 0;
}
static int menu_hook(uint16_t addr) {
    (void)addr;
    if (go_on() && !cyc_mod_isolated()) ++menu_choices;
    return 0;
}

static const char *const hook_ids[] = {
    "faxanadu.indoor-equip.enter", "faxanadu.indoor-equip.set-weapon", "faxanadu.indoor-equip.menu",
};
static void hooks(int on) {
    for (unsigned i = 0; i < sizeof hook_ids / sizeof hook_ids[0]; i++) nes_mod_set_function_hook_enabled(hook_ids[i], on);
}
static void deactivate(void) {
    enabled = false;
    entered = weapons_set = menu_choices = 0;
    hooks(0);
}
static void activate(void) {
    enabled = true;
    hooks(1);
    nes_mod_set_local_only("Equip indoors", 1);   /* a peer without it would play other rules */
}

#ifdef CYC_WITH_SDL
static void indoor_state(int id, const char *line) {
    (void)line;
    char fields[160];
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"entered\":%u,\"weapons_set\":%u,\"menu_choices\":%u,\"area\":%u,"
             "\"selected_weapon\":%u,\"weapon_in_hand\":%u",
             enabled ? "true" : "false", entered, weapons_set, menu_choices, cyc_mod_peek(AREA),
             cyc_mod_peek(SELECTED_WEAPON), cyc_mod_peek(PLAYER_CUR_WEAPON));
    cyc_tcp_ok(id, fields);
}
void fax_indoor_tcp_setup(void) {
    cyc_tcp_register("indoor_equip_state",
                     "Equip indoors: building entries, weapons and menu choices let through, and the weapon in hand",
                     indoor_state);
}
#else
void fax_indoor_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_indoor) {
    if (!nes_mod_register_function_entry_plugin(hook_ids[0], ENTER_BUILDING, enter_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[1], SET_WEAPON_CHECK, set_weapon_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[2], MENU_CHECK, menu_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.indoor-equip", activate)) abort();
}
