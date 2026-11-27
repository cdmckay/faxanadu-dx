/* Zone respawn. USA 57dd23d1, chipx86 byte-exact disassembly.
 * Dying brings the hero back at the entrance of the zone he died in (a tower,
 * a castle, a region: fax_zone.c), not at the furthest temple. The Guru still
 * speaks first: the death path is the game's up to Player_Spawn, which refills
 * HP and MP and clears the Mattock bit and the timed items as before.
 *
 * - The checkpoint is the last entrance used. Every screen the game loads goes
 *   through Screen_SetupNew, and at its entry the way in is complete in RAM:
 *   the area, region, screen, palette, start position, music and facing that
 *   the door, the region's door, the town's edge or the building's exit set.
 *   Entering a different zone records them. Buildings belong to no zone, and
 *   their way out leads back where the hero stood, so they change nothing.
 * - The respawn replays that entrance through the game's own loader for a
 *   place outside: Game_SetupExitBuilding, which loads Area_SavedArea,
 *   Area_SavedScreen, Screen_SavedPalette and Player_SavedPosXY with the
 *   area's tiles, music and sprites. At Player_Spawn's JSR Game_SpawnInTemple
 *   a death with a checkpoint for its zone skips the call, so Player_Spawn
 *   goes on at its next instruction ($DB20, where Save files saves the file
 *   at every respawn) to JSR Game_SetupEnterBuilding; at that routine's entry
 *   the hook writes the checkpoint where Game_ExitBuilding reads it and calls
 *   Game_SetupExitBuilding instead. Player_Spawn's JMP Game_MainLoop follows.
 * - The fallback is the original: no checkpoint, or one for another zone (he
 *   died in a building), lays out the temple at TempleSpawnPoint, which Save
 *   files keeps at the Guru last saved with (#43). Any temple spawn (the
 *   fallback, continuing a file or a mantra, a Guru travel journey) forgets
 *   the checkpoint, so the way out of the temple becomes the next one.
 *
 * The checkpoint lives for the session and in this feature's save-state
 * record. A file continues at its Guru, so it never needs the checkpoint.
 * See docs/GAME_INTERNALS.md, "Entrances and zone respawn". */
#include "fax_respawn.h"
#include "fax_zone.h"
#include "cyc_mod.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#ifdef CYC_WITH_SDL
#include "cyc_tcp.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Fixed bank */
#define SCREEN_SETUP_NEW   0xDD13u   /* Screen_SetupNew: JSR Player_DrawSpriteImmediately. Every screen load */
/* Player_HandleDeath's @_afterSetExpGoldFarJump: JSR Screen_FadeToBlack,
 * after the death script and Player_SetInitialExpAndGold */
#define DEATH_FADE         0xD9A0u
/* Player_Spawn's JSR Game_SpawnInTemple; then JSR Game_SetupEnterBuilding,
 * JMP Game_MainLoop */
#define SPAWN_IN_TEMPLE    0xDB1Du
#define SPAWNED            0xDB20u   /* Player_Spawn's JSR Game_SetupEnterBuilding (Save files saves here) */
#define SETUP_ENTER_BUILDING 0xDAAFu /* Game_SetupEnterBuilding: JSR PPU_WaitUntilFlushed */
#define SETUP_EXIT_BUILDING 0xDABEu  /* Game_SetupExitBuilding: Game_ExitBuilding, Screen_ResetForGamePlay, ... RTS */
/* RAM */
#define AREA_CURRENT_AREA   0x0024u
#define AREA_CURRENT_SCREEN 0x0063u
#define AREA_LOADING_SCREEN 0x0064u  /* Area_LoadingScreenIndex: the screen Screen_SetupNew makes current */
#define SCREEN_DEST_PALETTE 0x0065u  /* Screen_DestPaletteOrIndex: the palette it sets */
#define SCREEN_START_POS    0x006Cu  /* Screen_StartPosYX: where it puts the hero, block Y and X */
#define PLAYER_FLAGS        0x00A4u
#define AREAS_DEFAULT_MUSIC 0x03D1u
#define AREA_SAVED_AREA     0x03D5u  /* where Game_ExitBuilding goes: the area, */
#define AREA_SAVED_SCREEN   0x03D6u  /* the screen, */
#define SCREEN_SAVED_PALETTE 0x03D7u /* the palette */
#define PLAYER_SAVED_POS    0x03D8u  /* and the position, block Y and X */
#define AREA_REGION         0x0435u  /* REGION_*: which region's doors and music */
#define FACING_RIGHT        0x40u    /* Player_Flags */
/* Limits of what the game sets there */
#define LAST_REGION  0x05u           /* REGION_EVIL_FORTRESS */
#define LAST_PALETTE 0x1Bu           /* PALETTE_TOWN */
#define LAST_MUSIC   0x10u           /* MUSIC_EVIL_FORTRESS */

