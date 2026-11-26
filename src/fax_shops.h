#pragma once
/* Shops (fax_shops.c): a shopkeeper talks as the original's does (his
 * greeting, Buy or Sell, the list's line), buying and selling keep you in
 * the list until B, which thanks you if you traded, and any shop buys
 * anything you carry. It registers itself; the host adds its TCP command and
 * draws its hint over the presented picture. */
#include <stdbool.h>
#include <stdint.h>

void fax_shops_tcp_setup(void);
/* Draw the list's hint line ("Not for sale", "Already owned"), its page and
 * the prices it covers over the picture, after Seamless, zone names and
 * speaker names; returns the picture to show. */
const uint32_t *fax_shops_present(const uint32_t *picture, int *w, int *h);

enum { FAX_SHOPS_RED_POTION = 0x90 };   /* the Red Potion as shops list it: the Items' $10, category 4 */

/* What a shop pays for an item (its full ID, category in the top 3 bits):
 * half what this shop asks for it (`here`, 0 if it does not sell it), else
 * half the lowest price any shop asks (`lowest`, 0 if none does), else 100.
 * The Red Potion, which the tool shops sell from 160 to 2000, pays half the
 * lowest at every shop: 80. 0 for an item no shop buys at all. */
uint16_t fax_shops_sell_price(uint8_t id, uint16_t here, uint16_t lowest);
/* Items no shop buys: the Dragon Slayer, the Battle Suit and Helmet, and the
 * SpecialItems bits (the rings, the Magical Rod, the Elixir, the Pendant, the
 * Black Onyx). */
bool fax_shops_never_buys(uint8_t id);

/* What the hero has, for fax_shops_owned: SelectedItem and the Items list
 * (its first `count` entries), as IDs within the Items category, SpecialItems,
 * the features that keep items after use, and for the weapons, armor, shields
 * and spells (by category, 0-3) the one equipped ($FF for none) and the list
 * (its first gear_count entries), as IDs within the category. */
enum { FAX_SHOPS_GEAR = 4, FAX_SHOPS_GEAR_SLOTS = 4 };
typedef struct {
    uint8_t selected, count, items[8], special;
    bool elixir_spent;            /* recharging items: it revived him, and waits for a Guru */
    bool recharging, reusable;    /* recharging items and reusable tools are on */
    uint8_t gear_selected[FAX_SHOPS_GEAR], gear_count[FAX_SHOPS_GEAR], gear[FAX_SHOPS_GEAR][FAX_SHOPS_GEAR_SLOTS];
} FaxShopsHero;
/* An item (its full ID) that the hero already has, so in a buy list Quick
 * shops shows it as owned and A on it does nothing: a weapon, armor, shield or
 * spell he has equipped or in its list (one of each is all he can use; a
 * different one, a weaker sword say, is not owned); the Wing Boots and the
 * Hour Glass with recharging items, and a key or the Mattock with reusable
 * tools, selected or in his list; and the Elixir, charged or spent. Never a
 * consumable. */
bool fax_shops_owned(uint8_t id, const FaxShopsHero *hero);
/* An item in the sell list (its full ID) that Quick shops won't buy from the
 * hero, so it shows no price and A on it does nothing: one no shop buys
 * (fax_shops_never_buys), and an Item he has for good (fax_shops_owned's
 * Items), which neither buys nor sells, so the A and Jo keys' givers, who hand
 * out another to a hero without one, can't be farmed for gold. Gear he owns
 * still sells. */
bool fax_shops_not_for_sale(uint8_t id, const FaxShopsHero *hero);

/* A buy list of `n` entries (items[i] at prices[i]) without the `drops` items
 * in `drop` (full IDs): the entries after each one dropped move up, in order.
 * Returns the entries kept. The tool shops' lists lose the Wing Boots, the
 * Hour Glass and the Elixir this way, which are found, not bought
 * (fax_found.h). */
unsigned fax_shops_without(const uint8_t drop[], unsigned drops, uint8_t items[], uint16_t prices[], unsigned n);
