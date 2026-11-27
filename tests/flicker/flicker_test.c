/* Unit tests for No sprite flicker (fax_flicker.c), without the emulator or a
 * ROM. The game's sprite slots are modelled as the disassembly has them:
 *
 * - Sprites_Reset: every Y to $F0, sprite 0 the status bar's (Y $17, tile
 *   $7F, attributes $23, X 8), the next slot 1, the slots not full, and
 *   Sprites_StartSlotRange ($1C) flipped between $00 and $80;
 * - Sprite_Draw: each cell, unless Sprites_SlotsFull, at (slot x 4) EOR $1C;
 * - Sprites_IncNextSpriteSlot: slot EOR $20, and in the lower half one more,
 *   full at 32.
 *
 * A frame draws as Game_MainLoop does: the hero first (a 2x4 body), then the
 * actors. Scenes are painted with fax_flicker_paint_order, sprite by sprite,
 * the last painted in front, onto a canvas that records whose pixel is on
 * top; a sprite's 8x8 cell is all opaque. So the checks compare who is in
 * front, frame against frame, with the order on and off (the NES's own).
 * And the frames Seamless takes: those whose shadow OAM OnInterrupt copies. */
#include "fax_flicker.c"
#include <stdio.h>
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

static uint8_t zero_page[0x20]; /* Game_InterruptsHandledLatch, Game_NeedOAMReset */
uint8_t cyc_mod_peek(uint16_t addr) { return addr < sizeof zero_page ? zero_page[addr] : 0; }
int nes_mod_register_reset_callback(NESModActivationCallback cb) { return cb == deactivate; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    return !strcmp(id, "faxanadu.no-flicker") && cb == activate;
}

/* ---- the machine: the shadow OAM and the slot variables ---- */
static uint8_t oam[256], range, slot, full;

static void sprites_reset(void) { /* Sprites_Reset, sprite 0 mode 0 */
    for (unsigned i = 0; i < 256; i += 4) oam[i] = 0xF0;
    oam[0] = 0x17;
    oam[1] = 0x7F;
    oam[2] = 0x23;
    oam[3] = 8;
    slot = 1;
    full = 0;
    range = (uint8_t)((range & 0x80) ^ 0x80);
}
static void inc_slot(void) { /* Sprites_IncNextSpriteSlot */
    slot ^= 0x20;
    if (slot & 0x20) return;
    if (++slot >= 0x20) ++full;
}
/* One cell of Sprite_Draw. The tile names its object, to tell them apart. */
static void cell(uint8_t y, uint8_t tile, uint8_t attr, uint8_t x) {
    if (full) return;
    uint8_t i = (uint8_t)(slot << 2 ^ range);
    oam[i] = y;
    oam[i + 1] = tile;
    oam[i + 2] = attr;
    oam[i + 3] = x;
    inc_slot();
}
/* An object, columns x rows of cells from (x, y), row by row, left to right. */
static void object(uint8_t id, int x, int y, int columns, int rows) {
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < columns; ++c) cell((uint8_t)(y + 8 * r), id, 0, (uint8_t)(x + 8 * c));
}

/* ---- painting ---- */
enum { W = 256, H = 240, HERO = 1 }; /* OAM X is a byte: the NES's 256 pixels */
static uint8_t canvas[H][W];         /* the tile (object) on top; 0 nothing */

static void paint(void) {
    memset(canvas, 0, sizeof canvas);
    uint8_t order[FAX_FLICKER_SPRITES];
    unsigned n = fax_flicker_paint_order(range, order);
    for (unsigned k = 0; k < n; ++k) {
        const uint8_t *o = oam + order[k] * 4;
        if (o[0] >= 0xEF) continue;
        for (int dy = 0; dy < 8; ++dy)
            for (int dx = 0; dx < 8; ++dx) {
                int px = o[3] + dx, py = o[0] + 1 + dy;
                if (px < W && py < H) canvas[py][px] = o[1];
            }
    }
}

/* A frame of the town: the hero at x, two villagers over him and one apart. */
static void town(int hero_x) {
    sprites_reset();
    object(HERO, hero_x, 150, 2, 4);
    object(2, hero_x + 6, 150, 2, 4);
    object(3, hero_x - 5, 150, 2, 4);
    object(4, hero_x + 60, 150, 2, 4);
}

