/* Forgiving jumps: coyote time and jump buffering. USA 57dd23d1, chipx86
 * byte-exact disassembly.
 *
 * The hero jumps only from Player_UpdateClimbOrJump ($E444), and only when
 * Player_HandleVertLadderOrJump ($E399) found a block under him (or a ladder
 * in front of him) and Joy1_ChangedButtonMask shows A newly pressed that very
 * update. So A one frame after walking off a ledge does nothing, and A on the
 * frame he lands, or before, is lost. Both are presented to the game's own
 * jump code, at its own branches, for that one update:
 *
 * - Coyote time. A pressed within COYOTE_FRAMES updates of walking off a block
 *   he could have jumped from makes the ground check at $E3B2 find ground. The
 *   game then runs its jump from where he is, after this update's 8-pixel fall
 *   step (the hook makes that step itself). A fall is 8 pixels a frame from the
 *   first frame, so a late jump starts lower than the ledge: its arc is the
 *   best original jump's (A on the last frame on the ledge) shifted later and
 *   lower, and never reaches above it or beyond it.
 * - Jump buffering. A pressed in the air (jumping or falling) counts for
 *   BUFFER_FRAMES updates. The first update on the ground within them, the
 *   jump check at $E44B sees A pressed, and the game jumps exactly as if it had
 *   been pressed then.
 *
 * Neither applies with the Wing Boots running (A flies), in knockback, on a
 * ladder, or across a screen change. See docs/GAME_INTERNALS.md. */
#include "fax_jumps.h"
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

#define COYOTE_FRAMES 5u           /* A on the 1st-5th update in the air after walking off still jumps */
#define BUFFER_FRAMES 6u           /* A in the air jumps on landing if the landing comes within 6 updates */

/* Bank 15 */
#define VERT_MOVEMENT     0xE2C8u  /* Player_CheckHandleVertMovement: LDA Player_StatusFlag, BPL. Once per update */
#define GROUND_CHECK      0xE3B2u  /* Player_HandleVertLadderOrJump's BEQ @_checkAdjacent, after LDA #$03 (the
                                      game's LDX slip) and JSR Player_CheckIfPassable: Z clear = a block below */
#define JUMP_CHECK        0xE44Bu  /* Player_UpdateClimbOrJump's BPL Player_ClearJumpingAndHoldingToClimb, after
                                      LDX Joy1_ChangedButtonMask: N set = A newly pressed */
#define CHECK_IF_PASSABLE 0xE6C8u  /* Player_CheckIfPassable: X = 0 left, 1 right, 2 up, 3 down; A = 0 passable */
/* RAM */
#define JOY1_CHANGED      0x0019u  /* Joy1_ChangedButtonMask */
#define AREA              0x0024u  /* Area_CurrentArea */
#define SCREEN            0x0063u  /* Area_CurrentScreen */
#define PLAYER_POS_X      0x009Eu  /* Player_PosX_Block */
#define PLAYER_POS_Y      0x00A1u
#define PLAYER_FLAGS      0x00A4u
#define PLAYER_STATUS     0x00A5u  /* Player_StatusFlag */
#define A_BUTTON          0x80u
#define JUMPING           0x01u    /* Player_Flags */
#define FALLING_OFF       0x04u
#define CAN_CLIMB         0x08u
#define CLIMBING          0x10u
#define KNOCKBACK         0x02u    /* Player_StatusFlag */
#define WING_BOOTS        0x80u
#define UP                2u
#define FALL_STEP         8u       /* @_climbDown: a fall without the Wing Boots, every update */
#define LOWEST_Y          0xC0u    /* past it a fall step scrolls to the screen below */
#define P_Z               0x02u
#define P_N               0x80u

