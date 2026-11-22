/* Double gold. USA 57dd23d1, chipx86 byte-exact disassembly.
 *
 * Two things in the game pay gold for play, and both pay twice with the
 * remaster:
 *
 *   - The King. His script (ISCRIPT_EOLIS_KING, $A350 bank 12) is
 *     `80, 0D A35A, 01 35, 09 DC 05, 00`: with no gold, his plea (message
 *     $35) and ADD_GOLD 1500, the only ADD_GOLD in the game. The action
 *     (IScriptAction_AddGold, $8525) reads the amount into Temp_0202/3 and
 *     calls IScripts_ProgressivelyAddGold, which pays it 10 Golds a frame
 *     through Player_AddGold. A hook at that JSR ($8531) recognises the
 *     King's step by where the script stands (IScriptOrCHRAddr +
 *     IScriptOffset past its two bytes, $A359) and the 1500 read, and makes
 *     it 3000. A hook at the routine's JSR Player_AddGold ($8560), on the
 *     King's step, takes another 10 from what is left and makes the add 20,
 *     so his 3000 counts up in the 150 frames the original's 1500 took. His
 *     plea names the amount ("I shall give you1500 Golds."), so a hook in
 *     the text box's reader ($F4DF, after LDA (Temp_Int24),Y) serves "3000"
 *     for its four digits while message $35 types.
 *
 *     He gives it once a game. The original gives to any hero with exactly
 *     0 gold, again and again. Once he has paid, a hook on
 *     IScriptAction_IfPlayerHasGold's no-gold path ($862A, JMP
 *     IScripts_SkipAddrAndInvoke) points his CHECK_GOLD at $A35A, where a
 *     hero with gold goes: "There is nothing more I can do to help you."
 *     Whether he has given is kept in the file in play (gold.king_given) and
 *     in this feature's save-state record. A file saved before the field
 *     counts as given if the quest log has The King's Plea in quests.given;
 *     a save state from before the record, if the quest log has it given.
 *
 *   - Coins. A monster that dies leaves a coin or bread, by its entity
 *     (SPRITE_ENTITY_DROP_VALUES_INDEX, bank 14), whose value
 *     (SPRITE_DROP_VALUES, 10 to 250 Golds) goes in the drop's
 *     CurrentSprites_Values. Touching it runs Player_HandleTouchCoin
 *     ($8B9A), which adds that byte to Gold and draws it. No coin lies in
 *     the world: every one is a monster's. A hook after its sound ($8B9F,
 *     before LDA Gold) stores Gold so that the game's own add of the value
 *     lands on twice the value more, held at Player_AddGold's cap ($FFFFFF).
 *     The original's coin add has no cap and wraps to 0 past it; this
 *     cannot.
 *
 * Nothing else changes: selling (IScriptAction_ShowSellMenu, through the
 * same IScripts_ProgressivelyAddGold, 10 a frame), the title's starting gold
 * (Player_SetInitialExpAndGold) and spending. No ROM and no read overrides:
 * apart from whether the King has given, every decision is made from RAM and
 * the registers each time. The hooks run in Seamless's isolated rooms too
 * (on their scratch machine) and count only outside them. See
 * docs/GAME_INTERNALS.md, "Gold". */
#include "fax_gold.h"
#include "fax_quest_table.h"
#include "fax_quests.h"
#include "fax_save.h"
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
#define ADD_GOLD_PAY    0x8531u   /* IScriptAction_AddGold: JSR IScripts_ProgressivelyAddGold, after the two
                                     IScripts_LoadByte that set Temp_0202/3; then JMP IScripts_InvokeNextAction */
#define COUNT_PAY       0x8560u   /* IScripts_ProgressivelyAddGold: JSR Player_AddGold, after SBC #$0A from
                                     Temp_0202/3, LDA #$0A, STA Temp_Int24, LDA #$00, STA Temp_Int24_M; then
                                     JMP IScripts_ProgressivelyAddGold */
#define NO_GOLD_SKIP    0x862Au   /* IScriptAction_IfPlayerHasGold, after LDA Gold, ORA Gold_M, ORA Gold_U,
                                     BNE @_hasGold: JMP IScripts_SkipAddrAndInvoke, only with no gold */