static void order_is_every_sprite(void) {
    for (int on = 0; on < 2; ++on) {
        on ? activate() : deactivate();
        for (unsigned r = 0; r < 2; ++r) {
            uint8_t order[FAX_FLICKER_SPRITES], seen[64] = { 0 };
            CHECK(fax_flicker_paint_order((uint8_t)(r * 0x80), order) == FAX_FLICKER_SPRITES);
            for (unsigned k = 0; k < FAX_FLICKER_SPRITES; ++k) {
                CHECK(order[k] >= 1 && order[k] <= 63); /* never sprite 0, the status bar's */
                CHECK(!seen[order[k]]);
                seen[order[k]] = 1;
            }
            if (!on)
                for (unsigned k = 0; k < FAX_FLICKER_SPRITES; ++k) CHECK(order[k] == 63 - k);
        }
    }
    activate();
    uint8_t order[FAX_FLICKER_SPRITES];
    /* The last painted: slots 33, 1 and 32 (the second drawn when sprite 0 is
     * off, so never in play: it is the first slot past full). */
    fax_flicker_paint_order(0x00, order);
    CHECK(order[60] == 33 && order[61] == 1 && order[62] == 32);
    fax_flicker_paint_order(0x80, order); /* the halves swapped: slot 33 at index 1 */
    CHECK(order[60] == 1 && order[61] == 33 && order[62] == 32);
}

/* The NES's order swaps who is in front over the hero every frame; the paint
 * order keeps the hero in front, and the same picture on both frames. */
static void hero_stays_in_front(void) {
    static uint8_t before[2][H][W];
    for (int on = 0; on < 2; ++on) {
        on ? activate() : deactivate();
        range = 0;
        int hidden[2] = { 0, 0 };
        for (int frame = 0; frame < 2; ++frame) {
            town(100);
            CHECK(range == (frame ? 0x00 : 0x80)); /* the two halves, a frame each */
            paint();
            memcpy(before[frame], canvas, sizeof canvas);
            for (int y = 151; y < 151 + 32; ++y)
                for (int x = 100; x < 116; ++x) hidden[frame] += canvas[y][x] != HERO;
        }
        if (on) {
            CHECK(hidden[0] == 0 && hidden[1] == 0);
            CHECK(!memcmp(before[0], before[1], sizeof before[0]));
        } else { /* the original: hidden on both, by halves */
            CHECK(hidden[0] > 0 && hidden[1] > 0);
            CHECK(memcmp(before[0], before[1], sizeof before[0]) != 0);
        }
    }
    activate();
}

/* Two actors over each other keep their order: the first drawn (Sprites_UpdateAll
 * draws slot 7 first) in front, on both frames. */
static void actors_keep_their_order(void) {
    activate();
    for (int frame = 0; frame < 2; ++frame) {
        sprites_reset();
        object(HERO, 20, 40, 2, 4);
        object(7, 200, 150, 2, 4);
        object(6, 204, 152, 2, 4);
        paint();
        for (int y = 153; y < 151 + 32; ++y)
            for (int x = 204; x < 216; ++x) CHECK(canvas[y][x] == 7);
        CHECK(canvas[152 + 32][210] == 6); /* below the first, the second shows */
    }
}

/* Every sprite on a line is painted, all 62 the slots hold: 32 side by side
 * across one line's 256 pixels, and 30 across another. */
static void every_sprite_on_a_line(void) {
    activate();
    for (int frame = 0; frame < 2; ++frame) {
        sprites_reset();
        for (int i = 0; i < 62; ++i) object((uint8_t)(10 + i), 8 * (i % 32), i < 32 ? 100 : 140, 1, 1);
        CHECK(full);         /* 62 drawn: the slots are full */
        cell(100, 99, 0, 0); /* one more is not drawn, as in the game */
        paint();
        for (int i = 0; i < 62; ++i)
            for (int x = 8 * (i % 32); x < 8 * (i % 32) + 8; ++x) CHECK(canvas[i < 32 ? 104 : 144][x] == 10 + i);
    }
}

/* The NMI's rule for copying the shadow OAM, at a frame's end: WaitForNextFrame
 * clears Game_InterruptsHandledLatch ($10) and waits for OnInterrupt to set
 * it; a frame that ends with it set is one the game lagged, and the PPU keeps
 * its sprites unless Game_NeedOAMReset ($1B) asks. */
static void frames_the_nmi_copies(void) {
    for (int on = 0; on < 2; ++on) {
        on ? activate() : deactivate();
        zero_page[0x10] = 0;
        zero_page[0x1B] = 0; /* waiting in WaitForNextFrame */
        CHECK(fax_flicker_frame_drawn());
        zero_page[0x10] = 1;                     /* still drawing: a lagging frame */
        CHECK(fax_flicker_frame_drawn() == !on); /* off: Seamless took every frame */
        zero_page[0x1B] = 1;                     /* the NMI copies it anyway */
        CHECK(fax_flicker_frame_drawn());
    }
    activate();
}

int main(void) {
    frames_the_nmi_copies();
    order_is_every_sprite();
    hero_stays_in_front();
    actors_keep_their_order();
    every_sprite_on_a_line();
    printf("flicker unit tests: %u checks ok\n", checks);
    return 0;
}
