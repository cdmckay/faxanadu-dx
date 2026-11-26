#pragma once
/* Taller dagger (fax_dagger.c). The step serves the dagger's reach once the
 * cart is loaded; call it at power-on and at the end of each frame. The host
 * also adds its TCP command. */
#include <stdint.h>
void fax_dagger_step(void);
/* The dagger's Y reach as the game reads it: $08 once served, else $04. */
uint8_t fax_dagger_reach_y(void);
void fax_dagger_tcp_setup(void);
