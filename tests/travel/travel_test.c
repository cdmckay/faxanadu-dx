/* Unit tests for Guru travel (fax_travel.c), without the emulator or a ROM.
 * The module runs against a model of the machine: CPU RAM, the stock bytes of
 * the Guru scripts it relies on, unused ROM ($FF) and the engine's read
 * overrides. A small interpreter plays the interaction-script actions the
 * Gurus use, calling the module before each one as IScripts_InvokeNextAction
 * does, and records what the Guru says. When the module handles the call, it
 * has pushed a chain of the game's routines to return through: the model
 * plays those too (the menu's box, its rows through UI_DrawString and the
 * module's two hooks there, its wait, the dialogue box again) until the chain
 * comes back to IScripts_InvokeNextAction. */
#include "fax_travel.c"
#include <stdarg.h>

static unsigned checks;
#define CHECK(test)                                                                                                    \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (!(test)) {                                                                                                 \
            fprintf(stderr, "FAIL line %u: %s\n", __LINE__, #test);                                                    \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)

/* ---- the machine ---- */
static uint8_t ram[0x800];
static uint8_t rom[16 * 16384]; /* stock image: $FF but the Guru script bytes below */
static uint8_t over[16 * 16384];
static bool overridden[16 * 16384];
static CycModRegs cpu = { .s = 0xFD };
static int commits, commit_temple = -1;
static const char *hooks_on[8];
static unsigned hooks_on_n;

static unsigned bank_at(uint16_t addr) {
    return addr >= 0xB400 && addr < 0xC000 ? 13 : 12;
} /* the slot, else scripts */
uint8_t cyc_mod_peek(uint16_t addr) {
    if (addr < 0x2000) return ram[addr & 0x7FF];
    if (addr < 0x8000 || addr >= 0xC000) return 0;
    uint32_t i = prg(bank_at(addr), addr);
    return overridden[i] ? over[i] : rom[i];
}
bool cyc_mod_poke(uint16_t addr, uint8_t value) {
    if (addr >= 0x2000) return false;
    ram[addr & 0x7FF] = value;
    return true;
}
bool cyc_mod_set_prg_read_override(uint32_t offset, uint8_t compare, uint8_t value) {
    if (offset >= sizeof rom || rom[offset] != compare) return false;
    over[offset] = value;
    overridden[offset] = value != compare;
    return true;
}
void cyc_mod_regs(CycModRegs *out) { *out = cpu; }
void cyc_mod_set_regs(const CycModRegs *in) { cpu = *in; }
bool cyc_mod_call_commit(uint16_t routine, CycModRegs *regs) {
    (void)regs;
    CHECK(routine == 0xDD61);
    ++commits;
    commit_temple = ram[TEMPLE_SPAWN_POINT];
    return true;
}
bool cyc_host_saves_enabled(void) { return false; }
/* Save files, as fax_travel.c sees it: whether it runs the Guru's choice, and
 * the file in play's `gurus` field. */
static bool guru_choice, save_files;
static char gurus_field[FAX_SAVE_VALUE];
static FaxSaveListener save_listener;
bool fax_files_guru_choice(void) { return guru_choice; }
/* B in the menu goes back to Save files' choice: `again_at` (0: it cannot). */
static uint16_t again_at;
static unsigned again_calls, reopened;
static unsigned portrait_frames; /* IScripts_UpdatePortraitAnimation's frames the module returned into */
uint16_t fax_files_guru_again(void) {
    ++again_calls;
    return again_at;
}
void fax_files_box_reopened(void) { ++reopened; }
bool cyc_mod_isolated(void) { return false; }
bool fax_save_enabled(void) { return save_files; }
const char *fax_save_get(const char *key) { return !strcmp(key, "gurus") ? gurus_field : NULL; }
bool fax_save_set(const char *key, const char *value) {
    CHECK(!strcmp(key, "gurus"));
    snprintf(gurus_field, sizeof gurus_field, "%s", value);
    return true;
}
uint8_t fax_save_gurus_parse(const char *text) {
    static const char *const names[] = { "Eolis",  "Apolune",  "Forepaw",  "Mascon",
                                         "Victim", "Conflate", "Daybreak", "Dartmoor" };
    uint8_t v = 0;
    for (int t = 0; t < 8; ++t)
        if (text && strstr(text, names[t])) v |= (uint8_t)(1u << t);
    return v;
}
void fax_save_gurus_format(uint8_t visited, char out[FAX_SAVE_VALUE]) {
    static const char *const names[] = { "Eolis",  "Apolune",  "Forepaw",  "Mascon",
                                         "Victim", "Conflate", "Daybreak", "Dartmoor" };
    size_t n = 0;
    out[0] = 0;
    for (int t = 0; t < 8; ++t)
        if (visited >> t & 1) n += (size_t)snprintf(out + n, FAX_SAVE_VALUE - n, "%s%s", n ? " " : "", names[t]);
}
bool fax_save_listen(FaxSaveListener listener) {
    save_listener = listener;
    return true;
}
int nes_mod_register_function_entry_plugin(const char *id, uint16_t addr, NESModFunctionEntryCallback cb) {
    (void)id;
    (void)addr;
    (void)cb;
    return 1;
}
int nes_mod_set_function_hook_enabled(const char *id, int on) {
    if (on && hooks_on_n < 8) hooks_on[hooks_on_n++] = id;
    if (!on) hooks_on_n = 0;
    return 1;
}
int nes_mod_register_reset_callback(NESModActivationCallback cb) {
    (void)cb;
    return 1;
}
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    (void)id;
    (void)cb;
    return 1;
}
int nes_mod_register_savestate_hook(const char *id, NESModSavestateGet get, NESModSavestateSet set) {
    (void)id;
    (void)get;
    (void)set;
    return 1;
}
int nes_mod_register_savestate_validator(const char *id, NESModSavestateValidate v) {
    (void)id;
    (void)v;
    return 1;
}
void nes_mod_set_local_only(const char *name, int required) {
    (void)name;
    (void)required;
}

