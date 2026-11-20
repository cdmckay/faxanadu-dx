/* Unit tests for Slain by (fax_slain.c), without the emulator or a ROM. The
 * names table is checked against the game's own tables, copied from bank 14:
 * which entities hurt the hero (SPRITE_CATEGORIES_BY_ENTITY, $B544) and how
 * much (SPRITE_ENTITY_DAMAGES, $B6D7). The hooks run against a model of the
 * machine: CPU RAM, the registers, and the stack as each of Player_ReduceHP's
 * four callers leaves it (the JSRs of the Poison and Sugata's flash, the JMPs
 * of Player_ApplyDamage and Player_HandleHitByMagic). */
#include <stdio.h>
static char logged[128];
#define FAX_SLAIN_LOG(...) snprintf(logged, sizeof logged, __VA_ARGS__)
#include "fax_slain.c"

static unsigned checks;
#define CHECK(test) do{++checks;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(1);}}while(0)

/* ---- the game's tables (bank 14) ---- */
enum { ENEMY = 0, MAGIC = 6, BOSS = 7 };
static const uint8_t CATEGORIES[ENTITY_COUNT] = {
    0,1,1,0,0,0,0,0,0,0,6,0,0,0,0,0, 0,7,7,3,3,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,7,7,7,
    7,7,7,7,2,2,2,2,2,2,2,2,2,2,2,2, 2,2,2,2,2,2,0,0,5,5,5,5,5,5,5,4, 5,6,3,6,6,5,5,5,5,5,5,5,5,5,5,5,
    5,3,3,3,3 };
static const uint8_t DAMAGES[ENTITY_COUNT] = {
    0,0,0,31,7,9,6,3,4,6,28,3,4,5,7,13, 15,20,20,0,0,7,10,10,4,13,5,14,23,0,15,12,
    11,17,18,9,16,11,7,10,16,12,5,6,15,18,28,24, 13,11,40,41,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,8,7,0,0,0,0,0,0,0,0, 0,8,0,28,8,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,252 };
/* chipx86 marks these unused: no screen holds them and nothing spawns them. */
static const uint8_t UNUSED[] = { 0x12, 0x16, 0x1D, 0x24, 0x25, 0x27, 0x29, 0x2B, 0x46 };

/* ---- the machine ---- */
static uint8_t ram[0x800];
static CycModRegs cpu;
static bool isolated;
static int hook_on[2] = { -1, -1 };
static unsigned hidden;                       /* fax_banner_hide calls */
static const char *drawn; static unsigned drawn_alpha;
static bool font = true;

uint8_t cyc_mod_peek(uint16_t addr) { return addr < 0x2000 ? ram[addr & 0x7FF] : 0; }
void cyc_mod_regs(CycModRegs *out) { *out = cpu; }
bool cyc_mod_isolated(void) { return isolated; }
int nes_mod_register_function_entry_plugin(const char *id, uint16_t addr, NESModFunctionEntryCallback cb) {
    return (!strcmp(id, hook_ids[0]) && addr == PLAYER_REDUCE_HP && cb == reduce_hp_hook) ||
           (!strcmp(id, hook_ids[1]) && addr == SPRITE_RESET_STATE && cb == spawn_hook);
}
int nes_mod_set_function_hook_enabled(const char *id, int on) {
    for (int i = 0; i < 2; ++i) if (!strcmp(id, hook_ids[i])) { hook_on[i] = on; return 1; }
    return 0;
}
int nes_mod_register_reset_callback(NESModActivationCallback cb) { return cb == deactivate; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    return !strcmp(id, "faxanadu.slain-by") && cb == activate;
}
int nes_mod_register_savestate_hook(const char *id, NESModSavestateGet get, NESModSavestateSet set) {
    return !strcmp(id, "faxanadu.slain-by") && get == state_get && set == state_set;
}
int nes_mod_register_savestate_validator(const char *id, NESModSavestateValidate v) {
    return !strcmp(id, "faxanadu.slain-by") && v == state_validate;
}
void fax_banner_hide(void) { ++hidden; }
const uint32_t *fax_banner_draw(const uint32_t *picture, int *w, int *h, const char *line, unsigned alpha) {
    (void)w; (void)h;
    static char copy[64];
    snprintf(copy, sizeof copy, "%s", line);
    drawn = copy; drawn_alpha = alpha;
    return picture + 1;                       /* "a canvas": not the picture */
}
bool fax_text_loaded(void) { return font; }
bool fax_text_load(void) { return font; }

