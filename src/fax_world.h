#pragma once
#include <stdbool.h>
#include <stdint.h>
void fax_world_reset(void);
void fax_world_loaded(void);
void fax_world_step(void);
bool fax_world_option(const char *name,const char *value);
void fax_world_event(const void *event);
const uint32_t *fax_world_present(int *w,int *h,const uint32_t *native);
void fax_world_tcp_setup(void);
/* Where the active room's top-left (native column 0, the first line under
 * the HUD) sat in the last picture fax_world_present composed of the world;
 * false when it presented none (off, or a scene it shows as the original's). */
bool fax_world_origin(int *x,int *y);
/* The same for any room of `area` placed in the map around the active one:
 * where its top-left sat in that picture. False when the picture showed no
 * world, or showed another area, or the room is not on its map. */
bool fax_world_room_origin(int area,int room,int *x,int *y);
/* The CPU RAM of a neighbouring room of `area` whose actors Seamless keeps
 * running (its sprite slots, as CurrentSprites_* hold them): NULL for the
 * active room, a room not on the map around it, or one not running. For the
 * quest log's marks over the people next door. */
const uint8_t *fax_world_room_ram(int area,int room);
/* A correction for each room Seamless decodes itself, in isolated calls where
 * no hooks run: called with the room's area, its screen and its RAM (2 KiB,
 * ScreenBuffer at $600) once Area_LoadBlocks or Area_LoadScrollData has
 * filled it, before the room is drawn, collided with or adopted. NULL for
 * none. The graphics fixes (fax_graphics.c) set it. */
void fax_world_set_room_fixup(void (*fixup)(uint8_t area,uint8_t screen,uint8_t *ram));
/* Where the status bar band's column 0 (its 32 lines) went in the last
 * picture, `width` wide: Seamless centres the band and the recharging items'
 * row past it together (fax_charges.h) while the status bar shows. Only the
 * band: rooms, the native picture below it and its text boxes stay centred
 * in the view. Without Seamless, the engine's own origin (0 at 256 wide). */
int fax_world_hud_x(int width);
