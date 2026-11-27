/* Unit tests for Quick shops (fax_shops.c), without the emulator or a ROM.
 *
 * The module runs against a model of the machine: CPU RAM, the registers and
 * the stack, bank 12 holding the 17 shops' scripts and lists (written out from
 * the disassembly, at their addresses, with the script table that points at
 * them), the hook sites fired as the scheduler fires them (a handled site
 * returns as an RTS would, from the stack the hook left), and the shop
 * actions played step by step as the disassembly has them: IScripts_Begin,
 * IScripts_InvokeNextAction, ShowMessage, IScriptAction_OpenShop,
 * ShowBuySellMenu and ShowSellMenu, each list's B at its JMP
 * IScriptAction_EndScript, IScripts_WaitForMenuInput and Menu_UpdateAndDraw,
 * the purchase's checks, Player_AddToInventory and Player_RemoveItem, the
 * gold, IScripts_ShowFinalMessage, Maybe_Shop_DrawTextBox (its TextBox_Close
 * and TextBox_OpenForNPC) and EndScript. A conversation is played from a
 * queue of button presses, one per frame the game reads them, so the tests
 * count presses as a player would (the greeting, the Buy/Sell menu, the
 * list's line, the list), and check what the hero ends up with, what the
 * game said, which boxes it drew, and that the stack is where it started. The hint, and an owned item's OWNED, are drawn on a model
 * picture. Reusable tools and recharging items are switches here, and so is
 * whether Found items is on (fax_found.h): off but in found_items(), so the
 * other tests have the tool shops' own lists. */
#include <stdio.h>
static char logged[160];
#define FAX_SHOPS_LOG(...) snprintf(logged, sizeof logged, __VA_ARGS__)
#include "fax_text.c"
#include "fax_shops.c"

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
static uint8_t bank12[0x4000];
/* The features that keep items (fax_keys.c, fax_recharge.c), as switches. */
static bool reusable_on = true, recharging_on = true, spent;
bool fax_reusable_tools_on(void) { return reusable_on; }
bool fax_recharge_on(void) { return recharging_on; }
bool fax_recharge_elixir_spent(void) { return recharging_on && spent; }
/* Found items (fax_found.c), as a switch, with its list (found_test.c checks
 * fax_found.c's own). */
static bool found_on;
bool fax_found_on(void) { return found_on; }
const uint8_t fax_found_unsold[FAX_FOUND_UNSOLD] = { 0x8F, 0x8D, 0x92 };
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
void cyc_mod_set_regs(const CycModRegs *in) {
    cpu.a = in->a;
    cpu.x = in->x;
    cpu.y = in->y;
    cpu.s = in->s;
    cpu.p = in->p;
}
bool cyc_mod_isolated(void) { return isolated; }
size_t cyc_mod_copy_cart_payload(uint8_t *out, size_t capacity) {
    if (out && capacity >= sizeof cart) memcpy(out, cart, sizeof cart);
    return sizeof cart;
}
int cyc_render_line_scroll_x(int y) {
    (void)y;
    return 0;
}
int cyc_render_line_scroll_y(int y) { return y; }
uint8_t cyc_render_nametable(uint16_t a) { return ciram[(a & 0x3FF) | (a & 0x400)]; }
uint16_t cyc_render_line_bg_table(int y) {
    (void)y;
    return 0x1000;
}
uint8_t cyc_render_line_mask(int y) {
    (void)y;
    return 0x1E;
} /* fax_text.c's HUD test: rendering on */
uint8_t cyc_render_chr(uint16_t a) { return chr[a & 0x1FFF]; }
/* Palette entry e as a color whose blue byte is e + 1, so a pixel tells its entry. */
uint32_t cyc_render_color(int e) { return 0xFF000000u | (uint32_t)((e & 31) + 1); }
const uint32_t *cyc_render_present(int *w, int *h) {
    *w = 256;
    *h = 240;
    return native;
}

/* The engine's hook table: registration order, an enabled flag each. */
typedef struct {
    const char *id;
    uint16_t addr;
    NESModFunctionEntryCallback cb;
    int on;
} Site;
static Site sites[16];
static int site_count;
int nes_mod_register_function_entry_plugin(const char *id, uint16_t addr, NESModFunctionEntryCallback cb) {
    if (site_count == 16) return 0;
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
static NESModActivationCallback reset_cb, activate_cb;
int nes_mod_register_reset_callback(NESModActivationCallback cb) {
    reset_cb = cb;
    return 1;
}
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    if (strcmp(id, "faxanadu.shops")) return 0;
    activate_cb = cb;
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
    if (strcmp(id, "faxanadu.shops")) return 0;
    save_get = get;
    save_set = set;
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
        if (sites[i].addr == addr && sites[i].on) {
            ++fired;
            if (sites[i].cb(addr)) return 1;
        }
    return 0;
}
static void push(uint8_t v) {
    ram[0x100 | cpu.s] = v;
    cpu.s = (uint8_t)(cpu.s - 1);
}
static uint8_t pull(void) {
    cpu.s = (uint8_t)(cpu.s + 1);
    return ram[0x100 | cpu.s];
}
static void jsr(uint16_t last) {
    push((uint8_t)(last >> 8));
    push((uint8_t)last);
} /* the JSR's last byte */
static uint16_t rts(void) {
    uint8_t lo = pull();
    return (uint16_t)((lo | pull() << 8) + 1);
}

