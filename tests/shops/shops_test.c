/* Unit tests for Quick shops (fax_shops.c), without the emulator or a ROM.
 *
 * The module runs against a model of the machine: CPU RAM, the registers and
 * the stack, bank 12 holding the 17 shops' scripts and lists (written out from
 * the disassembly, at their addresses, with the script table that points at
 * them), the hook sites fired as the scheduler fires them (a handled site
 * returns as an RTS would, from the stack the hook left), and the shop
 * actions played step by step as the disassembly has them: IScripts_Begin,
 * IScripts_InvokeNextAction, IScriptAction_OpenShop, ShowBuySellMenu and
 * ShowSellMenu, IScripts_WaitForMenuInput and Menu_UpdateAndDraw, the
 * purchase's checks, Player_AddToInventory and Player_RemoveItem, the gold,
 * IScripts_ShowFinalMessage, Maybe_Shop_DrawTextBox and EndScript. A
 * conversation is played from a queue of button presses, one per frame the
 * game reads them, so the tests count presses as a player would, and check
 * what the hero ends up with, what the game said, and that the stack is
 * where it started. The hint, and an owned item's OWNED, are drawn on a model
 * picture. Reusable tools and recharging items are switches here, and so is
 * whether the Potion Sellers sell the Red Potions (fax_potions.h): off but in
 * potion_sellers(), so the other tests have the tool shops' own lists. */
#include <stdio.h>
static char logged[160];
#define FAX_SHOPS_LOG(...) snprintf(logged, sizeof logged, __VA_ARGS__)
#include "fax_text.c"
#include "fax_shops.c"

static unsigned checks;
#define CHECK(test) do{++checks;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(1);}}while(0)

/* ---- the machine ---- */
static uint8_t ram[0x800];
static uint8_t bank12[0x4000];
/* The features that keep items (fax_keys.c, fax_recharge.c), as switches. */
static bool reusable_on = true, recharging_on = true, spent;
bool fax_reusable_tools_on(void) { return reusable_on; }
bool fax_recharge_on(void) { return recharging_on; }
bool fax_recharge_elixir_spent(void) { return recharging_on && spent; }
/* The Potion Sellers (fax_potions.c), as a switch. */
static bool potions_on;
bool fax_potions_selling(void) { return potions_on; }
static uint8_t cart[16 * 16384];
static uint8_t ciram[0x800];
static uint8_t chr[0x2000];
static uint32_t native[256 * 240];
static CycModRegs cpu;
static bool isolated;

uint8_t cyc_mod_peek(uint16_t addr) {
    if (addr < 0x2000) return ram[addr & 0x7FF];
    if (addr >= 0x8000 && addr < 0xC000) return bank12[addr - 0x8000];
    return 0;
}
bool cyc_mod_poke(uint16_t addr, uint8_t value) {
    if (addr >= 0x2000) return false;
    ram[addr & 0x7FF] = value;
    return true;
}
void cyc_mod_regs(CycModRegs *out) { *out = cpu; }
void cyc_mod_set_regs(const CycModRegs *in) { cpu.a = in->a; cpu.x = in->x; cpu.y = in->y; cpu.s = in->s; cpu.p = in->p; }
bool cyc_mod_isolated(void) { return isolated; }
size_t cyc_mod_copy_cart_payload(uint8_t *out, size_t capacity) {
    if (out && capacity >= sizeof cart) memcpy(out, cart, sizeof cart);
    return sizeof cart;
}
int cyc_render_line_scroll_x(int y) { (void)y; return 0; }
int cyc_render_line_scroll_y(int y) { return y; }
uint8_t cyc_render_nametable(uint16_t a) { return ciram[(a & 0x3FF) | (a & 0x400)]; }
uint16_t cyc_render_line_bg_table(int y) { (void)y; return 0x1000; }
uint8_t cyc_render_line_mask(int y) { (void)y; return 0x1E; }   /* fax_text.c's HUD test: rendering on */
uint8_t cyc_render_chr(uint16_t a) { return chr[a & 0x1FFF]; }
/* Palette entry e as a color whose blue byte is e + 1, so a pixel tells its entry. */
uint32_t cyc_render_color(int e) { return 0xFF000000u | (uint32_t)((e & 31) + 1); }
const uint32_t *cyc_render_present(int *w, int *h) { *w = 256; *h = 240; return native; }

/* The engine's hook table: registration order, an enabled flag each. */
typedef struct { const char *id; uint16_t addr; NESModFunctionEntryCallback cb; int on; } Site;
static Site sites[16];
static int site_count;
int nes_mod_register_function_entry_plugin(const char *id, uint16_t addr, NESModFunctionEntryCallback cb) {
    if (site_count == 16) return 0;
    sites[site_count++] = (Site){ id, addr, cb, 0 };
    return 1;
}
int nes_mod_set_function_hook_enabled(const char *id, int on) {
    for (int i = 0; i < site_count; ++i)
        if (!strcmp(sites[i].id, id)) { sites[i].on = on; return 1; }
    return 0;
}
static NESModActivationCallback reset_cb, activate_cb;
int nes_mod_register_reset_callback(NESModActivationCallback cb) { reset_cb = cb; return 1; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    if (strcmp(id, "faxanadu.shops")) return 0;
    activate_cb = cb;
    return 1;
}
void nes_mod_set_local_only(const char *name, int required) { (void)name; (void)required; }
static NESModSavestateGet save_get;
static NESModSavestateSet save_set;
static NESModSavestateValidate save_validate;
int nes_mod_register_savestate_hook(const char *id, NESModSavestateGet get, NESModSavestateSet set) {
    if (strcmp(id, "faxanadu.shops")) return 0;
    save_get = get; save_set = set;
    return 1;
}
int nes_mod_register_savestate_validator(const char *id, NESModSavestateValidate validate) {
    if (strcmp(id, "faxanadu.shops")) return 0;
    save_validate = validate;
    return 1;
}

static unsigned fired;
static int fire(uint16_t addr) {
    for (int i = 0; i < site_count; ++i)
        if (sites[i].addr == addr && sites[i].on) { ++fired; if (sites[i].cb(addr)) return 1; }
    return 0;
}
static void push(uint8_t v) { ram[0x100 | cpu.s] = v; cpu.s = (uint8_t)(cpu.s - 1); }
static uint8_t pull(void) { cpu.s = (uint8_t)(cpu.s + 1); return ram[0x100 | cpu.s]; }
static void jsr(uint16_t last) { push((uint8_t)(last >> 8)); push((uint8_t)last); }   /* the JSR's last byte */
static uint16_t rts(void) { uint8_t lo = pull(); return (uint16_t)((lo | pull() << 8) + 1); }

/* ---- bank 12: the shops, from the disassembly ---- */
enum { HAND_DAGGER = 0x00, LONG_SWORD = 0x01, GIANT_BLADE = 0x02, DRAGON_SLAYER = 0x03,
       STUDDED_MAIL = 0x21, FULL_PLATE = 0x22, BATTLE_SUIT = 0x23,
       SMALL_SHIELD = 0x40, LARGE_SHIELD = 0x41, MAGIC_SHIELD = 0x42, BATTLE_HELMET = 0x43,
       DELUGE = 0x60, THUNDER = 0x61, FIRE = 0x62, DEATH = 0x63, TILTE = 0x64,
       RING_OF_ELF = 0x80, KEY_A = 0x84, KEY_K = 0x85, KEY_Q = 0x86, KEY_J = 0x87, KEY_JO = 0x88, MATTOCK = 0x89,
       MAGICAL_ROD = 0x8A, HOUR_GLASS = 0x8D, WING_BOOTS = 0x8F, RED_POTION = 0x90, POISON = 0x91,
       ELIXIR = 0x92, PENDANT = 0x93, BLACK_ONYX = 0x94 };
