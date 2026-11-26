/* Unit tests for Seams lose nothing (fax_seams.c), without the emulator or a
 * ROM. The module runs against a model of the machine: CPU RAM, the
 * registers, bank 14's MAYBE_SPRITE_EXTENTS, and Sprites_IsSpriteOutOfWeaponRange
 * ($88CB) played instruction by instruction as the disassembly has it, with
 * the hook site at its entry. So the tests check what the game decides, not
 * the module's own arithmetic, against the box the weapon really covers. */
#include "fax_seams.c"
#include <string.h>

static unsigned checks;
#define CHECK(test) do{++checks;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(1);}}while(0)

#define WEAPON_Y          0x00D0u   /* Maybe_Something_WeaponPosY */
#define WEAPON_RANGE_Y    0x00D3u   /* Maybe_WeaponRange_Y */
#define SPRITE_Y          0x0380u   /* Something_Maybe_WeaponDistanceY */
#define SPRITE_EXTENTS_Y  0xB408u   /* MAYBE_SPRITE_EXTENTS_1_ */
enum { ZOZURA = 0x47, COIN = 0x02, KING_GRIEVE = 0x3C, SLOT = 7 };
enum { DAGGER_X = 0x08, DAGGER_Y = 0x04, SWORD_X = 0x10, SWORD_Y = 0x08 };   /* MAYBE_WEAPON_RANGES */

/* ---- the machine ---- */
static uint8_t ram[0x800], bank14[0x4000];
static CycModRegs cpu;
static int reach_on = -1, touch_on = -1;        /* the hooks' enabled states, as last set */
static bool isolated, ticking;                   /* an isolated scope; Seamless's neighbour tick inside it */

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
void cyc_mod_set_regs(const CycModRegs *in) { cpu = *in; }
bool cyc_mod_isolated(void) { return isolated; }
bool fax_world_ticking(void) { return ticking; }
int nes_mod_register_function_entry_plugin(const char *id, uint16_t addr, NESModFunctionEntryCallback cb) {
    return id && ((!strcmp(id, reach_id) && addr == OUT_OF_WEAPON_RANGE && cb == reach_hook) ||
                  (!strcmp(id, touch_id) && addr == CHECK_HIT_PLAYER && cb == touch_hook));
}
int nes_mod_set_function_hook_enabled(const char *id, int on) {
    if (!strcmp(id, reach_id)) reach_on = on;
    else if (!strcmp(id, touch_id)) touch_on = on;
    else return 0;
    return 1;
}
int nes_mod_register_reset_callback(NESModActivationCallback cb) { return cb == deactivate; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    return !strcmp(id, "faxanadu.seams") && cb == activate;
}
void nes_mod_set_local_only(const char *name, int required) { (void)name; (void)required; }

static uint8_t rom(uint16_t addr) { return bank14[addr - 0x8000]; }

/* CMP or SBC's flags on the model's P: C, Z, N, and V for SBC. */
static void compare(uint8_t a, uint8_t m) {
    uint8_t r = (uint8_t)(a - m);
    cpu.p = (uint8_t)((cpu.p & ~(FLAG_C | FLAG_Z | FLAG_N)) | (a >= m ? FLAG_C : 0) | (r ? 0 : FLAG_Z) | (r & FLAG_N));
}
static uint8_t subtract(uint8_t a, uint8_t m) {        /* SEC, SBC */
    uint8_t r = (uint8_t)(a - m);
    compare(a, m);
    cpu.p = (uint8_t)((cpu.p & ~FLAG_V) | (((a ^ m) & (a ^ r) & 0x80) ? FLAG_V : 0));
    return r;
}

/* Sprites_IsSpriteOutOfWeaponRange for slot x: the ROM's code from $88CB,
 * with the hook site at its entry when `hooked` and the hook enabled. True
 * when in range (C clear on return, which the caller's BCC takes). */
