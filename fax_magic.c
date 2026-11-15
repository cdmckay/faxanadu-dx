/* Magic button. USA 57dd23d1, chipx86 byte-exact disassembly.
 * Casting is Up+B: Player_CastMagic (bank 14) casts the selected spell when Up
 * is held and B newly pressed. Game_MainLoop calls it each frame after
 * GameLoop_UpdatePlayer, which enters doors on a new Up and swings the sword
 * on a new B, and just before GameLoop_CheckUseCurrentItem; NPCs listen for a
 * new Up after that, in Sprites_UpdateAll. A press of the Magic button (C, or
 * Y on a controller) shows Up+B to Player_CastMagic alone: its entry hook ORs
 * them into the joypad masks, and GameLoop_CheckUseCurrentItem's entry hook
 * puts the real masks back, before anything else or the next joypad read sees
 * them. The game still decides: one spell at a time, none while climbing, and
 * it costs MP. Up+B is untouched. See docs/GAME_INTERNALS.md. */
#include "fax_magic.h"
#include "cyc_mod.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#ifdef CYC_WITH_SDL
#include "cyc_input.h"
#include "cyc_tcp.h"
#include <SDL.h>
#endif
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Zero page. Input_HandleOnInterrupt, in the NMI, reads the pad once a frame. */
#define JOY1_BUTTON_MASK         0x0016u   /* held: A $80, B $40, Select $20, Start $10, Up $08, Down $04, Left $02, Right $01 */
#define JOY1_PREV_BUTTON_MASK    0x0018u   /* the held mask at the read before */
#define JOY1_CHANGED_BUTTON_MASK 0x0019u   /* newly pressed: held now and not at the read before */
#define BUTTON_B  0x40u
#define BUTTON_UP 0x08u
/* Bank 14 */
#define PLAYER_CAST_MAGIC 0xBA5Bu   /* LDA CastMagic_Type, BPL CastMagic_RunSpellHandler, JSR Player_IsClimbing */
/* Fixed bank */
#define GAME_LOOP_CHECK_USE_CURRENT_ITEM 0xC478u   /* LDA Player_Flags, AND #$05, BNE GameLoop_UseItem_Return */
#define MAIN_LOOP_CAST_PUSHED 0xDB61u   /* Game_MainLoop's JSR Player_CastMagic ($DB5F) pushes the address of its
                                           last byte. EndGame_MainLoop's ($DC0B), which plays scripted buttons,
                                           pushes $DC0D. */

/* The defaults as SDL numbers them: SDL_SCANCODE_C, and SDL_CONTROLLER_BUTTON_Y
 * in the host's binding encoding (cyc_input.h CYC_PAD_BUTTON: 1 + the button). */
enum { DEFAULT_KEY = 6, DEFAULT_PAD = 1 + 3, AXIS_PRESS = 16384 };

/* The real masks while Up+B stands in for them, between the two hooks. Saved
 * with the machine, in case a save state lands in between (a lag frame). */
typedef struct { uint8_t injected, held, changed; } Swap;

static bool enabled;
static bool pressed;              /* Magic went down since the last frame ended */
static Swap swap;
static bool axis_down;            /* a stick or trigger binding is past its press point */
static unsigned presses, presented, restored, reread;
static char label[80] = "Magic: C / Y";   /* the Controls row; the bindings follow "Magic: " */

/* A press of the Magic button. Like a button the NMI reads, it lasts one
 * frame: if Player_CastMagic does not run in that frame (a menu, a
 * conversation, a lag frame), the press is gone, as a stock B press would be. */
static void press(void) {
    if (!enabled) return;
    pressed = true;
    ++presses;
}

static uint16_t jsr_pushed(uint8_t s) {   /* what the JSR that entered this routine pushed */
    return (uint16_t)(cyc_mod_peek((uint16_t)(0x100u | (uint8_t)(s + 1))) |
                      cyc_mod_peek((uint16_t)(0x100u | (uint8_t)(s + 2))) << 8);
}

/* Player_CastMagic, from Game_MainLoop: show it Up held and B newly pressed. */
static int cast_hook(uint16_t addr) {
    (void)addr;
    if (!pressed || swap.injected) return 0;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    if (jsr_pushed(regs.s) != MAIN_LOOP_CAST_PUSHED) return 0;
    pressed = false;
    swap = (Swap){ 1, cyc_mod_peek(JOY1_BUTTON_MASK), cyc_mod_peek(JOY1_CHANGED_BUTTON_MASK) };
    cyc_mod_poke(JOY1_BUTTON_MASK, (uint8_t)(swap.held | BUTTON_UP | BUTTON_B));
    cyc_mod_poke(JOY1_CHANGED_BUTTON_MASK, (uint8_t)(swap.changed | BUTTON_B));
    ++presented;
    return 0;
}

/* Put the real masks back. If the NMI read the pad in between, it took our
 * mask as the previous one: redo its changed mask against the real one, so a
 * button pressed meanwhile is still new and Up and B are not held over. After
 * two reads or more, the last one never saw ours. The changed mask tells
 * whether a read came: ours has B new, and a read after ours cannot, since
 * B was already held in ours. */
