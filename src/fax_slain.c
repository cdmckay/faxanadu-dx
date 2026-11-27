/* Slain by. USA 57dd23d1, chipx86 byte-exact disassembly.
 * When the hero dies, the zone names banner (fax_banner.c) says what killed
 * him, "Slain by a Sugata", while he dissolves. The game never names its
 * enemies; this teaches the names the way zone names teach places.
 *
 * - Every loss of HP goes through Player_ReduceHP (fixed bank), from four
 *   places: Player_ApplyDamage (an enemy's or a shot's touch), Player_
 *   HandleHitByMagic (a shot on the shield), Sugata's flash, and picking up
 *   the Poison. At its entry the hook works out whether this hit kills: HP
 *   below the damage (Arg_PlayerHealthDelta) and no Elixir to save him. Only
 *   a killing hit names anything, so a death with no clear source (nothing
 *   took his HP) shows nothing. The Poison and the flash are told by the
 *   return address their JSR left; every other hit by CurrentSpriteIndex,
 *   the sprite whose collision or behavior is running.
 * - A shot or a falling rock has no name of its own: it is named by the
 *   sprite that spawned it. Every sprite spawned by another goes through
 *   Sprite_Maybe_ResetState (bank 14) with X the spawner and Y the new slot,
 *   and the hook there keeps each slot's spawner. A slot forgets it once its
 *   sprite is gone. Where no spawn was seen (with Seamless on, a neighboring
 *   room's; a save state from before this feature), the shot is named by the
 *   only kind of sprite on screen that casts it, if there is one.
 * - The line takes the banner's place as the hit lands and fades out before
 *   the Guru speaks: the dissolve lasts 256 frames. It hides behind a box
 *   (fax_text_box_showing), as zone names do. Both hooks only observe.
 *
 * The line showing and the spawners live in this feature's save-state record.
 * See docs/GAME_INTERNALS.md, "What killed the hero". */
#include "fax_slain.h"
#include "fax_banner.h"
#include "fax_text.h"
#include "cyc_mod.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#ifdef CYC_WITH_SDL
#include "cyc_tcp.h"
#endif
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef FAX_SLAIN_LOG
#define FAX_SLAIN_LOG(...) fprintf(stderr, __VA_ARGS__)
#endif

/* Fixed bank */
#define PLAYER_REDUCE_HP   0xC08Eu   /* Player_ReduceHP: LDA Player_HP_L, SEC, SBC Arg_PlayerHealthDelta_L */
#define POISON_RESUME      0xC860u   /* Player_PickUpPoison's LDX CurrentSpriteIndex, after its JSR Player_ReduceHP */
/* Bank 14 */
#define SPRITE_RESET_STATE 0xA202u   /* Sprite_Maybe_ResetState: TXA, PHA. X the spawner, Y the slot spawned */
/* SpriteBehavior_FlashScreenHitPlayer's LDX CurrentSpriteIndex, after its JSR
 * Player_ReduceHP */
#define FLASH_RESUME       0xAB60u
/* RAM */
#define SPRITE_ENTITIES    0x02CCu   /* CurrentSprites_Entities: 8 slots, $FF empty */
#define CURRENT_SPRITE     0x0378u   /* CurrentSpriteIndex */
#define SPECIAL_ITEMS      0x042Cu   /* bit $08: the Elixir, which Player_ReduceHP uses instead of a death */
#define PLAYER_HP_U        0x0431u   /* whole HP */
#define PLAYER_HP_L        0x0432u   /* its fraction */
#define HEALTH_DELTA_L     0x04BCu   /* Arg_PlayerHealthDelta_L */
#define HEALTH_DELTA_U     0x04BDu

enum { SLOTS = 8, ENTITY_COUNT = 0x65, NO_ONE = 0xFF, POISON = 0xFE, ELIXIR = 0x08 };
/* How the killing hit came. */
enum { HOW_NONE, HOW_TOUCH, HOW_SHOT, HOW_FLASH, HOW_POISON };
/* Frames: fade in, hold, fade out. The dissolve before the Guru's words is 256. */
enum { FADE_IN = 8, HOLD = 200, FADE_OUT = 40, SHOWN = FADE_IN + HOLD + FADE_OUT };

/* What each entity that can hurt the hero is called (chipx86's SPRITE_*
 * constants, as a player would say one of them), with its article. Entities
 * with no name are NPCs, items, effects, the unused enemies, and the shots
 * and rocks below. */
