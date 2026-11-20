/* Unit tests for Equip indoors (fax_indoor.c), without the emulator or a ROM.
 * The module runs against a model of the machine: CPU RAM, the registers and
 * flags, and the three pieces of code it hooks, played instruction by
 * instruction as the disassembly has them, with the hook sites where the game
 * has them: Game_EnterBuilding's first lines ($DE06), Player_SetWeapon
 * ($EDEC) and the Select menu's choice (@_hasInput, $8B83, bank 12). So the
 * tests check what the game does with the hooks: in a building, exactly what
 * it does outdoors. */
#include "fax_indoor.c"
#include <string.h>

static unsigned checks;
#define CHECK(test) do{++checks;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(1);}}while(0)

/* RAM the model reads besides the module's */
#define MENU_SELECTED_INVENTORY 0x020Eu   /* PlayerMenu_SelectedInventory: 0 weapons ... 3 magic, 4 items */
#define MENU_CURSOR             0x021Eu   /* Menu_CursorPos */
#define DATA_ARRAY              0x0220u   /* the list's entries */
#define WEAPON_INVENTORY        0x039Du   /* armor $03A1, shields $03A5: 4 entries each */
#define NUMBER_OF_WEAPONS       0x03C2u   /* armor $03C3, shields $03C4 */
#define P_C                     0x01u
#define P_N                     0x80u
enum { DAGGER, LONG_SWORD, GIANT_BLADE, DRAGON_SLAYER, NO_WEAPON = 0xFF };
enum { OUTDOORS = 0, BUILDINGS = 4 };
enum { INVALID, EQUIP, EQUIP_ALL };               /* what A on a menu entry does */
enum { ENTER, SET_WEAPON, MENU, SITES };

/* ---- the machine ---- */
static uint8_t ram[0x800];
static CycModRegs cpu;
static bool isolated;
static int hook_on[SITES] = {-1, -1, -1};          /* each hook's enabled state, as last set */
static NESModFunctionEntryCallback callbacks[SITES];
static uint16_t sites[SITES];
static NESModActivationCallback on_activate, on_reset;
static unsigned draws;                            /* Player_DrawSprite calls */

uint8_t cyc_mod_peek(uint16_t addr) { return addr < 0x2000 ? ram[addr & 0x7FF] : 0; }
bool cyc_mod_poke(uint16_t addr, uint8_t value) {
    if (addr >= 0x2000) return false;
    ram[addr & 0x7FF] = value;
    return true;
}
void cyc_mod_regs(CycModRegs *out) { *out = cpu; }
void cyc_mod_set_regs(const CycModRegs *in) {
    cpu.a = in->a;
    cpu.x = in->x;
    cpu.y = in->y;
    cpu.s = in->s;
    cpu.p = in->p;
}
bool cyc_mod_isolated(void) { return isolated; }
static int site_of(const char *id) {
    for (int i = 0; i < SITES; i++)
        if (!strcmp(id, hook_ids[i])) return i;
    return -1;
}
int nes_mod_register_function_entry_plugin(const char *id, uint16_t addr, NESModFunctionEntryCallback cb) {
    int i = site_of(id);
    if (i < 0 || callbacks[i]) return 0;
    callbacks[i] = cb;
    sites[i] = addr;
    return 1;
}
int nes_mod_set_function_hook_enabled(const char *id, int on) {
    int i = site_of(id);
    if (i < 0) return 0;
    hook_on[i] = on;
    return 1;
}
int nes_mod_register_reset_callback(NESModActivationCallback cb) {
    on_reset = cb;
    return 1;
}
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    if (strcmp(id, "faxanadu.indoor-equip")) return 0;
    on_activate = cb;
    return 1;
}
void nes_mod_set_local_only(const char *name, int required) { (void)name; (void)required; }

static void set_nz(uint8_t v) { cpu.p = (uint8_t)((cpu.p & ~(P_N | P_Z)) | (v & P_N) | (v ? 0 : P_Z)); }
static void cmp(uint8_t m) {
    set_nz((uint8_t)(cpu.a - m));
    cpu.p = (uint8_t)((cpu.p & ~P_C) | (cpu.a >= m ? P_C : 0));
}

/* A hook site: the callback runs before the instruction, while its hook is on,
 * and never handles it. `z_only`: it may change Z and nothing else. */
static void site(int i, bool z_only) {
    CycModRegs before = cpu;
    if (hook_on[i] == 1) CHECK(callbacks[i](sites[i]) == 0);
    CHECK(cpu.a == before.a && cpu.x == before.x && cpu.y == before.y && cpu.s == before.s);
    CHECK((cpu.p | (z_only ? P_Z : 0)) == (before.p | (z_only ? P_Z : 0)));
}

