/* Save files: three files instead of mantras, as in Zelda. USA 57dd23d1,
 * chipx86 byte-exact disassembly.
 *
 * The title screen offers three files, drawn over its picture in the game's
 * own font and text box frame. Its loop far-calls StartScreen_CheckHandleInput
 * once a frame; the hook there reads the pad for the file select instead, and
 * hides Start from the loop's own check until a file is chosen. Then:
 *   - an empty file starts a new game, through the stock Start path;
 *   - a saved file continues, through the stock Continue path with the mantra
 *     screen skipped: the hook at PasswordScreen_Show puts the file's progress
 *     where Password_Load would, Player_SetInitialExpAndGold runs as after a
 *     mantra, and the hook at its resume ($FC95, JMP Player_Spawn) puts back
 *     the exact gold, experience and next title. Player_Spawn does the rest,
 *     as for a mantra: full HP and MP, the timed items and the Mattock quest
 *     cleared, and the temple at TempleSpawnPoint.
 * Where a Guru would show the mantra (and after a new title, and at Eolis's
 * Guru with the Ring of Elf), he offers a choice in the shops' Buy/Sell menu:
 * "Remember my journey." or "Send me to another Guru." (a question, "Shall I
 * remember your journey?", when there is nowhere to send you). B leaves. The
 * interpreter is steered to a step of ours in unused ROM; the menu's message
 * ID is an immediate operand, read through an override for that one menu.
 * Saving makes this Guru's temple the spawn point (the Guru you last saved
 * with is where you continue and where death sends you), then Guru travel
 * offers to send you on. Talking to a Guru no longer raises the spawn point:
 * SET_SPAWN_POINT only raises the furthest temple, which the file keeps.
 * Every respawn (death, a Guru travel journey, continuing) saves the file too,
 * keeping its spawn point. The lines go in message $C3, the slot Guru travel
 * uses too: each feature writes its line there just before the step that
 * shows it. The ROM image is never changed.
 * See docs/GAME_INTERNALS.md, "Save files". */
#include "fax_files.h"
#include "fax_save.h"
#include "fax_text.h"
#include "fax_travel.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "cyc_host_extras.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#ifdef CYC_WITH_SDL
#include "cyc_tcp.h"
#include <SDL.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef FAX_FILES_LOG
#define FAX_FILES_LOG(...) fprintf(stderr, __VA_ARGS__)
#endif

/* RAM */
#define JOY1_CHANGED        0x0019u   /* Joy1_ChangedButtonMask: buttons newly pressed */
#define AREA_CURRENT_AREA   0x0024u
#define AREA_CURRENT_SCREEN 0x0063u
#define ISCRIPT_ADDR        0x00DBu   /* IScriptOrCHRAddr; the next action is at it + IScriptOffset */
#define ISCRIPT_OFFSET      0x00DDu
#define TEXTBOX_CLOSED      0x020Cu   /* 0 while a text box or the menu is open */
#define TEXTBOX_LINE_SCROLL 0x021Au   /* TextBox_LineScrollOffset: the CHR line block of the box's top row */
#define EXPERIENCE          0x0390u
#define GOLD                0x0392u
#define TEMPLE_SPAWN_POINT  0x0439u   /* with Save files: the temple of the Guru last saved with */
#define NEXT_PLAYER_TITLE   0x04EDu
#define START_SCREEN_CHOICE 0x0687u   /* DAT_0687: 0 Start, 1 Continue */
/* Bank 12 */
#define START_SCREEN_INPUT   0x9F44u  /* StartScreen_CheckHandleInput: Select toggles the choice, draws the arrow */
#define PASSWORD_SCREEN_SHOW 0x909Du  /* PasswordScreen_Show: the mantra screen, then Password_Load */
#define BUY_SELL_MESSAGE     0x8631u  /* IScriptAction_ShowBuySellMenu: LDA #$10 (this byte), JSR IScripts_LoadAndShowMessage */
#define GURU_MANTRA          0xA5FEu  /* ISCRIPT_COMMON_GURU_PASSWORD ($A5FC): 03 22, here 14, 00 */
#define GURU_NEW_RANK_END    0xA602u  /* ISCRIPT_COMMON_GURU_NEW_RANK ($A600): 01 23, here 00 */
#define EOLIS_GURU_RING_END  0xA0C8u  /* Eolis's Guru with the Ring of Elf ($A0C6): 03 2D, here 00 */
#define STOCK_END            0xA5FFu  /* an END: where the mantra's script ends */
#define STEP                 0xAE10u  /* the choice: 0F SAY (menu) or 16 C3 SAY (question); B on the question goes on to + 4 */
#define OWN_SAY              0xAE15u  /* 03 C3 00, when Guru travel's own is not there */
/* Bank 13: message $C3, Guru travel's line slot (fax_travel.c) */
#define SLOT_ID  0xC3u
#define SLOT     0xB45Bu
#define BUY_SELL_MESSAGE_ID 0x10u     /* "Come here / to buy. / Come here / to sell." */
/* Fixed bank */
#define START_SPAWN       0xFC95u   /* Game_ShowStartScreen's @_afterSetExpGoldFarJump: JMP Player_Spawn */
#define SPAWNED           0xDB20u   /* Player_Spawn, after JSR Game_SpawnInTemple: JSR Game_SetupEnterBuilding */
#define SOUND_PLAY_EFFECT 0xD0E4u   /* Sound_PlayEffect: A = the effect */
#define CLEAR_SCROLLED    0xF439u   /* TextBox_ClearPasswordSize (from Messages_Load), after STA TextBox_LineScrollOffset */
/* Joypad bits */
enum { BUTTON_A = 0x80, BUTTON_B = 0x40, BUTTON_SELECT = 0x20, BUTTON_START = 0x10, BUTTON_UP = 0x08, BUTTON_DOWN = 0x04 };
enum { SLOT_LEN = 48, BUILDINGS = 4, SOUND_CURSOR = 0x0B };
/* IScript actions */
enum { END = 0x00, UNSKIPPABLE_MESSAGE = 0x01, SHOW_MESSAGE = 0x03, SET_SPAWN_POINT = 0x06, BUY_SELL_MENU = 0x0F,
       MANTRA = 0x14, QUESTION_JUMP_IF_ACCEPTED = 0x16 };

