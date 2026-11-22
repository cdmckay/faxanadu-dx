/* Unit tests for Potion Sellers (fax_potions.c), without the emulator or a
 * ROM. The module runs against a model of the machine: CPU RAM, A and S and
 * the stack, bank 12's seven meat shops' scripts as the ROM has them, the
 * read overrides it serves in banks 12 and 13, the hook sites as the engine
 * dispatches them (a handled site returns as an RTS would, from the stack the
 * hook left), and the interaction-script interpreter as the disassembly has
 * it: messages and questions (A goes on, B ends),
 * IScriptAction_ShowQuestionMessageCheckIfDismissed (A jumps, B goes on),
 * IScriptAction_SpendGold (its two IScripts_LoadByte, the price hooks, the
 * copy to Temp_Int24, then IScripts_CheckEnoughMoney: too little is "This is
 * not enough Golds." and the end), ADD_HP and ADD_MP,
 * IScripts_ShowFinalMessage (the message in A, then the end) and
 * IScriptAction_AddInventoryItem after its IScripts_LoadByte (the item in A:
 * Player_AddToInventory, and its box, which waits for A). So the tests check
 * what he says, what the hero pays and gets, and that the stack is where it
 * started, rather than restate the module's constants. */
#include <stdio.h>
static char logged[200];
#define FAX_POTIONS_LOG(...) snprintf(logged, sizeof logged, __VA_ARGS__)
#include "fax_potions.c"

static unsigned checks;
#define CHECK(test) do{++checks;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(1);}}while(0)

/* ---- the machine ---- */
static uint8_t ram[0x800];
static bool isolated;
static uint8_t bank12[0x4000];               /* $8000-$BFFF as bank 12 maps it */
static CycModRegs cpu;
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
void cyc_mod_regs(CycModRegs *out) { *out = cpu; }
void cyc_mod_set_regs(const CycModRegs *in) { cpu.a = in->a; cpu.x = in->x; cpu.y = in->y; cpu.s = in->s; cpu.p = in->p; }

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
    if (strcmp(id, "faxanadu.potion-sellers")) return 0;
    save_get = get; save_set = set;
    return 1;
}
int nes_mod_register_savestate_validator(const char *id, NESModSavestateValidate validate) {
    if (strcmp(id, "faxanadu.potion-sellers")) return 0;
    save_validate = validate;
    return 1;
}

/* Hook sites at `addr`, as the scheduler runs them: enabled callbacks in
 * order until one handles the call. 1 when the routine was handled. */
static int fire(uint16_t addr) {
    for (int i = 0; i < site_count; ++i)
        if (sites[i].addr == addr && sites[i].on && sites[i].cb(addr)) return 1;
    return 0;
}
static uint8_t pull(void) { cpu.s = (uint8_t)(cpu.s + 1); return ram[0x100 | cpu.s]; }
static uint16_t rts(void) { uint8_t lo = pull(); return (uint16_t)((lo | pull() << 8) + 1); }

/* ---- the game, as the disassembly has it ---- */
/* Bank 12 from $A4A1: ISCRIPT_EOLIS_MEAT_SHOP to ISCRIPT_DARTMOOR_MEAT_SHOP, 13 bytes each: the Meat
 * Salesman, the question "Dried meat to bring energy for N Golds." (messages 7 to 13), pay N, add 30 HP, add
 * 30 MP, "Thank you for shopping.", end. The magic hall's Magician ($A368: Fire for 3000), for a pay step
 * that is not a meat shop's. Unused ROM ($FF) at $AE00. */
