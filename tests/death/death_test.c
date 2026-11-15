/* Unit tests for death keeping gold and experience (fax_death.c), without the
 * emulator or a ROM. The module runs against a model of the machine: CPU RAM,
 * the registers, and MMC1_LoadBankAndJump and MMC1_PopBank played step by step
 * as the disassembly has them, so the tests check the far call's frame rather
 * than restate the module's constants. */
#include "fax_death.c"
#include <string.h>

static unsigned checks;
#define CHECK(test) do{++checks;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(1);}}while(0)

/* ---- the machine ---- */
static uint8_t ram[0x800];
static CycModRegs cpu;
static uint8_t bank;                             /* CurrentROMBank */
static int hook_on = -1;                         /* the hook's enabled state, as last set */

uint8_t cyc_mod_peek(uint16_t addr) { return addr < 0x2000 ? ram[addr & 0x7FF] : 0; }
void cyc_mod_regs(CycModRegs *out) { *out = cpu; }
int nes_mod_register_function_entry_plugin(const char *id, uint16_t addr, NESModFunctionEntryCallback cb) {
    return id && !strcmp(id, hook_id) && addr == PLAYER_SET_INITIAL_EXP_AND_GOLD && cb == set_exp_gold_hook;
}
int nes_mod_set_function_hook_enabled(const char *id, int on) {
    if (strcmp(id, hook_id)) return 0;
    hook_on = on;
    return 1;
}
int nes_mod_register_reset_callback(NESModActivationCallback cb) { return cb == deactivate; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    return !strcmp(id, "faxanadu.death-keeps-gold") && cb == activate;
}
void nes_mod_set_local_only(const char *name, int required) { (void)name; (void)required; }

static void push(uint8_t v) { ram[0x100 | cpu.s] = v; cpu.s = (uint8_t)(cpu.s - 1); }
static uint8_t pull(void) { cpu.s = (uint8_t)(cpu.s + 1); return ram[0x100 | cpu.s]; }
static uint16_t rts(void) { uint8_t lo = pull(); return (uint16_t)((lo | pull() << 8) + 1); }

/* JSR MMC1_LoadBankAndJump at `jsr` (its three inline bytes follow: bank 12,
 * then the target - 1), run as the ROM does, up to its RTS into the target. */
static void far_call(uint16_t jsr, uint8_t s) {
    cpu.s = s;
    uint16_t last = (uint16_t)(jsr + 2);         /* JSR pushes the address of its own last byte */
    push((uint8_t)(last >> 8)); push((uint8_t)last);
    uint8_t lo = pull(), x = pull();             /* PLA Temp_Int24, PLA Temp_Int24_M, TAX */
    uint8_t ret = (uint8_t)(lo + 3);             /* past the inline bytes, - 1 */
    if (ret < lo) ++x;                           /* BCC, INX */
    push(x); push(ret); push(bank);              /* the caller's resume address, then CurrentROMBank */
    bank = 12;                                   /* JSR MMC1_UpdateROMBank: returns, stack balanced */
    push((uint8_t)(MMC1_POP_BANK >> 8)); push((uint8_t)(MMC1_POP_BANK - 1));
    uint16_t target = PLAYER_SET_INITIAL_EXP_AND_GOLD - 1;
    push((uint8_t)(target >> 8)); push((uint8_t)target);
    CHECK(rts() == PLAYER_SET_INITIAL_EXP_AND_GOLD);
}
/* The engine's return from a handled routine, then MMC1_PopBank: PLA, TAX,
 * JMP MMC1_UpdateROMBank, whose RTS goes back to the caller. */
static uint16_t return_to_caller(void) {
    CHECK(rts() == MMC1_POP_BANK);
    bank = pull();
    return rts();
}

static void death_keeps_them(void) {
    deactivate(); activate();
    CHECK(hook_on == 1);
    /* Player_HandleDeath set S to $FF itself, and it is back there by now. */
    bank = 14;
    far_call(0xD99A, 0xFF);
    CHECK(cpu.s == 0xFA);
    CHECK(far_call_resume(cpu.s) == DEATH_RESUME);
    CHECK(set_exp_gold_hook(PLAYER_SET_INITIAL_EXP_AND_GOLD) == 1);
    CHECK(kept == 1 && passed == 0);
    /* Skipping it is safe: the death path goes on to Screen_FadeToBlack in
     * bank 14 with the stack where the routine's own RTS would leave it. */
    CHECK(return_to_caller() == DEATH_RESUME && bank == 14 && cpu.s == 0xFF);
}

static void a_mantra_sets_them(void) {
    deactivate(); activate();
    bank = 14;
    far_call(0xFC8F, 0xFF);                      /* Game_ShowStartScreen, after PasswordScreen_Show */
    CHECK(far_call_resume(cpu.s) == START_RESUME);
    CHECK(set_exp_gold_hook(PLAYER_SET_INITIAL_EXP_AND_GOLD) == 0);
    CHECK(kept == 0 && passed == 1);
}

static void anything_else_runs_it(void) {
    deactivate(); activate();
    bank = 15;
    far_call(0xC0FB, 0xF0);                      /* a far call whose resume address carries into the next page */
    CHECK(far_call_resume(cpu.s) == 0xC101);
    CHECK(set_exp_gold_hook(PLAYER_SET_INITIAL_EXP_AND_GOLD) == 0);
    /* A plain JSR from the death path's own bytes is not the far call. */
    cpu.s = 0xFF;
    push(0xD9); push(0x9C);
    CHECK(far_call_resume(cpu.s) == 0);
    CHECK(set_exp_gold_hook(PLAYER_SET_INITIAL_EXP_AND_GOLD) == 0);
    CHECK(kept == 0 && passed == 2);
}

static void the_stack_wraps(void) {
    /* A frame that straddles $0100/$01FF is read the way the 6502 pushed it. */
    deactivate(); activate();
    bank = 14;
    far_call(0xD99A, 0x03);
    CHECK(cpu.s == 0xFE);
    CHECK(set_exp_gold_hook(PLAYER_SET_INITIAL_EXP_AND_GOLD) == 1);
    CHECK(return_to_caller() == DEATH_RESUME && cpu.s == 0x03);
}

static void off_means_off(void) {
    activate(); kept = 3;
    deactivate();
    CHECK(hook_on == 0 && !enabled && kept == 0 && passed == 0);
}

int main(void) {
    death_keeps_them();
    a_mantra_sets_them();
    anything_else_runs_it();
    the_stack_wraps();
    off_means_off();
    printf("death_test: %u checks passed\n", checks);
    return 0;
}
