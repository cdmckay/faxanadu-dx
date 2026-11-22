#pragma once
/* The Magician's lesson (fax_lesson.c): Eolis's Magician teaches the hero,
 * once, for gold, to recover MP faster, in place of his 40 MP sale. It
 * registers itself and keeps whether the hero has learned in its save-state
 * record and in the file in play (Save files' named field lesson.learned);
 * the host adds its TCP command and calls fax_lesson_loaded. */
#include <stdbool.h>

/* The hero has the lesson: MP refill (fax_mana.c) runs its faster
 * schedule. False with the remaster off. */
bool fax_lesson_learned(void);
void fax_lesson_tcp_setup(void);
/* After a save state has loaded: a line of his that was showing is put back
 * in message $C3's slot (read overrides are not saved). */
void fax_lesson_loaded(void);
