/* Unit tests for Eolis's edge (fax_shaft.c), without the emulator or a ROM.
 * The module runs against a model of the machine: CPU RAM, the registers, a
 * screen laid out as Eolis $08 is (the floor in row 11, the right wall in
 * column 14, rows 3-11, column 15 open to the bottom), its block properties,
 * and the game's collision code around the hook played as the disassembly
 * has it: ScreenBuffer_IsBlockImpassable, the hero's move right
 * (Area_CanPlayerMoveRight, Area_CanPlayerMoveAtY) and his ground check
 * (Player_CheckIfPassable down, Area_CanMoveImmediatelyRight). So the tests
 * check where the hero can go, not the module's own arithmetic. */
#include "fax_shaft.c"
#include <string.h>

static unsigned checks;
#define CHECK(test) do{++checks;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(1);}}while(0)

#define BLOCK_PROPERTIES 0x043Cu
#define SCREEN_BUFFER    0x0600u
#define FLAG_C           0x01u
#define SHAFT_X          240u     /* column 15 */
#define WALL_TOP_Y       16u      /* standing on row 3, the wall's top */

/* ---- the machine ---- */
static uint8_t ram[0x800];
static CycModRegs cpu;
static bool isolated;
static int solid_on = -1, rescue_on = -1;

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
    return (!strcmp(id, hook_ids[0]) && addr == IS_BLOCK_IMPASSABLE && cb == solid_hook) ||
           (!strcmp(id, hook_ids[1]) && addr == UPDATE_PLAYER_CALL && cb == rescue_hook);
}
int nes_mod_set_function_hook_enabled(const char *id, int on) {
    if (!strcmp(id, hook_ids[0])) solid_on = on;
    else if (!strcmp(id, hook_ids[1])) rescue_on = on;
    else return 0;
    return 1;
}
int nes_mod_register_reset_callback(NESModActivationCallback cb) { return cb == deactivate; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    return !strcmp(id, "faxanadu.eolis-shaft") && cb == activate;
}
void nes_mod_set_local_only(const char *name, int required) { (void)name; (void)required; }

/* ---- the game's code, as the disassembly has it ---- */
static const uint8_t IMPASSABLE_MAP[16] = { 0, 1, 0, 0, 0, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0 };

/* Area_GetBlockProperty ($E8C6): the block's nibble of BlockProperties. */
static uint8_t property(uint8_t block) {
    uint8_t both = ram[BLOCK_PROPERTIES + (block >> 1)];
    return block & 1 ? both >> 4 : both & 15;
}

/* ScreenBuffer_IsBlockImpassable ($E87C) with X the block's index: the hook
 * at its entry, then (unless it returned) LDY ScreenBuffer,X, JSR
 * Area_GetBlockProperty, TAY, LDA BLOCK_PROPERTY_IMPASSABLE_MAP,Y, RTS. */
static uint8_t is_impassable(uint8_t index) {
    cpu.x = index;
    if (solid_on == 1 && solid_hook(IS_BLOCK_IMPASSABLE)) return cpu.a;
    cpu.y = property(ram[SCREEN_BUFFER + index]);
    cpu.a = IMPASSABLE_MAP[cpu.y];
    cpu.p = (uint8_t)((cpu.p & ~(FLAG_N | FLAG_Z)) | (cpu.a ? 0 : FLAG_Z));
    return cpu.a;
}

/* Area_ConvertPixelsToBlockPos: (Y & $F0) | X >> 4. */
static uint8_t block_at(unsigned px, unsigned py) { return (uint8_t)((py & 0xF0u) | (px & 0xFFu) >> 4); }

/* Player_CheckIfPassable, right (Area_CanPlayerMoveRight, then
 * Area_CanPlayerMoveAtY): the column past his right edge, at his rows. Past
 * the screen it reads FirstColumnInRightScreen, which these screens leave
 * empty (no screen right). */
static bool blocked_right(uint8_t x, uint8_t y) {
    if (x + 16u > 255u) return false;
    uint8_t i = block_at(x + 16u, y);
    bool blocked = is_impassable(i) | is_impassable((uint8_t)(i + 16));
    if (y & 15u) blocked |= is_impassable((uint8_t)(i + 32));
    return blocked;
}