static bool in_range(uint8_t x, bool hooked) {
    cpu.x = x;
    if (hooked && reach_on == 1 && reach_hook(OUT_OF_WEAPON_RANGE)) return !(cpu.p & FLAG_C);   /* handled: RTS */
    cpu.a = ram[ENTITIES + x];                                   /* LDA CurrentSprites_Entities,X */
    cpu.a = (uint8_t)(cpu.a << 1);                               /* ASL A */
    cpu.y = cpu.a;                                               /* TAY */
    ram[TEMP_00] = (uint8_t)(rom((uint16_t)(SPRITE_EXTENTS + cpu.y)) + ram[WEAPON_RANGE_X]);   /* LDA, CLC, ADC, STA */
    cpu.a = (uint8_t)(ram[WEAPON_X] + ram[WEAPON_RANGE_X]);      /* LDA PlayerPosXPlus10, CLC, ADC */
    cpu.a = subtract(cpu.a, ram[SPRITE_X]);                      /* SEC, SBC Something_Maybe_WeaponDistanceX */
    compare(cpu.a, ram[TEMP_00]);                                /* CMP Temp_00 */
    if (cpu.p & FLAG_C) return false;                            /* BCS @_return */
    ram[TEMP_00] = (uint8_t)(rom((uint16_t)(SPRITE_EXTENTS_Y + cpu.y)) + ram[WEAPON_RANGE_Y]);
    cpu.a = (uint8_t)(ram[WEAPON_Y] + ram[WEAPON_RANGE_Y]);
    cpu.a = subtract(cpu.a, ram[SPRITE_Y]);
    compare(cpu.a, ram[TEMP_00]);
    return !(cpu.p & FLAG_C);                                    /* BCS @_return, @_return: RTS */
}

/* A sprite of `entity` in SLOT at (sx, sy), and the weapon at x16 (from the
 * screen's left, as Player_DrawWeapon's 16 bits put it) and wy, with a reach. */
static void place(uint8_t entity, uint8_t sx, uint8_t sy, int x16, uint8_t wy, uint8_t reach_x, uint8_t reach_y,
                  uint8_t screen) {
    ram[ENTITIES + SLOT] = entity;
    ram[SPRITE_X] = sx;
    ram[SPRITE_Y] = sy;
    ram[SCROLL_X] = screen;
    ram[WEAPON_X] = (uint8_t)x16;
    ram[WEAPON_X_HIGH] = (uint8_t)(screen + (x16 < 0 ? -1 : x16 > 255 ? 1 : 0));
    ram[WEAPON_Y] = wy;
    ram[WEAPON_RANGE_X] = reach_x;
    ram[WEAPON_RANGE_Y] = reach_y;
}

/* The box the weapon really covers, in 16 bits: from its X, X reach to the
 * right, and Y reach down (GAME_INTERNALS, "Weapon reach"), meets the
 * sprite's from its top-left. */
static bool overlaps(uint8_t entity, int sx, int sy, int x16, int wy, int reach_x, int reach_y) {
    int ex = rom((uint16_t)(SPRITE_EXTENTS + entity * 2)), ey = rom((uint16_t)(SPRITE_EXTENTS_Y + entity * 2));
    int dx = x16 + reach_x - sx, dy = wy + reach_y - sy;
    return dx >= 0 && dx < ex + reach_x && dy >= 0 && dy < ey + reach_y;
}

static void setup(void) {
    memset(ram, 0, sizeof ram);
    memset(bank14, 0xFF, sizeof bank14);
    for (unsigned e = 0; e < 0x66; e++) {                        /* 16 by 16 unless set below */
        bank14[SPRITE_EXTENTS - 0x8000 + e * 2] = 0x10;
        bank14[SPRITE_EXTENTS - 0x8000 + e * 2 + 1] = 0x10;
    }
    bank14[SPRITE_EXTENTS - 0x8000 + COIN * 2] = bank14[SPRITE_EXTENTS - 0x8000 + COIN * 2 + 1] = 0x08;
    bank14[SPRITE_EXTENTS - 0x8000 + KING_GRIEVE * 2] = 0x40;   /* 64 by 16 */
    cpu = (CycModRegs){.a = 0x5A, .x = SLOT, .y = 0x33, .s = 0xF0, .p = 0x24};
    isolated = ticking = false;
    deactivate();
    activate();
    CHECK(reach_on == 1 && touch_on == 1 && enabled);
}