#define KING_CHECK_GOLD 0xA352u   /* ISCRIPT_EOLIS_KING's CHECK_GOLD address word (0D at $A351, then 5A A3) */
#define KING_GIFT_END   0xA359u   /* ISCRIPT_EOLIS_KING's END, past ADD_GOLD's word (09 DC 05 at $A356) */
#define KING_NOTHING_MORE 0xA35Au /* where his CHECK_GOLD jumps with gold: 03 36 (message $36), 00 */
/* Bank 14 */
#define TOUCH_COIN_ADD  0x8B9Fu   /* Player_HandleTouchCoin, after LDA #$09, JSR Sound_PlayEffect: LDA Gold,
                                     CLC, ADC CurrentSprites_Values,X, STA Gold, then ADC #0 into Gold_M and
                                     Gold_U, and JSR UI_DrawGoldValue */
/* Fixed bank */
#define LOAD_MESSAGE_CHAR 0xF4DFu /* TextBox_ShowMessage_LoadMessage, after LDA (Temp_Int24),Y: CMP #$FF,
                                     BEQ @_endOfMessage, CMP #$FE, ... */
/* Bank 13 */
#define KING_DIGITS     0x8C81u   /* message $35's "1500" ("I shall give you1500 Golds.") */
/* RAM */
#define ISCRIPT_ADDR      0x00DBu   /* IScriptOrCHRAddr, 16 bits; IScriptOffset follows */
#define ISCRIPT_OFFSET    0x00DDu
#define TEMP_INT24        0x00ECu   /* the message's address, which Y indexes; Player_AddGold's amount, 16 bits */
#define CURRENT_ROM_BANK  0x0100u
#define TEMP_0202         0x0202u   /* the amount IScripts_ProgressivelyAddGold has left to pay, 16 bits */
#define MESSAGE_ID        0x0213u
#define MESSAGE_PROCESSED_LENGTH 0x0214u   /* the next byte is Message_StartAddr + this */
#define MESSAGE_START_ADDR 0x0218u   /* 16 bits */
#define CURRENT_SPRITES_VALUES 0x036Cu
#define GOLD              0x0392u   /* 24 bits, little-endian */
/* Values */
#define MESSAGE_KING_EXPOSITION 0x35u
#define MESSAGE_BANK      13u
#define KING_STOCK        1500u
#define KING_GIFT         (2u * KING_STOCK)   /* below $8000, a multiple of 10: the routine pays it exactly */
#define STOCK_PAY         10u                 /* what IScripts_ProgressivelyAddGold pays a frame */
#define KING_PAY          (2u * STOCK_PAY)    /* the King's count: KING_GIFT in the frames KING_STOCK takes */
#define FIELD_GIVEN       "gold.king_given"   /* the file in play's: "yes" once the King has paid, else "no" */

static const char stock_digits[] = "1500", gift_digits[] = "3000";
_Static_assert(KING_STOCK == 1500u && KING_GIFT == 3000u, "the digits above are the amounts");
_Static_assert(KING_GIFT % KING_PAY == 0 && KING_GIFT / KING_PAY == KING_STOCK / STOCK_PAY,
               "the King's count pays whole steps, in the original's frames");

static bool enabled;
static bool given;     /* the King has paid his gift in this game */
static bool infer;     /* a save state without the record was loaded: fax_gold_loaded() asks the quest log */
typedef struct {
    unsigned kings, coins, capped, line;   /* outside isolation: gifts doubled, coins, coins held at the
                                              cap, digits of the plea served */
    unsigned king_pays, nothing_more;      /* the King's count's pays of KING_PAY, his CHECK_GOLD sent on */
    unsigned long coin_gold;               /* what those coins paid, in all */
    unsigned last_coin;                    /* the last coin's own value */
} Seen;
static Seen seen;

static uint8_t peek(uint16_t addr) { return cyc_mod_peek(addr); }
static void poke(uint16_t addr, uint8_t value) { cyc_mod_poke(addr, value); }
static uint16_t word(uint16_t addr) { return (uint16_t)(peek(addr) | peek((uint16_t)(addr + 1)) << 8); }
static void set_word(uint16_t addr, uint16_t value) {
    poke(addr, (uint8_t)value);
    poke((uint16_t)(addr + 1), (uint8_t)(value >> 8));
}
static uint32_t gold(void) { return (uint32_t)word(GOLD) | (uint32_t)peek(GOLD + 2) << 16; }
static void set_gold(uint32_t value) {
    set_word(GOLD, (uint16_t)value);
    poke(GOLD + 2, (uint8_t)(value >> 16));
}
/* Where the script stands: the next byte IScripts_LoadByte reads. */
static uint16_t script_at(void) { return (uint16_t)(word(ISCRIPT_ADDR) + peek(ISCRIPT_OFFSET)); }

