/* MP from kills. USA 57dd23d1, chipx86 byte-exact disassembly.
 *
 * MP comes from kills, as hearts do in Castlevania, not from waiting (#125:
 * #98's refill over time is gone). With the remaster:
 * - Every kill the game gives experience for gives MP. The weapon's and
 *   magic's kills (Player_HitSpriteWithWeapon, Player_HitEnemyWithMagic)
 *   each call Player_AddExperienceFromSprite with X the slot, on the frame
 *   the enemy's HP borrows, bosses' too, and nothing else does, so that is
 *   where it hooks: a fall, a spring or anything the hero did not kill gives
 *   none. Sugata's flash turned back by the Magic Shield kills through
 *   Player_HitEnemyWithMagic's own code in a committed call, which runs no
 *   hooks, so Sugata's flash (fax_sugata.c) tells this module
 *   (fax_mana_killed).
 * - How much (kill_mp): the enemy's experience (ENEMY_EXPERIENCE) squared,
 *   / 512, at least 1, and a boss (SPRITE_CATEGORIES_BY_ENTITY 7) BOSS_MP.
 *   The Magician's lesson (fax_lesson.c) doubles it. MP stops at 80.
 * - Nothing holds it: a kill's MP comes while the Wing Boots fly or the Hour
 *   Glass freezes (fax_recharge.c), which read MP afresh every frame and
 *   take theirs from what there is.
 * - Talking to a Guru fills HP and MP to 80 (fax_mana_guru, from Recharging
 *   items' guru_hook at his first script action, before he speaks), the
 *   safety net for a stretch with nothing to kill.
 *
 * How, on the unchanged ROM: a kill's MP goes into Player_MP at once, so
 * every cost and drain that frame sees it, and its bar is drawn at the end
 * of the frame, with Player_SetMP in a committed call, as the game's own
 * fills draw it. Player_SetMP queues the bar's tiles and its partial tile's
 * pattern, the NMI that starts the next frame sends them, and
 * PPUBuffer_QueueCommandOrLength waits for room, which it cannot do in a
 * committed call (no NMI runs), so only a buffer with room for both of the
 * bar's commands will do; on a quiet screen it is empty. A frame's end will
 * do when the screen is up (loaded, not scrolling) and the CPU waits in
 * WaitForNextFrame, from anywhere (the main loop, a pause, a text box): the
 * code that called it is between two of its steps, with no command half
 * queued (PPUBuffer_WaitForCapacity spins without it). Otherwise, or with
 * no room, the next frame's end that will. A Guru's HP and MP go in at once
 * too, and each bar is drawn at once, between two of the interpreter's
 * actions, if the buffer has room (UI_DrawPlayerHP, Player_SetMP); at his
 * first action it is usually still full of his box, so a frame or two into
 * his conversation. The bars still to draw are this feature's save-state
 * record. MP is in no save file
 * (continuing fills it). Nothing happens inside Seamless's isolated
 * simulation of other rooms. With the remaster off, the original: MP only
 * from the meat shops, Eolis's Magician, the Elixir and a spawn. See
 * docs/GAME_INTERNALS.md, "MP and its bar" and "Kills and experience". */
#include "fax_mana.h"
#include "fax_lesson.h"
#include "cyc_mod.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#ifdef CYC_WITH_SDL
#include "cyc_tcp.h"
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* RAM */
#define SCREEN_READY_STATE       0x000Eu /* Screen_ReadyState: 0 once a screen has loaded */
#define INTERRUPTS_ON            0x0013u /* Game_InterruptHandlersEnabled */
#define PPU_BUFFER_READ          0x001Fu /* PPUBuffer_ReadOffset */
#define PPU_BUFFER_WRITE         0x0020u /* PPUBuffer_WriteOffset: the buffer is $0500-$05FF, circular */
#define SCROLL_DIRECTION         0x0054u /* Screen_ScrollDirection: $FF not scrolling (0 on the title) */
#define CURRENT_SPRITES_ENTITIES 0x02CCu /* 8 slots, $FF empty */
#define PLAYER_MP                0x039Au /* at most 80 */
#define PLAYER_HP_U              0x0431u /* whole HP, at most 80; Player_HP_L ($0432) the 256ths */
#define BAR_WIDTH                0x04EEu /* UI_MPAndHPBarWidth: 10 tiles, set by UI_DrawHUDSprites */
/* Bank 14 */
/* Player_AddExperienceFromSprite: LDY CurrentSprites_Entities,X,
 * LDA ENEMY_EXPERIENCE,Y, STA Temp_Int24, ..., JSR Player_UpdateExperience;
 * from both kills, before the death's sound or effect */