static bool enabled;
static uint32_t tick;              /* player updates seen */
static bool reached, reached_last; /* this update and the last reached the ground check */
static bool grounded;              /* this update found a block under him */
static bool armed;                 /* coyote time: he left a block he could jump from, by falling */
static uint32_t ground_tick;       /* the last update he stood on that block */
static uint8_t ledge_x, ledge_y;   /* where he stood */
static bool buffered;              /* A pressed in the air, not yet used */
static uint32_t press_tick;
static uint8_t area = 0xFF, screen = 0xFF;
static unsigned coyote_jumps, buffered_jumps;

static uint8_t peek(uint16_t addr) { return cyc_mod_peek(addr); }

/* Whether the game would have let him jump from the ledge: the block above his
 * head there passable, by the game's own check in a scratch copy of the
 * machine. A ceiling that stopped a jump on the ledge stops a coyote jump. */
static bool could_jump_from(uint8_t x, uint8_t y) {
    CycModRegs regs;
    cyc_mod_regs(&regs);
    if (!cyc_mod_isolate_begin()) return false;
    cyc_mod_poke(PLAYER_POS_X, x);
    cyc_mod_poke(PLAYER_POS_Y, y);
    regs.x = UP;
    bool ok = cyc_mod_call(CHECK_IF_PASSABLE, &regs) && regs.a == 0;
    cyc_mod_isolate_end();
    return ok;
}

/* Player_CheckHandleVertMovement: a new update. Ages what came before, and
 * keeps an A press made in the air. */
static int tick_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) return 0;
    ++tick;
    reached_last = reached;
    reached = grounded = false;
    uint8_t a = peek(AREA), s = peek(SCREEN), status = peek(PLAYER_STATUS), flags = peek(PLAYER_FLAGS);
    if (a != area || s != screen || (status & (WING_BOOTS | KNOCKBACK)) || (flags & CLIMBING)) {
        armed = buffered = false;
        area = a;
        screen = s;
    }
    if (!reached_last) armed = false;   /* he jumped or climbed in between */
    if ((peek(JOY1_CHANGED) & A_BUTTON) && (flags & (JUMPING | FALLING_OFF)) && !(status & (WING_BOOTS | KNOCKBACK)) &&
        !(flags & CLIMBING)) {
        buffered = true;
        press_tick = tick;
    }
    return 0;
}

/* The ground check of an update without a jump. On a block: remember it. In
 * the air within coyote time, with A newly pressed: take this update's fall
 * step, then say there is ground, so the game's jump code runs. */
static int ground_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) return 0;
    reached = true;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    uint8_t flags = peek(PLAYER_FLAGS), status = peek(PLAYER_STATUS), y = peek(PLAYER_POS_Y);
    if (!(regs.p & P_Z)) {
        grounded = true;
        /* Player_IsClimbing as the jump check sees it, with PLAYER_FALLING_OFF cleared */
        armed = !(status & (WING_BOOTS | KNOCKBACK)) && !((flags & CAN_CLIMB) && (flags & CLIMBING));
        ground_tick = tick;
        ledge_x = peek(PLAYER_POS_X);
        ledge_y = y;
        return 0;
    }
    if (!armed) return 0;
    if (tick - ground_tick > COYOTE_FRAMES) { armed = false; return 0; }
    /* In front of a ladder the game catches him itself; at the screen's foot the
     * fall step would scroll down. */
    if (!(peek(JOY1_CHANGED) & A_BUTTON) || (flags & CAN_CLIMB) || y > LOWEST_Y - FALL_STEP) return 0;
    armed = false;
    if (!could_jump_from(ledge_x, ledge_y)) return 0;
    buffered = false;                    /* this press is the jump */
    cyc_mod_poke(PLAYER_POS_Y, (uint8_t)(y + FALL_STEP));
    regs.a = 1;
    regs.p = (uint8_t)(regs.p & ~P_Z);
    cyc_mod_set_regs(&regs);
    ++coyote_jumps;
    return 0;
}

/* The jump check, on the first update back on the ground: a press kept from
 * the air becomes this update's press. X holds the buttons the game loaded. */
