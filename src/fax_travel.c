/* Guru travel. USA 57dd23d1, chipx86 byte-exact disassembly.
 * After the mantra (or a new title), a temple's Guru offers to send the hero to
 * another Guru the hero has visited: "All Gurus meditate as one.", then "Shall
 * I send you to another Guru?". A on that opens a menu of the towns (#206).
 * With Save files and a file in play, Save files asks first (fax_files.c): its
 * "Send me to another Guru." opens the menu at once (fax_travel_menu), and
 * after a save the intro and the offer follow its line (fax_travel_begin).
 *
 * The menu is the game's own list, drawn and driven by the routines behind the
 * Select menu and the shops' lists: a box (TextBox_Open) in the dialogue box's
 * columns, as tall as its rows need and never shorter than the dialogue box,
 * whose bottom it keeps, so it covers it and rises above it as a shop's list
 * does; a row a town, every visited Guru's that travel offers from here, in
 * the HUD's capitals as the shops list their items (PlayerMenu_DrawStringLines,
 * the Select menu's rows, through UI_DrawString); the cursor on the first, Up
 * and Down moving it with the cursor sound and wrapping (Menu_UpdateAndDraw);
 * and the shops' wait (IScripts_WaitForMenuInput), which takes only a new
 * press of A or B (Joy1_ChangedButtonMask), so an A held from before chooses
 * nothing. A sends the hero there: "Close your eyes and clear your mind.",
 * then the journey. B goes back: to Save files' choice when that opened the
 * menu (fax_files_guru_again), else "Then go in peace.". Either way the
 * dialogue box opens again over the list (TextBox_OpenForNPC) without closing
 * it first, as after a shop's purchase: Seamless shows only the box opened
 * last, and a close between them would blank the portrait for its frames.
 *
 * The CPU goes through those routines from IScripts_InvokeNextAction by
 * return, with the script parked on the SAY step's END (PARK), and each comes
 * back to IScripts_InvokeNextAction, where the phase says what is next: the
 * box (OPENING); then, since TextBox_Open counts in Temp_Int24, the rows and
 * the wait (MENU); then the dialogue box (DEPART, FAREWELL, or BACK). Each
 * box comes two frames of the portrait's animation after the press, as
 * Maybe_Shop_DrawTextBox's do, so the last cursor or prompt goes. The rows'
 * strings are served at UI_DrawString's two loads, A becoming a town's length
 * and then its letters while the table address (Temp_Int24) is TOWNS, which
 * only this menu uses, as the Select menu serves its two added rows
 * (fax_menu.c): no ROM and no read override, of which none are left.
 *
 * The ROM image is never changed (save states hash it): the conversation runs
 * in the game's own interaction-script interpreter and text box, steered from
 * C at IScripts_InvokeNextAction. Two script steps and one message slot sit in
 * unused ROM, supplied through read overrides, and C rewrites the slot before
 * each line. The journey is the respawn path without its penalties: HP, MP,
 * gold, experience, timed items and quests are kept, and so is the spawn
 * point. The visited Gurus are kept in the file in play (its `gurus` field),
 * or without Save files in faxanadu_gurus.txt.
 * See docs/GAME_INTERNALS.md. */
#include "fax_travel.h"
#include "fax_files.h"
#include "fax_save.h"
#include "cyc_mod.h"
#include "cyc_host_extras.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#ifdef CYC_WITH_SDL
#include "cyc_render.h"
#include "cyc_tcp.h"
#include <SDL.h>
#endif
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { TEMPLES = 8, SLOT_LEN = FAX_TRAVEL_SLOT_LEN };

/* RAM */
#define PPU_BUFFER_READ        0x001Fu /* PPUBuffer_ReadOffset: the write offset again once the NMIs sent it all */
#define PPU_BUFFER_WRITE       0x0020u /* PPUBuffer_WriteOffset */
#define TEMP_INT24             0x00ECu /* UI_DrawString's table address, low byte first (Temp_Int24, _M) */
#define TEXTBOX_X              0x0208u /* TextBox_X, _Y, _Width, _Height: the box last opened */
#define TEXTBOX_CLOSED         0x020Cu /* 0 while a box is open */
#define MENU_CURSOR_POS        0x021Eu
#define ARG_STRINGS_COUNT      0x021Fu /* the menu's rows */
#define DATA_ARRAY             0x0220u /* DataArray: a row's string number, 8 at most */
#define TEMPLE_SPAWN_POINT     0x0439u /* the original's furthest temple; SET_SPAWN_POINT never lowers it */
#define CURRENT_ROM_BANK       0x0100u
#define ISCRIPT_ADDR           0x00DBu /* IScriptOrCHRAddr; the next action is at it + IScriptOffset */
#define ISCRIPT_OFFSET         0x00DDu
/* Fixed bank */
#define SCREEN_FADE_TO_BLACK   0xDA2Fu
#define GAME_INIT_STATE_SPAWN  0xDA7Du
/* Player_Spawn's JSR Game_SpawnInTemple; then
 * JSR Game_SetupEnterBuilding, JMP Game_MainLoop */
