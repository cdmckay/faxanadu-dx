#pragma once
/* The original's graphics bugs, fixed (fax_graphics.c). It registers itself;
 * the host only adds its TCP command. */
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    FAX_GFX_STUDDED_CLIMB, /* Studded Mail's climbing pose: a tile slot nothing loads */
    FAX_GFX_FIRE,          /* the Fire spell's second frame: a tile of its first */
    FAX_GFX_MYCONID,       /* the Myconid's second frame: one tile in the wrong palette */
    FAX_GFX_TITLE,         /* the title: the F's tip in the stars' grey */
    FAX_GFX_FORTRESS,      /* the Tower of Fortress ($2D): the Mattock wall's block in the floor */
    FAX_GFX_FIXES
} FaxGraphicsFix;

/* A byte of a sprite's tile info as Sprite_Draw reads it, from bank 7 at
 * `addr`: the byte to draw with, and in *fix which fix changed it
 * (FAX_GFX_FIXES when none did; every other byte is the stock one). */
uint8_t fax_graphics_sprite_byte(uint16_t addr, uint8_t value, FaxGraphicsFix *fix);
/* A byte PPU_WriteTilesFromCHRRAM copies from bank 10 at `addr`: the byte to
 * write (the title's attribute table, corrected). */
uint8_t fax_graphics_title_byte(uint16_t addr, uint8_t value);
/* Correct a screen's blocks as Area_LoadBlocks decompressed them (its
 * ScreenBuffer, 256 blocks, row x 16 + column) for `area`'s `screen`. Returns
 * whether it changed one. */
bool fax_graphics_fix_blocks(uint8_t area, uint8_t screen, uint8_t blocks[256]);

void fax_graphics_tcp_setup(void);
