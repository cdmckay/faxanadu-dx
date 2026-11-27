/* Unit tests for Seams (fax_seams.c), without the emulator or a ROM.
 *
 * Slice 0, seams lose nothing: the module runs against a model of the
 * machine: CPU RAM, the registers, bank 14's MAYBE_SPRITE_EXTENTS, and
 * Sprites_IsSpriteOutOfWeaponRange ($88CB) played instruction by instruction
 * as the disassembly has it, with the hook site at its entry. So the tests
 * check what the game decides, not the module's own arithmetic, against the
 * box the weapon really covers.
 *
 * Slice 1, seams are open ground: a model of Seamless's rooms too (three side
 * by side, 5, 4 and 3, room 4 the live game's; each room's RAM and Seams'
 * block; the view), and bank 14's and 15's tables as the ROM has them. The
 * edge and floor tests are checked against the blocks a sprite really
 * covers in the joined world, the draw's cells against where each really
 * lies in 16 bits, and the moves byte for byte. The rooms are the Trunk's
 * (a region), but for towns stay safe: Eolis's, with bank 11's lists for
 * its screens as the ROM has them, and fax_zone.c's zones.
 *
 * Slice 3, the spell flies on: CastMagic_UpdateXPosition ($BCAF) and
 * CastMagic_CheckDirection ($BD8F) played as the disassembly has them, with
 * the hook sites at $BD8F and the two off-screen CMPs, and the update
 * handlers' use of C and Blocks_Result; every flight from every 7th place,
 * each way, three spells, a wall in each of many columns of the three rooms
 * and either side unjoined, checked frame by frame against the truth in the
 * joined world (its X in 16 bits, the view's edge, the walls of the room each
 * pixel lies in), and in an arena against the original's own code. */
#include "fax_seams.c"
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

#define WEAPON_Y         0x00D0u /* Maybe_Something_WeaponPosY */
#define WEAPON_RANGE_Y   0x00D3u /* Maybe_WeaponRange_Y */
#define SPRITE_Y         0x0380u /* Something_Maybe_WeaponDistanceY */
#define SPRITE_EXTENTS_Y 0xB408u /* MAYBE_SPRITE_EXTENTS_1_ */
#define INTERNAL_STATES  0x02FCu /* CurrentSprites_InternalBehaviorStates: a death keeps the entity there */
#define HP_              0x0344u /* CurrentSprites_HP */
enum { ZOZURA = 0x47, COIN = 0x02, KING_GRIEVE = 0x3C, SLOT = 7 };
enum { FIREBALL = 0x0A, WALKING_MAN = 0x34, MAN = 0x3D, ROKUSUTAHN = 0x11 };
enum { DAGGER_X = 0x08, DAGGER_Y = 0x04, SWORD_X = 0x10, SWORD_Y = 0x08 }; /* MAYBE_WEAPON_RANGES */

/* ---- the machine ---- */
static uint8_t ram[0x800], bank11[0x4000], bank14[0x4000], bank15[0x4000];
static CycModRegs cpu;
static int reach_on = -1, touch_on = -1; /* the slice 0 hooks' enabled states, as last set */
static int hooks_on[HOOKS];
static bool isolated, ticking; /* an isolated scope; Seamless's neighbour tick inside it */

uint8_t cyc_mod_peek(uint16_t addr) {
    if (addr < 0x2000) return ram[addr & 0x7FF];
    if (addr >= 0x8000 && addr < 0xC000) return bank14[addr - 0x8000];
    return addr >= 0xC000 ? bank15[addr - 0xC000] : 0;
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
    for (unsigned i = 0; i < HOOKS; i++)
        if (id && !strcmp(id, hooks[i].id)) return addr == hooks[i].addr && cb == hooks[i].hook;
    return 0;
}
int nes_mod_set_function_hook_enabled(const char *id, int on) {
    if (!strcmp(id, "faxanadu.seams.reach")) reach_on = on;
    if (!strcmp(id, "faxanadu.seams.touch")) touch_on = on;
    for (unsigned i = 0; i < HOOKS; i++)
        if (!strcmp(id, hooks[i].id)) {
            hooks_on[i] = on;
            return 1;
        }
    return 0;
}
int nes_mod_register_reset_callback(NESModActivationCallback cb) { return cb == deactivate; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    return !strcmp(id, "faxanadu.seams") && cb == activate;
}
void nes_mod_set_local_only(const char *name, int required) {
    (void)name;
    (void)required;
}
static NESModSavestateGet record_get;
static NESModSavestateSet record_set;
static NESModSavestateValidate record_validate;
int nes_mod_register_savestate_hook(const char *id, NESModSavestateGet get, NESModSavestateSet set) {
    if (strcmp(id, "faxanadu.seams")) return 0;
    record_get = get, record_set = set;
    return 1;
}
int nes_mod_register_savestate_validator(const char *id, NESModSavestateValidate validate) {
    if (strcmp(id, "faxanadu.seams")) return 0;
    record_validate = validate;
    return 1;
}
/* Committed calls: only Sound_PlayEffect, with A the effect (slice 2). */
static uint8_t played_effects[16];
static unsigned committed;
bool cyc_mod_call_commit(uint16_t routine, CycModRegs *regs) {
    CHECK(routine == SOUND_PLAY_EFFECT && !isolated);
    if (committed < 16) played_effects[committed] = regs->a;
    ++committed;
    return true;
}

/* ---- Seamless's rooms ---- */
static uint8_t records[ROOMS][2048], blocks[ROOMS][FAX_WORLD_SEAMS_BYTES];
static int place_x[ROOMS], edge[ROOMS][2], active_room = -1, tick_room = -1, area_now = FAX_AREA_TRUNK;
static bool live_room[ROOMS], view_ok;
static int view_x, view_y;
static unsigned loads;
int fax_world_seams_room(void) { return ticking ? tick_room : isolated ? -1 : active_room; }
int fax_world_active_room(void) { return active_room; }
int fax_world_area(void) { return area_now; }
bool fax_world_room_live(int r) { return r >= 0 && r < ROOMS && live_room[r]; }
int fax_world_joined(int r, int side) {
    if (r < 0 || r >= ROOMS) return -1;
    int n = edge[r][side];
    return n >= 0 && live_room[n] ? n : -1;
}
uint8_t *fax_world_seams_block(int r) { return r >= 0 && r < ROOMS ? blocks[r] : NULL; }
uint8_t *fax_world_room_record(int r) { return r >= 0 && r < ROOMS ? records[r] : NULL; }
static int place_y[ROOMS]; /* rows of rooms: all in one but where a test moves one */
void fax_world_place(int r, int *x, int *y) {
    *x = place_x[r] * 256;
    *y = place_y[r] * 208;
}
bool fax_world_view(int *x, int *y) {
    if (!view_ok) return false;
    *x = view_x, *y = view_y;
    return true;
}
static int hero_wx, hero_wy; /* the hero's place, in world pixels */
bool fax_world_hero(int *x, int *y) {
    if (!view_ok) return false;
    *x = hero_wx, *y = hero_wy;
    return true;
}
void fax_world_update_view(void) {}
void fax_world_screen_loaded(void) { ++loads; }
uint8_t fax_world_rom(unsigned bank, unsigned addr) {
    return bank == 11   ? bank11[addr & 0x3FFF]
           : bank == 14 ? bank14[addr & 0x3FFF]
           : bank == 15 ? bank15[addr & 0x3FFF]
                        : 0;
}

static uint8_t rom(uint16_t addr) { return bank14[addr - 0x8000]; }

/* CMP or SBC's flags on the model's P: C, Z, N, and V for SBC. */
static void compare(uint8_t a, uint8_t m) {
    uint8_t r = (uint8_t)(a - m);
    cpu.p = (uint8_t)((cpu.p & ~(FLAG_C | FLAG_Z | FLAG_N)) | (a >= m ? FLAG_C : 0) | (r ? 0 : FLAG_Z) | (r & FLAG_N));
}
static uint8_t subtract(uint8_t a, uint8_t m) { /* SEC, SBC */
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
    if (hooked && reach_on == 1 && reach_hook(OUT_OF_WEAPON_RANGE)) return !(cpu.p & FLAG_C); /* handled: RTS */
    cpu.a = ram[ENTITIES + x];     /* LDA CurrentSprites_Entities,X */
    cpu.a = (uint8_t)(cpu.a << 1); /* ASL A */
    cpu.y = cpu.a;                 /* TAY */
    ram[TEMP_00] = (uint8_t)(rom((uint16_t)(SPRITE_EXTENTS + cpu.y)) + ram[WEAPON_RANGE_X]); /* LDA, CLC, ADC, STA */
    cpu.a = (uint8_t)(ram[WEAPON_X] + ram[WEAPON_RANGE_X]); /* LDA PlayerPosXPlus10, CLC, ADC */
    cpu.a = subtract(cpu.a, ram[SPRITE_X]);                 /* SEC, SBC Something_Maybe_WeaponDistanceX */
    compare(cpu.a, ram[TEMP_00]);                           /* CMP Temp_00 */
    if (cpu.p & FLAG_C) return false;                       /* BCS @_return */
    ram[TEMP_00] = (uint8_t)(rom((uint16_t)(SPRITE_EXTENTS_Y + cpu.y)) + ram[WEAPON_RANGE_Y]);
    cpu.a = (uint8_t)(ram[WEAPON_Y] + ram[WEAPON_RANGE_Y]);
    cpu.a = subtract(cpu.a, ram[SPRITE_Y]);
    compare(cpu.a, ram[TEMP_00]);
    return !(cpu.p & FLAG_C); /* BCS @_return, @_return: RTS */
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
    for (unsigned e = 0; e < 0x66; e++) { /* 16 by 16 unless set below */
        bank14[SPRITE_EXTENTS - 0x8000 + e * 2] = 0x10;
        bank14[SPRITE_EXTENTS - 0x8000 + e * 2 + 1] = 0x10;
    }
    bank14[SPRITE_EXTENTS - 0x8000 + COIN * 2] = bank14[SPRITE_EXTENTS - 0x8000 + COIN * 2 + 1] = 0x08;
    bank14[SPRITE_EXTENTS - 0x8000 + KING_GRIEVE * 2] = 0x40; /* 64 by 16 */
    cpu = (CycModRegs){ .a = 0x5A, .x = SLOT, .y = 0x33, .s = 0xF0, .p = 0x24 };
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
    CHECK(!overlaps(ZOZURA, 236, 160, 4 - 24, 152, SWORD_X, SWORD_Y)); /* 232 pixels apart */
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
    place(ZOZURA, 0, 160, 4 - 16, 152, SWORD_X, SWORD_Y, 0); /* -12: the box covers -12 to 4 */
    CHECK(overlaps(ZOZURA, 0, 160, -12, 152, SWORD_X, SWORD_Y) && in_range(SLOT, true) && refused == 0);
    place(ZOZURA, 240, 160, 250, 152, SWORD_X, SWORD_Y, 0); /* facing right at the right edge */
    CHECK(in_range(SLOT, true) && refused == 0);
    place(ZOZURA, 100, 100, 120 - 16, 152, SWORD_X, SWORD_Y, 0); /* too high: the Y test, untouched */
    CHECK(!in_range(SLOT, true) && !in_range(SLOT, false) && refused == 0);
}