static const uint8_t temple_screen[FAX_SAVE_TEMPLES] = {   /* DEST_SCREEN_FOR_TEMPLE_SPAWN */
    0x02, 0x0B, 0x10, 0x1E, 0x23, 0x2B, 0x33, 0x3C };

/* The Guru's lines: 16 columns, 4 lines; '\n' breaks a line. The menu's two
 * choices start on lines 1 and 3, where its cursor points. */
enum { TEXT_MENU, TEXT_QUESTION, TEXT_SAVED, TEXTS };
static const char *const guru_text[TEXTS] = {
    "Remember my\njourney.\nSend me to\nanother Guru.", "Shall I remember\nyour journey?",
    "I will remember\nyour journey." };

/* Where a Guru offers the choice: the action there and the two before it. */
typedef enum { AT_NONE, AT_MANTRA, AT_NEW_RANK, AT_EOLIS } Trigger;
static const struct { uint16_t at; uint8_t stock[3]; } trigger[] = {
    [AT_MANTRA] = { GURU_MANTRA, { SHOW_MESSAGE, 0x22, MANTRA } },
    [AT_NEW_RANK] = { GURU_NEW_RANK_END, { UNSKIPPABLE_MESSAGE, 0x23, END } },
    [AT_EOLIS] = { EOLIS_GURU_RING_END, { SHOW_MESSAGE, 0x2D, END } },
};
/* The conversation, once a Guru offers the choice. */
typedef enum { GURU_IDLE, GURU_MENU, GURU_QUESTION, GURU_SAVED, GURU_PHASES } GuruPhase;

/* ---- the file select ---- */

typedef enum { PICK, ERASE, CONFIRM } Mode;
typedef struct { uint8_t mode, cursor, target; } Menu;   /* cursor: files 0-2, 3 the Erase row */
typedef struct { FaxSaveStatus status; FaxSaveProgress p; } Slot;
enum { MENU_ROWS = 10, MENU_COLS = 26, ERASE_ROW = 3 };

static bool enabled, failed;
static Menu menu;
static Slot slots[FAX_SAVE_FILES];
static bool title_up;          /* the file select is up: the title's loop has called us */
static uint8_t seen = 255;     /* frames since the title's loop last called us */
static int continuing;         /* the file being continued, until it spawns */
static FaxSaveProgress pending;
static uint8_t furthest;       /* the furthest temple reached in the file in play */
static uint8_t guru;           /* GuruPhase */
static uint16_t say_step;      /* the step that shows the saved line: Guru travel's, or OWN_SAY */
static bool held;              /* SET_SPAWN_POINT is running: put the spawn point back after it */
static uint8_t held_spawn;
static bool stepped, own_say, menu_message;   /* overrides of ours that are set */
static bool keep_rows;         /* our lines are in a box that may have scrolled: keep its rows */
static uint8_t rows;           /* TextBox_LineScrollOffset as the box's last line left it */
static unsigned saves, save_failures, imports;
static int last_spawn = -1;
static char title_names[16][10];
static bool names_loaded;

static uint8_t q(uint16_t addr) { return cyc_mod_peek(addr); }
static uint32_t prg(unsigned bank, uint16_t addr) { return bank * 16384u + (addr - 0x8000u); }

static void scan(void) {
    for (int i = 0; i < FAX_SAVE_FILES; ++i) {
        FaxSaveRecord r;
        slots[i].status = fax_save_file_read(i + 1, &r);
        if (slots[i].status == FAX_SAVE_OK && !fax_save_progress_get(&r, &slots[i].p)) slots[i].status = FAX_SAVE_MALFORMED;
    }
}
static bool has_data(int i) { return slots[i].status != FAX_SAVE_MISSING; }

