/* Unit tests for Guru travel (fax_travel.c), without the emulator or a ROM.
 * The module runs against a model of the machine: CPU RAM, the stock bytes of
 * the Guru scripts it relies on, unused ROM ($FF) and the engine's read
 * overrides. A small interpreter plays the interaction-script actions the
 * Gurus use, calling the module before each one as IScripts_InvokeNextAction
 * does, and records what the Guru says. */
#include "fax_travel.c"
#include <stdarg.h>

static unsigned checks;
#define CHECK(test) do{++checks;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(1);}}while(0)

/* ---- the machine ---- */
static uint8_t ram[0x800];
static uint8_t rom[16 * 16384];                  /* stock image: $FF but the Guru script bytes below */
static uint8_t over[16 * 16384];
static bool overridden[16 * 16384];
static CycModRegs cpu = { .s = 0xFD };
static int commits, commit_temple = -1;
static const char *hooks_on[8];
static unsigned hooks_on_n;

static unsigned bank_at(uint16_t addr) { return addr >= 0xB400 && addr < 0xC000 ? 13 : 12; }   /* the slot, else scripts */
uint8_t cyc_mod_peek(uint16_t addr) {
    if (addr < 0x2000) return ram[addr & 0x7FF];
    if (addr < 0x8000 || addr >= 0xC000) return 0;
    uint32_t i = prg(bank_at(addr), addr);
    return overridden[i] ? over[i] : rom[i];
}
bool cyc_mod_poke(uint16_t addr, uint8_t value) { if (addr >= 0x2000) return false; ram[addr & 0x7FF] = value; return true; }
bool cyc_mod_set_prg_read_override(uint32_t offset, uint8_t compare, uint8_t value) {
    if (offset >= sizeof rom || rom[offset] != compare) return false;
    over[offset] = value; overridden[offset] = value != compare;
    return true;
}
void cyc_mod_regs(CycModRegs *out) { *out = cpu; }
void cyc_mod_set_regs(const CycModRegs *in) { cpu = *in; }
bool cyc_mod_call_commit(uint16_t routine, CycModRegs *regs) {
    (void)regs; CHECK(routine == 0xDD61); ++commits; commit_temple = ram[TEMPLE_SPAWN_POINT]; return true;
}
bool cyc_host_saves_enabled(void) { return false; }
int nes_mod_register_function_entry_plugin(const char *id, uint16_t addr, NESModFunctionEntryCallback cb) { (void)id; (void)addr; (void)cb; return 1; }
int nes_mod_set_function_hook_enabled(const char *id, int on) {
    if (on && hooks_on_n < 8) hooks_on[hooks_on_n++] = id;
    if (!on) hooks_on_n = 0;
    return 1;
}
int nes_mod_register_reset_callback(NESModActivationCallback cb) { (void)cb; return 1; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) { (void)id; (void)cb; return 1; }
int nes_mod_register_savestate_hook(const char *id, NESModSavestateGet get, NESModSavestateSet set) { (void)id; (void)get; (void)set; return 1; }
int nes_mod_register_savestate_validator(const char *id, NESModSavestateValidate v) { (void)id; (void)v; return 1; }
void nes_mod_set_local_only(const char *name, int required) { (void)name; (void)required; }

static void put(uint16_t addr, const uint8_t *bytes, size_t n) { memcpy(rom + prg(12, addr), bytes, n); }
static void stock(void) {
    memset(rom, 0xFF, sizeof rom); memset(overridden, 0, sizeof overridden); memset(ram, 0, sizeof ram);
    static const uint8_t endings[] = { 0x03, 0x22, 0x14, 0x00, 0x01, 0x23, 0x00 };   /* $A5FC: mantra; new title */
    static const uint8_t eolis[] = { 0x03, 0x2D, 0x00 };                               /* $A0C6 */
    put(0xA5FC, endings, sizeof endings);
    put(0xA0C6, eolis, sizeof eolis);
    deactivate(); activate(); fax_travel_step();
    CHECK(served);
}

