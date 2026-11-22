/* Unit tests for Kept finds (fax_finds.c), without the emulator or a ROM.
 * The module runs against a model of the machine: CPU RAM, the registers, and
 * the game's code around the hook played as the disassembly has it:
 * Player_HandleTouchItem (Player_PickUp, then the sprite removed), each find's
 * routine (its Quests bit for Ripasheiku's Mattock and Zorugeriru's Wing
 * Boots, its line, then Player_PickUpItem's append unless the list holds 8),
 * and Recharging items' own pickup hook taking Wing Boots or an Hour Glass the
 * hero already holds. So the tests check what the game ends up with (the
 * list, the bits, the sprite, the lines and sounds), not the module's own
 * arithmetic. */
#include "fax_finds.c"
#include <string.h>

static unsigned checks;
#define CHECK(test) do{++checks;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(1);}}while(0)

#define QUESTS          0x042Du
#define QUEST_WING_BOOTS 0x08u
#define QUEST_MATTOCK   0x10u

/* ---- the machine ---- */
static uint8_t ram[0x800];
static CycModRegs cpu;
static bool isolated, recharge;
static int touch_on = -1;
static unsigned lines, sounds_queued, invalid_sounds;

uint8_t cyc_mod_peek(uint16_t addr) { return addr < 0x2000 ? ram[addr & 0x7FF] : 0; }
bool cyc_mod_poke(uint16_t addr, uint8_t value) {
    if (addr >= 0x2000) return false;
    ram[addr & 0x7FF] = value;
    return true;
}
void cyc_mod_regs(CycModRegs *out) { *out = cpu; }
void cyc_mod_set_regs(const CycModRegs *in) { uint16_t pc = cpu.pc; cpu = *in; cpu.pc = pc; }
bool cyc_mod_isolated(void) { return isolated; }
bool cyc_mod_call_commit(uint16_t addr, CycModRegs *regs) {
    if (addr != SOUND_PLAY_EFFECT) return false;
    if (regs->a == SOUND_INVALID_CHOICE) ++invalid_sounds;
    return true;
}
bool fax_recharge_on(void) { return recharge; }
int nes_mod_register_function_entry_plugin(const char *id, uint16_t addr, NESModFunctionEntryCallback cb) {
    return !strcmp(id, touch_id) && addr == HANDLE_TOUCH_ITEM && cb == touch_hook;
}
int nes_mod_set_function_hook_enabled(const char *id, int on) {
    if (strcmp(id, touch_id)) return 0;
    touch_on = on;
    return 1;
}
int nes_mod_register_reset_callback(NESModActivationCallback cb) { return cb == deactivate; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    return !strcmp(id, "faxanadu.kept-finds") && cb == activate;
}
void nes_mod_set_local_only(const char *name, int required) { (void)name; (void)required; }

/* Player_PickUpItem ($C8CD): LDX NumberOfItems, CPX #$08, BCS return, STA
 * ItemInventory,X, INX, STX NumberOfItems. Recharging items' hook at its
 * entry takes Wing Boots or an Hour Glass the hero holds (readies his). */
static void pick_up_item(uint8_t item) {
    if (recharge && (item == ITEM_WING_BOOTS || item == ITEM_HOUR_GLASS) &&
        holds(item, ram[SELECTED_ITEM], &ram[ITEM_INVENTORY], ram[NUMBER_OF_ITEMS])) return;
    uint8_t x = ram[NUMBER_OF_ITEMS];
    if (x >= 8) return;
    ram[ITEM_INVENTORY + x] = item;
    ram[NUMBER_OF_ITEMS] = (uint8_t)(x + 1);
}

/* Player_PickUp ($C764) for the finds: the quest pickups set their bit, then
 * every one runs its line, plays sound 8 and adds its item. */
static void pick_up(uint8_t entity) {
    if (entity == MATTOCK_QUEST_FIND) ram[QUESTS] |= QUEST_MATTOCK;
    if (entity == WING_BOOTS_QUEST_FIND) ram[QUESTS] |= QUEST_WING_BOOTS;
    ++lines;
    ++sounds_queued;
    pick_up_item(find_item(entity));
}

