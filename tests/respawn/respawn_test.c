/* Unit tests for zone respawn (fax_respawn.c), without the emulator or a ROM.
 * The module runs against a model of the machine: CPU RAM (the stack page
 * included), the registers, and the game's ways into a screen played as the
 * disassembly has them, each setting what it sets before Screen_SetupNew. The
 * respawn's stack is then run with RTS as the CPU would. */
#include "fax_zone.c"
#include "fax_respawn.c"

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
#define SCREEN_PALETTE_INDEX 0x03D0u /* what Screen_SetupNew sets from the palette */
#define PLAYER_POS_X         0x009Eu
#define PLAYER_POS_Y         0x00A1u
static uint8_t ram[0x800];
static CycModRegs cpu;
static bool isolated;
static int hooks_on; /* hooks enabled, as last set */

uint8_t cyc_mod_peek(uint16_t addr) { return addr < 0x2000 ? ram[addr & 0x7FF] : 0; }
bool cyc_mod_poke(uint16_t addr, uint8_t value) {
    if (addr >= 0x2000) return false;
    ram[addr & 0x7FF] = value;
    return true;
}
void cyc_mod_regs(CycModRegs *out) { *out = cpu; }
void cyc_mod_set_regs(const CycModRegs *in) { cpu = *in; }
bool cyc_mod_isolated(void) { return isolated; }
int nes_mod_register_function_entry_plugin(const char *id, uint16_t addr, NESModFunctionEntryCallback cb) {
    return (!strcmp(id, hook_ids[0]) && addr == SCREEN_SETUP_NEW && cb == load_hook) ||
           (!strcmp(id, hook_ids[1]) && addr == DEATH_FADE && cb == death_hook) ||
           (!strcmp(id, hook_ids[2]) && addr == SPAWN_IN_TEMPLE && cb == spawn_hook) ||
           (!strcmp(id, hook_ids[3]) && addr == SETUP_ENTER_BUILDING && cb == enter_building_hook);
}
int nes_mod_set_function_hook_enabled(const char *id, int on) {
    for (unsigned i = 0; i < 4; ++i)
        if (!strcmp(id, hook_ids[i])) {
            hooks_on = on ? hooks_on | 1 << i : hooks_on & ~(1 << i);
            return 1;
        }
    return 0;
}
int nes_mod_register_reset_callback(NESModActivationCallback cb) { return cb == deactivate; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    return !strcmp(id, "faxanadu.zone-respawn") && cb == activate;
}
int nes_mod_register_savestate_hook(const char *id, NESModSavestateGet get, NESModSavestateSet set) {
    return !strcmp(id, "faxanadu.zone-respawn") && get == state_get && set == state_set;
}
int nes_mod_register_savestate_validator(const char *id, NESModSavestateValidate v) {
    return !strcmp(id, "faxanadu.zone-respawn") && v == state_validate;
}
void nes_mod_set_local_only(const char *name, int required) {
    (void)name;
    (void)required;
}

static void push(uint8_t v) {
    ram[0x100 | cpu.s] = v;
    cpu.s = (uint8_t)(cpu.s - 1);
}
static uint8_t pull(void) {
    cpu.s = (uint8_t)(cpu.s + 1);
    return ram[0x100 | cpu.s];
}
static uint16_t rts(void) {
    uint8_t lo = pull();
    return (uint16_t)((lo | pull() << 8) + 1);
}
static void jsr(uint16_t at) {
    uint16_t last = (uint16_t)(at + 2);
    push((uint8_t)(last >> 8));
    push((uint8_t)last);
}

