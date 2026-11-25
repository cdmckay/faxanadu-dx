#pragma once
/* Charges (fax_charges.c): the status bar shows the Elixir left of its M:
 * and P: bars, which it refills when it revives the hero, as Super Metroid
 * shows a reserve tank beside Energy: in its colours when it is charged,
 * grey when it is spent, and not at all when he does not have it. It blinks
 * as it greys, when it revives him. The Wing Boots and the Hour Glass are not
 * drawn: they run on MP, T: counts the selected one's run, and the item box
 * lights while the one in it runs (fax_item_box.h). Seamless (fax_world.c)
 * centres the status bar and the Elixir together; the stock 256-wide view
 * has no room for it, so there it draws nothing. It registers itself; the host calls these. */
#include <stdbool.h>
#include <stdint.h>

/* The Elixir, in pixels: FAX_CHARGES_ITEM wide, right against the status
 * bar's left edge. The status bar's column 0 is blank, so it stands 8 pixels
 * from the "M" and the "P". Its rows are the two bars': 16-31. */
enum { FAX_CHARGES_ITEM = 16, FAX_CHARGES_TOP = 16 };
/* How far the Elixir reaches left of the status bar when the feature is on
 * (FAX_CHARGES_ITEM), else 0: Seamless lays the status bar out with it. */
int fax_charges_width(void);

void fax_charges_frame_end(void);   /* the end of each machine frame */
/* The picture to present: `picture` (Seamless's canvas, or if NULL the
 * engine's own) with the Elixir drawn beside the status bar when there is
 * room for it; otherwise `picture` itself. */
const uint32_t *fax_charges_present(const uint32_t *picture, int *w, int *h);
void fax_charges_tcp_setup(void);

/* The rules (exposed for the unit tests). An Elixir just spent blinks for
 * FAX_CHARGES_BLINK frames, not counting those a text box is up (the "I've
 * used Elixir." line), off while InterruptCounter & FAX_CHARGES_BLINK_OFF. */
enum { FAX_CHARGES_BLINK = 96, FAX_CHARGES_BLINK_OFF = 0x08 };
/* The grey a color `c` of the Elixir's (an NES color, $00-$3F) takes while
 * it is spent: the NES's own grey of its brightness, a step darker. */
uint8_t fax_charges_grey(uint8_t c);
