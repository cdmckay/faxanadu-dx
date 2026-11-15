/* Unit tests for the taller dagger (fax_dagger.c), without the emulator or a
 * ROM. The module runs against a model of the machine: bank 14's weapon range
 * table as the stock image has it, and the engine's read overrides, matched
 * by physical PRG offset and stock value as cyc_mod.c matches them. Models of
 * Player_DrawWeapon's range load and of Sprites_IsSpriteOutOfWeaponRange, in
 * the 6502's 8-bit arithmetic, then swing each weapon at sprites around the
 * hero, placed as the game places them (see docs/GAME_INTERNALS.md). */
#include "fax_dagger.c"
#include <string.h>

static unsigned checks;
#define CHECK(test) do{++checks;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(1);}}while(0)

/* ---- the machine ----
 * The registrations below accept only this module's own callbacks: the
 * constructor aborts before main if any is wrong. */
static uint8_t rom[16 * 16384];                  /* $FF but the weapon range table */
typedef struct { uint32_t offset; uint8_t compare, value; } Override;
static Override overrides[64];
static unsigned override_count;
static bool local_only;

bool cyc_mod_set_prg_read_override(uint32_t offset, uint8_t compare, uint8_t value) {
    if (offset >= sizeof rom || rom[offset] != compare) return false;
    for (unsigned i = 0; i < override_count; ++i)
        if (overrides[i].offset == offset) { overrides[i].compare = compare; overrides[i].value = value; return true; }
    if (override_count == 64) return false;
    overrides[override_count++] = (Override){ offset, compare, value };
    return true;
}
/* What the CPU reads at addr with `bank` mapped at $8000-$BFFF. */
static uint8_t cpu_read(unsigned bank, uint16_t addr) {
    uint32_t offset = prg(bank, addr);
    uint8_t value = rom[offset];
    for (unsigned i = 0; i < override_count; ++i)
        if (overrides[i].offset == offset && overrides[i].compare == value) return overrides[i].value;
    return value;
}
int nes_mod_register_reset_callback(NESModActivationCallback cb) { return cb == deactivate; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    return !strcmp(id, "faxanadu.taller-dagger") && cb == activate;
}
int nes_mod_register_savestate_hook(const char *id, NESModSavestateGet get, NESModSavestateSet set) {
    return !strcmp(id, "faxanadu.taller-dagger") && get == state_get && set == state_set;
}
int nes_mod_register_savestate_validator(const char *id, NESModSavestateValidate validate) {
    return !strcmp(id, "faxanadu.taller-dagger") && validate == state_validate;
}
void nes_mod_set_local_only(const char *name, int required) { (void)name; local_only = required; }

/* A fresh machine with the stock image and the feature off. */
static void stock(void) {
    static const uint8_t ranges[] = { 0x08, 0x04, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08 };
    deactivate();
    memset(rom, 0xFF, sizeof rom);
    memcpy(rom + prg(14, WEAPON_RANGES), ranges, sizeof ranges);
    override_count = 0; local_only = false;
}
static void on(void) { stock(); activate(); fax_dagger_step(); }

/* ---- the game ---- */
enum { DAGGER, LONG_SWORD, GIANT_BLADE, DRAGON_SLAYER };
typedef struct { uint8_t x, y; } Reach;
/* Player_DrawWeapon, which runs in bank 14: X = Player_CurWeapon * 2, then
 * LDA MAYBE_WEAPON_RANGES,X and MAYBE_WEAPON_RANGES+1,X. */
static Reach reach(int weapon) {
    uint16_t at = (uint16_t)(WEAPON_RANGES + 2 * weapon);
    return (Reach){ cpu_read(14, at), cpu_read(14, (uint16_t)(at + 1)) };
}
/* Sprites_IsSpriteOutOfWeaponRange, negated: is a sprite at (sx, sy), with
 * extents (ex, ey) from MAYBE_SPRITE_EXTENTS, inside the box of a weapon at
 * (wx, wy) (PlayerPosXPlus10, Maybe_Something_WeaponPosY)? */
static bool hits(Reach r, uint8_t wx, uint8_t wy, uint8_t sx, uint8_t sy, uint8_t ex, uint8_t ey) {
    if ((uint8_t)(wx + r.x - sx) >= (uint8_t)(ex + r.x)) return false;
    return (uint8_t)(wy + r.y - sy) < (uint8_t)(ey + r.y);
}
/* The hero stands at (120, 144) facing right, 32 pixels tall, with no
 * shield. A swing hits on its animation frames 5 and 6, where Player_DrawWeapon
 * puts the weapon at the hero plus an offset from its per-weapon tables
 * (PTR_ARRAY_PRG14__b89f for X, by facing; PTR_ARRAY_PRG14__b90f for Y): the
 * dagger at (x + 14, y + 8) then (x + 24, y + 6), the swords at (x + 16, y + 8)
 * then (x + 24, y + 6). A Zozura is 16 by 16 (MAYBE_SPRITE_EXTENTS) and stands
 * at y + 16 on the same ground; a Monodron is 16 by 32. */
enum { HERO_X = 120, HERO_Y = 144, ZOZURA = 16, MONODRON_HEIGHT = 32 };
typedef struct { uint8_t dx, dy; } Phase;
static const Phase dagger_phases[] = { { 14, 8 }, { 24, 6 } }, sword_phases[] = { { 16, 8 }, { 24, 6 } };
static const Phase *phases(int weapon) { return weapon == DAGGER ? dagger_phases : sword_phases; }
static bool swing(int weapon, uint8_t sx, uint8_t sy, uint8_t ex, uint8_t ey) {
    bool hit = false;
    for (unsigned p = 0; p < 2; ++p) {
        const Phase *at = &phases(weapon)[p];
        hit |= hits(reach(weapon), (uint8_t)(HERO_X + at->dx), (uint8_t)(HERO_Y + at->dy), sx, sy, ex, ey);
    }
    return hit;
}