#define ADD_EXPERIENCE           0x8B87u
#define SPRITE_CATEGORIES        0xB544u /* SPRITE_CATEGORIES_BY_ENTITY: 0 an enemy, 7 a boss, ... */
/* experience by entity: 10-65 for an enemy, 75-158 for a boss (0 for the
 * last two) */
#define ENEMY_EXPERIENCE         0xB60Eu
/* Fixed bank */
/* LDA Player_HP_U, JMP UI_DrawPlayerHPValue: caps at 80, stores, then
 * UI_SetHUDBarValue with Y = 0 draws the Power bar */
#define UI_DRAW_PLAYER_HP        0xC0BDu
#define PLAYER_SET_MP            0xFA85u /* CMP #$51, BCC, LDA #$50, STA Player_MP, LDY #$01: draws the Magic bar */
#define WAIT_FOR_NEXT_FRAME      0xCA25u /* LDA #$00, STA Game_InterruptsHandledLatch, then LDA, BEQ until the NMI */
/* Values */
enum { MP_MAX = 80, HP_MAX = 80, CATEGORY_BOSS = 7, JSR = 0x20, NOT_SCROLLING = 0xFF };
/* PPUBuffer_HasCapacity: room (ReadOffset - WriteOffset) is 0 (empty) or at
 * least $24. A bar's first command is 3 bytes and BarWidth + 1 tiles, and
 * the second (16 bytes of the partial tile) checks again after it. */
enum { PPU_ROOM = 0x24, COMMAND = 3 };

/* What a kill gives, in MP: decided in #125, and halved after Cameron's
 * playtest (#202), in which Deluge could be cast without end; retuning means
 * changing these and nothing else. The aim: an early fight gives back about
 * a Deluge (2 MP), a mid-game one half a Tilte (8 of its 16). A screen's
 * enemies give about twice the experience from the Trunk's first screens
 * (two Monodrons or Zozuras, 40-60) to a median screen of the Mist or the
 * Branch (89-96), while the aim grows four times, so no share of experience
 * meets both (experience / 10 gives the first screens 4 to 6 MP, two or
 * three Deluges): MP grows with its square instead, experience² / 512 (#125's
 * / 256 halved). A Monodron (20), a Hornet (25) or a Zozura (30) gives 1, a
 * Wolfman (49) 4, a Sir Gawaine or a Grimlock (58) 6, a Charron (65) 8; the
 * Trunk's first screens 2 (1 to 3), the Mist's and the Branch's median
 * screens 7 and 8. Every kill gives at least LEAST, and a boss (met once,
 * alone, at a cost) BOSS_MP, a quarter of the bar. The lesson doubles all of
 * it. */
enum { SCALE = 512, LEAST = 1, BOSS_MP = 20, TAUGHT = 2 };

static unsigned kill_mp(uint8_t experience, uint8_t category, bool learned) {
    unsigned mp = category == CATEGORY_BOSS ? (unsigned)BOSS_MP : (unsigned)experience * experience / SCALE;
    if (mp < LEAST) mp = LEAST;
    return learned ? mp * TAUGHT : mp;
}
/* MP after a gain: at most 80. */
static uint8_t with_gain(uint8_t mp, unsigned gain) {
    unsigned now = mp + gain;
    return (uint8_t)(now > MP_MAX ? MP_MAX : now);
}

/* The bars still to draw: what a save state must keep. */
enum { BAR_HP = 1, BAR_MP = 2, BARS = BAR_HP | BAR_MP };