static void put(uint16_t addr, const uint8_t *bytes, size_t n) { memcpy(rom + prg(12, addr), bytes, n); }
/* Save files' choice, as the model stands it in: a line, then END. */
enum { CHOICE = 0xA200 };
static void stock(void) {
    memset(rom, 0xFF, sizeof rom);
    memset(overridden, 0, sizeof overridden);
    memset(ram, 0, sizeof ram);
    cpu = (CycModRegs){ .s = 0xFD };
    static const uint8_t endings[] = { 0x03, 0x22, 0x14, 0x00, 0x01, 0x23, 0x00 }; /* $A5FC: mantra; new title */
    static const uint8_t eolis[] = { 0x03, 0x2D, 0x00 };                           /* $A0C6 */
    static const uint8_t choice[] = { 0x03, 0x99, 0x00 };
    put(0xA5FC, endings, sizeof endings);
    put(0xA0C6, eolis, sizeof eolis);
    put(CHOICE, choice, sizeof choice);
    guru_choice = false;
    again_at = 0;
    again_calls = reopened = portrait_frames = 0;
    deactivate();
    activate();
    fax_travel_step();
    CHECK(served);
}

/* ---- the interpreter ---- */
static char said[24][64];
static unsigned said_n;
static void say(const char *fmt, ...) {
    CHECK(said_n < sizeof said / sizeof said[0]);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(said[said_n++], sizeof said[0], fmt, ap);
    va_end(ap);
}
static void slot_text(char *out) {
    for (unsigned i = 0; i < SLOT_LEN; ++i) {
        uint8_t c = cyc_mod_peek((uint16_t)(SLOT + i));
        if (c == 0xFF) {
            *out = 0;
            return;
        }
        *out++ = c == 0xFD ? ' ' : c == 0xFE ? '/' : (char)c;
    }
    *out = 0;
}
static void message(uint8_t id) {
    char text[64];
    if (id == SLOT_ID) {
        slot_text(text);
        say("%s", text);
    } else say("msg %02X", id);
}
static void jump(uint16_t to) {
    ram[ISCRIPT_ADDR] = (uint8_t)to;
    ram[ISCRIPT_ADDR + 1] = (uint8_t)(to >> 8);
    ram[ISCRIPT_OFFSET] = 0;
}

