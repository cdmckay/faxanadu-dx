/* Unit tests for the Select menu (fax_menu.c), without the emulator or a ROM.
 *
 * The feature runs against Block's model of the machine (../block/model.c):
 * RAM and the stack, the registers, bank 12's menu strings as the
 * disassembly has them, and the hook sites as the engine runs them. Around
 * the sites the test plays the game's own code, instruction by instruction as
 * the disassembly has it: PlayerMenu_Show to its input loop, with
 * TextBox_Open; PlayerMenu_DrawStringLines and UI_DrawString, into the rows
 * they draw; Menu_UpdateAndDraw's cursor; and @_loadMenuIDs, into the list,
 * the PLAYER page, or whatever the hook returns into (TextBox_Close,
 * Sound_PlayEffect then @_inputLoop). So it checks what the game does with
 * the feature: the rows it draws, and what each choice opens. */
#include "fax_menu.c"
#include "model.h"

/* ---- what fax_menu.c uses besides the machine ---- */

static bool quests_ok = true, map_ok = true; /* whether each screen would open */
static unsigned quests_asked, map_asked;
bool fax_quests_request(void) {
    ++quests_asked;
    return quests_ok;
}
bool fax_map_request(void) {
    ++map_asked;
    return map_ok;
}

/* ---- the game's code around the sites ---- */

#define TEXTBOX_X          0x0208u
#define TEXTBOX_Y          0x0209u
#define TEXTBOX_WIDTH      0x020Au
#define SELECTED_INVENTORY 0x020Eu /* PlayerMenu_SelectedInventory */
#define TEMP_INT24_U       0x00EEu
#define STATUS_STRINGS     0x88D3u /* PLAYER_STATUS_MENU_STRINGS, right after the top menu's six rows */
#define NO_ITEM_STRINGS    0x8A43u /* NO_INVENTORY_ITEM_STRINGS ("NO WEAPON" ...) */
enum { UP = 0x08, DOWN = 0x04, SOUND_MENU = 0x18 };
enum { OPENS_LIST, OPENS_PLAYER, CLOSES, WAITS }; /* where a choice goes */

/* Bank 12's strings, as the disassembly lays them out: a length, then the text, 16 bytes a row. */
static void put_strings(uint16_t at, const char *const *rows, unsigned n) {
    for (unsigned i = 0; i < n; ++i) {
        uint8_t *row = &rom[12][at - 0x8000 + i * 16];
        memset(row, ' ', 16);
        row[0] = (uint8_t)strlen(rows[i]);
        memcpy(row + 1, rows[i], strlen(rows[i]));
    }
}
static const char *const top_rows[] = { "WEAPON", "ARMOR", "SHIELD", "MAGIC", "ITEM", "PLAYER" };
/* The PLAYER page's strings and the lists' "NO ..." are all 9 long, padded with spaces. */
static const char *const status_rows[] = { "    RANK:", "NEXT EXP:", "WEAPON:  ", " ARMOR:  ",
                                           "SHIELD:  ", " MAGIC:  ", "  ITEM:  " };
static const char *const no_item_rows[] = { "NO WEAPON", "NO ARMOR ", "NO SHIELD", "NO MAGIC ", "NO ITEM  " };
static void cart(void) {
    put_strings(TOP_MENU_STRINGS, top_rows, 6);
    put_strings(STATUS_STRINGS, status_rows, 7);
    put_strings(NO_ITEM_STRINGS, no_item_rows, 5);
}

static void load_a(uint8_t v) { /* LDA: A, N and Z */
    cpu.a = v;
    cpu.p = (uint8_t)((cpu.p & ~(P_N | P_Z)) | (v & P_N) | (v ? 0 : P_Z));
}

/* What TextBox_Open drew, and the rows drawn into it: their text and tile row. */
static struct {
    uint8_t x, y, w, h;
} box;
static char rows[16][16];
static unsigned row_y[16], row_count;

/* UI_DrawString, Y the string's offset from Temp_Int24, into rows[row_count]. */
static void ui_draw_string(uint8_t y, unsigned tile_y) {
    uint16_t base = (uint16_t)(ram[TEMP_INT24] | ram[TEMP_INT24 + 1] << 8);
    cpu.y = y;
    load_a(rom_read(12, (uint16_t)(base + cpu.y))); /* LDA (Temp_Int24),Y */
    CHECK(SITES(DRAW_LENGTH, "faxanadu.select-menu.length"));
    ram[TEMP_INT24_U] = cpu.a; /* STA Temp_Int24_U */
    cpu.y++;                   /* INY, JSR PPUBuffer_QueueCommandOrLength */
    char *text = rows[row_count];
    unsigned n = 0;
    do {
        load_a(rom_read(12, (uint16_t)(base + cpu.y))); /* @_drawLoop: LDA (Temp_Int24),Y */
        CHECK(SITES(DRAW_LETTER, "faxanadu.select-menu.letter"));
        CHECK(n < 15);
        text[n++] = (char)cpu.a; /* JSR Strings_ASCIIToIndex, its tile */
        cpu.y++;
        ram[TEMP_INT24_U]--; /* DEC Temp_Int24_U, BNE */
    } while (ram[TEMP_INT24_U]);
    text[n] = 0;
    row_y[row_count++] = tile_y;
}