#define PLAYER_SPAWN_IN_TEMPLE 0xDB1Du
/* Bank 12: where the Gurus' scripts end, and two steps of ours in unused ROM. */
#define GURU_PASSWORD_END      0xA5FFu        /* ISCRIPT_COMMON_GURU_PASSWORD: 03 22, 14, here 00 */
#define GURU_NEW_RANK_END      0xA602u        /* ISCRIPT_COMMON_GURU_NEW_RANK: 01 23, here 00 */
#define EOLIS_GURU_HAS_RING    0xA0C6u        /* Eolis's Guru, who sets no spawn point: 03 2D, 00 */
#define SAY                    FAX_TRAVEL_SAY /* $AE00: 03 C3 00: show the slot, then END */
#define ASK                    FAX_TRAVEL_ASK /* $AE03: 16 C3 SAY: ask; A jumps to SAY, B goes on to ASK + 4 */
#define PARK                   (SAY + 2)      /* the SAY step's END: the script waits there while the menu runs */
/* The menu's string table, as UI_DrawString is told it (string t at t * 16):
 * Guru travel's own steps, which no table of the game's names. What is read
 * there is replaced (length_hook, letter_hook). */
#define TOWNS                  SAY
#define INVOKE_NEXT_ACTION     0x826Eu /* IScripts_InvokeNextAction */
#define TEXTBOX_OPEN_FOR_NPC 0x81E2u /* TextBox_OpenForNPC: the dialogue box (10,16, 20 x 8), its text rows laid out */
/* IScripts_UpdatePortraitAnimation: a frame, the sprites drawn anew (the
 * portrait, and no cursor or prompt), as Maybe_Shop_DrawTextBox begins */
#define PORTRAIT_FRAME       0x87B0u
/* IScripts_WaitForMenuInput: a frame, then Menu_UpdateAndDraw (Up and Down,
 * the cursor); A returns C clear, B C set */
#define WAIT_FOR_MENU_INPUT  0x84EDu
/* PlayerMenu_DrawStringLines: row X at (TextBox_X + 2, TextBox_Y + 2 + 2X),
 * UI_DrawString with Y = DataArray[X] * 16, for X up to Arg_StringsCount */
#define DRAW_STRING_LINES    0x8E6Bu
#define DRAW_LENGTH          0x8E9Du /* UI_DrawString: STA Temp_Int24_U, after LDA (Temp_Int24),Y (the length) */
/* UI_DrawString's @_drawLoop: JSR Strings_ASCIIToIndex, after
 * LDA (Temp_Int24),Y (a letter) */
#define DRAW_LETTER          0x8EA5u
#define TEXTBOX_OPEN         0x8EF1u /* TextBox_Open: the frame at TextBox_X to _Height, then its background */
/* Bank 13: Messages_Load counts $FF terminators from $8300. Message 193 is
 * followed by glyph data whose run ends at the $FF at $B45A, so message $C3
 * starts at $B45B, in unused ROM. */
#define SLOT_ID              FAX_TRAVEL_SLOT_ID /* $C3 */
#define SLOT                 FAX_TRAVEL_SLOT    /* $B45B */
/* IScript actions */
enum { END = 0x00, SHOW_MESSAGE = 0x03, SET_SPAWN_POINT = 0x06, QUESTION_JUMP_IF_ACCEPTED = 0x16 };
enum { P_C = 0x01, P_Z = 0x02, P_N = 0x80 };
/* The menu's box, in tiles: the dialogue box's columns
 * (TextBox_SetCoordsForNPC: 10,16, 20 x 8); a row a town, a blank row above
 * each, as the Select menu's rows are (PlayerMenu_DrawStringLines: the last
 * just inside the bottom frame), but never shorter than the dialogue box,
 * whose bottom (row 23) it keeps. Seven towns at most: rows 8 to 23, inside
 * what IScriptAction_EndScript clears (2,8, 30 x 20). */
enum { MENU_X = 10, MENU_W = 20, MENU_BOTTOM = 24, MENU_MIN_H = 8 };

static const char *const town[TEMPLES] = { "Eolis",  "Apolune",  "Forepaw",  "Mascon",
                                           "Victim", "Conflate", "Daybreak", "Dartmoor" };
/* The Guru's lines: 16 columns, 4 lines to a box; '\n' breaks a line.
 * TEXT_TO + temple, "To the Guru of <town>?", was asked of each town before
 * the menu (#206), and is kept for a state saved while asking. */
enum { TEXT_INTRO, TEXT_OFFER, TEXT_FAREWELL, TEXT_DEPART, TEXT_TO };
static const char *const lines[] = { "All Gurus\nmeditate as one.", "Shall I send\nyou to another\nGuru?",
                                     "Then go in\npeace.", "Close your eyes\nand clear your\nmind." };