/* ---- the game's ways into a screen, each up to Screen_SetupNew ---- */
enum { REGION_EOLIS, REGION_TRUNK, REGION_MIST, REGION_BRANCH, REGION_DARTMOOR, REGION_EVIL_FORTRESS };
enum {
    MUSIC_APOLUNE = 0x03,
    MUSIC_TOWER = 0x06,
    MUSIC_EOLIS = 0x07,
    MUSIC_MASCON_VICTIM = 0x09,
    MUSIC_TEMPLE = 0x0E,
    MUSIC_SHOP = 0x0F
};
enum {
    PALETTE_EOLIS = 0x00,
    PALETTE_OUTSIDE = 0x06,
    PALETTE_TOWER = 0x07,
    PALETTE_TEMPLE = 0x12,
    PALETTE_MEAT_SHOP = 0x18,
    PALETTE_TOWN = 0x1B
};

static void setup_new(void) { load_hook(SCREEN_SETUP_NEW); } /* the hook, then the routine's own work: */
static void screen_load(void) {
    setup_new();
    ram[AREA_CURRENT_SCREEN] = ram[AREA_LOADING_SCREEN];
    ram[SCREEN_PALETTE_INDEX] = ram[SCREEN_DEST_PALETTE];
}
/* A door within the area (Area_SetStateFromDoorDestination, then the palette's
 * music in Player_CheckHandleEnterDoor, then Game_SetupEnterScreen). */
static void door(unsigned screen, unsigned palette, unsigned start, unsigned music) {
    ram[AREA_LOADING_SCREEN] = (uint8_t)screen;
    ram[SCREEN_DEST_PALETTE] = (uint8_t)palette;
    ram[SCREEN_START_POS] = (uint8_t)start;
    ram[AREAS_DEFAULT_MUSIC] = (uint8_t)music;
    screen_load();
}
/* A door to the next region (Player_EnterDoorToOutside, Game_LoadCurrentArea,
 * which faces him right with Player_SetInitialState). */
static void region_door(unsigned region, unsigned area, unsigned screen, unsigned start, unsigned palette,
                        unsigned music) {
    ram[AREA_REGION] = (uint8_t)region;
    ram[AREA_CURRENT_AREA] = (uint8_t)area;
    ram[AREA_LOADING_SCREEN] = (uint8_t)screen;
    ram[SCREEN_START_POS] = (uint8_t)start;
    ram[PLAYER_FLAGS] = FACING_RIGHT;
    ram[SCREEN_DEST_PALETTE] = (uint8_t)palette;
    ram[AREAS_DEFAULT_MUSIC] = (uint8_t)music;
    screen_load();
}
/* A town's edge (Player_CheckSwitchScreen_SwitchAreaHoriz, Game_EnterAreaHandler). */
static void edge(unsigned area, unsigned screen, unsigned start, unsigned palette, unsigned music) {
    ram[AREA_CURRENT_AREA] = (uint8_t)area;
    ram[AREA_LOADING_SCREEN] = (uint8_t)screen;
    ram[SCREEN_START_POS] = (uint8_t)start;
    ram[SCREEN_DEST_PALETTE] = (uint8_t)palette;
    ram[AREAS_DEFAULT_MUSIC] = (uint8_t)music;
    screen_load();
}
/* Into a building (Player_EnterDoorToInside, Game_EnterBuilding), and out
 * again (Game_BeginExitBuilding, Game_ExitBuilding). */
