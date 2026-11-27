#pragma once
/* Seamless's camera (#166's slice 4: the view shows only rooms). A pure
 * function of the map and the hero's place, so the view is the same however
 * often a frame asks for it, and a save state or a rewind needs nothing
 * kept: fax_world.c asks it for the picture (fax_world_present) and for the
 * view Seams' actors wake in (fax_world_update_view). See fax_camera.c. */
#include <stdbool.h>

/* The view below the status bar, and a room, in pixels. */
enum { FAX_CAMERA_W = 424, FAX_CAMERA_H = 208, FAX_CAMERA_ROOM_W = 256, FAX_CAMERA_ROOM_H = 208 };

typedef struct {
    /* Whether a room lies at (col, row) of the map, in rooms. */
    bool (*room)(void *ctx, int col, int row);
    /* Where the hero can pass between the room at (col, row - 1) and the one
     * at (col, row) under it: bit b for its block column b (0 the left), 0
     * when the two aren't joined. NULL: nowhere. */
    unsigned (*openings)(void *ctx, int col, int row);
    void *ctx;
} FaxCameraMap;

/* The view's top-left in world pixels (the room at map place (0, 0) has its
 * top-left at 0, 0), with the hero's top-left (his 16x32) at (hx, hy). */
void fax_camera_place(const FaxCameraMap *map, int hx, int hy, int *x, int *y);
