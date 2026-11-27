/* Run (#198). USA 57dd23d1, chipx86 byte-exact disassembly.
 *
 * Hold a bindable button (Left Shift, or the left trigger on a controller;
 * RunKey and RunButton in config.ini [Game]) and the hero walks 1.5 times as
 * far each update on the ground: 2.25 pixels a frame at his top speed, where
 * the walk's is 1.5.
 *
 * How the game walks (bank 15): Player_CheckHandleJump ($E174) moves him with
 * Player_TryMoveRight ($E1CF) or Player_TryMoveLeft ($E220). Each calls
 * Player_UpdateAcceleration ($E291), which raises Player_MoveAcceleration
 * (8.8) by 2, 4, 6 or 8 256ths an update, by title, from $C0 as he sets off to
 * $180 at most, then adds it to Player_PosX (or takes it away), and only then
 * asks Player_CheckIfPassable ($E6C8) about the block ahead and, at the
 * screen's side, scrolls.
 *
 * At the LDX before that check, in either routine, the hook moves him on by
 * RUN_SCALE's share of the acceleration more, so the check and the screen's
 * side see the whole step, as they would a longer one of the game's. Going
 * left, a step past X 0 clears the carry the routine pushed (its PHP), so the
 * routine goes on to the screen's side as it does when its own step goes past.
 * The acceleration itself stays the game's: he speeds up as he walks, the
 * same whether he runs or not, and a jump takes it into the air.
 *
 * Only on foot, in play: the move must be the one GameLoop_UpdatePlayer makes
 * for Game_MainLoop (not the ending's walk to the King, nor knockback's push,
 * whose stacks differ), and he must be on the ground: not jumping or falling
 * (a jump keeps the walk's speed in the air, so running reaches no gap or
 * ledge sooner than walking does), not climbing, not lining up with a ladder
 * (Up or Down in front of one), not hanging on one in the air (the game's own
 * down check, in a scratch copy of the machine, as Holding on to ladders
 * asks), not flying with the Wing Boots, and not knocked back. Conversations,
 * shops, menus and the pause never reach the walk. With the remaster off the
 * hooks are off. See docs/GAME_INTERNALS.md, "The hero's movement". */
#include "fax_run.h"
#include "cyc_mod.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#ifdef CYC_WITH_SDL
#include "cyc_input.h"
#include "cyc_tcp.h"
#include <SDL.h>
#endif
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* RAM */
#define JOY1_BUTTON_MASK     0x0016u /* held: Up $08, Down $04 */
#define PLAYER_POS_X_FRAC    0x009Du /* Player_PosX_Frac */
#define PLAYER_POS_X         0x009Eu /* Player_PosX_Block */
#define PLAYER_FLAGS         0x00A4u
#define PLAYER_STATUS        0x00A5u /* Player_StatusFlag */
#define PLAYER_MOVE_ACCEL    0x00A9u /* Player_MoveAcceleration, 8.8 with _U at $AA */
#define PLAYER_MOVE_ACCEL_U  0x00AAu
#define JUMPING              0x01u /* Player_Flags */
#define FALLING_OFF          0x04u
#define CAN_CLIMB            0x08u
#define CLIMBING             0x10u
#define KNOCKBACK            0x02u /* Player_StatusFlag */
#define WING_BOOTS           0x80u
#define UP_OR_DOWN           0x0Cu
#define DOWN                 3u /* Player_CheckIfPassable's X */
#define P_C                  0x01u
/* Bank 15 */
/* Player_TryMoveRight: ..., ADC Player_MoveAcceleration_U, STA Player_PosX_Block,
 * then LDX #$01, JSR Player_CheckIfPassable, BEQ @_checkAtRightEdge */
#define MOVE_RIGHT_CHECK     0xE1EEu
/* Player_TryMoveLeft: ..., SBC Player_MoveAcceleration_U, PHP, BCS @_saveFlags, LDA #$00,
 * @_saveFlags: STA Player_PosX_Block, then LDX #$00, JSR Player_CheckIfPassable, BEQ @_checkAtLeftEdge */
#define MOVE_LEFT_CHECK      0xE246u
#define CHECK_IF_PASSABLE    0xE6C8u /* X = 0 left, 1 right, 2 up, 3 down; A = 0 passable */
/* What the JSRs on the way to the walk pushed: GameLoop_UpdatePlayer's JSR
 * Player_CheckHandleJump ($E0D0), which reaches both routines by a branch or
 * a JMP, and Game_MainLoop's JSR GameLoop_UpdatePlayer ($DB53).
 * EndGame_MainLoop's ($DBFF) pushes $DC01, Player_HandleKnockback's JSR
 * Player_KnockbackHoriz ($E15C) $E15E. */
