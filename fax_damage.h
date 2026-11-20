#pragma once
/* Damage numbers (fax_damage.c): every hit that takes HP shows how much, in
 * the HUD's own digits, floating up from where it landed and fading. Light
 * for damage the hero deals, pink for damage he takes. It registers its own
 * hooks, which only observe; the host calls these. */
#include <stdbool.h>
#include <stdint.h>

void fax_damage_frame_end(void);
/* The picture to present: `picture` (Seamless's canvas, or if NULL the
 * engine's own) with the numbers drawn over it while any show; otherwise
 * `picture` itself. */
const uint32_t *fax_damage_present(const uint32_t *picture, int *w, int *h);
void fax_damage_tcp_setup(void);

/* For a hit the game deals outside the hooked sites: `amount` HP off the
 * enemy in sprite slot `slot`, now, at its place. Sugata's flash
 * (fax_sugata.c) reflects with a committed call, where no hook runs. */
void fax_damage_enemy_hit(unsigned slot, unsigned amount);

/* The rules (exposed for the unit tests). A number lives FAX_DAMAGE_LIFE
 * frames, rising a pixel every FAX_DAMAGE_STEP frames (15 pixels in the
 * first 3/4 s), and fades over its last FAX_DAMAGE_FADE. Every number rises
 * at the same pace, so two that do not overlap when the newer appears never
 * do. At most FAX_DAMAGE_MAX show; a new one takes the oldest's place. */
enum { FAX_DAMAGE_MAX = 16, FAX_DAMAGE_LIFE = 60, FAX_DAMAGE_FADE = 16, FAX_DAMAGE_STEP = 3 };
int fax_damage_rise(unsigned age);       /* pixels risen at `age` frames */
unsigned fax_damage_alpha(unsigned age); /* 255 down to 0 */
/* The hero's damage, from Player_ReduceHP's Arg_PlayerHealthDelta (whole HP
 * and 1/256ths, as armor leaves it): rounded to the nearest whole HP, and at
 * least 1 when it takes anything. 0 takes nothing. */
unsigned fax_damage_hero_amount(uint8_t whole, uint8_t fraction);