static int jump_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated() || !grounded) return 0;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    if (regs.x & A_BUTTON) { buffered = false; return 0; }   /* a press of his own */
    if (!buffered) return 0;
    buffered = false;
    if (tick - press_tick > BUFFER_FRAMES) return 0;
    regs.x |= A_BUTTON;
    regs.p = (uint8_t)((regs.p | P_N) & ~P_Z);
    cyc_mod_set_regs(&regs);
    ++buffered_jumps;
    return 0;
}

/* The record: what the next update needs, with ages instead of ticks. */
enum { STATE_LEN = 7 };
static uint8_t age(uint32_t since) { return (uint8_t)(tick - since > 0xFF ? 0xFF : tick - since); }
static int state_get(uint8_t *buf, int cap) {
    if (cap < STATE_LEN) return -1;
    buf[0] = (uint8_t)(reached | armed << 1 | buffered << 2);
    buf[1] = age(ground_tick);
    buf[2] = age(press_tick);
    buf[3] = ledge_x;
    buf[4] = ledge_y;
    buf[5] = area;
    buf[6] = screen;
    return STATE_LEN;
}
static int state_validate(const uint8_t *buf, int len) { return len == 0 || (len == STATE_LEN && buf[0] < 8); }
static void clear(void) {
    tick = 0x100;
    reached = reached_last = grounded = armed = buffered = false;
    ground_tick = press_tick = 0;
    ledge_x = ledge_y = 0;
    area = screen = 0xFF;
}
static int state_set(const uint8_t *buf, int len) {
    if (!state_validate(buf, len)) return 0;
    clear();
    if (len) {
        reached = buf[0] & 1;
        armed = buf[0] >> 1 & 1;
        buffered = buf[0] >> 2 & 1;
        ground_tick = tick - buf[1];
        press_tick = tick - buf[2];
        ledge_x = buf[3];
        ledge_y = buf[4];
        area = buf[5];
        screen = buf[6];
    }
    return 1;
}

static const char *const hook_ids[] = {
    "faxanadu.forgiving-jumps.tick", "faxanadu.forgiving-jumps.ground", "faxanadu.forgiving-jumps.jump",
};
static void hooks(int on) {
    for (unsigned i = 0; i < sizeof hook_ids / sizeof hook_ids[0]; i++) nes_mod_set_function_hook_enabled(hook_ids[i], on);
}
static void deactivate(void) {
    enabled = false;
    clear();
    coyote_jumps = buffered_jumps = 0;
    hooks(0);
}
static void activate(void) {
    enabled = true;
    clear();
    hooks(1);
    nes_mod_set_local_only("Forgiving jumps", 1);
}

#ifdef CYC_WITH_SDL
static void jumps_state(int id, const char *line) {
    (void)line;
    char fields[192];
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"coyote\":%u,\"buffered\":%u,\"coyote_frames\":%u,\"buffer_frames\":%u,\"armed\":%s,"
             "\"pending\":%s",
             enabled ? "true" : "false", coyote_jumps, buffered_jumps, COYOTE_FRAMES, BUFFER_FRAMES,
             armed ? "true" : "false", buffered ? "true" : "false");
    cyc_tcp_ok(id, fields);
}
void fax_jumps_tcp_setup(void) {
    cyc_tcp_register("forgiving_jumps_state", "Forgiving jumps: coyote and buffered jumps it gave", jumps_state);
}
#else
void fax_jumps_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_jumps) {
    if (!nes_mod_register_function_entry_plugin(hook_ids[0], VERT_MOVEMENT, tick_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[1], GROUND_CHECK, ground_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[2], JUMP_CHECK, jump_hook) ||
        !nes_mod_register_savestate_hook("faxanadu.forgiving-jumps", state_get, state_set) ||
        !nes_mod_register_savestate_validator("faxanadu.forgiving-jumps", state_validate) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.forgiving-jumps", activate)) abort();
}