/* A press on the file select. Returns the file to play (1-3), or 0. */
static int menu_press(Menu *m, uint8_t pressed, bool *moved) {
    *moved = false;
    if (pressed & (BUTTON_UP | BUTTON_DOWN)) {
        int rows = m->mode == PICK ? 4 : 3, step = pressed & BUTTON_UP ? rows - 1 : 1;
        if (m->mode == CONFIRM) return 0;
        m->cursor = (uint8_t)((m->cursor + step) % rows);
        *moved = true;
        return 0;
    }
    bool go = pressed & (BUTTON_A | BUTTON_START), back = pressed & BUTTON_B;
    switch (m->mode) {
    case PICK:
        if (!go) return 0;
        if (m->cursor == ERASE_ROW) {
            for (int i = 0; i < FAX_SAVE_FILES; ++i)
                if (has_data(i)) { m->mode = ERASE; m->cursor = (uint8_t)i; *moved = true; break; }
            return 0;
        }
        if (slots[m->cursor].status == FAX_SAVE_OK || slots[m->cursor].status == FAX_SAVE_MISSING) return m->cursor + 1;
        return 0;
    case ERASE:
        if (back) { m->mode = PICK; m->cursor = ERASE_ROW; *moved = true; }
        else if (go && has_data(m->cursor)) { m->mode = CONFIRM; m->target = m->cursor; *moved = true; }
        return 0;
    case CONFIRM:
        if (go) {
            if (!fax_save_file_erase(m->target + 1)) FAX_FILES_LOG("[files] cannot erase file %d\n", m->target + 1);
            scan();
            m->mode = PICK; m->cursor = m->target; *moved = true;
        } else if (back) { m->mode = ERASE; m->cursor = m->target; *moved = true; }
        return 0;
    }
    return 0;
}

static void load_names(void) {
    if (names_loaded) return;
    for (int t = 0; t < 16; ++t) snprintf(title_names[t], sizeof title_names[t], "Title %d", t);
    size_t need = cyc_mod_copy_cart_payload(NULL, 0);
    uint8_t *payload = need >= 16u * 16384u ? malloc(need) : NULL;
    if (payload && cyc_mod_copy_cart_payload(payload, need) == need) {
        /* PLAYER_TITLE_STRINGS ($F649, bank 15): 16 characters a title, space-padded. */
        const uint8_t *s = payload + 15u * 16384u + (0xF649u - 0xC000u);
        for (int t = 0; t < 16; ++t) {
            int n = 0;
            for (; n < 9 && s[t * 16 + n] > 0x20 && s[t * 16 + n] < 0x7F; ++n) title_names[t][n] = (char)s[t * 16 + n];
            if (n) title_names[t][n] = 0;
        }
    }
    free(payload);
    names_loaded = true;
}

/* The text of row `row` of the file select (MENU_COLS wide at most), and
 * whether the cursor is on it. */
static void menu_row(const Menu *m, int row, char out[MENU_COLS + 1], bool *cursor) {
    char text[64] = "";
    int file = row / 3, part = row % 3;
    *cursor = false;
    if (row == MENU_ROWS - 1) {
        if (m->mode == PICK) snprintf(text, sizeof text, "  Erase a file");
        else if (m->mode == ERASE) snprintf(text, sizeof text, "  Erase which file?");
        else snprintf(text, sizeof text, "  Erase file %d? A yes B no", m->target + 1);
        *cursor = m->mode == PICK && m->cursor == ERASE_ROW;
    } else if (file < FAX_SAVE_FILES && part < 2) {
        const Slot *s = &slots[file];
        if (part == 0) {
            *cursor = (m->mode == CONFIRM ? m->target : m->cursor) == file && !(m->mode == PICK && m->cursor == ERASE_ROW);
            if (s->status == FAX_SAVE_OK)
                snprintf(text, sizeof text, "  %d %-12s %s", file + 1, title_names[s->p.title & 15], fax_save_town(s->p.furthest));
            else if (s->status == FAX_SAVE_MISSING) snprintf(text, sizeof text, "  %d New game", file + 1);
            else if (s->status == FAX_SAVE_NEWER) snprintf(text, sizeof text, "  %d From a newer version", file + 1);
            else snprintf(text, sizeof text, "  %d Damaged file", file + 1);
        } else if (s->status == FAX_SAVE_OK) {
            char gold[16];
            snprintf(gold, sizeof gold, "Gold %lu", (unsigned long)s->p.gold);
            snprintf(text, sizeof text, "    %-12s %s%u", gold, "Exp ", s->p.experience);
        }
    }
    snprintf(out, MENU_COLS + 1, "%s", text);
    if (*cursor) out[0] = '}';   /* the font's right-pointing triangle */
}

/* ---- mantras to import: faxanadu.srm, the launcher's Mantra field, and --password ---- */

extern const char *faxanadu_cli_mantra(void);   /* cyc_extras.c: --password, or NULL */

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
static FILE *open_sidecar(const char *name, const char *mode) {
    char path[1100];
    return sidecar_path(name, path) ? fopen(path, mode) : NULL;
}
static void strip(char *s) {   /* a mantra as written: spaces and line ends dropped */
    char *o = s;
    for (char *i = s; *i; ++i) if (*i != ' ' && *i != '\r' && *i != '\n') *o++ = *i;
    *o = 0;
}