static bool enabled;
static uint8_t bars;
typedef struct {
    unsigned kills, bosses, gained; /* kills, of them bosses, and the MP they gave (after the cap) */
    unsigned restores;              /* Gurus who filled HP and MP */
    unsigned drawn, deferred;       /* bars drawn; and frames one waited for room */
    uint8_t entity, experience, mp; /* the last kill: whom, its experience, its MP before the cap */
} Seen;
static Seen seen; /* for TCP */

/* ---- the machine ---- */

static uint8_t q(uint16_t addr) { return cyc_mod_peek(addr); }

/* The CPU waits in WaitForNextFrame, which pushes nothing: the return
 * address at S + 1 is that of a JSR WaitForNextFrame (it points at the
 * JSR's last byte), wherever it was called from (the caller's code is
 * mapped: it is running). */
static bool waits(const CycModRegs *regs) {
    uint16_t pushed = (uint16_t)(q((uint16_t)(0x100u | (uint8_t)(regs->s + 1))) |
                                 q((uint16_t)(0x100u | (uint8_t)(regs->s + 2))) << 8);
    return q((uint16_t)(pushed - 2)) == JSR && q((uint16_t)(pushed - 1)) == (WAIT_FOR_NEXT_FRAME & 0xFF) &&
           q(pushed) == WAIT_FOR_NEXT_FRAME >> 8;
}
/* The screen has loaded and is not scrolling: the NMI sends the PPU buffer,
 * and the status bar is where the bars go. */
static bool screen_up(void) {
    return q(SCREEN_READY_STATE) == 0 && q(INTERRUPTS_ON) && q(SCROLL_DIRECTION) == NOT_SCROLLING;
}
/* The PPU buffer has room for a bar: both of UI_SetHUDBarValue's
 * PPUBuffer_WaitForCapacity calls would return at once. */
static bool bar_fits(void) {
    uint8_t room = (uint8_t)(q(PPU_BUFFER_READ) - q(PPU_BUFFER_WRITE));
    return room == 0 || room >= PPU_ROOM + COMMAND + q(BAR_WIDTH) + 1;
}
/* `routine` with A = `a`, committed; the CPU's registers as they were. */
static bool commit(uint16_t routine, uint8_t a) {
    CycModRegs regs, call;
    cyc_mod_regs(&regs);
    call = regs;
    call.a = a;
    bool ok = cyc_mod_call_commit(routine, &call);
    cyc_mod_set_regs(&regs);
    return ok;
}
/* Each bar still to draw, as the game draws it, if the buffer has room for
 * it (after the one before, if both). */
static void draw_bars(void) {
    if ((bars & BAR_HP) && bar_fits() && commit(UI_DRAW_PLAYER_HP, 0)) bars &= (uint8_t)~BAR_HP, ++seen.drawn;
    if ((bars & BAR_MP) && bar_fits() && commit(PLAYER_SET_MP, q(PLAYER_MP))) bars &= (uint8_t)~BAR_MP, ++seen.drawn;
}

/* A kill of `entity`: its MP, now; its bar at the frame's end. */
void fax_mana_killed(uint8_t entity) {
    if (!enabled || cyc_mod_isolated()) return;
    uint8_t experience = q((uint16_t)(ENEMY_EXPERIENCE + entity)), category = q((uint16_t)(SPRITE_CATEGORIES + entity));
    unsigned mp = kill_mp(experience, category, fax_lesson_learned());
    uint8_t was = q(PLAYER_MP), now = with_gain(was, mp);
    if (now != was) {
        cyc_mod_poke(PLAYER_MP, now);
        bars |= BAR_MP;
    }
    ++seen.kills;
    if (category == CATEGORY_BOSS) ++seen.bosses;
    seen.gained += (unsigned)(now - was);
    seen.entity = entity;
    seen.experience = experience;
    seen.mp = (uint8_t)mp;
}

/* Player_AddExperienceFromSprite, X the slot of the enemy just killed. */
static int kill_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) return 0;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    fax_mana_killed(q((uint16_t)(CURRENT_SPRITES_ENTITIES + regs.x)));
    return 0;
}