static void restore(void) {
    uint8_t ours = (uint8_t)(swap.held | BUTTON_UP | BUTTON_B);
    uint8_t held = cyc_mod_peek(JOY1_BUTTON_MASK), changed = cyc_mod_peek(JOY1_CHANGED_BUTTON_MASK);
    if (held == ours && changed == (uint8_t)(swap.changed | BUTTON_B)) {
        cyc_mod_poke(JOY1_BUTTON_MASK, swap.held);
        cyc_mod_poke(JOY1_CHANGED_BUTTON_MASK, swap.changed);
    } else if (cyc_mod_peek(JOY1_PREV_BUTTON_MASK) == ours) {
        cyc_mod_poke(JOY1_PREV_BUTTON_MASK, swap.held);
        cyc_mod_poke(JOY1_CHANGED_BUTTON_MASK, (uint8_t)((held ^ swap.held) & held));
        ++reread;
    }
    swap.injected = 0;
    ++restored;
}

/* GameLoop_CheckUseCurrentItem: the real masks, for the item check and on. */
static int restore_hook(uint16_t addr) {
    (void)addr;
    if (swap.injected) restore();
    return 0;
}

void fax_magic_frame_end(void) { pressed = false; }

static const char *const hook_ids[] = { "faxanadu.magic-button.cast", "faxanadu.magic-button.restore" };
static void hooks(int on) {
    for (unsigned i = 0; i < sizeof hook_ids / sizeof hook_ids[0]; ++i) nes_mod_set_function_hook_enabled(hook_ids[i], on);
}
static void deactivate(void) {
    if (swap.injected) restore();
    enabled = pressed = axis_down = false;
    presses = presented = restored = reread = 0;
    hooks(0);
}
static void activate(void) {
    enabled = true;
    hooks(1);
    nes_mod_set_local_only("Magic button", 1);   /* host input the other side would not see */
}

static int state_get(uint8_t *buf, int cap) {
    if (!swap.injected) return 0;
    if (cap < 3) return -1;
    buf[0] = swap.injected; buf[1] = swap.held; buf[2] = swap.changed;
    return 3;
}
static int state_validate(const uint8_t *buf, int len) {
    return len == 0 || (len == 3 && buf[0] == 1);
}
static int state_set(const uint8_t *buf, int len) {
    if (!state_validate(buf, len)) return 0;
    swap = len ? (Swap){ buf[0], buf[1], buf[2] } : (Swap){ 0 };
    pressed = false;
    if (swap.injected && !enabled) restore();   /* turned off since: no hook will */
    return 1;
}

/* ---- the bindings (config.ini [Game]) ---- */

const char *fax_magic_controls_label(void) { return label; }

#ifdef CYC_WITH_SDL
static int key = DEFAULT_KEY, pad = DEFAULT_PAD;   /* 0: unbound */

static void describe(void) {   /* as the host shows its own shortcuts: "C / Y" */
    char hint[64] = "unbound";
    CycBinding both = { key, pad };
    if (key || pad) cyc_binding_hint(&both, hint, sizeof hint);
    snprintf(label, sizeof label, "Magic: %s", hint);
}

bool fax_magic_load_setting(const char *name, const char *value) {
    int v;
    if (!strcmp(name, "MagicKey")) {
        if (cyc_key_parse(value, &v)) key = v;
        else fprintf(stderr, "[magic] MagicKey: SDL has no key named \"%s\"; keeping the default\n", value);
    } else if (!strcmp(name, "MagicButton")) {
        if (cyc_pad_parse(value, &v) && !CYC_PAD_IS_COMBO(v)) pad = v;
        else fprintf(stderr, "[magic] MagicButton: \"%s\" is not one controller button or axis; keeping the default\n", value);
    } else {
        return false;
    }
    describe();
    return true;
}
void fax_magic_save_settings(FILE *f) {
    char k[32], p[48];
    cyc_key_text(key, k, sizeof k);
    cyc_pad_text(pad, p, sizeof p);
    fprintf(f, "MagicKey = %s\nMagicButton = %s\n", k, p);
}

void fax_magic_event(const void *ev) {
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

static void magic_state(int id, const char *line) {
    (void)line;
    char fields[320], text[96];
    cyc_tcp_quote(label + 7, text, sizeof text);
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"binding\":%s,\"presses\":%u,\"presented\":%u,\"restored\":%u,\"reread\":%u,"
             "\"injected\":%s",
             enabled ? "true" : "false", text, presses, presented, restored, reread,
             swap.injected ? "true" : "false");
    cyc_tcp_ok(id, fields);
}
void fax_magic_tcp_setup(void) {
    cyc_tcp_register("magic_state",
                     "Magic button: bindings, presses, presses shown to Player_CastMagic, masks restored", magic_state);
}
#else
bool fax_magic_load_setting(const char *name, const char *value) { (void)name; (void)value; return false; }
void fax_magic_save_settings(FILE *f) { (void)f; }
void fax_magic_event(const void *ev) { (void)ev; }
void fax_magic_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_magic) {
    if (!nes_mod_register_function_entry_plugin(hook_ids[0], PLAYER_CAST_MAGIC, cast_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[1], GAME_LOOP_CHECK_USE_CURRENT_ITEM, restore_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.magic-button", activate) ||
        !nes_mod_register_savestate_hook("faxanadu.magic-button", state_get, state_set) ||
        !nes_mod_register_savestate_validator("faxanadu.magic-button", state_validate)) abort();
}