/* One conversation with a Guru. The menu: OPENING while its box opens, MENU
 * while its rows draw and it waits, then DEPART or FAREWELL while the dialogue
 * box opens again for that line, or BACK, for Save files' choice. `back`: B
 * in the menu goes back to Save files' choice, which opened it. ASKING (a
 * town asked about, `asking`) and INTRO_TOWNS (the intro before the towns)
 * come only from a state saved before #206. */
typedef enum { IDLE, INTRO, OFFER, ASKING, FAREWELL, DEPART, INTRO_TOWNS, OPENING, MENU, BACK, PHASES } Phase;
typedef struct {
    uint8_t phase, offer, asking, back;
} Talk;
/* Where the CPU goes from IScripts_InvokeNextAction, by return: nowhere, the
 * menu's box, its rows and wait, or the dialogue box again. Each comes back. */
typedef enum { NO_CHAIN, OPEN_BOX, ROWS, REOPEN } Chain;
/* What the interpreter reads next: the address (or the one it was going to
 * read), the line for the slot (or -1), a chosen temple (or -1), the routines
 * to run first (Chain), and whether Save files' choice comes back (`again`). */
typedef struct {
    uint16_t at;
    int text, go;
    uint8_t chain;
    bool again;
} Steer;

static bool enabled, served, failed;
static uint8_t visited = 1; /* bit per temple; Eolis is where every game begins */
static Talk talk;
static int text = -1;                     /* the line in the slot */
static int dest = -1;                     /* chosen temple, until the journey starts */
static bool journey;                      /* the respawn path is running toward dest */
static unsigned menus, letters, journeys; /* for TCP: menus opened, letters served, journeys begun */

static uint32_t prg(unsigned bank, uint16_t addr) { return bank * 16384u + (addr - 0x8000u); }
static uint8_t q(uint16_t addr) { return cyc_mod_peek(addr); }
static void p(uint16_t addr, uint8_t v) { cyc_mod_poke(addr, v); }

/* Encode a line in the dialogue font, $FF-terminated; false if it would not
 * fit the box or the slot. */
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
    out[n++] = 0xFF;
    while (n < SLOT_LEN) out[n++] = 0xFF;
    return true;
}
static bool line_for(int which, uint8_t out[SLOT_LEN]) {
    char buf[40];
    if (which >= TEXT_TO) snprintf(buf, sizeof buf, "To the Guru of\n%s?", town[which - TEXT_TO]);
    return encode(which >= TEXT_TO ? buf : lines[which], out);
}

/* Gurus to offer from `here`: those visited up to the furthest temple. */
static uint8_t offered(unsigned here, unsigned furthest, uint8_t seen) {
    if (furthest >= TEMPLES) furthest = 0;
    if (here > furthest) furthest = here;
    uint8_t reach = (uint8_t)((2u << furthest) - 1);
    return (uint8_t)(((seen & reach) | 1u | 1u << furthest) & ~(1u << here));
}

/* The menu's rows, in temple order: the towns in `offer`. */
static unsigned menu_rows(uint8_t offer, uint8_t out[TEMPLES]) {
    unsigned n = 0;
    for (unsigned t = 0; t < TEMPLES; ++t)
        if (offer >> t & 1) out[n++] = (uint8_t)t;
    return n;
}
/* The menu's box for `rows` rows: its top row and its height. */
static void menu_box(unsigned rows, uint8_t *y, uint8_t *h) {
    unsigned tall = 2 * rows + 2;
    if (tall < MENU_MIN_H) tall = MENU_MIN_H;
    *h = (uint8_t)tall;
    *y = (uint8_t)(MENU_BOTTOM - tall);
}

/* The menu opens: the script waits at PARK while the box is drawn. */
static void open_menu(Talk *k, bool back, Steer *s) {
    k->phase = OPENING;
    k->back = back;
    s->at = PARK;
    s->chain = OPEN_BOX;
}

/* The conversation, one interpreter step at a time. In the menu, `b` and
 * `row` are what IScripts_WaitForMenuInput returned: B (C set), and the
 * cursor's row. */
