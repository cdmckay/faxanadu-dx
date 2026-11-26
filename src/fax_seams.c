/* Seams: the plan for one big world across Seamless's seams (#166,
 * docs/SEAMLESS-COMBAT.md). USA 57dd23d1, chipx86 byte-exact disassembly.
 *
 * Slice 0, seams lose nothing. Two rules in bank 14's sprite loop
 * (Sprites_UpdateAll, $8000):
 *
 *   - **A neighbouring room's copy touches nothing.** Seamless runs each
 *     neighbour's actors on an isolated copy of the machine, against a hero
 *     projected to that room's edge (fax_world.c's shadow()). His
 *     invincibility there keeps enemies and their shots off him, but
 *     CurrentSprite_CheckHitPlayer ($890A) sends every touch to its
 *     category's handler without looking at it: a coin or bread at the edge
 *     was taken (Player_HandleTouchBreadOrCoin, $8968) and its gold or HP
 *     thrown away with the copy's RAM, and an item there
 *     (Player_HandleTouchItem, $88FB) or a person who talks when touched
 *     (Player_HandleTouchNPC) started a script that waits for frames, which
 *     an isolated call cannot run: the call failed, Seamless dropped the
 *     room's tick, and the room stood still while he stood beside it. A hook
 *     at the routine's entry returns at once in those ticks
 *     (fax_world_ticking()), so the live game is the only place the hero is
 *     touched. The coin waits until he walks into its room, which is then
 *     the live game's.
 *
 *   - **The weapon's reach in 16 bits** (the original's bug). Player_DrawWeapon
 *     ($B7D6) places the weapon in 16 bits, PlayerPosXPlus10 ($CE) low and
 *     Maybe_Something_WeaponPosX ($CF) high, against the screen's
 *     Screen_Maybe_ScrollXCounter ($9F); Sprites_IsSpriteOutOfWeaponRange
 *     ($88CB) then compares only $CE with the sprite's X, in 8 bits. So a
 *     swing whose box reaches past the screen's edge, or wraps past 255 with
 *     the reach added, hits a monster at the other edge: from Eolis $04's x
 *     4 facing left, the Long Sword kills a Zozura at its x 236, 232 pixels
 *     behind him. A hook at the routine's entry works the X test out in 16
 *     bits, and when the 8-bit test would hit only by wrapping round the
 *     screen, returns "out of range" itself, as the X test's failure leaves
 *     it; otherwise the original runs. The Y test can't wrap inside a room
 *     (its 208 lines leave the weapon's box below 256), so it is left alone.
 *
 * Slice 1, seams are open ground. Movers (monsters, category 0; their shots,
 * 6; and the people who walk, $34 $41 $42 $44 $45 of category 2) go through
 * a seam side by side, whichever way they walk, between any two rooms, the
 * live game's included:
 *
 *   - **Dormant until seen.** Every mover, and the big enemies (7), stands
 *     still until the first frame it comes within 16 pixels of the view
 *     (fax_world_view(), the 424x240 picture): until then hooks at
 *     Sprites_UpdateBehavior ($A66B) and CurrentSprite_UpdateState ($8BD2)
 *     return at once for it, so it neither moves, seeks him nor is drawn,
 *     in his room as in a neighbour's. Once seen it stays awake (a bit per
 *     slot) until it dies or a load refreshes its room. A shot wholly out of
 *     the view by 16 pixels is removed between frames, as the original
 *     removes one at its screen's edge.
 *   - **The edges open.** The game calls a move past a screen's side blocked
 *     in four places, each hooked: walking left (FUN_PRG14__86bd, $86BD: X
 *     $F1 and up, wrapped past 0), walking right (Sprites_MoveRight, $86A7:
 *     X plus the hitbox's width carries), and flying or a shot
 *     (Sprite_Maybe_TurnAroundIfAtScreenEdgeHoriz after its move, $842A: X
 *     $F0 and up). There, for an awake mover, a joined room that way whose
 *     actors run, with fewer than 5 actors (and movers on their way in), is
 *     open ground: the blocks across are tested in that room's own
 *     ScreenBuffer and BlockProperties, as the game tests its own, and the
 *     slot is marked as through that side. A walker's floor tests that look
 *     past a side, CurrentSprite_CanMoveInDirection's up and down test
 *     ($873A, across its hitbox's columns) and Sprites_CheckPlacementInvalidAtY's
 *     ($855E, its leading column), read the room the column is in the same
 *     way while it straddles a seam. Anything else, or anyone else, finds the
 *     seam a wall as before.
 *   - **The move**, between frames (fax_seams_step(), once every room has
 *     run): a slot whose origin is across (marked left and X 128 and up, or
 *     marked right and X below 128) goes, every byte of it, to a free slot of
 *     the room it entered (the highest, as Sprites_PopulateNextAvailableSprite
 *     takes them), and its old slot is emptied. Side by side the bytes stay
 *     as they are: room 5's x 250 is room 4's x -6, the same byte.
 *   - **Drawn whole.** Hooks at Sprite_Draw_Standard ($F0EC) and
 *     Sprite_Draw_FlippedHoriz ($F181), inside a sprite loop, play the
 *     routine's tile loop in C and keep each cell it skips for lying past the
 *     screen's side (with the Y it would have, the tile, the attributes),
 *     and the slot of each cell it does put in OAM. Seamless paints the
 *     first in the next room's space. A slot whose origin crossed in this
 *     frame is drawn a screen over (its Sprite_TileInfo_OffsetX moved by
 *     one), so its cells are all past the side and painted where it is.
 *   - **Tiles from home.** A slot moving into a room with its entity takes
 *     that slot's CurrentSprites_PPUOffsets; otherwise Seamless paints it
 *     from the CHR of the room its tiles are in, which the block keeps.
 *
 * A load within the area (Screen_LoadSpriteInfo, $C1B4, in play) has
 * Seamless forget every other room (fax_world_screen_loaded()). Each room's
 * state is a block in Seamless's record (version 5); nothing else is kept
 * but counts for the TCP command. All of it is part of the remaster: with it
 * off, the original's walls and its ghost hit stay. See docs/GAME_INTERNALS.md,
 * "Weapon reach" and "Seams: actors across rooms", and "Features and what
 * they use". */
#include "fax_seams.h"
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
#include <string.h>

