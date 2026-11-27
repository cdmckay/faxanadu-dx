/* Taller dagger. USA 57dd23d1, chipx86 byte-exact disassembly.
 * The Hand Dagger's hit box is half as tall as every other weapon's, so it
 * swings over low enemies that the swords hit, such as the Zozura outside
 * Eolis. Each frame Player_DrawWeapon (bank 14) loads the equipped weapon's
 * reach from MAYBE_WEAPON_RANGES into Maybe_WeaponRange_X/Y, and
 * Sprites_IsSpriteOutOfWeaponRange tests each enemy against the box that
 * reach spans right of and below the weapon. Serving the dagger's Y reach as
 * the swords' $08 makes its box as tall as theirs, reaching 4 pixels lower;
 * its X reach stays $08, half theirs. The ROM image is never changed (save
 * states hash it): the byte is served through a read override. Overrides are
 * not machine state, so the save-state hook serves it again after every load.
 * See docs/GAME_INTERNALS.md. */
#include "fax_dagger.h"
#include "cyc_mod.h"
#include "mod_runtime.h"
#include "mod_savestate.h"
#ifdef CYC_WITH_SDL
#include "cyc_tcp.h"
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Bank 14: MAYBE_WEAPON_RANGES, an (X, Y) reach per weapon, read only by
 * Player_DrawWeapon. Stock: dagger 08 04, Long Sword, Giant Blade and Dragon
 * Slayer 10 08. */
#define WEAPON_RANGES  0xB897u
#define DAGGER_REACH_Y (WEAPON_RANGES + 1) /* $B898 */
enum { STOCK_REACH_Y = 0x04, TALL_REACH_Y = 0x08 };

static bool enabled, served, failed;

static uint32_t prg(unsigned bank, uint16_t addr) { return bank * 16384u + (addr - 0x8000u); }

/* The override matches only bank 14's byte, and only while it holds the stock
 * value, so another ROM layout is refused rather than patched. */
static bool serve(bool tall) {
    return cyc_mod_set_prg_read_override(prg(14, DAGGER_REACH_Y), STOCK_REACH_Y, tall ? TALL_REACH_Y : STOCK_REACH_Y);
}

static void deactivate(void) {
    if (served) serve(false); /* back to the stock byte: overrides cannot be removed one at a time */
    enabled = served = failed = false;
}
static void activate(void) {
    enabled = true;
    nes_mod_set_local_only("Taller dagger", 1); /* a peer without it would play other rules */
}

/* Serve the reach once the cart is loaded: at power-on, before any save state
 * is loaded, or on the first frame after activation in play. */
void fax_dagger_step(void) {
    if (!enabled || served || failed) return;
    if (!serve(true)) {
        fprintf(stderr, "[dagger] unexpected ROM layout; the taller dagger is off\n");
        failed = true;
        return;
    }
    served = true;
}

/* The Hand Dagger's Y reach as the game reads it: the taller one once it is
 * served, else the stock byte. Punches give the fist the same (fax_punch.c). */
uint8_t fax_dagger_reach_y(void) { return served ? TALL_REACH_Y : STOCK_REACH_Y; }

/* The feature keeps no state of its own, so its record is empty. A load calls
 * the setter whether or not the save has a record, and that is where the
 * reach is served again. */
static int state_get(uint8_t *buf, int cap) {
    (void)buf;
    (void)cap;
    return 0;
}
static int state_validate(const uint8_t *buf, int len) {
    (void)buf;
    return len == 0;
}
static int state_set(const uint8_t *buf, int len) {
    if (!state_validate(buf, len)) return 0;
    if (served && !serve(true)) {
        fprintf(stderr, "[dagger] could not serve the reach after a load; the taller dagger is off\n");
        served = false;
        failed = true;
    }
    return 1;
}

#ifdef CYC_WITH_SDL
static void dagger_state(int id, const char *line) {
    (void)line;
    char fields[96];
    snprintf(fields, sizeof fields, "\"enabled\":%s,\"served\":%s,\"failed\":%s", enabled ? "true" : "false",
             served ? "true" : "false", failed ? "true" : "false");
    cyc_tcp_ok(id, fields);
}
void fax_dagger_tcp_setup(void) {
    cyc_tcp_register("dagger_state", "Taller dagger: on, and whether the dagger's reach is served", dagger_state);
}
#else
void fax_dagger_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_dagger) {
    if (!nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.taller-dagger", activate) ||
        !nes_mod_register_savestate_hook("faxanadu.taller-dagger", state_get, state_set) ||
        !nes_mod_register_savestate_validator("faxanadu.taller-dagger", state_validate))
        abort();
}
