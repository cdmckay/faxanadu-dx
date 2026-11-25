#pragma once
/* Recharging items (fax_recharge.c): the Wing Boots, the Hour Glass and the
 * Elixir are kept after use. The Wing Boots and the Hour Glass both run on
 * MP, with no cooldown, and turning either on or off shows no text box: an
 * end the player did not cause says so below the HUD, and the first use of
 * each teaches it once; a Guru restores the Elixir (and fills HP and MP:
 * fax_mana.c), says when he does, and once that he can. It registers itself
 * and keeps its state in its save-state record, in the file in play (Save
 * files' named fields recharge.elixir and recharge.elixir_told) and in
 * faxanadu_recharge.txt (the lessons learned); the host adds its TCP command
 * and its present, and calls fax_recharge_loaded. */
#include <stdbool.h>
#include <stdint.h>

void fax_recharge_tcp_setup(void);
/* Recharging items is on: the Wing Boots and the Hour Glass the hero has are
 * his for good, so Quick shops shows them in its buy list as owned, and
 * won't buy them (fax_shops.c). */
bool fax_recharge_on(void);
/* The Elixir revived the hero and waits, spent, for a Guru to charge it: it
 * is still his, as a charged one is (SpecialItems' bit). False when off. */
bool fax_recharge_elixir_spent(void);
/* After a save state has loaded: a Guru's line about the Elixir, or an
 * item's lesson, that was showing is put back in message $C3's slot (read
 * overrides are not saved). */
void fax_recharge_loaded(void);
/* The picture to present: `picture` (or, if NULL, the engine's own) with the
 * end of a run the player did not cause said over it, below the HUD in Zone
 * names' box (fax_banner_draw), while it shows; otherwise `picture` itself. */
const uint32_t *fax_recharge_present(const uint32_t *picture, int *w, int *h);

/* Where each recharging item stands (the status bar shows the Elixir's:
 * fax_charges.c): not the hero's (NONE), ready to use (READY), or his but not
 * ready (USED): the Wing Boots or the Hour Glass running, the Elixir spent.
 * NONE for all three when Recharging items is off. */
enum { FAX_RECHARGE_BOOTS, FAX_RECHARGE_GLASS, FAX_RECHARGE_ELIXIR, FAX_RECHARGE_ITEMS };
typedef enum { FAX_CHARGE_NONE, FAX_CHARGE_READY, FAX_CHARGE_USED } FaxCharge;
FaxCharge fax_recharge_charge(int item);
