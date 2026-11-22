/* The original's graphics bugs, fixed. USA 57dd23d1, chipx86 byte-exact
 * disassembly. Five bugs in data the game draws from, each fixed where the
 * game reads it, so the ROM, the CHR it loads and every read override stay as
 * they are (see docs/GAME_INTERNALS.md, "Graphics bugs"):
 *
 * - Sprites. Sprite_Draw reads a sprite's tile info from bank 7: a tile slot
 *   and an attribute byte per cell. Three cells are wrong: Studded Mail's
 *   climbing pose names slot $34, which Studded Mail never loads (it loads
 *   slots 0-51), so the hero's chest shows whatever an earlier armor, shield
 *   or the Dragon Slayer left there; the Fire spell's second frame ends with
 *   its first frame's tile ($66) where its own top's ($7E) belongs, mirrored;
 *   and the Myconid's second frame has one foot in palette 2. Hooks at the
 *   instruction after each of Sprite_Draw_Standard's and
 *   Sprite_Draw_FlippedHoriz's loads (the tile's CLC, the attribute's EOR) put
 *   the right byte in A when the byte read is one of those three, so the game
 *   writes the right tile or palette into OAM itself.
 * - The title. Its attribute table (bank 10, START_SCREEN_PPU_ATTRIBUTES)
 *   gives the 16x16 square holding the tip of the logo's F (tile $03) the
 *   stars' grey palette, 3. A hook at PPU_WriteTilesFromCHRRAM's STA PPUDATA
 *   writes $00 for that byte: the logo's palette, as for the rest of the F.
 * - The Tower of Fortress. Trunk's screen $2D has block $63, the Mattock wall
 *   east of the Tower of Trunk, in the floor under the left pillar: a brown
 *   stone block in the green floor (and breakable into a pit that leads
 *   nowhere). A hook after Area_LoadScrollData decompresses the current
 *   screen puts the floor's block $08 there, as the Faxanadu Retranslation
 *   does in the ROM. With Seamless on, rooms are decompressed in isolated
 *   calls, which run no hooks, so Seamless asks the same correction of each
 *   room it decodes (fax_world_set_room_fixup).
 *
 * Each hook only looks at the machine and changes A or ScreenBuffer; none
 * keeps state, so they also run in Seamless's isolated rooms. What each fixed,
 * outside them, is counted for the TCP command. */
#include "fax_graphics.h"
#include "fax_world.h"
#include "cyc_mod.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#ifdef CYC_WITH_SDL
#include "cyc_tcp.h"
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Fixed bank */
#define DRAW_TILE         0xF131u  /* Sprite_Draw_Standard: CLC, ADC Sprites_PPUOffset after
                                      LDA (Sprite_TileInfo_ReadAddr_L),Y, the cell's tile slot */
#define DRAW_ATTRIBUTE    0xF13Bu  /* Sprite_Draw_Standard: EOR CurrentSprite_FlipMask after
                                      LDA (Sprite_TileInfo_ReadAddr_L),Y, the cell's attribute */
#define FLIPPED_TILE      0xF1E5u  /* Sprite_Draw_FlippedHoriz: the same two */
#define FLIPPED_ATTRIBUTE 0xF1EFu
#define TITLE_WRITE       0xF8B7u  /* PPU_WriteTilesFromCHRRAM: STA PPUDATA after LDA (IScriptOrCHRAddr),Y */
#define SCREEN_LOADED     0xD1BEu  /* Area_LoadScrollData: JSR MMC1_RestorePrevROMBank, right after
                                      LDA Area_CurrentScreen, JSR Area_LoadBlocks */
/* RAM */
#define TILEINFO_READ_L   0x003Au  /* Sprite_TileInfo_ReadAddr_L/_U: the cell bytes, past the header */
#define TILEINFO_READ_U   0x003Bu
#define CHR_ADDR_L        0x00DBu  /* IScriptOrCHRAddr/_U: what PPU_WriteTilesFromCHRRAM copies */
#define CHR_ADDR_U        0x00DCu
#define AREA_CURRENT      0x0024u
#define SCREEN_CURRENT    0x0063u
#define CURRENT_ROM_BANK  0x0100u
#define SCREEN_BUFFER     0x0600u
/* Values */
#define SPRITE_BANK       7u
#define TITLE_BANK        10u
#define FLAG_N            0x80u
#define FLAG_Z            0x02u

