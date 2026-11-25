/* The Use item button (#147). USA 57dd23d1, chipx86 byte-exact disassembly.
 * Using an item is Down+B: GameLoop_CheckUseCurrentItem ($C478, fixed bank),
 * which Game_MainLoop calls each frame after Player_CastMagic, refuses while
 * jumping or falling (Player_Flags & $05), then wants Down held and B newly
 * pressed, then runs the selected item's routine from USE_ITEM_JUMP_TABLE.
 * A press of Use item (S, or B on a controller) shows Down and a new B to
 * that check alone: at its AND #$04 ($C480, after LDA Joy1_ButtonMask) A
 * gets Down, and at its AND #$40 ($C487, after LDA Joy1_ChangedButtonMask) A
 * gets B. Only the registers the game just loaded change, never the masks:
 * the sword (GameLoop_UpdatePlayer, earlier in the pass), the ladders and
 * Sprites_UpdateAll (after it) see the pad as it is, so the button neither
 * swings the sword nor climbs down a ladder.
 *
 * The game's rule stands: the check at $C47A refuses while jumping or
 * falling, and the item's routine decides the rest. The one exception the
 * stock Down+B has is flight: with the Wing Boots flying (Player_StatusFlag
 * $80) and A held, Player_CheckHandleVertMovement takes a held Down as a
 * climb down, which clears the jumping and falling bits before the item
 * check, so A+Down+B in the air uses the item. The button does not hold Down
 * through the pass, so at $C47A (AND #$05, after LDA Player_Flags), in that
 * same case alone, A loses those two bits. See docs/GAME_INTERNALS.md, "Input
 * and casting" and "Wing Boots, the Hour Glass and the Elixir". */
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
#define JOY1_BUTTON_MASK   0x0016u   /* held: A $80, B $40, Select $20, Start $10, Up $08, Down $04, Left $02, Right $01 */
#define PLAYER_FLAGS       0x00A4u   /* $04 falling, $01 jumping */
#define PLAYER_STATUS_FLAG 0x00A5u   /* $80 while the Wing Boots fly */
#define BUTTON_A    0x80u
#define BUTTON_B    0x40u
#define BUTTON_DOWN 0x04u
#define IN_THE_AIR  0x05u   /* Player_Flags: jumping or falling, what $C47A refuses */
#define FLYING      0x80u
/* Fixed bank: GameLoop_CheckUseCurrentItem ($C478): LDA Player_Flags, $C47A AND #$05, BNE return, LDA
 * Joy1_ButtonMask, $C480 AND #$04, BEQ return, LDA a:Joy1_ChangedButtonMask, $C487 AND #$40, BEQ return, then
 * the selected item's routine. Each site is the AND after a load: A is the loaded byte, and the AND sets N and
 * Z again from what it leaves. */
#define CHECK_IN_THE_AIR 0xC47Au
#define CHECK_DOWN       0xC480u
#define CHECK_B          0xC487u
#define MAIN_LOOP_ITEM_PUSHED 0xDB64u   /* Game_MainLoop's JSR GameLoop_CheckUseCurrentItem ($DB62) pushes the
                                           address of its last byte; EndGame_MainLoop's ($DC0E), which plays
                                           scripted buttons, pushes $DC10 */

/* The defaults as SDL numbers them: SDL_SCANCODE_S, and SDL_CONTROLLER_BUTTON_B
 * (the right face button) in the host's binding encoding (cyc_input.h
 * CYC_PAD_BUTTON: 1 + the button). */
enum { DEFAULT_KEY = 22, DEFAULT_PAD = 1 + 1, AXIS_PRESS = 16384 };

static bool enabled;
static bool pressed;              /* Use item went down since the last frame ended */
static bool axis_down;            /* a stick or trigger binding is past its press point */
static unsigned presses, shown, lapsed, flights;
static char label[80] = "Use item: S / B";   /* the Controls row; the bindings follow "Use item: " */

/* A press of Use item. Like a button the NMI reads, it lasts one frame: if
 * the item check does not get that far in that frame (in the air, a menu, a
 * conversation, a lag frame), the press is gone, as a stock B press would be. */
static void press(void) {
    if (!enabled) return;
    pressed = true;
    ++presses;
}

static bool from_main_loop(void) {   /* the JSR that entered GameLoop_CheckUseCurrentItem: Game_MainLoop's */
    CycModRegs r;
    cyc_mod_regs(&r);
    uint16_t pushed = (uint16_t)(cyc_mod_peek((uint16_t)(0x100u | (uint8_t)(r.s + 1))) |
                                 cyc_mod_peek((uint16_t)(0x100u | (uint8_t)(r.s + 2))) << 8);
    return pushed == MAIN_LOOP_ITEM_PUSHED;
}
static bool wanted(void) { return enabled && pressed && !cyc_mod_isolated() && from_main_loop(); }
static void or_into_a(uint8_t bits, bool clear) {   /* A as the load left it, with `bits` set (or cleared) */
    CycModRegs r;
    cyc_mod_regs(&r);
    r.a = clear ? (uint8_t)(r.a & ~bits) : (uint8_t)(r.a | bits);
    cyc_mod_set_regs(&r);
}

/* $C47A: Player_Flags in A. In flight with A held, Down+B would have cleared
 * the jumping and falling bits by now; the button clears them from A alone. */
