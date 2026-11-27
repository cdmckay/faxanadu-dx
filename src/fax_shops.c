/* Shops. USA 57dd23d1, chipx86 byte-exact disassembly.
 *
 * All 17 shops (the 10 tool shops and the 7 key shops) run one script layout
 * (bank 12):
 *
 *   base     8x          the Tools or Key Salesman's portrait
 *   base+1   03 msg      "I sell tools." / "I sell keys." (A)
 *   base+3   0F BUY      IScriptAction_ShowBuySellMenu: Buy jumps to BUY
 *   base+6   11 ITEMS    else IScriptAction_ShowSellMenu ("Which one would like to sell?")
 *   base+9   03 02, 00   "Thank you for shopping.", the end
 *   BUY      08 ITEMS    IScriptAction_OpenShop ("What would you like?", then the list)
 *   BUY+3    03 02, 00
 *
 * This steers that script and its two list actions, and lets the game's own
 * code do everything else: the greeting, the Buy/Sell menu and each list's
 * line, draw the lists (Shop_Draw), move the cursor, check what the hero
 * already has, his room and his gold, add and remove the item, count the gold
 * up or down, and say "You can't carry any more." or "This is not enough
 * Golds.". The ROM is never changed and no read override is used. Its hook at
 * IScripts_InvokeNextAction shares the address with Guru travel's and Save
 * files'; it acts only on a script with the shops' layout and its two list
 * steps, so the three may run in either order (mods/allocations.toml).
 *
 * The conversation is the original's, step for step: Up on the shopkeeper,
 * his portrait and greeting, the Buy/Sell menu ("Come here to buy. Come here
 * to sell."), then the list with its own line ("What would you like?" or
 * "Which one would like to sell?"). The one change: after a purchase or a
 * sale the hero stays in that list until he is done.
 *
 * - Buying: after a purchase OpenShop closes the list and invokes BUY+3, the
 *   thank-you. Its close is skipped and BUY+3 goes back to BUY, so the list is
 *   drawn again where it stood, with the cursor where it was (Shop_Draw puts it
 *   at the top; it is put back before the first menu update). "What would you
 *   like?" types only the first time the buy list opens in a visit: at
 *   IScriptAction_OpenShop's entry it is skipped after that, returning into
 *   the action right after its message loop.
 * - Selling: ShowSellMenu's own scan offers only Items the shop itself lists.
 *   After it, the list is replaced with every item carried, priced by
 *   fax_shops_sell_price, 8 to a page; Up from the top or Down from the bottom
 *   turns the page. "Which one would like to sell?" types the first time, and
 *   after a sale (its close skipped too) SELL+3 goes back to SELL, past it.
 * - B leaves, at the JMP IScriptAction_EndScript that ends each list action
 *   (OpenShop's at $83FF, ShowSellMenu's at $8701: B in the list, or on its
 *   line). If nothing was bought or sold this visit, the script ends there,
 *   as the original's B did. Otherwise the shopkeeper first says "Thank you
 *   for shopping." (the list's own thank-you step, BUY+3 or SELL+3, which the
 *   original said after each purchase), in the dialogue box, which opens
 *   over the list as after a purchase (Maybe_Shop_DrawTextBox). So a line a
 *   feature adds at the script's end (Block's, fax_block.c, after the first
 *   shield) still shows in the dialogue box. Select does nothing, as in the
 *   original.
 * - A shop's message (ShowFinalMessage: "You can't carry any more.", "This is
 *   not enough Golds.", or the empty sell list's "You have nothing to buy.")
 *   goes back to the list instead of ending the conversation, to buying if
 *   there is nothing left to sell.
 * - The list's close is skipped whenever a shop's list gives way to the
 *   dialogue box (Maybe_Shop_DrawTextBox's JSR TextBox_Close, $8477): the box
 *   opens over it. Seamless (fax_world.c) shows a script's boxes, the
 *   portrait's among them, only while TextBox_Closed is 0, so a close between
 *   two boxes blanked the portrait for the frames it took. The rows of the
 *   list outside the dialogue box stay in the nametable, which Seamless never
 *   shows, until IScriptAction_EndScript clears both boxes' whole area.
 * - An item the hero already has (fax_shops_owned: a weapon, armor, shield
 *   or spell equipped or in its list, of which he can only use one; the Wing
 *   Boots, the Hour Glass, the Elixir, the keys and the Mattock, which he has
 *   for good as the features that keep them are on) keeps its row in the buy
 *   list, with OWNED where its price was; the hint says "Already owned" while
 *   the cursor is on it, and A on it is ignored, before the game's own checks:
 *   no gold, and not the game's "You can't carry any more." for gear he has.
 *   Sold, it is for sale again. A consumable, and a different piece (a weaker
 *   weapon than his), are never owned.
 * - An item not for sale (fax_shops_not_for_sale: one no shop buys, and one
 *   the hero has for good, so a key's giver can't be farmed for gold; gear he
 *   owns still sells, a second copy bought before too) is listed
 *   without a price, the hint says "Not for sale" while the cursor is on it,
 *   and A on it is ignored.
 * - A Red Potion sells for 80 at every shop, half the lowest price any shop
 *   asks for one (Eolis's 160), where the original paid half the shop's own
 *   price, up to 1000 at Dartmoor's 2000. With Guru travel, potions bought in
 *   Eolis and sold in Dartmoor were a loop of gold for menu work (#176).
 * - The Wing Boots, the Hour Glass and the Elixir are found, not bought (Found
 *   items, fax_found.h), so while it is on a tool shop's buy list loses their
 *   lines: after Shop_Populate has copied the list, before Shop_Draw draws it,
 *   the entries after each move up (fax_shops_without): Eolis's Elixir,
 *   Forepaw's and Conflate's Wing Boots, the shop after Mascon's Hour Glass and
 *   Elixir.
 *
 * A step is entered "by return": a hook writes the address it wants on the
 * stack where the routine's RTS will pull it, or pushes a chain of them, and
 * handles the instruction, so the scheduler returns there (docs/GAME_INTERNALS.md,
 * "The engine"). Every jump keeps the stack where the game's own JMP would.
 *
 * The hint line ("Not for sale" and "Already owned", while the cursor is on
 * such an item) sits on the list's last row, and the sell list's page ("1 of
 * 2") on its first, inside its frame, in the game's font (fax_text.c), while
 * the list is the box up and its frame is on screen. See
 * docs/GAME_INTERNALS.md, "Shops". */
#include "fax_shops.h"
#include "fax_found.h"
#include "fax_keys.h"
#include "fax_recharge.h"
#include "fax_text.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#ifdef CYC_WITH_SDL
#include "cyc_tcp.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef FAX_SHOPS_LOG
#define FAX_SHOPS_LOG(...) fprintf(stderr, __VA_ARGS__)
#endif

