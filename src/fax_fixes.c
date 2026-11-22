/* Small fixes: three of the original's small bugs (#39, #82). USA 57dd23d1,
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
 * Iframes, Ointment and the shield. Enemy magic that hits the shield runs
 * Player_HandleShieldHitByMagic ($87CB, bank 14): Player_HandleHitByMagic
 * ($87DC) lets SPRITE_ENTITY_DAMAGES >> (shield + 1) through, with knockback
 * and iframes, checking neither iframes nor Ointment; then the shot is removed
 * with the shield's sound. Magic that touches the hero checks both
 * (Player_HandleTouchEnemyMagic, $8999: Player_InvincibilityPhase, then
 * DurationOintment), so under Ointment a shot did nothing to the hero but hurt
 * him through his shield (#39), and a second shot on the shield inside the 60
 * frames after a hit hurt him again, with the iframes started over and more
 * knockback (#82). A hook at Player_HandleHitByMagic's entry skips it while
 * either runs, in the touch handler's order (Player_InvincibilityPhase not 0,
 * then DurationOintment bit 7 clear): no damage, no knockback, the iframes
 * left counting down. The shield still takes the shot, with its sound.
 * Block's hook at the same address runs first.
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
/* Bank 14 */
#define HANDLE_HIT_BY_MAGIC     0x87DCu   /* Player_HandleHitByMagic: LDX CurrentSpriteIndex,
                                             LDY CurrentSprites_Entities,X, LDA SPRITE_ENTITY_DAMAGES,Y,
                                             LDY SelectedShield, the shift, then damage and knockback */
/* RAM */
#define INVINCIBILITY_PHASE      0x00ADu  /* Player_InvincibilityPhase: $3C on a hit, counted down each frame */
#define DURATION_OINTMENT        0x0427u  /* bit 7 clear while the Ointment lasts; $FF none */

static bool enabled;
static unsigned sounds, iframes_saved, ointment_saved;   /* sites acted on, outside isolation */

/* What each fix decides, from the machine's values (unit-tested alone). */
static uint8_t message_sound_index(uint8_t anded) { return (uint8_t)(anded >> 1); }   /* 0 or 2: 0 or 1 */
static bool ointment_running(uint8_t duration) { return !(duration & 0x80u); }

/* What keeps a shot on the shield from hurting, checked as
 * Player_HandleTouchEnemyMagic checks a touch: iframes, then Ointment. */
enum { LET_THROUGH, IFRAMES, OINTMENT };
static int shield_guard(uint8_t phase, uint8_t duration) {
    if (phase) return IFRAMES;
    return ointment_running(duration) ? OINTMENT : LET_THROUGH;
}

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

/* Player_HandleHitByMagic, from the shield: during iframes or under
 * Ointment nothing gets through. Its caller then removes the magic with the
 * shield's sound. */
static int hit_by_magic_hook(uint16_t addr) {
    (void)addr;
    int guard = shield_guard(cyc_mod_peek(INVINCIBILITY_PHASE), cyc_mod_peek(DURATION_OINTMENT));
    if (guard == LET_THROUGH) return 0;
    if (!cyc_mod_isolated()) {
        if (guard == IFRAMES) ++iframes_saved;
        else ++ointment_saved;
    }
    return 1;
}

static const char *const sound_id = "faxanadu.small-fixes.message-sound";
static const char *const shield_id = "faxanadu.small-fixes.shield-hit";

static void deactivate(void) {
    enabled = false;
    sounds = iframes_saved = ointment_saved = 0;
    nes_mod_set_function_hook_enabled(sound_id, 0);
    nes_mod_set_function_hook_enabled(shield_id, 0);
}
static void activate(void) {
    enabled = true;
    nes_mod_set_function_hook_enabled(sound_id, 1);
    nes_mod_set_function_hook_enabled(shield_id, 1);
    nes_mod_set_local_only("Small fixes", 1);
}

#ifdef CYC_WITH_SDL
static void small_fixes_state(int id, const char *line) {
    (void)line;
    char fields[240];
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"sounds\":%u,\"iframes_saved\":%u,\"ointment_saved\":%u",
             enabled ? "true" : "false", sounds, iframes_saved, ointment_saved);
    cyc_tcp_ok(id, fields);
}
void fax_fixes_tcp_setup(void) {
    cyc_tcp_register("small_fixes_state",
                     "Small fixes: typing-sound ticks steered, shield hits iframes and Ointment kept from hurting",
                     small_fixes_state);
}
#else
void fax_fixes_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_small_fixes) {
    if (!nes_mod_register_function_entry_plugin(sound_id, MESSAGE_SOUND_INDEX, message_sound_hook) ||
        !nes_mod_register_function_entry_plugin(shield_id, HANDLE_HIT_BY_MAGIC, hit_by_magic_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.small-fixes", activate)) abort();
}
