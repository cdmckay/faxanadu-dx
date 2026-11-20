/* Fair entrances. USA 57dd23d1, chipx86 byte-exact disassembly.
 * A few enemies are placed where the hero comes onto their screen (a door, a
 * building's way out, a temple's), so they hit him before the player can
 * react: on his first update, or within a few frames, whatever he presses.
 * This moves just those, a little way off; every other enemy stays where the
 * original puts it (#44). The spots and the measurements behind each move are
 * in docs/GAME_INTERNALS.md, "Fair entrances".
 *
 * Each screen's sprites come from bank 11's placement list, one (entity, YX)
 * pair at a time: Screen_LoadSpriteInfo reads a pair into CurrentSprite_Entity,
 * CurrentSprite_YPos and CurrentSprite_XPos, then calls
 * Sprites_PopulateNextAvailableSprite, which copies them into a free slot.
 * Every way onto a screen loads it this way (Screen_SetupSprites for a door, a
 * building's or a temple's way out, a region's door and a town's edge; the
 * main loop for a scroll), so a hook at that routine's entry rewrites the
 * position of a listed enemy before anything reads it. No ROM is changed and
 * no read override is used: the placement byte is only ever read here.
 *
 * Seamless (fax_world.c) loads a neighbouring room's sprites with the same
 * routine on an isolated machine, where hooks do not run, so it calls
 * fax_entrances_room_loaded right after: that moves listed enemies still at
 * their placement in that machine's sprite slots.
 *
 * The positions live in the game's own sprite RAM, so a save state keeps
 * them and the feature has no state of its own. */
#include "fax_entrances.h"
#include "cyc_mod.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#ifdef CYC_WITH_SDL
#include "cyc_tcp.h"
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Fixed bank */
#define POPULATE_SPRITE     0xC205u   /* Sprites_PopulateNextAvailableSprite: LDX #$07, STX CurrentSpriteIndex */
/* RAM */
#define AREA_CURRENT_AREA   0x0024u
#define AREA_CURRENT_SCREEN 0x0063u
#define SPRITE_X            0x00BAu   /* CurrentSprites_XPos_Full, 8 slots */
#define SPRITE_Y            0x00C2u   /* CurrentSprites_YPos, 8 slots */
#define SPRITE_ENTITY       0x02CCu   /* CurrentSprites_Entities, 8 slots, $FF free */
#define CURRENT_X           0x0389u   /* CurrentSprite_XPos: block X << 4, from the placement's low nibble */
#define CURRENT_Y           0x038Au   /* CurrentSprite_YPos: the placement's high nibble */
#define CURRENT_ENTITY      0x038Bu   /* CurrentSprite_Entity */
#define SLOTS 8

/* One move: the enemy (entity) bank 11 places at block `from` (YX, as the
 * placement list stores it) on area:screen starts at block `to` instead. */
typedef struct { uint8_t area, screen, entity, from, to; } FaxEntranceMove;

/* Entities, from chipx86's DEFS.asm */
enum { RAIDEN = 0x04, ZOMBIE = 0x06, SUGATA = 0x18, GIANT_STRIDER = 0x1E, SIR_GAWAINE = 0x1F,
       MASKMAN = 0x20, WOLFMAN = 0x21, IKEDA = 0x26, LAMPREY = 0x28 };
enum { TRUNK = 1, MIST = 2, BRANCH = 5, DARTMOOR = 6 };

static const FaxEntranceMove moves[] = {
    { MIST, 0x06, SIR_GAWAINE, 0x89, 0x9D },
};
enum { MOVES = sizeof moves / sizeof moves[0] };

static bool enabled;
static unsigned moved;                       /* enemies the hook moved since the feature came on */
static unsigned seamless;                    /* enemies moved in Seamless's isolated room loads */
static int last = -1;                        /* the last move used, an index into moves */

static uint8_t peek(uint16_t a) { return cyc_mod_peek(a); }
static void poke(uint16_t a, uint8_t v) { cyc_mod_poke(a, v); }

static int find(uint8_t area, uint8_t screen, uint8_t entity, uint8_t yx) {
    for (int i = 0; i < MOVES; ++i)
        if (moves[i].area == area && moves[i].screen == screen && moves[i].entity == entity && moves[i].from == yx)
            return i;
    return -1;
}

/* Sprites_PopulateNextAvailableSprite's entry: the next placement is in
 * CurrentSprite_*, and nothing has read it yet. The routine's first
 * instruction loads X, so no register needs keeping. */
static int populate_hook(uint16_t addr) {
    (void)addr;
    uint8_t x = peek(CURRENT_X), y = peek(CURRENT_Y);
    int i = find(peek(AREA_CURRENT_AREA), peek(AREA_CURRENT_SCREEN), peek(CURRENT_ENTITY),
                 (uint8_t)(y | x >> 4));
    if (i < 0) return 0;
    poke(CURRENT_X, (uint8_t)(moves[i].to << 4));
    poke(CURRENT_Y, (uint8_t)(moves[i].to & 0xF0));
    if (!cyc_mod_isolated()) { ++moved; last = i; }
    return 0;
}

unsigned fax_entrances_room_loaded(void) {
    if (!enabled) return 0;
    unsigned n = 0;
    uint8_t area = peek(AREA_CURRENT_AREA), screen = peek(AREA_CURRENT_SCREEN);
    for (unsigned k = 0; k < SLOTS; ++k) {
        uint8_t x = peek((uint16_t)(SPRITE_X + k)), y = peek((uint16_t)(SPRITE_Y + k));
        if (x & 0x0F || y & 0x0F) continue;              /* placements are on whole blocks */
        int i = find(area, screen, peek((uint16_t)(SPRITE_ENTITY + k)), (uint8_t)(y | x >> 4));
        if (i < 0) continue;
        poke((uint16_t)(SPRITE_X + k), (uint8_t)(moves[i].to << 4));
        poke((uint16_t)(SPRITE_Y + k), (uint8_t)(moves[i].to & 0xF0));
        ++n;
    }
    seamless += n;
    return n;
}

static const char *const hook_id = "faxanadu.fair-entrances.populate";
static void deactivate(void) {
    enabled = false;
    moved = seamless = 0;
    last = -1;
    nes_mod_set_function_hook_enabled(hook_id, 0);
}
static void activate(void) {
    enabled = true;
    nes_mod_set_function_hook_enabled(hook_id, 1);
    nes_mod_set_local_only("Fair entrances", 1);
}

#ifdef CYC_WITH_SDL
static void entrances_state(int id, const char *line) {
    (void)line;
    char fields[240];
    int n = snprintf(fields, sizeof fields, "\"enabled\":%s,\"moved\":%u,\"seamless\":%u,\"moves\":%d",
                     enabled ? "true" : "false", moved, seamless, (int)MOVES);
    if (last >= 0 && n > 0 && (size_t)n < sizeof fields)
        snprintf(fields + n, sizeof fields - (size_t)n,
                 ",\"last\":{\"area\":%u,\"screen\":%u,\"entity\":%u,\"from\":%u,\"to\":%u}",
                 moves[last].area, moves[last].screen, moves[last].entity, moves[last].from, moves[last].to);
    cyc_tcp_ok(id, fields);
}
void fax_entrances_tcp_setup(void) {
    cyc_tcp_register("entrances_state", "Fair entrances: enemies moved off the ways onto their screens",
                     entrances_state);
}
#else
void fax_entrances_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_fair_entrances) {
    if (!nes_mod_register_function_entry_plugin(hook_id, POPULATE_SPRITE, populate_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.fair-entrances", activate)) abort();
}
