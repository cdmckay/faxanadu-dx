/* Spell cost: the M: bar shows what the selected spell will cost. USA
 * 57dd23d1, chipx86 byte-exact disassembly.
 *
 * Castlevania: Belmont's Curse draws the part of its MP bar a cast will take
 * a lighter shade of the bar. Here the M: bar does the same: one pixel an MP
 * (UI_SetHUDBarValue), so the last N pixels of its fill, N the selected
 * spell's cost (MAGIC_COSTS, $B7A9, bank 14: Deluge 2, Thunder 4, Fire 6,
 * Death 10, Tilte 16), are drawn a step lighter, and what a cast leaves is
 * the plain fill before them. With less MP than the cost the spell will not
 * go off (Player_ReduceMP refuses it), and the whole fill is drawn a step
 * darker instead. With no spell selected (SelectedMagic $FF) the bar is the
 * game's. The shades are the bar's own color a row of the NES palette up or
 * down, as the game's fades step ($19, its green nearly everywhere, to $29
 * and $09), so they read as the same bar.
 *
 * The game draws the bar only when Player_SetMP runs, so the span is worked
 * out from Player_MP and SelectedMagic at every present, whatever changed
 * them: a cast, a kill, a Guru, the Wing Boots' and the Hour Glass's drain, a
 * shop. Only the bar's own fill pixels are
 * recolored, those that show the fill's color on its four fill lines, so in
 * the frame or two before the game redraws the bar after MP changes nothing
 * is drawn where the bar shows no fill. Drawn over the status bar band
 * wherever Seamless put it (fax_world_hud_x), after the Elixir and the lit
 * item box (fax_item_box.c) and before every other feature's drawing, only
 * while the frame shows the status bar (fax_text_hud_showing), in its
 * palette (UI_AttributeDataIndex) and with the emphasis and greyscale its
 * lines were drawn with.
 *
 * No hooks, no ROM, no record, nothing written to the machine: it reads RAM,
 * the cart image's MAGIC_COSTS and the frame, and draws over the presented
 * picture. See docs/GAME_INTERNALS.md, "MP and its bar". */
#include "fax_spell_cost.h"
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

#ifndef FAX_SPELL_COST_LOG
#define FAX_SPELL_COST_LOG(...) fprintf(stderr, __VA_ARGS__)
#endif

extern uint32_t hw_palette_argb[512];   /* the host's NES colors, emphasis in bits 6-8 */

/* Bank 14 */
#define MAGIC_COSTS 0xB7A9u             /* 2, 4, 6, 10, 16: what Player_ReduceMP takes, by SelectedMagic */
/* RAM */
#define PLAYER_MP               0x039Au   /* at most 80 */
#define SELECTED_MAGIC          0x03C0u   /* 0 Deluge, 1 Thunder, 2 Fire, 3 Death, 4 Tilte; $FF none */
#define UI_ATTRIBUTE_DATA_INDEX 0x038Du   /* the status bar's and the text box's palette, 0-3 */
#define UI_BAR_WIDTH            0x04EEu   /* UI_MPAndHPBarWidth: 10 tiles, set by UI_DrawHUDSprites */

enum { MAGIC_BANK = 14, BANK_SIZE = 16384, FILL = 2 };   /* the Magic bar's fill is pixel value 2 (its planes swapped) */

static bool enabled, failed, loaded;
static uint8_t costs[FAX_SPELL_COST_SPELLS];
static uint32_t *canvas;
static size_t capacity;
/* What the last present drew, for TCP. */
static FaxSpellCostSpan last;
static bool shown;
static int bar_x = -1;
static uint8_t fill_nes, shade_nes;

static uint8_t q(uint16_t addr) { return cyc_mod_peek(addr); }

/* ---- the rules ---- */

FaxSpellCostSpan fax_spell_cost_span(uint8_t mp, uint8_t selected, const uint8_t c[FAX_SPELL_COST_SPELLS],
                                     unsigned bar_max) {
    FaxSpellCostSpan s = { FAX_SPELL_COST_NONE, 0, 0 };
    if (selected >= FAX_SPELL_COST_SPELLS) return s;
    unsigned shows = mp < bar_max ? mp : bar_max, cost = c[selected];
    if (!shows) return s;
    if (mp < cost) s.kind = FAX_SPELL_COST_SHORT, s.from = 0;
    else if (cost) s.kind = FAX_SPELL_COST_CAST, s.from = (uint8_t)(shows > cost ? shows - cost : 0);
    else return s;
    s.to = (uint8_t)shows;
    return s;
}

static bool black(uint8_t c) { return (c & 0x0Fu) >= 0x0E || c == 0x0D || c == 0x1D; }
uint8_t fax_spell_cost_lighter(uint8_t c) {
    c &= 0x3F;
    if (black(c)) return 0x0F;
    return c < 0x30 ? (uint8_t)(c + 0x10) : 0x30;
}
uint8_t fax_spell_cost_darker(uint8_t c) {
    c &= 0x3F;
    if (black(c) || c < 0x10) return 0x0F;   /* Screen_SetFadeOutPalette: under $10 is black */
    c = (uint8_t)(c - 0x10);
    return black(c) ? 0x0F : c;
}

/* ---- the machine ---- */

static bool load_costs(void) {
    size_t need = cyc_mod_copy_cart_payload(NULL, 0);
    if (need < (size_t)(MAGIC_BANK + 1) * BANK_SIZE) return false;
    uint8_t *payload = malloc(need);
    if (!payload) return false;
    bool ok = cyc_mod_copy_cart_payload(payload, need) == need;
    if (ok) memcpy(costs, payload + MAGIC_BANK * BANK_SIZE + (MAGIC_COSTS - 0x8000u), sizeof costs);
    free(payload);
    for (int i = 0; ok && i < FAX_SPELL_COST_SPELLS; ++i) ok = costs[i] && (i == 0 || costs[i] > costs[i - 1]);
    return ok;   /* the game's are 2, 4, 6, 10, 16: anything else is not its cart */
}