static Steer steer(Talk *k, uint16_t at, bool b, unsigned row) {
    Steer s = { at, -1, -1, NO_CHAIN, false };
    if (at == PARK && (k->phase == OPENING || k->phase == MENU || k->phase == BACK)) {
        uint8_t rows[TEMPLES];
        unsigned n = menu_rows(k->offer, rows);
        if (k->phase == OPENING && n) {
            k->phase = MENU; /* the box is up: the rows, and the wait */
            s.chain = ROWS;
        } else if (k->phase == BACK) {
            k->phase = IDLE; /* the dialogue box is up again: Save files' choice */
            s.again = true;
        } else if (k->phase == MENU && !b && n) {
            k->phase = DEPART;
            s.go = rows[row < n ? row : 0];
            s.at = SAY;
            s.text = TEXT_DEPART;
            s.chain = REOPEN;
        } else if (k->back) {
            k->phase = BACK;
            s.chain = REOPEN;
        } else {
            k->phase = FAREWELL;
            s.at = SAY;
            s.text = TEXT_FAREWELL;
            s.chain = REOPEN;
        }
    } else if (at == GURU_PASSWORD_END || at == GURU_NEW_RANK_END || at == EOLIS_GURU_HAS_RING + 2) {
        if (fax_files_guru_choice()) return s; /* Save files asks first, and calls fax_travel_menu or _begin */
        if (k->offer) k->phase = INTRO, s.at = SAY, s.text = TEXT_INTRO;
        else k->phase = IDLE;
    } else if (at == SAY + 2) { /* a line of ours has been read */
        if (k->phase == INTRO) k->phase = OFFER, s.at = ASK, s.text = TEXT_OFFER;
        else if (k->phase == INTRO_TOWNS && k->offer) open_menu(k, true, &s); /* saved before #206 */
        else k->phase = IDLE;                                                 /* the farewell or the departure: END */
    } else if (at == SAY) {                                                   /* A on ASK */
        if (k->phase == OFFER) open_menu(k, false, &s);
        else if (k->phase == ASKING) { /* saved before #206, asking about a town: go there */ k->phase = DEPART;
            s.text = TEXT_DEPART;
            s.go = k->asking;
        }
    } else if (at == ASK + 4) { /* B on ASK */
        if (k->phase == OFFER) {
            k->phase = IDLE;
            s.at = SAY + 2; /* an END: B on the offer closes the box */
        } else if (k->phase == ASKING) {
            k->phase = FAREWELL;
            s.at = SAY;
            s.text = TEXT_FAREWELL;
        }
    }
    return s;
}

static bool serve(int which) {
    uint8_t bytes[SLOT_LEN];
    if (!line_for(which, bytes)) return false;
    for (unsigned i = 0; i < SLOT_LEN; ++i)
        if (!cyc_mod_set_prg_read_override(prg(13, (uint16_t)(SLOT + i)), 0xFF, bytes[i])) return false;
    text = which;
    return true;
}
static bool serve_script(bool on) {
    static const uint8_t steps[] = { SHOW_MESSAGE, SLOT_ID,    END,     QUESTION_JUMP_IF_ACCEPTED,
                                     SLOT_ID,      SAY & 0xFF, SAY >> 8 };
    for (unsigned i = 0; i < sizeof steps; ++i)
        if (!cyc_mod_set_prg_read_override(prg(12, (uint16_t)(SAY + i)), 0xFF, on ? steps[i] : 0xFF)) return false;
    return true;
}
static void unserve(void) {
    serve_script(false);
    for (unsigned i = 0; i < SLOT_LEN; ++i) cyc_mod_set_prg_read_override(prg(13, (uint16_t)(SLOT + i)), 0xFF, 0xFF);
    text = -1;
}

static bool sidecar_path(const char *name, char out[1100]) {
#ifdef CYC_WITH_SDL
    char *base = SDL_GetBasePath();
    if (!base) return false;
    int n = snprintf(out, 1100, "%s%s", base, name);
    SDL_free(base);
#else
    int n = snprintf(out, 1100, "%s", name);
#endif
    return n > 0 && n < 1100;
}
/* faxanadu_gurus.txt: the towns whose Gurus this playthrough has visited. */
static void load_visited(void) {
    char path[1100], line[64];
    visited = 1;
    if (!cyc_host_saves_enabled() || !sidecar_path("faxanadu_gurus.txt", path)) return;
    FILE *f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        for (int t = 0; t < TEMPLES; ++t)
            if (!strcmp(line, town[t])) visited |= (uint8_t)(1u << t);
    }
    fclose(f);
}
static void save_visited(void) {
    char path[1100], temp[1120];
    if (fax_save_enabled()) return; /* the file in play keeps them (on_save) */
    if (!cyc_host_saves_enabled() || !sidecar_path("faxanadu_gurus.txt", path)) return;
    snprintf(temp, sizeof temp, "%s.tmp", path);
    FILE *f = fopen(temp, "w");
    if (!f) return;
    for (int t = 0; t < TEMPLES; ++t)
        if (visited >> t & 1) fprintf(f, "%s\n", town[t]);
    if (fclose(f) != 0) {
        remove(temp);
        return;
    }
#ifdef _WIN32
    extern bool faxanadu_replace_file(const char *, const char *);
    if (!faxanadu_replace_file(temp, path)) remove(temp);
#else
    if (rename(temp, path) != 0) remove(temp);
#endif
}

