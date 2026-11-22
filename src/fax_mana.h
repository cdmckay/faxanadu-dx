#pragma once
/* MP refill (fax_mana.c): once the hero has spent no MP for 4 seconds,
 * Player_MP climbs back toward full, 1 MP every 30 frames, while the game is
 * playing; after the Magician's lesson (fax_lesson.c), 2 seconds and 15
 * frames. It registers itself and keeps its countdown in its save-state
 * record; the host calls it at the end of each frame and adds its TCP
 * command. */

void fax_mana_frame_end(void);
void fax_mana_tcp_setup(void);