/* RAM */
#define JOY1_CHANGED_BUTTON_MASK 0x0019u
#define ISCRIPT_ADDR        0x00DBu   /* IScriptOrCHRAddr; the next byte is at it + IScriptOffset */
#define ISCRIPT_OFFSET      0x00DDu
#define TEXTBOX_X           0x0208u   /* TextBox_X, _Y, _Width, _Height: the box last opened or closed */
#define TEXTBOX_CLOSED      0x020Cu   /* 0 while a box is open */
#define MENU_CURSOR_POS     0x021Eu
#define ARG_STRINGS_COUNT   0x021Fu   /* the entries in the list */
#define DATA_ARRAY          0x0220u   /* the list's items, 8 at most: costs follow at $0228 and $0230 */
#define SHOP_ITEM_COSTS_L   0x0228u
#define SHOP_ITEM_COSTS_U   0x0230u
#define ITEM_INVENTORY      0x03ADu   /* the Items list, NumberOfItems entries */
/* then armor, shields, spells, items: each kind's equipped entry,
 * in no list; $FF for none */
#define SELECTED_WEAPON     0x03BDu
#define SELECTED_ITEM       0x03C1u   /* the Items' selection, in no list; $FF for none */
#define NUMBER_OF_WEAPONS   0x03C2u   /* then armor, shields, spells, items */
#define NUMBER_OF_ITEMS     0x03C6u
#define SPECIAL_ITEMS       0x042Cu   /* the rings, the Elixir ($08), ...: bits, in no list */
#define ISCRIPT_PORTRAIT_ID 0x03C7u   /* $FF once IScriptAction_EndScript has run */
/* Bank 12 */
#define INVOKE_NEXT_ACTION  0x826Eu   /* IScripts_InvokeNextAction: JSR IScripts_LoadByte, TAX, dispatch */
#define FINAL_MESSAGE_END   0x838Eu   /* IScripts_ShowFinalMessage's @_endScript: JMP IScriptAction_EndScript */
#define OPEN_SHOP           0x83D8u   /* IScriptAction_OpenShop: LDA #$14 ("What would you like?"), its loop */
#define OPEN_SHOP_LIST      0x83EAu   /* ... after the loop: JSR IScripts_LoadByte (the items), Populate, Draw */
#define OPEN_SHOP_DRAW      0x83F7u   /* ... after JSR Shop_Populate: JSR Shop_Draw */
#define BUY_WAIT            0x83FAu   /* ... JSR IScripts_WaitForMenuInput, BCC to BUY_PICK, JMP EndScript */
#define BUY_END             0x83FFu   /* ... @_endScript: JMP IScriptAction_EndScript, on B in the list or the line */
#define BUY_PICK            0x8402u   /* ... A: JSR Shop_GetPlayerHasSelectedItem, the room, the gold, the item */
/* ... after Player_AddToInventory and the gold: JSR Maybe_Shop_DrawTextBox,
 * JMP IScripts_InvokeNextAction */
#define BOUGHT              0x8463u
#define MAYBE_SHOP_DRAW_TEXTBOX 0x8471u  /* two frames, then closes the list and opens the dialogue box again */
#define MAYBE_SHOP_CLOSE    0x8477u   /* ... JSR TextBox_Close */
#define MAYBE_SHOP_OPEN     0x847Au   /* ... JMP TextBox_OpenForNPC */
/* IScripts_WaitForMenuInput, after the frame: JSR Menu_UpdateAndDraw,
 * LDA Joy1_ChangedButtonMask */
#define MENU_UPDATE         0x84F0u
/* IScriptAction_ShowSellMenu after its scan: LDA Arg_StringsCount, BNE,
 * LDA #$12, JMP IScripts_ShowFinalMessage */
#define SELL_LIST           0x86BCu
#define SELL_DRAW           0x86D8u   /* ... after "Which one would like to sell?": JSR Shop_Draw */
#define SELL_WAIT           0x86DBu   /* ... JSR IScripts_WaitForMenuInput */
#define SELL_PICK           0x86E0u   /* ... A: LDX Menu_CursorPos, the price, the item, JSR Player_RemoveItem */
/* ... after the gold: JSR TextBox_Close, JSR TextBox_OpenForNPC, JMP
 * IScripts_InvokeNextAction */
#define SOLD                0x86F8u
#define SELL_END            0x8701u   /* ... @_endScript: JMP IScriptAction_EndScript, on B in the list or the line */
#define ISCRIPT_ADDRS_L     0x9F6Bu   /* every script's address, low bytes, then high bytes */
#define ISCRIPT_ADDRS_U     0xA003u
/* Fixed bank */
#define SOUND_PLAY_EFFECT   0xD0E4u   /* A = the effect; keeps X and Y */

enum { ISCRIPTS = ISCRIPT_ADDRS_U - ISCRIPT_ADDRS_L };   /* 152 */
/* IScript actions */
enum { SHOW_MESSAGE = 0x03, OPEN_SHOP_ACTION = 0x08, SHOW_BUY_SELL_MENU = 0x0F, SHOW_SELL_MENU = 0x11 };
enum { BUTTON_UP = 0x08, BUTTON_DOWN = 0x04 };
enum { SOUND_CURSOR = 0x0B };                     /* Menu_UpdateAndDraw's */
enum { CATEGORIES = 5, ITEMS = 4, PAGE = 8, MOST = 4 + 4 + 4 + 4 + 8 };
enum { BIT_ELIXIR = 0x08 };                       /* SpecialItems' */
enum { BUY, SELL, NONE };
enum { STAYING, LEAVING, THANKING };
/* Shop_Draw's list, in tiles: its box and its rows (an entry is two rows). */
enum { LIST_X = 10, LIST_Y = 8, LIST_W = 20, LIST_H = 20, HINT_COL = LIST_X + 2, HINT_ROW = LIST_Y + LIST_H - 2,
       HINT_COLS = 16, PAGE_ROW = LIST_Y + 1 };
/* TextBox_Open's frame corner tiles */
enum { CORNER_TOP_LEFT = 1, CORNER_TOP_RIGHT = 3, CORNER_BOTTOM_LEFT = 5, CORNER_BOTTOM_RIGHT = 6 };

/* The inventory lists (Weapons, Armor, Shields, Magic, Items) and how many
 * each holds (MAX_INVENTORY_SLOTS). Their entries are IDs within the
 * category; the category is the top 3 bits of a full item ID. */
static const uint16_t list_at[CATEGORIES] = { 0x039D, 0x03A1, 0x03A5, 0x03A9, 0x03AD };
static const uint8_t list_max[CATEGORIES] = { 4, 4, 4, 4, 8 };
/* The sell list's order: the Items, as the original lists them, then the rest. */
static const uint8_t sell_order[CATEGORIES] = { ITEMS, 0, 1, 2, 3 };

