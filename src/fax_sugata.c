/* Sugata's flash. USA 57dd23d1, chipx86 byte-exact disassembly.
 *
 * Sugata (entity $18) walks toward the hero in steps (BSCRIPTS_ENEMY_SUGATA)
 * and then switches to behavior $1F, SpriteBehavior_FlashScreenHitPlayer
 * ($AB26, bank 14), the only enemy that uses it. Its first call sets the
 * greyscale bit of PPU_Mask ($0B, which the NMI writes to $2001) and
 * CurrentSprites_BehaviorData2 to 2; the second clears the bit and, at $AB44,
 * plays sound 4, gives 60 frames of invincibility and knockback, and takes
 * 10 HP with Player_ReduceHP, wherever the hero is. Then it finishes the
 * behavior ($AB60) and the script starts over.
 *
 * Here, at the same sites:
 *   - Warning: while the behavior is not yet under way, its call is skipped
 *     for SUGATA_WARNING frames, and on every other four of them
 *     SpriteUpdateHandler_Enemy_Sugata returns through
 *     SpriteUpdateHandler_Invisible, so Sugata blinks where it stands. The
 *     blink is counted back from the flash, so its last four frames show
 *     Sugata: the picture lags its sprites by a frame or two, and a blink
 *     ending hidden would hide Sugata in the flash's first frames.
 *   - The band: where the behavior has just set greyscale, it is cleared
 *     again and the presented picture greys only the band within reach (and
 *     dims it faintly through the warning), as the PPU's greyscale would:
 *     each color becomes the grey of its row of the palette.
 *   - Range: at $AB44, out of reach the damage is skipped and the behavior
 *     finishes as the game's does. In reach, with Block up and the Magic
 *     Shield, the flash goes back to Sugata: the enemy-hit sound and
 *     SUGATA_REFLECTED HP through Player_HitEnemyWithMagic's own HP and death
 *     code (@_reduceEnemyHP, $81FD), and a Deluge's knockback, away from the
 *     hero, with its damage number (fax_damage.c) and, if it kills, its MP
 *     (fax_mana.c), which that committed call cannot give itself (it runs no
 *     hooks). With Block up
 *     and anything else in the shield slot, the site is left to Block
 *     (fax_block.c), which stops it.
 *   - Ointment: the original's flash never checks DurationOintment (bit 7
 *     clear while it lasts), unlike every other hit. In reach, under Ointment,
 *     the flash does its own steps but Player_ReduceHP: the hit sound, 60
 *     frames of invincibility and knockback (Player_HandleIFrames ends the
 *     knockback when they reach $39, so it needs them), and no HP.
 *   Otherwise the game's damage goes on.
 * Sugata's death ends the band, as the game's death code ends its greyscale.
 *
 * In Seamless a neighbouring room's Sugata does the same (Seams, fax_seams.c,
 * #166's slice 2), in that room's tick: it warns and blinks there, its band
 * stands where it does (a band for each Sugata, at most FAX_SUGATA_BANDS),
 * and at the damage its reach is measured to the hero's true place, his row
 * of rooms only. In reach, the live game takes the flash: the copy's
 * behavior waits at $AB44 (BehaviorData2 back to 1, so each tick comes back
 * to it) until Seams' visitors' pass brings that Sugata into the live game
 * and enters $AB44 for it, where all of the above runs on the live hero.
 * Places are in 16 bits (fax_seams_x16): a visitor's from its side.
 * See docs/GAME_INTERNALS.md. */
#include "fax_sugata.h"
#include "fax_block.h"
#include "fax_damage.h"
#include "fax_mana.h"
#include "fax_seams.h"
#include "fax_world.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#ifdef CYC_WITH_SDL
#include "cyc_png.h"
#include "cyc_tcp.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint32_t hw_palette_argb[512]; /* the host's NES colors, emphasis in bits 6-8 */

enum {
    SLOTS = 8,
    FLASH_SHOWN = 8,
    BLINK = 4,
    HUD = 32,
    ROOM_H = 208,
    ROOMS = 83,
    NO_ROOM = ROOMS,
    BANDS = FAX_SUGATA_BANDS
};

