/* Death keeps gold and experience. USA 57dd23d1, chipx86 byte-exact disassembly.
 * Dying runs Player_HandleDeath, which far-calls Player_SetInitialExpAndGold
 * (bank 12) through MMC1_LoadBankAndJump: experience and gold drop to the
 * starting amounts for the hero's title. Game_ShowStartScreen makes the same
 * far call after a mantra, where it must still set them, since a mantra holds
 * neither. The hook at the routine's entry tells the two apart by the return
 * address in the far call's frame, and returns at once for a death. The rest
 * of death is the game's: the Guru's line, full HP and MP, the Mattock bit and
 * timed items cleared, and the furthest temple. The decision is made from the
 * machine's own stack each time, so there is nothing to save or clear.
 * See docs/GAME_INTERNALS.md. */
#include "fax_death.h"
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

/* Bank 12 */
#define PLAYER_SET_INITIAL_EXP_AND_GOLD 0x95B1u   /* LDA PlayerTitle, STA NextPlayerTitle, BEQ @_isNovice */
/* Fixed bank */
#define MMC1_POP_BANK  0xF8C6u   /* the far call's trampoline: PLA, TAX, JMP MMC1_UpdateROMBank */
#define DEATH_RESUME   0xD9A0u   /* Player_HandleDeath's @_afterSetExpGoldFarJump: JSR Screen_FadeToBlack */
#define START_RESUME   0xFC95u   /* Game_ShowStartScreen's @_afterSetExpGoldFarJump: JMP Player_Spawn */

static bool enabled;
static unsigned kept, passed;    /* deaths whose reset was skipped; calls left to the game */

static uint16_t stack_word(uint8_t s, unsigned at) {   /* the 6502 stack wraps within page 1 */
    return (uint16_t)(cyc_mod_peek((uint16_t)(0x100u | (uint8_t)(s + at))) |
                      cyc_mod_peek((uint16_t)(0x100u | (uint8_t)(s + at + 1))) << 8);
}

/* Where a far call to the routine at hand resumes, or 0 if this entry was not
 * a far call. MMC1_LoadBankAndJump enters the target with an RTS, leaving
 * above S: MMC1_PopBank - 1 (where the target's RTS goes), the caller's bank,
 * and the address after the caller's inline bytes, - 1. */
static uint16_t far_call_resume(uint8_t s) {
    if (stack_word(s, 1) != MMC1_POP_BANK - 1) return 0;
    return (uint16_t)(stack_word(s, 4) + 1);
}

/* Player_SetInitialExpAndGold: skip it when Player_HandleDeath called it.
 * Returning goes to MMC1_PopBank, which restores bank 14 and returns to
 * Screen_FadeToBlack, as the routine's own RTS would. */
static int set_exp_gold_hook(uint16_t addr) {
    (void)addr;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    if (far_call_resume(regs.s) == DEATH_RESUME) { ++kept; return 1; }
    ++passed;
    return 0;
}

static const char *const hook_id = "faxanadu.death-keeps-gold.set-exp-gold";
static void deactivate(void) {
    enabled = false;
    kept = passed = 0;
    nes_mod_set_function_hook_enabled(hook_id, 0);
}
static void activate(void) {
    enabled = true;
    nes_mod_set_function_hook_enabled(hook_id, 1);
    nes_mod_set_local_only("Death keeps gold", 1);
}

#ifdef CYC_WITH_SDL
static void death_state(int id, const char *line) {
    (void)line;
    char fields[96];
    snprintf(fields, sizeof fields, "\"enabled\":%s,\"kept\":%u,\"passed\":%u", enabled ? "true" : "false", kept,
             passed);
    cyc_tcp_ok(id, fields);
}
void fax_death_tcp_setup(void) {
    cyc_tcp_register("death_state", "Death keeps gold: deaths that kept gold and experience, other calls passed",
                     death_state);
}
#else
void fax_death_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_death) {
    if (!nes_mod_register_function_entry_plugin(hook_id, PLAYER_SET_INITIAL_EXP_AND_GOLD, set_exp_gold_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.death-keeps-gold", activate)) abort();
}