typedef struct { uint8_t id; uint16_t price; } Offer;
typedef struct { uint16_t base, buy, sell, items; } Script;
typedef struct {
    uint8_t active;
    uint8_t mode;        /* the list the conversation is on: BUY or SELL, NONE before either */
    uint8_t shown;       /* the list drawn last: BUY, SELL or NONE */
    uint8_t page;        /* the sell list's page */
    uint8_t cursor[2];   /* each list's cursor, to put back */
    uint8_t restore;     /* put it back at the next list's first menu update */
    uint8_t selling;     /* entries in the sell list, all pages */
    uint8_t lines;       /* bits (1 << BUY, 1 << SELL): the list's line has been said this visit */
    uint8_t traded;      /* something was bought or sold this visit */
    uint8_t leaving;     /* B: on the way out, LEAVING, then THANKING once "Thank you for shopping." begins */
    Script script;
} Session;

static bool enabled, failed, priced;
static Session shop;
static uint16_t lowest[256];   /* by item: the lowest price any shop asks, 0 if none sells it */
static unsigned opened, bought, sold, refused, ignored, turned, messages, dropped, thanked, boxes_kept;
static unsigned waits;         /* frames a list has waited for a button: it reads the pad */

static uint8_t q(uint16_t addr) { return cyc_mod_peek(addr); }
static void p(uint16_t addr, uint8_t v) { cyc_mod_poke(addr, v); }
static uint16_t word(uint16_t addr) { return (uint16_t)(q(addr) | q((uint16_t)(addr + 1)) << 8); }

/* ---- prices ---- */

bool fax_shops_never_buys(uint8_t id) {
    switch (id) {
    case 0x03:   /* WEAPON_DRAGON_SLAYER: "Defeat the Evil one with Dragon Slayer." ($9C); King Grieve drops it */
    case 0x23:   /* ARMOR_BATTLE_SUIT and */
    case 0x43:   /* SHIELD_BATTLE_HELMET: worn only with the Dragon Slayer, for the last fight ($8B) */
    case 0x80: case 0x81: case 0x82: case 0x83:   /* the rings of Elf, Ruby, Dworf, the Demon's: doors, the rock */
    case 0x8A:   /* SPECIAL_MAGICAL_ROD */
    case 0x92:   /* SPECIAL_ELIXIR: the Spring of Trunk asks for it (with the remaster, its charge) */
    case 0x93:   /* SPECIAL_PENDANT */
    case 0x94:   /* SPECIAL_BLACK_ONYX: the man by the gate to Branch gives the A key for it */
        return true;
    }
    return false;
}

uint16_t fax_shops_sell_price(uint8_t id, uint16_t here, uint16_t lowest_price) {
    enum { FLAT = 100 };   /* for what no shop sells, as the 40th Anniversary Edition pays */
    if (fax_shops_never_buys(id)) return 0;
    /* The Red Potion: half the lowest price, whatever this shop asks (#176). */
    if (id == FAX_SHOPS_RED_POTION && lowest_price) here = 0;
    /* IScriptAction_ShowSellMenu halves the shop's price (LSR, ROR). The gold
     * counts by 10s (IScripts_ProgressivelyAddGold), and every price in the
     * game is a multiple of 20, so this never rounds. */
    uint16_t price = here ? here / 2 : lowest_price ? lowest_price / 2 : FLAT;
    return (uint16_t)(price - price % 10);
}

/* ---- what the hero owns ---- */

/* An item the hero has for good: kept after use by a feature that is on, or
 * the Elixir, charged or spent. */
static bool kept_for_good(uint8_t id, const FaxShopsHero *h) {
    bool kept;
    switch (id) {
    case 0x92:                                   /* SPECIAL_ELIXIR: a bit, one at most; spent, still his */
        return (h->special & BIT_ELIXIR) || h->elixir_spent;
    case 0x8D: case 0x8F:                        /* the Hour Glass, the Wing Boots: kept, they run on MP */
        kept = h->recharging;
        break;
    case 0x84: case 0x85: case 0x86: case 0x87: case 0x88:   /* the A, K, Q, J and Jo keys */
    case 0x89:                                   /* the Mattock */
        kept = h->reusable;
        break;
    default:
        return false;
    }
    if (!kept) return false;                     /* used up: a second is worth buying */
    uint8_t item = id & 0x1F;                    /* the selected item is in no list (Player_Equip) */
    if (h->selected == item) return true;
    for (unsigned i = 0; i < h->count && i < sizeof h->items; ++i)
        if (h->items[i] == item) return true;
    return false;
}

bool fax_shops_owned(uint8_t id, const FaxShopsHero *h) {
    unsigned c = id >> 5;
    if (c >= ITEMS) return kept_for_good(id, h);
    /* A weapon, armor, shield or spell: he uses one at a time, so a second
     * copy does nothing. Equipped (in no list, Player_Equip), or in the list. */
    uint8_t entry = id & 0x1F;
    if (h->gear_selected[c] == entry) return true;
    for (unsigned i = 0; i < h->gear_count[c] && i < FAX_SHOPS_GEAR_SLOTS; ++i)
        if (h->gear[c][i] == entry) return true;
    return false;
}

static FaxShopsHero hero_now(void) {
    FaxShopsHero h = { .selected = q(SELECTED_ITEM), .count = q(NUMBER_OF_ITEMS), .special = q(SPECIAL_ITEMS),
                       .elixir_spent = fax_recharge_elixir_spent(), .recharging = fax_recharge_on(),
                       .reusable = fax_reusable_tools_on() };
    if (h.count > sizeof h.items) h.count = sizeof h.items;
    for (unsigned i = 0; i < h.count; ++i) h.items[i] = q((uint16_t)(ITEM_INVENTORY + i));
    for (unsigned c = 0; c < ITEMS; ++c) {       /* weapons, armor, shields, spells */
        h.gear_selected[c] = q((uint16_t)(SELECTED_WEAPON + c));
        h.gear_count[c] = q((uint16_t)(NUMBER_OF_WEAPONS + c));
        if (h.gear_count[c] > FAX_SHOPS_GEAR_SLOTS) h.gear_count[c] = FAX_SHOPS_GEAR_SLOTS;
        for (unsigned i = 0; i < h.gear_count[c]; ++i) h.gear[c][i] = q((uint16_t)(list_at[c] + i));
    }
    return h;
}
static bool owned(uint8_t id) {
    FaxShopsHero h = hero_now();
    return fax_shops_owned(id, &h);
}
/* The list on screen is the buy list, and its entry `i` is owned. */
static bool owned_entry(unsigned i) {
    return shop.shown == BUY && i < q(ARG_STRINGS_COUNT) && i < PAGE && owned(q((uint16_t)(DATA_ARRAY + i)));
}