static void push(uint8_t v) { ram[0x100 | cpu.s] = v; cpu.s = (uint8_t)(cpu.s - 1); }
static void jsr_from(uint16_t jsr) {          /* JSR pushes the address of its own last byte */
    uint16_t last = (uint16_t)(jsr + 2);
    push((uint8_t)(last >> 8)); push((uint8_t)last);
}

static const uint8_t MONODRON = 0x2A, SUGATA = 0x18, EXECUTION_HOOD = 0x10, NASH = 0x0D, ISHIISU = 0x0F,
                     SHOT = 0x51, ROCK = 0x03, ZORUGERIRU = 0x31, PAKUKAME = 0x30, LILITH = 0x09, POISON_JAR = 0x4B;

static void fresh(void) {
    memset(ram, 0, sizeof ram);
    memset(ram + SPRITE_ENTITIES, 0xFF, SLOTS);
    ram[TEXTBOX_CLOSED] = 1;                    /* no box open */
    isolated = false;
    hidden = 0; drawn = NULL;
    deactivate(); activate();
}
static void sprite(uint8_t slot, uint8_t entity) { ram[SPRITE_ENTITIES + slot] = entity; }
static void hp(unsigned whole, unsigned fraction) { ram[PLAYER_HP_U] = (uint8_t)whole; ram[PLAYER_HP_L] = (uint8_t)fraction; }
/* A hit of `whole`.`fraction` HP, from the sprite in `slot`, as Player_ApplyDamage
 * or Player_HandleHitByMagic reach Player_ReduceHP: by JMP, so S + 1 holds the
 * return into whatever ran the sprite's collision (CurrentSprite_CheckHitPlayer's
 * caller, in the sprite loop). */
static void hit(uint8_t slot, unsigned whole, unsigned fraction) {
    ram[CURRENT_SPRITE] = slot;
    ram[HEALTH_DELTA_U] = (uint8_t)whole; ram[HEALTH_DELTA_L] = (uint8_t)fraction;
    cpu.s = 0xF0;
    jsr_from(0xE5F0);                           /* any return address but the two below */
    CHECK(reduce_hp_hook(PLAYER_REDUCE_HP) == 0);
}
static void hit_from(uint16_t jsr, uint8_t slot, unsigned whole) {
    ram[CURRENT_SPRITE] = slot;
    ram[HEALTH_DELTA_U] = (uint8_t)whole; ram[HEALTH_DELTA_L] = 0;
    cpu.s = 0xF0;
    jsr_from(jsr);
    CHECK(reduce_hp_hook(PLAYER_REDUCE_HP) == 0);
}
static void spawn(uint8_t parent_slot, uint8_t child_slot, uint8_t child) {
    sprite(child_slot, child);                  /* stored before the JMP Sprite_Maybe_ResetState */
    cpu.x = parent_slot; cpu.y = child_slot;
    CHECK(spawn_hook(SPRITE_RESET_STATE) == 0);
}
static const char *showing(void) {
    static char line[40];
    return s.age < SHOWN ? line_for(s.named, line) : NULL;
}

