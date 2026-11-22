/* Unit tests for MP refill (fax_mana.c), without the emulator or a ROM. The
 * module runs against a model of the machine: CPU RAM, the registers and the
 * stack as each of the game's waits leaves them when a frame ends (the JSRs
 * push their real return addresses), Player_ReduceMP's spend, and
 * Player_AddMP as the committed call runs it: the cap, Player_SetMP's two
 * PPU buffer commands, and PPUBuffer_WaitForCapacity, which spins for ever
 * (the call fails, nothing committed) when the buffer has no room. So the
 * tests check what the hero's MP and its bar do, frame by frame. The
 * Magician's lesson (fax_lesson.c) is one flag here, whether he has taught
 * the hero, and whether the Wing Boots or the Hour Glass take MP
 * (fax_recharge.c) another. */
#include "fax_mana.c"

static unsigned checks;
#define CHECK(test) do{++checks;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(1);}}while(0)

/* ---- the machine ---- */
static uint8_t ram[0x800];
static CycModRegs cpu;
static bool isolated, refuse;               /* refuse: the next committed call fails */
static unsigned calls, drawn;               /* committed calls run; MP bars queued */
static int bar_mp = -1;                     /* the MP the bar was last queued with */

#define MP          ram[0x39A]
#define BANK        ram[0x100]
#define PAUSED      ram[0x120]
#define DEAD        ram[0x438]
#define READ        ram[0x1F]               /* PPUBuffer_ReadOffset */
#define WRITE       ram[0x20]               /* PPUBuffer_WriteOffset */

uint8_t cyc_mod_peek(uint16_t addr) { return addr < 0x2000 ? ram[addr & 0x7FF] : 0; }
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
static bool flying;                          /* the Wing Boots fly (or the Hour Glass freezes), on MP */
bool fax_recharge_draining(void) { return flying; }

/* PPUBuffer_HasCapacity: ReadOffset - WriteOffset is 0 (empty) or at least $24. */
static bool has_capacity(uint8_t write) { uint8_t room = (uint8_t)(READ - write); return room == 0 || room >= 0x24; }
/* Player_AddMP ($C0D9), A the MP to add: CLC, ADC Player_MP, STA, CMP #$50,
 * capped at $50, then Player_SetMP draws the bar: the tiles (3 + width + 1
 * bytes) and the partial tile's pattern (3 + 16), each queued after
 * PPUBuffer_WaitForCapacity. Run in the committed call: all or nothing. */
bool cyc_mod_call_commit(uint16_t routine, CycModRegs *regs) {
    CHECK(routine == 0xC0D9);
    CHECK(regs->s == cpu.s);                 /* JSRs from where the CPU is */
    ++calls;
    if (refuse) { refuse = false; return false; }
    uint8_t w = WRITE;
    if (!has_capacity(w)) return false;      /* spins until its cycle budget runs out */
    w = (uint8_t)(w + 3 + ram[0x4EE] + 1);
    if (!has_capacity(w)) return false;
    w = (uint8_t)(w + 3 + 16);
    unsigned mp = MP + regs->a;
    MP = (uint8_t)(mp > 80 ? 80 : mp);
    WRITE = w;
    bar_mp = MP;
    ++drawn;
    regs->a = 0x10; regs->x = w; regs->y = 0x08; regs->p = 0x26;   /* what the routine leaves */
    cyc_mod_set_regs(regs);                  /* the engine: they become the CPU's */
    return true;
}

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

/* ---- where a frame ends ---- */

