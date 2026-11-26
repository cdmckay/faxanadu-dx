/* Unit tests for Punches (fax_punch.c), without the emulator or a ROM. The
 * module runs against a model of the machine: CPU RAM, the registers, bank
 * 14's tables as the disassembly has them, and the two routines played
 * instruction by instruction: Player_DrawWeapon from $B7D6 and
 * Player_HitSpriteWithWeapon's weapon check and damage, from $8814 to the
 * enemy's HP. Every hook registered at a site runs before its instruction, as
 * the scheduler runs them: Punches' three and the Pendant fix's at
 * @_checkGlove (fax_pendant.c, linked beside this file, which gives the fist
 * its strength). So the tests check the damage and the box the game ends up
 * with, not the module's own arithmetic. */
#include "fax_punch.c"
#include <string.h>

static unsigned checks;
#define CHECK(test) do{++checks;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(1);}}while(0)

#define PORTRAIT_ID       0x03C7u   /* IScript_PortraitID: bit 7 clear while a portrait is up */
#define PLAYER_CUR_WEAPON 0x03C8u
#define CURRENT_SPRITES_HP 0x0344u
#define DURATION_GLOVE    0x0428u   /* $FF when not running */
#define SPECIAL_ITEMS     0x042Cu
#define STATUS            0x00A5u   /* Player_StatusFlag */
#define JOY               0x0016u   /* Joy1_ButtonMask */
#define TICK              0x00A3u   /* Player_MovementTick */
#define FLIP_MASK         0x0029u   /* CurrentSprite_FlipMask */
#define WEAPON_STRENGTHS  0xB7A5u
#define GLOVE_STRENGTHS   0x88C7u   /* WEAPON_TO_GLOVE_STRENGTH */
#define BIT_PENDANT       0x02u
#define P_Z               0x02u
enum { LONG_SWORD = 1, DRAGON_SLAYER = 3, WEAPONS = 4, FIST = 0xFF, ZOZURA_HP = 9, MONODRON_HP = 10 };

/* The rule, from issue #180: a punch does 2, the Glove adds 1, and the
 * Pendant's quarter of 2 is 0. The weapons as the Pendant fix has them. */
static const uint8_t base[WEAPONS] = {4, 8, 12, 16}, with_pendant[WEAPONS] = {5, 10, 15, 20},
                     glove[WEAPONS] = {2, 4, 8, 10};

/* ---- the machine ---- */
static uint8_t ram[0x800], bank14[0x4000];
static CycModRegs cpu;
static bool isolated;
static uint8_t served_reach_y = 0x08;            /* the Taller dagger's $B898, as the CPU reads it */

uint8_t cyc_mod_peek(uint16_t addr) {            /* as the host's: RAM and the ROM, no read override */
    if (addr < 0x2000) return ram[addr & 0x7FF];
    return addr >= 0x8000 && addr < 0xC000 ? bank14[addr - 0x8000] : 0;
}
bool cyc_mod_poke(uint16_t addr, uint8_t value) {
    if (addr >= 0x2000) return false;
    ram[addr & 0x7FF] = value;
    return true;
}
void cyc_mod_regs(CycModRegs *out) { *out = cpu; }
void cyc_mod_set_regs(const CycModRegs *in) { cpu = *in; }
bool cyc_mod_isolated(void) { return isolated; }
uint8_t fax_dagger_reach_y(void) { return served_reach_y; }

/* The registrations, as the host keeps them: both modules register from
 * their constructors before main(). */
typedef struct { const char *id; uint16_t addr; NESModFunctionEntryCallback cb; bool on; } Site;
static Site sites[8];
static unsigned site_count;
static NESModActivationCallback resets[4], activations[4];
static const char *activation_ids[4];
static unsigned reset_count, activation_count;

