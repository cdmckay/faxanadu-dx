/* The Magician's lesson. USA 57dd23d1, chipx86 byte-exact disassembly.
 *
 * Eolis's Magician (building $05) sold 40 MP for 200 Golds. MP comes from
 * kills (fax_mana.c), and a Guru fills it, so instead he teaches the hero,
 * once, for PRICE, to recover it faster: every kill gives twice its MP
 * (fax_mana.c's TAUGHT), a Zozura 6 rather than 3 and a boss the whole bar.
 * The Wing Boots and the Hour Glass run on MP (fax_recharge.c), so it means
 * flying and freezing longer on the same fights too. His lines say what he
 * teaches as they always did. He teaches only a hero who has
 * studied at the magic hall in the Mist (ready(), below): bought Fire from
 * its Magician (building $1C), even if he has sold it since.
 *
 * His script (ISCRIPT_EOLIS_MARTIAL_ARTS_MAGIC_SHOP, $A35D, bank 12) is
 * `83, 02 15, 05 C8 00, 0A 28, 01 16, 00`: the question "I shall give magic
 * for 200 Golds." (B on it ends the script), pay 200 (with less, "This is not
 * enough Golds." and the script ends), add 40 MP, "Watch your remaining power
 * level.", end. It is steered at IScripts_InvokeNextAction, through Guru
 * travel's SAY and ASK steps and message $C3's slot (fax_travel.h), as the
 * remaster's Guru lines are:
 *   - His first action, the question: what he teaches, "I can teach you to
 *     recover magic faster.", at the SAY step (B on it ends the script). Once
 *     the hero has learned, "I have nothing more to teach you." there
 *     instead, and the script ends.
 *   - A on it (the SAY step's END): to a hero who has not studied at the
 *     magic hall, "First study at the magic hall in the Mist." at the SAY
 *     step, and the script ends; gold is never checked. To one who has, his
 *     price, "It will cost 30000 Golds.", asked at the ASK step (A jumps to
 *     the SAY step, B goes on past ASK).
 *   - A on the price (the SAY step): back to his own pay step. A hook in
 *     IScriptAction_SpendGold, after it has read the amount into
 *     Temp_0202/3 and before it checks the hero's gold, makes his 200 PRICE,
 *     so the game takes the gold or says it is not enough and ends the
 *     script, as at every counter. A hook at
 *     IScripts_ProgressivelySubtractGold's JSR Player_SubtractGold counts his
 *     price down PAY a frame rather than the game's 10, so 30000 takes 200
 *     frames (then the game's 60); every other payment counts 10.
 *   - Paid (his ADD_MP step): the hero has learned, and his last line shows
 *     at the SAY step instead of the 40 MP and "Watch your remaining power
 *     level.".
 *   - B on the price (past ASK): the SAY step's END, so the box closes, as B
 *     on his question closed it.
 * If his lines cannot be supplied, he sells his 40 MP as he always did. The
 * magic hall's Magician (Fire for 3000, ISCRIPT_AFTER_VICTIM_MAGIC_SHOP) is
 * not steered: the same hook only sees his script reach the step that gives
 * Fire, and from then the hero has studied. Whether the hero has studied and
 * learned are the file in play's lesson.studied and lesson.learned fields
 * (Save files): written whenever the file is, read back when it is
 * continued, reset by a new game; a file saved before lesson.studied counts
 * the hero as studied if Fire is among his spells. The save-state record
 * keeps both and the conversation. Hooks do nothing inside Seamless's
 * isolated simulation of other rooms. See docs/GAME_INTERNALS.md, "The
 * Magician's lesson". */
