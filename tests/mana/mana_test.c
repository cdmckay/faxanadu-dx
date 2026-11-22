/* Unit tests for MP from kills (fax_mana.c), without the emulator or a ROM.
 * The module runs against a model of the machine: CPU RAM, the registers and
 * the stack as each of the game's waits leaves them when a frame ends (the
 * JSRs push their real return addresses, and the ROM has the JSRs there),
 * whether the screen is up, bank 14's ENEMY_EXPERIENCE and
 * SPRITE_CATEGORIES_BY_ENTITY for the enemies used here (as the ROM has
 * them), the hook site as the engine dispatches it (with X the slot, as both
 * kills call Player_AddExperienceFromSprite), and Player_SetMP and
 * UI_DrawPlayerHP as the committed call runs them: the cap, each bar's two
 * PPU buffer commands, and PPUBuffer_WaitForCapacity, which spins for ever
 * (the call fails, nothing committed) when the buffer has no room. So the
 * tests check what the hero's MP and its bar do, kill by kill and frame by
 * frame. The Magician's lesson (fax_lesson.c) is one flag here, whether he
 * has taught the hero. */
#include "fax_mana.c"

static unsigned checks;
#define CHECK(test) do{++checks;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(1);}}while(0)

/* ---- the machine ---- */
static uint8_t ram[0x800];
static uint8_t rom[0x8000];                  /* $8000-$FFFF: bank 14's two tables, and the JSRs at the waits */
static CycModRegs cpu;
static bool isolated, refuse;               /* refuse: the next committed call fails */
static unsigned calls, mp_drawn, hp_drawn;  /* committed calls run; bars queued */
static int bar_mp = -1, bar_hp = -1;        /* the values the bars were last queued with */

#define MP          ram[0x39A]
#define HP          ram[0x431]
#define SCREEN_READY ram[0x0E]              /* Screen_ReadyState: 0 once loaded */
#define INTERRUPTS  ram[0x13]               /* Game_InterruptHandlersEnabled */
#define SCROLLING   ram[0x54]               /* Screen_ScrollDirection: $FF not scrolling */
#define READ        ram[0x1F]               /* PPUBuffer_ReadOffset */
#define WRITE       ram[0x20]               /* PPUBuffer_WriteOffset */
#define ENTITIES    0x2CC                   /* CurrentSprites_Entities, 8 slots */
#define DEATH       0x13                    /* the death effect a kill leaves in the slot */

/* Entities, and what bank 14's tables give each (ENEMY_EXPERIENCE at $B60E,
 * SPRITE_CATEGORIES_BY_ENTITY at $B544). */
enum {
    ROCK = 0x03,          /* Zorugeriru's falling rock: an enemy's category, no experience */
    HORNET = 0x07, ISHIISU = 0x0F, ROKUSUTAHN = 0x11, CHARRON = 0x15, SUGATA = 0x18, GRIMLOCK = 0x19,
    WOLFMAN = 0x21, MONODRON = 0x2A, RIPASHEIKU = 0x2D, ZORADOHNA = 0x2E, KING_GRIEVE = 0x32, ZOZURA = 0x47,
};
static const struct { uint8_t entity, experience, category; } TABLE[] = {
    { ROCK, 0, 0 }, { HORNET, 25, 0 }, { ISHIISU, 10, 0 }, { ROKUSUTAHN, 120, 7 }, { CHARRON, 65, 0 },
    { SUGATA, 13, 0 }, { GRIMLOCK, 58, 0 }, { WOLFMAN, 49, 0 }, { MONODRON, 20, 0 }, { RIPASHEIKU, 90, 7 },
    { ZORADOHNA, 158, 7 }, { KING_GRIEVE, 0, 7 }, { ZOZURA, 30, 0 },
};
static void load_tables(void) {
    for (unsigned i = 0; i < sizeof TABLE / sizeof TABLE[0]; ++i) {
        rom[0xB60E - 0x8000 + TABLE[i].entity] = TABLE[i].experience;
        rom[0xB544 - 0x8000 + TABLE[i].entity] = TABLE[i].category;
    }
    /* JSR WaitForNextFrame ($CA25) at Game_MainLoop's top, the pause loop,
     * the scroll path and a text box's wait (bank 12); JSR Sprites_UpdateAll
     * ($8000) in the pass. */
    static const uint16_t waits[] = { 0xDB4D, 0xE036, 0xDBC4, 0x8A10 };
    for (unsigned i = 0; i < sizeof waits / sizeof waits[0]; ++i)
        memcpy(rom + (waits[i] - 0x8000), (const uint8_t[]){ 0x20, 0x25, 0xCA }, 3);
    memcpy(rom + (0xDB65 - 0x8000), (const uint8_t[]){ 0x20, 0x00, 0x80 }, 3);
}