int nes_mod_register_function_entry_plugin(const char *id, uint16_t addr, NESModFunctionEntryCallback cb) {
    if (site_count == sizeof sites / sizeof sites[0]) return 0;
    sites[site_count++] = (Site){id, addr, cb, false};
    return 1;
}
int nes_mod_set_function_hook_enabled(const char *id, int on) {
    for (unsigned i = 0; i < site_count; ++i)
        if (!strcmp(sites[i].id, id)) { sites[i].on = on; return 1; }
    return 0;
}
int nes_mod_register_reset_callback(NESModActivationCallback cb) {
    resets[reset_count++] = cb;
    return 1;
}
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    activation_ids[activation_count] = id;
    activations[activation_count++] = cb;
    return 1;
}
void nes_mod_set_local_only(const char *name, int required) { (void)name; (void)required; }

static bool hook_on(const char *id) {
    for (unsigned i = 0; i < site_count; ++i)
        if (!strcmp(sites[i].id, id)) return sites[i].on;
    return false;
}

/* The scheduler at an instruction: every enabled hook there, before it. None
 * of these handles its routine. */
static void at(uint16_t pc) {
    for (unsigned i = 0; i < site_count; ++i)
        if (sites[i].addr == pc && sites[i].on) CHECK(sites[i].cb(pc) == 0);
}

/* What the CPU reads: RAM, or bank 14 with the Taller dagger's override. */
static uint8_t rd(uint16_t addr) {
    if (addr < 0x2000) return ram[addr & 0x7FF];
    if (addr == 0xB898) return served_reach_y;
    return bank14[addr - 0x8000];
}
static uint16_t rd16(uint16_t addr) { return (uint16_t)(rd(addr) | rd((uint16_t)(addr + 1)) << 8); }
static void nz(uint8_t v) { cpu.p = (uint8_t)((cpu.p & ~(P_N | P_Z)) | (v & P_N) | (v ? 0 : P_Z)); }

/* Bank 14's tables, as the disassembly has them. */
static void load_rom(void) {
    static const uint8_t strengths[] = {0x04, 0x08, 0x0C, 0x10}, gloves[] = {0x02, 0x04, 0x08, 0x0A};
    static const uint8_t ranges[] = {0x08, 0x04, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08};
    static const uint8_t x_ptrs[] = {0xAF, 0xB8, 0xBF, 0xB8, 0xCF, 0xB8, 0xDF, 0xB8,
                                     0xEF, 0xB8, 0xFF, 0xB8, 0xFF, 0xB8, 0xFF, 0xB8};
    static const uint8_t x_tables[] = {   /* $B8AF: dagger, dagger + shield, Long Sword, ..., Giant Blade + shield */
        0xF8, 0x10, 0xFC, 0x0C, 0xF8, 0x10, 0xF8, 0x10, 0xF8, 0x10, 0xF8, 0x0E, 0xF0, 0x18, 0x00, 0x00,
        0xF8, 0x10, 0xF8, 0x10, 0xF8, 0x10, 0xF8, 0x10, 0xF8, 0x10, 0xF8, 0x10, 0xF0, 0x18, 0x00, 0x00,
        0xF8, 0x10, 0xFC, 0x0C, 0xF8, 0x10, 0xF8, 0x10, 0xF8, 0x10, 0xF0, 0x10, 0xE8, 0x18, 0x00, 0x00,
        0xF8, 0x10, 0xF8, 0x10, 0xF8, 0x10, 0xF8, 0x10, 0xF8, 0x10, 0xF0, 0x10, 0xE8, 0x18, 0x00, 0x00,
        0xF0, 0x10, 0xFC, 0x0C, 0xF8, 0x10, 0xF0, 0x10, 0xF8, 0x10, 0xF0, 0x10, 0xE8, 0x18, 0x00, 0x00,
        0xF0, 0x10, 0xF8, 0x10, 0xF8, 0x10, 0xF0, 0x10, 0xF8, 0x10, 0xF0, 0x10, 0xE8, 0x18, 0x00, 0x00};
    static const uint8_t y_ptrs[] = {0x17, 0xB9, 0x1F, 0xB9, 0x1F, 0xB9, 0x1F, 0xB9};
    static const uint8_t y_tables[] = {0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x06, 0x00,    /* $B917: dagger */
                                       0x00, 0x08, 0x08, 0x00, 0x08, 0x08, 0x06, 0x00};   /* $B91F: swords */
    static const uint8_t poses[] = {0x04, 0x05, 0x06, 0x00, 0x01, 0x02, 0x01};          /* $B97B, $B97E */
    memset(bank14, 0xEA, sizeof bank14);
    memcpy(&bank14[WEAPON_STRENGTHS - 0x8000], strengths, sizeof strengths);
    memcpy(&bank14[GLOVE_STRENGTHS - 0x8000], gloves, sizeof gloves);
    memcpy(&bank14[WEAPON_RANGES - 0x8000], ranges, sizeof ranges);
    memcpy(&bank14[WEAPON_X_TABLES - 0x8000], x_ptrs, sizeof x_ptrs);
    memcpy(&bank14[0xB8AF - 0x8000], x_tables, sizeof x_tables);
    memcpy(&bank14[WEAPON_Y_TABLES - 0x8000], y_ptrs, sizeof y_ptrs);
    memcpy(&bank14[0xB917 - 0x8000], y_tables, sizeof y_tables);
    memcpy(&bank14[ATTACK_POSES - 0x8000], poses, sizeof poses);
    bank14[GLOVE_STRENGTHS + 0xFF - 0x8000] = 0x02;      /* $89C6, code: what ADC ...,Y reads for Y $FF */
}

