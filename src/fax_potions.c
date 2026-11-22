/* Potion Sellers. USA 57dd23d1, chipx86 byte-exact disassembly.
 *
 * The seven meat shops (the Meat Salesman, portrait 8; Apolune has none) sold
 * dried meat, 30 HP and 30 MP, for 50 Golds in Eolis up to 800 in Dartmoor.
 * With the remaster each sells a Red Potion instead, at the price its town's
 * tool shop asked (fax_potions_price), and the tool shops stop selling them:
 * Quick shops drops the line from their buy lists while fax_potions_selling()
 * (fax_shops.c). They still buy one, at half their old price.
 *
 * Each meat shop's script (bank 12, 13 bytes, ISCRIPT_EOLIS_MEAT_SHOP at
 * $A4A1 to ISCRIPT_DARTMOOR_MEAT_SHOP at $A4EF) is
 * `88, 02 msg, 05 price, 13 1E, 0A 1E, 03 02, 00`: the question "Dried meat
 * to bring energy for 50 Golds." (B on it ends the script), pay (with less,
 * "This is not enough Golds." and the script ends), add 30 HP, add 30 MP,
 * "Thank you for shopping.", end. It is steered at IScripts_InvokeNextAction,
 * through Guru travel's SAY and ASK steps and message $C3's slot
 * (fax_travel.h), as the Magician's lesson steers his (fax_lesson.c):
 *   - His question (the script at base + 1): "A Red Potion to restore you
 *     for 160 Golds.", with his town's price, asked at the ASK step (A jumps
 *     to the SAY step, B goes on past ASK).
 *   - A on it (the SAY step): with the Items list full (8, its
 *     MAX_INVENTORY_SLOTS), the game's "You can't carry any more." (message
 *     6), entered at IScripts_ShowFinalMessage, which then ends the script, as
 *     a tool shop refuses a ninth item. Else his own pay step (base + 3). A
 *     hook in IScriptAction_SpendGold, after it has read the amount into
 *     Temp_0202/3 and before it checks the hero's gold, makes his meat's price
 *     the potion's, so the game takes the gold, 10 a frame, or says it is not
 *     enough and ends the script, as at every counter.
 *   - Paid (his ADD_HP step, base + 6): IScriptAction_AddInventoryItem is
 *     entered after its IScripts_LoadByte, with the Red Potion in A, and the
 *     script pointed at his thanks (base + 10): Player_AddToInventory adds it
 *     to the Items list, the game's item box shows it until A, as a gift's
 *     does, then "Thank you for shopping." and the end. No HP, no MP.
 *   - B on his question (past ASK): the SAY step's END, so the box closes, as
 *     B on the original's question closed it.
 * If his line cannot be supplied, he sells his dried meat as he always did,
 * and the tool shops their potions. The save-state record keeps the
 * conversation. Hooks do nothing inside Seamless's isolated simulation of
 * other rooms. See docs/GAME_INTERNALS.md, "Potion Sellers". */
#include "fax_potions.h"
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

#ifndef FAX_POTIONS_LOG
#define FAX_POTIONS_LOG(...) fprintf(stderr, __VA_ARGS__)
#endif

/* Bank 12 */
#define ISCRIPTS_INVOKE_NEXT_ACTION 0x826Eu  /* JSR IScripts_LoadByte: the action at IScriptOrCHRAddr + IScriptOffset */
#define SPEND_GOLD_CHECK    0x8367u  /* IScriptAction_SpendGold, after the two IScripts_LoadByte and STA Temp_0202/3:
                                        LDA Temp_0202, STA Temp_Int24, LDA Temp_0203, STA Temp_Int24_M,
                                        JSR IScripts_CheckEnoughMoney, BCC IScripts_ShowNotEnoughMoney */
#define SHOW_FINAL_MESSAGE  0x837Eu  /* IScripts_ShowFinalMessage: JSR Messages_Load (A, the message), its loop,
                                        then JMP IScriptAction_EndScript */