/* An NES color as the status bar's lines were drawn: their emphasis and greyscale. */
static uint32_t ink(uint8_t nes, unsigned mask) {
    unsigned keep = mask & 1u ? 0x30u : 0x3Fu, emphasis = (mask >> 5) << 6;
    return hw_palette_argb[((nes & keep) | emphasis) & 511] | 0xFF000000u;
}

const uint32_t *fax_spell_cost_present(const uint32_t *picture, int *w, int *h) {
    shown = false;
    bar_x = -1;
    last = (FaxSpellCostSpan){ FAX_SPELL_COST_NONE, 0, 0 };
    if (!enabled || failed) return picture;
    int width = 0, height = 0;
    const uint32_t *from = picture;
    if (from) width = *w, height = *h;
    else from = cyc_render_present(&width, &height);
    if (!from || height < FAX_SPELL_COST_FILL_TOP + FAX_SPELL_COST_FILL_LINES || !fax_text_hud_showing()) return picture;
    if (!loaded && !(loaded = load_costs())) {
        FAX_SPELL_COST_LOG("[spell-cost] cannot read MAGIC_COSTS; the M: bar's cost is off\n");
        failed = true;
        return picture;
    }
    unsigned bar_max = (unsigned)q(UI_BAR_WIDTH) * 8u;
    int x = fax_world_hud_x(width) + FAX_SPELL_COST_BAR_X;
    if (x < 0 || x + (int)bar_max > width) return picture;
    bar_x = x;
    last = fax_spell_cost_span(q(PLAYER_MP), q(SELECTED_MAGIC), costs, bar_max);
    unsigned palette = q(UI_ATTRIBUTE_DATA_INDEX) & 3u, mask = cyc_render_line_mask(FAX_SPELL_COST_FILL_TOP);
    fill_nes = (uint8_t)(cyc_render_palette((int)(palette * 4 + FILL)) & 0x3F);
    shade_nes = last.kind == FAX_SPELL_COST_SHORT ? fax_spell_cost_darker(fill_nes) : fax_spell_cost_lighter(fill_nes);
    /* A fill as black as the empty bar (a screen fading out) cannot be told
     * from it, and a fill darkened to black would read as no MP at all. */
    if (last.kind == FAX_SPELL_COST_NONE || black(fill_nes) || black(shade_nes) ||
        fill_nes == (cyc_render_palette(0) & 0x3F))
        return picture;
    uint32_t fill = ink(fill_nes, mask), shade = ink(shade_nes, mask);
    if (shade == fill) return picture;
    size_t n = (size_t)width * (size_t)height;
    if (n > capacity) {
        uint32_t *grown = realloc(canvas, n * sizeof *grown);
        if (!grown) return picture;
        canvas = grown; capacity = n;
    }
    memcpy(canvas, from, n * sizeof *canvas);
    for (int r = 0; r < FAX_SPELL_COST_FILL_LINES; ++r) {
        uint32_t *line = canvas + (size_t)(FAX_SPELL_COST_FILL_TOP + r) * (size_t)width + (size_t)x;
        for (unsigned i = last.from; i < last.to; ++i)
            if (line[i] == fill) line[i] = shade, shown = true;
    }
    if (!shown) return picture;
    *w = width; *h = height;
    return canvas;
}

#ifdef CYC_WITH_SDL
static void spell_cost_state(int id, const char *line) {
    (void)line;
    static const char *const KINDS[] = { "none", "cast", "short" };
    char fields[320];
    /* The span is the last present's: its kind, and the bar's pixels it covers, from <= i < to. */
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"hud\":%s,\"mp\":%u,\"selected\":%u,\"cost\":%u,\"kind\":\"%s\",\"from\":%u,\"to\":%u,"
             "\"x\":%d,\"y\":%d,\"lines\":%d,\"fill\":%u,\"shade\":%u,\"fill_rgb\":%u,\"shade_rgb\":%u,\"shown\":%s",
             enabled ? "true" : "false", fax_text_hud_showing() ? "true" : "false", q(PLAYER_MP), q(SELECTED_MAGIC),
             loaded && q(SELECTED_MAGIC) < FAX_SPELL_COST_SPELLS ? costs[q(SELECTED_MAGIC)] : 0u, KINDS[last.kind],
             last.from, last.to, bar_x, FAX_SPELL_COST_FILL_TOP, FAX_SPELL_COST_FILL_LINES, fill_nes, shade_nes,
             (unsigned)(ink(fill_nes, cyc_render_line_mask(FAX_SPELL_COST_FILL_TOP)) & 0xFFFFFFu),
             (unsigned)(ink(shade_nes, cyc_render_line_mask(FAX_SPELL_COST_FILL_TOP)) & 0xFFFFFFu),
             shown ? "true" : "false");
    cyc_tcp_ok(id, fields);
}
void fax_spell_cost_tcp_setup(void) {
    cyc_tcp_register("spell_cost_state",
                     "Spell cost: the selected spell's cost on the M: bar (MP, the span drawn, its colors), and where",
                     spell_cost_state);
}
#else
void fax_spell_cost_tcp_setup(void) {}
#endif

static void deactivate(void) { enabled = failed = false; }
static void activate(void) { enabled = true; }

NES_MOD_CONSTRUCTOR(register_spell_cost) {
    if (!nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.spell-cost", activate))
        abort();
}
