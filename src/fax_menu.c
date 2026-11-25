/* The Select menu. USA 57dd23d1, chipx86 byte-exact disassembly.
 *
 * With the remaster the menu lists QUESTS and MAP above the original's six
 * rows (#146): QUESTS, MAP, WEAPON, ARMOR, SHIELD, MAGIC, ITEM, PLAYER. It
 * opens on QUESTS. A on QUESTS opens the quest log, on MAP the map, as their
 * buttons (L, M) do: the menu closes as B closes it, and the screen opens at
 * the main loop's pause check, in the same pass (once TextBox_Close has had
 * room in the PPU buffer), so closing it returns to play.
 * Where the button would do nothing (the map with no page seen yet), A on the
 * row plays the game's invalid-choice sound and the menu stays up, as a list
 * does for an item it cannot equip. The original's rows each open their list
 * or the PLAYER page from their new place. The buttons stay.
 *
 * How, on the unchanged ROM (bank 12; see docs/GAME_INTERNALS.md, "The Select
 * menu"). PlayerMenu_Show opens a box at (2,6), 10 x 14, sets
 * Arg_StringsCount to 6 and Menu_CursorPos to 0, points Temp_Int24 at
 * PLAYER_TOP_MENU_ITEM_STRINGS (16 bytes a row: a length, then the text),
 * fills DataArray 0-5 with 0-5, each row's string number, and draws them
 * with PlayerMenu_DrawStringLines, which calls UI_DrawString for row X with
 * Y = DataArray[X] * 16. Menu_UpdateAndDraw wraps the cursor at
 * Arg_StringsCount, and @_loadMenuIDs sends cursor 5 to the PLAYER page and
 * the others to PlayerMenu_ShowSubmenu, which reads the list from the cursor.
 * Five hooks, none shared, each changing only the game's own variables or
 * the register a load just set:
 * - $8AB1 (LDA #$18, before TextBox_Open): TextBox_Height 18.
 * - $8ADA (JSR PlayerMenu_DrawStringLines): DataArray 6, 7, 0-5 (all eight
 *   entries) and Arg_StringsCount 8, in the box this feature made.
 * - $8E9D and $8EA5, UI_DrawString after each LDA (Temp_Int24),Y: for strings
 *   6 and 7 of that table (Y $60-$7F), which lie past its end (the PLAYER
 *   page's), A becomes QUESTS' or MAP's length, then each letter. So the
 *   game draws the eight rows itself, and no ROM or read override is used.
 * - $8AF0 (@_loadMenuIDs' LDX Menu_CursorPos): rows 2-7 take the cursor back
 *   by two, so the game's own code opens the original's row. On QUESTS or
 *   MAP it asks the quest log or the map to open (fax_quests_request,
 *   fax_map_request) and returns into TextBox_Close, B's way out; refused,
 *   it returns through Sound_PlayEffect (A $0D) into @_inputLoop.
 * Every state the menu is in is the machine's: no save-state record. */
#include "fax_menu.h"
#include "fax_map.h"
#include "fax_quests.h"
#include "cyc_mod.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#ifdef CYC_WITH_SDL
#include "cyc_tcp.h"
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Bank 12 */
#define MENU_OPEN          0x8AB1u  /* PlayerMenu_Show: LDA #$18, JSR Sound_PlayEffect, JSR TextBox_Open, after
                                       LDA #$0E, STA TextBox_Height (and X 2, Y 6, width 10) */
#define MENU_ROWS          0x8ADAu  /* PlayerMenu_Show: JSR PlayerMenu_DrawStringLines, after Temp_Int24 =
                                       PLAYER_TOP_MENU_ITEM_STRINGS and DataArray 0-5 = 0-5 */
#define MENU_INPUT_LOOP    0x8ADDu  /* @_inputLoop: JSR WaitForNextFrame, ..., JSR Menu_UpdateAndDraw, A to
                                       @_loadMenuIDs, B to JMP TextBox_Close ($8AED) */
#define MENU_CHOOSE        0x8AF0u  /* @_loadMenuIDs: LDX Menu_CursorPos, CPX #$05, BNE PlayerMenu_ShowSubmenu,
                                       JMP PlayerMenu_ShowStatusMenu */
#define DRAW_LENGTH        0x8E9Du  /* UI_DrawString: STA Temp_Int24_U, after LDA (Temp_Int24),Y (the length) */
#define DRAW_LETTER        0x8EA5u  /* UI_DrawString's @_drawLoop: JSR Strings_ASCIIToIndex, after
                                       LDA (Temp_Int24),Y (a letter); it begins CMP #$20 */