enum { TOOLS = 0x89, KEYS = 0x8A, I_SELL_TOOLS = 0x0F, I_SELL_KEYS = 0x0E };
typedef struct { uint8_t index; uint16_t script, items; uint8_t portrait; Offer list[5]; } Shop;
static const Shop shops[] = {
    { 13, 0xA37A, 0xA42E, TOOLS, { { HAND_DAGGER, 400 }, { RED_POTION, 160 }, { ELIXIR, 320 }, { DELUGE, 400 } } },
    { 25, 0xA38C, 0xA43B, TOOLS, { { SMALL_SHIELD, 800 }, { HAND_DAGGER, 500 }, { DELUGE, 500 }, { RED_POTION, 300 } } },
    { 29, 0xA39E, 0xA448, TOOLS, { { MAGIC_SHIELD, 7600 }, { DEATH, 12500 } } },
    { 40, 0xA3B0, 0xA44F, TOOLS, { { LONG_SWORD, 1600 }, { STUDDED_MAIL, 2500 }, { SMALL_SHIELD, 1100 },
                                   { RED_POTION, 400 }, { WING_BOOTS, 2800 } } },
    { 57, 0xA3C2, 0xA45F, TOOLS, { { LARGE_SHIELD, 3500 }, { THUNDER, 1200 }, { MATTOCK, 800 }, { RED_POTION, 600 } } },
    { 60, 0xA3D4, 0xA46C, TOOLS, { { DEATH, 9800 }, { HOUR_GLASS, 5600 }, { ELIXIR, 4300 }, { RED_POTION, 300 } } },
    { 72, 0xA3E6, 0xA479, TOOLS, { { FULL_PLATE, 5200 }, { MATTOCK, 1200 }, { RED_POTION, 800 } } },
    { 87, 0xA3F8, 0xA483, TOOLS, { { GIANT_BLADE, 8500 }, { MAGIC_SHIELD, 9800 }, { WING_BOOTS, 5500 },
                                   { RED_POTION, 1000 } } },
    { 102, 0xA40A, 0xA490, TOOLS, { { TILTE, 15000 }, { GIANT_BLADE, 11500 }, { RED_POTION, 1200 } } },
    { 117, 0xA41C, 0xA49A, TOOLS, { { GIANT_BLADE, 13000 }, { RED_POTION, 2000 } } },
    { 12, 0xA4FC, 0xA57A, KEYS, { { KEY_J, 100 } } },
    { 26, 0xA50E, 0xA57E, KEYS, { { KEY_J, 140 } } },
    { 43, 0xA520, 0xA582, KEYS, { { KEY_J, 200 }, { KEY_Q, 500 } } },
    { 59, 0xA532, 0xA589, KEYS, { { KEY_J, 500 }, { KEY_Q, 600 } } },
    { 74, 0xA544, 0xA590, KEYS, { { KEY_K, 1500 }, { KEY_Q, 1200 } } },
    { 104, 0xA556, 0xA597, KEYS, { { KEY_K, 1200 }, { KEY_Q, 1000 } } },
    { 119, 0xA568, 0xA59E, KEYS, { { KEY_K, 1300 } } },
};
enum { SHOPS = sizeof shops / sizeof shops[0], EOLIS_TOOLS = 0, APOLUNE_TOOLS = 1, FOREPAW_TOOLS = 3,
       DARTMOOR_TOOLS = 9, EOLIS_KEYS = 10 };
enum { MEAT_SHOP_INDEX = 11, MEAT_SHOP = 0xA4A2 };   /* a shop of another layout: 88, question, pay, add HP */

static void rom_put(uint16_t at, const uint8_t *bytes, unsigned n) { memcpy(bank12 + (at - 0x8000), bytes, n); }
static void build_bank12(void) {
    memset(bank12, 0xFF, sizeof bank12);
    for (unsigned i = 0; i < ISCRIPTS; ++i) {             /* every other script: a message and an end */
        bank12[ISCRIPT_ADDRS_L - 0x8000 + i] = 0x00;
        bank12[ISCRIPT_ADDRS_U - 0x8000 + i] = 0xB0;
    }
    static const uint8_t plain[] = { 0x00, SHOW_MESSAGE, 0x39, 0x00 };
    rom_put(0xB000, plain, sizeof plain);
    for (unsigned k = 0; k < SHOPS; ++k) {
        const Shop *s = &shops[k];
        uint16_t buy = (uint16_t)(s->script + 12), it = s->items;
        const uint8_t script[] = { s->portrait, SHOW_MESSAGE, s->portrait == TOOLS ? I_SELL_TOOLS : I_SELL_KEYS,
                                   SHOW_BUY_SELL_MENU, (uint8_t)buy, (uint8_t)(buy >> 8),
                                   SHOW_SELL_MENU, (uint8_t)it, (uint8_t)(it >> 8), SHOW_MESSAGE, 0x02, 0x00,
                                   OPEN_SHOP_ACTION, (uint8_t)it, (uint8_t)(it >> 8), SHOW_MESSAGE, 0x02, 0x00 };
        rom_put(s->script, script, sizeof script);
        for (unsigned j = 0; j < 5 && s->list[j].price; ++j) {
            const uint8_t entry[] = { s->list[j].id, (uint8_t)s->list[j].price, (uint8_t)(s->list[j].price >> 8) };
            rom_put((uint16_t)(it + 3 * j), entry, 3);
            bank12[it + 3 * (j + 1) - 0x8000] = 0xFF;
        }
        bank12[ISCRIPT_ADDRS_L - 0x8000 + s->index] = (uint8_t)s->script;
        bank12[ISCRIPT_ADDRS_U - 0x8000 + s->index] = (uint8_t)(s->script >> 8);
    }
    static const uint8_t meat[] = { 0x88, 0x02, 0x07, 0x05, 0x32, 0x00, 0x13, 0x20, 0x00 };
    rom_put(MEAT_SHOP, meat, sizeof meat);
    bank12[ISCRIPT_ADDRS_L - 0x8000 + MEAT_SHOP_INDEX] = (uint8_t)MEAT_SHOP;
    bank12[ISCRIPT_ADDRS_U - 0x8000 + MEAT_SHOP_INDEX] = (uint8_t)(MEAT_SHOP >> 8);
}

/* ---- the hero ---- */
enum { GOLD = 0x0392, SELECTED = 0x03BD, MESSAGE_ID = 0x0213 };   /* SPECIAL_ITEMS: fax_shops.c's */
static const uint8_t special_ids[8] = { 0x80, 0x81, 0x82, 0x83, 0x92, 0x8A, 0x93, 0x94 };
static const uint8_t special_bits[8] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 };
static int special(uint8_t id) {
    for (int i = 0; i < 8; ++i) if (special_ids[i] == id) return i;
    return -1;
}
static unsigned gold(void) { return ram[GOLD] | ram[GOLD + 1] << 8 | ram[GOLD + 2] << 16; }
static void set_gold(unsigned g) { ram[GOLD] = (uint8_t)g; ram[GOLD + 1] = (uint8_t)(g >> 8); ram[GOLD + 2] = (uint8_t)(g >> 16); }
static void carry(unsigned g, const uint8_t *ids, unsigned n) {
    for (int c = 0; c < CATEGORIES; ++c) {
        ram[NUMBER_OF_WEAPONS + c] = 0;
        memset(ram + list_at[c], 0xEE, list_max[c]);       /* stale bytes past the counts */
        ram[SELECTED + c] = 0xFF;
    }
    ram[SELECTED] = HAND_DAGGER;
    ram[SPECIAL_ITEMS] = 0;
    for (unsigned i = 0; i < n; ++i) {
        unsigned c = ids[i] >> 5;
        ram[list_at[c] + ram[NUMBER_OF_WEAPONS + c]++] = ids[i] & 0x1F;
    }
    set_gold(g);
}
static unsigned count_of(uint8_t id) {
    unsigned c = id >> 5, n = 0;
    for (unsigned i = 0; i < ram[NUMBER_OF_WEAPONS + c]; ++i) n += ram[list_at[c] + i] == (id & 0x1F);
    return n;
}

/* ---- the game: the shop actions, as the disassembly has them ---- */
static uint8_t inputs[64];
static unsigned input_count, input_at;
static uint8_t said[32];                                     /* message IDs, in order */
static unsigned said_count, draws, closes, sounds, stuck;
static char events[256];                                     /* d: list drawn, c: list closed, m: a message */
static bool done;

