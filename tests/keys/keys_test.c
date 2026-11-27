/* Unit tests for auto keys and reusable tools (fax_keys.c), without the
 * emulator or a ROM. The module runs against a model of the machine: CPU RAM,
 * the registers, the hook sites as the engine dispatches them, and the door
 * and item code played step by step as the disassembly has them (the JSRs
 * push their real return addresses), so the tests check what the player ends
 * up with rather than restate the module's constants. */
#include "fax_keys.c"
#include <string.h>

static unsigned checks;
#define CHECK(test)                                                                                                    \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (!(test)) {                                                                                                 \
            fprintf(stderr, "FAIL line %u: %s\n", __LINE__, #test);                                                    \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)

/* ---- the machine ---- */
static uint8_t ram[0x800];
static CycModRegs cpu;

uint8_t cyc_mod_peek(uint16_t addr) { return addr < 0x2000 ? ram[addr & 0x7FF] : 0; }
bool cyc_mod_poke(uint16_t addr, uint8_t value) {
    if (addr >= 0x2000) return false;
    ram[addr & 0x7FF] = value;
    return true;
}
void cyc_mod_regs(CycModRegs *out) { *out = cpu; }

/* The engine's hook table: registration order, an enabled flag each. */
typedef struct {
    const char *id;
    uint16_t addr;
    NESModFunctionEntryCallback cb;
    int on;
} Site;
static Site sites[8];
static int site_count;
static bool reversed; /* fire sites at one address last-registered first */
int nes_mod_register_function_entry_plugin(const char *id, uint16_t addr, NESModFunctionEntryCallback cb) {
    if (site_count == 8) return 0;
    sites[site_count++] = (Site){ id, addr, cb, 0 };
    return 1;
}
int nes_mod_set_function_hook_enabled(const char *id, int on) {
    for (int i = 0; i < site_count; ++i)
        if (!strcmp(sites[i].id, id)) {
            sites[i].on = on;
            return 1;
        }
    return 0;
}
static NESModActivationCallback reset_cb, plugin_cb[2];
static const char *plugin_id[2];
static int plugin_count;
int nes_mod_register_reset_callback(NESModActivationCallback cb) {
    reset_cb = cb;
    return 1;
}
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    if (plugin_count == 2) return 0;
    plugin_id[plugin_count] = id;
    plugin_cb[plugin_count++] = cb;
    return 1;
}
void nes_mod_set_local_only(const char *name, int required) {
    (void)name;
    (void)required;
}
static NESModSavestateGet save_get;
static NESModSavestateSet save_set;
static NESModSavestateValidate save_validate;
int nes_mod_register_savestate_hook(const char *id, NESModSavestateGet get, NESModSavestateSet set) {
    if (strcmp(id, "faxanadu.keys")) return 0;
    save_get = get;
    save_set = set;
    return 1;
}
int nes_mod_register_savestate_validator(const char *id, NESModSavestateValidate validate) {
    if (strcmp(id, "faxanadu.keys")) return 0;
    save_validate = validate;
    return 1;
}

/* Hook sites at `addr`, as the scheduler runs them: enabled callbacks in
 * order until one handles the call. 1 when the routine was handled. */
static int fire(uint16_t addr) {
    for (int k = 0; k < site_count; ++k) {
        int i = reversed ? site_count - 1 - k : k;
        if (sites[i].addr == addr && sites[i].on && sites[i].cb(addr)) return 1;
    }
    return 0;
}
static void push(uint8_t v) {
    ram[0x100 | cpu.s] = v;
    cpu.s = (uint8_t)(cpu.s - 1);
}
static uint8_t pull(void) {
    cpu.s = (uint8_t)(cpu.s + 1);
    return ram[0x100 | cpu.s];
}
static void jsr(uint16_t at) {
    uint16_t last = (uint16_t)(at + 2);
    push((uint8_t)(last >> 8));
    push((uint8_t)last);
}
static uint16_t rts(void) {
    uint8_t lo = pull();
    return (uint16_t)((lo | pull() << 8) + 1);
}