static void the_names(void) {
    unsigned named = 0;
    for (unsigned e = 0; e < ENTITY_COUNT; ++e) {
        bool unused = memchr(UNUSED, (int)e, sizeof UNUSED) != NULL;
        bool hurts = (CATEGORIES[e] == ENEMY || CATEGORIES[e] == MAGIC || CATEGORIES[e] == BOSS) && DAMAGES[e] && !unused;
        int kind = spawned_index((uint8_t)e);
        if (NAMES[e]) {
            ++named;
            /* Only what hurts the hero, and never a shot or a rock. */
            CHECK(hurts && kind < 0 && CATEGORIES[e] != MAGIC);
            /* "a" or "an" by the sound, or none for the two who have a name of their own. */
            const char *n = NAMES[e];
            if (!strcmp(n, "King Grieve") || !strcmp(n, "Shadow Eura")) CHECK(CATEGORIES[e] == BOSS);
            else if (!strncmp(n, "an ", 3)) CHECK(strchr("AEIOU", n[3]));
            else CHECK(!strncmp(n, "a ", 2) && !strchr("AEIOU", n[2]));
            /* The banner is a row of the screen's 32 tiles: the line, a blank either side, the frame. */
            char line[40];
            CHECK(line_for((uint8_t)e, line) && strlen(line) + 4 <= 32);
        } else if (hurts) {
            /* What hurts and has no name is spawned, by sprites that do. */
            CHECK(kind >= 0);
            for (unsigned j = 0; j < 3; ++j)
                if (SPAWNED[kind].by[j] != NO_ONE) CHECK(NAMES[SPAWNED[kind].by[j]] && CATEGORIES[SPAWNED[kind].by[j]] != MAGIC);
        }
    }
    CHECK(named == 38);
    for (unsigned k = 0; k < sizeof SPAWNED / sizeof SPAWNED[0]; ++k)   /* every shot is magic; the rock an "enemy" */
        CHECK(SPAWNED[k].spawned == ROCK ? CATEGORIES[ROCK] == ENEMY : CATEGORIES[SPAWNED[k].spawned] == MAGIC);
    CHECK(!strcmp(NAMES[SUGATA], "a Sugata") && !strcmp(NAMES[MONODRON], "a Monodron") &&
          !strcmp(NAMES[0x47], "a Zozura") && !strcmp(NAMES[EXECUTION_HOOD], "an Execution Hood"));
    CHECK(!name_of(POISON_JAR) && !strcmp(name_of(POISON), "Poison"));
}

static void a_killing_touch(void) {
    fresh();
    sprite(3, MONODRON);
    hp(80, 0); hit(3, 5, 0);                    /* not his last HP: nothing */
    CHECK(!showing() && deaths == 0 && unnamed == 0 && hidden == 0);
    hp(5, 0); hit(3, 5, 0);                     /* to 0 exactly, which the game survives (no borrow) */
    CHECK(!showing());
    hp(4, 0x80); hit(3, 4, 0x90);               /* the fraction borrows */
    CHECK(showing() && !strcmp(showing(), "Slain by a Monodron") && s.how == HOW_TOUCH && s.age == 0);
    CHECK(deaths == 1 && hidden == 1 && !strcmp(last, "Slain by a Monodron"));
}

static void the_elixir_saves_him(void) {
    fresh();
    sprite(0, MONODRON);
    ram[SPECIAL_ITEMS] = ELIXIR;
    hp(1, 0); hit(0, 5, 0);
    CHECK(!showing() && deaths == 0 && unnamed == 0);
}

static void a_shot_names_its_caster(void) {
    fresh();
    sprite(7, EXECUTION_HOOD);
    spawn(7, 2, SHOT);
    CHECK(s.child[2] == SHOT && s.parent[2] == EXECUTION_HOOD && spawns == 1);
    sprite(4, NASH);                            /* another caster of the same shot: the record decides */
    hp(3, 0); hit(2, 8, 0);
    CHECK(showing() && !strcmp(showing(), "Slain by an Execution Hood") && s.how == HOW_SHOT);
    /* A falling rock is Zorugeriru's. */
    fresh();
    sprite(1, ZORUGERIRU);
    spawn(1, 5, ROCK);
    hp(20, 0); hit(5, 31, 0);
    CHECK(!strcmp(showing(), "Slain by a Zorugeriru") && s.how == HOW_SHOT);
    /* A spawn with a name of its own is named itself: Pakukame's Liliths. */
    fresh();
    sprite(0, PAKUKAME);
    spawn(0, 6, LILITH);
    hp(2, 0); hit(6, 6, 0);
    CHECK(!strcmp(showing(), "Slain by a Lilith") && s.how == HOW_TOUCH);
}

