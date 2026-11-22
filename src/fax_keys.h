#pragma once
/* Auto keys and reusable tools (fax_keys.c): two mod packages, each its own
 * toggle, in one module because both act on the same door code. It registers
 * itself; the host only adds its TCP command. */
#include <stdbool.h>

void fax_keys_tcp_setup(void);
/* Reusable tools is on: a key or the Mattock the hero has is his for good,
 * so Quick shops shows one in its buy list as owned, and won't buy it
 * (fax_shops.c). */
bool fax_reusable_tools_on(void);