/* The model's stack, popped as an RTS pops it. */
static uint16_t pull(void) {
    uint8_t lo = ram[0x100 + (uint8_t)(cpu.s + 1)], hi = ram[0x100 + (uint8_t)(cpu.s + 2)];
    cpu.s = (uint8_t)(cpu.s + 2);
    return (uint16_t)((lo | hi << 8) + 1);
}
/* UI_DrawString for string `number`, as PlayerMenu_DrawStringLines calls it
 * (Y = number * 16): the length, then each letter, loaded from Temp_Int24 + Y,
 * each load seen by the module's hook after it, as the game's sites are. */
static void draw_string(uint8_t number, char *out) {
    uint16_t base = (uint16_t)(ram[TEMP_INT24] | ram[TEMP_INT24 + 1] << 8);
    uint8_t y = (uint8_t)(number * 16);
    cpu.y = y;
    cpu.a = cyc_mod_peek((uint16_t)(base + y));
    length_hook(DRAW_LENGTH);
    unsigned n = cpu.a;
    CHECK(n < 16);
    for (unsigned k = 1; k <= n; ++k) {
        cpu.y = (uint8_t)(y + k);
        cpu.a = cyc_mod_peek((uint16_t)(base + y + k));
        letter_hook(DRAW_LETTER);
        *out++ = (char)cpu.a;
    }
    *out = 0;
}
/* The routines the module's hook returned into, in turn, until the chain is
 * back at IScripts_InvokeNextAction. The menu's wait takes the next answers:
 * 'D' and 'U' move its cursor, wrapping as Menu_UpdateAndDraw does, then 'A'
 * (C clear) or 'B' (C set). */
static void routines(const char **answers) {
    for (;;) {
        uint16_t to = pull();
        switch (to) {
        case PORTRAIT_FRAME: /* a frame: the sprites drawn anew, the last cursor or prompt gone */
            ++portrait_frames;
            break;
        case TEXTBOX_OPEN:
            say("box %u,%u %ux%u", ram[TEXTBOX_X], ram[TEXTBOX_X + 1], ram[TEXTBOX_X + 2], ram[TEXTBOX_X + 3]);
            ram[TEXTBOX_CLOSED] = 0;
            ram[TEMP_INT24] = 0; /* TextBox_FillBackground counts in it */
            break;
        case DRAW_STRING_LINES: {
            char rows[128] = "", row[16];
            for (unsigned i = 0; i < ram[ARG_STRINGS_COUNT]; ++i) {
                draw_string(ram[DATA_ARRAY + i], row);
                strcat(rows, i ? "," : "");
                strcat(rows, row);
            }
            say("menu %s", rows);
            break;
        }
        case WAIT_FOR_MENU_INPUT: {
            unsigned n = ram[ARG_STRINGS_COUNT];
            CHECK(n && ram[MENU_CURSOR_POS] == 0); /* on the first row */
            for (; **answers == 'D' || **answers == 'U'; ++*answers)
                ram[MENU_CURSOR_POS] = (uint8_t)((ram[MENU_CURSOR_POS] + (**answers == 'D' ? 1 : n - 1)) % n);
            CHECK(**answers == 'A' || **answers == 'B');
            cpu.p = (uint8_t)(**answers == 'B' ? cpu.p | P_C : cpu.p & ~P_C);
            ++*answers;
            break;
        }
        case TEXTBOX_OPEN_FOR_NPC:
            say("dialogue box");
            break;
        case INVOKE_NEXT_ACTION:
            CHECK(cpu.s == 0xFD); /* the stack where it was */
            return;
        default:
            fprintf(stderr, "unmodelled routine $%04X\n", to);
            exit(1);
        }
    }
}
/* Play from where the script is. Each question takes the next answer, 'A'
 * or 'B'; the menu takes its moves, then 'A' or 'B'. Every answer is used. */
