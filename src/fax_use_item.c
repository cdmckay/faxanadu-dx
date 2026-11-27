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
 * same case alone, A loses those two bits.
 *
 * On a controller the button is also the NES B outside play (#160), as a
 * modern pad's B backs out. Each press is one or the other, decided by the
 * loop whose pad read first sees it. The game reads the pad only in the NMI,
 * and only while its main thread waits in WaitForNextFrame ($CA25, which
 * stores 0 in Game_InterruptsHandledLatch and spins until the NMI sets it):
 * the address its JSR pushed names the loop that takes the read. A press that
 * meets the read of a text box or question (IScripts_UpdatePortraitAnimation,
 * once a frame in every script's message loop: a shop, Guru travel, saving),
 * the Select menu, a list or the PLAYER page, the pause (the map and the
 * quest log) or the title (its file picks) presses the NES B for as long as
 * it is held, until a read of play's: there it lets go, and stays up until
 * the button does. Any other read (Game_MainLoop's, a door's, an item's or a
 * death's waits) makes it a press of Use item, which never presses the NES
 * B. A back press a read inside GameLoop_UpdatePlayer took (a door's line) is
 * still new for the rest of that pass: at Game_MainLoop's JSR Player_CastMagic
 * ($DB5F) it is taken out of the changed mask, so neither the cast nor the
 * item check sees it. The keyboard's S is only ever Use item. See
 * docs/GAME_INTERNALS.md, "Input and casting" and "Wing Boots, the Hour Glass
 * and the Elixir". */
#include "fax_use_item.h"
#include "fax_controls.h"
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
/* Game_InterruptsHandledLatch: WaitForNextFrame stores 0, the NMI that
 * reads the pad 1; an NMI that finds it 1 reads nothing (a lag frame) */
#define INTERRUPTS_HANDLED_LATCH   0x0010u
#define INTERRUPT_HANDLERS_ENABLED 0x0013u   /* Game_InterruptHandlersEnabled: 0, the NMI does none of its work */
#define JOY1_BUTTON_MASK   0x0016u   /* held: A $80, B $40, Select $20, Start $10, Up $08, Down $04, Left $02, Right $01 */
#define JOY1_CHANGED_BUTTON_MASK 0x0019u   /* newly pressed: held now and not at the read before */
#define PLAYER_FLAGS       0x00A4u   /* $04 falling, $01 jumping */
#define PLAYER_STATUS_FLAG 0x00A5u   /* $80 while the Wing Boots fly */
#define CURRENT_ROM_BANK   0x0100u   /* CurrentROMBank: the bank at $8000 */
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
/* Game_MainLoop's JSR GameLoop_CheckUseCurrentItem ($DB62) pushes the
 * address of its last byte; EndGame_MainLoop's ($DC0E), which plays
 * scripted buttons, pushes $DC10 */
#define MAIN_LOOP_ITEM_PUSHED 0xDB64u
/* Game_MainLoop: JSR Player_CastMagic, JSR GameLoop_CheckUseCurrentItem, after
 * GameLoop_UpdatePlayer and the hero's three sprites */
#define MAIN_LOOP_CAST 0xDB5Fu
#define WAIT_FOR_NEXT_FRAME     0xCA25u   /* LDA #$00, STA Game_InterruptsHandledLatch, $CA29 @_loop: LDA it, */
#define WAIT_FOR_NEXT_FRAME_RTS 0xCA2Du   /* BEQ @_loop, $CA2D RTS */
#define MENU_BANK 12u

/* The JSRs to WaitForNextFrame whose frame's pad read a text box or a menu
 * takes, by the address each pushes (the JSR's, plus 2). Bank 12's need it
 * mapped; no other switched bank calls WaitForNextFrame. */
static const uint16_t MENU_WAITS[] = {
    0x87B2u,   /* IScripts_UpdatePortraitAnimation ($87B0), once a frame in every script's message, question and
                  shop loop: the text, the questions, the shops' lists, Guru travel and saving */
    0x8ADFu,   /* PlayerMenu_Show's @_inputLoop ($8ADD): the Select menu */
    0x8B43u,   /* PlayerMenu_ShowSubmenu's @_inputLoop ($8B41): a list with nothing in it */
    0x8B73u,   /* PlayerMenu_HandleInventoryMenuInput ($8B71): a list */
    0x8E43u,   /* PlayerMenu_ShowStatusMenu's @_waitForInputLoop ($8E41): the PLAYER page */
    0xE038u,   /* GameLoop_CheckPauseGame's @_waitForUnpause ($E036): the map and the quest log, which pause */
    0xFC70u,   /* Game_ShowStartScreen's @_waitForInput ($FC6E): the title and its file picks */
};

/* The defaults as SDL numbers them: SDL_SCANCODE_S, and SDL_CONTROLLER_BUTTON_B
 * (the right face button) in the host's binding encoding (cyc_input.h
 * CYC_PAD_BUTTON: 1 + the button). */
enum { DEFAULT_KEY = 22, DEFAULT_PAD = 1 + 1, AXIS_PRESS = 16384 };

static bool enabled;
static bool pressed;              /* Use item went down since the last frame ended */
static bool axis_down;            /* a stick or trigger binding is past its press point */
static unsigned presses, shown, lapsed, flights;

/* The controller binding's press, from its going down to its letting go. */
enum { PAD_UP, PAD_NEW, PAD_BACK, PAD_DONE };
enum { READ_NONE, READ_PLAY, READ_MENU };   /* what a frame's pad read is */
static uint8_t pad_press = PAD_UP;   /* PAD_NEW until a read decides it, PAD_BACK while it is the NES B, then
                                        PAD_DONE (used, or the NES B let go) until the button is up */
static bool pad_down;                /* the button is down; a press let go before any read stays PAD_NEW */
static bool back_read;               /* the last pad read had the NES B from a back press alone */
static uint8_t last_read = READ_NONE;
static unsigned backs, kept;         /* presses that were the NES B; their B kept from play at $DB5F */
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

/* ---- the controller's press: Use item in play, the NES B outside it ---- */

/* The pad read the NMI that starts the next frame makes, from where the
 * game's main thread waits: none (a lag frame, or the NMI's work off), a
 * menu's (one of MENU_WAITS), or play's (any other). */
static uint8_t next_read(void) {
    if (!cyc_mod_peek(INTERRUPT_HANDLERS_ENABLED) || cyc_mod_peek(INTERRUPTS_HANDLED_LATCH)) return READ_NONE;
    CycModRegs r;
    cyc_mod_regs(&r);
    if (r.pc < WAIT_FOR_NEXT_FRAME || r.pc > WAIT_FOR_NEXT_FRAME_RTS) return READ_PLAY;   /* the latch's other store */
    uint16_t pushed = (uint16_t)(cyc_mod_peek((uint16_t)(0x100u | (uint8_t)(r.s + 1))) |
                                 cyc_mod_peek((uint16_t)(0x100u | (uint8_t)(r.s + 2))) << 8);
    if (pushed < 0xC000u && cyc_mod_peek(CURRENT_ROM_BANK) != MENU_BANK) return READ_PLAY;
    for (unsigned i = 0; i < sizeof MENU_WAITS / sizeof MENU_WAITS[0]; ++i)
        if (pushed == MENU_WAITS[i]) return READ_MENU;
    return READ_PLAY;
}

static void pad_pressed(void) {
    if (!enabled) return;
    pad_down = true;
    pad_press = PAD_NEW;
}
static void pad_released(void) {
    pad_down = false;
    if (pad_press != PAD_NEW) pad_press = PAD_UP;   /* a tap no read has seen yet: the next frame decides it */
}

static void pad_sync(void);

/* The pad bytes for the next frame (cyc_host_extras.h input), after the
 * host's mapping: the NES B for a back press. */
void fax_use_item_input(uint8_t buttons[2]) {
    if (!enabled || cyc_mod_isolated()) return;
    pad_sync();
    uint8_t read = next_read();
    if (read != READ_NONE) last_read = read;
    if (pad_press == PAD_NEW) {
        if (read == READ_NONE) {
            if (!pad_down) pad_press = PAD_UP;     /* a tap between two reads, as an NES button's would be */
        } else if (read == READ_MENU) {
            pad_press = PAD_BACK;
            ++backs;
        } else {
            pad_press = PAD_DONE;
            press();
        }
    } else if (pad_press == PAD_BACK && read == READ_PLAY) {
        pad_press = PAD_DONE;                      /* play reads the pad: the NES B lets go */
    }
    bool back = pad_press == PAD_BACK;
    if (read != READ_NONE) back_read = back && !(buttons[0] & BUTTON_B);
    if (back) buttons[0] |= BUTTON_B;
    if (!pad_down && pad_press != PAD_NEW) pad_press = PAD_UP;   /* a tap: this one frame */
}

/* Game_MainLoop's JSR Player_CastMagic: a back press's B that a read in
 * GameLoop_UpdatePlayer took (a door's line) is new to neither the cast nor
 * the item check after it. */
static int back_hook(uint16_t addr) {
    (void)addr;
    if (!enabled || !back_read || cyc_mod_isolated()) return 0;
    uint8_t changed = cyc_mod_peek(JOY1_CHANGED_BUTTON_MASK);
    if (!(changed & BUTTON_B)) return 0;
    cyc_mod_poke(JOY1_CHANGED_BUTTON_MASK, (uint8_t)(changed & ~BUTTON_B));
    ++kept;
    return 0;
}

static const char *const hook_ids[] = { "faxanadu.use-item.air", "faxanadu.use-item.down", "faxanadu.use-item.b",
                                        "faxanadu.use-item.back" };
static void hooks(int on) {
    for (unsigned i = 0; i < sizeof hook_ids / sizeof hook_ids[0]; ++i) nes_mod_set_function_hook_enabled(hook_ids[i], on);
}
static void deactivate(void) {
    enabled = pressed = axis_down = pad_down = back_read = false;
    pad_press = PAD_UP;
    last_read = READ_NONE;
    presses = shown = lapsed = flights = backs = kept = 0;
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

/* The keyboard's press is Use item; the controller's waits for the next pad
 * read to say which it is (fax_use_item_input). */
void fax_use_item_event(const void *ev) {
    const SDL_Event *e = ev;
    if (e->type == SDL_KEYDOWN) {
        if (key && !e->key.repeat && (int)e->key.keysym.scancode == key) press();
    } else if (e->type == SDL_CONTROLLERBUTTONDOWN || e->type == SDL_CONTROLLERBUTTONUP) {
        if (CYC_PAD_IS_BUTTON(pad) && e->cbutton.button == pad - 1) {
            if (e->type == SDL_CONTROLLERBUTTONDOWN) pad_pressed();
            else pad_released();
        }
    } else if (e->type == SDL_CONTROLLERAXISMOTION && CYC_PAD_IS_AXIS(pad) && e->caxis.axis == (pad - 100) / 2) {
        int value = (pad - 100) & 1 ? e->caxis.value : -e->caxis.value;
        bool down = value >= AXIS_PRESS;
        if (down && !axis_down) pad_pressed();
        else if (!down && axis_down) pad_released();
        axis_down = down;
    }
}

/* The host passes the game no events while its menu is up: a button let go
 * there is up, as SDL reads it now. */
static void pad_sync(void) {
    if (pad_down && !fax_controls_pad_held(pad)) {
        axis_down = false;
        pad_released();
    }
}

static void use_item_state(int id, const char *line) {
    (void)line;
    static const char *const reads[] = { "none", "play", "menu" }, *const pads[] = { "up", "new", "back", "done" };
    char fields[384], text[96];
    cyc_tcp_quote(label + 10, text, sizeof text);
    /* presses: presses taken; shown: those that reached the item check's B; lapsed: those whose frame ended
     * first (in the air, a menu, a lag frame); flights: shown in flight with A held, past the air check;
     * backs: controller presses that were the NES B; kept: of those, B taken from play at $DB5F; read: the
     * last frame's pad read, play's or a menu's; pad: the controller press now */
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"binding\":%s,\"presses\":%u,\"shown\":%u,\"lapsed\":%u,\"flights\":%u,"
             "\"backs\":%u,\"kept\":%u,\"read\":\"%s\",\"pad\":\"%s\"",
             enabled ? "true" : "false", text, presses, shown, lapsed, flights, backs, kept, reads[last_read],
             pads[pad_press]);
    cyc_tcp_ok(id, fields);
}
void fax_use_item_tcp_setup(void) {
    cyc_tcp_register("use_item_state",
                     "Use item button: bindings, presses, presses shown to GameLoop_CheckUseCurrentItem, lapsed",
                     use_item_state);
}
#else
static void pad_sync(void) {}
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
        !nes_mod_register_function_entry_plugin(hook_ids[3], MAIN_LOOP_CAST, back_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.use-item", activate)) abort();
}
