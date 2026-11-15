/* Block (fax_block.c) linked into Sugata's flash's unit tests, with the
 * button held or not. */
#include "fax_block.c"
void test_block_hold(bool on) { key_held = on; }
unsigned test_block_flashes(void) { return flashes_blocked; }
