/* Unit tests for Found items (fax_found.c), without the emulator or a ROM.
 *
 * The module runs against a model of the machine: CPU RAM, the registers, the
 * hook sites as the engine fires them (a handled one returns as the routine's
 * RTS), and the game's code around them as the disassembly has it: a
 * random-show item's behavior on its first frame (JSR Screen_Is3Of4Visits,
 * then BCC: it stays, else JMP Sprites_Remove), Screen_Is3Of4Visits itself
 * (INC Screen_TrackedVisitCount, C set on 3 counts of 4), and
 * Player_HandleTouchItem (the pickup, then the slot emptied), with Kept
 * finds' site before it refusing a find the full Items list would lose. So
 * the tests check what lies in the world, what the counter does, and what
 * the file and the save state keep. The shops' rule (fax_found_unsold) is
 * checked here as data; Quick shops' use of it is tests/shops/shops_test.c's. */
#include "fax_found.c"

static unsigned checks;
#define CHECK(test) do{++checks;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(1);}}while(0)

#define VISIT_COUNT 0x043Au   /* Screen_TrackedVisitCount */

/* ---- the machine ---- */
static uint8_t ram[0x800];
static CycModRegs cpu;
static bool isolated;
static bool kept_finds_refuses;              /* Kept finds' site before ours: the Items list is full */

uint8_t cyc_mod_peek(uint16_t addr) { return addr < 0x2000 ? ram[addr & 0x7FF] : 0; }
bool cyc_mod_poke(uint16_t addr, uint8_t value) {
    if (addr >= 0x2000) return false;
    ram[addr & 0x7FF] = value;
    return true;
}
void cyc_mod_regs(CycModRegs *out) { *out = cpu; }
void cyc_mod_set_regs(const CycModRegs *in) { uint16_t pc = cpu.pc; cpu = *in; cpu.pc = pc; }
bool cyc_mod_isolated(void) { return isolated; }

typedef struct { const char *id; uint16_t addr; NESModFunctionEntryCallback cb; int on; } Site;
static Site sites[4];
static unsigned site_count;
int nes_mod_register_function_entry_plugin(const char *id, uint16_t addr, NESModFunctionEntryCallback cb) {
    if (site_count == 4) return 0;
    sites[site_count++] = (Site){ id, addr, cb, 0 };
    return 1;
}
int nes_mod_set_function_hook_enabled(const char *id, int on) {
    for (unsigned i = 0; i < site_count; ++i)
        if (!strcmp(sites[i].id, id)) { sites[i].on = on; return 1; }
    return 0;
}
static NESModActivationCallback reset_cb, activate_cb;
int nes_mod_register_reset_callback(NESModActivationCallback cb) { reset_cb = cb; return 1; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    if (strcmp(id, "faxanadu.found-items")) return 0;
    activate_cb = cb;
    return 1;
}
void nes_mod_set_local_only(const char *name, int required) { (void)name; (void)required; }
static NESModSavestateGet save_get;
static NESModSavestateSet save_set;
static NESModSavestateValidate save_validate;
int nes_mod_register_savestate_hook(const char *id, NESModSavestateGet get, NESModSavestateSet set) {
    if (strcmp(id, "faxanadu.found-items")) return 0;
    save_get = get; save_set = set;
    return 1;
}
int nes_mod_register_savestate_validator(const char *id, NESModSavestateValidate validate) {
    if (strcmp(id, "faxanadu.found-items")) return 0;
    save_validate = validate;
    return 1;
}

/* Save files: the file in play's fields. */
static FaxSaveListener listener;
static struct { char key[32], value[96]; } fields[8];
static unsigned field_count;
bool fax_save_listen(FaxSaveListener l) { listener = l; return true; }
static int field_at(const char *key) {
    for (unsigned i = 0; i < field_count; ++i) if (!strcmp(fields[i].key, key)) return (int)i;
    return -1;
}
const char *fax_save_get(const char *key) { int i = field_at(key); return i >= 0 ? fields[i].value : NULL; }
bool fax_save_set(const char *key, const char *value) {
    int i = field_at(key);
    if (!value) { if (i >= 0) fields[i] = fields[--field_count]; return true; }
    if (i < 0) { CHECK(field_count < 8); i = (int)field_count++; snprintf(fields[i].key, 32, "%s", key); }
    snprintf(fields[i].value, 96, "%s", value);
    return true;
}

