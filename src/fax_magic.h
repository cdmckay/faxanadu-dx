#pragma once
/* Magic button (fax_magic.c): a bindable button that casts the selected spell,
 * as Up+B does. It registers its own hooks; the host passes it window events
 * and its config.ini [Game] keys, and adds its TCP command and Controls row. */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include "cyc_host_extras.h"

void fax_magic_event(const void *sdl_event);
/* MagicKey and MagicButton; false for any other key. */
bool fax_magic_load_setting(const char *key, const char *value);
void fax_magic_save_settings(FILE *f);
/* "Magic: C / Y", the runtime menu's Controls row. The buffer stays put and
 * follows the bindings. */
const char *fax_magic_controls_label(void);
/* The binding, for the launcher's Controls page (cyc_host_extras.h). */
const CycHostBinding *fax_magic_binding(void);
void fax_magic_frame_end(void);
void fax_magic_tcp_setup(void);