/* faxanadu_imported.txt: the mantras already imported, one a line. Its
 * absence means the first run with save files. */
enum { IMPORTED_MAX = 32 };
static char imported[IMPORTED_MAX][32];
static int imported_count;
static bool read_imported(void) {
    imported_count = 0;
    FILE *f = open_sidecar("faxanadu_imported.txt", "r");
    if (!f) return false;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        strip(line);
        if (line[0] && imported_count < IMPORTED_MAX) snprintf(imported[imported_count++], 32, "%.31s", line);
    }
    fclose(f);
    return true;
}
static bool was_imported(const char *m) {
    for (int i = 0; i < imported_count; ++i) if (!strcmp(imported[i], m)) return true;
    return false;
}
static void write_imported(void) {
    FILE *f = open_sidecar("faxanadu_imported.txt", "w");
    if (!f) return;
    fprintf(f, "# Mantras imported into save files; a mantra listed here is not imported again.\n");
    for (int i = 0; i < imported_count; ++i) fprintf(f, "%s\n", imported[i]);
    fclose(f);
}
static void read_mantra_file(const char *name, char out[64]) {
    out[0] = 0;
    FILE *f = open_sidecar(name, "r");
    if (!f) return;
    if (!fgets(out, 64, f)) out[0] = 0;
    fclose(f);
    strip(out);
}
static uint8_t read_gurus_sidecar(void) {
    uint8_t visited = 1;
    FILE *f = open_sidecar("faxanadu_gurus.txt", "r");
    if (!f) return visited;
    char line[64];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        visited |= fax_save_gurus_parse(line);
    }
    fclose(f);
    return visited;
}

/* Import a mantra into the first empty file. */
static bool import_mantra(const char *mantra, uint8_t gurus) {
    if (!mantra[0] || was_imported(mantra)) return false;
    int file = 0;
    for (int i = FAX_SAVE_FILES; i >= 1; --i) if (slots[i - 1].status == FAX_SAVE_MISSING) file = i;
    if (!file) {
        FAX_FILES_LOG("[files] no empty file to import the mantra %s into\n", mantra);
        return false;
    }
    FaxSaveProgress p;
    if (!fax_save_progress_from_mantra(mantra, &p)) {
        FAX_FILES_LOG("[files] the game rejects the mantra %s; not imported\n", mantra);
    } else {
        FaxSaveRecord r;
        char g[FAX_SAVE_VALUE];
        fax_save_record_clear(&r);
        fax_save_progress_put(&r, &p);
        fax_save_gurus_format((uint8_t)(gurus | 1u | 1u << p.furthest), g);   /* he set the spawn point */
        fax_save_record_set(&r, "gurus", g);
        fax_save_record_set(&r, "imported_mantra", mantra);
        if (!fax_save_file_write(file, &r)) {
            FAX_FILES_LOG("[files] cannot write file %d\n", file);
            return false;
        }
        ++imports;
        FAX_FILES_LOG("[files] imported the mantra %s into file %d\n", mantra, file);
        scan();
    }
    if (imported_count < IMPORTED_MAX) snprintf(imported[imported_count++], 32, "%s", mantra);
    return true;
}

/* On the title: the first run imports faxanadu.srm's mantra and the visited
 * Gurus into file 1; after that, a mantra not imported before (a new one in
 * faxanadu.srm, which the launcher's Mantra field edits, or --password) goes
 * into the first empty file. */
static void import_mantras(void) {
    if (!cyc_host_saves_enabled()) return;
    bool first = !read_imported(), changed = first;
    char srm[64];
    read_mantra_file("faxanadu.srm", srm);
    changed |= import_mantra(srm, first ? read_gurus_sidecar() : 1);
    const char *cli = faxanadu_cli_mantra();
    if (cli) {
        char m[64];
        snprintf(m, sizeof m, "%s", cli);
        strip(m);
        changed |= import_mantra(m, 1);
    }
    if (changed) write_imported();
}

/* ---- the title ---- */

static void play_sound(uint8_t effect) {
    CycModRegs regs;
    cyc_mod_regs(&regs);
    regs.a = effect;
    cyc_mod_call_commit(SOUND_PLAY_EFFECT, &regs);
}

static void enter_title(void) {
    title_up = true;
    continuing = 0;
    fax_save_end();
    load_names();
    scan();
    import_mantras();
    menu = (Menu){ PICK, 0, 0 };
}

/* StartScreen_CheckHandleInput, far-called once a frame from the title's
 * loop: run the file select instead, and let the loop see Start only when a
 * file is chosen. Handling it returns through MMC1_PopBank, as its RTS would. */
