/* Unit tests for the Battle Suit and Dragon Slayer fix (fax_inventory.c),
 * without the emulator or a ROM. The module runs against a model of the
 * machine: CPU RAM, the registers, and the tail of each pickup routine from
 * its LDX ($C6F7, $C737) to its RTS, played instruction by instruction as the
 * disassembly has it, with the hook site at its CPX #$04. The Battle Helmet's
 * routine, the same code on the shield list, is the reference. So the tests
 * check the lists the game ends with, not the module's own arithmetic. */
#include "fax_inventory.c"
#include <string.h>

static unsigned checks;
#define CHECK(test) do{++checks;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(1);}}while(0)

#define WEAPON_INVENTORY  0x039Du
#define ARMOR_INVENTORY   0x03A1u
#define SHIELD_INVENTORY  0x03A5u
#define NUMBER_OF_SHIELDS 0x03C4u
#define STALE             0xEEu    /* what a list holds after its count */
#define FLAG_C            0x01u
enum { MAX = 4, BATTLE_SUIT = 3, DRAGON_SLAYER = 3, BATTLE_HELMET = 3 };

/* ---- the machine ---- */
static uint8_t ram[0x800];
static CycModRegs cpu;
static bool isolated;
static int suit_on = -1, slayer_on = -1;          /* the hooks' enabled states, as last set */

uint8_t cyc_mod_peek(uint16_t addr) { return addr < 0x2000 ? ram[addr & 0x7FF] : 0; }
bool cyc_mod_poke(uint16_t addr, uint8_t value) {
    if (addr >= 0x2000) return false;
    ram[addr & 0x7FF] = value;
    return true;
}
void cyc_mod_regs(CycModRegs *out) { *out = cpu; }
void cyc_mod_set_regs(const CycModRegs *in) { uint16_t pc = cpu.pc; cpu = *in; cpu.pc = pc; }
bool cyc_mod_isolated(void) { return isolated; }
int nes_mod_register_function_entry_plugin(const char *id, uint16_t addr, NESModFunctionEntryCallback cb) {
    return (!strcmp(id, suit_id) && addr == SUIT_CAP && cb == suit_hook) ||
           (!strcmp(id, slayer_id) && addr == SLAYER_CAP && cb == slayer_hook);
}
int nes_mod_set_function_hook_enabled(const char *id, int on) {
    if (!strcmp(id, suit_id)) suit_on = on;
    else if (!strcmp(id, slayer_id)) slayer_on = on;
    else return 0;
    return 1;
}
int nes_mod_register_reset_callback(NESModActivationCallback cb) { return cb == deactivate; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    return !strcmp(id, "faxanadu.inventory-fix") && cb == activate;
}
void nes_mod_set_local_only(const char *name, int required) { (void)name; (void)required; }

static void nz(uint8_t v) { cpu.p = (uint8_t)((cpu.p & ~(FLAG_N | FLAG_Z)) | (v & FLAG_N) | (v ? 0 : FLAG_Z)); }

/* A pickup's tail: LDX `read`, the hook site at CPX #$04 (when `hook` is
 * given), CPX, BCC @_addItem, LDX #$03, @_addItem: LDA #$03, STA list,X, INX,
 * STX count, RTS. */
static void tail(uint16_t read, int (*hook)(uint16_t), uint16_t site, uint16_t list, uint16_t count) {
    cpu.x = ram[read]; nz(cpu.x);                                  /* LDX read */
    if (hook) CHECK(hook(site) == 0);                              /* the game goes on */
    uint8_t diff = (uint8_t)(cpu.x - 4);                           /* CPX #$04 */
    cpu.p = (uint8_t)((cpu.p & ~FLAG_C) | (cpu.x >= 4 ? FLAG_C : 0)); nz(diff);
    if (cpu.p & FLAG_C) { cpu.x = 3; nz(cpu.x); }                  /* BCC; LDX #$03 */
    cpu.a = 3; nz(cpu.a);                                          /* LDA #$03 */
    ram[list + cpu.x] = cpu.a;                                     /* STA list,X */
    ++cpu.x; nz(cpu.x);                                            /* INX */
    ram[count] = cpu.x;                                            /* STX count */
}
static void pick_up_battle_suit(bool hooked) {
    tail(NUMBER_OF_WEAPONS, hooked && suit_on == 1 ? suit_hook : NULL, SUIT_CAP, ARMOR_INVENTORY, NUMBER_OF_ARMORS);
}
static void pick_up_dragon_slayer(bool hooked) {
    tail(NUMBER_OF_ARMORS, hooked && slayer_on == 1 ? slayer_hook : NULL, SLAYER_CAP, WEAPON_INVENTORY,
         NUMBER_OF_WEAPONS);
}
static void pick_up_battle_helmet(void) {                      /* the stock routine, right as it is */
    tail(NUMBER_OF_SHIELDS, NULL, 0, SHIELD_INVENTORY, NUMBER_OF_SHIELDS);
}