/* ---- the interpreter ---- */
static char said[16][64];
static unsigned said_n;
static void say(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vsnprintf(said[said_n++], sizeof said[0], fmt, ap); va_end(ap);
}
static void slot_text(char *out) {
    for (unsigned i = 0; i < SLOT_LEN; ++i) {
        uint8_t c = cyc_mod_peek((uint16_t)(SLOT + i));
        if (c == 0xFF) { *out = 0; return; }
        *out++ = c == 0xFD ? ' ' : c == 0xFE ? '/' : (char)c;
    }
    *out = 0;
}
static void message(uint8_t id) {
    char text[64];
    if (id == SLOT_ID) { slot_text(text); say("%s", text); } else say("msg %02X", id);
}
/* Run a Guru's script from `at`. Each question takes the next answer, 'A' or 'B'. */
static void run(uint16_t at, uint8_t furthest, const char *answers) {
    said_n = 0;
    ram[TEMPLE_SPAWN_POINT] = furthest;
    ram[ISCRIPT_ADDR] = (uint8_t)at; ram[ISCRIPT_ADDR + 1] = (uint8_t)(at >> 8); ram[ISCRIPT_OFFSET] = 0;
    for (int steps = 0; steps < 64; ++steps) {
        next_action_hook(0x826E);
        uint16_t p = (uint16_t)((ram[ISCRIPT_ADDR] | ram[ISCRIPT_ADDR + 1] << 8) + ram[ISCRIPT_OFFSET]);
        uint8_t op = cyc_mod_peek(p), arg = cyc_mod_peek((uint16_t)(p + 1));
        uint16_t jump = (uint16_t)(cyc_mod_peek((uint16_t)(p + 2)) | cyc_mod_peek((uint16_t)(p + 3)) << 8);
        unsigned len = 1;
        switch (op) {
        case 0x00: say("end"); return;
        case 0x01: case 0x03: message(arg); len = 2; break;
        case 0x06: if (arg > ram[TEMPLE_SPAWN_POINT]) ram[TEMPLE_SPAWN_POINT] = arg; len = 2; break;
        case 0x14: say("mantra"); break;
        case 0x16:
            message(arg);
            CHECK(*answers);
            if (*answers++ == 'A') {
                ram[ISCRIPT_ADDR] = (uint8_t)jump; ram[ISCRIPT_ADDR + 1] = (uint8_t)(jump >> 8); ram[ISCRIPT_OFFSET] = 0;
                continue;
            }
            len = 4; break;
        case 0x17: {
            uint16_t to = (uint16_t)(arg | cyc_mod_peek((uint16_t)(p + 2)) << 8);
            ram[ISCRIPT_ADDR] = (uint8_t)to; ram[ISCRIPT_ADDR + 1] = (uint8_t)(to >> 8); ram[ISCRIPT_OFFSET] = 0;
            continue; }
        default: fprintf(stderr, "unmodelled action %02X at $%04X\n", op, p); exit(1);
        }
        ram[ISCRIPT_OFFSET] = (uint8_t)(ram[ISCRIPT_OFFSET] + len);
    }
    CHECK(!"the script did not end");
}
/* A temple Guru as in the stock scripts: SET_SPAWN_POINT here, then the
 * mantra (JUMP ISCRIPT_COMMON_GURU_PASSWORD) or a new title (..._NEW_RANK). */
static void guru(uint8_t here, uint8_t furthest, bool new_title, const char *answers) {
    const uint8_t script[] = { 0x06, here, 0x17, new_title ? 0x00 : 0xFC, new_title ? 0xA6 : 0xA5 };
    put(0xA000, script, sizeof script);
    run(0xA000, furthest, answers);
}
static void expect(const char *const *lines) {
    unsigned n = 0;
    for (; lines[n]; ++n) {
        if (n >= said_n || strcmp(said[n], lines[n])) {
            fprintf(stderr, "line %u: said \"%s\", expected \"%s\"\n", n, n < said_n ? said[n] : "", lines[n]);
            for (unsigned i = 0; i < said_n; ++i) fprintf(stderr, "  %s\n", said[i]);
            exit(1);
        }
        ++checks;
    }
    CHECK(n == said_n);
}
#define EXPECT(...) do { static const char *const lines_[] = { __VA_ARGS__, NULL }; expect(lines_); } while (0)
#define INTRO "All Gurus/meditate as one."
#define OFFER_LINE "Shall I send/your spirit to/another Guru?"

static void lines_fit(void) {
    uint8_t out[SLOT_LEN];
    for (int t = 0; t < TEXT_TO + TEMPLES; ++t) CHECK(line_for(t, out));
    CHECK(encode("sixteen chars ok", out) && out[16] == 0xFF);
    CHECK(!encode("seventeen chars!!", out));
    CHECK(!encode("1\n2\n3\n4\n5", out));
    CHECK(encode("a b\nc", out) && out[1] == 0xFD && out[3] == 0xFE && out[5] == 0xFF);
    CHECK(!encode("tab\there", out));
}

static void what_is_offered(void) {
    CHECK(offered(5, 5, 0x2B) == 0x0B);             /* Eolis, Apolune, Mascon from Conflate */
    CHECK(offered(0, 0, 0x01) == 0x00);             /* a new game: nowhere to go */
    CHECK(offered(6, 5, 0x3F) == 0x3F);             /* a first visit to Daybreak offers all before it */
    CHECK(offered(1, 5, 0xFF) == 0x3D);             /* never past the furthest temple */
    CHECK(offered(0, 9, 0x00) == 0x00);             /* a garbage spawn point counts as Eolis */
}

