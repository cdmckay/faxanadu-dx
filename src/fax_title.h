#pragma once
/* REMASTERED on the title (fax_title.c): the word under the FAXANADU logo,
 * drawn over the presented picture while the title screen's loop runs, in the
 * game's dialogue font and the logo's own palette. It registers itself; the
 * host calls these. */
#include <stdbool.h>
#include <stdint.h>

void fax_title_power_on(void);   /* from the extras' power_on */
void fax_title_frame_end(void);  /* from the extras' frame_end */
/* The picture to present: `picture` (or, if NULL, the engine's own) with the
 * word over it while the title is up; otherwise `picture` itself. */
const uint32_t *fax_title_present(const uint32_t *picture, int *w, int *h);
void fax_title_tcp_setup(void);