/* Save files: the visited Gurus are the file's `gurus` field. */
static void on_save(FaxSaveEvent event) {
    char gurus[FAX_SAVE_VALUE];
    if (event == FAX_SAVE_NEW_GAME) visited = 1;
    else if (event == FAX_SAVE_CONTINUED) visited = (uint8_t)(fax_save_gurus_parse(fax_save_get("gurus")) | 1u);
    else {
        fax_save_gurus_format(visited, gurus);
        fax_save_set("gurus", gurus);
    }
}

/* The furthest temple: the highest visited Guru's, or the spawn point, which
 * is the furthest temple in the original but the Guru last saved with under
 * Save files. */
static unsigned furthest_temple(void) {
    unsigned furthest = q(TEMPLE_SPAWN_POINT);
    if (furthest >= TEMPLES) furthest = 0;
    for (unsigned t = furthest + 1; t < TEMPLES; ++t)
        if (visited >> t & 1) furthest = t;
    return furthest;
}

static void meet(unsigned here) {
    if (!(visited >> here & 1)) {
        visited |= (uint8_t)(1u << here);
        save_visited();
    }
    talk.phase = IDLE;
    talk.offer = offered(here, furthest_temple(), visited);
}

/* The handled instruction returns into each of `chain`'s `n` routines in
 * turn, as pushed RTS addresses: each routine's RTS goes on to the next, and
 * S ends where it was. */
static int go(const uint16_t *chain, unsigned n) {
    CycModRegs r;
    cyc_mod_regs(&r);
    uint8_t s = r.s;
    for (unsigned i = n; i-- > 0;) { /* pushed as a JSR would: high byte first; the first on top */
        uint16_t v = (uint16_t)(chain[i] - 1);
        p((uint16_t)(0x100u | s), (uint8_t)(v >> 8));
        s = (uint8_t)(s - 1);
        p((uint16_t)(0x100u | s), (uint8_t)v);
        s = (uint8_t)(s - 1);
    }
    r.s = s;
    cyc_mod_set_regs(&r);
    return 1;
}

/* Send the CPU through a chain of the game's routines, set up for them, back
 * to IScripts_InvokeNextAction. A box gives way to another two frames after
 * the choice, as a shop's list does (Maybe_Shop_DrawTextBox): the sprites are
 * drawn anew on the first, without the cursor or the question's prompt, and
 * sent to the PPU on the second, since the NMI copies them only on a frame
 * WaitForNextFrame asked for, and none comes while a box is drawn. Returns
 * what the hook returns. */
static int run_chain(uint8_t chain) {
    static const uint16_t open_box[] = { PORTRAIT_FRAME, PORTRAIT_FRAME, TEXTBOX_OPEN, INVOKE_NEXT_ACTION };
    static const uint16_t rows_and_wait[] = { DRAW_STRING_LINES, WAIT_FOR_MENU_INPUT, INVOKE_NEXT_ACTION };
    static const uint16_t reopen[] = { PORTRAIT_FRAME, PORTRAIT_FRAME, TEXTBOX_OPEN_FOR_NPC, INVOKE_NEXT_ACTION };
    uint8_t rows[TEMPLES], y, h;
    unsigned n = menu_rows(talk.offer, rows);
    switch (chain) {
    case OPEN_BOX: /* TextBox_Open draws the box at TextBox_X to _Height */
        menu_box(n, &y, &h);
        p(TEXTBOX_X, MENU_X);
        p(TEXTBOX_X + 1, y);
        p(TEXTBOX_X + 2, MENU_W);
        p(TEXTBOX_X + 3, h);
        ++menus;
        return go(open_box, 4);
    case ROWS: /* each row's string is its town's, from TOWNS; the cursor on the first */
        p(TEMP_INT24, (uint8_t)TOWNS);
        p(TEMP_INT24 + 1, (uint8_t)(TOWNS >> 8));
        for (unsigned i = 0; i < n; ++i) p((uint16_t)(DATA_ARRAY + i), rows[i]);
        p(ARG_STRINGS_COUNT, (uint8_t)n);
        p(MENU_CURSOR_POS, 0);
        return go(rows_and_wait, 3);
    case REOPEN: /* its text rows are laid out afresh, from the top */
        fax_files_box_reopened();
        return go(reopen, 4);
    }
    return 0;
}

static void point(uint16_t at) {
    p(ISCRIPT_ADDR, (uint8_t)at);
    p(ISCRIPT_ADDR + 1, (uint8_t)(at >> 8));
    p(ISCRIPT_OFFSET, 0);
}

/* Do what steer() decided, at the script's place `at`. */
static int apply(Steer *s, uint16_t at) {
    if (s->again) { /* B in the menu: back to Save files' choice, or a farewell */
        uint16_t to = fax_files_guru_again();
        if (to) s->at = to;
        else talk.phase = FAREWELL, s->at = SAY, s->text = TEXT_FAREWELL;
    }
    if (s->text >= 0 && !serve(s->text)) {
        fprintf(stderr, "[travel] cannot supply the Guru's line; Guru travel is off\n");
        failed = true;
        talk.phase = IDLE;
        s->at = at;
        s->go = -1;
        s->chain = NO_CHAIN;
    }
    if (s->go >= 0) dest = s->go;
    if (s->at != at) point(s->at);
    return s->chain ? run_chain(s->chain) : 0;
}

