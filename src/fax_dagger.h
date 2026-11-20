#pragma once
/* Taller dagger (fax_dagger.c). The step serves the dagger's reach once the
 * cart is loaded; call it at power-on and at the end of each frame. The host
 * also adds its TCP command. */
void fax_dagger_step(void);
void fax_dagger_tcp_setup(void);
