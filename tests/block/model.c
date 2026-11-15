/* The model machine for Block's and Sugata's flash's unit tests (model.h). */
#include "model.h"
#include "cyc_host_extras.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#include <string.h>

unsigned checks;
uint8_t ram[0x800], rom[16][0x4000];
unsigned mapped = 14;
CycModRegs cpu = { .s = 0xF0, .p = 0x24 };
bool isolated_scope, saves_on;
uint8_t sounds[64];
unsigned sound_count;

/* ---- memory ---- */
uint8_t cyc_mod_peek(uint16_t addr) {
    if (addr < 0x2000) return ram[addr & 0x7FF];
    if (addr >= 0x8000 && addr < 0xC000) return rom[mapped][addr - 0x8000];
    return 0;
}
bool cyc_mod_poke(uint16_t addr, uint8_t value) {
    if (addr >= 0x2000) return false;
    ram[addr & 0x7FF] = value;
    return true;
}
void cyc_mod_regs(CycModRegs *out) { *out = cpu; }
void cyc_mod_set_regs(const CycModRegs *in) { cpu = *in; }
bool cyc_mod_isolated(void) { return isolated_scope; }

static struct { uint32_t offset; uint8_t value; } overrides[64];
static unsigned override_count;
bool cyc_mod_set_prg_read_override(uint32_t offset, uint8_t compare, uint8_t value) {
    if (compare != rom[offset / 0x4000][offset % 0x4000]) return false;
    for (unsigned i = 0; i < override_count; ++i)
        if (overrides[i].offset == offset) { overrides[i].value = value; return true; }
    if (override_count == 64) return false;
    overrides[override_count].offset = offset;
    overrides[override_count++].value = value;
    return true;
}
uint8_t rom_read(unsigned bank, uint16_t addr) {
    uint32_t offset = bank * 0x4000u + (addr - 0x8000u);
    for (unsigned i = 0; i < override_count; ++i) if (overrides[i].offset == offset) return overrides[i].value;
    return rom[bank][addr - 0x8000];
}
unsigned overrides_used(void) { return override_count; }

bool cyc_host_saves_enabled(void) { return saves_on; }

/* ---- the stack and routines ---- */
void push16(uint16_t v) {
    ram[0x100 | cpu.s] = (uint8_t)(v >> 8); cpu.s = (uint8_t)(cpu.s - 1);
    ram[0x100 | cpu.s] = (uint8_t)v;        cpu.s = (uint8_t)(cpu.s - 1);
}
uint16_t pop16(void) {
    cpu.s = (uint8_t)(cpu.s + 1); uint16_t lo = ram[0x100 | cpu.s];
    cpu.s = (uint8_t)(cpu.s + 1); uint16_t hi = ram[0x100 | cpu.s];
    return (uint16_t)(lo | hi << 8);
}

static struct { uint16_t addr; ModelCode code; } routines[32];
static unsigned routine_count;
void model_routine(uint16_t addr, ModelCode code) {
    for (unsigned i = 0; i < routine_count; ++i) if (routines[i].addr == addr) { routines[i].code = code; return; }
    routines[routine_count].addr = addr;
    routines[routine_count++].code = code;
}
static void run(uint16_t addr) {
    for (unsigned i = 0; i < routine_count; ++i)
        if (routines[i].addr == addr) { routines[i].code(); return; }
    fprintf(stderr, "FAIL: nothing modeled at $%04X\n", addr);
    exit(1);
}

/* Sound_PlayEffect, from the fixed bank: memory only, keeps X and Y. */
static void sound_play_effect(void) {
    if (sound_count < sizeof sounds) sounds[sound_count++] = cpu.a;
}
uint8_t last_sound(void) { return sound_count ? sounds[sound_count - 1] : 0xFF; }

bool cyc_mod_call_commit(uint16_t routine, CycModRegs *regs) {
    CHECK(!isolated_scope);
    CycModRegs saved = cpu;
    cpu = *regs;
    push16(0xFFEF);                       /* cyc_mod_call's JSR */
    if (routine == 0xD0E4) { sound_play_effect(); pop16(); }
    else { run(routine); pop16(); }
    CHECK(cpu.s == saved.s);              /* it returned to the caller's frame */
    *regs = cpu;
    return true;
}