/* ---- the game, as the disassembly has it ---- */
enum { RED_POTION = 0x10, WING_BOOTS = 0x0F, HOUR_GLASS = 0x0D, KEY_K = 0x05, KEY_J = 0x07, KEY_JO = 0x08 };
enum { DOOR_J = 4, RING_OF_ELF = 6 };
static int picture; /* the item the HUD's picture tiles show, NO_ITEM when blank */
static int message; /* the last script the door or item ran */
enum { SCRIPT_USED_KEY = 0x84, SCRIPT_USED_MATTOCK = 0x81, SCRIPT_KEY_MARK = 0x02, SCRIPT_RING_MARK = 0x7F };

static uint8_t selected(void) { return ram[SELECTED_ITEM]; }
static unsigned count(void) { return ram[NUMBER_OF_ITEMS]; }
static void give(const uint8_t *items, unsigned n, uint8_t selection) {
    memset(ram + ITEM_INVENTORY, 0xEE, 16); /* stale bytes past the count */
    memcpy(ram + ITEM_INVENTORY, items, n);
    ram[NUMBER_OF_ITEMS] = (uint8_t)n;
    ram[SELECTED_ITEM] = selection;
    picture = selection;
}
static bool holds(const uint8_t *items, unsigned n) { return count() == n && !memcmp(ram + ITEM_INVENTORY, items, n); }

/* UI_ClearSelectedItemPic, entered by the JSR at `from`. */
static void clear_picture(uint16_t from) {
    jsr(from);
    if (!fire(UI_CLEAR_SELECTED_ITEM_PIC)) picture = NO_ITEM; /* its four tiles of 0 */
    CHECK(rts() == from + 3);
}
/* Player_CheckHandleEnterDoor at a locked door: JSR Game_RunDoorRequirementHandler
 * ($E539), the handler for the requirement, and its RTS. True if it opened. */
static bool press_up_at_door(uint8_t requirement) {
    cpu.s = 0xF0;
    ram[CURRENT_DOOR_KEY_REQUIREMENT] = requirement;
    message = 0;
    jsr(0xE539);
    fire(GAME_RUN_DOOR_REQUIREMENT_HANDLER); /* it never handles */
    if (requirement >= DOOR_A_KEY && requirement <= DOOR_JO_KEY) {
        if (selected() == ITEM_KEY_A + requirement - DOOR_A_KEY) { /* LDA SelectedItem, CMP #key, BEQ */
            message = SCRIPT_USED_KEY;                             /* Game_UnlockDoorWithUsableItem */
            ram[SELECTED_ITEM] = NO_ITEM;
            clear_picture(UNLOCK_CLEARS_PICTURE);
            ram[CURRENT_DOOR_KEY_REQUIREMENT] = 0; /* Game_UnlockDoor */
        } else {
            message = requirement == DOOR_J ? SCRIPT_KEY_MARK : 0x7E;
        }
    } else if (requirement >= RING_OF_ELF) {
        message = SCRIPT_RING_MARK; /* no ring in these tests */
    }
    CHECK(rts() == 0xE53C && cpu.s == 0xF0);
    return ram[CURRENT_DOOR_KEY_REQUIREMENT] == 0;
}
/* Down-B with an item that clears itself: Player_UseMattock and the others
 * JSR Player_ClearSelectedItem (PHA, LDA #$FF, STA SelectedItem,
 * JSR UI_ClearSelectedItemPic at $C4C5, PLA). */
static void use_item(uint16_t clear_call, int script) {
    cpu.s = 0xE0;
    message = script;
    jsr(clear_call);
    if (!fire(PLAYER_CLEAR_SELECTED_ITEM)) {
        push(0x42);
        ram[SELECTED_ITEM] = NO_ITEM;
        clear_picture(0xC4C5);
        CHECK(pull() == 0x42);
    }
    CHECK(rts() == clear_call + 3 && cpu.s == 0xE0);
}
#define USE_MATTOCK() use_item(MATTOCK_CLEARS_ITEM, SCRIPT_USED_MATTOCK)

static void play(bool auto_keys, bool reusable) { /* a launch with these toggles */
    reset_cb();
    if (auto_keys) plugin_cb[0]();
    if (reusable) plugin_cb[1]();
    memset(ram, 0, sizeof ram);
    reversed = false;
}

