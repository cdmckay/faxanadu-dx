#pragma once
/* The map (fax_map.c): a Map button (M, or LT on a controller) pauses the
 * game and shows every screen the hero has been to, a page per region, tower
 * or castle, laid out by fax_map_layout.c. Left and right turn the page; B,
 * Start or Map close it. What he has seen is kept in the file in play. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "cyc_host_extras.h"

void fax_map_frame_end(void);
const uint32_t *fax_map_present(const uint32_t *picture, int *w, int *h);
void fax_map_event(const void *event);
bool fax_map_load_setting(const char *key, const char *value);
void fax_map_save_settings(FILE *f);
const char *fax_map_controls_label(void); /* "Map: M / BACK", for the runtime menu */
/* The binding, for the launcher's Controls page and the layout's move (cyc_host_extras.h). */
const CycHostBinding *fax_map_binding(void);
void fax_map_tcp_setup(void);
bool fax_map_showing(void); /* the map is open */
/* Open the map as the Map button does, at the main loop's next pause check:
 * the Select menu's MAP row (fax_menu.c). False where it would show nothing
 * (no page seen yet), or the quest log is open. */
bool fax_map_request(void);
/* The hero has stood on `area`'s `screen` (outside), as the map has it: for
 * the quest log, which counts the places he has reached. */
bool fax_map_seen(unsigned area, unsigned screen);
