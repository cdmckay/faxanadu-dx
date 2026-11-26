#pragma once
/* Seams (fax_seams.c, #166): slice 0, seams lose nothing, and slice 1,
 * seams are open ground. It registers its own hooks; the host adds its TCP
 * command, and Seamless (fax_world.c) calls the rest. Each room's state is a
 * block of FAX_WORLD_SEAMS_BYTES that Seamless keeps in the room's record. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void fax_seams_tcp_setup(void);

/* A sprite cell a room's actor drew past the room's side, which the NES
 * skips (Sprite_Draw_Standard, Sprite_Draw_FlippedHoriz): x from the room's
 * left edge in pixels (below 0 or 256 and up), y as OAM Y, its tile and
 * attributes as the game would have put them in OAM, and the actor's slot. */
typedef struct { int16_t x; uint8_t y, tile, attr, slot; } FaxSeamsCell;

/* The cells past the room's sides from its last finished sprite loop. */
unsigned fax_seams_cells(const uint8_t *block, const FaxSeamsCell **cells);
/* The actor slot (0-7) that drew OAM entry `oam` (0-63) in that loop, or -1
 * for a cell no actor drew (the hero, his weapon, his spell). */
int fax_seams_oam_slot(const uint8_t *block, unsigned oam);
/* The room whose CHR holds the slot's tiles, when it came through a seam
 * into a room without tiles for it (tiles from home); -1 for its own room. */
int fax_seams_slot_tiles(const uint8_t *block, unsigned slot);

/* Seamless captured the room's RAM: after the live game's sprite loop, after
 * a neighbour's tick, or as its actors started from its list (fresh). */
enum { FAX_SEAMS_FRESH, FAX_SEAMS_LIVE, FAX_SEAMS_TICK };
void fax_seams_captured(uint8_t *block, int how);
/* Between frames, once the live game and the neighbours have run: actors
 * whose origin crossed a seam move to the room they entered; shots wholly
 * out of the view go. */
void fax_seams_step(void);
/* Whether room `room` of Seamless's area is a town room: a screen of a
 * town's zone (fax_zone.c) whose sprite list holds no monster. A seam
 * between a town room and one that isn't is a wall for every mover. */
bool fax_seams_town(int room);
/* Whether a block from a save state is one Seams could have written, for a
 * map of `rooms` rooms. */
bool fax_seams_valid(const uint8_t *block, unsigned rooms);
/* The block as JSON fields for fax_world_room: "awake", "through", "tiles". */
void fax_seams_describe(const uint8_t *block, char *out, size_t size);