#define UPDATE_PLAYER_PUSHED 0xE0D2u
#define MAIN_LOOP_PUSHED     0xDB55u

/* A run's step, in 256ths of the walk's: 384, 1.5 times. */
enum { RUN_SCALE = 384 };

static bool enabled;
static unsigned runs, walks; /* updates of play's walk with Run held: at its speed, and kept the game's */
static unsigned last_step;   /* the last run update's step, 8.8 */
static char label[80] = "Run: LEFT SHIFT / LT";

static uint8_t peek(uint16_t a) { return cyc_mod_peek(a); }

static bool held(void);

/* What the JSR whose return sits at S + 1 pushed. */
static uint16_t pushed(uint8_t s) {
    return (uint16_t)(peek((uint16_t)(0x100u | (uint8_t)(s + 1))) | peek((uint16_t)(0x100u | (uint8_t)(s + 2))) << 8);
}

/* Whether the block under his feet is passable, by the game's own check in a
 * scratch copy of the machine. */
static bool in_the_air(void) {
    CycModRegs regs;
    cyc_mod_regs(&regs);
    if (!cyc_mod_isolate_begin()) return false;
    regs.x = DOWN;
    bool air = cyc_mod_call(CHECK_IF_PASSABLE, &regs) && regs.a == 0;
    cyc_mod_isolate_end();
    return air;
}

/* On the ground, walking, as this update left him. */
static bool on_foot(void) {
    uint8_t flags = peek(PLAYER_FLAGS);
    if (peek(PLAYER_STATUS) & (KNOCKBACK | WING_BOOTS)) return false;
    if (flags & (JUMPING | FALLING_OFF | CLIMBING)) return false;
    if (!(flags & CAN_CLIMB)) return true;
    /* In front of a ladder: lining up to climb it, or hanging on it, is the ladder's. */
    return !(peek(JOY1_BUTTON_MASK) & UP_OR_DOWN) && !in_the_air();
}

/* Both routines' LDX before Player_CheckIfPassable: the game has made its
 * step; a run adds the rest. */
static int move_hook(uint16_t addr) {
    if (!enabled || cyc_mod_isolated() || !held()) return 0;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    bool left = addr == MOVE_LEFT_CHECK;
    uint8_t s = (uint8_t)(regs.s + (left ? 1 : 0)); /* past the P that Player_TryMoveLeft pushed */
    if (pushed(s) != UPDATE_PLAYER_PUSHED || pushed((uint8_t)(s + 2)) != MAIN_LOOP_PUSHED) return 0;
    if (!on_foot()) {
        ++walks;
        return 0;
    }
    unsigned accel = peek(PLAYER_MOVE_ACCEL) | (unsigned)peek(PLAYER_MOVE_ACCEL_U) << 8;
    unsigned extra = accel * (RUN_SCALE - 256u) / 256u;
    unsigned pos = (unsigned)peek(PLAYER_POS_X) << 8 | peek(PLAYER_POS_X_FRAC);
    if (!left) {
        pos += extra; /* past $FF it wraps, as the routine's own ADC does */
    } else {
        uint16_t pushed_p = (uint16_t)(0x100u | (uint8_t)(regs.s + 1));
        uint8_t p = peek(pushed_p);
        if (!(p & P_C)) {
            pos = (pos - extra) & 0xFFu; /* already past X 0: the routine keeps X at 0 */
        } else if (pos < extra) {
            pos = (pos - extra) & 0xFFu; /* now past it: X 0, and on to the screen's side */
            cyc_mod_poke(pushed_p, (uint8_t)(p & ~P_C));
        } else {
            pos -= extra;
        }
    }
    cyc_mod_poke(PLAYER_POS_X, (uint8_t)(pos >> 8));
    cyc_mod_poke(PLAYER_POS_X_FRAC, (uint8_t)pos);
    last_step = accel + extra;
    ++runs;
    return 0;
}

static const char *const hook_ids[] = { "faxanadu.run.right", "faxanadu.run.left" };
static void hooks(int on) {
    for (unsigned i = 0; i < sizeof hook_ids / sizeof hook_ids[0]; ++i)
        nes_mod_set_function_hook_enabled(hook_ids[i], on);
}
static void deactivate(void) {
    enabled = false;
    runs = walks = last_step = 0;
    hooks(0);
}
static void activate(void) {
    enabled = true;
    hooks(1);
    nes_mod_set_local_only("Run", 1); /* host input the other side would not see */
}

/* ---- the bindings (config.ini [Game]) ---- */

const char *fax_run_controls_label(void) { return label; }

