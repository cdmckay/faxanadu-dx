#pragma once
/* The Use item button (fax_use_item.c): a bindable button that uses the
 * selected item, as Down+B does, under the game's rule. On a controller it is
 * the NES B outside play: in a text box, a menu, a shop, the pause's screens
 * and the title, it backs out (#160). It registers its own hooks; the host
 * passes it window events, the pad bytes and its config.ini [Game] keys, and
 * adds its TCP command, Controls row and launcher row. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "cyc_host_extras.h"

void fax_use_item_event(const void *sdl_event);
/* The next frame's pad bytes, after the host's mapping (cyc_host_extras.h
 * input): the NES B, while the controller's press is a back press. */
void fax_use_item_input(uint8_t buttons[2]);
/* UseItemKey and UseItemButton; false for any other key. */
bool fax_use_item_load_setting(const char *key, const char *value);
void fax_use_item_save_settings(FILE *f);
/* "Use item: S / B", the runtime menu's Controls row. The buffer stays put
 * and follows the bindings. */
const char *fax_use_item_controls_label(void);
/* The binding, for the launcher's Controls page (cyc_host_extras.h). */
const CycHostBinding *fax_use_item_binding(void);
void fax_use_item_frame_end(void);
void fax_use_item_tcp_setup(void);
