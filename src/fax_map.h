#pragma once
/* The map (fax_map.c): a Map button (M, or LT on a controller) pauses the
 * game and shows every screen the hero has been to, a page per region, tower
 * or castle, laid out by fax_map_layout.c. Left and right turn the page; B,
 * Start or Map close it. What he has seen is kept in the file in play. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

void fax_map_frame_end(void);
const uint32_t *fax_map_present(const uint32_t *picture, int *w, int *h);
void fax_map_event(const void *event);
bool fax_map_load_setting(const char *key, const char *value);
void fax_map_save_settings(FILE *f);
const char *fax_map_controls_label(void);   /* "Map: M / LT", for the runtime menu */
void fax_map_tcp_setup(void);
bool fax_map_showing(void);                 /* the map is open */
/* The hero has stood on `area`'s `screen` (outside), as the map has it: for
 * the quest log, which counts the places he has reached. */
bool fax_map_seen(unsigned area, unsigned screen);
