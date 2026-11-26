/* Block (fax_block.c) linked into Sugata's flash's unit tests, with the
 * button held or not. */
#include "fax_block.c"
#include "model.h"
/* Seams (fax_seams.c): the box's left in 16 bits, for Block's magic from the
 * front; the flash has no box. */
int fax_seams_box_left(void) { return ram[SPRITE_BOX_LEFT]; }
void test_block_hold(bool on) { key_held = on; }
unsigned test_block_flashes(void) { return flashes_blocked; }