static void the_original_wraps(void) {
    /* The model is the stock game: from Eolis $04's x 4 facing left, the Long
     * Sword's pose 5 (x-16) and pose 6 (x-24) put the weapon off the screen,
     * $CE 244 and 236 with $CF $FF, and both hit a Zozura at x 236 on his
     * ground. Off the screen Player_DrawWeapon keeps the last Y and reach:
     * here a walk's, y+8 with the hero at y 144. */
    setup();
    place(ZOZURA, 236, 160, 4 - 16, 152, SWORD_X, SWORD_Y, 0);
    CHECK(ram[WEAPON_X] == 244 && ram[WEAPON_X_HIGH] == 0xFF);
    CHECK(in_range(SLOT, false));
    place(ZOZURA, 236, 160, 4 - 24, 152, SWORD_X, SWORD_Y, 0);
    CHECK(ram[WEAPON_X] == 236 && in_range(SLOT, false));
    CHECK(!overlaps(ZOZURA, 236, 160, 4 - 24, 152, SWORD_X, SWORD_Y));   /* 232 pixels apart */
}

static void the_ghost_hit_is_refused(void) {
    setup();
    place(ZOZURA, 236, 160, 4 - 16, 152, SWORD_X, SWORD_Y, 0);
    CHECK(!in_range(SLOT, true));
    /* What the X test's failure leaves: A the 8-bit difference, Y the
     * entity twice, Temp_00 its limit, and C set. */
    CHECK(cpu.a == (uint8_t)(244 + SWORD_X - 236) && cpu.y == ZOZURA * 2 && ram[TEMP_00] == 0x10 + SWORD_X);
    CHECK((cpu.p & FLAG_C) && !(cpu.p & FLAG_Z) && cpu.x == SLOT);
    CHECK(refused == 1 && last_weapon_x == -12 && last_sprite_x == 236 && last_entity == ZOZURA);
    /* Pose 6, and at the right edge: the hero at x 250 before he crosses
     * (Seamless), the weapon at x 274 ($CE 18, $CF 1), a Zozura at x 10. */
    place(ZOZURA, 236, 160, 4 - 24, 152, SWORD_X, SWORD_Y, 0);
    CHECK(!in_range(SLOT, true) && refused == 2);
    place(ZOZURA, 10, 160, 250 + 24, 152, SWORD_X, SWORD_Y, 0);
    CHECK(ram[WEAPON_X] == 18 && ram[WEAPON_X_HIGH] == 1 && in_range(SLOT, false));
    CHECK(!in_range(SLOT, true) && refused == 3 && last_weapon_x == 274);
    /* On the screen, the reach added wraps too: the weapon at x 6 (the hero
     * at 30 facing left) and a Zozura at 250. */
    place(ZOZURA, 250, 160, 6, 152, SWORD_X, SWORD_Y, 0);
    CHECK(ram[WEAPON_X_HIGH] == 0 && in_range(SLOT, false));
    CHECK(!in_range(SLOT, true) && refused == 4 && last_weapon_x == 6);
    /* Whatever the screen's high byte. */
    place(ZOZURA, 236, 160, 4 - 16, 152, SWORD_X, SWORD_Y, 3);
    CHECK(ram[WEAPON_X_HIGH] == 2 && in_range(SLOT, false) && !in_range(SLOT, true));
}

static void real_hits_are_the_originals(void) {
    /* In front of him, and a box half off the screen meeting a sprite at its
     * edge: the original decides, and the registers are its own. */
    setup();
    place(ZOZURA, 100, 160, 120 - 16, 152, SWORD_X, SWORD_Y, 0);
    CHECK(in_range(SLOT, true) && refused == 0);
    place(ZOZURA, 0, 160, 4 - 16, 152, SWORD_X, SWORD_Y, 0);   /* -12: the box covers -12 to 4 */
    CHECK(overlaps(ZOZURA, 0, 160, -12, 152, SWORD_X, SWORD_Y) && in_range(SLOT, true) && refused == 0);
    place(ZOZURA, 240, 160, 250, 152, SWORD_X, SWORD_Y, 0);   /* facing right at the right edge */
    CHECK(in_range(SLOT, true) && refused == 0);
    place(ZOZURA, 100, 100, 120 - 16, 152, SWORD_X, SWORD_Y, 0);   /* too high: the Y test, untouched */
    CHECK(!in_range(SLOT, true) && !in_range(SLOT, false) && refused == 0);
}

