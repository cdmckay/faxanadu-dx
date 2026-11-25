#pragma once
/* Named fields of the player's progress kept beside the game (fax_remember.c),
 * a line naming each in a small text file, until save slots (#24) keep named
 * fields: Block's tutorial (faxanadu_block.txt) and Recharging items' first-use
 * lessons (faxanadu_recharge.txt). A field is set once and never cleared. With
 * saves off (--no-save) nothing is kept: fax_remembered is false and
 * fax_remember does nothing. */
#include <stdbool.h>

/* `field` is a line of `file` beside the game. */
bool fax_remembered(const char *file, const char *field);
/* Add `field` to `file`, keeping the fields already there; written whole to a
 * temporary file and renamed over it. */
void fax_remember(const char *file, const char *field);