/* RAM */
#define TEMP_00                              0x0000u
#define PPU_MASK                             0x000Bu /* bit 0: greyscale, written to $2001 in the NMI */
#define PLAYER_POS_X                         0x009Eu /* the hero's left edge; he is 16 pixels wide */
#define PLAYER_STATUS_FLAG                   0x00A5u /* $02: knockback */
#define PLAYER_INVINCIBILITY_PHASE           0x00ADu /* iframes, from $3C down; knockback until $38 */
#define CURRENT_SPRITES_XPOS                 0x00BAu /* a sprite's left edge */
#define CURRENT_SPRITES_ENTITIES             0x02CCu
#define CURRENT_SPRITES_BEHAVIORS            0x02D4u /* the behavior running; bit 7 once it has finished */
#define CURRENT_SPRITES_BEHAVIOR_DATA2       0x02ECu /* the flash's countdown: 2, then 1, and the damage at 0 */
/* $40: the behavior is under way (Sprite_SetBehaviorReady);
 * $01: facing right */
#define CURRENT_SPRITES_FLAGS                0x02DCu
#define CURRENT_SPRITES_HIT_BY_MAGIC         0x0334u /* the knockback a spell gave it, $FF for none */
#define CURRENT_SPRITES_HIT_BY_MAGIC_IFRAMES 0x033Cu
#define CURRENT_SPRITE_INDEX                 0x0378u
#define SELECTED_SHIELD                      0x03BFu
#define DURATION_OINTMENT                    0x0427u /* bit 7 clear while the Ointment lasts; $FF none */
/* Bank 14 */
/* Player_HitEnemyWithMagic's @_reduceEnemyHP: HP - Temp_00; on a kill,
 * Sugata's greyscale clear, experience, sound 3 and the death effect */
#define REDUCE_ENEMY_HP                      0x81FDu
/* SpriteUpdateHandler_Invisible: LDA #0, STA Sprites_PPUOffset,
 * STA MovingSpriteVisibility, RTS */
#define INVISIBLE_DRAW                       0x8C98u
#define SUGATA_DRAW      0x929Fu /* SpriteUpdateHandler_Enemy_Sugata: JSR CurrentSprite_UpdateFlipMask */
/* SpriteBehavior_FlashScreenHitPlayer: JSR Sprite_IsBehaviorNotReady,
 * BNE $AB39, LDA #$02, STA CurrentSprites_BehaviorData2,X */
#define FLASH_BEHAVIOR   0xAB26u
#define FLASH_GREYSCALE  0xAB36u /* JSR Sprite_SetBehaviorReady, after LDA PPU_Mask, ORA #$01, STA PPU_Mask */
#define FLASH_HIT        0xAB44u /* LDA #$04, JSR Sound_PlayEffect: the damage, greyscale just cleared */
#define FLASH_FINISH     0xAB60u /* LDX CurrentSpriteIndex, JMP Sprite_FinishBehavior */
/* Values */
#define SUGATA           0x18u
#define BEHAVIOR_FLASH   0x1Fu
#define BEHAVIOR_READY   0x40u
#define FACING_RIGHT     0x01u /* CurrentSprites_Flags */
#define SHIELD_MAGIC     2u
#define SOUND_ENEMY_HIT  0x02u /* what a spell's hit plays (Player_HitEnemyWithMagic) */
#define SOUND_HERO_HIT   0x04u /* what the flash plays */
#define KNOCKBACK        0x02u
#define IFRAMES          0x3Cu
#define DELUGE_KNOCKBACK 0u /* CAST_MAGIC_HIT_BEHAVIORS[Deluge] */
#define DELUGE_IFRAMES   4u /* CAST_MAGIC_HIT_IFRAMES[Deluge] */

typedef enum { NO_BAND, WARNING_BAND, FLASH_BAND } Kind;
/* A band: its Sugata's room (NO_ROOM without Seamless) and slot, and its
 * middle in that room's pixels. */