/* Bank 14 */
#define SPRITES_UPDATE_ALL    0x8000u   /* Sprites_UpdateAll: LDA Sprites_UpdatesPaused, BNE @_spritesLoop, LDA DurationHourGlass */
#define SPRITES_LOOP_DONE     0x8049u   /* Sprites_UpdateAll, past the slots' loop: LDA #$00, STA Sprites_PPUOffset, RTS */
#define FLIER_EDGE            0x842Au   /* Sprite_Maybe_TurnAroundIfAtScreenEdgeHoriz, after JSR Sprite_MoveForwardX:
                                           LDA CurrentSprites_XPos_Full,X, CMP #$F0, BCC @_returnFalse */
#define PLACEMENT_BLOCK       0x855Eu   /* Sprites_CheckPlacementInvalidAtY: JSR Area_ConvertPixelsToBlockPos,
                                           JMP Area_IsBlockImpassableOrLadder (Arg_PixelPosX = X + 0 or 15 by facing) */
#define MOVE_RIGHT            0x86A7u   /* Sprites_MoveRight: LDX CurrentSprites_HitBoxTypes,Y, LDA CurrentSprites_XPos_Full,Y,
                                           CLC, ADC SPRITE_HITBOX_WIDTHS,X, STA Arg_PixelPosX, BCC (test), else blocked */
#define MOVE_LEFT             0x86BDu   /* FUN_PRG14__86bd: LDA CurrentSprites_XPos_Full,Y, CMP #$F1, BCS (blocked),
                                           STA Arg_PixelPosX, then the test (Sprites_MoveRight__86c6) */
#define MOVE_VERTICAL         0x873Au   /* CurrentSprite_CanMoveInDirection, up and down: LDA CurrentSprites_XPos_Full,Y,
                                           STA Arg_PixelPosX, JSR Area_ConvertPixelsToBlockPos, then the hitbox's columns */
#define OUT_OF_WEAPON_RANGE   0x88CBu   /* Sprites_IsSpriteOutOfWeaponRange: LDA CurrentSprites_Entities,X, ASL A,
                                           TAY, LDA MAYBE_SPRITE_EXTENTS,Y, CLC, ADC Maybe_WeaponRange_X, STA Temp_00 */
#define CHECK_HIT_PLAYER      0x890Au   /* CurrentSprite_CheckHitPlayer: LDX CurrentSpriteIndex,
                                           LDA CurrentSprites_Entities,X, CMP #$FF, BEQ RETURN_8909, JSR ... */
#define UPDATE_STATE          0x8BD2u   /* CurrentSprite_UpdateState: LDA IScript_PortraitID, BPL @_return, LDX CurrentSpriteIndex */
#define UPDATE_BEHAVIOR       0xA66Bu   /* Sprites_UpdateBehavior: LDA DurationHourGlass, BMI, LDY CurrentSprites_Entities,X */
#define SPRITE_EXTENTS        0xB407u   /* MAYBE_SPRITE_EXTENTS: (EX, EY) per entity */
#define HITBOX_WIDTHS         0xB4D1u   /* SPRITE_HITBOX_WIDTHS: a hitbox type's width less 1 */
#define HITBOX_HEIGHTS        0xB4D8u   /* SPRITE_HITBOX_HEIGHTS: its height less 1 */
#define CATEGORIES            0xB544u   /* SPRITE_CATEGORIES_BY_ENTITY */
/* Bank 15 */
#define LOAD_SPRITE_INFO      0xC1B4u   /* Screen_LoadSpriteInfo: LDA $0100, PHA, LDX #$0B, JSR MMC1_UpdateROMBank */
#define BLOCK_IMPASSABLE      0xE8D9u   /* BLOCK_PROPERTY_IMPASSABLE_MAP: 1 for a solid block property */
#define DRAW_STANDARD         0xF0ECu   /* Sprite_Draw_Standard: LDA #$00, STA Temp_Sprites_TileYOffset, TAY */
#define DRAW_FLIPPED          0xF181u   /* Sprite_Draw_FlippedHoriz: LDA #$00, STA Temp_05, LDY Temp_Sprites_ColsRemaining */
#define VISIBILITY_MASK       0xF224u   /* DRAW_SPRITE_VISIBILITY_MASK: 1, 2 (standard) */
#define VISIBILITY_FLIPPED    0xF226u   /* BYTE_ARRAY_PRG15_MIRROR__f226: 2, 1 (flipped) */
#define TILE_POSITIONS        0xF23Du   /* SPRITE_TILE_BLOCK_POSITIONS: 0, 8, 16 ... */
/* RAM */
#define TEMP_00               0x0000u   /* the X test's limit, EX + the reach */
#define TEMP_ADDR_L           0x0002u   /* Temp_Addr_L: the flip's pivot (Sprite_Draw) */
#define START_SLOT_RANGE      0x001Cu   /* Sprites_StartSlotRange */
#define SPRITE_SLOT           0x0025u   /* Sprites_SpriteSlot: the next OAM entry, as Sprites_IncNextSpriteSlot counts */
#define MOVING_VISIBILITY     0x0026u   /* MovingSpriteVisibility */
#define FLIP_MASK             0x0029u   /* CurrentSprite_FlipMask */
#define PPU_OFFSET            0x0033u   /* Sprites_PPUOffset */
#define SLOTS_FULL            0x0039u   /* Sprites_SlotsFull */
#define READ_ADDR             0x003Au   /* Sprite_TileInfo_ReadAddr_L/_U */
#define NORM_X                0x003Cu   /* Temp_Sprites_NormXPos */
#define NORM_Y                0x003Du   /* Temp_Sprites_NormYPos */
#define OFFSET_X              0x003Eu   /* Sprite_TileInfo_OffsetX: the high byte of a cell's X, 0 on the screen */
#define OFFSET_Y              0x003Fu   /* Sprite_TileInfo_OffsetY */
#define COLS                  0x0040u   /* Temp_Sprites_ColsRemaining: columns less 1 */
#define ROWS                  0x0041u   /* Temp_Sprites_RowsRemaining: rows less 1 */
#define SCROLL_X              0x009Fu   /* Screen_Maybe_ScrollXCounter: the screen's high byte of X */
#define PIXEL_X               0x00B5u   /* Arg_PixelPosX */
#define PIXEL_Y               0x00B6u   /* Arg_PixelPosY */
#define BLOCKS_RESULT         0x00B7u   /* Blocks_Result: nonzero, blocked */
#define SLOT_X                0x00BAu   /* CurrentSprites_XPos_Full, 8 slots */
#define SLOT_Y                0x00C2u   /* CurrentSprites_YPos */
#define WEAPON_X              0x00CEu   /* PlayerPosXPlus10: the weapon's X, low byte */
#define WEAPON_X_HIGH         0x00CFu   /* Maybe_Something_WeaponPosX: its high byte */
#define WEAPON_RANGE_X        0x00D2u   /* Maybe_WeaponRange_X */
#define SLOT_ARRAYS           0x02BCu   /* CurrentSprites_XPos_Frac ... CurrentSprites_Values: 23 arrays of 8, to $0373 */
#define ENTITIES              0x02CCu   /* CurrentSprites_Entities */
#define FLAGS                 0x02DCu   /* CurrentSprites_Flags: bit 0 facing right */
#define HITBOX                0x0304u   /* CurrentSprites_HitBoxTypes */
#define PPU_OFFSETS           0x032Cu   /* CurrentSprites_PPUOffsets */
#define CURRENT_SPRITE_INDEX  0x0378u   /* CurrentSpriteIndex */
#define SPRITE_X              0x037Fu   /* Something_Maybe_WeaponDistanceX: the caller's copy of the sprite's X */
#define BLOCK_PROPERTIES      0x043Cu   /* BlockProperties: a nibble per block id */
#define SCREEN_BUFFER         0x0600u   /* ScreenBuffer: 16 blocks a row */
/* Flags */
enum { FLAG_C = 0x01, FLAG_Z = 0x02, FLAG_V = 0x40, FLAG_N = 0x80 };

