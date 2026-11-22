/* Unit tests for Double gold (fax_gold.c), without the emulator or a ROM.
 * The module runs against a model of the machine: CPU RAM, the registers,
 * and the game's code around each hook site played as the disassembly has
 * it: Player_HandleTouchCoin's add from $8B9F, IScriptAction_AddGold with
 * IScripts_LoadByte and IScripts_ProgressivelyAddGold (through
 * Player_AddGold, 10 Golds a frame), and the text box's reader,
 * TextBox_ShowMessage_LoadMessage, over a copy of the King's plea where bank
 * 13 has it. So the tests check the gold and the text the game ends with,
 * not the module's own arithmetic. */
#include "fax_gold.c"
#include <string.h>

static unsigned checks;
#define CHECK(test) do{++checks;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(1);}}while(0)

#define KING_SCRIPT 0xA350u         /* ISCRIPT_EOLIS_KING */
#define PLEA_START  0x8BB6u         /* message $35 in bank 13 */
#define OTHER_GIFT  0xAE00u         /* a script elsewhere (unused ROM), for an ADD_GOLD that is not the King's */
#define PLEA        MESSAGE_KING_EXPOSITION

/* ---- the machine ---- */
static uint8_t ram[0x800];
static uint8_t rom[0x10000];        /* what the CPU reads at $8000-$FFFF, for the routines modelled here */
static CycModRegs cpu;
static bool isolated;
static int king_on = -1, coin_on = -1, line_on = -1;   /* the hooks' enabled states, as last set */

uint8_t cyc_mod_peek(uint16_t addr) { return addr < 0x2000 ? ram[addr & 0x7FF] : addr >= 0x8000 ? rom[addr] : 0; }
bool cyc_mod_poke(uint16_t addr, uint8_t value) {
    if (addr >= 0x2000) return false;
    ram[addr & 0x7FF] = value;
    return true;
}
void cyc_mod_regs(CycModRegs *out) { *out = cpu; }
void cyc_mod_set_regs(const CycModRegs *in) { uint16_t pc = cpu.pc; cpu = *in; cpu.pc = pc; }
bool cyc_mod_isolated(void) { return isolated; }
int nes_mod_register_function_entry_plugin(const char *id, uint16_t addr, NESModFunctionEntryCallback cb) {
    return (!strcmp(id, king_id) && addr == ADD_GOLD_PAY && cb == king_hook) ||
           (!strcmp(id, coin_id) && addr == TOUCH_COIN_ADD && cb == coin_hook) ||
           (!strcmp(id, line_id) && addr == LOAD_MESSAGE_CHAR && cb == line_hook);
}
int nes_mod_set_function_hook_enabled(const char *id, int on) {
    if (!strcmp(id, king_id)) king_on = on;
    else if (!strcmp(id, coin_id)) coin_on = on;
    else if (!strcmp(id, line_id)) line_on = on;
    else return 0;
    return 1;
}
int nes_mod_register_reset_callback(NESModActivationCallback cb) { return cb == deactivate; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    return !strcmp(id, "faxanadu.double-gold") && cb == activate;
}
void nes_mod_set_local_only(const char *name, int required) { (void)name; (void)required; }

static uint32_t gold_now(void) { return ram[GOLD] | ram[GOLD + 1] << 8 | (uint32_t)ram[GOLD + 2] << 16; }
static void set_gold_to(uint32_t g) { ram[GOLD] = (uint8_t)g; ram[GOLD + 1] = (uint8_t)(g >> 8); ram[GOLD + 2] = (uint8_t)(g >> 16); }

static void reset(void) {
    memset(ram, 0, sizeof ram);
    cpu = (CycModRegs){.a = 0x5A, .x = 0x77, .y = 0x33, .s = 0xF0, .p = 0x24};
    isolated = false;
}
static void on(void) {
    deactivate();
    activate();
    CHECK(king_on == 1 && coin_on == 1 && line_on == 1 && enabled);
}
static void off(void) {
    activate();
    deactivate();
    CHECK(king_on == 0 && coin_on == 0 && line_on == 0 && !enabled);
}