static const uint8_t MEAT_SHOPS[] = {
    0x88, 0x02, 0x07, 0x05, 0x32, 0x00, 0x13, 0x1E, 0x0A, 0x1E, 0x03, 0x02, 0x00,
    0x88, 0x02, 0x08, 0x05, 0x96, 0x00, 0x13, 0x1E, 0x0A, 0x1E, 0x03, 0x02, 0x00,
    0x88, 0x02, 0x09, 0x05, 0xB4, 0x00, 0x13, 0x1E, 0x0A, 0x1E, 0x03, 0x02, 0x00,
    0x88, 0x02, 0x0A, 0x05, 0xFA, 0x00, 0x13, 0x1E, 0x0A, 0x1E, 0x03, 0x02, 0x00,
    0x88, 0x02, 0x0B, 0x05, 0x5E, 0x01, 0x13, 0x1E, 0x0A, 0x1E, 0x03, 0x02, 0x00,
    0x88, 0x02, 0x0C, 0x05, 0xF4, 0x01, 0x13, 0x1E, 0x0A, 0x1E, 0x03, 0x02, 0x00,
    0x88, 0x02, 0x0D, 0x05, 0x20, 0x03, 0x13, 0x1E, 0x0A, 0x1E, 0x03, 0x02, 0x00 };
enum { EOLIS_MEAT = 0xA4A1, DAYBREAK_MEAT = 0xA4E2, DARTMOOR_MEAT = 0xA4EF, HALL_MAGICIAN = 0xA368 };
static const uint8_t HALL[] = { 0x83, 0x12, 0x62, 0x77, 0xA3, 0x02, 0x17, 0x05, 0xB8, 0x0B, 0x07, 0x62, 0x01, 0x79,
                                0x00, 0x03, 0x18, 0x00 };
static const uint16_t meat_prices[FAX_POTIONS_TOWNS] = { 50, 150, 180, 250, 350, 500, 800 };
static const char *const towns[FAX_POTIONS_TOWNS] = { "Eolis", "Forepaw", "Mascon", "Victim", "Conflate",
                                                      "Daybreak", "Dartmoor" };
/* The tool shops' lists from $A42E, as the disassembly has them (item, price); the Red Potion is $90. The
 * meat shops' towns' shops, in the same order. */
typedef struct { const char *town; uint16_t list[5][2]; } ToolShop;
static const ToolShop tool_shops[FAX_POTIONS_TOWNS] = {
    { "Eolis", { { 0x00, 400 }, { 0x90, 160 }, { 0x92, 320 }, { 0x60, 400 } } },
    { "Forepaw", { { 0x01, 1600 }, { 0x21, 2500 }, { 0x40, 1100 }, { 0x90, 400 }, { 0x8F, 2800 } } },
    { "Mascon", { { 0x41, 3500 }, { 0x61, 1200 }, { 0x89, 800 }, { 0x90, 600 } } },
    { "Victim", { { 0x22, 5200 }, { 0x89, 1200 }, { 0x90, 800 } } },
    { "Conflate", { { 0x02, 8500 }, { 0x42, 9800 }, { 0x8F, 5500 }, { 0x90, 1000 } } },
    { "Daybreak", { { 0x64, 15000 }, { 0x02, 11500 }, { 0x90, 1200 } } },
    { "Dartmoor", { { 0x02, 13000 }, { 0x90, 2000 } } },
};

