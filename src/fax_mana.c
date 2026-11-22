/* MP refill. USA 57dd23d1, chipx86 byte-exact disassembly.
 *
 * Once the hero has spent no MP for 4 seconds (WAIT, 240 frames), Player_MP
 * climbs back toward its cap of 80, 1 MP every 30 frames (STEP), so an empty
 * bar is full 40 s after the wait. Any spend of MP starts the wait again. Only
 * frames the game plays count: not while it is paused (Start, the map, the
 * quest log), in a text box, a conversation, a shop or the menu, while a
 * screen loads or scrolls, while the hero is dead, on the title or in the
 * ending.
 *
 * How, with no hook and no ROM: it watches RAM at the end of each frame.
 * - A spend is Player_MP lower than at the last frame's end: every cast
 *   (Player_ReduceMP, from Player_CastMagic, which Up+B and the Magic button
 *   both reach) and anything else that takes MP. It is seen on any frame.
 * - The game played a frame when the frame ends with Game_MainLoop waiting
 *   for the next one: in WaitForNextFrame, called from the loop's top (the
 *   JSR at $DB4D pushes $DB4F, and the loop has just set S to $FF, so S is
 *   $FD). A pause, a text box, the menu, a screen load or scroll and the
 *   death path each wait for their frames through their own JSR, and a pass
 *   still running as the frame ends (a lag frame) has not got there. On
 *   those frames the wait holds still. Game_PausedState, PlayerIsDead and the
 *   main loop's bank (14) must agree too.
 * - Such a frame counts the wait down. When it runs out, 1 MP is given
 *   through Player_AddMP ($C0D9) in a committed call, as the game's own fills
 *   give it, so the MP bar is drawn: Player_SetMP queues the bar's tiles and
 *   its partial tile's pattern in the PPU buffer, and the NMI that starts the
 *   next frame sends them. The CPU is in WaitForNextFrame's loop, which uses
 *   nothing Player_AddMP touches, and gets its registers back after the call.
 *   PPUBuffer_QueueCommandOrLength waits for room, which it cannot do in a
 *   committed call (no NMI runs), so the MP waits for a frame whose buffer
 *   has room for both of the bar's commands; on a quiet screen it is empty.
 * The wait and the MP seen at the last frame's end are this feature's
 * save-state record. MP is not in save files (continuing fills it). Nothing
 * happens inside Seamless's isolated simulation of other rooms. See
 * docs/GAME_INTERNALS.md, "MP refill". */
#include "fax_mana.h"
#include "cyc_mod.h"
#include "mod_runtime.h"
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
#define PPU_BUFFER_READ      0x001Fu   /* PPUBuffer_ReadOffset */
#define PPU_BUFFER_WRITE     0x0020u   /* PPUBuffer_WriteOffset: the buffer is $0500-$05FF, circular */
#define CURRENT_ROM_BANK     0x0100u   /* 14 in the main loop */
#define GAME_PAUSED_STATE    0x0120u   /* nonzero while paused: Start, the map, the quest log */
#define PLAYER_MP            0x039Au   /* at most 80 */
#define PLAYER_IS_DEAD       0x0438u
#define BAR_WIDTH            0x04EEu   /* UI_MPAndHPBarWidth: 10 tiles, set by UI_DrawHUDSprites */
/* Fixed bank */
#define PLAYER_ADD_MP        0xC0D9u   /* CLC, ADC Player_MP, STA Player_MP, CMP #$50: caps at 80, then
                                          JMP Player_SetMP, which draws the MP bar */
#define MAIN_LOOP_WAIT_PUSHED 0xDB4Fu  /* Game_MainLoop ($DB45): LDX #$FF, TXS, LDX #$0E, JSR MMC1_UpdateROMBank,
                                          then JSR WaitForNextFrame at $DB4D pushes the address of its last byte */
#define MAIN_LOOP_WAIT_S     0xFDu     /* S inside that call */
/* Values */
enum { MAIN_LOOP_BANK = 14, MP_MAX = 80 };
/* PPUBuffer_HasCapacity: room (ReadOffset - WriteOffset) is 0 (empty) or at
 * least $24. The bar's first command is 3 bytes and BarWidth + 1 tiles, and
 * the second (16 bytes of the partial tile) checks again after it. */
enum { PPU_ROOM = 0x24, COMMAND = 3 };

/* The schedule. Spells cost 2 (Deluge), 4, 6, 10 and 16 (Tilte) of the 80
 * (MAGIC_COSTS, $B7A9, bank 14); see the PR for why these. */
enum { WAIT = 240, STEP = 30 };

/* The state: what a save state must keep. */
typedef struct {
    uint8_t seen;     /* last_mp is the MP at the last frame's end */
    uint8_t last_mp;
    uint8_t left;     /* played frames until the next MP, 1-WAIT */
} Refill;

static bool enabled;
static Refill r = { 0, 0, WAIT };
static bool idle_now;                  /* the last frame's end: the game played it */
static unsigned spends, refills, deferred, played;   /* for TCP */

/* ---- the rules, on plain values ---- */

