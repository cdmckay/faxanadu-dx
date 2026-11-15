#pragma once
/* Save files (fax_files.c): the file select on the title screen, a Guru
 * saving instead of showing the mantra, and continuing a file. The record and
 * its API are in fax_save.h. */
#include <stdbool.h>
#include <stdint.h>

void fax_files_power_on(void);       /* from the extras' power_on */
void fax_files_frame_end(void);      /* from the extras' frame_end */
void fax_files_state_loaded(void);   /* from the extras' state_loaded */
/* The file select, drawn over the title screen's picture. */
const uint32_t *fax_files_present(const uint32_t *picture, int *w, int *h);
void fax_files_tcp_setup(void);
/* A file is in play and Save files run the end of a Guru's conversation:
 * Guru travel leaves it alone then, until fax_travel_begin. */
bool fax_files_guru_choice(void);
