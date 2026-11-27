/* Unit tests for the Pendant fix (fax_pendant.c), without the emulator or a
 * ROM. The module runs against a model of the machine: CPU RAM, the registers,
 * bank 14's two weapon tables, and Player_HitSpriteWithWeapon's damage code
 * from LDY Player_CurWeapon ($886C) to the enemy's HP (from $8891), played
 * instruction by instruction as the disassembly has it, with the hook site at
 * @_checkGlove. So the tests check the damage the game deals, not the module's
 * own arithmetic. */
#include "fax_pendant.c"
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

enum { DAGGER, LONG_SWORD, GIANT_BLADE, DRAGON_SLAYER, WEAPONS };
#define PLAYER_CUR_WEAPON        0x03C8u
#define DURATION_GLOVE           0x0428u /* $FF when not running */
#define CURRENT_SPRITES_HP       0x0344u
#define WEAPON_TO_GLOVE_STRENGTH 0x88C7u

/* The literal rule, from issue #29: each weapon's damage without and with the
 * Pendant, and what the Glove adds. */
static const uint8_t base[WEAPONS] = { 4, 8, 12, 16 }, with_pendant[WEAPONS] = { 5, 10, 15, 20 },
                     glove[WEAPONS] = { 2, 4, 8, 10 };

/* ---- the machine ---- */
static uint8_t ram[0x800], bank14[0x4000];
static CycModRegs cpu;
static int hook_on = -1; /* the hook's enabled state, as last set */
static bool isolated;    /* in one of Seamless's isolated rooms */

uint8_t cyc_mod_peek(uint16_t addr) {
    if (addr < 0x2000) return ram[addr & 0x7FF];
    return addr >= 0x8000 && addr < 0xC000 ? bank14[addr - 0x8000] : 0;
}
bool cyc_mod_poke(uint16_t addr, uint8_t value) {
    if (addr >= 0x2000) return false;
    ram[addr & 0x7FF] = value;
    return true;
}
void cyc_mod_regs(CycModRegs *out) { *out = cpu; }
bool cyc_mod_isolated(void) { return isolated; }
int nes_mod_register_function_entry_plugin(const char *id, uint16_t addr, NESModFunctionEntryCallback cb) {
    return id && !strcmp(id, hook_id) && addr == CHECK_GLOVE && cb == check_glove_hook;
}
int nes_mod_set_function_hook_enabled(const char *id, int on) {
    if (strcmp(id, hook_id)) return 0;
    hook_on = on;
    return 1;
}
int nes_mod_register_reset_callback(NESModActivationCallback cb) { return cb == deactivate; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    return !strcmp(id, "faxanadu.pendant-fix") && cb == activate;
}
void nes_mod_set_local_only(const char *name, int required) {
    (void)name;
    (void)required;
}

static uint8_t rom(uint16_t addr) { return bank14[addr - 0x8000]; }

/* The hero hits the enemy in sprite slot x with the weapon in hand: the ROM's
 * code from $886C, with the hook site at @_checkGlove when `hooked`. Returns
 * the HP the enemy lost. */
static unsigned hit(uint8_t x, bool hooked) {
    uint8_t hp = ram[CURRENT_SPRITES_HP + x];
    cpu.x = x;
    cpu.y = ram[PLAYER_CUR_WEAPON];                    /* $886C LDY Player_CurWeapon */
    cpu.a = rom((uint16_t)(WEAPON_STRENGTHS + cpu.y)); /* $886F LDA WEAPON_STRENGTHS,Y */
    ram[TEMP_00] = cpu.a;                              /* $8872 STA Temp_00 */
    cpu.a = ram[SPECIAL_ITEMS] & 0x02;                 /* $8874 LDA SpecialItems, AND #$02 */
    if (cpu.a == 0) {                                  /* $8879 BNE @_checkGlove: the bug */
        cpu.a = (uint8_t)(ram[TEMP_00] >> 2);          /* $887B LDA Temp_00, LSR, LSR */
        cpu.a = (uint8_t)(cpu.a + ram[TEMP_00]);       /* $887F CLC, ADC Temp_00 */
        ram[TEMP_00] = cpu.a;                          /* $8882 STA Temp_00 */
    }
    /* $8884 @_checkGlove: the hook runs before the instruction, and leaves
     * the registers alone. */
    if (hooked) {
        CycModRegs before = cpu;
        CHECK(check_glove_hook(CHECK_GLOVE) == 0); /* the game goes on */
        CHECK(!memcmp(&before, &cpu, sizeof cpu));
    }
    cpu.a = ram[DURATION_GLOVE]; /* $8884 LDA DurationGlove */
    if (!(cpu.a & 0x80)) {       /* $8887 BMI @_updateSpriteHP */
        cpu.a = (uint8_t)(ram[TEMP_00] + rom((uint16_t)(WEAPON_TO_GLOVE_STRENGTH + cpu.y)));
        ram[TEMP_00] = cpu.a; /* $8889 LDA Temp_00, CLC, ADC, STA */
    }
    ram[CURRENT_SPRITES_HP + x] = (uint8_t)(hp - ram[TEMP_00]); /* $8891 LDA, SEC, SBC Temp_00, STA */
    return (uint8_t)(hp - ram[CURRENT_SPRITES_HP + x]);
}

/* A hit on an enemy with 100 HP in slot 3. */
static unsigned damage(uint8_t weapon, bool pendant, bool gloved, bool hooked) {
    ram[PLAYER_CUR_WEAPON] = weapon;
    ram[SPECIAL_ITEMS] = pendant ? 0x02 | 0x01 | 0x80 : 0x01 | 0x80; /* with the Black Onyx and Ring of Elf */
    ram[DURATION_GLOVE] = gloved ? 0x40 : 0xFF;
    ram[CURRENT_SPRITES_HP + 3] = 100;
    return hit(3, hooked);
}