static uint8_t music_outside;
static void enter_building(unsigned building, unsigned palette, unsigned music) {
    music_outside = ram[AREAS_DEFAULT_MUSIC];
    ram[AREAS_DEFAULT_MUSIC] = (uint8_t)music;
    ram[AREA_SAVED_AREA] = ram[AREA_CURRENT_AREA];
    ram[AREA_SAVED_SCREEN] = ram[AREA_CURRENT_SCREEN];
    ram[SCREEN_SAVED_PALETTE] = ram[SCREEN_PALETTE_INDEX];
    ram[PLAYER_SAVED_POS] = (uint8_t)((ram[PLAYER_POS_Y] & 0xF0) | ram[PLAYER_POS_X] >> 4);
    ram[AREA_CURRENT_AREA] = FAX_AREA_BUILDINGS;
    ram[AREA_LOADING_SCREEN] = (uint8_t)building;
    ram[SCREEN_DEST_PALETTE] = (uint8_t)palette;
    screen_load();
}
static void exit_building_load(void) {
    ram[AREA_CURRENT_AREA] = ram[AREA_SAVED_AREA];
    ram[AREA_LOADING_SCREEN] = ram[AREA_SAVED_SCREEN];
    ram[SCREEN_DEST_PALETTE] = ram[SCREEN_SAVED_PALETTE];
    ram[SCREEN_START_POS] = ram[PLAYER_SAVED_POS];
    screen_load();
}
static void exit_building(void) {
    ram[PLAYER_FLAGS] |= FACING_RIGHT;
    ram[AREAS_DEFAULT_MUSIC] = music_outside;
    exit_building_load();
}
/* Standing somewhere on the current screen. */
static void stand(unsigned x, unsigned y) {
    ram[PLAYER_POS_X] = (uint8_t)x;
    ram[PLAYER_POS_Y] = (uint8_t)y;
}
/* A new game: Game_LoadFirstLevel loads Eolis screen 0. */
static void new_game(void) {
    memset(ram, 0, sizeof ram);
    ram[PLAYER_FLAGS] = FACING_RIGHT;
    region_door(REGION_EOLIS, FAX_AREA_EOLIS, 0x00, 0x91, PALETTE_EOLIS, MUSIC_EOLIS);
}

/* ---- dying and spawning ---- */
/* The temple's layout, as Game_SpawnInTemple and Game_EnterBuilding make it,
 * for the temple of Eolis. */
static void temple(void) {
    ram[AREA_CURRENT_AREA] = FAX_AREA_EOLIS;
    ram[AREA_CURRENT_SCREEN] = 0x02;
    ram[AREA_REGION] = REGION_EOLIS;
    stand(0x50, 0x90);
    ram[SCREEN_PALETTE_INDEX] = PALETTE_EOLIS;
    ram[AREAS_DEFAULT_MUSIC] = MUSIC_EOLIS;
    ram[PLAYER_FLAGS] &= (uint8_t)~FACING_RIGHT;
    enter_building(0x02, PALETTE_TEMPLE, MUSIC_TEMPLE);
}
/* Player_Spawn from its JSR Game_SpawnInTemple, where S is $FF, to its JMP
 * Game_MainLoop, each hook where the CPU meets it and RTS as the CPU does it.
 * 1: back at the checkpoint; 0: the temple, as in the original. */
static unsigned spawned; /* Player_Spawn reached SPAWNED, where Save files saves */
static int player_spawn(void) {
    cpu.s = 0xFF;
    if (spawn_hook(SPAWN_IN_TEMPLE)) CHECK(rts() == SPAWNED); /* the JSR skipped, as if it returned */
    else temple();                                            /* half of it: Game_SpawnInTemple's layout */
    ++spawned;
    jsr(SPAWNED); /* JSR Game_SetupEnterBuilding */
    int back = enter_building_hook(SETUP_ENTER_BUILDING);
    if (back) {
        CHECK(rts() == SETUP_EXIT_BUILDING);
        exit_building_load(); /* Game_ExitBuilding's Screen_Load */
    }
    CHECK(rts() == SPAWNED + 3 && cpu.s == 0xFF); /* JMP Game_MainLoop */
    return back;
}
/* A death: Player_HandleDeath to the end of Player_Spawn. */
static int die(void) {
    death_hook(DEATH_FADE);
    ram[PLAYER_FLAGS] = FACING_RIGHT; /* Player_SetInitialState, in Game_InitStateForSpawn */
    return player_spawn();
}
/* Player_Spawn without a death: a file or a mantra continued, or a Guru travel
 * journey's prepared stack. */
static int spawn(void) { return player_spawn(); }

static bool at(int zone, unsigned area, unsigned screen, unsigned start) {
    return checkpoint.zone == zone && checkpoint.area == area && checkpoint.screen == screen &&
           checkpoint.start == start;
}