/* A byte of bank 7's tile info: what the game reads, and what it should. */
typedef struct { uint16_t addr; uint8_t stock, fixed; FaxGraphicsFix fix; } SpriteByte;
static const SpriteByte SPRITE_BYTES[] = {
    /* SPRITE_TILEINFO_PLAYER_ARMOR_STUDDED_07 ($ABF1, appearance 23), row 1, column 0: slot $34;
       slot $30 holds STUDDED_48 ($7D), the tile Studded Mail with a shield and Full Plate climb with */
    { 0xABF9u, 0x34u, 0x30u, FAX_GFX_STUDDED_CLIMB },
    /* The Fire spell's second frame (bank 7 $A5C3, appearance 156), row 3, column 1: $66, its
       first frame's; $7E, its own row 0's, mirrored, as the first frame mirrors its rows */
    { 0xA5D3u, 0x66u, 0x7Eu, FAX_GFX_FIRE },
    /* SPRITE_TILEINFO_ENEMY_MYCONID_01 ($95D3, appearance 50), row 3, column 1's attribute:
       palette 2; every other cell of both frames is palette 3 */
    { 0x95E6u, 0x02u, 0x03u, FAX_GFX_MYCONID },
};
#define TITLE_ATTRIBUTES  0x9B60u  /* START_SCREEN_PPU_ATTRIBUTES (bank 10): $23C0's 64 bytes */
#define TITLE_F_TIP       9u       /* x 32-63, y 32-63: top right (x 48-63, y 32-47) is palette 3 */
#define TITLE_F_TIP_STOCK 0x0Cu
#define TITLE_F_TIP_FIXED 0x00u

/* A block of a decompressed screen: what Area_LoadBlocks gives, and what it should. */
typedef struct { uint8_t area, screen, index, stock, fixed; } ScreenBlock;
static const ScreenBlock SCREEN_BLOCKS[] = {
    /* Trunk ($01), screen $2D (Tower of Fortress), row 11, column 3: block $63 (Trunk's Mattock
       wall) in the floor's 1B 08 1B 08 row, under the left pillar */
    { 0x01u, 0x2Du, 0xB3u, 0x63u, 0x08u },
};

static bool enabled;
static unsigned applied[FAX_GFX_FIXES];  /* bytes each fix changed, outside isolated calls */

uint8_t fax_graphics_sprite_byte(uint16_t addr, uint8_t value, FaxGraphicsFix *fix) {
    for (size_t i = 0; i < sizeof SPRITE_BYTES / sizeof *SPRITE_BYTES; ++i)
        if (SPRITE_BYTES[i].addr == addr && SPRITE_BYTES[i].stock == value) {
            *fix = SPRITE_BYTES[i].fix;
            return SPRITE_BYTES[i].fixed;
        }
    *fix = FAX_GFX_FIXES;
    return value;
}

uint8_t fax_graphics_title_byte(uint16_t addr, uint8_t value) {
    return addr == TITLE_ATTRIBUTES + TITLE_F_TIP && value == TITLE_F_TIP_STOCK ? TITLE_F_TIP_FIXED : value;
}

bool fax_graphics_fix_blocks(uint8_t area, uint8_t screen, uint8_t blocks[256]) {
    bool changed = false;
    for (size_t i = 0; i < sizeof SCREEN_BLOCKS / sizeof *SCREEN_BLOCKS; ++i) {
        const ScreenBlock *b = &SCREEN_BLOCKS[i];
        if (b->area == area && b->screen == screen && blocks[b->index] == b->stock) {
            blocks[b->index] = b->fixed;
            changed = true;
        }
    }
    return changed;
}

static void count(FaxGraphicsFix fix) {
    if (!cyc_mod_isolated()) ++applied[fix];
}

/* Put `value` in A, with N and Z as the LDA that loaded A would have set
 * them. The next instruction (CLC, EOR, STA) sets or ignores them anyway. */
static void set_a(CycModRegs *regs, uint8_t value) {
    regs->a = value;
    regs->p = (uint8_t)((regs->p & ~(FLAG_N | FLAG_Z)) | (value & FLAG_N) | (value ? 0 : FLAG_Z));
    cyc_mod_set_regs(regs);
}

/* After LDA (Sprite_TileInfo_ReadAddr_L),Y in either draw loop: A is the
 * tile info byte at ReadAddr + Y, with bank 7 mapped (Sprite_Draw's callers
 * map it). Only the three bytes above change. */
