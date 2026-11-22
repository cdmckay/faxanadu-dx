/* Unit tests for the Magician's lesson (fax_lesson.c), without the emulator or
 * a ROM. The module runs against a model of the machine: CPU RAM, bank 12's
 * two Magicians' scripts as the ROM has them, the read overrides it serves in
 * banks 12 and 13, the hook sites as the engine dispatches them, and the
 * interaction-script interpreter as the disassembly has it: messages and
 * questions (A goes on, B ends), IScriptAction_ShowQuestionMessageCheckIfDismissed
 * (A jumps, B goes on), IScriptAction_SpendGold (its two IScripts_LoadByte,
 * the hook, then IScripts_CheckEnoughMoney: enough is taken, too little is
 * "This is not enough Golds." and the end), ADD_MP, CHECK_FOR_ITEM and
 * ADD_ITEM. So the tests check what the Magician says and what the hero pays
 * and gets, rather than restate the module's constants. */
#include "fax_lesson.c"

static unsigned checks;
#define CHECK(test) do{++checks;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(1);}}while(0)

/* ---- the machine ---- */
static uint8_t ram[0x800];
static bool isolated;
static uint8_t bank12[0x4000];               /* $8000-$BFFF as bank 12 maps it */
/* Read overrides in banks 12 and 13: what a read returns instead, and how
 * many bytes have an entry. Not machine state: a load does not bring them back. */
static uint8_t served[2][0x4000];
static bool serving[2][0x4000];
static unsigned entries;
static bool table_full;                      /* the engine's table has no room: every new entry fails */
bool cyc_mod_set_prg_read_override(uint32_t offset, uint8_t compare, uint8_t value) {
    unsigned bank = offset / 0x4000, at = offset % 0x4000;
    CHECK((bank == 12 || bank == 13) && compare == 0xFF);   /* unused ROM, in the two banks it uses */
    if (!serving[bank - 12][at]) {
        if (table_full) return false;
        ++entries;
    }
    serving[bank - 12][at] = true;
    served[bank - 12][at] = value;
    return true;
}
static void overrides_lost(void) { memset(serving, 0, sizeof serving); entries = 0; }
uint8_t cyc_mod_peek(uint16_t addr) {
    if (addr < 0x2000) return ram[addr & 0x7FF];
    if (addr < 0x8000 || addr >= 0xC000) return 0;
    return serving[0][addr - 0x8000] ? served[0][addr - 0x8000] : bank12[addr - 0x8000];
}
bool cyc_mod_poke(uint16_t addr, uint8_t value) {
    if (addr >= 0x2000) return false;
    ram[addr & 0x7FF] = value;
    return true;
}
bool cyc_mod_isolated(void) { return isolated; }