/* The stack as JSRs left it: S, and the return address the innermost pushed. */
static void at(uint8_t s, uint16_t pushed, uint16_t pc) {
    cpu.s = s;
    ram[0x100 | (uint8_t)(s + 1)] = (uint8_t)pushed;
    ram[0x100 | (uint8_t)(s + 2)] = (uint8_t)(pushed >> 8);
    cpu.pc = pc;
}
typedef enum {
    PLAYING,       /* Game_MainLoop ($DB45: TXS with $FF) in WaitForNextFrame from $DB4D */
    PAUSE,         /* GameLoop_CheckPauseGame's loop: JSR WaitForNextFrame at $E036, under the JSR at $DB74 */
    TEXT,          /* a script's text box, waiting on its frames in bank 12 */
    LAG,           /* a pass still in Sprites_UpdateAll (JSR at $DB65) as the frame ends */
    TOP_LEVEL,     /* back from WaitForNextFrame at the loop's top, before its next JSR: S $FF */
    ENDING,        /* EndGame_MainLoop's JSR WaitForNextFrame at $DBF6 */
    SCROLL,        /* Game_MainLoop's scroll loop: JSR WaitForNextFrame at $DBC4 */
    OTHER_BANK,    /* the stack as if playing, with another bank than 14 mapped */
} Where;
static void frame_ends(Where where) {
    BANK = 14;
    switch (where) {
    case PLAYING:   at(0xFD, 0xDB4F, 0xCA29); break;
    case PAUSE:     ram[0x1FE] = 0x76; ram[0x1FF] = 0xDB; at(0xFB, 0xE038, 0xCA29); break;
    case TEXT:      BANK = 12; at(0xF7, 0x8A12, 0xCA29); break;
    case LAG:       at(0xFD, 0xDB67, 0x8123); break;
    case TOP_LEVEL: at(0xFD, 0xDB4F, 0xDB50); cpu.s = 0xFF; break;
    case ENDING:    at(0xFD, 0xDBF8, 0xCA29); break;
    case SCROLL:    at(0xFD, 0xDBC6, 0xCA29); break;
    case OTHER_BANK: BANK = 12; at(0xFD, 0xDB4F, 0xCA29); break;
    }
    fax_mana_frame_end();
}
/* Player_ReduceMP, in a pass of the main loop: the spell's cost, if MP covers it. */
static void cast(uint8_t cost) { if (MP >= cost) MP = (uint8_t)(MP - cost); }
/* The NMI that starts each frame sends the PPU buffer. */
static void nmi(void) { READ = WRITE; }
static void frames(Where where, unsigned n) { for (unsigned i = 0; i < n; ++i) { nmi(); frame_ends(where); } }

static void fresh_as(uint8_t mp, bool learned) {
    reset_cb();
    plugin_cb();
    memset(ram, 0, sizeof ram);
    ram[0x4EE] = 10;                         /* UI_MPAndHPBarWidth, as UI_DrawHUDSprites sets it */
    MP = mp;
    calls = drawn = 0;
    bar_mp = -1;
    isolated = refuse = flying = false;
    taught = learned;
    cpu = (CycModRegs){ .a = 0x00, .x = 0x22, .y = 0x33, .s = 0xFD, .p = 0x02 };
    frames(PLAYING, 1);                      /* the MP it starts from */
}
static void fresh(uint8_t mp) { fresh_as(mp, false); }

/* The frame (counted from the spend's) MP comes back on, playing throughout. */
static unsigned frames_until_more(Where where) {
    uint8_t mp = MP;
    for (unsigned n = 1; n < 2000; ++n) {
        nmi();
        frame_ends(where);
        if (MP != mp) return n;
    }
    return 0;
}

/* ---- the tests ---- */

static void registers(void) {
    CHECK(reset_cb == deactivate && plugin_cb == activate && !strcmp(plugin_id, "faxanadu.mana-refill"));
    CHECK(save_get == state_get && save_set == state_set && save_validate == state_validate);
}

static void full_mp_stays_put(void) {
    fresh(80);
    frames(PLAYING, 1000);
    CHECK(MP == 80 && calls == 0);
}

