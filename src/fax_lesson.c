/* The Magician's lesson. USA 57dd23d1, chipx86 byte-exact disassembly.
 *
 * Eolis's Magician (building $05) sold 40 MP for 200 Golds. MP refill
 * (fax_mana.c) brings MP back by itself, so instead he teaches the hero,
 * once, for PRICE, to recover it faster: the refill's wait after a spend and
 * its step are halved (fax_mana.c's TAUGHT_WAIT and TAUGHT_STEP), and an
 * empty bar is full in 22 s rather than 44.
 *
 * His script (ISCRIPT_EOLIS_MARTIAL_ARTS_MAGIC_SHOP, $A35D, bank 12) is
 * `83, 02 15, 05 C8 00, 0A 28, 01 16, 00`: the question "I shall give magic
 * for 200 Golds." (B on it ends the script), pay 200 (with less, "This is not
 * enough Golds." and the script ends), add 40 MP, "Watch your remaining power
 * level.", end. It is steered at IScripts_InvokeNextAction, through Guru
 * travel's SAY and ASK steps and message $C3's slot (fax_travel.h), as the
 * Gurus' remastered lines are:
 *   - His first action, the question: his offer instead, asked at the ASK
 *     step (A jumps to the SAY step, B goes on past ASK). Once the hero has
 *     learned, "I have nothing more to teach you." at the SAY step instead,
 *     and the script ends.
 *   - A on the offer (the SAY step): back to his own pay step. A hook in
 *     IScriptAction_SpendGold, after it has read the amount into
 *     Temp_0202/3 and before it checks the hero's gold, makes his 200 PRICE,
 *     so the game takes the gold, 10 a frame, or says it is not enough and
 *     ends the script, as at every counter.
 *   - Paid (his ADD_MP step): the hero has learned, and his last line shows
 *     at the SAY step instead of the 40 MP and "Watch your remaining power
 *     level.".
 *   - B on the offer (past ASK): the SAY step's END, so the box closes, as B
 *     on his question closed it.
 * If his lines cannot be supplied, he sells his 40 MP as he always did. The
 * magic hall's Magician (Fire for 3000) is another script, untouched. Whether
 * the hero has learned is the file in play's lesson.learned field (Save
 * files): written whenever the file is, read back when it is continued, reset
 * by a new game. The save-state record keeps it and the conversation. Hooks
 * do nothing inside Seamless's isolated simulation of other rooms. See
 * docs/GAME_INTERNALS.md, "The Magician's lesson". */
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
#define SPEND_GOLD_CHECK    0x8367u  /* IScriptAction_SpendGold, after the two IScripts_LoadByte and STA Temp_0202/3:
                                        LDA Temp_0202, STA Temp_Int24, LDA Temp_0203, STA Temp_Int24_M,
                                        JSR IScripts_CheckEnoughMoney, BCC IScripts_ShowNotEnoughMoney */
#define MAGICIAN            0xA35Du  /* ISCRIPT_EOLIS_MARTIAL_ARTS_MAGIC_SHOP: 83 (the Magician's portrait), ... */
#define MAGICIAN_ASKS       0xA35Eu  /* 02 15: "I shall give magic for 200 Golds.", a question */
#define MAGICIAN_PAY_OFFSET 3u       /* $A360: 05 C8 00, pay 200 */
#define MAGICIAN_GIVES      0xA363u  /* 0A 28: ADD_MP 40; then 01 16 ("Watch your remaining power level."), 00 */
#define SAY                 FAX_TRAVEL_SAY   /* $AE00: 03 C3 00, show the slot, then END */
#define ASK                 FAX_TRAVEL_ASK   /* $AE03: 16 C3 00 AE, ask the slot: A jumps to SAY, B goes on */
/* Bank 13 */
#define SLOT                FAX_TRAVEL_SLOT  /* $B45B: message $C3, the line slot */
/* RAM */
#define ISCRIPT_ADDR        0x00DBu  /* IScriptOrCHRAddr, 16 bits; IScriptOffset follows */
#define ISCRIPT_OFFSET      0x00DDu
#define TEMP_0202           0x0202u  /* the amount being paid, 16 bits, as IScriptAction_SpendGold read it */
#define GOLD                0x0392u  /* 24 bits, little-endian */
/* IScript actions */
enum { END = 0x00, SHOW_MESSAGE = 0x03, ASK_JUMP_IF_ACCEPTED = 0x16 };
enum { SLOT_LEN = FAX_TRAVEL_SLOT_LEN, SLOT_ID = FAX_TRAVEL_SLOT_ID };

