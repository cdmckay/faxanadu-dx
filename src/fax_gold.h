#pragma once
/* Double gold (fax_gold.c): the King gives 3000 Golds instead of 1500 and
 * says so, counted up in the time his 1500 took, and only once a game; and
 * every coin a monster drops pays twice its value, up to the game's cap. It
 * registers itself; the host adds its TCP command and tells it of loaded
 * save states. */
#include <stdint.h>

#define FAX_GOLD_MAX 0xFFFFFFu   /* Player_AddGold's cap: all three bytes $FF */

/* Gold after a coin of `value` is picked up with the remaster: twice the
 * value added, held at FAX_GOLD_MAX. */
uint32_t fax_gold_after_coin(uint32_t gold, uint8_t value);

/* How many of the four digits of the King's plea ("1500", or "3000" with
 * the remaster) have typed, as the text box stands, on or off: 0 when the
 * plea is not loaded. */
unsigned fax_gold_plea_digits(void);

/* A save state was loaded, every record in: one from before this feature's
 * record takes whether the King has given from the quest log. */
void fax_gold_loaded(void);

void fax_gold_tcp_setup(void);
