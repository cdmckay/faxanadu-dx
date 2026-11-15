#pragma once
/* Hold A to speed up text (fax_text_speed.c). The feature runs from its hook;
 * the host only registers its TCP command. */
void fax_text_speed_tcp_setup(void);
