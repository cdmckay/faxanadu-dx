/* Unit tests for Fair entrances (fax_entrances.c), without the emulator or a
 * ROM. The module runs against a model of the machine: CPU RAM, and the
 * game's sprite loader as the disassembly has it. Screen_LoadSpriteInfo reads
 * each (entity, YX) pair of the screen's bank 11 placement list into
 * CurrentSprite_Entity, _YPos (YX & $F0) and _XPos (YX << 4), and calls
 * Sprites_PopulateNextAvailableSprite, whose entry is the hook site; that
 * copies the three into the highest free slot of eight. The placement lists
 * below are the stock bytes for the screens the table moves an enemy on. So
 * the tests check where the game puts each sprite, with the feature on and
 * off, on the live machine (the hook) and on Seamless's isolated one (hooks
 * off, then fax_entrances_room_loaded). */
#include "fax_entrances.c"
#include <string.h>

static unsigned checks;
#define CHECK(test) do{++checks;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(1);}}while(0)

/* ---- the machine ---- */
static uint8_t ram[0x800];
static bool isolated;
static int hook_on = -1;                          /* the hook's enabled state, as last set */

uint8_t cyc_mod_peek(uint16_t addr) { return addr < 0x2000 ? ram[addr & 0x7FF] : 0; }
bool cyc_mod_poke(uint16_t addr, uint8_t value) {
    if (addr >= 0x2000) return false;
    ram[addr & 0x7FF] = value;
    return true;
}
bool cyc_mod_isolated(void) { return isolated; }
int nes_mod_register_function_entry_plugin(const char *id, uint16_t addr, NESModFunctionEntryCallback cb) {
    return id && !strcmp(id, hook_id) && addr == POPULATE_SPRITE && cb == populate_hook;
}
int nes_mod_set_function_hook_enabled(const char *id, int on) {
    if (strcmp(id, hook_id)) return 0;
    hook_on = on;
    return 1;
}
int nes_mod_register_reset_callback(NESModActivationCallback cb) { return cb == deactivate; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    return !strcmp(id, "faxanadu.fair-entrances") && cb == activate;
}
void nes_mod_set_local_only(const char *name, int required) { (void)name; (void)required; }

/* ---- bank 11: the stock placement lists of the screens with a move ---- */
enum { HORNET = 0x07, YUINARU = 0x0B, ROKUSUTAHN = 0x11, YAREEKA = 0x22, POISON = 0x5E };
typedef struct { uint8_t area, screen, n, pairs[8][2]; } Screen;
static const Screen screens[] = {
    { TRUNK,    0x06, 2, {{ZOMBIE, 0xA6}, {ZOMBIE, 0x5B}} },
    { MIST,     0x06, 2, {{SIR_GAWAINE, 0x23}, {SIR_GAWAINE, 0x89}} },
    { MIST,     0x17, 3, {{HORNET, 0x69}, {HORNET, 0xA3}, {RAIDEN, 0xA5}} },
    { MIST,     0x1F, 2, {{POISON, 0xA1}, {SUGATA, 0x95}} },
    { MIST,     0x20, 2, {{YAREEKA, 0x39}, {SIR_GAWAINE, 0x96}} },
    { MIST,     0x29, 2, {{IKEDA, 0x7D}, {IKEDA, 0x75}} },
    { BRANCH,   0x0E, 1, {{LAMPREY, 0x6D}} },
    { BRANCH,   0x13, 1, {{MASKMAN, 0x8C}} },
    { DARTMOOR, 0x02, 2, {{GIANT_STRIDER, 0x49}, {MASKMAN, 0xA5}} },
    { DARTMOOR, 0x03, 3, {{LAMPREY, 0xAA}, {SIR_GAWAINE, 0xA4}, {YUINARU, 0x58}} },
    { DARTMOOR, 0x08, 3, {{ROKUSUTAHN, 0x98}, {SIR_GAWAINE, 0x42}, {HORNET, 0x78}} },
    { DARTMOOR, 0x0C, 2, {{WOLFMAN, 0xAB}, {WOLFMAN, 0xAE}} },
};
enum { SCREENS = sizeof screens / sizeof screens[0] };

