/* Small fixes: three of the original's small bugs (#39). USA 57dd23d1,
 * chipx86 byte-exact disassembly.
 *
 * The typing sound. SoundEffect_Message_OnTick ($86A8, bank 5) runs a tick of
 * sound effect 1, a message's typing sound, four ticks a character. It
 * picks the tick's NOISE_LO from the two-byte table
 * SOUNDEFFECT_MESSAGE_NOISE_LO ($86D1: $10, $01) by SoundEffect_State_Counter
 * AND #$02, so ticks 3 and 4 read index 2, past the table's end: $A9, the
 * opcode of SoundEffect_HitEnemy_Setup's LDA #$14 (chipx86: the loud clack
 * is a bug; an AND #$01 made AND #$02 without a padding byte). A hook at the
 * TAX after the AND halves A, so ticks 3 and 4 read index 1: the table's
 * own $01, the softer sound, still switching every two ticks.
 *
 * The mantra's line ends. IScriptAction_ShowPassword ($8737, bank 12) types
 * the mantra itself, a character a frame through PasswordScreen_ShowNextChar
 * and TextBox_ShowMessage. At each line's 16th column that routine
 * (TextBox_ShowMessage_HandleSpaceOrEndOfLine) decides whether to start a
 * new line by reading the message's next byte, at Message_StartAddr +
 * Message_ProcessedLength: left over from the Guru's last message, which is
 * in bank 13, but read with bank 12 mapped. A $FF or $FC there would start no
 * line: the mantra's second line would overwrite its first. In the shipped
 * game the Guru's greeting ($22) always comes just before, leaving the
 * pointer on the next message's first byte ($876B), and bank 12 holds $48
 * there, so every mantra comes out right; bank 12's $8760, a few bytes
 * before, is $FF. A hook at the action's entry points the message at the
 * mantra's own characters, PASSWORD_ENTERED_CHARS ($8764, bank 12: "ABC..."),
 * so the check always reads a letter and starts the new line (chipx86's
 * suggestion). Nothing reads the pointer again before the next Messages_Load.
 *
 * Ointment and the shield. Enemy magic that hits the shield runs
 * Player_HandleShieldHitByMagic ($87CB, bank 14): Player_HandleHitByMagic
 * ($87DC) lets SPRITE_ENTITY_DAMAGES >> (shield + 1) through, with knockback
 * and iframes, checking neither iframes nor Ointment; then the shot is removed
 * with the shield's sound. Magic that touches the hero checks both
 * (Player_HandleTouchEnemyMagic, $8999), so under Ointment a shot did nothing
 * to the hero but hurt him through his shield. While Ointment runs
 * (DurationOintment bit 7 clear), a hook at Player_HandleHitByMagic's entry
 * skips it: no damage, no knockback. The shield still takes the shot, with
 * its sound. Block's hook at the same address runs first.
 *
 * None keeps state: each works from the machine at its site, so each runs
 * in Seamless's isolated rooms too, and counts only outside them. See
 * docs/GAME_INTERNALS.md. */
#include "fax_fixes.h"
#include "cyc_mod.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#ifdef CYC_WITH_SDL
#include "cyc_tcp.h"
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Bank 5 */
#define MESSAGE_SOUND_INDEX     0x86B2u   /* SoundEffect_Message_OnTick: TAX, after LDA SoundEffect_State_Counter,
                                             AND #$02; then LDA SOUNDEFFECT_MESSAGE_NOISE_LO,X, STA NOISE_LO */
/* Bank 12 */
#define SHOW_PASSWORD           0x8737u   /* IScriptAction_ShowPassword: JSR Password_GenerateStateAndReset,
                                             JSR TextBox_ClearPasswordSize, ... the character loop */
#define PASSWORD_ENTERED_CHARS  0x8764u   /* the mantra's 64 characters, "ABCDEFGH..." */
/* Bank 14 */
#define HANDLE_HIT_BY_MAGIC     0x87DCu   /* Player_HandleHitByMagic: LDX CurrentSpriteIndex,
                                             LDY CurrentSprites_Entities,X, LDA SPRITE_ENTITY_DAMAGES,Y,
                                             LDY SelectedShield, the shift, then damage and knockback */
/* RAM */
#define MESSAGE_PROCESSED_LENGTH 0x0214u  /* Message_ProcessedLength: Y into the message */
#define MESSAGE_START_ADDR       0x0218u  /* Message_StartAddr, _U at $0219 */
#define DURATION_OINTMENT        0x0427u  /* bit 7 clear while the Ointment lasts; $FF none */