/* The engine's hook table: registration order, an enabled flag each. */
typedef struct { const char *id; uint16_t addr; NESModFunctionEntryCallback cb; int on; } Site;
static Site sites[8];
static int site_count;
int nes_mod_register_function_entry_plugin(const char *id, uint16_t addr, NESModFunctionEntryCallback cb) {
    if (site_count == 8) return 0;
    sites[site_count++] = (Site){ id, addr, cb, 0 };
    return 1;
}
int nes_mod_set_function_hook_enabled(const char *id, int on) {
    for (int i = 0; i < site_count; ++i)
        if (!strcmp(sites[i].id, id)) { sites[i].on = on; return 1; }
    return 0;
}
static NESModActivationCallback reset_cb, plugin_cb;
static const char *plugin_id;
int nes_mod_register_reset_callback(NESModActivationCallback cb) { reset_cb = cb; return 1; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    plugin_id = id; plugin_cb = cb;
    return 1;
}
void nes_mod_set_local_only(const char *name, int required) { (void)name; (void)required; }
static NESModSavestateGet save_get;
static NESModSavestateSet save_set;
static NESModSavestateValidate save_validate;
int nes_mod_register_savestate_hook(const char *id, NESModSavestateGet get, NESModSavestateSet set) {
    if (strcmp(id, "faxanadu.magician-lesson")) return 0;
    save_get = get; save_set = set;
    return 1;
}
int nes_mod_register_savestate_validator(const char *id, NESModSavestateValidate validate) {
    if (strcmp(id, "faxanadu.magician-lesson")) return 0;
    save_validate = validate;
    return 1;
}
/* Save files (fax_save.c): the file in play's fields, a small key-value list. */
static FaxSaveListener save_listener;
static bool file_in_play;
static struct { char key[FAX_SAVE_KEY], value[FAX_SAVE_VALUE]; } fields[4];
static int field_count;
bool fax_save_listen(FaxSaveListener listener) { save_listener = listener; return true; }
static const char *field(const char *key) {
    for (int i = 0; i < field_count; ++i) if (!strcmp(fields[i].key, key)) return fields[i].value;
    return NULL;
}
const char *fax_save_get(const char *key) { return file_in_play ? field(key) : NULL; }
bool fax_save_set(const char *key, const char *value) {
    if (!file_in_play) return false;
    CHECK(!strncmp(key, "lesson.", 7));      /* its own fields only */
    int i = 0;
    while (i < field_count && strcmp(fields[i].key, key)) ++i;
    if (!value) { if (i < field_count) fields[i] = fields[--field_count]; return true; }
    if (i == field_count) { CHECK(field_count < 4); snprintf(fields[field_count++].key, FAX_SAVE_KEY, "%s", key); }
    snprintf(fields[i].value, FAX_SAVE_VALUE, "%s", value);
    return true;
}

/* Hook sites at `addr`, as the scheduler runs them: enabled callbacks in
 * order until one handles the call. 1 when the routine was handled. */
static int fire(uint16_t addr) {
    for (int i = 0; i < site_count; ++i)
        if (sites[i].addr == addr && sites[i].on && sites[i].cb(addr)) return 1;
    return 0;
}

/* ---- the game, as the disassembly has it ---- */
enum { FIRE_MAGICIAN = 0xA368, MAGIC_FIRE = 0x62, THE_GURU = 0xA0BC };
/* Bank 12 from $A35D: ISCRIPT_EOLIS_MARTIAL_ARTS_MAGIC_SHOP, then ISCRIPT_AFTER_VICTIM_MAGIC_SHOP (Fire for 3000;
 * with Fire, $A377: "Don't rely too much on magic."). Unused ROM ($FF) at $AE00. */
static const uint8_t MAGICIANS[] = { 0x83, 0x02, 0x15, 0x05, 0xC8, 0x00, 0x0A, 0x28, 0x01, 0x16, 0x00,
                                     0x83, 0x12, 0x62, 0x77, 0xA3, 0x02, 0x17, 0x05, 0xB8, 0x0B, 0x07, 0x62, 0x01, 0x79,
                                     0x00, 0x03, 0x18, 0x00 };
static const uint8_t GURU[] = { 0x81, 0x03, 0x2D, 0x00 };   /* someone else's script: a line, the end */
static bool has_fire;

static void set_gold(uint32_t v) { ram[GOLD] = (uint8_t)v; ram[GOLD + 1] = (uint8_t)(v >> 8); ram[GOLD + 2] = (uint8_t)(v >> 16); }
enum { MP = 0x039A };

/* Message $C3, as Messages_Load would read it from bank 13: '/' for a new line. */
static void slot_text(char out[64]) {
    unsigned n = 0;
    for (unsigned i = 0; i < SLOT_LEN && n < 63; ++i) {
        uint16_t at = (uint16_t)(SLOT - 0x8000 + i);
        uint8_t c = serving[1][at] ? served[1][at] : 0xFF;
        if (c == 0xFF) break;
        out[n++] = c == 0xFD ? ' ' : c == 0xFE ? '/' : (char)c;
    }
    out[n] = 0;
}

/* What the box showed, in order: "msg 15", the slot's text, a question's
 * text with "?", "B" for a line B dismissed, "paid 2000", "+40 MP", "Fire",
 * "end". */