static void a_shot_with_no_spawn_seen(void) {
    /* Its slot's record is gone (the old shot left, a new one came where no
     * spawn was seen): the one kind of caster on screen is named. */
    fresh();
    sprite(7, EXECUTION_HOOD);
    spawn(7, 2, SHOT);
    sprite(2, 0xFF);
    fax_slain_frame_end();                      /* the shot is gone: its slot forgets */
    CHECK(s.child[2] == NO_ONE && s.parent[2] == NO_ONE);
    sprite(2, SHOT);
    sprite(5, NASH);
    sprite(7, 0xFF);
    hp(1, 0); hit(2, 8, 0);
    CHECK(!strcmp(showing(), "Slain by a Nash"));
    /* Two kinds that cast it: no telling which, so nothing. */
    fresh();
    sprite(2, SHOT); sprite(5, NASH); sprite(6, ISHIISU);
    hp(1, 0); hit(2, 8, 0);
    CHECK(!showing() && unnamed == 1 && deaths == 0 && hidden == 0 && s.how == HOW_SHOT);
    /* Two of one kind are one name. */
    fresh();
    sprite(2, SHOT); sprite(5, NASH); sprite(6, NASH);
    hp(1, 0); hit(2, 8, 0);
    CHECK(!strcmp(showing(), "Slain by a Nash"));
    /* A record for another kind in that slot does not count. */
    fresh();
    sprite(7, 0x2D);                            /* Ripasheiku's fire, $0A, in slot 2 */
    spawn(7, 2, 0x0A);
    sprite(2, SHOT);                            /* replaced within the frame */
    hp(1, 0); hit(2, 8, 0);
    CHECK(!showing() && unnamed == 1);
}

static void the_flash_and_the_poison(void) {
    fresh();
    sprite(6, SUGATA);
    hp(9, 0);
    hit_from(0xAB5D, 6, 10);                    /* SpriteBehavior_FlashScreenHitPlayer's JSR Player_ReduceHP */
    CHECK(!strcmp(showing(), "Slain by a Sugata") && s.how == HOW_FLASH);
    fresh();
    sprite(6, SUGATA);
    hp(3, 0); hit(6, 4, 0);                     /* its touch */
    CHECK(!strcmp(showing(), "Slain by a Sugata") && s.how == HOW_TOUCH);
    /* The Poison: told by its JSR, whatever CurrentSpriteIndex holds. */
    fresh();
    sprite(1, MONODRON);
    hp(16, 0);
    hit_from(0xC85D, 1, 16);                    /* Player_PickUpPoison's JSR Player_ReduceHP: 16.0 from 16.0 lives */
    CHECK(!showing());
    hp(15, 0xFF);
    hit_from(0xC85D, 1, 16);
    CHECK(!strcmp(showing(), "Slain by Poison") && s.how == HOW_POISON);
    /* No sprite at CurrentSpriteIndex: nothing. */
    fresh();
    hp(1, 0); hit(3, 5, 0);
    CHECK(!showing() && unnamed == 1);
    ram[CURRENT_SPRITE] = 0xFF; hit(0xFF, 5, 0);
    CHECK(!showing() && unnamed == 2);
}

static void the_stack_wraps(void) {
    fresh();
    sprite(6, SUGATA);
    hp(1, 0);
    ram[CURRENT_SPRITE] = 6;
    ram[HEALTH_DELTA_U] = 10;
    cpu.s = 0x00;                               /* the return address straddles $01FF/$0100 */
    jsr_from(0xAB5D);
    CHECK(cpu.s == 0xFE && ram[0x100] == 0xAB && ram[0x1FF] == 0x5F);
    reduce_hp_hook(PLAYER_REDUCE_HP);
    CHECK(s.how == HOW_FLASH);
}