bool fax_shops_not_for_sale(uint8_t id, const FaxShopsHero *h) {
    return fax_shops_never_buys(id) || (id >> 5 == ITEMS && kept_for_good(id, h));
}
static bool unsold(uint8_t id) {
    FaxShopsHero h = hero_now();
    return fax_shops_not_for_sale(id, &h);
}
/* The list on screen is the sell list, and its entry `i` is not for sale. */
static bool unsold_entry(unsigned i) {
    return shop.shown == SELL && i < q(ARG_STRINGS_COUNT) && i < PAGE && unsold(q((uint16_t)(DATA_ARRAY + i)));
}

/* The layout every shop's script has, from its first byte. */
static bool shop_script(uint16_t base, Script *s) {
    if (base < 0x8000 || base > 0xBFF0) return false;
    if (q(base) < 0x80 || q((uint16_t)(base + 1)) != SHOW_MESSAGE || q((uint16_t)(base + 3)) != SHOW_BUY_SELL_MENU ||
        q((uint16_t)(base + 6)) != SHOW_SELL_MENU || q((uint16_t)(base + 9)) != SHOW_MESSAGE) return false;
    uint16_t buy = word((uint16_t)(base + 4)), items = word((uint16_t)(base + 7));
    if (buy < 0x8000 || buy > 0xBFF0 || items < 0x8000 || items > 0xBFF0) return false;
    if (q(buy) != OPEN_SHOP_ACTION || word((uint16_t)(buy + 1)) != items || q((uint16_t)(buy + 3)) != SHOW_MESSAGE)
        return false;
    *s = (Script){ base, buy, (uint16_t)(base + 6), items };
    return true;
}
/* A shop's list: an item and its price, to a $FF. */
static int shop_list(uint16_t at, Offer out[PAGE]) {
    int n = 0;
    while (n < PAGE && q(at) != 0xFF) {
        out[n++] = (Offer){ q(at), word((uint16_t)(at + 1)) };
        at = (uint16_t)(at + 3);
    }
    return n;
}
/* The lowest price each item has in any shop: every script in ISCRIPT_ADDRS
 * with a shop's layout. False if there are none (not this ROM). */
static bool price_everything(void) {
    memset(lowest, 0, sizeof lowest);
    int shops = 0;
    for (int i = 0; i < ISCRIPTS; ++i) {
        Script s;
        if (!shop_script((uint16_t)(q((uint16_t)(ISCRIPT_ADDRS_L + i)) | q((uint16_t)(ISCRIPT_ADDRS_U + i)) << 8), &s))
            continue;
        Offer list[PAGE];
        int n = shop_list(s.items, list);
        for (int k = 0; k < n; ++k)
            if (list[k].price && (!lowest[list[k].id] || list[k].price < lowest[list[k].id]))
                lowest[list[k].id] = list[k].price;
        ++shops;
    }
    return shops > 0;
}
static uint16_t price_here(uint8_t id) {
    Offer list[PAGE];
    int n = shop_list(shop.script.items, list);
    uint16_t here = 0;
    for (int k = 0; k < n; ++k)
        if (list[k].id == id) here = list[k].price;
    return fax_shops_sell_price(id, here, lowest[id]);
}

/* Everything carried, as the sell list shows it: each inventory list from
 * its last entry, as ShowSellMenu reads the Items. What is equipped or
 * selected is in no list, so it is not offered, as in the original. What is
 * not for sale is listed at 0. */
static int sell_list(Offer out[MOST]) {
    FaxShopsHero h = hero_now();
    int n = 0;
    for (int k = 0; k < CATEGORIES; ++k) {
        unsigned c = sell_order[k], count = q((uint16_t)(NUMBER_OF_WEAPONS + c));
        if (count > list_max[c]) count = list_max[c];
        for (int i = (int)count - 1; i >= 0; --i) {
            uint8_t id = (uint8_t)((q((uint16_t)(list_at[c] + i)) & 0x1F) | c << 5);
            out[n++] = (Offer){ id, fax_shops_not_for_sale(id, &h) ? 0 : price_here(id) };
        }
    }
    return n;
}
static int pages(void) { return shop.selling ? (shop.selling + PAGE - 1) / PAGE : 1; }
static int on_page(int page) {
    int left = shop.selling - page * PAGE;
    return left < 0 ? 0 : left > PAGE ? PAGE : left;
}

/* ---- steering ---- */

static uint16_t script_at(void) { return (uint16_t)(word(ISCRIPT_ADDR) + q(ISCRIPT_OFFSET)); }
static void point_at(uint16_t step) {
    p(ISCRIPT_ADDR, (uint8_t)step);
    p(ISCRIPT_ADDR + 1, (uint8_t)(step >> 8));
    p(ISCRIPT_OFFSET, 0);
}
static uint16_t step_of(int mode) { return mode == SELL ? shop.script.sell : shop.script.buy; }
/* Go to a list: its step next, with its cursor put back. */
static void to_list(int mode) {
    shop.mode = (uint8_t)mode;
    shop.restore = 1;
    point_at(step_of(mode));
}

/* The handled instruction "returns" through these: `to` first, then each
 * `then` in turn, as RTSes would. With `ret` (a hook inside a subroutine),
 * the subroutine's own return address is replaced by the last of them, so the
 * chain ends a level up, where its caller was. */
static int go(bool ret, uint16_t to, uint16_t then, uint16_t last, int a) {
    CycModRegs r;
    cyc_mod_regs(&r);
    uint8_t s = r.s;
    uint16_t chain[3] = { to, then, last };
    int n = last ? 3 : then ? 2 : 1;
    int i = n - 1;
    if (ret) {                                  /* the routine's RTS goes to the last */
        uint16_t v = (uint16_t)(chain[i--] - 1);
        p((uint16_t)(0x100u | (uint8_t)(s + 1)), (uint8_t)v);
        p((uint16_t)(0x100u | (uint8_t)(s + 2)), (uint8_t)(v >> 8));
    }
    for (; i >= 0; --i) {                       /* pushed as a JSR would: high byte first */
        uint16_t v = (uint16_t)(chain[i] - 1);
        p((uint16_t)(0x100u | s), (uint8_t)(v >> 8)); s = (uint8_t)(s - 1);
        p((uint16_t)(0x100u | s), (uint8_t)v);        s = (uint8_t)(s - 1);
    }
    r.s = s;
    if (a >= 0) r.a = (uint8_t)a;
    cyc_mod_set_regs(&r);
    return 1;
}

static bool here(void) { return enabled && !failed && !cyc_mod_isolated(); }
static bool in_step(int mode, int after) { return shop.active && script_at() == step_of(mode) + after; }

/* IScripts_InvokeNextAction: a shop's script begins as the original's, and
 * after a purchase or a sale goes back to its list. */