static void every_place_against_the_true_box(void) {
    /* Every weapon X a swing can reach (the hero on the screen, plus the
     * offsets of 24 either way and the reach) against every sprite X, at a
     * height both weapons reach (y 150, under a box from y 152): hooked,
     * the game hits exactly when the boxes meet; unhooked, it also hits a
     * screen away, and only then do the two differ. */
    setup();
    static const uint8_t reaches[][2] = {{DAGGER_X, DAGGER_Y}, {SWORD_X, SWORD_Y}};
    static const uint8_t entities[] = {ZOZURA, COIN, KING_GRIEVE};
    unsigned ghosts = 0;
    for (unsigned r = 0; r < 2; r++)
        for (unsigned e = 0; e < sizeof entities; e++)
            for (int x16 = -24; x16 <= 255 + 25; x16++)
                for (int sx = 0; sx < 256; sx++) {
                    place(entities[e], (uint8_t)sx, 150, x16, 152, reaches[r][0], reaches[r][1], 0);
                    bool truth = overlaps(entities[e], sx, 150, x16, 152, reaches[r][0], reaches[r][1]);
                    bool stock = in_range(SLOT, false), fixed = in_range(SLOT, true);
                    if (fixed != truth) {
                        fprintf(stderr, "x16 %d sx %d entity %u reach %u: fixed %d truth %d\n", x16, sx,
                                entities[e], reaches[r][0], fixed, truth);
                        CHECK(false);
                    }
                    if (stock != fixed) {
                        CHECK(stock && !fixed && (sx - x16 > 128 || x16 - sx > 128));   /* a screen away */
                        ++ghosts;
                    }
                }
    CHECK(ghosts > 0 && refused == ghosts);
}

static void the_same_rule_in_a_copy_uncounted(void) {
    setup();
    isolated = ticking = true;
    place(ZOZURA, 236, 160, 4 - 16, 152, SWORD_X, SWORD_Y, 0);
    CHECK(!in_range(SLOT, true) && refused == 0 && last_entity == 0xFF);
}

static void copies_touch_nothing(void) {
    /* CurrentSprite_CheckHitPlayer's entry: in a neighbour's tick it returns
     * at once with X the current slot; anywhere else the original runs, the
     * registers untouched. */
    setup();
    ram[CURRENT_SPRITE_INDEX] = 3;
    cpu = (CycModRegs){.a = 0x11, .x = 0x80, .y = 0x22, .s = 0xF0, .p = 0x24 | FLAG_N};
    CycModRegs before = cpu;
    CHECK(touch_hook(CHECK_HIT_PLAYER) == 0 && !memcmp(&before, &cpu, sizeof cpu));   /* the live game */
    isolated = true;
    CHECK(touch_hook(CHECK_HIT_PLAYER) == 0 && !memcmp(&before, &cpu, sizeof cpu));   /* another isolated call */
    ticking = true;
    CHECK(touch_hook(CHECK_HIT_PLAYER) == 1);
    CHECK(cpu.x == 3 && cpu.a == 0x11 && cpu.y == 0x22 && cpu.s == 0xF0 && !(cpu.p & (FLAG_N | FLAG_Z)));
    ram[CURRENT_SPRITE_INDEX] = 0;
    CHECK(touch_hook(CHECK_HIT_PLAYER) == 1 && cpu.x == 0 && (cpu.p & FLAG_Z) && skipped == 2);
    ticking = isolated = false;
    CHECK(touch_hook(CHECK_HIT_PLAYER) == 0 && skipped == 2);
}

static void off_means_off(void) {
    setup();
    place(ZOZURA, 236, 160, 4 - 16, 152, SWORD_X, SWORD_Y, 0);
    CHECK(!in_range(SLOT, true) && refused == 1);
    deactivate();
    CHECK(reach_on == 0 && touch_on == 0 && !enabled && refused == 0 && skipped == 0 && last_entity == 0xFF);
    CHECK(in_range(SLOT, true));                                 /* the hook is off: the original's ghost hit */
}

int main(void) {
    the_original_wraps();
    the_ghost_hit_is_refused();
    real_hits_are_the_originals();
    every_place_against_the_true_box();
    the_same_rule_in_a_copy_uncounted();
    copies_touch_nothing();
    off_means_off();
    printf("seams_test: %u checks passed\n", checks);
    return 0;
}