/* ---- the tests ---- */
static void begin(void) {
    deactivate();
    activate();
    isolated = false;
    new_game();
}

static void entrances(void) {
    begin();
    CHECK(hooks_on == 15);
    CHECK(checkpoint.zone == FAX_ZONE_NONE); /* outside the gate: no zone */
    /* Through the gate: Eolis, from where the gate puts him. */
    door(0x01, PALETTE_EOLIS, 0x99, MUSIC_EOLIS);
    CHECK(at(FAX_ZONE_EOLIS, FAX_AREA_EOLIS, 0x01, 0x99) && recorded == 1);
    CHECK(checkpoint.palette == PALETTE_EOLIS && checkpoint.music == MUSIC_EOLIS && checkpoint.facing == FACING_RIGHT);
    /* A shop and out again: buildings change nothing. */
    stand(0x30, 0x90);
    enter_building(0x00, PALETTE_MEAT_SHOP, MUSIC_SHOP);
    CHECK(at(FAX_ZONE_EOLIS, FAX_AREA_EOLIS, 0x01, 0x99));
    exit_building();
    CHECK(at(FAX_ZONE_EOLIS, FAX_AREA_EOLIS, 0x01, 0x99) && recorded == 1);
    /* Out of Eolis by its door: the Trunk, a region. */
    region_door(REGION_TRUNK, FAX_AREA_TRUNK, 0x00, 0xA1, PALETTE_OUTSIDE, MUSIC_APOLUNE);
    CHECK(at(FAX_ZONE_TRUNK, FAX_AREA_TRUNK, 0x00, 0xA1) && checkpoint.region == REGION_TRUNK);
    /* Into the Tower of Trunk, with its music and palette. */
    ram[PLAYER_FLAGS] = 0; /* facing left at the door */
    door(0x0D, PALETTE_TOWER, 0x91, MUSIC_TOWER);
    CHECK(at(FAX_ZONE_TOWER_OF_TRUNK, FAX_AREA_TRUNK, 0x0D, 0x91));
    CHECK(checkpoint.palette == PALETTE_TOWER && checkpoint.music == MUSIC_TOWER && checkpoint.facing == 0);
    /* And out: the Trunk again, now from the tower's door. */
    door(0x0B, PALETTE_OUTSIDE, 0xA9, MUSIC_APOLUNE);
    CHECK(at(FAX_ZONE_TRUNK, FAX_AREA_TRUNK, 0x0B, 0xA9));
    /* Doors and climbs within a zone keep its way in: the Joker spring, the
     * climb from $0C to $16 (AREA_SCREEN_COMPARATORS). */
    door(0x3E, PALETTE_TOWER, 0x92, MUSIC_TOWER);
    door(0x16, PALETTE_OUTSIDE, 0xB3, MUSIC_APOLUNE);
    CHECK(at(FAX_ZONE_TRUNK, FAX_AREA_TRUNK, 0x0B, 0xA9) && recorded == 4);
    /* A town's edge: Apolune, and back into the Trunk by the other edge. */
    edge(FAX_AREA_TOWNS, 0x00, 0x92, PALETTE_TOWN, MUSIC_MASCON_VICTIM);
    CHECK(at(FAX_ZONE_APOLUNE, FAX_AREA_TOWNS, 0x00, 0x92));
    edge(FAX_AREA_TRUNK, 0x08, 0x71, PALETTE_OUTSIDE, MUSIC_APOLUNE);
    CHECK(at(FAX_ZONE_TRUNK, FAX_AREA_TRUNK, 0x08, 0x71) && recorded == 6);
    /* Seamless's neighboring rooms run in an isolated scope: never a way in. */
    isolated = true;
    door(0x0D, PALETTE_TOWER, 0x91, MUSIC_TOWER);
    isolated = false;
    CHECK(at(FAX_ZONE_TRUNK, FAX_AREA_TRUNK, 0x08, 0x71) && recorded == 6);
    /* A new game's Eolis screen 0 (no zone) forgets it. */
    new_game();
    CHECK(checkpoint.zone == FAX_ZONE_NONE);
}

