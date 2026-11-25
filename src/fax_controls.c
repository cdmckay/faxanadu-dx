/* The controller layout (#147). USA 57dd23d1, chipx86 byte-exact disassembly.
 *
 * With the remaster, a modern player's pad: Start opens the Select menu (its
 * QUESTS, MAP and the gear) and closes it again, Back is the Map button's
 * (fax_map.c's binding), and every action has its own button: Magic on Y,
 * Use item on B (fax_use_item.c), Block on the right trigger, the quest log on
 * the left. Nothing pauses but the screens that do anyway: the Select menu,
 * the map, the quest log and the runtime menu. With the remaster off its
 * hooks are off, and Start and Back are the NES's Start and Select again.
 * The keyboard keeps its keys: Return (the NES Start) opens the menu as Start
 * does, and Backslash (the NES Select) still opens it too.
 *
 * - A controller button bound to one of the remaster's actions (Magic, Use
 *   item, Block, Map, Quest log) is that action's while it is held: it does
 *   not also press the NES button the host binds to the same button, as a
 *   host shortcut's buttons are the shortcut's (cyc_input.h). So Back, the
 *   NES Select's and the map's, opens the map and not the menu. The game's
 *   input callback takes those bits out of the pad byte before the NES reads
 *   it, from the controllers as SDL reads them that frame.
 *
 * How, on the unchanged ROM (fixed bank and bank 12):
 * - GameLoop_CheckShowPlayerMenu ($E016), which opens the menu on a new
 *   Select, comes in Game_MainLoop's pass just before GameLoop_CheckPauseGame,
 *   which pauses on a new Start, and nothing between them reads the pad. At
 *   its entry this feature rewrites Joy1_ChangedButtonMask: a new Start
 *   becomes a new Select, so the game opens its own menu (Seamless's freeze
 *   at the same site, after this one, sees the Select it captures for), and
 *   the Start is gone before the pause check. The changed mask is what the
 *   NMI's next read rewrites whole, and the held mask is left alone, so the
 *   next frame's pad is the real one.
 * - GameLoop_CheckPauseGame's AND #$10 ($E02D, after LDA
 *   Joy1_ChangedButtonMask): A loses Start, so nothing pauses on it. A Start
 *   that closed the menu comes back to it new in the same pass (TextBox_Close
 *   need not wait a frame), past the menu check. The map's and the quest
 *   log's sites at the BEQ after it still open their screens through the
 *   pause.
 * - PlayerMenu_Show's @_inputLoop ($8AE8, bank 12: the BMI after LDA
 *   Joy1_ChangedButtonMask): A, then B by ASL. A new Start there gets B's bit
 *   in A, so Start closes the menu as B does (TextBox_Close, back to play).
 *   The lists and the PLAYER page keep the original's B.
 * - config.ini: the layout's defaults moved Block from B to the right
 *   trigger, the map from the left trigger to Back and the quest log from the
 *   right stick's press to the left trigger. [Game] ControlsLayout says which
 *   layout a file was written with; one without it is layout 1's, and each of
 *   those bindings still on layout 1's default moves, once (the engine moves
 *   its shortcuts the same way, cyc_settings.h). One the player changed stays.
 * See docs/GAME_INTERNALS.md, "Input and casting". */
#include "fax_controls.h"
#include "fax_block.h"
#include "fax_magic.h"
#include "fax_map.h"
#include "fax_quests.h"
#include "fax_use_item.h"
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

/* Zero page */
#define JOY1_CHANGED_BUTTON_MASK 0x0019u   /* newly pressed: A $80, B $40, Select $20, Start $10 */
#define BUTTON_A      0x80u
#define BUTTON_B      0x40u
#define BUTTON_SELECT 0x20u
#define BUTTON_START  0x10u
#define P_Z 0x02u
#define P_N 0x80u
/* Fixed bank: GameLoop_CheckShowPlayerMenu: LDA Area_CurrentArea, BNE, LDA Area_CurrentScreen, BEQ return
 * (no menu outside Eolis's gate), LDA Joy1_ChangedButtonMask, AND #$20, BEQ return, far-call PlayerMenu_Show.
 * GameLoop_CheckPauseGame ($E02B): LDA Joy1_ChangedButtonMask, $E02D AND #$10, BEQ return, then the pause. */