/* Game_EnterBuilding's first lines. Returns Player_CurWeapon after the site. */
static uint8_t enter_building(void) {
    cpu.a = 0xFF;                                         /* $DE06 LDA #$FF */
    set_nz(cpu.a);
    ram[PLAYER_CUR_WEAPON] = cpu.a;                       /* $DE08 STA Player_CurWeapon */
    site(ENTER, false);                                   /* $DE0B LDA Areas_DefaultMusic, ... */
    return ram[PLAYER_CUR_WEAPON];
}

/* Player_SetWeapon with A = the weapon, as Player_Equip jumps to it. */
static void set_weapon(uint8_t weapon) {
    cpu.a = weapon;
    uint8_t pushed = cpu.a;                               /* $EDEC PHA */
    cpu.a = ram[AREA];                                    /* $EDED LDA Area_CurrentArea */
    set_nz(cpu.a);
    cmp(0x04);                                            /* $EDEF CMP #$04 */
    site(SET_WEAPON, true);
    bool refused = cpu.p & P_Z;                           /* $EDF1 BEQ @_cannotEquip */
    cpu.a = pushed;                                       /* PLA, on either path */
    set_nz(cpu.a);
    ram[SELECTED_WEAPON] = cpu.a;                         /* STA SelectedWeapon */
    if (!refused) {
        ram[PLAYER_CUR_WEAPON] = cpu.a;                   /* STA Player_CurWeapon */
        ++draws;                                          /* JSR Player_DrawSprite */
    }
    cpu.p &= (uint8_t)~P_C;                               /* CLC, RTS */
}

/* Player_LacksItem: the item (category in the top 3 bits) is neither selected
 * nor in its list. */
static bool lacks(uint8_t item) {
    unsigned category = item >> 5;
    uint8_t value = item & 0x1F;
    if (ram[SELECTED_WEAPON + category] == value) return false;
    for (unsigned i = 0; i < ram[NUMBER_OF_WEAPONS + category]; i++)
        if (ram[WEAPON_INVENTORY + category * 4 + i] == value) return false;
    return true;
}

/* A on the cursor's entry of the Select menu's list:
 * PlayerMenu_HandleInventoryMenuInput from @_hasInput. */
static int choose(void) {
    cpu.a = ram[AREA];                                    /* $8B83 LDA Area_CurrentArea */
    set_nz(cpu.a);
    cmp(0x04);                                            /* $8B86 CMP #$04 */
    site(MENU, true);
    if (cpu.p & P_Z) return INVALID;                      /* $8B88 BEQ PlayerMenu_HandleInvalidChoice */
    cpu.a = ram[MENU_SELECTED_INVENTORY];                 /* $8B8A LDA PlayerMenu_SelectedInventory */
    cmp(0x03);                                            /* CMP #$03 */
    if (cpu.p & P_C) return EQUIP;                        /* BCS PlayerMenu_EquipItem: magic and items */
    cpu.a = ram[SELECTED_WEAPON];                         /* LDA SelectedWeapon */
    cmp(DRAGON_SLAYER);
    if (cpu.p & P_Z) return INVALID;                      /* BEQ: the Dragon Slayer's set stays on */
    cpu.a = ram[DATA_ARRAY + ram[MENU_CURSOR]];           /* LDX Menu_CursorPos, LDA DataArray,X */
    cmp(DRAGON_SLAYER);
    if (!(cpu.p & P_Z)) return EQUIP;                     /* BNE PlayerMenu_EquipItem */
    if (lacks(0x03) || lacks(0x23) || lacks(0x43)) return INVALID;   /* the sword, suit and helmet */
    return EQUIP_ALL;
}

static void set_flags(uint8_t p) { cpu.p = p; }

