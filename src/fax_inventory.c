/* The Battle Suit and Dragon Slayer fix. USA 57dd23d1, chipx86 byte-exact
 * disassembly.
 *
 * Touching an item sprite runs Player_PickUp ($C764), which jumps by entity
 * to the item's own pickup. Player_PickUpBattleSuit ($C6EA), the Battle
 * Helmet's ($C70A) and the Dragon Slayer's ($C72A) are the same routine: run
 * the "I've got ..." script, play sound 8, then
 *
 *     LDX count, CPX #$04, BCC +2, LDX #$03, LDA #$03, STA list,X, INX, STX count
 *
 * which appends the item, or replaces the last entry of a full list, as
 * Player_AddToInventory does for shops. The helmet's reads NumberOfShields.
 * The suit's reads NumberOfWeapons and the slayer's NumberOfArmors (chipx86:
 * "Armor/Weapon cap logic swapped"), so each adds at the other list's count
 * and sets its own list's count from it. With fewer weapons than armor, the
 * Battle Suit overwrites an armor and drops the ones after it; with more, the
 * armor list grows over stale bytes (old or blank entries, shown and sold as
 * armor). The Dragon Slayer does the same to the weapons.
 *
 * The fix: a hook at each routine's CPX #$04 (after the wrong LDX) puts the
 * right list's count in X, with the flags its LDX would have set, and returns
 * 0. The game's own cap, store and count follow. It keeps no state, so it
 * runs in Seamless's isolated rooms too; it counts only outside them. See
 * docs/GAME_INTERNALS.md, "Items and the inventory". */
#include "fax_inventory.h"
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
/* Player_PickUpBattleSuit: CPX #$04, after LDX NumberOfWeapons (the bug);
 * then BCC @_addItem, LDX #$03, LDA #$03, STA ArmorInventory,X, INX,
 * STX NumberOfArmors */
#define SUIT_CAP    0xC6FAu
/* Player_PickUpDragonSlayer: CPX #$04, after LDX NumberOfArmors (the bug);
 * then the same with WeaponInventory and NumberOfWeapons */
#define SLAYER_CAP  0xC73Au
/* RAM */
#define NUMBER_OF_WEAPONS 0x03C2u
#define NUMBER_OF_ARMORS  0x03C3u
/* Values */
#define FLAG_N 0x80u
#define FLAG_Z 0x02u

static bool enabled;
static unsigned fixed[FAX_PICKUPS];      /* pickups whose count the hook set, outside isolation */
static uint8_t last_wrong = 0xFF, last_right = 0xFF;

uint16_t fax_inventory_count_addr(FaxPickup item) {
    return item == FAX_PICKUP_BATTLE_SUIT ? NUMBER_OF_ARMORS : NUMBER_OF_WEAPONS;
}

/* The routine at hand has loaded X from the wrong count: load the list's own,
 * as LDX would (N and Z from it). CPX sets N, Z and C again at once. */
static int cap(FaxPickup item) {
    CycModRegs regs;
    cyc_mod_regs(&regs);
    uint8_t right = cyc_mod_peek(fax_inventory_count_addr(item));
    uint8_t wrong = regs.x;
    regs.x = right;
    regs.p = (uint8_t)((regs.p & ~(FLAG_N | FLAG_Z)) | (right & FLAG_N) | (right ? 0 : FLAG_Z));
    cyc_mod_set_regs(&regs);
    if (!cyc_mod_isolated()) {
        ++fixed[item];
        last_wrong = wrong;
        last_right = right;
    }
    return 0;
}
static int suit_hook(uint16_t addr) { (void)addr; return cap(FAX_PICKUP_BATTLE_SUIT); }
static int slayer_hook(uint16_t addr) { (void)addr; return cap(FAX_PICKUP_DRAGON_SLAYER); }

static const char *const suit_id = "faxanadu.inventory-fix.battle-suit";
static const char *const slayer_id = "faxanadu.inventory-fix.dragon-slayer";

static void deactivate(void) {
    enabled = false;
    fixed[FAX_PICKUP_BATTLE_SUIT] = fixed[FAX_PICKUP_DRAGON_SLAYER] = 0;
    last_wrong = last_right = 0xFF;
    nes_mod_set_function_hook_enabled(suit_id, 0);
    nes_mod_set_function_hook_enabled(slayer_id, 0);
}
static void activate(void) {
    enabled = true;
    nes_mod_set_function_hook_enabled(suit_id, 1);
    nes_mod_set_function_hook_enabled(slayer_id, 1);
    nes_mod_set_local_only("Battle Suit and Dragon Slayer fix", 1);
}

#ifdef CYC_WITH_SDL
static void inventory_state(int id, const char *line) {
    (void)line;
    char fields[160];
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"battle_suits\":%u,\"dragon_slayers\":%u,\"last_wrong\":%u,\"last_right\":%u",
             enabled ? "true" : "false", fixed[FAX_PICKUP_BATTLE_SUIT], fixed[FAX_PICKUP_DRAGON_SLAYER],
             last_wrong, last_right);
    cyc_tcp_ok(id, fields);
}
void fax_inventory_tcp_setup(void) {
    cyc_tcp_register("inventory_fix_state",
                     "Battle Suit and Dragon Slayer fix: pickups it gave the right count, and the last one's counts",
                     inventory_state);
}
#else
void fax_inventory_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_inventory) {
    if (!nes_mod_register_function_entry_plugin(suit_id, SUIT_CAP, suit_hook) ||
        !nes_mod_register_function_entry_plugin(slayer_id, SLAYER_CAP, slayer_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.inventory-fix", activate)) abort();
}