/* The price, in Golds. His stock 200 bought 40 MP, which MP refill now gives
 * back in 20 s for nothing. The lesson saves the hero half of every wait for
 * the rest of the game, so it is priced against the first weapon and shield
 * rather than his sale: more than the Long Sword (1600) and below the magic
 * hall's Fire (3000). With the King's 3000 (Double gold), the dagger (400)
 * and Deluge (400), the spell without which the lesson does nothing, it
 * leaves 200: the Elixir (320) and Apolune's Small Shield (800) wait for
 * coins. Retuning means changing this and nothing else: his offer names it.
 * A multiple of 10, which IScripts_ProgressivelySubtractGold takes exactly,
 * and at most 9999, four digits in his line. */
enum { STOCK_PRICE = 200, PRICE = 2000 };
_Static_assert(PRICE % 10 == 0 && PRICE <= 9999, "a price the game takes exactly and the line can name");

/* His lines, in his own box: 16 columns, 4 lines, '\n' breaks a line. The
 * offer is a question: the game adds its flashing "?". */
enum { LINE_NONE, LINE_OFFER, LINE_TAUGHT, LINE_NOTHING_MORE, LINES };
#define OFFER_FORMAT "Learn to recover\nmagic faster\nfor %u Golds."
static const char *const line_text[LINES] = {
    "", OFFER_FORMAT, "Your magic will\nnow return\nfaster.", "I have nothing\nmore to teach\nyou." };

/* The conversation: his offer up (ASKING, A or B next), his pay step running
 * (PAYING), or a line of his up with its END next (SAYING). */
enum { IDLE, ASKING, PAYING, SAYING, PHASES };
typedef struct { uint8_t learned, phase, line; } Lesson;

/* Where a step leaves the interpreter: the script and offset it goes to (or
 * none), and the line for the slot. */
typedef struct { bool go; uint16_t script; uint8_t offset; int line; } Steer;

static bool enabled, failed;
static Lesson lesson;
static unsigned offers, declined, short_of_gold, taught, nothing_more;   /* for TCP */

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
        return l->learned ? show(l, SAYING, SAY, LINE_NOTHING_MORE) : show(l, ASKING, ASK, LINE_OFFER);
    if (l->phase == ASKING && at == SAY)                         /* A on the offer: his own pay step */
        return jump(l, PAYING, MAGICIAN, MAGICIAN_PAY_OFFSET);
    if (l->phase == ASKING && at == ASK + 4)                     /* B on the offer: the SAY step's END */
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
    if (which == LINE_OFFER) snprintf(text, sizeof text, OFFER_FORMAT, (unsigned)PRICE);
    else snprintf(text, sizeof text, "%s", line_text[which]);
    return encode(text, out);
}

/* ---- the machine ---- */

static uint8_t q(uint16_t addr) { return cyc_mod_peek(addr); }
static uint16_t word(uint16_t addr) { return (uint16_t)(q(addr) | q((uint16_t)(addr + 1)) << 8); }
static uint32_t gold(void) { return (uint32_t)word(GOLD) | (uint32_t)q(GOLD + 2) << 16; }
static uint32_t prg(unsigned bank, uint16_t addr) { return bank * 16384u + (addr - 0x8000u); }

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
    cyc_mod_poke(ISCRIPT_ADDR, (uint8_t)script);
    cyc_mod_poke(ISCRIPT_ADDR + 1, (uint8_t)(script >> 8));
    cyc_mod_poke(ISCRIPT_OFFSET, offset);
}
static void fail(void) {
    fprintf(stderr, "[lesson] cannot supply the Magician's line; he sells his MP as he always did\n");
    failed = true;
    lesson.phase = IDLE;
    lesson.line = LINE_NONE;
}

/* IScripts_InvokeNextAction: steer his conversation. A script beginning
 * (IScripts_Begin leaves IScriptOffset 1; every step this goes to is entered
 * at another) ends any conversation of his that B or a lack of gold ended. */
static int next_action_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated() || failed) return 0;
    if (q(ISCRIPT_OFFSET) == 1) lesson.phase = IDLE, lesson.line = LINE_NONE;
    uint16_t base = word(ISCRIPT_ADDR), at = (uint16_t)(base + q(ISCRIPT_OFFSET));
    bool asking = lesson.phase == ASKING;
    Steer s = steer(&lesson, base, at);
    if (s.line != LINE_NONE && !serve(s.line)) {     /* the stock script goes on; paid, he has learned */
        fail();
        return 0;
    }
    if (s.go) go(s.script, s.offset);
    if (s.line == LINE_OFFER) ++offers;
    else if (s.line == LINE_TAUGHT) ++taught;
    else if (s.line == LINE_NOTHING_MORE) ++nothing_more;
    else if (asking && lesson.phase == IDLE) ++declined;
    return 0;
}

/* IScriptAction_SpendGold, with his 200 read: the lesson's price instead.
 * IScripts_CheckEnoughMoney and IScripts_ProgressivelySubtractGold then use
 * it, from Temp_0202/3 as the next instructions copy it. */