static int fire(uint16_t addr) {
    for (unsigned i = 0; i < site_count; ++i)
        if (sites[i].addr == addr && sites[i].on && sites[i].cb(addr)) return 1;
    return 0;
}

/* Screen_Is3Of4Visits, as the ROM has it: INC Screen_TrackedVisitCount, LDA,
 * CMP #$04, BCC: C set (true) below 4; else 0 stored, CLC. */
static void is_3_of_4_visits(void) {
    uint8_t n = ++ram[VISIT_COUNT];
    if (n < 4) { cpu.p |= P_C; return; }
    ram[VISIT_COUNT] = 0;
    cpu.p &= (uint8_t)~P_C;
}
/* A random-show item's behavior, its first frame, in slot `slot`: JSR
 * Screen_Is3Of4Visits (the hooks at its entry first), BCC: it stays; else
 * JMP Sprites_Remove. Whether it stayed. */
static bool first_frame(unsigned slot) {
    cpu.x = (uint8_t)slot;
    cpu.p ^= P_C;                                  /* whatever C was, the answer sets it */
    if (!fire(SCREEN_IS_3_OF_4_VISITS)) is_3_of_4_visits();
    if (cpu.p & P_C) { ram[SPRITE_ENTITIES + slot] = 0xFF; return false; }
    return true;
}
/* The screen loads with this item in slot 3, and its behavior runs. */
static bool visit(unsigned area, unsigned screen, unsigned entity) {
    ram[AREA_CURRENT_AREA] = (uint8_t)area;
    ram[AREA_CURRENT_SCREEN] = (uint8_t)screen;
    memset(&ram[SPRITE_ENTITIES], 0xFF, SLOTS);
    ram[SPRITE_ENTITIES + 3] = (uint8_t)entity;
    return first_frame(3);
}
/* Player_HandleTouchItem, the item in slot 3: Kept finds' site first, which
 * handles a find the full list would lose (the sprite stays), then ours, then
 * the pickup (an Hour Glass to the list) and the slot emptied. */
static void touch(void) {
    cpu.x = 3;
    if (kept_finds_refuses) return;
    CHECK(!fire(PLAYER_HANDLE_TOUCH_ITEM));        /* it only observes */
    if (ram[SPRITE_ENTITIES + 3] == FAX_FOUND_GLASS) ram[ITEM_INVENTORY + ram[NUMBER_OF_ITEMS]++] = ITEM_HOUR_GLASS;
    ram[SPRITE_ENTITIES + 3] = 0xFF;
}
static void play(bool on) {
    reset_cb();
    memset(ram, 0, sizeof ram);
    ram[SELECTED_ITEM] = 0xFF;
    field_count = 0;
    isolated = kept_finds_refuses = false;
    if (on) activate_cb();
    listener(FAX_SAVE_NEW_GAME);
}

enum { TRUNK = 1, MIST = 2, BUILDINGS = 4, BRANCH = 5, DARTMOOR = 6 };
enum { MATTOCK_FIND = 0x50, RED_POTION_FIND = 0x5D, ELIXIR_FIND = 0x4D, BOOTS_QUEST_FIND = 0x5C, ZOZURA = 0x47 };

/* ---- tests ---- */

static void registers(void) {
    CHECK(site_count == 2 && reset_cb && activate_cb && save_get && save_set && save_validate && listener);
    CHECK(sites[0].addr == 0xA529 && sites[1].addr == 0x88FB);
}

/* What no tool shop sells: the Wing Boots, the Hour Glass, the Elixir, as
 * the shops' lists name them, and nothing else. */
static void unsold(void) {
    CHECK(FAX_FOUND_UNSOLD == 3);
    CHECK(fax_found_unsold[0] == 0x8F && fax_found_unsold[1] == 0x8D && fax_found_unsold[2] == 0x92);
}

/* The rule, by place and entity: every Wing Boots $55 gone, every Hour Glass
 * $56 gone but Mist $30's, every other sprite as it is (the Elixir, the
 * Mattock, the Red Potion, Zorugeriru's pair, an enemy). The places are bank
 * 11's (docs/GAME_INTERNALS.md, "Items in the world"). */