/* A way into a zone, as Screen_SetupNew found it. */
typedef struct {
    int8_t zone;                     /* FAX_ZONE_NONE: no checkpoint */
    uint8_t area, screen, start, palette, region, music, facing;
} Checkpoint;

static const Checkpoint none = { FAX_ZONE_NONE, 0, 0, 0, 0, 0, 0, 0 };
static bool enabled;
static Checkpoint checkpoint = { FAX_ZONE_NONE, 0, 0, 0, 0, 0, 0, 0 };
/* Where a death is: between DEATH_FADE and Player_Spawn's JSR Game_SpawnInTemple
 * (DYING), then, coming back at the checkpoint, until Game_SetupEnterBuilding
 * (RESPAWNING). */
enum { ALIVE, DYING, RESPAWNING };
static uint8_t death;
static unsigned recorded, respawns, fallbacks;

static uint8_t q(uint16_t addr) { return cyc_mod_peek(addr); }
static void p(uint16_t addr, uint8_t v) { cyc_mod_poke(addr, v); }

/* A checkpoint the game can load: a zone's own screen, and values the game
 * itself sets. */
static bool sound(const Checkpoint *c) {
    return c->zone != FAX_ZONE_NONE && c->area != FAX_AREA_BUILDINGS && c->zone == fax_zone_at(c->area, c->screen) &&
           c->palette <= LAST_PALETTE && c->region <= LAST_REGION && c->music <= LAST_MUSIC &&
           (c->facing & ~FACING_RIGHT) == 0;
}

/* The checkpoint after a screen loads through `in`. */
static Checkpoint on_load(Checkpoint now, Checkpoint in) {
    if (in.area == FAX_AREA_BUILDINGS) return now;    /* no zone; the way out leads back */
    if (in.zone == FAX_ZONE_NONE) return none;        /* Eolis outside the gate: a new game */
    if (in.zone == now.zone) return now;              /* the same zone: keep the way in */
    return in;
}

/* Whether a death at (area, screen) comes back at the checkpoint. A building
 * is in no zone, so a death there goes to the temple. */
static bool usable(const Checkpoint *c, unsigned area, unsigned screen) {
    return sound(c) && c->zone == fax_zone_at(area, screen);
}

/* Screen_SetupNew: note the way in. */
static int load_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) return 0;   /* Seamless's neighboring rooms */
    Checkpoint in = { FAX_ZONE_NONE, q(AREA_CURRENT_AREA), q(AREA_LOADING_SCREEN), q(SCREEN_START_POS),
                      q(SCREEN_DEST_PALETTE), q(AREA_REGION), q(AREAS_DEFAULT_MUSIC),
                      (uint8_t)(q(PLAYER_FLAGS) & FACING_RIGHT) };
    if (in.area != FAX_AREA_BUILDINGS) in.zone = (int8_t)fax_zone_at(in.area, in.screen);
    Checkpoint next = on_load(checkpoint, in);
    if (memcmp(&next, &checkpoint, sizeof next) && next.zone != FAX_ZONE_NONE) ++recorded;
    checkpoint = next;
    return 0;
}