#ifdef CYC_WITH_SDL
/* The defaults as SDL numbers them: SDL_SCANCODE_LSHIFT, and the left trigger
 * (SDL_CONTROLLER_AXIS_TRIGGERLEFT, 4) pressed, in the host's binding encoding
 * (cyc_input.h CYC_PAD_AXIS: 100 + axis * 2 + positive). The left trigger
 * was the quest log's before layout 3 (fax_controls.c moves it). */
enum { DEFAULT_KEY = 225, DEFAULT_PAD = 100 + 4 * 2 + 1, AXIS_PRESS = 16384 };

static int key = DEFAULT_KEY, pad = DEFAULT_PAD; /* 0: unbound */
static bool key_held, pad_held, axis_held;

static bool held(void) { return key_held || pad_held || axis_held; }

static void describe(void) { /* as the host shows its own shortcuts: "LEFT SHIFT / LT" */
    char text[64] = "unbound";
    CycBinding both = { key, pad };
    if (key || pad) cyc_binding_hint(&both, text, sizeof text);
    snprintf(label, sizeof label, "Run: %s", text);
}
static void rebound(void) {
    key_held = pad_held = axis_held = false;
    describe();
}
const CycHostBinding *fax_run_binding(void) {
    static const CycHostBinding b = { "Run (hold)", &key, &pad, DEFAULT_KEY, DEFAULT_PAD, rebound };
    return &b;
}

bool fax_run_load_setting(const char *name, const char *value) {
    int v;
    if (!strcmp(name, "RunKey")) {
        if (cyc_key_parse(value, &v)) key = v;
        else fprintf(stderr, "[run] RunKey: SDL has no key named \"%s\"; keeping the default\n", value);
    } else if (!strcmp(name, "RunButton")) {
        if (cyc_pad_parse(value, &v) && !CYC_PAD_IS_COMBO(v)) pad = v;
        else
            fprintf(stderr, "[run] RunButton: \"%s\" is not one controller button or axis; keeping the default\n",
                    value);
    } else {
        return false;
    }
    describe();
    return true;
}
void fax_run_save_settings(FILE *f) {
    char k[32], p[48];
    cyc_key_text(key, k, sizeof k);
    cyc_pad_text(pad, p, sizeof p);
    fprintf(f, "RunKey = %s\nRunButton = %s\n", k, p);
}

void fax_run_event(const void *ev) {
    const SDL_Event *e = ev;
    if (e->type == SDL_KEYDOWN || e->type == SDL_KEYUP) {
        if (key && (int)e->key.keysym.scancode == key) key_held = e->type == SDL_KEYDOWN;
    } else if (e->type == SDL_CONTROLLERBUTTONDOWN || e->type == SDL_CONTROLLERBUTTONUP) {
        if (CYC_PAD_IS_BUTTON(pad) && e->cbutton.button == pad - 1) pad_held = e->type == SDL_CONTROLLERBUTTONDOWN;
    } else if (e->type == SDL_CONTROLLERAXISMOTION) {
        if (CYC_PAD_IS_AXIS(pad) && e->caxis.axis == (pad - 100) / 2) {
            int value = (pad - 100) & 1 ? e->caxis.value : -e->caxis.value;
            axis_held = value >= AXIS_PRESS;
        }
    }
}

static void run_state(int id, const char *line) {
    (void)line;
    char fields[256], text[96];
    cyc_tcp_quote(label + 5, text, sizeof text);
    /* runs: updates of play's walk at Run's speed; walks: those with Run held that kept the game's (in the air,
     * on a ladder, in flight or knockback); step: the last run update's step, in 256ths of a pixel */
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"binding\":%s,\"held\":%s,\"scale\":%d,\"runs\":%u,\"walks\":%u,\"step\":%u",
             enabled ? "true" : "false", text, held() ? "true" : "false", RUN_SCALE, runs, walks, last_step);
    cyc_tcp_ok(id, fields);
}
void fax_run_tcp_setup(void) {
    cyc_tcp_register("run_state", "Run: bindings, held, updates run and walked with it held, the last step", run_state);
}
#else
static bool held(void) { return false; }
const CycHostBinding *fax_run_binding(void) { return NULL; }
bool fax_run_load_setting(const char *name, const char *value) {
    (void)name;
    (void)value;
    return false;
}
void fax_run_save_settings(FILE *f) { (void)f; }
void fax_run_event(const void *ev) { (void)ev; }
void fax_run_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_run) {
    if (!nes_mod_register_function_entry_plugin(hook_ids[0], MOVE_RIGHT_CHECK, move_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[1], MOVE_LEFT_CHECK, move_hook) ||
        !nes_mod_register_reset_callback(deactivate) || !nes_mod_register_activation_plugin("faxanadu.run", activate))
        abort();
}