int main(void) {
    /* Registered at the three sites, off until the feature is activated. */
    CHECK(sites[ENTER] == 0xDE0B && sites[SET_WEAPON] == 0xEDF1 && sites[MENU] == 0x8B88);
    CHECK(on_activate && on_reset);
    on_reset();
    for (int i = 0; i < SITES; i++) CHECK(hook_on[i] == 0);

    /* Off: the original. In a building the weapon leaves his hand, a new one
     * is only selected, and the menu refuses everything. */
    ram[AREA] = BUILDINGS;
    ram[SELECTED_WEAPON] = LONG_SWORD;
    CHECK(enter_building() == NO_WEAPON);
    draws = 0;
    set_weapon(GIANT_BLADE);
    CHECK(ram[SELECTED_WEAPON] == GIANT_BLADE && ram[PLAYER_CUR_WEAPON] == NO_WEAPON && draws == 0);
    for (uint8_t inventory = 0; inventory < 5; inventory++) {
        ram[MENU_SELECTED_INVENTORY] = inventory;
        CHECK(choose() == INVALID);
    }

    on_activate();
    for (int i = 0; i < SITES; i++) CHECK(hook_on[i] == 1);
    CHECK(enabled && entered == 0 && weapons_set == 0 && menu_choices == 0);

    /* Into a building: the weapon selected stays in hand, or none. */
    const uint8_t held[] = {DAGGER, LONG_SWORD, GIANT_BLADE, DRAGON_SLAYER, NO_WEAPON};
    for (unsigned i = 0; i < sizeof held; i++) {
        ram[SELECTED_WEAPON] = held[i];
        CHECK(enter_building() == held[i]);
    }
    CHECK(entered == sizeof held);

    /* Player_SetWeapon equips and redraws everywhere, as it does outdoors;
     * only a building's call counts as let through. */
    for (uint8_t area = 0; area < 8; area++) {
        for (uint8_t weapon = DAGGER; weapon <= DRAGON_SLAYER; weapon++) {
            ram[AREA] = area;
            ram[SELECTED_WEAPON] = ram[PLAYER_CUR_WEAPON] = NO_WEAPON;
            draws = 0;
            unsigned before = weapons_set;
            set_flags(0x24);
            set_weapon(weapon);
            CHECK(ram[SELECTED_WEAPON] == weapon && ram[PLAYER_CUR_WEAPON] == weapon && draws == 1);
            CHECK(weapons_set == before + (area == BUILDINGS));
        }
    }

    /* The menu in a building chooses exactly as outdoors, the Dragon Slayer's
     * set included, for every list, entry and weapon in hand. */
    unsigned cases = 0, equips = 0;
    for (uint8_t inventory = 0; inventory < 5; inventory++)
        for (uint8_t entry = DAGGER; entry <= DRAGON_SLAYER; entry++)
            for (unsigned in_hand = 0; in_hand < 4; in_hand++)
                for (unsigned full_set = 0; full_set < 2; full_set++) {
                    memset(ram + WEAPON_INVENTORY, 0, 12);
                    memset(ram + NUMBER_OF_WEAPONS, 0, 3);
                    ram[SELECTED_WEAPON] = (uint8_t[]){DAGGER, LONG_SWORD, DRAGON_SLAYER, NO_WEAPON}[in_hand];
                    ram[SELECTED_WEAPON + 1] = full_set ? 3 : 0;        /* the Battle Suit, or Leather */
                    ram[SELECTED_WEAPON + 2] = full_set ? 3 : 0xFF;     /* the Battle Helmet, or none */
                    if (full_set && ram[SELECTED_WEAPON] != DRAGON_SLAYER) {
                        ram[WEAPON_INVENTORY] = DRAGON_SLAYER;
                        ram[NUMBER_OF_WEAPONS] = 1;
                    }
                    ram[MENU_SELECTED_INVENTORY] = inventory;
                    ram[MENU_CURSOR] = 0;
                    ram[DATA_ARRAY] = entry;
                    ram[AREA] = OUTDOORS;
                    on_reset();
                    int stock = choose();
                    on_activate();
                    CHECK(choose() == stock);                             /* outdoors, nothing changes */
                    ram[AREA] = BUILDINGS;
                    CHECK(choose() == stock);                             /* a building is outdoors now */
                    CHECK(menu_choices == 1);
                    ++cases;
                    equips += stock != INVALID;
                }
    CHECK(cases == 160 && equips > 0 && equips < cases);

    /* Inside an isolated call (Seamless's neighbour rooms) the game is steered
     * the same, but nothing is counted. */
    on_reset();
    on_activate();
    isolated = true;
    ram[AREA] = BUILDINGS;
    ram[SELECTED_WEAPON] = GIANT_BLADE;
    CHECK(enter_building() == GIANT_BLADE);
    set_weapon(LONG_SWORD);
    CHECK(ram[PLAYER_CUR_WEAPON] == LONG_SWORD);
    ram[MENU_SELECTED_INVENTORY] = 4;
    CHECK(choose() == EQUIP);
    CHECK(entered == 0 && weapons_set == 0 && menu_choices == 0);
    isolated = false;

    /* Off again: counts cleared, hooks off, the original back. */
    on_reset();
    CHECK(!enabled && entered == 0);
    ram[SELECTED_WEAPON] = LONG_SWORD;
    CHECK(enter_building() == NO_WEAPON);

    printf("indoor_test: %u checks passed\n", checks);
    return 0;
}
