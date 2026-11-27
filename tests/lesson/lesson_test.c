/* Unit tests for the Magician's lesson (fax_lesson.c), without the emulator or
 * a ROM. The module runs against a model of the machine: CPU RAM, bank 12's
 * two Magicians' scripts as the ROM has them, the read overrides it serves in
 * banks 12 and 13, the hook sites as the engine dispatches them, and the
 * interaction-script interpreter as the disassembly has it: messages and
 * questions (A goes on, B ends), IScriptAction_ShowQuestionMessageCheckIfDismissed
 * (A jumps, B goes on), IScriptAction_SpendGold (its two IScripts_LoadByte,
 * the price hook, the copy to Temp_Int24, then IScripts_CheckEnoughMoney:
 * too little is "This is not enough Golds." and the end, enough is taken by
 * IScripts_ProgressivelySubtractGold a turn a frame, with the count's hook
 * before each Player_SubtractGold, then its 60-frame wait), ADD_MP,
 * CHECK_FOR_ITEM and ADD_ITEM. So the tests check what the Magician says, what
 * the hero pays and gets, and how many frames the paying takes, rather than
 * restate the module's constants. */
#include "fax_lesson.c"

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
static bool isolated;
static uint8_t bank12[0x4000]; /* $8000-$BFFF as bank 12 maps it */
/* Read overrides in banks 12 and 13: what a read returns instead, and how
 * many bytes have an entry. Not machine state: a load does not bring them back. */
static uint8_t served[2][0x4000];
static bool serving[2][0x4000];
static unsigned entries;
static bool table_full; /* the engine's table has no room: every new entry fails */
bool cyc_mod_set_prg_read_override(uint32_t offset, uint8_t compare, uint8_t value) {
    unsigned bank = offset / 0x4000, at = offset % 0x4000;
    CHECK((bank == 12 || bank == 13) && compare == 0xFF); /* unused ROM, in the two banks it uses */
    if (!serving[bank - 12][at]) {
        if (table_full) return false;
        ++entries;
    }
    serving[bank - 12][at] = true;
    served[bank - 12][at] = value;
    return true;
}
static void overrides_lost(void) {
    memset(serving, 0, sizeof serving);
    entries = 0;
}
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
typedef struct {
    const char *id;
    uint16_t addr;
    NESModFunctionEntryCallback cb;
    int on;
} Site;
static Site sites[8];
static int site_count;
int nes_mod_register_function_entry_plugin(const char *id, uint16_t addr, NESModFunctionEntryCallback cb) {
    if (site_count == 8) return 0;
    sites[site_count++] = (Site){ id, addr, cb, 0 };
    return 1;
}
int nes_mod_set_function_hook_enabled(const char *id, int on) {
    for (int i = 0; i < site_count; ++i)
        if (!strcmp(sites[i].id, id)) {
            sites[i].on = on;
            return 1;
        }
    return 0;
}
static NESModActivationCallback reset_cb, plugin_cb;
static const char *plugin_id;
int nes_mod_register_reset_callback(NESModActivationCallback cb) {
    reset_cb = cb;
    return 1;
}
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    plugin_id = id;
    plugin_cb = cb;
    return 1;
}
void nes_mod_set_local_only(const char *name, int required) {
    (void)name;
    (void)required;
}
static NESModSavestateGet save_get;
static NESModSavestateSet save_set;
static NESModSavestateValidate save_validate;
int nes_mod_register_savestate_hook(const char *id, NESModSavestateGet get, NESModSavestateSet set) {
    if (strcmp(id, "faxanadu.magician-lesson")) return 0;
    save_get = get;
    save_set = set;
    return 1;
}
int nes_mod_register_savestate_validator(const char *id, NESModSavestateValidate validate) {
    if (strcmp(id, "faxanadu.magician-lesson")) return 0;
    save_validate = validate;
    return 1;
}
/* Save files (fax_save.c): the file in play's fields, a small key-value list,
 * its core fields' keys among them. */
const char *const fax_save_list_key[FAX_SAVE_LISTS] = { "weapons", "armor", "shields", "spells", "items" };
const char *const fax_save_selected_key[FAX_SAVE_LISTS] = { "selected_weapon", "selected_armor", "selected_shield",
                                                            "selected_spell", "selected_item" };
static FaxSaveListener save_listener;
static bool file_in_play;
enum { FIELDS = 8 };
static struct {
    char key[FAX_SAVE_KEY], value[FAX_SAVE_VALUE];
} fields[FIELDS];
static int field_count;
bool fax_save_listen(FaxSaveListener listener) {
    save_listener = listener;
    return true;
}
static const char *field(const char *key) {
    for (int i = 0; i < field_count; ++i)
        if (!strcmp(fields[i].key, key)) return fields[i].value;
    return NULL;
}
/* Set or remove (NULL) a field, as a file on disk would have it. */
static void put(const char *key, const char *value) {
    int i = 0;
    while (i < field_count && strcmp(fields[i].key, key)) ++i;
    if (!value) {
        if (i < field_count) fields[i] = fields[--field_count];
        return;
    }
    if (i == field_count) {
        CHECK(field_count < FIELDS);
        snprintf(fields[field_count++].key, FAX_SAVE_KEY, "%s", key);
    }
    snprintf(fields[i].value, FAX_SAVE_VALUE, "%s", value);
}
const char *fax_save_get(const char *key) { return file_in_play ? field(key) : NULL; }
bool fax_save_set(const char *key, const char *value) {
    if (!file_in_play) return false;
    CHECK(!strncmp(key, "lesson.", 7)); /* its own fields only */
    put(key, value);
    return true;
}
static bool is(const char *key, const char *value) {
    const char *v = field(key);
    return v && !strcmp(v, value);
}

