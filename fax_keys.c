/* Auto keys and reusable tools. USA 57dd23d1, chipx86 byte-exact disassembly.
 *
 * A locked door runs Game_RunDoorRequirementHandler, which dispatches on
 * CurrentDoor_KeyRequirement (1-5: the A, K, Q, J and Jo keys) to a handler
 * that compares SelectedItem with its key. On a match it takes
 * Game_UnlockDoorWithUsableItem: the "I've used key." script, then
 * SelectedItem = $FF and UI_ClearSelectedItemPic, which is how the key is used
 * up: a selected item lives in SelectedItem only, not in ItemInventory (the
 * game swaps them when you select one). Otherwise the door's "mark of ... by
 * the key hole" script runs and the door stays shut.
 *
 * Auto keys (faxanadu.auto-keys): at the handler's entry, when the door's key
 * is in ItemInventory but not selected, the key is lent to SelectedItem, so the
 * game's own check passes and its own unlock path runs. Where that path clears
 * the HUD picture, the lent key is taken from ItemInventory (unless reusable
 * tools keeps it) and the selection the player had is put back. The HUD never
 * stopped showing it, so the clear is skipped.
 *
 * Reusable tools (faxanadu.reusable-tools): the same clear, reached with a
 * selected key, puts the key back in SelectedItem and is skipped, so the key
 * stays selected and drawn. Player_UseMattock clears the Mattock through
 * Player_ClearSelectedItem; that call is skipped, and only that one. Which
 * item each Player_ClearSelectedItem call uses up, and who keeps it, is one
 * list (fax_items.h): the Red Potion stays used up, and the Wing Boots and
 * Hour Glass are recharging items' (fax_recharge.c).
 *
 * The calls are told apart by their return address, as fax_death.c does. Both
 * features hook UI_ClearSelectedItemPic with one callback (unlocked_hook) that
 * finishes the door for whichever features are on, so it does not matter which
 * of the two sites fires first. The lent key is the only state, and it lives
 * from a door's handler to its unlock, inside one frame-frozen script; it is
 * still saved with save states. See docs/GAME_INTERNALS.md. */
#include "fax_keys.h"
#include "fax_items.h"
#include "cyc_mod.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#ifdef CYC_WITH_SDL
#include "cyc_tcp.h"
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Fixed bank */
#define GAME_RUN_DOOR_REQUIREMENT_HANDLER 0xEB2Fu  /* LDA CurrentDoor_KeyRequirement, BEQ RETURN_EB3E, ASL A */
#define UI_CLEAR_SELECTED_ITEM_PIC        0xC8DCu  /* LDA #$13, STA PPU_TargetAddr_U: blank the HUD's item picture */
#define PLAYER_CLEAR_SELECTED_ITEM        0xC4BFu  /* PHA, LDA #$FF, STA SelectedItem, JSR UI_ClearSelectedItemPic */
#define UNLOCK_CLEARS_PICTURE             0xEBDEu  /* Game_UnlockDoorWithUsableItem: JSR UI_ClearSelectedItemPic,
                                                      after the "used key" script and STA SelectedItem #$FF */
#define MATTOCK_CLEARS_ITEM               0xC64Cu  /* Player_UseMattock: JSR Player_ClearSelectedItem,
                                                      after the "used Mattock" script */
/* RAM */
#define ITEM_INVENTORY               0x03ADu   /* the unselected items, NumberOfItems of them */
#define SELECTED_ITEM                0x03C1u   /* $FF for none */
#define NUMBER_OF_ITEMS              0x03C6u
#define CURRENT_DOOR_KEY_REQUIREMENT 0x042Bu   /* 1-5 the A, K, Q, J, Jo keys; 6-8 the rings */
/* Values */
#define MAX_ITEMS     8                        /* MAX_INVENTORY_SLOTS[INVENTORY_ITEM] */
#define DOOR_A_KEY    1
#define DOOR_JO_KEY   5
#define ITEM_KEY_A    0x04                     /* ITEMINVENTORY_KEY_A; K, Q, J, Jo follow */
#define ITEM_MATTOCK  0x09
#define NO_ITEM       0xFF