static bool enabled;
static unsigned sounds, mantras, ointment_saved;   /* sites acted on, outside isolation */
static uint8_t last_pointer_was[3];                /* the mantra's: Message_StartAddr, _U, ProcessedLength */

/* What each fix decides, from the machine's values (unit-tested alone). */
static uint8_t message_sound_index(uint8_t anded) { return (uint8_t)(anded >> 1); }   /* 0 or 2: 0 or 1 */
static bool ointment_running(uint8_t duration) { return !(duration & 0x80u); }

/* SoundEffect_Message_OnTick's TAX: A is SoundEffect_State_Counter AND #$02.
 * TAX sets N and Z from the A it copies, and LDA sets them again. */
static int message_sound_hook(uint16_t addr) {
    (void)addr;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    regs.a = message_sound_index(regs.a);
    cyc_mod_set_regs(&regs);
    if (!cyc_mod_isolated()) ++sounds;
    return 0;
}

/* IScriptAction_ShowPassword's entry: the message the line-end check reads
 * becomes the mantra's characters, in bank 12, where the check reads. */
static int show_password_hook(uint16_t addr) {
    (void)addr;
    uint8_t was[3] = { cyc_mod_peek(MESSAGE_START_ADDR), cyc_mod_peek(MESSAGE_START_ADDR + 1),
                       cyc_mod_peek(MESSAGE_PROCESSED_LENGTH) };
    cyc_mod_poke(MESSAGE_START_ADDR, (uint8_t)PASSWORD_ENTERED_CHARS);
    cyc_mod_poke(MESSAGE_START_ADDR + 1, (uint8_t)(PASSWORD_ENTERED_CHARS >> 8));
    cyc_mod_poke(MESSAGE_PROCESSED_LENGTH, 0);
    if (!cyc_mod_isolated()) {
        ++mantras;
        for (int i = 0; i < 3; ++i) last_pointer_was[i] = was[i];
    }
    return 0;
}

/* Player_HandleHitByMagic, from the shield: under Ointment nothing gets
 * through. Its caller then removes the magic with the shield's sound. */
static int hit_by_magic_hook(uint16_t addr) {
    (void)addr;
    if (!ointment_running(cyc_mod_peek(DURATION_OINTMENT))) return 0;
    if (!cyc_mod_isolated()) ++ointment_saved;
    return 1;
}

static const char *const sound_id = "faxanadu.small-fixes.message-sound";
static const char *const mantra_id = "faxanadu.small-fixes.mantra";
static const char *const ointment_id = "faxanadu.small-fixes.ointment";

static void deactivate(void) {
    enabled = false;
    sounds = mantras = ointment_saved = 0;
    last_pointer_was[0] = last_pointer_was[1] = last_pointer_was[2] = 0;
    nes_mod_set_function_hook_enabled(sound_id, 0);
    nes_mod_set_function_hook_enabled(mantra_id, 0);
    nes_mod_set_function_hook_enabled(ointment_id, 0);
}
static void activate(void) {
    enabled = true;
    nes_mod_set_function_hook_enabled(sound_id, 1);
    nes_mod_set_function_hook_enabled(mantra_id, 1);
    nes_mod_set_function_hook_enabled(ointment_id, 1);
    nes_mod_set_local_only("Small fixes", 1);
}

#ifdef CYC_WITH_SDL
static void small_fixes_state(int id, const char *line) {
    (void)line;
    char fields[200];
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"sounds\":%u,\"mantras\":%u,\"mantra_pointer_was\":%u,\"ointment_saved\":%u",
             enabled ? "true" : "false", sounds, mantras,
             (unsigned)(last_pointer_was[0] | last_pointer_was[1] << 8) + last_pointer_was[2], ointment_saved);
    cyc_tcp_ok(id, fields);
}
void fax_fixes_tcp_setup(void) {
    cyc_tcp_register("small_fixes_state",
                     "Small fixes: typing-sound ticks steered, mantras typed (and where the message pointed), "
                     "shield hits Ointment kept from hurting",
                     small_fixes_state);
}
#else
void fax_fixes_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_small_fixes) {
    if (!nes_mod_register_function_entry_plugin(sound_id, MESSAGE_SOUND_INDEX, message_sound_hook) ||
        !nes_mod_register_function_entry_plugin(mantra_id, SHOW_PASSWORD, show_password_hook) ||
        !nes_mod_register_function_entry_plugin(ointment_id, HANDLE_HIT_BY_MAGIC, hit_by_magic_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.small-fixes", activate)) abort();
}
