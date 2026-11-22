/* Kept finds: with the Items list full, a find stays where it lies (#38).
 * USA 57dd23d1, chipx86 byte-exact disassembly.
 *
 * Touching an item sprite runs Player_HandleTouchItem ($88FB, bank 14): JSR
 * Player_PickUp ($C764), which jumps by entity to the item's routine, then
 * the sprite is removed. The routines of the Items that go in the list (the
 * Mattock, the Wing Boots, the Hour Glass, the Red Potion) run the item's
 * "I've got ..." script, play the pickup sound and end in Player_PickUpItem
 * ($C8CD), which does nothing when NumberOfItems is 8: the line said it was
 * got, and the sprite is gone all the same. Ripasheiku's Mattock ($5B) and
 * Zorugeriru's Wing Boots ($5C) set their Quests bit before the line
 * (Player_PickUpMattockWithQuest, Player_PickUpWingBootsWithQuest), and their
 * sprites remove themselves while it is set: the Mattock is gone until a
 * death clears $10 (Player_Spawn), the boots for good. A speedrunner's notes
 * (minirevver) give the Mattock as "you can't get it again".
 *
 * A hook at Player_HandleTouchItem's entry skips the whole routine when the
 * sprite is one of those finds and Player_PickUpItem would refuse it: no
 * line, no bit, and the sprite stays, to be picked up once there is room. The
 * one exception is a find Recharging items takes instead (Wing Boots or an
 * Hour Glass the hero already holds: it readies his, adding nothing), which
 * goes through. On the first refused touch the game's "invalid choice" sound
 * plays (Sound_PlayEffect, in a committed call), once a touch: the routine
 * runs on every frame the hero overlaps the sprite.
 *
 * It keeps only that sound's latch, which no save state needs. See
 * docs/GAME_INTERNALS.md, "Softlocks". */
#include "fax_finds.h"
#include "fax_recharge.h"
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
#define HANDLE_TOUCH_ITEM     0x88FBu   /* Player_HandleTouchItem: LDA CurrentSprites_Entities,X, JSR Player_PickUp,
                                           LDX CurrentSpriteIndex, LDA #$FF, STA CurrentSprites_Entities,X */
/* Bank 15 */
#define SOUND_PLAY_EFFECT     0xD0E4u   /* STA Temp_SoundIDToPlay, ...: queues sound A, keeps X and Y */
/* RAM */
#define INTERRUPT_COUNTER     0x001Au   /* +1 each NMI */
#define ENTITIES              0x02CCu   /* CurrentSprites_Entities, 8 slots */
#define ITEM_INVENTORY        0x03ADu
#define SELECTED_ITEM         0x03C1u
#define NUMBER_OF_ITEMS       0x03C6u
/* Values */
#define MAX_ITEMS             8u        /* Player_PickUpItem's CPX #$08 */
#define SOUND_INVALID_CHOICE  0x0Du
#define TOUCH_GAP             8u        /* frames without a refused touch that end one */
/* Sprite entities, and the Items ID each one's routine adds */
enum { MATTOCK_FIND = 0x50, WING_BOOTS_FIND = 0x55, HOUR_GLASS_FIND = 0x56, MATTOCK_QUEST_FIND = 0x5B,
       WING_BOOTS_QUEST_FIND = 0x5C, RED_POTION_FIND = 0x5D };
enum { ITEM_MATTOCK = 0x09, ITEM_HOUR_GLASS = 0x0D, ITEM_WING_BOOTS = 0x0F, ITEM_RED_POTION = 0x10, ITEM_NONE = 0xFF };

static bool enabled;
static unsigned refused, beeps;           /* touches skipped, sounds played (outside isolation) */
static uint8_t last_entity = 0xFF;        /* the last find refused */
static int latch_slot = -1;               /* the sprite slot of the touch that sounded, and when it last touched */
static uint8_t latch_frame;

/* The Items ID a find's routine passes to Player_PickUpItem, or ITEM_NONE for
 * every other sprite (the rings' bits, the battle gear's lists of 4, the
 * timed and instant ones, coins, enemies). */