#include "fax_lesson.h"
#include "fax_save.h"
#include "fax_travel.h"
#include "cyc_mod.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#ifdef CYC_WITH_SDL
#include "cyc_tcp.h"
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Bank 12 */
#define ISCRIPTS_INVOKE_NEXT_ACTION 0x826Eu  /* JSR IScripts_LoadByte: the action at IScriptOrCHRAddr + IScriptOffset */
#define SUBTRACT_PAY        0x8347u  /* IScripts_ProgressivelySubtractGold: JSR Player_SubtractGold, after SBC #$0A
                                        from Temp_0202/3, LDA #$0A, STA Temp_Int24, LDA #$00, STA Temp_Int24_M;
                                        then JMP IScripts_ProgressivelySubtractGold */
#define SPEND_GOLD_CHECK    0x8367u  /* IScriptAction_SpendGold, after the two IScripts_LoadByte and STA Temp_0202/3:
                                        LDA Temp_0202, STA Temp_Int24, LDA Temp_0203, STA Temp_Int24_M,
                                        JSR IScripts_CheckEnoughMoney, BCC IScripts_ShowNotEnoughMoney */
#define MAGICIAN            0xA35Du  /* ISCRIPT_EOLIS_MARTIAL_ARTS_MAGIC_SHOP: 83 (the Magician's portrait), ... */
#define MAGICIAN_ASKS       0xA35Eu  /* 02 15: "I shall give magic for 200 Golds.", a question */
#define MAGICIAN_PAY_OFFSET 3u       /* $A360: 05 C8 00, pay 200 */
#define MAGICIAN_GIVES      0xA363u  /* 0A 28: ADD_MP 40; then 01 16 ("Watch your remaining power level."), 00 */
#define HALL_MAGICIAN       0xA368u  /* ISCRIPT_AFTER_VICTIM_MAGIC_SHOP: 83, 12 62 A377 (Fire held: $A377,
                                        03 18 00), 02 17, 05 B8 0B (pay 3000), 07 62, 01 79, 00 */
#define HALL_GIVES_FIRE     0xA372u  /* 07 62: ADD_ITEM Fire, his pay step done */
#define SAY                 FAX_TRAVEL_SAY   /* $AE00: 03 C3 00, show the slot, then END */
#define ASK                 FAX_TRAVEL_ASK   /* $AE03: 16 C3 00 AE, ask the slot: A jumps to SAY, B goes on */
/* Bank 13 */
#define SLOT                FAX_TRAVEL_SLOT  /* $B45B: message $C3, the line slot */
/* RAM */
#define ISCRIPT_ADDR        0x00DBu  /* IScriptOrCHRAddr, 16 bits; IScriptOffset follows */
#define ISCRIPT_OFFSET      0x00DDu
#define TEMP_INT24          0x00ECu  /* Player_SubtractGold's amount, 16 bits */
#define TEMP_0202           0x0202u  /* the amount being paid, 16 bits, as IScriptAction_SpendGold read it; then
                                        what IScripts_ProgressivelySubtractGold has left to take */
#define GOLD                0x0392u  /* 24 bits, little-endian */
#define MAGIC_INVENTORY     0x03A9u  /* the spells listed, 4 at most */
#define SELECTED_MAGIC      0x03C0u  /* the spell in hand, $FF none */
#define NUMBER_OF_MAGIC     0x03C5u
/* Values */
#define MAGIC_FIRE          2u       /* SELECTEDMAGIC_FIRE: Fire's entry in the spells (MAGIC_FIRE, $62, as an item) */
#define MAGIC_SLOTS         4u
/* IScript actions */
enum { END = 0x00, SHOW_MESSAGE = 0x03, ASK_JUMP_IF_ACCEPTED = 0x16 };
enum { SLOT_LEN = FAX_TRAVEL_SLOT_LEN, SLOT_ID = FAX_TRAVEL_SLOT_ID };

/* The price, in Golds. His stock 200 bought 40 MP, which any Guru now gives
 * for nothing. The lesson doubles every kill's MP for the rest of the game,
 * and he teaches only after the magic hall's Fire (3000), so it is priced for
 * the late game: ten times Fire, and twice
 * Daybreak's Tilte (15000), the dearest thing a shop sells. A multiple of
 * 10, which IScripts_ProgressivelySubtractGold takes exactly, and below
 * $8000: the routine keeps the amount left in Temp_0202/3 and marks its
 * closing wait with bit 15. His price line names it (it must fit his box). */
