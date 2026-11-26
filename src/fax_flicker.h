#pragma once
/* No sprite flicker (fax_flicker.c): the order Seamless paints a frame's
 * sprites in, so the first the game drew is in front, whichever half of the
 * shadow OAM it landed in. */
#include <stdbool.h>
#include <stdint.h>

enum { FAX_FLICKER_SPRITES = 63 };   /* OAM entries 1-63; 0 is the status bar's sprite 0 */

/* Fill `order` with OAM indices 1-63, each once, back to front: paint them in
 * this order and the last painted is in front. `start_slot_range` is the
 * frame's Sprites_StartSlotRange ($1C, $00 or $80), read from the same RAM as
 * the OAM. With the feature off, the NES's own priority: 63 down to 1, the
 * lower index in front. Returns the count, FAX_FLICKER_SPRITES. */
unsigned fax_flicker_paint_order(uint8_t start_slot_range, uint8_t order[FAX_FLICKER_SPRITES]);

/* At a frame's end: the shadow OAM is what the NMI is about to copy to the
 * PPU, a frame the game finished drawing. False on a frame the game lagged
 * (the NMI keeps the PPU's last sprites), so Seamless keeps its last capture
 * rather than one half drawn. With the feature off, always true. Seamless
 * asks once a frame of play; each false counts in the TCP command's `kept`. */
bool fax_flicker_frame_drawn(void);

/* TCP no_flicker_state: whether on, and `kept`. */
void fax_flicker_tcp_setup(void);