/* IScripts_InvokeNextAction: meet the Guru, and steer the conversation. */
static int next_action_hook(uint16_t addr) {
    (void)addr;
    if (!served) return 0;
    uint16_t at = (uint16_t)((q(ISCRIPT_ADDR) | q(ISCRIPT_ADDR + 1) << 8) + q(ISCRIPT_OFFSET));
    /* A script begins (IScripts_Begin leaves IScriptOffset 1; every step of
     * ours is entered at 0): a conversation of ours that B ended is over. B on
     * "All Gurus meditate as one." leaves INTRO or INTRO_TOWNS, and the next
     * END of the SAY step that anyone else shows a line through (Block's
     * tutorial, the quest log, the Guru's Elixir line) would have been taken
     * for the intro's, and steered to the offer. */
    if (q(ISCRIPT_OFFSET) == 1) talk.phase = IDLE;
    if (q(at) == SET_SPAWN_POINT && q((uint16_t)(at + 1)) < TEMPLES) meet(q((uint16_t)(at + 1)));
    else if (at == EOLIS_GURU_HAS_RING) meet(0);
    CycModRegs r;
    cyc_mod_regs(&r);
    Steer s = steer(&talk, at, r.p & P_C, q(MENU_CURSOR_POS));
    return apply(&s, at);
}

/* UI_DrawString's loads, while the menu draws its rows: the string at Y in
 * TOWNS is town Y / 16's, its length at Y % 16 == 0, then its letters, in
 * capitals (the HUD's font has no others: Strings_ASCIIToIndex takes a
 * letter to its tile less $31). */
static bool here(void) { return enabled && served && !failed && !cyc_mod_isolated(); }
static const char *town_at(uint8_t y, unsigned *at) {
    if (!here() || talk.phase != MENU || (q(TEMP_INT24) | q(TEMP_INT24 + 1) << 8) != TOWNS) return NULL;
    *at = y & 15u;
    return (unsigned)(y >> 4) < TEMPLES ? town[y >> 4] : NULL;
}
static void load(CycModRegs *r, uint8_t value) { /* A as LDA leaves it: N and Z too */
    r->a = value;
    r->p = (uint8_t)((r->p & ~(P_N | P_Z)) | (value & P_N) | (value ? 0 : P_Z));
    cyc_mod_set_regs(r);
}
static int length_hook(uint16_t addr) {
    (void)addr;
    CycModRegs r;
    cyc_mod_regs(&r);
    unsigned at;
    const char *name = town_at(r.y, &at);
    if (name && at == 0) load(&r, (uint8_t)strlen(name));
    return 0;
}
static int letter_hook(uint16_t addr) {
    (void)addr;
    CycModRegs r;
    cyc_mod_regs(&r);
    unsigned at;
    const char *name = town_at(r.y, &at);
    if (!name || at == 0 || at > strlen(name)) return 0;
    load(&r, (uint8_t)toupper((unsigned char)name[at - 1]));
    ++letters;
    return 0;
}

/* GameLoop_CheckShowPlayerMenu, once the conversation has closed: return
 * into the respawn path with a fresh stack. */
static int main_loop_hook(uint16_t addr) {
    (void)addr;
    if (dest < 0 || journey || q(CURRENT_ROM_BANK) != 14) return 0;
    static const uint16_t path[] = { SCREEN_FADE_TO_BLACK, GAME_INIT_STATE_SPAWN, PLAYER_SPAWN_IN_TEMPLE };
    uint16_t sp = 0x1FF;
    for (int i = 2; i >= 0; --i) {
        uint16_t r = (uint16_t)(path[i] - 1);
        p(sp--, (uint8_t)(r >> 8));
        p(sp--, (uint8_t)r);
    }
    CycModRegs regs;
    cyc_mod_regs(&regs);
    regs.s = (uint8_t)sp;
    cyc_mod_set_regs(&regs);
    journey = true;
    ++journeys;
    return 1;
}

/* Game_SpawnInTemple: lay out the chosen temple, keeping the furthest one as
 * the spawn point. */
static int spawn_hook(uint16_t addr) {
    if (!journey) return 0;
    uint8_t furthest = q(TEMPLE_SPAWN_POINT);
    p(TEMPLE_SPAWN_POINT, (uint8_t)dest);
    CycModRegs regs;
    cyc_mod_regs(&regs);
    bool ok = cyc_mod_call_commit(addr, &regs);
    p(TEMPLE_SPAWN_POINT, furthest);
    dest = -1;
    journey = false;
    return ok;
}