static const char *const NAMES[ENTITY_COUNT] = {
    [0x04] = "a Raiden",           [0x05] = "a Necron Aide",      [0x06] = "a Zombie",
    [0x07] = "a Hornet",           [0x08] = "a Bihoruda",         [0x09] = "a Lilith",
    [0x0B] = "a Yuinaru",          [0x0C] = "a Snowman",          [0x0D] = "a Nash",
    [0x0E] = "a Fire Giant",       [0x0F] = "an Ishiisu",         [0x10] = "an Execution Hood",
    [0x11] = "a Rokusutahn",       [0x15] = "a Charron",          [0x17] = "a Geributa",
    [0x18] = "a Sugata",           [0x19] = "a Grimlock",         [0x1A] = "a Giant Bee",
    [0x1B] = "a Myconid",          [0x1C] = "a Naga",             [0x1E] = "a Giant Strider",
    [0x1F] = "a Sir Gawaine",      [0x20] = "a Maskman",          [0x21] = "a Wolfman",
    [0x22] = "a Yareeka",          [0x23] = "a Magman",           [0x26] = "an Ikeda",
    [0x28] = "a Lamprey",          [0x2A] = "a Monodron",         [0x2C] = "a Tamazutsu",
    [0x2D] = "a Ripasheiku",       [0x2E] = "a Zoradohna",        [0x2F] = "a Borabohra",
    [0x30] = "a Pakukame",         [0x31] = "a Zorugeriru",       [0x32] = "King Grieve",
    [0x33] = "Shadow Eura",        [0x47] = "a Zozura",
};

/* Shots and rocks, and the only sprites that spawn each (from the code that
 * stores the entity before its JMP Sprite_Maybe_ResetState). */
static const struct { uint8_t spawned, by[3]; } SPAWNED[] = {
    { 0x03, { 0x31, NO_ONE, NO_ONE } },   /* falling rock: SpriteBehavior_Zorugeriru__9e13 */
    { 0x0A, { 0x2D, 0x32, NO_ONE } },     /* fire: Sprite_CastMagic, from Ripasheiku's and King Grieve's behaviors */
    { 0x51, { 0x0D, 0x0F, 0x10 } },       /* shot: FUN_PRG14__a0a0 (Nash, Ishiisu), SpriteBehavior_ExecutionHood__a0ad */
    { 0x53, { 0x33, NO_ONE, NO_ONE } },   /* fire: SpriteBehavior_ShadowEura__a12d */
    { 0x54, { 0x15, NO_ONE, NO_ONE } },   /* fireball: BScript_Action_CastMagic, in Charron's script (FUN_PRG14__a1cc,
                                             the other, has no caller) */
};

typedef struct {
    uint8_t child[SLOTS], parent[SLOTS];   /* each slot's spawn: what it is, and its spawner's entity */
    uint8_t named, how;                    /* the line: who (an entity, or POISON), and how it hit */
    uint16_t age;                          /* frames it has shown; SHOWN when none shows */
} Slain;

static bool enabled, font_failed;
static Slain s;
static unsigned deaths, unnamed, spawns;   /* since power-on: deaths named, deaths not, spawns seen */
static char last[40];                      /* the last line said */

static uint8_t q(uint16_t addr) { return cyc_mod_peek(addr); }

static void forget(void) {
    memset(s.child, NO_ONE, sizeof s.child);
    memset(s.parent, NO_ONE, sizeof s.parent);
    s.named = NO_ONE;
    s.how = HOW_NONE;
    s.age = SHOWN;
}

static const char *name_of(uint8_t who) {
    if (who == POISON) return "Poison";
    return who < ENTITY_COUNT ? NAMES[who] : NULL;
}
/* "Slain by a Sugata", into `out`; NULL for no one. */
static const char *line_for(uint8_t who, char out[40]) {
    const char *name = name_of(who);
    if (!name) return NULL;
    snprintf(out, 40, "Slain by %s", name);
    return out;
}

static int spawned_index(uint8_t entity) {
    for (unsigned i = 0; i < sizeof SPAWNED / sizeof SPAWNED[0]; ++i)
        if (SPAWNED[i].spawned == entity) return (int)i;
    return -1;
}
static bool spawns_it(int kind, uint8_t entity) {   /* an empty slot ($FF) is no one */
    for (unsigned j = 0; j < 3; ++j)
        if (entity != NO_ONE && SPAWNED[kind].by[j] == entity) return true;
    return false;
}

/* Who spawned the shot or rock in `slot`: the spawn seen there, else the one
 * kind of sprite on screen that spawns it. */
