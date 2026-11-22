/* Found items. USA 57dd23d1, chipx86 byte-exact disassembly.
 *
 * With the remaster the Wing Boots, the Hour Glass and the Elixir are
 * upgrades found in the world, not bought (#138). Each is kept for good
 * (Recharging items, fax_recharge.c), and the boots and the glass run on MP,
 * so one is all the hero ever needs: buying them made no sense any more.
 * - No shop sells them. Quick shops drops their lines from the tool shops'
 *   buy lists (fax_found_unsold): Eolis's Elixir, Forepaw's and Conflate's
 *   Wing Boots, and the Hour Glass and the Elixir of the shop after Mascon,
 *   as it drops the Red Potion for the Potion Sellers (fax_shops.c).
 * - The Wing Boots come only from Zorugeriru (Trunk $3B, sprite $5C, until
 *   Quests $08): beat the boss, get the movement upgrade. The seven sprites
 *   $55 that showed one counted visit in four are gone.
 * - The Hour Glass is one fixed find: Mist $30's sprite $56 (block $32), on
 *   the ledge at the top left of the Tower of Suffer's first room, there at
 *   every visit until the hero has taken it. The shop after Mascon sold it,
 *   Mascon's nurse talks of it, and it lay there one visit in four, so it
 *   stays where its power fits: mid-game, a Q key in. Taken is kept in the
 *   file in play. The other four $56 are gone.
 * - The Elixir's finds stay as they are (Trunk $3C, every visit, a room left
 *   of the Spring of Trunk; Mist $41). The Spring of Trunk spends a charged
 *   Elixir instead of taking it: Recharging items' part.
 *
 * How, on the unchanged ROM. Each of the seven SpriteBehavior_RandomlyShowItem_*
 * (bank 14) calls Screen_Is3Of4Visits ($A529) on the sprite's first frame,
 * with X its slot: C set (3 visits of 4) and the behavior removes it (JMP
 * Sprites_Remove), C clear (the 4th) and it stays, shown while no enemy is on
 * the screen (Sprite_ShowIfNoEnemies). A hook at the routine's entry answers
 * for $55 and $56 instead: C set for one that is gone, and for the fixed Hour
 * Glass C clear while it lies there, else set; then it returns as the
 * routine's RTS would. Screen_TrackedVisitCount, which only these calls
 * move, is not counted for them, so the Mattock, the Red Potion, the Glove,
 * the Ointment and the Poison that still show one visit in four count their
 * visits among themselves. The hook keeps no state, so it also runs in
 * Seamless's isolated rooms, which run a room's actors before the hero walks
 * in. Player_HandleTouchItem ($88FB, bank 14), after Kept finds (which skips
 * a touch it refuses, the sprite left lying) and the map: the fixed Hour Glass
 * touched is taken.
 *
 * The map (fax_map.c) shows no dot for a find that is gone, and the fixed
 * Hour Glass's while it lies there (fax_found_spawn, fax_found_glass_there).
 * The state is the save-state record, and Save files keeps whether the Hour
 * Glass is taken in the file in play (on_save): written whenever the file is,
 * read back when it is continued, reset by a new game. See
 * docs/GAME_INTERNALS.md, "Items in the world". */
#include "fax_found.h"
#include "fax_save.h"
#include "cyc_mod.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#ifdef CYC_WITH_SDL
#include "cyc_tcp.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Bank 14 */
#define PLAYER_HANDLE_TOUCH_ITEM 0x88FBu  /* LDA CurrentSprites_Entities,X, JSR Player_PickUp, ..., the slot emptied */
#define SCREEN_IS_3_OF_4_VISITS  0xA529u  /* INC Screen_TrackedVisitCount, LDA, CMP #$04, BCC: C set for 3 of 4 */
/* RAM */
#define AREA_CURRENT_AREA   0x0024u
#define AREA_CURRENT_SCREEN 0x0063u
#define SPRITE_ENTITIES     0x02CCu   /* CurrentSprites_Entities: 8 slots, $FF empty */
#define ITEM_INVENTORY      0x03ADu   /* the Items list, NumberOfItems entries */
#define SELECTED_ITEM       0x03C1u   /* the Items' selection, in no list */
#define NUMBER_OF_ITEMS     0x03C6u
/* Values */
enum { SLOTS = 8, MAX_ITEMS = 8, P_C = 0x01 };
enum { WING_BOOTS_FIND = 0x55, HOUR_GLASS_FIND = 0x56 };   /* SpriteBehavior_RandomlyShowItem_55, _51 */
enum { ITEM_HOUR_GLASS = 0x0D };                            /* in the Items list */
enum { MIST = 2, GLASS_SCREEN = 0x30 };                     /* the Tower of Suffer's first room */

