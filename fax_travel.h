#pragma once
/* Guru travel (fax_travel.c). The step supplies its script once the cart is
 * loaded; call it at power-on and at the end of each frame. */
void fax_travel_step(void);
void fax_travel_tcp_setup(void);
/* Guru travel's SAY step in bank 12 (03 C3 00: show message $C3, then END)
 * and message $C3's slot in bank 13, both supplied through read overrides.
 * The Block tutorial (fax_block.c) borrows them for its one line. It speaks
 * only at the end of a shop's or a gift's script, never during a Guru's, and
 * Guru travel writes the slot again before each of its own lines. */
enum { FAX_TRAVEL_SAY = 0xAE00, FAX_TRAVEL_SLOT = 0xB45B, FAX_TRAVEL_SLOT_ID = 0xC3, FAX_TRAVEL_SLOT_LEN = 48 };