uint8_t cyc_mod_peek(uint16_t addr) {
    if (addr < 0x2000) return ram[addr & 0x7FF];
    return addr >= 0x8000 ? rom[addr - 0x8000] : 0;
}
bool cyc_mod_poke(uint16_t addr, uint8_t value) {
    if (addr >= 0x2000) return false;
    ram[addr & 0x7FF] = value;
    return true;
}
void cyc_mod_regs(CycModRegs *out) { *out = cpu; }
void cyc_mod_set_regs(const CycModRegs *in) { uint16_t pc = cpu.pc; cpu = *in; cpu.pc = pc; }
bool cyc_mod_isolated(void) { return isolated; }
static bool taught;                          /* the Magician's lesson, learned */
bool fax_lesson_learned(void) { return taught; }

/* PPUBuffer_HasCapacity: ReadOffset - WriteOffset is 0 (empty) or at least $24. */
static bool has_capacity(uint8_t write) { uint8_t room = (uint8_t)(READ - write); return room == 0 || room >= 0x24; }
/* Player_SetMP ($FA85), A the MP: capped at 80, stored; UI_DrawPlayerHP
 * ($C0BD): Player_HP_U, capped at 80, stored. Then UI_SetHUDBarValue draws
 * the bar: the tiles (3 + width + 1 bytes) and the partial tile's pattern
 * (3 + 16), each queued after PPUBuffer_WaitForCapacity. Run in the
 * committed call: all or nothing. */
bool cyc_mod_call_commit(uint16_t routine, CycModRegs *regs) {
    CHECK(routine == 0xFA85 || routine == 0xC0BD);
    CHECK(regs->s == cpu.s);                 /* JSRs from where the CPU is */
    ++calls;
    if (refuse) { refuse = false; return false; }
    uint8_t w = WRITE;
    if (!has_capacity(w)) return false;      /* spins until its cycle budget runs out */
    w = (uint8_t)(w + 3 + ram[0x4EE] + 1);
    if (!has_capacity(w)) return false;
    w = (uint8_t)(w + 3 + 16);
    WRITE = w;
    if (routine == 0xFA85) {
        MP = (uint8_t)(regs->a > 80 ? 80 : regs->a);
        bar_mp = MP;
        ++mp_drawn;
    } else {
        HP = (uint8_t)(HP > 80 ? 80 : HP);
        bar_hp = HP;
        ++hp_drawn;
    }
    regs->a = 0x10; regs->x = w; regs->y = 0x08; regs->p = 0x26;   /* what the routine leaves */
    cyc_mod_set_regs(regs);                  /* the engine: they become the CPU's */
    return true;
}

/* The engine's hook table: one site, with an enabled flag. */
static const char *site_id;
static uint16_t site_addr;
static NESModFunctionEntryCallback site_cb;
static int site_on;
int nes_mod_register_function_entry_plugin(const char *id, uint16_t addr, NESModFunctionEntryCallback cb) {
    CHECK(!site_cb);
    site_id = id; site_addr = addr; site_cb = cb;
    return 1;
}
int nes_mod_set_function_hook_enabled(const char *id, int on) {
    if (!site_id || strcmp(id, site_id)) return 0;
    site_on = on;
    return 1;
}
static const char *local_only;
void nes_mod_set_local_only(const char *name, int required) { local_only = required ? name : NULL; }
static NESModActivationCallback reset_cb, plugin_cb;
static const char *plugin_id;
int nes_mod_register_reset_callback(NESModActivationCallback cb) { reset_cb = cb; return 1; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    plugin_id = id; plugin_cb = cb;
    return 1;
}
static NESModSavestateGet save_get;
static NESModSavestateSet save_set;
static NESModSavestateValidate save_validate;
int nes_mod_register_savestate_hook(const char *id, NESModSavestateGet get, NESModSavestateSet set) {
    if (strcmp(id, "faxanadu.mana-refill")) return 0;
    save_get = get; save_set = set;
    return 1;
}
int nes_mod_register_savestate_validator(const char *id, NESModSavestateValidate validate) {
    if (strcmp(id, "faxanadu.mana-refill")) return 0;
    save_validate = validate;
    return 1;
}