static char said[12][64];
static unsigned said_n;
static void note(const char *text) { CHECK(said_n < 12); snprintf(said[said_n++], sizeof said[0], "%s", text); }
static void (*on_line)(void);                /* called as each message shows, for a test to act mid-script */
static void message(uint8_t id, bool question) {
    char text[64];
    if (id == SLOT_ID) slot_text(text); else snprintf(text, sizeof text, "msg %02X", id);
    if (question) strcat(text, "?");
    note(text);
    if (on_line) on_line();
}
static uint16_t base(void) { return (uint16_t)(ram[ISCRIPT_ADDR] | ram[ISCRIPT_ADDR + 1] << 8); }
static void go_to(uint16_t at) { ram[ISCRIPT_ADDR] = (uint8_t)at; ram[ISCRIPT_ADDR + 1] = (uint8_t)(at >> 8); ram[ISCRIPT_OFFSET] = 0; }

/* Run the script at `script` as IScripts_Begin starts it (IScriptOffset 1,
 * past its entity byte), firing IScripts_InvokeNextAction's hooks before each
 * action and going wherever they point it. `keys` answers each question and
 * each line B may dismiss, in order: 'A' goes on, 'B' dismisses (a question
 * $16 goes on past its address instead). */
static void talk_from(uint16_t script, uint8_t offset, const char *keys) {
    said_n = 0;
    ram[ISCRIPT_ADDR] = (uint8_t)script; ram[ISCRIPT_ADDR + 1] = (uint8_t)(script >> 8); ram[ISCRIPT_OFFSET] = offset;
    for (int steps = 0; steps < 32; ++steps) {
        CHECK(!fire(ISCRIPTS_INVOKE_NEXT_ACTION));                 /* it never handles the call */
        uint16_t p = (uint16_t)(base() + ram[ISCRIPT_OFFSET]);
        uint8_t op = cyc_mod_peek(p), arg = cyc_mod_peek((uint16_t)(p + 1));
        uint16_t to = (uint16_t)(arg | cyc_mod_peek((uint16_t)(p + 2)) << 8);
        uint16_t if_held = (uint16_t)(cyc_mod_peek((uint16_t)(p + 2)) | cyc_mod_peek((uint16_t)(p + 3)) << 8);
        char key = *keys ? *keys : 'A';
        switch (op) {
        case 0x00: note("end"); return;
        case 0x01: message(arg, false); ram[ISCRIPT_OFFSET] += 2; break;
        case 0x02: case 0x03:
            message(arg, op == 0x02);
            if (*keys) ++keys;
            if (key == 'B') { note("B"); return; }                  /* IScriptAction_EndScript */
            ram[ISCRIPT_OFFSET] += 2;
            break;
        case 0x16:                                                  /* QUESTION_JUMP_IF_ACCEPTED */
            message(arg, true);
            if (*keys) ++keys;
            if (key == 'B') { note("B"); ram[ISCRIPT_OFFSET] += 4; }  /* IScripts_SkipAddrAndInvoke */
            else go_to(if_held);                                   /* IScripts_JumpToNextAddr */
            break;
        case 0x05: {                                                /* IScriptAction_SpendGold */
            ram[TEMP_0202] = arg; ram[TEMP_0202 + 1] = cyc_mod_peek((uint16_t)(p + 2));
            ram[ISCRIPT_OFFSET] += 3;                                /* the action and its two IScripts_LoadByte */
            CHECK(!fire(SPEND_GOLD_CHECK));                         /* only writes Temp_0202/3 */
            unsigned amount = ram[TEMP_0202] | ram[TEMP_0202 + 1] << 8;
            if (gold() < amount) { message(0x03, false); note("end"); return; }   /* IScripts_ShowNotEnoughMoney */
            set_gold(gold() - amount);
            char text[32];
            snprintf(text, sizeof text, "paid %u", amount);
            note(text);
            break; }
        case 0x0A: {                                                /* IScriptAction_AddMP */
            unsigned mp = ram[MP] + arg;
            ram[MP] = (uint8_t)(mp > 80 ? 80 : mp);
            char text[32];
            snprintf(text, sizeof text, "+%u MP", arg);
            note(text);
            ram[ISCRIPT_OFFSET] += 2;
            break; }
        case 0x12:                                                  /* CHECK_FOR_ITEM: Fire, the one item here */
            if (arg == MAGIC_FIRE && has_fire) go_to(if_held);
            else ram[ISCRIPT_OFFSET] += 4;
            break;
        case 0x07: CHECK(arg == MAGIC_FIRE); has_fire = true; note("Fire"); ram[ISCRIPT_OFFSET] += 2; break;
        case 0x17: go_to(to); break;
        default: fprintf(stderr, "unmodelled action %02X at $%04X\n", op, p); exit(1);
        }
    }
    CHECK(!"the script did not end");
}
static void talk(uint16_t script, const char *keys) { talk_from(script, 1, keys); }
static void expect(const char *const *lines) {
    unsigned n = 0;
    for (; lines[n]; ++n) {
        if (n >= said_n || strcmp(said[n], lines[n])) {
            fprintf(stderr, "said %u: \"%s\", expected \"%s\"\n", n, n < said_n ? said[n] : "", lines[n]);
            for (unsigned i = 0; i < said_n; ++i) fprintf(stderr, "  %s\n", said[i]);
            exit(1);
        }
        ++checks;
    }
    CHECK(n == said_n);
}
#define SAID(...) do { static const char *const lines_[] = { __VA_ARGS__, NULL }; expect(lines_); } while (0)
#define OFFER_LINE "Learn to recover/magic faster/for 2000 Golds.?"
#define TAUGHT_LINE "Your magic will/now return/faster."
#define NOTHING_LINE "I have nothing/more to teach/you."