typedef struct {
    uint8_t kind, room, slot, frames;
    int16_t center;
} Band;

static bool enabled;
/* Frames of warning a slot's Sugata has given before this flash, by the room
 * it runs in (the live one, or a neighbour's tick), and a row for no room. */
static uint8_t warn[ROOMS + 1][SLOTS];
static Band bands[BANDS];
static unsigned warnings, warned, flashes, in_reach, out_of_reach, reflected, reflect_failed,
    anointed;                                                          /* warned: flashes after a full warning */
static unsigned near_warnings, near_flashes, near_out_of_reach, waits; /* the same, in neighbours' ticks; and their
                                                                            flashes left to the live game */
static int last_distance;    /* the hero's middle less Sugata's, at the last flash */
static int last_center = -1; /* the middle of the last flash's band */
static int last_room = -1;   /* its room */
static int last_pushed;      /* the last reflection's knockback: 1 right, -1 left */
static bool last_reached;

static uint8_t peek(uint16_t a) { return cyc_mod_peek(a); }
static unsigned slot(void) { return peek(CURRENT_SPRITE_INDEX) & (SLOTS - 1); }
static int middle(unsigned s) { return fax_seams_x16(s) + 8; } /* 16 wide (hitbox 1), in 16 bits */
/* The hooks act in the live game and in a neighbour's tick, nowhere else. */
static bool ours(void) { return !cyc_mod_isolated() || fax_world_ticking(); }
static unsigned here(void) {
    int r = fax_world_seams_room();
    return r >= 0 && r < ROOMS ? (unsigned)r : NO_ROOM;
}
/* A byte of a room's slots: the machine's for the live room (or no room),
 * its record's for a neighbour. */
static uint8_t room_byte(unsigned room, uint16_t addr) {
    if (room == NO_ROOM || (int)room == fax_world_active_room()) return peek(addr);
    const uint8_t *ram = fax_world_room_record((int)room);
    return ram ? ram[addr & 0x7FF] : 0;
}

bool fax_sugata_reaches(int hero_x, int center) {
    return hero_x < center + SUGATA_REACH && hero_x + 16 > center - SUGATA_REACH;
}

/* The band of a room's slot, or a free one (NULL when all are taken). */
static Band *band_of(unsigned room, unsigned s, bool add) {
    Band *free_band = NULL;
    for (unsigned i = 0; i < BANDS; ++i) {
        if (bands[i].kind && bands[i].room == room && bands[i].slot == s) return &bands[i];
        if (!bands[i].kind && !free_band) free_band = &bands[i];
    }
    return add ? free_band : NULL;
}

/* ---- the hooks ---- */

/* SpriteBehavior_FlashScreenHitPlayer: warn before the flash starts. */
static int behavior_hook(uint16_t addr) {
    (void)addr;
    if (!enabled || !ours()) return 0;
    unsigned s = slot();
    uint8_t *w = &warn[here()][s];
    if (peek((uint16_t)(CURRENT_SPRITES_ENTITIES + s)) != SUGATA ||
        peek((uint16_t)(CURRENT_SPRITES_FLAGS + s)) & BEHAVIOR_READY)
        return 0;
    if (*w < SUGATA_WARNING) {
        if (!*w) ++*(cyc_mod_isolated() ? &near_warnings : &warnings);
        ++*w;
        return 1; /* this frame, the behavior does nothing, as when it waits */
    }
    *w = 0;
    if (!cyc_mod_isolated()) ++warned;
    return 0;
}

/* SpriteUpdateHandler_Enemy_Sugata: blink through the warning. */
static int blink_hook(uint16_t addr) {
    (void)addr;
    if (!enabled || !ours()) return 0;
    unsigned s = slot(), w = warn[here()][s];
    if (!w || !((SUGATA_WARNING - w) & BLINK)) return 0;
    fax_block_return_via(INVISIBLE_DRAW);
    return 1;
}