enum { HP = 0x0431, MP = 0x039A, ITEMS = 0x03AD, SELECTED_ITEM = 0x03C1, TEMP_INT24 = 0x00EC };
static void set_gold(uint32_t v) { ram[GOLD] = (uint8_t)v; ram[GOLD + 1] = (uint8_t)(v >> 8); ram[GOLD + 2] = (uint8_t)(v >> 16); }
static uint32_t the_gold(void) { return ram[GOLD] | ram[GOLD + 1] << 8 | (uint32_t)ram[GOLD + 2] << 16; }
static void carry(const uint8_t *ids, unsigned n) {
    memset(ram + ITEMS, 0xEE, 8);                            /* stale bytes past the count */
    for (unsigned i = 0; i < n; ++i) ram[ITEMS + i] = ids[i];
    ram[NUMBER_OF_ITEMS] = (uint8_t)n;
}
static unsigned potions(void) {
    unsigned k = 0;
    for (unsigned i = 0; i < ram[NUMBER_OF_ITEMS]; ++i) k += ram[ITEMS + i] == (RED_POTION & 0x1F);
    return k;
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

/* What the box showed, in order: "msg 07", the slot's text, a question's
 * text with "?", "B" for a line B dismissed, "paid 160", "+30 HP", "+30 MP",
 * the item box ("Red Potion"), "end". */
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
static uint16_t temp(uint16_t addr) { return (uint16_t)(ram[addr] | ram[addr + 1] << 8); }
/* IScripts_CheckEnoughMoney: Gold against Temp_Int24/_M, the carry. */
static bool enough_money(void) {
    if (ram[GOLD + 2] != 0) return true;
    if (ram[GOLD + 1] != ram[TEMP_INT24 + 1]) return ram[GOLD + 1] > ram[TEMP_INT24 + 1];
    return ram[GOLD] >= ram[TEMP_INT24];
}
/* Player_AddToInventory, for an Items ID: a full list loses its last entry. */
static void add_to_inventory(uint8_t id) {
    CHECK((id & 0xE0) == 0x80);
    if (ram[NUMBER_OF_ITEMS] == 8) --ram[NUMBER_OF_ITEMS];
    ram[ITEMS + ram[NUMBER_OF_ITEMS]++] = id & 0x1F;
}

/* Run the script at `script` as IScripts_Begin starts it (IScriptOffset 1,
 * past its entity byte), firing IScripts_InvokeNextAction's hooks before each
 * action and going wherever they point it, or where a handled site returns.
 * `keys` answers each question, each line B may dismiss and the item's box,
 * in order: 'A' goes on, 'B' dismisses (a question $16 goes on past its
 * address instead). The stack ends where it began. */
static void talk_from(uint16_t script, uint8_t offset, const char *keys) {
    said_n = 0;
    cpu = (CycModRegs){ .s = 0xF0 };
    ram[ISCRIPT_ADDR] = (uint8_t)script; ram[ISCRIPT_ADDR + 1] = (uint8_t)(script >> 8); ram[ISCRIPT_OFFSET] = offset;
    for (int steps = 0; steps < 32; ++steps) {
        char key = *keys ? *keys : 'A';
        if (fire(ISCRIPTS_INVOKE_NEXT_ACTION)) {                   /* handled: where its RTS goes */
            uint16_t to = rts();
            CHECK(cpu.s == 0xF0);
            if (to == SHOW_FINAL_MESSAGE) {                         /* the message, then the end on A or B */
                message(cpu.a, false);
                if (*keys) ++keys;
                note("end");
                return;
            }
            CHECK(to == ADD_ITEM_TAKEN);                            /* the item, its box until A, the next action */
            add_to_inventory(cpu.a);
            note(cpu.a == RED_POTION ? "Red Potion" : "an item");
            CHECK(key == 'A');
            if (*keys) ++keys;
            continue;
        }
        uint16_t p = (uint16_t)(base() + ram[ISCRIPT_OFFSET]);
        uint8_t op = cyc_mod_peek(p), arg = cyc_mod_peek((uint16_t)(p + 1));
        uint16_t if_held = (uint16_t)(cyc_mod_peek((uint16_t)(p + 2)) | cyc_mod_peek((uint16_t)(p + 3)) << 8);
        switch (op) {
        case 0x00: note("end"); CHECK(cpu.s == 0xF0); return;
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
            unsigned amount = temp(TEMP_0202);
            ram[TEMP_INT24] = (uint8_t)amount; ram[TEMP_INT24 + 1] = (uint8_t)(amount >> 8);
            if (!enough_money()) { message(0x03, false); note("end"); return; }   /* IScripts_ShowNotEnoughMoney */
            set_gold(the_gold() - amount);                          /* IScripts_ProgressivelySubtractGold */
            char text[32];
            snprintf(text, sizeof text, "paid %u", amount);
            note(text);
            break; }
        case 0x13: case 0x0A: {                                     /* IScriptAction_AddHP, _AddMP */
            uint16_t at = op == 0x13 ? HP : MP;
            unsigned v = ram[at] + arg;
            ram[at] = (uint8_t)(v > 80 ? 80 : v);
            char text[32];
            snprintf(text, sizeof text, "+%u %s", arg, op == 0x13 ? "HP" : "MP");
            note(text);
            ram[ISCRIPT_OFFSET] += 2;
            break; }
        case 0x12:                                                  /* CHECK_FOR_ITEM: Fire, not held here */
            ram[ISCRIPT_OFFSET] += 4;
            break;
        case 0x07: note("Fire"); ram[ISCRIPT_OFFSET] += 2; break;   /* ADD_ITEM, the hall's Fire */
        default: fprintf(stderr, "unmodelled action %02X at $%04X\n", op, p); exit(1);
        }
    }
    CHECK(!"the script did not end");
}
static void visit(uint16_t script, const char *keys) { talk_from(script, 1, keys); }
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
#define EOLIS_LINE "A Red Potion to/restore you for/160 Golds.?"
#define DAYBREAK_LINE "A Red Potion to/restore you for/1200 Golds.?"
#define THANKS "msg 02", "end"

