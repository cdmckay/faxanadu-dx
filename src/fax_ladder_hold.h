#pragma once
/* Holding on to ladders (fax_ladder_hold.c): a hit no longer knocks the hero
 * off a ladder. It registers itself; the host only adds its TCP command. */
void fax_ladder_hold_tcp_setup(void);