uint32_t fax_gold_after_coin(uint32_t gold_now, uint8_t value) {
    uint32_t after = gold_now + 2u * value;
    return after > FAX_GOLD_MAX ? FAX_GOLD_MAX : after;
}

/* IScriptAction_AddGold's JSR IScripts_ProgressivelyAddGold: on the King's
 * step, pay 3000 instead of 1500. From here he has given. */
static int king_hook(uint16_t addr) {
    (void)addr;
    if (script_at() != KING_GIFT_END || word(TEMP_0202) != KING_STOCK) return 0;
    set_word(TEMP_0202, KING_GIFT);
    if (!cyc_mod_isolated()) {
        ++seen.kings;
        given = true;
    }
    return 0;
}

/* IScripts_ProgressivelyAddGold's JSR Player_AddGold, the routine's 10 taken
 * from what is left and in Temp_Int24: on the King's step (the script still
 * at his END, where only his ADD_GOLD calls the routine), 10 more taken and
 * an add of 20. Never more than is left, so the total is what he gives. */
static int count_hook(uint16_t addr) {
    (void)addr;
    uint16_t left = word(TEMP_0202);
    if (script_at() != KING_GIFT_END || word(TEMP_INT24) != STOCK_PAY || left < KING_PAY - STOCK_PAY || left & 0x8000u)
        return 0;
    set_word(TEMP_0202, (uint16_t)(left - (KING_PAY - STOCK_PAY)));
    set_word(TEMP_INT24, KING_PAY);
    if (!cyc_mod_isolated()) ++seen.king_pays;
    return 0;
}

/* IScriptAction_IfPlayerHasGold with no gold, about to skip the address: once
 * the King has given, his CHECK_GOLD goes on as with gold. The script is set
 * two bytes before $A35A, so IScripts_SkipAddrAndInvoke's two loads ($A358,
 * $A359: ADD_GOLD's high byte and END) leave it there. */
static int once_hook(uint16_t addr) {
    (void)addr;
    if (!given || script_at() != KING_CHECK_GOLD) return 0;
    set_word(ISCRIPT_ADDR, KING_NOTHING_MORE - 2u);
    poke(ISCRIPT_OFFSET, 0);
    if (!cyc_mod_isolated()) ++seen.nothing_more;
    return 0;
}

/* Player_HandleTouchCoin, before its add: Gold set so that the add of the
 * coin's value ends at fax_gold_after_coin(). That is at most FAX_GOLD_MAX
 * and at least the value more than what is stored, so the add never
 * carries out of Gold_U. */
static int coin_hook(uint16_t addr) {
    (void)addr;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    uint8_t value = peek((uint16_t)(CURRENT_SPRITES_VALUES + regs.x));
    uint32_t before = gold(), after = fax_gold_after_coin(before, value);
    set_gold(after - value);
    if (!cyc_mod_isolated()) {
        ++seen.coins;
        seen.coin_gold += after - before;
        seen.last_coin = value;
        if (before + 2u * value > FAX_GOLD_MAX) ++seen.capped;
    }
    return 0;
}

/* The text box's reader, with the message's byte in A: the King's plea
 * says the amount he gives. */
static int line_hook(uint16_t addr) {
    (void)addr;
    if (peek(MESSAGE_ID) != MESSAGE_KING_EXPOSITION || peek(CURRENT_ROM_BANK) != MESSAGE_BANK) return 0;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    uint16_t at = (uint16_t)(word(TEMP_INT24) + regs.y);
    if (at < KING_DIGITS || at >= KING_DIGITS + sizeof stock_digits - 1) return 0;
    unsigned i = at - KING_DIGITS;
    if (regs.a != (uint8_t)stock_digits[i] || stock_digits[i] == gift_digits[i]) return 0;
    regs.a = (uint8_t)gift_digits[i];   /* CMP #$FF next sets the flags from it */
    cyc_mod_set_regs(&regs);
    if (!cyc_mod_isolated()) ++seen.line;
    return 0;
}

/* ---- the file in play ---- */

/* gold.king_given, "yes" or "no", written at every save. A file from before
 * the field (or with it damaged) has given if The King's Plea is in its
 * quests.given: the quest log gives it at his first word, which to a hero
 * with no gold is the plea his gift follows. A file without quests.given
 * (from before the quest log) has nothing to tell, and has not given: his
 * next gift is his last. */