/* A launch: the remaster on (activated) or off (reset only), a fresh machine. */
static void play(bool on) {
    reset_cb();
    if (on) plugin_cb();
    memset(ram, 0, sizeof ram);
    ram[SELECTED_ITEM] = 0xFF;
    ram[HP] = 0x20;
    ram[MP] = 0x10;
    memset(bank12, 0xFF, sizeof bank12);
    memcpy(bank12 + (EOLIS_MEAT - 0x8000), MEAT_SHOPS, sizeof MEAT_SHOPS);
    memcpy(bank12 + (HALL_MAGICIAN - 0x8000), HALL, sizeof HALL);
    overrides_lost();
    isolated = table_full = false;
    /* Guru travel serves its SAY and ASK steps, and a line, at power-on. */
    static const uint8_t steps[] = { 0x03, 0xC3, 0x00, 0x16, 0xC3, 0x00, 0xAE };
    for (unsigned i = 0; i < sizeof steps; ++i) CHECK(cyc_mod_set_prg_read_override(prg(12, (uint16_t)(SAY + i)), 0xFF, steps[i]));
    for (unsigned i = 0; i < SLOT_LEN; ++i) CHECK(cyc_mod_set_prg_read_override(prg(13, (uint16_t)(SLOT + i)), 0xFF, 0xFF));
    on_line = NULL;
    logged[0] = 0;
}

/* ---- the tests ---- */

static void registers(void) {
    CHECK(!strcmp(plugin_id, "faxanadu.potion-sellers") && reset_cb == deactivate && plugin_cb == activate);
    CHECK(save_get == state_get && save_set == state_set && save_validate == state_validate);
    CHECK(site_count == 2 && sites[0].addr == 0x826E && sites[1].addr == 0x8367);
    for (int i = 0; i < site_count; ++i) CHECK(!strncmp(sites[i].id, "faxanadu.potions.", 17));
}

/* Each meat shop asks what its town's tool shop asked for a Red Potion; the
 * game takes any of them exactly. */
static void the_price_table(void) {
    for (unsigned t = 0; t < FAX_POTIONS_TOWNS; ++t) {
        uint16_t tools = 0;
        for (unsigned k = 0; k < 5; ++k) if (tool_shops[t].list[k][0] == RED_POTION) tools = tool_shops[t].list[k][1];
        CHECK(!strcmp(tool_shops[t].town, towns[t]) && !strcmp(shops[t].town, towns[t]));
        CHECK(tools && fax_potions_price(t) == tools);
        CHECK(fax_potions_price(t) % 10 == 0 && fax_potions_price(t) < 0x8000);   /* IScripts_ProgressivelySubtractGold */
        CHECK(shops[t].meat == meat_prices[t] && shops[t].script == EOLIS_MEAT + 13 * t);
    }
    static const uint16_t issue[FAX_POTIONS_TOWNS] = { 160, 400, 600, 800, 1000, 1200, 2000 };
    for (unsigned t = 0; t < FAX_POTIONS_TOWNS; ++t) CHECK(fax_potions_price(t) == issue[t]);
    CHECK(fax_potions_price(FAX_POTIONS_TOWNS) == 0 && fax_potions_price(~0u) == 0);
}

