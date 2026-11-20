#pragma once
/* Slain by (fax_slain.c): when the hero dies, the zone names banner says what
 * killed him, "Slain by a Sugata", while he dissolves. It registers itself;
 * the host calls these. */
#include <stdint.h>

void fax_slain_frame_end(void);   /* the end of each machine frame */
/* The picture to present: `picture` (or, if NULL, the engine's own) with the
 * line in the banner while it shows; otherwise `picture` itself. */
const uint32_t *fax_slain_present(const uint32_t *picture, int *w, int *h);
void fax_slain_tcp_setup(void);
