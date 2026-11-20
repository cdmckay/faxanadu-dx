#pragma once
/* Recharging items (fax_recharge.c): the Wing Boots, the Hour Glass and the
 * Elixir are kept after use and come back. It registers itself and keeps its
 * state in its save-state record and in the file in play (Save files' named
 * fields recharge.wingboots, recharge.hourglass and recharge.elixir); the
 * host only adds its TCP command. */
#include <stdbool.h>

void fax_recharge_tcp_setup(void);
/* Recharging items is on: the Wing Boots and the Hour Glass the hero has are
 * his for good, so Quick shops shows them in its buy list as owned
 * (fax_shops.c). */
bool fax_recharge_on(void);
/* The Elixir revived the hero and waits, spent, for a Guru to charge it: it
 * is still his, as a charged one is (SpecialItems' bit). False when off. */
bool fax_recharge_elixir_spent(void);