/* ---- tests ---- */
static void registers(void) {
    CHECK(site_count == 4 && plugin_count == 2 && reset_cb == deactivate);
    CHECK(!strcmp(plugin_id[0], "faxanadu.auto-keys") && !strcmp(plugin_id[1], "faxanadu.reusable-tools"));
    CHECK(save_get && save_set && save_validate);
    play(true, true);
    for (int i = 0; i < site_count; ++i) CHECK(sites[i].on);
    play(false, false);
    for (int i = 0; i < site_count; ++i) CHECK(!sites[i].on);
    play(true, false);
    CHECK(sites[0].on && sites[1].on && !sites[2].on && !sites[3].on);
}

static void inventory_lookup(void) {
    play(true, false);
    const uint8_t items[] = { RED_POTION, KEY_K, KEY_J, WING_BOOTS };
    give(items, 4, NO_ITEM);
    CHECK(inventory_find(RED_POTION) == 0 && inventory_find(KEY_J) == 2 && inventory_find(WING_BOOTS) == 3);
    CHECK(inventory_find(KEY_JO) == -1);
    CHECK(inventory_find(0xEE) == -1); /* stale bytes are not items */
    ram[NUMBER_OF_ITEMS] = 0;
    CHECK(inventory_find(RED_POTION) == -1);
    memset(ram + ITEM_INVENTORY, KEY_J, 16);
    ram[ITEM_INVENTORY + 8] = KEY_JO;
    ram[NUMBER_OF_ITEMS] = 12; /* the game holds 8 at most */
    CHECK(inventory_find(KEY_JO) == -1 && inventory_find(KEY_J) == 0);
    give(items, 4, NO_ITEM);
    inventory_remove(1);
    const uint8_t left[] = { RED_POTION, KEY_J, WING_BOOTS };
    CHECK(holds(left, 3));
    inventory_remove(-1);
    inventory_remove(3);
    CHECK(holds(left, 3));
    for (uint8_t need = 0; need <= 8; ++need) {
        ram[CURRENT_DOOR_KEY_REQUIREMENT] = need;
        CHECK(door_key() == (need >= 1 && need <= 5 ? 3 + need : 0));
    }
}

static void auto_keys_alone(void) {
    play(true, false);
    const uint8_t items[] = { RED_POTION, KEY_J, WING_BOOTS };
    give(items, 3, ITEM_MATTOCK);
    CHECK(press_up_at_door(DOOR_J));
    CHECK(message == SCRIPT_USED_KEY); /* the game's own message */
    const uint8_t used[] = { RED_POTION, WING_BOOTS };
    CHECK(holds(used, 2));                                        /* the key is used up */
    CHECK(selected() == ITEM_MATTOCK && picture == ITEM_MATTOCK); /* the selection is as it was */
    CHECK(!lend.active && auto_opened == 1 && keys_kept == 0);
    CHECK(!press_up_at_door(DOOR_J)); /* no key left */
    CHECK(message == SCRIPT_KEY_MARK && holds(used, 2) && selected() == ITEM_MATTOCK);
    /* Nothing selected before, nothing after. */
    const uint8_t one[] = { KEY_J };
    give(one, 1, NO_ITEM);
    CHECK(press_up_at_door(DOOR_J) && count() == 0 && selected() == NO_ITEM && picture == NO_ITEM);
    /* Another key selected: the door takes its own, the K key stays selected. */
    give(items, 3, KEY_K);
    CHECK(press_up_at_door(DOOR_J) && holds(used, 2) && selected() == KEY_K && picture == KEY_K);
    /* The key selected already: the game's own path, which uses it up. */
    const uint8_t two[] = { KEY_J, RED_POTION };
    give(two, 2, KEY_J);
    CHECK(press_up_at_door(DOOR_J) && holds(two, 2) && selected() == NO_ITEM && picture == NO_ITEM);
    /* The first of two copies goes. */
    const uint8_t pair[] = { KEY_J, RED_POTION, KEY_J }, rest[] = { RED_POTION, KEY_J };
    give(pair, 3, NO_ITEM);
    CHECK(press_up_at_door(DOOR_J) && holds(rest, 2));
}

