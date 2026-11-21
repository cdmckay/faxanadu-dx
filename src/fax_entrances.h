#pragma once
/* Fair entrances (fax_entrances.c). It registers itself; the host adds its TCP
 * command, and Seamless (fax_world.c) calls fax_entrances_room_loaded after it
 * loads a neighbouring room's sprites on an isolated machine, where hooks do
 * not run. */
void fax_entrances_tcp_setup(void);
/* Moves the enemies of the screen in Area_CurrentArea/Area_CurrentScreen whose
 * sprite slots still hold their placement, on whichever machine is current.
 * Does nothing while the feature is off. Returns how many it moved. */
unsigned fax_entrances_room_loaded(void);