enum { STOCK_PRICE = 200, PRICE = 30000 };
_Static_assert(PRICE % 10 == 0 && PRICE < 0x8000, "a price the game takes exactly, in Temp_0202/3");

/* His count: the game takes 10 a frame, so 30000 would take 3000 frames (50 s).
 * PAY a frame takes it in 200, then the game's 60. Every other payment
 * counts 10. The count never takes more than is left, so any price is taken
 * exactly; what a price leaves below PAY goes at 10 a frame. */
enum { STOCK_PAY = 10, PAY = 150 };
_Static_assert(PAY % STOCK_PAY == 0 && PAY > STOCK_PAY, "a count in the game's steps of 10");

/* His lines, in his own box: 16 columns, 4 lines, '\n' breaks a line. The
 * price is a question: the game adds its flashing "?". The first three keep
 * their values from the record's version 1, where 1 was his offer at the old
 * price: a version 1 state taken with it up asks this price. */
enum { LINE_NONE, LINE_PRICE, LINE_TAUGHT, LINE_NOTHING_MORE, LINE_TEACHES, LINE_NOT_READY, LINES };

typedef struct { uint8_t learned, phase, line, studied; } Lesson;

/* ---- whom he teaches ---- */

/* He teaches a hero who is ready, and tells any other where to go first:
 * the requirement and its line, together, to change together. Ready: the
 * hero has studied at the magic hall in the Mist (its Magician gave him
 * Fire). He never names the spell. */
#define NOT_READY_TEXT "First study at\nthe magic hall\nin the Mist."
static bool ready(const Lesson *l) { return l->studied; }

#define PRICE_FORMAT "It will cost\n%u Golds."
static const char *const line_text[LINES] = {
    "", PRICE_FORMAT, "Your magic will\nnow return\nfaster.", "I have nothing\nmore to teach\nyou.",
    "I can teach you\nto recover magic\nfaster.", NOT_READY_TEXT };

/* The conversation: what he teaches up, its END next (TELLING), his price up
 * (ASKING, A or B next), his pay step running (PAYING), or a last line of his
 * up with its END next (SAYING). The values of the first four are those of
 * the record's version 1. */
enum { IDLE, ASKING, PAYING, SAYING, TELLING, PHASES };

/* Where a step leaves the interpreter: the script and offset it goes to (or
 * none), and the line for the slot. */
typedef struct { bool go; uint16_t script; uint8_t offset; int line; } Steer;

static bool enabled, failed;
static bool infer;   /* a save state without the studied byte was loaded: fax_lesson_loaded() asks the spells */
static Lesson lesson;
typedef struct {
    unsigned told, not_ready, offers, declined, short_of_gold, taught, nothing_more;   /* his lines and answers */
    unsigned pays;            /* turns of his count that took PAY */
    unsigned fire_given;      /* the magic hall's Magician's script reaching the step that gives Fire */
} Seen;
static Seen seen;                                                     /* for TCP */

/* ---- the rules, on plain values ---- */