static void the_rule(void) {
    static const uint8_t boots[][2] = { { TRUNK, 0x2D }, { MIST, 0x11 }, { MIST, 0x1B }, { MIST, 0x32 },
                                        { BUILDINGS, 0x1F }, { BRANCH, 0x1C }, { BRANCH, 0x22 } };
    static const uint8_t glasses[][2] = { { TRUNK, 0x13 }, { BRANCH, 0x16 }, { DARTMOOR, 0x04 }, { DARTMOOR, 0x12 } };
    for (unsigned i = 0; i < sizeof boots / sizeof boots[0]; ++i)
        CHECK(fax_found_spawn(boots[i][0], boots[i][1], 0x55) == FAX_FOUND_GONE);
    for (unsigned i = 0; i < sizeof glasses / sizeof glasses[0]; ++i)
        CHECK(fax_found_spawn(glasses[i][0], glasses[i][1], 0x56) == FAX_FOUND_GONE);
    CHECK(fax_found_spawn(MIST, 0x30, 0x56) == FAX_FOUND_FIXED);
    CHECK(fax_found_spawn(TRUNK, 0x30, 0x56) == FAX_FOUND_GONE);   /* another area's $30 */
    CHECK(fax_found_spawn(MIST, 0x31, 0x56) == FAX_FOUND_GONE);
    static const uint8_t others[] = { ELIXIR_FIND, MATTOCK_FIND, RED_POTION_FIND, BOOTS_QUEST_FIND, ZOZURA, 0x5F, 0x60 };
    for (unsigned i = 0; i < sizeof others; ++i) {
        CHECK(fax_found_spawn(MIST, 0x30, others[i]) == FAX_FOUND_AS_IS);
        CHECK(fax_found_spawn(TRUNK, 0x3B, others[i]) == FAX_FOUND_AS_IS);
    }
}

/* A former Wing Boots spot shows none, at any count, and the count stays
 * where it was; the Mattock beside them counts as before. */
static void boots_are_gone(void) {
    play(true);
    for (unsigned n = 0; n < 8; ++n) {
        ram[VISIT_COUNT] = (uint8_t)(n % 4);
        CHECK(!visit(TRUNK, 0x2D, 0x55) && ram[SPRITE_ENTITIES + 3] == 0xFF && ram[VISIT_COUNT] == n % 4);
    }
    CHECK(gone == 8 && shown == 0);
    ram[VISIT_COUNT] = 3;
    CHECK(visit(TRUNK, 0x0E, MATTOCK_FIND) && ram[VISIT_COUNT] == 0);   /* the 4th: there, counted */
    CHECK(!visit(TRUNK, 0x0E, MATTOCK_FIND) && ram[VISIT_COUNT] == 1);
    CHECK(gone == 8);
    /* Off, the original: one visit in four. */
    play(false);
    unsigned there = 0;
    for (unsigned n = 0; n < 8; ++n) there += visit(TRUNK, 0x2D, 0x55);
    CHECK(there == 2 && ram[VISIT_COUNT] == 0);
}

/* The other Hour Glasses are gone; Mist $30's is there at every count until
 * taken, then never; the count untouched. A full Items list leaves it lying
 * (Kept finds), not taken. */