/* ---- Player_DrawWeapon ---- */

/* Maybe_Player_CalcAnimFrame ($B927), as the disassembly has it. */
static uint8_t calc_anim_frame(void) {
    uint8_t f = ram[PLAYER_FLAGS];
    if (f & JUMPING) return f & ATTACKING ? rd((uint16_t)(ATTACK_POSES + ram[ATTACK_PHASE])) : 3;
    if (f & CAN_CLIMB) {                                           /* (flags >> 1) & 4 */
        bool to_b943 = !(ram[HERO_X] & 0x0F) || !(f & FALLING);
        if (to_b943 && (f & CLIMBING)) {
            ram[FLIP_MASK] = (uint8_t)((ram[TICK] & 0x10) << 2);
            return 7;
        }
    }
    if (f & ATTACKING) return rd((uint16_t)(ATTACK_POSES + ram[ATTACK_PHASE]));
    if ((ram[STATUS] & 0x80) && (ram[JOY] & 0x80)) return 3;
    return rd((uint16_t)(ATTACK_POSES + 3 + (f & 0x20 ? (ram[TICK] >> 3) & 3 : 0)));
}

/* Player_DrawWeapon from $B7D6, with the hook site at $B7DE. Whether it goes
 * on to draw the weapon (@LAB_PRG14__b850). Off the screen it places the Y
 * and the reach too, as it does in the remaster: Seams' site at the BNE after
 * the X ($B81A, fax_seams.c) keeps the branch from being taken, and the
 * routine's next test returns before the draw. */
