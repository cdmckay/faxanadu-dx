/* Unit tests for the graphics fixes (fax_graphics.c), without the emulator or
 * a ROM. The module runs against a model of the machine: CPU RAM, the
 * registers, the mapped bank, and the three routines it hooks, played as the
 * disassembly has them, with the hook sites where game.toml puts them:
 *
 * - Sprite_Draw_Standard and Sprite_Draw_FlippedHoriz, cell by cell, on the
 *   tile info of the poses concerned (the bytes of bank 7, copied below),
 *   giving the (slot, attribute) each cell writes to OAM. The checks compare
 *   poses with the ones the game draws right: Studded Mail's climbing pose
 *   with Studded Mail and a shield's, the Fire spell's second frame with the
 *   symmetry of its first, the Myconid's second frame with its first.
 * - PPU_WriteTilesFromCHRRAM copying the title's attribute table (bank 10's
 *   64 bytes, copied below) to the PPU.
 * - Area_LoadScrollData's tail, after the current screen is decompressed,
 *   on a model of Trunk's $2D floor row, and Seamless's rooms.
 *
 * So the tests check what the game draws, not the module's own tables. */
#include "fax_graphics.c"
#include <string.h>

static unsigned checks;
#define CHECK(test)                                                                                                    \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (!(test)) {                                                                                                 \
            fprintf(stderr, "FAIL line %u: %s\n", __LINE__, #test);                                                    \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)

/* ---- the machine ---- */
static uint8_t ram[0x800];
static CycModRegs cpu;
static bool isolated;
static int hook_on[HOOKS];
static void (*seamless_fixup)(uint8_t, uint8_t, uint8_t *);

uint8_t cyc_mod_peek(uint16_t addr) { return addr < 0x2000 ? ram[addr & 0x7FF] : 0; }
bool cyc_mod_poke(uint16_t addr, uint8_t value) {
    if (addr >= 0x2000) return false;
    ram[addr & 0x7FF] = value;
    return true;
}
void cyc_mod_regs(CycModRegs *out) { *out = cpu; }
void cyc_mod_set_regs(const CycModRegs *in) {
    uint16_t pc = cpu.pc;
    cpu = *in;
    cpu.pc = pc;
}
bool cyc_mod_isolated(void) { return isolated; }
void fax_world_set_room_fixup(void (*fixup)(uint8_t, uint8_t, uint8_t *)) { seamless_fixup = fixup; }
static const uint16_t SITES[HOOKS] = { DRAW_TILE,         DRAW_ATTRIBUTE, FLIPPED_TILE,
                                       FLIPPED_ATTRIBUTE, TITLE_WRITE,    SCREEN_LOADED };
static NESModFunctionEntryCallback registered[HOOKS];
int nes_mod_register_function_entry_plugin(const char *id, uint16_t addr, NESModFunctionEntryCallback cb) {
    for (size_t i = 0; i < HOOKS; ++i)
        if (!strcmp(id, hook_ids[i]) && addr == SITES[i]) {
            registered[i] = cb;
            return 1;
        }
    return 0;
}
int nes_mod_set_function_hook_enabled(const char *id, int on) {
    for (size_t i = 0; i < HOOKS; ++i)
        if (!strcmp(id, hook_ids[i])) {
            hook_on[i] = on;
            return 1;
        }
    return 0;
}
int nes_mod_register_reset_callback(NESModActivationCallback cb) { return cb == deactivate; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    return !strcmp(id, "faxanadu.graphics-fix") && cb == activate;
}
void nes_mod_set_local_only(const char *name, int required) {
    (void)name;
    (void)required;
}

/* A hook site: its callback, if registered and enabled, at `site`. */
static void site(size_t hook) {
    if (registered[hook] && hook_on[hook] == 1) CHECK(registered[hook](SITES[hook]) == 0); /* the game goes on */
}
enum { H_TILE, H_ATTRIBUTE, H_FLIPPED_TILE, H_FLIPPED_ATTRIBUTE, H_TITLE, H_SCREEN };

/* ---- bank 7: the tile info of the poses concerned, as the ROM has them ---- */
typedef struct {
    uint16_t addr;
    uint8_t bytes[20];
} Info;
static const Info INFO[] = {
    /* SPRITE_TILEINFO_PLAYER_ARMOR_STUDDED_07: 4 rows, 2 columns, then (slot, attribute) per cell */
    { 0xABF1, { 0x31, 0x00, 0x00, 0x08, 0x2C, 0x00, 0x2D, 0x00, 0x34, 0x01,
                0x31, 0x01, 0x32, 0x01, 0x33, 0x01, 0x2E, 0x00, 0x2F, 0x00 } },
    /* SPRITE_TILEINFO_PLAYER_ARMOR_STUDDED_WSHIELD_07 */
    { 0xAE6E, { 0x31, 0x00, 0x00, 0x08, 0x20, 0x00, 0x21, 0x00, 0x22, 0x01,
                0x23, 0x01, 0x24, 0x01, 0x25, 0x01, 0x26, 0x00, 0x27, 0x00 } },
    /* The Fire spell's frames, appearances 155 and 156 ($FF: an empty cell, one byte) */
    { 0xA5AF, { 0x31, 0x00, 0x00, 0x08, 0x65, 0x00, 0x66, 0x00, 0x7A, 0x00,
                0x7B, 0x00, 0x7A, 0x80, 0x7B, 0x80, 0x65, 0x80, 0x66, 0x80 } },
    { 0xA5C3,
      { 0x31, 0x00, 0x00, 0x08, 0xFF, 0x7E, 0x00, 0x7C, 0x00, 0x7D, 0x00, 0x7C, 0x80, 0x7D, 0x80, 0xFF, 0x66, 0x80 } },
    /* SPRITE_TILEINFO_ENEMY_MYCONID_00 and _01 */
    { 0x95BF, { 0x31, 0x00, 0x00, 0x08, 0x00, 0x03, 0x01, 0x03, 0x02, 0x03,
                0x03, 0x03, 0x04, 0x03, 0x05, 0x03, 0x06, 0x03, 0x07, 0x03 } },
    { 0x95D3, { 0x31, 0x00, 0x00, 0x08, 0x08, 0x03, 0x09, 0x03, 0x0A, 0x03,
                0x0B, 0x03, 0x04, 0x03, 0x05, 0x03, 0x0C, 0x03, 0x0D, 0x02 } },
};
enum {
    STUDDED_CLIMB = 0xABF1,
    STUDDED_SHIELD_CLIMB = 0xAE6E,
    FIRE_0 = 0xA5AF,
    FIRE_1 = 0xA5C3,
    MYCONID_0 = 0x95BF,
    MYCONID_1 = 0x95D3
};
static uint8_t bank7(uint16_t addr) {
    for (size_t i = 0; i < sizeof INFO / sizeof *INFO; ++i)
        if (addr >= INFO[i].addr && addr < INFO[i].addr + sizeof INFO[i].bytes)
            return INFO[i].bytes[addr - INFO[i].addr];
    return 0xEE;
}
/* Bank 8: the tiles Studded Mail loads into slots 0-51 (TILES_PLAYER_ARMOR_STUDDED_MAIL_START, $8056), and
   with a shield, into 0-39 (_WSHIELD_START, $811A) */
static const uint8_t STUDDED[52] = { 0x00, 0x01, 0x06, 0x07, 0x08, 0x09, 0x0E, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
                                     0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
                                     0x30, 0x4B, 0x4C, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5E,
                                     0x5F, 0x60, 0x61, 0x62, 0x63, 0x75, 0x76, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F, 0x80 };
static const uint8_t STUDDED_SHIELD[40] = { 0x00, 0x01, 0x8D, 0x8A, 0x8E, 0x8C, 0x06, 0x07, 0x08, 0x09,
                                            0x0E, 0x13, 0x14, 0x49, 0x4A, 0x4B, 0x4C, 0x53, 0x54, 0x55,
                                            0x56, 0x51, 0x52, 0x57, 0x58, 0x59, 0x60, 0x61, 0x62, 0x63,
                                            0x5E, 0x5F, 0x75, 0x76, 0x7D, 0x7E, 0x7F, 0x80, 0x7B, 0x7C };

/* A drawn cell: where (row, column on screen), and what it writes to OAM. */
typedef struct {
    int row, col;
    uint8_t tile, attr;
} Cell;

/* Sprite_Draw from its header on, as the disassembly has it, with bank
 * `bank` mapped: Sprite_Draw_Standard, or _FlippedHoriz (columns placed right
 * to left). Every cell fits on screen. Returns the cells drawn. */
static int draw(uint16_t info, bool flipped, uint8_t bank, Cell *out) {
    ram[CURRENT_ROM_BANK] = bank;
    uint8_t head = bank7(info);
    int rows = (head >> 4) + 1, cols = (head & 15) + 1, n = 0;
    uint16_t read = (uint16_t)(info + 4); /* ADC #$04 past the header */
    ram[TILEINFO_READ_L] = (uint8_t)read;
    ram[TILEINFO_READ_U] = (uint8_t)(read >> 8);
    uint8_t flip = flipped ? 0x40 : 0x00, y = 0; /* CurrentSprite_FlipMask */
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            if (bank7((uint16_t)(read + y)) == 0xFF) {
                ++y;
                continue;
            } /* CMP #$ff, BEQ: one byte */
            cpu.y = y;
            cpu.a = bank7((uint16_t)(read + y));               /* LDA (Sprite_TileInfo_ReadAddr_L),Y */
            site(flipped ? H_FLIPPED_TILE : H_TILE);           /* CLC */
            uint8_t tile = cpu.a;                              /* + Sprites_PPUOffset, 0 here */
            cpu.y = ++y;                                       /* INY */
            cpu.a = bank7((uint16_t)(read + y));               /* LDA (Sprite_TileInfo_ReadAddr_L),Y */
            site(flipped ? H_FLIPPED_ATTRIBUTE : H_ATTRIBUTE); /* EOR CurrentSprite_FlipMask */
            out[n++] = (Cell){ r, flipped ? cols - 1 - c : c, tile, (uint8_t)(cpu.a ^ flip) };
            ++y;
        }
    return n;
}
static const Cell *at(const Cell *cells, int n, int row, int col) {
    for (int i = 0; i < n; ++i)
        if (cells[i].row == row && cells[i].col == col) return &cells[i];
    return NULL;
}

