#pragma once
/* The Use item button (fax_use_item.c): a bindable button that uses the
 * selected item, as Down+B does, under the game's rule. It registers its own
 * hooks; the host passes it window events and its config.ini [Game] keys, and
 * adds its TCP command, Controls row and launcher row. */
#include <stdbool.h>
#include <stdio.h>
#include "cyc_host_extras.h"

void fax_use_item_event(const void *sdl_event);
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
