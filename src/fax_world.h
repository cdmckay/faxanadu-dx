#pragma once
#include <stdbool.h>
#include <stdint.h>
void fax_world_reset(void);
void fax_world_loaded(void);
void fax_world_step(void);
bool fax_world_option(const char *name,const char *value);
void fax_world_event(const void *event);
const uint32_t *fax_world_present(int *w,int *h,const uint32_t *native);
/* The last fax_world_present returned Seamless's view (the remaster on):
 * the window then scales the picture by the host's one fixed rule
 * (cyc_host_extras.h sharp_scale), whatever the player's Display settings. */
bool fax_world_composed(void);
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
/* True while a neighbouring room's actors run their tick on its isolated
 * copy of the machine (Sprites_UpdateAll, with hooks allowed), against the
 * hero projected to that room's edge, invincible, not swinging and with no
 * spell: the hooks a feature runs there act on the copy, whose effects on
 * anything but the room's own bytes are thrown away. Seams lose nothing
 * (fax_seams.c) keeps those actors from touching him there. */
bool fax_world_ticking(void);
/* A correction for each room Seamless decodes itself, in isolated calls where
 * no hooks run: called with the room's area, its screen and its RAM (2 KiB,
 * ScreenBuffer at $600) once Area_LoadBlocks or Area_LoadScrollData has
 * filled it, before the room is drawn, collided with or adopted. NULL for
 * none. The graphics fixes (fax_graphics.c) set it. */
void fax_world_set_room_fixup(void (*fixup)(uint8_t area,uint8_t screen,uint8_t *ram));
/* ---- for Seams (fax_seams.c, #166) ----
 * Rooms are numbered by their screen in the current area. A room's place is
 * in world pixels: its top-left on the map Seamless lays out around the
 * active room, 256 wide and 208 tall per room, so two rooms side by side are
 * 256 apart. */
enum { FAX_WORLD_SEAMS_BYTES = 384 };
/* The room whose sprite loop the machine is running: inside a neighbour's
 * tick (fax_world_ticking()) that room, else the active room; -1 with
 * Seamless off or before it has a room. */
int fax_world_seams_room(void);
int fax_world_active_room(void);
/* The area (Area_CurrentArea) whose screens the rooms are: -1 with Seamless
 * off or before it has a map. */
int fax_world_area(void);
/* The room joined to `room` on `side` (0 left, 1 right) whose actors run
 * (started from its list): -1 for none, an arena, or one not started. */
int fax_world_joined(int room, int side);
/* Whether `room` is on the map and its actors run. */
bool fax_world_room_live(int room);
/* FAX_WORLD_SEAMS_BYTES of the room's that Seams keeps in Seamless's record
 * (so in save states): zero when the room is fresh. NULL off the map. */
uint8_t *fax_world_seams_block(int room);
/* The room's CPU RAM as Seamless keeps it: a neighbour's after its last
 * tick, the active room's as last captured (this frame's, between frames). */
uint8_t *fax_world_room_record(int room);
/* The room's top-left in world pixels. */
void fax_world_place(int room, int *x, int *y);
/* The view's top-left in world pixels (VIEW 424 wide, 208 tall below the
 * status bar), where fax_world_present puts it for the frame the machine is
 * running, whether or not F4's map shows: false when there is no view. It is
 * Seamless's camera (fax_camera.c, #166's slice 4), over rooms only where
 * it can be, so it need not be centred on him.
 * fax_world_update_view() works it out again from the live hero (the live
 * sprite loop's start); Seamless does too before its neighbours' ticks. */
bool fax_world_view(int *x, int *y);
void fax_world_update_view(void);
/* The hero's place (his top-left) in world pixels, from the live game, for
 * the same frame as fax_world_view(), and false when that has none: in a
 * neighbour's tick his true place, not the one projected to its edge. */
bool fax_world_hero(int *x, int *y);
/* A byte of the cartridge's PRG: `bank` (0-15) at `addr` as the CPU sees it
 * mapped there ($8000-$BFFF, or $C000-$FFFF for bank 15). */
uint8_t fax_world_rom(unsigned bank, unsigned addr);
/* Called by Seams when the game loads a screen's sprite list in play (a
 * door, a respawn, the original's scroll): the next refresh forgets every
 * other room of the area, so each comes back fresh from its list. */
void fax_world_screen_loaded(void);
/* Where the status bar band's column 0 (its 32 lines) went in the last
 * picture, `width` wide: Seamless centres the band and the Elixir left of it
 * together (fax_charges.h) while the status bar shows. Only the band: rooms,
 * the native picture below it and its text boxes stay centred in the view.
 * Without Seamless, the engine's own origin (0 at 256 wide). */
int fax_world_hud_x(int width);