static bool draw_weapon(void) {
    if (!(rd(PORTRAIT_ID) & 0x80)) return false;                 /* LDA IScript_PortraitID, BPL */
    cpu.a = rd(PLAYER_CUR_WEAPON);                                /* $B7DB LDA Player_CurWeapon */
    nz(cpu.a);
    at(DRAW_NO_WEAPON);
    if (cpu.p & P_N) return false;                                /* $B7DE BMI RETURN_B7D5 */
    ram[TEMP_00] = ram[ARMOR_INDEX] & 1;
    uint8_t weapon = rd(PLAYER_CUR_WEAPON) == 0xFF ? 0 : rd(PLAYER_CUR_WEAPON);   /* Player_GetNormalizedWeaponID */
    uint16_t xs = rd16((uint16_t)(WEAPON_X_TABLES + (uint8_t)((weapon << 1 | ram[TEMP_00]) << 1)));
    uint8_t pose = calc_anim_frame();
    ram[TEMP_00] = pose;
    uint8_t y = (uint8_t)(pose << 1 | (ram[PLAYER_FLAGS] & FACING_RIGHT ? 1 : 0));
    ram[0x04] = rd((uint16_t)(xs + y));                           /* Player_CalcValueAndFFForNeg */
    ram[0x05] = ram[0x04] & 0x80 ? 0xFF : 0x00;
    unsigned sum = (unsigned)ram[HERO_X] + ram[0x04];
    ram[WEAPON_X] = (uint8_t)sum;
    ram[WEAPON_X_HIGH] = (uint8_t)(ram[SCROLL_X] + ram[0x05] + (sum >> 8));
    /* CMP, BNE RETURN_B7D5: not taken (Seams) */
    uint8_t index = (uint8_t)(rd(PLAYER_CUR_WEAPON) << 1);
    uint16_t ys = rd16((uint16_t)(WEAPON_Y_TABLES + index));
    sum = (unsigned)ram[HERO_Y] + rd((uint16_t)(ys + ram[TEMP_00]));
    ram[WEAPON_Y] = (uint8_t)sum;
    ram[WEAPON_Y_HIGH] = (uint8_t)(ram[SCROLL_Y] + (sum >> 8));
    ram[REACH_X] = rd((uint16_t)(WEAPON_RANGES + index));
    ram[REACH_Y] = rd((uint16_t)(WEAPON_RANGES + index + 1));
    return ram[WEAPON_X_HIGH] == ram[SCROLL_X];                   /* LDA, CMP, BEQ to the draw, else RTS */
}

/* ---- Player_HitSpriteWithWeapon ---- */

/* From $8814, the hero swinging in a hit phase at an enemy in reach (the
 * routine's checks before and between, which a punch passes as a weapon
 * does): the HP the enemy in `slot` lost, or -1 if the routine returned at
 * the weapon check. */
static int hit(uint8_t slot) {
    uint8_t hp = ram[CURRENT_SPRITES_HP + slot];
    cpu.a = rd(PLAYER_CUR_WEAPON);                                /* $8814 LDA Player_CurWeapon */
    nz(cpu.a);
    at(HIT_NO_WEAPON);
    if (cpu.p & P_N) return -1;                                   /* $8817 BMI RETURN_87CA */
    cpu.x = slot;
    cpu.y = rd(PLAYER_CUR_WEAPON);                                /* $886C LDY Player_CurWeapon */
    ram[TEMP_00] = rd((uint16_t)(WEAPON_STRENGTHS + cpu.y));      /* LDA WEAPON_STRENGTHS,Y, STA Temp_00 */
    if (!(rd(SPECIAL_ITEMS) & BIT_PENDANT))                       /* the Pendant Bug's BNE */
        ram[TEMP_00] = (uint8_t)(ram[TEMP_00] + (ram[TEMP_00] >> 2));
    at(0x8884);                                                   /* @_checkGlove */
    cpu.a = rd(DURATION_GLOVE);
    nz(cpu.a);
    if (!(cpu.p & P_N)) {                                         /* BMI @_updateSpriteHP */
        cpu.a = (uint8_t)(ram[TEMP_00] + rd((uint16_t)(GLOVE_STRENGTHS + cpu.y)));   /* LDA, CLC, ADC ...,Y */
        at(GLOVE_STORE);
        ram[TEMP_00] = cpu.a;                                     /* $888F STA Temp_00 */
    }
    ram[CURRENT_SPRITES_HP + slot] = (uint8_t)(hp - ram[TEMP_00]);   /* $8891 LDA, SEC, SBC Temp_00, STA */
    return ram[TEMP_00];
}

/* A hit with this kit on an enemy with 100 HP in slot 3. */
static int damage(uint8_t weapon, bool pendant, bool gloved) {
    ram[PLAYER_CUR_WEAPON] = weapon;
    ram[SPECIAL_ITEMS] = pendant ? BIT_PENDANT | 0x01 : 0x01;    /* with the Black Onyx */
    ram[DURATION_GLOVE] = gloved ? 0x40 : 0xFF;
    ram[CURRENT_SPRITES_HP + 3] = 100;
    return hit(3);
}