/* What one frame's end does: see `mp`; then, if the game played the frame
 * and MP is not full, count down, unless MP was spent in it, which starts
 * the wait again, so the first MP comes back WAIT frames after the frame of
 * the spend. True when 1 MP is due now; the caller gives it, or calls hold()
 * if it cannot this frame. */
static bool step(Refill *s, uint8_t mp, bool played_frame) {
    bool spent = s->seen && mp < s->last_mp;
    s->seen = 1;
    s->last_mp = mp;
    if (spent) { s->left = WAIT; ++spends; return false; }
    if (!played_frame || mp >= MP_MAX) return false;
    if (s->left > 1) { --s->left; return false; }
    return true;
}
static void gave(Refill *s, uint8_t mp) { s->last_mp = mp; s->left = STEP; }
static void hold(Refill *s) { s->left = 1; }   /* due, the next frame that can */

/* ---- the machine ---- */

static uint8_t q(uint16_t addr) { return cyc_mod_peek(addr); }

/* Game_MainLoop waits in WaitForNextFrame, called from its top. */
static bool main_loop_waits(const CycModRegs *regs) {
    if (regs->s != MAIN_LOOP_WAIT_S) return false;
    uint16_t pushed = (uint16_t)(q(0x01FE) | q(0x01FF) << 8);
    return pushed == MAIN_LOOP_WAIT_PUSHED;
}
static bool playing(const CycModRegs *regs) {
    return main_loop_waits(regs) && q(CURRENT_ROM_BANK) == MAIN_LOOP_BANK && !q(GAME_PAUSED_STATE) &&
           !q(PLAYER_IS_DEAD);
}
/* The PPU buffer has room for the MP bar: both of Player_SetMP's
 * PPUBuffer_WaitForCapacity calls would return at once. */
static bool bar_fits(void) {
    uint8_t room = (uint8_t)(q(PPU_BUFFER_READ) - q(PPU_BUFFER_WRITE));
    return room == 0 || room >= PPU_ROOM + COMMAND + q(BAR_WIDTH) + 1;
}
/* Player_AddMP with A = 1, committed; the CPU's registers as they were. */
static bool give(void) {
    CycModRegs regs, call;
    cyc_mod_regs(&regs);
    call = regs;
    call.a = 1;
    bool ok = cyc_mod_call_commit(PLAYER_ADD_MP, &call);
    cyc_mod_set_regs(&regs);
    return ok;
}

void fax_mana_frame_end(void) {
    if (!enabled || cyc_mod_isolated()) return;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    idle_now = playing(&regs);
    if (idle_now) ++played;
    if (!step(&r, q(PLAYER_MP), idle_now)) return;
    if (!bar_fits() || !give()) { hold(&r); ++deferred; return; }
    gave(&r, q(PLAYER_MP));
    ++refills;
}

/* ---- the save-state record ---- */

/* Version 1: [0] 1, [1] MP seen, [2] the MP seen at the last frame's end,
 * [3] played frames until the next MP (1-WAIT). */
enum { RECORD_VERSION = 1, RECORD = 4 };
static int state_get(uint8_t *buf, int cap) {
    if (cap < RECORD) return -1;
    const uint8_t rec[RECORD] = { RECORD_VERSION, r.seen, r.last_mp, r.left };
    memcpy(buf, rec, sizeof rec);
    return RECORD;
}
static int state_validate(const uint8_t *buf, int len) {
    return len == 0 || (len == RECORD && buf[0] == RECORD_VERSION && buf[1] <= 1 && buf[3] >= 1 && buf[3] <= WAIT);
}
/* Length 0 (a state from before the feature): the MP is taken as it is, and
 * the whole wait runs first. */
static int state_set(const uint8_t *buf, int len) {
    if (!state_validate(buf, len)) return 0;
    r = len ? (Refill){ buf[1], buf[2], buf[3] } : (Refill){ 0, 0, WAIT };
    idle_now = false;
    return 1;
}

/* ---- activation ---- */

static void deactivate(void) {
    enabled = idle_now = false;
    r = (Refill){ 0, 0, WAIT };
    spends = refills = deferred = played = 0;
}
static void activate(void) { enabled = true; }

#ifdef CYC_WITH_SDL
static void mana_state(int id, const char *line) {
    (void)line;
    char fields[256];
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"mp\":%u,\"left\":%u,\"playing\":%s,\"played\":%u,\"spends\":%u,\"refills\":%u,"
             "\"deferred\":%u,\"wait\":%d,\"step\":%d",
             enabled ? "true" : "false", q(PLAYER_MP), r.left, idle_now ? "true" : "false", played, spends, refills,
             deferred, WAIT, STEP);
    cyc_tcp_ok(id, fields);
}
void fax_mana_tcp_setup(void) {
    cyc_tcp_register("mana_state",
                     "MP refill: MP, played frames until the next MP, whether the last frame was played, counts",
                     mana_state);
}
#else
void fax_mana_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_mana) {
    if (!nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.mana-refill", activate) ||
        !nes_mod_register_savestate_hook("faxanadu.mana-refill", state_get, state_set) ||
        !nes_mod_register_savestate_validator("faxanadu.mana-refill", state_validate)) abort();
}
