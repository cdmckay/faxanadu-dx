#pragma once
/* Block (fax_block.c): hold a bindable button (V, or B on a controller) with a
 * shield (or the Battle Helmet) in hand to stand still and stop enemy magic
 * from the front, and Sugata's flash from anywhere. The game teaches it once,
 * after the first shield-slot item, and shows: the hero glints as Block goes
 * up. It registers its own hooks; the host
 * passes it window events, the controller bytes and its config.ini [Game]
 * keys, and adds its TCP command and Controls row. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "cyc_host_extras.h"

void fax_block_event(const void *sdl_event);
/* The NES controller bytes for the next frame: while Block is up in play, the
 * hero stands still (no d-pad, A or B). */
void fax_block_input(uint8_t buttons[2]);
/* BlockKey and BlockButton; false for any other key. */
bool fax_block_load_setting(const char *key, const char *value);
void fax_block_save_settings(FILE *f);
/* "Block: V / B", the runtime menu's Controls row. The buffer stays put and
 * follows the bindings. */
const char *fax_block_controls_label(void);
/* The binding, for the launcher's Controls page and the layout's move (cyc_host_extras.h). */
const CycHostBinding *fax_block_binding(void);
void fax_block_frame_end(void);
/* A save state was loaded: put the tutorial's line back if it was showing. */
void fax_block_loaded(void);
void fax_block_tcp_setup(void);

/* Block is up: the feature is on, its button is held, and the shield slot
 * holds something (the Small, Large or Magic Shield, or the Battle Helmet).
 * Never inside an isolated machine. */
bool fax_block_raised(void);
/* For a hook at a site of the running game: make the next RTS go to `to`
 * first, then where it would have gone. And play one of the game's sounds,
 * as Sound_PlayEffect would. Both keep the registers. */
void fax_block_return_via(uint16_t to);
bool fax_block_play_sound(uint8_t sound);