#define ADD_ITEM_TAKEN      0x83A2u  /* IScriptAction_AddInventoryItem ($839F) after its JSR IScripts_LoadByte:
                                        PHA, PHA, JSR Player_AddToInventory, the item's box until A,
                                        JMP IScripts_InvokeNextAction */
#define SAY                 FAX_TRAVEL_SAY   /* $AE00: 03 C3 00, show the slot, then END */
#define ASK                 FAX_TRAVEL_ASK   /* $AE03: 16 C3 00 AE, ask the slot: A jumps to SAY, B goes on */
/* Bank 13 */
#define SLOT                FAX_TRAVEL_SLOT  /* $B45B: message $C3, the line slot */
/* RAM */
#define ISCRIPT_ADDR        0x00DBu  /* IScriptOrCHRAddr, 16 bits; IScriptOffset follows */
#define ISCRIPT_OFFSET      0x00DDu
#define TEMP_0202           0x0202u  /* the amount being paid, 16 bits, as IScriptAction_SpendGold read it */
#define GOLD                0x0392u  /* 24 bits, little-endian */
#define NUMBER_OF_ITEMS     0x03C6u  /* the Items list's entries (ItemInventory, $03AD) */
/* Values */
enum { MEAT_SALESMAN = 0x88 };   /* ISCRIPT_ENTITY_MEAT_SALESMAN: his portrait */
enum { RED_POTION = FAX_POTIONS_RED_POTION };
enum { ITEM_SLOTS = 8 };         /* MAX_INVENTORY_SLOTS ($847D), the Items' */
enum { CANT_CARRY = 0x06, THANK_YOU = 0x02, RESTORE = 0x1E };   /* "You can't carry any more.", the thanks; 30 */
/* IScript actions */
enum { END = 0x00, SHOW_QUESTION = 0x02, SHOW_MESSAGE = 0x03, SPEND_GOLD = 0x05, ADD_MP = 0x0A, ADD_HP = 0x13,
       ASK_JUMP_IF_ACCEPTED = 0x16 };
enum { SLOT_LEN = FAX_TRAVEL_SLOT_LEN, SLOT_ID = FAX_TRAVEL_SLOT_ID };
/* A meat shop's script, by offset from its entity byte. */
enum { ASKS = 1, PAYS = 3, GIVES = 6, THANKS = 10, SCRIPT_LEN = 13 };

/* The meat shops: the script, its question's message and price, and the
 * potion's price, which the town's tool shop asked (its list from $A42E,
 * docs/GAME_INTERNALS.md, "Shops"). Every price is a multiple of 10, which
 * IScripts_ProgressivelySubtractGold takes exactly, and below $8000. */
typedef struct { uint16_t script; uint8_t message; uint16_t meat, potion; const char *town; } Shop;
static const Shop shops[FAX_POTIONS_TOWNS] = {
    { 0xA4A1, 0x07,  50,  160, "Eolis" },     /* ISCRIPT_EOLIS_MEAT_SHOP, MESSAGEID_DRIED_MEAT_50G */
    { 0xA4AE, 0x08, 150,  400, "Forepaw" },   /* ISCRIPT_FOREPAW_MEAT_SHOP, _150G */
    { 0xA4BB, 0x09, 180,  600, "Mascon" },    /* ISCRIPT_MASCON_MEAT_SHOP, _180G */
    { 0xA4C8, 0x0A, 250,  800, "Victim" },    /* ISCRIPT_VICTIM_MEAT_SHOP, _250G */
    { 0xA4D5, 0x0B, 350, 1000, "Conflate" },  /* ISCRIPT_CONFLATE_MEAT_SHOP, _350G */
    { 0xA4E2, 0x0C, 500, 1200, "Daybreak" },  /* ISCRIPT_DAYBREAK_MEAT_SHOP, _500G */
    { 0xA4EF, 0x0D, 800, 2000, "Dartmoor" },  /* ISCRIPT_DARTMOOR_MEAT_SHOP, _800G */
};
enum { NO_SHOP = 0xFF };