/* A list of `n` entries 0, 1, ... (n may be 4 only with a repeat: the
 * Leather Armor and Hand Dagger are 0) and stale bytes after them. */
static void fill(uint16_t list, uint16_t count, unsigned n) {
    for (unsigned i = 0; i < MAX; ++i) ram[list + i] = i < n ? (uint8_t)(i % 3) : STALE;
    ram[count] = (uint8_t)n;
}
static void setup(unsigned weapons, unsigned armor) {
    memset(ram, 0, sizeof ram);
    fill(WEAPON_INVENTORY, NUMBER_OF_WEAPONS, weapons);
    fill(ARMOR_INVENTORY, NUMBER_OF_ARMORS, armor);
    fill(SHIELD_INVENTORY, NUMBER_OF_SHIELDS, armor);
    cpu = (CycModRegs){.a = 0x5A, .x = 0x77, .y = 0x33, .s = 0xF0, .p = 0x24};
}
static void on(void) {
    deactivate();
    isolated = false;
    activate();
    CHECK(suit_on == 1 && slayer_on == 1 && enabled);
}

/* A list after an item is added the right way, from `n` entries: appended, or
 * the last entry replaced when the list is full, as the Battle Helmet's
 * routine and Player_AddToInventory do. */
static void expect_added(uint16_t list, uint16_t count, unsigned n, uint8_t item) {
    unsigned at = n < MAX ? n : MAX - 1;
    for (unsigned i = 0; i < at; ++i) CHECK(ram[list + i] == (uint8_t)(i % 3));
    CHECK(ram[list + at] == item);
    for (unsigned i = at + 1; i < MAX; ++i) CHECK(ram[list + i] == STALE);
    CHECK(ram[count] == at + 1);
}
static void expect_untouched(uint16_t list, uint16_t count, unsigned n) {
    for (unsigned i = 0; i < MAX; ++i) CHECK(ram[list + i] == (i < n ? (uint8_t)(i % 3) : STALE));
    CHECK(ram[count] == n);
}

static void the_count_each_reads(void) {
    CHECK(fax_inventory_count_addr(FAX_PICKUP_BATTLE_SUIT) == NUMBER_OF_ARMORS);
    CHECK(fax_inventory_count_addr(FAX_PICKUP_DRAGON_SLAYER) == NUMBER_OF_WEAPONS);
    CHECK(NUMBER_OF_ARMORS == NUMBER_OF_WEAPONS + 1 && NUMBER_OF_SHIELDS == NUMBER_OF_WEAPONS + 2);
}

static void the_helmet_is_the_reference(void) {
    for (unsigned n = 0; n <= MAX; ++n) {
        setup(0, n);
        pick_up_battle_helmet();
        expect_added(SHIELD_INVENTORY, NUMBER_OF_SHIELDS, n, BATTLE_HELMET);
    }
}

static void the_original_reads_the_other_list(void) {
    /* The model is the stock game: the Battle Suit adds at the weapons'
     * count, the Dragon Slayer at the armor's. */
    deactivate();
    setup(0, 2);                                     /* no weapons, Leather and Studded Mail */
    pick_up_battle_suit(true);
    CHECK(ram[ARMOR_INVENTORY] == BATTLE_SUIT && ram[NUMBER_OF_ARMORS] == 1);    /* both armors lost */
    setup(3, 0);                                     /* three weapons, no armor */
    pick_up_battle_suit(true);
    CHECK(ram[ARMOR_INVENTORY + 3] == BATTLE_SUIT && ram[NUMBER_OF_ARMORS] == 4);   /* three stale "armors" */
    CHECK(ram[ARMOR_INVENTORY] == STALE);
    setup(3, 0);
    pick_up_dragon_slayer(true);
    CHECK(ram[WEAPON_INVENTORY] == DRAGON_SLAYER && ram[NUMBER_OF_WEAPONS] == 1);  /* three weapons lost */
    setup(0, 2);
    pick_up_dragon_slayer(true);
    CHECK(ram[WEAPON_INVENTORY + 2] == DRAGON_SLAYER && ram[NUMBER_OF_WEAPONS] == 3);
    CHECK(ram[WEAPON_INVENTORY] == STALE && ram[WEAPON_INVENTORY + 1] == STALE);
    setup(2, 2);                                     /* equal counts hide it */
    pick_up_battle_suit(true);
    expect_added(ARMOR_INVENTORY, NUMBER_OF_ARMORS, 2, BATTLE_SUIT);
}