static int title_hook(uint16_t addr) {
    (void)addr;
    if (!enabled) return 0;
    seen = 0;
    if (!title_up) enter_title();
    uint8_t pressed = q(JOY1_CHANGED);
    bool moved;
    int file = menu_press(&menu, pressed, &moved);
    uint8_t mask = (uint8_t)(pressed & ~(BUTTON_START | BUTTON_SELECT));
    if (moved) play_sound(SOUND_CURSOR);
    if (file) {
        FaxSaveRecord r;
        if (slots[file - 1].status == FAX_SAVE_OK && fax_save_file_read(file, &r) == FAX_SAVE_OK &&
            fax_save_progress_get(&r, &pending)) {
            continuing = file;
            furthest = pending.furthest;
            cyc_mod_poke(START_SCREEN_CHOICE, 1);
            fax_save_begin_continue(file, &r);
        } else {
            continuing = 0;
            furthest = 0;
            cyc_mod_poke(START_SCREEN_CHOICE, 0);
            fax_save_begin_new(file);
        }
        mask |= BUTTON_START;
        title_up = false;
    }
    cyc_mod_poke(JOY1_CHANGED, mask);
    return 1;
}

/* PasswordScreen_Show, when continuing a file: no mantra screen; the file's
 * progress goes where Password_Load would put the mantra's. */
static int mantra_screen_hook(uint16_t addr) {
    (void)addr;
    if (!enabled || !continuing) return 0;
    fax_save_progress_apply(&pending);
    return 1;
}

/* Game_ShowStartScreen's JMP Player_Spawn, after Player_SetInitialExpAndGold
 * reset gold, experience and the next title from the title: put back the
 * file's. */
static int start_spawn_hook(uint16_t addr) {
    (void)addr;
    if (!enabled || !continuing) return 0;
    cyc_mod_poke(EXPERIENCE, (uint8_t)pending.experience);
    cyc_mod_poke(EXPERIENCE + 1, (uint8_t)(pending.experience >> 8));
    cyc_mod_poke(GOLD, (uint8_t)pending.gold);
    cyc_mod_poke(GOLD + 1, (uint8_t)(pending.gold >> 8));
    cyc_mod_poke(GOLD + 2, (uint8_t)(pending.gold >> 16));
    cyc_mod_poke(NEXT_PLAYER_TITLE, pending.next_title);
    continuing = 0;
    return 0;
}

static bool commit(void) {
    last_spawn = q(TEMPLE_SPAWN_POINT) & 7;
    bool ok = fax_save_commit(furthest);
    if (ok) ++saves;
    else ++save_failures;
    return ok;
}

/* Player_Spawn, once Game_SpawnInTemple has laid out the temple (after a
 * death, a Guru travel journey or continuing): save the file, keeping its
 * spawn point. */
static int spawned_hook(uint16_t addr) {
    (void)addr;
    if (enabled && fax_save_file()) commit();
    return 0;
}

/* ---- the Guru's choice ---- */

/* Encode a line in the dialogue font, $FF-terminated and padded; false if it
 * would not fit the box (16 columns, 4 lines) or the slot. */
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
    while (n < SLOT_LEN) out[n++] = 0xFF;
    return true;
}
static bool serve_text(int which) {
    uint8_t bytes[SLOT_LEN];
    if (!encode(guru_text[which], bytes)) return false;
    for (unsigned i = 0; i < SLOT_LEN; ++i)
        if (!cyc_mod_set_prg_read_override(prg(13, (uint16_t)(SLOT + i)), 0xFF, bytes[i])) return false;
    return true;
}
static bool serve(uint16_t at, const uint8_t *bytes, unsigned n) {
    for (unsigned i = 0; i < n; ++i)
        if (!cyc_mod_set_prg_read_override(prg(12, (uint16_t)(at + i)), 0xFF, bytes[i])) return false;
    return true;
}
/* The choice at STEP: the menu (option 0 jumps to the saved line, option 1
 * goes on to STEP + 3, B ends the script) or the question (A jumps to the
 * saved line, B goes on to STEP + 4). */
static bool serve_step(bool as_menu) {
    stepped = true;
    const uint8_t m[] = { BUY_SELL_MENU, (uint8_t)say_step, (uint8_t)(say_step >> 8) };
    const uint8_t qn[] = { QUESTION_JUMP_IF_ACCEPTED, SLOT_ID, (uint8_t)say_step, (uint8_t)(say_step >> 8) };
    return as_menu ? serve(STEP, m, sizeof m) : serve(STEP, qn, sizeof qn);
}
static bool serve_own_say(void) {
    static const uint8_t say[] = { SHOW_MESSAGE, SLOT_ID, END };
    own_say = true;
    return serve(OWN_SAY, say, sizeof say);
}
/* The menu shows message $C3 instead of $10, for this one menu: the operand
 * is read as the menu starts, and put back at the end of the frame. */
static bool serve_menu_message(bool on) {
    menu_message = on;
    return cyc_mod_set_prg_read_override(prg(12, BUY_SELL_MESSAGE), BUY_SELL_MESSAGE_ID, on ? SLOT_ID : BUY_SELL_MESSAGE_ID);
}
static void unserve(void) {
    static const uint8_t ff[5] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    if (stepped) serve(STEP, ff, 4);
    if (own_say) serve(OWN_SAY, ff, 3);
    if (menu_message) serve_menu_message(false);
    stepped = own_say = false;
}

