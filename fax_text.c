/* The game's dialogue font and text box, drawn at present time. USA 57dd23d1,
 * chipx86 byte-exact disassembly. TextBox_WriteChar copies a character's
 * glyph from bank 13 (TILE_GLYPHS_START, 8 bytes a character from $20, one bit
 * a pixel) into both bit planes of a pattern tile, so text is color 3 of its
 * palette. TextBox_Open frames a box with background tiles 1-6 (corners, edges)
 * around tile 0, and the box takes the palette in UI_AttributeDataIndex
 * (Screen_LoadUIPalette sets it from the screen's palette). This draws the same
 * pixels onto a presented ARGB picture, with the palette and pattern tiles the
 * frame was drawn with, blended for fading. It never touches the machine.
 * See docs/GAME_INTERNALS.md, "The text box". */
#include "fax_text.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include <stdlib.h>
#include <string.h>

/* Bank 13 */
#define TILE_GLYPHS_START 0x8000u   /* TILE_GLYPH_SPACE, then one glyph per character to $7F */
/* RAM */
#define UI_ATTRIBUTE_DATA_INDEX 0x038Du   /* the HUD and text box palette, 0-3 */

enum { GLYPH_BANK = 13, BANK_SIZE = 16384, FIRST = 0x20, GLYPHS = 0x60 };
/* TextBox_Open's frame tiles */
enum { TILE_INSIDE = 0, TILE_TOP_LEFT = 1, TILE_TOP = 2, TILE_TOP_RIGHT = 3, TILE_SIDE = 4,
       TILE_BOTTOM_LEFT = 5, TILE_BOTTOM_RIGHT = 6 };
#define TEXT_BOX_LINE 64   /* a playfield line, to find the background pattern table boxes are drawn from */

static uint8_t glyphs[GLYPHS][8];
static bool loaded;

bool fax_text_load(void) {
    if (loaded) return true;
    size_t need = cyc_mod_copy_cart_payload(NULL, 0);
    if (need < (size_t)(GLYPH_BANK + 1) * BANK_SIZE) return false;
    uint8_t *payload = malloc(need);
    if (!payload) return false;
    bool ok = cyc_mod_copy_cart_payload(payload, need) == need;
    if (ok) memcpy(glyphs, payload + GLYPH_BANK * BANK_SIZE + (TILE_GLYPHS_START - 0x8000u), sizeof glyphs);
    free(payload);
    /* The space is blank and every letter is not. */
    static const uint8_t blank[8];
    ok = ok && !memcmp(glyphs[0], blank, 8);
    for (int c = 'A'; ok && c <= 'Z'; ++c) ok = memcmp(glyphs[c - FIRST], blank, 8) != 0;
    loaded = ok;
    return ok;
}
bool fax_text_loaded(void) { return loaded; }
void fax_text_unload(void) { loaded = false; }

int fax_text_width(const char *s) { return (int)strlen(s) * FAX_TEXT_CELL; }

static uint32_t blend(uint32_t under, uint32_t over, unsigned alpha) {
    if (alpha >= 255) return over;
    uint32_t out = 0xFF000000u;
    for (int shift = 0; shift < 24; shift += 8) {
        unsigned u = under >> shift & 0xFF, o = over >> shift & 0xFF;
        out |= (uint32_t)((o * alpha + u * (255 - alpha) + 127) / 255) << shift;
    }
    return out;
}
static void put(uint32_t *px, int w, int h, int x, int y, uint32_t color, unsigned alpha) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    uint32_t *p = px + (size_t)y * (size_t)w + (size_t)x;
    *p = blend(*p, color, alpha);
}
static unsigned ui_palette(void) { return cyc_mod_peek(UI_ATTRIBUTE_DATA_INDEX) & 3u; }

/* A background tile as the PPU draws it, every pixel opaque (0 is the
 * universal background color). */
static void tile(uint32_t *px, int w, int h, int x, int y, unsigned t, unsigned alpha) {
    uint16_t pattern = (uint16_t)(cyc_render_line_bg_table(TEXT_BOX_LINE) + t * 16u);
    unsigned palette = ui_palette();
    for (int r = 0; r < 8; ++r) {
        uint8_t lo = cyc_render_chr((uint16_t)(pattern + r)), hi = cyc_render_chr((uint16_t)(pattern + r + 8));
        for (int c = 0; c < 8; ++c) {
            unsigned v = (lo >> (7 - c) & 1u) | (hi >> (7 - c) & 1u) << 1;
            put(px, w, h, x + c, y + r, cyc_render_color(v ? (int)(palette * 4 + v) : 0), alpha);
        }
    }
}

void fax_text_box(uint32_t *px, int w, int h, int x, int y, int cols, int rows, unsigned alpha) {
    for (int r = 0; r < rows + 2; ++r)
        for (int c = 0; c < cols + 2; ++c) {
            bool top = r == 0, bottom = r == rows + 1, left = c == 0, right = c == cols + 1;
            unsigned t = top ? (left ? TILE_TOP_LEFT : right ? TILE_TOP_RIGHT : TILE_TOP)
                       : bottom ? (left ? TILE_BOTTOM_LEFT : right ? TILE_BOTTOM_RIGHT : TILE_TOP)
                       : left || right ? TILE_SIDE : TILE_INSIDE;
            tile(px, w, h, x + c * FAX_TEXT_CELL, y + r * FAX_TEXT_CELL, t, alpha);
        }
}

void fax_text_draw(uint32_t *px, int w, int h, int x, int y, const char *s, unsigned alpha) {
    if (!loaded) return;
    uint32_t color = cyc_render_color((int)(ui_palette() * 4 + 3));
    for (; *s; ++s, x += FAX_TEXT_CELL) {
        unsigned ch = (unsigned char)*s;
        if (ch < FIRST || ch >= FIRST + GLYPHS) continue;
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c)
                if (glyphs[ch - FIRST][r] >> (7 - c) & 1) put(px, w, h, x + c, y + r, color, alpha);
    }
}