/* A line of his at `step` (ASK or SAY), or a jump with none. */
static Steer show(Lesson *l, uint8_t phase, uint16_t step, int line) {
    l->phase = phase;
    l->line = (uint8_t)line;
    return (Steer){ true, step, 0, line };
}
static Steer jump(Lesson *l, uint8_t phase, uint16_t script, uint8_t offset) {
    l->phase = phase;
    l->line = LINE_NONE;
    return (Steer){ true, script, offset, LINE_NONE };
}
/* The next action is at `at`, in the script at `base`. */
static Steer steer(Lesson *l, uint16_t base, uint16_t at) {
    if (base == MAGICIAN && at == MAGICIAN_ASKS)                 /* he begins */
        return l->learned ? show(l, SAYING, SAY, LINE_NOTHING_MORE) : show(l, TELLING, SAY, LINE_TEACHES);
    if (l->phase == TELLING && at == SAY + 2)                    /* A on what he teaches: who he teaches */
        return ready(l) ? show(l, ASKING, ASK, LINE_PRICE) : show(l, SAYING, SAY, LINE_NOT_READY);
    if (l->phase == ASKING && at == SAY)                         /* A on the price: his own pay step */
        return jump(l, PAYING, MAGICIAN, MAGICIAN_PAY_OFFSET);
    if (l->phase == ASKING && at == ASK + 4)                     /* B on the price: the SAY step's END */
        return jump(l, IDLE, SAY, 2);
    if (l->phase == PAYING && base == MAGICIAN && at == MAGICIAN_GIVES) {   /* paid */
        l->learned = 1;
        return show(l, SAYING, SAY, LINE_TAUGHT);
    }
    if (l->phase == SAYING && at == SAY + 2) l->phase = IDLE, l->line = LINE_NONE;   /* read: its END runs */
    return (Steer){ false, 0, 0, LINE_NONE };
}

/* A line in the dialogue font ($FD a space, $FE a new line), $FF-terminated
 * and padded to the slot; false if it would not fit the box (16 columns, 4
 * lines) or the slot. */
static bool encode(const char *s, uint8_t out[SLOT_LEN]) {
    unsigned n = 0, col = 0, row = 1;
    for (; *s; ++s) {
        uint8_t v;
        if (*s == '\n') v = 0xFE, col = 0, ++row;
        else if (*s == ' ') v = 0xFD, ++col;
        else if (*s > 0x20 && *s < 0x7F) v = (uint8_t)*s, ++col;
        else return false;
        if (col > 16 || row > 4 || n + 1 >= SLOT_LEN) return false;
        out[n++] = v;
    }
    while (n < SLOT_LEN) out[n++] = 0xFF;
    return true;
}
static bool line_bytes(int which, uint8_t out[SLOT_LEN]) {
    char text[64];
    if (which <= LINE_NONE || which >= LINES) return false;
    if (which == LINE_PRICE) snprintf(text, sizeof text, PRICE_FORMAT, (unsigned)PRICE);
    else snprintf(text, sizeof text, "%s", line_text[which]);
    return encode(text, out);
}

/* ---- the machine ---- */

static uint8_t q(uint16_t addr) { return cyc_mod_peek(addr); }
static uint16_t word(uint16_t addr) { return (uint16_t)(q(addr) | q((uint16_t)(addr + 1)) << 8); }
static void set_word(uint16_t addr, uint16_t value) {
    cyc_mod_poke(addr, (uint8_t)value);
    cyc_mod_poke((uint16_t)(addr + 1), (uint8_t)(value >> 8));
}
static uint32_t gold(void) { return (uint32_t)word(GOLD) | (uint32_t)q(GOLD + 2) << 16; }
static uint32_t prg(unsigned bank, uint16_t addr) { return bank * 16384u + (addr - 0x8000u); }
/* Where the script stands: the next byte IScripts_LoadByte reads. */
static uint16_t script_at(void) { return (uint16_t)(word(ISCRIPT_ADDR) + q(ISCRIPT_OFFSET)); }

/* Fire in hand or listed, in RAM: the old states' test of having studied. */
static bool fire_in_ram(void) {
    unsigned n = q(NUMBER_OF_MAGIC);
    if (q(SELECTED_MAGIC) == MAGIC_FIRE) return true;
    for (unsigned i = 0; i < n && i < MAGIC_SLOTS; ++i)
        if (q((uint16_t)(MAGIC_INVENTORY + i)) == MAGIC_FIRE) return true;
    return false;
}

/* The line into message $C3's slot, and Guru travel's SAY and ASK steps, as
 * Guru travel serves them: the same bytes whoever serves them. */