enum {
    EMPTY = 0xFF, SHOT = 6, BIG = 7, PEOPLE = 2,
    THROUGH_LEFT = 1, THROUGH_RIGHT = 2,    /* a slot let through its room's side this frame, or straddling it */
    ROOM_LIMIT = 5,                         /* a room takes no mover once it holds this many (the most a list holds) */
    PAD = 16,                               /* the view's margin: what an actor draws past its hitbox */
    VIEW_W = 424, VIEW_H = 208,             /* the view below the status bar */
    CELLS = 48, ROOMS = 83,
};

/* A room's block in Seamless's record. */
typedef struct {
    uint8_t awake;              /* a bit per slot: seen once, so it runs */
    uint8_t count;              /* cells past the room's sides, in its last finished sprite loop */
    uint8_t through[8];         /* 0, THROUGH_LEFT or THROUGH_RIGHT */
    uint8_t tiles[8];           /* 0: the room's own CHR; else 1 + the room whose CHR has the slot's tiles */
    uint8_t drawn_tiles[8];     /* tiles as they were when the slot was last drawn: the picture's */
    uint8_t owner[64];          /* per OAM entry: 1 + the slot that drew it, 0 none */
    FaxSeamsCell cell[CELLS];
} Block;
_Static_assert(sizeof(Block) <= FAX_WORLD_SEAMS_BYTES, "Seams' block fits Seamless's record");

/* What a sprite loop drew, until Seamless captures the room it ran:
 * [0] the live game's, [1] a neighbour's tick. */
typedef struct { uint8_t owner[64], count, tiles[8]; FaxSeamsCell cell[CELLS]; } Drawn;
static Drawn drawn[2];
static bool looping[2];
static int loop_room[2] = {-1, -1};

static bool enabled;
static unsigned skipped;                 /* CheckHitPlayer calls skipped in neighbours' ticks */
static unsigned refused;                 /* weapon matches refused: they hit only by wrapping (live game) */
static int last_weapon_x = 0x7FFF;       /* the last refused one's weapon X, in 16 bits from the screen's left */
static unsigned last_sprite_x, last_entity = 0xFF;
/* Slice 1's counts, for the TCP command. */
static unsigned steps, moves, kept_out, removed, woken, frozen, pushed_back;
static int last_woken[4] = {-1, -1, -1, -1};          /* room, slot, entity, step */
enum { LOG = 16 };
static int move_log[LOG][6];                          /* the last moves: from, slot, to, slot, entity, step */

/* ---- slice 0 ---- */

/* The weapon's X in 16 bits from the screen's left edge: its low byte, and
 * its high byte against the screen's (-1 left of the screen, 1 right of it). */
static int weapon_x16(uint8_t low, uint8_t high, uint8_t screen) {
    return (int)(int8_t)(uint8_t)(high - screen) * 256 + low;
}

/* Sprites_IsSpriteOutOfWeaponRange's X test, as the game does it in 8 bits:
 * in range when (weapon x + reach - sprite x) mod 256 < extent + reach. */
static bool hits_8bit(uint8_t weapon_x, uint8_t reach, uint8_t sprite_x, uint8_t extent) {
    return (uint8_t)(weapon_x + reach - sprite_x) < (uint8_t)(extent + reach);
}

/* Whether the 8-bit X test hits only by wrapping round the screen: the same
 * sum in 16 bits is outside 0-255, so the box and the sprite are a screen
 * apart. A sprite's X is always on the screen (0-255). */
static bool wraps(int weapon_x, uint8_t reach, uint8_t sprite_x, uint8_t extent) {
    int d = weapon_x + reach - sprite_x;
    return hits_8bit((uint8_t)weapon_x, reach, sprite_x, extent) && (d < 0 || d > 255);
}

/* Sprites_IsSpriteOutOfWeaponRange's entry. X is the sprite's slot; its X is
 * in Something_Maybe_WeaponDistanceX (Player_HitSpriteWithWeapon stores it
 * just before the JSR). Bank 14 is mapped, so a peek reads the extents the
 * routine would. A refusal leaves what the X test's failure leaves (A the
 * 8-bit difference, Y the entity times 2, Temp_00 the limit, the flags of
 * its SBC and CMP) but with C set: out of range. The one caller, at $8838,
 * reads only C (BCC, else JMP @_return). */
static int reach_hook(uint16_t addr) {
    (void)addr;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    uint8_t entity = cyc_mod_peek((uint16_t)(ENTITIES + (regs.x & 7u)));
    uint8_t index = (uint8_t)(entity << 1);
    uint8_t extent = cyc_mod_peek((uint16_t)(SPRITE_EXTENTS + index));
    uint8_t reach = cyc_mod_peek(WEAPON_RANGE_X), sprite_x = cyc_mod_peek(SPRITE_X);
    int weapon_x = weapon_x16(cyc_mod_peek(WEAPON_X), cyc_mod_peek(WEAPON_X_HIGH), cyc_mod_peek(SCROLL_X));
    if (!wraps(weapon_x, reach, sprite_x, extent)) return 0;   /* the original decides */
    uint8_t limit = (uint8_t)(extent + reach), sum = (uint8_t)(weapon_x + reach);
    uint8_t difference = (uint8_t)(sum - sprite_x), compare = (uint8_t)(difference - limit);
    uint8_t p = (uint8_t)(regs.p & ~(FLAG_C | FLAG_Z | FLAG_V | FLAG_N));
    if ((sum ^ sprite_x) & (sum ^ difference) & 0x80) p |= FLAG_V;      /* SBC's overflow */
    if (compare & 0x80) p |= FLAG_N;                                       /* CMP's N; never Z: difference < limit */
    regs.a = difference;
    regs.y = index;
    regs.p = (uint8_t)(p | FLAG_C);
    cyc_mod_poke(TEMP_00, limit);
    cyc_mod_set_regs(&regs);
    if (!cyc_mod_isolated()) {          /* a neighbour's copy never swings (shadow()), but count only the live game */
        ++refused;
        last_weapon_x = weapon_x;
        last_sprite_x = sprite_x;
        last_entity = entity;
    }
    return 1;
}

