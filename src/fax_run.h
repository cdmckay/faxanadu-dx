#pragma once
/* Run (fax_run.c, #198): hold a bindable button (Left Shift, or the left
 * trigger on a controller) and the hero walks 1.5 times as fast on the
 * ground. Not in the air, on a ladder, in Wing Boots flight or knockback, nor
 * anywhere but play. It registers its own hooks; the host passes it window
 * events and its config.ini [Game] keys, and adds its TCP command, Controls
 * row and launcher row. */
#include <stdbool.h>
#include <stdio.h>
#include "cyc_host_extras.h"

void fax_run_event(const void *sdl_event);
/* RunKey and RunButton; false for any other key. */
bool fax_run_load_setting(const char *key, const char *value);
void fax_run_save_settings(FILE *f);
/* "Run: LEFT SHIFT / LT", the runtime menu's Controls row. The buffer stays
 * put and follows the bindings. */
const char *fax_run_controls_label(void);
/* The binding, for the launcher's Controls page and the layout's move (cyc_host_extras.h). */
const CycHostBinding *fax_run_binding(void);
void fax_run_tcp_setup(void);