/* The behavior has just set greyscale: the band instead. */
static int greyscale_hook(uint16_t addr) {
    (void)addr;
    if (!enabled || !ours()) return 0;
    cyc_mod_poke(PPU_MASK, (uint8_t)(peek(PPU_MASK) & ~1u));
    unsigned s = slot(), room = here();
    Band *b = band_of(room, s, true);
    if (b) *b = (Band){ FLASH_BAND, (uint8_t)room, (uint8_t)s, FLASH_SHOWN, (int16_t)middle(s) };
    last_center = middle(s);
    last_room = room == NO_ROOM ? -1 : (int)room;
    ++*(cyc_mod_isolated() ? &near_flashes : &flashes);
    return 0;
}

/* As Sprite_CheckHitByCastMagic does for a spell's hit: the knockback, and
 * the facing it moves Sugata in (CurrentSprites_Flags bit 0, set: right),
 * the way a spell from the hero would fly, so away from him; then the sound
 * and the HP, through Player_HitEnemyWithMagic's own code. */
static void reflect(unsigned s) {
    uint8_t flags = (uint8_t)(peek((uint16_t)(CURRENT_SPRITES_FLAGS + s)) & ~FACING_RIGHT);
    if (middle(s) >= peek(PLAYER_POS_X) + 8) flags |= FACING_RIGHT; /* in 16 bits: across a seam too */
    last_pushed = flags & FACING_RIGHT ? 1 : -1;
    cyc_mod_poke((uint16_t)(CURRENT_SPRITES_FLAGS + s), flags);
    fax_block_play_sound(SOUND_ENEMY_HIT);
    /* Its number, at Sugata as it is now: the committed call runs no hooks,
     * and a kill makes it the death effect. */
    fax_damage_enemy_hit(s, SUGATA_REFLECTED);
    cyc_mod_poke(TEMP_00, SUGATA_REFLECTED);
    CycModRegs regs, call;
    cyc_mod_regs(&regs);
    call = regs;
    call.x = (uint8_t)s;
    bool ok = cyc_mod_call_commit(REDUCE_ENEMY_HP, &call);
    cyc_mod_set_regs(&regs);
    if (!ok) {
        ++reflect_failed;
        return;
    }
    if (peek((uint16_t)(CURRENT_SPRITES_ENTITIES + s)) == SUGATA) { /* still alive: a spell's knockback */
        cyc_mod_poke((uint16_t)(CURRENT_SPRITES_HIT_BY_MAGIC + s), DELUGE_KNOCKBACK);
        cyc_mod_poke((uint16_t)(CURRENT_SPRITES_HIT_BY_MAGIC_IFRAMES + s), DELUGE_IFRAMES);
    } else {
        fax_mana_killed(SUGATA); /* the kill's experience came through the call: its MP, which no hook gave */
    }
    ++reflected;
}

/* The flash is about to hurt: only in reach. The Magic Shield sends it back,
 * Block stops it (fax_block.c's site, next), and Ointment keeps the HP. In a
 * neighbour's tick, in reach of the hero's true place, it waits here for the
 * live game (Seams' visitors' pass), which enters this site for it. */
static int hit_hook(uint16_t addr) {
    (void)addr;
    if (!enabled || !ours()) return 0;
    unsigned s = slot();
    int center = middle(s), hero = 0;
    bool known = fax_seams_hero_x(&hero);
    last_distance = hero + 8 - center;
    last_reached = known && fax_sugata_reaches(hero, center);
    if (last_reached && cyc_mod_isolated() && fax_seams_flash()) {
        cyc_mod_poke((uint16_t)(CURRENT_SPRITES_BEHAVIOR_DATA2 + s), 1); /* the next call comes back here */
        ++waits;
        return 1; /* the behavior's RTS: nothing more this tick */
    }
    if (!last_reached || cyc_mod_isolated()) {
        ++*(cyc_mod_isolated() ? &near_out_of_reach : &out_of_reach);
        fax_block_return_via(FLASH_FINISH);
        return 1;
    }
    ++in_reach;
    if (fax_block_raised()) {
        if (peek(SELECTED_SHIELD) != SHIELD_MAGIC) return 0;
        reflect(s);
        fax_block_return_via(FLASH_FINISH);
        return 1;
    }
    if (!(peek(DURATION_OINTMENT) & 0x80)) {
        fax_block_play_sound(SOUND_HERO_HIT);
        cyc_mod_poke(PLAYER_INVINCIBILITY_PHASE, IFRAMES);
        cyc_mod_poke(PLAYER_STATUS_FLAG, (uint8_t)(peek(PLAYER_STATUS_FLAG) | KNOCKBACK));
        fax_block_return_via(FLASH_FINISH);
        ++anointed;
        return 1;
    }
    return 0;
}