static int next_action_hook(uint16_t addr) {
    (void)addr;
    if (!here()) return 0;
    uint16_t at = script_at();
    if (q(ISCRIPT_OFFSET) == 1) {               /* a script begins (IScripts_Begin): the greeting */
        Script s;
        shop.active = 0;
        shop.shown = NONE;
        if (!shop_script(word(ISCRIPT_ADDR), &s)) return 0;
        if (!priced && !(priced = price_everything())) {
            FAX_SHOPS_LOG("[shops] cannot read the shops' lists; shops are as they were\n");
            failed = true;
            return 0;
        }
        shop = (Session){ .active = 1, .mode = NONE, .shown = NONE, .script = s };
        ++opened;
        return 0;
    }
    if (!shop.active) return 0;
    bool thanks = at == shop.script.buy + 3 || at == shop.script.sell + 3;
    if (at == shop.script.base + 3) return 0;   /* the Buy/Sell menu */
    if (shop.leaving) {                         /* the thank-you, then the end */
        if (thanks) shop.leaving = THANKING;
        else shop.active = 0;
        return 0;
    }
    if (at == shop.script.buy + 3) {
        shop.cursor[BUY] = q(MENU_CURSOR_POS); ++bought; shop.traded = 1; to_list(BUY);
    } else if (at == shop.script.sell + 3) {
        shop.cursor[SELL] = q(MENU_CURSOR_POS); ++sold; shop.traded = 1; to_list(SELL);
    } else if (at == shop.script.buy || at == shop.script.sell) {
        shop.mode = at == shop.script.buy ? BUY : SELL;   /* the menu's choice */
    } else {
        shop.active = 0;                         /* not ours any more */
    }
    return 0;
}

/* IScriptAction_OpenShop: "What would you like?" the first time in a visit,
 * as the original says it; back to the list after that, without it. */
static int open_hook(uint16_t addr) {
    (void)addr;
    if (!here() || !in_step(BUY, 1)) return 0;
    if (!(shop.lines & 1u << BUY)) {
        shop.lines |= 1u << BUY;
        return 0;
    }
    return go(false, OPEN_SHOP_LIST, 0, 0, -1);
}

/* OpenShop's A, before the game's checks: an item the hero owns stays, and
 * the list waits again, as for an item no shop buys in the sell list. */
static int buy_pick_hook(uint16_t addr) {
    (void)addr;
    if (!here() || !in_step(BUY, 3)) return 0;
    unsigned cursor = q(MENU_CURSOR_POS);
    if (cursor >= PAGE || !owned(q((uint16_t)(DATA_ARRAY + cursor)))) return 0;
    ++ignored;
    return go(false, BUY_WAIT, 0, 0, -1);
}

static bool dropped_item(const uint8_t drop[], unsigned drops, uint8_t id) {
    for (unsigned k = 0; k < drops; ++k) if (drop[k] == id) return true;
    return false;
}
unsigned fax_shops_without(const uint8_t drop[], unsigned drops, uint8_t items[], uint16_t prices[], unsigned n) {
    unsigned kept = 0;
    for (unsigned i = 0; i < n; ++i)
        if (!dropped_item(drop, drops, items[i])) items[kept] = items[i], prices[kept] = prices[i], ++kept;
    return kept;
}

/* OpenShop, its list copied (Shop_Populate) and not yet drawn: the list
 * without the Wing Boots, the Hour Glass and the Elixir while Found items is
 * on. A list of nothing else (no shop's) would be kept. */
static int drawing_hook(uint16_t addr) {
    (void)addr;
    if (!here() || !in_step(BUY, 3)) return 0;
    shop.shown = BUY;
    if (!fax_found_on()) return 0;
    uint8_t items[PAGE];
    uint16_t prices[PAGE];
    unsigned n = q(ARG_STRINGS_COUNT);
    if (n > PAGE) n = PAGE;
    for (unsigned i = 0; i < n; ++i) {
        items[i] = q((uint16_t)(DATA_ARRAY + i));
        prices[i] = (uint16_t)(q((uint16_t)(SHOP_ITEM_COSTS_L + i)) | q((uint16_t)(SHOP_ITEM_COSTS_U + i)) << 8);
    }
    unsigned kept = fax_shops_without(fax_found_unsold, FAX_FOUND_UNSOLD, items, prices, n);
    if (kept == n || kept == 0) return 0;
    for (unsigned i = 0; i < kept; ++i) {
        p((uint16_t)(DATA_ARRAY + i), items[i]);
        p((uint16_t)(SHOP_ITEM_COSTS_L + i), (uint8_t)prices[i]);
        p((uint16_t)(SHOP_ITEM_COSTS_U + i), (uint8_t)(prices[i] >> 8));
    }
    p(ARG_STRINGS_COUNT, (uint8_t)kept);
    dropped += n - kept;
    return 0;
}

/* After a purchase: keep the list up and go on (to BUY + 3, which comes back). */
static int bought_hook(uint16_t addr) {
    (void)addr;
    if (!here() || !in_step(BUY, 3)) return 0;
    return go(false, INVOKE_NEXT_ACTION, 0, 0, -1);
}

static bool list_open(void) {
    return !q(TEXTBOX_CLOSED) && q(TEXTBOX_X) == LIST_X && q(TEXTBOX_X + 1) == LIST_Y &&
           q(TEXTBOX_X + 2) == LIST_W && q(TEXTBOX_X + 3) == LIST_H;
}

/* IScriptAction_ShowSellMenu, after its own scan: the list is everything
 * carried, a page of it, drawn after "Which one would like to sell?" the
 * first time in a visit, and at once after that. */
static int sell_list_hook(uint16_t addr) {
    (void)addr;
    if (!here() || !in_step(SELL, 3)) return 0;
    Offer all[MOST];
    int n = sell_list(all);
    shop.selling = (uint8_t)n;
    if (shop.page >= pages()) {                 /* the last page sold out: the end of the one before */
        shop.page = (uint8_t)(pages() - 1);
        shop.cursor[SELL] = PAGE - 1;
    }
    int first = shop.page * PAGE, count = on_page(shop.page);
    for (int i = 0; i < count; ++i) {
        p((uint16_t)(DATA_ARRAY + i), all[first + i].id);
        p((uint16_t)(SHOP_ITEM_COSTS_L + i), (uint8_t)all[first + i].price);
        p((uint16_t)(SHOP_ITEM_COSTS_U + i), (uint8_t)(all[first + i].price >> 8));
    }
    p(ARG_STRINGS_COUNT, (uint8_t)count);
    if (count) {
        shop.shown = SELL;
        if (!(shop.lines & 1u << SELL)) {        /* the game goes on to the line, then draws */
            shop.lines |= 1u << SELL;
            return 0;
        }
        return go(false, SELL_DRAW, 0, 0, -1);
    }
    /* Nothing to sell: the game says so ("You have nothing to buy."), in the
     * dialogue box, so after the last sale the box opens over the list first,
     * and then comes back here. */
    if (list_open()) return go(false, MAYBE_SHOP_DRAW_TEXTBOX, SELL_LIST, 0, -1);
    shop.shown = NONE;
    return 0;
}

