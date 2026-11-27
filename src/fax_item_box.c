/* Item box: the status bar's item box lights while its item runs (#149,
 * #159). USA 57dd23d1, chipx86 byte-exact disassembly.
 *
 * Nothing on screen said the Wing Boots or the Hour Glass were on but T:
 * counting down. Now, while the item the status bar's item box shows is
 * running, the box's [ and ] turn gold, and go back to white the frame the
 * run ends. Running is the game's own state, read at every present:
 * - the Wing Boots fly while Player_StatusFlag has $80.
 *   Game_DecWingBootsDuration sets it every main-loop frame DurationWingBoots
 *   runs and clears it in the frame the flight ends (@_wingBootsDone), and
 *   Game_InitStateForSpawn zeroes it;
 * - the Hour Glass freezes while DurationHourGlass is below $80, as
 *   Sprites_UpdateBehavior's BMI reads it: from Player_UseHourGlass's STA to
 *   the second it wraps to $FF, or Game_ClearTimedItems'.
 * The game's "The power of ... is gone." comes up as each ends; Recharging
 * items skips it (#151), and ends nothing sooner or later for that.
 * Recharging items (fax_recharge.c) runs both on MP and ends them through
 * the game's own countdowns, so these are its runs too, to the frame. The
 * box shows SelectedItem (UI_DrawSelectedItem), and only that item lights
 * it: the Hour Glass selected while the boots fly shows the Hour Glass,
 * unlit, while T: shows the flight. The Ointment and the Glove run on timers
 * too, but are never in the box.
 *
 * Only the brackets' own pixels change: in the [ and ] tiles, each pixel that
 * shows the status bar's white (its palette's color 3, which the brackets are
 * drawn in) is drawn gold instead, so they keep their shape and nothing else
 * moves: not the item's picture, not the backdrop around them, not the rest
 * of the band. #149 drew a closed frame in their place, which looked pasted
 * on (#159). The gold is $27, a color the status bar has nowhere (its
 * palette is black, the HP bar's pink $15, the MP bar's green $19, or $1A in
 * King Grieve's room, and the white $30 of its text and brackets, in every
 * region), and far darker than the white it replaces. A screen fading out
 * (Screen_SetFadeOutPalette) takes the white down a row a stage, and the
 * gold goes down with it (fax_item_box_gold), so the brackets fade with the
 * bar rather than staying lit over a black screen.
 *
 * Drawn over the status bar band wherever Seamless put it (fax_world_hud_x),
 * after the Elixir (fax_charges.c), only while the frame shows the status bar
 * (fax_text_hud_showing) with the box's [ and ] tiles where
 * UI_DrawHUDSprites puts them, and with the emphasis and greyscale each of
 * its lines was drawn with: a pixel is the white as that line drew it, and
 * turns the gold as that line would draw it.
 *
 * No hooks, no ROM, no record, nothing written to the machine: it reads RAM,
 * palette RAM and the last frame's nametable, and recolors the presented
 * picture. See docs/GAME_INTERNALS.md, "The status bar". */
#include "fax_item_box.h"
#include "fax_text.h"
#include "fax_world.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "mod_runtime.h"
#ifdef CYC_WITH_SDL
#include "cyc_tcp.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint32_t hw_palette_argb[512]; /* the host's NES colors, emphasis in bits 6-8 */

/* RAM */
#define PLAYER_STATUS_FLAG      0x00A5u /* bit 7: the Wing Boots fly */
#define UI_ATTRIBUTE_DATA_INDEX 0x038Du /* the status bar's and the text box's palette, 0-3 */
#define SELECTED_ITEM           0x03C1u /* the item the box shows: ITEMINVENTORY_*, $FF none */
#define DURATION_HOUR_GLASS     0x042Au /* seconds left, $FF when not running */