static void respawning(void) {
    begin();
    region_door(REGION_TRUNK, FAX_AREA_TRUNK, 0x00, 0xA1, PALETTE_OUTSIDE, MUSIC_APOLUNE);
    ram[PLAYER_FLAGS] = 0;
    door(0x0D, PALETTE_TOWER, 0x91, MUSIC_TOWER);
    Checkpoint tower = checkpoint;
    /* Deeper in: a scroll changes the screen, nothing loads. */
    ram[AREA_CURRENT_SCREEN] = 0x12;
    ram[AREA_REGION] = REGION_EOLIS; /* anything; the respawn sets it */
    ram[AREAS_DEFAULT_MUSIC] = 0x0A; /* the boss's, say */
    memset(ram + AREA_SAVED_AREA, 0x5A, 4);
    death_hook(DEATH_FADE);
    CHECK(death == DYING);
    cpu.s = 0xFF;
    ram[PLAYER_FLAGS] = FACING_RIGHT;
    /* Player_Spawn's JSR Game_SpawnInTemple is skipped, as if it returned at
     * once, so Player_Spawn goes on at SPAWNED (Save files saves there) with
     * nothing laid out yet. */
    CHECK(spawn_hook(SPAWN_IN_TEMPLE) == 1 && death == RESPAWNING);
    CHECK(cpu.s == 0xFD && rts() == SPAWNED && cpu.s == 0xFF);
    CHECK(ram[AREA_SAVED_AREA] == 0x5A && respawns == 0);
    /* Its JSR Game_SetupEnterBuilding: Game_ExitBuilding will read the
     * entrance, with its region and music, and face him as he came in. */
    jsr(SPAWNED);
    CHECK(enter_building_hook(SETUP_ENTER_BUILDING) == 1 && death == ALIVE);
    CHECK(respawns == 1 && fallbacks == 0);
    CHECK(ram[AREA_SAVED_AREA] == FAX_AREA_TRUNK && ram[AREA_SAVED_SCREEN] == 0x0D);
    CHECK(ram[SCREEN_SAVED_PALETTE] == PALETTE_TOWER && ram[PLAYER_SAVED_POS] == 0x91);
    CHECK(ram[AREA_REGION] == REGION_TRUNK && ram[AREAS_DEFAULT_MUSIC] == MUSIC_TOWER);
    CHECK((ram[PLAYER_FLAGS] & FACING_RIGHT) == 0);
    /* The engine RTSes from S into Game_SetupExitBuilding, whose RTS goes back
     * to Player_Spawn's JMP Game_MainLoop. */
    CHECK(cpu.s == 0xFB && rts() == SETUP_EXIT_BUILDING);
    jsr(0xDAC1);
    CHECK(rts() == 0xDAC4); /* its JSR Game_ExitBuilding returns as usual */
    CHECK(rts() == SPAWNED + 3 && cpu.s == 0xFF);
    /* Game_ExitBuilding's load lands in the same zone: the checkpoint stays. */
    exit_building_load();
    CHECK(memcmp(&checkpoint, &tower, sizeof tower) == 0);
    CHECK(ram[AREA_CURRENT_SCREEN] == 0x0D);
    /* Again: every death in the zone comes back there, through SPAWNED. */
    ram[AREA_CURRENT_SCREEN] = 0x15;
    spawned = 0;
    CHECK(die() == 1 && respawns == 2 && spawned == 1);
    CHECK(memcmp(&checkpoint, &tower, sizeof tower) == 0 && ram[AREA_CURRENT_SCREEN] == 0x0D);
    /* Game_SetupEnterBuilding from anywhere else is the game's: a door into a
     * building, or (should a respawn ever get there another way) not from
     * Player_Spawn, which also ends the respawn. */
    memset(ram + AREA_SAVED_AREA, 0x5A, 4);
    cpu.s = 0xF0;
    jsr(0xE530);
    CHECK(enter_building_hook(SETUP_ENTER_BUILDING) == 0 && rts() == 0xE533);
    death = RESPAWNING;
    jsr(0xE530);
    CHECK(enter_building_hook(SETUP_ENTER_BUILDING) == 0 && death == ALIVE && rts() == 0xE533);
    CHECK(ram[AREA_SAVED_AREA] == 0x5A && respawns == 2);
}