/* The temple the hero is in, for a Guru's save. */
static unsigned temple_here(void) {
    for (int t = 0; q(AREA_CURRENT_AREA) == BUILDINGS && t < FAX_SAVE_TEMPLES; ++t)
        if (temple_screen[t] == q(AREA_CURRENT_SCREEN)) return (unsigned)t;
    return q(TEMPLE_SPAWN_POINT) & 7u;
}

static Trigger trigger_at(uint16_t at) {
    for (int k = AT_MANTRA; k <= AT_EOLIS; ++k)
        if (at == trigger[k].at && q((uint16_t)(at - 2)) == trigger[k].stock[0] &&
            q((uint16_t)(at - 1)) == trigger[k].stock[1] && q(at) == trigger[k].stock[2])
            return (Trigger)k;
    return AT_NONE;
}

static void go(uint16_t to) {
    cyc_mod_poke(ISCRIPT_ADDR, (uint8_t)to);
    cyc_mod_poke(ISCRIPT_ADDR + 1, (uint8_t)(to >> 8));
    cyc_mod_poke(ISCRIPT_OFFSET, 0);
}

bool fax_files_guru_choice(void) { return enabled && !failed && fax_save_file() != 0; }

/* Save: this Guru's temple becomes the spawn point. */
static void save_here(void) {
    unsigned here = temple_here();
    cyc_mod_poke(TEMPLE_SPAWN_POINT, (uint8_t)here);
    if (here > furthest) furthest = (uint8_t)here;
    commit();
}

static void fail(const char *why) {
    FAX_FILES_LOG("[files] %s; the Guru shows the mantra instead\n", why);
    unserve();
    failed = true;
    guru = GURU_IDLE;
}

/* TextBox_ClearPasswordSize, as Messages_Load starts each message, has just
 * zeroed TextBox_LineScrollOffset. The box's rows are not laid out again
 * between two messages, so after one that scrolled (a new title's, 5 lines)
 * the next would show its lines rotated. While our lines (and Guru travel's
 * after them) are in the box, put back the offset the last line left. */
static int scroll_hook(uint16_t addr) {
    (void)addr;
    if (enabled && keep_rows) cyc_mod_poke(TEXTBOX_LINE_SCROLL, rows);
    return 0;
}

/* IScripts_InvokeNextAction: where a Guru would show the mantra, offer the
 * choice; then save, or hand over to Guru travel. Works whichever of this and
 * Guru travel's hook at the same address runs first: neither redirects at an
 * address the other acts on. */
static int next_action_hook(uint16_t addr) {
    (void)addr;
    if (!enabled || failed) return 0;
    if (held) { cyc_mod_poke(TEMPLE_SPAWN_POINT, held_spawn); held = false; }
    uint16_t at = (uint16_t)((q(ISCRIPT_ADDR) | q(ISCRIPT_ADDR + 1) << 8) + q(ISCRIPT_OFFSET));
    switch (guru) {
    case GURU_MENU:
    case GURU_QUESTION:
        if (at == say_step) {                                   /* A on "Remember my journey." */
            save_here();
            if (!serve_text(TEXT_SAVED)) { fail("cannot supply the Guru's line"); return 0; }
            guru = GURU_SAVED;
            return 0;
        }
        if (guru == GURU_MENU && at == STEP + 3) {              /* "Send me to another Guru." */
            uint16_t to = fax_travel_begin(true);
            guru = GURU_IDLE;
            go(to ? to : STOCK_END);
            return 0;
        }
        if (guru == GURU_QUESTION && at == STEP + 4) {          /* B on the question */
            guru = GURU_IDLE;
            go(STOCK_END);
            return 0;
        }
        break;
    case GURU_SAVED:
        if (at == say_step + 2) {                               /* the saved line was read: travel may offer */
            guru = GURU_IDLE;
            uint16_t to = fax_travel_begin(false);
            if (to) go(to);
            return 0;
        }
        break;
    }
    if (!fax_files_guru_choice()) return 0;
    if (q(at) == SET_SPAWN_POINT && q((uint16_t)(at + 1)) < FAX_SAVE_TEMPLES) {
        /* Talking to a Guru raises only the furthest temple; the spawn point
         * stays the Guru last saved with. */
        uint8_t t = q((uint16_t)(at + 1));
        if (t > furthest) furthest = t;
        held = true;
        held_spawn = q(TEMPLE_SPAWN_POINT);
        return 0;
    }
    if (!trigger_at(at)) return 0;
    bool as_menu = fax_travel_can_offer();
    say_step = fax_travel_say_step();
    if (!say_step) {
        say_step = OWN_SAY;
        if (!serve_own_say()) { fail("cannot supply the Guru's step"); return 0; }
    }
    if (!serve_step(as_menu) || !serve_text(as_menu ? TEXT_MENU : TEXT_QUESTION) ||
        (as_menu && !serve_menu_message(true))) {
        fail("cannot supply the Guru's choice");
        return 0;
    }
    guru = (uint8_t)(as_menu ? GURU_MENU : GURU_QUESTION);
    keep_rows = true;
    rows = q(TEXTBOX_LINE_SCROLL) & 3;
    go(STEP);
    return 0;
}

/* ---- present ---- */