static void event(char e) { size_t n = strlen(events); if (n + 1 < sizeof events) events[n] = e, events[n + 1] = 0; }
static void press(const char *buttons) {                     /* one press per character, a frame each */
    input_count = input_at = 0;
    for (; *buttons; ++buttons)
        inputs[input_count++] = *buttons == 'A' ? 0x80 : *buttons == 'B' ? 0x40 : *buttons == 's' ? 0x20 :
                                *buttons == 'u' ? 0x08 : *buttons == 'd' ? 0x04 : 0;
}
/* The pad, read at the next frame: false when the player has no more presses. */
static bool next_frame(void) {
    if (input_at == input_count) { ++stuck; return false; }
    ram[JOY1_CHANGED_BUTTON_MASK] = inputs[input_at++];
    return true;
}
static uint8_t load_byte(void) {
    uint8_t off = ram[ISCRIPT_OFFSET];
    uint8_t v = cyc_mod_peek((uint16_t)(ram[ISCRIPT_ADDR] + (ram[ISCRIPT_ADDR + 1] << 8) + off));
    ram[ISCRIPT_OFFSET] = (uint8_t)(off + 1);
    return v;
}
static void text_box(uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
    ram[TEXTBOX_X] = x; ram[TEXTBOX_X + 1] = y; ram[TEXTBOX_X + 2] = w; ram[TEXTBOX_X + 3] = h;
    ram[TEXTBOX_CLOSED] = 0;
}
/* A message typed out, and the button that goes on: true for A. */
static void say(uint8_t id) {                                /* IScripts_LoadAndShowMessage: no wait */
    said[said_count++ & 31] = id;
    ram[MESSAGE_ID] = id;
    event('m');
}
static bool message(uint8_t id) {
    say(id);
    if (!next_frame()) return false;
    return ram[JOY1_CHANGED_BUTTON_MASK] & 0x80;
}
static void shop_draw(void) { text_box(LIST_X, LIST_Y, LIST_W, LIST_H); ram[MENU_CURSOR_POS] = 0; ++draws; event('d'); }
static void close_list(void) { text_box(10, 16, 20, 8); ++closes; event('c'); }   /* Maybe_Shop_DrawTextBox */
static bool has_already(uint8_t id) {                        /* Shop_GetPlayerHasSelectedItem */
    int k = special(id);
    if (k >= 0) return ram[SPECIAL_ITEMS] & special_bits[k];
    if (id & 0x80) return false;
    unsigned c = id >> 5;
    return ram[SELECTED + c] == (id & 0x1F) || count_of(id) > 0;
}
static void remove_item(uint8_t id) {                        /* Player_RemoveItem */
    int k = special(id);
    if (k >= 0) { ram[SPECIAL_ITEMS] &= (uint8_t)~special_bits[k]; return; }
    unsigned c = id >> 5, n = ram[NUMBER_OF_WEAPONS + c];
    for (unsigned i = 0; i < n; ++i)
        if (ram[list_at[c] + i] == (id & 0x1F)) {
            memmove(ram + list_at[c] + i, ram + list_at[c] + i + 1, n - i - 1);
            ram[NUMBER_OF_WEAPONS + c] = (uint8_t)(n - 1);
            return;
        }
}
static unsigned price(unsigned i) { return ram[SHOP_ITEM_COSTS_L + i] | ram[SHOP_ITEM_COSTS_U + i] << 8; }

/* Up on a shopkeeper: IScripts_Begin with script `index`, played to its end.
 * The stack is where it was at the end. */
static void talk(unsigned index) {
    uint16_t base = (uint16_t)(bank12[ISCRIPT_ADDRS_L - 0x8000 + index] | bank12[ISCRIPT_ADDRS_U - 0x8000 + index] << 8);
    said_count = draws = closes = sounds = stuck = 0;
    events[0] = 0;
    done = false;
    cpu = (CycModRegs){ .s = 0xF0 };
    ram[ISCRIPT_ADDR] = (uint8_t)base; ram[ISCRIPT_ADDR + 1] = (uint8_t)(base >> 8); ram[ISCRIPT_OFFSET] = 1;
    ram[ISCRIPT_PORTRAIT_ID] = bank12[base - 0x8000] & 0x7F;
    text_box(10, 16, 20, 8);
    uint16_t pc = INVOKE_NEXT_ACTION, items = 0;
    bool carry_set = false;
    for (int steps = 0; !done && steps < 100000; ++steps) {
        switch (pc) {
        case INVOKE_NEXT_ACTION: {
            if (fire(pc)) { pc = rts(); break; }
            uint8_t action = load_byte();
            pc = action == 0x00 ? 0x82B4 : action == SHOW_MESSAGE ? 0x82D8 : action == OPEN_SHOP_ACTION ? OPEN_SHOP :
                 action == SHOW_BUY_SELL_MENU ? 0x8630 : action == SHOW_SELL_MENU ? 0x8660 : 0x82B4;
            break;
        }
        case 0x82B4:                                         /* IScriptAction_EndScript */
            ram[ISCRIPT_PORTRAIT_ID] = 0xFF;
            ram[TEXTBOX_CLOSED] = 1;
            done = true;
            break;
        case 0x82D8: {                                       /* IScriptAction_ShowMessage: B ends the script */
            uint8_t id = load_byte();
            pc = message(id) ? INVOKE_NEXT_ACTION : 0x82B4;
            break;
        }
        case 0x8630:                                         /* IScriptAction_ShowBuySellMenu */
            say(0x10);
            ram[ARG_STRINGS_COUNT] = 2;
            ram[MENU_CURSOR_POS] = 0;
            for (;;) {
                if (!next_frame()) { done = true; break; }
                uint8_t m = ram[JOY1_CHANGED_BUTTON_MASK];
                if (m & 0x0C) ram[MENU_CURSOR_POS] ^= 1;
                if (m & 0x80) break;
                if (m & 0x40) { pc = 0x82B4; break; }
            }
            if (done || pc == 0x82B4) break;
            if (ram[MENU_CURSOR_POS] == 0) {                 /* IScripts_JumpToNextAddr */
                uint8_t lo = load_byte(), hi = load_byte();
                ram[ISCRIPT_ADDR] = lo; ram[ISCRIPT_ADDR + 1] = hi; ram[ISCRIPT_OFFSET] = 0;
            } else { load_byte(); load_byte(); }
            pc = INVOKE_NEXT_ACTION;
            break;
        case OPEN_SHOP:
            if (fire(pc)) { pc = rts(); break; }
            pc = message(0x14) ? OPEN_SHOP_LIST : 0x82B4;
            break;
        case OPEN_SHOP_LIST: {                               /* the items, Shop_Populate, Shop_Draw */
            uint8_t lo = load_byte(), hi = load_byte();
            items = (uint16_t)(lo | hi << 8);
            Offer list[PAGE];
            int n = shop_list(items, list);
            for (int i = 0; i < n; ++i) {
                ram[DATA_ARRAY + i] = list[i].id;
                ram[SHOP_ITEM_COSTS_L + i] = (uint8_t)list[i].price;
                ram[SHOP_ITEM_COSTS_U + i] = (uint8_t)(list[i].price >> 8);
            }
            ram[ARG_STRINGS_COUNT] = (uint8_t)n;
            CHECK(!fire(OPEN_SHOP_DRAW));                    /* JSR Shop_Draw: only writes the list */
            shop_draw();
            pc = BUY_WAIT;
            break;
        }
        case BUY_WAIT: jsr(0x83FC); pc = 0x84ED; break;   /* JSR IScripts_WaitForMenuInput */
        case 0x84ED:                                         /* IScripts_WaitForMenuInput: the frame */
            if (!next_frame()) { done = true; break; }
            pc = MENU_UPDATE;
            break;
        case MENU_UPDATE: {
            if (fire(pc)) { pc = rts(); break; }
            uint8_t m = ram[JOY1_CHANGED_BUTTON_MASK], n = ram[ARG_STRINGS_COUNT];
            if (m & 0x08) ram[MENU_CURSOR_POS] = (uint8_t)(ram[MENU_CURSOR_POS] ? ram[MENU_CURSOR_POS] - 1 : n - 1);
            else if (m & 0x04) ram[MENU_CURSOR_POS] = (uint8_t)(ram[MENU_CURSOR_POS] + 1 == n ? 0 : ram[MENU_CURSOR_POS] + 1);
            if (m & 0x80) { carry_set = false; pc = rts(); }
            else if (m & 0x40) { carry_set = true; pc = rts(); }
            else pc = 0x84ED;
            break;
        }
        case 0x83FD: pc = carry_set ? 0x82B4 : BUY_PICK; break;   /* OpenShop, after the menu: BCC, JMP EndScript */
        case BUY_PICK: {                                     /* A: Shop_GetPlayerHasSelectedItem, ... */
            if (fire(pc)) { pc = rts(); break; }
            unsigned x = ram[MENU_CURSOR_POS];
            uint8_t id = ram[DATA_ARRAY + x];
            if (has_already(id)) { pc = 0x8469; break; }
            if (special(id) < 0) {
                unsigned c = id >> 5;
                if (ram[NUMBER_OF_WEAPONS + c] == list_max[c]) { pc = 0x8469; break; }
            }
            if (gold() < price(x)) { close_list(); pc = 0x837C; break; }
            int k = special(id);
            if (k >= 0) ram[SPECIAL_ITEMS] |= special_bits[k];
            else { unsigned c = id >> 5; ram[list_at[c] + ram[NUMBER_OF_WEAPONS + c]++] = id & 0x1F; }
            set_gold(gold() - price(x));                     /* IScripts_ProgressivelySubtractGold */
            pc = BOUGHT;
            break;
        }
        case BOUGHT:
            if (fire(pc)) { pc = rts(); break; }
            close_list();
            pc = INVOKE_NEXT_ACTION;
            break;
        case 0x8469: close_list(); cpu.a = 0x06; pc = 0x837E; break;
        case 0x837C: cpu.a = 0x03; pc = 0x837E; break;     /* IScripts_ShowNotEnoughMoney */
        case 0x837E: message(cpu.a); pc = FINAL_MESSAGE_END; break;
        case FINAL_MESSAGE_END:
            if (fire(pc)) { pc = rts(); break; }
            pc = 0x82B4;
            break;
        case MAYBE_SHOP_DRAW_TEXTBOX: close_list(); pc = rts(); break;
        case SOUND_PLAY_EFFECT: sounds += cpu.a == SOUND_CURSOR; pc = rts(); break;
        case 0x8660: {                                       /* ShowSellMenu: the items, the stock scan */
            uint8_t lo = load_byte(), hi = load_byte();
            items = (uint16_t)(lo | hi << 8);
            Offer list[PAGE];
            int n = shop_list(items, list), count = 0;
            for (int y = ram[NUMBER_OF_WEAPONS + ITEMS] - 1; y >= 0; --y) {
                uint8_t id = (uint8_t)(ram[list_at[ITEMS] + y] | 0x80);
                for (int k = 0; k < n; ++k)
                    if (list[k].id == id) {
                        ram[DATA_ARRAY + count] = id;
                        ram[SHOP_ITEM_COSTS_L + count] = (uint8_t)(list[k].price >> 1);
                        ram[SHOP_ITEM_COSTS_U + count] = (uint8_t)(list[k].price >> 9);
                        ++count;
                        break;
                    }
            }
            ram[ARG_STRINGS_COUNT] = (uint8_t)count;
            pc = SELL_LIST;
            break;
        }
        case SELL_LIST:
            if (fire(pc)) { pc = rts(); break; }
            if (!ram[ARG_STRINGS_COUNT]) { cpu.a = 0x12; pc = 0x837E; break; }
            pc = message(0x13) ? SELL_DRAW : 0x82B4;
            break;
        case SELL_DRAW: shop_draw(); pc = SELL_WAIT; break;
        case SELL_WAIT: jsr(0x86DD); pc = 0x84ED; break;
        case 0x86DE: pc = carry_set ? 0x82B4 : SELL_PICK; break;
        case SELL_PICK: {
            if (fire(pc)) { pc = rts(); break; }
            unsigned x = ram[MENU_CURSOR_POS];
            unsigned amount = price(x);
            remove_item(ram[DATA_ARRAY + x]);
            set_gold(gold() + amount);                       /* IScripts_ProgressivelyAddGold */
            pc = SOLD;
            break;
        }
        case SOLD:
            if (fire(pc)) { pc = rts(); break; }
            close_list();
            pc = INVOKE_NEXT_ACTION;
            break;
        default:
            fprintf(stderr, "the model has no code at $%04X\n", pc);
            exit(1);
        }
    }
    CHECK(done);
    if (cpu.s != 0xF0) fprintf(stderr, "talk(%u): S=%02X events=%s said=%u stuck=%u\n", index, cpu.s, events, said_count, stuck);
    CHECK(cpu.s == 0xF0);                                    /* every jump kept the stack where the game's would */
}
static void on(bool shops_on) { reset_cb(); if (shops_on) activate_cb(); }
static bool listed(const uint8_t *ids, const uint16_t *prices, unsigned n) {
    if (ram[ARG_STRINGS_COUNT] != n) return false;
    for (unsigned i = 0; i < n; ++i)
        if (ram[DATA_ARRAY + i] != ids[i] || price(i) != prices[i]) return false;
    return true;
}