static uint8_t spawner(uint8_t slot, uint8_t entity) {
    int kind = spawned_index(entity);
    if (kind < 0) return NO_ONE;
    if (s.child[slot] == entity && name_of(s.parent[slot])) return s.parent[slot];
    uint8_t found = NO_ONE;
    for (uint8_t i = 0; i < SLOTS; ++i) {
        uint8_t e = q((uint16_t)(SPRITE_ENTITIES + i));
        if (i == slot || !spawns_it(kind, e)) continue;
        if (found != NO_ONE && found != e) return NO_ONE;   /* two kinds cast it: no telling which */
        found = e;
    }
    return found;
}

/* What the hit at Player_ReduceHP's entry came from: its resume address (the
 * word at S + 1, + 1) tells the Poison and the flash; anything else is the
 * sprite at CurrentSpriteIndex, or what spawned it. */
static uint8_t attacker(uint16_t resume, uint8_t *how) {
    if (resume == POISON_RESUME) { *how = HOW_POISON; return POISON; }
    uint8_t slot = q(CURRENT_SPRITE);
    if (slot >= SLOTS) { *how = HOW_NONE; return NO_ONE; }
    uint8_t e = q((uint16_t)(SPRITE_ENTITIES + slot));
    if (resume == FLASH_RESUME) *how = HOW_FLASH;
    else *how = spawned_index(e) >= 0 ? HOW_SHOT : HOW_TOUCH;
    if (name_of(e)) return e;
    return spawner(slot, e);
}

/* Player_ReduceHP subtracts the delta from HP (16 bits, the whole and the
 * fraction) and kills when that borrows, unless the Elixir is carried. */
static bool kills(void) {
    unsigned hp = (unsigned)q(PLAYER_HP_U) << 8 | q(PLAYER_HP_L);
    unsigned delta = (unsigned)q(HEALTH_DELTA_U) << 8 | q(HEALTH_DELTA_L);
    return hp < delta && !(q(SPECIAL_ITEMS) & ELIXIR);
}

static uint16_t stack_word(uint8_t sp, unsigned at) {   /* the 6502 stack wraps within page 1 */
    return (uint16_t)(q((uint16_t)(0x100u | (uint8_t)(sp + at))) | q((uint16_t)(0x100u | (uint8_t)(sp + at + 1))) << 8);
}

static void say(uint8_t who, uint8_t how) {
    char line[40];
    s.how = how;
    if (!line_for(who, line)) { ++unnamed; s.named = NO_ONE; s.age = SHOWN; return; }
    ++deaths;
    s.named = who;
    s.age = 0;
    snprintf(last, sizeof last, "%s", line);
    fax_banner_hide();   /* the line takes the banner's place */
}

/* Player_ReduceHP: a hit that kills names what it came from. */
static int reduce_hp_hook(uint16_t addr) {
    (void)addr;
    if (!enabled || cyc_mod_isolated() || !kills()) return 0;   /* Seamless's neighboring rooms keep no state */
    CycModRegs regs;
    cyc_mod_regs(&regs);
    uint8_t how;
    uint8_t who = attacker((uint16_t)(stack_word(regs.s, 1) + 1), &how);
    say(who, how);
    return 0;
}

/* Sprite_Maybe_ResetState: the sprite in slot X spawned the one in slot Y. */
static int spawn_hook(uint16_t addr) {
    (void)addr;
    if (!enabled || cyc_mod_isolated()) return 0;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    if (regs.x >= SLOTS || regs.y >= SLOTS || regs.x == regs.y) return 0;
    s.child[regs.y] = q((uint16_t)(SPRITE_ENTITIES + regs.y));
    s.parent[regs.y] = q((uint16_t)(SPRITE_ENTITIES + regs.x));
    ++spawns;
    return 0;
}

void fax_slain_frame_end(void) {
    if (!enabled) return;
    for (uint8_t i = 0; i < SLOTS; ++i)   /* a slot whose spawn is gone forgets its spawner */
        if (s.child[i] != NO_ONE && q((uint16_t)(SPRITE_ENTITIES + i)) != s.child[i]) s.child[i] = s.parent[i] = NO_ONE;
    if (s.age >= SHOWN) return;
    if (!fax_text_loaded() && !font_failed && !fax_text_load()) {
        FAX_SLAIN_LOG("[slain by] cannot read the game's font; the line is not drawn\n");
        font_failed = true;
    }
    ++s.age;
}

