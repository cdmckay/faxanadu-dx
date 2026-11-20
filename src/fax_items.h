#pragma once
/* How using an item uses it up, in one list for every feature that changes
 * it. USA 57dd23d1, chipx86 byte-exact disassembly.
 *
 * A consumable ends its use routine with JSR Player_ClearSelectedItem ($C4BF:
 * SelectedItem = $FF, then UI_ClearSelectedItemPic): a selected item lives in
 * SelectedItem only, so clearing it is what uses it up. Features that keep an
 * item skip that call when it comes from the item's own routine, which they
 * tell by the JSR's return address. This is the list of those calls and who
 * keeps each item: reusable tools (fax_keys.c) the Mattock, recharging items
 * (fax_recharge.c) the Wing Boots and the Hour Glass. The Red Potion stays used
 * up. Each feature handles only its own rows, so their hook sites at $C4BF can
 * fire in either order. See docs/GAME_INTERNALS.md, "Items and the inventory". */
#include <stdint.h>

/* ITEMINVENTORY_* IDs */
enum { FAX_ITEM_MATTOCK = 0x09, FAX_ITEM_HOUR_GLASS = 0x0D, FAX_ITEM_WING_BOOTS = 0x0F, FAX_ITEM_RED_POTION = 0x10 };

#define FAX_PLAYER_CLEAR_SELECTED_ITEM 0xC4BFu   /* PHA, LDA #$FF, STA SelectedItem, JSR UI_ClearSelectedItemPic */

typedef enum { FAX_KEPT_BY_NONE, FAX_KEPT_BY_REUSABLE_TOOLS, FAX_KEPT_BY_RECHARGE } FaxKeeper;
typedef struct { uint16_t jsr; uint8_t item; FaxKeeper keeper; } FaxUseUp;

/* Each JSR Player_ClearSelectedItem, the item it uses up, and who keeps it. */
static const FaxUseUp FAX_USE_UPS[] = {
    { 0xC53Bu, FAX_ITEM_RED_POTION, FAX_KEPT_BY_NONE },             /* Player_UseRedPotion */
    { 0xC581u, FAX_ITEM_WING_BOOTS, FAX_KEPT_BY_RECHARGE },         /* Player_UseWingBoots */
    { 0xC5D0u, FAX_ITEM_HOUR_GLASS, FAX_KEPT_BY_RECHARGE },         /* Player_UseHourGlass */
    { 0xC64Cu, FAX_ITEM_MATTOCK, FAX_KEPT_BY_REUSABLE_TOOLS },      /* Player_UseMattock */
};

/* Who keeps the item whose use-up JSRed at `jsr` (NONE for any other caller). */
static inline FaxKeeper fax_use_up_keeper(uint16_t jsr) {
    for (unsigned i = 0; i < sizeof FAX_USE_UPS / sizeof FAX_USE_UPS[0]; ++i)
        if (FAX_USE_UPS[i].jsr == jsr) return FAX_USE_UPS[i].keeper;
    return FAX_KEPT_BY_NONE;
}

/* The JSR that entered the routine at hand: a JSR pushes the address of its
 * own last byte, and the 6502 stack wraps within page 1. `s` is the stack
 * pointer at the routine's entry; `peek` reads CPU memory. */
static inline uint16_t fax_jsr_caller(uint8_t s, uint8_t (*peek)(uint16_t)) {
    uint16_t pushed = (uint16_t)(peek((uint16_t)(0x100u | (uint8_t)(s + 1))) |
                                 peek((uint16_t)(0x100u | (uint8_t)(s + 2))) << 8);
    return (uint16_t)(pushed - 2);
}