/* ---- the tests ---- */

static void prices(void) {
    /* Half this shop's price where it sells the item, as ShowSellMenu
     * halves it; else half the lowest any shop asks; else 100. */
    CHECK(fax_shops_sell_price(RED_POTION, 160, 160) == 80);
    CHECK(fax_shops_sell_price(RED_POTION, 2000, 160) == 1000);
    CHECK(fax_shops_sell_price(KEY_J, 0, 100) == 50);
    CHECK(fax_shops_sell_price(KEY_A, 0, 0) == 100);
    CHECK(fax_shops_sell_price(FIRE, 0, 0) == 100);
    CHECK(fax_shops_sell_price(RED_POTION, 0, 165) == 80);   /* the gold counts by 10s */
    /* Never: the last fight's gear, and every SpecialItems bit. */
    static const uint8_t never[] = { DRAGON_SLAYER, BATTLE_SUIT, BATTLE_HELMET, 0x80, 0x81, 0x82, 0x83,
                                     MAGICAL_ROD, ELIXIR, PENDANT, BLACK_ONYX };
    for (unsigned i = 0; i < sizeof never; ++i) {
        CHECK(fax_shops_never_buys(never[i]));
        CHECK(fax_shops_sell_price(never[i], 320, 320) == 0);
    }
    for (int k = 0; k < 8; ++k) CHECK(fax_shops_never_buys(special_ids[k]));
    static const uint8_t sells[] = { HAND_DAGGER, LONG_SWORD, GIANT_BLADE, 0x20, STUDDED_MAIL, FULL_PLATE, SMALL_SHIELD,
                                     LARGE_SHIELD, MAGIC_SHIELD, DELUGE, THUNDER, FIRE, DEATH, TILTE, KEY_A, KEY_K,
                                     KEY_Q, KEY_J, KEY_JO, MATTOCK, HOUR_GLASS, WING_BOOTS, RED_POTION, POISON };
    for (unsigned i = 0; i < sizeof sells; ++i) CHECK(!fax_shops_never_buys(sells[i]));

    /* The lowest prices, read from the shops' scripts. */
    priced = price_everything();
    CHECK(priced);
    CHECK(lowest[RED_POTION] == 160 && lowest[KEY_J] == 100 && lowest[KEY_Q] == 500 && lowest[KEY_K] == 1200);
    CHECK(lowest[GIANT_BLADE] == 8500 && lowest[MATTOCK] == 800 && lowest[WING_BOOTS] == 2800);
    CHECK(lowest[DEATH] == 9800 && lowest[MAGIC_SHIELD] == 7600 && lowest[ELIXIR] == 320);
    CHECK(!lowest[KEY_A] && !lowest[KEY_JO] && !lowest[FIRE] && !lowest[DRAGON_SLAYER] && !lowest[POISON]);
    CHECK(!lowest[0x00 + 0x20]);                                      /* the Leather Armor: no shop */
    /* Every shop's script is found, and nothing else. */
    int found = 0;
    for (int i = 0; i < ISCRIPTS; ++i) {
        Script s;
        found += shop_script((uint16_t)(bank12[ISCRIPT_ADDRS_L - 0x8000 + i] | bank12[ISCRIPT_ADDRS_U - 0x8000 + i] << 8), &s);
    }
    CHECK(found == SHOPS);

    /* No new way to make gold: selling at a shop that does not sell the item
     * never pays what any shop asks for it. (Where a shop does sell it, the
     * price is the original's half of that shop's, Red Potions bought in
     * Eolis and sold in Dartmoor included.) Every price is a multiple of 10. */
    for (unsigned s = 0; s < SHOPS; ++s)
        for (unsigned t = 0; t < SHOPS; ++t)
            for (unsigned j = 0; j < 5 && shops[t].list[j].price; ++j) {
                uint8_t id = shops[t].list[j].id;
                uint16_t here = 0;
                for (unsigned k = 0; k < 5 && shops[s].list[k].price; ++k)
                    if (shops[s].list[k].id == id) here = shops[s].list[k].price;
                uint16_t pay = fax_shops_sell_price(id, here, lowest[id]);
                CHECK(pay % 10 == 0);
                if (!here) CHECK(pay < shops[t].list[j].price);
            }
}

static void straight_to_the_list(void) {
    on(true);
    /* Eolis's tool shop: Up opens the list. No message, not a press spent. */
    carry(700, (const uint8_t[]){ KEY_J }, 1);
    press("B");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(said_count == 0 && !strcmp(events, "dc") && input_at == 1 && !stuck);   /* B: closed, then the end */
    CHECK(listed((const uint8_t[]){ HAND_DAGGER, RED_POTION, ELIXIR, DELUGE }, (const uint16_t[]){ 400, 160, 320, 400 }, 4));
    CHECK(ram[ISCRIPT_PORTRAIT_ID] == 0xFF && gold() == 700);
    /* Every shop, the key shops too. */
    for (unsigned k = 0; k < SHOPS; ++k) {
        press("B");
        talk(shops[k].index);
        CHECK(said_count == 0 && draws == 1 && ram[DATA_ARRAY] == shops[k].list[0].id);
    }
    /* Another kind of script is left alone. */
    press("A");
    talk(MEAT_SHOP_INDEX);
    CHECK(said_count == 0 && draws == 0 && !shop.active);
    press("A");
    talk(0);
    CHECK(said_count == 1 && said[0] == 0x39 && !shop.active);
}