/* The tile a Studded Mail slot holds: what the armor's loader put there. */
static int loaded(const uint8_t *list, size_t count, uint8_t slot) { return slot < count ? list[slot] : -1; }

static void studded_climb(bool fixed, bool flipped) {
    Cell plain[8], shield[8];
    int n = draw(STUDDED_CLIMB, flipped, SPRITE_BANK, plain);
    CHECK(n == 8 && draw(STUDDED_SHIELD_CLIMB, flipped, SPRITE_BANK, shield) == 8);
    for (int i = 0; i < n; ++i) {
        const Cell *s = at(shield, 8, plain[i].row, plain[i].col);
        bool chest = plain[i].row == 1 && plain[i].col == (flipped ? 1 : 0);
        CHECK(s && s->attr == plain[i].attr);
        if (chest && !fixed) {
            CHECK(plain[i].tile == 0x34 && loaded(STUDDED, 52, plain[i].tile) < 0); /* a slot nothing loads */
            continue;
        }
        /* The tile Studded Mail loads there is the one it climbs with under a shield. */
        CHECK(loaded(STUDDED, 52, plain[i].tile) == loaded(STUDDED_SHIELD, 40, s->tile));
        if (chest) CHECK(plain[i].tile == 0x30 && STUDDED[0x30] == 0x7D);
    }
}