/* ---- Player_HandleTouchCoin, from the hook site on ----
 * $8B9F LDA Gold, CLC, ADC CurrentSprites_Values,X, STA Gold, LDA Gold_M,
 * ADC #0, STA Gold_M, LDA Gold_U, ADC #0, STA Gold_U (the carry out is
 * lost), JSR UI_DrawGoldValue. X is the coin's slot. */
static void touch_coin(unsigned slot, uint8_t value) {
    ram[CURRENT_SPRITES_VALUES + slot] = value;
    cpu.x = (uint8_t)slot;
    if (coin_on == 1) CHECK(coin_hook(TOUCH_COIN_ADD) == 0);
    unsigned sum = ram[GOLD] + ram[CURRENT_SPRITES_VALUES + cpu.x];
    ram[GOLD] = (uint8_t)sum;
    sum = ram[GOLD + 1] + (sum >> 8);
    ram[GOLD + 1] = (uint8_t)sum;
    sum = ram[GOLD + 2] + (sum >> 8);
    ram[GOLD + 2] = (uint8_t)sum;
}

/* ---- IScriptAction_AddGold, and what it calls ---- */
static uint8_t load_byte(void) {                    /* IScripts_LoadByte */
    uint16_t base = (uint16_t)(ram[ISCRIPT_ADDR] | ram[ISCRIPT_ADDR + 1] << 8);
    uint8_t y = ram[ISCRIPT_OFFSET], value = rom[(uint16_t)(base + y)];
    if (++y == 0) ++ram[ISCRIPT_ADDR + 1];
    ram[ISCRIPT_OFFSET] = y;
    return value;
}
static void add_gold(uint8_t amount) {              /* Player_AddGold, the upper byte of the add 0 */
    unsigned sum = ram[GOLD] + amount;
    ram[GOLD] = (uint8_t)sum;
    sum = ram[GOLD + 1] + (sum >> 8);
    ram[GOLD + 1] = (uint8_t)sum;
    sum = ram[GOLD + 2] + (sum >> 8);
    ram[GOLD + 2] = (uint8_t)sum;
    if (sum > 0xFF) ram[GOLD] = ram[GOLD + 1] = ram[GOLD + 2] = 0xFF;
}
/* IScripts_ProgressivelyAddGold: 10 Golds a frame while the amount is above
 * 0 (its bit 15 clear), then a 60-frame wait. The frames it took. */
static unsigned progressively_add_gold(void) {
    unsigned frames = 0;
    for (;;) {
        ++frames;                                   /* IScripts_UpdatePortraitAnimation waits for the frame */
        uint8_t hi = ram[TEMP_0202 + 1];
        if (!(hi & 0x80)) {
            if (hi | ram[TEMP_0202]) {
                unsigned left = (unsigned)(ram[TEMP_0202] | hi << 8) - 10u;
                ram[TEMP_0202] = (uint8_t)left;
                ram[TEMP_0202 + 1] = (uint8_t)(left >> 8);
                add_gold(10);
                continue;
            }
            --ram[TEMP_0202 + 1];                   /* $FF: the wait */
            ram[TEMP_0202] = 0x3C;
        }
        if (--ram[TEMP_0202] == 0) return frames;
    }
}
/* The script at `script` run from its first byte to its ADD_GOLD at `at`
 * (IScripts_InvokeNextAction has read the action byte), then the action:
 * the frames it paid for. */
static unsigned run_add_gold(uint16_t script, uint16_t at) {
    ram[ISCRIPT_ADDR] = (uint8_t)script;
    ram[ISCRIPT_ADDR + 1] = (uint8_t)(script >> 8);
    ram[ISCRIPT_OFFSET] = 0;
    while ((uint16_t)(script + ram[ISCRIPT_OFFSET]) <= at) load_byte();
    CHECK(rom[at] == 0x09);                          /* ADD_GOLD */
    ram[TEMP_0202] = load_byte();
    ram[TEMP_0202 + 1] = load_byte();
    if (king_on == 1) CHECK(king_hook(ADD_GOLD_PAY) == 0);     /* JSR IScripts_ProgressivelyAddGold */
    unsigned frames = progressively_add_gold();
    CHECK(load_byte() == 0x00);                      /* IScripts_InvokeNextAction: the END */
    return frames;
}

