#pragma once
/* Guru travel (fax_travel.c). The step supplies its script once the cart is
 * loaded; call it at power-on and at the end of each frame. */
#include <stdbool.h>
#include <stdint.h>
void fax_travel_step(void);
void fax_travel_tcp_setup(void);

/* For Save files (fax_files.c), which asks a Guru's question first while a
 * file is in play: whether this Guru has another to send the hero to; the
 * step that shows the line slot and ends, if Guru travel supplies it (else
 * 0); and starting Guru travel's part, "All Gurus meditate as one." then the
 * offer, or (towns) the towns at once. Returns the step to go to, with its
 * line in the slot, or 0 if there is nothing to offer. */
bool fax_travel_can_offer(void);
uint16_t fax_travel_say_step(void);
uint16_t fax_travel_begin(bool towns);
