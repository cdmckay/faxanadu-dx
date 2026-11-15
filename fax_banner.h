#pragma once
/* Zone names (fax_banner.c): entering a different town, region, tower or
 * castle shows its name below the HUD for about two seconds, drawn over the
 * presented picture in the game's own font and text box. It registers itself;
 * the host calls these. */
#include <stdint.h>

void fax_banner_reset(void);   /* the machine powered on */
void fax_banner_step(void);    /* the end of each machine frame */
/* The picture to present: `picture` (or, if NULL, the engine's own) with the
 * banner over it while one shows; otherwise `picture` itself. */
const uint32_t *fax_banner_present(const uint32_t *picture, int *w, int *h);
void fax_banner_tcp_setup(void);
