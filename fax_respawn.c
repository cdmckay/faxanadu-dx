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
 *   the hook writes the checkpoint there and returns into it, then into
 *   Game_MainLoop, instead of the temple.
 * - The fallback is the original: no checkpoint, or one for another zone (he
 *   died in a building), lays out the furthest temple. Any temple spawn (the
 *   fallback, a mantra, a Guru travel journey) forgets the checkpoint, so the
 *   way out of the temple becomes the next one. Mantras are unchanged.
 *
 * The checkpoint lives for the session and in this feature's save-state
 * record. See docs/GAME_INTERNALS.md, "Zone respawn". */
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
#define DEATH_FADE         0xD9A0u   /* Player_HandleDeath's @_afterSetExpGoldFarJump: JSR Screen_FadeToBlack,
                                        after the death script and Player_SetInitialExpAndGold */
#define SPAWN_IN_TEMPLE    0xDB1Du   /* Player_Spawn's JSR Game_SpawnInTemple; then JSR Game_SetupEnterBuilding,
                                        JMP Game_MainLoop */
#define SETUP_EXIT_BUILDING 0xDABEu  /* Game_SetupExitBuilding: Game_ExitBuilding, Screen_ResetForGamePlay, ... RTS */
#define MAIN_LOOP          0xDB45u   /* Game_MainLoop: LDX #$FF, TXS */
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
static bool dying;                   /* the death path is between DEATH_FADE and Player_Spawn */
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
    if (cyc_mod_isolated()) return 0;   /* Seamless's neighbouring rooms */
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
    if (!cyc_mod_isolated()) dying = true;
    return 0;
}

/* Lay out the checkpoint where Game_ExitBuilding reads the way out, and return
 * into Game_SetupExitBuilding and then Game_MainLoop on a fresh stack. */
static void respawn(const Checkpoint *c) {
    p(AREA_SAVED_AREA, c->area);
    p(AREA_SAVED_SCREEN, c->screen);
    p(SCREEN_SAVED_PALETTE, c->palette);
    p(PLAYER_SAVED_POS, c->start);
    p(AREA_REGION, c->region);
    p(AREAS_DEFAULT_MUSIC, c->music);     /* Game_ExitBuilding plays it */
    p(PLAYER_FLAGS, (uint8_t)((q(PLAYER_FLAGS) & ~FACING_RIGHT) | c->facing));
    static const uint16_t path[] = { SETUP_EXIT_BUILDING, MAIN_LOOP };
    uint16_t sp = 0x1FF;
    for (int i = 1; i >= 0; --i) {
        uint16_t r = (uint16_t)(path[i] - 1);
        p(sp--, (uint8_t)(r >> 8));
        p(sp--, (uint8_t)r);
    }
    CycModRegs regs;
    cyc_mod_regs(&regs);
    regs.s = (uint8_t)sp;
    cyc_mod_set_regs(&regs);
}

/* Player_Spawn's JSR Game_SpawnInTemple: a death with a checkpoint for this
 * zone comes back there; anything else is a temple, as in the original. */
static int spawn_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) return 0;
    bool death = dying;
    dying = false;
    if (death && usable(&checkpoint, q(AREA_CURRENT_AREA), q(AREA_CURRENT_SCREEN))) {
        respawn(&checkpoint);
        ++respawns;
        return 1;
    }
    /* The temple: Game_SpawnInTemple lays out the furthest one (TempleSpawnPoint).
     * Respawning at the most recent Guru instead (#43) would choose it here. */
    if (death) ++fallbacks;
    checkpoint = none;                    /* the way out of the temple is the next way in */
    return 0;
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

/* Save states carry the checkpoint and whether a death is on its way to Player_Spawn. */
enum { VERSION = 1, RECORD = 2 + FAX_RESPAWN_CHECKPOINT_LEN };
static int state_get(uint8_t *buf, int cap) {
    if (cap < RECORD) return -1;
    buf[0] = VERSION;
    fax_respawn_checkpoint_pack(buf + 1);
    buf[1 + FAX_RESPAWN_CHECKPOINT_LEN] = dying;
    return RECORD;
}
static int state_validate(const uint8_t *buf, int len) {
    Checkpoint c;
    return len == 0 || (len == RECORD && buf[0] == VERSION && unpack(buf + 1, FAX_RESPAWN_CHECKPOINT_LEN, &c) &&
                        buf[1 + FAX_RESPAWN_CHECKPOINT_LEN] <= 1);
}
static int state_set(const uint8_t *buf, int len) {
    if (!state_validate(buf, len)) return 0;
    if (!len) { checkpoint = none; dying = false; return 1; }   /* saved without the feature */
    unpack(buf + 1, FAX_RESPAWN_CHECKPOINT_LEN, &checkpoint);
    dying = buf[1 + FAX_RESPAWN_CHECKPOINT_LEN];
    return 1;
}

#ifdef CYC_WITH_SDL
static void respawn_state(int id, const char *line) {
    (void)line;
    char fields[512];
    const FaxZone *z = fax_zone(checkpoint.zone);
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"zone\":%s%s%s,\"area\":%u,\"screen\":%u,\"start\":%u,\"palette\":%u,\"region\":%u,"
             "\"music\":%u,\"facing_right\":%s,\"dying\":%s,\"recorded\":%u,\"respawns\":%u,\"fallbacks\":%u",
             enabled ? "true" : "false", z ? "\"" : "", z ? z->name : "null", z ? "\"" : "", checkpoint.area,
             checkpoint.screen, checkpoint.start, checkpoint.palette, checkpoint.region, checkpoint.music,
             checkpoint.facing ? "true" : "false", dying ? "true" : "false", recorded, respawns, fallbacks);
    cyc_tcp_ok(id, fields);
}
void fax_respawn_tcp_setup(void) {
    cyc_tcp_register("respawn_state", "Zone respawn: the checkpoint, and respawns at it or at a temple", respawn_state);
}
#else
void fax_respawn_tcp_setup(void) {}
#endif

static const char *const hook_ids[] = {
    "faxanadu.zone-respawn.load", "faxanadu.zone-respawn.death", "faxanadu.zone-respawn.spawn" };
static void hooks(bool on) {
    for (unsigned i = 0; i < sizeof hook_ids / sizeof hook_ids[0]; ++i) nes_mod_set_function_hook_enabled(hook_ids[i], on);
}
static void deactivate(void) {
    enabled = dying = false;
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
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.zone-respawn", activate) ||
        !nes_mod_register_savestate_hook("faxanadu.zone-respawn", state_get, state_set) ||
        !nes_mod_register_savestate_validator("faxanadu.zone-respawn", state_validate)) abort();
}
