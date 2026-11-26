#pragma once
/* Punches (fax_punch.c): the fist, the weapon of a hero who has none. It
 * registers itself; the host only adds its TCP command. The Pendant fix
 * (fax_pendant.c) takes the fist's strength from here, since it works every
 * hit's damage out by the weapon in Y. */
#include <stdint.h>

/* Y at Player_HitSpriteWithWeapon's damage code is Player_CurWeapon, $FF
 * without a weapon: any value with bit 7 set is the fist, as the game's BMIs
 * read it. */
#define FAX_PUNCH_NO_WEAPON 0x80u
/* Half the Hand Dagger's: its strength 4 (WEAPON_STRENGTHS) and the Glove's
 * 2 (WEAPON_TO_GLOVE_STRENGTH). */
enum { FAX_PUNCH_STRENGTH = 2, FAX_PUNCH_GLOVE = 1 };

void fax_punch_tcp_setup(void);