static void every_place_against_the_true_box(void) {
    /* Every weapon X a swing can reach (the hero on the screen, plus the
     * offsets of 24 either way and the reach) against every sprite X, at a
     * height both weapons reach (y 150, under a box from y 152): hooked,
     * the game hits exactly when the boxes meet; unhooked, it also hits a
     * screen away, and only then do the two differ. */
    setup();
    static const uint8_t reaches[][2] = { { DAGGER_X, DAGGER_Y }, { SWORD_X, SWORD_Y } };
    static const uint8_t entities[] = { ZOZURA, COIN, KING_GRIEVE };
    unsigned ghosts = 0;
    for (unsigned r = 0; r < 2; r++)
        for (unsigned e = 0; e < sizeof entities; e++)
            for (int x16 = -24; x16 <= 255 + 25; x16++)
                for (int sx = 0; sx < 256; sx++) {
                    place(entities[e], (uint8_t)sx, 150, x16, 152, reaches[r][0], reaches[r][1], 0);
                    bool truth = overlaps(entities[e], sx, 150, x16, 152, reaches[r][0], reaches[r][1]);
                    bool stock = in_range(SLOT, false), fixed = in_range(SLOT, true);
                    if (fixed != truth) {
                        fprintf(stderr, "x16 %d sx %d entity %u reach %u: fixed %d truth %d\n", x16, sx, entities[e],
                                reaches[r][0], fixed, truth);
                        CHECK(false);
                    }
                    if (stock != fixed) {
                        CHECK(stock && !fixed && (sx - x16 > 128 || x16 - sx > 128)); /* a screen away */
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
    cpu = (CycModRegs){ .a = 0x11, .x = 0x80, .y = 0x22, .s = 0xF0, .p = 0x24 | FLAG_N };
    CycModRegs before = cpu;
    CHECK(touch_hook(CHECK_HIT_PLAYER) == 0 && !memcmp(&before, &cpu, sizeof cpu)); /* the live game */
    isolated = true;
    CHECK(touch_hook(CHECK_HIT_PLAYER) == 0 && !memcmp(&before, &cpu, sizeof cpu)); /* another isolated call */
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
    CHECK(in_range(SLOT, true)); /* the hook is off: the original's ghost hit */
}

/* ==== slice 1 ==== */

enum { LEFT_ROOM = 5, LIVE = 4, RIGHT_ROOM = 3, AIR = 0, SOLID = 1, LADDER = 2, DATA = 0xA000 };

/* Rooms 5, 4 and 3 side by side, every one's actors running, room 4 the
 * live game's (the machine's RAM); the tables of banks 14 and 15 as the ROM
 * has them; block id 0 air, 1 solid, 2 a ladder in every room. */
static void world(void) {
    setup();
    memset(records, 0, sizeof records);
    memset(blocks, 0, sizeof blocks);
    memset(live_room, 0, sizeof live_room);
    memset(bank15, 0, sizeof bank15);
    static const uint8_t widths[] = { 15, 15, 31, 31, 31, 63, 47 }, heights[] = { 15, 31, 47, 63, 31, 31, 79 };
    static const uint8_t impassable[] = { 0, 1, 0, 0, 0, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0 };
    memcpy(bank14 + (HITBOX_WIDTHS - 0x8000), widths, sizeof widths);
    memcpy(bank14 + (HITBOX_HEIGHTS - 0x8000), heights, sizeof heights);
    memset(bank14 + (CATEGORIES - 0x8000), 5, 0x66); /* items, unless set below */
    bank14[CATEGORIES - 0x8000 + ZOZURA] = 0;
    bank14[CATEGORIES - 0x8000 + COIN] = 1;
    bank14[CATEGORIES - 0x8000 + FIREBALL] = 6;
    bank14[CATEGORIES - 0x8000 + WALKING_MAN] = 2;
    bank14[CATEGORIES - 0x8000 + MAN] = 2;
    bank14[CATEGORIES - 0x8000 + ROKUSUTAHN] = 7;
    memcpy(bank15 + (BLOCK_IMPASSABLE - 0xC000), impassable, sizeof impassable);
    for (unsigned i = 0; i < 16; i++) bank15[TILE_POSITIONS - 0xC000 + i] = (uint8_t)(i * 8);
    bank15[VISIBILITY_MASK - 0xC000] = 1, bank15[VISIBILITY_MASK - 0xC000 + 1] = 2;
    bank15[VISIBILITY_FLIPPED - 0xC000] = 2, bank15[VISIBILITY_FLIPPED - 0xC000 + 1] = 1;
    int ids[3] = { LEFT_ROOM, LIVE, RIGHT_ROOM };
    for (int i = 0; i < 3; i++) {
        int r = ids[i];
        live_room[r] = true;
        place_x[r] = i - 1;
        edge[r][0] = i > 0 ? ids[i - 1] : -1;
        edge[r][1] = i < 2 ? ids[i + 1] : -1;
        memset(records[r] + ENTITIES, EMPTY, 8);
        records[r][BLOCK_PROPERTIES] = 0x10;     /* id 0 air, id 1 solid */
        records[r][BLOCK_PROPERTIES + 1] = 0x02; /* id 2 a ladder */
    }
    edge[LEFT_ROOM][0] = edge[RIGHT_ROOM][1] = -1;
    memcpy(ram, records[LIVE], sizeof ram);
    active_room = LIVE, tick_room = -1, area_now = FAX_AREA_TRUNK;
    isolated = ticking = false;
    view_ok = true, view_x = -84, view_y = 0; /* the hero at room 4's x 120 */
    cpu = (CycModRegs){ .a = 0, .x = 0, .y = 0, .s = 0xF0, .p = 0x24 };
}
/* Run `room`'s sprite loop: the live game's for room 4, else its tick on
 * the copy (the machine holds the room's RAM); and back. */
static void run(int room) {
    if (room == LIVE) {
        isolated = ticking = false;
        return;
    }
    memcpy(records[active_room], ram, sizeof ram);
    memcpy(ram, records[room], sizeof ram);
    isolated = ticking = true, tick_room = room;
}
static void done(int room) {
    if (room == LIVE) return;
    memcpy(records[room], ram, sizeof ram);
    memcpy(ram, records[active_room], sizeof ram);
    isolated = ticking = false, tick_room = -1;
}
static uint8_t *mem(int room) { return room == fax_world_seams_room() ? ram : records[room]; }
static Block *blk(int room) { return (Block *)blocks[room]; }
static void put(int room, unsigned s, uint8_t entity, uint8_t x, uint8_t y, bool right, bool woken) {
    uint8_t *m = mem(room);
    m[ENTITIES + s] = entity, m[SLOT_X + s] = x, m[SLOT_Y + s] = y, m[HITBOX + s] = 0;
    m[FLAGS + s] = right ? 1 : 0;
    if (woken) blk(room)->awake |= (uint8_t)(1u << s);
}
static void block_at(int room, int col, int row, uint8_t id) { mem(room)[SCREEN_BUFFER + row * 16 + col] = id; }

/* The truth: whether pixel (px, py) of `room` (px past its sides in the
 * room joined there) lies in a solid block, or one solid or a ladder. */
static bool truly(int room, int px, int py, bool ladder) {
    int r = px < 0 ? edge[room][0] : px >= 256 ? edge[room][1] : room;
    if (r < 0) return true;
    uint8_t id = mem(r)[SCREEN_BUFFER + (py & 0xF0) + ((px & 0xFF) >> 4)];
    return id == SOLID || (ladder && id == LADDER);
}
/* Any solid block among the rows [y, y + ext] at column px, or the columns
 * [x, x + ext] at row y. */
static bool any_down(int room, int px, int y, int ext) {
    for (int t = y; t <= y + ext; t++)
        if (truly(room, px, t, false)) return true;
    return false;
}
static bool any_across(int room, int x, int py, int ext) {
    for (int t = x; t <= x + ext; t++)
        if (truly(room, t, py, false)) return true;
    return false;
}
static unsigned rand_state = 1;
static unsigned next_rand(void) {
    rand_state = rand_state * 1103515245u + 12345u;
    return rand_state >> 16;
}
static void scatter(int room, int col) {
    for (int row = 0; row < 13; row++) block_at(room, col, row, next_rand() % 3 == 0 ? SOLID : AIR);
}
static int test_hook(int (*hook)(uint16_t), unsigned s, bool by_x) {
    if (by_x) cpu.x = (uint8_t)s;
    else cpu.y = (uint8_t)s;
    ram[BLOCKS_RESULT] = 0xEE;
    return hook(0);
}

static void walking_right_through_the_seam(void) {
    /* Room 5's tick: a Zozura past its right side, X 241-255 (X + 15
     * carries: the original's wall). Against every place, with blocks
     * scattered in room 4's first column: the test is room 4's, at its
     * column the hitbox's right edge is in, down its rows. */
    for (unsigned trial = 0; trial < 40; trial++) {
        world();
        scatter(LIVE, 0), scatter(LIVE, 1);
        run(LEFT_ROOM);
        for (int x = 241; x <= 255; x++)
            for (int y = 96; y <= 176; y += 5) {
                blk(LEFT_ROOM)->through[SLOT] = 0;
                put(LEFT_ROOM, SLOT, ZOZURA, (uint8_t)x, (uint8_t)y, true, true);
                CHECK(test_hook(right_hook, SLOT, false) == 1);
                bool truth = any_down(LEFT_ROOM, x + 15, y, 15);
                CHECK(!ram[BLOCKS_RESULT] == !truth && cpu.x == SLOT && cpu.a == ram[BLOCKS_RESULT]);
                CHECK(blk(LEFT_ROOM)->through[SLOT] == (truth ? 0 : THROUGH_RIGHT));
                CHECK(ram[PIXEL_X] == (uint8_t)(x + 15) && ram[PIXEL_Y] == y);
            }
        done(LEFT_ROOM);
    }
    /* Inside the room, the original's test. */
    world();
    run(LEFT_ROOM);
    put(LEFT_ROOM, SLOT, ZOZURA, 200, 160, true, true);
    CHECK(test_hook(right_hook, SLOT, false) == 0 && ram[BLOCKS_RESULT] == 0xEE);
    done(LEFT_ROOM);
}

static void walking_left_through_the_seam(void) {
    /* The live game: a Zozura that walked left past x 0 (X wrapped, $F1 and
     * up): room 5's last column. And one straddling room 4's right side,
     * let through it (in from room 3), walking left: its own column 15. */
    for (unsigned trial = 0; trial < 40; trial++) {
        world();
        scatter(LEFT_ROOM, 15), scatter(LIVE, 15);
        for (int x = 0xF1; x <= 0xFF; x++)
            for (int y = 96; y <= 176; y += 5) {
                blk(LIVE)->through[SLOT] = 0;
                put(LIVE, SLOT, ZOZURA, (uint8_t)x, (uint8_t)y, false, true);
                CHECK(test_hook(left_hook, SLOT, false) == 1);
                bool truth = any_down(LIVE, x - 256, y, 15);
                CHECK(!ram[BLOCKS_RESULT] == !truth && blk(LIVE)->through[SLOT] == (truth ? 0 : THROUGH_LEFT));
                blk(LIVE)->through[SLOT] = THROUGH_RIGHT;
                CHECK(test_hook(left_hook, SLOT, false) == 1);
                CHECK(!ram[BLOCKS_RESULT] == !any_down(LIVE, x, y, 15) && blk(LIVE)->through[SLOT] == THROUGH_RIGHT);
            }
    }
}

static void walls_stay_walls(void) {
    world();
    /* No room joined that way: the original's wall. */
    run(RIGHT_ROOM);
    put(RIGHT_ROOM, SLOT, ZOZURA, 250, 160, true, true);
    CHECK(test_hook(right_hook, SLOT, false) == 0);
    done(RIGHT_ROOM);
    /* Not awake, not a mover (a person who stands, a big enemy), or Seamless
     * with no room: the original's. */
    put(LIVE, SLOT, ZOZURA, 250, 160, true, false);
    CHECK(test_hook(right_hook, SLOT, false) == 0);
    put(LIVE, SLOT, MAN, 250, 160, true, true);
    CHECK(test_hook(right_hook, SLOT, false) == 0);
    put(LIVE, SLOT, ROKUSUTAHN, 250, 160, true, true);
    CHECK(test_hook(right_hook, SLOT, false) == 0);
    put(LIVE, SLOT, WALKING_MAN, 250, 160, true, true);
    CHECK(test_hook(right_hook, SLOT, false) == 1); /* a person who walks goes */
    view_ok = false;                                /* the live machine holds another room */
    CHECK(test_hook(right_hook, SLOT, false) == 0);
}

static void a_full_room_is_a_wall(void) {
    world();
    /* Room 3 holding 5 actors (dormant ones too): a mover from room 4 stays
     * out, counted. With 4 it goes in, and a mover on its way in from room
     * 3's other side counts as one of the 5. One already through goes on. */
    for (unsigned k = 0; k < 5; k++) records[RIGHT_ROOM][ENTITIES + k] = ZOZURA;
    put(LIVE, SLOT, ZOZURA, 250, 160, true, true);
    CHECK(test_hook(right_hook, SLOT, false) == 0 && kept_out == 1 && !blk(LIVE)->through[SLOT]);
    records[RIGHT_ROOM][ENTITIES + 4] = EMPTY;
    CHECK(test_hook(right_hook, SLOT, false) == 1 && blk(LIVE)->through[SLOT] == THROUGH_RIGHT);
    blk(LIVE)->through[SLOT] = 0;
    live_room[2] = true, edge[RIGHT_ROOM][1] = 2, edge[2][0] = RIGHT_ROOM;
    blk(2)->through[0] = THROUGH_LEFT; /* on its way into room 3 from its right */
    CHECK(test_hook(right_hook, SLOT, false) == 0 && kept_out == 2);
    blk(LIVE)->through[SLOT] = THROUGH_RIGHT; /* already through: not entering */
    CHECK(test_hook(right_hook, SLOT, false) == 1);
    /* The flier's edge the same. */
    blk(LIVE)->through[SLOT] = 0;
    put(LIVE, SLOT, FIREBALL, 0xF4, 150, true, true);
    CHECK(test_hook(flier_hook, SLOT, true) == 0 && kept_out == 3);
}

static void floors_across_the_seam(void) {
    /* Straddling room 4's right side (through it), or wrapped past it: the
     * up and down test's row across the hitbox's columns, and the placement
     * test's leading column, in the room each column is in. */
    for (unsigned trial = 0; trial < 40; trial++) {
        world();
        scatter(LIVE, 14), scatter(LIVE, 15), scatter(RIGHT_ROOM, 0), scatter(RIGHT_ROOM, 1);
        mem(RIGHT_ROOM)[SCREEN_BUFFER + 10 * 16 + 0] = LADDER;
        for (int x16 = 233; x16 <= 270; x16++)
            for (int py = 100; py <= 190; py += 9) {
                bool wrapped = x16 >= 256;
                put(LIVE, SLOT, ZOZURA, (uint8_t)x16, 150, true, true);
                blk(LIVE)->through[SLOT] = x16 + 15 > 255 ? THROUGH_RIGHT : 0;
                ram[PIXEL_Y] = (uint8_t)py;
                int handled = test_hook(vertical_hook, SLOT, false);
                CHECK(handled == (x16 + 15 > 255));
                if (handled) CHECK(!ram[BLOCKS_RESULT] == !any_across(LIVE, x16, py, 15));
                ram[PIXEL_Y] = (uint8_t)py;
                handled = test_hook(placement_hook, SLOT, true);
                CHECK(handled == (x16 + 15 > 255));
                if (handled) CHECK(!cpu.a == !truly(LIVE, x16 + 15, py, true));
                (void)wrapped;
            }
    }
}

static void fliers_and_shots(void) {
    world();
    /* X $F0 and up facing right: through room 4's right side, "not at the
     * edge" (C clear). Facing left there, wrapped past its left side. */
    put(LIVE, SLOT, FIREBALL, 0xF2, 150, true, true);
    cpu.p = 0x24 | FLAG_C;
    CHECK(test_hook(flier_hook, SLOT, true) == 1 && !(cpu.p & FLAG_C) && cpu.a == 0xF2);
    CHECK(blk(LIVE)->through[SLOT] == THROUGH_RIGHT);
    blk(LIVE)->through[SLOT] = 0;
    put(LIVE, SLOT, FIREBALL, 0xFE, 150, false, true);
    CHECK(test_hook(flier_hook, SLOT, true) == 1 && blk(LIVE)->through[SLOT] == THROUGH_LEFT);
    /* On the screen: the original's. */
    put(LIVE, SLOT, FIREBALL, 0x80, 150, false, true);
    CHECK(test_hook(flier_hook, SLOT, true) == 0);
}

static void dormant_until_seen(void) {
    world();
    /* Room 5's tick: a Zozura at its x 100 (world -156), the view from -84:
     * it stands still and isn't drawn. Within 16 pixels of the view it
     * wakes, and stays awake out of it. */
    run(LEFT_ROOM);
    put(LEFT_ROOM, SLOT, ZOZURA, 100, 160, false, false);
    ram[CURRENT_SPRITE_INDEX] = SLOT;
    CHECK(behavior_hook(UPDATE_BEHAVIOR) == 1 && cpu.x == SLOT && state_hook(UPDATE_STATE) == 1);
    CHECK(frozen == 1 && !blk(LEFT_ROOM)->awake);
    view_x = -156 + 16 + PAD; /* its right edge, padded: just short */
    CHECK(behavior_hook(UPDATE_BEHAVIOR) == 1);
    view_x -= 1;
    CHECK(behavior_hook(UPDATE_BEHAVIOR) == 0 && (blk(LEFT_ROOM)->awake >> SLOT & 1) && woken == 1);
    CHECK(state_hook(UPDATE_STATE) == 0);
    view_x = 400;
    CHECK(behavior_hook(UPDATE_BEHAVIOR) == 0 && state_hook(UPDATE_STATE) == 0);
    /* Not sleepers: a coin, a person who stands. A big enemy sleeps. */
    put(LEFT_ROOM, 2, COIN, 100, 160, false, false);
    ram[CURRENT_SPRITE_INDEX] = 2;
    CHECK(behavior_hook(UPDATE_BEHAVIOR) == 0 && state_hook(UPDATE_STATE) == 0);
    put(LEFT_ROOM, 2, MAN, 100, 160, false, false);
    CHECK(behavior_hook(UPDATE_BEHAVIOR) == 0);
    put(LEFT_ROOM, 2, ROKUSUTAHN, 100, 160, false, false);
    CHECK(behavior_hook(UPDATE_BEHAVIOR) == 1);
    done(LEFT_ROOM);
}

/* The draw's truth: each cell of a sprite at 16-bit X (the high byte's sign
 * and the low byte), its columns 8 apart (mirrored round the pivot when
 * flipped), its rows 8 apart. */
static void drawn_where_it_lies(void) {
    for (unsigned flipped = 0; flipped < 2; flipped++)
        for (unsigned cols = 0; cols < 4; cols++)
            for (int base = -40; base < 300; base += 3) {
                world();
                unsigned rows = 1, s = 5;
                uint16_t data = DATA;
                for (unsigned i = 0, at = 0; i < (rows + 1) * (cols + 1);
                     i++) { /* a tile and attributes a cell, $FF one empty */
                    if (i == 1) {
                        bank14[data - 0x8000 + at++] = 0xFF;
                        continue;
                    }
                    bank14[data - 0x8000 + at++] = (uint8_t)(0x10 + i);
                    bank14[data - 0x8000 + at++] = (uint8_t)(i & 3);
                }
                ram[COLS] = (uint8_t)cols, ram[ROWS] = (uint8_t)rows, ram[NORM_Y] = 120, ram[OFFSET_Y] = 0;
                ram[NORM_X] = (uint8_t)base, ram[OFFSET_X] = (uint8_t)(base < 0 ? 0xFF : base >= 256 ? 1 : 0);
                ram[READ_ADDR] = (uint8_t)data, ram[READ_ADDR + 1] = (uint8_t)(data >> 8);
                ram[FLIP_MASK] = flipped ? 0x40 : 0, ram[MOVING_VISIBILITY] = 1, ram[PPU_OFFSET] = 0x40;
                ram[SPRITE_SLOT] = 3, ram[START_SLOT_RANGE] = 0x10, ram[SLOTS_FULL] = 0, ram[TEMP_ADDR_L] = 4;
                Drawn d;
                memset(&d, 0, sizeof d);
                play_draw(&d, s, flipped);
                int shift = flipped ? (int8_t)(uint8_t)(4 * 2 - (cols + 1) * 8) : 0;
                unsigned slot = 3, n = 0, owned = 0;
                for (unsigned row = 0, i = 0; row <= rows; row++)
                    for (unsigned k = 0; k <= cols; k++, i++) {
                        if (i == 1) continue;
                        unsigned tx = flipped ? cols - k : k;
                        int x = base + shift + (int)tx * 8;
                        uint8_t attr = (uint8_t)((i & 3) ^ (flipped ? 0x40 : 0));
                        if ((flipped ? (tx & 1 ? 1 : 2) : (tx & 1 ? 2 : 1)) & 1) attr |= 0x20;
                        if (x >= 0 && x < 256) {
                            CHECK(d.owner[((slot << 2 ^ 0x10) >> 2) & 63] == s + 1);
                            slot ^= 0x20;
                            if (!(slot & 0x20)) ++slot;
                            ++owned;
                        } else {
                            CHECK(n < d.count && d.cell[n].x == x && d.cell[n].y == 120 + row * 8);
                            CHECK(d.cell[n].tile == (uint8_t)(0x10 + i + 0x40) && d.cell[n].attr == attr &&
                                  d.cell[n].slot == s);
                            ++n;
                        }
                    }
                unsigned marked = 0;
                for (unsigned k = 0; k < 64; k++) marked += d.owner[k] != 0;
                CHECK(n == d.count && marked == owned);
            }
}

static void drawn_a_screen_over(void) {
    /* The live loop draws a slot through room 4's left side and wrapped (X
     * 250: x -6): its cells move a screen over, so the first lies at -6,
     * past the side, and the second at 2, on the screen. */
    world();
    loop_hook(SPRITES_UPDATE_ALL);
    put(LIVE, SLOT, ZOZURA, 250, 120, false, true);
    blk(LIVE)->through[SLOT] = THROUGH_LEFT;
    ram[CURRENT_SPRITE_INDEX] = SLOT;
    bank14[DATA - 0x8000] = 0x20, bank14[DATA - 0x8000 + 1] = 0, bank14[DATA - 0x8000 + 2] = 0x21,
                  bank14[DATA - 0x8000 + 3] = 0;
    ram[COLS] = 1, ram[ROWS] = 0, ram[NORM_X] = 250, ram[OFFSET_X] = 0, ram[NORM_Y] = 152, ram[OFFSET_Y] = 0;
    ram[READ_ADDR] = (uint8_t)DATA, ram[READ_ADDR + 1] = DATA >> 8, ram[SPRITE_SLOT] = 0, ram[START_SLOT_RANGE] = 0;
    CHECK(draw_hook(DRAW_STANDARD) == 0 && ram[OFFSET_X] == 0xFF);
    CHECK(drawn[0].count == 1 && drawn[0].cell[0].x == -6 && drawn[0].owner[0] == SLOT + 1);
    loop_done_hook(SPRITES_LOOP_DONE);
    fax_seams_captured(blocks[LIVE], FAX_SEAMS_LIVE);
    const FaxSeamsCell *cells;
    CHECK(fax_seams_cells(blocks[LIVE], &cells) == 1 && cells[0].x == -6 &&
          fax_seams_oam_slot(blocks[LIVE], 0) == SLOT);
    /* Outside a sprite loop (the hero's own draws), nothing. */
    ram[OFFSET_X] = 0;
    CHECK(draw_hook(DRAW_STANDARD) == 0 && ram[OFFSET_X] == 0);
}

static void moves_between_frames(void) {
    /* Room 5's slot 7, wrapped past its right side (X 2, through it): to
     * room 4, the live game's, every byte, into its highest free slot,
     * taking the tiles of the Zozura there; its old slot emptied. */
    world();
    put(LIVE, 6, ZOZURA, 100, 160, false, true);
    ram[PPU_OFFSETS + 6] = 0x5C;
    blk(LIVE)->tiles[6] = 0;
    uint8_t *r5 = records[LEFT_ROOM];
    for (unsigned a = SLOT_ARRAYS; a < SLOT_ARRAYS + 23 * 8; a += 8) r5[a + SLOT] = (uint8_t)(a >> 2);
    put(LEFT_ROOM, SLOT, ZOZURA, 2, 160, true, true);
    r5[PPU_OFFSETS + SLOT] = 0x70;
    blk(LEFT_ROOM)->through[SLOT] = THROUGH_RIGHT;
    fax_seams_step();
    CHECK(r5[ENTITIES + SLOT] == EMPTY && !blk(LEFT_ROOM)->through[SLOT] && !blk(LEFT_ROOM)->awake);
    CHECK(ram[ENTITIES + 7] == ZOZURA && ram[SLOT_X + 7] == 2 && ram[SLOT_Y + 7] == 160);
    for (unsigned a = SLOT_ARRAYS; a < SLOT_ARRAYS + 23 * 8; a += 8)
        if (a != ENTITIES && a != FLAGS && a != HITBOX && a != PPU_OFFSETS) CHECK(ram[a + 7] == (uint8_t)(a >> 2));
    CHECK(ram[PPU_OFFSETS + 7] == 0x5C && blk(LIVE)->tiles[7] == 0 && (blk(LIVE)->awake >> 7 & 1));
    CHECK(moves == 1 && move_log[0][0] == LEFT_ROOM && move_log[0][2] == LIVE && move_log[0][3] == 7);
    /* Room 4's, wrapped past its left side (X 250, through it): to room 5,
     * which has no Zozura now: its tiles stay room 4's, and it straddles room
     * 5's right side, so it is marked through it. */
    put(LIVE, 3, ZOZURA, 250, 160, false, true);
    ram[PPU_OFFSETS + 3] = 0x5C;
    blk(LIVE)->through[3] = THROUGH_LEFT;
    fax_seams_step();
    CHECK(ram[ENTITIES + 3] == EMPTY && r5[ENTITIES + 7] == ZOZURA && r5[SLOT_X + 7] == 250);
    CHECK(r5[PPU_OFFSETS + 7] == 0x5C && blk(LEFT_ROOM)->tiles[7] == LIVE + 1);
    CHECK(blk(LEFT_ROOM)->through[7] == THROUGH_RIGHT);
    CHECK(fax_seams_slot_tiles(blocks[LEFT_ROOM], 7) == -1); /* the picture's: not drawn there yet */
    /* Back home it takes its own room's tiles again, through room 5's
     * right side: room 4's tiles, no mark needed. */
    r5[SLOT_X + 7] = 1;
    fax_seams_step();
    CHECK(r5[ENTITIES + 7] == EMPTY && ram[ENTITIES + 5] == ZOZURA && ram[SLOT_X + 5] == 1); /* its highest free slot */
    CHECK(ram[PPU_OFFSETS + 5] == 0x5C && blk(LIVE)->tiles[5] == 0 && !blk(LIVE)->through[5]);
    /* Nowhere to go (every slot of room 4 taken): back inside, turned round. */
    for (unsigned k = 0; k < 8; k++) ram[ENTITIES + k] = ZOZURA;
    put(LEFT_ROOM, 2, ZOZURA, 3, 160, true, true);
    blk(LEFT_ROOM)->through[2] = THROUGH_RIGHT;
    fax_seams_step();
    CHECK(r5[ENTITIES + 2] == ZOZURA && r5[SLOT_X + 2] == 240 && !(r5[FLAGS + 2] & 1) && pushed_back == 1);
    /* Turned back before crossing: no longer through. */
    put(LEFT_ROOM, 1, ZOZURA, 238, 160, false, true);
    blk(LEFT_ROOM)->through[1] = THROUGH_RIGHT;
    fax_seams_step();
    CHECK(!blk(LEFT_ROOM)->through[1] && r5[ENTITIES + 1] == ZOZURA);
}

static void shots_out_of_the_view_go(void) {
    world();
    /* The view from -84: room 5's x 172 and up lies within it. A fireball at
     * its x 100 is gone; one at 230 stays; an empty slot's wake goes. */
    put(LEFT_ROOM, 1, FIREBALL, 100, 150, false, true);
    put(LEFT_ROOM, 2, FIREBALL, 230, 150, false, true);
    blk(LEFT_ROOM)->awake |= 1;
    fax_seams_step();
    CHECK(records[LEFT_ROOM][ENTITIES + 1] == EMPTY && records[LEFT_ROOM][ENTITIES + 2] == FIREBALL && removed == 1);
    CHECK(blk(LEFT_ROOM)->awake == 0x04);
    /* A Zozura out of the view stays, dormant or not. */
    put(LEFT_ROOM, 1, ZOZURA, 20, 150, false, true);
    fax_seams_step();
    CHECK(records[LEFT_ROOM][ENTITIES + 1] == ZOZURA && removed == 1);
}

static void blocks_and_the_record(void) {
    world();
    Block *b = blk(LIVE);
    b->awake = 3, b->count = 2, b->owner[0] = 1;
    fax_seams_captured(blocks[LIVE], FAX_SEAMS_FRESH);
    CHECK(!b->awake && !b->count && !b->owner[0]);
    drawn[1].count = 1, drawn[1].owner[5] = 3, drawn[1].tiles[2] = LEFT_ROOM + 1;
    fax_seams_captured(blocks[RIGHT_ROOM], FAX_SEAMS_TICK);
    CHECK(fax_seams_oam_slot(blocks[RIGHT_ROOM], 5) == 2 && fax_seams_slot_tiles(blocks[RIGHT_ROOM], 2) == LEFT_ROOM);
    CHECK(fax_seams_valid(blocks[RIGHT_ROOM], ROOMS));
    blk(RIGHT_ROOM)->through[0] = 3;
    CHECK(!fax_seams_valid(blocks[RIGHT_ROOM], ROOMS));
    blk(RIGHT_ROOM)->through[0] = 0, blk(RIGHT_ROOM)->owner[9] = 9; /* the spell's (slice 3) */
    CHECK(fax_seams_valid(blocks[RIGHT_ROOM], ROOMS));
    blk(RIGHT_ROOM)->owner[9] = 10;
    CHECK(!fax_seams_valid(blocks[RIGHT_ROOM], ROOMS));
    char out[300];
    blk(RIGHT_ROOM)->owner[9] = 0, blk(RIGHT_ROOM)->awake = 0x81, blk(RIGHT_ROOM)->tiles[1] = LEFT_ROOM + 1;
    fax_seams_describe(blocks[RIGHT_ROOM], out, sizeof out);
    CHECK(strstr(out, "\"awake\":129") && strstr(out, "\"tiles\":[-1,5,"));
    /* A load in play is the live game's: Seamless is told. */
    load_hook(LOAD_SPRITE_INFO);
    isolated = true;
    load_hook(LOAD_SPRITE_INFO);
    CHECK(loads == 1);
}

/* ==== towns stay safe ==== */

enum { NURSE = 0x44, WOMAN = 0x45, GUARD = 0x42, MONODRON = 0x2A, TRIGGER = 0x4F, DWARF = 0x3B, SPRING = 0x62 };

/* Bank 11's AREA_SPRITE_ADDRESSES for Eolis, the towns and the Trunk, each
 * screen's list (entity, block) to a $FF, as the ROM has them for Eolis and
 * the screens named below; the categories the ROM gives their entities. */
static void lists(void) {
    memset(bank11, 0xFF, sizeof bank11);
    static const uint8_t eolis[9][5] = { { TRIGGER, 0x04, EMPTY },
                                         { WALKING_MAN, 0x9C, EMPTY },
                                         { WOMAN, 0x97, EMPTY },
                                         { MAN, 0x84, EMPTY },
                                         { ZOZURA, 0xA9, ZOZURA, 0xA4, EMPTY },
                                         { ZOZURA, 0xAB, ZOZURA, 0xA5, EMPTY },
                                         { NURSE, 0x93, EMPTY },
                                         { GUARD, 0x9A, EMPTY },
                                         { MONODRON, 0x9C, EMPTY } };
    static const uint8_t towns[14][3] = { { WOMAN, 0, EMPTY },
                                          { WALKING_MAN, 0, EMPTY },
                                          { NURSE, 0, EMPTY },
                                          { WALKING_MAN, 0, EMPTY },
                                          { WALKING_MAN, 0, EMPTY },
                                          { EMPTY },
                                          { WOMAN, 0, EMPTY },
                                          { EMPTY },
                                          { GUARD, 0, EMPTY },
                                          { WALKING_MAN, 0, EMPTY },
                                          { 0x41, 0, EMPTY },
                                          { EMPTY },
                                          { EMPTY },
                                          { EMPTY } };
    unsigned at = 0x9000;
    for (unsigned a = 0; a < 8; a++) { /* a list of 0x40 screens an area, each an empty list */
        unsigned base = 0x8400 + a * 0x80;
        bank11[0x0210 + 2 * a] = (uint8_t)base, bank11[0x0210 + 2 * a + 1] = (uint8_t)(base >> 8);
        for (unsigned sc = 0; sc < 0x40; sc++, at += 12) {
            bank11[base - 0x8000 + 2 * sc] = (uint8_t)at, bank11[base - 0x8000 + 2 * sc + 1] = (uint8_t)(at >> 8);
            const uint8_t *l = a == FAX_AREA_EOLIS && sc < 9    ? eolis[sc]
                               : a == FAX_AREA_TOWNS && sc < 14 ? towns[sc]
                                                                : NULL;
            static const uint8_t spring[] = { DWARF, 0x9C, SPRING, 0x5C, EMPTY };
            if (a == FAX_AREA_TRUNK && sc == 0x25) l = spring;
            for (unsigned k = 0; l && k < 5; k++) {
                bank11[at - 0x8000 + k] = l[k];
                if (l[k] == EMPTY && !(k & 1)) break;
            }
        }
    }
    bank14[CATEGORIES - 0x8000 + NURSE] = bank14[CATEGORIES - 0x8000 + WOMAN] = 2;
    bank14[CATEGORIES - 0x8000 + GUARD] = bank14[CATEGORIES - 0x8000 + 0x41] = bank14[CATEGORIES - 0x8000 + DWARF] = 2;
    bank14[CATEGORIES - 0x8000 + MONODRON] = 0;
    bank14[CATEGORIES - 0x8000 + TRIGGER] = 4;
    bank14[CATEGORIES - 0x8000 + SPRING] = 3;
}

static void what_a_town_room_is(void) {
    world();
    lists();
    /* Eolis: its town's screens and the King's, not the Zozuras' path, the
     * Monodron's by the way out, or the gate's (no zone). */
    area_now = FAX_AREA_EOLIS;
    static const bool eolis[9] = { false, true, true, true, false, false, true, true, false };
    for (int sc = 0; sc < 9; sc++) CHECK(fax_seams_town(sc) == eolis[sc]);
    /* Every screen of the towns, those with no one listed too. */
    area_now = FAX_AREA_TOWNS;
    for (int sc = 0; sc < 14; sc++) CHECK(fax_seams_town(sc));
    /* A region's screen with a person and no monster (the Trunk's spring
     * $25) is no town; nor any room with no area, or no room. */
    area_now = FAX_AREA_TRUNK;
    CHECK(!fax_seams_town(0x25) && !fax_seams_town(3));
    area_now = -1;
    CHECK(!fax_seams_town(3));
    area_now = FAX_AREA_EOLIS;
    CHECK(!fax_seams_town(-1));
}

static void towns_stay_safe(void) {
    /* Eolis $05, $04 and $03 (rooms 5, 4 and 3), room 4 the live game's: $03
     * is the town's left end, so the seam between 4 and 3 is its edge. */
    world();
    lists();
    area_now = FAX_AREA_EOLIS;
    /* A Zozura walking right into the town, at every place past room 4's
     * side: the original's wall (the hook leaves it), never let through,
     * counted. Walking left into room 5, the Zozuras' own, it goes. */
    for (int x = 241; x <= 255; x++)
        for (int y = 96; y <= 176; y += 20) {
            put(LIVE, SLOT, ZOZURA, (uint8_t)x, (uint8_t)y, true, true);
            unsigned before = town_edge;
            CHECK(test_hook(right_hook, SLOT, false) == 0 && ram[BLOCKS_RESULT] == 0xEE);
            CHECK(!blk(LIVE)->through[SLOT] && town_edge == before + 1 && kept_out == 0);
            put(LIVE, SLOT, ZOZURA, (uint8_t)x, (uint8_t)y, false, true); /* wrapped past x 0 */
            CHECK(test_hook(left_hook, SLOT, false) == 1 && blk(LIVE)->through[SLOT] == THROUGH_LEFT);
            blk(LIVE)->through[SLOT] = 0;
        }
    /* Its shot too, flying right. */
    put(LIVE, SLOT, FIREBALL, 0xF4, 150, true, true);
    unsigned before = town_edge;
    CHECK(test_hook(flier_hook, SLOT, true) == 0 && town_edge == before + 1 && !blk(LIVE)->through[SLOT]);
    /* From a neighbour's tick toward the live game: the hero in room 3, the
     * Zozura in room 4's copy, the same. */
    memcpy(records[LIVE], ram, sizeof ram);
    active_room = RIGHT_ROOM;
    isolated = ticking = true, tick_room = LIVE; /* room 4's tick: the machine holds its RAM */
    put(LIVE, SLOT, ZOZURA, 245, 160, true, true);
    before = town_edge;
    CHECK(test_hook(right_hook, SLOT, false) == 0 && town_edge == before + 1 && !blk(LIVE)->through[SLOT]);
    memcpy(records[LIVE], ram, sizeof ram);
    memcpy(ram, records[RIGHT_ROOM], sizeof ram);
    isolated = ticking = false, tick_room = -1;
    /* The people stay in: the walking man in room 3 (now the live game's)
     * walking left past its side, toward the path, finds a wall; walking
     * right into room 2, the town's next screen, he goes. */
    live_room[2] = true, edge[RIGHT_ROOM][1] = 2, edge[2][0] = RIGHT_ROOM, edge[2][1] = -1;
    memset(records[2] + ENTITIES, EMPTY, 8);
    put(RIGHT_ROOM, 1, WALKING_MAN, 0xF8, 160, false, true);
    cpu.y = 1;
    before = town_edge;
    CHECK(left_hook(MOVE_LEFT) == 0 && town_edge == before + 1 && !blk(RIGHT_ROOM)->through[1]);
    put(RIGHT_ROOM, 1, WALKING_MAN, 245, 160, true, true);
    CHECK(test_hook(right_hook, 1, false) == 1 && blk(RIGHT_ROOM)->through[1] == THROUGH_RIGHT);
    CHECK(town_edge == before + 1);
    /* In the towns' area every screen is a town's: people go both ways. */
    area_now = FAX_AREA_TOWNS;
    blk(RIGHT_ROOM)->through[1] = 0;
    put(RIGHT_ROOM, 1, WALKING_MAN, 0xF8, 160, false, true);
    CHECK(test_hook(left_hook, 1, false) == 1 && blk(RIGHT_ROOM)->through[1] == THROUGH_LEFT);
    /* A full room is still full, counted as such. */
    area_now = FAX_AREA_EOLIS;
    for (unsigned k = 0; k < 5; k++) records[2][ENTITIES + k] = WOMAN;
    blk(RIGHT_ROOM)->through[1] = 0;
    put(RIGHT_ROOM, 1, WALKING_MAN, 245, 160, true, true);
    before = town_edge;
    unsigned full_before = kept_out;
    CHECK(test_hook(right_hook, 1, false) == 0 && kept_out == full_before + 1 && town_edge == before);
}

/* ==== slice 2 ==== */

enum { SUGATA_E = 0x18, DEATH_E = 0x13, BREAD = 0x01, ITEM = 0x5A };

/* A slot as Sprites_PopulateNextAvailableSprite leaves it: no hit counter,
 * no magic hit, hitbox 0 (16 by 16). */
static void fill(int room, unsigned s, uint8_t entity, uint8_t x, uint8_t y) {
    uint8_t *m = mem(room);
    for (unsigned k = 0; k < SLOT_BYTES; k++)
        m[byte_at(k, s)] = (uint8_t)(0x40 + s * 3 + k); /* every byte told apart */
    m[ENTITIES + s] = entity, m[SLOT_X + s] = x, m[SLOT_Y + s] = y, m[HITBOX + s] = 0;
    m[HIT_COUNTER + s] = 0, m[HIT_BY_MAGIC + s] = 0xFF, m[FLAGS + s] = 0;
}
/* The live game at the site after the sprite loop, the hero at room 4's
 * (hx, 160). */
static void fight(uint8_t hx) {
    world();
    bank14[CATEGORIES - 0x8000 + SUGATA_E] = 0;
    bank14[CATEGORIES - 0x8000 + DEATH_E] = 3;
    bank14[CATEGORIES - 0x8000 + BREAD] = 1;
    ram[HERO_X] = hx, ram[HERO_Y] = 144;
    ram[CURRENT_ROM_BANK] = 14, ram[SPRITES_PAUSED] = 0, ram[PLAYER_IS_DEAD] = 0, ram[CAST_TYPE] = 0xFF;
    hero_wx = hx, hero_wy = 144;
    view_x = hx + 8 - VIEW_W / 2;
    committed = 0;
}
static uint16_t stack_word(uint8_t at) {
    return (uint16_t)(ram[0x100 | (uint8_t)(at + 1)] | ram[0x100 | (uint8_t)(at + 2)] << 8);
}

/* The game's 8-bit overlap test, with the hook at its entry: CurrentSprite_CheckHitPlayer's X test. */
static bool touches(uint8_t hero, uint8_t left, uint8_t width) {
    ram[HERO_X] = hero, ram[BOX_LEFT] = left, ram[BOX_WIDTH] = width;
    if (touch_hook(CHECK_HIT_PLAYER)) return false;
    return (uint8_t)(hero + TOUCH_PAD - left) < (uint8_t)(width + TOUCH_PAD);
}

static void touches_told_apart(void) {
    /* Every place of the hero in his room against every box 16 wide from a
     * room left of his to one right of it: the live slot's X in 16 bits from
     * its mark, its box at X (Sprite_GetBounds' offset 0), the truth that
     * the 11-pixel test's two spans meet. Hooked, the game decides it; off,
     * it also touches a room away. */
    fight(120);
    unsigned ghosts = 0;
    for (int hero = 0; hero < 256; hero += 3)
        for (int x = -40; x < 296; x++) {
            Block *b = blk(LIVE);
            b->through[SLOT] = x < 0 ? THROUGH_LEFT : x >= 256 ? THROUGH_RIGHT : 0;
            fill(LIVE, SLOT, ZOZURA, (uint8_t)x, 150);
            ram[CURRENT_SPRITE_INDEX] = SLOT;
            int d = hero + TOUCH_PAD - x;
            bool truth = d >= 0 && d < 15 + TOUCH_PAD;
            unsigned before = touch_refused;
            bool game = touches((uint8_t)hero, (uint8_t)x, 15);
            CHECK(game == truth);
            ghosts += touch_refused > before;
        }
    CHECK(ghosts > 0);
    /* A box of $80 and up is the game's trick: it decides. */
    fill(LIVE, SLOT, ZOZURA, 250, 150);
    blk(LIVE)->through[SLOT] = 0;
    unsigned before = touch_refused;
    CHECK(touches(4, 250, 0xF0) == ((uint8_t)(4 + 11 - 250) < (uint8_t)(0xF0 + 11)) && touch_refused == before);
    /* An empty slot: the original's own (it returns). */
    ram[ENTITIES + SLOT] = EMPTY;
    CHECK(touch_hook(CHECK_HIT_PLAYER) == 0);
    /* Sprite_GetBounds' offset is signed: a person's box 8 left of its X. */
    fill(LIVE, SLOT, WALKING_MAN, 4, 150);
    ram[BOX_LEFT] = (uint8_t)(4 - 8);
    CHECK(fax_seams_box_left() == -4);
}

static void shields_and_spells_told_apart(void) {
    /* The shield: he at x 250 facing right, his shield at 258 (its byte 2).
     * A shot of room 3 visiting at its x 4 (the live 260) meets it; one of
     * his own room at x 4, 246 pixels behind him, does not. */
    fight(250);
    ram[SHIELD_X] = 2;
    fill(LIVE, SLOT, FIREBALL, 4, 150);
    ram[CURRENT_SPRITE_INDEX] = SLOT, ram[BOX_LEFT] = 4, ram[BOX_WIDTH] = 7;
    CHECK(shield_hook(CHECK_SHIELD) == 1 && shield_refused == 1 && cpu.x == SLOT);
    blk(LIVE)->through[SLOT] = THROUGH_RIGHT; /* straddling into room 3: the live 260 */
    CHECK(shield_hook(CHECK_SHIELD) == 0 && shield_refused == 1);
    /* Not a shot: the original's (it returns before its test). */
    ram[ENTITIES + SLOT] = ZOZURA;
    CHECK(shield_hook(CHECK_SHIELD) == 0);
    /* In a neighbour's tick the shield check never runs: its shield is the
     * live room's. Another isolated call: the original's. */
    isolated = true;
    CHECK(shield_hook(CHECK_SHIELD) == 0);
    ticking = true;
    CHECK(shield_hook(CHECK_SHIELD) == 1 && shield_skipped == 1);
    isolated = ticking = false;
    /* The spell: Deluge at x 3, 11 wide; a Zozura of his room at x 250 is
     * 236 pixels away (refused, as the X test's failure leaves it: X the
     * slot, A the difference, C set); one through his left side at -6 is
     * under it (the original's). */
    fight(40);
    bank14[CAST_WIDTHS - 0x8000] = 11;
    ram[CAST_TYPE] = 0, ram[CAST_X] = 3;
    fill(LIVE, SLOT, ZOZURA, 250, 150);
    ram[CURRENT_SPRITE_INDEX] = SLOT;
    CHECK(magic_hook(CHECK_MAGIC) == 1 && magic_refused == 1 && cpu.x == SLOT && (cpu.p & FLAG_C));
    CHECK(cpu.a == (uint8_t)(3 + 11 - 250));
    blk(LIVE)->through[SLOT] = THROUGH_LEFT;
    CHECK(magic_hook(CHECK_MAGIC) == 0 && magic_refused == 1);
    /* No spell, a coin, or a copy: the original's. */
    ram[CAST_TYPE] = 0xFF;
    CHECK(magic_hook(CHECK_MAGIC) == 0);
    ram[CAST_TYPE] = 0, ram[ENTITIES + SLOT] = COIN;
    CHECK(magic_hook(CHECK_MAGIC) == 0);
    /* The weapon off the screen: Player_DrawWeapon's BNE not taken, in the
     * live game only. */
    cpu.p = 0x24;
    CHECK(weapon_hook(WEAPON_OFF_SCREEN) == 0 && (cpu.p & FLAG_Z) && weapon_placed == 1);
    CHECK(weapon_hook(WEAPON_OFF_SCREEN) == 0 && weapon_placed == 1); /* on it: as it was */
    cpu.p = 0x24, isolated = true;
    CHECK(weapon_hook(WEAPON_OFF_SCREEN) == 0 && !(cpu.p & FLAG_Z));
    isolated = false;
}

/* The site after the sprite loop: its first arrival, as the scheduler takes a
 * handled site (the RTS the hook prepared). */
static int arrive(void) { return visit_hook(MAIN_LOOP_COUNTDOWN); }

static void the_visitors_pass(void) {
    /* Room 4 live, its Zozura in slot 7; room 5 left of it with a coin at
     * its x 250 (straddling), a Zozura at 230 and one at 40, far off; room 3
     * a fireball at its x 100. The hero at room 4's x 4. */
    fight(4);
    fill(LIVE, 7, ZOZURA, 120, 160);
    blk(LIVE)->awake = 0x80;
    fill(LEFT_ROOM, 2, ZOZURA, 230, 160);
    fill(LEFT_ROOM, 3, COIN, 250, 168);
    fill(LEFT_ROOM, 4, ZOZURA, 40, 160);
    fill(RIGHT_ROOM, 1, FIREBALL, 100, 150);
    uint8_t live_before[0x800], room5_before[0x800];
    memcpy(live_before, ram, sizeof ram);
    memcpy(room5_before, records[LEFT_ROOM], sizeof room5_before);
    cpu.s = 0xFF;
    CHECK(arrive() == 1 && pass.open && pass.count == 2 && passes == 1 && visits == 2);
    /* The stack: the loop's entry past its INC (the site's RTS takes it), and
     * this site again for the loop's own RTS. */
    CHECK(cpu.s == 0xFB && stack_word(0xFB) == SPRITES_LOOP - 1 && stack_word(0xFD) == MAIN_LOOP_COUNTDOWN - 1);
    /* The room's own hidden; the nearest first in the highest free slot:
     * the coin (it meets him), then the Zozura (8 pixels off). Every byte
     * of each, its X the same byte and 16 bits from its side. */
    CHECK(ram[ENTITIES + 7] == EMPTY);
    CHECK(pass.v[0].room == LEFT_ROOM && pass.v[0].slot == 3 && pass.v[0].live == 6 && pass.v[0].shift == -256);
    CHECK(pass.v[1].room == LEFT_ROOM && pass.v[1].slot == 2 && pass.v[1].live == 5);
    for (unsigned k = 0; k < SLOT_BYTES; k++) {
        CHECK(ram[byte_at(k, 6)] == records[LEFT_ROOM][byte_at(k, 3)]);
        CHECK(ram[byte_at(k, 5)] == records[LEFT_ROOM][byte_at(k, 2)]);
    }
    CHECK(fax_seams_x16(6) == 250 - 256 && fax_seams_x16(5) == 230 - 256 && fax_seams_slot_room(6) == LEFT_ROOM);
    ram[CURRENT_SPRITE_INDEX] = 6, ram[BOX_LEFT] = 250 + 4; /* a coin's box, 4 right of its X */
    CHECK(fax_seams_box_left() == -2);
    /* Their behaviour and draw never run in the live game. */
    ram[CURRENT_SPRITE_INDEX] = 5;
    cpu.x = 0x33;
    CHECK(behavior_hook(UPDATE_BEHAVIOR) == 1 && cpu.x == 5 && state_hook(UPDATE_STATE) == 1);
    /* Its weapon reaches across: the Long Sword at -12 (its byte 244), off
     * the screen, meets the Zozura at the live -26; the same at x 230 in his
     * own room, 238 pixels away, is the ghost hit, refused. */
    ram[WEAPON_X] = 244, ram[WEAPON_X_HIGH] = 0xFF, ram[SCROLL_X] = 0, ram[WEAPON_RANGE_X] = 16;
    bank14[SPRITE_EXTENTS - 0x8000 + ZOZURA * 2] = 16;
    ram[SPRITE_X] = 230;
    cpu.x = 5;
    unsigned refused0 = refused;
    CHECK(reach_hook(OUT_OF_WEAPON_RANGE) == 0 && refused == refused0);
    /* The pass: the Zozura killed (the death effect), the coin taken ($FF),
     * and anything a handler might leave in their X. */
    ram[ENTITIES + 5] = DEATH_E, ram[INTERNAL_STATES + 5] = ZOZURA, ram[HP_ + 5] = 0xFE;
    ram[ENTITIES + 6] = EMPTY;
    ram[SLOT_X + 5] = 99, ram[SLOT_Y + 5] = 98;
    CHECK(arrive() == 0 && !pass.open && pass.count == 0);
    /* Home with every byte but the position; the live slots as they were. */
    CHECK(records[LEFT_ROOM][ENTITIES + 2] == DEATH_E && records[LEFT_ROOM][HP_ + 2] == 0xFE);
    CHECK(records[LEFT_ROOM][INTERNAL_STATES + 2] == ZOZURA && records[LEFT_ROOM][ENTITIES + 3] == EMPTY);
    CHECK(records[LEFT_ROOM][SLOT_X + 2] == 230 && records[LEFT_ROOM][SLOT_Y + 2] == 160);
    for (unsigned k = 0; k < SLOT_BYTES; k++)
        if (byte_at(k, 2) != ENTITIES + 2 && byte_at(k, 2) != HP_ + 2 && byte_at(k, 2) != INTERNAL_STATES + 2)
            CHECK(records[LEFT_ROOM][byte_at(k, 2)] == room5_before[byte_at(k, 2)]);
    CHECK(!memcmp(records[LEFT_ROOM] + ENTITIES + 4, room5_before + ENTITIES + 4, 1)); /* the far one untouched */
    for (unsigned s = 0; s < 8; s++)
        for (unsigned k = 0; k < SLOT_BYTES; k++) CHECK(ram[byte_at(k, s)] == live_before[byte_at(k, s)]);
    CHECK(visit_log[0][0] == LEFT_ROOM && visit_log[0][1] == 3 && visit_log[0][3] == COIN && visit_log[0][4] == EMPTY);
    CHECK(visit_log[1][1] == 2 && visit_log[1][3] == ZOZURA && visit_log[1][4] == DEATH_E);
    /* Nobody in reach: no pass. */
    fight(140);
    fill(LEFT_ROOM, 2, ZOZURA, 230, 160);
    CHECK(arrive() == 0 && !pass.open && passes == 0); /* (world() starts the counts over) */
}

static void who_visits(void) {
    fight(4);
    /* In reach but knocked back (a hit counter), hit by a spell, a person,
     * an item, an effect: the loop lets none interact. A big enemy and bread
     * go. */
    fill(LEFT_ROOM, 0, ZOZURA, 240, 160);
    mem(LEFT_ROOM)[HIT_COUNTER] = 5;
    fill(LEFT_ROOM, 1, ZOZURA, 240, 160);
    mem(LEFT_ROOM)[HIT_BY_MAGIC + 1] = 0;
    fill(LEFT_ROOM, 2, WALKING_MAN, 240, 160);
    fill(LEFT_ROOM, 3, ITEM, 244, 160);
    fill(LEFT_ROOM, 4, DEATH_E, 244, 160);
    fill(LEFT_ROOM, 5, ROKUSUTAHN, 200, 128);
    fill(LEFT_ROOM, 6, BREAD, 248, 168);
    CHECK(arrive() == 1 && pass.count == 2);
    CHECK((pass.v[0].slot == 6 && pass.v[1].slot == 5) || (pass.v[0].slot == 5 && pass.v[1].slot == 6));
    arrive();
    /* Above or below his reach, or the spell's: the spell's box brings one
     * in that he can't reach. */
    fight(120);
    fill(LEFT_ROOM, 0, ZOZURA, 250, 160);
    CHECK(arrive() == 0);
    ram[CAST_TYPE] = 0, ram[CAST_X] = 2, ram[CAST_Y] = 150;
    CHECK(arrive() == 1 && pass.count == 1);
    arrive();
    /* No free slot: they wait a frame, counted. */
    fight(4);
    for (unsigned s = 0; s < 8; s++) fill(LIVE, s, ZOZURA, (uint8_t)(100 + s), 160);
    fill(LEFT_ROOM, 0, ZOZURA, 240, 160);
    CHECK(arrive() == 0 && waited == 1 && !pass.open);
    /* One free: the nearest only. */
    ram[ENTITIES + 3] = EMPTY;
    fill(LEFT_ROOM, 1, ZOZURA, 200, 160);
    fill(RIGHT_ROOM, 0, ZOZURA, 0, 160); /* the live 256: far off */
    CHECK(arrive() == 1 && pass.count == 1 && pass.v[0].slot == 0 && pass.v[0].live == 3 && waited == 2);
    arrive();
    /* From the right: the live X 256 on. */
    fight(240);
    fill(RIGHT_ROOM, 0, ZOZURA, 6, 160);
    CHECK(arrive() == 1 && pass.v[0].room == RIGHT_ROOM && pass.v[0].shift == 256 &&
          fax_seams_x16(pass.v[0].live) == 262);
    CHECK(fax_seams_slot_room(pass.v[0].live) == RIGHT_ROOM);
    arrive();
    /* Paused, dead, another bank, no view, or a copy: no pass. */
    fill(RIGHT_ROOM, 0, ZOZURA, 6, 160);
    ram[SPRITES_PAUSED] = 1;
    CHECK(arrive() == 0);
    ram[SPRITES_PAUSED] = 0, ram[PLAYER_IS_DEAD] = 1;
    CHECK(arrive() == 0);
    ram[PLAYER_IS_DEAD] = 0, ram[CURRENT_ROM_BANK] = 12;
    CHECK(arrive() == 0);
    ram[CURRENT_ROM_BANK] = 14, view_ok = false;
    CHECK(arrive() == 0);
    view_ok = true, isolated = true;
    CHECK(arrive() == 0);
    isolated = false;
    /* No arena: an arena joins nothing (fax_world_joined), so nothing comes
     * from it. */
    edge[LIVE][1] = -1;
    CHECK(arrive() == 0);
}

static void sugatas_flash(void) {
    /* In room 5's tick, at its flash's damage: room 5 joined beside the live
     * room, the flash waits for the live game. */
    fight(4);
    run(LEFT_ROOM);
    fill(LEFT_ROOM, 2, SUGATA_E, 236, 160);
    ram[BEHAVIORS + 2] = BEHAVIOR_FLASH, ram[FLAGS + 2] = BEHAVIOR_READY;
    ram[CURRENT_SPRITE_INDEX] = 2;
    int hx;
    CHECK(fax_seams_hero_x(&hx) && hx == 4 + 256); /* his true place, in room 5's pixels */
    CHECK(fax_seams_flash() && (blk(LEFT_ROOM)->flash & 4) && flashes_due == 1);
    CHECK(fax_seams_flash() && flashes_due == 1);
    done(LEFT_ROOM);
    /* The next pass takes it first, wherever it is, and enters the damage
     * for it: three returns, the damage's on top, and CurrentSpriteIndex and
     * X its live slot. */
    fill(LEFT_ROOM, 5, ZOZURA, 250, 160); /* nearer, but the flash goes first */
    cpu.s = 0xFF;
    CHECK(arrive() == 1 && pass.v[0].flash && pass.v[0].slot == 2 && flash_runs == 1);
    CHECK(cpu.s == 0xF9 && stack_word(0xF9) == FLASH_HIT - 1 && stack_word(0xFB) == SPRITES_LOOP - 1);
    CHECK(ram[CURRENT_SPRITE_INDEX] == pass.v[0].live && cpu.x == pass.v[0].live && !(blk(LEFT_ROOM)->flash & 4));
    arrive();
    /* A flash no longer waiting there (its behaviour finished, or it moved):
     * forgotten. */
    fight(200);
    fill(LEFT_ROOM, 2, SUGATA_E, 236, 160);
    blk(LEFT_ROOM)->flash = 4;
    CHECK(arrive() == 0 && !blk(LEFT_ROOM)->flash);
    /* Not joined beside his (a diagonal neighbour ticking), or the live
     * game, or his row not the room's: no. */
    fight(4);
    run(LEFT_ROOM);
    edge[LIVE][0] = -1;
    ram[CURRENT_SPRITE_INDEX] = 2;
    CHECK(!fax_seams_flash());
    edge[LIVE][0] = LEFT_ROOM;
    place_y[LEFT_ROOM] = 1; /* below his row */
    CHECK(!fax_seams_hero_x(&hx));
    place_y[LEFT_ROOM] = 0;
    done(LEFT_ROOM);
    CHECK(!fax_seams_flash());
    /* In the live game his place is his. */
    CHECK(fax_seams_hero_x(&hx) && hx == 4);
}

static void a_neighbours_sounds(void) {
    /* In room 5's tick, the coin's sound from a slot in the view is kept; one
     * out of it, or from the live game, is not. Between frames they wait,
     * and the next pass's site plays them with the game's Sound_PlayEffect. */
    fight(4);
    run(LEFT_ROOM);
    fill(LEFT_ROOM, 1, COIN, 100, 160);  /* the world's -156: in the view, out of reach */
    fill(LEFT_ROOM, 6, ZOZURA, 10, 160); /* room 5's x 10: the world's -246, out */
    ram[CURRENT_SPRITE_INDEX] = 1, cpu.a = 0x10;
    CHECK(sound_hook(SOUND_PLAY_EFFECT) == 0 && heard_count == 1 && heard[0].effect == 0x10 &&
          heard[0].room == LEFT_ROOM);
    ram[CURRENT_SPRITE_INDEX] = 6;
    CHECK(sound_hook(SOUND_PLAY_EFFECT) == 0 && heard_count == 1 && heard_total == 2);
    done(LEFT_ROOM);
    cpu.a = 0x09;
    CHECK(sound_hook(SOUND_PLAY_EFFECT) == 0 && heard_count == 1);
    fax_seams_step();
    CHECK(pending_count == 1 && heard_count == 0);
    cpu.a = 0x77;
    CHECK(arrive() == 0 && committed == 1 && played_effects[0] == 0x10 && cpu.a == 0x77 && pending_count == 0);
    CHECK(played == 1 && sound_log[0][0] == LEFT_ROOM && sound_log[0][2] == 0x10);
    /* A frame whose pass never came: dropped at the next step. */
    run(LEFT_ROOM);
    ram[CURRENT_SPRITE_INDEX] = 1, cpu.a = 0x10;
    sound_hook(SOUND_PLAY_EFFECT);
    done(LEFT_ROOM);
    fax_seams_step();
    fax_seams_step();
    CHECK(pending_count == 0);
}

static void the_record_mid_pass(void) {
    /* A frame can end inside the pass: the record holds it, and a load ends
     * it at its site as the running game would. */
    fight(4);
    fill(LIVE, 7, ZOZURA, 120, 160);
    fill(LEFT_ROOM, 2, ZOZURA, 236, 160);
    uint8_t live_before[0x800];
    memcpy(live_before, ram, sizeof ram);
    CHECK(arrive() == 1 && fax_seams_visiting());
    uint8_t rec[RECORD + 4];
    int n = record_get(rec, sizeof rec);
    CHECK(n == RECORD && record_validate(rec, n) && record_get(rec, 8) == -1);
    clear_pass();
    CHECK(!fax_seams_visiting() && record_set(rec, n) && fax_seams_visiting() && pass.count == 1);
    ram[HP_ + pass.v[0].live] = 3;
    CHECK(arrive() == 0 && records[LEFT_ROOM][HP_ + 2] == 3);
    for (unsigned s = 0; s < 8; s++)
        for (unsigned k = 0; k < SLOT_BYTES; k++) CHECK(ram[byte_at(k, s)] == live_before[byte_at(k, s)]);
    /* Bad records are refused; none clears it. */
    rec[2] = 9;
    CHECK(!record_validate(rec, n));
    rec[2] = 1, rec[5] = ROOMS;
    CHECK(!record_validate(rec, n) && !record_validate(rec, n - 1));
    rec[5] = LEFT_ROOM, rec[0] = 7;
    CHECK(!record_validate(rec, n));
    rec[0] = RECORD_VERSION, rec[1] = 0, rec[4] = 1; /* out of the pass, but swapped */
    CHECK(!record_validate(rec, n));
    CHECK(record_validate(NULL, 0) && record_set(NULL, 0) && !fax_seams_visiting());
    /* A frame that ends inside the pass: between frames the machine shows
     * the live slots as the game has them, the pass's kept (and saved with
     * the record); the next frame begins with the pass's back, and the pass
     * ends as if nothing had come between. */
    fight(4);
    fill(LIVE, 7, ZOZURA, 120, 160);
    fill(LEFT_ROOM, 2, ZOZURA, 236, 160);
    memcpy(live_before, ram, sizeof ram);
    CHECK(arrive() == 1);
    unsigned live = pass.v[0].live;
    ram[HP_ + live] = 4; /* hit in the pass */
    uint8_t during[0x800];
    memcpy(during, ram, sizeof ram);
    fax_seams_frame_end();
    for (unsigned s = 0; s < 8; s++)
        for (unsigned k = 0; k < SLOT_BYTES; k++) CHECK(ram[byte_at(k, s)] == live_before[byte_at(k, s)]);
    CHECK(ram[ENTITIES + 7] == ZOZURA && pass.swapped && fax_seams_visiting());
    fax_seams_frame_end(); /* twice: still the same */
    CHECK(ram[ENTITIES + 7] == ZOZURA);
    n = record_get(rec, sizeof rec);
    clear_pass();
    CHECK(record_set(rec, n) && pass.swapped); /* a state saved then, loaded */
    fax_seams_frame_begin();
    CHECK(!memcmp(ram, during, sizeof ram) && !pass.swapped);
    fax_seams_frame_begin();
    CHECK(!memcmp(ram, during, sizeof ram));
    CHECK(arrive() == 0 && records[LEFT_ROOM][HP_ + 2] == 4 && !fax_seams_visiting());
    for (unsigned s = 0; s < 8; s++)
        for (unsigned k = 0; k < SLOT_BYTES; k++) CHECK(ram[byte_at(k, s)] == live_before[byte_at(k, s)]);
    fax_seams_frame_end();
    fax_seams_frame_begin(); /* no pass: nothing */
    CHECK(!pass.swapped);
    /* The live loop's start finds no pass open; if it ever did, it ends it. */
    arrive();
    CHECK(fax_seams_visiting());
    loop_hook(SPRITES_UPDATE_ALL);
    CHECK(!fax_seams_visiting() && broken == 1);
    /* Off: nothing kept, nothing open. */
    deactivate();
    CHECK(!fax_seams_visiting() && record_get(rec, sizeof rec) == 0);
}

/* ==== slice 3: the spell flies on ==== */

enum { DELUGE = 0, THUNDER = 1, FIRE_SPELL = 2, THUNDER_HIT = 6, FIRE_HIT = 7, SPELL_Y = 152, FOREGROUND = 3 };

/* The spell's tables as bank 14 has them: CAST_MAGIC_MAX_SCREEN_WIDTHS and
 * the code after it (CastMagic_UpdateYPosition: LDA CastMagic_Flags, BMI,
 * LDA ...), which types 5 and up read; CAST_MAGIC_WIDTHS and _HEIGHTS; the
 * hit boxes, CAST_MAGIC_HIT_WIDTHS and _HEIGHTS. */
static void spell_world(uint8_t hx) {
    fight(hx);
    static const uint8_t max[] = { 0xF0, 0xF0, 0xE0, 0xF0, 0xF0, 0xAD, 0xB4, 0x02, 0x30, 0x1E, 0xAD, 0xB7 };
    static const uint8_t widths[] = { 15, 15, 31, 15, 15, 15, 15, 31, 15, 15, 15, 15 };
    static const uint8_t heights[] = { 15, 31, 15, 15, 15, 15, 31, 15, 15, 15, 15, 15 };
    static const uint8_t hit_w[] = { 11, 11, 27, 11, 11 }, hit_h[] = { 11, 27, 11, 11, 11 };
    memcpy(bank14 + (CAST_MAX_WIDTHS - 0x8000), max, sizeof max);
    memcpy(bank14 + (CAST_MOVE_WIDTHS - 0x8000), widths, sizeof widths);
    memcpy(bank14 + (CAST_HEIGHTS - 0x8000), heights, sizeof heights);
    memcpy(bank14 + (CAST_WIDTHS - 0x8000), hit_w, sizeof hit_w);
    memcpy(bank14 + (CAST_HEIGHTS_HIT - 0x8000), hit_h, sizeof hit_h);
    for (int r = RIGHT_ROOM; r <= LEFT_ROOM; r++) mem(r)[BLOCK_PROPERTIES + 1] = 0x42; /* id 3 a foreground (4) */
}
/* Player_CastMagic's entry with no spell on, then the cast: `type` from his
 * X (the ADC's carry clear), moving right or left. */
static void cast(uint8_t type, bool right) {
    ram[CAST_TYPE] = 0xFF;
    CHECK(cast_hook(PLAYER_CAST_MAGIC) == 0 && spell.casting && spell.side == 0);
    ram[CAST_TYPE] = type, ram[CAST_X] = ram[HERO_X], ram[CAST_Y] = SPELL_Y, ram[CAST_FLAGS] = right ? 0x40 : 0;
}
/* The live room's own block test, ScreenBuffer_IsBlockImpassable. */
static uint8_t live_solid(unsigned index) {
    uint8_t id = ram[SCREEN_BUFFER + (index & 0xFF)], bits = ram[BLOCK_PROPERTIES + (id >> 1)];
    return bank15[BLOCK_IMPASSABLE - 0xC000 + (id & 1 ? bits >> 4 : bits & 15)];
}
/* CastMagic_CheckDirection, the ROM's code from $BD8F with the hook site at
 * its entry when `hooked`. */
static void check_direction(bool hooked) {
    if (hooked && direction_hook(CAST_CHECK_DIRECTION)) return;
    uint8_t type = ram[CAST_TYPE];
    if (cpu.x > 1) {
        ram[BLOCKS_RESULT] = 0;
        return;
    } /* up and down */
    unsigned px = ram[CAST_X] + (cpu.x ? rom((uint16_t)(CAST_MOVE_WIDTHS + type)) : 0);
    ram[PIXEL_X] = (uint8_t)px;
    if (cpu.x ? px > 255 : px >= 0xF0) {
        ram[BLOCKS_RESULT] = 1;
        return;
    } /* CastMagic_SetShouldDisperse */
    ram[PIXEL_Y] = ram[CAST_Y]; /* _CheckImpassable */
    unsigned x = (ram[PIXEL_Y] & 0xF0u) | (ram[PIXEL_X] >> 4);
    uint8_t r = live_solid(x);
    if (!r && rom((uint16_t)(CAST_HEIGHTS + type)) >= 0x10) r = live_solid(x = (x + 16) & 0xFF);
    if (!r && (ram[CAST_Y] & 15)) r = live_solid(x = (x + 16) & 0xFF);
    ram[BLOCKS_RESULT] = r;
}
/* CastMagic_UpdateXPosition by `delta` pixels (the fraction 0): the ROM's
 * code from $BCAF, the hook sites at $BD8F and the CMPs when `hooked`. C on
 * return: off the screen. */
static bool update_x(uint8_t delta, bool hooked) {
    bool right = ram[CAST_FLAGS] & 0x40;
    ram[CAST_X] = (uint8_t)(right ? ram[CAST_X] + delta : ram[CAST_X] - delta);
    cpu.x = right;
    check_direction(hooked);
    if (right) cpu.x = ram[CAST_TYPE];
    cpu.a = ram[CAST_X];
    if (hooked && edge_hook(right ? CAST_RIGHT_EDGE : CAST_LEFT_EDGE)) return cpu.p & FLAG_C;
    compare(cpu.a, right ? rom((uint16_t)(CAST_MAX_WIDTHS + ram[CAST_TYPE])) : 0xF0);
    return cpu.p & FLAG_C;
}
/* One frame of flight, as the update handlers have it: 0 on, 1 off the
 * screen (CastMagic_Clear), 2 a wall (Deluge's and Fire's: the hit sound;
 * Thunder's and Death's go through). */
enum { FLYING, OFF, WALL };
static int fly(uint8_t delta, bool walls, bool hooked) {
    if (update_x(delta, hooked)) {
        ram[CAST_TYPE] = 0xFF;
        return OFF;
    }
    if (walls && ram[BLOCKS_RESULT]) {
        ram[CAST_TYPE] = 0xFF;
        return WALL;
    }
    return FLYING;
}
/* The truth, in the joined world: a spell `w` wide (less 1) at x (from room
 * 4's left) after its move. Off once its leading edge passes the view (from
 * view_x, room 4 at world 0) or a side with nothing joined; else a wall when
 * its leading pixel's column, in the room it lies in, is solid on its rows. */
static int truth(int x, int w, int h, bool right, bool walls) {
    bool off = right ? x + w + 1 >= view_x + VIEW_W || (x + w + 1 >= 256 && edge[LIVE][1] < 0)
                     : x < view_x || (x < 0 && edge[LIVE][0] < 0);
    if (off) return OFF;
    if (!walls) return FLYING;
    int px = right ? x + w : x;
    bool hit = truly(LIVE, px, SPELL_Y, false) || (h >= 16 && truly(LIVE, px, SPELL_Y + 16, false)) ||
               ((SPELL_Y & 15) && truly(LIVE, px, SPELL_Y + (h >= 16 ? 32 : 16), false));
    return hit ? WALL : FLYING;
}

static void the_spells_side(void) {
    /* From his X, a byte's moves of a few pixels a frame make its side: cast
     * left from x 2, three a frame, it is past room 4's left at -1 (byte
     * 255); cast right from x 255 with the cast's carry (byte 0), at 256. */
    spell_world(2);
    cast(DELUGE, false);
    ram[CAST_X] = 255;
    CHECK(spell_x16() == -1 && spell.side == -1);
    for (int x = -4; x > -300; x -= 3) {
        ram[CAST_X] = (uint8_t)x;
        CHECK(spell_x16() == x && spell.side == (x < -256 ? -2 : -1));
    }
    ram[HERO_X] = 255;
    cast(DELUGE, true);
    ram[CAST_X] = 0; /* 255 + 0 + C */
    CHECK(spell_x16() == 256 && spell.side == 1);
    ram[CAST_X] = 3;
    CHECK(spell_x16() == 259);
    /* The cast's entry also starts this frame's spell draws, which end at
     * GameLoop_CheckUseCurrentItem's entry (an item draws the hero again)
     * or the sprite loop's start; a copy's call changes nothing. */
    CHECK(spell.casting && cast_done_hook(CHECK_USE_ITEM) == 0 && !spell.casting);
    ram[CAST_TYPE] = 0;
    cast_hook(PLAYER_CAST_MAGIC);
    CHECK(spell.casting && spell.side == 1); /* on: its side kept */
    loop_hook(SPRITES_UPDATE_ALL);
    CHECK(!spell.casting);
    isolated = true;
    CHECK(cast_hook(PLAYER_CAST_MAGIC) == 0 && !spell.casting && cast_done_hook(CHECK_USE_ITEM) == 0);
    isolated = false;
    /* A load (a door, a respawn): a spell left on is in the room loaded. */
    load_hook(LOAD_SPRITE_INFO);
    CHECK(spell.side == 0);
}

static void every_flight_against_the_truth(void) {
    /* Every 7th place of the hero, each way, Deluge, Fire and Thunder, with a
     * wall in one column of room 5, 4 or 3 or none, and room 5 or 3 not
     * joined (or both, an arena): frame by frame the game's flight, hooked,
     * is the truth's, and its X in 16 bits is where it truly is. */
    static const uint8_t types[] = { DELUGE, FIRE_SPELL, THUNDER }, deltas[] = { 3, 4, 2 };
    unsigned flights = 0, walls_hit = 0, abroad = 0;
    for (int joined = 0; joined < 4; joined++)
        for (int hx = 0; hx < 256; hx += 7)
            for (int dir = 0; dir < 2; dir++)
                for (unsigned t = 0; t < 3; t++)
                    for (int wall = -1; wall < 48; wall += 5) {
                        spell_world((uint8_t)hx);
                        if (joined & 1) edge[LIVE][0] = -1;
                        if (joined & 2) edge[LIVE][1] = -1;
                        if (joined == 3) view_x = (0 + 256 - VIEW_W) / 2;       /* an arena: centred */
                        else if (joined == 1) view_x = view_x < 0 ? 0 : view_x; /* clamped to the rooms */
                        else if (joined == 2) view_x = view_x + VIEW_W > 256 ? 256 - VIEW_W : view_x;
                        if (wall >= 0) {
                            int room = wall < 16 ? LEFT_ROOM : wall < 32 ? LIVE : RIGHT_ROOM;
                            block_at(room, wall & 15, 9 + (wall & 1), SOLID);
                        }
                        uint8_t type = types[t];
                        int w = rom((uint16_t)(CAST_MOVE_WIDTHS + type)), h = rom((uint16_t)(CAST_HEIGHTS + type));
                        bool right = dir, walls = type != THUNDER;
                        cast(type, right);
                        int x = hx, outcome = FLYING;
                        for (int f = 0; f < 260 && outcome == FLYING; f++) {
                            x += right ? deltas[t] : -deltas[t];
                            outcome = fly(deltas[t], walls, true);
                            abroad += outcome == FLYING && (x < 0 || x >= 256);
                            int expected = truth(x, w, h, right, walls);
                            if (outcome != expected || spell_x16() != x) {
                                fprintf(stderr,
                                        "joined %d hx %d %s type %u wall %d frame %d x %d: game %d truth %d x16 %d\n",
                                        joined, hx, right ? "right" : "left", type, wall, f, x, outcome, expected,
                                        spell_x16());
                                CHECK(false);
                            }
                        }
                        if (outcome == FLYING)
                            fprintf(stderr, "never ended: joined %d hx %d %s type %u wall %d x %d\n", joined, hx,
                                    right ? "right" : "left", type, wall, x);
                        CHECK(outcome != FLYING);
                        ++flights;
                        walls_hit += outcome == WALL;
                    }
    CHECK(flights > 0 && walls_hit > 0 && abroad > 0);
}

static void the_original_in_an_arena(void) {
    /* Joined on neither side (an arena) and the view wider than the room:
     * from anywhere he can cast (x below 240), each frame hooked is the
     * original's, walls included. */
    for (int hx = 0; hx < 240; hx += 3)
        for (int dir = 0; dir < 2; dir++)
            for (int wall = -1; wall < 16; wall += 3) {
                spell_world((uint8_t)hx);
                edge[LIVE][0] = edge[LIVE][1] = -1;
                view_x = (256 - VIEW_W) / 2;
                if (wall >= 0) block_at(LIVE, wall, 10, SOLID);
                cast(DELUGE, dir);
                uint8_t start[0x800];
                memcpy(start, ram, sizeof ram);
                int game[100], original[100], n = 0;
                for (int f = 0; f < 100 && (!n || game[n - 1] == FLYING); f++, n++) game[n] = fly(3, true, true);
                memcpy(ram, start, sizeof ram);
                for (int f = 0; f < n; f++) {
                    original[f] = fly(3, true, false);
                    CHECK(original[f] == game[f]);
                }
            }
}

static void its_own_rules(void) {
    /* Moving right, Thunder after a first hit ends at x 180 of the room it
     * is in and Fire after one at once (the original's bytes past the
     * table); moving left they run to the edge as the others do. */
    spell_world(40);
    cast(THUNDER_HIT, true);
    ram[CAST_X] = 170;
    CHECK(fly(2, false, true) == FLYING && fly(2, false, true) == FLYING);
    CHECK(fly(2, false, true) == FLYING && fly(2, false, true) == FLYING && ram[CAST_X] == 178);
    CHECK(fly(2, false, true) == OFF && spell_end[2] == 3);
    cast(FIRE_HIT, true);
    CHECK(fly(4, true, true) == OFF);
    cast(FIRE_HIT, false);
    CHECK(fly(4, true, true) == FLYING);
    /* Cast left from x 245 (Seamless lets him stand at 241-255): the
     * original ends it at once (X $F0 and up reads as wrapped); hooked it
     * flies, its X 242 in his room. */
    spell_world(245);
    cast(DELUGE, false);
    uint8_t start[0x800];
    memcpy(start, ram, sizeof ram);
    CHECK(fly(3, true, false) == OFF);
    memcpy(ram, start, sizeof ram);
    CHECK(fly(3, true, true) == FLYING && spell_x16() == 242);
    /* Ended at the view's edge, logged: its X, the edge, why. */
    spell_world(60);
    cast(DELUGE, false);
    while (fly(3, true, true) == FLYING) {}
    CHECK(spell_end[2] == 1 && spell_end[0] < spell_end[1] && spell_end[1] - spell_end[0] <= 3);
    CHECK(spell_end[1] == 60 + 8 - VIEW_W / 2 && spell_view_ends == 1);
    /* No view (a load under way), another isolated call, or Seams off: the
     * original's own code. */
    spell_world(60);
    cast(DELUGE, false);
    view_ok = false;
    cpu.x = 0;
    CHECK(direction_hook(CAST_CHECK_DIRECTION) == 0 && edge_hook(CAST_LEFT_EDGE) == 0 &&
          visibility_hook(CAST_VISIBILITY) == 0);
    view_ok = true, isolated = true;
    CHECK(direction_hook(CAST_CHECK_DIRECTION) == 0 && edge_hook(CAST_LEFT_EDGE) == 0);
    isolated = false;
    cpu.x = 2; /* up and down: the original's 0 */
    CHECK(direction_hook(CAST_CHECK_DIRECTION) == 0);
}

static void spells_told_apart_and_dormant(void) {
    /* Deluge at room 4's x -6 (byte 250, side -1): a room 5 Zozura visiting
     * at its x 246 (the live -10) is under it, the original's hit; his own
     * room's Zozura at x 246, 252 pixels off, is refused. */
    spell_world(40);
    cast(DELUGE, false);
    for (int x = 39; x >= -6; x -= 3) ram[CAST_X] = (uint8_t)x, spell_x16();
    CHECK(spell_x16() == -6);
    fill(LIVE, SLOT, ZOZURA, 246, 150);
    blk(LIVE)->awake = 1u << SLOT;
    ram[CURRENT_SPRITE_INDEX] = SLOT;
    CHECK(magic_hook(CHECK_MAGIC) == 1 && magic_refused == 1 && magic_dormant == 0 && (cpu.p & FLAG_C));
    fill(LEFT_ROOM, 2, ZOZURA, 246, 150);
    blk(LEFT_ROOM)->awake = 1u << 2;
    fill(LIVE, SLOT, ZOZURA, 100, 150); /* his own, far off */
    CHECK(arrive() == 1 && pass.count == 1 && pass.v[0].room == LEFT_ROOM);
    unsigned live = pass.v[0].live;
    ram[CURRENT_SPRITE_INDEX] = (uint8_t)live;
    CHECK(fax_seams_x16(live) == -10 && magic_hook(CHECK_MAGIC) == 0); /* the game's hit */
    /* The same visitor never seen: refused, counted, as the X test's failure. */
    blk(LEFT_ROOM)->awake = 0;
    CHECK(magic_hook(CHECK_MAGIC) == 1 && magic_dormant == 1 && (cpu.p & FLAG_C) && cpu.x == live);
    /* Under it but too high, or hidden: no hit for the game either, nothing counted. */
    uint8_t y = ram[SLOT_Y + live];
    ram[SLOT_Y + live] = 40;
    CHECK(magic_hook(CHECK_MAGIC) == 0 && magic_dormant == 1);
    ram[SLOT_Y + live] = y, ram[FLAGS + live] |= HIDDEN;
    CHECK(magic_hook(CHECK_MAGIC) == 0 && magic_dormant == 1);
    arrive();
    /* His own room's sleeper not yet seen, under the spell (the hole the
     * view's edge closes): refused; awake, the game's. A coin: the game's. */
    spell_world(40);
    cast(DELUGE, false);
    ram[CAST_X] = 20, spell_x16();
    fill(LIVE, SLOT, ZOZURA, 16, 150);
    ram[CURRENT_SPRITE_INDEX] = SLOT;
    CHECK(magic_hook(CHECK_MAGIC) == 1 && magic_dormant == 1);
    blk(LIVE)->awake = 1u << SLOT;
    CHECK(magic_hook(CHECK_MAGIC) == 0 && magic_dormant == 1);
    blk(LIVE)->awake = 0, ram[ENTITIES + SLOT] = COIN;
    CHECK(magic_hook(CHECK_MAGIC) == 0);
}

static void visitors_by_the_spell(void) {
    /* The hero at room 4's x 200, the Deluge at room 4's -40 (side -1): a
     * room 5 Zozura at its x 210 (the live -46) is far from him but at the
     * spell, so it visits. With the spell at 216 of his room, it doesn't. */
    spell_world(200);
    cast(DELUGE, false);
    for (int x = 197; x >= -40; x -= 3) ram[CAST_X] = (uint8_t)x, spell_x16();
    fill(LEFT_ROOM, 1, ZOZURA, 210, 150);
    CHECK(spell_x16() == -40 && arrive() == 1 && pass.count == 1 && pass.v[0].slot == 1);
    arrive();
    spell_world(200);
    cast(DELUGE, false);
    ram[CAST_X] = 216, spell_x16();
    fill(LEFT_ROOM, 1, ZOZURA, 210, 150);
    CHECK(arrive() == 0);
}

static void drawn_on_its_side(void) {
    /* The Deluge at -6 (byte 250, side -1), drawn inside Player_CastMagic: a
     * screen over, so its left cell lies at -6, past room 4's side, the
     * spell's, and its right one at 2 is in OAM, the spell's; his room's
     * capture keeps both beside the sprite loop's, his own wrap untouched. */
    spell_world(40);
    cast(DELUGE, false);
    for (int x = 39; x >= -6; x -= 3) ram[CAST_X] = (uint8_t)x, spell_x16();
    bank14[DATA - 0x8000] = 0x20, bank14[DATA - 0x8000 + 1] = 0, bank14[DATA - 0x8000 + 2] = 0x21,
                  bank14[DATA - 0x8000 + 3] = 0;
    ram[COLS] = 1, ram[ROWS] = 0, ram[NORM_X] = 250, ram[OFFSET_X] = 0, ram[NORM_Y] = 152, ram[OFFSET_Y] = 0;
    ram[READ_ADDR] = (uint8_t)DATA, ram[READ_ADDR + 1] = DATA >> 8, ram[SPRITE_SLOT] = 0, ram[START_SLOT_RANGE] = 0;
    ram[FLIP_MASK] = 0, ram[MOVING_VISIBILITY] = 0, ram[SLOTS_FULL] = 0;
    CHECK(draw_hook(DRAW_STANDARD) == 0 && ram[OFFSET_X] == 0xFF && spell_drawn_abroad == 1);
    CHECK(spell_drawn.count == 1 && spell_drawn.cell[0].x == -6 && spell_drawn.cell[0].slot == SPELL);
    CHECK(spell_drawn.owner[0] == SPELL + 1);
    cast_done_hook(CHECK_USE_ITEM);
    loop_hook(SPRITES_UPDATE_ALL); /* the sprite loop draws a Zozura after it */
    put(LIVE, 3, ZOZURA, 100, 120, false, true);
    ram[CURRENT_SPRITE_INDEX] = 3, ram[OFFSET_X] = 0, ram[NORM_X] = 100, ram[SPRITE_SLOT] = 1;
    draw_hook(DRAW_STANDARD);
    loop_done_hook(SPRITES_LOOP_DONE);
    fax_seams_captured(blocks[LIVE], FAX_SEAMS_LIVE);
    const FaxSeamsCell *cells;
    CHECK(fax_seams_cells(blocks[LIVE], &cells) == 1 && cells[0].x == -6 && cells[0].slot == SPELL);
    CHECK(fax_seams_oam_slot(blocks[LIVE], 0) == SPELL && fax_seams_slot_tiles(blocks[LIVE], SPELL) == -1);
    CHECK(fax_seams_oam_slot(blocks[LIVE], 1) == 3 && fax_seams_valid(blocks[LIVE], ROOMS));
    /* A neighbour's capture takes none of it. */
    fax_seams_captured(blocks[LEFT_ROOM], FAX_SEAMS_TICK);
    CHECK(fax_seams_oam_slot(blocks[LEFT_ROOM], 0) != SPELL);
    /* In his room the draw is left where it is, but still the spell's. */
    spell_world(40);
    cast(DELUGE, false);
    bank14[DATA - 0x8000] = 0x20, bank14[DATA - 0x8000 + 1] = 0, bank14[DATA - 0x8000 + 2] = 0x21,
                  bank14[DATA - 0x8000 + 3] = 0;
    ram[CAST_X] = 30, ram[NORM_X] = 30, ram[OFFSET_X] = 0;
    ram[READ_ADDR] = (uint8_t)DATA, ram[READ_ADDR + 1] = DATA >> 8, ram[COLS] = 1, ram[ROWS] = 0, ram[NORM_Y] = 152;
    CHECK(draw_hook(DRAW_STANDARD) == 0 && ram[OFFSET_X] == 0 && spell_drawn.count == 0);
    CHECK(spell_drawn.owner[0] == SPELL + 1 && spell_drawn_abroad == 0);
}

static void its_visibility(void) {
    /* Room 5's column 5, row 9 a foreground (property 4). The Deluge at
     * room 4's -175 moving left: both its pixels (-171, -163) in it, behind
     * (3). At -187: its left pixel in column 4 (air), its right in 5: bit 1
     * only, then swapped moving right. Wholly in his room: the original's. */
    spell_world(40);
    block_at(LEFT_ROOM, 5, 9, FOREGROUND);
    cast(DELUGE, false);
    for (int x = 38; x >= -175; x -= 3) ram[CAST_X] = (uint8_t)x, spell_x16();
    CHECK(spell_x16() == -175 && visibility_hook(CAST_VISIBILITY) == 1 && ram[MOVING_VISIBILITY] == 3 &&
          ram[TEMP_VISIBILITY] == 3);
    CHECK(ram[PIXEL_X] == (uint8_t)(-163) && ram[PIXEL_Y] == SPELL_Y && cpu.a == 3);
    for (int x = -175; x >= -187; x -= 3) ram[CAST_X] = (uint8_t)x, spell_x16();
    CHECK(spell_x16() == -187 && visibility_hook(CAST_VISIBILITY) == 1 && ram[MOVING_VISIBILITY] == 2);
    ram[CAST_FLAGS] = 0x40;
    CHECK(visibility_hook(CAST_VISIBILITY) == 1 && ram[MOVING_VISIBILITY] == 1 && ram[TEMP_VISIBILITY] == 2);
    /* Only 4 counts on the right (the routine's BNE), 9 and $0D on the left too. */
    spell_world(40);
    cast(DELUGE, false);
    for (int x = 37; x >= -20; x -= 3) ram[CAST_X] = (uint8_t)x, spell_x16(); /* -20: pixels -16 and -8 */
    mem(LEFT_ROOM)[BLOCK_PROPERTIES + 2] = 0x90;                              /* id 5 property 9 */
    block_at(LEFT_ROOM, 15, 9, 5);
    CHECK(visibility_hook(CAST_VISIBILITY) == 1 && ram[MOVING_VISIBILITY] == 1);
    block_at(LEFT_ROOM, 15, 9, AIR), block_at(LEFT_ROOM, 14, 9, 5);
    CHECK(visibility_hook(CAST_VISIBILITY) == 1 && ram[MOVING_VISIBILITY] == 0);
    ram[CAST_X] = 20; /* back in his room: the original's */
    spell.side = 0, spell.last = 20;
    CHECK(visibility_hook(CAST_VISIBILITY) == 0);
}

static void a_crossing_keeps_it(void) {
    /* He crosses into room 5 with the spell at room 4's -41: it is room 5's
     * 215, his room's now (side 0); into room 3 with it at 298: room 3's 42.
     * His own room's spell is a room to the side then. Up or down, it ends. */
    spell_world(4);
    cast(DELUGE, false);
    for (int x = 1; x >= -41; x -= 3) ram[CAST_X] = (uint8_t)x, spell_x16();
    fax_seams_adopted(LIVE, LEFT_ROOM);
    CHECK(spell.side == 0 && spell_x16() == 215 && ram[CAST_TYPE] == DELUGE && spell_crossed == 1);
    spell_world(250);
    cast(DELUGE, true);
    for (int x = 253; x <= 298; x += 3) ram[CAST_X] = (uint8_t)x, spell_x16();
    fax_seams_adopted(LIVE, RIGHT_ROOM);
    CHECK(spell.side == 0 && spell_x16() == 42);
    spell_world(250);
    cast(DELUGE, false);
    fax_seams_adopted(LIVE, RIGHT_ROOM);
    CHECK(spell.side == -1 && spell_x16() == 250 - 256 && ram[CAST_TYPE] == DELUGE);
    place_y[RIGHT_ROOM] = 1; /* below his: a ladder down */
    spell_world(250);
    cast(DELUGE, false);
    fax_seams_adopted(LIVE, RIGHT_ROOM);
    CHECK(ram[CAST_TYPE] == 0xFF && spell.side == 0 && spell_cleared == 1);
    place_y[RIGHT_ROOM] = 0;
    /* Too far (two rooms off): it ends, as it would at the view's edge. */
    spell_world(4);
    cast(DELUGE, true);
    spell.side = 1;
    fax_seams_adopted(LIVE, LEFT_ROOM);
    CHECK(ram[CAST_TYPE] == 0xFF);
    /* With Seams off Seamless clears it, as it always did. */
    spell_world(4);
    cast(DELUGE, false);
    enabled = false;
    fax_seams_adopted(LIVE, LEFT_ROOM);
    CHECK(ram[CAST_TYPE] == 0xFF);
    enabled = true;
}

static void the_record_keeps_its_side(void) {
    /* Version 2 keeps the side, its last byte and that a cast is drawing; a
     * version 1 record (slice 2's) loads, the spell in his room. */
    spell_world(4);
    cast(DELUGE, false);
    for (int x = 1; x >= -41; x -= 3) ram[CAST_X] = (uint8_t)x, spell_x16();
    uint8_t rec[RECORD + 4];
    int n = record_get(rec, sizeof rec);
    CHECK(n == RECORD && rec[0] == 2 && record_validate(rec, n));
    clear_spell();
    CHECK(record_set(rec, n) && spell.side == -1 && spell.last == (uint8_t)-41 && spell.casting && !spell.resync);
    CHECK(spell_x16() == -41);
    rec[RECORD_V1] = 2; /* side 2: never written */
    CHECK(!record_validate(rec, n));
    rec[RECORD_V1] = 0xFF, rec[RECORD_V1 + 3] = 2;
    CHECK(!record_validate(rec, n));
    rec[RECORD_V1 + 3] = 1, rec[0] = 1; /* version 1 at version 2's length */
    CHECK(!record_validate(rec, n));
    CHECK(record_validate(rec, RECORD_V1) && record_set(rec, RECORD_V1));
    CHECK(spell.resync && spell.side == 0 && !spell.casting);
    ram[CAST_X] = 200;
    CHECK(spell_x16() == 200 && !spell.resync);
    rec[0] = 2;
    CHECK(!record_validate(rec, RECORD_V1));
}

int main(void) {
    the_original_wraps();
    the_ghost_hit_is_refused();
    real_hits_are_the_originals();
    every_place_against_the_true_box();
    the_same_rule_in_a_copy_uncounted();
    copies_touch_nothing();
    off_means_off();
    walking_right_through_the_seam();
    walking_left_through_the_seam();
    walls_stay_walls();
    a_full_room_is_a_wall();
    floors_across_the_seam();
    fliers_and_shots();
    dormant_until_seen();
    drawn_where_it_lies();
    drawn_a_screen_over();
    moves_between_frames();
    shots_out_of_the_view_go();
    blocks_and_the_record();
    what_a_town_room_is();
    towns_stay_safe();
    touches_told_apart();
    shields_and_spells_told_apart();
    the_visitors_pass();
    who_visits();
    sugatas_flash();
    a_neighbours_sounds();
    the_record_mid_pass();
    the_spells_side();
    every_flight_against_the_truth();
    the_original_in_an_arena();
    its_own_rules();
    spells_told_apart_and_dormant();
    visitors_by_the_spell();
    drawn_on_its_side();
    its_visibility();
    a_crossing_keeps_it();
    the_record_keeps_its_side();
    printf("seams_test: %u checks passed\n", checks);
    return 0;
}
