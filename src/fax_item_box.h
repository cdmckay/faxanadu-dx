#pragma once
/* Item box (fax_item_box.c): while the item in the status bar's item box is
 * running (the Wing Boots flying, the Hour Glass freezing), the box's [ and ]
 * turn gold, and go back to white the frame the run ends. Only the item the
 * box shows lights them. Drawn at present time, over the status bar band
 * wherever Seamless (fax_world.c) put it. It registers itself; the host calls
 * these. */
#include <stdbool.h>
#include <stdint.h>

/* The picture to present: `picture` (the picture so far, or if NULL the
 * engine's own) with the box's brackets gold, when its item is running and
 * the status bar shows; otherwise `picture` itself. */
const uint32_t *fax_item_box_present(const uint32_t *picture, int *w, int *h);
void fax_item_box_tcp_setup(void);

/* ---- the rules (exposed for the unit tests) ---- */

/* Where the box's brackets are in the 256-wide picture: its [ tile at x
 * 216-223 and its ] tile at 240-247 (UI_STATUS_TILES' $2C over $2D, and $2E
 * over $2F, at PPU $205B and $205E), on lines 16-31, the item's picture
 * between them at 224-239. Each bracket is 22 pixels of the status bar's
 * white (its palette's color 3): an upright (x 220 or 243) on lines 17-30,
 * with arms three wide on lines 17-18 and 29-30. Lit, those pixels, and only
 * those, are gold. */
enum {
    FAX_ITEM_BOX_OPEN_X = 216,
    FAX_ITEM_BOX_CLOSE_X = 240,
    FAX_ITEM_BOX_TILE = 8, /* the [ and ] tiles, 8 wide */
    FAX_ITEM_BOX_LINE = 16,
    FAX_ITEM_BOX_LINES = 16,  /* the box's lines: 16-31 */
    FAX_ITEM_BOX_PIXELS = 44, /* the brackets' white pixels */
};

/* The box's item is running: `selected` (SelectedItem, the item the box
 * shows) is the Wing Boots with `status_flag` (Player_StatusFlag) $80, or the
 * Hour Glass with `hour_glass` (DurationHourGlass) below $80. Any other item,
 * or none, never. */
bool fax_item_box_running(uint8_t selected, uint8_t status_flag, uint8_t hour_glass);
/* The brackets' NES color, for a status bar whose text and brackets are
 * `white` (color 3 of its palette): gold ($27) while that is white ($30, or
 * $20), $17 for light grey ($10) and $07 for grey ($00), as a fade-out takes
 * the white down a row at a time, and black ($0F, nothing to draw) for black. */
uint8_t fax_item_box_gold(uint8_t white);
/* Pixel (x, y) of the 256-wide picture is in the [ or ] tile. */
bool fax_item_box_in_brackets(int x, int y);