static bool auto_on, reuse_on;
/* Auto keys: the key lent to SelectedItem for one door, and what it replaced. */
typedef struct { bool active; uint8_t key, prev; } Lend;
static Lend lend;
static unsigned auto_opened, keys_kept, mattock_kept;   /* for the TCP state */

/* The slot of `item` in ItemInventory, or -1. Only the first NumberOfItems
 * slots hold items; the game leaves stale bytes after them. */
static int inventory_find(uint8_t item) {
    unsigned n = cyc_mod_peek(NUMBER_OF_ITEMS);
    if (n > MAX_ITEMS) n = MAX_ITEMS;
    for (unsigned i = 0; i < n; ++i)
        if (cyc_mod_peek((uint16_t)(ITEM_INVENTORY + i)) == item) return (int)i;
    return -1;
}
/* Take the item at `slot` out of ItemInventory as Player_RemoveItem does: the
 * ones after it move down and NumberOfItems drops by one. */
static void inventory_remove(int slot) {
    unsigned n = cyc_mod_peek(NUMBER_OF_ITEMS);
    if (n > MAX_ITEMS) n = MAX_ITEMS;
    if (slot < 0 || (unsigned)slot >= n) return;
    for (unsigned i = (unsigned)slot + 1; i < n; ++i)
        cyc_mod_poke((uint16_t)(ITEM_INVENTORY + i - 1), cyc_mod_peek((uint16_t)(ITEM_INVENTORY + i)));
    cyc_mod_poke(NUMBER_OF_ITEMS, (uint8_t)(n - 1));
}
/* The key the current door needs, or 0 for none (no lock, or a ring). */
static uint8_t door_key(void) {
    uint8_t need = cyc_mod_peek(CURRENT_DOOR_KEY_REQUIREMENT);
    return need >= DOOR_A_KEY && need <= DOOR_JO_KEY ? (uint8_t)(ITEM_KEY_A + need - DOOR_A_KEY) : 0;
}
/* The routine at hand was entered by a JSR at `jsr`: a JSR pushes the address
 * of its own last byte. The 6502 stack wraps within page 1. */
static bool called_from(uint16_t jsr) {
    CycModRegs regs;
    cyc_mod_regs(&regs);
    uint16_t pushed = (uint16_t)(cyc_mod_peek((uint16_t)(0x100u | (uint8_t)(regs.s + 1))) |
                                 cyc_mod_peek((uint16_t)(0x100u | (uint8_t)(regs.s + 2))) << 8);
    return pushed == (uint16_t)(jsr + 2);
}

/* Game_RunDoorRequirementHandler: lend the door's key to SelectedItem when it
 * is in the inventory but not selected. The handler runs as it always does. */
static int door_hook(uint16_t addr) {
    (void)addr;
    lend.active = false;
    uint8_t key = door_key(), selected = cyc_mod_peek(SELECTED_ITEM);
    if (!key || selected == key || inventory_find(key) < 0) return 0;   /* the game's own check decides */
    lend = (Lend){ true, key, selected };
    cyc_mod_poke(SELECTED_ITEM, key);
    return 0;
}

/* UI_ClearSelectedItemPic, called from Game_UnlockDoorWithUsableItem: the
 * "used key" script has run and SelectedItem is $FF. Both features' sites
 * call this; it does all of the work for both, so a second call finds nothing
 * left to do. Returning 1 skips the clear and goes on to Game_UnlockDoor. */
static int unlocked_hook(uint16_t addr) {
    (void)addr;
    if (!called_from(UNLOCK_CLEARS_PICTURE)) return 0;
    uint8_t key = door_key();
    if (lend.active) {
        lend.active = false;
        if (key && lend.key == key) {
            if (reuse_on) ++keys_kept;
            else inventory_remove(inventory_find(key));   /* used up, as if it had been selected */
            cyc_mod_poke(SELECTED_ITEM, lend.prev);       /* the HUD still shows it */
            ++auto_opened;
            return 1;
        }
    }
    if (reuse_on && key) {                                /* a selected key: keep it selected */
        cyc_mod_poke(SELECTED_ITEM, key);
        ++keys_kept;
        return 1;
    }
    return 0;
}

/* Player_ClearSelectedItem, called from Player_UseMattock: keep the Mattock.
 * The routine preserves A, and Player_UseMattock restores X and Y from the
 * stack after it, so skipping it changes nothing else. The other calls are
 * left to whoever fax_items.h names. */