static void each_adds_to_its_own_list(void) {
    on();
    unsigned suits = 0, slayers = 0;
    for (unsigned w = 0; w <= MAX; ++w)
        for (unsigned a = 0; a <= MAX; ++a) {
            setup(w, a);
            pick_up_battle_suit(true);
            ++suits;
            expect_added(ARMOR_INVENTORY, NUMBER_OF_ARMORS, a, BATTLE_SUIT);
            expect_untouched(WEAPON_INVENTORY, NUMBER_OF_WEAPONS, w);
            CHECK(last_wrong == w && last_right == a);
            setup(w, a);
            pick_up_dragon_slayer(true);
            ++slayers;
            expect_added(WEAPON_INVENTORY, NUMBER_OF_WEAPONS, w, DRAGON_SLAYER);
            expect_untouched(ARMOR_INVENTORY, NUMBER_OF_ARMORS, a);
            CHECK(last_wrong == a && last_right == w);
            /* As the Battle Helmet does with the shields. */
            setup(w, a);
            pick_up_battle_helmet();
            expect_added(SHIELD_INVENTORY, NUMBER_OF_SHIELDS, a, BATTLE_HELMET);
        }
    CHECK(fixed[FAX_PICKUP_BATTLE_SUIT] == suits && fixed[FAX_PICKUP_DRAGON_SLAYER] == slayers);
}

static void only_x_and_its_flags(void) {
    /* The hook leaves A, Y, S and the other flags as they were, and sets N
     * and Z as an LDX of the right count would. */
    on();
    for (unsigned n = 0; n <= MAX; ++n) {
        setup(MAX - n, n);
        cpu.x = ram[NUMBER_OF_WEAPONS]; nz(cpu.x);
        cpu.p |= FLAG_C | 0x40;                        /* C and V from before, kept */
        CycModRegs before = cpu;
        CHECK(suit_hook(SUIT_CAP) == 0);
        CHECK(cpu.x == n && cpu.a == before.a && cpu.y == before.y && cpu.s == before.s);
        CHECK((cpu.p & ~(FLAG_N | FLAG_Z)) == (before.p & ~(FLAG_N | FLAG_Z)));
        CHECK(!!(cpu.p & FLAG_Z) == (n == 0) && !(cpu.p & FLAG_N));
    }
    setup(0, 0);
    ram[NUMBER_OF_WEAPONS] = 0x80;                     /* N as LDX would set it, though no list has 128 */
    CHECK(slayer_hook(SLAYER_CAP) == 0 && cpu.x == 0x80 && (cpu.p & FLAG_N) && !(cpu.p & FLAG_Z));
}

static void isolated_rooms_fix_but_do_not_count(void) {
    on();
    isolated = true;
    setup(3, 0);
    pick_up_dragon_slayer(true);
    expect_added(WEAPON_INVENTORY, NUMBER_OF_WEAPONS, 3, DRAGON_SLAYER);
    CHECK(fixed[FAX_PICKUP_DRAGON_SLAYER] == 0 && last_wrong == 0xFF);
    isolated = false;
}

static void off_means_off(void) {
    on();
    setup(1, 2);
    pick_up_battle_suit(true);
    CHECK(fixed[FAX_PICKUP_BATTLE_SUIT] == 1);
    deactivate();
    CHECK(suit_on == 0 && slayer_on == 0 && !enabled && fixed[FAX_PICKUP_BATTLE_SUIT] == 0 && last_right == 0xFF);
    setup(1, 2);
    pick_up_battle_suit(true);                       /* the hooks are off: the original's bug */
    CHECK(ram[ARMOR_INVENTORY + 1] == BATTLE_SUIT && ram[NUMBER_OF_ARMORS] == 2);
}

int main(void) {
    the_count_each_reads();
    the_helmet_is_the_reference();
    the_original_reads_the_other_list();
    each_adds_to_its_own_list();
    only_x_and_its_flags();
    isolated_rooms_fix_but_do_not_count();
    off_means_off();
    printf("inventory_test: %u checks passed\n", checks);
    return 0;
}