/* ---- hook sites ---- */
static struct { const char *id; uint16_t addr; NESModFunctionEntryCallback cb; int on; } hooks_[32];
static unsigned hook_count;
int nes_mod_register_function_entry_plugin(const char *id, uint16_t addr, NESModFunctionEntryCallback cb) {
    if (hook_count == 32) return 0;
    hooks_[hook_count].id = id; hooks_[hook_count].addr = addr; hooks_[hook_count].cb = cb; hooks_[hook_count++].on = 0;
    return 1;
}
int nes_mod_set_function_hook_enabled(const char *id, int on) {
    for (unsigned i = 0; i < hook_count; ++i) if (!strcmp(hooks_[i].id, id)) { hooks_[i].on = on; return 1; }
    return 0;
}
bool hook_enabled(const char *id) {
    for (unsigned i = 0; i < hook_count; ++i) if (!strcmp(hooks_[i].id, id)) return hooks_[i].on;
    return false;
}

bool sites(uint16_t addr, uint16_t back, const char *const *ids, unsigned n) {
    uint8_t s = cpu.s;
    push16((uint16_t)(back - 1));
    bool handled = false;
    for (unsigned k = 0; k < n && !handled; ++k)
        for (unsigned i = 0; i < hook_count; ++i)
            if (!strcmp(hooks_[i].id, ids[k])) {
                CHECK(hooks_[i].addr == addr);
                if (hooks_[i].on && hooks_[i].cb(addr)) handled = true;
                break;
            }
    if (!handled) { CHECK(pop16() == (uint16_t)(back - 1)); CHECK(cpu.s == s); return true; }
    for (;;) {
        uint16_t to = (uint16_t)(pop16() + 1);   /* the site's RTS */
        if (to == back) break;
        run(to);                                  /* which ends in an RTS of its own */
    }
    CHECK(cpu.s == s);
    return false;
}

/* ---- activation and save states ---- */
static NESModActivationCallback resets[8];
static unsigned reset_count;
static struct { const char *id; NESModActivationCallback cb; } plugins[8];
static unsigned plugin_count;
int nes_mod_register_reset_callback(NESModActivationCallback cb) { resets[reset_count++] = cb; return 1; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    plugins[plugin_count].id = id; plugins[plugin_count++].cb = cb;
    return 1;
}
void nes_mod_set_local_only(const char *name, int required) { (void)name; (void)required; }
void model_reset(void) { for (unsigned i = 0; i < reset_count; ++i) resets[i](); }
void model_activate(const char *plugin) {
    for (unsigned i = 0; i < plugin_count; ++i) if (!strcmp(plugins[i].id, plugin)) { plugins[i].cb(); return; }
    CHECK(!"no such plugin");
}

static struct { const char *id; NESModSavestateGet get; NESModSavestateSet set; NESModSavestateValidate ok; } states[8];
static unsigned state_count;
int nes_mod_register_savestate_hook(const char *id, NESModSavestateGet get, NESModSavestateSet set) {
    states[state_count].id = id; states[state_count].get = get; states[state_count++].set = set;
    return 1;
}
int nes_mod_register_savestate_validator(const char *id, NESModSavestateValidate validate) {
    for (unsigned i = 0; i < state_count; ++i) if (!strcmp(states[i].id, id)) { states[i].ok = validate; return 1; }
    return 0;
}
int model_state_get(const char *id, uint8_t *buf, int cap) {
    for (unsigned i = 0; i < state_count; ++i) if (!strcmp(states[i].id, id)) return states[i].get(buf, cap);
    return -2;
}
int model_state_set(const char *id, const uint8_t *buf, int len) {
    for (unsigned i = 0; i < state_count; ++i)
        if (!strcmp(states[i].id, id)) return states[i].ok(buf, len) && states[i].set(buf, len);
    return 0;
}