static void the_wait_then_one_mp_a_step(void) {
    fresh(80);
    frames(PLAYING, 50);
    nmi(); cast(16); frame_ends(PLAYING);    /* Tilte, in this frame */
    CHECK(MP == 64 && r.left == WAIT && spends == 1);
    CHECK(frames_until_more(PLAYING) == WAIT);
    CHECK(MP == 65 && bar_mp == 65 && drawn == 1);
    for (int k = 66; k <= 80; ++k) {
        CHECK(frames_until_more(PLAYING) == STEP);
        CHECK(MP == k && bar_mp == k);
    }
    CHECK(drawn == 16 && refills == 16);
    frames(PLAYING, 500);                    /* full: no more calls */
    CHECK(MP == 80 && calls == 16);
}

static void empty_to_full(void) {
    fresh(0);                                /* a new game: Game_Start leaves MP at 0 */
    unsigned n = 1;                          /* fresh() played the first frame */
    while (MP < 80 && n < 10000) { nmi(); frame_ends(PLAYING); ++n; }
    CHECK(MP == 80 && n == WAIT + 79 * STEP);   /* 2,610 frames: the wait, then 40 s */
}

/* Taught by the Magician: 120 frames, then 1 MP every 15. */
static void the_lesson_halves_both(void) {
    fresh_as(80, true);
    frames(PLAYING, 50);
    nmi(); cast(16); frame_ends(PLAYING);
    CHECK(MP == 64 && r.left == TAUGHT_WAIT);
    CHECK(frames_until_more(PLAYING) == TAUGHT_WAIT && MP == 65);
    for (int k = 66; k <= 80; ++k) CHECK(frames_until_more(PLAYING) == TAUGHT_STEP && MP == k && bar_mp == k);
    fresh_as(0, true);
    unsigned n = 1;
    while (MP < 80 && n < 10000) { nmi(); frame_ends(PLAYING); ++n; }
    CHECK(MP == 80 && n == TAUGHT_WAIT + 79 * TAUGHT_STEP);   /* 1,305 frames: 21.75 s */
    CHECK(TAUGHT_WAIT * 2 == WAIT && TAUGHT_STEP * 2 == STEP);
}

/* Learned while a wait runs (in his conversation, which no frame counts):
 * what is left of it is cut to the taught wait, and a wait already shorter
 * runs on; the next MP is a taught step after. */
static void learned_during_the_wait(void) {
    fresh(60);
    nmi(); cast(2); frame_ends(PLAYING);
    frames(PLAYING, 20);
    CHECK(r.left == WAIT - 20);
    frames(TEXT, 300);                       /* his offer, the gold counted out, his line */
    taught = true;
    nmi(); frame_ends(PLAYING);
    CHECK(r.left == TAUGHT_WAIT - 1);
    CHECK(frames_until_more(PLAYING) == TAUGHT_WAIT - 1 && MP == 59);
    CHECK(frames_until_more(PLAYING) == TAUGHT_STEP && MP == 60);
    fresh(60);
    nmi(); cast(2); frame_ends(PLAYING);
    frames(PLAYING, 200);                    /* 40 left, under the taught wait */
    taught = true;
    CHECK(frames_until_more(PLAYING) == WAIT - 200 && MP == 59);
    CHECK(frames_until_more(PLAYING) == TAUGHT_STEP);
    /* A save state taken untaught, with its whole wait to run, loaded taught. */
    uint8_t rec[RECORD] = { RECORD_VERSION, 1, 50, WAIT };
    CHECK(save_set(rec, RECORD) == 1);
    MP = 50;
    CHECK(frames_until_more(PLAYING) == TAUGHT_WAIT);
}

static void the_cpu_gets_its_registers_back(void) {
    fresh(60);
    cpu.a = 0x00; cpu.x = 0x44; cpu.y = 0x55; cpu.p = 0x02;   /* WaitForNextFrame: LDA latch gave 0, Z set */
    r.left = 1;
    nmi(); frame_ends(PLAYING);
    CHECK(MP == 61 && calls == 1);
    CHECK(cpu.a == 0x00 && cpu.x == 0x44 && cpu.y == 0x55 && cpu.p == 0x02 && cpu.s == 0xFD);
}

