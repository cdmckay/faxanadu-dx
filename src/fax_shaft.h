#pragma once
/* Eolis's edge (fax_shaft.c): the open column at the right edge of Eolis $00
 * and $08 stands solid down to the floor, so a flight can't end in the shaft
 * beneath it, and a hero found there is put back on the floor. It registers
 * itself; the host only adds its TCP command. */
#include <stdbool.h>
#include <stdint.h>

/* Whether ScreenBuffer's block `index` (row x 16 + column) of `area`'s
 * `screen` stands solid: column 15, rows 0-11, of Eolis $00 and $08. */
bool fax_shaft_solid(uint8_t area, uint8_t screen, uint8_t index);
/* Whether a hero at (x, y) on `area`'s `screen` is in the shaft (column 15 of
 * $00 or $08) or under Eolis's floor, and if so, where he goes: onto the floor
 * where he is, or next to the wall on $00 and $08. */
bool fax_shaft_rescue(uint8_t area, uint8_t screen, uint8_t x, uint8_t y, uint8_t *to_x, uint8_t *to_y);

void fax_shaft_tcp_setup(void);
