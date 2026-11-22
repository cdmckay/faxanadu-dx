#pragma once
/* MP from kills (fax_mana.c): every kill the game gives experience for, by
 * weapon or magic, gives MP, from the enemy's experience (a boss 40), up to
 * the cap of 80; the Magician's lesson (fax_lesson.c) doubles it. Nothing
 * comes back over time. Talking to a Guru fills HP and MP. It registers
 * itself and keeps the bars it has still to draw in its save-state record;
 * the host calls it at the end of each frame and adds its TCP command. */
#include <stdint.h>

void fax_mana_frame_end(void);
void fax_mana_tcp_setup(void);
/* A kill of `entity` that the game gave experience for in a committed call,
 * which runs no hooks (Sugata turned back on itself: fax_sugata.c). Bank 14
 * must be mapped, as it is in that code. Nothing when off. */
void fax_mana_killed(uint8_t entity);
/* A Guru's conversation begins (Recharging items' guru_hook, at his first
 * script action): HP and MP to 80, and their bars. Nothing when off. */
void fax_mana_guru(void);