static void interpret(const char *answers) {
    for (int steps = 0; steps < 64; ++steps) {
        if (next_action_hook(0x826E)) {
            routines(&answers);
            continue;
        }
        uint16_t p = (uint16_t)((ram[ISCRIPT_ADDR] | ram[ISCRIPT_ADDR + 1] << 8) + ram[ISCRIPT_OFFSET]);
        uint8_t op = cyc_mod_peek(p), arg = cyc_mod_peek((uint16_t)(p + 1));
        uint16_t to = (uint16_t)(cyc_mod_peek((uint16_t)(p + 2)) | cyc_mod_peek((uint16_t)(p + 3)) << 8);
        unsigned len = 1;
        switch (op) {
        case 0x00:
            say("end");
            CHECK(!*answers);
            return;
        case 0x01:
        case 0x03:
            message(arg);
            len = 2;
            break;
        case 0x06:
            if (arg > ram[TEMPLE_SPAWN_POINT]) ram[TEMPLE_SPAWN_POINT] = arg;
            len = 2;
            break;
        case 0x14:
            say("mantra");
            break;
        case 0x16:
            message(arg);
            CHECK(*answers);
            if (*answers++ == 'A') {
                jump(to);
                continue;
            }
            len = 4;
            break;
        case 0x17:
            jump((uint16_t)(arg | cyc_mod_peek((uint16_t)(p + 2)) << 8));
            continue;
        default:
            fprintf(stderr, "unmodelled action %02X at $%04X\n", op, p);
            exit(1);
        }
        ram[ISCRIPT_OFFSET] = (uint8_t)(ram[ISCRIPT_OFFSET] + len);
    }
    CHECK(!"the script did not end");
}
/* Run a script from IScriptOrCHRAddr `base` + IScriptOffset `offset` (1 for a
 * script that begins, as IScripts_Begin leaves it after the entity byte). */
static void run_at(uint16_t base, uint8_t offset, uint8_t furthest, const char *answers) {
    said_n = 0;
    ram[TEMPLE_SPAWN_POINT] = furthest;
    jump(base);
    ram[ISCRIPT_OFFSET] = offset;
    interpret(answers);
}
/* Run a Guru's script from `at`. */
static void run(uint16_t at, uint8_t furthest, const char *answers) { run_at(at, 0, furthest, answers); }
/* "Send me to another Guru.": Save files' hook, at its choice's second
 * option ($AE13), opens the menu and handles the call. */
static void run_menu(const char *answers) {
    said_n = 0;
    jump(0xAE13);
    CHECK(fax_travel_menu() == 1);
    routines(&answers);
    interpret(answers);
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
#define EXPECT(...)                                                                                                    \
    do {                                                                                                               \
        static const char *const lines_[] = { __VA_ARGS__, NULL };                                                     \
        expect(lines_);                                                                                                \
    } while (0)
enum { PHASE_INTRO = INTRO }; /* the phase, before INTRO names its line */
#define INTRO         "All Gurus/meditate as one."
#define OFFER_LINE    "Shall I send/you to another/Guru?"
#define DEPART_LINE   "Close your eyes/and clear your/mind."
#define FAREWELL_LINE "Then go in/peace."
#define SMALL_BOX     "box 10,16 20x8" /* up to three rows: the dialogue box's place */
#define FROM_CONFLATE "menu EOLIS,APOLUNE,MASCON"

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
    CHECK(offered(5, 5, 0x2B) == 0x0B); /* Eolis, Apolune, Mascon from Conflate */
    CHECK(offered(0, 0, 0x01) == 0x00); /* a new game: nowhere to go */
    CHECK(offered(6, 5, 0x3F) == 0x3F); /* a first visit to Daybreak offers all before it */
    CHECK(offered(1, 5, 0xFF) == 0x3D); /* never past the furthest temple */
    CHECK(offered(0, 9, 0x00) == 0x00); /* a garbage spawn point counts as Eolis */
}