static bool serve(int which) {
    static const uint8_t steps[] = { SHOW_MESSAGE, SLOT_ID, END, ASK_JUMP_IF_ACCEPTED, SLOT_ID, SAY & 0xFF, SAY >> 8 };
    uint8_t bytes[SLOT_LEN];
    if (!line_bytes(which, bytes)) return false;
    for (unsigned i = 0; i < sizeof steps; ++i)
        if (!cyc_mod_set_prg_read_override(prg(12, (uint16_t)(SAY + i)), 0xFF, steps[i])) return false;
    for (unsigned i = 0; i < SLOT_LEN; ++i)
        if (!cyc_mod_set_prg_read_override(prg(13, (uint16_t)(SLOT + i)), 0xFF, bytes[i])) return false;
    return true;
}
static void go(uint16_t script, uint8_t offset) {
    set_word(ISCRIPT_ADDR, script);
    cyc_mod_poke(ISCRIPT_OFFSET, offset);
}
static void fail(void) {
    fprintf(stderr, "[lesson] cannot supply the Magician's line; he sells his MP as he always did\n");
    failed = true;
    lesson.phase = IDLE;
    lesson.line = LINE_NONE;
}

/* IScripts_InvokeNextAction: note the magic hall's Fire, and steer his
 * conversation. A script beginning (IScripts_Begin leaves IScriptOffset 1;
 * every step this goes to is entered at another) ends any conversation of
 * his that B or a lack of gold ended. */
static int next_action_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) return 0;
    if (q(ISCRIPT_OFFSET) == 1) lesson.phase = IDLE, lesson.line = LINE_NONE;
    uint16_t base = word(ISCRIPT_ADDR), at = script_at();
    if (base == HALL_MAGICIAN && at == HALL_GIVES_FIRE) {          /* the magic hall: paid, Fire next */
        lesson.studied = 1;
        ++seen.fire_given;
    }
    if (failed) return 0;                                           /* he sells his MP */
    uint8_t was = lesson.phase;
    Steer s = steer(&lesson, base, at);
    if (s.line != LINE_NONE && !serve(s.line)) {     /* the stock script goes on; paid, he has learned */
        fail();
        return 0;
    }
    if (s.go) go(s.script, s.offset);
    switch (s.line) {
    case LINE_TEACHES: ++seen.told; break;
    case LINE_NOT_READY: ++seen.not_ready; break;
    case LINE_PRICE: ++seen.offers; break;
    case LINE_TAUGHT: ++seen.taught; break;
    case LINE_NOTHING_MORE: ++seen.nothing_more; break;
    default: if (was == ASKING && lesson.phase == IDLE) ++seen.declined;
    }
    return 0;
}

/* IScriptAction_SpendGold, with his 200 read: the lesson's price instead.
 * IScripts_CheckEnoughMoney and IScripts_ProgressivelySubtractGold then use
 * it, from Temp_0202/3 as the next instructions copy it. */
static int price_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated() || lesson.phase != PAYING) return 0;
    if (script_at() != MAGICIAN_GIVES || word(TEMP_0202) != STOCK_PRICE) return 0;
    set_word(TEMP_0202, PRICE);
    if (gold() < PRICE) ++seen.short_of_gold;
    return 0;
}

/* IScripts_ProgressivelySubtractGold's JSR Player_SubtractGold, the routine's
 * 10 taken from what is left and in Temp_Int24: while his price is paid (his
 * pay step running, the script past it, where only his SPEND_GOLD calls the
 * routine), PAY - 10 more taken and a subtract of PAY. Never more than is
 * left, so the total is his price; a shop's purchase and every other
 * payment stand elsewhere and count 10. */
