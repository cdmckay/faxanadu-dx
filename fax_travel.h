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

/* Guru travel's SAY step in bank 12 (03 C3 00: show message $C3, then END)
 * and message $C3's slot in bank 13, both supplied through read overrides.
 * The Block tutorial (fax_block.c) borrows them for its one line. It speaks
 * only at the end of a shop's or a gift's script, never during a Guru's, and
 * Guru travel writes the slot again before each of its own lines. */
enum { FAX_TRAVEL_SAY = 0xAE00, FAX_TRAVEL_SLOT = 0xB45B, FAX_TRAVEL_SLOT_ID = 0xC3, FAX_TRAVEL_SLOT_LEN = 48 };

/* A Guru's conversation has begun: a temple Guru's at his SET_SPAWN_POINT,
 * Eolis's at his first action. `temple` is 0-7. Features that act when the
 * hero talks to a Guru listen here rather than hook the script interpreter
 * again; it works whether or not Guru travel is on. Register in activation,
 * and stop (on = false) in the reset callback. Up to four listeners. */
typedef void (*FaxGuruListener)(unsigned temple);
void fax_travel_listen(FaxGuruListener fn, bool on);