enum { ITEMINVENTORY_HOUR_GLASS = 0x0D, ITEMINVENTORY_WING_BOOTS = 0x0F, WING_BOOTS_ON = 0x80, NOT_RUNNING = 0x80 };
/* UI_STATUS_TILES' item box: [ in column 27 and ] in 30, rows 2 (top) and 3 (bottom). */
enum { OPEN_COL = 27, CLOSE_COL = 30, TOP_ROW = 2, BOTTOM_ROW = 3 };
enum { TILE_OPEN_TOP = 0x2C, TILE_OPEN_BOTTOM = 0x2D, TILE_CLOSE_TOP = 0x2E, TILE_CLOSE_BOTTOM = 0x2F };
enum { WHITE = 3, NATIVE_W = 256, BAND = 32 }; /* the text's and brackets' color in the palette; the band's lines */

static bool enabled;
static uint32_t *canvas;
static size_t capacity;
/* What the last present drew, for TCP: the [ tile's x when the brackets were
 * gold, how many pixels turned, and the colors. */
static int open_x = -1;
static unsigned pixels;
static uint8_t white_nes = 0x0F, gold_nes = 0x0F;

static uint8_t q(uint16_t addr) { return cyc_mod_peek(addr); }

/* ---- the rules ---- */

bool fax_item_box_running(uint8_t selected, uint8_t status_flag, uint8_t hour_glass) {
    if (selected == ITEMINVENTORY_WING_BOOTS) return (status_flag & WING_BOOTS_ON) != 0;
    if (selected == ITEMINVENTORY_HOUR_GLASS) return hour_glass < NOT_RUNNING;
    return false;
}

static bool black(uint8_t c) { return (c & 0x0Fu) >= 0x0E || c == 0x0D || c == 0x1D; }
uint8_t fax_item_box_gold(uint8_t white) {
    white &= 0x3F;
    if (black(white)) return 0x0F;
    switch (white >> 4) {
    case 3:
    case 2:
        return 0x27; /* white: $20 is the NES's white too, a fade's first stage */
    case 1:
        return 0x17;
    default:
        return 0x07;
    }
}

bool fax_item_box_in_brackets(int x, int y) {
    if (y < FAX_ITEM_BOX_LINE || y >= FAX_ITEM_BOX_LINE + FAX_ITEM_BOX_LINES) return false;
    return (x >= FAX_ITEM_BOX_OPEN_X && x < FAX_ITEM_BOX_OPEN_X + FAX_ITEM_BOX_TILE) ||
           (x >= FAX_ITEM_BOX_CLOSE_X && x < FAX_ITEM_BOX_CLOSE_X + FAX_ITEM_BOX_TILE);
}

/* ---- the machine ---- */

/* The last frame showed the box's [ and ] where UI_DrawHUDSprites puts them. */
static bool box_showing(void) {
    return fax_text_shown_tile(OPEN_COL, TOP_ROW) == TILE_OPEN_TOP &&
           fax_text_shown_tile(OPEN_COL, BOTTOM_ROW) == TILE_OPEN_BOTTOM &&
           fax_text_shown_tile(CLOSE_COL, TOP_ROW) == TILE_CLOSE_TOP &&
           fax_text_shown_tile(CLOSE_COL, BOTTOM_ROW) == TILE_CLOSE_BOTTOM;
}

static bool running(void) {
    return fax_item_box_running(q(SELECTED_ITEM), q(PLAYER_STATUS_FLAG), q(DURATION_HOUR_GLASS));
}

/* An NES color as a line of the band was drawn: its emphasis and greyscale. */
static uint32_t ink(uint8_t nes, unsigned mask) {
    unsigned keep = mask & 1u ? 0x30u : 0x3Fu, emphasis = (mask >> 5) << 6;
    return hw_palette_argb[((nes & keep) | emphasis) & 511] | 0xFF000000u;
}