/* A Sugata still there: the slot holds one, its behavior the flash (a
 * warning) or anything (a flash's band). */
static bool still(unsigned room, unsigned s, bool flashing) {
    return room_byte(room, (uint16_t)(CURRENT_SPRITES_ENTITIES + s)) == SUGATA &&
           (!flashing || room_byte(room, (uint16_t)(CURRENT_SPRITES_BEHAVIORS + s)) == BEHAVIOR_FLASH);
}
void fax_sugata_frame_end(void) {
    if (!enabled) return;
    for (unsigned r = 0; r <= ROOMS; ++r)
        for (unsigned s = 0; s < SLOTS; ++s)
            if (warn[r][s] && !still(r, s, true)) warn[r][s] = 0;
    for (unsigned i = 0; i < BANDS; ++i) {
        Band *b = &bands[i];
        if (b->kind == FLASH_BAND && (!--b->frames || !still(b->room, b->slot, false))) b->kind = NO_BAND;
        if (b->kind == WARNING_BAND) b->kind = NO_BAND; /* made again below while it warns */
    }
    for (unsigned r = 0; r <= ROOMS; ++r)
        for (unsigned s = 0; s < SLOTS; ++s) {
            if (!warn[r][s] || band_of(r, s, false)) continue;
            Band *b = band_of(r, s, true);
            int x = r == NO_ROOM ? peek((uint16_t)(CURRENT_SPRITES_XPOS + s)) : fax_seams_room_x16((int)r, s);
            if (b) *b = (Band){ WARNING_BAND, (uint8_t)r, (uint8_t)s, 0, (int16_t)(x + 8) };
        }
}

/* ---- the picture ---- */

/* The PPU's greyscale keeps a color's row of the palette and drops its hue:
 * entry & $30. Pictures hold colors, so find each one's entry (black is $0F,
 * the game's), falling back to the grey of nearest brightness. */
static uint32_t greys[4];
static struct {
    uint32_t from, to;
} seen[256];
static bool palette_ready;
static void prepare(void) {
    memset(seen, 0, sizeof seen);
    for (int g = 0; g < 4; ++g) greys[g] = hw_palette_argb[g * 0x10] | 0xFF000000u;
    for (int n = -1; n < 64; ++n) {
        int e = n < 0 ? 0x0F : n;
        uint32_t c = hw_palette_argb[e] | 0xFF000000u;
        unsigned h = (c * 2654435761u) >> 24;
        while (seen[h].from && seen[h].from != c) h = (h + 1) & 255;
        if (!seen[h].from) seen[h].from = c, seen[h].to = greys[(e & 0x30) >> 4];
    }
    palette_ready = true;
}
static unsigned luma(uint32_t c) { return (2 * (c >> 16 & 255) + 5 * (c >> 8 & 255) + (c & 255)) / 8; }
static uint32_t grey(uint32_t c) {
    if (!palette_ready) prepare();
    c |= 0xFF000000u;
    for (unsigned h = (c * 2654435761u) >> 24, n = 0; seen[h].from && n < 256; h = (h + 1) & 255, ++n)
        if (seen[h].from == c) return seen[h].to;
    unsigned y = luma(c), best = 0, gap = 1000;
    for (unsigned g = 0; g < 4; ++g) {
        unsigned d = luma(greys[g]) > y ? luma(greys[g]) - y : y - luma(greys[g]);
        if (d < gap) gap = d, best = g;
    }
    return greys[best];
}
uint32_t fax_sugata_grey(uint32_t color) { return grey(color); }
static uint32_t dim(uint32_t c) { /* a third darker */
    uint32_t r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
    return 0xFF000000u | (r - r / 3) << 16 | (g - g / 3) << 8 | (b - b / 3);
}