/* Game_Start: a new game has met no Guru but Eolis's. */
static int new_game_hook(uint16_t addr) {
    (void)addr;
    if (visited != 1) {
        visited = 1;
        save_visited();
    }
    return 0;
}

static const char *const hook_ids[] = { "faxanadu.travel.next-action", "faxanadu.travel.main-loop",
                                        "faxanadu.travel.spawn",       "faxanadu.travel.new-game",
                                        "faxanadu.travel.length",      "faxanadu.travel.letter" };
static void hooks(bool on) {
    for (unsigned i = 0; i < sizeof hook_ids / sizeof hook_ids[0]; ++i)
        nes_mod_set_function_hook_enabled(hook_ids[i], on);
}
static void deactivate(void) {
    if (served) unserve();
    enabled = served = failed = journey = false;
    dest = -1;
    memset(&talk, 0, sizeof talk);
    menus = letters = journeys = 0;
    hooks(false);
}
static void activate(void) {
    enabled = true;
    load_visited();
    hooks(true);
    nes_mod_set_local_only("Guru travel", 1);
}

/* Supply our script steps once the cart is loaded: at power-on, before any
 * save state is loaded, or on the first frame after activation in play. */
void fax_travel_step(void) {
    if (!enabled || served || failed) return;
    if (!serve_script(true)) {
        fprintf(stderr, "[travel] unexpected ROM layout; Guru travel is off\n");
        unserve();
        failed = true;
        hooks(false);
        return;
    }
    served = true;
}

/* Saved with the machine: the journey and the conversation, so a state loads
 * into it as it was. A state saved while the menu opens, waits or gives way
 * has the CPU in the game's routines with their returns on the stack, which
 * come back to IScripts_InvokeNextAction in that phase. Version 2 (#206):
 * [0] the version, [1] the chosen temple ($FF none), [2] the journey, [3]
 * the phase, [4] the Gurus offered, [5] the town asked about, [6] the line in
 * the slot ($FF none), [7] whether B goes back to Save files' choice. Version
 * 1 had no version byte and no [7], and the towns asked one at a time: it is
 * [1] to [6] at [0] to [5], and loads into its question (ASKING), where A
 * goes there and B says farewell. */
enum { RECORD = 8, RECORD_V1 = 6, VERSION = 2 };
static int state_get(uint8_t *buf, int cap) {
    if (cap < RECORD) return -1;
    const uint8_t rec[RECORD] = { VERSION,     (uint8_t)(dest < 0 ? 0xFF : dest), journey,  talk.phase, talk.offer,
                                  talk.asking, (uint8_t)(text < 0 ? 0xFF : text), talk.back };
    memcpy(buf, rec, RECORD);
    return RECORD;
}
static int state_validate(const uint8_t *buf, int len) {
    if (len == 0) return 1;
    const uint8_t *r = len == RECORD && buf[0] == VERSION ? buf + 1 : len == RECORD_V1 ? buf : NULL;
    if (!r) return 0;
    unsigned phases = len == RECORD ? PHASES : INTRO_TOWNS + 1;
    return (r[0] < TEMPLES || r[0] == 0xFF) && r[1] <= 1 && r[2] < phases && r[4] < TEMPLES &&
           (r[5] < TEXT_TO + TEMPLES || r[5] == 0xFF) && (len == RECORD_V1 || r[6] <= 1);
}
static int state_set(const uint8_t *buf, int len) {
    if (!state_validate(buf, len)) return 0;
    const uint8_t *r = len == RECORD ? buf + 1 : buf;
    dest = len && r[0] != 0xFF ? r[0] : -1;
    journey = len && r[1];
    talk = len ? (Talk){ r[2], r[3], r[4], len == RECORD && r[6] } : (Talk){ 0 };
    if (served && len && r[5] != 0xFF) serve(r[5]); /* overrides are not machine state */
    return 1;
}

/* For the map: the Gurus visited, a bit per temple (Eolis's always). 0 with Guru travel off. */
uint8_t fax_travel_visited(void) { return enabled ? visited : 0; }

/* For Save files, which offers saving first while a file is in play. */
bool fax_travel_can_offer(void) { return enabled && served && !failed && talk.offer; }
uint16_t fax_travel_say_step(void) { return enabled && served && !failed ? SAY : 0; }
uint16_t fax_travel_begin(void) {
    if (!fax_travel_can_offer()) return 0;
    if (!serve(TEXT_INTRO)) {
        fprintf(stderr, "[travel] cannot supply the Guru's line; Guru travel is off\n");
        failed = true;
        talk.phase = IDLE;
        return 0;
    }
    talk.phase = INTRO;
    return SAY;
}
int fax_travel_menu(void) {
    if (!fax_travel_can_offer()) return 0;
    Steer s = { PARK, -1, -1, NO_CHAIN, false };
    open_menu(&talk, true, &s);
    point(PARK);
    return run_chain(s.chain);
}