/* Return from a hooked routine with X the current slot, its N and Z as LDX
 * CurrentSpriteIndex leaves them, and A and C as given (A < 0: kept). */
static void leave(unsigned slot, int a, int carry) {
    CycModRegs regs;
    cyc_mod_regs(&regs);
    regs.x = (uint8_t)slot;
    if (a >= 0) regs.a = (uint8_t)a;
    regs.p = (uint8_t)((regs.p & ~(FLAG_Z | FLAG_N)) | (regs.x ? 0 : FLAG_Z) | (regs.x & FLAG_N));
    if (carry >= 0) regs.p = (uint8_t)((regs.p & ~FLAG_C) | (carry ? FLAG_C : 0));
    cyc_mod_set_regs(&regs);
}

/* CurrentSprite_CheckHitPlayer's entry: in a neighbouring room's tick the
 * routine returns at once, with X as its first instruction (LDX
 * CurrentSpriteIndex) leaves it; CurrentSprite_UpdateState, next in the
 * loop, loads X again anyway. Anywhere else the original runs. */
static int touch_hook(uint16_t addr) {
    (void)addr;
    if (!cyc_mod_isolated() || !fax_world_ticking()) return 0;
    leave(cyc_mod_peek(CURRENT_SPRITE_INDEX), -1, -1);
    ++skipped;
    return 1;
}

/* ---- slice 1: the rooms and their slots ---- */

static uint8_t rom14(unsigned addr) { return fax_world_rom(14, addr); }
static uint8_t rom15(unsigned addr) { return fax_world_rom(15, addr); }
static Block *block(int room) { return (Block *)fax_world_seams_block(room); }

/* The room whose loop the machine is running, or -1 when there is none or
 * the live machine holds another (a load under way: no view). */
static int running(void) {
    int room = fax_world_seams_room(), x, y;
    if (room >= 0 && !cyc_mod_isolated() && !fax_world_view(&x, &y)) return -1;
    return room;
}

/* A room's RAM: the machine's own while it runs that room (a neighbour in
 * its tick, the active room otherwise), else the room's record. */
static bool here(int room) {
    return room >= 0 && room == (cyc_mod_isolated() ? fax_world_seams_room() : fax_world_active_room());
}
static uint8_t rd(int room, unsigned addr) {
    if (here(room)) return cyc_mod_peek((uint16_t)addr);
    const uint8_t *ram = fax_world_room_record(room);
    return ram ? ram[addr & 0x7FF] : 0;
}
static void wr(int room, unsigned addr, uint8_t value) {
    if (here(room)) { cyc_mod_poke((uint16_t)addr, value); return; }
    uint8_t *ram = fax_world_room_record(room);
    if (ram) ram[addr & 0x7FF] = value;
}

static unsigned category(uint8_t entity) { return entity <= 0x65 ? rom14(CATEGORIES + entity) : EMPTY; }
static bool walker(uint8_t e) { return e == 0x34 || e == 0x41 || e == 0x42 || e == 0x44 || e == 0x45; }
/* Goes through seams: monsters, their shots, the people who walk. */
static bool mover(uint8_t e) {
    unsigned c = category(e);
    return c == 0 || c == SHOT || (c == PEOPLE && walker(e));
}
/* Dormant until seen: the movers, and the big enemies, who stay home. */
static bool sleeper(uint8_t e) { return mover(e) || category(e) == BIG; }
static unsigned hitbox(int room, unsigned s) { unsigned h = rd(room, HITBOX + s); return h > 6 ? 6 : h; }
static unsigned width_less_1(int room, unsigned s) { return rom14(HITBOX_WIDTHS + hitbox(room, s)); }

/* A slot's X in 16 bits from its room's left edge: past the left side once
 * let through it and wrapped, past the right once it wrapped the other way. */
static int x16_of(const Block *b, unsigned s, uint8_t x) {
    if (b->through[s] == THROUGH_LEFT && x >= 128) return x - 256;
    if (b->through[s] == THROUGH_RIGHT && x < 128) return x + 256;
    return x;
}
static int x16(int room, unsigned s) { return x16_of(block(room), s, rd(room, SLOT_X + s)); }

/* Whether the slot's hitbox, with `pad` pixels round it, meets the view. */
static bool in_view(int room, unsigned s, int pad) {
    int vx, vy, px, py;
    if (!fax_world_view(&vx, &vy)) return false;
    fax_world_place(room, &px, &py);
    unsigned h = hitbox(room, s);
    int x = px + x16(room, s), y = py + rd(room, SLOT_Y + s);
    int w = rom14(HITBOX_WIDTHS + h) + 1, t = rom14(HITBOX_HEIGHTS + h) + 1;
    return x + w + pad > vx && x - pad < vx + VIEW_W && y + t + pad > vy && y - pad < vy + VIEW_H;
}

/* ScreenBuffer_IsBlockImpassable for the block at `index` of a room: its
 * block id's property nibble, through BLOCK_PROPERTY_IMPASSABLE_MAP. With
 * `ladder`, Area_IsBlockImpassableOrLadder: a ladder (property 2) too. */
static uint8_t solid(int room, unsigned index, bool ladder) {
    uint8_t id = rd(room, SCREEN_BUFFER + (index & 0xFF));
    uint8_t bits = rd(room, BLOCK_PROPERTIES + (id >> 1));
    uint8_t property = id & 1 ? bits >> 4 : bits & 15;
    if (ladder && property == 2) return 1;
    return rom15(BLOCK_IMPASSABLE + property);
}
/* The room pixel column `px` lies in, from `room`'s left edge: the room
 * joined left below 0, right from 256; -1 for none. */
static int room_at(int room, int px) {
    return px < 0 ? fax_world_joined(room, 0) : px >= 256 ? fax_world_joined(room, 1) : room;
}
/* Sprites_MoveRight__86c6's test at column `col` of `room`: the block at
 * the slot's Y, then one a row further down for every 16 of its hitbox's
 * height, and one more if Y is not on a block's edge, until one is solid. */