/* ---- the game ---- */

/* An enemy in `slot` dies: the weapon's or magic's death code calls
 * Player_AddExperienceFromSprite with X the slot, where the engine runs the
 * site if it is on; then the game adds its experience and puts the death
 * effect in the slot. */
static unsigned experience;
static void kill(unsigned slot, uint8_t entity) {
    ram[ENTITIES + slot] = entity;
    cpu.x = (uint8_t)slot;
    if (site_on) CHECK(site_cb(site_addr) == 0);   /* it only observes */
    experience += rom[0xB60E - 0x8000 + entity];
    ram[ENTITIES + slot] = DEATH;
}

/* The stack as JSRs left it: S, and the return address the innermost pushed. */
static void at(uint8_t s, uint16_t pushed) {
    cpu.s = s;
    ram[0x100 | (uint8_t)(s + 1)] = (uint8_t)pushed;
    ram[0x100 | (uint8_t)(s + 2)] = (uint8_t)(pushed >> 8);
}
typedef enum {
    PLAYING,       /* Game_MainLoop ($DB45: TXS with $FF) in WaitForNextFrame from $DB4D */
    PAUSE,         /* GameLoop_CheckPauseGame's loop: JSR WaitForNextFrame at $E036, under the JSR at $DB74 */
    TEXT,          /* a script's text box, waiting on its frames in bank 12 */
    LAG,           /* a pass still in Sprites_UpdateAll (JSR at $DB65) as the frame ends */
    SCROLL,        /* Game_MainLoop's scroll loop: JSR WaitForNextFrame at $DBC4, scrolling */
    LOADING,       /* waiting at the loop's top with a screen not yet loaded, the interrupt handlers off */
} Where;
static void frame_ends(Where where) {
    SCREEN_READY = 0; INTERRUPTS = 1; SCROLLING = 0xFF;
    switch (where) {
    case PLAYING: at(0xFD, 0xDB4F); break;
    case PAUSE:   ram[0x1FE] = 0x76; ram[0x1FF] = 0xDB; at(0xFB, 0xE038); break;
    case TEXT:    at(0xF7, 0x8A12); break;
    case LAG:     at(0xFD, 0xDB67); break;
    case SCROLL:  at(0xFD, 0xDBC6); SCROLLING = 1; break;
    case LOADING: at(0xFD, 0xDB4F); SCREEN_READY = 2; INTERRUPTS = 0; break;
    }
    fax_mana_frame_end();
}
/* The NMI that starts each frame sends the PPU buffer. */
static void nmi(void) { READ = WRITE; }
static void frames(Where where, unsigned n) { for (unsigned i = 0; i < n; ++i) { nmi(); frame_ends(where); } }
/* Player_ReduceMP, in a pass of the main loop: the spell's cost, if MP covers it. */
static void cast(uint8_t cost) { if (MP >= cost) MP = (uint8_t)(MP - cost); }

static void fresh_as(uint8_t mp, bool learned) {
    reset_cb();
    plugin_cb();
    memset(ram, 0, sizeof ram);
    ram[0x4EE] = 10;                         /* UI_MPAndHPBarWidth, as UI_DrawHUDSprites sets it */
    MP = mp;
    HP = 80;
    calls = mp_drawn = hp_drawn = experience = 0;
    bar_mp = bar_hp = -1;
    isolated = refuse = false;
    taught = learned;
    cpu = (CycModRegs){ .a = 0x00, .x = 0x22, .y = 0x33, .s = 0xFD, .p = 0x02 };
    frames(PLAYING, 1);
}
static void fresh(uint8_t mp) { fresh_as(mp, false); }

/* The MP one kill of `entity` gives from `mp`, with the lesson or not. */
static int gives(uint8_t entity, bool learned, uint8_t mp) {
    fresh_as(mp, learned);
    kill(7, entity);
    return MP - mp;
}

/* ---- the tests ---- */

static void registers(void) {
    CHECK(reset_cb == deactivate && plugin_cb == activate && !strcmp(plugin_id, "faxanadu.mana-refill"));
    CHECK(save_get == state_get && save_set == state_set && save_validate == state_validate);
    CHECK(!strcmp(site_id, "faxanadu.mana-refill.kill") && site_addr == 0x8B87 && site_cb == kill_hook);
    plugin_cb();
    CHECK(site_on && local_only && !strcmp(local_only, "MP from kills"));
    reset_cb();
    CHECK(!site_on);
}