/* His line fits his box (16 columns, 4 lines) and the slot (47 bytes and the
 * terminator) at every price. */
static void lines_fit(void) {
    for (unsigned t = 0; t < FAX_POTIONS_TOWNS; ++t) {
        uint8_t bytes[SLOT_LEN];
        char text[64];
        CHECK(line_bytes(t, bytes) && bytes[SLOT_LEN - 1] == 0xFF);
        snprintf(text, sizeof text, LINE_FORMAT, (unsigned)fax_potions_price(t));
        CHECK(strlen(text) <= SLOT_LEN - 1);
    }
    CHECK(!line_bytes(FAX_POTIONS_TOWNS, (uint8_t[SLOT_LEN]){ 0 }));
    CHECK(!encode("This line is far too long for a row", (uint8_t[SLOT_LEN]){ 0 }));
    CHECK(!encode("1\n2\n3\n4\n5", (uint8_t[SLOT_LEN]){ 0 }));
    CHECK(!encode("Sixteen columns.\nSixteen columns.\nSixteen columns.", (uint8_t[SLOT_LEN]){ 0 }));   /* 50 bytes */
}

/* In every town: his question, A, his price taken, the Red Potion's box, his
 * thanks. No HP and no MP. */
static void buying(void) {
    for (unsigned t = 0; t < FAX_POTIONS_TOWNS; ++t) {
        play(true);
        uint16_t price = fax_potions_price(t);
        set_gold(price + 5u);
        carry((const uint8_t[]){ 0x11 }, 1);                   /* a Poison */
        visit(shops[t].script, "AA");
        char line[64], paid[32];
        snprintf(line, sizeof line, "A Red Potion to/restore you for/%u Golds.?", (unsigned)price);
        snprintf(paid, sizeof paid, "paid %u", (unsigned)price);
        const char *const lines[] = { line, paid, "Red Potion", THANKS, NULL };
        expect(lines);
        CHECK(the_gold() == 5 && ram[NUMBER_OF_ITEMS] == 2 && ram[ITEMS] == 0x11 && ram[ITEMS + 1] == 0x10);
        CHECK(ram[HP] == 0x20 && ram[MP] == 0x10);
        CHECK(seen.offers == 1 && seen.sold == 1 && seen.declined == 0 && talk.phase == IDLE && talk.shop == NO_SHOP);
    }
    /* Exactly the price, and again: a potion each time. */
    play(true);
    set_gold(2 * 160);
    visit(EOLIS_MEAT, "AA");
    visit(EOLIS_MEAT, "AA");
    SAID(EOLIS_LINE, "paid 160", "Red Potion", THANKS);
    CHECK(the_gold() == 0 && potions() == 2 && seen.sold == 2 && seen.offers == 2);
    /* Seven in the list and one selected: room for the eighth. */
    play(true);
    set_gold(1000);
    carry((const uint8_t[]){ 0x10, 0x11, 0x10, 0x10, 0x11, 0x10, 0x11 }, 7);
    ram[SELECTED_ITEM] = 0x10;
    visit(DAYBREAK_MEAT, "AA");
    SAID(DAYBREAK_LINE, "msg 03", "end");                    /* not enough: 1000 */
    set_gold(1200);
    visit(DAYBREAK_MEAT, "AA");
    SAID(DAYBREAK_LINE, "paid 1200", "Red Potion", THANKS);
    CHECK(ram[NUMBER_OF_ITEMS] == 8 && ram[ITEMS + 7] == 0x10 && the_gold() == 0);
}

/* B on his question, a short purse, a full Items list: the game's own
 * endings, nothing taken. */