/* Punches until an enemy with `hp` dies: a borrow kills, damage equal to its
 * HP leaves it at 0. */
static unsigned punches_to_kill(uint8_t hp, bool pendant, bool gloved) {
    ram[PLAYER_CUR_WEAPON] = FIST;
    ram[SPECIAL_ITEMS] = pendant ? BIT_PENDANT : 0;
    ram[DURATION_GLOVE] = gloved ? 0x40 : 0xFF;
    ram[CURRENT_SPRITES_HP + 7] = hp;
    for (unsigned n = 1; n < 50; ++n) {
        uint8_t before = ram[CURRENT_SPRITES_HP + 7];
        CHECK(hit(7) > 0);
        if (ram[CURRENT_SPRITES_HP + 7] > before) return n;
    }
    CHECK(!"it never died");
    return 0;
}

static void reset_all(void) { for (unsigned i = 0; i < reset_count; ++i) resets[i](); }
static void activate_all(void) { for (unsigned i = 0; i < activation_count; ++i) activations[i](); }

static void setup(void) {
    memset(ram, 0, sizeof ram);
    load_rom();
    ram[PORTRAIT_ID] = 0xFF;
    cpu = (CycModRegs){.a = 0x5A, .s = 0xF0, .p = 0x24};
    isolated = false;
    served_reach_y = 0x08;
    reset_all();
    activate_all();
    CHECK(enabled && hook_on("faxanadu.punches.draw") && hook_on("faxanadu.punches.hit") &&
          hook_on("faxanadu.punches.glove") && hook_on("faxanadu.pendant-fix.check-glove"));
}

static void registered(void) {
    /* Each module registered once, its hooks at their sites, off until activated. */
    unsigned punches = 0;
    for (unsigned i = 0; i < activation_count; ++i) punches += !strcmp(activation_ids[i], "faxanadu.punches");
    CHECK(punches == 1 && activation_count == 2 && reset_count == 2 && site_count == 4);
    static const struct { const char *id; uint16_t addr; } want[] = {
        {"faxanadu.punches.draw", 0xB7DE}, {"faxanadu.punches.hit", 0x8817},
        {"faxanadu.punches.glove", 0x888F}, {"faxanadu.pendant-fix.check-glove", 0x8884}};
    for (unsigned w = 0; w < 4; ++w) {
        bool found = false;
        for (unsigned i = 0; i < site_count; ++i) found |= !strcmp(sites[i].id, want[w].id) && sites[i].addr == want[w].addr;
        CHECK(found);
    }
}

static void the_fist_hits(void) {
    setup();
    CHECK(damage(FIST, false, false) == FAX_PUNCH_STRENGTH && FAX_PUNCH_STRENGTH == 2);
    CHECK(damage(FIST, true, false) == 2);                        /* a quarter of 2 is 0 */
    CHECK(damage(FIST, false, true) == FAX_PUNCH_STRENGTH + FAX_PUNCH_GLOVE && FAX_PUNCH_GLOVE == 1);
    CHECK(damage(FIST, true, true) == 3);
    CHECK(gloved == 2);
    /* Half the Hand Dagger's, either way. */
    CHECK(FAX_PUNCH_STRENGTH * 2 == base[0] && FAX_PUNCH_GLOVE * 2 == glove[0]);
}

static void the_kills(void) {
    setup();
    CHECK(punches_to_kill(ZOZURA_HP, false, false) == 5);         /* 9 7 5 3 1, then dead */
    CHECK(punches_to_kill(ZOZURA_HP, true, false) == 5);
    CHECK(punches_to_kill(ZOZURA_HP, false, true) == 4);          /* 9 6 3 0, then dead */
    CHECK(punches_to_kill(ZOZURA_HP, true, true) == 4);
    CHECK(punches_to_kill(MONODRON_HP, false, false) == 6);       /* 10 8 6 4 2 0, then dead */
    CHECK(punches_to_kill(MONODRON_HP, false, true) == 4);        /* 10 7 4 1, then dead */
}

