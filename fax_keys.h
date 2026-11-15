#pragma once
/* Auto keys and reusable tools (fax_keys.c): two mod packages, each its own
 * toggle, in one module because both act on the same door code. It registers
 * itself; the host only adds its TCP command. */
void fax_keys_tcp_setup(void);