static uint8_t rows_test(int room, unsigned col, uint8_t y, unsigned hb) {
    unsigned index = ((y & 0xF0u) | col) & 0xFF;
    uint8_t result = solid(room, index, false);
    for (int t = rom14(HITBOX_HEIGHTS + hb); !result && t >= 16; t -= 16) {
        index = (index + 16) & 0xFF;
        result = solid(room, index, false);
    }
    if (!result && (y & 15)) result = solid(room, (index + 16) & 0xFF, false);
    return result;
}
/* The same test along a row, at column c (from `room`'s; below 0 or past 15
 * in the room joined there, solid where none is). */
static uint8_t column_cell(int room, int c, unsigned row) {
    int r = room_at(room, c * 16);
    return r < 0 ? 1 : solid(r, row | ((unsigned)c & 15), false);
}

/* Whether `to` is full for a mover coming from slot s of `from`: its actors
 * (dormant ones included) and the movers on their way in from its sides
 * make ROOM_LIMIT. */
static bool full(int to, int from, unsigned s) {
    unsigned n = 0;
    for (unsigned k = 0; k < 8; k++) n += rd(to, ENTITIES + k) < 0x80;
    for (int side = 0; side < 2; side++) {
        int nb = fax_world_joined(to, side);
        const Block *o = nb >= 0 ? block(nb) : NULL;
        if (!o) continue;
        for (unsigned k = 0; k < 8; k++)
            if (!(nb == from && k == s) && o->through[k] == (side == 0 ? THROUGH_RIGHT : THROUGH_LEFT)) ++n;
    }
    return n >= ROOM_LIMIT;
}

/* The running room and its block when slot s is an awake mover there. */
static bool awake_mover(unsigned s, int *room, Block **b) {
    *room = running();
    *b = *room >= 0 ? block(*room) : NULL;
    return *b && ((*b)->awake >> s & 1) && mover(cyc_mod_peek(ENTITIES + s));
}
/* Whether a mover in slot s of `room` may go through into `to`, entering it
 * now (not already on its way): `to` runs and isn't full. */
static bool may_enter(int to, int room, unsigned s, bool entering) {
    if (to < 0) return false;
    if (entering && full(to, room, s)) { ++kept_out; return false; }
    return true;
}
/* A block test's result as the game's own leaves it: Blocks_Result, the
 * pixel it looked at, and X the slot. */
static void tested(unsigned s, int px, uint8_t py, uint8_t result) {
    cyc_mod_poke(PIXEL_X, (uint8_t)px);
    cyc_mod_poke(PIXEL_Y, py);
    cyc_mod_poke(BLOCKS_RESULT, result);
    leave(s, result, -1);
}

/* ---- slice 1: the hooks ---- */

/* Sprites_UpdateAll's entry and its loop's end: which room's loop runs, and
 * what it draws, for the live game (outside isolation) and a tick. */
static int loop_hook(uint16_t addr) {
    (void)addr;
    unsigned c = cyc_mod_isolated();
    if (!c) fax_world_update_view();
    looping[c] = true;
    loop_room[c] = running();
    memset(&drawn[c], 0, sizeof drawn[c]);
    return 0;
}
static int loop_done_hook(uint16_t addr) {
    (void)addr;
    looping[cyc_mod_isolated()] = false;
    return 0;
}

/* Sprites_UpdateBehavior's entry: a sleeper not yet seen wakes as it comes
 * within PAD of the view, and stands still till then. */
static int behavior_hook(uint16_t addr) {
    (void)addr;
    int room = running();
    Block *b = room >= 0 ? block(room) : NULL;
    unsigned s = cyc_mod_peek(CURRENT_SPRITE_INDEX) & 7;
    uint8_t e = cyc_mod_peek(ENTITIES + s);
    if (!b || (b->awake >> s & 1) || !sleeper(e)) return 0;
    if (in_view(room, s, PAD)) {
        b->awake |= (uint8_t)(1u << s);
        ++woken;
        last_woken[0] = room, last_woken[1] = (int)s, last_woken[2] = e, last_woken[3] = (int)steps;
        return 0;
    }
    ++frozen;
    leave(s, -1, -1);
    return 1;
}
/* CurrentSprite_UpdateState's entry: a dormant sleeper isn't drawn either
 * (it is out of the view). */
static int state_hook(uint16_t addr) {
    (void)addr;
    int room = running();
    Block *b = room >= 0 ? block(room) : NULL;
    unsigned s = cyc_mod_peek(CURRENT_SPRITE_INDEX) & 7;
    if (!b || (b->awake >> s & 1) || !sleeper(cyc_mod_peek(ENTITIES + s))) return 0;
    leave(s, -1, -1);
    return 1;
}

/* FUN_PRG14__86bd: the test walking left. Y is the slot. */
static int left_hook(uint16_t addr) {
    (void)addr;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    unsigned s = regs.y & 7;
    int room;
    Block *b;
    if (!awake_mover(s, &room, &b)) return 0;
    uint8_t x = cyc_mod_peek(SLOT_X + s), y = cyc_mod_peek(SLOT_Y + s);
    int px = x16_of(b, s, x);
    if (px == x && x >= 0xF1 && b->through[s] != THROUGH_RIGHT) px = x - 256;   /* wrapped past the left side */
    if (px >= 0 && px < 0xF1) return 0;                                         /* the original's test */
    int to = room_at(room, px);
    if (!may_enter(to, room, s, px < 0 && b->through[s] != THROUGH_LEFT)) return 0;
    uint8_t result = rows_test(to, ((unsigned)px & 0xFF) >> 4, y, hitbox(room, s));
    if (!result && px < 0) b->through[s] = THROUGH_LEFT;
    tested(s, px, y, result);
    return 1;
}
/* Sprites_MoveRight: the test walking right, at the hitbox's right edge. */
static int right_hook(uint16_t addr) {
    (void)addr;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    unsigned s = regs.y & 7;
    int room;
    Block *b;
    if (!awake_mover(s, &room, &b)) return 0;
    uint8_t x = cyc_mod_peek(SLOT_X + s), y = cyc_mod_peek(SLOT_Y + s);
    int left = x16_of(b, s, x), px = left + width_less_1(room, s);
    if (left >= 0 && px < 256) return 0;                                        /* the original's test */
    int to = room_at(room, px);
    if (!may_enter(to, room, s, px >= 256 && b->through[s] != THROUGH_RIGHT)) return 0;
    uint8_t result = rows_test(to, ((unsigned)px & 0xFF) >> 4, y, hitbox(room, s));
    if (!result && px >= 256) b->through[s] = THROUGH_RIGHT;
    tested(s, px, y, result);
    return 1;
}
/* CurrentSprite_CanMoveInDirection's up and down test, with Arg_PixelPosY
 * set: the row there, across the hitbox's columns (one for every 16 of its
 * width, and one more if X is not on a block's edge), for a slot past a
 * side, whose columns lie in two rooms. */
