#pragma once
/* Sugata's flash (fax_sugata.c): Sugata blinks for half a second before it
 * flashes, and its range shows as a faintly dimmed band of the screen; the
 * flash greys only that band and hurts only there; Block (fax_block.c) stops
 * it, and with the Magic Shield turns it back on Sugata. It registers its own
 * hooks; the host calls these. */
#include <stdbool.h>
#include <stdint.h>

void fax_sugata_frame_end(void);
/* The picture to present: `picture` (Seamless's canvas, or if NULL the
 * engine's own) with the band drawn over the screen while one shows;
 * otherwise `picture` itself. */
const uint32_t *fax_sugata_present(const uint32_t *picture, int *w, int *h);
void fax_sugata_tcp_setup(void);

/* The rules, in screen pixels (exposed for the unit tests). The flash reaches
 * the band [center - SUGATA_REACH, center + SUGATA_REACH) around Sugata's
 * middle, and hurts the hero when any of his 16 pixels is in it. */
enum { SUGATA_REACH = 40, SUGATA_WARNING = 30, SUGATA_REFLECTED = 12 };
bool fax_sugata_reaches(uint8_t hero_x, int center);