static void on_save(FaxSaveEvent event) {
    if (!enabled) return;
    if (event == FAX_SAVE_WRITING) {
        fax_save_set(FIELD_GIVEN, given ? "yes" : "no");
        return;
    }
    infer = given = false;
    if (event != FAX_SAVE_CONTINUED) return;
    const char *v = fax_save_get(FIELD_GIVEN);
    if (v && (!strcmp(v, "yes") || !strcmp(v, "no"))) given = v[0] == 'y';
    else given = fax_quests_given_in_file(FAX_QUEST_KING);
}

/* ---- the save-state record ---- */

/* Version 1: [0] 1, [1] the King has given (0 or 1). */
enum { RECORD_VERSION = 1, RECORD = 2 };
static int state_get(uint8_t *buf, int cap) {
    if (cap < RECORD) return -1;
    buf[0] = RECORD_VERSION;
    buf[1] = given;
    return RECORD;
}
static int state_validate(const uint8_t *buf, int len) {
    return len == 0 || (len == RECORD && buf[0] == RECORD_VERSION && buf[1] <= 1);
}
/* Length 0 (a state from before the record): inferred in fax_gold_loaded(),
 * once the quest log has its own record. */
static int state_set(const uint8_t *buf, int len) {
    if (!state_validate(buf, len)) return 0;
    given = len && buf[1];
    infer = !len;
    return 1;
}
void fax_gold_loaded(void) {
    if (!infer) return;
    infer = false;
    given = enabled && fax_quests_given(FAX_QUEST_KING);
}

/* ---- activation ---- */

static const char *const king_id = "faxanadu.double-gold.king";
static const char *const count_id = "faxanadu.double-gold.king-count";
static const char *const once_id = "faxanadu.double-gold.king-once";
static const char *const coin_id = "faxanadu.double-gold.coin";
static const char *const line_id = "faxanadu.double-gold.king-line";

static void hooks(int on) {
    const char *const ids[] = { king_id, count_id, once_id, coin_id, line_id };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; ++i) nes_mod_set_function_hook_enabled(ids[i], on);
}
static void deactivate(void) {
    enabled = given = infer = false;
    seen = (Seen){0};
    hooks(0);
}
static void activate(void) {
    enabled = true;
    hooks(1);
    nes_mod_set_local_only("Double gold", 1);
}

unsigned fax_gold_plea_digits(void) {
    if (peek(MESSAGE_ID) != MESSAGE_KING_EXPOSITION) return 0;
    unsigned next = (uint16_t)(word(MESSAGE_START_ADDR) + peek(MESSAGE_PROCESSED_LENGTH));
    unsigned typed = next > KING_DIGITS ? next - KING_DIGITS : 0;
    return typed < sizeof stock_digits - 1 ? typed : (unsigned)(sizeof stock_digits - 1);
}

#ifdef CYC_WITH_SDL
static void gold_state(int id, const char *line) {
    (void)line;
    char fields[320];
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"gold\":%lu,\"given\":%s,\"kings\":%u,\"king_pays\":%u,\"nothing_more\":%u,"
             "\"line\":%u,\"plea_digits\":%u,\"coins\":%u,\"coin_gold\":%lu,\"last_coin\":%u,\"capped\":%u",
             enabled ? "true" : "false", (unsigned long)gold(), given ? "true" : "false", seen.kings, seen.king_pays,
             seen.nothing_more, seen.line, fax_gold_plea_digits(), seen.coins, seen.coin_gold, seen.last_coin,
             seen.capped);
    cyc_tcp_ok(id, fields);
}
void fax_gold_tcp_setup(void) {
    cyc_tcp_register("gold_state",
                     "Double gold: gold now, whether the King has given, his gifts, pays of 20 and nothing-more "
                     "turns, the plea's digits served and typed, coins and what they paid",
                     gold_state);
}
#else
void fax_gold_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_gold) {
    if (!nes_mod_register_function_entry_plugin(king_id, ADD_GOLD_PAY, king_hook) ||
        !nes_mod_register_function_entry_plugin(count_id, COUNT_PAY, count_hook) ||
        !nes_mod_register_function_entry_plugin(once_id, NO_GOLD_SKIP, once_hook) ||
        !nes_mod_register_function_entry_plugin(coin_id, TOUCH_COIN_ADD, coin_hook) ||
        !nes_mod_register_function_entry_plugin(line_id, LOAD_MESSAGE_CHAR, line_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.double-gold", activate) ||
        !nes_mod_register_savestate_hook("faxanadu.double-gold", state_get, state_set) ||
        !nes_mod_register_savestate_validator("faxanadu.double-gold", state_validate) ||
        !fax_save_listen(on_save)) abort();
}