/* Screen_LoadSpriteInfo for a placement list, with Screen_ClearSprites
 * before it. The hook runs at Sprites_PopulateNextAvailableSprite's entry when
 * enabled and the machine is not isolated (hooks are off in isolated scopes). */
static void load(uint8_t area, uint8_t screen, const uint8_t (*pairs)[2], unsigned n) {
    ram[AREA_CURRENT_AREA] = area;
    ram[AREA_CURRENT_SCREEN] = screen;
    for (unsigned k = 0; k < SLOTS; ++k) {
        ram[SPRITE_ENTITY + k] = 0xFF;
        ram[SPRITE_X + k] = ram[SPRITE_Y + k] = 0;
    }
    for (unsigned i = 0; i < n; ++i) {
        ram[CURRENT_ENTITY] = pairs[i][0];
        ram[CURRENT_Y] = pairs[i][1] & 0xF0;
        ram[CURRENT_X] = (uint8_t)(pairs[i][1] << 4);
        if (hook_on == 1 && !isolated) populate_hook(POPULATE_SPRITE);
        for (int x = 7; x >= 0; --x)                  /* LDX #$07 ... the first free slot from the top */
            if (ram[SPRITE_ENTITY + x] == 0xFF) {
                ram[SPRITE_X + x] = ram[CURRENT_X];
                ram[SPRITE_Y + x] = ram[CURRENT_Y];
                ram[SPRITE_ENTITY + x] = ram[CURRENT_ENTITY];
                break;
            }
    }
}
static void load_screen(const Screen *s) { load(s->area, s->screen, s->pairs, s->n); }

/* Where the game put the i-th sprite of the list: slot 7 - i. */
static uint8_t placed(unsigned i) {
    unsigned k = 7 - i;
    return (uint8_t)(ram[SPRITE_Y + k] | ram[SPRITE_X + k] >> 4);
}
static uint8_t placed_entity(unsigned i) { return ram[SPRITE_ENTITY + 7 - i]; }

/* The move for a screen's i-th placement, or NULL. */
static const FaxEntranceMove *move_for(const Screen *s, unsigned i) {
    for (unsigned m = 0; m < MOVES; ++m)
        if (moves[m].area == s->area && moves[m].screen == s->screen && moves[m].entity == s->pairs[i][0] &&
            moves[m].from == s->pairs[i][1])
            return &moves[m];
    return NULL;
}

static void check_stock(const Screen *s) {
    for (unsigned i = 0; i < s->n; ++i) {
        CHECK(placed_entity(i) == s->pairs[i][0]);
        CHECK(placed(i) == s->pairs[i][1]);
    }
}
static void check_moved(const Screen *s) {
    for (unsigned i = 0; i < s->n; ++i) {
        const FaxEntranceMove *m = move_for(s, i);
        CHECK(placed_entity(i) == s->pairs[i][0]);
        CHECK(placed(i) == (m ? m->to : s->pairs[i][1]));
    }
}

/* The table itself: each move is of an enemy the stock list places on its
 * screen, once; it goes somewhere else on the playfield (rows 0-11, above the
 * floor row the hero stands on at most), not onto another sprite's block, and
 * no move starts where another ends (one pass places everything). */
static void test_table(void) {
    CHECK(MOVES > 0);
    for (unsigned m = 0; m < MOVES; ++m) {
        const FaxEntranceMove *a = &moves[m];
        const Screen *s = NULL;
        for (unsigned i = 0; i < SCREENS; ++i)
            if (screens[i].area == a->area && screens[i].screen == a->screen) s = &screens[i];
        CHECK(s != NULL);
        unsigned found = 0;
        for (unsigned i = 0; i < s->n; ++i) {
            if (s->pairs[i][0] == a->entity && s->pairs[i][1] == a->from) ++found;
            CHECK(s->pairs[i][1] != a->to);
        }
        CHECK(found == 1);
        CHECK(a->to != a->from);
        CHECK((a->to >> 4) <= 11);
        CHECK(a->entity < 0x34);                          /* an enemy, not an NPC or an item */
        for (unsigned n = 0; n < MOVES; ++n) {
            if (n == m) continue;
            const FaxEntranceMove *b = &moves[n];
            if (b->area != a->area || b->screen != a->screen) continue;
            CHECK(!(b->entity == a->entity && b->from == a->from));
            CHECK(b->to != a->to);
            CHECK(b->from != a->to);
        }
    }
    /* every screen modelled has a move */
    for (unsigned i = 0; i < SCREENS; ++i) {
        unsigned n = 0;
        for (unsigned j = 0; j < screens[i].n; ++j) n += move_for(&screens[i], j) != NULL;
        CHECK(n >= 1);
    }
}

