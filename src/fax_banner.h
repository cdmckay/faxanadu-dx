#pragma once
/* Zone names (fax_banner.c): entering a different town, region, tower or
 * castle shows its name below the HUD for about two seconds, drawn over the
 * presented picture in the game's own font and text box. It registers itself;
 * the host calls these. */
#include <stdbool.h>
#include <stdint.h>

void fax_banner_reset(void);   /* the machine powered on */
void fax_banner_step(void);    /* the end of each machine frame */
/* The picture to present: `picture` (or, if NULL, the engine's own) with the
 * banner over it while one shows; otherwise `picture` itself. */
const uint32_t *fax_banner_present(const uint32_t *picture, int *w, int *h);
void fax_banner_tcp_setup(void);

/* The banner's box with `line` in it, below the HUD and centered, over
 * `picture` (or the engine's own) at alpha 0-255. Returns the banner's
 * canvas, or `picture` if there is nothing to draw or the font is not
 * loaded. For other features' lines: Slain by (fax_slain.c) says what killed
 * the hero. Drawn over a zone name if one was drawn this frame. */
const uint32_t *fax_banner_draw(const uint32_t *picture, int *w, int *h, const char *line, unsigned alpha);
/* Put away the zone name showing, as walking into a building does, for a
 * line that takes the banner's place. */
void fax_banner_hide(void);
/* A zone name is on the banner (or held behind a box or the pause): the
 * quest log waits for it to go before it says a quest is complete there. */
bool fax_banner_busy(void);