static int air_hook(uint16_t addr) {
    (void)addr;
    if (!wanted()) return 0;
    if ((cyc_mod_peek(PLAYER_STATUS_FLAG) & FLYING) && (cyc_mod_peek(JOY1_BUTTON_MASK) & BUTTON_A)) {
        CycModRegs r;
        cyc_mod_regs(&r);
        if (r.a & IN_THE_AIR) ++flights;
        or_into_a(IN_THE_AIR, true);
    }
    return 0;
}
/* $C480: the held mask in A. Down. */
static int down_hook(uint16_t addr) {
    (void)addr;
    if (wanted()) or_into_a(BUTTON_DOWN, false);
    return 0;
}
/* $C487: the changed mask in A. A new B: the press is used. */
static int b_hook(uint16_t addr) {
    (void)addr;
    if (!wanted()) return 0;
    or_into_a(BUTTON_B, false);
    pressed = false;
    ++shown;
    return 0;
}

void fax_use_item_frame_end(void) {
    if (pressed) ++lapsed;
    pressed = false;
}

static const char *const hook_ids[] = { "faxanadu.use-item.air", "faxanadu.use-item.down", "faxanadu.use-item.b" };
static void hooks(int on) {
    for (unsigned i = 0; i < sizeof hook_ids / sizeof hook_ids[0]; ++i) nes_mod_set_function_hook_enabled(hook_ids[i], on);
}
static void deactivate(void) {
    enabled = pressed = axis_down = false;
    presses = shown = lapsed = flights = 0;
    hooks(0);
}
static void activate(void) {
    enabled = true;
    hooks(1);
    nes_mod_set_local_only("Use item button", 1);   /* host input the other side would not see */
}

/* ---- the bindings (config.ini [Game]) ---- */

const char *fax_use_item_controls_label(void) { return label; }

#ifdef CYC_WITH_SDL
static int key = DEFAULT_KEY, pad = DEFAULT_PAD;   /* 0: unbound */

static void describe(void) {   /* as the host shows its own shortcuts: "S / B" */
    char hint[64] = "unbound";
    CycBinding both = { key, pad };
    if (key || pad) cyc_binding_hint(&both, hint, sizeof hint);
    snprintf(label, sizeof label, "Use item: %s", hint);
}

const CycHostBinding *fax_use_item_binding(void) {
    static const CycHostBinding b = { "Use item", &key, &pad, DEFAULT_KEY, DEFAULT_PAD, describe };
    return &b;
}

bool fax_use_item_load_setting(const char *name, const char *value) {
    int v;
    if (!strcmp(name, "UseItemKey")) {
        if (cyc_key_parse(value, &v)) key = v;
        else fprintf(stderr, "[use item] UseItemKey: SDL has no key named \"%s\"; keeping the default\n", value);
    } else if (!strcmp(name, "UseItemButton")) {
        if (cyc_pad_parse(value, &v) && !CYC_PAD_IS_COMBO(v)) pad = v;
        else fprintf(stderr, "[use item] UseItemButton: \"%s\" is not one controller button or axis; keeping the default\n", value);
    } else {
        return false;
    }
    describe();
    return true;
}
void fax_use_item_save_settings(FILE *f) {
    char k[32], p[48];
    cyc_key_text(key, k, sizeof k);
    cyc_pad_text(pad, p, sizeof p);
    fprintf(f, "UseItemKey = %s\nUseItemButton = %s\n", k, p);
}

void fax_use_item_event(const void *ev) {
    const SDL_Event *e = ev;
    if (e->type == SDL_KEYDOWN) {
        if (key && !e->key.repeat && e->key.keysym.scancode == key) press();
    } else if (e->type == SDL_CONTROLLERBUTTONDOWN) {
        if (CYC_PAD_IS_BUTTON(pad) && e->cbutton.button == pad - 1) press();
    } else if (e->type == SDL_CONTROLLERAXISMOTION && CYC_PAD_IS_AXIS(pad) && e->caxis.axis == (pad - 100) / 2) {
        int value = (pad - 100) & 1 ? e->caxis.value : -e->caxis.value;
        bool down = value >= AXIS_PRESS;
        if (down && !axis_down) press();
        axis_down = down;
    }
}

static void use_item_state(int id, const char *line) {
    (void)line;
    char fields[256], text[96];
    cyc_tcp_quote(label + 10, text, sizeof text);
    /* presses: presses taken; shown: those that reached the item check's B; lapsed: those whose frame ended
     * first (in the air, a menu, a lag frame); flights: shown in flight with A held, past the air check */
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"binding\":%s,\"presses\":%u,\"shown\":%u,\"lapsed\":%u,\"flights\":%u",
             enabled ? "true" : "false", text, presses, shown, lapsed, flights);
    cyc_tcp_ok(id, fields);
}
void fax_use_item_tcp_setup(void) {
    cyc_tcp_register("use_item_state",
                     "Use item button: bindings, presses, presses shown to GameLoop_CheckUseCurrentItem, lapsed",
                     use_item_state);
}
#else
const CycHostBinding *fax_use_item_binding(void) { return NULL; }
bool fax_use_item_load_setting(const char *name, const char *value) { (void)name; (void)value; return false; }
void fax_use_item_save_settings(FILE *f) { (void)f; }
void fax_use_item_event(const void *ev) { (void)ev; }
void fax_use_item_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_use_item) {
    if (!nes_mod_register_function_entry_plugin(hook_ids[0], CHECK_IN_THE_AIR, air_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[1], CHECK_DOWN, down_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[2], CHECK_B, b_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.use-item", activate)) abort();
}