static int pay_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated() || lesson.phase != PAYING || script_at() != MAGICIAN_GIVES) return 0;
    uint16_t left = word(TEMP_0202);
    if (word(TEMP_INT24) != STOCK_PAY || left < PAY - STOCK_PAY || left & 0x8000u) return 0;
    set_word(TEMP_0202, (uint16_t)(left - (PAY - STOCK_PAY)));
    set_word(TEMP_INT24, PAY);
    ++seen.pays;
    return 0;
}

bool fax_lesson_learned(void) { return enabled && lesson.learned; }
bool fax_lesson_studied(void) { return enabled && lesson.studied; }

/* ---- the file in play (Save files, fax_save.h) ---- */

#define FIELD_LEARNED "lesson.learned"   /* "yes", or no field */
#define FIELD_STUDIED "lesson.studied"   /* "yes" or "no", at every save; no field: a file from before it */

/* Fire in the file's spells, in hand or listed: a file from before
 * lesson.studied has studied if so (one that sold Fire since cannot tell). */
static bool fire_in_file(void) {
    char want[4];
    snprintf(want, sizeof want, "%u", MAGIC_FIRE);
    const char *sel = fax_save_get(fax_save_selected_key[FAX_SAVE_MAGIC]);
    if (sel && !strcmp(sel, want)) return true;
    const char *list = fax_save_get(fax_save_list_key[FAX_SAVE_MAGIC]);
    while (list && *list) {
        while (*list == ' ') ++list;
        size_t n = strcspn(list, " ");
        if (n == strlen(want) && !strncmp(list, want, n)) return true;
        list += n;
    }
    return false;
}
static void on_save(FaxSaveEvent event) {
    if (!enabled) return;
    if (event == FAX_SAVE_WRITING) {
        fax_save_set(FIELD_LEARNED, lesson.learned ? "yes" : NULL);
        fax_save_set(FIELD_STUDIED, lesson.studied ? "yes" : "no");
        return;
    }
    lesson = (Lesson){ 0, IDLE, LINE_NONE, 0 };
    infer = false;
    if (event != FAX_SAVE_CONTINUED) return;
    const char *v = fax_save_get(FIELD_LEARNED), *s = fax_save_get(FIELD_STUDIED);
    lesson.learned = (uint8_t)(v && !strcmp(v, "yes"));
    if (s && (!strcmp(s, "yes") || !strcmp(s, "no"))) lesson.studied = s[0] == 'y';
    else lesson.studied = fire_in_file();
}

/* ---- the save-state record ---- */

/* Version 2: [0] 2, [1] learned, [2] the conversation's phase, [3] the line
 * of his in the slot (none unless telling, asking or saying), [4] studied.
 * Version 1 (before studied): the first four, with phases and lines up to
 * SAYING and LINE_NOTHING_MORE; studied then comes from the spells, as an old
 * file's does. */
enum { RECORD_VERSION = 2, RECORD = 5, RECORD_V1 = 4 };
static int state_get(uint8_t *buf, int cap) {
    if (cap < RECORD) return -1;
    const uint8_t rec[RECORD] = { RECORD_VERSION, lesson.learned, lesson.phase, lesson.line, lesson.studied };
    memcpy(buf, rec, sizeof rec);
    return RECORD;
}
static int state_validate(const uint8_t *buf, int len) {
    if (len == 0) return 1;
    bool v1 = len == RECORD_V1 && buf[0] == 1, v2 = len == RECORD && buf[0] == RECORD_VERSION;
    if (!(v1 || v2) || buf[1] > 1 || (v2 && buf[4] > 1)) return 0;
    if (buf[2] >= (v1 ? TELLING : PHASES) || buf[3] >= (v1 ? LINE_TEACHES : LINES)) return 0;
    switch (buf[2]) {
    case TELLING: return buf[3] == LINE_TEACHES;
    case ASKING: return buf[3] == LINE_PRICE;
    case SAYING: return buf[3] == LINE_TAUGHT || buf[3] == LINE_NOTHING_MORE || buf[3] == LINE_NOT_READY;
    default: return buf[3] == LINE_NONE;
    }
}
/* Length 0 (a state from before the feature): not learned. Without the
 * studied byte (length 0, or version 1), studied is inferred in
 * fax_lesson_loaded(), once the state's RAM is in. */