/* The menu's box: the dialogue box's columns and bottom row, up to three rows
 * in its own place, taller above it after that, inside what
 * IScriptAction_EndScript clears (rows 8 to 27); every name fits a row. */
static void the_box(void) {
    uint8_t rows[TEMPLES], y, h;
    for (unsigned n = 1; n <= 7; ++n) {
        menu_box(n, &y, &h);
        CHECK(y + h == 24 && h >= 2 * n + 2 && y >= 8);
        CHECK(n > 3 ? h == 2 * n + 2 : (y == 16 && h == 8));
    }
    CHECK(menu_rows(0x0B, rows) == 3 && rows[0] == 0 && rows[1] == 1 && rows[2] == 3);
    CHECK(menu_rows(0x7F, rows) == 7 && rows[6] == 6);
    /* A row's text starts 2 tiles in (the frame, the cursor's column) and
     * ends before the right frame. */
    for (int t = 0; t < TEMPLES; ++t) CHECK(strlen(town[t]) <= MENU_W - 3);
}

/* UI_DrawString's loads: the towns, in capitals, from TOWNS only, only while
 * the menu draws. The Select menu's table, and any other, is left alone. */
static void menu_strings(void) {
    stock();
    talk = (Talk){ MENU, 0x0B, 0, 0 };
    ram[TEMP_INT24] = 0x73; /* PLAYER_TOP_MENU_ITEM_STRINGS ($8873), fax_menu.c's */
    ram[TEMP_INT24 + 1] = 0x88;
    cpu.y = 0x60;
    cpu.a = 0x09;
    length_hook(DRAW_LENGTH);
    CHECK(cpu.a == 0x09);
    ram[TEMP_INT24] = (uint8_t)TOWNS;
    ram[TEMP_INT24 + 1] = (uint8_t)(TOWNS >> 8);
    cpu.y = 0x50;
    cpu.a = 0xFF;
    length_hook(DRAW_LENGTH);
    CHECK(cpu.a == 8 && !(cpu.p & P_Z) && !(cpu.p & P_N)); /* CONFLATE */
    cpu.y = 0x51;
    letter_hook(DRAW_LETTER);
    CHECK(cpu.a == 'C');
    cpu.y = 0x58;
    letter_hook(DRAW_LETTER);
    CHECK(cpu.a == 'E');
    cpu.y = 0x59;
    cpu.a = 0xFF;
    letter_hook(DRAW_LETTER); /* past its end */
    CHECK(cpu.a == 0xFF);
    talk.phase = OFFER;
    cpu.y = 0x50;
    length_hook(DRAW_LENGTH); /* not drawing the menu */
    CHECK(cpu.a == 0xFF);
}