#define CHECK_SHOW_PLAYER_MENU 0xE016u
#define CHECK_PAUSE_START      0xE02Du
/* Bank 12: PlayerMenu_Show's @_inputLoop: ..., JSR Menu_UpdateAndDraw, LDA Joy1_ChangedButtonMask, $8AE8 BMI
 * @_loadMenuIDs (A), ASL A, BPL @_inputLoop, JMP TextBox_Close (B). */
#define MENU_INPUT_BRANCH 0x8AE8u

enum { LAYOUT = 2 };   /* ControlsLayout: 1 before #147 (none written) */

static bool enabled;
static unsigned starts, closes, unpaused;  /* Starts shown as Select, that closed the menu, kept from the pause */
static unsigned taken;                     /* frames a held action's button kept an NES button up */
static int file_layout;                    /* the layout config.ini said, 0 none read yet */
static unsigned moved;                     /* bindings the last load moved */
static char menu_label[80] = "Select menu: RETURN / START";

static uint8_t q(uint16_t a) { return cyc_mod_peek(a); }

/* GameLoop_CheckShowPlayerMenu's entry: a new Start is a new Select. */
static int menu_hook(uint16_t addr) {
    (void)addr;
    if (!enabled || cyc_mod_isolated()) return 0;
    uint8_t changed = q(JOY1_CHANGED_BUTTON_MASK);
    if (!(changed & BUTTON_START)) return 0;
    ++starts;
    cyc_mod_poke(JOY1_CHANGED_BUTTON_MASK, (uint8_t)((changed & ~BUTTON_START) | BUTTON_SELECT));
    return 0;
}

/* The pause check's AND, after its load: no Start. */
static int pause_hook(uint16_t addr) {
    (void)addr;
    if (!enabled || cyc_mod_isolated()) return 0;
    CycModRegs r;
    cyc_mod_regs(&r);
    if (!(r.a & BUTTON_START)) return 0;
    r.a = (uint8_t)(r.a & ~BUTTON_START);
    cyc_mod_set_regs(&r);
    ++unpaused;
    return 0;
}

/* The menu's input loop, at the BMI after its load: a new Start is B. */
static int close_hook(uint16_t addr) {
    (void)addr;
    if (!enabled || cyc_mod_isolated()) return 0;
    CycModRegs r;
    cyc_mod_regs(&r);
    if (!(r.a & BUTTON_START) || (r.a & BUTTON_A)) return 0;
    r.a |= BUTTON_B;
    r.p = (uint8_t)((r.p & ~(P_N | P_Z)) | (r.a & P_N));   /* as the load leaves them: nonzero, N bit 7 */
    cyc_mod_set_regs(&r);
    ++closes;
    return 0;
}

static const char *const hook_ids[] = { "faxanadu.controls.menu", "faxanadu.controls.pause", "faxanadu.controls.close" };
static void hooks(int on) {
    for (unsigned i = 0; i < sizeof hook_ids / sizeof hook_ids[0]; ++i) nes_mod_set_function_hook_enabled(hook_ids[i], on);
}
static void deactivate(void) {
    enabled = false;
    starts = closes = unpaused = taken = 0;
    hooks(0);
}
static void activate(void) {
    enabled = true;
    hooks(1);
    nes_mod_set_local_only("Controller layout", 1);   /* a peer without it would play other rules */
}

/* ---- config.ini [Game] ---- */

bool fax_controls_load_setting(const char *name, const char *value) {
    if (strcmp(name, "ControlsLayout")) return false;
    char *end;
    long v = strtol(value, &end, 10);
    if (end == value || v < 1) fprintf(stderr, "[controls] ControlsLayout: \"%s\" is not a layout; reading it as 1\n", value);
    else file_layout = (int)v;
    return true;
}
void fax_controls_save_settings(FILE *f) {
    fprintf(f, "# the controls' layout these bindings were written with\nControlsLayout = %d\n", LAYOUT);
}

const char *fax_controls_menu_label(void) { return menu_label; }

#ifdef CYC_WITH_SDL
/* Layout 1's controller defaults, of the bindings layout 2 moved. */
static const struct { const CycHostBinding *(*binding)(void); int pad; } LAYOUT1[] = {
    { fax_block_binding, CYC_PAD_BUTTON(SDL_CONTROLLER_BUTTON_B) },
    { fax_map_binding, CYC_PAD_AXIS(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1) },
    { fax_quests_binding, CYC_PAD_BUTTON(SDL_CONTROLLER_BUTTON_RIGHTSTICK) },
};

