/* Hold A to speed up text. USA 57dd23d1, chipx86 byte-exact disassembly.
 * Every message loop calls TextBox_ShowNextChar once a frame. It increments
 * TextBox_Timer, asks for the typing sound (TextBox_PlayTextSound = 1) and
 * calls TextBox_ShowNextChar_IfReady, which draws the next character only
 * when the timer is a multiple of 4: one character every 4 frames.
 *
 * While A is held, a hook at IfReady rounds the timer up to the next multiple
 * of 4, so a character types every frame, 4x. Releasing A returns to the
 * game's pace from the next character. The typing sound is 4 frames long (a
 * soft hiss, then a clack), so starting it every frame would leave only an
 * unbroken hiss: while A is held it is asked for on every 4th frame only, the
 * rhythm it has at the game's own pace.
 *
 * Only TextBox_Timer and TextBox_PlayTextSound are written. Page breaks
 * ($FC), finished messages and questions continue only on a newly pressed A
 * (Joy1_ChangedButtonMask), so holding A types to the end of the page and
 * stops there: release and press to go on. The frame A is first pressed is
 * left at the game's pace, so a press can never finish a page that the stock
 * game would not have finished on that frame. B is untouched. The Magic
 * button (fax_magic.c) ORs Up and B into the joypad masks for
 * Player_CastMagic alone, where no message types, and never touches A, so
 * the two cannot see each other. See docs/GAME_INTERNALS.md. */
#include "fax_text_speed.h"
#include "cyc_mod.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#ifdef CYC_WITH_SDL
#include "cyc_tcp.h"
#endif
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

/* RAM */
#define JOY1_BUTTON_MASK         0x0016u   /* buttons held, read in the NMI */
#define JOY1_CHANGED_BUTTON_MASK 0x0019u   /* buttons newly pressed this frame */
#define TEXTBOX_PLAY_TEXT_SOUND  0x0212u   /* TextBox_ShowMessageWithSound plays the typing sound if set */
#define TEXTBOX_TIMER            0x021Du   /* Messages_Load zeroes it; TextBox_ShowNextChar increments it */
#define BUTTON_A 0x80u
/* Fixed bank. TextBox_ShowNextChar ($F466): LDA CurrentROMBank, PHA, LDX #13,
 * JSR MMC1_UpdateROMBank, INC TextBox_Timer, LDA #1, STA TextBox_PlayTextSound,
 * JSR TextBox_ShowNextChar_IfReady, which nothing else calls. */
/* LDA MessageID, BEQ, LDA TextBox_MessagePaused, BNE,
 * LDA TextBox_Timer, AND #3, BNE: return unless ready */
#define TEXTBOX_SHOW_NEXT_CHAR_IF_READY 0xF491u

static bool enabled;
static unsigned long fast;           /* frames sped up, for the TCP command */

/* A is held, and was not just pressed. */
static bool speeding(uint8_t held, uint8_t pressed) {
    return (held & BUTTON_A) && !(pressed & BUTTON_A);
}
/* The timer rounded up to a multiple of 4, so IfReady draws this frame. */
static uint8_t ready(uint8_t timer) { return (uint8_t)((timer + 3u) & ~3u); }
/* Sped up, the timer gains 4 a frame: keep the typing sound to every 4th. */
static bool sound(uint8_t timer) { return (timer & 0x0Cu) == 0; }

/* TextBox_ShowNextChar_IfReady: make this a ready frame, then let it run. */
static int if_ready_hook(uint16_t addr) {
    (void)addr;
    if (!speeding(cyc_mod_peek(JOY1_BUTTON_MASK), cyc_mod_peek(JOY1_CHANGED_BUTTON_MASK))) return 0;
    uint8_t timer = ready(cyc_mod_peek(TEXTBOX_TIMER));
    cyc_mod_poke(TEXTBOX_TIMER, timer);
    if (!sound(timer)) cyc_mod_poke(TEXTBOX_PLAY_TEXT_SOUND, 0);
    ++fast;
    return 0;
}

static void deactivate(void) {
    enabled = false;
    nes_mod_set_function_hook_enabled("faxanadu.text-speed.if-ready", 0);
}
static void activate(void) {
    enabled = true;
    nes_mod_set_function_hook_enabled("faxanadu.text-speed.if-ready", 1);
}

#ifdef CYC_WITH_SDL
static void text_speed_state(int id, const char *line) {
    (void)line;
    char fields[96];
    snprintf(fields, sizeof fields, "\"enabled\":%s,\"fast\":%lu", enabled ? "true" : "false", fast);
    cyc_tcp_ok(id, fields);
}
void fax_text_speed_tcp_setup(void) {
    cyc_tcp_register("text_speed_state", "Hold A to speed up text: enabled, frames sped up", text_speed_state);
}
#else
void fax_text_speed_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_text_speed) {
    if (!nes_mod_register_function_entry_plugin("faxanadu.text-speed.if-ready", TEXTBOX_SHOW_NEXT_CHAR_IF_READY,
                                                if_ready_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.text-speed", activate)) abort();
}