static void buying(void) {
    on(true);
    carry(700, (const uint8_t[]){ KEY_J }, 1);
    /* Down to the Red Potion, A three times, B: five presses, five choices. */
    press("dAAAB");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(input_at == 5 && !stuck && said_count == 0);
    CHECK(gold() == 700 - 3 * 160 && count_of(RED_POTION) == 3 && count_of(KEY_J) == 1);
    CHECK(draws == 4 && closes == 1);                    /* drawn again in place, closed as B leaves */
    CHECK(bought == 3);
    /* The Elixir: bought once, then it is owned, and A on it does nothing:
     * no gold, no line, the list as it was. */
    carry(700, NULL, 0);
    press("ddAAAB");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(gold() == 700 - 320 && (ram[SPECIAL_ITEMS] & 0x08));
    CHECK(said_count == 0 && !strcmp(events, "ddc") && ignored == 2 && input_at == 6 && !stuck);
}

static void the_games_checks(void) {
    on(true);
    /* Not enough Golds: the game's line, then the list again, the cursor where it was. */
    carry(300, NULL, 0);
    press("dddAAuuAB");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(said_count == 1 && said[0] == 0x03);
    CHECK(!strcmp(events, "dcmddc"));                    /* the list, closed for the line, again; a purchase */
    CHECK(gold() == 300 - 160 && count_of(RED_POTION) == 1 && count_of(DELUGE) == 0);
    CHECK(input_at == 9 && !stuck);
    /* The Hand Dagger in hand: "You can't carry any more." */
    carry(1000, NULL, 0);
    press("AAB");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(said_count == 1 && said[0] == 0x06 && gold() == 1000);
    /* A full Items list: the same line for a Red Potion. */
    static const uint8_t eight[] = { KEY_J, KEY_J, KEY_J, KEY_J, KEY_Q, KEY_Q, KEY_Q, MATTOCK };
    carry(1000, eight, 8);
    press("dAAB");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(said_count == 1 && said[0] == 0x06 && gold() == 1000 && count_of(RED_POTION) == 0);
    /* The Elixir with one in hand is owned: A does nothing, where the game
     * would refuse it. */
    carry(1000, NULL, 0);
    ram[SPECIAL_ITEMS] = 0x08;
    press("ddAAB");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(said_count == 0 && gold() == 1000 && ignored == 2 && !strcmp(events, "dc"));
}

/* fax_shops_owned, on plain values: each item, held or selected, with the
 * feature that keeps it on or off. */
static void ownership(void) {
    FaxShopsHero none = { .selected = 0xFF, .recharging = true, .reusable = true };
    static const uint8_t kept[] = { WING_BOOTS, HOUR_GLASS, KEY_A, KEY_K, KEY_Q, KEY_J, KEY_JO, MATTOCK };
    for (unsigned k = 0; k < sizeof kept; ++k) {
        uint8_t id = kept[k];
        bool by_recharge = id == WING_BOOTS || id == HOUR_GLASS;
        FaxShopsHero h = none;
        CHECK(!fax_shops_owned(id, &h));                            /* not his */
        h.items[0] = RED_POTION & 0x1F; h.items[1] = id & 0x1F; h.count = 2;
        CHECK(fax_shops_owned(id, &h));                             /* in his list */
        h.count = 1;
        CHECK(!fax_shops_owned(id, &h));                            /* a stale byte past the count */
        h = none; h.selected = id & 0x1F;
        CHECK(fax_shops_owned(id, &h));                             /* selected, in no list */
        if (by_recharge) h.recharging = false; else h.reusable = false;
        CHECK(!fax_shops_owned(id, &h));                            /* used up without its feature */
        if (by_recharge) h.reusable = false, h.recharging = true; else h.recharging = false, h.reusable = true;
        CHECK(fax_shops_owned(id, &h));                             /* the other feature does not matter */
        CHECK(!fax_shops_owned((uint8_t)(id & 0x1F), &h));          /* the same ID in the Weapons: no */
    }
    /* The Elixir: charged (its SpecialItems bit) or spent, whatever is on. */
    FaxShopsHero h = none;
    CHECK(!fax_shops_owned(ELIXIR, &h));
    h.special = 0x08;
    CHECK(fax_shops_owned(ELIXIR, &h));
    h.recharging = h.reusable = false;
    CHECK(fax_shops_owned(ELIXIR, &h));
    h = none; h.elixir_spent = true;
    CHECK(fax_shops_owned(ELIXIR, &h));
    h = none; h.special = 0xF7;                                     /* every other bit */
    CHECK(!fax_shops_owned(ELIXIR, &h));
    /* Anything else never is: a Red Potion or the Poison held, weapons,
     * armor, shields and spells (the game refuses those it has), the other
     * special items. */
    static const uint8_t never[] = { RED_POTION, POISON, HAND_DAGGER, LONG_SWORD, STUDDED_MAIL, SMALL_SHIELD,
                                     DELUGE, DEATH, RING_OF_ELF, MAGICAL_ROD, PENDANT, BLACK_ONYX };
    for (unsigned k = 0; k < sizeof never; ++k) {
        h = none;
        h.selected = never[k] & 0x1F; h.items[0] = never[k] & 0x1F; h.count = 1; h.special = 0xFF;
        CHECK(!fax_shops_owned(never[k], &h));
    }
}

/* fax_shops_not_for_sale, on plain values: what the hero has for good no
 * shop buys from him, while its feature keeps it; nor what no shop buys at
 * all; the rest sells. */
static void not_for_sale(void) {
    FaxShopsHero none = { .selected = 0xFF, .recharging = true, .reusable = true };
    static const uint8_t kept[] = { WING_BOOTS, HOUR_GLASS, KEY_A, KEY_K, KEY_Q, KEY_J, KEY_JO, MATTOCK };
    for (unsigned k = 0; k < sizeof kept; ++k) {
        uint8_t id = kept[k];
        FaxShopsHero h = none;
        h.items[0] = RED_POTION & 0x1F; h.items[1] = id & 0x1F; h.count = 2;
        CHECK(fax_shops_not_for_sale(id, &h));                      /* his for good */
        CHECK(!fax_shops_not_for_sale(RED_POTION, &h));             /* the potion beside it sells */
        if (id == WING_BOOTS || id == HOUR_GLASS) h.recharging = false; else h.reusable = false;
        CHECK(!fax_shops_not_for_sale(id, &h));                     /* used up without its feature: it sells */
    }
    /* What no shop buys, whatever the hero has and whatever is on. */
    static const uint8_t never[] = { DRAGON_SLAYER, BATTLE_SUIT, BATTLE_HELMET, 0x80, 0x81, 0x82, 0x83,
                                     MAGICAL_ROD, ELIXIR, PENDANT, BLACK_ONYX };
    for (unsigned k = 0; k < sizeof never; ++k) {
        FaxShopsHero h = { .selected = 0xFF };
        CHECK(fax_shops_not_for_sale(never[k], &h));
    }
    /* The rest sells: potions, the Poison, weapons, armor, shields and spells. */
    static const uint8_t sells[] = { RED_POTION, POISON, HAND_DAGGER, LONG_SWORD, STUDDED_MAIL, SMALL_SHIELD,
                                     DELUGE, DEATH };
    for (unsigned k = 0; k < sizeof sells; ++k) {
        FaxShopsHero h = none;
        h.items[0] = sells[k] & 0x1F; h.count = 1;
        CHECK(!fax_shops_not_for_sale(sells[k], &h));
    }
}