static int price_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated() || lesson.phase != PAYING) return 0;
    if ((uint16_t)(word(ISCRIPT_ADDR) + q(ISCRIPT_OFFSET)) != MAGICIAN_GIVES || word(TEMP_0202) != STOCK_PRICE) return 0;
    cyc_mod_poke(TEMP_0202, (uint8_t)PRICE);
    cyc_mod_poke(TEMP_0202 + 1, (uint8_t)(PRICE >> 8));
    if (gold() < PRICE) ++short_of_gold;
    return 0;
}

bool fax_lesson_learned(void) { return enabled && lesson.learned; }

/* ---- the file in play (Save files, fax_save.h) ---- */

#define FIELD "lesson.learned"   /* "yes", or no field */
static void on_save(FaxSaveEvent event) {
    if (!enabled) return;
    if (event == FAX_SAVE_WRITING) {
        fax_save_set(FIELD, lesson.learned ? "yes" : NULL);
        return;
    }
    const char *v = event == FAX_SAVE_CONTINUED ? fax_save_get(FIELD) : NULL;
    lesson = (Lesson){ (uint8_t)(v && !strcmp(v, "yes")), IDLE, LINE_NONE };
}

/* ---- the save-state record ---- */

/* Version 1: [0] 1, [1] learned, [2] the conversation's phase, [3] the line
 * of his in the slot (none unless asking or saying). */
enum { RECORD_VERSION = 1, RECORD = 4 };
static int state_get(uint8_t *buf, int cap) {
    if (cap < RECORD) return -1;
    const uint8_t rec[RECORD] = { RECORD_VERSION, lesson.learned, lesson.phase, lesson.line };
    memcpy(buf, rec, sizeof rec);
    return RECORD;
}
static int state_validate(const uint8_t *buf, int len) {
    if (len == 0) return 1;
    if (len != RECORD || buf[0] != RECORD_VERSION || buf[1] > 1 || buf[2] >= PHASES || buf[3] >= LINES) return 0;
    if (buf[2] == ASKING) return buf[3] == LINE_OFFER;
    if (buf[2] == SAYING) return buf[3] == LINE_TAUGHT || buf[3] == LINE_NOTHING_MORE;
    return buf[3] == LINE_NONE;
}
/* Length 0 (a state from before the feature): not learned. */
static int state_set(const uint8_t *buf, int len) {
    if (!state_validate(buf, len)) return 0;
    lesson = len ? (Lesson){ buf[1], buf[2], buf[3] } : (Lesson){ 0, IDLE, LINE_NONE };
    return 1;
}

/* After the whole state is in: his line's overrides are not machine state,
 * and Guru travel's record may have put its own last line in the slot. */
void fax_lesson_loaded(void) {
    if (!enabled || failed || lesson.line == LINE_NONE || serve(lesson.line)) return;
    fail();
}

/* ---- activation ---- */

static const char *const hook_ids[] = { "faxanadu.lesson.next-action", "faxanadu.lesson.price" };
static void hooks(bool on) {
    for (unsigned i = 0; i < sizeof hook_ids / sizeof hook_ids[0]; ++i) nes_mod_set_function_hook_enabled(hook_ids[i], on);
}
static void deactivate(void) {
    enabled = failed = false;
    lesson = (Lesson){ 0, IDLE, LINE_NONE };
    offers = declined = short_of_gold = taught = nothing_more = 0;
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
    char fields[320];
    /* phase: 0 none, 1 his offer up, 2 paying, 3 a line of his up; line: 0 none, 1 the offer, 2 taught, 3 nothing
     * more. Counts: offers made, declined (B), short of gold, lessons taught, "nothing more" said. */
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"learned\":%s,\"price\":%d,\"phase\":%u,\"line\":%u,\"offers\":%u,\"declined\":%u,"
             "\"short\":%u,\"taught\":%u,\"nothing_more\":%u,\"failed\":%s",
             enabled ? "true" : "false", fax_lesson_learned() ? "true" : "false", PRICE, lesson.phase, lesson.line,
             offers, declined, short_of_gold, taught, nothing_more, failed ? "true" : "false");
    cyc_tcp_ok(id, fields);
}
void fax_lesson_tcp_setup(void) {
    cyc_tcp_register("lesson_state",
                     "Magician's lesson: learned, the price, his conversation, offers, declines, short of gold, lessons",
                     lesson_state);
}
#else
void fax_lesson_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_lesson) {
    if (!nes_mod_register_function_entry_plugin(hook_ids[0], ISCRIPTS_INVOKE_NEXT_ACTION, next_action_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[1], SPEND_GOLD_CHECK, price_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.magician-lesson", activate) ||
        !nes_mod_register_savestate_hook("faxanadu.magician-lesson", state_get, state_set) ||
        !nes_mod_register_savestate_validator("faxanadu.magician-lesson", state_validate) || !fax_save_listen(on_save))
        abort();
}