/* His line, in his own box: 16 columns, 4 lines, '\n' breaks a line; the
 * game adds its flashing "?". It follows the original's "Dried meat to /
 * bring energy / for 50 Golds.". */
#define LINE_FORMAT "A Red Potion to\nrestore you for\n%u Golds."

/* The conversation: his question up (ASKING, A or B next), or his pay step
 * running (PAYING), in the shop `shop`. */
enum { IDLE, ASKING, PAYING, PHASES };
typedef struct { uint8_t phase, shop; } Talk;

/* Where a step leaves the interpreter: as it is, at another step, at his
 * question (the ASK step, with his line), at the game's refusal, or giving
 * the potion and then at `script` + `offset`. */
typedef enum { STAY, GO, ASK_HIM, CANT, GIVE } Move;
typedef struct { Move move; uint16_t script; uint8_t offset; } Steer;

static bool enabled, failed;
static Talk talk = { IDLE, NO_SHOP };
typedef struct { unsigned offers, declined, full, short_of_gold, sold; } Seen;
static Seen seen;                                                     /* for TCP */

uint16_t fax_potions_price(unsigned town) { return town < FAX_POTIONS_TOWNS ? shops[town].potion : 0; }
bool fax_potions_selling(void) { return enabled && !failed; }

/* ---- the rules, on plain values ---- */

static int shop_at(uint16_t base) {
    for (int i = 0; i < FAX_POTIONS_TOWNS; ++i)
        if (shops[i].script == base) return i;
    return -1;
}
static Steer idle(Talk *t, Move move, uint16_t script, uint8_t offset) {
    t->phase = IDLE;
    t->shop = NO_SHOP;
    return (Steer){ move, script, offset };
}
/* The next action is at `at`, in the script at `base`; the hero has `items`
 * in his Items list. */
