#pragma once
/* Guru travel (fax_travel.c). The step supplies its script once the cart is
 * loaded; call it at power-on and at the end of each frame. */
void fax_travel_step(void);
void fax_travel_tcp_setup(void);

/* A Guru's conversation has begun: a temple Guru's at his SET_SPAWN_POINT,
 * Eolis's at his first action. `temple` is 0-7. Features that act when the
 * hero talks to a Guru listen here rather than hook the script interpreter
 * again; it works whether or not Guru travel is on. Register in activation,
 * and stop (on = false) in the reset callback. Up to four listeners. */
#include <stdbool.h>
typedef void (*FaxGuruListener)(unsigned temple);
void fax_travel_listen(FaxGuruListener fn, bool on);