static int vertical_hook(uint16_t addr) {
    (void)addr;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    unsigned s = regs.y & 7;
    int room;
    Block *b;
    if (!awake_mover(s, &room, &b)) return 0;
    int left = x16_of(b, s, cyc_mod_peek(SLOT_X + s));
    int ext = width_less_1(room, s);
    if (left >= 0 && left + ext < 256) return 0;                                /* the original's test */
    uint8_t py = cyc_mod_peek(PIXEL_Y);
    unsigned row = py & 0xF0u;
    int c = (left + 256) / 16 - 16;                                             /* floor(left / 16) */
    uint8_t result = column_cell(room, c, row);
    for (int t = ext; !result && t >= 16; t -= 16) result = column_cell(room, ++c, row);
    if (!result && (left & 15)) result = column_cell(room, ++c, row);
    tested(s, left, py, result);
    return 1;
}
/* Sprites_CheckPlacementInvalidAtY's block (Arg_PixelPosY set, the X there
 * X + 15 facing right, X facing left): the room it lies in, past a side. */
static int placement_hook(uint16_t addr) {
    (void)addr;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    unsigned s = regs.x & 7;
    int room;
    Block *b;
    if (!awake_mover(s, &room, &b)) return 0;
    int px = x16_of(b, s, cyc_mod_peek(SLOT_X + s)) + (cyc_mod_peek(FLAGS + s) & 1 ? 15 : 0);
    if (px >= 0 && px < 256) return 0;                                          /* the original's */
    int to = room_at(room, px);
    if (to < 0) return 0;
    uint8_t py = cyc_mod_peek(PIXEL_Y);
    unsigned index = (py & 0xF0u) | (((unsigned)px & 0xFF) >> 4);
    uint8_t result = solid(to, index, true);
    cyc_mod_poke(PIXEL_X, (uint8_t)px);
    regs.a = result;
    regs.x = (uint8_t)index;
    regs.p = (uint8_t)((regs.p & ~(FLAG_Z | FLAG_N)) | (result ? 0 : FLAG_Z));
    cyc_mod_set_regs(&regs);
    return 1;
}
/* Sprite_Maybe_TurnAroundIfAtScreenEdgeHoriz, after its move: X $F0 and up
 * is the screen's side for a flier or a shot, facing right its right side,
 * facing left wrapped past its left (unless it straddles the right one).
 * Returns "not at the edge", C clear, as the original's @_returnFalse. */
static int flier_hook(uint16_t addr) {
    (void)addr;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    unsigned s = regs.x & 7;
    int room;
    Block *b;
    if (!awake_mover(s, &room, &b)) return 0;
    uint8_t x = cyc_mod_peek(SLOT_X + s);
    if (x < 0xF0) return 0;
    if (cyc_mod_peek(FLAGS + s) & 1) {
        if (b->through[s] != THROUGH_RIGHT && b->through[s] != THROUGH_LEFT) {
            if (!may_enter(fax_world_joined(room, 1), room, s, true)) return 0;
            b->through[s] = THROUGH_RIGHT;
        }
    } else if (!b->through[s]) {
        if (!may_enter(fax_world_joined(room, 0), room, s, true)) return 0;
        b->through[s] = THROUGH_LEFT;
    }
    regs.a = x;
    regs.p = (uint8_t)((regs.p & ~(FLAG_C | FLAG_Z | FLAG_N)) | (x == 0xF0 ? FLAG_Z : 0) | ((x - 0xF0) & FLAG_N));
    cyc_mod_set_regs(&regs);
    return 1;
}

/* Sprite_Draw_Standard's or _FlippedHoriz's tile loop, played from the state
 * Sprite_Draw left (the disassembly's, cell for cell): each cell it puts in
 * OAM gets the slot as its owner, and each it skips for lying past the
 * screen's side, where its Y is on the screen, is kept as a cell past the
 * side. Sprites_IncNextSpriteSlot's count (two halves, $20 apart) gives the
 * OAM entry. */
static void play_draw(Drawn *d, unsigned s, bool flipped) {
    uint8_t cols = cyc_mod_peek(COLS), rows = cyc_mod_peek(ROWS);
    uint8_t normx = cyc_mod_peek(NORM_X), normy = cyc_mod_peek(NORM_Y);
    uint8_t offx = cyc_mod_peek(OFFSET_X), offy = cyc_mod_peek(OFFSET_Y);
    uint16_t addr = (uint16_t)(cyc_mod_peek(READ_ADDR) | cyc_mod_peek(READ_ADDR + 1) << 8);
    uint8_t flip = cyc_mod_peek(FLIP_MASK), visible = cyc_mod_peek(MOVING_VISIBILITY), ppu = cyc_mod_peek(PPU_OFFSET);
    uint8_t slot = cyc_mod_peek(SPRITE_SLOT), range = cyc_mod_peek(START_SLOT_RANGE), full = cyc_mod_peek(SLOTS_FULL);
    if (cols > 15 || rows > 15) return;
    if (flipped) {   /* the pivot: X moves by its double less the width, in 16 bits */
        uint8_t shift = (uint8_t)((cyc_mod_peek(TEMP_ADDR_L) << 1) - (cols + 1) * 8);
        unsigned sum = (unsigned)shift + normx;
        normx = (uint8_t)sum;
        offx = (uint8_t)(offx + (shift & 0x80 ? 0xFF : 0) + (sum >> 8));
    }
    unsigned y = 0;
    for (unsigned row = 0; row <= rows; row++)
        for (unsigned k = 0; k <= cols; k++) {
            unsigned tx = flipped ? cols - k : k;
            uint8_t tile = cyc_mod_peek((uint16_t)(addr + y));
            if (tile == 0xFF) { y += 1; continue; }
            unsigned lo = normx + cyc_mod_peek((uint16_t)(TILE_POSITIONS + tx));
            unsigned hi = offx + (lo >> 8);
            bool fits_x = (hi & 0xFF) == 0;
            unsigned ylo = normy + cyc_mod_peek((uint16_t)(TILE_POSITIONS + row)) + (fits_x ? hi >> 8 : 0);
            bool fits_y = ((offy + (ylo >> 8)) & 0xFF) == 0;
            uint8_t attr = (uint8_t)(cyc_mod_peek((uint16_t)(addr + y + 1)) ^ flip);
            if (visible & cyc_mod_peek((uint16_t)((flipped ? VISIBILITY_FLIPPED : VISIBILITY_MASK) + (tx & 1)))) attr |= 0x20;
            if (fits_x && fits_y && !full) {
                d->owner[(((unsigned)slot << 2 ^ range) >> 2) & 63] = (uint8_t)(s + 1);
                slot ^= 0x20;
                if (!(slot & 0x20) && ++slot >= 0x20) full = 1;
            } else if (!fits_x && fits_y && d->count < CELLS) {
                d->cell[d->count++] = (FaxSeamsCell){(int16_t)((int)(int8_t)(uint8_t)hi * 256 + (lo & 0xFF)),
                                                     (uint8_t)ylo, (uint8_t)(tile + ppu), attr, (uint8_t)s};
            }
            y += 2;
        }
}
/* Sprite_Draw_Standard's and Sprite_Draw_FlippedHoriz's entries, in a sprite
 * loop: a slot whose origin is across a side is drawn a screen over, then
 * the loop is played for its cells. */