static Steer steer(Talk *t, uint16_t base, uint16_t at, unsigned items) {
    int i = shop_at(base);
    if (i >= 0 && at == base + ASKS) {                              /* he begins: his question */
        t->phase = ASKING;
        t->shop = (uint8_t)i;
        return (Steer){ ASK_HIM, ASK, 0 };
    }
    if (t->phase == ASKING && at == SAY) {                           /* A: the room first, as a tool shop checks */
        if (items >= ITEM_SLOTS) return idle(t, CANT, 0, 0);
        t->phase = PAYING;                                           /* then his own pay step */
        return (Steer){ GO, shops[t->shop].script, PAYS };
    }
    if (t->phase == ASKING && at == ASK + 4) return idle(t, GO, SAY, 2);   /* B: the SAY step's END */
    if (t->phase == PAYING && t->shop < FAX_POTIONS_TOWNS && base == shops[t->shop].script && at == base + GIVES)
        return idle(t, GIVE, base, THANKS);                          /* paid: the potion, then his thanks */
    return (Steer){ STAY, 0, 0 };
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
static bool line_bytes(unsigned shop, uint8_t out[SLOT_LEN]) {
    char text[64];
    if (shop >= FAX_POTIONS_TOWNS) return false;
    snprintf(text, sizeof text, LINE_FORMAT, (unsigned)shops[shop].potion);
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

/* The shop's script as the stock ROM has it: what the steps above stand on. */
static bool stock_script(unsigned shop) {
    const Shop *s = &shops[shop];
    static const uint8_t tail[] = { ADD_HP, RESTORE, ADD_MP, RESTORE, SHOW_MESSAGE, THANK_YOU, END };
    if (q(s->script) != MEAT_SALESMAN || q((uint16_t)(s->script + ASKS)) != SHOW_QUESTION ||
        q((uint16_t)(s->script + ASKS + 1)) != s->message || q((uint16_t)(s->script + PAYS)) != SPEND_GOLD ||
        word((uint16_t)(s->script + PAYS + 1)) != s->meat) return false;
    for (unsigned i = 0; i < sizeof tail; ++i)
        if (q((uint16_t)(s->script + GIVES + i)) != tail[i]) return false;
    return true;
}

/* His line into message $C3's slot, and Guru travel's SAY and ASK steps, as
 * Guru travel serves them: the same bytes whoever serves them. */
static bool serve(unsigned shop) {
    static const uint8_t steps[] = { SHOW_MESSAGE, SLOT_ID, END, ASK_JUMP_IF_ACCEPTED, SLOT_ID, SAY & 0xFF, SAY >> 8 };
    uint8_t bytes[SLOT_LEN];
    if (!line_bytes(shop, bytes)) return false;
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
/* Handle the instruction by entering `to` with `a` in A: its address - 1
 * pushed, as a JSR would, for the handled return to pull, so S ends where it
 * was (docs/GAME_INTERNALS.md, "The engine"). */
static int enter(uint16_t to, uint8_t a) {
    CycModRegs r;
    cyc_mod_regs(&r);
    uint16_t v = (uint16_t)(to - 1);
    cyc_mod_poke((uint16_t)(0x100u | r.s), (uint8_t)(v >> 8));
    r.s = (uint8_t)(r.s - 1);
    cyc_mod_poke((uint16_t)(0x100u | r.s), (uint8_t)v);
    r.s = (uint8_t)(r.s - 1);
    r.a = a;
    cyc_mod_set_regs(&r);
    return 1;
}
static void fail(const char *why) {
    FAX_POTIONS_LOG("[potions] %s; the meat shops sell dried meat and the tool shops potions, as they always did\n",
                    why);
    failed = true;
    talk = (Talk){ IDLE, NO_SHOP };
}

/* IScripts_InvokeNextAction: steer a meat shop's conversation. A script
 * beginning (IScripts_Begin leaves IScriptOffset 1; every step this goes to
 * is entered at another) ends any conversation of his that B or a lack of
 * gold ended. */
static int next_action_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated() || failed) return 0;
    if (q(ISCRIPT_OFFSET) == 1) talk = (Talk){ IDLE, NO_SHOP };
    uint16_t base = word(ISCRIPT_ADDR), at = script_at();
    int i = shop_at(base);
    if (i >= 0 && at == base + ASKS && !stock_script((unsigned)i)) {   /* not this ROM's meat shop: as it is */
        fail("unexpected meat shop script");
        return 0;
    }
    Steer s = steer(&talk, base, at, q(NUMBER_OF_ITEMS));
    switch (s.move) {
    case ASK_HIM:
        if (!serve(talk.shop)) {                                    /* the stock question goes on */
            fail("cannot supply the Potion Seller's line");
            return 0;
        }
        ++seen.offers;
        go(s.script, s.offset);
        return 0;
    case GO:
        if (s.script == SAY) ++seen.declined;
        go(s.script, s.offset);
        return 0;
    case CANT:
        ++seen.full;
        return enter(SHOW_FINAL_MESSAGE, CANT_CARRY);
    case GIVE:
        ++seen.sold;
        go((uint16_t)(s.script + s.offset), 0);
        return enter(ADD_ITEM_TAKEN, RED_POTION);
    default:
        return 0;
    }
}

/* IScriptAction_SpendGold, with his meat's price read: the potion's instead.
 * IScripts_CheckEnoughMoney and IScripts_ProgressivelySubtractGold then use
 * it, from Temp_0202/3 as the next instructions copy it. */
static int price_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated() || talk.phase != PAYING || talk.shop >= FAX_POTIONS_TOWNS) return 0;
    const Shop *s = &shops[talk.shop];
    if (script_at() != s->script + GIVES || word(TEMP_0202) != s->meat) return 0;
    set_word(TEMP_0202, s->potion);
    if (gold() < s->potion) ++seen.short_of_gold;
    return 0;
}

/* ---- the save-state record ---- */