/* Hook sites at `addr`, as the scheduler runs them: enabled callbacks in
 * order until one handles the call. 1 when the routine was handled. */
static int fire(uint16_t addr) {
    for (int i = 0; i < site_count; ++i)
        if (sites[i].addr == addr && sites[i].on && sites[i].cb(addr)) return 1;
    return 0;
}

/* ---- the game, as the disassembly has it ---- */
enum { FIRE_MAGICIAN = 0xA368, ITEM_FIRE = 0x62, THE_GURU = 0xA0BC, A_SHOP = 0xA42E };
/* Bank 12 from $A35D: ISCRIPT_EOLIS_MARTIAL_ARTS_MAGIC_SHOP, then ISCRIPT_AFTER_VICTIM_MAGIC_SHOP (Fire for 3000;
 * with Fire, $A377: "Don't rely too much on magic."). Unused ROM ($FF) at $AE00. */
static const uint8_t MAGICIANS[] = { 0x83, 0x02, 0x15, 0x05, 0xC8, 0x00, 0x0A, 0x28, 0x01, 0x16,
                                     0x00, 0x83, 0x12, 0x62, 0x77, 0xA3, 0x02, 0x17, 0x05, 0xB8,
                                     0x0B, 0x07, 0x62, 0x01, 0x79, 0x00, 0x03, 0x18, 0x00 };
static const uint8_t GURU[] = { 0x81, 0x03, 0x2D, 0x00 }; /* someone else's script: a line, the end */

static void set_gold(uint32_t v) {
    ram[GOLD] = (uint8_t)v;
    ram[GOLD + 1] = (uint8_t)(v >> 8);
    ram[GOLD + 2] = (uint8_t)(v >> 16);
}
enum { MP = 0x039A };

/* The spells: Player_LacksItem looks at the one in hand and the list,
 * Player_AddToInventory appends to the list (or overwrites its last entry
 * when it is full), and selling one takes it out of the list. */
static bool holds_fire(void) {
    if (ram[SELECTED_MAGIC] == MAGIC_FIRE) return true;
    for (unsigned i = 0; i < ram[NUMBER_OF_MAGIC] && i < 4; ++i)
        if (ram[MAGIC_INVENTORY + i] == MAGIC_FIRE) return true;
    return false;
}
static void add_spell(uint8_t spell) {
    unsigned n = ram[NUMBER_OF_MAGIC];
    if (n >= 4) n = 3;
    else ram[NUMBER_OF_MAGIC] = (uint8_t)(n + 1);
    ram[MAGIC_INVENTORY + n] = spell;
}
static void sell_fire(void) {
    if (ram[SELECTED_MAGIC] == MAGIC_FIRE) ram[SELECTED_MAGIC] = 0xFF;
    for (unsigned i = 0; i < ram[NUMBER_OF_MAGIC]; ++i)
        if (ram[MAGIC_INVENTORY + i] == MAGIC_FIRE) {
            memmove(ram + MAGIC_INVENTORY + i, ram + MAGIC_INVENTORY + i + 1, 3 - i);
            --ram[NUMBER_OF_MAGIC];
            break;
        }
    CHECK(!holds_fire());
}

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
static void note(const char *text) {
    CHECK(said_n < 12);
    snprintf(said[said_n++], sizeof said[0], "%s", text);
}
static void (*on_line)(void); /* called as each message shows, for a test to act mid-script */
static void message(uint8_t id, bool question) {
    char text[64];
    if (id == SLOT_ID) slot_text(text);
    else snprintf(text, sizeof text, "msg %02X", id);
    if (question) strcat(text, "?");
    note(text);
    if (on_line) on_line();
}
static uint16_t base(void) { return (uint16_t)(ram[ISCRIPT_ADDR] | ram[ISCRIPT_ADDR + 1] << 8); }
static void go_to(uint16_t at) {
    ram[ISCRIPT_ADDR] = (uint8_t)at;
    ram[ISCRIPT_ADDR + 1] = (uint8_t)(at >> 8);
    ram[ISCRIPT_OFFSET] = 0;
}

/* The last payment: its turns that took gold (a frame each), the least and
 * most one took, and the frames until IScripts_ProgressivelySubtractGold
 * returned, its 60-frame wait included. */
static unsigned pay_turns, pay_frames, take_min, take_max;
static uint16_t temp(uint16_t addr) { return (uint16_t)(ram[addr] | ram[addr + 1] << 8); }
static void set_temp(uint16_t addr, unsigned v) {
    ram[addr] = (uint8_t)v;
    ram[addr + 1] = (uint8_t)(v >> 8);
}
/* IScripts_CheckEnoughMoney: Gold against Temp_Int24/_M, the carry. */
static bool enough_money(void) {
    if (ram[GOLD + 2] != 0) return true;
    if (ram[GOLD + 1] != ram[TEMP_INT24 + 1]) return ram[GOLD + 1] > ram[TEMP_INT24 + 1];
    return ram[GOLD] >= ram[TEMP_INT24];
}
/* IScripts_ProgressivelySubtractGold, a frame a turn: while Temp_0203 has no
 * bit 7 and Temp_0202/3 something left, 10 off it, 10 in Temp_Int24, the
 * count's hook site, and Player_SubtractGold of Temp_Int24; then DEC
 * Temp_0203 (to $FF), 60 in Temp_0202, and down to 0. */