static void seamless_rooms_and_off(void) {
    fresh();
    isolated = true;                            /* a neighboring room's actors, in their own machine */
    sprite(7, EXECUTION_HOOD);
    spawn(7, 2, SHOT);
    hp(1, 0); hit(7, 15, 0);
    CHECK(!showing() && s.child[2] == NO_ONE && spawns == 0 && unnamed == 0);
    isolated = false;
    deactivate();
    CHECK(hook_on[0] == 0 && hook_on[1] == 0 && !enabled);
    hit(7, 15, 0);
    spawn(7, 2, SHOT);
    CHECK(!showing() && s.child[2] == NO_ONE);
    activate();
    CHECK(hook_on[0] == 1 && hook_on[1] == 1);
}

static void the_line_plays_out(void) {
    fresh();
    sprite(3, MONODRON);
    hp(1, 0); hit(3, 7, 0);
    uint32_t picture[4] = { 0 };
    int w = 2, h = 2;
    CHECK(fax_slain_present(picture, &w, &h) == picture + 1 && !strcmp(drawn, "Slain by a Monodron"));
    CHECK(drawn_alpha == 255 / FADE_IN);
    unsigned frames = 0;
    while (showing()) {
        drawn = NULL;
        ram[TEXTBOX_CLOSED] = 1;
        const uint32_t *p = fax_slain_present(picture, &w, &h);
        if (s.age >= FADE_IN && s.age < FADE_IN + HOLD) CHECK(drawn_alpha == 255);
        CHECK(p == picture + 1 || (s.age >= SHOWN - 1 && p == picture));
        ram[TEXTBOX_CLOSED] = 0;                /* a box open: hidden, but time goes on */
        drawn = NULL;
        CHECK(fax_slain_present(picture, &w, &h) == picture && !drawn);
        fax_slain_frame_end();
        ++frames;
    }
    /* It is gone before the dissolve's 256 frames end and the Guru speaks. */
    CHECK(frames == SHOWN && SHOWN < 256);
    ram[TEXTBOX_CLOSED] = 1;
    CHECK(fax_slain_present(picture, &w, &h) == picture);
    /* With no font, nothing is drawn, and it says so once. */
    fresh();
    font = false;
    sprite(3, MONODRON);
    hp(1, 0); hit(3, 7, 0);
    fax_slain_frame_end();
    CHECK(font_failed && strstr(logged, "font"));
    font = true;
}

static void save_states(void) {
    fresh();
    sprite(7, EXECUTION_HOOD);
    spawn(7, 2, SHOT);
    hp(1, 0); hit(2, 8, 0);
    for (int i = 0; i < 30; ++i) fax_slain_frame_end();
    uint8_t buf[64];
    int n = state_get(buf, sizeof buf);
    CHECK(n == RECORD && state_get(buf, RECORD - 1) == -1);
    Slain was = s;
    forget();
    CHECK(state_set(buf, n) && !memcmp(&s, &was, sizeof s) && s.age == 30 && !strcmp(showing(), "Slain by an Execution Hood"));
    /* Saved without the feature: no line, no spawners. */
    CHECK(state_set(buf, 0) && !showing() && s.child[2] == NO_ONE);
    /* Refused: another version, a bad how, an age past the line, a line for no one. */
    uint8_t bad[64];
    memcpy(bad, buf, (size_t)n); bad[0] = 2; CHECK(!state_validate(bad, n));
    memcpy(bad, buf, (size_t)n); bad[2] = HOW_POISON + 1; CHECK(!state_validate(bad, n));
    memcpy(bad, buf, (size_t)n); bad[3] = SHOWN + 1; bad[4] = 0; CHECK(!state_validate(bad, n));
    memcpy(bad, buf, (size_t)n); bad[1] = POISON_JAR; CHECK(!state_validate(bad, n));
    memcpy(bad, buf, (size_t)n); bad[1] = NO_ONE; bad[3] = SHOWN; CHECK(state_validate(bad, n));
    CHECK(!state_validate(buf, n - 1) && !state_set(buf, n - 1));
}

int main(void) {
    forget();
    the_names();
    a_killing_touch();
    the_elixir_saves_him();
    a_shot_names_its_caster();
    a_shot_with_no_spawn_seen();
    the_flash_and_the_poison();
    the_stack_wraps();
    seamless_rooms_and_off();
    the_line_plays_out();
    save_states();
    printf("slain_test: %u checks passed\n", checks);
    return 0;
}