/* In the buy list, an owned item keeps its row, and A on it does nothing. */
static void owned_rows(void) {
    enum { BOOTS_ROW = 4 };
    /* Forepaw's: buy the Wing Boots; A on them again does nothing; then a Red Potion. */
    on(true);
    carry(9000, (const uint8_t[]){ RED_POTION }, 1);
    press("ddddAAuAB");
    talk(shops[FOREPAW_TOOLS].index);
    CHECK(!stuck && input_at == 9 && said_count == 0);
    CHECK(gold() == 9000 - 2800 - 400 && count_of(WING_BOOTS) == 1 && count_of(RED_POTION) == 2);
    CHECK(bought == 2 && ignored == 1 && !strcmp(events, "dddc"));  /* drawn, redrawn after each purchase, left */
    CHECK(ram[ARG_STRINGS_COUNT] == 5 && ram[DATA_ARRAY + BOOTS_ROW] == WING_BOOTS && price(BOOTS_ROW) == 2800);
    /* Back again, the boots selected (in no list): the same row, the same price, owned. */
    on(true);
    carry(5800, NULL, 0);
    ram[0x03C1] = WING_BOOTS & 0x1F;
    press("ddddAAAuB");
    talk(shops[FOREPAW_TOOLS].index);
    CHECK(gold() == 5800 && ignored == 3 && said_count == 0 && !strcmp(events, "dc") && ram[MENU_CURSOR_POS] == 3);
    CHECK(listed((const uint8_t[]){ LONG_SWORD, STUDDED_MAIL, SMALL_SHIELD, RED_POTION, WING_BOOTS },
                 (const uint16_t[]){ 1600, 2500, 1100, 400, 2800 }, 5));
    /* Without recharging items they are used up, so a second pair sells, as
     * in the original (Shop_GetPlayerHasSelectedItem refuses no Items). */
    recharging_on = false;
    on(true);
    carry(9000, (const uint8_t[]){ WING_BOOTS }, 1);
    press("ddddAB");
    talk(shops[FOREPAW_TOOLS].index);
    CHECK(gold() == 9000 - 2800 && count_of(WING_BOOTS) == 2 && ignored == 0);
    recharging_on = true;

    /* After Mascon's (Death, Hour Glass, Elixir, Red Potion): the Hour Glass held, and the Elixir spent. */
    enum { AFTER_MASCON = 5 };
    on(true);
    carry(20000, (const uint8_t[]){ HOUR_GLASS }, 1);
    spent = true;
    press("dAdAdAB");
    talk(shops[AFTER_MASCON].index);
    CHECK(gold() == 20000 - 300 && count_of(HOUR_GLASS) == 1 && count_of(RED_POTION) == 1);
    CHECK(!(ram[SPECIAL_ITEMS] & 0x08) && ignored == 2 && said_count == 0);   /* buying never charges it */
    spent = false;
    /* Neither: both sell. */
    on(true);
    carry(20000, NULL, 0);
    press("dAdAB");
    talk(shops[AFTER_MASCON].index);
    CHECK(gold() == 20000 - 5600 - 4300 && count_of(HOUR_GLASS) == 1 && (ram[SPECIAL_ITEMS] & 0x08) && ignored == 0);

    /* The J key at Eolis's key shop and the Mattock at Mascon's, held. */
    enum { MASCON_TOOLS = 4 };
    on(true);
    carry(1000, (const uint8_t[]){ KEY_J }, 1);
    press("AAB");
    talk(shops[EOLIS_KEYS].index);
    CHECK(gold() == 1000 && count_of(KEY_J) == 1 && ignored == 2);
    on(true);
    carry(1000, (const uint8_t[]){ MATTOCK }, 1);
    press("ddAB");
    talk(shops[MASCON_TOOLS].index);
    CHECK(gold() == 1000 && count_of(MATTOCK) == 1 && ignored == 1);
    /* Without reusable tools a key is used up, so a second sells. */
    reusable_on = false;
    on(true);
    carry(1000, (const uint8_t[]){ KEY_J }, 1);
    press("AB");
    talk(shops[EOLIS_KEYS].index);
    CHECK(gold() == 900 && count_of(KEY_J) == 2 && ignored == 0);
    reusable_on = true;

    /* Nor does a shop buy them: in the sell list owned items are not for
     * sale, and A on them does nothing. */
    on(true);
    carry(0, (const uint8_t[]){ WING_BOOTS }, 1);
    press("sAAB");
    talk(shops[FOREPAW_TOOLS].index);
    CHECK(listed((const uint8_t[]){ WING_BOOTS }, (const uint16_t[]){ 0 }, 1));
    CHECK(gold() == 0 && count_of(WING_BOOTS) == 1 && refused == 2 && sold == 0 && said_count == 0 && !stuck);
    /* Without recharging items they are used up, so they sell, at Forepaw's half. */
    recharging_on = false;
    on(true);
    carry(0, (const uint8_t[]){ WING_BOOTS }, 1);
    press("sAAB");                                                  /* the last thing sold: the game's line */
    talk(shops[FOREPAW_TOOLS].index);
    CHECK(gold() == 1400 && count_of(WING_BOOTS) == 0 && refused == 0 && !stuck);
    recharging_on = true;

    /* Quick shops off: the original. A second pair sells, and a held Elixir
     * gets the game's own "You can't carry any more.". */
    on(false);
    carry(9000, (const uint8_t[]){ WING_BOOTS }, 1);
    press("AAAddddAA");
    talk(shops[FOREPAW_TOOLS].index);
    CHECK(gold() == 9000 - 2800 && count_of(WING_BOOTS) == 2);
    carry(1000, NULL, 0);
    ram[SPECIAL_ITEMS] = 0x08;
    press("AAAddAA");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(gold() == 1000 && said[said_count - 1] == 0x06);
    on(true);
}

static void selling(void) {
    on(true);
    static const uint8_t carried[] = { DRAGON_SLAYER, LONG_SWORD, KEY_J, KEY_A, WING_BOOTS, POISON, RED_POTION,
                                       RED_POTION };
    carry(100, carried, sizeof carried);
    /* Select: the sell list, the Items first as the original lists them (from
     * the last), then the weapons. The Poison no shop sells: 100. The Wing
     * Boots and the keys, his for good, and the Dragon Slayer: no price. */
    press("sB");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(said_count == 0 && sounds == 1 && !strcmp(events, "ddc"));
    CHECK(listed((const uint8_t[]){ RED_POTION, RED_POTION, POISON, WING_BOOTS, KEY_A, KEY_J, LONG_SWORD, DRAGON_SLAYER },
                 (const uint16_t[]){ 80, 80, 100, 0, 0, 0, 800, 0 }, 8));
    /* Sell the Poison (no shop's: 100) and a Red Potion (this shop's: 80). */
    carry(100, carried, sizeof carried);
    press("sddAuuAB");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(said_count == 0 && !stuck && input_at == 8);
    CHECK(gold() == 100 + 100 + 80 && count_of(POISON) == 0 && count_of(RED_POTION) == 1 && count_of(KEY_A) == 1);
    CHECK(closes == 1);
    /* The Wing Boots, the A and J keys and the Dragon Slayer: A does
     * nothing, and the list waits. */
    on(true);
    carry(100, carried, sizeof carried);
    press("sdddAdAdAddAB");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(gold() == 100 && count_of(WING_BOOTS) == 1 && count_of(KEY_A) == 1 && count_of(KEY_J) == 1);
    CHECK(count_of(DRAGON_SLAYER) == 1 && said_count == 0 && refused == 4 && sold == 0 && !stuck);
    /* Without reusable tools and recharging items, the keys and the boots
     * are used up, so they sell: the J key at a key shop's half (50), the A
     * key, which no shop sells, at 100, the boots at Forepaw's half (1400). */
    reusable_on = recharging_on = false;
    on(true);
    carry(100, carried, sizeof carried);
    press("sB");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(listed((const uint8_t[]){ RED_POTION, RED_POTION, POISON, WING_BOOTS, KEY_A, KEY_J, LONG_SWORD, DRAGON_SLAYER },
                 (const uint16_t[]){ 80, 80, 100, 1400, 100, 50, 800, 0 }, 8));
    press("sdddddAB");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(gold() == 150 && count_of(KEY_J) == 0 && refused == 0);
    reusable_on = recharging_on = true;
    /* At a key shop, a Red Potion sells at half the lowest tool shop's
     * price. Then nothing is left: the original's line, and buying. */
    carry(0, (const uint8_t[]){ RED_POTION }, 1);
    press("sAAB");
    talk(shops[EOLIS_KEYS].index);
    CHECK(gold() == 80 && count_of(RED_POTION) == 0 && said_count == 1 && said[0] == 0x12);
    /* At Dartmoor's, at half its own, as the original pays. */
    carry(0, (const uint8_t[]){ RED_POTION }, 1);
    press("sAAB");
    talk(shops[DARTMOOR_TOOLS].index);
    CHECK(gold() == 1000 && !stuck);
    /* A special item that somehow sits in a list is never sold, so its
     * SpecialItems bit (Player_RemoveItem clears that) stays. */
    carry(0, NULL, 0);
    ram[list_at[ITEMS]] = ELIXIR & 0x1F; ram[NUMBER_OF_WEAPONS + ITEMS] = 1;
    ram[SPECIAL_ITEMS] = 0x08;
    press("sAB");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(ram[SPECIAL_ITEMS] == 0x08 && gold() == 0 && ram[NUMBER_OF_WEAPONS + ITEMS] == 1);
}

static void switching(void) {
    on(true);
    carry(500, (const uint8_t[]){ KEY_J, LONG_SWORD }, 2);
    /* Each list keeps its cursor: buy on the Red Potion, sell, back. */
    press("dsdsAB");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(sounds == 2 && gold() == 500 - 160 && count_of(RED_POTION) == 1);
    CHECK(shop.cursor[SELL] == 1);
    /* Nothing to sell: the list closes, the original's line, then buying again. */
    carry(500, NULL, 0);
    press("sAdAB");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(said_count == 1 && said[0] == 0x12 && !strcmp(events, "dcmddc"));
    CHECK(gold() == 500 - 160);
    /* Selling the last thing: the line, then buying. */
    carry(0, (const uint8_t[]){ RED_POTION }, 1);
    press("sAAB");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(gold() == 80 && said_count == 1 && said[0] == 0x12 && !strcmp(events, "ddcmdc"));
}