static int state_set(const uint8_t *buf, int len) {
    if (!state_validate(buf, len)) return 0;
    lesson = len ? (Lesson){ buf[1], buf[2], buf[3], len == RECORD ? buf[4] : 0 } : (Lesson){ 0, IDLE, LINE_NONE, 0 };
    infer = len != RECORD;
    return 1;
}

/* After the whole state is in: studied for an old state, from the spells in
 * RAM; and his line's overrides, which are not machine state (Guru travel's
 * record may have put its own last line in the slot). */
void fax_lesson_loaded(void) {
    if (infer) lesson.studied = (uint8_t)(enabled && fire_in_ram());
    infer = false;
    if (!enabled || failed || lesson.line == LINE_NONE || serve(lesson.line)) return;
    fail();
}

/* ---- activation ---- */

static const char *const hook_ids[] = { "faxanadu.lesson.next-action", "faxanadu.lesson.price", "faxanadu.lesson.pay" };
static void hooks(bool on) {
    for (unsigned i = 0; i < sizeof hook_ids / sizeof hook_ids[0]; ++i) nes_mod_set_function_hook_enabled(hook_ids[i], on);
}
static void deactivate(void) {
    enabled = failed = infer = false;
    lesson = (Lesson){ 0, IDLE, LINE_NONE, 0 };
    seen = (Seen){ 0 };
    hooks(false);
}
static void activate(void) {
    enabled = true;
    hooks(true);
    nes_mod_set_local_only("Magician's lesson", 1);
}

#ifdef CYC_WITH_SDL
static void lesson_state(int id, const char *line) {
    (void)line;
    char fields[512];
    /* phase: 0 none, 1 his price up, 2 paying, 3 a last line of his up, 4 what he teaches up; line: 0 none, 1 the
     * price, 2 taught, 3 nothing more, 4 what he teaches, 5 not ready. Counts: what he teaches said, not ready said,
     * prices asked, declined (B), short of gold, lessons taught, "nothing more" said, his count's turns of PAY,
     * the magic hall's Fire given. */
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"learned\":%s,\"studied\":%s,\"price\":%d,\"pay\":%d,\"phase\":%u,\"line\":%u,"
             "\"told\":%u,\"not_ready\":%u,\"offers\":%u,\"declined\":%u,\"short\":%u,\"taught\":%u,"
             "\"nothing_more\":%u,\"pays\":%u,\"fire_given\":%u,\"failed\":%s",
             enabled ? "true" : "false", fax_lesson_learned() ? "true" : "false",
             fax_lesson_studied() ? "true" : "false", PRICE, PAY, lesson.phase, lesson.line, seen.told,
             seen.not_ready, seen.offers, seen.declined, seen.short_of_gold, seen.taught, seen.nothing_more,
             seen.pays, seen.fire_given, failed ? "true" : "false");
    cyc_tcp_ok(id, fields);
}
void fax_lesson_tcp_setup(void) {
    cyc_tcp_register("lesson_state",
                     "Magician's lesson: learned, studied at the magic hall, the price, his conversation, his lines, "
                     "declines, short of gold, lessons, his count's turns, the hall's Fire given",
                     lesson_state);
}
#else
void fax_lesson_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_lesson) {
    if (!nes_mod_register_function_entry_plugin(hook_ids[0], ISCRIPTS_INVOKE_NEXT_ACTION, next_action_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[1], SPEND_GOLD_CHECK, price_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[2], SUBTRACT_PAY, pay_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.magician-lesson", activate) ||
        !nes_mod_register_savestate_hook("faxanadu.magician-lesson", state_get, state_set) ||
        !nes_mod_register_savestate_validator("faxanadu.magician-lesson", state_validate) || !fax_save_listen(on_save))
        abort();
}
