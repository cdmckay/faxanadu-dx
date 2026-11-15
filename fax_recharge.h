#pragma once
/* Recharging items (fax_recharge.c): the Wing Boots, the Hour Glass and the
 * Elixir are kept after use and come back. It registers itself; the host only
 * adds its TCP command. */
#include <stdbool.h>
#include <stdint.h>

void fax_recharge_tcp_setup(void);

/* The feature's whole state, exactly as its save-state record holds it:
 * whether each timed item is running, its cooldown, and whether the Elixir is
 * spent. This is the seam for save slots (#24): a slot that stores these bytes
 * with the game's RAM and hands them back on load carries cooldowns and the
 * Elixir's charge across sessions, and needs nothing else. */
enum { FAX_RECHARGE_RECORD = 6 };
/* Bytes written (FAX_RECHARGE_RECORD), or -1 when `cap` is too small. */
int fax_recharge_record_get(uint8_t *buf, int cap);
/* False, changing nothing, for a record it does not accept. Length 0 (a state
 * or slot from before the feature) means everything ready and nothing spent. */
bool fax_recharge_record_set(const uint8_t *buf, int len);
