#pragma once
/* Kept finds (fax_finds.c): with the Items list full, a find stays where it
 * lies instead of vanishing. It registers itself; the host only adds its TCP
 * command. */
void fax_finds_tcp_setup(void);