static void fallbacks_to_the_temple(void) {
    /* No checkpoint: a death outside Eolis's gate. The temple at
     * TempleSpawnPoint (Save files keeps the Guru last saved with there). */
    begin();
    spawned = 0;
    CHECK(die() == 0 && fallbacks == 1 && respawns == 0 && death == ALIVE && spawned == 1);
    CHECK(ram[AREA_CURRENT_AREA] == FAX_AREA_BUILDINGS);
    /* Walking out of the temple records the town. */
    exit_building();
    CHECK(at(FAX_ZONE_EOLIS, FAX_AREA_EOLIS, 0x02, 0x95));
    /* A checkpoint for another zone than where he died. */
    ram[AREA_CURRENT_AREA] = FAX_AREA_TRUNK;
    ram[AREA_CURRENT_SCREEN] = 0x03;
    CHECK(die() == 0 && fallbacks == 2);
    CHECK(checkpoint.zone == FAX_ZONE_NONE); /* the temple's way out is the next way in */
    /* A death in a building, which is in no zone. */
    exit_building();
    stand(0x30, 0x90);
    enter_building(0x00, PALETTE_MEAT_SHOP, MUSIC_SHOP);
    CHECK(checkpoint.zone == FAX_ZONE_EOLIS);
    CHECK(die() == 0 && fallbacks == 3 && respawns == 0);
}

static void other_spawns_forget_it(void) {
    /* Continuing a file or a mantra, or a Guru travel journey, reaches the
     * same JSR without a death: the temple is laid out, and the checkpoint goes. */
    begin();
    door(0x01, PALETTE_EOLIS, 0x99, MUSIC_EOLIS);
    spawned = 0;
    CHECK(spawn() == 0 && checkpoint.zone == FAX_ZONE_NONE && fallbacks == 0 && respawns == 0 && spawned == 1);
    /* Walking out into the zone it forgot: the temple's door is its way in. */
    exit_building();
    CHECK(at(FAX_ZONE_EOLIS, FAX_AREA_EOLIS, 0x02, 0x95));
    /* A death is a death once: the next spawn is not. */
    CHECK(die() == 1);
    CHECK(spawn() == 0 && respawns == 1 && checkpoint.zone == FAX_ZONE_NONE);
    /* Seamless's isolated scope never takes the death, the spawn or the swap. */
    exit_building();
    isolated = true;
    death_hook(DEATH_FADE);
    CHECK(death == ALIVE && spawn_hook(SPAWN_IN_TEMPLE) == 0 && checkpoint.zone == FAX_ZONE_EOLIS);
    death = RESPAWNING;
    CHECK(enter_building_hook(SETUP_ENTER_BUILDING) == 0 && death == RESPAWNING);
    isolated = false;
    death = ALIVE;
}