static int draw_hook(uint16_t addr) {
    unsigned c = cyc_mod_isolated();
    if (!looping[c]) return 0;
    Block *b = loop_room[c] >= 0 ? block(loop_room[c]) : NULL;
    if (!b) return 0;
    unsigned s = cyc_mod_peek(CURRENT_SPRITE_INDEX) & 7;
    int x = x16_of(b, s, cyc_mod_peek(SLOT_X + s));
    if (x < 0 || x >= 256) cyc_mod_poke(OFFSET_X, (uint8_t)(cyc_mod_peek(OFFSET_X) + (x < 0 ? 0xFF : 1)));
    drawn[c].tiles[s] = b->tiles[s];
    play_draw(&drawn[c], s, addr == DRAW_FLIPPED);
    return 0;
}
/* Screen_LoadSpriteInfo in play: a screen loads its list. */
static int load_hook(uint16_t addr) {
    (void)addr;
    if (!cyc_mod_isolated()) fax_world_screen_loaded();
    return 0;
}

/* ---- slice 1: between frames ---- */

/* Slot s of `from`, across its side `side`, to a free slot of the room
 * there: every byte, its tiles, its wake, and a mark if it straddles the
 * new room's right side (in from the right). With nowhere to go (no free
 * slot, the room gone) it is put back inside its room, turned round, as at
 * a wall. */
static void move(int from, unsigned s, int side) {
    Block *bf = block(from);
    int to = fax_world_joined(from, side), d = -1;
    uint8_t x = rd(from, SLOT_X + s), e = rd(from, ENTITIES + s);
    unsigned ext = width_less_1(from, s);
    Block *bt = to >= 0 ? block(to) : NULL;
    for (int k = 7; bt && k >= 0 && d < 0; k--) if (rd(to, ENTITIES + (unsigned)k) == EMPTY) d = k;
    if (d < 0) {
        wr(from, SLOT_X + s, side ? (uint8_t)((255 - ext) & 0xF0) : 0);
        wr(from, FLAGS + s, (uint8_t)(rd(from, FLAGS + s) ^ 1));
        bf->through[s] = 0;
        ++pushed_back;
        return;
    }
    unsigned u = (unsigned)d;
    wr(to, SLOT_X + u, x);
    wr(to, SLOT_Y + u, rd(from, SLOT_Y + s));
    for (unsigned a = SLOT_ARRAYS; a < SLOT_ARRAYS + 23 * 8; a += 8) wr(to, a + u, rd(from, a + s));
    unsigned k = 0;
    while (k < 8 && (k == u || rd(to, ENTITIES + k) != e)) ++k;
    if (k < 8) {                                       /* its entity's tiles are where that slot's are */
        wr(to, PPU_OFFSETS + u, rd(to, PPU_OFFSETS + k));
        bt->tiles[u] = bt->tiles[k];
    } else {
        int home = bf->tiles[s] ? bf->tiles[s] - 1 : from;
        bt->tiles[u] = home == to ? 0 : (uint8_t)(home + 1);
    }
    bt->awake |= (uint8_t)(1u << u);
    bt->through[u] = side == 0 && x + ext >= 256 ? THROUGH_RIGHT : 0;
    wr(from, ENTITIES + s, EMPTY);
    bf->through[s] = bf->tiles[s] = 0;
    bf->awake &= (uint8_t)~(1u << s);
    int note[6] = {from, (int)s, to, d, e, (int)steps};
    memcpy(move_log[moves++ % LOG], note, sizeof note);
}

void fax_seams_step(void) {
    if (!enabled || fax_world_active_room() < 0) return;
    int vx, vy;
    bool view = fax_world_view(&vx, &vy);
    ++steps;
    for (int room = 0; room < ROOMS; room++) {
        Block *b = fax_world_room_live(room) ? block(room) : NULL;
        if (!b) continue;
        for (unsigned s = 0; s < 8; s++) {
            uint8_t e = rd(room, ENTITIES + s);
            if (e == EMPTY) {
                b->through[s] = b->tiles[s] = 0;
                b->awake &= (uint8_t)~(1u << s);
                continue;
            }
            if (view && category(e) == SHOT && !in_view(room, s, PAD)) {   /* out of the view: gone */
                wr(room, ENTITIES + s, EMPTY);
                b->through[s] = b->tiles[s] = 0;
                b->awake &= (uint8_t)~(1u << s);
                ++removed;
                continue;
            }
            if (!b->through[s]) continue;
            uint8_t x = rd(room, SLOT_X + s);
            if (b->through[s] == THROUGH_LEFT) {
                if (x >= 128) move(room, s, 0);
                else b->through[s] = 0;
            } else if (x < 128) {
                move(room, s, 1);
            } else if (x + width_less_1(room, s) < 256) {
                b->through[s] = 0;                   /* back inside */
            }
        }
    }
}

/* ---- what Seamless asks ---- */

