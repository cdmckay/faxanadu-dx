#pragma once
#include <stdbool.h>
#include <stdint.h>
void fax_world_reset(void);
void fax_world_loaded(void);
void fax_world_step(void);
bool fax_world_option(const char *name,const char *value);
void fax_world_event(const void *event);
const uint32_t *fax_world_present(int *w,int *h,const uint32_t *native);
void fax_world_tcp_setup(void);
/* Where the active room's top-left (native column 0, the first line under
 * the HUD) sat in the last picture fax_world_present composed of the world;
 * false when it presented none (off, or a scene it shows as the original's). */
bool fax_world_origin(int *x,int *y);
