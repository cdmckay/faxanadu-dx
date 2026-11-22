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

/* Guru travel's SAY step in bank 12 (03 C3 00: show message $C3, then END),
 * its ASK step after it (16 C3 00 AE: ask message $C3; A jumps to SAY, B goes
 * on to ASK + 4) and message $C3's slot in bank 13, all supplied through read
 * overrides. The Block tutorial (fax_block.c) borrows SAY and the slot for its
 * one line. It speaks only at the end of a shop's or a gift's script, never
 * during a Guru's, and Guru travel writes the slot again before each of its
 * own lines. Eolis's Magician (fax_lesson.c) asks his offer at the ASK step. */
enum { FAX_TRAVEL_SAY = 0xAE00, FAX_TRAVEL_ASK = 0xAE03, FAX_TRAVEL_SLOT = 0xB45B, FAX_TRAVEL_SLOT_ID = 0xC3,
       FAX_TRAVEL_SLOT_LEN = 48 };

/* The Gurus this playthrough has visited, a bit per temple (0 Eolis ... 7
 * Dartmoor); Eolis's is set from the start of every game. 0 with Guru travel
 * off. The map counts them as their temples' doors. */
uint8_t fax_travel_visited(void);
