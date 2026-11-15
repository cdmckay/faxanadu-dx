/* The Pendant fix. USA 57dd23d1, chipx86 byte-exact disassembly.
 * Player_HitSpriteWithWeapon ($8804, bank 14) builds a weapon hit's damage in
 * Temp_00: the weapon's strength from WEAPON_STRENGTHS, plus a quarter of it
 * when the Pendant bit of SpecialItems is NOT set (chipx86: "the famous
 * Pendant Bug"; the branch goes the wrong way). So the hero gets +25% until he
 * finds the Pendant, and the Pendant takes it away.
 *
 * The branch is compiled to native code, so a read override cannot flip it.
 * Both of its paths meet at @_checkGlove, before the Glove adds its bonus; a
 * hook there sets Temp_00 again the way the designers meant: the weapon's
 * strength, plus a quarter of it only with the Pendant. With the Pendant that
 * is exactly the original's damage without it, and without the Pendant it is
 * the base strength. The Glove's bonus and the enemy's HP follow as before.
 * The damage is worked out from the machine each hit, so there is nothing to
 * save or clear. See docs/GAME_INTERNALS.md. */
#include "fax_pendant.h"
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

/* Bank 14 */
#define CHECK_GLOVE      0x8884u   /* Player_HitSpriteWithWeapon's @_checkGlove: LDA DurationGlove,
                                      BMI @_updateSpriteHP. Y holds Player_CurWeapon (LDY at $886C) */
#define WEAPON_STRENGTHS 0xB7A5u   /* Hand Dagger 4, Long Sword 8, Giant Blade 12, Dragon Slayer 16 */
/* RAM */
#define TEMP_00          0x0000u   /* Temp_00: the hit's damage, so far */
#define SPECIAL_ITEMS    0x042Cu
#define BIT_PENDANT      0x02u

static bool enabled;
static unsigned hits;                                    /* hits whose damage the hook set */
static uint8_t last_weapon = 0xFF, last_strength, last_damage;
static bool last_pendant;

/* The damage before the Glove's bonus: the weapon's strength, and a quarter
 * more with the Pendant, rounded down as the game's two LSRs do. */
static uint8_t pendant_damage(uint8_t strength, uint8_t special_items) {
    return (uint8_t)(special_items & BIT_PENDANT ? strength + (strength >> 2) : strength);
}

/* @_checkGlove, reached from both sides of the Pendant branch. Only Temp_00
 * carries the damage on from here: the next instruction loads A and N/Z, and
 * C is set (CLC, SEC) before anything reads it. Y is the weapon, as the
 * Glove's lookup right after uses it. Bank 14 is mapped, so a peek reads the
 * table the game just read. */
static int check_glove_hook(uint16_t addr) {
    (void)addr;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    uint8_t strength = cyc_mod_peek((uint16_t)(WEAPON_STRENGTHS + regs.y));
    uint8_t special = cyc_mod_peek(SPECIAL_ITEMS);
    uint8_t damage = pendant_damage(strength, special);
    cyc_mod_poke(TEMP_00, damage);
    ++hits;
    last_weapon = regs.y;
    last_strength = strength;
    last_pendant = (special & BIT_PENDANT) != 0;
    last_damage = damage;
    return 0;
}

static const char *const hook_id = "faxanadu.pendant-fix.check-glove";
static void deactivate(void) {
    enabled = false;
    hits = 0;
    last_weapon = 0xFF;
    last_strength = last_damage = 0;
    last_pendant = false;
    nes_mod_set_function_hook_enabled(hook_id, 0);
}
static void activate(void) {
    enabled = true;
    nes_mod_set_function_hook_enabled(hook_id, 1);
    nes_mod_set_local_only("Pendant fix", 1);
}

#ifdef CYC_WITH_SDL
static void pendant_state(int id, const char *line) {
    (void)line;
    char fields[160];
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"hits\":%u,\"weapon\":%u,\"strength\":%u,\"pendant\":%s,\"damage\":%u",
             enabled ? "true" : "false", hits, last_weapon, last_strength, last_pendant ? "true" : "false",
             last_damage);
    cyc_tcp_ok(id, fields);
}
void fax_pendant_tcp_setup(void) {
    cyc_tcp_register("pendant_state", "Pendant fix: weapon hits it set, and the last one's damage before the Glove",
                     pendant_state);
}
#else
void fax_pendant_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_pendant) {
    if (!nes_mod_register_function_entry_plugin(hook_id, CHECK_GLOVE, check_glove_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.pendant-fix", activate)) abort();
}