static uint32_t *canvas;
static size_t capacity;
/* Title screen palette RAM entries for the frame's pixel values 0-3: its
 * first background palette, the logo's border ($0F, $05, $18, $38), and for
 * the text, its second palette's white ($30), the notices' color. */
static const uint8_t menu_palette[4] = { 0, 1, 2, 3 };
enum { MENU_TEXT_COLOR = 7 };

const uint32_t *fax_files_present(const uint32_t *picture, int *w, int *h) {
    if (!enabled || seen > 1) return picture;   /* the title's loop ran this frame */
    if (!fax_text_loaded() && !fax_text_load()) return picture;
    int width = 0, height = 0;
    const uint32_t *from = picture;
    if (from) width = *w, height = *h;
    else from = cyc_render_present(&width, &height);
    if (!from || width < 256 || height < 240) return picture;
    size_t n = (size_t)width * (size_t)height;
    if (n > capacity) {
        uint32_t *grown = realloc(canvas, n * sizeof *grown);
        if (!grown) return picture;
        canvas = grown; capacity = n;
    }
    memcpy(canvas, from, n * sizeof *canvas);
    /* Under the logo, over the stock START / CONTINUE and the notices. */
    int x = cyc_render_native_origin(width) + 16, y = 144;
    uint32_t colors[4];
    for (int v = 0; v < 4; ++v) colors[v] = cyc_render_color(menu_palette[v]);
    fax_text_box_rom(canvas, width, height, x, y, MENU_COLS, MENU_ROWS, colors, 255);
    for (int row = 0; row < MENU_ROWS; ++row) {
        char text[MENU_COLS + 1];
        bool cursor;
        menu_row(&menu, row, text, &cursor);
        fax_text_draw_color(canvas, width, height, x + FAX_TEXT_CELL, y + (row + 1) * FAX_TEXT_CELL, text,
                            cyc_render_color(MENU_TEXT_COLOR), 255);
    }
    *w = width; *h = height;
    return canvas;
}

/* ---- life cycle ---- */

void fax_files_power_on(void) {
    title_up = false;
    seen = 255;
    continuing = 0;
    furthest = 0;
    guru = GURU_IDLE;
    held = false;
    keep_rows = false;
    menu = (Menu){ PICK, 0, 0 };
    fax_save_end();
}
void fax_files_frame_end(void) {
    if (seen < 255) ++seen;
    if (title_up && seen > 30) title_up = false;   /* the title went without a choice (a state was loaded) */
    if (menu_message) serve_menu_message(false);   /* the menu has read its message ID */
    if (q(TEXTBOX_CLOSED)) { guru = GURU_IDLE; keep_rows = false; }   /* the box closed: B left, or it ended */
    else if (keep_rows) rows = q(TEXTBOX_LINE_SCROLL) & 3;
}

/* Save states carry the file in play, the file select and the Guru's choice,
 * so loading one resumes them as they were. The file's fields are reread. */
enum { RECORD = 14, VERSION = 1 };
static int state_get(uint8_t *buf, int cap) {
    if (cap < RECORD) return -1;
    const uint8_t rec[RECORD] = { VERSION, (uint8_t)fax_save_file(), title_up, menu.mode, menu.cursor, menu.target,
                                  (uint8_t)continuing, furthest, guru, seen, (uint8_t)say_step,
                                  (uint8_t)(say_step >> 8), keep_rows, rows };
    memcpy(buf, rec, RECORD);
    return RECORD;
}
static int state_validate(const uint8_t *buf, int len) {
    return len == 0 || (len == RECORD && buf[0] == VERSION && buf[1] <= FAX_SAVE_FILES && buf[2] <= 1 &&
                        buf[3] <= CONFIRM && buf[4] <= ERASE_ROW && buf[5] < FAX_SAVE_FILES &&
                        buf[6] <= FAX_SAVE_FILES && buf[7] < FAX_SAVE_TEMPLES && buf[8] < GURU_PHASES &&
                        buf[12] <= 1 && buf[13] <= 3);
}
static uint8_t loaded_file;
static int state_set(const uint8_t *buf, int len) {
    if (!state_validate(buf, len)) return 0;
    if (!len) { fax_files_power_on(); loaded_file = 0; return 1; }
    loaded_file = buf[1];
    title_up = buf[2];
    menu = (Menu){ buf[3], buf[4], buf[5] };
    continuing = buf[6];
    furthest = buf[7];
    guru = buf[8];
    seen = buf[9];
    say_step = (uint16_t)(buf[10] | buf[11] << 8);
    keep_rows = buf[12];
    rows = buf[13];
    held = false;
    return 1;
}
/* After the whole state is in: our overrides are not machine state, and Guru
 * travel may have put its own line back in the shared slot. */
void fax_files_state_loaded(void) {
    if (!enabled) return;
    fax_save_resume(loaded_file);
    scan();
    if (continuing) {
        FaxSaveRecord r;
        if (fax_save_file_read(continuing, &r) != FAX_SAVE_OK || !fax_save_progress_get(&r, &pending)) continuing = 0;
    }
    if (!guru || failed) return;
    bool ok = say_step != OWN_SAY || serve_own_say();
    ok = ok && serve_step(guru == GURU_MENU);
    ok = ok && serve_text(guru == GURU_SAVED ? TEXT_SAVED : guru == GURU_MENU ? TEXT_MENU : TEXT_QUESTION);
    if (!ok) fail("cannot supply the Guru's choice");
}

