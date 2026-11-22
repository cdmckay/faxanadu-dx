#pragma once
/* Shops (fax_shops.c): a shopkeeper opens his list at once, Select switches
 * between buying and selling inside it, buying and selling keep you in it,
 * and any shop buys anything you carry. It registers itself; the host adds
 * its TCP command and draws its hint over the presented picture. */
#include <stdbool.h>
#include <stdint.h>

void fax_shops_tcp_setup(void);
/* Draw the list's hint line ("Select to sell") over the picture, after
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

/* What the hero has, for fax_shops_owned: SelectedItem and the Items list
 * (its first `count` entries), as IDs within the Items category, SpecialItems,
 * and the features that keep items after use. */
typedef struct {
    uint8_t selected, count, items[8], special;
    bool elixir_spent;            /* recharging items: it revived him, and waits for a Guru */
    bool recharging, reusable;    /* recharging items and reusable tools are on */
} FaxShopsHero;
/* An item (its full ID) that the hero already has for good, so in a buy list
 * Quick shops shows it as owned and A on it does nothing: the Wing Boots and
 * the Hour Glass with recharging items, and a key or the Mattock with reusable
 * tools, selected or in his list; and the Elixir, charged or spent. */
bool fax_shops_owned(uint8_t id, const FaxShopsHero *hero);
/* An item in the sell list (its full ID) that Quick shops won't buy from the
 * hero, so it shows no price and A on it does nothing: one no shop buys
 * (fax_shops_never_buys), and one he has for good (fax_shops_owned), which
 * neither buys nor sells, so the A and Jo keys' givers, who hand out another
 * to a hero without one, can't be farmed for gold. */
bool fax_shops_not_for_sale(uint8_t id, const FaxShopsHero *hero);
