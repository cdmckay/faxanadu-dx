#pragma once
/* The game's dialogue font and text box, drawn over a presented picture
 * (fax_text.c). The glyphs are the text box's own (bank 13, TILE_GLYPHS_START),
 * the frame is its pattern tiles, and the colors are the HUD and text box
 * palette the game chose for the screen (UI_AttributeDataIndex), so the text
 * looks as the game's would there. Call from a present callback: the colors
 * and tiles are the frame's. Shared by zone names and speaker names. */
#include <stdbool.h>
#include <stdint.h>

enum { FAX_TEXT_CELL = 8 }; /* a character or a frame tile is 8x8 pixels */

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

/* The same frame with its tiles from ROM rather than the pattern table the
 * picture was drawn with, in the colors given for pixel values 0-3: for a
 * screen whose pattern tables hold something else, like the title. */
void fax_text_box_rom(uint32_t *px, int w, int h, int x, int y, int cols, int rows, const uint32_t colors[4],
                      unsigned alpha);
/* A line of text in a given ARGB color. */
void fax_text_draw_color(uint32_t *px, int w, int h, int x, int y, const char *s, uint32_t color, unsigned alpha);
/* A character's glyph: 8 rows, the left pixel the top bit; NULL if the font
 * is not loaded or it has none ($20-$7F). For marks drawn in the font: the
 * quest log's "!" and "?". */
const uint8_t *fax_text_glyph(char ch);

/* ---- the game's own boxes, in what the last frame showed ---- */
enum { FAX_TEXT_NO_TILE = 0x100 };
/* The background tile the last frame showed in cell (col, row) of the
 * 256 x 240 picture, through that line's scroll, in the nametable it fetched
 * from; FAX_TEXT_NO_TILE off the picture or the nametables. */
unsigned fax_text_shown_tile(int col, int row);
/* A text box or menu of the game's is open: TextBox_Closed reads 0 and a
 * box's frame shows below the HUD. TextBox_Closed alone is not enough: every
 * screen the game draws or scrolls to leaves one of its tiles there, and on
 * some screens that tile is 0 (docs/GAME_INTERNALS.md, "The text box"). For
 * the features that hold or hide behind boxes: zone names, Slain by, damage
 * numbers. */
bool fax_text_box_showing(void);
/* The status bar shows in the last frame: the "P:" UI_DrawHUDSprites puts at
 * PPU $2061 (tiles $1F and $0A, from UI_STATUS_TILES), with the background
 * on in that row. Not on the title, the password screen or the ending, nor
 * while the game has the screen off to load one. For what goes beside the
 * status bar: Seamless's layout and the Elixir left of it
 * (fax_charges.c). */
bool fax_text_hud_showing(void);