/* ShowSellMenu's A: an item not for sale stays, and the list waits again. */
static int sell_pick_hook(uint16_t addr) {
    (void)addr;
    if (!here() || !in_step(SELL, 3)) return 0;
    unsigned cursor = q(MENU_CURSOR_POS);
    if (cursor >= PAGE || !unsold(q((uint16_t)(DATA_ARRAY + cursor)))) return 0;
    ++refused;
    return go(false, SELL_WAIT, 0, 0, -1);
}

/* After a sale: keep the list up and go on (to SELL + 3, which comes back). */
static int sold_hook(uint16_t addr) {
    (void)addr;
    if (!here() || !in_step(SELL, 3)) return 0;
    return go(false, INVOKE_NEXT_ACTION, 0, 0, -1);
}

/* IScripts_WaitForMenuInput, each frame before the menu moves: the cursor
 * put back, and the sell list's pages. */
static int menu_hook(uint16_t addr) {
    (void)addr;
    if (!here()) return 0;
    int list = in_step(BUY, 3) ? BUY : in_step(SELL, 3) ? SELL : NONE;
    if (list == NONE) return 0;
    ++waits;
    unsigned count = q(ARG_STRINGS_COUNT), cursor = q(MENU_CURSOR_POS);
    if (shop.restore) {
        shop.restore = 0;
        cursor = count && shop.cursor[list] < count ? shop.cursor[list] : count ? count - 1 : 0;
        p(MENU_CURSOR_POS, (uint8_t)cursor);
    }
    uint8_t pressed = q(JOY1_CHANGED_BUTTON_MASK);
    if (list != SELL || pages() < 2) return 0;   /* the menu wraps, as it always did */
    int turn = pressed & BUTTON_UP ? (cursor == 0 ? -1 : 0) : pressed & BUTTON_DOWN ? (cursor + 1 >= count ? 1 : 0) : 0;
    if (!turn) return 0;
    shop.page = (uint8_t)((shop.page + pages() + turn) % pages());
    shop.cursor[SELL] = (uint8_t)(turn > 0 ? 0 : on_page(shop.page) - 1);
    to_list(SELL);
    ++turned;
    return go(true, SOUND_PLAY_EFFECT, INVOKE_NEXT_ACTION, 0, SOUND_CURSOR);
}

/* IScripts_ShowFinalMessage, once its message is read: a shop's message goes
 * back to the list, or to buying when there is nothing left to sell. */
static int final_hook(uint16_t addr) {
    (void)addr;
    if (!here() || !(in_step(BUY, 3) || in_step(SELL, 3))) return 0;
    Offer all[MOST];
    int list = in_step(BUY, 3) ? BUY : SELL;
    shop.cursor[list] = q(MENU_CURSOR_POS);     /* the message left it where it was */
    shop.selling = (uint8_t)sell_list(all);
    to_list(list == SELL && shop.selling ? SELL : BUY);
    ++messages;
    return go(false, INVOKE_NEXT_ACTION, 0, 0, -1);
}

/* The JMP IScriptAction_EndScript that ends a list action, on B in the list
 * or on its line. Nothing bought or sold: the end, as the original's B. Else
 * "Thank you for shopping." first, the list's own thank-you step (the list
 * gives way to the dialogue box first, as after the original's purchase). */
static int leave(int list) {
    if (!here() || !(in_step(list, 1) || in_step(list, 3))) return 0;
    bool up = list_open();
    if (up) shop.cursor[list] = q(MENU_CURSOR_POS);
    shop.shown = NONE;
    if (!shop.traded) return 0;
    shop.leaving = LEAVING;
    ++thanked;
    point_at((uint16_t)(step_of(list) + 3));
    if (up) return go(false, MAYBE_SHOP_DRAW_TEXTBOX, INVOKE_NEXT_ACTION, 0, -1);
    return go(false, INVOKE_NEXT_ACTION, 0, 0, -1);
}
static int buy_end_hook(uint16_t addr) { (void)addr; return leave(BUY); }
static int sell_end_hook(uint16_t addr) { (void)addr; return leave(SELL); }

/* Maybe_Shop_DrawTextBox's JSR TextBox_Close, in a shop: the dialogue box
 * opens over the list instead, so no frame between them has TextBox_Closed
 * set, which in Seamless hid the portrait and every box. */
static int keep_hook(uint16_t addr) {
    (void)addr;
    if (!here() || !shop.active) return 0;
    ++boxes_kept;
    return go(false, MAYBE_SHOP_OPEN, 0, 0, -1);
}

/* ---- the hint ---- */

/* A list of this conversation is the box up: drawn last, and the box the
 * game opened last (a dialogue box opened over it is not). */
static bool in_list(void) {
    return shop.active && shop.shown != NONE && q(ISCRIPT_PORTRAIT_ID) != 0xFF && list_open();
}
/* The line under the list, or false for none: why A does nothing on the
 * entry under the cursor. The font has no colon or slash (TILE_GLYPHS_START
 * leaves them blank), so the words say it. */
static bool hint(char out[HINT_COLS + 1]) {
    if (!in_list()) return false;
    unsigned cursor = q(MENU_CURSOR_POS);
    const char *say = unsold_entry(cursor) ? "Not for sale" : owned_entry(cursor) ? "Already owned" : NULL;
    if (!say) return false;
    snprintf(out, HINT_COLS + 1, "%s", say);
    return true;
}
/* The sell list's page, over its first entry at the right ("1 of 2"), when
 * there is more than one. */
static bool page_label(char out[HINT_COLS + 1]) {
    if (!in_list() || shop.shown != SELL || pages() < 2) return false;
    snprintf(out, HINT_COLS + 1, "%d of %d", shop.page + 1, pages());
    return true;
}

/* The background tile the last frame showed in cell (col, row): through that
 * line's scroll, in the nametable it fetched from (as fax_speaker.c reads it). */
static unsigned shown_tile(int col, int row) {
    int line = row * FAX_TEXT_CELL + FAX_TEXT_CELL / 2;
    int x = (cyc_render_line_scroll_x(line) + col * FAX_TEXT_CELL + FAX_TEXT_CELL / 2) & 511;
    int y = cyc_render_line_scroll_y(line);
    if (y < 0 || y >= 480) return 0x100;
    uint16_t table = (uint16_t)(0x2000u + (x >= 256 ? 0x400u : 0) + (y >= 240 ? 0x800u : 0));
    return cyc_render_nametable((uint16_t)(table + (y % 240) / FAX_TEXT_CELL * 32 + (x & 255) / FAX_TEXT_CELL));
}
/* The list's frame is all on screen (TextBox_Open draws it top row first,
 * TextBox_Close clears it top row first). */
