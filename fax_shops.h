#pragma once
/* Shops (fax_shops.c): a shopkeeper opens his list at once, Select switches
 * between buying and selling inside it, buying and selling keep you in it,
 * and any shop buys anything you carry. It registers itself; the host adds
 * its TCP command and draws its hint over the presented picture. */
#include <stdbool.h>
#include <stdint.h>

void fax_shops_tcp_setup(void);
/* Draw the list's hint line ("Select: Sell") over the picture, after
 * Seamless, zone names and speaker names; returns the picture to show. */
const uint32_t *fax_shops_present(const uint32_t *picture, int *w, int *h);

/* What a shop pays for an item (its full ID, category in the top 3 bits):
 * half what this shop asks for it (`here`, 0 if it does not sell it), else
 * half the lowest price any shop asks (`lowest`, 0 if none does), else 100.
 * 0 for an item no shop buys at all. */
uint16_t fax_shops_sell_price(uint8_t id, uint16_t here, uint16_t lowest);
/* Items no shop buys: the Dragon Slayer, the Battle Suit and Helmet, and the
 * SpecialItems bits (the rings, the Magical Rod, the Elixir, the Pendant, the
 * Black Onyx). */
bool fax_shops_never_buys(uint8_t id);