static void a_spend_starts_the_wait_again(void) {
    fresh(60);
    frames(PLAYING, 100);
    CHECK(r.left == WAIT - 101);
    nmi(); cast(2); frame_ends(PLAYING);     /* Deluge during the wait */
    CHECK(MP == 58 && r.left == WAIT);
    CHECK(frames_until_more(PLAYING) == WAIT && MP == 59);
    CHECK(frames_until_more(PLAYING) == STEP && MP == 60);
    frames(PLAYING, 10);
    nmi(); cast(6); frame_ends(PLAYING);     /* Fire while MP climbs */
    CHECK(MP == 54 && r.left == WAIT);
    CHECK(frames_until_more(PLAYING) == WAIT && MP == 55);
    /* MP rising by other means (a meat shop's IScriptAction_AddMP, the
     * Elixir's fill) is no spend: the steps go on. */
    frames(PLAYING, 10);
    MP = 70;
    CHECK(frames_until_more(PLAYING) == STEP - 10 && MP == 71);
}

/* While the Wing Boots fly (or the Hour Glass freezes) no MP comes back,
 * though they take one only every 24 to 64 frames, and the lesson's step is
 * 15: each frame of the run starts the wait again, so it runs from the end,
 * whole. */
static void nothing_back_while_flying(void) {
    static const bool learned[] = { false, true };
    for (unsigned i = 0; i < 2; ++i) {
        fresh_as(50, learned[i]);
        unsigned wait = learned[i] ? TAUGHT_WAIT : WAIT;
        frames(PLAYING, wait - 10);          /* the wait nearly over: a flight begins */
        flying = true;
        for (unsigned n = 0; n < 1000; ++n) {
            nmi();
            if (n % 32 == 31) MP = (uint8_t)(MP - 1);   /* the flight's drain, a Novice's */
            frame_ends(PLAYING);
            CHECK(r.left == wait && calls == 0);
        }
        CHECK(MP == 50 - 1000 / 32);
        flying = false;                      /* landed */
        CHECK(frames_until_more(PLAYING) == wait);
        CHECK(frames_until_more(PLAYING) == (learned[i] ? TAUGHT_STEP : STEP));
    }
    /* Flying on a full bar's first frames: nothing to refill anyway, and the
     * MP the flight leaves waits the whole wait from the landing. */
    fresh(80);
    flying = true;
    frames(PLAYING, 31);
    nmi(); MP = 79; frame_ends(PLAYING);
    frames(PLAYING, 500);
    CHECK(MP == 79 && calls == 0);
    flying = false;
    CHECK(frames_until_more(PLAYING) == WAIT && MP == 80);
}

static void only_played_frames_count(void) {
    static const Where held[] = { PAUSE, TEXT, LAG, TOP_LEVEL, ENDING, SCROLL, OTHER_BANK };
    for (unsigned i = 0; i < sizeof held / sizeof held[0]; ++i) {
        fresh(60);
        frames(PLAYING, 39);
        uint8_t left = r.left;
        frames(held[i], 1000);
        CHECK(MP == 60 && r.left == left && calls == 0 && !idle_now);
        frames(PLAYING, left - 1);
        CHECK(MP == 60);
        frames(PLAYING, 1);
        CHECK(MP == 61);
    }
    /* Paused or dead, with the stack as if playing: no. */
    fresh(60);
    uint8_t left = r.left;
    PAUSED = 1; frames(PLAYING, 300); PAUSED = 0;
    DEAD = 1; frames(PLAYING, 300); DEAD = 0;
    CHECK(MP == 60 && r.left == left && calls == 0);
    /* A spend counts whenever it happens: in a pass that lagged, too. */
    fresh(60);
    frames(PLAYING, 100);
    nmi(); cast(4); frame_ends(LAG);
    CHECK(MP == 56 && r.left == WAIT && spends == 1);
}

