#pragma once
/* Spell cost (fax_spell_cost.c): the M: bar shows what the selected spell
 * will cost, as Castlevania: Belmont's Curse does. The last N pixels of its
 * fill, N the spell's cost (one pixel an MP), are drawn a step lighter, so
 * what a cast leaves shows at a glance; with less MP than that the spell will
 * not go off, and the whole fill is drawn a step darker. No spell selected:
 * the bar as the game draws it. Drawn at present time, over the status bar
 * band wherever Seamless (fax_world.c) put it. It registers itself; the host
 * calls these. */
#include <stdbool.h>
#include <stdint.h>

/* The picture to present: `picture` (the picture so far, or if NULL the
 * engine's own) with the bar's cost drawn, when a spell is selected and the
 * status bar shows; otherwise `picture` itself. */
const uint32_t *fax_spell_cost_present(const uint32_t *picture, int *w, int *h);
void fax_spell_cost_tcp_setup(void);

/* ---- the rules (exposed for the unit tests) ---- */

/* Where the M: bar is in the 256-wide picture: from x 24 (PPU $2043, column
 * 3 of row 2), one pixel an MP, its fill on lines 18-21 (rows 2-5 of the
 * fill tile $09 and the partial tile $0D, between the border's lines 17 and
 * 22). */
enum { FAX_SPELL_COST_BAR_X = 24, FAX_SPELL_COST_FILL_TOP = 18, FAX_SPELL_COST_FILL_LINES = 4 };
enum { FAX_SPELL_COST_SPELLS = 5 };   /* Deluge, Thunder, Fire, Death, Tilte */

typedef enum {
    FAX_SPELL_COST_NONE = 0,   /* no spell selected, or nothing to draw: the bar as it is */
    FAX_SPELL_COST_CAST,       /* enough MP: the cost's pixels a step lighter */
    FAX_SPELL_COST_SHORT,      /* not enough: the whole fill a step darker */
} FaxSpellCostKind;

typedef struct {
    FaxSpellCostKind kind;
    uint8_t from, to;          /* the bar's pixels drawn, from <= x < to (0 at the bar's left) */
} FaxSpellCostSpan;

/* The span for `mp` with spell `selected` (SelectedMagic: 0-4, else none),
 * spells costing `costs` (MAGIC_COSTS) on a bar `bar_max` pixels long (its
 * width in tiles times 8: the game draws at most that much fill). */
FaxSpellCostSpan fax_spell_cost_span(uint8_t mp, uint8_t selected, const uint8_t costs[FAX_SPELL_COST_SPELLS],
                                     unsigned bar_max);
/* An NES color ($00-$3F) a step lighter and a step darker: a row of the
 * palette up or down ($19 to $29 or $09), as the game's own fades step. The
 * pale row lightens to white, the dark row darkens to black, and the blacks
 * stay black. */
uint8_t fax_spell_cost_lighter(uint8_t c);
uint8_t fax_spell_cost_darker(uint8_t c);