/* experience² / 256, at least 1; a boss 40; doubled by the lesson. */
static void per_kill_amounts(void) {
    static const struct { uint8_t entity; int mp; } want[] = {
        { ISHIISU, 1 },        /* 10: 100 / 256 is 0, so the least */
        { SUGATA, 1 },         /* 13 */
        { MONODRON, 1 },       /* 20: 400 / 256 */
        { HORNET, 2 },         /* 25: 625 / 256 */
        { ZOZURA, 3 },         /* 30: 900 / 256 */
        { WOLFMAN, 9 },        /* 49: 2401 / 256 */
        { GRIMLOCK, 13 },      /* 58: 3364 / 256 */
        { CHARRON, 16 },       /* 65: 4225 / 256, a Tilte */
        { ROCK, 1 },           /* no experience: still a kill */
        { ROKUSUTAHN, 40 }, { RIPASHEIKU, 40 }, { ZORADOHNA, 40 },   /* bosses, whatever their experience */
        { KING_GRIEVE, 40 },   /* a boss with none */
    };
    for (unsigned i = 0; i < sizeof want / sizeof want[0]; ++i) {
        CHECK(gives(want[i].entity, false, 0) == want[i].mp);
        CHECK(seen.kills == 1 && seen.entity == want[i].entity && seen.mp == want[i].mp && seen.gained == seen.mp);
        CHECK(seen.bosses == (want[i].mp == BOSS_MP));
        CHECK(gives(want[i].entity, true, 0) == 2 * want[i].mp);   /* the lesson: twice */
    }
    /* The early fights and the mid-game ones the numbers were tuned for. */
    fresh(0);
    kill(7, MONODRON); kill(6, ZOZURA);      /* the Trunk's screen 3 */
    CHECK(MP == 4);                          /* two Deluges */
    fresh(0);
    kill(7, MONODRON); kill(6, MONODRON);    /* its screens 1 and 5 */
    CHECK(MP == 2);                          /* one */
    fresh(0);
    kill(7, GRIMLOCK); kill(6, HORNET);      /* a screen of the Mist */
    CHECK(MP == 15);                         /* about a Tilte */
}

/* A kill's MP is in RAM at once, so a cast or a drain that frame has it; its
 * bar is drawn at the end of the frame, as the game draws it. */
static void a_kill_gives_mp_at_once(void) {
    fresh(10);
    nmi();
    kill(7, ZOZURA);
    CHECK(MP == 13 && experience == 30 && bars == BAR_MP && calls == 0);
    cast(2);                                 /* Deluge in the same pass */
    CHECK(MP == 11);
    cpu.a = 0x00; cpu.x = 0x44; cpu.y = 0x55; cpu.p = 0x02;   /* WaitForNextFrame: LDA latch gave 0, Z set */
    frame_ends(PLAYING);
    CHECK(MP == 11 && bar_mp == 11 && mp_drawn == 1 && hp_drawn == 0 && bars == 0);
    CHECK(cpu.a == 0x00 && cpu.x == 0x44 && cpu.y == 0x55 && cpu.p == 0x02 && cpu.s == 0xFD);
    frames(PLAYING, 100);
    CHECK(calls == 1);                       /* nothing more to draw */
    /* Two kills in a frame: both, and one bar. */
    nmi();
    kill(6, ZOZURA); kill(7, MONODRON);
    frame_ends(PLAYING);
    CHECK(MP == 15 && seen.kills == 3 && mp_drawn == 2 && bar_mp == 15);
}

/* MP stops at 80: the kill counts, the bar is left alone when nothing changed. */
static void the_cap(void) {
    fresh(78);
    kill(7, CHARRON);
    CHECK(MP == 80 && seen.mp == 16 && seen.gained == 2);
    frames(PLAYING, 1);
    CHECK(bar_mp == 80 && mp_drawn == 1);
    kill(7, CHARRON);
    CHECK(MP == 80 && seen.kills == 2 && seen.gained == 2 && bars == 0);
    frames(PLAYING, 10);
    CHECK(mp_drawn == 1);
    fresh_as(30, true);
    kill(7, RIPASHEIKU);                     /* a boss, taught: 80, the whole bar */
    CHECK(MP == 80 && seen.mp == 80 && seen.bosses == 1);
}