static void a_full_ppu_buffer_waits(void) {
    fresh(60);
    r.left = 1;
    READ = 0x10; WRITE = (uint8_t)(READ - 49);   /* room for the tiles, not then for the pattern */
    frame_ends(PLAYING);
    CHECK(MP == 60 && calls == 0 && deferred == 1 && r.left == 1);
    frame_ends(PLAYING);
    CHECK(MP == 60 && calls == 0 && deferred == 2);
    WRITE = (uint8_t)(READ - 50);            /* just enough */
    frame_ends(PLAYING);
    CHECK(MP == 61 && drawn == 1 && r.left == STEP);
    CHECK(WRITE == (uint8_t)(READ - 50 + 33));
    /* Empty (ReadOffset = WriteOffset) is room. */
    r.left = 1;
    READ = WRITE = 0x80;
    frame_ends(PLAYING);
    CHECK(MP == 62 && drawn == 2);
    /* A call that fails anyway gives nothing, and it tries again. */
    nmi();
    r.left = 1;
    refuse = true;
    frame_ends(PLAYING);
    CHECK(MP == 62 && calls == 3 && deferred == 3 && r.left == 1);
    nmi(); frame_ends(PLAYING);
    CHECK(MP == 63 && drawn == 3);
}

static void save_states(void) {
    fresh(60);
    frames(PLAYING, 77);
    uint8_t buf[16];
    CHECK(save_get(buf, 3) == -1);
    CHECK(save_get(buf, sizeof buf) == RECORD);
    CHECK(buf[0] == RECORD_VERSION && buf[1] == 1 && buf[2] == 60 && buf[3] == WAIT - 78);
    /* Loaded: the same wait, and the MP it saw, so the load is no spend. */
    frames(PLAYING, 50);
    MP = 60;
    CHECK(save_set(buf, RECORD) == 1);
    nmi(); frame_ends(PLAYING);
    CHECK(r.left == WAIT - 79 && spends == 0);
    /* Bad records are refused. */
    uint8_t bad[RECORD];
    memcpy(bad, buf, RECORD); bad[0] = 2;           CHECK(!save_validate(bad, RECORD) && save_set(bad, RECORD) == 0);
    memcpy(bad, buf, RECORD); bad[1] = 2;           CHECK(!save_validate(bad, RECORD));
    memcpy(bad, buf, RECORD); bad[3] = 0;           CHECK(!save_validate(bad, RECORD));
    memcpy(bad, buf, RECORD); bad[3] = WAIT + 1;    CHECK(!save_validate(bad, RECORD));
    CHECK(!save_validate(buf, RECORD - 1) && save_validate(buf, RECORD));
    /* A state from before the feature: the MP is taken as it is (no spend,
     * though lower than the last seen), and the whole wait runs. */
    MP = 30;
    CHECK(save_validate(buf, 0) && save_set(buf, 0) == 1);
    CHECK(frames_until_more(PLAYING) == WAIT && spends == 0);
}

static void off_and_isolated_do_nothing(void) {
    fresh(60);
    reset_cb();                              /* the remaster off: no activation follows */
    r.left = 1;
    frames(PLAYING, 1000);
    CHECK(MP == 60 && calls == 0);
    plugin_cb();
    isolated = true;                         /* Seamless's simulation of another room */
    r.left = 1;
    frames(PLAYING, 1000);
    CHECK(MP == 60 && calls == 0);
    isolated = false;
    frames(PLAYING, 1);
    CHECK(MP == 61);
}

int main(void) {
    registers();
    full_mp_stays_put();
    the_wait_then_one_mp_a_step();
    empty_to_full();
    the_lesson_halves_both();
    learned_during_the_wait();
    the_cpu_gets_its_registers_back();
    a_spend_starts_the_wait_again();
    nothing_back_while_flying();
    only_played_frames_count();
    a_full_ppu_buffer_waits();
    save_states();
    off_and_isolated_do_nothing();
    printf("mana_test: %u checks passed\n", checks);
    return 0;
}
