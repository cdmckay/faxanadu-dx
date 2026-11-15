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
 *     code (@_reduceEnemyHP, $81FD), and a Deluge's knockback. With Block up
 *     and anything else in the shield slot, the site is left to Block
 *     (fax_block.c), which stops it.
 *   - Ointment: the original's flash never checks DurationOintment (bit 7
 *     clear while it lasts), unlike every other hit. In reach, under Ointment,
 *     the flash does its own steps but Player_ReduceHP: the hit sound, 60
 *     frames of invincibility and knockback (Player_HandleIFrames ends the
 *     knockback when they reach $39, so it needs them), and no HP.
 *   Otherwise the game's damage goes on.
 * Sugata's death ends the band, as the game's death code ends its greyscale.
 * See docs/GAME_INTERNALS.md. */
#include "fax_sugata.h"
#include "fax_block.h"
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

extern uint32_t hw_palette_argb[512];   /* the host's NES colors, emphasis in bits 6-8 */

enum { SLOTS = 8, FLASH_SHOWN = 8, BLINK = 4, HUD = 32, ROOM_H = 208 };

/* RAM */
#define TEMP_00                         0x0000u
#define PPU_MASK                        0x000Bu   /* bit 0: greyscale, written to $2001 in the NMI */
#define PLAYER_POS_X                    0x009Eu   /* the hero's left edge; he is 16 pixels wide */
#define PLAYER_STATUS_FLAG              0x00A5u   /* $02: knockback */
#define PLAYER_INVINCIBILITY_PHASE      0x00ADu   /* iframes, from $3C down; knockback until $38 */
#define CURRENT_SPRITES_XPOS            0x00BAu   /* a sprite's left edge */
#define CURRENT_SPRITES_ENTITIES        0x02CCu
#define CURRENT_SPRITES_BEHAVIORS       0x02D4u   /* the behavior running; bit 7 once it has finished */
#define CURRENT_SPRITES_FLAGS           0x02DCu   /* $40: the behavior is under way (Sprite_SetBehaviorReady) */
#define CURRENT_SPRITES_HIT_BY_MAGIC    0x0334u   /* the knockback a spell gave it, $FF for none */
#define CURRENT_SPRITES_HIT_BY_MAGIC_IFRAMES 0x033Cu
#define CURRENT_SPRITE_INDEX            0x0378u
#define SELECTED_SHIELD                 0x03BFu
#define DURATION_OINTMENT               0x0427u   /* bit 7 clear while the Ointment lasts; $FF none */
/* Bank 14 */
#define REDUCE_ENEMY_HP   0x81FDu   /* Player_HitEnemyWithMagic's @_reduceEnemyHP: HP - Temp_00; on a kill,
                                       Sugata's greyscale clear, experience, sound 3 and the death effect */
#define INVISIBLE_DRAW    0x8C98u   /* SpriteUpdateHandler_Invisible: LDA #0, STA Sprites_PPUOffset,
                                       STA MovingSpriteVisibility, RTS */
#define SUGATA_DRAW       0x929Fu   /* SpriteUpdateHandler_Enemy_Sugata: JSR CurrentSprite_UpdateFlipMask */
#define FLASH_BEHAVIOR    0xAB26u   /* SpriteBehavior_FlashScreenHitPlayer: JSR Sprite_IsBehaviorNotReady,
                                       BNE $AB39, LDA #$02, STA CurrentSprites_BehaviorData2,X */
#define FLASH_GREYSCALE   0xAB36u   /* JSR Sprite_SetBehaviorReady, after LDA PPU_Mask, ORA #$01, STA PPU_Mask */
#define FLASH_HIT         0xAB44u   /* LDA #$04, JSR Sound_PlayEffect: the damage, greyscale just cleared */
#define FLASH_FINISH      0xAB60u   /* LDX CurrentSpriteIndex, JMP Sprite_FinishBehavior */
/* Values */
#define SUGATA            0x18u
#define BEHAVIOR_FLASH    0x1Fu
#define BEHAVIOR_READY    0x40u
#define SHIELD_MAGIC      2u
#define SOUND_ENEMY_HIT   0x02u     /* what a spell's hit plays (Player_HitEnemyWithMagic) */
#define SOUND_HERO_HIT    0x04u     /* what the flash plays */
#define KNOCKBACK         0x02u
#define IFRAMES           0x3Cu
#define DELUGE_KNOCKBACK  0u        /* CAST_MAGIC_HIT_BEHAVIORS[Deluge] */
#define DELUGE_IFRAMES    4u        /* CAST_MAGIC_HIT_IFRAMES[Deluge] */