static void conversations(void) {
    /* The offer, then the menu of the Gurus visited up to the furthest
     * temple, the cursor on the first: A goes there. */
    stock();
    visited = 0x2B; /* Eolis, Apolune, Mascon, Conflate */
    guru(5, 5, false, "AA");
    EXPECT("msg 22", "mantra", INTRO, OFFER_LINE, SMALL_BOX, FROM_CONFLATE, "dialogue box", DEPART_LINE, "end");
    CHECK(dest == 0 && ram[TEMPLE_SPAWN_POINT] == 5 && menus == 1 && reopened == 1 && letters == 5 + 7 + 6);
    CHECK(talk.phase == IDLE && portrait_frames == 4); /* two frames before each box: the prompt, then the cursor, go */

    /* Down, then A: the second row's Guru. Up and Down wrap. */
    stock();
    visited = 0x2B;
    guru(5, 5, false, "ADA");
    CHECK(dest == 1);
    stock();
    visited = 0x2B;
    guru(5, 5, false, "AUA");
    CHECK(dest == 3);
    stock();
    visited = 0x2B;
    guru(5, 5, false, "ADDDA");
    CHECK(dest == 0);

    /* B on the offer ends it; B in the menu says farewell in the dialogue box,
     * opened again over the list. */
    stock();
    visited = 0x2B;
    guru(5, 5, false, "B");
    EXPECT("msg 22", "mantra", INTRO, OFFER_LINE, "end");
    CHECK(dest == -1 && menus == 0);
    stock();
    visited = 0x2B;
    guru(5, 5, false, "ADB");
    EXPECT("msg 22", "mantra", INTRO, OFFER_LINE, SMALL_BOX, FROM_CONFLATE, "dialogue box", FAREWELL_LINE, "end");
    CHECK(dest == -1 && again_calls == 0 && reopened == 1);

    /* The first visit to Daybreak, which earns a title: offered everything
     * before it, and remembered. Six rows: the box rises above the dialogue
     * box's place. */
    stock();
    visited = 0x3F;
    guru(6, 5, true, "AA");
    EXPECT("msg 23", INTRO, OFFER_LINE, "box 10,10 20x14", "menu EOLIS,APOLUNE,FOREPAW,MASCON,VICTIM,CONFLATE",
           "dialogue box", DEPART_LINE, "end");
    CHECK(dest == 0 && visited == 0x7F && ram[TEMPLE_SPAWN_POINT] == 6);

    /* Dartmoor, every Guru visited: seven rows, from row 8. */
    stock();
    visited = 0xFF;
    guru(7, 7, false, "AUA");
    EXPECT("msg 22", "mantra", INTRO, OFFER_LINE, "box 10,8 20x16",
           "menu EOLIS,APOLUNE,FOREPAW,MASCON,VICTIM,CONFLATE,DAYBREAK", "dialogue box", DEPART_LINE, "end");
    CHECK(dest == 6);

    /* Eolis's Guru sets no spawn point; with no other Guru met, he only talks.
     * With one, the menu has one row, which Up and Down keep. */
    stock();
    visited = 0x01;
    run(0xA0C6, 0, "");
    EXPECT("msg 2D", "end");
    stock();
    visited = 0x03;
    run(0xA0C6, 1, "ADUA");
    EXPECT("msg 2D", INTRO, OFFER_LINE, SMALL_BOX, "menu APOLUNE", "dialogue box", DEPART_LINE, "end");
    CHECK(dest == 1);

    /* Any other script passes through untouched. */
    stock();
    visited = 0xFF;
    static const uint8_t npc[] = { 0x03, 0x40, 0x00 };
    put(0xA100, npc, sizeof npc);
    run(0xA100, 7, "");
    EXPECT("msg 40", "end");
}

/* B on "All Gurus meditate as one." ends the script with the intro under
 * way. The next script to begin is someone else's, which shows a line
 * through the same SAY step (a shop's end with Block's tutorial, Eolis's
 * Guru with the Elixir's line): its END is not taken for the intro's. */
static void b_on_the_intro(void) {
    stock();
    visited = 0x2B;
    talk.phase = PHASE_INTRO;                                             /* the intro was said, then B */
    static const uint8_t script[] = { 0x83, 0x17, SAY & 0xFF, SAY >> 8 }; /* a portrait, then on to SAY */
    put(0xA100, script, sizeof script);
    run_at(0xA100, 1, 5, "");
    EXPECT("", "end"); /* the slot's line, then END: no offer */
    CHECK(talk.phase == IDLE && dest == -1);
}

/* With a file in play, Save files asks first (fax_files.c): the script's end
 * is left alone, and Save files starts Guru travel's part when it chooses. */