static void subtract_progressively(void) {
    pay_turns = pay_frames = take_max = 0;
    take_min = ~0u;
    for (;;) {
        ++pay_frames; /* IScripts_UpdatePortraitAnimation */
        if (!(ram[TEMP_0202 + 1] & 0x80)) {
            if (temp(TEMP_0202)) {
                set_temp(TEMP_0202, temp(TEMP_0202) - 10u);
                set_temp(TEMP_INT24, 10);
                CHECK(!fire(SUBTRACT_PAY)); /* only writes Temp_0202/3 and Temp_Int24 */
                unsigned take = temp(TEMP_INT24);
                set_gold((gold() - take) & 0xFFFFFF);
                ++pay_turns;
                if (take < take_min) take_min = take;
                if (take > take_max) take_max = take;
                continue;
            }
            --ram[TEMP_0202 + 1];
            ram[TEMP_0202] = 0x3C;
        }
        if (--ram[TEMP_0202] == 0) return;
    }
}

/* Run the script at `script` as IScripts_Begin starts it (IScriptOffset 1,
 * past its entity byte), firing IScripts_InvokeNextAction's hooks before each
 * action and going wherever they point it. `keys` answers each question and
 * each line B may dismiss, in order: 'A' goes on, 'B' dismisses (a question
 * $16 goes on past its address instead). */