static uint32_t *canvas;
static size_t capacity;
#ifdef CYC_WITH_SDL
static char capture_path[1024];
static int capture_kind = NO_BAND;
static unsigned capture_at, captured; /* a capture waits for this many frames of its band */
static unsigned warning_frames(void) {
    unsigned most = 0;
    for (unsigned r = 0; r <= ROOMS; ++r)
        for (unsigned s = 0; s < SLOTS; ++s)
            if (warn[r][s] > most) most = warn[r][s];
    return most;
}
#endif

/* Where a band's room's top-left is in the picture: Seamless's room origin,
 * or with no room, the picture's own. */
static bool band_origin(const Band *b, bool world, int width, int *ox, int *oy) {
    if (!world) {
        if (b->room != NO_ROOM && (int)b->room != fax_world_active_room()) return false;
        *ox = cyc_render_native_origin(width);
        *oy = HUD;
        return true;
    }
    if (b->room == NO_ROOM) return fax_world_origin(ox, oy);
    return fax_world_room_origin(fax_world_area(), b->room, ox, oy);
}

const uint32_t *fax_sugata_present(const uint32_t *picture, int *w, int *h) {
    unsigned shown = 0;
    for (unsigned i = 0; i < BANDS; ++i) shown += bands[i].kind != NO_BAND;
    if (!enabled || !shown) return picture;
    int width = 0, height = 0, left, right, ox, oy;
    const uint32_t *from = picture;
    bool world = from != NULL;
    if (world) {
        width = *w;
        height = *h;
        if (!fax_world_origin(&ox, &oy)) return picture;
        left = 0;
        right = width;
    } else {
        from = cyc_render_present(&width, &height);
        if (!from) return picture;
        left = cyc_render_native_origin(width);
        right = left + 256;
    }
    if (width <= 0 || height <= 0) return picture;
    size_t n = (size_t)width * (size_t)height;
    if (n > capacity) {
        uint32_t *grown = realloc(canvas, n * sizeof *grown);
        if (!grown) return picture;
        canvas = grown;
        capacity = n;
    }
    memcpy(canvas, from, n * sizeof *canvas);
    if (!palette_ready) prepare();
    for (int kind = WARNING_BAND; kind <= FLASH_BAND; ++kind) /* a flash over a warning */
        for (unsigned i = 0; i < BANDS; ++i) {
            const Band *b = &bands[i];
            if (b->kind != kind || !band_origin(b, world, width, &ox, &oy)) continue;
            int x0 = ox + b->center - SUGATA_REACH, x1 = ox + b->center + SUGATA_REACH;
            int y0 = oy < HUD ? HUD : oy, y1 = oy + ROOM_H > height ? height : oy + ROOM_H;
            if (x0 < left) x0 = left;
            if (x1 > right) x1 = right;
            if (x0 < 0) x0 = 0;
            if (x1 > width) x1 = width;
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x) {
                    uint32_t *p = canvas + (size_t)y * (size_t)width + (size_t)x;
                    *p = kind == FLASH_BAND ? grey(*p) : dim(*p);
                }
        }
#ifdef CYC_WITH_SDL
    for (unsigned i = 0; i < BANDS && capture_path[0] && capture_kind != NO_BAND; ++i) {
        const Band *b = &bands[i];
        if (capture_kind == b->kind && (b->kind == FLASH_BAND ? (unsigned)(FLASH_SHOWN - b->frames) >= capture_at
                                                              : warning_frames() >= capture_at)) {
            if (cyc_write_png(capture_path, canvas, width, height)) ++captured;
            capture_kind = NO_BAND;
        }
    }
#endif
    *w = width;
    *h = height;
    return canvas;
}

/* ---- the machine's record ---- */