/* A launch: the remaster on (activated) or off (reset only), a fresh machine. */
static void play(bool on) {
    reset_cb();
    if (on) plugin_cb();
    memset(ram, 0, sizeof ram);
    memset(bank12, 0xFF, sizeof bank12);
    memcpy(bank12 + (MAGICIAN - 0x8000), MAGICIANS, sizeof MAGICIANS);
    memcpy(bank12 + (THE_GURU - 0x8000), GURU, sizeof GURU);
    overrides_lost();
    isolated = table_full = false;
    /* Guru travel serves its SAY and ASK steps, and a line, at power-on. */
    static const uint8_t steps[] = { 0x03, 0xC3, 0x00, 0x16, 0xC3, 0x00, 0xAE };
    for (unsigned i = 0; i < sizeof steps; ++i) CHECK(cyc_mod_set_prg_read_override(prg(12, (uint16_t)(SAY + i)), 0xFF, steps[i]));
    for (unsigned i = 0; i < SLOT_LEN; ++i) CHECK(cyc_mod_set_prg_read_override(prg(13, (uint16_t)(SLOT + i)), 0xFF, 0xFF));
    has_fire = false;
    on_line = NULL;
    file_in_play = false;
    field_count = 0;
}

/* ---- the tests ---- */

static void registers(void) {
    CHECK(!strcmp(plugin_id, "faxanadu.magician-lesson") && reset_cb == deactivate && plugin_cb == activate);
    CHECK(save_get == state_get && save_set == state_set && save_validate == state_validate && save_listener == on_save);
    CHECK(site_count == 2 && sites[0].addr == 0x826E && sites[1].addr == 0x8367);
    CHECK(!strncmp(sites[0].id, "faxanadu.lesson.", 16) && !strncmp(sites[1].id, "faxanadu.lesson.", 16));
}

/* Every line fits his box (16 columns, 4 lines) and the slot (47 bytes and
 * the terminator), and the offer names the price. */
static void lines_fit(void) {
    for (int which = LINE_OFFER; which < LINES; ++which) {
        uint8_t bytes[SLOT_LEN];
        CHECK(line_bytes(which, bytes) && bytes[SLOT_LEN - 1] == 0xFF);
    }
    CHECK(!encode("This line is far too long for a row", (uint8_t[SLOT_LEN]){ 0 }));
    CHECK(!encode("1\n2\n3\n4\n5", (uint8_t[SLOT_LEN]){ 0 }));
    CHECK(!encode("Sixteen columns.\nSixteen columns.\nSixteen columns.", (uint8_t[SLOT_LEN]){ 0 }));   /* 50 bytes */
    char text[64];
    snprintf(text, sizeof text, OFFER_FORMAT, (unsigned)PRICE);
    CHECK(strstr(text, "2000 Golds") && PRICE == 2000);
}