static void refusals(void) {
    play(true);
    set_gold(5000);
    visit(EOLIS_MEAT, "B");
    SAID(EOLIS_LINE, "B", "end");                             /* past ASK: the SAY step's END */
    CHECK(the_gold() == 5000 && potions() == 0 && seen.declined == 1 && talk.phase == IDLE);
    /* Not enough: "This is not enough Golds.", the end. */
    set_gold(159);
    visit(EOLIS_MEAT, "A");
    SAID(EOLIS_LINE, "msg 03", "end");
    CHECK(the_gold() == 159 && potions() == 0 && seen.short_of_gold == 1 && seen.sold == 0);
    set_gold(1999);
    visit(DARTMOOR_MEAT, "A");
    SAID("A Red Potion to/restore you for/2000 Golds.?", "msg 03", "end");
    CHECK(the_gold() == 1999 && potions() == 0 && seen.short_of_gold == 2);
    /* A full Items list: "You can't carry any more.", the end; never his pay step. */
    static const uint8_t full[8] = { 0x10, 0x11, 0x0F, 0x04, 0x10, 0x07, 0x11, 0x10 };
    carry(full, 8);
    set_gold(100000);
    visit(EOLIS_MEAT, "AA");
    SAID(EOLIS_LINE, "msg 06", "end");
    CHECK(the_gold() == 100000 && ram[NUMBER_OF_ITEMS] == 8 && !memcmp(ram + ITEMS, full, 8));
    CHECK(seen.full == 1 && seen.sold == 0 && seen.short_of_gold == 2 && talk.phase == IDLE);
    /* Full and short: the room is checked first, as a tool shop does. */
    set_gold(0);
    visit(EOLIS_MEAT, "A");
    SAID(EOLIS_LINE, "msg 06", "end");
    CHECK(seen.full == 2 && seen.short_of_gold == 2);
    /* B's END, A's refusal or a short purse leave nothing behind for the next script. */
    carry(NULL, 0);
    set_gold(160);
    visit(EOLIS_MEAT, "AA");
    SAID(EOLIS_LINE, "paid 160", "Red Potion", THANKS);
}

/* Other scripts: the magic hall's pay step keeps its 3000, and its steps are
 * its own. */
static void other_scripts(void) {
    play(true);
    set_gold(3500);
    visit(HALL_MAGICIAN, "A");
    SAID("msg 17?", "paid 3000", "Fire", "msg 79", "end");
    CHECK(the_gold() == 500 && seen.offers == 0 && talk.phase == IDLE);
    /* Begun in the middle (not at his question): as it is. */
    set_gold(500);
    talk_from(EOLIS_MEAT, 3, "A");
    SAID("paid 50", "+30 HP", "+30 MP", THANKS);
    CHECK(seen.sold == 0 && potions() == 0);
}

/* With the remaster off: dried meat, as the original. */
static void the_original(void) {
    play(false);
    set_gold(100);
    visit(EOLIS_MEAT, "AA");
    SAID("msg 07?", "paid 50", "+30 HP", "+30 MP", THANKS);
    CHECK(the_gold() == 50 && potions() == 0 && ram[HP] == 0x3E && ram[MP] == 0x2E && !fax_potions_selling());
    CHECK(fax_potions_price(0) == 160);                       /* the table is the table */
}

/* Seamless's isolated rooms run scripts with no effect on the real one. */
static void isolated_runs_change_nothing(void) {
    play(true);
    set_gold(500);
    isolated = true;
    visit(EOLIS_MEAT, "AA");
    SAID("msg 07?", "paid 50", "+30 HP", "+30 MP", THANKS);
    CHECK(seen.offers == 0 && talk.phase == IDLE);
    isolated = false;
}

/* With no room in the override table for his line, he sells dried meat for
 * the session, and the tool shops keep their potions. */