static int mattock_hook(uint16_t addr) {
    (void)addr;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    if (fax_use_up_keeper(fax_jsr_caller(regs.s, cyc_mod_peek)) != FAX_KEPT_BY_REUSABLE_TOOLS) return 0;
    ++mattock_kept;
    return 1;
}

static const char *const door_id = "faxanadu.auto-keys.door";
static const char *const auto_unlocked_id = "faxanadu.auto-keys.unlocked";
static const char *const reuse_unlocked_id = "faxanadu.reusable-tools.unlocked";
static const char *const mattock_id = "faxanadu.reusable-tools.mattock";

static void deactivate(void) {
    auto_on = reuse_on = false;
    lend = (Lend){ 0 };
    auto_opened = keys_kept = mattock_kept = 0;
    nes_mod_set_function_hook_enabled(door_id, 0);
    nes_mod_set_function_hook_enabled(auto_unlocked_id, 0);
    nes_mod_set_function_hook_enabled(reuse_unlocked_id, 0);
    nes_mod_set_function_hook_enabled(mattock_id, 0);
}
static void activate_auto(void) {
    auto_on = true;
    nes_mod_set_function_hook_enabled(door_id, 1);
    nes_mod_set_function_hook_enabled(auto_unlocked_id, 1);
    nes_mod_set_local_only("Auto keys", 1);
}
static void activate_reuse(void) {
    reuse_on = true;
    nes_mod_set_function_hook_enabled(reuse_unlocked_id, 1);
    nes_mod_set_function_hook_enabled(mattock_id, 1);
    nes_mod_set_local_only("Reusable tools", 1);
}

/* A save state taken during the "used key" message holds a lent key. */
static int state_get(uint8_t *buf, int cap) {
    if (cap < 3) return -1;
    buf[0] = lend.active; buf[1] = lend.key; buf[2] = lend.prev;
    return 3;
}
static int state_validate(const uint8_t *buf, int len) {
    return len == 0 ||
           (len == 3 && buf[0] <= 1 && (!buf[0] || (buf[1] >= ITEM_KEY_A && buf[1] < ITEM_KEY_A + DOOR_JO_KEY)));
}
static int state_set(const uint8_t *buf, int len) {
    if (!state_validate(buf, len)) return 0;
    lend = len ? (Lend){ buf[0], buf[1], buf[2] } : (Lend){ 0 };
    return 1;
}

#ifdef CYC_WITH_SDL
static void keys_state(int id, const char *line) {
    (void)line;
    char fields[192];
    snprintf(fields, sizeof fields,
             "\"auto\":%s,\"reuse\":%s,\"auto_opened\":%u,\"keys_kept\":%u,\"mattock_kept\":%u,\"lent\":%s",
             auto_on ? "true" : "false", reuse_on ? "true" : "false", auto_opened, keys_kept, mattock_kept,
             lend.active ? "true" : "false");
    cyc_tcp_ok(id, fields);
}
void fax_keys_tcp_setup(void) {
    cyc_tcp_register("keys_state", "Auto keys and reusable tools: toggles, doors opened, keys and Mattocks kept",
                     keys_state);
}
#else
void fax_keys_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_keys) {
    if (!nes_mod_register_function_entry_plugin(door_id, GAME_RUN_DOOR_REQUIREMENT_HANDLER, door_hook) ||
        !nes_mod_register_function_entry_plugin(auto_unlocked_id, UI_CLEAR_SELECTED_ITEM_PIC, unlocked_hook) ||
        !nes_mod_register_function_entry_plugin(reuse_unlocked_id, UI_CLEAR_SELECTED_ITEM_PIC, unlocked_hook) ||
        !nes_mod_register_function_entry_plugin(mattock_id, PLAYER_CLEAR_SELECTED_ITEM, mattock_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.auto-keys", activate_auto) ||
        !nes_mod_register_activation_plugin("faxanadu.reusable-tools", activate_reuse) ||
        !nes_mod_register_savestate_hook("faxanadu.keys", state_get, state_set) ||
        !nes_mod_register_savestate_validator("faxanadu.keys", state_validate)) abort();
}