#ifdef CYC_WITH_SDL
/* The background tile the last frame showed in cell (col, row): through that
 * line's scroll, in the nametable it fetched from (as fax_shops.c reads it). */
static unsigned shown_tile(int col, int row) {
    int line = row * 8 + 4;
    int x = (cyc_render_line_scroll_x(line) + col * 8 + 4) & 511;
    int y = cyc_render_line_scroll_y(line);
    if (y < 0 || y >= 480) return 0x100;
    uint16_t table = (uint16_t)(0x2000u + (x >= 256 ? 0x400u : 0) + (y >= 240 ? 0x800u : 0));
    return cyc_render_nametable((uint16_t)(table + (y % 240) / 8 * 32 + (x & 255) / 8));
}
/* The menu's box is the box up: open, where the menu puts it for its rows. */
static bool menu_box_up(unsigned n) {
    uint8_t y, h;
    menu_box(n, &y, &h);
    return !q(TEXTBOX_CLOSED) && q(TEXTBOX_X) == MENU_X && q(TEXTBOX_X + 1) == y && q(TEXTBOX_X + 2) == MENU_W &&
           q(TEXTBOX_X + 3) == h;
}
/* Row i of the menu as the screen shows it: its first 8 cells, in the HUD
 * font's capitals (a space is tile 0, a letter its ASCII less $31). */
static void shown_row(unsigned i, unsigned n, char out[9]) {
    uint8_t y, h;
    menu_box(n, &y, &h);
    int k = 0;
    for (; k < 8; ++k) {
        unsigned tile = shown_tile(MENU_X + 2 + k, y + 2 + 2 * (int)i);
        out[k] = tile == 0 ? ' ' : tile >= 0x10 && tile <= 0x29 ? (char)(tile + 0x31) : '?';
    }
    while (k > 0 && out[k - 1] == ' ') --k;
    out[k] = 0;
}
static void travel_state(int id, const char *line) {
    (void)line;
    uint8_t rows[TEMPLES];
    unsigned n = menu_rows(talk.offer, rows);
    bool in_menu = talk.phase == OPENING || talk.phase == MENU;
    /* The menu waits for a button: its rows drawn, the PPU buffer sent. */
    bool up = talk.phase == MENU && menu_box_up(n) && q(PPU_BUFFER_READ) == q(PPU_BUFFER_WRITE);
    char menu[48] = "", shown[96] = "";
    for (unsigned i = 0; in_menu && i < n; ++i) {
        char row[9];
        size_t a = strlen(menu), b = strlen(shown);
        snprintf(menu + a, sizeof menu - a, "%s%u", i ? "," : "", rows[i]);
        shown_row(i, n, row);
        snprintf(shown + b, sizeof shown - b, "%s\"%s\"", i ? "," : "", row);
    }
    char fields[512];
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"served\":%s,\"visited\":%u,\"furthest\":%u,\"phase\":%u,\"offer\":%u,"
             "\"asking\":%u,\"text\":%d,\"dest\":%d,\"journey\":%s,\"menu\":[%s],\"shown\":[%s],\"menu_up\":%s,"
             "\"cursor\":%u,\"back\":%s,\"menus\":%u,\"letters\":%u,\"journeys\":%u",
             enabled ? "true" : "false", served ? "true" : "false", visited, q(TEMPLE_SPAWN_POINT), talk.phase,
             talk.offer, talk.asking, text, dest, journey ? "true" : "false", menu, up ? shown : "",
             up ? "true" : "false", q(MENU_CURSOR_POS), talk.back ? "true" : "false", menus, letters, journeys);
    cyc_tcp_ok(id, fields);
}
void fax_travel_tcp_setup(void) {
    cyc_tcp_register("travel_state",
                     "Guru travel: visited Gurus (bit per temple), conversation, the menu (rows, shown, cursor), "
                     "journey",
                     travel_state);
}
#else
void fax_travel_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_travel) {
    if (!nes_mod_register_function_entry_plugin("faxanadu.travel.next-action", INVOKE_NEXT_ACTION, next_action_hook) ||
        !nes_mod_register_function_entry_plugin("faxanadu.travel.main-loop", 0xe016, main_loop_hook) ||
        !nes_mod_register_function_entry_plugin("faxanadu.travel.spawn", 0xdd61, spawn_hook) ||
        !nes_mod_register_function_entry_plugin("faxanadu.travel.new-game", 0xdb26, new_game_hook) ||
        !nes_mod_register_function_entry_plugin("faxanadu.travel.length", DRAW_LENGTH, length_hook) ||
        !nes_mod_register_function_entry_plugin("faxanadu.travel.letter", DRAW_LETTER, letter_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.guru-travel", activate) ||
        !nes_mod_register_savestate_hook("faxanadu.travel", state_get, state_set) ||
        !nes_mod_register_savestate_validator("faxanadu.travel", state_validate) || !fax_save_listen(on_save))
        abort();
}