/* Each frame of the Fire spell is mirrored top to bottom: row 3 is row 0
 * flipped vertically and row 2 row 1, cell for cell. */
static bool fire_mirrored(uint16_t info, bool flipped) {
    Cell cells[8];
    int n = draw(info, flipped, SPRITE_BANK, cells);
    for (int r = 0; r < 2; ++r)
        for (int c = 0; c < 2; ++c) {
            const Cell *top = at(cells, n, r, c), *bottom = at(cells, n, 3 - r, c);
            if (!top != !bottom) return false;
            if (top && (top->tile != bottom->tile || (top->attr ^ 0x80) != bottom->attr)) return false;
        }
    return true;
}

/* Every cell of a Myconid's frame is in palette 3. */
static bool myconid_palette_3(uint16_t info, bool flipped) {
    Cell cells[8];
    int n = draw(info, flipped, SPRITE_BANK, cells);
    for (int i = 0; i < n; ++i)
        if ((cells[i].attr & 3) != 3) return false;
    return n == 8;
}

/* ---- bank 10: START_SCREEN_PPU_ATTRIBUTES ($9B60), the title's attribute table ---- */
static const uint8_t TITLE[64] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x05, 0xAA, 0x20,
                                   0x2D, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                   0x80, 0x00, 0x00, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00,
                                   0x00, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
                                   0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55 };

/* PPU_WriteTilesFromCHRRAM with X = `bank` and Y = 4 tiles: 64 bytes from
 * IScriptOrCHRAddr, each LDA (IScriptOrCHRAddr),Y then STA PPUDATA (the hook
 * site), INY, and INC IScriptOrCHRAddr_U when Y wraps. */