#define TEXTBOX_CLOSE      0x9002u  /* TextBox_Close: the box at TextBox_X/Y/Width/Height, put back */
#define TOP_MENU_STRINGS   0x8873u  /* PLAYER_TOP_MENU_ITEM_STRINGS: WEAPON ... PLAYER, 16 bytes a row; the
                                       PLAYER page's PLAYER_STATUS_MENU_STRINGS follow at $88D3 */
/* Bank 15 */
#define SOUND_PLAY_EFFECT  0xD0E4u  /* A the sound; keeps X and Y */
/* RAM */
#define TEMP_INT24         0x00ECu  /* the strings' address, low byte first (Temp_Int24, Temp_Int24_M) */
#define TEXTBOX_HEIGHT     0x020Bu
#define MENU_CURSOR_POS    0x021Eu
#define ARG_STRINGS_COUNT  0x021Fu
#define DATA_ARRAY         0x0220u  /* DataArray: 8 entries, then the shops' costs at $0228 */
/* Values */
enum { HEIGHT = 18, STOCK_HEIGHT = 14, SOUND_INVALID = 0x0D };
enum { STRING_QUESTS = FAX_MENU_STOCK_ROWS, STRING_MAP };   /* the table's rows 6 and 7 */
enum { P_Z = 0x02, P_N = 0x80 };

const unsigned char fax_menu_strings[FAX_MENU_ROWS] = { STRING_QUESTS, STRING_MAP, 0, 1, 2, 3, 4, 5 };
static const char *const added_text[FAX_MENU_ADDED] = { "QUESTS", "MAP" };   /* 7 letters fit a row */

const char *fax_menu_text(unsigned string) {
    return string >= STRING_QUESTS && string < STRING_QUESTS + FAX_MENU_ADDED ? added_text[string - STRING_QUESTS]
                                                                                : NULL;
}

static bool enabled;
static unsigned opened, served, quests, maps, originals, refused;   /* for TCP */
static int last_row = -1;                                           /* the row chosen last, 0-7 */

static uint8_t q(uint16_t a) { return cyc_mod_peek(a); }
static void p(uint16_t a, uint8_t v) { cyc_mod_poke(a, v); }
static bool here(void) { return enabled && !cyc_mod_isolated(); }

unsigned fax_menu_height(void) { return enabled ? HEIGHT : STOCK_HEIGHT; }

/* ---- the box and its rows ---- */

/* PlayerMenu_Show, its box's size stored: four rows taller. */
static int open_hook(uint16_t addr) {
    (void)addr;
    if (here()) p(TEXTBOX_HEIGHT, HEIGHT);
    return 0;
}

/* PlayerMenu_Show, its rows filled: all eight, in the box made for them. */
static int rows_hook(uint16_t addr) {
    (void)addr;
    if (!here() || q(TEXTBOX_HEIGHT) != HEIGHT) return 0;
    for (unsigned i = 0; i < FAX_MENU_ROWS; ++i) p((uint16_t)(DATA_ARRAY + i), fax_menu_strings[i]);
    p(ARG_STRINGS_COUNT, FAX_MENU_ROWS);
    ++opened;
    return 0;
}

/* The added row UI_DrawString is reading at Y, from the top menu's table, or
 * NULL: string 6 or 7, Y its start (`at` 0, the length) or a letter. */
static const char *added_at(uint8_t y, unsigned *at) {
    if ((q(TEMP_INT24) | q(TEMP_INT24 + 1) << 8) != TOP_MENU_STRINGS) return NULL;
    *at = y & 15u;
    return fax_menu_text(y >> 4);
}
static void load(CycModRegs *r, uint8_t value) {   /* A as LDA leaves it: N and Z too */
    r->a = value;
    r->p = (uint8_t)((r->p & ~(P_N | P_Z)) | (value & P_N) | (value ? 0 : P_Z));
    cyc_mod_set_regs(r);
}
static int length_hook(uint16_t addr) {
    (void)addr;
    if (!here()) return 0;
    CycModRegs r;
    cyc_mod_regs(&r);
    unsigned at;
    const char *text = added_at(r.y, &at);
    if (!text || at) return 0;
    load(&r, (uint8_t)strlen(text));
    ++served;
    return 0;
}
static int letter_hook(uint16_t addr) {
    (void)addr;
    if (!here()) return 0;
    CycModRegs r;
    cyc_mod_regs(&r);
    unsigned at;
    const char *text = added_at(r.y, &at);
    if (!text || at == 0 || at > strlen(text)) return 0;
    load(&r, (uint8_t)text[at - 1]);
    return 0;
}