#ifdef CYC_WITH_SDL
static void files_state(int id, const char *line_in) {
    (void)line_in;
    char fields[1400];
    int n = snprintf(fields, sizeof fields,
                     "\"enabled\":%s,\"file\":%d,\"title\":%s,\"shown\":%s,\"mode\":%u,\"cursor\":%u,\"target\":%u,"
                     "\"continuing\":%d,\"guru\":%u,\"furthest\":%u,\"saves\":%u,\"save_failures\":%u,\"imports\":%u,\"spawn\":%d,"
                     "\"failed\":%s,\"palette\":[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u],\"slots\":[",
                     enabled ? "true" : "false", fax_save_file(), title_up ? "true" : "false",
                     enabled && seen <= 1 ? "true" : "false", menu.mode, menu.cursor, menu.target, continuing,
                     guru, furthest, saves, save_failures, imports, last_spawn, failed ? "true" : "false",
                     cyc_render_palette(0), cyc_render_palette(1), cyc_render_palette(2), cyc_render_palette(3),
                     cyc_render_palette(4), cyc_render_palette(5), cyc_render_palette(6), cyc_render_palette(7),
                     cyc_render_palette(8), cyc_render_palette(9), cyc_render_palette(10), cyc_render_palette(11),
                     cyc_render_palette(12), cyc_render_palette(13), cyc_render_palette(14), cyc_render_palette(15));
    for (int i = 0; i < FAX_SAVE_FILES && n > 0 && (size_t)n < sizeof fields; ++i) {
        const Slot *s = &slots[i];
        n += snprintf(fields + n, sizeof fields - (size_t)n,
                      "%s{\"status\":%d,\"title\":%u,\"spawn\":%u,\"furthest\":%u,\"gold\":%lu,\"experience\":%u}",
                      i ? "," : "", (int)s->status, s->p.title, s->p.spawn, s->p.furthest, (unsigned long)s->p.gold,
                      s->p.experience);
    }
    if (n > 0 && (size_t)n < sizeof fields) n += snprintf(fields + n, sizeof fields - (size_t)n, "],\"rows\":[");
    for (int row = 0; row < MENU_ROWS && n > 0 && (size_t)n < sizeof fields; ++row) {
        char text[MENU_COLS + 1];
        bool cursor;
        menu_row(&menu, row, text, &cursor);
        n += snprintf(fields + n, sizeof fields - (size_t)n, "%s\"%s\"", row ? "," : "", text);
    }
    if (n > 0 && (size_t)n < sizeof fields) snprintf(fields + n, sizeof fields - (size_t)n, "]");
    cyc_tcp_ok(id, fields);
}
void fax_files_tcp_setup(void) {
    cyc_tcp_register("files_state", "Save files: the file in play, the file select and each file", files_state);
}
#else
void fax_files_tcp_setup(void) {}
#endif

static const char *const hook_ids[] = {
    "faxanadu.files.title", "faxanadu.files.mantra-screen", "faxanadu.files.start-spawn", "faxanadu.files.spawned",
    "faxanadu.files.next-action", "faxanadu.files.scroll" };
static void hooks(bool on) {
    for (unsigned i = 0; i < sizeof hook_ids / sizeof hook_ids[0]; ++i) nes_mod_set_function_hook_enabled(hook_ids[i], on);
}
static void deactivate(void) {
    unserve();
    enabled = failed = false;
    fax_save_set_enabled(false);
    fax_files_power_on();
    hooks(false);
}
static void activate(void) {
    enabled = true;
    fax_save_set_enabled(true);
    hooks(true);
    nes_mod_set_local_only("Save files", 1);
}

NES_MOD_CONSTRUCTOR(register_files) {
    if (!nes_mod_register_function_entry_plugin("faxanadu.files.title", START_SCREEN_INPUT, title_hook) ||
        !nes_mod_register_function_entry_plugin("faxanadu.files.mantra-screen", PASSWORD_SCREEN_SHOW, mantra_screen_hook) ||
        !nes_mod_register_function_entry_plugin("faxanadu.files.start-spawn", START_SPAWN, start_spawn_hook) ||
        !nes_mod_register_function_entry_plugin("faxanadu.files.spawned", SPAWNED, spawned_hook) ||
        !nes_mod_register_function_entry_plugin("faxanadu.files.next-action", 0x826e, next_action_hook) ||
        !nes_mod_register_function_entry_plugin("faxanadu.files.scroll", CLEAR_SCROLLED, scroll_hook) ||
        !nes_mod_register_reset_callback(deactivate) || !nes_mod_register_activation_plugin("faxanadu.save-files", activate) ||
        !nes_mod_register_savestate_hook("faxanadu.files", state_get, state_set) ||
        !nes_mod_register_savestate_validator("faxanadu.files", state_validate)) abort();
}