static void pages_of_eight(void) {
    on(true);
    static const uint8_t carried[] = { KEY_J, KEY_Q, KEY_K, MATTOCK, RED_POTION, RED_POTION, KEY_A, RED_POTION,
                                       LONG_SWORD, GIANT_BLADE, DRAGON_SLAYER };
    carry(0, carried, sizeof carried);
    press("sB");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(shop.selling == 11 && pages() == 2 && ram[ARG_STRINGS_COUNT] == 8 && ram[DATA_ARRAY] == RED_POTION);
    /* Down from the bottom: the second page, its top. */
    carry(0, carried, sizeof carried);
    press("sdddddddddB");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(shop.page == 1 && turned == 1 && ram[ARG_STRINGS_COUNT] == 3);
    CHECK(listed((const uint8_t[]){ DRAGON_SLAYER, GIANT_BLADE, LONG_SWORD }, (const uint16_t[]){ 0, 4250, 800 }, 3));
    /* Up from the top of the first: the last page, its bottom; sell the Long Sword there. */
    carry(0, carried, sizeof carried);
    press("suAB");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(gold() == 800 && count_of(LONG_SWORD) == 0 && shop.page == 1);
    /* Sell the last of a page: back to the end of the page before. */
    static const uint8_t nine[] = { KEY_J, KEY_Q, KEY_K, MATTOCK, RED_POTION, RED_POTION, KEY_A, RED_POTION, LONG_SWORD };
    carry(0, nine, sizeof nine);
    press("suAB");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(gold() == 800 && shop.page == 0 && ram[MENU_CURSOR_POS] == 7);
}

static void off(void) {
    on(false);
    carry(700, NULL, 0);
    /* As the original: "I sell tools.", Buy/Sell, "What would you like?", the
     * list, then "Thank you for shopping." after a purchase. */
    press("AAAdAA");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(said_count == 4 && said[0] == I_SELL_TOOLS && said[1] == 0x10 && said[2] == 0x14 && said[3] == 0x02);
    CHECK(gold() == 700 - 160);
    /* The sell menu offers only the Items this shop sells. */
    carry(700, (const uint8_t[]){ KEY_J, RED_POTION, LONG_SWORD }, 3);
    press("AdAAB");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(listed((const uint8_t[]){ RED_POTION }, (const uint16_t[]){ 80 }, 1));
    /* A key shop buys a J key, at half its price: "I sell keys.", Sell,
     * "Which one would like to sell?", the key, "Thank you for shopping.". */
    carry(0, (const uint8_t[]){ KEY_J }, 1);
    press("AdAAAA");
    talk(shops[EOLIS_KEYS].index);
    CHECK(said_count == 4 && said[0] == I_SELL_KEYS && said[2] == 0x13 && said[3] == 0x02);
    CHECK(gold() == 50 && count_of(KEY_J) == 0 && !stuck);
    on(true);
}

/* While the Potion Sellers sell the Red Potions, the tool shops' buy lists
 * lose theirs: the entries after it move up, and buy as before. A shop still
 * buys one, at half its own old price. */
static void potion_sellers(void) {
    /* On plain values: every entry of the item goes, the rest keep their order. */
    uint8_t ids[] = { HAND_DAGGER, RED_POTION, ELIXIR, RED_POTION, DELUGE };
    uint16_t costs[] = { 400, 160, 320, 300, 400 };
    CHECK(fax_shops_without(RED_POTION, ids, costs, 5) == 3);
    CHECK(ids[0] == HAND_DAGGER && ids[1] == ELIXIR && ids[2] == DELUGE);
    CHECK(costs[0] == 400 && costs[1] == 320 && costs[2] == 400);
    CHECK(fax_shops_without(RED_POTION, ids, costs, 3) == 3 && fax_shops_without(RED_POTION, ids, costs, 0) == 0);

    on(true);
    potions_on = true;
    /* Every shop: its own list, less its Red Potion; nine tool shops sold
     * one, "After Mascon"'s (300) among them. */
    unsigned lost = 0;
    for (unsigned k = 0; k < SHOPS; ++k) {
        uint8_t want[5];
        uint16_t want_costs[5];
        unsigned n = 0, had = 0;
        for (unsigned j = 0; j < 5 && shops[k].list[j].price; ++j, ++had)
            if (shops[k].list[j].id != RED_POTION) want[n] = shops[k].list[j].id, want_costs[n++] = shops[k].list[j].price;
        carry(0, NULL, 0);
        press("B");
        talk(shops[k].index);
        CHECK(listed(want, want_costs, n) && draws == 1 && !stuck);
        lost += had - n;
    }
    CHECK(lost == 9);
    /* Eolis's: Hand Dagger, Elixir, Deluge. Down, A: the Elixir; Up from the
     * top wraps to the Deluge, the last of three. */
    carry(1000, (const uint8_t[]){ KEY_J }, 1);
    press("dAB");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(gold() == 1000 - 320 && (ram[SPECIAL_ITEMS] & 0x08) && count_of(RED_POTION) == 0);
    carry(1000, (const uint8_t[]){ KEY_J }, 1);
    press("uAB");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(gold() == 1000 - 400 && count_of(DELUGE) == 1 && count_of(RED_POTION) == 0);
    /* Bought again, the list is drawn again without it, the cursor kept. */
    carry(2000, NULL, 0);
    press("ddAAAB");                                                   /* the Deluge, again: refused, A, the list */
    talk(shops[EOLIS_TOOLS].index);
    CHECK(gold() == 2000 - 400 && count_of(DELUGE) == 1 && said_count == 1 && said[0] == 0x06 && draws == 3);
    CHECK(listed((const uint8_t[]){ HAND_DAGGER, ELIXIR, DELUGE }, (const uint16_t[]){ 400, 320, 400 }, 3));
    /* Selling a Red Potion: half the shop's own price, from its list. */
    static const struct { unsigned shop; uint16_t pays; } sales[] = {
        { EOLIS_TOOLS, 80 }, { APOLUNE_TOOLS, 150 }, { FOREPAW_TOOLS, 200 }, { 5, 150 }, { DARTMOOR_TOOLS, 1000 },
        { EOLIS_KEYS, 80 } };                                         /* a key shop: half the lowest, 160 */
    for (unsigned i = 0; i < sizeof sales / sizeof sales[0]; ++i) {
        carry(0, (const uint8_t[]){ RED_POTION, RED_POTION }, 2);
        press("sAB");
        talk(shops[sales[i].shop].index);
        CHECK(gold() == sales[i].pays && count_of(RED_POTION) == 1 && !stuck);
    }
    /* Not selling them (his line could not be supplied): the lists as they were. */
    potions_on = false;
    press("B");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(listed((const uint8_t[]){ HAND_DAGGER, RED_POTION, ELIXIR, DELUGE }, (const uint16_t[]){ 400, 160, 320, 400 }, 4));
    /* Nor with Quick shops off, or in Seamless's isolated scope. */
    potions_on = true;
    isolated = true;
    carry(700, NULL, 0);
    press("AAAB");                                                     /* the greeting, Buy, the question, the list */
    talk(shops[EOLIS_TOOLS].index);
    isolated = false;
    CHECK(listed((const uint8_t[]){ HAND_DAGGER, RED_POTION, ELIXIR, DELUGE }, (const uint16_t[]){ 400, 160, 320, 400 }, 4));
    on(false);
    carry(700, NULL, 0);
    press("AAAB");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(listed((const uint8_t[]){ HAND_DAGGER, RED_POTION, ELIXIR, DELUGE }, (const uint16_t[]){ 400, 160, 320, 400 }, 4));
    potions_on = false;
    on(true);
}

static void isolated_and_saved(void) {
    on(true);
    /* In Seamless's isolated scope, nothing is steered. */
    isolated = true;
    carry(700, NULL, 0);
    press("AAAB");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(said[0] == I_SELL_TOOLS && !shop.active);
    isolated = false;
    /* A state saved in a shop goes on with it. */
    carry(700, (const uint8_t[]){ KEY_J }, 1);
    press("dsB");
    talk(shops[EOLIS_TOOLS].index);
    uint8_t buf[32];
    int n = save_get(buf, sizeof buf);
    CHECK(n == STATE_LEN && save_validate(buf, n));
    Session before = shop;
    shop = (Session){ 0 };
    CHECK(save_set(buf, n) && !memcmp(&shop, &before, sizeof shop));
    CHECK(save_validate(NULL, 0) && save_set(NULL, 0) && !shop.active);
    buf[2] = 7;
    CHECK(!save_validate(buf, n) && !save_set(buf, n));
    CHECK(save_get(buf, 4) < 0);
}