static void conversations(void) {
    stock(); visited = 0x2B;                         /* Eolis, Apolune, Mascon, Conflate */
    guru(5, 5, false, "ABBA");
    EXPECT("msg 22", "mantra", INTRO, OFFER_LINE, "To the Guru of/Eolis?", "To the Guru of/Apolune?",
           "To the Guru of/Mascon?", "Close your eyes/and clear your/mind.", "end");
    CHECK(dest == 3 && ram[TEMPLE_SPAWN_POINT] == 5);

    stock(); visited = 0x2B;
    guru(5, 5, false, "B");
    EXPECT("msg 22", "mantra", INTRO, OFFER_LINE, "end");
    CHECK(dest == -1);

    stock(); visited = 0x2B;
    guru(5, 5, false, "ABBB");
    EXPECT("msg 22", "mantra", INTRO, OFFER_LINE, "To the Guru of/Eolis?", "To the Guru of/Apolune?",
           "To the Guru of/Mascon?", "Then go in/peace.", "end");
    CHECK(dest == -1);

    /* The first visit to Daybreak, which earns a title: offered everything
     * before it, and remembered. */
    stock(); visited = 0x3F;
    guru(6, 5, true, "AA");
    EXPECT("msg 23", INTRO, OFFER_LINE, "To the Guru of/Eolis?", "Close your eyes/and clear your/mind.", "end");
    CHECK(dest == 0 && visited == 0x7F && ram[TEMPLE_SPAWN_POINT] == 6);

    /* Eolis's Guru sets no spawn point; with no other Guru met, he only talks. */
    stock(); visited = 0x01;
    run(0xA0C6, 0, "");
    EXPECT("msg 2D", "end");
    stock(); visited = 0x03;
    run(0xA0C6, 1, "AA");
    EXPECT("msg 2D", INTRO, OFFER_LINE, "To the Guru of/Apolune?", "Close your eyes/and clear your/mind.", "end");
    CHECK(dest == 1);

    /* Any other script passes through untouched. */
    stock(); visited = 0xFF;
    static const uint8_t npc[] = { 0x03, 0x40, 0x00 };
    put(0xA100, npc, sizeof npc);
    run(0xA100, 7, "");
    EXPECT("msg 40", "end");
}

static void journey_path(void) {
    stock(); dest = 3; ram[TEMPLE_SPAWN_POINT] = 5;
    ram[CURRENT_ROM_BANK] = 12;
    CHECK(!main_loop_hook(0xE016) && !journey);      /* not back in the main loop's bank yet */
    ram[CURRENT_ROM_BANK] = 14;
    CHECK(main_loop_hook(0xE016) && journey && cpu.s == 0xF9);
    /* RTS from the hook site, then from each routine in turn. */
    CHECK((ram[0x1FA] | ram[0x1FB] << 8) == SCREEN_FADE_TO_BLACK - 1);
    CHECK((ram[0x1FC] | ram[0x1FD] << 8) == GAME_INIT_STATE_SPAWN - 1);
    CHECK((ram[0x1FE] | ram[0x1FF] << 8) == PLAYER_SPAWN_IN_TEMPLE - 1);
    CHECK(!main_loop_hook(0xE016));                  /* once */
    CHECK(spawn_hook(0xDD61) == 1 && commits == 1);
    CHECK(commit_temple == 3 && ram[TEMPLE_SPAWN_POINT] == 5 && dest == -1 && !journey);
    CHECK(spawn_hook(0xDD61) == 0 && commits == 1);  /* a death's respawn is left alone */
}

static void resume(void) {
    /* A state saved while asking about Mascon restores the line, although
     * read overrides are not machine state. */
    stock();
    talk = (Talk){ ASKING, 0x0B, 3 }; CHECK(serve(TEXT_TO + 3));
    uint8_t buf[8];
    int n = state_get(buf, sizeof buf);
    CHECK(n == 6 && state_validate(buf, n));
    serve(TEXT_FAREWELL); talk = (Talk){ 0 };
    CHECK(state_set(buf, n) && talk.phase == ASKING && talk.asking == 3 && text == TEXT_TO + 3);
    char line[64]; slot_text(line);
    CHECK(!strcmp(line, "To the Guru of/Mascon?"));
    buf[2] = 9;
    CHECK(!state_validate(buf, n) && !state_set(buf, n));
    CHECK(state_set(buf, 0) && talk.phase == IDLE && dest == -1);
}

static void refuses_other_layouts(void) {
    stock();
    deactivate();                                    /* back to the stock image */
    rom[prg(12, ASK)] = 0x00;                        /* someone else's bytes where our steps go */
    activate(); fax_travel_step();
    CHECK(failed && !served && hooks_on_n == 0);
    for (unsigned i = 0; i < 7; ++i) CHECK(!overridden[prg(12, (uint16_t)(SAY + i))]);
}

int main(void) {
    lines_fit();
    what_is_offered();
    conversations();
    journey_path();
    resume();
    refuses_other_layouts();
    printf("travel_test: %u checks passed\n", checks);
    return 0;
}