void fax_seams_captured(uint8_t *bytes, int how) {
    Block *b = (Block *)bytes;
    if (!b) return;
    if (how == FAX_SEAMS_FRESH) { memset(b, 0, sizeof *b); return; }
    unsigned c = how == FAX_SEAMS_TICK;
    if (!c && block(loop_room[0]) != b) {       /* the live loop drew another room: nothing of this one's */
        memset(b->owner, 0, sizeof b->owner);
        b->count = 0;
        return;
    }
    memcpy(b->owner, drawn[c].owner, sizeof b->owner);
    memcpy(b->drawn_tiles, drawn[c].tiles, sizeof b->drawn_tiles);
    b->count = drawn[c].count;
    memcpy(b->cell, drawn[c].cell, sizeof b->cell);
}
unsigned fax_seams_cells(const uint8_t *bytes, const FaxSeamsCell **cells) {
    const Block *b = (const Block *)bytes;
    *cells = b->cell;
    return b->count <= CELLS ? b->count : 0;
}
int fax_seams_oam_slot(const uint8_t *bytes, unsigned oam) {
    const Block *b = (const Block *)bytes;
    return oam < 64 && b->owner[oam] ? b->owner[oam] - 1 : -1;
}
int fax_seams_slot_tiles(const uint8_t *bytes, unsigned slot) {
    const Block *b = (const Block *)bytes;
    return slot < 8 && b->drawn_tiles[slot] ? b->drawn_tiles[slot] - 1 : -1;
}
bool fax_seams_valid(const uint8_t *bytes, unsigned rooms) {
    const Block *b = (const Block *)bytes;
    if (b->count > CELLS) return false;
    for (unsigned s = 0; s < 8; s++)
        if (b->through[s] > THROUGH_RIGHT || b->tiles[s] > rooms || b->drawn_tiles[s] > rooms) return false;
    for (unsigned k = 0; k < 64; k++) if (b->owner[k] > 8) return false;
    for (unsigned k = 0; k < b->count; k++) if (b->cell[k].slot > 7) return false;
    return true;
}
void fax_seams_describe(const uint8_t *bytes, char *out, size_t size) {
    const Block *b = (const Block *)bytes;
    int t[8];
    for (unsigned s = 0; s < 8; s++) t[s] = b->tiles[s] ? b->tiles[s] - 1 : -1;
    snprintf(out, size, "\"awake\":%u,\"through\":[%u,%u,%u,%u,%u,%u,%u,%u],\"tiles\":[%d,%d,%d,%d,%d,%d,%d,%d],\"cells\":%u",
             b->awake, b->through[0], b->through[1], b->through[2], b->through[3], b->through[4], b->through[5],
             b->through[6], b->through[7], t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7], b->count);
}

/* ---- the plugin ---- */

static const struct { const char *id; uint16_t addr; NESModFunctionEntryCallback hook; } hooks[] = {
    {"faxanadu.seams.reach", OUT_OF_WEAPON_RANGE, reach_hook},
    {"faxanadu.seams.touch", CHECK_HIT_PLAYER, touch_hook},
    {"faxanadu.seams.loop", SPRITES_UPDATE_ALL, loop_hook},
    {"faxanadu.seams.loop-done", SPRITES_LOOP_DONE, loop_done_hook},
    {"faxanadu.seams.behavior", UPDATE_BEHAVIOR, behavior_hook},
    {"faxanadu.seams.state", UPDATE_STATE, state_hook},
    {"faxanadu.seams.left", MOVE_LEFT, left_hook},
    {"faxanadu.seams.right", MOVE_RIGHT, right_hook},
    {"faxanadu.seams.vertical", MOVE_VERTICAL, vertical_hook},
    {"faxanadu.seams.placement", PLACEMENT_BLOCK, placement_hook},
    {"faxanadu.seams.flier", FLIER_EDGE, flier_hook},
    {"faxanadu.seams.draw", DRAW_STANDARD, draw_hook},
    {"faxanadu.seams.draw-flipped", DRAW_FLIPPED, draw_hook},
    {"faxanadu.seams.load", LOAD_SPRITE_INFO, load_hook},
};
enum { HOOKS = sizeof hooks / sizeof hooks[0] };

static void deactivate(void) {
    enabled = false;
    skipped = refused = 0;
    last_weapon_x = 0x7FFF;
    last_sprite_x = 0;
    last_entity = 0xFF;
    steps = moves = kept_out = removed = woken = frozen = pushed_back = 0;
    memset(move_log, 0, sizeof move_log);
    for (unsigned i = 0; i < 4; i++) last_woken[i] = -1;
    looping[0] = looping[1] = false;
    loop_room[0] = loop_room[1] = -1;
    for (unsigned i = 0; i < HOOKS; i++) nes_mod_set_function_hook_enabled(hooks[i].id, 0);
}
static void activate(void) {
    enabled = true;
    for (unsigned i = 0; i < HOOKS; i++) nes_mod_set_function_hook_enabled(hooks[i].id, 1);
    nes_mod_set_local_only("Seams", 1);
}

#ifdef CYC_WITH_SDL
static void seams_state(int id, const char *line) {
    (void)line;
    char fields[1400], log[700] = "";
    int vx = 0, vy = 0;
    bool view = fax_world_view(&vx, &vy);
    size_t at = 0;
    for (unsigned i = moves > LOG ? moves - LOG : 0; i < moves; i++) {   /* oldest first */
        const int *m = move_log[i % LOG];
        at += (size_t)snprintf(log + at, sizeof log - at, "%s[%d,%d,%d,%d,%d,%d]", at ? "," : "", m[0], m[1], m[2],
                               m[3], m[4], m[5]);
    }
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"skipped\":%u,\"refused\":%u,\"last_weapon_x\":%d,\"last_sprite_x\":%u,\"last_entity\":%u,"
             "\"steps\":%u,\"moves\":%u,\"kept_out\":%u,\"pushed_back\":%u,\"removed\":%u,\"woken\":%u,\"frozen\":%u,"
             "\"view\":[%d,%d,%s],\"last_woken\":[%d,%d,%d,%d],\"log\":[%s]",
             enabled ? "true" : "false", skipped, refused, last_weapon_x, last_sprite_x, last_entity, steps, moves,
             kept_out, pushed_back, removed, woken, frozen, vx, vy, view ? "true" : "false", last_woken[0],
             last_woken[1], last_woken[2], last_woken[3], log);
    cyc_tcp_ok(id, fields);
}
void fax_seams_tcp_setup(void) {
    cyc_tcp_register("seams_state",
                     "Seams: touches skipped in neighbouring rooms' copies, weapon hits refused for wrapping round the "
                     "screen; actors moved through seams, kept out of full rooms, shots gone out of the view, "
                     "sleepers woken and kept frozen, the view, the last move and wake",
                     seams_state);
}
#else
void fax_seams_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_seams) {
    for (unsigned i = 0; i < HOOKS; i++)
        if (!nes_mod_register_function_entry_plugin(hooks[i].id, hooks[i].addr, hooks[i].hook)) abort();
    if (!nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.seams", activate)) abort();
}