typedef enum { NO_BAND, WARNING_BAND, FLASH_BAND } Kind;
typedef struct { uint8_t kind, slot, center, frames; } Band;

static bool enabled;
static uint8_t warn[SLOTS];          /* frames of warning a slot's Sugata has given before this flash */
static Band band;
static unsigned warnings, warned, flashes, in_reach, out_of_reach, reflected, reflect_failed, anointed;   /* warned: flashes after a full warning */
static int last_distance;            /* the hero's middle less Sugata's, at the last flash */
static bool last_reached;

static uint8_t peek(uint16_t a) { return cyc_mod_peek(a); }
static unsigned slot(void) { return peek(CURRENT_SPRITE_INDEX) & (SLOTS - 1); }
static int middle(unsigned s) { return peek((uint16_t)(CURRENT_SPRITES_XPOS + s)) + 8; }   /* 16 wide (hitbox 1) */

bool fax_sugata_reaches(uint8_t hero_x, int center) {
    return hero_x < center + SUGATA_REACH && hero_x + 16 > center - SUGATA_REACH;
}

/* ---- the hooks ---- */

/* SpriteBehavior_FlashScreenHitPlayer: warn before the flash starts. */
static int behavior_hook(uint16_t addr) {
    (void)addr;
    if (!enabled || cyc_mod_isolated()) return 0;
    unsigned s = slot();
    if (peek((uint16_t)(CURRENT_SPRITES_ENTITIES + s)) != SUGATA ||
        peek((uint16_t)(CURRENT_SPRITES_FLAGS + s)) & BEHAVIOR_READY)
        return 0;
    if (warn[s] < SUGATA_WARNING) {
        if (!warn[s]) ++warnings;
        ++warn[s];
        return 1;   /* this frame, the behavior does nothing, as when it waits */
    }
    warn[s] = 0;
    ++warned;
    return 0;
}

/* SpriteUpdateHandler_Enemy_Sugata: blink through the warning. */
static int blink_hook(uint16_t addr) {
    (void)addr;
    if (!enabled || cyc_mod_isolated()) return 0;
    unsigned s = slot();
    if (!warn[s] || !((SUGATA_WARNING - warn[s]) & BLINK)) return 0;
    fax_block_return_via(INVISIBLE_DRAW);
    return 1;
}

/* The behavior has just set greyscale: the band instead. */
static int greyscale_hook(uint16_t addr) {
    (void)addr;
    if (!enabled || cyc_mod_isolated()) return 0;
    cyc_mod_poke(PPU_MASK, (uint8_t)(peek(PPU_MASK) & ~1u));
    unsigned s = slot();
    band = (Band){ FLASH_BAND, (uint8_t)s, (uint8_t)(middle(s) - 8), FLASH_SHOWN };
    ++flashes;
    return 0;
}

static void reflect(unsigned s) {
    fax_block_play_sound(SOUND_ENEMY_HIT);
    cyc_mod_poke(TEMP_00, SUGATA_REFLECTED);
    CycModRegs regs, call;
    cyc_mod_regs(&regs);
    call = regs;
    call.x = (uint8_t)s;
    bool ok = cyc_mod_call_commit(REDUCE_ENEMY_HP, &call);
    cyc_mod_set_regs(&regs);
    if (!ok) { ++reflect_failed; return; }
    if (peek((uint16_t)(CURRENT_SPRITES_ENTITIES + s)) == SUGATA) {   /* still alive: a spell's knockback */
        cyc_mod_poke((uint16_t)(CURRENT_SPRITES_HIT_BY_MAGIC + s), DELUGE_KNOCKBACK);
        cyc_mod_poke((uint16_t)(CURRENT_SPRITES_HIT_BY_MAGIC_IFRAMES + s), DELUGE_IFRAMES);
    }
    ++reflected;
}