static void both_on(void) {
    for (int order = 0; order < 2; ++order) {
        play(true, true);
        reversed = order; /* either site may fire first */
        const uint8_t items[] = { RED_POTION, KEY_J, WING_BOOTS };
        give(items, 3, ITEM_MATTOCK);
        CHECK(press_up_at_door(DOOR_J) && message == SCRIPT_USED_KEY);
        CHECK(holds(items, 3) && selected() == ITEM_MATTOCK && picture == ITEM_MATTOCK);
        CHECK(press_up_at_door(DOOR_J) && holds(items, 3) && selected() == ITEM_MATTOCK); /* the same key again */
        CHECK(auto_opened == 2 && keys_kept == 2 && !lend.active);
        /* Selected by hand, it stays selected. */
        give(items, 3, KEY_J);
        CHECK(press_up_at_door(DOOR_J) && selected() == KEY_J && picture == KEY_J && holds(items, 3));
        CHECK(auto_opened == 2 && keys_kept == 3);
        /* Without its key, the door's own message. */
        const uint8_t none[] = { RED_POTION, KEY_K };
        give(none, 2, ITEM_MATTOCK);
        CHECK(!press_up_at_door(DOOR_J) && message == SCRIPT_KEY_MARK && holds(none, 2) && selected() == ITEM_MATTOCK);
    }
}

static void reusable_alone(void) {
    play(false, true);
    const uint8_t items[] = { RED_POTION, KEY_J };
    give(items, 2, KEY_J);
    CHECK(press_up_at_door(DOOR_J) && message == SCRIPT_USED_KEY);
    CHECK(selected() == KEY_J && picture == KEY_J && holds(items, 2) && keys_kept == 1);
    CHECK(press_up_at_door(DOOR_J) && selected() == KEY_J && keys_kept == 2);
    /* Unselected, the door stays shut: that is auto keys' job. */
    give(items, 2, ITEM_MATTOCK);
    CHECK(!press_up_at_door(DOOR_J) && message == SCRIPT_KEY_MARK && selected() == ITEM_MATTOCK);
}

static void stock_when_off(void) {
    play(false, false);
    const uint8_t items[] = { RED_POTION, KEY_J };
    give(items, 2, ITEM_MATTOCK);
    CHECK(!press_up_at_door(DOOR_J) && message == SCRIPT_KEY_MARK && holds(items, 2) && selected() == ITEM_MATTOCK);
    give(items, 2, KEY_J);
    CHECK(press_up_at_door(DOOR_J) && selected() == NO_ITEM && picture == NO_ITEM && holds(items, 2));
    give(items, 2, ITEM_MATTOCK);
    USE_MATTOCK();
    CHECK(selected() == NO_ITEM && picture == NO_ITEM);
    CHECK(auto_opened == 0 && keys_kept == 0 && mattock_kept == 0);
}

static void rings_untouched(void) {
    play(true, true);
    const uint8_t items[] = { KEY_J };
    give(items, 1, ITEM_MATTOCK);
    for (uint8_t ring = RING_OF_ELF; ring <= 8; ++ring) {
        CHECK(!press_up_at_door(ring) && message == SCRIPT_RING_MARK);
        CHECK(!lend.active && selected() == ITEM_MATTOCK && holds(items, 1));
    }
    CHECK(press_up_at_door(0) && !lend.active && selected() == ITEM_MATTOCK); /* no lock at all */
}

static void mattock_kept_and_consumables_used(void) {
    play(true, true);
    const uint8_t items[] = { RED_POTION };
    give(items, 1, ITEM_MATTOCK);
    USE_MATTOCK();
    CHECK(message == SCRIPT_USED_MATTOCK && selected() == ITEM_MATTOCK && picture == ITEM_MATTOCK);
    USE_MATTOCK();
    CHECK(selected() == ITEM_MATTOCK && mattock_kept == 2 && holds(items, 1));
    const struct {
        uint16_t call;
        uint8_t item;
    } used_up[] = {
        { 0xC53B, RED_POTION },
        { 0xC581, WING_BOOTS },
        { 0xC5D0, HOUR_GLASS }, /* Player_Use{RedPotion,WingBoots,HourGlass} */
    };
    for (unsigned i = 0; i < 3; ++i) {
        give(items, 1, used_up[i].item);
        use_item(used_up[i].call, 0);
        CHECK(selected() == NO_ITEM && picture == NO_ITEM);
    }
    CHECK(mattock_kept == 2);
    /* Auto keys alone keeps nothing. */
    play(true, false);
    give(items, 1, ITEM_MATTOCK);
    USE_MATTOCK();
    CHECK(selected() == NO_ITEM && picture == NO_ITEM);
}