const uint8_t fax_found_unsold[FAX_FOUND_UNSOLD] = {
    0x8F,   /* ITEM_WING_BOOTS: Forepaw 2800, Conflate 5500 */
    0x8D,   /* ITEM_HOUR_GLASS: the shop after Mascon 5600 */
    0x92,   /* SPECIAL_ELIXIR: Eolis 320, the shop after Mascon 4300 */
};

static bool enabled;
static uint8_t taken;              /* the fixed Hour Glass: the hero has picked it up */
static unsigned gone, shown, away, takes;   /* for TCP: finds removed, the glass shown, kept away, taken */
static struct { int entity, area, screen, rule, left; } last = { -1, -1, -1, -1, -1 };   /* the last answer */

/* ---- the rules, on plain values ---- */

FaxFoundSpawn fax_found_spawn(unsigned area, unsigned screen, unsigned entity) {
    if (entity == WING_BOOTS_FIND) return FAX_FOUND_GONE;
    if (entity != HOUR_GLASS_FIND) return FAX_FOUND_AS_IS;
    return area == MIST && screen == GLASS_SCREEN ? FAX_FOUND_FIXED : FAX_FOUND_GONE;
}

/* Whether a find left by the rule lies there this visit: C clear for
 * Screen_Is3Of4Visits' answer. */
static bool lies(FaxFoundSpawn rule, bool glass_there) { return rule == FAX_FOUND_FIXED && glass_there; }

/* ---- the machine ---- */

static uint8_t q(uint16_t addr) { return cyc_mod_peek(addr); }

/* The hero has an Hour Glass: selected, or in the first NumberOfItems slots. */
static bool holds_glass(void) {
    if (q(SELECTED_ITEM) == ITEM_HOUR_GLASS) return true;
    unsigned n = q(NUMBER_OF_ITEMS);
    if (n > MAX_ITEMS) n = MAX_ITEMS;
    for (unsigned i = 0; i < n; ++i) if (q((uint16_t)(ITEM_INVENTORY + i)) == ITEM_HOUR_GLASS) return true;
    return false;
}

bool fax_found_on(void) { return enabled; }
bool fax_found_glass_there(void) { return !taken && !holds_glass(); }

/* Screen_Is3Of4Visits, from a random-show item's behavior with X its slot:
 * for the Wing Boots and the Hour Glass, this answers, and returns. It reads
 * only, so in Seamless's isolated rooms too. */
static int visits_hook(uint16_t addr) {
    (void)addr;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    uint8_t area = q(AREA_CURRENT_AREA), screen = q(AREA_CURRENT_SCREEN);
    uint8_t entity = q((uint16_t)(SPRITE_ENTITIES + (regs.x & (SLOTS - 1))));
    FaxFoundSpawn rule = fax_found_spawn(area, screen, entity);
    if (rule == FAX_FOUND_AS_IS) return 0;
    bool there = lies(rule, fax_found_glass_there());
    regs.p = there ? (uint8_t)(regs.p & ~P_C) : (uint8_t)(regs.p | P_C);
    cyc_mod_set_regs(&regs);
    if (!cyc_mod_isolated()) {
        if (rule == FAX_FOUND_GONE) ++gone;
        else if (there) ++shown;
        else ++away;
        last.entity = entity; last.area = area; last.screen = screen; last.rule = rule; last.left = there;
    }
    return 1;                                   /* as its RTS: the counter not moved */
}

/* Player_HandleTouchItem, X the slot: the fixed Hour Glass is picked up (Kept
 * finds, before this, skips a touch it refuses, so this one goes through). */