/* PlayerMenu_DrawStringLines: row X at the box's top + 2 + 2X, string DataArray[X]. */
static void draw_string_lines(void) {
    unsigned x = 0;
    do {
        ui_draw_string((uint8_t)(ram[DATA_ARRAY + x] << 4), ram[TEXTBOX_Y] + 2u + 2u * x);
        ++x;
    } while (x != ram[ARG_STRINGS_COUNT]);
}

/* PlayerMenu_Show, from its entry to @_inputLoop. */
static void player_menu_show(void) {
    row_count = 0;
    ram[ARG_STRINGS_COUNT] = 6; /* LDA #$06, STA Arg_StringsCount */
    ram[MENU_CURSOR_POS] = 0;
    ram[TEXTBOX_X] = 2;
    ram[TEXTBOX_Y] = 6;
    ram[TEXTBOX_WIDTH] = 0x0A;
    load_a(0x0E);
    ram[TEXTBOX_HEIGHT] = cpu.a;                          /* LDA #$0E, STA TextBox_Height */
    CHECK(SITES(MENU_OPEN, "faxanadu.select-menu.open")); /* LDA #$18, JSR Sound_PlayEffect */
    sounds[sound_count++] = SOUND_MENU;
    box.x = ram[TEXTBOX_X];
    box.y = ram[TEXTBOX_Y];
    box.w = ram[TEXTBOX_WIDTH];
    box.h = ram[TEXTBOX_HEIGHT];
    ram[TEMP_INT24] = 0x73;
    ram[TEMP_INT24 + 1] = 0x88;                                          /* JSR TextBox_Open, Temp_Int24 = the table */
    for (cpu.x = 0; cpu.x < 6; ++cpu.x) ram[DATA_ARRAY + cpu.x] = cpu.x; /* LDX #$00, STX DataArray ... INX */
    --cpu.x;
    CHECK(SITES(MENU_ROWS, "faxanadu.select-menu.rows")); /* JSR PlayerMenu_DrawStringLines */
    draw_string_lines();
}

/* Menu_UpdateAndDraw's cursor, for one frame's new buttons. */
static void menu_update(uint8_t changed) {
    int cursor = ram[MENU_CURSOR_POS];
    if (changed & UP) cursor = cursor ? cursor - 1 : ram[ARG_STRINGS_COUNT] - 1;
    else if (changed & DOWN) cursor = cursor + 1 == ram[ARG_STRINGS_COUNT] ? 0 : cursor + 1;
    ram[MENU_CURSOR_POS] = (uint8_t)cursor;
}

/* Where a returned-into address leads: TextBox_Close, Sound_PlayEffect, @_inputLoop. */
static int went;
static struct {
    uint8_t x, y, w, h;
} closed;
static void textbox_close(void) {
    went = CLOSES;
    closed.x = ram[TEXTBOX_X];
    closed.y = ram[TEXTBOX_Y];
    closed.w = ram[TEXTBOX_WIDTH];
    closed.h = ram[TEXTBOX_HEIGHT];
}
static void play_effect(void) { sounds[sound_count++] = cpu.a; } /* keeps X and Y */
static void input_loop(void) { went = WAITS; }

/* A on the cursor's row: @_loadMenuIDs, entered by BMI with A the new buttons (A down). Returns where it goes;
 * `list`, a list's PlayerMenu_SelectedInventory. */
static int choose(uint8_t *list) {
    went = -1;
    load_a(0x80);
    uint8_t s = cpu.s;
    if (!SITES(MENU_CHOOSE, "faxanadu.select-menu.choose")) {
        CHECK(cpu.s == s);
        return went;
    }
    cpu.x = ram[MENU_CURSOR_POS];        /* LDX Menu_CursorPos, CPX #$05 */
    if (cpu.x == 5) return OPENS_PLAYER; /* JMP PlayerMenu_ShowStatusMenu */
    cpu.x = ram[MENU_CURSOR_POS];        /* PlayerMenu_ShowSubmenu: LDX Menu_CursorPos */
    ram[SELECTED_INVENTORY] = cpu.x;     /* STX PlayerMenu_SelectedInventory */
    *list = cpu.x;
    return OPENS_LIST;
}