/* Version 1: [0] 1, [1] the conversation's phase, [2] its shop ($FF none). */
enum { RECORD_VERSION = 1, RECORD = 3 };
static int state_get(uint8_t *buf, int cap) {
    if (cap < RECORD) return -1;
    buf[0] = RECORD_VERSION;
    buf[1] = talk.phase;
    buf[2] = talk.shop;
    return RECORD;
}
static int state_validate(const uint8_t *buf, int len) {
    if (len == 0) return 1;
    if (len != RECORD || buf[0] != RECORD_VERSION || buf[1] >= PHASES) return 0;
    return buf[1] == IDLE ? buf[2] == NO_SHOP : buf[2] < FAX_POTIONS_TOWNS;
}
/* Length 0 (a state from before the feature): no conversation. */
static int state_set(const uint8_t *buf, int len) {
    if (!state_validate(buf, len)) return 0;
    talk = len ? (Talk){ buf[1], buf[2] } : (Talk){ IDLE, NO_SHOP };
    return 1;
}

/* After the whole state is in: his line's overrides, which are not machine
 * state (Guru travel's record may have put its own last line in the slot). */
void fax_potions_loaded(void) {
    if (!enabled || failed || talk.phase != ASKING || serve(talk.shop)) return;
    fail("cannot supply the Potion Seller's line");
}

/* ---- activation ---- */

static const char *const hook_ids[] = { "faxanadu.potions.next-action", "faxanadu.potions.price" };
static void hooks(bool on) {
    for (unsigned i = 0; i < sizeof hook_ids / sizeof hook_ids[0]; ++i) nes_mod_set_function_hook_enabled(hook_ids[i], on);
}
static void deactivate(void) {
    enabled = failed = false;
    talk = (Talk){ IDLE, NO_SHOP };
    seen = (Seen){ 0 };
    hooks(false);
}
static void activate(void) {
    enabled = true;
    hooks(true);
    nes_mod_set_local_only("Potion Sellers", 1);
}

#ifdef CYC_WITH_SDL
static void potions_state(int id, const char *line) {
    (void)line;
    char fields[512];
    bool in_shop = talk.shop < FAX_POTIONS_TOWNS;
    /* phase: 0 none, 1 his question up, 2 his pay step running; shop: the town (0 Eolis ... 6 Dartmoor) or -1.
     * Counts: questions asked, declined (B), refused for a full Items list, short of gold, potions sold. */
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"selling\":%s,\"failed\":%s,\"phase\":%u,\"shop\":%d,\"town\":%s%s%s,\"price\":%u,"
             "\"offers\":%u,\"declined\":%u,\"full\":%u,\"short\":%u,\"sold\":%u",
             enabled ? "true" : "false", fax_potions_selling() ? "true" : "false", failed ? "true" : "false",
             talk.phase, in_shop ? talk.shop : -1, in_shop ? "\"" : "", in_shop ? shops[talk.shop].town : "null",
             in_shop ? "\"" : "", in_shop ? shops[talk.shop].potion : 0u, seen.offers, seen.declined, seen.full,
             seen.short_of_gold, seen.sold);
    cyc_tcp_ok(id, fields);
}
void fax_potions_tcp_setup(void) {
    cyc_tcp_register("potions_state",
                     "Potion Sellers: whether the meat shops sell Red Potions, the conversation, the shop and its "
                     "price, questions, declines, full lists, short of gold, potions sold",
                     potions_state);
}
#else
void fax_potions_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_potions) {
    if (!nes_mod_register_function_entry_plugin(hook_ids[0], ISCRIPTS_INVOKE_NEXT_ACTION, next_action_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[1], SPEND_GOLD_CHECK, price_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.potion-sellers", activate) ||
        !nes_mod_register_savestate_hook("faxanadu.potion-sellers", state_get, state_set) ||
        !nes_mod_register_savestate_validator("faxanadu.potion-sellers", state_validate))
        abort();
}