static void weapons_as_before(void) {
    /* The Pendant fix's damage for every weapon: Punches passes them by. */
    setup();
    for (int w = 0; w < WEAPONS; w++) {
        CHECK(damage((uint8_t)w, false, false) == base[w]);
        CHECK(damage((uint8_t)w, true, false) == with_pendant[w]);
        CHECK(damage((uint8_t)w, false, true) == base[w] + glove[w]);
        CHECK(damage((uint8_t)w, true, true) == with_pendant[w] + glove[w]);
    }
    CHECK(gloved == 0);
}

static void off_means_the_original(void) {
    /* The remaster off: no punch lands, and nothing is placed. */
    setup();
    damage(FIST, false, true);
    reset_all();
    CHECK(!enabled && placed == 0 && gloved == 0);
    for (unsigned i = 0; i < site_count; ++i) CHECK(!sites[i].on);
    CHECK(damage(FIST, false, false) == -1 && ram[CURRENT_SPRITES_HP + 3] == 100);
    CHECK(damage(LONG_SWORD, false, false) == with_pendant[LONG_SWORD]);   /* the original's Pendant Bug */
    ram[PLAYER_CUR_WEAPON] = FIST;
    ram[PLAYER_FLAGS] = ATTACKING;
    ram[ATTACK_PHASE] = 1;
    uint8_t before[sizeof ram];
    memcpy(before, ram, sizeof ram);
    CHECK(!draw_weapon() && !memcmp(before, ram, sizeof ram));
}

/* The box the routine leaves: $CE-$D3. */
static void box_of(uint8_t out[6]) { memcpy(out, &ram[WEAPON_X], 6); }

static void the_dagger_s_box(void) {
    /* In every attacking pose, either facing, with or without a shield, on
     * the screen or off it: the fist's box is the Hand Dagger's, to the byte,
     * the reach as the Taller dagger serves it. */
    static const uint8_t xs[] = {0, 4, 7, 8, 15, 16, 100, 216, 240, 247, 248, 255};
    static const uint8_t flag_sets[] = {ATTACKING, ATTACKING | FACING_RIGHT, ATTACKING | JUMPING,
                                        ATTACKING | JUMPING | FACING_RIGHT, ATTACKING | FALLING, ATTACKING | 0x20,
                                        ATTACKING | CAN_CLIMB | CLIMBING, ATTACKING | CAN_CLIMB | CLIMBING | FALLING,
                                        ATTACKING | JUMPING | CAN_CLIMB | CLIMBING};
    unsigned cases = 0, off_screen = 0;
    for (int tall = 0; tall < 2; ++tall)
        for (unsigned f = 0; f < sizeof flag_sets; ++f)
            for (uint8_t phase = 0; phase < 3; ++phase)
                for (uint8_t shield = 0; shield < 2; ++shield)
                    for (unsigned x = 0; x < sizeof xs; ++x)
                        for (uint8_t scroll = 0; scroll < 2; ++scroll) {
                            setup();
                            served_reach_y = tall ? 0x08 : 0x04;
                            ram[PLAYER_FLAGS] = flag_sets[f];
                            ram[ATTACK_PHASE] = phase;
                            ram[ARMOR_INDEX] = (uint8_t)(0x02 | shield);   /* an armor, and the shield bit */
                            ram[HERO_X] = xs[x];
                            ram[SCROLL_X] = (uint8_t)(scroll * 3);
                            ram[HERO_Y] = (uint8_t)(x * 13);
                            ram[SCROLL_Y] = scroll;
                            ram[TICK] = (uint8_t)(x * 7);
                            for (int i = 0; i < 6; ++i) ram[WEAPON_X + i] = (uint8_t)(0xA0 + i);   /* last frame's */
                            uint8_t start[sizeof ram], dagger[6], fist[6];
                            memcpy(start, ram, sizeof ram);
                            ram[PLAYER_CUR_WEAPON] = 0;
                            draw_weapon();
                            box_of(dagger);
                            start[PLAYER_CUR_WEAPON] = FIST;
                            memcpy(ram, start, sizeof ram);
                            CHECK(!draw_weapon());                   /* nothing drawn */
                            box_of(fist);
                            CHECK(!memcmp(dagger, fist, 6));
                            CHECK(placed == 1);
                            /* Nothing but the box: the routine's temporaries are the dagger's alone. */
                            CHECK(!memcmp(ram, start, WEAPON_X) &&
                                  !memcmp(&ram[REACH_Y + 1], &start[REACH_Y + 1], sizeof ram - REACH_Y - 1));
                            off_screen += fist[1] != ram[SCROLL_X];
                            ++cases;
                        }
    CHECK(off_screen > 0 && off_screen < cases);
    /* The swing's two hit poses, facing left on the screen: 8 and 16 pixels
     * left of the hero, 8 and 6 down, reach (8, 8). */
    setup();
    ram[HERO_X] = 200, ram[HERO_Y] = 144;
    ram[PLAYER_CUR_WEAPON] = FIST;
    ram[PLAYER_FLAGS] = ATTACKING;
    ram[ATTACK_PHASE] = 1;
    draw_weapon();
    CHECK(ram[WEAPON_X] == 192 && ram[WEAPON_X_HIGH] == 0 && ram[WEAPON_Y] == 152 && ram[REACH_X] == 8 &&
          ram[REACH_Y] == 8);
    CHECK(last.x == 192 && last.y == 152 && last.on_screen && last.reach_y == 8);
    ram[ATTACK_PHASE] = 2;
    draw_weapon();
    CHECK(ram[WEAPON_X] == 184 && ram[WEAPON_Y] == 150);
}