/* ---- a choice ---- */

/* The handled instruction returns into `to`, then `then` (0: none), as
 * pushed RTS addresses, with A = `a` (or kept, -1): S ends where it was. */
static int go(uint16_t to, uint16_t then, int a) {
    CycModRegs r;
    cyc_mod_regs(&r);
    uint8_t s = r.s;
    uint16_t chain[2] = { then, to };
    for (unsigned i = then ? 0 : 1; i < 2; ++i) {   /* pushed as a JSR would: high byte first; `to` on top */
        uint16_t v = (uint16_t)(chain[i] - 1);
        p((uint16_t)(0x100u | s), (uint8_t)(v >> 8)); s = (uint8_t)(s - 1);
        p((uint16_t)(0x100u | s), (uint8_t)v);        s = (uint8_t)(s - 1);
    }
    r.s = s;
    if (a >= 0) r.a = (uint8_t)a;
    cyc_mod_set_regs(&r);
    return 1;
}

/* @_loadMenuIDs, A pressed on the cursor's row. */
static int choose_hook(uint16_t addr) {
    (void)addr;
    if (!here() || q(ARG_STRINGS_COUNT) != FAX_MENU_ROWS) return 0;
    uint8_t row = q(MENU_CURSOR_POS);
    if (row >= FAX_MENU_ROWS) return 0;
    last_row = row;
    if (row >= FAX_MENU_ADDED) {                 /* the original's row: its own cursor */
        p(MENU_CURSOR_POS, (uint8_t)(row - FAX_MENU_ADDED));
        ++originals;
        return 0;
    }
    bool opens = row == FAX_MENU_QUESTS ? fax_quests_request() : fax_map_request();
    if (!opens) {
        ++refused;
        return go(SOUND_PLAY_EFFECT, MENU_INPUT_LOOP, SOUND_INVALID);
    }
    if (row == FAX_MENU_QUESTS) ++quests;
    else ++maps;
    return go(TEXTBOX_CLOSE, 0, -1);             /* as B: JMP TextBox_Close */
}

/* ---- activation ---- */

static const char *const hook_ids[] = {
    "faxanadu.select-menu.open", "faxanadu.select-menu.rows", "faxanadu.select-menu.length",
    "faxanadu.select-menu.letter", "faxanadu.select-menu.choose",
};
static void hooks(int on) {
    for (unsigned i = 0; i < sizeof hook_ids / sizeof hook_ids[0]; ++i) nes_mod_set_function_hook_enabled(hook_ids[i], on);
}
static void deactivate(void) {
    enabled = false;
    opened = served = quests = maps = originals = refused = 0;
    last_row = -1;
    hooks(0);
}
static void activate(void) {
    enabled = true;
    hooks(1);
    nes_mod_set_local_only("Select menu", 1);   /* a peer without it would choose other rows */
}

#ifdef CYC_WITH_SDL
static void menu_state(int id, const char *line) {
    (void)line;
    char fields[320];
    /* opened: menus drawn with eight rows; served: added rows drawn; quests, map: rows that opened their screen;
     * originals: the original's rows chosen; refused: QUESTS or MAP refused; last_row: the row chosen last (0-7).
     * The menu as the machine has it: its rows (Arg_StringsCount), the box's height and the cursor. */
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"height\":%u,\"opened\":%u,\"served\":%u,\"quests\":%u,\"map\":%u,\"originals\":%u,"
             "\"refused\":%u,\"last_row\":%d,\"rows\":%u,\"box_height\":%u,\"cursor\":%u",
             enabled ? "true" : "false", fax_menu_height(), opened, served, quests, maps, originals, refused, last_row,
             q(ARG_STRINGS_COUNT), q(TEXTBOX_HEIGHT), q(MENU_CURSOR_POS));
    cyc_tcp_ok(id, fields);
}
void fax_menu_tcp_setup(void) {
    cyc_tcp_register("select_menu_state",
                     "Select menu: menus drawn with QUESTS and MAP, rows chosen and refused, the menu's rows, height "
                     "and cursor",
                     menu_state);
}
#else
void fax_menu_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_menu) {
    if (!nes_mod_register_function_entry_plugin(hook_ids[0], MENU_OPEN, open_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[1], MENU_ROWS, rows_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[2], DRAW_LENGTH, length_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[3], DRAW_LETTER, letter_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[4], MENU_CHOOSE, choose_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.select-menu", activate)) abort();
}
