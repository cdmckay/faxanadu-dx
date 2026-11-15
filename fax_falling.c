/* The falling pose. USA 57dd23d1, chipx86 byte-exact disassembly.
 * Walking off a ledge, the hero fell in the standing or walking pose, as if
 * stepping on air. The pose comes from Player_Flags in two copies of the same
 * rule: Player_GetBodySpriteFrameOffset ($ECAC, bank 15) for the body, and
 * Maybe_Player_CalcAnimFrame ($B927, bank 14) for the weapon and shield, which
 * have their own tiles and offsets per pose. Only PLAYER_JUMPING picks frame 3,
 * the jump pose.
 *
 * A fall that is not a jump sets PLAYER_FALLING_OFF instead: every frame the
 * hero is in the air without jumping, Player_HandleVertLadderOrJump sets it at
 * @_setFalling ($E3D1) and moves him 8 pixels down. That covers walking off a
 * ledge, stepping off a ladder, being knocked off one, and the rest of a jump
 * that outlasts its curve. The flag stays set on the frame he lands
 * (@_checkCapFall only rounds his Y onto the block) and is cleared the next
 * frame, so the hooks there tell landing apart from falling.
 *
 * Both pose routines are hooked at their entry: falling, not jumping, not
 * attacking, not climbing and not just landed, they return frame 3 as the
 * jumping branch does; everything else runs the game's own choice. The shield
 * sits where it does in frames 0-3 alike, and the weapon only hits while
 * attacking, so the change is the picture alone. See docs/GAME_INTERNALS.md. */
#include "fax_falling.h"
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

/* Bank 15 */
#define GET_BODY_FRAME  0xECACu   /* Player_GetBodySpriteFrameOffset: LDA Player_Flags, LSR A, BCC */
#define SET_FALLING     0xE3D1u   /* Player_HandleVertLadderOrJump's @_setFalling: LDA Player_Flags,
                                     ORA #PLAYER_FALLING_OFF. Every frame in the air without jumping */
#define CAP_FALL        0xE423u   /* @_checkCapFall's BEQ @_return, after JSR Player_CheckIfPassable
                                     (down) following the fall step. Z clear: he landed */
/* Bank 14 */
#define CALC_ANIM_FRAME 0xB927u   /* Maybe_Player_CalcAnimFrame, the same rule for the weapon and shield */
/* RAM */
#define PLAYER_POS_X    0x009Eu   /* Player_PosX_Block */
#define PLAYER_FLAGS    0x00A4u
#define JUMPING         0x01u     /* PLAYER_JUMPING */
#define FALLING_OFF     0x04u     /* PLAYER_FALLING_OFF */
#define CAN_CLIMB       0x08u
#define CLIMBING        0x10u
#define ATTACKING       0x80u
#define JUMP_POSE       3u
#define P_Z             0x02u
#define P_N             0x80u

static bool enabled;
static bool landed;               /* the last fall step put him on a block */
static unsigned body, gear;       /* pose calls answered with the jump pose */

/* Player_IsClimbing, as the game has it. */
static bool climbing(uint8_t flags, uint8_t x) {
    return (flags & CAN_CLIMB) && ((x & 0x0F) == 0 || !(flags & FALLING_OFF)) && (flags & CLIMBING);
}

/* In the air without a jump: falling, and none of the poses the game puts
 * first (jumping, climbing, attacking). The frame he lands still has the flag,
 * and gets the game's pose. */
static bool falling(uint8_t flags, uint8_t x, bool on_block) {
    return (flags & FALLING_OFF) && !(flags & (JUMPING | ATTACKING)) && !climbing(flags, x) && !on_block;
}

/* Both pose routines' entry. Answering returns frame 3 the way their jumping
 * branch does (LDA #$03, RTS): the callers use only A. */
static int pose_hook(uint16_t addr) {
    if (!falling(cyc_mod_peek(PLAYER_FLAGS), cyc_mod_peek(PLAYER_POS_X), landed)) return 0;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    regs.a = JUMP_POSE;
    regs.p = (uint8_t)(regs.p & ~(P_N | P_Z));
    cyc_mod_set_regs(&regs);
    if (!cyc_mod_isolated()) ++*(addr == GET_BODY_FRAME ? &body : &gear);
    return 1;
}

/* A fall step begins: he is in the air until @_checkCapFall finds a block. */
static int set_falling_hook(uint16_t addr) {
    (void)addr;
    if (!cyc_mod_isolated()) landed = false;
    return 0;
}

/* After the fall step: Z clear means the block below is solid and the game
 * rounds his Y onto it. PLAYER_FALLING_OFF is cleared next frame. */
static int cap_fall_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) return 0;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    landed = !(regs.p & P_Z);
    return 0;
}

static int state_get(uint8_t *buf, int cap) {
    if (cap < 1) return -1;
    buf[0] = landed;
    return 1;
}
static int state_validate(const uint8_t *buf, int len) { return len == 0 || (len == 1 && buf[0] <= 1); }
static int state_set(const uint8_t *buf, int len) {
    if (!state_validate(buf, len)) return 0;
    landed = len && buf[0];
    return 1;
}

static const char *const hook_ids[] = {
    "faxanadu.falling-pose.body", "faxanadu.falling-pose.gear",
    "faxanadu.falling-pose.set-falling", "faxanadu.falling-pose.cap-fall",
};
static void hooks(int on) {
    for (unsigned i = 0; i < sizeof hook_ids / sizeof hook_ids[0]; i++) nes_mod_set_function_hook_enabled(hook_ids[i], on);
}
static void deactivate(void) {
    enabled = landed = false;
    body = gear = 0;
    hooks(0);
}
static void activate(void) {
    enabled = true;
    hooks(1);
    nes_mod_set_local_only("Falling pose", 1);
}

#ifdef CYC_WITH_SDL
static void falling_state(int id, const char *line) {
    (void)line;
    char fields[128];
    snprintf(fields, sizeof fields, "\"enabled\":%s,\"body\":%u,\"gear\":%u,\"landed\":%s", enabled ? "true" : "false",
             body, gear, landed ? "true" : "false");
    cyc_tcp_ok(id, fields);
}
void fax_falling_tcp_setup(void) {
    cyc_tcp_register("falling_pose_state", "Falling pose: body and gear poses it set to the jump pose", falling_state);
}
#else
void fax_falling_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_falling) {
    if (!nes_mod_register_function_entry_plugin(hook_ids[0], GET_BODY_FRAME, pose_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[1], CALC_ANIM_FRAME, pose_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[2], SET_FALLING, set_falling_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[3], CAP_FALL, cap_fall_hook) ||
        !nes_mod_register_savestate_hook("faxanadu.falling-pose", state_get, state_set) ||
        !nes_mod_register_savestate_validator("faxanadu.falling-pose", state_validate) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.falling-pose", activate)) abort();
}