/* ---- the hint, on a model picture ---- */
static void frame_list(bool up) {
    memset(ciram, 0, sizeof ciram);
    if (!up) return;
    ciram[LIST_Y * 32 + LIST_X] = CORNER_TOP_LEFT;
    ciram[LIST_Y * 32 + LIST_X + LIST_W - 1] = CORNER_TOP_RIGHT;
    ciram[(LIST_Y + LIST_H - 1) * 32 + LIST_X] = CORNER_BOTTOM_LEFT;
    ciram[(LIST_Y + LIST_H - 1) * 32 + LIST_X + LIST_W - 1] = CORNER_BOTTOM_RIGHT;
}
static bool lit(const uint32_t *px, int col, int row) {
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c)
            if (px[(row * 8 + r) * 256 + col * 8 + c] != native[(row * 8 + r) * 256 + col * 8 + c]) return true;
    return false;
}
static void the_hint(void) {
    on(true);
    for (int ch = 0x21; ch < 0x80; ++ch) cart[13 * 16384 + (ch - 0x20) * 8] = 0x80;   /* a pixel each */
    for (int i = 0; i < 256 * 240; ++i) native[i] = 0xFF123456u;
    char line[HINT_COLS + 1], page[HINT_COLS + 1];
    int w = 256, h = 240;

    static const uint8_t carried[] = { RED_POTION, KEY_J, KEY_Q, KEY_K, MATTOCK, RED_POTION, KEY_A, RED_POTION,
                                       LONG_SWORD, DRAGON_SLAYER };
    carry(0, carried, sizeof carried);
    press("B");
    talk(shops[EOLIS_TOOLS].index);
    /* The script has ended: no hint. */
    CHECK(!hint(line));
    ram[ISCRIPT_PORTRAIT_ID] = 9;
    CHECK(hint(line) && !strcmp(line, "Select to sell") && !page_label(page));
    frame_list(false);
    CHECK(fax_shops_present(NULL, &w, &h) == NULL);           /* the list's frame is not up yet: unchanged */
    frame_list(true);
    const uint32_t *px = fax_shops_present(NULL, &w, &h);
    CHECK(px != native && w == 256 && h == 240);
    CHECK(lit(px, HINT_COL, HINT_ROW) && lit(px, HINT_COL + 13, HINT_ROW) && !lit(px, HINT_COL + 14, HINT_ROW));
    CHECK(!lit(px, HINT_COL, HINT_ROW - 1));

    /* The sell list: two pages, the page over the first entry. */
    press("sB");
    talk(shops[EOLIS_TOOLS].index);
    ram[ISCRIPT_PORTRAIT_ID] = 9;
    CHECK(hint(line) && !strcmp(line, "Select to buy") && page_label(page) && !strcmp(page, "1 of 2"));
    px = fax_shops_present(NULL, &w, &h);
    CHECK(lit(px, HINT_COL + HINT_COLS - 1, PAGE_ROW) && lit(px, HINT_COL + HINT_COLS - 6, PAGE_ROW) &&
          !lit(px, HINT_COL + HINT_COLS - 7, PAGE_ROW));
    /* The first page, from the last of the Items: Red Potion, the A key, ...
     * On the A key, his for good: "Not for sale", and no price. */
    CHECK(ram[DATA_ARRAY + 1] == KEY_A && price(1) == 0 && ram[DATA_ARRAY + 3] == MATTOCK && price(3) == 0);
    uint32_t inside = cyc_render_color(0);
    for (int x = (LIST_X + 13) * 8; x < (LIST_X + 18) * 8; ++x) {
        CHECK(px[(LIST_Y + 5) * 8 * 256 + x] == inside);                  /* the A key's price cells */
        CHECK(px[(LIST_Y + 3) * 8 * 256 + x] == native[0]);               /* the Red Potion's: as drawn */
    }
    ram[MENU_CURSOR_POS] = 1;
    CHECK(hint(line) && !strcmp(line, "Not for sale"));
    ram[MENU_CURSOR_POS] = 0;
    /* On the second page, the cursor on the Dragon Slayer: "Not for sale",
     * and its price cells painted the box's inside. */
    press("sdddddddddB");
    talk(shops[EOLIS_TOOLS].index);
    ram[ISCRIPT_PORTRAIT_ID] = 9;
    CHECK(shop.page == 1 && ram[DATA_ARRAY] == DRAGON_SLAYER);
    ram[MENU_CURSOR_POS] = 0;
    CHECK(hint(line) && !strcmp(line, "Not for sale") && page_label(page) && !strcmp(page, "2 of 2"));
    px = fax_shops_present(NULL, &w, &h);
    for (int x = (LIST_X + 13) * 8; x < (LIST_X + 18) * 8; ++x) CHECK(px[(LIST_Y + 3) * 8 * 256 + x] == inside);
    CHECK(px[(LIST_Y + 5) * 8 * 256 + (LIST_X + 13) * 8] == native[0]);   /* the Long Sword's price: untouched */
    ram[MENU_CURSOR_POS] = 1;
    CHECK(hint(line) && !strcmp(line, "Select to buy"));

    /* Forepaw's buy list with the Wing Boots owned: OWNED in their price's
     * five cells, on the box's inside, and "Already owned" while the cursor
     * is on them. */
    carry(0, (const uint8_t[]){ RED_POTION, WING_BOOTS }, 2);
    press("B");
    talk(shops[FOREPAW_TOOLS].index);
    ram[ISCRIPT_PORTRAIT_ID] = 9;
    ram[MENU_CURSOR_POS] = 3;
    CHECK(hint(line) && !strcmp(line, "Select to sell"));
    ram[MENU_CURSOR_POS] = 4;
    CHECK(hint(line) && !strcmp(line, "Already owned") && !page_label(page));
    px = fax_shops_present(NULL, &w, &h);
    int boots = (LIST_Y + 3 + 2 * 4) * 8 * 256, potion = (LIST_Y + 3 + 2 * 3) * 8 * 256;
    for (int c = 0; c < 5; ++c) {
        int x = (LIST_X + 13 + c) * 8;
        CHECK(px[boots + x] != inside && px[boots + x] != native[0]);   /* each letter's one pixel */
        CHECK(px[boots + 256 + x + 1] == inside && px[boots + 7 * 256 + x + 7] == inside);
        CHECK(px[potion + x] == native[0]);                              /* the Red Potion's price: as drawn */
    }
    CHECK(px[boots + (LIST_X + 12) * 8 + 7] == native[0] && px[boots + (LIST_X + 18) * 8] == native[0]);
    CHECK(lit(px, HINT_COL, HINT_ROW) && lit(px, HINT_COL + 12, HINT_ROW) && !lit(px, HINT_COL + 13, HINT_ROW));
    /* Not without recharging items: the price stays, and so does the hint. */
    recharging_on = false;
    CHECK(hint(line) && !strcmp(line, "Select to sell"));
    px = fax_shops_present(NULL, &w, &h);
    CHECK(px[boots + (LIST_X + 13) * 8] == native[0]);
    recharging_on = true;
    /* The sell list shows no OWNED: the boots are not for sale, with no
     * price, as the Dragon Slayer; the Red Potion under them sells. */
    press("sB");
    talk(shops[FOREPAW_TOOLS].index);
    ram[ISCRIPT_PORTRAIT_ID] = 9;
    CHECK(ram[DATA_ARRAY] == WING_BOOTS && ram[DATA_ARRAY + 1] == RED_POTION);   /* the Items from the last */
    ram[MENU_CURSOR_POS] = 0;
    CHECK(hint(line) && !strcmp(line, "Not for sale"));
    px = fax_shops_present(NULL, &w, &h);
    for (int x = (LIST_X + 13) * 8; x < (LIST_X + 18) * 8; ++x)
        for (int r = 0; r < 8; ++r) CHECK(px[((LIST_Y + 3) * 8 + r) * 256 + x] == inside);   /* no OWNED either */
    CHECK(px[(LIST_Y + 5) * 8 * 256 + (LIST_X + 13) * 8] == native[0]);
    ram[MENU_CURSOR_POS] = 1;
    CHECK(hint(line) && !strcmp(line, "Select to buy"));
    /* In a wider picture (Seamless), the native picture is centered. */
    static uint32_t wide[424 * 240];
    for (int i = 0; i < 424 * 240; ++i) wide[i] = 0xFF654321u;
    w = 424; h = 240;
    px = fax_shops_present(wide, &w, &h);
    CHECK(px != wide && w == 424 && px[(HINT_ROW * 8) * 424 + 84 + HINT_COL * 8] != wide[0]);
    /* Off: nothing. */
    on(false);
    w = 256; h = 240;
    CHECK(fax_shops_present(NULL, &w, &h) == NULL);
}

int main(void) {
    build_bank12();
    CHECK(site_count == 10 && reset_cb && activate_cb && save_get && save_set && save_validate);
    prices();
    straight_to_the_list();
    buying();
    the_games_checks();
    ownership();
    not_for_sale();
    owned_rows();
    selling();
    switching();
    pages_of_eight();
    off();
    isolated_and_saved();
    the_hint();
    potion_sellers();
    printf("shops_test: %u checks passed (%u hook calls)\n", checks, fired);
    return 0;
}
