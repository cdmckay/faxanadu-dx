#pragma once
/* Save files (fax_files.c): the title's NEW GAME and CONTINUE and their file
 * picks, a Guru's choice to save instead of the mantra, and continuing a
 * file. The record and
 * its API are in fax_save.h. */
#include <stdbool.h>
#include <stdint.h>

void fax_files_power_on(void);     /* from the extras' power_on */
void fax_files_frame_end(void);    /* from the extras' frame_end */
void fax_files_state_loaded(void); /* from the extras' state_loaded */
/* The title's menu, drawn over the title screen's picture. */
const uint32_t *fax_files_present(const uint32_t *picture, int *w, int *h);
void fax_files_tcp_setup(void);
/* A file is in play and Save files run the end of a Guru's conversation:
 * Guru travel leaves it alone then, until fax_travel_menu or
 * fax_travel_begin. */
bool fax_files_guru_choice(void);
/* For Guru travel: B in its menu, opened by "Send me to another Guru.", goes
 * back to the Guru's choice. Serves it again and returns the step to point
 * the script at, its menu shown when the interpreter reads it in this frame,
 * or 0 if it cannot be offered. */
uint16_t fax_files_guru_again(void);
/* For Guru travel: the dialogue box is opened again (TextBox_OpenForNPC), its
 * text rows laid out from the top, so its next line starts with the line
 * scroll at 0, not where the Guru's lines left it. */
void fax_files_box_reopened(void);
