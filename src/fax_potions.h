#pragma once
/* Potion Sellers (fax_potions.c): the seven meat shops sell a Red Potion,
 * each at the price its town's tool shop charged, in place of their dried
 * meat, and the tool shops stop selling them (Quick shops, fax_shops.c, drops
 * the line from their lists). It registers itself and keeps the conversation
 * in its save-state record; the host adds its TCP command and calls
 * fax_potions_loaded. */
#include <stdbool.h>
#include <stdint.h>

enum { FAX_POTIONS_TOWNS = 7 };          /* the meat shops, Eolis to Dartmoor; Apolune has none */
enum { FAX_POTIONS_RED_POTION = 0x90 };  /* the Red Potion as shops list it: the Items' $10, category 4 */

/* The price of a Red Potion at town `town`'s meat shop (0 Eolis, 1 Forepaw,
 * 2 Mascon, 3 Victim, 4 Conflate, 5 Daybreak, 6 Dartmoor), in Golds: what
 * that town's tool shop asked. 0 for no such town. */
uint16_t fax_potions_price(unsigned town);

/* The meat shops sell the Red Potions, so the tool shops don't: true with
 * the remaster on, unless the Potion Seller's line could not be supplied
 * (then he sells his dried meat, and the tool shops their potions). */
bool fax_potions_selling(void);

void fax_potions_tcp_setup(void);
/* After a save state has loaded: his question, if it was up, is put back in
 * message $C3's slot (read overrides are not saved). */
void fax_potions_loaded(void);