/* Yes, with enough gold: the price is taken, he says it is learned, no MP. */
static void buying(void) {
    play(true);
    set_gold(3000);
    ram[MP] = 10;
    CHECK(!fax_lesson_learned());
    talk(MAGICIAN, "A");
    SAID(OFFER_LINE, "paid 2000", TAUGHT_LINE, "end");
    CHECK(gold() == 1000 && ram[MP] == 10 && fax_lesson_learned());
    CHECK(offers == 1 && taught == 1 && declined == 0 && short_of_gold == 0 && lesson.phase == IDLE);
    /* Exactly the price is enough. */
    play(true);
    set_gold(PRICE);
    talk(MAGICIAN, "A");
    SAID(OFFER_LINE, "paid 2000", TAUGHT_LINE, "end");
    CHECK(gold() == 0 && fax_lesson_learned());
    /* B on his last line ends the conversation: paid, so learned. */
    play(true);
    set_gold(5000);
    talk(MAGICIAN, "AB");
    SAID(OFFER_LINE, "paid 2000", TAUGHT_LINE, "B");
    CHECK(gold() == 3000 && fax_lesson_learned());
}

/* Once learned, nothing more to teach, and nothing taken, every time. */
static void nothing_more_to_teach(void) {
    play(true);
    set_gold(3000);
    talk(MAGICIAN, "A");
    for (int i = 0; i < 2; ++i) {
        talk(MAGICIAN, "A");
        SAID(NOTHING_LINE, "end");
        CHECK(gold() == 1000 && fax_lesson_learned());
    }
    talk(MAGICIAN, "B");
    SAID(NOTHING_LINE, "B");
    CHECK(nothing_more == 3 && offers == 1 && gold() == 1000);
}

/* B on his offer: the box closes, nothing taken. Too little gold: the
 * game's "This is not enough Golds.", and the end. Either way, he offers
 * again next time. */
static void refusals(void) {
    play(true);
    set_gold(3000);
    talk(MAGICIAN, "B");
    SAID(OFFER_LINE, "B", "end");                                 /* past ASK, the SAY step's END */
    CHECK(gold() == 3000 && !fax_lesson_learned() && declined == 1 && lesson.phase == IDLE);
    set_gold(PRICE - 1);
    talk(MAGICIAN, "A");
    SAID(OFFER_LINE, "msg 03", "end");
    CHECK(gold() == PRICE - 1 && !fax_lesson_learned() && short_of_gold == 1);
    CHECK(lesson.phase == PAYING);                                /* the script ended in his pay step */
    /* Someone else's script next: not touched, and his stale pay step forgotten. */
    talk(THE_GURU, "A");
    SAID("msg 2D", "end");
    CHECK(lesson.phase == IDLE);
    set_gold(PRICE - 1);
    talk(MAGICIAN, "A");
    SAID(OFFER_LINE, "msg 03", "end");
    set_gold(3000);
    talk(MAGICIAN, "A");                                          /* the stale pay step, then straight to him */
    SAID(OFFER_LINE, "paid 2000", TAUGHT_LINE, "end");
    CHECK(fax_lesson_learned() && offers == 4 && short_of_gold == 2 && declined == 1);
}

/* The magic hall's Magician (Fire for 3000) is his own script: untouched,
 * even right after a pay step of Eolis's ran short. */