/* ---- bank 12: the shops, from the disassembly ---- */
enum {
    HAND_DAGGER = 0x00,
    LONG_SWORD = 0x01,
    GIANT_BLADE = 0x02,
    DRAGON_SLAYER = 0x03,
    STUDDED_MAIL = 0x21,
    FULL_PLATE = 0x22,
    BATTLE_SUIT = 0x23,
    SMALL_SHIELD = 0x40,
    LARGE_SHIELD = 0x41,
    MAGIC_SHIELD = 0x42,
    BATTLE_HELMET = 0x43,
    DELUGE = 0x60,
    THUNDER = 0x61,
    FIRE = 0x62,
    DEATH = 0x63,
    TILTE = 0x64,
    RING_OF_ELF = 0x80,
    KEY_A = 0x84,
    KEY_K = 0x85,
    KEY_Q = 0x86,
    KEY_J = 0x87,
    KEY_JO = 0x88,
    MATTOCK = 0x89,
    MAGICAL_ROD = 0x8A,
    HOUR_GLASS = 0x8D,
    WING_BOOTS = 0x8F,
    RED_POTION = 0x90,
    POISON = 0x91,
    ELIXIR = 0x92,
    PENDANT = 0x93,
    BLACK_ONYX = 0x94
};
enum { TOOLS = 0x89, KEYS = 0x8A, I_SELL_TOOLS = 0x0F, I_SELL_KEYS = 0x0E };
typedef struct {
    uint8_t index;
    uint16_t script, items;
    uint8_t portrait;
    Offer list[5];
} Shop;
static const Shop shops[] = {
    { 13, 0xA37A, 0xA42E, TOOLS, { { HAND_DAGGER, 400 }, { RED_POTION, 160 }, { ELIXIR, 320 }, { DELUGE, 400 } } },
    { 25,
      0xA38C,
      0xA43B,
      TOOLS,
      { { SMALL_SHIELD, 800 }, { HAND_DAGGER, 500 }, { DELUGE, 500 }, { RED_POTION, 300 } } },
    { 29, 0xA39E, 0xA448, TOOLS, { { MAGIC_SHIELD, 7600 }, { DEATH, 12500 } } },
    { 40,
      0xA3B0,
      0xA44F,
      TOOLS,
      { { LONG_SWORD, 1600 },
        { STUDDED_MAIL, 2500 },
        { SMALL_SHIELD, 1100 },
        { RED_POTION, 400 },
        { WING_BOOTS, 2800 } } },
    { 57, 0xA3C2, 0xA45F, TOOLS, { { LARGE_SHIELD, 3500 }, { THUNDER, 1200 }, { MATTOCK, 800 }, { RED_POTION, 600 } } },
    { 60, 0xA3D4, 0xA46C, TOOLS, { { DEATH, 9800 }, { HOUR_GLASS, 5600 }, { ELIXIR, 4300 }, { RED_POTION, 300 } } },
    { 72, 0xA3E6, 0xA479, TOOLS, { { FULL_PLATE, 5200 }, { MATTOCK, 1200 }, { RED_POTION, 800 } } },
    { 87,
      0xA3F8,
      0xA483,
      TOOLS,
      { { GIANT_BLADE, 8500 }, { MAGIC_SHIELD, 9800 }, { WING_BOOTS, 5500 }, { RED_POTION, 1000 } } },
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
enum {
    SHOPS = sizeof shops / sizeof shops[0],
    EOLIS_TOOLS = 0,
    APOLUNE_TOOLS = 1,
    FOREPAW_TOOLS = 3,
    DARTMOOR_TOOLS = 9,
    EOLIS_KEYS = 10
};
enum { MEAT_SHOP_INDEX = 11, MEAT_SHOP = 0xA4A2 }; /* a shop of another layout: 88, question, pay, add HP */

static void rom_put(uint16_t at, const uint8_t *bytes, unsigned n) { memcpy(bank12 + (at - 0x8000), bytes, n); }
static void build_bank12(void) {
    memset(bank12, 0xFF, sizeof bank12);
    for (unsigned i = 0; i < ISCRIPTS; ++i) { /* every other script: a message and an end */
        bank12[ISCRIPT_ADDRS_L - 0x8000 + i] = 0x00;
        bank12[ISCRIPT_ADDRS_U - 0x8000 + i] = 0xB0;
    }
    static const uint8_t plain[] = { 0x00, SHOW_MESSAGE, 0x39, 0x00 };
    rom_put(0xB000, plain, sizeof plain);
    for (unsigned k = 0; k < SHOPS; ++k) {
        const Shop *s = &shops[k];
        uint16_t buy = (uint16_t)(s->script + 12), it = s->items;
        const uint8_t script[] = { s->portrait,        SHOW_MESSAGE, s->portrait == TOOLS ? I_SELL_TOOLS : I_SELL_KEYS,
                                   SHOW_BUY_SELL_MENU, (uint8_t)buy, (uint8_t)(buy >> 8),
                                   SHOW_SELL_MENU,     (uint8_t)it,  (uint8_t)(it >> 8),
                                   SHOW_MESSAGE,       0x02,         0x00,
                                   OPEN_SHOP_ACTION,   (uint8_t)it,  (uint8_t)(it >> 8),
                                   SHOW_MESSAGE,       0x02,         0x00 };
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
enum { GOLD = 0x0392, SELECTED = 0x03BD, MESSAGE_ID = 0x0213 }; /* SPECIAL_ITEMS: fax_shops.c's */
static const uint8_t special_ids[8] = { 0x80, 0x81, 0x82, 0x83, 0x92, 0x8A, 0x93, 0x94 };
static const uint8_t special_bits[8] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 };
static int special(uint8_t id) {
    for (int i = 0; i < 8; ++i)
        if (special_ids[i] == id) return i;
    return -1;
}
static unsigned gold(void) { return ram[GOLD] | ram[GOLD + 1] << 8 | ram[GOLD + 2] << 16; }
static void set_gold(unsigned g) {
    ram[GOLD] = (uint8_t)g;
    ram[GOLD + 1] = (uint8_t)(g >> 8);
    ram[GOLD + 2] = (uint8_t)(g >> 16);
}
static void carry(unsigned g, const uint8_t *ids, unsigned n) {
    for (int c = 0; c < CATEGORIES; ++c) {
        ram[NUMBER_OF_WEAPONS + c] = 0;
        memset(ram + list_at[c], 0xEE, list_max[c]); /* stale bytes past the counts */
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
static uint8_t said[32]; /* message IDs, in order */
static unsigned said_count, draws, closes, opens, sounds, stuck;
static char events[256]; /* d: list drawn, c: list closed, o: dialogue box
                                                                opened, m: a message */
static bool done;

static void event(char e) {
    size_t n = strlen(events);
    if (n + 1 < sizeof events) events[n] = e, events[n + 1] = 0;
}
static void press(const char *buttons) { /* one press per character, a frame each */
    input_count = input_at = 0;
    for (; *buttons; ++buttons)
        inputs[input_count++] = *buttons == 'A'   ? 0x80
                                : *buttons == 'B' ? 0x40
                                : *buttons == 's' ? 0x20
                                : *buttons == 'u' ? 0x08
                                : *buttons == 'd' ? 0x04
                                                  : 0;
}
/* The pad, read at the next frame: false when the player has no more presses. */
static bool next_frame(void) {
    if (input_at == input_count) {
        ++stuck;
        return false;
    }
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
    ram[TEXTBOX_X] = x;
    ram[TEXTBOX_X + 1] = y;
    ram[TEXTBOX_X + 2] = w;
    ram[TEXTBOX_X + 3] = h;
    ram[TEXTBOX_CLOSED] = 0;
}
/* A message typed out, and the button that goes on: true for A. */
static void say(uint8_t id) { /* IScripts_LoadAndShowMessage: no wait */
    said[said_count++ & 31] = id;
    ram[MESSAGE_ID] = id;
    event('m');
}
static bool message(uint8_t id) {
    say(id);
    if (!next_frame()) return false;
    return ram[JOY1_CHANGED_BUTTON_MASK] & 0x80;
}
static void shop_draw(void) {
    text_box(LIST_X, LIST_Y, LIST_W, LIST_H);
    ram[MENU_CURSOR_POS] = 0;
    ++draws;
    event('d');
}
static void close_list(void) {
    ram[TEXTBOX_CLOSED] = 1;
    ++closes;
    event('c');
} /* TextBox_Close */
static void open_npc(void) {
    text_box(10, 16, 20, 8);
    ++opens;
    event('o');
} /* TextBox_OpenForNPC */
static bool has_already(uint8_t id) { /* Shop_GetPlayerHasSelectedItem */
    int k = special(id);
    if (k >= 0) return ram[SPECIAL_ITEMS] & special_bits[k];
    if (id & 0x80) return false;
    unsigned c = id >> 5;
    return ram[SELECTED + c] == (id & 0x1F) || count_of(id) > 0;
}
static void remove_item(uint8_t id) { /* Player_RemoveItem */
    int k = special(id);
    if (k >= 0) {
        ram[SPECIAL_ITEMS] &= (uint8_t)~special_bits[k];
        return;
    }
    unsigned c = id >> 5, n = ram[NUMBER_OF_WEAPONS + c];
    for (unsigned i = 0; i < n; ++i)
        if (ram[list_at[c] + i] == (id & 0x1F)) {
            memmove(ram + list_at[c] + i, ram + list_at[c] + i + 1, n - i - 1);
            ram[NUMBER_OF_WEAPONS + c] = (uint8_t)(n - 1);
            return;
        }
}
static unsigned price(unsigned i) { return ram[SHOP_ITEM_COSTS_L + i] | ram[SHOP_ITEM_COSTS_U + i] << 8; }

/* Up on a shopkeeper: IScripts_Begin with script `index`, played to its end,
 * where the stack is back where it was; or until the player has no more
 * presses (`stuck`), the game left waiting where it was, a list up, say. */
static void talk(unsigned index) {
    uint16_t base =
        (uint16_t)(bank12[ISCRIPT_ADDRS_L - 0x8000 + index] | bank12[ISCRIPT_ADDRS_U - 0x8000 + index] << 8);
    said_count = draws = closes = opens = sounds = stuck = 0;
    events[0] = 0;
    done = false;
    cpu = (CycModRegs){ .s = 0xF0 };
    ram[ISCRIPT_ADDR] = (uint8_t)base;
    ram[ISCRIPT_ADDR + 1] = (uint8_t)(base >> 8);
    ram[ISCRIPT_OFFSET] = 1;
    ram[ISCRIPT_PORTRAIT_ID] = bank12[base - 0x8000] & 0x7F;
    text_box(10, 16, 20, 8);
    uint16_t pc = INVOKE_NEXT_ACTION, items = 0;
    bool carry_set = false;
    for (int steps = 0; !done && steps < 100000; ++steps) {
        switch (pc) {
        case INVOKE_NEXT_ACTION: {
            if (fire(pc)) {
                pc = rts();
                break;
            }
            uint8_t action = load_byte();
            pc = action == 0x00                 ? 0x82B4
                 : action == SHOW_MESSAGE       ? 0x82D8
                 : action == OPEN_SHOP_ACTION   ? OPEN_SHOP
                 : action == SHOW_BUY_SELL_MENU ? 0x8630
                 : action == SHOW_SELL_MENU     ? 0x8660
                                                : 0x82B4;
            break;
        }
        case 0x82B4: /* IScriptAction_EndScript */
            ram[ISCRIPT_PORTRAIT_ID] = 0xFF;
            ram[TEXTBOX_CLOSED] = 1;
            done = true;
            break;
        case 0x82D8: { /* IScriptAction_ShowMessage: B ends the script */
            uint8_t id = load_byte();
            pc = message(id) ? INVOKE_NEXT_ACTION : 0x82B4;
            break;
        }
        case 0x8630: /* IScriptAction_ShowBuySellMenu */
            say(0x10);
            ram[ARG_STRINGS_COUNT] = 2;
            ram[MENU_CURSOR_POS] = 0;
            for (;;) {
                if (!next_frame()) {
                    done = true;
                    break;
                }
                uint8_t m = ram[JOY1_CHANGED_BUTTON_MASK];
                if (m & 0x0C) ram[MENU_CURSOR_POS] ^= 1;
                if (m & 0x80) break;
                if (m & 0x40) {
                    pc = 0x82B4;
                    break;
                }
            }
            if (done || pc == 0x82B4) break;
            if (ram[MENU_CURSOR_POS] == 0) { /* IScripts_JumpToNextAddr */
                uint8_t lo = load_byte(), hi = load_byte();
                ram[ISCRIPT_ADDR] = lo;
                ram[ISCRIPT_ADDR + 1] = hi;
                ram[ISCRIPT_OFFSET] = 0;
            } else {
                load_byte();
                load_byte();
            }
            pc = INVOKE_NEXT_ACTION;
            break;
        case OPEN_SHOP:
            if (fire(pc)) {
                pc = rts();
                break;
            }
            pc = message(0x14) ? OPEN_SHOP_LIST : BUY_END;
            break;
        case OPEN_SHOP_LIST: { /* the items, Shop_Populate, Shop_Draw */
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
            CHECK(!fire(OPEN_SHOP_DRAW)); /* JSR Shop_Draw: only writes the list */
            shop_draw();
            pc = BUY_WAIT;
            break;
        }
        case BUY_WAIT:
            jsr(0x83FC);
            pc = 0x84ED;
            break;   /* JSR IScripts_WaitForMenuInput */
        case 0x84ED: /* IScripts_WaitForMenuInput: the frame */
            if (!next_frame()) {
                done = true;
                break;
            }
            pc = MENU_UPDATE;
            break;
        case MENU_UPDATE: {
            if (fire(pc)) {
                pc = rts();
                break;
            }
            uint8_t m = ram[JOY1_CHANGED_BUTTON_MASK], n = ram[ARG_STRINGS_COUNT];
            if (m & 0x08) ram[MENU_CURSOR_POS] = (uint8_t)(ram[MENU_CURSOR_POS] ? ram[MENU_CURSOR_POS] - 1 : n - 1);
            else if (m & 0x04)
                ram[MENU_CURSOR_POS] = (uint8_t)(ram[MENU_CURSOR_POS] + 1 == n ? 0 : ram[MENU_CURSOR_POS] + 1);
            if (m & 0x80) {
                carry_set = false;
                pc = rts();
            } else if (m & 0x40) {
                carry_set = true;
                pc = rts();
            } else pc = 0x84ED;
            break;
        }
        case 0x83FD:
            pc = carry_set ? BUY_END : BUY_PICK;
            break;    /* OpenShop, after the menu: BCC, JMP EndScript */
        case BUY_END: /* JMP IScriptAction_EndScript: B */
        case SELL_END:
            if (fire(pc)) {
                pc = rts();
                break;
            }
            pc = 0x82B4;
            break;
        case BUY_PICK: { /* A: Shop_GetPlayerHasSelectedItem, ... */
            if (fire(pc)) {
                pc = rts();
                break;
            }
            unsigned x = ram[MENU_CURSOR_POS];
            uint8_t id = ram[DATA_ARRAY + x];
            if (has_already(id)) {
                pc = 0x8469;
                break;
            }
            if (special(id) < 0) {
                unsigned c = id >> 5;
                if (ram[NUMBER_OF_WEAPONS + c] == list_max[c]) {
                    pc = 0x8469;
                    break;
                }
            }
            if (gold() < price(x)) {
                jsr(0x8453);
                pc = MAYBE_SHOP_DRAW_TEXTBOX;
                break;
            }
            int k = special(id);
            if (k >= 0) ram[SPECIAL_ITEMS] |= special_bits[k];
            else {
                unsigned c = id >> 5;
                ram[list_at[c] + ram[NUMBER_OF_WEAPONS + c]++] = id & 0x1F;
            }
            set_gold(gold() - price(x)); /* IScripts_ProgressivelySubtractGold */
            pc = BOUGHT;
            break;
        }
        case 0x8454:
            pc = 0x837C;
            break; /* JMP IScripts_ShowNotEnoughMoney */
        case BOUGHT:
            if (fire(pc)) {
                pc = rts();
                break;
            }
            jsr(0x8465); /* JSR Maybe_Shop_DrawTextBox */
            pc = MAYBE_SHOP_DRAW_TEXTBOX;
            break;
        case 0x8466:
            pc = INVOKE_NEXT_ACTION;
            break;
        case 0x8469:
            jsr(0x846B);
            pc = MAYBE_SHOP_DRAW_TEXTBOX;
            break;
        case 0x846C:
            cpu.a = 0x06;
            pc = 0x837E;
            break; /* LDA #$06, JMP IScripts_ShowFinalMessage */
        case 0x837C:
            cpu.a = 0x03;
            pc = 0x837E;
            break; /* IScripts_ShowNotEnoughMoney */
        case 0x837E:
            message(cpu.a);
            pc = FINAL_MESSAGE_END;
            break;
        case FINAL_MESSAGE_END:
            if (fire(pc)) {
                pc = rts();
                break;
            }
            pc = 0x82B4;
            break;
        case MAYBE_SHOP_DRAW_TEXTBOX:
            pc = MAYBE_SHOP_CLOSE;
            break;             /* two frames: no input read */
        case MAYBE_SHOP_CLOSE: /* JSR TextBox_Close */
            if (fire(pc)) {
                pc = rts();
                break;
            }
            close_list();
            pc = MAYBE_SHOP_OPEN;
            break;
        case MAYBE_SHOP_OPEN:
            open_npc();
            pc = rts();
            break; /* JMP TextBox_OpenForNPC */
        case SOUND_PLAY_EFFECT:
            sounds += cpu.a == SOUND_CURSOR;
            pc = rts();
            break;
        case 0x8660: { /* ShowSellMenu: the items, the stock scan */
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
            if (fire(pc)) {
                pc = rts();
                break;
            }
            if (!ram[ARG_STRINGS_COUNT]) {
                cpu.a = 0x12;
                pc = 0x837E;
                break;
            }
            pc = message(0x13) ? SELL_DRAW : SELL_END;
            break;
        case SELL_DRAW:
            shop_draw();
            pc = SELL_WAIT;
            break;
        case SELL_WAIT:
            jsr(0x86DD);
            pc = 0x84ED;
            break;
        case 0x86DE:
            pc = carry_set ? SELL_END : SELL_PICK;
            break;
        case SELL_PICK: {
            if (fire(pc)) {
                pc = rts();
                break;
            }
            unsigned x = ram[MENU_CURSOR_POS];
            unsigned amount = price(x);
            remove_item(ram[DATA_ARRAY + x]);
            set_gold(gold() + amount); /* IScripts_ProgressivelyAddGold */
            pc = SOLD;
            break;
        }
        case SOLD:
            if (fire(pc)) {
                pc = rts();
                break;
            }
            close_list(); /* JSR TextBox_Close, JSR TextBox_OpenForNPC */
            open_npc();
            pc = INVOKE_NEXT_ACTION;
            break;
        default:
            fprintf(stderr, "the model has no code at $%04X\n", pc);
            exit(1);
        }
    }
    CHECK(done);
    if (stuck) return; /* the player stopped: the game waits, mid-routine */
    if (cpu.s != 0xF0)
        fprintf(stderr, "talk(%u): S=%02X events=%s said=%u stuck=%u\n", index, cpu.s, events, said_count, stuck);
    CHECK(cpu.s == 0xF0); /* every jump kept the stack where the game's would */
}
static void on(bool shops_on) {
    reset_cb();
    if (shops_on) activate_cb();
}
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
    CHECK(fax_shops_sell_price(KEY_J, 140, 100) == 70);
    CHECK(fax_shops_sell_price(KEY_J, 0, 100) == 50);
    CHECK(fax_shops_sell_price(KEY_A, 0, 0) == 100);
    CHECK(fax_shops_sell_price(FIRE, 0, 0) == 100);
    CHECK(fax_shops_sell_price(LONG_SWORD, 1600, 1600) == 800);
    CHECK(fax_shops_sell_price(DEATH, 12500, 9800) == 6250);
    /* The Red Potion: half the lowest, whatever this shop asks (#176). */
    CHECK(fax_shops_sell_price(RED_POTION, 160, 160) == 80);
    CHECK(fax_shops_sell_price(RED_POTION, 2000, 160) == 80);
    CHECK(fax_shops_sell_price(RED_POTION, 0, 160) == 80);
    CHECK(fax_shops_sell_price(RED_POTION, 0, 165) == 80);  /* the gold counts by 10s */
    CHECK(fax_shops_sell_price(RED_POTION, 300, 0) == 150); /* no lowest (no shop's): this shop's half */
    CHECK(fax_shops_sell_price(POISON, 0, 0) == 100);       /* the Items' next: no such rule */
    /* Never: the last fight's gear, and every SpecialItems bit. */
    static const uint8_t never[] = { DRAGON_SLAYER, BATTLE_SUIT, BATTLE_HELMET, 0x80,    0x81,      0x82,
                                     0x83,          MAGICAL_ROD, ELIXIR,        PENDANT, BLACK_ONYX };
    for (unsigned i = 0; i < sizeof never; ++i) {
        CHECK(fax_shops_never_buys(never[i]));
        CHECK(fax_shops_sell_price(never[i], 320, 320) == 0);
    }
    for (int k = 0; k < 8; ++k) CHECK(fax_shops_never_buys(special_ids[k]));
    static const uint8_t sells[] = { HAND_DAGGER,  LONG_SWORD,   GIANT_BLADE,  0x20,       STUDDED_MAIL, FULL_PLATE,
                                     SMALL_SHIELD, LARGE_SHIELD, MAGIC_SHIELD, DELUGE,     THUNDER,      FIRE,
                                     DEATH,        TILTE,        KEY_A,        KEY_K,      KEY_Q,        KEY_J,
                                     KEY_JO,       MATTOCK,      HOUR_GLASS,   WING_BOOTS, RED_POTION,   POISON };
    for (unsigned i = 0; i < sizeof sells; ++i) CHECK(!fax_shops_never_buys(sells[i]));

    /* The lowest prices, read from the shops' scripts. */
    priced = price_everything();
    CHECK(priced);
    CHECK(lowest[RED_POTION] == 160 && lowest[KEY_J] == 100 && lowest[KEY_Q] == 500 && lowest[KEY_K] == 1200);
    CHECK(lowest[GIANT_BLADE] == 8500 && lowest[MATTOCK] == 800 && lowest[WING_BOOTS] == 2800);
    CHECK(lowest[DEATH] == 9800 && lowest[MAGIC_SHIELD] == 7600 && lowest[ELIXIR] == 320);
    CHECK(!lowest[KEY_A] && !lowest[KEY_JO] && !lowest[FIRE] && !lowest[DRAGON_SLAYER] && !lowest[POISON]);
    CHECK(!lowest[0x00 + 0x20]); /* the Leather Armor: no shop */
    /* Every shop's script is found, and nothing else. */
    int found = 0;
    for (int i = 0; i < ISCRIPTS; ++i) {
        Script s;
        found += shop_script(
            (uint16_t)(bank12[ISCRIPT_ADDRS_L - 0x8000 + i] | bank12[ISCRIPT_ADDRS_U - 0x8000 + i] << 8), &s);
    }
    CHECK(found == SHOPS);

    /* No way to make gold by trading: no shop pays for an item what any shop
     * asks for it. Not the keys, which a key shop buys for half its own price,
     * more than Eolis's asks, but which reusable tools keep for good, so no
     * shop buys them (fax_shops_not_for_sale). The Red Potion, which a tool
     * shop asks 160 to 2000 for, pays 80 at every shop, where the original
     * paid up to 1000 (#176). Every price is a multiple of 10. */
    unsigned potions = 0;
    for (unsigned s = 0; s < SHOPS; ++s)
        for (unsigned t = 0; t < SHOPS; ++t)
            for (unsigned j = 0; j < 5 && shops[t].list[j].price; ++j) {
                uint8_t id = shops[t].list[j].id;
                uint16_t here = 0;
                for (unsigned k = 0; k < 5 && shops[s].list[k].price; ++k)
                    if (shops[s].list[k].id == id) here = shops[s].list[k].price;
                uint16_t pay = fax_shops_sell_price(id, here, lowest[id]);
                CHECK(pay % 10 == 0);
                if (id < KEY_A || id > KEY_JO) CHECK(pay < shops[t].list[j].price);
                if (id == RED_POTION) {
                    CHECK(pay == 80);
                    ++potions;
                }
            }
    CHECK(potions == SHOPS * 9); /* nine tool shops sell one */
}

/* The presses to a list: A on the greeting, the Buy/Sell menu's choice (A on
 * Buy, Down and A on Sell), A on the list's line. B leaves a list; after a
 * purchase or a sale, A on "Thank you for shopping." then ends it. */
#define TO_BUY  "AAA"
#define TO_SELL "AdAA"

static void the_conversation(void) {
    on(true);
    /* Eolis's tool shop: the greeting, Buy/Sell, "What would you like?", the
     * list. B, nothing bought: the end there, as the original's B. */
    carry(700, (const uint8_t[]){ KEY_J }, 1);
    press(TO_BUY "B");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(said_count == 3 && said[0] == I_SELL_TOOLS && said[1] == 0x10 && said[2] == 0x14);
    CHECK(!strcmp(events, "mmmd") && input_at == 4 && !stuck && thanked == 0); /* no close, no box after */
    CHECK(listed((const uint8_t[]){ HAND_DAGGER, RED_POTION, ELIXIR, DELUGE }, (const uint16_t[]){ 400, 160, 320, 400 },
                 4));
    CHECK(ram[ISCRIPT_PORTRAIT_ID] == 0xFF && gold() == 700 && !shop.traded);
    /* Every shop, the key shops too, with its own greeting. */
    for (unsigned k = 0; k < SHOPS; ++k) {
        press(TO_BUY "B");
        talk(shops[k].index);
        CHECK(said_count == 3 && draws == 1 && ram[DATA_ARRAY] == shops[k].list[0].id && !stuck);
        CHECK(said[0] == (shops[k].portrait == TOOLS ? I_SELL_TOOLS : I_SELL_KEYS) && said[2] == 0x14);
    }
    /* Sell: Down on the menu, "Which one would like to sell?", the list. */
    carry(700, (const uint8_t[]){ RED_POTION }, 1);
    press(TO_SELL "B");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(said_count == 3 && said[2] == 0x13 && !strcmp(events, "mmmd") && !stuck && sounds == 0);
    CHECK(listed((const uint8_t[]){ RED_POTION }, (const uint16_t[]){ 80 }, 1));
    /* B on the greeting, on the menu or on a list's line: the end, as the original's. */
    static const char *const early[] = { "B", "AB", "AAB", "AdAB" };
    for (unsigned k = 0; k < 4; ++k) {
        press(early[k]);
        talk(shops[EOLIS_TOOLS].index);
        CHECK(said_count == k + (k == 3 ? 0 : 1) && draws == 0 && !stuck && thanked == 0);
        CHECK(ram[ISCRIPT_PORTRAIT_ID] == 0xFF && !strchr(events, 'o'));
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
    /* Down to the Red Potion, A three times: three purchases in a row, the
     * list drawn again in place each time; then B: the dialogue box opens over
     * the list, never closed, and he thanks you. */
    press(TO_BUY "dAAABA");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(input_at == 9 && !stuck && said_count == 4 && said[3] == 0x02);
    CHECK(gold() == 700 - 3 * 160 && count_of(RED_POTION) == 3 && count_of(KEY_J) == 1);
    CHECK(!strcmp(events, "mmmddddom") && closes == 0 && boxes_kept == 1);
    CHECK(bought == 3 && thanked == 1 && ram[ISCRIPT_PORTRAIT_ID] == 0xFF);
    /* The Elixir: bought once, then it is owned, and A on it does nothing:
     * no gold, no line, the list as it was. */
    carry(700, NULL, 0);
    press(TO_BUY "ddAAABA");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(gold() == 700 - 320 && (ram[SPECIAL_ITEMS] & 0x08));
    CHECK(said_count == 4 && !strcmp(events, "mmmddom") && ignored == 2 && input_at == 10 && !stuck);
}

static void the_games_checks(void) {
    on(true);
    /* Not enough Golds: the game's line, in the dialogue box opened over the
     * list, then the list again, the cursor where it was and no line before
     * it; a purchase; B, and the thanks. */
    carry(300, NULL, 0);
    press(TO_BUY "dddAAuuABA");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(said_count == 5 && said[3] == 0x03 && said[4] == 0x02);
    CHECK(!strcmp(events, "mmmdomddom") && closes == 0);
    CHECK(gold() == 300 - 160 && count_of(RED_POTION) == 1 && count_of(DELUGE) == 0);
    CHECK(input_at == 13 && !stuck && messages == 1);
    /* The Hand Dagger in hand is owned (#165): A does nothing, where the game
     * said "You can't carry any more."; B, nothing bought: no thanks. */
    on(true);
    carry(1000, NULL, 0);
    press(TO_BUY "AAB");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(said_count == 3 && gold() == 1000 && ignored == 2 && thanked == 0 && !stuck && count_of(HAND_DAGGER) == 0);
    /* A full Items list: the same line for a Red Potion. */
    static const uint8_t eight[] = { KEY_J, KEY_J, KEY_J, KEY_J, KEY_Q, KEY_Q, KEY_Q, MATTOCK };
    carry(1000, eight, 8);
    press(TO_BUY "dAAB");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(said_count == 4 && said[3] == 0x06 && gold() == 1000 && count_of(RED_POTION) == 0);
    /* The Elixir with one in hand is owned: A does nothing, where the game
     * would refuse it. */
    on(true);
    carry(1000, NULL, 0);
    ram[SPECIAL_ITEMS] = 0x08;
    press(TO_BUY "ddAAB");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(said_count == 3 && gold() == 1000 && ignored == 2 && !strcmp(events, "mmmd"));
}

/* fax_shops_owned, on plain values: each item, held or selected, with the
 * feature that keeps it on or off. */
static void ownership(void) {
    FaxShopsHero none = {
        .selected = 0xFF, .recharging = true, .reusable = true, .gear_selected = { 0xFF, 0xFF, 0xFF, 0xFF }
    };
    static const uint8_t kept[] = { WING_BOOTS, HOUR_GLASS, KEY_A, KEY_K, KEY_Q, KEY_J, KEY_JO, MATTOCK };
    for (unsigned k = 0; k < sizeof kept; ++k) {
        uint8_t id = kept[k];
        bool by_recharge = id == WING_BOOTS || id == HOUR_GLASS;
        FaxShopsHero h = none;
        CHECK(!fax_shops_owned(id, &h)); /* not his */
        h.items[0] = RED_POTION & 0x1F;
        h.items[1] = id & 0x1F;
        h.count = 2;
        CHECK(fax_shops_owned(id, &h)); /* in his list */
        h.count = 1;
        CHECK(!fax_shops_owned(id, &h)); /* a stale byte past the count */
        h = none;
        h.selected = id & 0x1F;
        CHECK(fax_shops_owned(id, &h)); /* selected, in no list */
        if (by_recharge) h.recharging = false;
        else h.reusable = false;
        CHECK(!fax_shops_owned(id, &h)); /* used up without its feature */
        if (by_recharge) h.reusable = false, h.recharging = true;
        else h.recharging = false, h.reusable = true;
        CHECK(fax_shops_owned(id, &h));                    /* the other feature does not matter */
        CHECK(!fax_shops_owned((uint8_t)(id & 0x1F), &h)); /* the same ID in the Weapons: no */
    }
    /* The Elixir: charged (its SpecialItems bit) or spent, whatever is on. */
    FaxShopsHero h = none;
    CHECK(!fax_shops_owned(ELIXIR, &h));
    h.special = 0x08;
    CHECK(fax_shops_owned(ELIXIR, &h));
    h.recharging = h.reusable = false;
    CHECK(fax_shops_owned(ELIXIR, &h));
    h = none;
    h.elixir_spent = true;
    CHECK(fax_shops_owned(ELIXIR, &h));
    h = none;
    h.special = 0xF7; /* every other bit */
    CHECK(!fax_shops_owned(ELIXIR, &h));
    /* Anything else never is: a Red Potion or the Poison held, the other
     * special items, and gear he does not have (gear()). */
    static const uint8_t never[] = { RED_POTION, POISON, HAND_DAGGER, LONG_SWORD,  STUDDED_MAIL, SMALL_SHIELD,
                                     DELUGE,     DEATH,  RING_OF_ELF, MAGICAL_ROD, PENDANT,      BLACK_ONYX };
    for (unsigned k = 0; k < sizeof never; ++k) {
        h = none;
        h.selected = never[k] & 0x1F;
        h.items[0] = never[k] & 0x1F;
        h.count = 1;
        h.special = 0xFF;
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
        h.items[0] = RED_POTION & 0x1F;
        h.items[1] = id & 0x1F;
        h.count = 2;
        CHECK(fax_shops_not_for_sale(id, &h));          /* his for good */
        CHECK(!fax_shops_not_for_sale(RED_POTION, &h)); /* the potion beside it sells */
        if (id == WING_BOOTS || id == HOUR_GLASS) h.recharging = false;
        else h.reusable = false;
        CHECK(!fax_shops_not_for_sale(id, &h)); /* used up without its feature: it sells */
    }
    /* What no shop buys, whatever the hero has and whatever is on. */
    static const uint8_t never[] = { DRAGON_SLAYER, BATTLE_SUIT, BATTLE_HELMET, 0x80,    0x81,      0x82,
                                     0x83,          MAGICAL_ROD, ELIXIR,        PENDANT, BLACK_ONYX };
    for (unsigned k = 0; k < sizeof never; ++k) {
        FaxShopsHero h = { .selected = 0xFF };
        CHECK(fax_shops_not_for_sale(never[k], &h));
    }
    /* The rest sells: potions, the Poison, weapons, armor, shields and spells. */
    static const uint8_t sells[] = { RED_POTION,   POISON,       HAND_DAGGER, LONG_SWORD,
                                     STUDDED_MAIL, SMALL_SHIELD, DELUGE,      DEATH };
    for (unsigned k = 0; k < sizeof sells; ++k) {
        FaxShopsHero h = none;
        h.items[0] = sells[k] & 0x1F;
        h.count = 1;
        CHECK(!fax_shops_not_for_sale(sells[k], &h));
    }
}

/* A weapon, armor, shield or spell he has, equipped or in its list, is
 * owned (#165): a second copy does nothing. Sold, it is not; a different
 * piece never is; and it still sells. */
static void gear(void) {
    FaxShopsHero none = {
        .selected = 0xFF, .recharging = true, .reusable = true, .gear_selected = { 0xFF, 0xFF, 0xFF, 0xFF }
    };
    static const uint8_t each[] = { HAND_DAGGER,  GIANT_BLADE,  STUDDED_MAIL, FULL_PLATE,
                                    SMALL_SHIELD, MAGIC_SHIELD, DELUGE,       TILTE };
    for (unsigned k = 0; k < sizeof each; ++k) {
        uint8_t id = each[k], c = id >> 5, entry = id & 0x1F, other = (uint8_t)(entry ^ 1);
        FaxShopsHero h = none;
        CHECK(!fax_shops_owned(id, &h)); /* neither */
        h.gear_selected[c] = entry;
        CHECK(fax_shops_owned(id, &h));                         /* equipped, in no list */
        CHECK(!fax_shops_owned((uint8_t)(c << 5 | other), &h)); /* a different piece: for sale */
        for (unsigned d = 0; d < 4; ++d)
            if (d != c) CHECK(!fax_shops_owned((uint8_t)(d << 5 | entry), &h)); /* another kind's same entry */
        CHECK(!fax_shops_owned((uint8_t)(0x80 | entry), &h));                   /* the Items' same entry */
        h = none;
        h.gear[c][0] = other;
        h.gear[c][1] = entry;
        h.gear_count[c] = 2;
        CHECK(fax_shops_owned(id, &h)); /* carried, in its list */
        h.gear_count[c] = 1;
        CHECK(!fax_shops_owned(id, &h)); /* just sold: a stale byte past the count */
        h.gear_count[c] = 2;
        CHECK(!fax_shops_not_for_sale(id, &h)); /* owned, and still for sale */
        h.reusable = h.recharging = false;
        CHECK(fax_shops_owned(id, &h)); /* whatever keeps items */
    }

    /* In a shop. Eolis's: Hand Dagger, Red Potion, Elixir, Deluge. The Giant
     * Blade in hand: the Hand Dagger, weaker, is for sale; bought, it is
     * owned, and A on it does nothing, nor on the Deluge once bought. */
    on(true);
    carry(2000, NULL, 0);
    ram[0x03BD] = GIANT_BLADE;
    press(TO_BUY "AAdddAABA");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(gold() == 2000 - 400 - 400 && count_of(HAND_DAGGER) == 1 && count_of(DELUGE) == 1 && !stuck);
    CHECK(bought == 2 && ignored == 2 && said_count == 4 && said[3] == 0x02 && messages == 0);
    /* Sell the dagger: then it buys again, at Eolis's 400. */
    on(true);
    press(TO_SELL "AB"
                  "A");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(gold() == 1200 + 200 && count_of(HAND_DAGGER) == 0 && sold == 1);
    press(TO_BUY "ABA");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(gold() == 1400 - 400 && count_of(HAND_DAGGER) == 1 && ignored == 0 && bought == 1);
    /* A duplicate bought before this change still sells: two Long Swords. */
    on(true);
    carry(0, (const uint8_t[]){ LONG_SWORD, LONG_SWORD }, 2);
    press(TO_SELL "AA"
                  "AABA"); /* the last: nothing to sell, then buying */
    talk(shops[EOLIS_TOOLS].index);
    CHECK(gold() == 800 + 800 && count_of(LONG_SWORD) == 0 && refused == 0 && !stuck);
    /* Consumables stay buyable as often as there is room: three Red Potions. */
    on(true);
    carry(1000, NULL, 0);
    press(TO_BUY "dAAABA");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(count_of(RED_POTION) == 3 && ignored == 0);
    /* Quick shops off: the game's own refusal, as the original. */
    on(false);
    carry(1000, NULL, 0);
    press("AAAAA");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(gold() == 1000 && said[said_count - 1] == 0x06);
    on(true);
}

/* In the buy list, an owned item keeps its row, and A on it does nothing. */
static void owned_rows(void) {
    enum { BOOTS_ROW = 4 };
    /* Forepaw's: buy the Wing Boots; A on them again does nothing; then a Red Potion. */
    on(true);
    carry(9000, (const uint8_t[]){ RED_POTION }, 1);
    press(TO_BUY "ddddAAuABA");
    talk(shops[FOREPAW_TOOLS].index);
    CHECK(!stuck && input_at == 13 && said_count == 4 && said[3] == 0x02);
    CHECK(gold() == 9000 - 2800 - 400 && count_of(WING_BOOTS) == 1 && count_of(RED_POTION) == 2);
    CHECK(bought == 2 && ignored == 1 && !strcmp(events, "mmmdddom")); /* drawn, redrawn after each purchase, left */
    CHECK(ram[ARG_STRINGS_COUNT] == 5 && ram[DATA_ARRAY + BOOTS_ROW] == WING_BOOTS && price(BOOTS_ROW) == 2800);
    /* Back again, the boots selected (in no list): the same row, the same price, owned. */
    on(true);
    carry(5800, NULL, 0);
    ram[0x03C1] = WING_BOOTS & 0x1F;
    press(TO_BUY "ddddAAAuB");
    talk(shops[FOREPAW_TOOLS].index);
    CHECK(gold() == 5800 && ignored == 3 && said_count == 3 && !strcmp(events, "mmmd") && ram[MENU_CURSOR_POS] == 3);
    CHECK(listed((const uint8_t[]){ LONG_SWORD, STUDDED_MAIL, SMALL_SHIELD, RED_POTION, WING_BOOTS },
                 (const uint16_t[]){ 1600, 2500, 1100, 400, 2800 }, 5));
    /* Without recharging items they are used up, so a second pair sells, as
     * in the original (Shop_GetPlayerHasSelectedItem refuses no Items). */
    recharging_on = false;
    on(true);
    carry(9000, (const uint8_t[]){ WING_BOOTS }, 1);
    press(TO_BUY "ddddABA");
    talk(shops[FOREPAW_TOOLS].index);
    CHECK(gold() == 9000 - 2800 && count_of(WING_BOOTS) == 2 && ignored == 0 && !stuck);
    recharging_on = true;

    /* After Mascon's (Death, Hour Glass, Elixir, Red Potion): the Hour Glass held, and the Elixir spent. */
    enum { AFTER_MASCON = 5 };
    on(true);
    carry(20000, (const uint8_t[]){ HOUR_GLASS }, 1);
    spent = true;
    press(TO_BUY "dAdAdABA");
    talk(shops[AFTER_MASCON].index);
    CHECK(gold() == 20000 - 300 && count_of(HOUR_GLASS) == 1 && count_of(RED_POTION) == 1);
    CHECK(!(ram[SPECIAL_ITEMS] & 0x08) && ignored == 2 && said_count == 4 && !stuck); /* buying never charges it */
    spent = false;
    /* Neither: both sell. */
    on(true);
    carry(20000, NULL, 0);
    press(TO_BUY "dAdABA");
    talk(shops[AFTER_MASCON].index);
    CHECK(gold() == 20000 - 5600 - 4300 && count_of(HOUR_GLASS) == 1 && (ram[SPECIAL_ITEMS] & 0x08) && ignored == 0);

    /* The J key at Eolis's key shop and the Mattock at Mascon's, held. */
    enum { MASCON_TOOLS = 4 };
    on(true);
    carry(1000, (const uint8_t[]){ KEY_J }, 1);
    press(TO_BUY "AAB");
    talk(shops[EOLIS_KEYS].index);
    CHECK(gold() == 1000 && count_of(KEY_J) == 1 && ignored == 2 && thanked == 0 && !stuck);
    on(true);
    carry(1000, (const uint8_t[]){ MATTOCK }, 1);
    press(TO_BUY "ddAB");
    talk(shops[MASCON_TOOLS].index);
    CHECK(gold() == 1000 && count_of(MATTOCK) == 1 && ignored == 1 && !stuck);
    /* Without reusable tools a key is used up, so a second sells. */
    reusable_on = false;
    on(true);
    carry(1000, (const uint8_t[]){ KEY_J }, 1);
    press(TO_BUY "ABA");
    talk(shops[EOLIS_KEYS].index);
    CHECK(gold() == 900 && count_of(KEY_J) == 2 && ignored == 0 && !stuck);
    reusable_on = true;

    /* Nor does a shop buy them: in the sell list owned items are not for
     * sale, and A on them does nothing. */
    on(true);
    carry(0, (const uint8_t[]){ WING_BOOTS }, 1);
    press(TO_SELL "AAB");
    talk(shops[FOREPAW_TOOLS].index);
    CHECK(listed((const uint8_t[]){ WING_BOOTS }, (const uint16_t[]){ 0 }, 1));
    CHECK(gold() == 0 && count_of(WING_BOOTS) == 1 && refused == 2 && sold == 0 && said_count == 3 && !stuck);
    /* Without recharging items they are used up, so they sell, at Forepaw's
     * half. The last thing sold: "You have nothing to buy.", "What would you
     * like?", the buy list; B, and the thanks. */
    recharging_on = false;
    on(true);
    carry(0, (const uint8_t[]){ WING_BOOTS }, 1);
    press(TO_SELL "AAABA");
    talk(shops[FOREPAW_TOOLS].index);
    CHECK(gold() == 1400 && count_of(WING_BOOTS) == 0 && refused == 0 && !stuck);
    CHECK(said_count == 6 && said[3] == 0x12 && said[4] == 0x14 && said[5] == 0x02);
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
    static const uint8_t carried[] = { DRAGON_SLAYER, LONG_SWORD, KEY_J,      KEY_A,
                                       WING_BOOTS,    POISON,     RED_POTION, RED_POTION };
    carry(100, carried, sizeof carried);
    /* The sell list: the Items first as the original lists them (from the
     * last), then the weapons. The Poison no shop sells: 100. The Wing Boots
     * and the keys, his for good, and the Dragon Slayer: no price. */
    press(TO_SELL "B");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(said_count == 3 && said[2] == 0x13 && !strcmp(events, "mmmd") && !stuck);
    CHECK(
        listed((const uint8_t[]){ RED_POTION, RED_POTION, POISON, WING_BOOTS, KEY_A, KEY_J, LONG_SWORD, DRAGON_SLAYER },
               (const uint16_t[]){ 80, 80, 100, 0, 0, 0, 800, 0 }, 8));
    /* Sell the Poison (no shop's: 100) and a Red Potion (this shop's: 80), in
     * a row, the list drawn again after each, its line not said again; B, the
     * dialogue box over the list, and the thanks. */
    on(true);
    carry(100, carried, sizeof carried);
    press(TO_SELL "ddAuuABA");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(said_count == 4 && said[3] == 0x02 && !stuck && input_at == 12);
    CHECK(gold() == 100 + 100 + 80 && count_of(POISON) == 0 && count_of(RED_POTION) == 1 && count_of(KEY_A) == 1);
    CHECK(!strcmp(events, "mmmdddom") && closes == 0 && sold == 2 && thanked == 1);
    /* The Wing Boots, the A and J keys and the Dragon Slayer: A does
     * nothing, and the list waits. B: nothing sold, no thanks. */
    on(true);
    carry(100, carried, sizeof carried);
    press(TO_SELL "dddAdAdAddAB");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(gold() == 100 && count_of(WING_BOOTS) == 1 && count_of(KEY_A) == 1 && count_of(KEY_J) == 1);
    CHECK(count_of(DRAGON_SLAYER) == 1 && said_count == 3 && refused == 4 && sold == 0 && thanked == 0 && !stuck);
    /* Without reusable tools and recharging items, the keys and the boots
     * are used up, so they sell: the J key at a key shop's half (50), the A
     * key, which no shop sells, at 100, the boots at Forepaw's half (1400). */
    reusable_on = recharging_on = false;
    on(true);
    carry(100, carried, sizeof carried);
    press(TO_SELL "B");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(
        listed((const uint8_t[]){ RED_POTION, RED_POTION, POISON, WING_BOOTS, KEY_A, KEY_J, LONG_SWORD, DRAGON_SLAYER },
               (const uint16_t[]){ 80, 80, 100, 1400, 100, 50, 800, 0 }, 8));
    press(TO_SELL "dddddABA");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(gold() == 150 && count_of(KEY_J) == 0 && refused == 0 && !stuck);
    reusable_on = recharging_on = true;
    /* At a key shop, a Red Potion sells for 80, as at every shop. Then
     * nothing is left: the original's line, and buying. */
    carry(0, (const uint8_t[]){ RED_POTION }, 1);
    press(TO_SELL "AAABA");
    talk(shops[EOLIS_KEYS].index);
    CHECK(gold() == 80 && count_of(RED_POTION) == 0 && said_count == 6 && said[0] == I_SELL_KEYS && said[3] == 0x12);
    /* At Dartmoor's too, which asks 2000 for one (#176). */
    carry(0, (const uint8_t[]){ RED_POTION }, 1);
    press(TO_SELL "AAABA");
    talk(shops[DARTMOOR_TOOLS].index);
    CHECK(gold() == 80 && !stuck);
    /* A special item that somehow sits in a list is never sold, so its
     * SpecialItems bit (Player_RemoveItem clears that) stays. */
    carry(0, NULL, 0);
    ram[list_at[ITEMS]] = ELIXIR & 0x1F;
    ram[NUMBER_OF_WEAPONS + ITEMS] = 1;
    ram[SPECIAL_ITEMS] = 0x08;
    press(TO_SELL "AB");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(ram[SPECIAL_ITEMS] == 0x08 && gold() == 0 && ram[NUMBER_OF_WEAPONS + ITEMS] == 1 && !stuck);
}

/* Select does nothing in a list; B leaves; nothing to sell goes to buying. */
static void leaving(void) {
    on(true);
    carry(500, (const uint8_t[]){ KEY_J, LONG_SWORD }, 2);
    /* Select on the Red Potion: nothing, as in the original; A buys it. */
    press(TO_BUY "dsABA");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(sounds == 0 && gold() == 500 - 160 && count_of(RED_POTION) == 1 && said_count == 4 && !stuck);
    /* Nothing to sell, from the menu: the original's line, then "What would
     * you like?" and buying; a purchase, B, the thanks. */
    on(true);
    carry(500, NULL, 0);
    press("AdAA"
          "AdABA");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(said_count == 5 && said[2] == 0x12 && said[3] == 0x14 && said[4] == 0x02);
    CHECK(!strcmp(events, "mmmmddom") && gold() == 500 - 160 && !stuck);
    /* Selling the last thing: the dialogue box over the list for the line,
     * then "What would you like?" and buying; B, the thanks. */
    on(true);
    carry(0, (const uint8_t[]){ RED_POTION }, 1);
    press(TO_SELL "AAABA");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(gold() == 80 && said_count == 6 && said[3] == 0x12 && !strcmp(events, "mmmdommdom") && closes == 0);
    /* B on "What would you like?" there: he still thanks you, something sold. */
    on(true);
    carry(0, (const uint8_t[]){ RED_POTION }, 1);
    press(TO_SELL "AABA");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(gold() == 80 && said_count == 6 && said[4] == 0x14 && said[5] == 0x02 && thanked == 1);
    CHECK(!strcmp(events, "mmmdommm") && !stuck);
    /* Quick shops off: the original's purchase closes the list and opens the
     * dialogue box for the thanks. */
    on(false);
    carry(500, NULL, 0);
    press("AAAdAA");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(!strcmp(events, "mmmdcom") && gold() == 500 - 160);
    on(true);
}

static void pages_of_eight(void) {
    on(true);
    static const uint8_t carried[] = { KEY_J, KEY_Q,      KEY_K,      MATTOCK,     RED_POTION,   RED_POTION,
                                       KEY_A, RED_POTION, LONG_SWORD, GIANT_BLADE, DRAGON_SLAYER };
    carry(0, carried, sizeof carried);
    press(TO_SELL "B");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(shop.selling == 11 && pages() == 2 && ram[ARG_STRINGS_COUNT] == 8 && ram[DATA_ARRAY] == RED_POTION);
    /* Down from the bottom: the second page, its top. */
    carry(0, carried, sizeof carried);
    press(TO_SELL "dddddddddB");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(shop.page == 1 && turned == 1 && ram[ARG_STRINGS_COUNT] == 3);
    CHECK(listed((const uint8_t[]){ DRAGON_SLAYER, GIANT_BLADE, LONG_SWORD }, (const uint16_t[]){ 0, 4250, 800 }, 3));
    /* Up from the top of the first: the last page, its bottom; sell the Long Sword there. */
    carry(0, carried, sizeof carried);
    press(TO_SELL "uABA");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(gold() == 800 && count_of(LONG_SWORD) == 0 && shop.page == 1 && !stuck);
    /* Sell the last of a page: back to the end of the page before. */
    static const uint8_t nine[] = {
        KEY_J, KEY_Q, KEY_K, MATTOCK, RED_POTION, RED_POTION, KEY_A, RED_POTION, LONG_SWORD
    };
    carry(0, nine, sizeof nine);
    press(TO_SELL "uAs"); /* Select: nothing, a frame in the list */
    talk(shops[EOLIS_TOOLS].index);
    CHECK(gold() == 800 && shop.page == 0 && ram[MENU_CURSOR_POS] == 7 && stuck); /* left waiting in the list */
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
    /* Dartmoor's tool shop buys a Red Potion for half its own 2000, as the
     * original does. */
    carry(0, (const uint8_t[]){ RED_POTION }, 1);
    press("AdAAA"
          "A");
    talk(shops[DARTMOOR_TOOLS].index);
    CHECK(gold() == 1000 && count_of(RED_POTION) == 0 && said[said_count - 1] == 0x02 && !stuck);
    on(true);
}

/* The Red Potion: every tool shop that lists one sells it, at its own
 * price, as often as there is room, the list staying up; and every shop, a
 * key shop too, buys one for 80 (#176). */
static void red_potions(void) {
    unsigned selling = 0;
    for (unsigned k = 0; k < SHOPS; ++k) {
        on(true); /* the counts from 0 */
        unsigned row = 5;
        for (unsigned j = 0; j < 5 && shops[k].list[j].price; ++j)
            if (shops[k].list[j].id == RED_POTION) row = j;
        /* Bought: down to it, A twice, B and the thanks. */
        if (row < 5) {
            char buttons[16] = TO_BUY;
            for (unsigned d = 0; d < row; ++d) strcat(buttons, "d");
            strcat(buttons, "AABA");
            uint16_t cost = shops[k].list[row].price;
            carry(2u * cost, NULL, 0);
            press(buttons);
            talk(shops[k].index);
            CHECK(gold() == 0 && count_of(RED_POTION) == 2 && bought == 2 && thanked == 1 && !stuck);
            CHECK(ram[DATA_ARRAY + row] == RED_POTION && price(row) == cost && ram[MENU_CURSOR_POS] == row);
            ++selling;
        }
        /* Sold: the sell list offers it at 80, and A pays that. */
        on(true);
        carry(0, (const uint8_t[]){ RED_POTION, RED_POTION }, 2);
        press(TO_SELL "B");
        talk(shops[k].index);
        CHECK(listed((const uint8_t[]){ RED_POTION, RED_POTION }, (const uint16_t[]){ 80, 80 }, 2) && !stuck);
        press(TO_SELL "ABA");
        talk(shops[k].index);
        CHECK(gold() == 80 && count_of(RED_POTION) == 1 && sold == 1 && !stuck);
    }
    CHECK(selling == 9);
}

/* With Found items on, no tool shop sells the Wing Boots, the Hour Glass or
 * the Elixir: their lines go as the Red Potion's do, with it or without it,
 * and what is left buys as before. */
static void found_items(void) {
    /* On plain values: every entry of the three goes, the rest keep their order. */
    static const uint8_t drops[] = { WING_BOOTS, HOUR_GLASS, ELIXIR };
    uint8_t ids[] = { DEATH, HOUR_GLASS, ELIXIR, RED_POTION, ELIXIR }; /* the shop after Mascon's, an Elixir more */
    uint16_t costs[] = { 9800, 5600, 4300, 300, 320 };
    CHECK(fax_shops_without(drops, 3, ids, costs, 5) == 2 && ids[0] == DEATH && ids[1] == RED_POTION);
    CHECK(costs[0] == 9800 && costs[1] == 300);
    CHECK(fax_shops_without(drops, 3, ids, costs, 2) == 2 && fax_shops_without(drops, 3, ids, costs, 0) == 0);
    CHECK(fax_shops_without(drops, 0, ids, costs, 2) == 2 && ids[1] == RED_POTION); /* nothing to drop */

    on(true);
    found_on = true;
    /* Every shop: its own list, less the three. Two sold the Wing Boots, one
     * the Hour Glass, two the Elixir; the Red Potions stay. */
    unsigned lost = 0;
    for (unsigned k = 0; k < SHOPS; ++k) {
        uint8_t want[5];
        uint16_t want_costs[5];
        unsigned n = 0, had = 0;
        for (unsigned j = 0; j < 5 && shops[k].list[j].price; ++j, ++had) {
            uint8_t id = shops[k].list[j].id;
            if (id == WING_BOOTS || id == HOUR_GLASS || id == ELIXIR) continue;
            want[n] = id, want_costs[n++] = shops[k].list[j].price;
        }
        carry(0, NULL, 0);
        press(TO_BUY "B");
        talk(shops[k].index);
        CHECK(listed(want, want_costs, n) && draws == 1 && !stuck);
        lost += had - n;
    }
    CHECK(lost == 5);
    /* Eolis's: the Hand Dagger, the Red Potion and the Deluge. Down twice,
     * A: the Deluge. */
    carry(1000, (const uint8_t[]){ KEY_J }, 1);
    press(TO_BUY "ddABA");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(gold() == 1000 - 400 && count_of(DELUGE) == 1 && !(ram[SPECIAL_ITEMS] & 0x08));
    CHECK(listed((const uint8_t[]){ HAND_DAGGER, RED_POTION, DELUGE }, (const uint16_t[]){ 400, 160, 400 }, 3));
    /* Forepaw's: the Long Sword, the Studded Mail, the Small Shield and the
     * Red Potion; Up from the top wraps to the potion, the last of four. */
    carry(2000, NULL, 0);
    press(TO_BUY "uABA");
    talk(shops[FOREPAW_TOOLS].index);
    CHECK(gold() == 2000 - 400 && count_of(RED_POTION) == 1 && count_of(WING_BOOTS) == 0 && !stuck);
    CHECK(listed((const uint8_t[]){ LONG_SWORD, STUDDED_MAIL, SMALL_SHIELD, RED_POTION },
                 (const uint16_t[]){ 1600, 2500, 1100, 400 }, 4));
    /* Off (the original's lists), or Quick shops off: the lists as they were. */
    found_on = false;
    carry(0, NULL, 0);
    press(TO_BUY "B");
    talk(shops[FOREPAW_TOOLS].index);
    CHECK(listed((const uint8_t[]){ LONG_SWORD, STUDDED_MAIL, SMALL_SHIELD, RED_POTION, WING_BOOTS },
                 (const uint16_t[]){ 1600, 2500, 1100, 400, 2800 }, 5));
    found_on = true;
    on(false);
    carry(700, NULL, 0);
    press("AAAB");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(listed((const uint8_t[]){ HAND_DAGGER, RED_POTION, ELIXIR, DELUGE }, (const uint16_t[]){ 400, 160, 320, 400 },
                 4));
    found_on = false;
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
    /* A state saved in a shop goes on with it: here in the buy list, after
     * a purchase, its line said. */
    carry(700, (const uint8_t[]){ KEY_J }, 1);
    press(TO_BUY "dAd");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(stuck && shop.traded && shop.lines == 1u << BUY && shop.shown == BUY && ram[MENU_CURSOR_POS] == 2);
    uint8_t buf[32];
    int n = save_get(buf, sizeof buf);
    CHECK(n == STATE_LEN && save_validate(buf, n));
    Session before = shop;
    shop = (Session){ 0 };
    CHECK(save_set(buf, n) && !memcmp(&shop, &before, sizeof shop));
    CHECK(save_validate(NULL, 0) && save_set(NULL, 0) && !shop.active);
    /* One saved before #163 (version 1: no lines, trade or leaving) goes on
     * in the list it was in, its line said. */
    buf[0] = 1; /* the state saved above, as version 1 */
    CHECK(save_validate(buf, STATE_V1_LEN) && save_set(buf, STATE_V1_LEN));
    CHECK(shop.active && shop.mode == BUY && shop.lines == 1u << BUY && !shop.traded && !shop.leaving);
    CHECK(!save_validate(buf, STATE_LEN)); /* version 1 is 17 bytes */
    buf[0] = STATE_VERSION;
    buf[2] = 7;
    CHECK(!save_validate(buf, n) && !save_set(buf, n));
    buf[2] = BUY;
    buf[17] = 4;
    CHECK(!save_validate(buf, n));
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
    for (int ch = 0x21; ch < 0x80; ++ch) cart[13 * 16384 + (ch - 0x20) * 8] = 0x80; /* a pixel each */
    for (int i = 0; i < 256 * 240; ++i) native[i] = 0xFF123456u;
    char line[HINT_COLS + 1], page[HINT_COLS + 1];
    int w = 256, h = 240;

    static const uint8_t carried[] = { RED_POTION, KEY_J, KEY_Q,      KEY_K,      MATTOCK,
                                       RED_POTION, KEY_A, RED_POTION, LONG_SWORD, DRAGON_SLAYER };
    carry(0, carried, sizeof carried);
    ram[0x03BD] = GIANT_BLADE; /* in hand: nothing Eolis sells is his */
    frame_list(true);
    /* B: the script has ended, nothing is drawn. */
    press(TO_BUY "B");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(!hint(line) && !page_label(page) && fax_shops_present(NULL, &w, &h) == NULL);
    /* Left in the buy list (the player stops pressing): an entry that buys
     * has no hint, and with nothing owned nothing is drawn. Select is no
     * longer a hint ("Select to sell" is gone). */
    press(TO_BUY);
    talk(shops[EOLIS_TOOLS].index);
    CHECK(stuck && shop.shown == BUY && list_open() && !hint(line) && !page_label(page));
    CHECK(fax_shops_present(NULL, &w, &h) == NULL);

    /* The sell list: two pages, the page over the first entry, on the
     * Red Potion no hint. */
    press(TO_SELL);
    talk(shops[EOLIS_TOOLS].index);
    CHECK(!hint(line) && page_label(page) && !strcmp(page, "1 of 2"));
    frame_list(false);
    CHECK(fax_shops_present(NULL, &w, &h) == NULL); /* the list's frame is not up yet: unchanged */
    frame_list(true);
    const uint32_t *px = fax_shops_present(NULL, &w, &h);
    CHECK(px != native && w == 256 && h == 240);
    CHECK(lit(px, HINT_COL + HINT_COLS - 1, PAGE_ROW) && lit(px, HINT_COL + HINT_COLS - 6, PAGE_ROW) &&
          !lit(px, HINT_COL + HINT_COLS - 7, PAGE_ROW));
    CHECK(!lit(px, HINT_COL, HINT_ROW)); /* no hint */
    /* The first page, from the last of the Items: Red Potion, the A key, ...
     * On the A key, his for good: "Not for sale", and no price. */
    CHECK(ram[DATA_ARRAY + 1] == KEY_A && price(1) == 0 && ram[DATA_ARRAY + 3] == MATTOCK && price(3) == 0);
    uint32_t inside = cyc_render_color(0);
    for (int x = (LIST_X + 13) * 8; x < (LIST_X + 18) * 8; ++x) {
        CHECK(px[(LIST_Y + 5) * 8 * 256 + x] == inside);    /* the A key's price cells */
        CHECK(px[(LIST_Y + 3) * 8 * 256 + x] == native[0]); /* the Red Potion's: as drawn */
    }
    ram[MENU_CURSOR_POS] = 1;
    CHECK(hint(line) && !strcmp(line, "Not for sale"));
    px = fax_shops_present(NULL, &w, &h);
    CHECK(lit(px, HINT_COL, HINT_ROW) && lit(px, HINT_COL + 11, HINT_ROW) && !lit(px, HINT_COL + 12, HINT_ROW));
    CHECK(!lit(px, HINT_COL, HINT_ROW - 1));
    /* A dialogue box opened over the list (a shop's message, the thanks):
     * the list is not the box up, so nothing is drawn over it. */
    text_box(10, 16, 20, 8);
    CHECK(!hint(line) && !page_label(page) && fax_shops_present(NULL, &w, &h) == NULL);
    text_box(LIST_X, LIST_Y, LIST_W, LIST_H);
    ram[MENU_CURSOR_POS] = 0;
    /* On the second page, the cursor on the Dragon Slayer: "Not for sale",
     * and its price cells painted the box's inside. */
    press(TO_SELL "ddddddddd");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(shop.page == 1 && ram[DATA_ARRAY] == DRAGON_SLAYER);
    ram[MENU_CURSOR_POS] = 0;
    CHECK(hint(line) && !strcmp(line, "Not for sale") && page_label(page) && !strcmp(page, "2 of 2"));
    px = fax_shops_present(NULL, &w, &h);
    for (int x = (LIST_X + 13) * 8; x < (LIST_X + 18) * 8; ++x) CHECK(px[(LIST_Y + 3) * 8 * 256 + x] == inside);
    CHECK(px[(LIST_Y + 5) * 8 * 256 + (LIST_X + 13) * 8] == native[0]); /* the Long Sword's price: untouched */
    ram[MENU_CURSOR_POS] = 1;
    CHECK(!hint(line));

    /* Forepaw's buy list with the Wing Boots owned: OWNED in their price's
     * five cells, on the box's inside, and "Already owned" while the cursor
     * is on them. */
    carry(0, (const uint8_t[]){ RED_POTION, WING_BOOTS }, 2);
    press(TO_BUY);
    talk(shops[FOREPAW_TOOLS].index);
    ram[MENU_CURSOR_POS] = 3;
    CHECK(!hint(line));
    ram[MENU_CURSOR_POS] = 4;
    CHECK(hint(line) && !strcmp(line, "Already owned") && !page_label(page));
    px = fax_shops_present(NULL, &w, &h);
    int boots = (LIST_Y + 3 + 2 * 4) * 8 * 256, potion = (LIST_Y + 3 + 2 * 3) * 8 * 256;
    for (int c = 0; c < 5; ++c) {
        int x = (LIST_X + 13 + c) * 8;
        CHECK(px[boots + x] != inside && px[boots + x] != native[0]); /* each letter's one pixel */
        CHECK(px[boots + 256 + x + 1] == inside && px[boots + 7 * 256 + x + 7] == inside);
        CHECK(px[potion + x] == native[0]); /* the Red Potion's price: as drawn */
    }
    CHECK(px[boots + (LIST_X + 12) * 8 + 7] == native[0] && px[boots + (LIST_X + 18) * 8] == native[0]);
    CHECK(lit(px, HINT_COL, HINT_ROW) && lit(px, HINT_COL + 12, HINT_ROW) && !lit(px, HINT_COL + 13, HINT_ROW));
    /* The cursor on the Red Potion: OWNED stays, with no hint. */
    ram[MENU_CURSOR_POS] = 3;
    px = fax_shops_present(NULL, &w, &h);
    CHECK(px && px[boots + (LIST_X + 13) * 8] != native[0] && !lit(px, HINT_COL, HINT_ROW));
    /* Not without recharging items: the price stays, no hint, nothing drawn. */
    recharging_on = false;
    ram[MENU_CURSOR_POS] = 4;
    CHECK(!hint(line) && fax_shops_present(NULL, &w, &h) == NULL);
    recharging_on = true;
    /* The sell list shows no OWNED: the boots are not for sale, with no
     * price, as the Dragon Slayer; the Red Potion under them sells. */
    press(TO_SELL);
    talk(shops[FOREPAW_TOOLS].index);
    CHECK(ram[DATA_ARRAY] == WING_BOOTS && ram[DATA_ARRAY + 1] == RED_POTION); /* the Items from the last */
    ram[MENU_CURSOR_POS] = 0;
    CHECK(hint(line) && !strcmp(line, "Not for sale"));
    px = fax_shops_present(NULL, &w, &h);
    for (int x = (LIST_X + 13) * 8; x < (LIST_X + 18) * 8; ++x)
        for (int r = 0; r < 8; ++r) CHECK(px[((LIST_Y + 3) * 8 + r) * 256 + x] == inside); /* no OWNED either */
    CHECK(px[(LIST_Y + 5) * 8 * 256 + (LIST_X + 13) * 8] == native[0]);
    ram[MENU_CURSOR_POS] = 1;
    CHECK(!hint(line));
    /* In a wider picture (Seamless), the native picture is centered. */
    ram[MENU_CURSOR_POS] = 0;
    static uint32_t wide[424 * 240];
    for (int i = 0; i < 424 * 240; ++i) wide[i] = 0xFF654321u;
    w = 424;
    h = 240;
    px = fax_shops_present(wide, &w, &h);
    CHECK(px != wide && w == 424 && px[(HINT_ROW * 8) * 424 + 84 + HINT_COL * 8] != wide[0]);
    /* Off: nothing. */
    on(false);
    w = 256;
    h = 240;
    CHECK(fax_shops_present(NULL, &w, &h) == NULL);
}

/* ---- after a purchase ---- */

/* The list's hint now, "" for none. */
static const char *hint_now(void) {
    static char line[HINT_COLS + 1];
    return hint(line) ? line : "";
}
static void just_bought(void) {
    enum { ELIXIR_ROW = 2, DELUGE_ROW = 3 };
    /* Eolis's tool shop, the Hand Dagger in hand: owned before the visit, so
     * "Already owned" at once. */
    on(true);
    carry(1000, NULL, 0);
    press(TO_BUY);
    talk(shops[EOLIS_TOOLS].index);
    CHECK(stuck && ram[MENU_CURSOR_POS] == 0 && !strcmp(hint_now(), "Already owned") && !shop.hushed);
    /* A on Deluge: the game's checks go on (the hook lets it), and once
     * Player_AddToInventory has it, his while the gold counts down, the list
     * still up, there is no hint on it. */
    carry(1000, NULL, 0);
    press(TO_BUY "ddd");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(stuck && ram[MENU_CURSOR_POS] == DELUGE_ROW && !*hint_now());
    CHECK(buy_pick_hook(BUY_PICK) == 0 && shop.hushed && shop.cursor[BUY] == DELUGE_ROW);
    ram[list_at[3] + ram[NUMBER_OF_WEAPONS + 3]++] = DELUGE & 0x1F;
    CHECK(owned_entry(DELUGE_ROW) && !*hint_now());
    /* Deluge bought: the list is drawn again, its cursor at the top until the
     * menu's first update puts it back, and no hint, not even the dagger's
     * there: it is the item just bought the list shows the cursor on. */
    carry(1000, NULL, 0);
    press(TO_BUY "dddA");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(stuck && count_of(DELUGE) == 1 && shop.hushed && shop.restore && owned_entry(DELUGE_ROW));
    CHECK(ram[MENU_CURSOR_POS] == 0 && cursor_shown() == DELUGE_ROW && !*hint_now());
    /* The cursor put back on it, a frame with no button: still none. */
    carry(1000, NULL, 0);
    press(TO_BUY "dddA.");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(stuck && !shop.restore && ram[MENU_CURSOR_POS] == DELUGE_ROW && shop.hushed && !*hint_now());
    /* Up to the Elixir, not owned: no hint. Down, back on Deluge: "Already owned". */
    carry(1000, NULL, 0);
    press(TO_BUY "dddA.u");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(stuck && ram[MENU_CURSOR_POS] == ELIXIR_ROW && !shop.hushed && !*hint_now());
    carry(1000, NULL, 0);
    press(TO_BUY "dddA.ud");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(stuck && ram[MENU_CURSOR_POS] == DELUGE_ROW && !strcmp(hint_now(), "Already owned"));
    /* A on it again does nothing, and the hint says why. */
    on(true);
    carry(1000, NULL, 0);
    press(TO_BUY "dddA.A");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(stuck && ignored == 1 && ram[MENU_CURSOR_POS] == DELUGE_ROW && !strcmp(hint_now(), "Already owned"));
    /* A list of one, Eolis's key shop: the J key bought, no hint; Down wraps
     * back to it, which counts as leaving it: "Already owned". */
    carry(1000, NULL, 0);
    press(TO_BUY "A.");
    talk(shops[EOLIS_KEYS].index);
    CHECK(stuck && count_of(KEY_J) == 1 && shop.hushed && owned_entry(0) && !*hint_now());
    carry(1000, NULL, 0);
    press(TO_BUY "A.d");
    talk(shops[EOLIS_KEYS].index);
    CHECK(stuck && ram[MENU_CURSOR_POS] == 0 && !shop.hushed && !strcmp(hint_now(), "Already owned"));
    /* A consumable: never a hint. Nor does the hush outlive the visit. */
    carry(1000, NULL, 0);
    press(TO_BUY "dA.");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(stuck && shop.hushed && !*hint_now());
    ram[MENU_CURSOR_POS] = 0; /* (the dagger, as if the cursor were there) */
    CHECK(!strcmp(hint_now(), "Already owned"));
    carry(1000, NULL, 0);
    press(TO_BUY);
    talk(shops[EOLIS_TOOLS].index);
    CHECK(!shop.hushed && !strcmp(hint_now(), "Already owned"));
    /* Saved with the machine; a state of version 2 (before the hush) shows
     * its hint at once. */
    carry(1000, NULL, 0);
    press(TO_BUY "dddA.");
    talk(shops[EOLIS_TOOLS].index);
    uint8_t buf[32];
    int n = save_get(buf, sizeof buf);
    CHECK(n == STATE_LEN && buf[0] == 3 && buf[20] == 1 && save_validate(buf, n));
    shop = (Session){ 0 };
    CHECK(save_set(buf, n) && shop.hushed && shop.traded && !*hint_now());
    buf[0] = 2;
    CHECK(save_validate(buf, STATE_V2_LEN) && save_set(buf, STATE_V2_LEN) && !shop.hushed && shop.traded);
    CHECK(!strcmp(hint_now(), "Already owned"));
    CHECK(!save_validate(buf, STATE_LEN)); /* version 2 is 20 bytes */
    buf[0] = STATE_VERSION;
    buf[20] = 2;
    CHECK(!save_validate(buf, STATE_LEN));
}

/* ---- a price on screen, as the list goes ---- */

/* Entry i's price in the nametable, as Shop_Draw writes it. */
static void put_price(unsigned i, unsigned price) {
    for (int k = PRICE_COLS - 1; k >= 0; --k, price /= 10)
        ciram[PRICE_ROW(i) * 32 + PRICE_COL + k] =
            (uint8_t)((k == PRICE_COLS - 1 || price) ? DIGIT_TILE + price % 10 : 0);
}
/* What entry i's price cells show in `px`: 'O' OWNED (a letter's one pixel
 * at each cell's top left, on the box's inside), 'b' the box's inside, 'n'
 * the native picture (the price, uncovered), '?' anything else. */
static char cells(const uint32_t *px, unsigned i) {
    uint32_t inside = cyc_render_color(0);
    bool letters = true, blank = true, bare = true;
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < PRICE_COLS * 8; ++c) {
            uint32_t v = px[(PRICE_ROW(i) * 8 + r) * 256 + PRICE_COL * 8 + c];
            bool corner = r == 0 && c % 8 == 0;
            letters = letters && (corner ? v != inside && v != native[0] : v == inside);
            blank = blank && v == inside;
            bare = bare && v == native[0];
        }
    return letters ? 'O' : blank ? 'b' : bare ? 'n' : '?';
}
/* The picture now: each entry's price cells (cells()), "" if nothing is drawn. */
static const char *prices_drawn(void) {
    static char out[PAGE + 1];
    int w = 256, h = 240;
    const uint32_t *px = fax_shops_present(NULL, &w, &h);
    if (!px) return "";
    for (unsigned i = 0; i < PAGE; ++i) out[i] = cells(px, i);
    out[PAGE] = 0;
    return out;
}
static void closing(void) {
    for (int ch = 0x21; ch < 0x80; ++ch) cart[13 * 16384 + (ch - 0x20) * 8] = 0x80;
    for (int i = 0; i < 256 * 240; ++i) native[i] = 0xFF123456u;
    static const uint16_t eolis[] = { 400, 160, 320, 400 };

    /* Eolis's tool shop, the Hand Dagger in hand (owned before the visit):
     * the list up, its frame and prices drawn; OWNED over the dagger's. */
    on(true);
    carry(1000, NULL, 0);
    press(TO_BUY);
    talk(shops[EOLIS_TOOLS].index);
    frame_list(true);
    for (unsigned i = 0; i < 4; ++i) put_price(i, eolis[i]);
    CHECK(!strcmp(prices_drawn(), "Onnnnnnn"));
    /* B, nothing bought: IScriptAction_EndScript, which takes frames
     * (IScripts_ClearPortraitImage, its ID $FF at once) before its
     * TextBox_Close, the list up all the while: the dagger's price stays
     * covered, with no hint. */
    CHECK(buy_end_hook(BUY_END) == 0 && shop.shown == NONE);
    CHECK(!strcmp(prices_drawn(), "Onnnnnnn"));
    ram[ISCRIPT_PORTRAIT_ID] = 0xFF;
    CHECK(!strcmp(prices_drawn(), "Onnnnnnn") && !*hint_now());
    /* A row that shows another price is not the one kept: uncovered. */
    put_price(0, 160);
    CHECK(!strcmp(prices_drawn(), ""));
    put_price(0, 400);
    /* TextBox_ClearForPortraitAndText: TextBox_Closed at once, and Seamless
     * shows no box: nothing is drawn, the prices still in the nametable. Nor
     * after, if a screen's tiles make TextBox_Closed read 0 again. */
    text_box(2, 8, 30, 20);
    ram[TEXTBOX_CLOSED] = 1;
    CHECK(!strcmp(prices_drawn(), "") && !last_list.kept);
    text_box(LIST_X, LIST_Y, LIST_W, LIST_H);
    CHECK(!strcmp(prices_drawn(), ""));

    /* Deluge bought, then B: "Thank you for shopping." first, so
     * Maybe_Shop_DrawTextBox's two frames with the list up, then the
     * dialogue box (10,16, 20 x 8) over its middle, the only box Seamless
     * shows: the dagger's price (row 11) is left in the nametable but not on
     * screen, so not covered; Deluge's (row 17) is in the dialogue box until
     * its rows are written over it. */
    enum { DELUGE_ROW = 3 };
    on(true);
    carry(1000, NULL, 0);
    press(TO_BUY "dddA.");
    talk(shops[EOLIS_TOOLS].index);
    frame_list(true);
    for (unsigned i = 0; i < 4; ++i) put_price(i, eolis[i]);
    CHECK(!strcmp(prices_drawn(), "OnnOnnnn"));
    CHECK(buy_end_hook(BUY_END) == 1 && shop.leaving == LEAVING && shop.shown == NONE);
    CHECK(!strcmp(prices_drawn(), "OnnOnnnn"));
    text_box(10, 16, 20, 8);
    CHECK(PRICE_ROW(DELUGE_ROW) == 17 && !strcmp(prices_drawn(), "nnnOnnnn"));
    memset(ciram + 16 * 32 + 10, 0, 2 * 32); /* the box's first two rows: Deluge's price written over */
    CHECK(!strcmp(prices_drawn(), ""));

    /* The sell list's "0" (an item not for sale) the same: two pages, the
     * second's first entry, the Dragon Slayer, not for sale. Turned to it,
     * the first page's prices are still in the nametable until the list is
     * drawn again: the A key's 0 under the Long Sword stays covered too. */
    on(true);
    static const uint8_t carried[] = { RED_POTION, KEY_J, KEY_Q,      KEY_K,      MATTOCK,
                                       RED_POTION, KEY_A, RED_POTION, LONG_SWORD, DRAGON_SLAYER };
    carry(0, carried, sizeof carried);
    ram[0x03BD] = GIANT_BLADE;
    press(TO_SELL "ddddddddd");
    talk(shops[EOLIS_TOOLS].index);
    CHECK(stuck && shop.page == 1 && ram[DATA_ARRAY] == DRAGON_SLAYER && ram[DATA_ARRAY + 1] == LONG_SWORD);
    static const uint16_t page1[] = { 80, 0, 80, 0, 0, 0, 0, 80 };
    frame_list(true);
    for (unsigned i = 0; i < PAGE; ++i) put_price(i, page1[i]);
    CHECK(!strcmp(prices_drawn(), "bbnbbbbn"));
    /* Drawn again: the Dragon Slayer's 0, the Long Sword's 800. */
    frame_list(true);
    put_price(0, 0);
    put_price(1, 800);
    CHECK(!strcmp(prices_drawn(), "bnnnnnnn"));
    /* B, nothing sold: the 0 stays covered while the list is up. */
    CHECK(sell_end_hook(SELL_END) == 0 && shop.shown == NONE);
    ram[ISCRIPT_PORTRAIT_ID] = 0xFF;
    CHECK(!strcmp(prices_drawn(), "bnnnnnnn"));
    ram[TEXTBOX_CLOSED] = 1;
    CHECK(!strcmp(prices_drawn(), ""));
    /* A 0 in a script that is not a shop's: left alone. */
    frame_list(true);
    put_price(0, 0);
    press("A");
    talk(0);
    text_box(LIST_X, LIST_Y, LIST_W, LIST_H);
    CHECK(!strcmp(prices_drawn(), ""));
    on(true);
}

int main(void) {
    build_bank12();
    CHECK(site_count == 13 && reset_cb && activate_cb && save_get && save_set && save_validate);
    prices();
    the_conversation();
    buying();
    the_games_checks();
    ownership();
    not_for_sale();
    gear();
    owned_rows();
    selling();
    leaving();
    pages_of_eight();
    off();
    isolated_and_saved();
    the_hint();
    just_bought();
    closing();
    red_potions();
    found_items();
    printf("shops_test: %u checks passed (%u hook calls)\n", checks, fired);
    return 0;
}