static void write_tiles(uint8_t bank, uint16_t from, const uint8_t *src, uint8_t *ppu) {
    ram[CURRENT_ROM_BANK] = bank;
    ram[CHR_ADDR_L] = (uint8_t)from;
    ram[CHR_ADDR_U] = (uint8_t)(from >> 8);
    uint8_t y = 0; /* LDY #$00 */
    for (int i = 0; i < 64; ++i) {
        cpu.y = y;
        cpu.a = src[i]; /* LDA (IScriptOrCHRAddr),Y */
        site(H_TITLE);  /* STA PPUDATA */
        ppu[i] = cpu.a;
        if (++y == 0) ++ram[CHR_ADDR_U];
    }
}
/* The palette of the 16x16 square at (x, y) on the title. */
static unsigned quadrant(const uint8_t *attrs, unsigned x, unsigned y) {
    uint8_t b = attrs[(y / 32) * 8 + x / 32];
    return (b >> (((y / 16) & 1) * 4 + ((x / 16) & 1) * 2)) & 3;
}

/* ---- ScreenBuffer, after Area_LoadScrollData's last Area_LoadBlocks ---- */
#define TRUNK       1
#define FORTRESS    0x2D
#define WALL_SCREEN 0x0C /* the Mattock wall east of the Tower of Trunk */
static void screen(uint8_t area, uint8_t number, const uint8_t *blocks) {
    ram[AREA_CURRENT] = area;
    ram[SCREEN_CURRENT] = number;
    memcpy(&ram[SCREEN_BUFFER], blocks, 256);
    site(H_SCREEN); /* JSR MMC1_RestorePrevROMBank */
}
/* Trunk's $2D as Area_LoadBlocks gives it, around the floor: row 11 is
   1B 08 20 63 1B 08 ..., with $63 under the left pillar; the rest a pattern. */
static void fortress(uint8_t *blocks) {
    for (int i = 0; i < 256; ++i) blocks[i] = (uint8_t)(0x40 + i % 37);
    for (int c = 0; c < 16; ++c) blocks[0xB0 + c] = c % 2 ? 0x08 : 0x1B;
    blocks[0xB2] = 0x20; /* the ladder down to $29 */
    blocks[0xB3] = 0x63;
}