/* The menu opened, and A on `row` after moving the cursor there with Down from the top. */
static int open_and_choose(unsigned row, uint8_t *list) {
    player_menu_show();
    for (unsigned i = 0; i < row; ++i) menu_update(DOWN);
    CHECK(ram[MENU_CURSOR_POS] == row);
    return choose(list);
}

static void reset(void) {
    model_reset();
    quests_ok = map_ok = true;
    quests_asked = map_asked = 0;
    sound_count = 0;
    isolated_scope = false;
}

int main(void) {
    cart();
    mapped = 12;
    model_routine(TEXTBOX_CLOSE, textbox_close);
    model_routine(SOUND_PLAY_EFFECT, play_effect);
    model_routine(MENU_INPUT_LOOP, input_loop);
    uint8_t list = 0xFF;

    /* The rows: QUESTS and MAP first, past the table's six; 7 letters fit a row. */
    CHECK(!strcmp(fax_menu_text(6), "QUESTS") && !strcmp(fax_menu_text(7), "MAP"));
    for (unsigned s = 0; s < 16; ++s) CHECK((fax_menu_text(s) != NULL) == (s == 6 || s == 7));
    CHECK(strlen(fax_menu_text(6)) <= 7 && strlen(fax_menu_text(7)) <= 7);
    static const unsigned char order[FAX_MENU_ROWS] = { 6, 7, 0, 1, 2, 3, 4, 5 };
    CHECK(!memcmp(fax_menu_strings, order, sizeof order));

    /* Off: the original's menu, 14 tall, its six rows from WEAPON, each opening its own. */
    reset();
    CHECK(!enabled && fax_menu_height() == 14);
    player_menu_show();
    CHECK(box.x == 2 && box.y == 6 && box.w == 10 && box.h == 14 && ram[ARG_STRINGS_COUNT] == 6);
    CHECK(row_count == 6);
    for (unsigned i = 0; i < 6; ++i) CHECK(!strcmp(rows[i], top_rows[i]) && row_y[i] == 8 + 2 * i);
    int stock[6];
    uint8_t stock_list[6];
    for (unsigned r = 0; r < 6; ++r) {
        list = 0xFF;
        stock[r] = open_and_choose(r, &list);
        stock_list[r] = list;
        CHECK(stock[r] == (r == 5 ? OPENS_PLAYER : OPENS_LIST) && (r == 5 || list == r));
    }
    player_menu_show();
    menu_update(UP); /* from the top, Up wraps to the last of six */
    CHECK(ram[MENU_CURSOR_POS] == 5);

    /* On: 18 tall, the eight rows, the cursor on QUESTS. */
    reset();
    model_activate("faxanadu.select-menu");
    CHECK(enabled && fax_menu_height() == 18);
    player_menu_show();
    CHECK(box.x == 2 && box.y == 6 && box.w == 10 && box.h == 18);
    CHECK(ram[ARG_STRINGS_COUNT] == 8 && ram[MENU_CURSOR_POS] == 0 && !memcmp(&ram[DATA_ARRAY], order, 8));
    static const char *const eight[] = { "QUESTS", "MAP", "WEAPON", "ARMOR", "SHIELD", "MAGIC", "ITEM", "PLAYER" };
    CHECK(row_count == 8);
    for (unsigned i = 0; i < 8; ++i) CHECK(!strcmp(rows[i], eight[i]) && row_y[i] == 8 + 2 * i);
    CHECK(row_y[7] + 1 == box.y + box.h - 1); /* the last row just inside the frame, as the original's */
    CHECK(opened == 1 && served == 2);
    /* The cursor goes round all eight. */
    menu_update(UP);
    CHECK(ram[MENU_CURSOR_POS] == 7);
    menu_update(DOWN);
    CHECK(ram[MENU_CURSOR_POS] == 0);
    for (unsigned i = 0; i < 8; ++i) menu_update(DOWN);
    CHECK(ram[MENU_CURSOR_POS] == 0);

    /* The original's rows, two down, do what they did: the same list, the PLAYER page. */
    for (unsigned r = 2; r < 8; ++r) {
        list = 0xFF;
        sound_count = 0;
        CHECK(open_and_choose(r, &list) == stock[r - 2]);
        CHECK(r == 7 || list == stock_list[r - 2]);
        CHECK(last_row == (int)r && sound_count == 1 && sounds[0] == SOUND_MENU);
    }
    CHECK(originals == 6 && quests_asked == 0 && map_asked == 0);

    /* QUESTS and MAP: the screen asked to open, and the menu closed as B closes it, the whole box. */
    sound_count = 0;
    CHECK(open_and_choose(0, &list) == CLOSES && quests_asked == 1 && map_asked == 0 && quests == 1);
    CHECK(closed.x == 2 && closed.y == 6 && closed.w == 10 && closed.h == 18 && sound_count == 1);
    CHECK(open_and_choose(1, &list) == CLOSES && map_asked == 1 && quests_asked == 1 && maps == 1);
    CHECK(closed.h == 18 && last_row == 1 && sound_count == 2);

    /* Refused (the map with nothing to show): the invalid-choice sound, and the menu waits, on the row. */
    map_ok = false;
    sound_count = 0;
    CHECK(open_and_choose(1, &list) == WAITS && map_asked == 2 && maps == 1 && refused == 1);
    CHECK(sound_count == 2 && sounds[1] == SOUND_INVALID);
    CHECK(ram[MENU_CURSOR_POS] == 1 && ram[ARG_STRINGS_COUNT] == 8);
    menu_update(UP); /* still the eight-row menu */
    CHECK(choose(&list) == CLOSES && quests_asked == 2);
    quests_ok = false;
    player_menu_show();
    CHECK(choose(&list) == WAITS && refused == 2 && sounds[sound_count - 1] == SOUND_INVALID);
    menu_update(DOWN);
    menu_update(DOWN);
    CHECK(choose(&list) == OPENS_LIST && list == 0); /* and WEAPON still opens its list */
    quests_ok = map_ok = true;

    /* UI_DrawString for anything else draws what the ROM holds: the PLAYER page's strings (from $88D3, rows 0-6:
     * 6 and 7 from the menu's table are its first two), a list's "NO WEAPON", and the menu's own six rows. */
    ram[TEMP_INT24] = STATUS_STRINGS & 0xFF;
    ram[TEMP_INT24 + 1] = STATUS_STRINGS >> 8;
    row_count = 0;
    for (unsigned i = 0; i < 7; ++i) ui_draw_string((uint8_t)(i << 4), 0);
    for (unsigned i = 0; i < 7; ++i) CHECK(!strcmp(rows[i], status_rows[i]));
    ram[TEMP_INT24] = NO_ITEM_STRINGS & 0xFF;
    ram[TEMP_INT24 + 1] = NO_ITEM_STRINGS >> 8;
    row_count = 0;
    for (unsigned i = 0; i < 5; ++i) ui_draw_string((uint8_t)(i << 4), 0);
    for (unsigned i = 0; i < 5; ++i) CHECK(!strcmp(rows[i], no_item_rows[i]));
    ram[TEMP_INT24] = 0x73;
    ram[TEMP_INT24 + 1] = 0x88;
    row_count = 0;
    for (unsigned i = 0; i < 6; ++i) ui_draw_string((uint8_t)(i << 4), 0);
    for (unsigned i = 0; i < 6; ++i) CHECK(!strcmp(rows[i], top_rows[i]));
    CHECK(served == 2 * opened); /* only the menus' QUESTS and MAP */

    /* A box the feature did not make (its height not 18 at the rows: the menu opened before the feature was
     * on) keeps the original's six rows, and so does a choice in a menu of six. */
    ram[TEXTBOX_HEIGHT] = 14;
    nes_mod_set_function_hook_enabled("faxanadu.select-menu.open", 0);
    player_menu_show();
    nes_mod_set_function_hook_enabled("faxanadu.select-menu.open", 1);
    CHECK(box.h == 14 && ram[ARG_STRINGS_COUNT] == 6 && row_count == 6 && !strcmp(rows[0], "WEAPON"));
    unsigned before = originals;
    CHECK(choose(&list) == OPENS_LIST && list == 0 && originals == before);

    /* In an isolated machine (Seamless's rooms; the menu never runs there) the hooks do nothing. */
    isolated_scope = true;
    player_menu_show();
    CHECK(box.h == 14 && ram[ARG_STRINGS_COUNT] == 6 && !strcmp(rows[0], "WEAPON"));
    CHECK(choose(&list) == OPENS_LIST && list == 0);
    isolated_scope = false;

    /* Off again: counts cleared, hooks off, the original back. */
    reset();
    CHECK(!enabled && opened == 0 && served == 0 && refused == 0 && last_row == -1 && fax_menu_height() == 14);
    for (unsigned i = 0; i < sizeof hook_ids / sizeof hook_ids[0]; ++i) CHECK(!hook_enabled(hook_ids[i]));
    player_menu_show();
    CHECK(box.h == 14 && row_count == 6 && !strcmp(rows[5], "PLAYER"));

    printf("menu_test: %u checks passed\n", checks);
    return 0;
}