static void save_states(void) {
    begin();
    region_door(REGION_TRUNK, FAX_AREA_TRUNK, 0x00, 0xA1, PALETTE_OUTSIDE, MUSIC_APOLUNE);
    door(0x0D, PALETTE_TOWER, 0x91, MUSIC_TOWER);
    death_hook(DEATH_FADE); /* saved during the fade */
    uint8_t rec[64];
    int n = state_get(rec, sizeof rec);
    CHECK(n == RECORD && state_get(rec, RECORD - 1) == -1);
    Checkpoint saved = checkpoint;
    deactivate();
    activate();
    CHECK(checkpoint.zone == FAX_ZONE_NONE && death == ALIVE);
    CHECK(state_set(rec, n) == 1 && death == DYING && memcmp(&checkpoint, &saved, sizeof saved) == 0);
    ram[AREA_CURRENT_SCREEN] = 0x0F;
    cpu.s = 0xFF;
    CHECK(spawn_hook(SPAWN_IN_TEMPLE) == 1 && death == RESPAWNING); /* the death goes on to the tower's door */
    /* Saved without the feature: no checkpoint. */
    CHECK(state_set(rec, 0) == 1 && checkpoint.zone == FAX_ZONE_NONE && death == ALIVE);
    /* Bad records are refused, and change nothing. */
    CHECK(state_set(rec, n) == 1);
    uint8_t bad[sizeof rec];
    struct {
        unsigned at;
        uint8_t value;
    } edits[] = {
        { 0, 2 },                  /* version */
        { 1, FAX_ZONE_COUNT },     /* no such zone */
        { 1, FAX_ZONE_TRUNK },     /* not the zone of area 1, screen $0D */
        { 2, FAX_AREA_BUILDINGS }, /* a building */
        { 3, 0x40 },               /* no such screen in the Trunk */
        { 5, 0x1C },               /* a palette the game never sets */
        { 6, 6 },                  /* a region */
        { 7, 0x11 },               /* a song */
        { 8, 0x01 },               /* a facing */
        { 9, RESPAWNING + 1 },     /* where a death is */
    };
    for (unsigned i = 0; i < sizeof edits / sizeof edits[0]; ++i) {
        memcpy(bad, rec, (size_t)n);
        bad[edits[i].at] = edits[i].value;
        CHECK(!state_validate(bad, n) && state_set(bad, n) == 0);
        CHECK(memcmp(&checkpoint, &saved, sizeof saved) == 0);
    }
    CHECK(!state_validate(rec, n - 1) && !state_validate(rec, n + 1));
    /* No checkpoint saves as zone $FF, whatever the other bytes hold. */
    memcpy(bad, rec, (size_t)n);
    bad[1] = 0xFF;
    bad[2] = 0x77;
    CHECK(state_set(bad, n) == 1 && checkpoint.zone == FAX_ZONE_NONE);
}

static void the_seam_for_save_slots(void) {
    begin();
    door(0x01, PALETTE_EOLIS, 0x99, MUSIC_EOLIS);
    uint8_t bytes[FAX_RESPAWN_CHECKPOINT_LEN];
    fax_respawn_checkpoint_pack(bytes);
    Checkpoint saved = checkpoint;
    new_game();
    CHECK(checkpoint.zone == FAX_ZONE_NONE);
    CHECK(fax_respawn_checkpoint_unpack(bytes, sizeof bytes) && memcmp(&checkpoint, &saved, sizeof saved) == 0);
    bytes[2] = 0x00; /* Eolis screen 0 is in no zone */
    CHECK(!fax_respawn_checkpoint_unpack(bytes, sizeof bytes) && checkpoint.zone == FAX_ZONE_EOLIS);
    CHECK(fax_respawn_checkpoint_unpack(bytes, 0) && checkpoint.zone == FAX_ZONE_NONE);
}

static void off_means_off(void) {
    begin();
    door(0x01, PALETTE_EOLIS, 0x99, MUSIC_EOLIS);
    death_hook(DEATH_FADE);
    deactivate();
    CHECK(hooks_on == 0 && !enabled && death == ALIVE && checkpoint.zone == FAX_ZONE_NONE && recorded == 0);
}

int main(void) {
    entrances();
    respawning();
    fallbacks_to_the_temple();
    other_spawns_forget_it();
    save_states();
    the_seam_for_save_slots();
    off_means_off();
    printf("respawn_test: %u checks passed\n", checks);
    return 0;
}