/* Player_HandleTouchItem ($88FB) with X the slot, the hook at its entry. */
static void touch(uint8_t slot) {
    cpu.x = slot;
    ram[0x0378] = slot;                                     /* CurrentSpriteIndex */
    if (touch_on == 1 && touch_hook(HANDLE_TOUCH_ITEM)) return;
    pick_up(ram[ENTITIES + slot]);
    ram[ENTITIES + slot] = 0xFF;
}

static void setup(uint8_t count, uint8_t selected) {
    memset(ram, 0, sizeof ram);
    memset(&ram[ENTITIES], 0xFF, 8);
    for (unsigned i = 0; i < 8; ++i) ram[ITEM_INVENTORY + i] = i < count ? ITEM_RED_POTION : 0;
    ram[NUMBER_OF_ITEMS] = count;
    ram[SELECTED_ITEM] = selected;
    cpu = (CycModRegs){ 0 };
    isolated = false;
    lines = sounds_queued = invalid_sounds = 0;
}

static unsigned listed(uint8_t item) {
    unsigned n = 0;
    for (unsigned i = 0; i < ram[NUMBER_OF_ITEMS]; ++i) n += ram[ITEM_INVENTORY + i] == item;
    return n;
}

int main(void) {
    CHECK(nes_mod_register_function_entry_plugin(touch_id, HANDLE_TOUCH_ITEM, touch_hook));
    CHECK(!nes_mod_register_function_entry_plugin(touch_id, 0x88FCu, touch_hook));
    activate();
    CHECK(enabled && touch_on == 1);
    recharge = true;

    /* What a touch would lose. */
    uint8_t full[8] = { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10 };
    CHECK(would_lose(MATTOCK_QUEST_FIND, 8, ITEM_NONE, full, true));
    CHECK(would_lose(MATTOCK_FIND, 8, ITEM_NONE, full, true));
    CHECK(would_lose(RED_POTION_FIND, 8, ITEM_RED_POTION, full, true));
    CHECK(!would_lose(MATTOCK_QUEST_FIND, 7, ITEM_NONE, full, true));      /* room */
    CHECK(!would_lose(0x58, 8, ITEM_NONE, full, true));                     /* the Battle Suit: its own list */
    CHECK(!would_lose(0x57, 8, ITEM_NONE, full, true));                     /* the Magical Rod: a bit */
    CHECK(!would_lose(0x5E, 8, ITEM_NONE, full, true));                     /* the Poison: never added */
    CHECK(!would_lose(0x5F, 8, ITEM_NONE, full, true));                     /* the Glove: timed */
    CHECK(would_lose(WING_BOOTS_QUEST_FIND, 8, ITEM_NONE, full, true));    /* none held */
    CHECK(!would_lose(WING_BOOTS_QUEST_FIND, 8, ITEM_WING_BOOTS, full, true));   /* held: Recharging takes it */
    uint8_t boots_listed[8] = { 0x10, 0x10, 0x10, ITEM_WING_BOOTS, 0x10, 0x10, 0x10, 0x10 };
    CHECK(!would_lose(WING_BOOTS_FIND, 8, ITEM_NONE, boots_listed, true));
    CHECK(would_lose(WING_BOOTS_FIND, 8, ITEM_NONE, boots_listed, false));  /* no Recharging: lost */
    CHECK(!would_lose(HOUR_GLASS_FIND, 8, ITEM_HOUR_GLASS, full, true));
    CHECK(would_lose(HOUR_GLASS_FIND, 8, ITEM_WING_BOOTS, full, true));     /* boots held, not a glass */

    /* The original: Ripasheiku's Mattock with 8 listed is said, its bit set,
     * and nothing added; the sprite is gone. */
    touch_on = 0;
    setup(8, ITEM_RED_POTION);
    ram[ENTITIES + 3] = MATTOCK_QUEST_FIND;
    touch(3);
    CHECK(lines == 1 && ram[QUESTS] == QUEST_MATTOCK && listed(ITEM_MATTOCK) == 0 && ram[ENTITIES + 3] == 0xFF);

    /* With the fix: nothing said, no bit, the sprite stays, one sound for
     * the whole touch however many frames it lasts. */
    touch_on = 1;
    setup(8, ITEM_RED_POTION);
    ram[ENTITIES + 3] = MATTOCK_QUEST_FIND;
    for (unsigned f = 0; f < 30; ++f) {
        ram[INTERRUPT_COUNTER] = (uint8_t)(0xF0 + f);           /* wraps, as the counter does */
        touch(3);
    }
    CHECK(lines == 0 && ram[QUESTS] == 0 && ram[ENTITIES + 3] == MATTOCK_QUEST_FIND && listed(ITEM_MATTOCK) == 0);
    CHECK(invalid_sounds == 1 && refused >= 30 && last_entity == MATTOCK_QUEST_FIND);

    /* Walking off and back on later is a new touch: one more sound. */
    ram[INTERRUPT_COUNTER] = (uint8_t)(ram[INTERRUPT_COUNTER] + TOUCH_GAP + 1);
    touch(3);
    CHECK(invalid_sounds == 2 && ram[ENTITIES + 3] == MATTOCK_QUEST_FIND);
    /* Another find in another slot in the same frame sounds too. */
    ram[ENTITIES + 5] = RED_POTION_FIND;
    touch(5);
    CHECK(invalid_sounds == 3 && ram[ENTITIES + 5] == RED_POTION_FIND && lines == 0);

    /* Room made (a potion drunk): the next touch picks it up, as the game does. */
    ram[NUMBER_OF_ITEMS] = 7;
    touch(3);
    CHECK(lines == 1 && ram[QUESTS] == QUEST_MATTOCK && listed(ITEM_MATTOCK) == 1 && ram[ENTITIES + 3] == 0xFF);
    CHECK(invalid_sounds == 3);

    /* Zorugeriru's Wing Boots, none held: left lying, no bit. */
    setup(8, ITEM_RED_POTION);
    ram[ENTITIES + 1] = WING_BOOTS_QUEST_FIND;
    touch(1);
    CHECK(lines == 0 && ram[QUESTS] == 0 && ram[ENTITIES + 1] == WING_BOOTS_QUEST_FIND && invalid_sounds == 1);
    /* Held (selected), with Recharging: the game's own pickup, which Recharging
     * takes: the line, the bit, nothing added, the sprite gone, no sound. */
    setup(8, ITEM_WING_BOOTS);
    ram[ENTITIES + 1] = WING_BOOTS_QUEST_FIND;
    touch(1);
    CHECK(lines == 1 && ram[QUESTS] == QUEST_WING_BOOTS && listed(ITEM_WING_BOOTS) == 0 &&
          ram[ENTITIES + 1] == 0xFF && invalid_sounds == 0);

    /* Not full: every find goes in, untouched by the hook. */
    setup(5, ITEM_NONE);
    ram[ENTITIES + 0] = HOUR_GLASS_FIND;
    touch(0);
    CHECK(lines == 1 && listed(ITEM_HOUR_GLASS) == 1 && ram[NUMBER_OF_ITEMS] == 6 && invalid_sounds == 0);

    /* Anything else with the list full is the game's: the Battle Suit's own routine. */
    setup(8, ITEM_RED_POTION);
    ram[ENTITIES + 2] = 0x58;
    touch(2);
    CHECK(lines == 1 && ram[ENTITIES + 2] == 0xFF && invalid_sounds == 0);

    /* In Seamless's isolated rooms it does nothing. */
    setup(8, ITEM_RED_POTION);
    ram[ENTITIES + 3] = MATTOCK_QUEST_FIND;
    isolated = true;
    unsigned before = refused;
    CHECK(touch_hook(HANDLE_TOUCH_ITEM) == 0 && refused == before && invalid_sounds == 0);

    deactivate();
    CHECK(!enabled && touch_on == 0 && refused == 0 && beeps == 0);

    printf("finds_test: %u checks ok\n", checks);
    return 0;
}