static bool list_shown(void) {
    enum { RIGHT = LIST_X + LIST_W - 1, BOTTOM = LIST_Y + LIST_H - 1 };
    return shown_tile(LIST_X, LIST_Y) == CORNER_TOP_LEFT && shown_tile(RIGHT, LIST_Y) == CORNER_TOP_RIGHT &&
           shown_tile(LIST_X, BOTTOM) == CORNER_BOTTOM_LEFT && shown_tile(RIGHT, BOTTOM) == CORNER_BOTTOM_RIGHT;
}
/* Where native pixel (x, y) is in a presented picture w x h: as it is, or
 * scaled to fit and centered, as Seamless shows a building (fax_speaker.c). */
static void to_picture(int w, int h, int *x, int *y) {
    int nw = w < 256 ? w : 256, nh = 240 * nw / 256;
    if (nh > h) { nh = h; nw = 256 * nh / 240; }
    *x = (w - nw) / 2 + *x * nw / 256;
    *y = *y * nh / 240;
}

/* Shop_Draw writes a price's last digit even for 0 (UI_DrawDigitsNoLeadingZeroes
 * blanks all but the units), so an item not for sale would read "0". Its
 * price cells are painted the box's inside again: tile 0, all color 0. An
 * owned item's are too, and OWNED is written in them, in the text's color:
 * five letters where five digits go, so it ends where the prices end. */
#define OWNED_LABEL "OWNED"
static bool covered(unsigned i) { return unsold_entry(i) || owned_entry(i); }
static bool any_covered(void) {
    for (unsigned i = 0; i < q(ARG_STRINGS_COUNT) && i < PAGE; ++i)
        if (covered(i)) return true;
    return false;
}
static void cover_prices(uint32_t *px, int w, int h) {
    enum { PRICE_COL = LIST_X + 13, PRICE_COLS = 5 };   /* Shop_Draw: X + 13, 5 digits, on an entry's second row */
    _Static_assert(sizeof OWNED_LABEL - 1 == PRICE_COLS, "OWNED fills the price's cells");
    unsigned count = q(ARG_STRINGS_COUNT);
    uint32_t inside = cyc_render_color(0);
    for (unsigned i = 0; i < count && i < PAGE; ++i) {
        bool mine = owned_entry(i);
        if (!covered(i)) continue;
        int x0 = PRICE_COL * FAX_TEXT_CELL, y0 = (LIST_Y + 3 + 2 * (int)i) * FAX_TEXT_CELL;
        int x1 = x0 + PRICE_COLS * FAX_TEXT_CELL, y1 = y0 + FAX_TEXT_CELL, x = x0, y = y0;
        to_picture(w, h, &x0, &y0);
        to_picture(w, h, &x1, &y1);
        for (int yy = y0 < 0 ? 0 : y0; yy < y1 && yy < h; ++yy)
            for (int xx = x0 < 0 ? 0 : x0; xx < x1 && xx < w; ++xx) px[(size_t)yy * (size_t)w + (size_t)xx] = inside;
        if (!mine) continue;
        to_picture(w, h, &x, &y);
        fax_text_draw(px, w, h, x, y, OWNED_LABEL, 255);
    }
}

static uint32_t *canvas;
static size_t capacity;

const uint32_t *fax_shops_present(const uint32_t *picture, int *w, int *h) {
    char line[HINT_COLS + 1], page[HINT_COLS + 1];
    if (!enabled || failed || !in_list() || !list_shown()) return picture;
    bool hinted = hint(line), paged = page_label(page);
    if (!hinted && !paged && !any_covered()) return picture;
    if (!fax_text_loaded() && !fax_text_load()) {
        FAX_SHOPS_LOG("[shops] cannot read the game's font; the list's hint is off\n");
        failed = true;
        return picture;
    }
    int width = 0, height = 0;
    const uint32_t *from = picture;
    if (from) width = *w, height = *h;
    else from = cyc_render_present(&width, &height);
    if (!from || width <= 0 || height <= 0) return picture;
    size_t n = (size_t)width * (size_t)height;
    if (n > capacity) {
        uint32_t *grown = realloc(canvas, n * sizeof *grown);
        if (!grown) return picture;
        canvas = grown; capacity = n;
    }
    memcpy(canvas, from, n * sizeof *canvas);
    cover_prices(canvas, width, height);
    int x, y;
    if (hinted) {
        x = HINT_COL * FAX_TEXT_CELL, y = HINT_ROW * FAX_TEXT_CELL;
        to_picture(width, height, &x, &y);
        fax_text_draw(canvas, width, height, x, y, line, 255);
    }
    if (paged) {
        x = (HINT_COL + HINT_COLS - (int)strlen(page)) * FAX_TEXT_CELL, y = PAGE_ROW * FAX_TEXT_CELL;
        to_picture(width, height, &x, &y);
        fax_text_draw(canvas, width, height, x, y, page, 255);
    }
    *w = width; *h = height;
    return canvas;
}

/* ---- the mod ---- */

static const char *const hook_ids[] = {
    "faxanadu.shops.next-action", "faxanadu.shops.open", "faxanadu.shops.bought", "faxanadu.shops.sell-list",
    "faxanadu.shops.sell-pick", "faxanadu.shops.sold", "faxanadu.shops.menu", "faxanadu.shops.final",
    "faxanadu.shops.buy-pick", "faxanadu.shops.drawing", "faxanadu.shops.buy-end", "faxanadu.shops.sell-end",
    "faxanadu.shops.keep" };
static void hooks(int on) {
    for (unsigned i = 0; i < sizeof hook_ids / sizeof hook_ids[0]; ++i) nes_mod_set_function_hook_enabled(hook_ids[i], on);
}
static void deactivate(void) {
    enabled = failed = priced = false;
    shop = (Session){ .mode = NONE, .shown = NONE };
    opened = bought = sold = refused = ignored = turned = messages = waits = dropped = thanked = boxes_kept = 0;
    hooks(0);
}
static void activate(void) {
    enabled = true;
    hooks(1);
    nes_mod_set_local_only("Quick shops", 1);
}

/* Saved with the machine: the conversation, which a state saved in a shop
 * goes on with. Version 1 (Quick shops before #163: straight to the buy list,
 * Select between the lists) had no lines, trade or leaving: a state of it in
 * a list goes on in that list, its line said. */