/* ---- the text box's reader over message `id` at `start` ----
 * Messages_Load: MessageID, Message_StartAddr; then a byte a turn:
 * TextBox_ShowMessage_LoadMessage's LDA (Temp_Int24),Y, the hook site at
 * CMP #$FF. Pauses and newlines are kept as '|' and '/'. */
static void type_message(uint8_t id, uint16_t start, uint8_t bank, char *out, size_t size) {
    ram[MESSAGE_ID] = id;
    ram[CURRENT_ROM_BANK] = bank;
    ram[MESSAGE_START_ADDR] = (uint8_t)start;
    ram[MESSAGE_START_ADDR + 1] = (uint8_t)(start >> 8);
    ram[MESSAGE_PROCESSED_LENGTH] = 0;
    size_t n = 0;
    for (;;) {
        ram[TEMP_INT24] = ram[MESSAGE_START_ADDR];
        ram[TEMP_INT24 + 1] = ram[MESSAGE_START_ADDR + 1];
        cpu.y = ram[MESSAGE_PROCESSED_LENGTH];
        if (++ram[MESSAGE_PROCESSED_LENGTH] == 0) ++ram[MESSAGE_START_ADDR + 1];
        cpu.a = rom[(uint16_t)((ram[TEMP_INT24] | ram[TEMP_INT24 + 1] << 8) + cpu.y)];
        if (line_on == 1) CHECK(line_hook(LOAD_MESSAGE_CHAR) == 0);
        if (cpu.a == 0xFF) break;
        char ch = cpu.a == 0xFD ? ' ' : cpu.a == 0xFE ? '/' : cpu.a == 0xFC ? '|' : (char)cpu.a;
        if (n + 1 < size) out[n++] = ch;
    }
    out[n] = 0;
    ram[MESSAGE_ID] = 0;
}

/* The game's own data, where the routines read it. */
static void encode(uint16_t at, const char *text) {
    for (; *text; ++text, ++at) rom[at] = *text == ' ' ? 0xFD : *text == '/' ? 0xFE : *text == '|' ? 0xFC : (uint8_t)*text;
    rom[at] = 0xFF;
}
static const char plea[] =
    "Glad you could/come./Disaster has/befallen us.|The Elf/fountain water,/our life source,has stopped.|"
    "The wells are/drying up.|Many men went/out and nobody/came back.|You are our lasthope./I shall give "
    "you1500 Golds.|Prepare/for your journeywith this money.|It will be a/dangerous/journey.|Take care/of yourself.";
static void load_rom(void) {
    memset(rom, 0xFF, sizeof rom);
    static const uint8_t king[] = { 0x80, 0x0D, 0x5A, 0xA3, 0x01, 0x35, 0x09, 0xDC, 0x05, 0x00, 0x03, 0x36, 0x00 };
    memcpy(rom + KING_SCRIPT, king, sizeof king);
    static const uint8_t other[] = { 0x00, 0x09, 0xDC, 0x05, 0x00 };   /* no portrait, ADD_GOLD 1500, END */
    memcpy(rom + OTHER_GIFT, other, sizeof other);
    encode(PLEA_START, plea);
}

/* ---- the tests ---- */
static void the_script_and_the_line_are_where_the_module_looks(void) {
    CHECK(KING_SCRIPT + 6 == 0xA356u && rom[0xA356] == 0x09);                 /* ADD_GOLD $05DC, then END */
    CHECK(KING_GIFT_END == KING_SCRIPT + 9 && rom[KING_GIFT_END] == 0x00);
    CHECK(!memcmp(rom + KING_DIGITS, "1500", 4) && !memcmp(rom + KING_DIGITS - 3, "you", 3));
    CHECK(rom[KING_DIGITS + 4] == 0xFD);
}

static void the_after_coin_rule(void) {
    CHECK(fax_gold_after_coin(0, 59) == 118);
    CHECK(fax_gold_after_coin(1000, 250) == 1500);
    CHECK(fax_gold_after_coin(0, 0) == 0);
    CHECK(fax_gold_after_coin(FAX_GOLD_MAX - 118, 59) == FAX_GOLD_MAX);           /* exactly to the cap */
    CHECK(fax_gold_after_coin(FAX_GOLD_MAX - 117, 59) == FAX_GOLD_MAX);           /* held there */
    CHECK(fax_gold_after_coin(FAX_GOLD_MAX, 250) == FAX_GOLD_MAX);
}