static const char *const hook_ids[] = { "faxanadu.sugata.behavior", "faxanadu.sugata.blink",
                                        "faxanadu.sugata.greyscale", "faxanadu.sugata.hit" };
static void hooks(int on) {
    for (unsigned i = 0; i < sizeof hook_ids / sizeof hook_ids[0]; ++i)
        nes_mod_set_function_hook_enabled(hook_ids[i], on);
}
static void deactivate(void) {
    enabled = false;
    memset(warn, 0, sizeof warn);
    memset(bands, 0, sizeof bands);
    warnings = warned = flashes = in_reach = out_of_reach = reflected = reflect_failed = anointed = 0;
    near_warnings = near_flashes = near_out_of_reach = waits = 0;
    last_distance = 0;
    last_reached = false;
    last_center = -1;
    last_room = -1;
    last_pushed = 0;
    hooks(0);
}
static void activate(void) {
    enabled = true;
    hooks(1);
    nes_mod_set_local_only("Sugata's flash", 1);
}

/* The warning decides when the flash comes, so it travels with the machine.
 * Version 2 (#166's slice 2) keeps it for every room and the bands, each
 * kind, room, slot, frames and middle (2 bytes, little-endian). A version 1
 * record (the live room's warning and one band) loads as none: the next
 * flash warns afresh. */
enum {
    WARNS = (ROOMS + 1) * SLOTS,
    BAND_BYTES = 6,
    RECORD = 1 + WARNS + BANDS * BAND_BYTES,
    VERSION = 2,
    RECORD_1 = 1 + SLOTS + 4
};
static int state_get(uint8_t *buf, int cap) {
    if (cap < RECORD) return -1;
    buf[0] = VERSION;
    memcpy(buf + 1, warn, WARNS);
    for (unsigned i = 0; i < BANDS; ++i) {
        uint8_t *p = buf + 1 + WARNS + i * BAND_BYTES;
        const Band *b = &bands[i];
        p[0] = b->kind;
        p[1] = b->room;
        p[2] = b->slot;
        p[3] = b->frames;
        p[4] = (uint8_t)b->center;
        p[5] = (uint8_t)((uint16_t)b->center >> 8);
    }
    return RECORD;
}
static int state_validate(const uint8_t *buf, int len) {
    if (len == 0) return 1;
    if (len == RECORD_1 && buf[0] == 1) return 1;
    if (len != RECORD || buf[0] != VERSION) return 0;
    for (unsigned i = 0; i < WARNS; ++i)
        if (buf[1 + i] > SUGATA_WARNING) return 0;
    for (unsigned i = 0; i < BANDS; ++i) {
        const uint8_t *p = buf + 1 + WARNS + i * BAND_BYTES;
        if (p[0] > FLASH_BAND || p[1] > NO_ROOM || p[2] >= SLOTS || p[3] > FLASH_SHOWN) return 0;
    }
    return 1;
}
static int state_set(const uint8_t *buf, int len) {
    if (!state_validate(buf, len)) return 0;
    memset(warn, 0, sizeof warn);
    memset(bands, 0, sizeof bands);
    if (len != RECORD) return 1;
    memcpy(warn, buf + 1, WARNS);
    for (unsigned i = 0; i < BANDS; ++i) {
        const uint8_t *p = buf + 1 + WARNS + i * BAND_BYTES;
        bands[i] = (Band){ p[0], p[1], p[2], p[3], (int16_t)(p[4] | p[5] << 8) };
    }
    return 1;
}

#ifdef CYC_WITH_SDL
static const char *kind_name(int k) { return k == FLASH_BAND ? "flash" : k == WARNING_BAND ? "warning" : "none"; }
/* "band" and "band_x" are the live room's band if it has one, else the
 * first; "bands" all of them: kind, room (-1 none), slot, and its reach in
 * its room's pixels. */
