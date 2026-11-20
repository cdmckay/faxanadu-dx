#pragma once
/* Recharging items (fax_recharge.c): the Wing Boots, the Hour Glass and the
 * Elixir are kept after use and come back. It registers itself and keeps its
 * state in its save-state record and in the file in play (Save files' named
 * fields recharge.wingboots, recharge.hourglass and recharge.elixir); the
 * host only adds its TCP command. */
void fax_recharge_tcp_setup(void);