static void talk_from(uint16_t script, uint8_t offset, const char *keys) {
    said_n = 0;
    ram[ISCRIPT_ADDR] = (uint8_t)script;
    ram[ISCRIPT_ADDR + 1] = (uint8_t)(script >> 8);
    ram[ISCRIPT_OFFSET] = offset;
    for (int steps = 0; steps < 32; ++steps) {
        CHECK(!fire(ISCRIPTS_INVOKE_NEXT_ACTION)); /* it never handles the call */
        uint16_t p = (uint16_t)(base() + ram[ISCRIPT_OFFSET]);
        uint8_t op = cyc_mod_peek(p), arg = cyc_mod_peek((uint16_t)(p + 1));
        uint16_t to = (uint16_t)(arg | cyc_mod_peek((uint16_t)(p + 2)) << 8);
        uint16_t if_held = (uint16_t)(cyc_mod_peek((uint16_t)(p + 2)) | cyc_mod_peek((uint16_t)(p + 3)) << 8);
        char key = *keys ? *keys : 'A';
        switch (op) {
        case 0x00:
            note("end");
            return;
        case 0x01:
            message(arg, false);
            ram[ISCRIPT_OFFSET] += 2;
            break;
        case 0x02:
        case 0x03:
            message(arg, op == 0x02);
            if (*keys) ++keys;
            if (key == 'B') {
                note("B");
                return;
            } /* IScriptAction_EndScript */
            ram[ISCRIPT_OFFSET] += 2;
            break;
        case 0x16: /* QUESTION_JUMP_IF_ACCEPTED */
            message(arg, true);
            if (*keys) ++keys;
            if (key == 'B') {
                note("B");
                ram[ISCRIPT_OFFSET] += 4;
            } /* IScripts_SkipAddrAndInvoke */
            else
                go_to(if_held); /* IScripts_JumpToNextAddr */
            break;
        case 0x05: { /* IScriptAction_SpendGold */
            ram[TEMP_0202] = arg;
            ram[TEMP_0202 + 1] = cyc_mod_peek((uint16_t)(p + 2));
            ram[ISCRIPT_OFFSET] += 3;       /* the action and its two IScripts_LoadByte */
            CHECK(!fire(SPEND_GOLD_CHECK)); /* only writes Temp_0202/3 */
            unsigned amount = temp(TEMP_0202);
            set_temp(TEMP_INT24, amount);
            if (!enough_money()) {
                message(0x03, false);
                note("end");
                return;
            } /* IScripts_ShowNotEnoughMoney */
            subtract_progressively();
            char text[32];
            snprintf(text, sizeof text, "paid %u", amount);
            note(text);
            break;
        }
        case 0x0A: { /* IScriptAction_AddMP */
            unsigned mp = ram[MP] + arg;
            ram[MP] = (uint8_t)(mp > 80 ? 80 : mp);
            char text[32];
            snprintf(text, sizeof text, "+%u MP", arg);
            note(text);
            ram[ISCRIPT_OFFSET] += 2;
            break;
        }
        case 0x12: /* CHECK_FOR_ITEM: Fire, the one item here */
            CHECK(arg == ITEM_FIRE);
            if (holds_fire()) go_to(if_held);
            else ram[ISCRIPT_OFFSET] += 4;
            break;
        case 0x07:
            CHECK(arg == ITEM_FIRE);
            add_spell(MAGIC_FIRE);
            note("Fire");
            ram[ISCRIPT_OFFSET] += 2;
            break;
        case 0x17:
            go_to(to);
            break;
        default:
            fprintf(stderr, "unmodelled action %02X at $%04X\n", op, p);
            exit(1);
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
#define SAID(...)                                                                                                      \
    do {                                                                                                               \
        static const char *const lines_[] = { __VA_ARGS__, NULL };                                                     \
        expect(lines_);                                                                                                \
    } while (0)
#define TEACHES_LINE   "I can teach you/to recover magic/faster."
#define NOT_READY_LINE "First study at/the magic hall/in the Mist."
#define PRICE_LINE     "It will cost/30000 Golds.?"
#define TAUGHT_LINE    "Your magic will/now return/faster."
#define NOTHING_LINE   "I have nothing/more to teach/you."
#define HALL_SALE      "msg 17?", "paid 3000", "Fire", "msg 79", "end"

/* A launch: the remaster on (activated) or off (reset only), a fresh machine. */
static void play(bool on) {
    reset_cb();
    if (on) plugin_cb();
    memset(ram, 0, sizeof ram);
    ram[SELECTED_MAGIC] = 0xFF;
    memset(bank12, 0xFF, sizeof bank12);
    memcpy(bank12 + (MAGICIAN - 0x8000), MAGICIANS, sizeof MAGICIANS);
    memcpy(bank12 + (THE_GURU - 0x8000), GURU, sizeof GURU);
    overrides_lost();
    isolated = table_full = false;
    /* Guru travel serves its SAY and ASK steps, and a line, at power-on. */
    static const uint8_t steps[] = { 0x03, 0xC3, 0x00, 0x16, 0xC3, 0x00, 0xAE };
    for (unsigned i = 0; i < sizeof steps; ++i)
        CHECK(cyc_mod_set_prg_read_override(prg(12, (uint16_t)(SAY + i)), 0xFF, steps[i]));
    for (unsigned i = 0; i < SLOT_LEN; ++i)
        CHECK(cyc_mod_set_prg_read_override(prg(13, (uint16_t)(SLOT + i)), 0xFF, 0xFF));
    on_line = NULL;
    file_in_play = false;
    field_count = 0;
}
/* The hero studies at the magic hall: Fire bought from its Magician, with
 * the gold for it given and taken. */
static void study(void) {
    uint32_t had = gold();
    set_gold(had + 3000);
    talk(FIRE_MAGICIAN, "A");
    SAID(HALL_SALE);
    CHECK(gold() == had && fax_lesson_studied());
}

/* ---- the tests ---- */

static void registers(void) {
    CHECK(!strcmp(plugin_id, "faxanadu.magician-lesson") && reset_cb == deactivate && plugin_cb == activate);
    CHECK(save_get == state_get && save_set == state_set && save_validate == state_validate &&
          save_listener == on_save);
    CHECK(site_count == 3 && sites[0].addr == 0x826E && sites[1].addr == 0x8367 && sites[2].addr == 0x8347);
    for (int i = 0; i < site_count; ++i) CHECK(!strncmp(sites[i].id, "faxanadu.lesson.", 16));
}

/* Every line fits his box (16 columns, 4 lines) and the slot (47 bytes and
 * the terminator); the price line names the price, and none names the
 * spell. */
static void lines_fit(void) {
    for (int which = LINE_PRICE; which < LINES; ++which) {
        uint8_t bytes[SLOT_LEN];
        CHECK(line_bytes(which, bytes) && bytes[SLOT_LEN - 1] == 0xFF);
        char text[64];
        if (which == LINE_PRICE) snprintf(text, sizeof text, PRICE_FORMAT, (unsigned)PRICE);
        else snprintf(text, sizeof text, "%s", line_text[which]);
        CHECK(strlen(text) <= SLOT_LEN - 1 && !strstr(text, "Fire") && !strstr(text, "colleague"));
    }
    CHECK(!encode("This line is far too long for a row", (uint8_t[SLOT_LEN]){ 0 }));
    CHECK(!encode("1\n2\n3\n4\n5", (uint8_t[SLOT_LEN]){ 0 }));
    CHECK(!encode("Sixteen columns.\nSixteen columns.\nSixteen columns.", (uint8_t[SLOT_LEN]){ 0 })); /* 50 bytes */
    char text[64];
    snprintf(text, sizeof text, PRICE_FORMAT, (unsigned)PRICE);
    CHECK(!strcmp(text, "It will cost\n30000 Golds.") && PRICE == 30000);
    CHECK(!strcmp(NOT_READY_TEXT, "First study at\nthe magic hall\nin the Mist.") && strlen(NOT_READY_TEXT) == 42);
}

/* Not studied, with gold enough and more: what he teaches, then where to go
 * first, and the end. Gold is never checked or taken. */
static void not_studied(void) {
    play(true);
    set_gold(40000);
    CHECK(!fax_lesson_studied());
    talk(MAGICIAN, "A");
    SAID(TEACHES_LINE, NOT_READY_LINE, "end");
    CHECK(gold() == 40000 && !fax_lesson_learned() && lesson.phase == IDLE);
    CHECK(seen.told == 1 && seen.not_ready == 1 && seen.offers == 0 && seen.short_of_gold == 0);
    /* With none at all, the same: his gold check is never reached. */
    set_gold(0);
    talk(MAGICIAN, "A");
    SAID(TEACHES_LINE, NOT_READY_LINE, "end");
    /* B on either page ends it. */
    talk(MAGICIAN, "B");
    SAID(TEACHES_LINE, "B");
    talk(MAGICIAN, "AB");
    SAID(TEACHES_LINE, NOT_READY_LINE, "B");
    CHECK(gold() == 0 && seen.not_ready == 3 && seen.told == 4 && seen.offers == 0 && !fax_lesson_learned());
    /* Holding Fire some other way is not having studied (only the old
     * files' and states' fallback looks at the spells). */
    add_spell(MAGIC_FIRE);
    talk(MAGICIAN, "A");
    SAID(TEACHES_LINE, NOT_READY_LINE, "end");
}

/* Studied, with exactly the price: taken 150 a frame, 200 frames and then
 * the game's 60; he says it is learned, no MP. */
static void buying(void) {
    play(true);
    study();
    set_gold(PRICE);
    ram[MP] = 10;
    talk(MAGICIAN, "AA");
    SAID(TEACHES_LINE, PRICE_LINE, "paid 30000", TAUGHT_LINE, "end");
    CHECK(gold() == 0 && ram[MP] == 10 && fax_lesson_learned());
    CHECK(pay_turns == 200 && take_min == 150 && take_max == 150 && pay_frames == 260 && seen.pays == 200);
    CHECK(seen.told == 1 && seen.offers == 1 && seen.taught == 1 && seen.declined == 0 && lesson.phase == IDLE);
    /* More than the price: exactly 30000 taken. */
    play(true);
    study();
    set_gold(54321);
    talk(MAGICIAN, "AA");
    SAID(TEACHES_LINE, PRICE_LINE, "paid 30000", TAUGHT_LINE, "end");
    CHECK(gold() == 24321 && pay_turns == 200 && fax_lesson_learned());
    /* B on his last line ends the conversation: paid, so learned. */
    play(true);
    study();
    set_gold(30000);
    talk(MAGICIAN, "AAB");
    SAID(TEACHES_LINE, PRICE_LINE, "paid 30000", TAUGHT_LINE, "B");
    CHECK(gold() == 0 && fax_lesson_learned());
}

/* Studied, 10 short: the game's "This is not enough Golds.", and the end. */
static void short_of_gold(void) {
    play(true);
    study();
    set_gold(PRICE - 1);
    talk(MAGICIAN, "AA");
    SAID(TEACHES_LINE, PRICE_LINE, "msg 03", "end");
    CHECK(gold() == 29999 && !fax_lesson_learned() && seen.short_of_gold == 1 && seen.pays == 0);
    CHECK(lesson.phase == PAYING); /* the script ended in his pay step */
    /* Gold in the top byte only, 65536 of it: enough. */
    set_gold(0x10000);
    talk(MAGICIAN, "AA");
    SAID(TEACHES_LINE, PRICE_LINE, "paid 30000", TAUGHT_LINE, "end");
    CHECK(gold() == 0x10000 - 30000 && fax_lesson_learned());
}

/* Once learned, nothing more to teach, and nothing taken, every time. */
static void nothing_more_to_teach(void) {
    play(true);
    study();
    set_gold(30000);
    talk(MAGICIAN, "AA");
    set_gold(30000);
    for (int i = 0; i < 2; ++i) {
        talk(MAGICIAN, "A");
        SAID(NOTHING_LINE, "end");
        CHECK(gold() == 30000 && fax_lesson_learned());
    }
    talk(MAGICIAN, "B");
    SAID(NOTHING_LINE, "B");
    CHECK(seen.nothing_more == 3 && seen.offers == 1 && seen.told == 1 && gold() == 30000);
}

/* B on his price: the box closes, nothing taken. Either way, he asks again
 * next time, and someone else's script forgets his stale pay step. */
static void refusals(void) {
    play(true);
    study();
    set_gold(30000);
    talk(MAGICIAN, "AB");
    SAID(TEACHES_LINE, PRICE_LINE, "B", "end"); /* past ASK, the SAY step's END */
    CHECK(gold() == 30000 && !fax_lesson_learned() && seen.declined == 1 && lesson.phase == IDLE);
    set_gold(PRICE - 1);
    talk(MAGICIAN, "AA");
    SAID(TEACHES_LINE, PRICE_LINE, "msg 03", "end");
    CHECK(lesson.phase == PAYING);
    talk(THE_GURU, "A");
    SAID("msg 2D", "end");
    CHECK(lesson.phase == IDLE);
    set_gold(PRICE - 1);
    talk(MAGICIAN, "AA");
    SAID(TEACHES_LINE, PRICE_LINE, "msg 03", "end");
    set_gold(30000);
    talk(MAGICIAN, "AA"); /* the stale pay step, then straight to him */
    SAID(TEACHES_LINE, PRICE_LINE, "paid 30000", TAUGHT_LINE, "end");
    CHECK(fax_lesson_learned() && seen.offers == 4 && seen.short_of_gold == 2 && seen.declined == 1);
}

/* The magic hall's Magician (Fire for 3000) is his own script, untouched: it
 * sells Fire at the game's 10 a frame, even right after a pay step of
 * Eolis's ran short, and the hero has studied once it gives Fire. Not when
 * he is short or says no; and selling Fire after does not undo it. */
static void the_magic_hall(void) {
    play(true);
    study();
    CHECK(pay_turns == 300 && take_min == 10 && take_max == 10 && pay_frames == 360 && seen.pays == 0);
    CHECK(seen.fire_given == 1 && holds_fire());
    talk(FIRE_MAGICIAN, "A"); /* with Fire */
    SAID("msg 18", "end");
    sell_fire();
    CHECK(fax_lesson_studied());
    set_gold(30000);
    talk(MAGICIAN, "AA");
    SAID(TEACHES_LINE, PRICE_LINE, "paid 30000", TAUGHT_LINE, "end");
    /* Short at the hall, or B on its question: not studied. */
    play(true);
    set_gold(2999);
    talk(FIRE_MAGICIAN, "A");
    SAID("msg 17?", "msg 03", "end");
    talk(FIRE_MAGICIAN, "B");
    SAID("msg 17?", "B");
    CHECK(!fax_lesson_studied() && seen.fire_given == 0 && gold() == 2999);
    /* After Eolis's pay step ran short (his phase left PAYING): the hall
     * still counts 10. */
    play(true);
    study();
    set_gold(PRICE - 1);
    talk(MAGICIAN, "AA");
    SAID(TEACHES_LINE, PRICE_LINE, "msg 03", "end");
    CHECK(lesson.phase == PAYING);
    sell_fire();
    set_gold(3000);
    talk(FIRE_MAGICIAN, "A");
    SAID(HALL_SALE);
    CHECK(pay_turns == 300 && take_max == 10 && gold() == 0 && seen.pays == 0);
    /* Learned, the same. */
    set_gold(30000);
    talk(MAGICIAN, "AA");
    sell_fire();
    set_gold(3000);
    talk(FIRE_MAGICIAN, "A");
    SAID(HALL_SALE);
    CHECK(take_max == 10 && seen.fire_given == 3);
}

/* Every other payment counts 10: a shop's purchase calls the same routine
 * with the script elsewhere, even were his pay step still marked running. */
static void other_payments(void) {
    play(true);
    lesson.phase = PAYING;
    set_gold(5000);
    go_to(A_SHOP);
    ram[ISCRIPT_OFFSET] = 3;
    set_temp(TEMP_0202, 2500);
    subtract_progressively();
    CHECK(gold() == 2500 && pay_turns == 250 && take_min == 10 && take_max == 10 && pay_frames == 310);
    CHECK(seen.pays == 0);
    /* His count never takes more than is left: an amount that is not a
     * multiple of 150 ends at 10 a frame, and exactly that much is taken. */
    play(true);
    lesson.phase = PAYING;
    go_to(MAGICIAN);
    ram[ISCRIPT_OFFSET] = 6;
    set_gold(1500);
    set_temp(TEMP_0202, 1000);
    subtract_progressively();
    CHECK(gold() == 500 && pay_turns == 6 + 10 && take_min == 10 && take_max == 150 && seen.pays == 6);
    /* Isolated, his own count is 10 too (the price hook did not run there). */
    play(true);
    lesson.phase = PAYING;
    go_to(MAGICIAN);
    ram[ISCRIPT_OFFSET] = 6;
    isolated = true;
    set_gold(200);
    set_temp(TEMP_0202, 200);
    subtract_progressively();
    CHECK(gold() == 0 && pay_turns == 20 && take_max == 10);
}

/* The remaster off: the original's 40 MP for 200, at 10 a frame, and the
 * magic hall teaches nothing but Fire. */
static void the_original(void) {
    play(false);
    set_gold(3200);
    ram[MP] = 20;
    talk(MAGICIAN, "A");
    SAID("msg 15?", "paid 200", "+40 MP", "msg 16", "end");
    CHECK(gold() == 3000 && ram[MP] == 60 && !fax_lesson_learned() && pay_turns == 20 && take_max == 10);
    talk(MAGICIAN, "B");
    SAID("msg 15?", "B");
    CHECK(gold() == 3000);
    talk(FIRE_MAGICIAN, "A");
    SAID(HALL_SALE);
    CHECK(!fax_lesson_studied() && !lesson.studied && seen.fire_given == 0 && pay_turns == 300);
    talk(MAGICIAN, "A");
    SAID("msg 15?", "msg 03", "end");
}

/* Seamless's isolated simulation of another room steers nothing and teaches
 * nothing. */
static void isolated_runs_change_nothing(void) {
    play(true);
    set_gold(3200);
    isolated = true;
    talk(MAGICIAN, "A");
    SAID("msg 15?", "paid 200", "+40 MP", "msg 16", "end");
    talk(FIRE_MAGICIAN, "A");
    SAID(HALL_SALE);
    CHECK(!fax_lesson_learned() && !fax_lesson_studied() && seen.told == 0 && seen.fire_given == 0);
}

static void lose_the_overrides(void) {
    if (said_n == 2) {
        overrides_lost();
        table_full = true;
    }
}
/* No room for his lines' overrides: he sells his MP as he always did, for
 * the rest of the session; the magic hall still counts. */
static void no_room_for_his_lines(void) {
    play(true);
    overrides_lost();
    table_full = true;
    set_gold(3200);
    ram[MP] = 0;
    talk(MAGICIAN, "A");
    SAID("msg 15?", "paid 200", "+40 MP", "msg 16", "end");
    CHECK(failed && !fax_lesson_learned() && gold() == 3000 && pay_turns == 20);
    table_full = false;
    talk(MAGICIAN, "A");
    SAID("msg 15?", "paid 200", "+40 MP", "msg 16", "end");
    set_gold(3000);
    talk(FIRE_MAGICIAN, "A");
    SAID(HALL_SALE);
    CHECK(fax_lesson_studied());
    /* The overrides gone while his price shows, with no room for them
     * again: A still reaches his pay step (the hook moves the script off the
     * SAY step before it is read), and paid, at his count, the hero has
     * learned; the stock script goes on from there. */
    play(true);
    study();
    set_gold(30000);
    ram[MP] = 0;
    on_line = lose_the_overrides;
    talk(MAGICIAN, "AA");
    SAID(TEACHES_LINE, PRICE_LINE, "paid 30000", "+40 MP", "msg 16", "end");
    CHECK(failed && fax_lesson_learned() && gold() == 0 && ram[MP] == 40 && pay_turns == 200);
}

/* Save files: studied and learned are the file's lesson.studied and
 * lesson.learned; a file from before lesson.studied has studied if Fire is
 * among its spells. */
static void in_the_file(void) {
    play(true);
    file_in_play = true;
    save_listener(FAX_SAVE_NEW_GAME);
    save_listener(FAX_SAVE_WRITING);
    CHECK(field("lesson.learned") == NULL && is("lesson.studied", "no"));
    study();
    save_listener(FAX_SAVE_WRITING);
    CHECK(field("lesson.learned") == NULL && is("lesson.studied", "yes"));
    set_gold(30000);
    talk(MAGICIAN, "AA");
    save_listener(FAX_SAVE_WRITING);
    CHECK(is("lesson.learned", "yes") && is("lesson.studied", "yes"));
    /* Quit, then continue the file: both. */
    play(true);
    file_in_play = true;
    put("lesson.learned", "yes");
    put("lesson.studied", "yes");
    CHECK(!fax_lesson_learned() && !fax_lesson_studied());
    save_listener(FAX_SAVE_CONTINUED);
    CHECK(fax_lesson_learned() && fax_lesson_studied());
    talk(MAGICIAN, "A");
    SAID(NOTHING_LINE, "end");
    /* Studied, sold Fire, not learned: he asks the price. */
    play(true);
    file_in_play = true;
    put("lesson.studied", "yes");
    put("spells", "0 1");
    save_listener(FAX_SAVE_CONTINUED);
    CHECK(fax_lesson_studied() && !fax_lesson_learned());
    set_gold(30000);
    talk(MAGICIAN, "AB");
    SAID(TEACHES_LINE, PRICE_LINE, "B", "end");
    /* "no", even holding Fire: not studied (the field rules). */
    put("lesson.studied", "no");
    put("spells", "0 2");
    save_listener(FAX_SAVE_CONTINUED);
    CHECK(!fax_lesson_studied());
    /* A file from before the field: Fire listed, or in hand, has studied;
     * neither has not. Each is written back with the field. */
    static const struct {
        const char *spells, *selected;
        bool studied;
    } before[] = { { "0 2", "1", true },  { "2", "none", true }, { "0 1 3", "2", true }, { "0 1 3 4", "none", false },
                   { "", "none", false }, { NULL, NULL, false }, { "12", "22", false } };
    for (unsigned i = 0; i < sizeof before / sizeof before[0]; ++i) {
        put("lesson.studied", NULL);
        put("lesson.learned", NULL);
        put("spells", before[i].spells);
        put("selected_spell", before[i].selected);
        save_listener(FAX_SAVE_CONTINUED);
        CHECK(fax_lesson_studied() == before[i].studied && !fax_lesson_learned());
        save_listener(FAX_SAVE_WRITING);
        CHECK(is("lesson.studied", before[i].studied ? "yes" : "no"));
    }
    put("lesson.studied", "maybe"); /* damaged: as from before */
    put("spells", "2");
    save_listener(FAX_SAVE_CONTINUED);
    CHECK(fax_lesson_studied());
    /* A new game: neither. */
    save_listener(FAX_SAVE_NEW_GAME);
    CHECK(!fax_lesson_learned() && !fax_lesson_studied());
    save_listener(FAX_SAVE_WRITING);
    CHECK(field("lesson.learned") == NULL && is("lesson.studied", "no"));
    /* With the remaster off it neither reads nor writes. */
    reset_cb();
    field_count = 0;
    put("lesson.learned", "yes");
    put("lesson.studied", "yes");
    save_listener(FAX_SAVE_CONTINUED);
    CHECK(!fax_lesson_learned() && !lesson.learned && !lesson.studied);
    save_listener(FAX_SAVE_NEW_GAME);
    save_listener(FAX_SAVE_WRITING);
    CHECK(is("lesson.learned", "yes") && is("lesson.studied", "yes") && field_count == 2);
}

static uint8_t mid[RECORD], mid_ram[0x800]; /* a save state taken as his price shows */
static void save_mid(void) {
    if (mid[0] || said_n != 2) return;
    CHECK(save_get(mid, RECORD) == RECORD);
    memcpy(mid_ram, ram, sizeof ram);
}
/* The save-state record: studied, learned and the conversation; a line that
 * was up is served again once the whole state is in. A state from before
 * studied (version 1, or none) takes it from the spells in RAM. */
static void save_states(void) {
    play(true);
    uint8_t buf[8];
    CHECK(save_get(buf, RECORD - 1) == -1);
    CHECK(save_get(buf, sizeof buf) == RECORD && buf[0] == 2 && buf[1] == 0 && buf[2] == IDLE && buf[3] == LINE_NONE &&
          buf[4] == 0);
    /* Saved as his price shows: loaded into a game whose slot holds another line. */
    study();
    set_gold(30000);
    on_line = save_mid;
    talk(MAGICIAN, "AB");
    on_line = NULL;
    CHECK(mid[1] == 0 && mid[2] == ASKING && mid[3] == LINE_PRICE && mid[4] == 1 && save_validate(mid, RECORD));
    play(true); /* a launch: the overrides gone, Guru travel's back */
    study();
    set_gold(30000);
    talk(MAGICIAN, "AA"); /* learned, his last line in the slot */
    memcpy(ram, mid_ram, sizeof ram);
    CHECK(save_set(mid, RECORD) == 1 && !fax_lesson_learned() && fax_lesson_studied());
    fax_lesson_loaded();
    char text[64];
    slot_text(text);
    CHECK(!strcmp(text, "It will cost/30000 Golds."));
    /* The box has his price; A on it now jumps to the SAY step. */
    talk_from(SAY, 0, "");
    SAID("paid 30000", TAUGHT_LINE, "end");
    CHECK(fax_lesson_learned() && gold() == 0 && pay_turns == 200 && take_min == 150);
    /* Saved learned: loaded, learned and studied. */
    CHECK(save_get(buf, sizeof buf) == RECORD && buf[1] == 1 && buf[4] == 1);
    play(true);
    CHECK(save_set(buf, RECORD) == 1 && fax_lesson_learned() && fax_lesson_studied());
    fax_lesson_loaded();
    CHECK(fax_lesson_studied()); /* the record's, not the spells' (none) */
    /* What he teaches up: served again too. */
    const uint8_t telling[RECORD] = { 2, 0, TELLING, LINE_TEACHES, 0 };
    CHECK(save_set(telling, RECORD) == 1);
    fax_lesson_loaded();
    slot_text(text);
    CHECK(!strcmp(text, "I can teach you/to recover magic/faster."));
    talk_from(SAY, 2, ""); /* A on it: its END, steered */
    SAID(NOT_READY_LINE, "end");
    /* Version 1 (before studied), and no record at all (before the
     * feature): studied if Fire is held once the state is in. */
    const uint8_t v1[RECORD_V1] = { 1, 1, IDLE, LINE_NONE };
    play(true);
    add_spell(0);
    add_spell(MAGIC_FIRE);
    CHECK(save_validate(v1, RECORD_V1) && save_set(v1, RECORD_V1) == 1 && fax_lesson_learned());
    fax_lesson_loaded();
    CHECK(fax_lesson_studied());
    play(true);
    ram[SELECTED_MAGIC] = MAGIC_FIRE;
    CHECK(save_set(buf, 0) == 1 && !fax_lesson_learned());
    fax_lesson_loaded();
    CHECK(fax_lesson_studied() && lesson.phase == IDLE);
    play(true);
    add_spell(0);
    add_spell(1);
    CHECK(save_set(v1, RECORD_V1) == 1);
    fax_lesson_loaded();
    CHECK(!fax_lesson_studied() && fax_lesson_learned());
    /* A version 1 state with his old offer up: valid, and his price served. */
    const uint8_t v1_offer[RECORD_V1] = { 1, 0, ASKING, LINE_PRICE };
    CHECK(save_validate(v1_offer, RECORD_V1) && save_set(v1_offer, RECORD_V1) == 1);
    fax_lesson_loaded();
    slot_text(text);
    CHECK(!strcmp(text, "It will cost/30000 Golds.") && lesson.phase == ASKING);
    /* Records it does not accept change nothing. */
    play(true);
    const uint8_t good[RECORD] = { 2, 1, IDLE, LINE_NONE, 1 };
    CHECK(save_set(good, RECORD) == 1);
    static const struct {
        int at;
        uint8_t value;
    } bad[] = { { 0, 1 },     { 0, 3 },          { 1, 2 },      { 4, 2 },      { 2, PHASES },
                { 3, LINES }, { 3, LINE_PRICE }, { 2, ASKING }, { 2, SAYING }, { 2, TELLING } };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        uint8_t b[RECORD];
        memcpy(b, good, sizeof b);
        b[bad[i].at] = bad[i].value;
        CHECK(!save_validate(b, sizeof b) && !save_set(b, sizeof b) && fax_lesson_learned() && fax_lesson_studied());
    }
    CHECK(!save_validate(good, RECORD - 1) && !save_validate(good, RECORD + 1));
    const uint8_t v1_bad[][RECORD_V1] = { { 2, 0, IDLE, LINE_NONE },
                                          { 1, 0, TELLING, LINE_TEACHES },
                                          { 1, 0, SAYING, LINE_NOT_READY } };
    for (unsigned i = 0; i < sizeof v1_bad / sizeof v1_bad[0]; ++i) CHECK(!save_validate(v1_bad[i], RECORD_V1));
    static const uint8_t ok[][RECORD] = { { 2, 1, SAYING, LINE_NOTHING_MORE, 1 },
                                          { 2, 0, PAYING, LINE_NONE, 1 },
                                          { 2, 0, SAYING, LINE_NOT_READY, 0 },
                                          { 2, 0, TELLING, LINE_TEACHES, 1 },
                                          { 2, 0, SAYING, LINE_TAUGHT, 1 } };
    for (unsigned i = 0; i < sizeof ok / sizeof ok[0]; ++i) CHECK(save_validate(ok[i], RECORD));
}

int main(void) {
    registers();
    lines_fit();
    not_studied();
    buying();
    short_of_gold();
    nothing_more_to_teach();
    refusals();
    the_magic_hall();
    other_payments();
    the_original();
    isolated_runs_change_nothing();
    no_room_for_his_lines();
    in_the_file();
    save_states();
    printf("lesson_test: %u checks passed\n", checks);
    return 0;
}