/* Player_CheckIfPassable, down (Y + $20 under $D0: Area_CanMoveImmediatelyRight). */
static bool grounded(uint8_t x, uint8_t y) {
    unsigned below = y + 0x20u;
    if (below >= 0xD0u) return false;                  /* FirstRowBelowScreen: none below Eolis */
    uint8_t i = block_at(x + 4u, below);
    bool solid = is_impassable(i);
    if ((uint8_t)((x & 15u) - 4u) < 8u) solid |= is_impassable((uint8_t)(i + 1));
    return solid;
}

/* A screen as Eolis $08's: sky, the floor, the wall, the open column. */
enum { SKY = 0x01, FLOOR = 0x2B, WALL = 0x0E, WALL_BACK_TOP = 0x5A, WALL_BACK = 0x54, WALL_BACK_FLOOR = 0x5F,
       UNDER = 0x48 };
static void set_property(uint8_t block, uint8_t value) {
    uint8_t *both = &ram[BLOCK_PROPERTIES + (block >> 1)];
    *both = block & 1 ? (uint8_t)((*both & 0x0F) | value << 4) : (uint8_t)((*both & 0xF0) | value);
}
static void eolis(uint8_t screen) {
    memset(ram, 0, sizeof ram);
    for (unsigned row = 0; row < 16; ++row)
        for (unsigned col = 0; col < 16; ++col) {
            uint8_t b = row == 15 ? 0x00 : row > 11 ? UNDER : SKY;
            if (row == 11 && col < 14) b = FLOOR;
            if (col == 14 && row >= 3 && row <= 11) b = WALL;
            if (col == 15) b = row < 3 ? SKY : row == 3 ? WALL_BACK_TOP : row < 11 ? WALL_BACK
                         : row == 11 ? WALL_BACK_FLOOR : row == 15 ? 0x00 : UNDER;
            ram[SCREEN_BUFFER + row * 16 + col] = b;
        }
    set_property(FLOOR, 1);
    set_property(WALL, 1);                               /* the rest: 0, air or background */
    ram[AREA_CURRENT] = AREA_EOLIS;
    ram[SCREEN_CURRENT] = screen;
    cpu = (CycModRegs){ 0 };
    isolated = false;
}

/* Game_MainLoop's JSR GameLoop_UpdatePlayer with the hero at (x, y): his
 * position after the site. */
static void main_loop(uint8_t x, uint8_t y) {
    ram[PLAYER_X] = x;
    ram[PLAYER_Y] = y;
    if (rescue_on == 1) CHECK(rescue_hook(UPDATE_PLAYER_CALL) == 0);   /* it never returns from the loop */
}