static uint8_t find_item(uint8_t entity) {
    switch (entity) {
    case MATTOCK_FIND: case MATTOCK_QUEST_FIND: return ITEM_MATTOCK;
    case WING_BOOTS_FIND: case WING_BOOTS_QUEST_FIND: return ITEM_WING_BOOTS;
    case HOUR_GLASS_FIND: return ITEM_HOUR_GLASS;
    case RED_POTION_FIND: return ITEM_RED_POTION;
    default: return ITEM_NONE;
    }
}

/* Whether the hero holds `item`: selected, or in the list's first `count`. */
static bool holds(uint8_t item, uint8_t selected, const uint8_t *list, uint8_t count) {
    if (selected == item) return true;
    for (unsigned i = 0; i < count && i < MAX_ITEMS; ++i)
        if (list[i] == item) return true;
    return false;
}

/* Whether the touch would lose the find: an Items find, the list full, and
 * not one Recharging items takes instead. */
static bool would_lose(uint8_t entity, uint8_t count, uint8_t selected, const uint8_t *list, bool recharge_on) {
    uint8_t item = find_item(entity);
    if (item == ITEM_NONE || count < MAX_ITEMS) return false;
    if (recharge_on && (item == ITEM_WING_BOOTS || item == ITEM_HOUR_GLASS) && holds(item, selected, list, count))
        return false;
    return true;
}

/* A refused touch sounds unless the same slot was refused within TOUCH_GAP frames. */
static bool new_touch(int slot, uint8_t frame) {
    bool fresh = slot != latch_slot || (uint8_t)(frame - latch_frame) > TOUCH_GAP;
    latch_slot = slot;
    latch_frame = frame;
    return fresh;
}

/* Player_HandleTouchItem's entry: X is the sprite's slot. */
static int touch_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) return 0;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    uint8_t slot = regs.x & 7u, entity = cyc_mod_peek((uint16_t)(ENTITIES + slot));
    uint8_t list[MAX_ITEMS];
    for (unsigned i = 0; i < MAX_ITEMS; ++i) list[i] = cyc_mod_peek((uint16_t)(ITEM_INVENTORY + i));
    if (!would_lose(entity, cyc_mod_peek(NUMBER_OF_ITEMS), cyc_mod_peek(SELECTED_ITEM), list, fax_recharge_on()))
        return 0;
    ++refused;
    last_entity = entity;
    if (new_touch(slot, cyc_mod_peek(INTERRUPT_COUNTER))) {
        regs.a = SOUND_INVALID_CHOICE;
        cyc_mod_call_commit(SOUND_PLAY_EFFECT, &regs);
        ++beeps;
    }
    return 1;
}

static const char *const touch_id = "faxanadu.kept-finds.touch";

static void deactivate(void) {
    enabled = false;
    refused = beeps = 0;
    last_entity = 0xFF;
    latch_slot = -1;
    nes_mod_set_function_hook_enabled(touch_id, 0);
}
static void activate(void) {
    enabled = true;
    nes_mod_set_function_hook_enabled(touch_id, 1);
    nes_mod_set_local_only("Kept finds", 1);
}

#ifdef CYC_WITH_SDL
static void kept_finds_state(int id, const char *line) {
    (void)line;
    char fields[120];
    snprintf(fields, sizeof fields, "\"enabled\":%s,\"refused\":%u,\"beeps\":%u,\"last_entity\":%u",
             enabled ? "true" : "false", refused, beeps, (unsigned)last_entity);
    cyc_tcp_ok(id, fields);
}
void fax_finds_tcp_setup(void) {
    cyc_tcp_register("kept_finds_state",
                     "Kept finds: touches of a find left lying with the Items list full, sounds played, the last find",
                     kept_finds_state);
}
#else
void fax_finds_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_kept_finds) {
    if (!nes_mod_register_function_entry_plugin(touch_id, HANDLE_TOUCH_ITEM, touch_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.kept-finds", activate)) abort();
}