/* A Guru's first action: HP and MP to 80, and their bars, now if they fit. */
void fax_mana_guru(void) {
    if (!enabled || cyc_mod_isolated()) return;
    ++seen.restores;
    if (q(PLAYER_HP_U) != HP_MAX) {
        cyc_mod_poke(PLAYER_HP_U, HP_MAX);
        bars |= BAR_HP;
    }
    if (q(PLAYER_MP) != MP_MAX) {
        cyc_mod_poke(PLAYER_MP, MP_MAX);
        bars |= BAR_MP;
    }
    draw_bars();
}

/* A frame's end: the bars still to draw, if it will do. */
void fax_mana_frame_end(void) {
    if (!enabled || cyc_mod_isolated() || !bars) return;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    if (waits(&regs) && screen_up()) draw_bars();
    if (bars) ++seen.deferred;
}

/* ---- the save-state record ---- */

/* Version 2 (#125): [0] 2, [1] the bars still to draw (1 HP, 2 MP). Version
 * 1 was #98's refill: [1] MP seen, [2] the MP seen, [3] frames until its
 * next MP (1-240); it loads with nothing to draw, the refill gone. */
enum { RECORD_VERSION = 2, RECORD = 2, RECORD_V1 = 4, V1_WAIT = 240 };
static int state_get(uint8_t *buf, int cap) {
    if (cap < RECORD) return -1;
    buf[0] = RECORD_VERSION;
    buf[1] = bars;
    return RECORD;
}
static int state_validate(const uint8_t *buf, int len) {
    if (len == 0) return 1;
    if (len == RECORD) return buf[0] == RECORD_VERSION && buf[1] <= BARS;
    return len == RECORD_V1 && buf[0] == 1 && buf[1] <= 1 && buf[3] >= 1 && buf[3] <= V1_WAIT;
}
/* Length 0 (a state from before the feature) or version 1: nothing to draw. */
static int state_set(const uint8_t *buf, int len) {
    if (!state_validate(buf, len)) return 0;
    bars = len == RECORD ? buf[1] : 0;
    return 1;
}

/* ---- activation ---- */

static const char *const HOOK_KILL = "faxanadu.mana-refill.kill";
static void deactivate(void) {
    enabled = false;
    bars = 0;
    seen = (Seen){ 0 };
    nes_mod_set_function_hook_enabled(HOOK_KILL, 0);
}
static void activate(void) {
    enabled = true;
    nes_mod_set_function_hook_enabled(HOOK_KILL, 1);
    nes_mod_set_local_only("MP from kills", 1); /* a peer without it would play other rules */
}

#ifdef CYC_WITH_SDL
static void mana_state(int id, const char *line) {
    (void)line;
    char fields[512];
    /* The last kill: its entity, its experience and its MP (before the cap); the per-kill rule: the scale, the
     * least, a boss's and the lesson's factor (taught: the hero has it). */
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"mp\":%u,\"hp\":%u,\"kills\":%u,\"bosses\":%u,\"gained\":%u,\"restores\":%u,"
             "\"bars\":%u,\"drawn\":%u,\"deferred\":%u,\"entity\":%u,\"experience\":%u,\"kill_mp\":%u,"
             "\"scale\":%d,\"least\":%d,\"boss_mp\":%d,\"taught\":%s,\"factor\":%d",
             enabled ? "true" : "false", q(PLAYER_MP), q(PLAYER_HP_U), seen.kills, seen.bosses, seen.gained,
             seen.restores, bars, seen.drawn, seen.deferred, seen.entity, seen.experience, seen.mp, SCALE, LEAST,
             BOSS_MP, fax_lesson_learned() ? "true" : "false", fax_lesson_learned() ? TAUGHT : 1);
    cyc_tcp_ok(id, fields);
}
void fax_mana_tcp_setup(void) {
    cyc_tcp_register("mana_state",
                     "MP from kills: MP, HP, kills, bosses, MP gained, Gurus' fills, bars to draw, the last kill, the "
                     "rule",
                     mana_state);
}
#else
void fax_mana_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_mana) {
    if (!nes_mod_register_function_entry_plugin(HOOK_KILL, ADD_EXPERIENCE, kill_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.mana-refill", activate) ||
        !nes_mod_register_savestate_hook("faxanadu.mana-refill", state_get, state_set) ||
        !nes_mod_register_savestate_validator("faxanadu.mana-refill", state_validate))
        abort();
}