int main(void) {
    /* Which blocks stand solid. */
    for (unsigned i = 0; i < 256; ++i) {
        bool edge = (i & 15) == 15 && i >> 4 <= 11;
        CHECK(fax_shaft_solid(AREA_EOLIS, 0x08, (uint8_t)i) == edge);
        CHECK(fax_shaft_solid(AREA_EOLIS, 0x00, (uint8_t)i) == edge);
        for (unsigned s = 1; s < 8; ++s) CHECK(!fax_shaft_solid(AREA_EOLIS, (uint8_t)s, (uint8_t)i));
        CHECK(!fax_shaft_solid(1, 0x08, (uint8_t)i));        /* the Trunk's $08 */
        CHECK(!fax_shaft_solid(4, 0x00, (uint8_t)i));        /* a building */
    }

    /* The original (the remaster off): flying at the top, Right takes him
     * over the wall into column 15, and nothing there holds him up. */
    eolis(0x08);
    CHECK(!blocked_right(224, 0) && !blocked_right(232, 8));
    for (unsigned y = 0; y <= 0xC0; y += 4) CHECK(!grounded(240, (uint8_t)y));
    CHECK(grounded(224, WALL_TOP_Y) && blocked_right(208, 100));   /* the wall's top, the wall */

    NESModFunctionEntryCallback none = NULL;
    CHECK(!nes_mod_register_function_entry_plugin(hook_ids[0], 0xE87Fu, solid_hook));   /* past LDY */
    CHECK(!nes_mod_register_function_entry_plugin(hook_ids[1], 0xE0CAu, rescue_hook));
    CHECK(!nes_mod_register_function_entry_plugin("faxanadu.eolis-shaft.other", IS_BLOCK_IMPASSABLE, none));
    activate();
    CHECK(enabled && solid_on == 1 && rescue_on == 1);

    /* With the remaster: Right stops at the wall's edge at every height down
     * to the floor; he stands on the wall's top; nothing past it holds air. */
    eolis(0x08);
    for (unsigned y = 0; y <= 16; ++y) CHECK(blocked_right(224, (uint8_t)y));
    for (unsigned y = 17; y <= FLOOR_Y; ++y) CHECK(blocked_right(208, (uint8_t)y));   /* the wall itself */
    CHECK(grounded(224, WALL_TOP_Y));
    for (unsigned y = 0; y <= FLOOR_Y; y += 4) CHECK(grounded(240, (uint8_t)y) && grounded(232, (uint8_t)y));
    CHECK(solid > 0);
    /* Under the floor the column is the game's (an old state there is put back anyway). */
    unsigned before = solid;
    CHECK(is_impassable(0xCF) == 0 && is_impassable(0xDF) == 0 && solid == before);
    /* The same on $00; on the town's screens and elsewhere, the stock answer. */
    eolis(0x00);
    CHECK(blocked_right(224, 0));
    eolis(0x01);
    CHECK(!blocked_right(224, 0));
    eolis(0x08);
    ram[AREA_CURRENT] = 1;
    CHECK(!blocked_right(224, 0));

    /* The registers as the routine leaves them for the wall beside it: A 1,
     * Y the solid property, N and Z clear (callers STA then BNE), X and C
     * kept; it returns (an RTS). */
    eolis(0x08);
    cpu = (CycModRegs){ .a = 0x00, .x = 0x3F, .y = 0x77, .p = FLAG_Z | FLAG_N | FLAG_C };
    CHECK(solid_hook(IS_BLOCK_IMPASSABLE) == 1);
    CHECK(cpu.a == 1 && cpu.y == PROPERTY_SOLID && cpu.x == 0x3F && (cpu.p & (FLAG_N | FLAG_Z)) == 0 &&
          (cpu.p & FLAG_C));
    cpu = (CycModRegs){ .a = 0x55, .x = 0x3E, .y = 0x66, .p = FLAG_C };   /* column 14: untouched */
    CHECK(solid_hook(IS_BLOCK_IMPASSABLE) == 0 && cpu.a == 0x55 && cpu.y == 0x66);
    /* In Seamless's isolated rooms it answers the same, uncounted. */
    isolated = true;
    before = solid;
    cpu.x = 0x0F;
    CHECK(solid_hook(IS_BLOCK_IMPASSABLE) == 1 && cpu.a == 1 && solid == before);
    isolated = false;

    /* Where a hero found in the shaft or under the floor goes. */
    uint8_t x, y;
    CHECK(fax_shaft_rescue(AREA_EOLIS, 0x08, SHAFT_X, BOTTOM_Y, &x, &y) && x == BESIDE_WALL_X && y == FLOOR_Y);
    CHECK(fax_shaft_rescue(AREA_EOLIS, 0x00, SHAFT_X, BOTTOM_Y, &x, &y) && x == BESIDE_WALL_X && y == FLOOR_Y);
    CHECK(fax_shaft_rescue(AREA_EOLIS, 0x08, SHAFT_X, 100, &x, &y) && x == BESIDE_WALL_X && y == FLOOR_Y);
    CHECK(fax_shaft_rescue(AREA_EOLIS, 0x08, 230, 0, &x, &y) && x == BESIDE_WALL_X && y == FLOOR_Y);
    CHECK(fax_shaft_rescue(AREA_EOLIS, 0x08, 216, BOTTOM_Y, &x, &y) && x == BESIDE_WALL_X && y == FLOOR_Y);
    CHECK(fax_shaft_rescue(AREA_EOLIS, 0x08, 100, BOTTOM_Y, &x, &y) && x == 100 && y == FLOOR_Y);
    CHECK(fax_shaft_rescue(AREA_EOLIS, 0x00, 120, BOTTOM_Y, &x, &y) && x == 120 && y == FLOOR_Y);
    CHECK(fax_shaft_rescue(AREA_EOLIS, 0x01, SHAFT_X, BOTTOM_Y, &x, &y) && x == SHAFT_X && y == FLOOR_Y);
    CHECK(fax_shaft_rescue(AREA_EOLIS, 0x06, 23, BOTTOM_Y, &x, &y) && x == 23 && y == FLOOR_Y);
    CHECK(fax_shaft_rescue(AREA_EOLIS, 0x03, 50, FLOOR_Y + 1, &x, &y) && x == 50 && y == FLOOR_Y);
    /* Where he may be: on the floor, by or on the wall, in the air above it,
     * at the screen's top; past the bottom edge is no place Y goes. */
    CHECK(!fax_shaft_rescue(AREA_EOLIS, 0x08, BESIDE_WALL_X, FLOOR_Y, &x, &y));
    CHECK(!fax_shaft_rescue(AREA_EOLIS, 0x08, WALL_X, WALL_TOP_Y, &x, &y));
    CHECK(!fax_shaft_rescue(AREA_EOLIS, 0x08, WALL_X, 0, &x, &y));
    CHECK(!fax_shaft_rescue(AREA_EOLIS, 0x01, SHAFT_X, FLOOR_Y, &x, &y));    /* the town's right edge */
    CHECK(!fax_shaft_rescue(AREA_EOLIS, 0x08, 0, 0, &x, &y));
    CHECK(!fax_shaft_rescue(AREA_EOLIS, 0x08, 100, 0xC1, &x, &y));
    CHECK(!fax_shaft_rescue(AREA_EOLIS, 0x08, 100, 0xFF, &x, &y));
    CHECK(!fax_shaft_rescue(1, 0x08, SHAFT_X, BOTTOM_Y, &x, &y));            /* the Trunk's pits are its own */
    CHECK(!fax_shaft_rescue(4, 0x00, SHAFT_X, BOTTOM_Y, &x, &y));

    /* The main loop: an old state at the bottom of the shaft is on the floor
     * next to the wall before he moves, standing, and it counts where he was. */
    eolis(0x08);
    main_loop(SHAFT_X, BOTTOM_Y);
    CHECK(ram[PLAYER_X] == BESIDE_WALL_X && ram[PLAYER_Y] == FLOOR_Y && grounded(ram[PLAYER_X], ram[PLAYER_Y]));
    CHECK(!is_impassable(0x9D) && !is_impassable(0xAD));                       /* his body's blocks: clear */
    CHECK(rescued == 1 && from_screen == 0x08 && from_x == SHAFT_X && from_y == BOTTOM_Y);
    main_loop(ram[PLAYER_X], ram[PLAYER_Y]);                                   /* then left alone */
    CHECK(rescued == 1 && ram[PLAYER_X] == BESIDE_WALL_X);
    /* Not in Seamless's isolated machines. */
    isolated = true;
    main_loop(SHAFT_X, BOTTOM_Y);
    CHECK(ram[PLAYER_X] == SHAFT_X && ram[PLAYER_Y] == BOTTOM_Y && rescued == 1);

    deactivate();
    CHECK(!enabled && solid_on == 0 && rescue_on == 0 && solid == 0 && rescued == 0 && from_screen == 0xFF);
    /* Off, the original's: nothing answered, nobody moved. */
    eolis(0x08);
    CHECK(!blocked_right(224, 0) && !grounded(240, 100));
    main_loop(SHAFT_X, BOTTOM_Y);
    CHECK(ram[PLAYER_X] == SHAFT_X && ram[PLAYER_Y] == BOTTOM_Y);

    printf("shaft_test: %u checks ok\n", checks);
    return 0;
}