/* Nothing comes back over time: not playing, not after a cast, not with the
 * lesson. */
static void no_refill_over_time(void) {
    static const bool learned[] = { false, true };
    for (unsigned i = 0; i < 2; ++i) {
        fresh_as(40, learned[i]);
        nmi(); cast(2); frame_ends(PLAYING);
        frames(PLAYING, 10000);
        CHECK(MP == 38 && calls == 0);
        fresh_as(0, learned[i]);             /* a new game: Game_Start leaves MP at 0 */
        frames(PLAYING, 10000);
        CHECK(MP == 0 && calls == 0);
    }
}

/* A flight's drain (an MP every 32 frames, a Novice's, which Recharging items
 * takes from whatever MP there is) goes on, and a kill mid-flight gives its
 * MP all the same. */
static void nothing_holds_it(void) {
    fresh(20);
    for (unsigned n = 1; n <= 320; ++n) {
        nmi();
        if (n % 32 == 0) MP = (uint8_t)(MP - 1);
        if (n == 100) kill(7, GRIMLOCK);
        frame_ends(PLAYING);
    }
    CHECK(MP == 20 - 10 + 13 && mp_drawn == 1);
}

/* The bar waits for a frame that ends with the CPU in WaitForNextFrame, from
 * anywhere, on a screen that is up, with room in the PPU buffer. */
static void the_bar_waits_for_room(void) {
    static const Where held[] = { LAG, SCROLL, LOADING };
    for (unsigned i = 0; i < sizeof held / sizeof held[0]; ++i) {
        fresh(10);
        kill(7, ZOZURA);
        frames(held[i], 300);
        CHECK(MP == 13 && calls == 0 && bars == BAR_MP && seen.deferred == 300);
        frames(PLAYING, 1);
        CHECK(bar_mp == 13 && bars == 0);
    }
    static const Where drawn_at[] = { PLAYING, PAUSE, TEXT };
    for (unsigned i = 0; i < sizeof drawn_at / sizeof drawn_at[0]; ++i) {
        fresh(10);
        kill(7, ZOZURA);
        frames(drawn_at[i], 1);
        CHECK(bar_mp == 13 && bars == 0 && seen.deferred == 0);
    }
    /* Room for the tiles, not then for the pattern: the next frame that has. */
    fresh(10);
    kill(7, ZOZURA);
    READ = 0x10; WRITE = (uint8_t)(READ - 49);
    frame_ends(PLAYING);
    CHECK(calls == 0 && seen.deferred == 1 && bars == BAR_MP);
    WRITE = (uint8_t)(READ - 50);            /* just enough */
    frame_ends(PLAYING);
    CHECK(mp_drawn == 1 && bars == 0 && WRITE == (uint8_t)(READ - 50 + 33));
    /* A call that fails anyway draws nothing, and it tries again. */
    fresh(10);
    kill(7, ZOZURA);
    refuse = true;
    frames(PLAYING, 1);
    CHECK(calls == 1 && mp_drawn == 0 && bars == BAR_MP && seen.deferred == 1);
    frames(PLAYING, 1);
    CHECK(mp_drawn == 1 && bar_mp == 13);
}

/* A Guru: HP and MP to 80 at once, and both bars, in the PPU buffer's room. */
static void a_guru_fills_hp_and_mp(void) {
    fresh(5);
    HP = 20;
    ram[0x432] = 0x80;                       /* half an HP: the game's own fills leave the 256ths */
    cpu.s = 0xF3;                            /* between two of the interpreter's actions */
    fax_mana_guru();
    CHECK(HP == 80 && MP == 80 && ram[0x432] == 0x80 && bar_hp == 80 && bar_mp == 80 && bars == 0);
    CHECK(hp_drawn == 1 && mp_drawn == 1 && seen.restores == 1 && WRITE == 66);
    frames(PLAYING, 10);
    CHECK(calls == 2);
    /* Full already: nothing to draw. One full, one not: that one. */
    fax_mana_guru();
    CHECK(calls == 2 && seen.restores == 2);
    MP = 33;
    fax_mana_guru();
    CHECK(MP == 80 && hp_drawn == 1 && mp_drawn == 2);
    /* No room as he begins: each at the first played frame's end that has it. */
    fresh(5);
    HP = 20;
    nmi();
    READ = 0x10; WRITE = (uint8_t)(READ - 60);   /* room for one bar, not two */
    fax_mana_guru();
    CHECK(HP == 80 && MP == 80 && hp_drawn == 1 && mp_drawn == 0 && bars == BAR_MP);
    frame_ends(TEXT);                        /* his box, still full: the NMI has not sent it yet */
    CHECK(mp_drawn == 0 && seen.deferred == 1);
    frames(TEXT, 1);                         /* a frame into his words */
    CHECK(mp_drawn == 1 && bar_mp == 80 && bars == 0);
    /* The lesson changes nothing here. */
    fresh_as(5, true);
    HP = 1;
    fax_mana_guru();
    CHECK(HP == 80 && MP == 80);
}