static int touch_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) return 0;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    if (regs.x >= SLOTS) return 0;
    uint8_t entity = q((uint16_t)(SPRITE_ENTITIES + regs.x));
    if (fax_found_spawn(q(AREA_CURRENT_AREA), q(AREA_CURRENT_SCREEN), entity) != FAX_FOUND_FIXED) return 0;
    taken = 1;
    ++takes;
    return 0;
}

/* ---- the file in play (Save files, fax_save.h) ---- */

#define FIELD_GLASS "found.hourglass"   /* "taken", or no field */

static void on_save(FaxSaveEvent event) {
    if (!enabled) return;
    if (event == FAX_SAVE_NEW_GAME) {
        taken = 0;
    } else if (event == FAX_SAVE_CONTINUED) {
        const char *t = fax_save_get(FIELD_GLASS);
        taken = t && !strcmp(t, "taken");
    } else {
        fax_save_set(FIELD_GLASS, taken ? "taken" : NULL);
    }
}

/* ---- the save-state record ---- */

/* Version 1: [0] 1, [1] the Hour Glass taken. */
enum { RECORD_VERSION = 1, RECORD = 2 };
static int state_get(uint8_t *buf, int cap) {
    if (cap < RECORD) return -1;
    buf[0] = RECORD_VERSION;
    buf[1] = taken;
    return RECORD;
}
static int state_validate(const uint8_t *buf, int len) {
    return len == 0 || (len == RECORD && buf[0] == RECORD_VERSION && buf[1] <= 1);
}
/* Length 0 (a state from before the feature): not taken. */
static int state_set(const uint8_t *buf, int len) {
    if (!state_validate(buf, len)) return 0;
    taken = len ? buf[1] : 0;
    return 1;
}

/* ---- activation ---- */

static const char *const hook_ids[] = { "faxanadu.found-items.visits", "faxanadu.found-items.touch" };
static void hooks(bool on) {
    for (unsigned i = 0; i < sizeof hook_ids / sizeof hook_ids[0]; ++i) nes_mod_set_function_hook_enabled(hook_ids[i], on);
}
static void deactivate(void) {
    enabled = false;
    taken = 0;
    gone = shown = away = takes = 0;
    last.entity = last.area = last.screen = last.rule = last.left = -1;
    hooks(false);
}
static void activate(void) {
    enabled = true;
    hooks(true);
    nes_mod_set_local_only("Found items", 1);
}

#ifdef CYC_WITH_SDL
static void found_state(int id, const char *line) {
    (void)line;
    char fields[512];
    /* taken: the fixed Hour Glass picked up (the file's); there: it lies at Mist $30; counts of finds removed, the
     * glass shown and kept away (outside Seamless's isolated rooms), and taken; the last answer: the entity, its
     * place, the rule (1 gone, 2 the fixed glass) and whether it stayed. */
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"taken\":%s,\"there\":%s,\"gone\":%u,\"shown\":%u,\"away\":%u,\"takes\":%u,"
             "\"last\":{\"entity\":%d,\"area\":%d,\"screen\":%d,\"rule\":%d,\"left\":%d}",
             enabled ? "true" : "false", taken ? "true" : "false", fax_found_glass_there() ? "true" : "false", gone,
             shown, away, takes, last.entity, last.area, last.screen, last.rule, last.left);
    cyc_tcp_ok(id, fields);
}
void fax_found_tcp_setup(void) {
    cyc_tcp_register("found_state",
                     "Found items: the fixed Hour Glass taken and lying there, finds removed, the glass shown and "
                     "kept away, the last answer",
                     found_state);
}
#else
void fax_found_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_found) {
    if (!nes_mod_register_function_entry_plugin(hook_ids[0], SCREEN_IS_3_OF_4_VISITS, visits_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[1], PLAYER_HANDLE_TOUCH_ITEM, touch_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.found-items", activate) ||
        !nes_mod_register_savestate_hook("faxanadu.found-items", state_get, state_set) ||
        !nes_mod_register_savestate_validator("faxanadu.found-items", state_validate) || !fax_save_listen(on_save))
        abort();
}