enum { STATE_VERSION = 2, STATE_LEN = 20, STATE_V1_LEN = 17 };
static int state_get(uint8_t *buf, int cap) {
    if (cap < STATE_LEN) return -1;
    const uint16_t w[4] = { shop.script.base, shop.script.buy, shop.script.sell, shop.script.items };
    buf[0] = STATE_VERSION;
    buf[1] = shop.active; buf[2] = shop.mode; buf[3] = shop.shown; buf[4] = shop.page;
    buf[5] = shop.cursor[BUY]; buf[6] = shop.cursor[SELL]; buf[7] = shop.restore; buf[8] = shop.selling;
    for (int i = 0; i < 4; ++i) { buf[9 + 2 * i] = (uint8_t)w[i]; buf[10 + 2 * i] = (uint8_t)(w[i] >> 8); }
    buf[17] = shop.lines; buf[18] = shop.traded; buf[19] = shop.leaving;
    return STATE_LEN;
}
static int state_validate(const uint8_t *buf, int len) {
    if (len == 0) return 1;
    bool v1 = len == STATE_V1_LEN && buf[0] == 1, v2 = len == STATE_LEN && buf[0] == STATE_VERSION;
    return (v1 || v2) && buf[1] <= 1 && buf[2] <= NONE && buf[3] <= NONE && buf[4] < (MOST + PAGE - 1) / PAGE &&
           buf[5] < PAGE && buf[6] < PAGE && buf[7] <= 1 && buf[8] <= MOST &&
           (v1 || (buf[17] <= (1u << BUY | 1u << SELL) && buf[18] <= 1 && buf[19] <= THANKING));
}
static int state_set(const uint8_t *buf, int len) {
    if (!state_validate(buf, len)) return 0;
    shop = (Session){ .mode = NONE, .shown = NONE };
    if (!len) return 1;                          /* a state from before Quick shops: no shop under way */
    shop.active = buf[1]; shop.mode = buf[2]; shop.shown = buf[3]; shop.page = buf[4];
    shop.cursor[BUY] = buf[5]; shop.cursor[SELL] = buf[6]; shop.restore = buf[7]; shop.selling = buf[8];
    uint16_t w[4];
    for (int i = 0; i < 4; ++i) w[i] = (uint16_t)(buf[9 + 2 * i] | buf[10 + 2 * i] << 8);
    shop.script = (Script){ w[0], w[1], w[2], w[3] };
    if (len == STATE_LEN) {
        shop.lines = buf[17]; shop.traded = buf[18]; shop.leaving = buf[19];
    } else if (shop.mode <= SELL) {
        shop.lines = (uint8_t)(1u << shop.mode);
    }
    return 1;
}

#ifdef CYC_WITH_SDL
/* Where the conversation is, by the script's place (fresh once a script has
 * begun: `opened` counts them): its greeting, the Buy/Sell menu, a list (its
 * line, the list, or a shop's message), or on the way out after B, leaving
 * and then the thank-you, which loads its line the frame it begins. */
static const char *step_name(void) {
    if (!shop.active) return "none";
    if (shop.leaving) return shop.leaving == THANKING ? "thanks" : "leaving";
    uint16_t at = script_at();
    if (at == shop.script.base + 1 || at == shop.script.base + 3) return "greeting";
    if (at == shop.script.base + 4) return "menu";
    if (at == shop.script.buy + 1 || at == shop.script.buy + 3) return "buy";
    if (at == shop.script.sell + 3) return "sell";
    return "none";
}
static void shops_state(int id, const char *line) {
    (void)line;
    char fields[1152], items[64] = "", prices[96] = "", mine[64] = "", say[HINT_COLS + 1], page[HINT_COLS + 1];
    unsigned count = q(ARG_STRINGS_COUNT);
    if (count > PAGE) count = PAGE;
    for (unsigned i = 0; i < count; ++i) {
        size_t a = strlen(items), b = strlen(prices), c = strlen(mine);
        snprintf(items + a, sizeof items - a, "%s%u", i ? "," : "", q((uint16_t)(DATA_ARRAY + i)));
        snprintf(prices + b, sizeof prices - b, "%s%u", i ? "," : "",
                 q((uint16_t)(SHOP_ITEM_COSTS_L + i)) | q((uint16_t)(SHOP_ITEM_COSTS_U + i)) << 8);
        snprintf(mine + c, sizeof mine - c, "%s%s", i ? "," : "", owned_entry(i) ? "true" : "false");
    }
    bool hinted = hint(say), up = in_list() && list_shown(), paged = page_label(page);
    static const char *const modes[] = { "buy", "sell", "none" };
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"active\":%s,\"step\":\"%s\",\"mode\":\"%s\",\"shown\":\"%s\",\"page\":%u,\"pages\":%d,"
             "\"selling\":%u,\"cursor\":%u,\"list\":[%s],\"prices\":[%s],\"owned\":[%s],\"list_up\":%s,"
             "\"hint\":%s%s%s,\"page_label\":%s%s%s,\"buy_line\":%s,\"sell_line\":%s,\"traded\":%s,"
             "\"leaving\":%s,\"script\":%u,\"opened\":%u,\"bought\":%u,\"sold\":%u,\"refused\":%u,"
             "\"ignored\":%u,\"turned\":%u,\"messages\":%u,\"waits\":%u,\"dropped\":%u,\"thanked\":%u,"
             "\"kept\":%u",
             enabled ? "true" : "false", shop.active ? "true" : "false", step_name(),
             modes[shop.mode <= SELL ? shop.mode : 2],
             modes[shop.shown <= NONE ? shop.shown : 2], shop.page, pages(), shop.selling, q(MENU_CURSOR_POS),
             items, prices, mine, up ? "true" : "false", hinted && up ? "\"" : "", hinted && up ? say : "null",
             hinted && up ? "\"" : "", up && paged ? "\"" : "", up && paged ? page : "null", up && paged ? "\"" : "",
             shop.lines & 1u << BUY ? "true" : "false", shop.lines & 1u << SELL ? "true" : "false",
             shop.traded ? "true" : "false", shop.leaving ? "true" : "false", shop.script.base, opened, bought, sold,
             refused, ignored, turned, messages, waits, dropped, thanked, boxes_kept);
    cyc_tcp_ok(id, fields);
}
void fax_shops_tcp_setup(void) {
    cyc_tcp_register("shops_state", "Quick shops: the conversation, the list on screen and its hint", shops_state);
}
#else
void fax_shops_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_shops) {
    static const uint16_t at[] = { INVOKE_NEXT_ACTION, OPEN_SHOP, BOUGHT, SELL_LIST, SELL_PICK, SOLD, MENU_UPDATE,
                                   FINAL_MESSAGE_END, BUY_PICK, OPEN_SHOP_DRAW, BUY_END, SELL_END,
                                   MAYBE_SHOP_CLOSE };
    static const NESModFunctionEntryCallback cb[] = { next_action_hook, open_hook, bought_hook, sell_list_hook,
                                                      sell_pick_hook, sold_hook, menu_hook, final_hook,
                                                      buy_pick_hook, drawing_hook, buy_end_hook, sell_end_hook,
                                                      keep_hook };
    for (unsigned i = 0; i < sizeof at / sizeof at[0]; ++i)
        if (!nes_mod_register_function_entry_plugin(hook_ids[i], at[i], cb[i])) abort();
    if (!nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.shops", activate) ||
        !nes_mod_register_savestate_hook("faxanadu.shops", state_get, state_set) ||
        !nes_mod_register_savestate_validator("faxanadu.shops", state_validate)) abort();
}