static void coins_pay_double(void) {
    static const uint8_t values[] = { 10, 15, 59, 128, 200, 250 };   /* SPRITE_DROP_VALUES' range */
    for (size_t i = 0; i < sizeof values; ++i) {
        reset();
        on();
        set_gold_to(1234);
        touch_coin(i % 8, values[i]);
        CHECK(gold_now() == 1234u + 2u * values[i]);
        CHECK(seen.coins == 1 && seen.coin_gold == 2u * values[i] && seen.last_coin == values[i] && !seen.capped);
    }
    /* Past one byte: 250 doubled carries into Gold_M and Gold_U. */
    reset();
    on();
    set_gold_to(0x00FFF0);
    touch_coin(3, 250);
    CHECK(gold_now() == 0x00FFF0u + 500u);
    /* The register X the game adds with picks the coin. */
    reset();
    on();
    ram[CURRENT_SPRITES_VALUES + 5] = 99;
    touch_coin(2, 40);
    CHECK(gold_now() == 80);
}

static void coins_stop_at_the_cap(void) {
    reset();
    on();
    set_gold_to(FAX_GOLD_MAX - 50);
    touch_coin(0, 59);
    CHECK(gold_now() == FAX_GOLD_MAX);
    CHECK(seen.capped == 1 && seen.coin_gold == 50);
    touch_coin(1, 250);                                   /* at the cap: no wrap */
    CHECK(gold_now() == FAX_GOLD_MAX);
    CHECK(seen.capped == 2 && seen.coin_gold == 50 && seen.coins == 2);
    reset();
    on();
    set_gold_to(FAX_GOLD_MAX - 118);                      /* to the cap exactly is not past it */
    touch_coin(0, 59);
    CHECK(gold_now() == FAX_GOLD_MAX && seen.capped == 0);
}

static void the_original_pays_the_value_and_wraps(void) {
    reset();
    off();
    set_gold_to(1234);
    touch_coin(0, 59);
    CHECK(gold_now() == 1234u + 59u);
    set_gold_to(FAX_GOLD_MAX - 50);
    touch_coin(0, 59);
    CHECK(gold_now() == 8);                               /* the original's add has no cap */
    CHECK(seen.coins == 0);
}

static void isolated_coins_double_but_do_not_count(void) {
    reset();
    on();
    isolated = true;                                      /* a Seamless neighbouring room's scratch machine */
    touch_coin(0, 59);
    CHECK(gold_now() == 118);
    CHECK(seen.coins == 0 && seen.coin_gold == 0);
}

static void the_king_pays_3000(void) {
    reset();
    on();
    unsigned frames = run_add_gold(KING_SCRIPT, 0xA356);
    CHECK(gold_now() == 3000);
    CHECK(frames == 300 + 60);                            /* 10 a frame, then the wait */
    CHECK(seen.kings == 1);
    reset();
    off();
    frames = run_add_gold(KING_SCRIPT, 0xA356);
    CHECK(gold_now() == 1500 && frames == 150 + 60);
    CHECK(seen.kings == 0);
}

static void only_the_kings_step(void) {
    /* Another script's ADD_GOLD (the game has none; the King's step served
     * elsewhere) pays what it says. */
    reset();
    on();
    run_add_gold(OTHER_GIFT, OTHER_GIFT + 1);
    CHECK(gold_now() == 1500 && seen.kings == 0);
    /* The King's step with another amount read (something served there) is
     * left alone too. */
    reset();
    on();
    rom[0xA357] = 0xE8;
    rom[0xA358] = 0x03;                                   /* 1000 */
    run_add_gold(KING_SCRIPT, 0xA356);
    CHECK(gold_now() == 1000 && seen.kings == 0);
    rom[0xA357] = 0xDC;
    rom[0xA358] = 0x05;
    /* Selling pays through IScripts_ProgressivelyAddGold without the
     * action's JSR: the hook never sees it. */
    reset();
    on();
    ram[TEMP_0202] = 0x2C;
    ram[TEMP_0202 + 1] = 0x01;                            /* 300, half a Red Potion's 600 */
    progressively_add_gold();
    CHECK(gold_now() == 300 && seen.kings == 0);
}