/* Player_HandleDeath, after the Guru's words: the next Player_Spawn is a death's. */
static int death_hook(uint16_t addr) {
    (void)addr;
    if (!cyc_mod_isolated()) death = DYING;
    return 0;
}

/* Push a return address as JSR does (the address - 1, high byte first). */
static void push_return(uint16_t to) {
    CycModRegs regs;
    cyc_mod_regs(&regs);
    uint16_t r = (uint16_t)(to - 1);
    p((uint16_t)(0x100 | regs.s), (uint8_t)(r >> 8));
    p((uint16_t)(0x100 | (uint8_t)(regs.s - 1)), (uint8_t)r);
    regs.s = (uint8_t)(regs.s - 2);
    cyc_mod_set_regs(&regs);
}
static uint16_t return_address(void) {   /* where the routine at hand returns to */
    CycModRegs regs;
    cyc_mod_regs(&regs);
    return (uint16_t)((q((uint16_t)(0x100 | (uint8_t)(regs.s + 1))) |
                       q((uint16_t)(0x100 | (uint8_t)(regs.s + 2))) << 8) + 1);
}

/* Player_Spawn's JSR Game_SpawnInTemple: a death with a checkpoint for this
 * zone skips the call (as if it returned at once), so Player_Spawn goes on at
 * SPAWNED; anything else is a temple, as in the original. */
static int spawn_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) return 0;
    bool died = death == DYING;
    death = ALIVE;
    if (died && usable(&checkpoint, q(AREA_CURRENT_AREA), q(AREA_CURRENT_SCREEN))) {
        push_return(SPAWNED);
        death = RESPAWNING;
        return 1;
    }
    /* The temple: Game_SpawnInTemple lays out the one at TempleSpawnPoint, which
     * Save files keeps at the Guru last saved with (#43), and the original at
     * the furthest temple. */
    if (died) ++fallbacks;
    checkpoint = none;                    /* the way out of the temple is the next way in */
    return 0;
}

/* Game_SetupEnterBuilding, called from SPAWNED while coming back at the
 * checkpoint: lay it out where Game_ExitBuilding reads the way out, and run
 * Game_SetupExitBuilding instead, which returns to Player_Spawn's JMP
 * Game_MainLoop. */
static int enter_building_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated() || death != RESPAWNING) return 0;
    death = ALIVE;
    if (return_address() != SPAWNED + 3) return 0;   /* not Player_Spawn's call: leave it be */
    const Checkpoint *c = &checkpoint;
    p(AREA_SAVED_AREA, c->area);
    p(AREA_SAVED_SCREEN, c->screen);
    p(SCREEN_SAVED_PALETTE, c->palette);
    p(PLAYER_SAVED_POS, c->start);
    p(AREA_REGION, c->region);
    p(AREAS_DEFAULT_MUSIC, c->music);     /* Game_ExitBuilding plays it */
    p(PLAYER_FLAGS, (uint8_t)((q(PLAYER_FLAGS) & ~FACING_RIGHT) | c->facing));
    push_return(SETUP_EXIT_BUILDING);
    ++respawns;
    return 1;
}

void fax_respawn_checkpoint_pack(uint8_t out[FAX_RESPAWN_CHECKPOINT_LEN]) {
    const Checkpoint *c = &checkpoint;
    const uint8_t bytes[FAX_RESPAWN_CHECKPOINT_LEN] = {
        (uint8_t)c->zone, c->area, c->screen, c->start, c->palette, c->region, c->music, c->facing };
    memcpy(out, bytes, sizeof bytes);
}
static bool unpack(const uint8_t *in, int len, Checkpoint *out) {
    if (len == 0) { *out = none; return true; }
    if (len != FAX_RESPAWN_CHECKPOINT_LEN) return false;
    Checkpoint c = { (int8_t)in[0], in[1], in[2], in[3], in[4], in[5], in[6], in[7] };
    if (c.zone == FAX_ZONE_NONE) { *out = none; return true; }
    if (!sound(&c)) return false;
    *out = c;
    return true;
}
bool fax_respawn_checkpoint_unpack(const uint8_t *in, int len) { return unpack(in, len, &checkpoint); }

