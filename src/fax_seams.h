#pragma once
/* Seams (fax_seams.c, #166): slice 0, seams lose nothing; slice 1, seams
 * are open ground; slice 2, fights across a seam; slice 3, the spell flies
 * on; slice 5, all of it one above the other; slice 6, what a neighbouring
 * room sees of him; slice 7, the fountain's ladder from afar (a room fixup
 * it sets with Seamless). It registers its own hooks
 * and save-state record; the host adds its TCP command, Seamless
 * (fax_world.c) calls the rest, and the features that hit or are hit ask it
 * where a slot is. Each room's state is a block of FAX_WORLD_SEAMS_BYTES that
 * Seamless keeps in the room's record. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void fax_seams_tcp_setup(void);

/* A sprite cell a room's actor drew past the room's side, top or bottom,
 * which the NES skips or hides (Sprite_Draw_Standard,
 * Sprite_Draw_FlippedHoriz): x from the room's left edge in pixels (below 0
 * or 256 and up), y as OAM Y in 16 bits (the status bar's 32 lines above the
 * room's top: below 32 past it, 239 and up past its bottom), its tile and
 * attributes as the game would have put them in OAM, and the actor's slot. */
typedef struct {
    int16_t x, y;
    uint8_t tile, attr, slot;
} FaxSeamsCell;

/* The slot, past the actors' 0-7, that the hero's spell's cells are told by
 * (slice 3): in his room's OAM and its cells past a side. */
enum { FAX_SEAMS_SPELL = 8 };
/* The cells past the room's sides, top and bottom from its last finished
 * sprite loop (his room's spell's too). */
unsigned fax_seams_cells(const uint8_t *block, const FaxSeamsCell **cells);
/* The actor slot (0-7) that drew OAM entry `oam` (0-63) in that loop,
 * FAX_SEAMS_SPELL for his spell's, or -1 for a cell no actor drew (the hero,
 * his weapon). */
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
/* Seamless adopts room `to` as his, from `from`: a spell on keeps flying,
 * its side moved by the rooms' places, unless the crossing is up or down
 * (it ends, CastMagic_Type $FF, as it always did then). */
void fax_seams_adopted(int from, int to);
/* Whether room `room` of Seamless's area is a town room: a screen of a
 * town's zone (fax_zone.c) whose sprite list holds no monster. A seam
 * between a town room and one that isn't is a wall for every mover. */
bool fax_seams_town(int room);
/* ---- slice 2, fights across a seam: for the features that hit or are hit ----
 * Rooms side by side share X's low byte (room 5's x 250 is room 4's x -6),
 * so a slot's X byte alone can't tell which room it is in. */
/* Slot `slot`'s X in 16 bits from the left edge of the room the machine runs:
 * in the live game a visitor's (in the visitors' pass) from the side it came
 * from, a slot let through a side past that side; in a neighbour's tick, that
 * room's slot. */
int fax_seams_x16(unsigned slot);
/* The same for slot `slot` of room `room` (its record, unless the machine
 * runs it). */
int fax_seams_room_x16(int room, unsigned slot);
/* The room whose pixels the live slot's X byte counts (its own for one that
 * has come from or straddles a joined room; a visitor's own): -1 outside
 * the live game or without Seamless. The damage numbers draw there. */
int fax_seams_slot_room(unsigned slot);
/* The live slot's Y in the pixels of that room: a visitor from above or
 * below has its own room's Y, its byte in the live game being his room's
 * (slice 5). */
int fax_seams_slot_y(unsigned slot);
/* The current sprite's box (SpriteBox_Left, as Sprite_GetBounds left it) in
 * 16 bits, as fax_seams_x16. */
int fax_seams_box_left(void);
/* The hero's X in the pixels of the room the machine runs: the live game's,
 * or in a neighbour's tick his true place, when that room is in his row of
 * rooms (false when it is above or below, or there is no view). */
bool fax_seams_hero_x(int *x);
/* In a neighbour's tick, at Sugata's flash's damage (fax_sugata.c): true when
 * the room is joined beside the live one, and then the live game runs that
 * damage for this slot at its next visitors' pass, from the behaviour's own
 * code; till then the flash waits (the caller keeps it at its damage). */
bool fax_seams_flash(void);
/* Whether the visitors' pass is under way (a frame ended inside it):
 * Seamless's step waits for it to end. */
bool fax_seams_visiting(void);
/* The host's frame end (first) and beginning (last): a frame that ends
 * inside the pass shows the live slots as the game has them between frames,
 * the pass's own (the room's hidden, visitors in its free slots) kept until
 * the next frame begins. */
void fax_seams_frame_end(void);
void fax_seams_frame_begin(void);

/* Whether a block from a save state is one Seams could have written, for a
 * map of `rooms` rooms. */
bool fax_seams_valid(const uint8_t *block, unsigned rooms);
/* The block as JSON fields for fax_world_room: "awake", "through", "tiles". */
void fax_seams_describe(const uint8_t *block, char *out, size_t size);