static void save_states(void) {
    fresh(10);
    kill(7, ZOZURA);
    uint8_t buf[16];
    CHECK(save_get(buf, 1) == -1);
    CHECK(save_get(buf, sizeof buf) == RECORD && buf[0] == RECORD_VERSION && buf[0] == 2 && buf[1] == BAR_MP);
    frames(PLAYING, 1);
    CHECK(bars == 0);
    /* Loaded: the bar still to draw, drawn at the next played frame's end. */
    CHECK(save_set(buf, RECORD) == 1 && bars == BAR_MP);
    frames(PLAYING, 1);
    CHECK(mp_drawn == 2 && bars == 0);
    /* Bad records are refused. */
    uint8_t bad[RECORD];
    memcpy(bad, buf, RECORD); bad[0] = 3;             CHECK(!save_validate(bad, RECORD) && save_set(bad, RECORD) == 0);
    memcpy(bad, buf, RECORD); bad[1] = BARS + 1;      CHECK(!save_validate(bad, RECORD));
    CHECK(!save_validate(buf, 3) && save_validate(buf, RECORD));
    /* #98's refill (version 1): it loads, with nothing to draw, and no MP comes back. */
    uint8_t v1[4] = { 1, 1, 50, 1 };
    MP = 50;
    CHECK(save_validate(v1, 4) && save_set(v1, 4) == 1 && bars == 0);
    frames(PLAYING, 1000);
    CHECK(MP == 50 && mp_drawn == 2);
    uint8_t v1_bad[4] = { 1, 1, 50, 0 };
    CHECK(!save_validate(v1_bad, 4));
    v1_bad[3] = 241;
    CHECK(!save_validate(v1_bad, 4));
    /* A state from before the feature. */
    bars = BAR_MP;
    CHECK(save_validate(buf, 0) && save_set(buf, 0) == 1 && bars == 0);
}

/* The remaster off: no site, no fill; a kill gives what the game gives
 * (experience). Isolated (Seamless's simulation of another room): nothing. */
static void off_and_isolated_do_nothing(void) {
    fresh(10);
    reset_cb();                              /* no activation follows */
    HP = 20;
    kill(7, ZOZURA);
    fax_mana_killed(SUGATA);
    fax_mana_guru();
    frames(PLAYING, 100);
    CHECK(MP == 10 && HP == 20 && experience == 30 && calls == 0 && !site_on);
    plugin_cb();
    isolated = true;
    kill(7, ZOZURA);
    fax_mana_killed(SUGATA);
    fax_mana_guru();
    frames(PLAYING, 100);
    CHECK(MP == 10 && HP == 20 && calls == 0);
    isolated = false;
    kill(7, ZOZURA);
    CHECK(MP == 13);
}

/* A kill the game made in a committed call (Sugata's flash turned back):
 * fax_mana_killed, the same MP. */
static void a_kill_without_the_site(void) {
    fresh(10);
    fax_mana_killed(SUGATA);
    CHECK(MP == 11 && seen.kills == 1 && seen.entity == SUGATA && bars == BAR_MP);
    frames(PLAYING, 1);
    CHECK(bar_mp == 11);
}

int main(void) {
    load_tables();
    registers();
    per_kill_amounts();
    a_kill_gives_mp_at_once();
    the_cap();
    no_refill_over_time();
    nothing_holds_it();
    the_bar_waits_for_room();
    a_guru_fills_hp_and_mp();
    save_states();
    off_and_isolated_do_nothing();
    a_kill_without_the_site();
    printf("mana_test: %u checks passed\n", checks);
    return 0;
}
