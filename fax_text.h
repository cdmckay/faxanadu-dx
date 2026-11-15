#pragma once
/* The game's dialogue font and text box, drawn over a presented picture
 * (fax_text.c). The glyphs are the text box's own (bank 13, TILE_GLYPHS_START),
 * the frame is its pattern tiles, and the colors are the HUD and text box
 * palette the game chose for the screen (UI_AttributeDataIndex), so the text
 * looks as the game's would there. Call from a present callback: the colors
 * and tiles are the frame's. Shared by zone names and speaker names. */
#include <stdbool.h>
#include <stdint.h>

enum { FAX_TEXT_CELL = 8 };   /* a character or a frame tile is 8x8 pixels */

/* Read the glyphs from the loaded cart, once; false if they are not there. */
bool fax_text_load(void);
bool fax_text_loaded(void);
void fax_text_unload(void);

/* Width in pixels of a line: 8 per character. */
int fax_text_width(const char *s);

/* The text box's frame and black inside around `cols` x `rows` characters,
 * with its top-left corner at pixel (x, y): (cols + 2) x (rows + 2) cells.
 * Blended over the ARGB canvas (w x h) at alpha 0-255, clipped to it. */
void fax_text_box(uint32_t *px, int w, int h, int x, int y, int cols, int rows, unsigned alpha);

/* A line of text (characters $20-$7F) with its top-left at (x, y), in the
 * text box's text color, blended at alpha 0-255. */
void fax_text_draw(uint32_t *px, int w, int h, int x, int y, const char *s, unsigned alpha);
