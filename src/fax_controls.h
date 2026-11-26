#pragma once
/* The controller layout (fax_controls.c, #147): with the remaster, Start
 * opens the Select menu and closes it, and never pauses; in play, Back (the
 * NES Select's, and the Map button's) opens the map and not the menu. With
 * the remaster off the pad is the original's. It also moves a config.ini
 * written before this layout onto its defaults, once, and names Start's
 * binding in the runtime menu's Controls rows. */
#include <stdbool.h>
#include <stdio.h>

/* ControlsLayout; false for any other key. */
bool fax_controls_load_setting(const char *key, const char *value);
void fax_controls_save_settings(FILE *f);
/* After the whole of config.ini: a file of layout 1 (no ControlsLayout) has
 * each of the game's bindings still on layout 1's default moved to the
 * current one. True when it moved any, so the host saves the file. */
bool fax_controls_settings_loaded(void);
/* "Select menu: RETURN / START", from the NES Start's binding, refreshed at
 * power-on. The buffer stays put. */
const char *fax_controls_menu_label(void);
/* A controller binding (cyc_input.h's encoding) held on any open controller,
 * as SDL reads it now: a button, or an axis past half travel. SDL builds. */
bool fax_controls_pad_held(int pad);
void fax_controls_power_on(void);
void fax_controls_tcp_setup(void);
