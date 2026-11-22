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
 *     it 3000. His plea names the amount ("I shall give you1500 Golds."),
 *     so a hook in the text box's reader ($F4DF, after LDA (Temp_Int24),Y)
 *     serves "3000" for its four digits while message $35 types.
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
 * same IScripts_ProgressivelyAddGold), the title's starting gold
 * (Player_SetInitialExpAndGold) and spending. No ROM and no read overrides:
 * every decision is made from RAM and the registers each time, so there is
 * nothing to save. The coin's hook runs in Seamless's isolated rooms too
 * (on their scratch machine) and counts only outside them. See
 * docs/GAME_INTERNALS.md, "Gold". */
#include "fax_gold.h"
#include "cyc_mod.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#ifdef CYC_WITH_SDL
#include "cyc_tcp.h"
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Bank 12 */
#define ADD_GOLD_PAY    0x8531u   /* IScriptAction_AddGold: JSR IScripts_ProgressivelyAddGold, after the two
                                     IScripts_LoadByte that set Temp_0202/3; then JMP IScripts_InvokeNextAction */
#define KING_GIFT_END   0xA359u   /* ISCRIPT_EOLIS_KING's END, past ADD_GOLD's word (09 DC 05 at $A356) */
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
#define TEMP_INT24        0x00ECu   /* the message's address, which Y indexes */
#define ISCRIPT_ADDR      0x00DBu   /* IScriptOrCHRAddr, 16 bits; IScriptOffset follows */
#define ISCRIPT_OFFSET    0x00DDu
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

static const char stock_digits[] = "1500", gift_digits[] = "3000";
_Static_assert(KING_STOCK == 1500u && KING_GIFT == 3000u, "the digits above are the amounts");

static bool enabled;
typedef struct {
    unsigned kings, coins, capped, line;   /* outside isolation: gifts doubled, coins, coins held at the
                                              cap, digits of the plea served */
    unsigned long coin_gold;               /* what those coins paid, in all */
    unsigned last_coin;                    /* the last coin's own value */
} Seen;
static Seen seen;

static uint8_t peek(uint16_t addr) { return cyc_mod_peek(addr); }
static void poke(uint16_t addr, uint8_t value) { cyc_mod_poke(addr, value); }
static uint16_t word(uint16_t addr) { return (uint16_t)(peek(addr) | peek((uint16_t)(addr + 1)) << 8); }
static uint32_t gold(void) { return (uint32_t)word(GOLD) | (uint32_t)peek(GOLD + 2) << 16; }
static void set_gold(uint32_t value) {
    poke(GOLD, (uint8_t)value);
    poke(GOLD + 1, (uint8_t)(value >> 8));
    poke(GOLD + 2, (uint8_t)(value >> 16));
}

uint32_t fax_gold_after_coin(uint32_t gold_now, uint8_t value) {
    uint32_t after = gold_now + 2u * value;
    return after > FAX_GOLD_MAX ? FAX_GOLD_MAX : after;
}

/* IScriptAction_AddGold's JSR IScripts_ProgressivelyAddGold: on the King's
 * step, pay 3000 instead of 1500. */
static int king_hook(uint16_t addr) {
    (void)addr;
    if ((uint16_t)(word(ISCRIPT_ADDR) + peek(ISCRIPT_OFFSET)) != KING_GIFT_END || word(TEMP_0202) != KING_STOCK)
        return 0;
    poke(TEMP_0202, (uint8_t)KING_GIFT);
    poke(TEMP_0202 + 1, (uint8_t)(KING_GIFT >> 8));
    if (!cyc_mod_isolated()) ++seen.kings;
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

static const char *const king_id = "faxanadu.double-gold.king";
static const char *const coin_id = "faxanadu.double-gold.coin";
static const char *const line_id = "faxanadu.double-gold.king-line";

static void deactivate(void) {
    enabled = false;
    seen = (Seen){0};
    nes_mod_set_function_hook_enabled(king_id, 0);
    nes_mod_set_function_hook_enabled(coin_id, 0);
    nes_mod_set_function_hook_enabled(line_id, 0);
}
static void activate(void) {
    enabled = true;
    nes_mod_set_function_hook_enabled(king_id, 1);
    nes_mod_set_function_hook_enabled(coin_id, 1);
    nes_mod_set_function_hook_enabled(line_id, 1);
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
    char fields[256];
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"gold\":%lu,\"kings\":%u,\"line\":%u,\"plea_digits\":%u,\"coins\":%u,"
             "\"coin_gold\":%lu,\"last_coin\":%u,\"capped\":%u",
             enabled ? "true" : "false", (unsigned long)gold(), seen.kings, seen.line, fax_gold_plea_digits(), seen.coins,
             seen.coin_gold, seen.last_coin, seen.capped);
    cyc_tcp_ok(id, fields);
}
void fax_gold_tcp_setup(void) {
    cyc_tcp_register("gold_state",
                     "Double gold: gold now, the King's gifts and plea digits served, the plea's digits typed, coins and what they paid",
                     gold_state);
}
#else
void fax_gold_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_gold) {
    if (!nes_mod_register_function_entry_plugin(king_id, ADD_GOLD_PAY, king_hook) ||
        !nes_mod_register_function_entry_plugin(coin_id, TOUCH_COIN_ADD, coin_hook) ||
        !nes_mod_register_function_entry_plugin(line_id, LOAD_MESSAGE_CHAR, line_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.double-gold", activate)) abort();
}
