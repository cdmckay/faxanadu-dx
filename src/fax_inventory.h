#pragma once
/* The Battle Suit and Dragon Slayer fix (fax_inventory.c). It registers
 * itself; the host only adds its TCP command. */
#include <stdint.h>

typedef enum { FAX_PICKUP_BATTLE_SUIT, FAX_PICKUP_DRAGON_SLAYER, FAX_PICKUPS } FaxPickup;

/* The count the item's pickup must add at: its own list's (NumberOfArmors for
 * the Battle Suit, NumberOfWeapons for the Dragon Slayer). */
uint16_t fax_inventory_count_addr(FaxPickup item);

void fax_inventory_tcp_setup(void);