static void after_save_files(void) {
    stock();
    visited = 0x2B;
    guru_choice = true;
    guru(5, 5, false, "");
    EXPECT("msg 22", "mantra", "end");
    CHECK(fax_travel_can_offer() && fax_travel_say_step() == SAY);

    /* "Send me to another Guru.": the menu at once; Down, A. */
    run_menu("DA");
    EXPECT(SMALL_BOX, FROM_CONFLATE, "dialogue box", DEPART_LINE, "end");
    CHECK(dest == 1 && talk.back == 1);

    /* B: back to Save files' choice, in the dialogue box opened again; there
     * "Send me to another Guru." opens the menu again, on its first row. */
    stock();
    visited = 0x2B;
    guru_choice = true;
    again_at = CHOICE;
    guru(5, 5, false, "");
    run_menu("DB");
    EXPECT(SMALL_BOX, FROM_CONFLATE, "dialogue box", "msg 99", "end");
    CHECK(dest == -1 && again_calls == 1 && reopened == 1 && talk.phase == IDLE);
    run_menu("A");
    EXPECT(SMALL_BOX, FROM_CONFLATE, "dialogue box", DEPART_LINE, "end");
    CHECK(dest == 0 && menus == 2);

    /* B, and the choice cannot come back: the farewell. */
    stock();
    visited = 0x2B;
    guru_choice = true;
    guru(5, 5, false, "");
    run_menu("B");
    EXPECT(SMALL_BOX, FROM_CONFLATE, "dialogue box", FAREWELL_LINE, "end");
    CHECK(dest == -1 && again_calls == 1);

    /* After saving: the intro and the offer, as after the mantra; B in the
     * menu that A opens says farewell (no choice to go back to). */
    stock();
    visited = 0x2B;
    guru_choice = true;
    guru(5, 5, false, "");
    run(fax_travel_begin(), 5, "AB");
    EXPECT(INTRO, OFFER_LINE, SMALL_BOX, FROM_CONFLATE, "dialogue box", FAREWELL_LINE, "end");
    CHECK(again_calls == 0);

    /* Nowhere to send the hero: nothing to begin. */
    stock();
    visited = 0x01;
    guru_choice = true;
    run(0xA0C6, 0, "");
    CHECK(!fax_travel_can_offer() && fax_travel_begin() == 0 && fax_travel_menu() == 0);
    guru_choice = false;

    /* The furthest temple is the highest visited Guru's, whatever the spawn
     * point says: with Save files it is the Guru last saved with. */
    stock();
    visited = 0x2B;
    meet(1);
    CHECK(talk.offer == 0x29); /* Eolis, Mascon, Conflate from Apolune, spawn point 0 */
}

/* The visited Gurus live in the file in play. */
static void in_the_file(void) {
    stock();
    save_files = true;
    CHECK(save_listener);
    snprintf(gurus_field, sizeof gurus_field, "Apolune Conflate");
    save_listener(FAX_SAVE_CONTINUED);
    CHECK(visited == 0x23);
    meet(3);
    save_listener(FAX_SAVE_WRITING);
    CHECK(!strcmp(gurus_field, "Eolis Apolune Mascon Conflate"));
    save_listener(FAX_SAVE_NEW_GAME);
    CHECK(visited == 1);
    save_files = false;
}

static void journey_path(void) {
    stock();
    dest = 3;
    ram[TEMPLE_SPAWN_POINT] = 5;
    ram[CURRENT_ROM_BANK] = 12;
    CHECK(!main_loop_hook(0xE016) && !journey); /* not back in the main loop's bank yet */
    ram[CURRENT_ROM_BANK] = 14;
    CHECK(main_loop_hook(0xE016) && journey && cpu.s == 0xF9);
    /* RTS from the hook site, then from each routine in turn. */
    CHECK((ram[0x1FA] | ram[0x1FB] << 8) == SCREEN_FADE_TO_BLACK - 1);
    CHECK((ram[0x1FC] | ram[0x1FD] << 8) == GAME_INIT_STATE_SPAWN - 1);
    CHECK((ram[0x1FE] | ram[0x1FF] << 8) == PLAYER_SPAWN_IN_TEMPLE - 1);
    CHECK(!main_loop_hook(0xE016)); /* once */
    CHECK(spawn_hook(0xDD61) == 1 && commits == 1);
    CHECK(commit_temple == 3 && ram[TEMPLE_SPAWN_POINT] == 5 && dest == -1 && !journey);
    CHECK(spawn_hook(0xDD61) == 0 && commits == 1); /* a death's respawn is left alone */
}

