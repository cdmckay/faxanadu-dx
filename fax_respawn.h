#pragma once
/* Zone respawn (fax_respawn.c). It registers itself; the host only adds its
 * TCP command. */
#include <stdbool.h>
#include <stdint.h>

void fax_respawn_tcp_setup(void);

/* The checkpoint as bytes, for whatever keeps it beyond a save state (the
 * save slots of #24, when they land). Pack writes FAX_RESPAWN_CHECKPOINT_LEN
 * bytes; unpack takes them, or 0 bytes for no checkpoint, and refuses a
 * checkpoint the game could not respawn at. */
enum { FAX_RESPAWN_CHECKPOINT_LEN = 8 };
void fax_respawn_checkpoint_pack(uint8_t out[FAX_RESPAWN_CHECKPOINT_LEN]);
bool fax_respawn_checkpoint_unpack(const uint8_t *in, int len);