static void sugata_state(int id, const char *line) {
    (void)line;
    char fields[1100], list[400] = "";
    const Band *shown = NULL;
    size_t at = 0;
    for (unsigned i = 0; i < BANDS; ++i) {
        const Band *b = &bands[i];
        if (!b->kind) continue;
        if (!shown || ((int)b->room == fax_world_active_room() && (int)shown->room != fax_world_active_room()) ||
            (b->room == NO_ROOM && shown->room != NO_ROOM))
            shown = b;
        at += (size_t)snprintf(list + at, sizeof list - at, "%s[\"%s\",%d,%u,%d,%d]", at ? "," : "", kind_name(b->kind),
                               b->room == NO_ROOM ? -1 : b->room, b->slot, b->center - SUGATA_REACH,
                               b->center + SUGATA_REACH);
    }
    int x0 = shown ? shown->center - SUGATA_REACH : 0, x1 = shown ? shown->center + SUGATA_REACH : 0;
    int f0 = last_center < 0 ? 0 : last_center - SUGATA_REACH, f1 = last_center < 0 ? 0 : last_center + SUGATA_REACH;
    unsigned warning = warning_frames();
    snprintf(
        fields, sizeof fields,
        "\"enabled\":%s,\"warnings\":%u,\"warning\":%u,\"warned\":%u,\"flashes\":%u,\"in_reach\":%u,\"out_of_reach\":%u,"
        "\"reflected\":%u,\"reflect_failed\":%u,\"pushed\":%d,\"anointed\":%u,\"last_distance\":%d,\"last_reached\":%s,"
        "\"band\":\"%s\",\"band_x\":[%d,%d],\"flash_x\":[%d,%d],\"flash_room\":%d,\"captured\":%u,"
        "\"near_warnings\":%u,\"near_flashes\":%u,\"near_out_of_reach\":%u,\"waits\":%u,\"bands\":[%s]",
        enabled ? "true" : "false", warnings, warning, warned, flashes, in_reach, out_of_reach, reflected,
        reflect_failed, last_pushed, anointed, last_distance, last_reached ? "true" : "false",
        kind_name(shown ? shown->kind : NO_BAND), x0, x1, f0, f1, last_room, captured, near_warnings, near_flashes,
        near_out_of_reach, waits, list);
    cyc_tcp_ok(id, fields);
}
/* Write the presented picture to `path` the next time a band of that kind
 * ("warning" or "flash") is drawn, `at` frames into it or later: the flash
 * lasts FLASH_SHOWN frames, and Sugata is hidden in the warning's frames 4-7,
 * 12-15 and so on. */
static void sugata_capture(int id, const char *line) {
    char kind[16] = "";
    long at = 0;
    cyc_tcp_long(line, "at", &at);
    capture_at = at > 0 ? (unsigned)at : 0;
    if (!cyc_tcp_str(line, "path", capture_path, sizeof capture_path) ||
        !cyc_tcp_str(line, "band", kind, sizeof kind) || (strcmp(kind, "warning") && strcmp(kind, "flash"))) {
        capture_path[0] = 0;
        cyc_tcp_err(id, "sugata_capture: path, and band warning|flash");
        return;
    }
    capture_kind = strcmp(kind, "flash") ? WARNING_BAND : FLASH_BAND;
    cyc_tcp_ok(id, NULL);
}
void fax_sugata_tcp_setup(void) {
    cyc_tcp_register("sugata_state", "Sugata's flash: warnings, flashes in and out of reach, reflections, the band",
                     sugata_state);
    cyc_tcp_register("sugata_capture", "Save the presented picture when the band next shows: path, band",
                     sugata_capture);
}
#else
void fax_sugata_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_sugata) {
    if (!nes_mod_register_function_entry_plugin(hook_ids[0], FLASH_BEHAVIOR, behavior_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[1], SUGATA_DRAW, blink_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[2], FLASH_GREYSCALE, greyscale_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[3], FLASH_HIT, hit_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.sugata-flash", activate) ||
        !nes_mod_register_savestate_hook("faxanadu.sugata-flash", state_get, state_set) ||
        !nes_mod_register_savestate_validator("faxanadu.sugata-flash", state_validate))
        abort();
}
