#pragma once
/* The controller layout (fax_controls.c, #147): with the remaster, Start
 * opens the Select menu and closes it, and never pauses; a controller button
 * bound to one of the remaster's actions is the action's, so Back (the NES
 * Select's, and the Map button's) opens the map and not the menu. With the
 * remaster off the pad is the original's. It also moves a config.ini written
 * before this layout onto its defaults, once, and names Start's binding in
 * the runtime menu's Controls rows. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* The pad byte before the NES reads it (cyc_host_extras.h input): the NES
 * buttons the host binds to a held action's controller button, taken out. */
void fax_controls_input(uint8_t buttons[2]);

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
void fax_controls_power_on(void);
void fax_controls_tcp_setup(void);