static void other_callers_untouched(void) {
    /* A lent key does not leak into an unrelated clear of the picture. */
    play(true, false);
    lend = (Lend){ true, KEY_J, ITEM_MATTOCK };
    ram[CURRENT_DOOR_KEY_REQUIREMENT] = DOOR_J;
    ram[SELECTED_ITEM] = RED_POTION;
    picture = RED_POTION;
    use_item(0xC53B, 0);
    CHECK(selected() == NO_ITEM && picture == NO_ITEM && lend.active);
    /* A lend for another door is left to the game. */
    const uint8_t items[] = { KEY_K };
    give(items, 1, KEY_J);
    lend = (Lend){ true, KEY_K, ITEM_MATTOCK };
    CHECK(press_up_at_door(DOOR_J) && !lend.active); /* the door hook clears it first */
    lend = (Lend){ true, KEY_K, ITEM_MATTOCK };
    cpu.s = 0xF0;
    ram[CURRENT_DOOR_KEY_REQUIREMENT] = DOOR_J;
    ram[SELECTED_ITEM] = NO_ITEM;
    picture = KEY_J;
    clear_picture(UNLOCK_CLEARS_PICTURE); /* reached without the door hook */
    CHECK(!lend.active && selected() == NO_ITEM && picture == NO_ITEM && holds(items, 1));
}

static void save_states(void) {
    play(true, false);
    const uint8_t items[] = { RED_POTION, KEY_J };
    give(items, 2, ITEM_MATTOCK);
    /* Stop after the handler, during the "used key" message, and save. */
    ram[CURRENT_DOOR_KEY_REQUIREMENT] = DOOR_J;
    cpu.s = 0xF0;
    door_hook(GAME_RUN_DOOR_REQUIREMENT_HANDLER);
    CHECK(lend.active && selected() == KEY_J);
    uint8_t buf[16];
    int n = save_get(buf, sizeof buf);
    CHECK(n == 3 && save_validate(buf, n));
    lend = (Lend){ 0 };
    CHECK(save_set(buf, n) && lend.active && lend.key == KEY_J && lend.prev == ITEM_MATTOCK);
    ram[SELECTED_ITEM] = NO_ITEM; /* the rest of the door */
    clear_picture(UNLOCK_CLEARS_PICTURE);
    const uint8_t used[] = { RED_POTION };
    CHECK(holds(used, 1) && selected() == ITEM_MATTOCK && picture == ITEM_MATTOCK);
    /* A state without the record lends nothing; bad records are refused. */
    lend = (Lend){ true, KEY_J, 0 };
    CHECK(save_validate(buf, 0) && save_set(buf, 0) && !lend.active);
    const uint8_t bad[][3] = { { 2, KEY_J, 0 }, { 1, 0x03, 0 }, { 1, 0x09, 0 } };
    for (unsigned i = 0; i < 3; ++i) CHECK(!save_validate(bad[i], 3) && !save_set(bad[i], 3));
    CHECK(!save_validate(buf, 2));
    CHECK(save_get(buf, 2) == -1);
}

static void off_means_off(void) {
    play(true, true);
    lend = (Lend){ true, KEY_J, 0 };
    auto_opened = keys_kept = mattock_kept = 3;
    deactivate();
    CHECK(!auto_on && !reuse_on && !lend.active && auto_opened + keys_kept + mattock_kept == 0);
    for (int i = 0; i < site_count; ++i) CHECK(!sites[i].on);
}

int main(void) {
    registers();
    inventory_lookup();
    auto_keys_alone();
    both_on();
    reusable_alone();
    stock_when_off();
    rings_untouched();
    mattock_kept_and_consumables_used();
    other_callers_untouched();
    save_states();
    off_means_off();
    printf("keys_test: %u checks passed\n", checks);
    return 0;
}