static void stock_reach(void) {
    stock();
    CHECK(reach(DAGGER).x == 0x08 && reach(DAGGER).y == 0x04);
    for (int w = LONG_SWORD; w <= DRAGON_SLAYER; ++w) CHECK(reach(w).x == 0x10 && reach(w).y == 0x08);
    fax_dagger_step();                               /* not activated: nothing served */
    CHECK(override_count == 0);
}

static void taller_reach(void) {
    on();
    CHECK(served && !failed && local_only);
    CHECK(reach(DAGGER).x == 0x08 && reach(DAGGER).y == 0x08);
    for (int w = LONG_SWORD; w <= DRAGON_SLAYER; ++w) CHECK(reach(w).x == 0x10 && reach(w).y == 0x08);
    /* One override, on bank 14's byte only: physical offset $3B898. */
    CHECK(override_count == 1 && overrides[0].offset == 0x3B898 && overrides[0].compare == 0x04 && overrides[0].value == 0x08);
    rom[prg(13, DAGGER_REACH_Y)] = 0x04;             /* the same address in another bank is untouched */
    CHECK(cpu_read(13, DAGGER_REACH_Y) == 0x04);
    fax_dagger_step(); fax_dagger_step();            /* served once */
    CHECK(override_count == 1);
    deactivate();                                    /* the launcher turns it off: stock again */
    CHECK(reach(DAGGER).y == 0x04 && !served && !enabled);
    activate(); fax_dagger_step();                   /* and on again, in play */
    CHECK(reach(DAGGER).y == 0x08 && override_count == 1);
}

static void refuses_other_layouts(void) {
    stock();
    rom[prg(14, DAGGER_REACH_Y)] = 0x06;             /* not the stock image */
    activate(); fax_dagger_step();
    CHECK(failed && !served && override_count == 0 && reach(DAGGER).y == 0x06);
    fax_dagger_step();                               /* no retry every frame */
    CHECK(override_count == 0);
}

/* Overrides are not machine state: whatever a host does with them across a
 * load, the save-state hook serves the reach again. Its record is empty. */
static void save_states(void) {
    uint8_t buf[4];
    on();
    CHECK(state_get(buf, sizeof buf) == 0);
    CHECK(state_validate(NULL, 0) && !state_validate(buf, 1) && !state_set(buf, 1));
    override_count = 0;                              /* a host that dropped every override */
    CHECK(reach(DAGGER).y == 0x04);
    CHECK(state_set(NULL, 0));                       /* a save without our record, or with an empty one */
    CHECK(reach(DAGGER).y == 0x08 && override_count == 1 && served);
    CHECK(state_set(NULL, 0) && override_count == 1);  /* a load where the override survived */
    stock();                                         /* turned off: a load serves nothing */
    CHECK(state_set(NULL, 0) && override_count == 0 && reach(DAGGER).y == 0x04);
}

static void low_enemies(void) {
    const uint8_t zx = HERO_X + 22, ground = HERO_Y + 16;
    stock();
    CHECK(!swing(DAGGER, zx, ground, ZOZURA, ZOZURA));        /* the stock dagger passes over a Zozura */
    CHECK(swing(LONG_SWORD, zx, ground, ZOZURA, ZOZURA));     /* that the Long Sword hits */
    on();
    CHECK(swing(DAGGER, zx, ground, ZOZURA, ZOZURA));
    CHECK(swing(LONG_SWORD, zx, ground, ZOZURA, ZOZURA));
    /* The dagger keeps its short reach: the Long Sword still hits further out. */
    CHECK(swing(LONG_SWORD, HERO_X + 32, ground, ZOZURA, ZOZURA));
    CHECK(!swing(DAGGER, HERO_X + 32, ground, ZOZURA, ZOZURA));
}

/* Every height a sprite can be at: the taller dagger hits whatever the stock
 * one hits, and its new hits are only 13-16 pixels below the hero's top, at
 * the hero's knees. Nothing above the swing is hit, and its box spans what
 * the Long Sword's does. */
static void nothing_above(void) {
    for (unsigned h = 0; h < 2; ++h) {
        uint8_t ey = h ? MONODRON_HEIGHT : ZOZURA;
        for (unsigned sy = 0; sy < 240; ++sy) {
            stock();
            bool before = swing(DAGGER, HERO_X + 20, (uint8_t)sy, ZOZURA, ey);
            on();
            bool after = swing(DAGGER, HERO_X + 20, (uint8_t)sy, ZOZURA, ey);
            CHECK(!before || after);
            if (after && !before) CHECK(sy > HERO_Y + 12 && sy <= HERO_Y + 16);
            if (sy + ey <= HERO_Y + 6) CHECK(!after);                     /* wholly above the swing */
            for (unsigned p = 0; p < 2; ++p) {
                uint8_t wx = (uint8_t)(HERO_X + sword_phases[p].dx), wy = (uint8_t)(HERO_Y + sword_phases[p].dy);
                bool dagger = hits(reach(DAGGER), wx, wy, wx, (uint8_t)sy, ZOZURA, ey);
                bool sword = hits(reach(LONG_SWORD), wx, wy, wx, (uint8_t)sy, ZOZURA, ey);
                CHECK(dagger == sword);                                    /* the same height */
            }
        }
    }
}

int main(void) {
    stock_reach();
    taller_reach();
    refuses_other_layouts();
    save_states();
    low_enemies();
    nothing_above();
    printf("dagger_test: %u checks passed\n", checks);
    return 0;
}