int main(void) {
    test_table();

    /* Off (before activation): the original placements, and Seamless's call
     * changes nothing. */
    CHECK(hook_on == -1);
    for (unsigned i = 0; i < SCREENS; ++i) {
        load_screen(&screens[i]);
        check_stock(&screens[i]);
        CHECK(fax_entrances_room_loaded() == 0);
        check_stock(&screens[i]);
    }

    /* On: the hook moves exactly the listed enemies, as they are placed. */
    activate();
    CHECK(hook_on == 1);
    for (unsigned i = 0; i < SCREENS; ++i) {
        unsigned before = moved;
        load_screen(&screens[i]);
        check_moved(&screens[i]);
        unsigned n = 0;
        for (unsigned j = 0; j < screens[i].n; ++j) n += move_for(&screens[i], j) != NULL;
        CHECK(moved == before + n);
        CHECK(last >= 0 && moves[last].area == screens[i].area && moves[last].screen == screens[i].screen);
        /* Seamless's call after a live load: they are already moved. */
        CHECK(fax_entrances_room_loaded() == 0);
        check_moved(&screens[i]);
    }

    /* Only that screen of that area: the same enemy at the same block
     * elsewhere stays, and so does the same block's other enemies. */
    for (unsigned m = 0; m < MOVES; ++m) {
        const uint8_t pair[1][2] = {{moves[m].entity, moves[m].from}};
        load(moves[m].area, (uint8_t)(moves[m].screen + 1), pair, 1);
        CHECK(placed(0) == moves[m].from);
        load((uint8_t)(moves[m].area ^ 1), moves[m].screen, pair, 1);
        CHECK(placed(0) == moves[m].from);
        const uint8_t other[1][2] = {{(uint8_t)(moves[m].entity ^ 0x01), moves[m].from}};
        load(moves[m].area, moves[m].screen, other, 1);
        CHECK(placed(0) == moves[m].from);
    }

    /* Seamless's isolated machine: hooks do not run, so the room loads as
     * stock, and fax_entrances_room_loaded moves the listed enemies in their
     * slots, once, without counting them as the live game's. */
    isolated = true;
    for (unsigned i = 0; i < SCREENS; ++i) {
        unsigned before = moved, rooms = seamless;
        load_screen(&screens[i]);
        check_stock(&screens[i]);
        unsigned n = 0;
        for (unsigned j = 0; j < screens[i].n; ++j) n += move_for(&screens[i], j) != NULL;
        CHECK(fax_entrances_room_loaded() == n);
        check_moved(&screens[i]);
        CHECK(fax_entrances_room_loaded() == 0);
        check_moved(&screens[i]);
        CHECK(moved == before && seamless == rooms + n);
    }
    /* A slot off the block grid (an enemy that has moved) is not a placement. */
    load_screen(&screens[0]);
    for (unsigned k = 0; k < SLOTS; ++k) if (ram[SPRITE_ENTITY + k] != 0xFF) ram[SPRITE_X + k] |= 0x03;
    CHECK(fax_entrances_room_loaded() == 0);
    isolated = false;

    /* Reset: off again, the hook disabled, nothing kept. */
    deactivate();
    CHECK(hook_on == 0);
    CHECK(moved == 0 && seamless == 0 && last == -1);
    for (unsigned i = 0; i < SCREENS; ++i) {
        load_screen(&screens[i]);
        check_stock(&screens[i]);
        CHECK(fax_entrances_room_loaded() == 0);
    }

    printf("fair entrances: %u checks passed (%d moves)\n", checks, (int)MOVES);
    return 0;
}