bool fax_controls_settings_loaded(void) {
    int layout = file_layout ? file_layout : 1;
    file_layout = 0;
    moved = 0;
    if (layout >= LAYOUT) return false;
    for (unsigned i = 0; i < sizeof LAYOUT1 / sizeof LAYOUT1[0]; ++i) {
        const CycHostBinding *b = LAYOUT1[i].binding();
        if (!b || *b->pad != LAYOUT1[i].pad) continue;
        *b->pad = b->default_pad;
        if (b->changed) b->changed();
        ++moved;
    }
    return true;   /* written now with this layout, moved or not */
}

/* ---- the remaster's actions own their controller buttons ---- */

/* A controller binding held on any open controller, as SDL reads it now
 * (button, or an axis past half travel, the actions' press point). */
static bool pad_held(int pad) {
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        SDL_GameController *c = SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(i));
        if (!c) continue;
        if (CYC_PAD_IS_BUTTON(pad) && SDL_GameControllerGetButton(c, (SDL_GameControllerButton)(pad - 1))) return true;
        if (CYC_PAD_IS_AXIS(pad)) {
            int v = SDL_GameControllerGetAxis(c, (SDL_GameControllerAxis)((pad - 100) / 2));
            if ((pad - 100) & 1 ? v >= 16384 : v <= -16384) return true;
        }
    }
    return false;
}

void fax_controls_input(uint8_t pad_bits[2]) {
    if (!enabled) return;
    const CycHostBinding *(*const actions[])(void) = {
        fax_magic_binding, fax_use_item_binding, fax_block_binding, fax_map_binding, fax_quests_binding,
    };
    const CycBindings *bind = cyc_window_bindings();
    uint8_t before = pad_bits[0];
    for (size_t a = 0; a < sizeof actions / sizeof actions[0]; ++a) {
        const CycHostBinding *b = actions[a]();
        if (!b || !*b->pad || !pad_held(*b->pad)) continue;
        for (int i = 0; i < CYC_INPUT_BUTTONS; ++i)
            if (bind->button[0][i].pad == *b->pad) pad_bits[0] &= (uint8_t)~(0x80u >> i);
    }
    if (pad_bits[0] != before) ++taken;
}

void fax_controls_power_on(void) {
    const CycBindings *bind = cyc_window_bindings();
    char hint[64] = "unbound";
    const CycBinding *start = &bind->button[0][3];   /* cyc_input.h: A B Select Start ... */
    if (start->key || start->pad) cyc_binding_hint(start, hint, sizeof hint);
    snprintf(menu_label, sizeof menu_label, "Select menu: %s", hint);
}

static void controls_state(int id, const char *line) {
    (void)line;
    char fields[320], text[96];
    cyc_tcp_quote(menu_label + 13, text, sizeof text);
    /* starts: Starts the menu check saw as Select; closes: Starts that closed the menu; unpaused: Starts the
     * pause check never saw; taken: frames an action's held button kept the NES button the host binds to it
     * up; moved: bindings the last config.ini load moved from layout 1's defaults */
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"layout\":%d,\"menu_binding\":%s,\"starts\":%u,\"closes\":%u,\"unpaused\":%u,"
             "\"taken\":%u,\"moved\":%u",
             enabled ? "true" : "false", LAYOUT, text, starts, closes, unpaused, taken, moved);
    cyc_tcp_ok(id, fields);
}
void fax_controls_tcp_setup(void) {
    cyc_tcp_register("controls_state",
                     "Controller layout: Starts shown to the menu check as Select, Starts that closed the menu, "
                     "NES buttons an action's button kept up, bindings moved from layout 1",
                     controls_state);
}
#else
void fax_controls_input(uint8_t pad_bits[2]) { (void)pad_bits; }
bool fax_controls_settings_loaded(void) { return false; }
void fax_controls_power_on(void) {}
void fax_controls_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_controls) {
    if (!nes_mod_register_function_entry_plugin(hook_ids[0], CHECK_SHOW_PLAYER_MENU, menu_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[1], CHECK_PAUSE_START, pause_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[2], MENU_INPUT_BRANCH, close_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.controls", activate)) abort();
}