static unsigned alpha_at(unsigned age) {
    if (age < FADE_IN) return (age + 1) * 255u / FADE_IN;
    if (age < FADE_IN + HOLD) return 255;
    if (age < SHOWN) return (SHOWN - age) * 255u / (FADE_OUT + 1);
    return 0;
}

const uint32_t *fax_slain_present(const uint32_t *picture, int *w, int *h) {
    char line[40];
    if (!enabled || s.age >= SHOWN || fax_text_box_showing() || !line_for(s.named, line)) return picture;
    return fax_banner_draw(picture, w, h, line, alpha_at(s.age));
}

/* Save states carry the line and the spawners, so loading one mid-death
 * resumes the line and a shot in flight keeps its caster. */
enum { VERSION = 1, RECORD = 5 + 2 * SLOTS };
static int state_get(uint8_t *buf, int cap) {
    if (cap < RECORD) return -1;
    buf[0] = VERSION;
    buf[1] = s.named;
    buf[2] = s.how;
    buf[3] = (uint8_t)s.age;
    buf[4] = (uint8_t)(s.age >> 8);
    memcpy(buf + 5, s.child, SLOTS);
    memcpy(buf + 5 + SLOTS, s.parent, SLOTS);
    return RECORD;
}
static int state_validate(const uint8_t *buf, int len) {
    if (len == 0) return 1;
    if (len != RECORD || buf[0] != VERSION || buf[2] > HOW_POISON) return 0;
    unsigned age = buf[3] | (unsigned)buf[4] << 8;
    return age <= SHOWN && (age == SHOWN || name_of(buf[1]));
}
static int state_set(const uint8_t *buf, int len) {
    if (!state_validate(buf, len)) return 0;
    forget();
    if (!len) return 1;   /* saved without the feature: no line, no spawners */
    s.named = buf[1];
    s.how = buf[2];
    s.age = (uint16_t)(buf[3] | buf[4] << 8);
    memcpy(s.child, buf + 5, SLOTS);
    memcpy(s.parent, buf + 5 + SLOTS, SLOTS);
    return 1;
}

#ifdef CYC_WITH_SDL
static const char *const HOW[] = { "none", "touch", "shot", "flash", "poison" };
static void slain_state(int id, const char *cmd) {
    (void)cmd;
    char fields[512], line[40], parents[64];
    const char *showing = s.age < SHOWN ? line_for(s.named, line) : NULL;
    int n = 0;
    for (unsigned i = 0; i < SLOTS; ++i)
        n += snprintf(parents + n, sizeof parents - (size_t)n, "%s%u", i ? "," : "", s.child[i] == NO_ONE ? 255u : s.parent[i]);
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"showing\":%s%s%s,\"alpha\":%u,\"age\":%u,\"how\":\"%s\",\"deaths\":%u,"
             "\"unnamed\":%u,\"spawns\":%u,\"spawners\":[%s],\"last\":%s%s%s",
             enabled ? "true" : "false", showing ? "\"" : "", showing ? showing : "null", showing ? "\"" : "",
             showing ? alpha_at(s.age) : 0u, s.age, HOW[s.how], deaths, unnamed, spawns, parents,
             last[0] ? "\"" : "", last[0] ? last : "null", last[0] ? "\"" : "");
    cyc_tcp_ok(id, fields);
}
void fax_slain_tcp_setup(void) {
    cyc_tcp_register("slain_state", "Slain by: the line showing, how the last death came, each slot's spawner",
                     slain_state);
}
#else
void fax_slain_tcp_setup(void) {}
#endif

static const char *const hook_ids[] = { "faxanadu.slain-by.reduce-hp", "faxanadu.slain-by.spawn" };
static void hooks(bool on) {
    for (unsigned i = 0; i < sizeof hook_ids / sizeof hook_ids[0]; ++i) nes_mod_set_function_hook_enabled(hook_ids[i], on);
}
static void deactivate(void) {
    enabled = font_failed = false;
    forget();
    deaths = unnamed = spawns = 0;
    last[0] = 0;
    hooks(false);
}
static void activate(void) {
    enabled = true;
    hooks(true);
}

NES_MOD_CONSTRUCTOR(register_slain_by) {
    forget();
    if (!nes_mod_register_function_entry_plugin(hook_ids[0], PLAYER_REDUCE_HP, reduce_hp_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[1], SPRITE_RESET_STATE, spawn_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.slain-by", activate) ||
        !nes_mod_register_savestate_hook("faxanadu.slain-by", state_get, state_set) ||
        !nes_mod_register_savestate_validator("faxanadu.slain-by", state_validate)) abort();
}