int main(void) {
    /* Off until the mod activates it, then all six hooks, and Seamless's fixup. */
    deactivate();
    for (size_t i = 0; i < HOOKS; ++i) CHECK(hook_on[i] == 0 && registered[i]);
    CHECK(!seamless_fixup);

    /* The original: the three poses as the ROM has them. */
    for (int f = 0; f < 2; ++f) {
        studded_climb(false, f);
        CHECK(fire_mirrored(FIRE_0, f) && !fire_mirrored(FIRE_1, f));
        CHECK(myconid_palette_3(MYCONID_0, f) && !myconid_palette_3(MYCONID_1, f));
    }
    activate();
    for (size_t i = 0; i < HOOKS; ++i) CHECK(hook_on[i] == 1);
    CHECK(seamless_fixup == room_fixup);

    /* 1. Sprites: each pose as the game meant it, drawn either way round. */
    for (int f = 0; f < 2; ++f) {
        studded_climb(true, f);
        CHECK(fire_mirrored(FIRE_0, f) && fire_mirrored(FIRE_1, f));
        CHECK(myconid_palette_3(MYCONID_0, f) && myconid_palette_3(MYCONID_1, f));
    }
    CHECK(applied[FAX_GFX_STUDDED_CLIMB] == 2 && applied[FAX_GFX_FIRE] == 2 && applied[FAX_GFX_MYCONID] == 2);
    /* Only those bytes: the reference poses drew untouched, and the three bytes
       only while bank 7 is mapped and only with their stock values. */
    Cell cells[8];
    CHECK(draw(FIRE_1, false, 6, cells) == 6 && at(cells, 6, 3, 1)->tile == 0x66);
    CHECK(draw(MYCONID_1, false, 6, cells) == 8 && (at(cells, 8, 3, 1)->attr & 3) == 2);
    FaxGraphicsFix fix;
    CHECK(fax_graphics_sprite_byte(0xABF9, 0x35, &fix) == 0x35 && fix == FAX_GFX_FIXES);
    CHECK(fax_graphics_sprite_byte(0xABF8, 0x34, &fix) == 0x34 && fix == FAX_GFX_FIXES);
    CHECK(fax_graphics_sprite_byte(0xA5D3, 0x66, &fix) == 0x7E && fix == FAX_GFX_FIRE);
    CHECK(fax_graphics_sprite_byte(0x95E6, 0x02, &fix) == 0x03 && fix == FAX_GFX_MYCONID);
    /* Seamless's isolated rooms draw their Myconids fixed, and are not counted. */
    isolated = true;
    unsigned before = applied[FAX_GFX_MYCONID];
    CHECK(myconid_palette_3(MYCONID_1, false) && applied[FAX_GFX_MYCONID] == before);
    isolated = false;
    /* N and Z as the LDA of the right byte would have left them, the rest of P as it was. */
    ram[CURRENT_ROM_BANK] = SPRITE_BANK;
    ram[TILEINFO_READ_L] = 0xC7; /* FIRE_1 + 4 */
    ram[TILEINFO_READ_U] = 0xA5;
    cpu.y = 12;
    cpu.a = 0x66;
    cpu.p = 0xFF;
    CHECK(sprite_hook(DRAW_TILE) == 0 && cpu.a == 0x7E && cpu.p == (0xFF & ~(FLAG_N | FLAG_Z)));

    /* 2. The title: the F's tip in the logo's palette, nothing else moved. */
    uint8_t ppu[64];
    write_tiles(TITLE_BANK, TITLE_ATTRIBUTES, TITLE, ppu);
    for (int i = 0; i < 64; ++i) CHECK(ppu[i] == (i == TITLE_F_TIP ? 0x00 : TITLE[i]));
    CHECK(quadrant(TITLE, 48, 40) == 3 && quadrant(ppu, 48, 40) == 0); /* the tile with the F's tip */
    CHECK(quadrant(ppu, 32, 40) == 0 && quadrant(ppu, 32, 48) == 0);   /* the rest of the F */
    CHECK(quadrant(ppu, 80, 40) == 1 && quadrant(ppu, 176, 40) == 3);  /* the moon; a star */
    CHECK(applied[FAX_GFX_TITLE] == 1);
    /* The same byte from anywhere else, or with another bank mapped, is copied as it is. */
    write_tiles(TITLE_BANK, 0x8E00, TITLE, ppu);
    CHECK(!memcmp(ppu, TITLE, 64));
    write_tiles(8, TITLE_ATTRIBUTES, TITLE, ppu);
    CHECK(!memcmp(ppu, TITLE, 64) && applied[FAX_GFX_TITLE] == 1);

    /* 3. The Tower of Fortress: the floor block under the left pillar, and nothing else. */
    uint8_t blocks[256], loaded_blocks[256];
    fortress(blocks);
    screen(TRUNK, FORTRESS, blocks);
    for (int i = 0; i < 256; ++i) CHECK(ram[SCREEN_BUFFER + i] == (i == 0xB3 ? 0x08 : blocks[i]));
    CHECK(applied[FAX_GFX_FORTRESS] == 1);
    for (int c = 0; c < 16; ++c) /* the floor's own pattern */
        CHECK(c == 2 || ram[SCREEN_BUFFER + 0xB0 + c] == (c % 2 ? 0x08 : 0x1B));
    /* The wall east of the Tower of Trunk ($0C, two blocks of $63) is the
       Mattock's, and stays; so does any other area's $2D. */
    fortress(blocks);
    blocks[0xA3] = 0x63;
    screen(TRUNK, WALL_SCREEN, blocks);
    CHECK(!memcmp(&ram[SCREEN_BUFFER], blocks, 256));
    screen(0, FORTRESS, blocks);
    CHECK(!memcmp(&ram[SCREEN_BUFFER], blocks, 256) && applied[FAX_GFX_FORTRESS] == 1);
    /* Seamless's rooms, decoded in isolated calls: the same correction, on the room's RAM. */
    uint8_t room[0x800] = { 0 };
    fortress(&room[SCREEN_BUFFER]);
    memcpy(loaded_blocks, &room[SCREEN_BUFFER], 256);
    seamless_fixup(TRUNK, FORTRESS, room);
    for (int i = 0; i < 256; ++i) CHECK(room[SCREEN_BUFFER + i] == (i == 0xB3 ? 0x08 : loaded_blocks[i]));
    CHECK(!fax_graphics_fix_blocks(TRUNK, FORTRESS, &room[SCREEN_BUFFER])); /* once fixed, left alone */

    /* 4. Off again (the remaster off, or a reset): the original's bytes. */
    deactivate();
    for (size_t i = 0; i < HOOKS; ++i) CHECK(hook_on[i] == 0);
    CHECK(!seamless_fixup && applied[FAX_GFX_FIRE] == 0);
    studded_climb(false, false);
    CHECK(!fire_mirrored(FIRE_1, false) && !myconid_palette_3(MYCONID_1, true));
    write_tiles(TITLE_BANK, TITLE_ATTRIBUTES, TITLE, ppu);
    CHECK(ppu[TITLE_F_TIP] == 0x0C);
    fortress(blocks);
    screen(TRUNK, FORTRESS, blocks);
    CHECK(ram[SCREEN_BUFFER + 0xB3] == 0x63);

    printf("graphics_test: %u checks passed\n", checks);
    return 0;
}
