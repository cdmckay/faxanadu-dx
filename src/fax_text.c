/* The game's dialogue font and text box, drawn at present time. USA 57dd23d1,
 * chipx86 byte-exact disassembly. TextBox_WriteChar copies a character's
 * glyph from bank 13 (TILE_GLYPHS_START, 8 bytes a character from $20, one bit
 * a pixel) into both bit planes of a pattern tile, so text is color 3 of its
 * palette. TextBox_Open frames a box with background tiles 1-6 (corners, edges)
 * around tile 0, and the box takes the palette in UI_AttributeDataIndex
 * (Screen_LoadUIPalette sets it from the screen's palette). This draws the same
 * pixels onto a presented ARGB picture, with the palette and pattern tiles the
 * frame was drawn with, blended for fading. It never touches the machine.
 *
 * It also says whether one of the game's boxes is open, by what the last
 * frame showed (fax_text_box_showing). TextBox_Closed ($020C) is 0 while a box
 * or the menu is open, but Screen_LoadBlocksHoriz buffers each column of tiles
 * it loads at $0200-$021D, over the text box's variables, so once a screen has
 * been drawn or scrolled sideways it holds one of the screen's tiles, which
 * can be 0 with no box up (Forepaw's screen 2). A box counts once its frame's
 * top-left corner, then its top edge, shows below the HUD: TextBox_Open draws
 * the top row first, and TextBox_Close sets TextBox_Closed before it clears
 * anything, so this agrees with the picture as a box comes and goes. Any
 * box's corner counts, not only the one at TextBox_X and _Y: those name a
 * menu's submenu a frame before its frame shows, over the menu.
 * See docs/GAME_INTERNALS.md, "The text box". */
#include "fax_text.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include <stdlib.h>
#include <string.h>

/* Bank 13 */
#define TILE_GLYPHS_START 0x8000u   /* TILE_GLYPH_SPACE, then one glyph per character to $7F */
/* RAM */
#define TEXT_BOX_CLOSED         0x020Cu   /* TextBox_Closed: 0 while a box or the menu is open, or a tile */
#define UI_ATTRIBUTE_DATA_INDEX 0x038Du   /* the HUD and text box palette, 0-3 */

enum { GLYPH_BANK = 13, BANK_SIZE = 16384, FIRST = 0x20, GLYPHS = 0x60 };
/* TextBox_Open's frame tiles */
enum { TILE_INSIDE = 0, TILE_TOP_LEFT = 1, TILE_TOP = 2, TILE_TOP_RIGHT = 3, TILE_SIDE = 4,
       TILE_BOTTOM_LEFT = 5, TILE_BOTTOM_RIGHT = 6 };
#define TEXT_BOX_LINE 64   /* a playfield line, to find the background pattern table boxes are drawn from */
enum { TEXT_COLS = 32, TEXT_ROWS = 30, TEXT_HUD_ROWS = 4 };   /* the picture in cells; the HUD's rows */
/* UI_DrawHUDSprites' "P:" at PPU $2061 (UI_STATUS_SYMBOL_PPU_ADDR_L), from UI_STATUS_TILES */
enum { HUD_P_COL = 1, HUD_P_ROW = 3, TILE_HUD_P = 0x1F, TILE_HUD_COLON = 0x0A };
#define PPUMASK_BACKGROUND 0x08u

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

/* The frame's tiles as the ROM holds them, for screens whose pattern tables
 * hold something else (the title screen): UI_DrawHUDSprites copies 60 tiles from
 * bank 10, HUD_TILES_START ($8140), to the playfield's pattern table at $1000,
 * so tile t is at $8140 + t * 16 there. */
#define HUD_TILES_START 0x8140u
enum { HUD_BANK = 10, FRAME_TILES = 7 };
static uint8_t rom_frame[FRAME_TILES][16];
static bool rom_frame_loaded;

static bool load_rom_frame(void) {
    if (rom_frame_loaded) return true;
    size_t need = cyc_mod_copy_cart_payload(NULL, 0);
    if (need < (size_t)(HUD_BANK + 1) * BANK_SIZE) return false;
    uint8_t *payload = malloc(need);
    if (!payload) return false;
    bool ok = cyc_mod_copy_cart_payload(payload, need) == need;
    if (ok) memcpy(rom_frame, payload + HUD_BANK * BANK_SIZE + (HUD_TILES_START - 0x8000u), sizeof rom_frame);
    free(payload);
    rom_frame_loaded = ok;
    return ok;
}

