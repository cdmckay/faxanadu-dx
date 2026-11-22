#pragma once
/* Double gold (fax_gold.c): the King gives 3000 Golds instead of 1500 and
 * says so, and every coin a monster drops pays twice its value, up to the
 * game's cap. It registers itself; the host only adds its TCP command. */
#include <stdint.h>

#define FAX_GOLD_MAX 0xFFFFFFu   /* Player_AddGold's cap: all three bytes $FF */

/* Gold after a coin of `value` is picked up with the remaster: twice the
 * value added, held at FAX_GOLD_MAX. */
uint32_t fax_gold_after_coin(uint32_t gold, uint8_t value);

void fax_gold_tcp_setup(void);