/* The flash is about to hurt: only in reach. The Magic Shield sends it back,
 * Block stops it (fax_block.c's site, next), and Ointment keeps the HP. */
static int hit_hook(uint16_t addr) {
    (void)addr;
    if (!enabled || cyc_mod_isolated()) return 0;
    unsigned s = slot();
    int center = middle(s);
    uint8_t hero = peek(PLAYER_POS_X);
    last_distance = hero + 8 - center;
    last_reached = fax_sugata_reaches(hero, center);
    if (!last_reached) {
        ++out_of_reach;
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

void fax_sugata_frame_end(void) {
    if (!enabled) return;
    for (unsigned s = 0; s < SLOTS; ++s)
        if (warn[s] && (peek((uint16_t)(CURRENT_SPRITES_ENTITIES + s)) != SUGATA ||
                        peek((uint16_t)(CURRENT_SPRITES_BEHAVIORS + s)) != BEHAVIOR_FLASH))
            warn[s] = 0;
    if (band.kind == FLASH_BAND && (!--band.frames || peek((uint16_t)(CURRENT_SPRITES_ENTITIES + band.slot)) != SUGATA))
        band.kind = NO_BAND;
    if (band.kind == FLASH_BAND) return;
    band.kind = NO_BAND;
    for (unsigned s = 0; s < SLOTS; ++s)
        if (warn[s]) { band = (Band){ WARNING_BAND, (uint8_t)s, (uint8_t)(middle(s) - 8), 0 }; break; }
}

/* ---- the picture ---- */

/* The PPU's greyscale keeps a color's row of the palette and drops its hue:
 * entry & $30. Pictures hold colors, so find each one's entry (black is $0F,
 * the game's), falling back to the grey of nearest brightness. */
static uint32_t greys[4];
static struct { uint32_t from, to; } seen[256];
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
static uint32_t dim(uint32_t c) {   /* a third darker */
    uint32_t r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
    return 0xFF000000u | (r - r / 3) << 16 | (g - g / 3) << 8 | (b - b / 3);
}

static uint32_t *canvas;
static size_t capacity;
#ifdef CYC_WITH_SDL
static char capture_path[1024];
static int capture_kind = NO_BAND;
static unsigned capture_at, captured;   /* a capture waits for this many frames of its band */
static unsigned warning_frames(void) {
    unsigned most = 0;
    for (unsigned s = 0; s < SLOTS; ++s) if (warn[s] > most) most = warn[s];
    return most;
}
#endif

const uint32_t *fax_sugata_present(const uint32_t *picture, int *w, int *h) {
    if (!enabled || band.kind == NO_BAND) return picture;
    int width = 0, height = 0, ox, oy, left, right;
    const uint32_t *from = picture;
    if (from) {
        width = *w; height = *h;
        if (!fax_world_origin(&ox, &oy)) return picture;
        left = 0; right = width;
    } else {
        from = cyc_render_present(&width, &height);
        if (!from) return picture;
        ox = cyc_render_native_origin(width); oy = HUD;
        left = ox; right = ox + 256;
    }
    if (width <= 0 || height <= 0) return picture;
    size_t n = (size_t)width * (size_t)height;
    if (n > capacity) {
        uint32_t *grown = realloc(canvas, n * sizeof *grown);
        if (!grown) return picture;
        canvas = grown; capacity = n;
    }
    memcpy(canvas, from, n * sizeof *canvas);
    if (!palette_ready) prepare();
    int center = band.center + 8;
    int x0 = ox + center - SUGATA_REACH, x1 = ox + center + SUGATA_REACH;
    int y0 = oy < HUD ? HUD : oy, y1 = oy + ROOM_H > height ? height : oy + ROOM_H;
    if (x0 < left) x0 = left;
    if (x1 > right) x1 = right;
    if (x0 < 0) x0 = 0;
    if (x1 > width) x1 = width;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            uint32_t *p = canvas + (size_t)y * (size_t)width + (size_t)x;
            *p = band.kind == FLASH_BAND ? grey(*p) : dim(*p);
        }
#ifdef CYC_WITH_SDL
    if (capture_kind == band.kind && capture_path[0] &&
        (band.kind == FLASH_BAND ? FLASH_SHOWN - band.frames >= capture_at : warning_frames() >= capture_at)) {
        if (cyc_write_png(capture_path, canvas, width, height)) ++captured;
        capture_kind = NO_BAND;
    }
#endif
    *w = width; *h = height;
    return canvas;
}

/* ---- the machine's record ---- */

static const char *const hook_ids[] = {
    "faxanadu.sugata.behavior", "faxanadu.sugata.blink", "faxanadu.sugata.greyscale", "faxanadu.sugata.hit" };
static void hooks(int on) {
    for (unsigned i = 0; i < sizeof hook_ids / sizeof hook_ids[0]; ++i) nes_mod_set_function_hook_enabled(hook_ids[i], on);
}
static void deactivate(void) {
    enabled = false;
    memset(warn, 0, sizeof warn);
    band = (Band){ 0 };
    warnings = warned = flashes = in_reach = out_of_reach = reflected = reflect_failed = anointed = 0;
    last_distance = 0; last_reached = false;
    hooks(0);
}
static void activate(void) {
    enabled = true;
    hooks(1);
    nes_mod_set_local_only("Sugata's flash", 1);
}

/* The warning decides when the flash comes, so it travels with the machine. */
enum { RECORD = 1 + SLOTS + 4, VERSION = 1 };
static int state_get(uint8_t *buf, int cap) {
    if (cap < RECORD) return -1;
    buf[0] = VERSION;
    memcpy(buf + 1, warn, SLOTS);
    buf[1 + SLOTS] = band.kind; buf[2 + SLOTS] = band.slot; buf[3 + SLOTS] = band.center; buf[4 + SLOTS] = band.frames;
    return RECORD;
}
static int state_validate(const uint8_t *buf, int len) {
    if (len == 0) return 1;
    if (len != RECORD || buf[0] != VERSION || buf[1 + SLOTS] > FLASH_BAND || buf[2 + SLOTS] >= SLOTS ||
        buf[4 + SLOTS] > FLASH_SHOWN)
        return 0;
    for (unsigned s = 0; s < SLOTS; ++s) if (buf[1 + s] > SUGATA_WARNING) return 0;
    return 1;
}
static int state_set(const uint8_t *buf, int len) {
    if (!state_validate(buf, len)) return 0;
    if (!len) { memset(warn, 0, sizeof warn); band = (Band){ 0 }; return 1; }
    memcpy(warn, buf + 1, SLOTS);
    band = (Band){ buf[1 + SLOTS], buf[2 + SLOTS], buf[3 + SLOTS], buf[4 + SLOTS] };
    return 1;
}

#ifdef CYC_WITH_SDL
static const char *kind_name(int k) { return k == FLASH_BAND ? "flash" : k == WARNING_BAND ? "warning" : "none"; }
static void sugata_state(int id, const char *line) {
    (void)line;
    char fields[512];
    int x0 = band.kind ? band.center + 8 - SUGATA_REACH : 0, x1 = band.kind ? band.center + 8 + SUGATA_REACH : 0;
    unsigned warning = warning_frames();
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"warnings\":%u,\"warning\":%u,\"warned\":%u,\"flashes\":%u,\"in_reach\":%u,\"out_of_reach\":%u,"
             "\"reflected\":%u,\"reflect_failed\":%u,\"anointed\":%u,\"last_distance\":%d,\"last_reached\":%s,"
             "\"band\":\"%s\",\"band_x\":[%d,%d],\"captured\":%u",
             enabled ? "true" : "false", warnings, warning, warned, flashes, in_reach, out_of_reach, reflected,
             reflect_failed, anointed, last_distance, last_reached ? "true" : "false", kind_name(band.kind), x0, x1, captured);
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
    if (!cyc_tcp_str(line, "path", capture_path, sizeof capture_path) || !cyc_tcp_str(line, "band", kind, sizeof kind) ||
        (strcmp(kind, "warning") && strcmp(kind, "flash"))) {
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
    cyc_tcp_register("sugata_capture", "Save the presented picture when the band next shows: path, band", sugata_capture);
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
        !nes_mod_register_savestate_validator("faxanadu.sugata-flash", state_validate)) abort();
}