static void the_fire_magician(void) {
    play(true);
    set_gold(PRICE - 1);
    talk(MAGICIAN, "A");
    SAID(OFFER_LINE, "msg 03", "end");
    set_gold(3500);
    talk(FIRE_MAGICIAN, "A");
    SAID("msg 17?", "paid 3000", "Fire", "msg 79", "end");
    CHECK(gold() == 500 && has_fire && !fax_lesson_learned());
    talk(FIRE_MAGICIAN, "A");
    SAID("msg 18", "end");
    /* Learned, the same. */
    set_gold(3000);
    talk(MAGICIAN, "A");
    has_fire = false;
    set_gold(3000);
    talk(FIRE_MAGICIAN, "A");
    SAID("msg 17?", "paid 3000", "Fire", "msg 79", "end");
}

/* The remaster off: the original's 40 MP for 200. */
static void the_original(void) {
    play(false);
    set_gold(3000);
    ram[MP] = 20;
    talk(MAGICIAN, "A");
    SAID("msg 15?", "paid 200", "+40 MP", "msg 16", "end");
    CHECK(gold() == 2800 && ram[MP] == 60 && !fax_lesson_learned());
    talk(MAGICIAN, "B");
    SAID("msg 15?", "B");
    CHECK(gold() == 2800);
}

/* Seamless's isolated simulation of another room steers nothing. */
static void isolated_runs_change_nothing(void) {
    play(true);
    set_gold(3000);
    isolated = true;
    talk(MAGICIAN, "A");
    SAID("msg 15?", "paid 200", "+40 MP", "msg 16", "end");
    CHECK(!fax_lesson_learned() && offers == 0);
}

static void lose_the_overrides(void) {
    if (said_n == 1) { overrides_lost(); table_full = true; }
}
/* No room for his lines' overrides: he sells his MP as he always did, for
 * the rest of the session. */
static void no_room_for_his_lines(void) {
    play(true);
    overrides_lost();
    table_full = true;
    set_gold(3000);
    ram[MP] = 0;
    talk(MAGICIAN, "A");
    SAID("msg 15?", "paid 200", "+40 MP", "msg 16", "end");
    CHECK(failed && !fax_lesson_learned() && gold() == 2800);
    table_full = false;
    talk(MAGICIAN, "A");
    SAID("msg 15?", "paid 200", "+40 MP", "msg 16", "end");
    /* The overrides gone while his offer shows, with no room for them
     * again: A still reaches his pay step (the hook moves the script off the
     * SAY step before it is read), and paid, the hero has learned; the stock
     * script goes on from there. */
    play(true);
    set_gold(3000);
    ram[MP] = 0;
    on_line = lose_the_overrides;
    talk(MAGICIAN, "A");
    SAID(OFFER_LINE, "paid 2000", "+40 MP", "msg 16", "end");
    CHECK(failed && fax_lesson_learned() && gold() == 1000 && ram[MP] == 40);
}

/* Save files: learned is the file's lesson.learned. */
static void in_the_file(void) {
    play(true);
    file_in_play = true;
    save_listener(FAX_SAVE_NEW_GAME);
    save_listener(FAX_SAVE_WRITING);
    CHECK(field("lesson.learned") == NULL);
    set_gold(3000);
    talk(MAGICIAN, "A");
    save_listener(FAX_SAVE_WRITING);
    CHECK(field("lesson.learned") && !strcmp(field("lesson.learned"), "yes"));
    /* Quit, then continue the file: learned. */
    play(true);
    file_in_play = true;
    snprintf(fields[0].key, FAX_SAVE_KEY, "lesson.learned");
    snprintf(fields[0].value, FAX_SAVE_VALUE, "yes");
    field_count = 1;
    CHECK(!fax_lesson_learned());
    save_listener(FAX_SAVE_CONTINUED);
    CHECK(fax_lesson_learned());
    talk(MAGICIAN, "A");
    SAID(NOTHING_LINE, "end");
    /* A new game: not learned. A file without the field (from before): not learned. */
    save_listener(FAX_SAVE_NEW_GAME);
    CHECK(!fax_lesson_learned());
    save_listener(FAX_SAVE_WRITING);
    CHECK(field("lesson.learned") == NULL);
    save_listener(FAX_SAVE_CONTINUED);
    CHECK(!fax_lesson_learned());
    /* With the remaster off it neither reads nor writes. */
    reset_cb();
    field_count = 1;
    snprintf(fields[0].key, FAX_SAVE_KEY, "lesson.learned");
    snprintf(fields[0].value, FAX_SAVE_VALUE, "yes");
    save_listener(FAX_SAVE_CONTINUED);
    CHECK(!fax_lesson_learned() && !lesson.learned);
    save_listener(FAX_SAVE_NEW_GAME);
    save_listener(FAX_SAVE_WRITING);
    CHECK(field("lesson.learned") && field_count == 1);
}