static void the_plea_names_the_gift(void) {
    char text[512];
    reset();
    on();
    type_message(PLEA, PLEA_START, MESSAGE_BANK, text, sizeof text);
    CHECK(strstr(text, "I shall give you3000 Golds.|") != NULL);
    CHECK(!strstr(text, "1500"));
    CHECK(seen.line == 2);                                /* "15" served as "30"; "00" is the stock */
    CHECK(strlen(text) == strlen(plea));
    /* Everything else as written. */
    char want[sizeof plea];
    strcpy(want, plea);
    memcpy(strstr(want, "1500"), "3000", 4);
    CHECK(!strcmp(text, want));
    reset();
    off();
    type_message(PLEA, PLEA_START, MESSAGE_BANK, text, sizeof text);
    CHECK(!strcmp(text, plea));
}

static void only_the_plea(void) {
    char text[512];
    /* Another message where the plea lies, or the plea's bytes with another
     * bank mapped: the stock bytes. */
    reset();
    on();
    type_message(0x36, PLEA_START, MESSAGE_BANK, text, sizeof text);
    CHECK(!strcmp(text, plea) && seen.line == 0);
    type_message(PLEA, PLEA_START, 12, text, sizeof text);
    CHECK(!strcmp(text, plea) && seen.line == 0);
    /* The plea's ID with other bytes at the digits' address: only a stock
     * digit is changed. */
    encode(KING_DIGITS - 3, "you9900 Golds.");
    type_message(PLEA, PLEA_START, MESSAGE_BANK, text, sizeof text);
    CHECK(strstr(text, "you9900 Golds.") && seen.line == 0);
    load_rom();
    /* The digits reached with Y wrapped past 255 (Message_StartAddr_U
     * carried, Temp_Int24 a page on): a message that starts 0x110 bytes
     * before them, through the plea. */
    uint16_t early = (uint16_t)(KING_DIGITS - 0x110);
    memset(rom + early, 'x', PLEA_START - early);
    reset();
    on();
    type_message(PLEA, early, MESSAGE_BANK, text, sizeof text);
    CHECK(strstr(text, "you3000 Golds.") && seen.line == 2);
    load_rom();
}

static void the_typed_digits(void) {
    reset();
    ram[MESSAGE_ID] = PLEA;
    ram[MESSAGE_START_ADDR] = (uint8_t)PLEA_START;
    ram[MESSAGE_START_ADDR + 1] = (uint8_t)(PLEA_START >> 8);
    ram[MESSAGE_PROCESSED_LENGTH] = (uint8_t)(KING_DIGITS - PLEA_START);
    CHECK(fax_gold_plea_digits() == 0);
    ram[MESSAGE_PROCESSED_LENGTH] += 2;
    CHECK(fax_gold_plea_digits() == 2);
    ram[MESSAGE_PROCESSED_LENGTH] += 5;
    CHECK(fax_gold_plea_digits() == 4);
    ram[MESSAGE_ID] = 0;
    CHECK(fax_gold_plea_digits() == 0);
}

static void registers(void) {
    CHECK(nes_mod_register_function_entry_plugin(king_id, ADD_GOLD_PAY, king_hook));
    CHECK(nes_mod_register_function_entry_plugin(coin_id, TOUCH_COIN_ADD, coin_hook));
    CHECK(nes_mod_register_function_entry_plugin(line_id, LOAD_MESSAGE_CHAR, line_hook));
    activate();
    seen.coins = 3;
    deactivate();
    CHECK(!enabled && seen.coins == 0);
}

int main(void) {
    load_rom();
    the_script_and_the_line_are_where_the_module_looks();
    the_after_coin_rule();
    coins_pay_double();
    coins_stop_at_the_cap();
    the_original_pays_the_value_and_wraps();
    isolated_coins_double_but_do_not_count();
    the_king_pays_3000();
    only_the_kings_step();
    the_plea_names_the_gift();
    only_the_plea();
    the_typed_digits();
    registers();
    printf("gold_test: %u checks passed\n", checks);
    return 0;
}