/* Save states carry the checkpoint and where a death is (ALIVE, DYING, RESPAWNING). */
enum { VERSION = 1, RECORD = 2 + FAX_RESPAWN_CHECKPOINT_LEN };
static int state_get(uint8_t *buf, int cap) {
    if (cap < RECORD) return -1;
    buf[0] = VERSION;
    fax_respawn_checkpoint_pack(buf + 1);
    buf[1 + FAX_RESPAWN_CHECKPOINT_LEN] = death;
    return RECORD;
}
static int state_validate(const uint8_t *buf, int len) {
    Checkpoint c;
    return len == 0 || (len == RECORD && buf[0] == VERSION && unpack(buf + 1, FAX_RESPAWN_CHECKPOINT_LEN, &c) &&
                        buf[1 + FAX_RESPAWN_CHECKPOINT_LEN] <= RESPAWNING);
}
static int state_set(const uint8_t *buf, int len) {
    if (!state_validate(buf, len)) return 0;
    if (!len) { checkpoint = none; death = ALIVE; return 1; }   /* saved without the feature */
    unpack(buf + 1, FAX_RESPAWN_CHECKPOINT_LEN, &checkpoint);
    death = buf[1 + FAX_RESPAWN_CHECKPOINT_LEN];
    return 1;
}

#ifdef CYC_WITH_SDL
static void respawn_state(int id, const char *line) {
    (void)line;
    char fields[512];
    const FaxZone *z = fax_zone(checkpoint.zone);
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"zone\":%s%s%s,\"area\":%u,\"screen\":%u,\"start\":%u,\"palette\":%u,\"region\":%u,"
             "\"music\":%u,\"facing_right\":%s,\"death\":%u,\"recorded\":%u,\"respawns\":%u,\"fallbacks\":%u",
             enabled ? "true" : "false", z ? "\"" : "", z ? z->name : "null", z ? "\"" : "", checkpoint.area,
             checkpoint.screen, checkpoint.start, checkpoint.palette, checkpoint.region, checkpoint.music,
             checkpoint.facing ? "true" : "false", death, recorded, respawns, fallbacks);
    cyc_tcp_ok(id, fields);
}
void fax_respawn_tcp_setup(void) {
    cyc_tcp_register("respawn_state", "Zone respawn: the checkpoint, and respawns at it or at a temple", respawn_state);
}
#else
void fax_respawn_tcp_setup(void) {}
#endif

static const char *const hook_ids[] = {
    "faxanadu.zone-respawn.load", "faxanadu.zone-respawn.death", "faxanadu.zone-respawn.spawn",
    "faxanadu.zone-respawn.enter-building" };
static void hooks(bool on) {
    for (unsigned i = 0; i < sizeof hook_ids / sizeof hook_ids[0]; ++i) nes_mod_set_function_hook_enabled(hook_ids[i], on);
}
static void deactivate(void) {
    enabled = false;
    death = ALIVE;
    checkpoint = none;
    recorded = respawns = fallbacks = 0;
    hooks(false);
}
static void activate(void) {
    enabled = true;
    hooks(true);
    nes_mod_set_local_only("Zone respawn", 1);
}

NES_MOD_CONSTRUCTOR(register_zone_respawn) {
    if (!nes_mod_register_function_entry_plugin(hook_ids[0], SCREEN_SETUP_NEW, load_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[1], DEATH_FADE, death_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[2], SPAWN_IN_TEMPLE, spawn_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[3], SETUP_ENTER_BUILDING, enter_building_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.zone-respawn", activate) ||
        !nes_mod_register_savestate_hook("faxanadu.zone-respawn", state_get, state_set) ||
        !nes_mod_register_savestate_validator("faxanadu.zone-respawn", state_validate)) abort();
}