static int sprite_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_peek(CURRENT_ROM_BANK) != SPRITE_BANK) return 0;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    uint16_t at = (uint16_t)((cyc_mod_peek(TILEINFO_READ_L) | cyc_mod_peek(TILEINFO_READ_U) << 8) + regs.y);
    FaxGraphicsFix fix;
    uint8_t value = fax_graphics_sprite_byte(at, regs.a, &fix);
    if (fix == FAX_GFX_FIXES) return 0;
    set_a(&regs, value);
    count(fix);
    return 0;
}

/* At STA PPUDATA in PPU_WriteTilesFromCHRRAM's loop: A is the byte read from
 * IScriptOrCHRAddr + Y in the bank it mapped. The title's attribute table is
 * the only one it copies from bank 10's $9B60. */
static int title_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_peek(CURRENT_ROM_BANK) != TITLE_BANK) return 0;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    uint16_t from = (uint16_t)((cyc_mod_peek(CHR_ADDR_L) | cyc_mod_peek(CHR_ADDR_U) << 8) + regs.y);
    uint8_t value = fax_graphics_title_byte(from, regs.a);
    if (value == regs.a) return 0;
    set_a(&regs, value);
    count(FAX_GFX_TITLE);
    return 0;
}

/* Right after Area_LoadScrollData decompressed the current screen into
 * ScreenBuffer (the four neighbors' loads before it only lend it their edge
 * rows and columns), before anything draws or collides with it. */
static int screen_hook(uint16_t addr) {
    (void)addr;
    uint8_t area = cyc_mod_peek(AREA_CURRENT), screen = cyc_mod_peek(SCREEN_CURRENT);
    uint8_t blocks[256];
    for (unsigned i = 0; i < 256; ++i) blocks[i] = cyc_mod_peek((uint16_t)(SCREEN_BUFFER + i));
    if (!fax_graphics_fix_blocks(area, screen, blocks)) return 0;
    for (unsigned i = 0; i < 256; ++i)
        if (cyc_mod_peek((uint16_t)(SCREEN_BUFFER + i)) != blocks[i]) cyc_mod_poke((uint16_t)(SCREEN_BUFFER + i), blocks[i]);
    count(FAX_GFX_FORTRESS);
    return 0;
}

/* Seamless's rooms: their RAM, as its isolated calls left it. */
static void room_fixup(uint8_t area, uint8_t screen, uint8_t *ram) {
    fax_graphics_fix_blocks(area, screen, ram + SCREEN_BUFFER);
}

static const char *const hook_ids[] = {
    "faxanadu.graphics-fix.tile", "faxanadu.graphics-fix.attribute",
    "faxanadu.graphics-fix.flipped-tile", "faxanadu.graphics-fix.flipped-attribute",
    "faxanadu.graphics-fix.title", "faxanadu.graphics-fix.screen",
};
#define HOOKS (sizeof hook_ids / sizeof *hook_ids)

static void set_hooks(int on) {
    for (size_t i = 0; i < HOOKS; ++i) nes_mod_set_function_hook_enabled(hook_ids[i], on);
}
static void deactivate(void) {
    enabled = false;
    for (unsigned i = 0; i < FAX_GFX_FIXES; ++i) applied[i] = 0;
    set_hooks(0);
    fax_world_set_room_fixup(NULL);
}
static void activate(void) {
    enabled = true;
    set_hooks(1);
    fax_world_set_room_fixup(room_fixup);
    nes_mod_set_local_only("Graphics fixes", 1);
}

#ifdef CYC_WITH_SDL
static void graphics_state(int id, const char *line) {
    (void)line;
    char fields[192];
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"studded_climb\":%u,\"fire\":%u,\"myconid\":%u,\"title\":%u,\"fortress\":%u",
             enabled ? "true" : "false", applied[FAX_GFX_STUDDED_CLIMB], applied[FAX_GFX_FIRE],
             applied[FAX_GFX_MYCONID], applied[FAX_GFX_TITLE], applied[FAX_GFX_FORTRESS]);
    cyc_tcp_ok(id, fields);
}
void fax_graphics_tcp_setup(void) {
    cyc_tcp_register("graphics_fix_state",
                     "Graphics fixes: whether on, and how many bytes each fix changed (studded_climb, fire, "
                     "myconid, title, fortress)",
                     graphics_state);
}
#else
void fax_graphics_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_graphics) {
    if (!nes_mod_register_function_entry_plugin(hook_ids[0], DRAW_TILE, sprite_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[1], DRAW_ATTRIBUTE, sprite_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[2], FLIPPED_TILE, sprite_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[3], FLIPPED_ATTRIBUTE, sprite_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[4], TITLE_WRITE, title_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[5], SCREEN_LOADED, screen_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.graphics-fix", activate)) abort();
}