static uint8_t mid[RECORD], mid_ram[0x800];      /* a save state taken as his offer shows */
static void save_mid(void) {
    if (mid[0]) return;
    CHECK(save_get(mid, RECORD) == RECORD);
    memcpy(mid_ram, ram, sizeof ram);
}
/* The save-state record: learned and the conversation; a line that was up
 * is served again once the whole state is in. */
static void save_states(void) {
    play(true);
    uint8_t buf[8];
    CHECK(save_get(buf, RECORD - 1) == -1);
    CHECK(save_get(buf, sizeof buf) == RECORD && buf[0] == 1 && buf[1] == 0 && buf[2] == IDLE && buf[3] == LINE_NONE);
    /* Saved as his offer shows: loaded into a game whose slot holds another line. */
    set_gold(3000);
    on_line = save_mid;
    talk(MAGICIAN, "B");
    on_line = NULL;
    CHECK(mid[1] == 0 && mid[2] == ASKING && mid[3] == LINE_OFFER && save_validate(mid, RECORD));
    play(true);                                                    /* a launch: the overrides gone, Guru travel's back */
    set_gold(3000);
    talk(MAGICIAN, "A");                                           /* learned, his last line in the slot */
    memcpy(ram, mid_ram, sizeof ram);
    CHECK(save_set(mid, RECORD) == 1 && !fax_lesson_learned());
    fax_lesson_loaded();
    char text[64];
    slot_text(text);
    CHECK(!strcmp(text, "Learn to recover/magic faster/for 2000 Golds."));
    /* The box has his offer; A on it now jumps to the SAY step. */
    talk_from(SAY, 0, "");
    SAID("paid 2000", TAUGHT_LINE, "end");
    CHECK(fax_lesson_learned() && gold() == 1000);
    /* Saved learned: loaded, learned. */
    CHECK(save_get(buf, sizeof buf) == RECORD && buf[1] == 1);
    play(true);
    CHECK(save_set(buf, RECORD) == 1 && fax_lesson_learned());
    /* A state from before the feature: not learned. */
    CHECK(save_validate(buf, 0) && save_set(buf, 0) == 1 && !fax_lesson_learned() && lesson.phase == IDLE);
    /* Records it does not accept change nothing. */
    CHECK(save_set(buf, RECORD) == 1);
    static const struct { int at; uint8_t value; } bad[] = {
        { 0, 2 }, { 1, 2 }, { 2, PHASES }, { 3, LINES }, { 3, LINE_OFFER }, { 2, ASKING }, { 2, SAYING } };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        uint8_t b[RECORD];
        memcpy(b, buf, sizeof b);
        b[bad[i].at] = bad[i].value;
        CHECK(!save_validate(b, sizeof b) && !save_set(b, sizeof b) && fax_lesson_learned());
    }
    CHECK(!save_validate(buf, RECORD - 1) && !save_validate(buf, RECORD + 1));
    const uint8_t saying[RECORD] = { 1, 1, SAYING, LINE_NOTHING_MORE }, paying[RECORD] = { 1, 0, PAYING, LINE_NONE };
    CHECK(save_validate(saying, RECORD) && save_validate(paying, RECORD));
}

int main(void) {
    registers();
    lines_fit();
    buying();
    nothing_more_to_teach();
    refusals();
    the_fire_magician();
    the_original();
    isolated_runs_change_nothing();
    no_room_for_his_lines();
    in_the_file();
    save_states();
    printf("lesson_test: %u checks passed\n", checks);
    return 0;
}
