#pragma once
/* The Magician's lesson (fax_lesson.c): Eolis's Magician teaches the hero,
 * once, for gold, to recover MP faster (for spells, and for the Wing Boots
 * and the Hour Glass, which run on it), in place of his 40 MP sale, and only
 * a hero who has studied at the magic hall in the Mist (bought Fire there).
 * It registers itself and keeps whether the hero has studied and learned in
 * its save-state record and in the file in play (Save files' named fields
 * lesson.studied and lesson.learned); the host adds its TCP command and calls
 * fax_lesson_loaded. */
#include <stdbool.h>

/* The hero has the lesson: MP refill (fax_mana.c) runs its faster
 * schedule. False with the remaster off. */
bool fax_lesson_learned(void);
/* The hero has studied at the magic hall: its Magician gave him Fire. False
 * with the remaster off. */
bool fax_lesson_studied(void);
void fax_lesson_tcp_setup(void);
/* After a save state has loaded: a line of his that was showing is put back
 * in message $C3's slot (read overrides are not saved), and a state from
 * before the studied flag takes it from the spells in RAM. */
void fax_lesson_loaded(void);