static void setup(void) {
    memset(ram, 0, sizeof ram);
    memset(bank14, 0xFF, sizeof bank14);
    for (int w = 0; w < WEAPONS; w++) {
        bank14[WEAPON_STRENGTHS - 0x8000 + w] = base[w];
        bank14[WEAPON_TO_GLOVE_STRENGTH - 0x8000 + w] = glove[w];
    }
    cpu = (CycModRegs){ .a = 0x5A, .s = 0xF0, .p = 0x24 };
    deactivate();
    activate();
    CHECK(hook_on == 1 && enabled);
}

static void the_formula(void) {
    for (int w = 0; w < WEAPONS; w++) {
        CHECK(pendant_damage(base[w], 0x00) == base[w]);
        CHECK(pendant_damage(base[w], BIT_PENDANT) == with_pendant[w]);
        CHECK(pendant_damage(base[w], 0xFD) == base[w]); /* every other item, no Pendant */
        CHECK(pendant_damage(base[w], 0xFF) == with_pendant[w]);
    }
    CHECK(pendant_damage(3, BIT_PENDANT) == 3); /* a quarter, rounded down */
    CHECK(pendant_damage(7, BIT_PENDANT) == 8);
}

static void the_original_is_reversed(void) {
    /* The model is the stock game: without the hook, the Pendant takes the
     * bonus away. */
    setup();
    for (int w = 0; w < WEAPONS; w++) {
        CHECK(damage(w, false, false, false) == with_pendant[w]);
        CHECK(damage(w, true, false, false) == base[w]);
        CHECK(damage(w, false, true, false) == with_pendant[w] + glove[w]);
        CHECK(damage(w, true, true, false) == base[w] + glove[w]);
    }
}

static void without_the_pendant_the_base_strength(void) {
    setup();
    for (int w = 0; w < WEAPONS; w++) {
        CHECK(damage(w, false, false, true) == base[w]);
        CHECK(last_weapon == w && last_strength == base[w] && !last_pendant && last_damage == base[w]);
        CHECK(damage(w, false, true, true) == base[w] + glove[w]);
    }
    CHECK(hits == 2 * WEAPONS);
}

static void with_the_pendant_the_originals_damage(void) {
    setup();
    for (int w = 0; w < WEAPONS; w++) {
        unsigned original = damage(w, false, false, false);
        CHECK(damage(w, true, false, true) == original);
        CHECK(original == with_pendant[w]);
        CHECK(last_pendant && last_damage == with_pendant[w]);
        unsigned original_gloved = damage(w, false, true, false);
        CHECK(damage(w, true, true, true) == original_gloved);
        CHECK(original_gloved == with_pendant[w] + glove[w]);
    }
}

static void it_kills_as_the_game_does(void) {
    /* Damage at least the enemy's HP kills it (SBC borrows); equal leaves 0. */
    setup();
    ram[PLAYER_CUR_WEAPON] = LONG_SWORD;
    ram[SPECIAL_ITEMS] = 0x02;
    ram[DURATION_GLOVE] = 0xFF;
    ram[CURRENT_SPRITES_HP + 7] = 9; /* a Zozura */
    CHECK(hit(7, true) == 10 && ram[CURRENT_SPRITES_HP + 7] == 0xFF);
    ram[SPECIAL_ITEMS] = 0x00;
    ram[CURRENT_SPRITES_HP + 7] = 9;
    CHECK(hit(7, true) == 8 && ram[CURRENT_SPRITES_HP + 7] == 1);
}

static void the_same_rule_in_seamless_rooms_uncounted(void) {
    /* A hit in one of Seamless's isolated rooms: the same damage, not counted. */
    setup();
    isolated = true;
    CHECK(damage(LONG_SWORD, false, false, true) == base[LONG_SWORD]);
    CHECK(damage(LONG_SWORD, true, false, true) == with_pendant[LONG_SWORD]);
    CHECK(hits == 0 && last_weapon == 0xFF);
    isolated = false;
    CHECK(damage(LONG_SWORD, false, false, true) == base[LONG_SWORD] && hits == 1);
}

static void the_fist(void) {
    /* A punch (fax_punch.c) reaches the hook with Y $FF, no weapon, where
     * WEAPON_STRENGTHS has no entry: Punches' strength, by the same rule. (The
     * Glove's bonus for it is Punches' own, after the hook.) */
    setup();
    CHECK(damage(0xFF, false, false, true) == FAX_PUNCH_STRENGTH);
    CHECK(last_weapon == 0xFF && last_strength == FAX_PUNCH_STRENGTH && !last_pendant && last_damage == 2);
    CHECK(damage(0xFF, true, false, true) == 2 && last_pendant); /* a quarter of 2 is 0 */
    CHECK(hits == 2);
}

static void off_means_off(void) {
    setup();
    damage(DRAGON_SLAYER, true, false, true);
    CHECK(hits == 1);
    deactivate();
    CHECK(hook_on == 0 && !enabled && hits == 0 && last_weapon == 0xFF);
}

int main(void) {
    the_formula();
    the_original_is_reversed();
    without_the_pendant_the_base_strength();
    with_the_pendant_the_originals_damage();
    the_same_rule_in_seamless_rooms_uncounted();
    it_kills_as_the_game_does();
    the_fist();
    off_means_off();
    printf("pendant_test: %u checks passed\n", checks);
    return 0;
}