void fax_text_box_rom(uint32_t *px, int w, int h, int x, int y, int cols, int rows, const uint32_t colors[4],
                      unsigned alpha) {
    if (!load_rom_frame()) return;
    for (int r = 0; r < rows + 2; ++r)
        for (int c = 0; c < cols + 2; ++c) {
            bool top = r == 0, bottom = r == rows + 1, left = c == 0, right = c == cols + 1;
            unsigned t = top ? (left ? TILE_TOP_LEFT : right ? TILE_TOP_RIGHT : TILE_TOP)
                       : bottom ? (left ? TILE_BOTTOM_LEFT : right ? TILE_BOTTOM_RIGHT : TILE_TOP)
                       : left || right ? TILE_SIDE : TILE_INSIDE;
            for (int py = 0; py < 8; ++py) {
                uint8_t lo = rom_frame[t][py], hi = rom_frame[t][py + 8];
                for (int pc = 0; pc < 8; ++pc) {
                    unsigned v = (lo >> (7 - pc) & 1u) | (hi >> (7 - pc) & 1u) << 1;
                    put(px, w, h, x + c * FAX_TEXT_CELL + pc, y + r * FAX_TEXT_CELL + py, colors[v], alpha);
                }
            }
        }
}

const uint8_t *fax_text_glyph(char ch) {
    unsigned c = (unsigned char)ch;
    return loaded && c >= FIRST && c < FIRST + GLYPHS ? glyphs[c - FIRST] : NULL;
}

void fax_text_draw_color(uint32_t *px, int w, int h, int x, int y, const char *s, uint32_t color, unsigned alpha) {
    if (!loaded) return;
    for (; *s; ++s, x += FAX_TEXT_CELL) {
        unsigned ch = (unsigned char)*s;
        if (ch < FIRST || ch >= FIRST + GLYPHS) continue;
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c)
                if (glyphs[ch - FIRST][r] >> (7 - c) & 1) put(px, w, h, x + c, y + r, color, alpha);
    }
}

/* ---- the game's own boxes, in what the last frame showed ---- */

unsigned fax_text_shown_tile(int col, int row) {
    if (col < 0 || col >= TEXT_COLS || row < 0 || row >= TEXT_ROWS) return FAX_TEXT_NO_TILE;
    int line = row * FAX_TEXT_CELL + FAX_TEXT_CELL / 2;
    int x = (cyc_render_line_scroll_x(line) + col * FAX_TEXT_CELL + FAX_TEXT_CELL / 2) & 511;
    int y = cyc_render_line_scroll_y(line);
    if (y < 0 || y >= 480) return FAX_TEXT_NO_TILE;
    uint16_t table = (uint16_t)(0x2000u + (x >= 256 ? 0x400u : 0) + (y >= 240 ? 0x800u : 0));
    return cyc_render_nametable((uint16_t)(table + (y % 240) / FAX_TEXT_CELL * 32 + (x & 255) / FAX_TEXT_CELL));
}

/* Below the HUD the screen shows tile 0 and the area's tiles, which
 * Area_LoadTiles puts from $1800 (tile $80) on, so a frame's corner followed
 * by its top edge (or, two cells wide, its other corner) is a box's. */
bool fax_text_box_showing(void) {
    if (cyc_mod_peek(TEXT_BOX_CLOSED)) return false;
    for (int row = TEXT_HUD_ROWS; row < TEXT_ROWS; ++row)
        for (int col = 0; col + 1 < TEXT_COLS; ++col) {
            if (fax_text_shown_tile(col, row) != TILE_TOP_LEFT) continue;
            unsigned next = fax_text_shown_tile(col + 1, row);
            if (next == TILE_TOP || next == TILE_TOP_RIGHT) return true;
        }
    return false;
}

bool fax_text_hud_showing(void) {
    int line = HUD_P_ROW * FAX_TEXT_CELL + FAX_TEXT_CELL / 2;
    return (cyc_render_line_mask(line) & PPUMASK_BACKGROUND) && fax_text_shown_tile(HUD_P_COL, HUD_P_ROW) == TILE_HUD_P &&
           fax_text_shown_tile(HUD_P_COL + 1, HUD_P_ROW) == TILE_HUD_COLON;
}