static void the_fixed_glass(void) {
    play(true);
    for (unsigned n = 0; n < 4; ++n) {
        ram[VISIT_COUNT] = (uint8_t)n;
        CHECK(!visit(TRUNK, 0x13, 0x56) && ram[VISIT_COUNT] == n);
        CHECK(visit(MIST, 0x30, 0x56) && ram[VISIT_COUNT] == n && fax_found_glass_there());
    }
    CHECK(shown == 4 && gone == 4 && last.rule == FAX_FOUND_FIXED && last.left == 1);
    kept_finds_refuses = true;
    touch();
    CHECK(!taken && ram[SPRITE_ENTITIES + 3] == 0x56);
    kept_finds_refuses = false;
    touch();
    CHECK(taken && takes == 1 && ram[NUMBER_OF_ITEMS] == 1 && !fax_found_glass_there());
    CHECK(!visit(MIST, 0x30, 0x56) && away == 1);
    /* Taken, then gone from his list (not by any way the game has, with the
     * remaster): still taken. */
    ram[NUMBER_OF_ITEMS] = 0;
    CHECK(!visit(MIST, 0x30, 0x56));
    /* Not taken, but he holds one (an Hour Glass from before): it does not lie
     * there, selected or listed. */
    play(true);
    ram[SELECTED_ITEM] = ITEM_HOUR_GLASS;
    CHECK(!fax_found_glass_there() && !visit(MIST, 0x30, 0x56));
    ram[SELECTED_ITEM] = 0xFF; ram[ITEM_INVENTORY + 1] = ITEM_HOUR_GLASS; ram[NUMBER_OF_ITEMS] = 2;
    CHECK(!visit(MIST, 0x30, 0x56));
    ram[NUMBER_OF_ITEMS] = 1;                       /* past the count: a stale byte */
    CHECK(visit(MIST, 0x30, 0x56));
    /* Touching anything else takes nothing. */
    play(true);
    visit(MIST, 0x30, ELIXIR_FIND);
    touch();
    ram[AREA_CURRENT_SCREEN] = 0x31;
    ram[SPRITE_ENTITIES + 3] = 0x56;               /* an Hour Glass on another screen (none lies there) */
    touch();
    CHECK(!taken && takes == 0);
}

/* In Seamless's isolated rooms the answers are the same, and nothing counts
 * or is taken there. */
static void isolated_rooms(void) {
    play(true);
    isolated = true;
    CHECK(!visit(TRUNK, 0x2D, 0x55) && visit(MIST, 0x30, 0x56));
    touch();
    CHECK(!taken && gone == 0 && shown == 0 && takes == 0);
    isolated = false;
}

/* The file: taken is written, read back on continuing, reset by a new game. */
static void the_file(void) {
    play(true);
    listener(FAX_SAVE_WRITING);
    CHECK(!fax_save_get(FIELD_GLASS));
    visit(MIST, 0x30, 0x56);
    touch();
    listener(FAX_SAVE_WRITING);
    CHECK(fax_save_get(FIELD_GLASS) && !strcmp(fax_save_get(FIELD_GLASS), "taken"));
    listener(FAX_SAVE_NEW_GAME);
    CHECK(!taken);
    listener(FAX_SAVE_CONTINUED);
    CHECK(taken);
    ram[NUMBER_OF_ITEMS] = 0;                       /* the file is what says so */
    CHECK(!visit(MIST, 0x30, 0x56));
    fax_save_set(FIELD_GLASS, "x");
    listener(FAX_SAVE_CONTINUED);
    CHECK(!taken && visit(MIST, 0x30, 0x56));
    fax_save_set(FIELD_GLASS, "taken");
    listener(FAX_SAVE_WRITING);                     /* not taken: the field goes */
    CHECK(!fax_save_get(FIELD_GLASS));
    /* Off, the file is left alone. */
    reset_cb();
    fax_save_set(FIELD_GLASS, "taken");
    listener(FAX_SAVE_WRITING);
    listener(FAX_SAVE_CONTINUED);
    CHECK(!taken && fax_save_get(FIELD_GLASS));
}

/* The save state: version 1, taken; a state from before the feature (no
 * record) is not taken; anything else is refused. */
static void save_states(void) {
    play(true);
    uint8_t buf[8];
    CHECK(save_get(buf, 1) == -1);
    CHECK(save_get(buf, sizeof buf) == 2 && buf[0] == 1 && buf[1] == 0);
    visit(MIST, 0x30, 0x56);
    touch();
    CHECK(save_get(buf, sizeof buf) == 2 && buf[1] == 1);
    play(true);
    CHECK(save_set(buf, 2) && taken);
    CHECK(save_set(buf, 0) && !taken);
    CHECK(save_validate(buf, 0) && save_validate((const uint8_t[]){ 1, 1 }, 2));
    CHECK(!save_validate((const uint8_t[]){ 2, 1 }, 2) && !save_validate((const uint8_t[]){ 1, 2 }, 2));
    CHECK(!save_validate((const uint8_t[]){ 1, 1, 0 }, 3) && !save_set((const uint8_t[]){ 1, 7 }, 2));
}

int main(void) {
    registers();
    unsold();
    the_rule();
    boots_are_gone();
    the_fixed_glass();
    isolated_rooms();
    the_file();
    save_states();
    printf("found_test: %u checks passed\n", checks);
    return 0;
}