static void resume(void) {
    /* A state saved while the menu waits: the phase, its Gurus and where B
     * goes come back, and the slot's line, although read overrides are not
     * machine state. The wait then returns into IScripts_InvokeNextAction,
     * the script parked: A on the third row goes to Mascon. */
    stock();
    talk = (Talk){ MENU, 0x0B, 0, 1 };
    CHECK(serve(TEXT_OFFER));
    uint8_t buf[16];
    int n = state_get(buf, sizeof buf);
    CHECK(n == RECORD && buf[0] == VERSION && state_validate(buf, n));
    serve(TEXT_FAREWELL);
    talk = (Talk){ 0 };
    CHECK(state_set(buf, n) && talk.phase == MENU && talk.offer == 0x0B && talk.back == 1 && text == TEXT_OFFER);
    said_n = 0;
    jump(PARK);
    ram[ARG_STRINGS_COUNT] = 3;
    ram[MENU_CURSOR_POS] = 2;
    cpu.p = 0; /* A: C clear */
    interpret("");
    EXPECT("dialogue box", DEPART_LINE, "end");
    CHECK(dest == 3);

    /* Version 1 (6 bytes, no version), asking about Mascon: the question
     * loads, with its line. A on it goes there; B says farewell. */
    stock();
    const uint8_t v1[RECORD_V1] = { 0xFF, 0, ASKING, 0x0B, 3, TEXT_TO + 3 };
    CHECK(state_validate(v1, RECORD_V1) && state_set(v1, RECORD_V1));
    CHECK(talk.phase == ASKING && talk.asking == 3 && !talk.back && text == TEXT_TO + 3 && dest == -1);
    char line[64];
    slot_text(line);
    CHECK(!strcmp(line, "To the Guru of/Mascon?"));
    said_n = 0;
    jump(SAY); /* A on the question jumps to SAY */
    interpret("");
    EXPECT(DEPART_LINE, "end");
    CHECK(dest == 3);
    stock();
    CHECK(state_set(v1, RECORD_V1));
    said_n = 0;
    jump(ASK + 4); /* B goes on past it */
    interpret("");
    EXPECT(FAREWELL_LINE, "end");
    CHECK(dest == -1);

    /* Version 1 with the intro said before the towns: the menu, and B goes
     * back to Save files' choice, which asked for the towns. */
    stock();
    again_at = CHOICE;
    const uint8_t v1_intro[RECORD_V1] = { 0xFF, 0, INTRO_TOWNS, 0x0B, 0, TEXT_INTRO };
    CHECK(state_set(v1_intro, RECORD_V1));
    said_n = 0;
    jump(SAY + 2);
    interpret("B");
    EXPECT(SMALL_BOX, FROM_CONFLATE, "dialogue box", "msg 99", "end");

    /* What does not load. */
    uint8_t bad[RECORD];
    memcpy(bad, buf, RECORD);
    bad[3] = PHASES;
    CHECK(!state_validate(bad, RECORD) && !state_set(bad, RECORD));
    bad[3] = MENU;
    bad[7] = 2;
    CHECK(!state_validate(bad, RECORD));
    bad[7] = 1;
    bad[0] = 3;
    CHECK(!state_validate(bad, RECORD));
    uint8_t v1_bad[RECORD_V1];
    memcpy(v1_bad, v1, RECORD_V1);
    v1_bad[2] = MENU; /* the menu's phases are version 2's */
    CHECK(!state_validate(v1_bad, RECORD_V1));
    CHECK(!state_validate(buf, 7));
    CHECK(state_set(buf, 0) && talk.phase == IDLE && dest == -1);
}

static void refuses_other_layouts(void) {
    stock();
    deactivate();             /* back to the stock image */
    rom[prg(12, ASK)] = 0x00; /* someone else's bytes where our steps go */
    activate();
    fax_travel_step();
    CHECK(failed && !served && hooks_on_n == 0);
    for (unsigned i = 0; i < 7; ++i) CHECK(!overridden[prg(12, (uint16_t)(SAY + i))]);
}

int main(void) {
    lines_fit();
    what_is_offered();
    the_box();
    menu_strings();
    conversations();
    b_on_the_intro();
    after_save_files();
    in_the_file();
    journey_path();
    resume();
    refuses_other_layouts();
    printf("travel_test: %u checks passed\n", checks);
    return 0;
}