static void only_while_swinging(void) {
    /* Outside a swing no hit can land: nothing is placed, and the last
     * frame's box stays, as without a weapon in the original. A weapon in
     * hand is the original's. A portrait up returns before the site. */
    setup();
    ram[PLAYER_CUR_WEAPON] = FIST;
    ram[HERO_X] = 120, ram[HERO_Y] = 144;
    for (uint8_t f = 0; f < 0x80; f = (uint8_t)(f + 0x05)) {
        ram[PLAYER_FLAGS] = f;
        uint8_t before[sizeof ram];
        memcpy(before, ram, sizeof ram);
        CHECK(!draw_weapon() && !memcmp(before, ram, sizeof ram));
    }
    ram[PLAYER_FLAGS] = ATTACKING;
    ram[ATTACK_PHASE] = 1;
    ram[PORTRAIT_ID] = 0x02;
    uint8_t before[sizeof ram];
    memcpy(before, ram, sizeof ram);
    CHECK(!draw_weapon() && !memcmp(before, ram, sizeof ram));
    CHECK(placed == 0);
    ram[PORTRAIT_ID] = 0xFF;
    ram[PLAYER_CUR_WEAPON] = DRAGON_SLAYER;
    CHECK(draw_weapon() && placed == 0);
    CHECK(ram[REACH_X] == 0x10 && ram[REACH_Y] == 0x08 && ram[WEAPON_X] == 120 - 16);
}

static void isolated_rooms_uncounted(void) {
    /* Seamless's neighbouring rooms: the same rules, not counted. */
    setup();
    isolated = true;
    CHECK(damage(FIST, false, true) == 3);
    ram[PLAYER_FLAGS] = ATTACKING;
    ram[ATTACK_PHASE] = 1;
    ram[HERO_X] = 100;
    draw_weapon();
    CHECK(ram[WEAPON_X] == 92);
    CHECK(placed == 0 && gloved == 0 && last.x == 0);
    isolated = false;
    draw_weapon();
    CHECK(placed == 1 && last.x == 92);
}

int main(void) {
    registered();
    the_fist_hits();
    the_kills();
    weapons_as_before();
    the_dagger_s_box();
    only_while_swinging();
    isolated_rooms_uncounted();
    off_means_the_original();
    printf("punch_test: %u checks passed\n", checks);
    return 0;
}
