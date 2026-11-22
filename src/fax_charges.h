#pragma once
/* Charges (fax_charges.c): the status bar shows each recharging item the hero
 * has, the Wing Boots, the Hour Glass and the Elixir, in a row beside the
 * item box, as Super Metroid shows its reserve tanks: in its colours when it
 * is ready, grey when it is used (running, recharging, or the Elixir spent),
 * and not at all when he does not have it. An item that is used blinks as it
 * greys. Seamless (fax_world.c) centres the status bar and the row together;
 * the stock 256-wide view has no room for the row, so there it draws
 * nothing. It registers itself; the host calls these. */
#include <stdbool.h>
#include <stdint.h>

/* The row, in pixels, past the status bar's right edge: three slots
 * FAX_CHARGES_SLOT apart, each item FAX_CHARGES_ITEM wide, the Wing Boots
 * first. The status bar's last column is blank, so the first item stands 8
 * pixels from the item box's "]". The rows of the item box's picture: 16-31. */
enum { FAX_CHARGES_ITEM = 16, FAX_CHARGES_SLOT = 24, FAX_CHARGES_ROW = 2 * FAX_CHARGES_SLOT + FAX_CHARGES_ITEM,
       FAX_CHARGES_TOP = 16 };
/* How wide the row is when the feature is on (FAX_CHARGES_ROW), else 0:
 * Seamless lays the status bar out with it. */
int fax_charges_row(void);

void fax_charges_frame_end(void);   /* the end of each machine frame */
/* The picture to present: `picture` (Seamless's canvas, or if NULL the
 * engine's own) with the row drawn beside the status bar when there is room
 * for it; otherwise `picture` itself. */
const uint32_t *fax_charges_present(const uint32_t *picture, int *w, int *h);
void fax_charges_tcp_setup(void);

/* The rules (exposed for the unit tests). A used item blinks for
 * FAX_CHARGES_BLINK frames, not counting those a text box is up (the
 * "I've used ..." line), off while InterruptCounter & FAX_CHARGES_BLINK_OFF. */
enum { FAX_CHARGES_BLINK = 96, FAX_CHARGES_BLINK_OFF = 0x08 };
/* The grey an item's color `c` (an NES color, $00-$3F) takes while it is used:
 * the NES's own grey of its brightness, a step darker. */
uint8_t fax_charges_grey(uint8_t c);