static void no_room_for_his_line(void) {
    play(true);
    overrides_lost();
    table_full = true;
    set_gold(500);
    CHECK(fax_potions_selling());
    visit(EOLIS_MEAT, "AA");
    SAID("msg 07?", "paid 50", "+30 HP", "+30 MP", THANKS);
    CHECK(!fax_potions_selling() && failed && strstr(logged, "cannot supply"));
    table_full = false;
    visit(EOLIS_MEAT, "AA");                                    /* for the session */
    SAID("msg 07?", "paid 50", "+30 HP", "+30 MP", THANKS);
    /* A meat shop that is not the stock one: as it is, and the same. */
    play(true);
    bank12[EOLIS_MEAT - 0x8000 + 6] = 0x0A;
    set_gold(500);
    visit(EOLIS_MEAT, "AA");
    SAID("msg 07?", "paid 50", "+30 MP", "+30 MP", THANKS);
    CHECK(!fax_potions_selling() && strstr(logged, "unexpected"));
}

static uint8_t mid[RECORD], mid_ram[sizeof ram];
static void save_mid(void) {
    if (talk.phase != ASKING) return;
    CHECK(save_get(mid, sizeof mid) == RECORD);
    memcpy(mid_ram, ram, sizeof ram);
}
static void save_states(void) {
    play(true);
    uint8_t buf[4];
    CHECK(save_get(buf, RECORD - 1) == -1);
    CHECK(save_get(buf, sizeof buf) == RECORD && buf[0] == 1 && buf[1] == IDLE && buf[2] == NO_SHOP);
    /* Saved as Eolis's question shows: loaded into a game whose slot holds Daybreak's. */
    set_gold(500);
    on_line = save_mid;
    visit(EOLIS_MEAT, "B");
    on_line = NULL;
    CHECK(mid[1] == ASKING && mid[2] == 0 && save_validate(mid, RECORD));
    play(true);                                                 /* a launch: the overrides gone, Guru travel's back */
    set_gold(5000);
    visit(DAYBREAK_MEAT, "B");                                   /* Daybreak's line in the slot */
    memcpy(ram, mid_ram, sizeof ram);
    CHECK(save_set(mid, RECORD) == 1 && talk.phase == ASKING && talk.shop == 0);
    fax_potions_loaded();
    char text[64];
    slot_text(text);
    CHECK(!strcmp(text, "A Red Potion to/restore you for/160 Golds."));
    /* The box has his question; A on it now jumps to the SAY step. */
    talk_from(SAY, 0, "A");
    SAID("paid 160", "Red Potion", THANKS);
    CHECK(the_gold() == 340 && potions() == 1);
    /* Saved paying (his pay step running): loaded, the price still his. */
    const uint8_t paying[RECORD] = { 1, PAYING, 5 };
    play(true);
    set_gold(1300);
    CHECK(save_set(paying, RECORD) == 1);
    fax_potions_loaded();
    talk_from(DAYBREAK_MEAT, PAYS, "A");
    SAID("paid 1200", "Red Potion", THANKS);
    CHECK(the_gold() == 100);
    /* No record (a state from before the feature): no conversation. */
    CHECK(save_set(buf, 0) == 1 && talk.phase == IDLE && talk.shop == NO_SHOP);
    /* Bad records. */
    CHECK(!save_validate((const uint8_t[]){ 2, IDLE, NO_SHOP }, RECORD));
    CHECK(!save_validate((const uint8_t[]){ 1, PHASES, 0 }, RECORD));
    CHECK(!save_validate((const uint8_t[]){ 1, ASKING, NO_SHOP }, RECORD));
    CHECK(!save_validate((const uint8_t[]){ 1, ASKING, FAX_POTIONS_TOWNS }, RECORD));
    CHECK(!save_validate((const uint8_t[]){ 1, IDLE, 0 }, RECORD));
    CHECK(!save_validate((const uint8_t[]){ 1, IDLE }, 2));
    CHECK(save_validate((const uint8_t[]){ 1, PAYING, 6 }, RECORD));
}

int main(void) {
    registers();
    the_price_table();
    lines_fit();
    buying();
    refusals();
    other_scripts();
    the_original();
    isolated_runs_change_nothing();
    no_room_for_his_line();
    save_states();
    printf("potions_test: %u checks passed\n", checks);
    return 0;
}