const uint32_t *fax_item_box_present(const uint32_t *picture, int *w, int *h) {
    open_x = -1;
    pixels = 0;
    if (!enabled) return picture;
    int width = 0, height = 0;
    const uint32_t *from = picture;
    if (from) width = *w, height = *h;
    else from = cyc_render_present(&width, &height);
    if (!from || height < BAND || !fax_text_hud_showing() || !box_showing()) return picture;
    int x = fax_world_hud_x(width);
    if (x < 0 || x + NATIVE_W > width) return picture;
    unsigned palette = q(UI_ATTRIBUTE_DATA_INDEX) & 3u;
    white_nes = (uint8_t)(cyc_render_palette((int)(palette * 4 + WHITE)) & 0x3F);
    gold_nes = fax_item_box_gold(white_nes);
    if (!running() || black(gold_nes)) return picture; /* black: a screen faded out, nothing to light */
    size_t n = (size_t)width * (size_t)height;
    if (n > capacity) {
        uint32_t *grown = realloc(canvas, n * sizeof *grown);
        if (!grown) return picture;
        canvas = grown;
        capacity = n;
    }
    memcpy(canvas, from, n * sizeof *canvas);
    for (int y = FAX_ITEM_BOX_LINE; y < FAX_ITEM_BOX_LINE + FAX_ITEM_BOX_LINES; ++y) {
        unsigned mask = cyc_render_line_mask(y);
        uint32_t white = ink(white_nes, mask), gold = ink(gold_nes, mask);
        if (white == gold) continue; /* greyscale: no gold to show */
        uint32_t *line = canvas + (size_t)y * (size_t)width + (size_t)x;
        for (int c = 0; c < FAX_ITEM_BOX_TILE; ++c) {
            uint32_t *open = &line[FAX_ITEM_BOX_OPEN_X + c], *close = &line[FAX_ITEM_BOX_CLOSE_X + c];
            if (*open == white) *open = gold, ++pixels;
            if (*close == white) *close = gold, ++pixels;
        }
    }
    if (!pixels) return picture;
    open_x = x + FAX_ITEM_BOX_OPEN_X;
    *w = width;
    *h = height;
    return canvas;
}

#ifdef CYC_WITH_SDL
static void item_box_state(int id, const char *line) {
    (void)line;
    char fields[384];
    unsigned mask = cyc_render_line_mask(FAX_ITEM_BOX_LINE);
    /* The game's state now; the brackets (where, how many pixels, their colors) as the last present drew them. */
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"hud\":%s,\"box\":%s,\"selected\":%u,\"wing_boots\":%s,\"hour_glass\":%s,"
             "\"running\":%s,\"lit\":%s,\"x\":%d,\"close_x\":%d,\"y\":%d,\"pixels\":%u,"
             "\"white\":%u,\"gold\":%u,\"gold_rgb\":%u",
             enabled ? "true" : "false", fax_text_hud_showing() ? "true" : "false", box_showing() ? "true" : "false",
             q(SELECTED_ITEM), q(PLAYER_STATUS_FLAG) & WING_BOOTS_ON ? "true" : "false",
             q(DURATION_HOUR_GLASS) < NOT_RUNNING ? "true" : "false", running() ? "true" : "false",
             pixels ? "true" : "false", open_x, open_x < 0 ? -1 : open_x + (FAX_ITEM_BOX_CLOSE_X - FAX_ITEM_BOX_OPEN_X),
             FAX_ITEM_BOX_LINE, pixels, white_nes, gold_nes, (unsigned)(ink(gold_nes, mask) & 0xFFFFFFu));
    cyc_tcp_ok(id, fields);
}
void fax_item_box_tcp_setup(void) {
    cyc_tcp_register("item_box_state",
                     "Item box: its brackets gold while its item runs (the item, the runs, lit in the last picture)",
                     item_box_state);
}
#else
void fax_item_box_tcp_setup(void) {}
#endif

static void deactivate(void) { enabled = false; }
static void activate(void) { enabled = true; }

NES_MOD_CONSTRUCTOR(register_item_box) {
    if (!nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.item-box", activate))
        abort();
}
