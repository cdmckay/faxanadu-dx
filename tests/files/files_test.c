/* Unit tests for Save files (fax_save.c, fax_files.c), without the emulator or
 * a ROM. The modules run against a model of the machine: CPU RAM, the stock
 * bytes of the Guru scripts and the Buy/Sell menu's message operand, unused ROM
 * ($FF), the engine's read overrides, and the title's letters in its pattern
 * table. A small interpreter plays the interaction-script actions the Gurus
 * use, calling the hook before each as IScripts_InvokeNextAction does. Files
 * are written in a scratch directory. */
#define _POSIX_C_SOURCE 200809L
#include "fax_save.c"
#include "fax_files.c"
#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned checks;
#define CHECK(test) do{++checks;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(1);}}while(0)

/* ---- the machine ---- */
static uint8_t ram[0x800];
static uint8_t rom[16 * 16384];
static uint8_t over[16 * 16384];
static bool overridden[16 * 16384];
static CycModRegs cpu = { .s = 0xFD };
static bool saves_on = true;

static uint32_t at_rom(uint16_t addr) {   /* bank 13 for the message slot, 15 above $C000, else 12 */
    if (addr >= 0xC000) return prg(15, (uint16_t)(addr - 0x4000));
    return prg(addr >= 0xB400 ? 13 : 12, addr);
}
uint8_t cyc_mod_peek(uint16_t addr) {   /* as the engine's: ROM without read overrides */
    if (addr < 0x2000) return ram[addr & 0x7FF];
    return addr >= 0x8000 ? rom[at_rom(addr)] : 0;
}
static uint8_t cpu_read(uint16_t addr) {  /* as the CPU reads it: with them */
    if (addr < 0x8000) return cyc_mod_peek(addr);
    uint32_t i = at_rom(addr);
    return overridden[i] ? over[i] : rom[i];
}
bool cyc_mod_poke(uint16_t addr, uint8_t value) { if (addr >= 0x2000) return false; ram[addr & 0x7FF] = value; return true; }
bool cyc_mod_set_prg_read_override(uint32_t offset, uint8_t compare, uint8_t value) {
    if (offset >= sizeof rom || rom[offset] != compare) return false;
    over[offset] = value; overridden[offset] = value != compare;
    return true;
}
void cyc_mod_regs(CycModRegs *out) { *out = cpu; }
void cyc_mod_set_regs(const CycModRegs *in) { cpu = *in; }
size_t cyc_mod_copy_cart_payload(uint8_t *out, size_t capacity) {
    if (out && capacity >= sizeof rom) memcpy(out, rom, sizeof rom);
    return sizeof rom;
}
bool cyc_mod_call_commit(uint16_t routine, CycModRegs *regs) { (void)regs; CHECK(routine == SOUND_PLAY_EFFECT); return true; }
bool cyc_host_saves_enabled(void) { return saves_on; }

/* Registration and the rest of the host. */
int nes_mod_register_function_entry_plugin(const char *id, uint16_t addr, NESModFunctionEntryCallback cb) { (void)id; (void)addr; (void)cb; return 1; }
static unsigned hooks_on;
int nes_mod_set_function_hook_enabled(const char *id, int on) { (void)id; hooks_on = on ? hooks_on + 1 : 0; return 1; }
int nes_mod_register_reset_callback(NESModActivationCallback cb) { (void)cb; return 1; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) { (void)id; (void)cb; return 1; }
int nes_mod_register_savestate_hook(const char *id, NESModSavestateGet get, NESModSavestateSet set) { (void)id; (void)get; (void)set; return 1; }
int nes_mod_register_savestate_validator(const char *id, NESModSavestateValidate v) { (void)id; (void)v; return 1; }
void nes_mod_set_local_only(const char *name, int required) { (void)name; (void)required; }
bool fax_text_load(void) { return true; }
bool fax_text_loaded(void) { return true; }
static struct { int x, y, cols, rows, lines, first_line_y, last_line_y; } drawn;   /* the last file pick drawn */
void fax_text_box_rom(uint32_t *px, int w, int h, int x, int y, int cols, int rows, const uint32_t colors[4], unsigned alpha) {
    (void)px; (void)w; (void)h; (void)colors; (void)alpha;
    drawn.x = x; drawn.y = y; drawn.cols = cols; drawn.rows = rows; drawn.lines = 0;
}
void fax_text_draw_color(uint32_t *px, int w, int h, int x, int y, const char *s, uint32_t color, unsigned alpha) {
    (void)px; (void)w; (void)h; (void)x; (void)s; (void)color; (void)alpha;
    if (!drawn.lines++) drawn.first_line_y = y;
    drawn.last_line_y = y;
}
uint32_t cyc_render_color(int entry) { return 0xFF000000u | (uint32_t)entry; }   /* the palette entry, opaque */
int cyc_render_native_origin(int width) { return (width - 256) / 2; }
const uint32_t *cyc_render_present(int *width, int *height) { *width = 0; *height = 0; return NULL; }
/* The title's pattern table ($1000 on its lines) and nametable: each letter
 * tile a solid block of pixel value 3, and the choices' row in palette 1. */
uint16_t cyc_render_line_bg_table(int y) { CHECK(y == TITLE_Y); return 0x1000; }
uint8_t cyc_render_chr(uint16_t addr) {
    unsigned tile = (addr - 0x1000u) / 16u;
    return addr >= 0x1000 && tile >= TITLE_LETTERS && tile < TITLE_LETTERS + 26u ? 0xFF : 0x00;
}
uint8_t cyc_render_nametable(uint16_t addr) { return addr == 0x23C0 + (TITLE_ROW / 4) * 8 + START_COL / 4 ||
                                                     addr == 0x23C0 + (TITLE_ROW / 4) * 8 + CONTINUE_COL / 4 ? 0x55 : 0; }
/* The frame's sprites: one 8x8, the arrow, its first line at (sprite_x,
 * sprite_y) of the native picture (none if sprite_y < 0), drawn in SPRITE
 * where place() takes it. */
enum { SPRITE = 0xFF00FF00u };
bool cyc_render_line_sprite16(int y) { (void)y; return false; }
static int sprite_x, sprite_y = -1;
void cyc_render_sprites(uint32_t *out, int width, int height, int native_x0, const uint8_t *bg_opaque,
                        CycSpritePlaceFn place, void *user) {
    (void)bg_opaque;
    int ox = native_x0 + sprite_x;
    if (sprite_y < 0 || (place && !place(0, sprite_x, sprite_y, &ox, user))) return;
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c)
            if (ox + c < width && sprite_y + r < height) out[(sprite_y + r) * width + ox + c] = SPRITE;
}

/* Guru travel, as Save files sees it. */
#define TRAVEL_SAY 0xAE00u
static bool travel_offers, travel_served;
static int travel_begun = -1;   /* -1, or whether it was asked for the towns */
bool fax_travel_can_offer(void) { return travel_offers; }
uint16_t fax_travel_say_step(void) { return travel_served ? TRAVEL_SAY : 0; }
uint16_t fax_travel_begin(bool towns) {
    if (!travel_offers) return 0;
    travel_begun = towns;
    return STOCK_END;   /* the model ends the script there; travel_test.c covers the rest */
}

static void stock(void) {
    memset(rom, 0xFF, sizeof rom); memset(overridden, 0, sizeof overridden); memset(ram, 0, sizeof ram);
    static const uint8_t endings[] = { 0x03, 0x22, 0x14, 0x00, 0x01, 0x23, 0x00 };   /* $A5FC: mantra; new title */
    static const uint8_t eolis[] = { 0x03, 0x2D, 0x00 };                               /* $A0C6 */
    memcpy(rom + prg(12, 0xA5FC), endings, sizeof endings);
    memcpy(rom + prg(12, 0xA0C6), eolis, sizeof eolis);
    rom[prg(12, BUY_SELL_MESSAGE)] = BUY_SELL_MESSAGE_ID;
    static const char titles[] = "Novice          Aspirant        Battler         Fighter         Adept           ";
    memcpy(rom + prg(15, 0xF649 - 0x4000), titles, sizeof titles - 1);
    for (int c = 0; c < FAX_SAVE_LISTS; ++c) ram[SELECTED + c] = 0xFF;
    travel_offers = travel_served = false; travel_begun = -1;
    deactivate(); activate();
    names_loaded = false;
}
static void remove_files(void) {
    static const char *const names[] = { "faxanadu_file1.txt", "faxanadu_file2.txt", "faxanadu_file3.txt" };
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; ++i) remove(names[i]);
}
static void write_text(const char *name, const char *text) {
    FILE *f = fopen(name, "w"); CHECK(f); fputs(text, f); fclose(f);
}
static bool exists(const char *name) { struct stat st; return stat(name, &st) == 0; }

/* ---- the record ---- */

static void record_fields(void) {
    FaxSaveRecord r;
    fax_save_record_clear(&r);
    CHECK(r.count == 1 && !strcmp(fax_save_record_get(&r, "version"), "1"));
    CHECK(fax_save_record_set(&r, "elixir.charge", "3") && fax_save_record_set(&r, "gold", "10"));
    CHECK(fax_save_record_set(&r, "elixir.charge", "2") && r.count == 3 && !strcmp(r.field[1].value, "2"));
    CHECK(!fax_save_record_set(&r, "Gold", "1") && !fax_save_record_set(&r, "", "1") && !fax_save_record_set(&r, "a b", "1"));
    CHECK(!fax_save_record_set(&r, "note", "two\nlines") && fax_save_record_set(&r, "note", "free text, spaces ok"));
    CHECK(fax_save_record_set(&r, "elixir.charge", NULL) && r.count == 3 && !fax_save_record_get(&r, "elixir.charge"));
    char key[16];
    for (int i = r.count; i < FAX_SAVE_FIELDS; ++i) { snprintf(key, sizeof key, "k%d", i); CHECK(fax_save_record_set(&r, key, "")); }
    CHECK(!fax_save_record_set(&r, "one.more", "1"));   /* full */
}

static void record_text(void) {
    FaxSaveRecord r, back;
    fax_save_record_clear(&r);
    fax_save_record_set(&r, "gold", "1234");
    fax_save_record_set(&r, "zone.checkpoint", "3 27");
    fax_save_record_set(&r, "empty", "");
    char text[FAX_SAVE_TEXT];
    int n = fax_save_record_format(&r, text, sizeof text);
    CHECK(n > 0 && text[0] == '#' && strstr(text, "\nversion = 1\ngold = 1234\nzone.checkpoint = 3 27\nempty = \n"));
    CHECK(fax_save_record_parse(&back, text) == FAX_SAVE_OK && back.count == 4);
    CHECK(!strcmp(fax_save_record_get(&back, "zone.checkpoint"), "3 27") && !strcmp(fax_save_record_get(&back, "empty"), ""));
    CHECK(fax_save_record_format(&r, text, 20) == -1);
    /* Hand-edited text: comments, blank lines, spaces, Windows line ends. */
    CHECK(fax_save_record_parse(&back, "# a comment\r\n\r\n  version=1\r\ngold   =  7 \r\n") == FAX_SAVE_OK);
    CHECK(!strcmp(fax_save_record_get(&back, "gold"), "7"));
    /* Refused: no version, a broken line, a key twice, a bad key. */
    CHECK(fax_save_record_parse(&back, "gold = 1\n") == FAX_SAVE_MALFORMED);
    CHECK(fax_save_record_parse(&back, "version = 1\ngold 1\n") == FAX_SAVE_MALFORMED);
    CHECK(fax_save_record_parse(&back, "version = 1\ngold = 1\ngold = 2\n") == FAX_SAVE_MALFORMED);
    CHECK(fax_save_record_parse(&back, "version = 1\nGold = 1\n") == FAX_SAVE_MALFORMED);
    CHECK(fax_save_record_parse(&back, "version = one\n") == FAX_SAVE_MALFORMED);
    CHECK(fax_save_record_parse(&back, "version = 0\n") == FAX_SAVE_MALFORMED);
}

static void versions(void) {
    FaxSaveRecord r;
    /* A newer record is not read: its fields may mean something else. */
    CHECK(fax_save_record_parse(&r, "version = 2\ngold = 1\n") == FAX_SAVE_NEWER);
    /* Fields this version does not know are kept, and written back. */
    CHECK(fax_save_record_parse(&r, "version = 1\nfuture.thing = x y\ngold = 5\n") == FAX_SAVE_OK);
    FaxSaveProgress p;
    CHECK(fax_save_progress_get(&r, &p) && p.gold == 5);
    p.gold = 6;
    CHECK(fax_save_progress_put(&r, &p));
    CHECK(!strcmp(fax_save_record_get(&r, "future.thing"), "x y") && !strcmp(fax_save_record_get(&r, "gold"), "6"));
    CHECK(!strcmp(r.field[1].key, "future.thing"));   /* in its place */
    /* Every version up to this one migrates to it. */
    for (int v = 1; v < FAX_SAVE_VERSION; ++v) CHECK(migrations[v]);
}

static FaxSaveProgress sample(void) {
    FaxSaveProgress p = { .title = 4, .next_title = 5, .experience = 4900, .gold = 0xABCDEF, .spawn = 3,
                          .furthest = 5, .special_items = 0x82, .quests = 0x03,
                          .count = { 2, 1, 0, 2, 3 },
                          .list = { { 1, 2 }, { 1 }, { 0 }, { 0, 2 }, { 4, 9, 16 } },
                          .selected = { 0, 2, 0xFF, 1, 7 } };
    return p;
}
static bool same(const FaxSaveProgress *a, const FaxSaveProgress *b) {
    if (a->title != b->title || a->next_title != b->next_title || a->experience != b->experience ||
        a->gold != b->gold || a->spawn != b->spawn || a->furthest != b->furthest ||
        a->special_items != b->special_items || a->quests != b->quests) return false;
    for (int c = 0; c < FAX_SAVE_LISTS; ++c) {
        if (a->count[c] != b->count[c] || a->selected[c] != b->selected[c]) return false;
        if (memcmp(a->list[c], b->list[c], a->count[c])) return false;
    }
    return true;
}

static void progress(void) {
    FaxSaveRecord r;
    FaxSaveProgress p = sample(), back;
    fax_save_record_clear(&r);
    CHECK(fax_save_progress_put(&r, &p) && fax_save_progress_get(&r, &back) && same(&p, &back));
    CHECK(!strcmp(fax_save_record_get(&r, "items"), "4 9 16") && !strcmp(fax_save_record_get(&r, "selected_shield"), "none"));
    CHECK(!strcmp(fax_save_record_get(&r, "gold"), "11259375") && !strcmp(fax_save_record_get(&r, "shields"), ""));
    /* Missing fields: a new game's Novice in Eolis. */
    fax_save_record_clear(&r);
    CHECK(fax_save_progress_get(&r, &back) && back.title == 0 && back.spawn == 0 && back.furthest == 0 &&
          back.selected[0] == 0xFF && back.count[4] == 0);
    fax_save_record_set(&r, "spawn", "3");
    fax_save_record_set(&r, "title", "2");
    CHECK(fax_save_progress_get(&r, &back) && back.furthest == 3 && back.next_title == 2);
    fax_save_record_set(&r, "furthest", "1");   /* never behind the spawn point */
    CHECK(fax_save_progress_get(&r, &back) && back.furthest == 3);
    /* Out of range. */
    static const char *const bad[][2] = {
        { "title", "16" }, { "spawn", "8" }, { "gold", "16777216" }, { "experience", "65536" }, { "quests", "256" },
        { "weapons", "1 2 3 0 1" }, { "items", "1 2 3 4 5 6 7 8 9" }, { "items", "none" }, { "items", "1 x" },
        { "selected_item", "255" }, { "selected_item", "-1" }, { "gold", "1e3" }, { "gold", " " } };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        fax_save_record_clear(&r);
        CHECK(fax_save_record_set(&r, bad[i][0], bad[i][1]));
        if (fax_save_progress_get(&r, &back)) { fprintf(stderr, "accepted %s = %s\n", bad[i][0], bad[i][1]); CHECK(0); }
    }
    fax_save_record_clear(&r);
    fax_save_record_set(&r, "items", "1 2 3 4 5 6 7 8");
    CHECK(fax_save_progress_get(&r, &back) && back.count[4] == 8);
}

static void gurus(void) {
    char text[FAX_SAVE_VALUE];
    fax_save_gurus_format(0x2B, text);
    CHECK(!strcmp(text, "Eolis Apolune Mascon Conflate"));
    CHECK(fax_save_gurus_parse(text) == 0x2B && fax_save_gurus_parse("  Dartmoor  Nowhere Eolis") == 0x81);
    CHECK(fax_save_gurus_parse(NULL) == 0 && fax_save_gurus_parse("") == 0);
    fax_save_gurus_format(0, text);
    CHECK(!strcmp(text, ""));
}

static void machine(void) {
    stock();
    FaxSaveProgress p = sample(), back;
    fax_save_progress_apply(&p);
    CHECK(ram[TEMPLE_SPAWN_POINT] == 3 && ram[GOLD + 2] == 0xAB && ram[lists[FAX_SAVE_ITEMS] + 2] == 16);
    fax_save_progress_capture(&back, 5);
    CHECK(same(&p, &back));
    fax_save_progress_capture(&back, 1);   /* the furthest temple is never behind the spawn point */
    CHECK(back.furthest == 3);
    ram[COUNTS + FAX_SAVE_WEAPONS] = 9;   /* a count past the list is cut to it */
    fax_save_progress_capture(&back, 5);
    CHECK(back.count[FAX_SAVE_WEAPONS] == 4);
}

/* ---- files ---- */

static void files_on_disk(void) {
    stock(); remove_files();
    FaxSaveRecord r, back;
    CHECK(fax_save_file_read(2, &back) == FAX_SAVE_MISSING);
    fax_save_record_clear(&r);
    fax_save_record_set(&r, "gold", "99");
    CHECK(fax_save_file_write(2, &r) && exists("faxanadu_file2.txt") && !exists("faxanadu_file2.txt.tmp"));
    CHECK(fax_save_file_read(2, &back) == FAX_SAVE_OK && !strcmp(fax_save_record_get(&back, "gold"), "99"));
    CHECK(!fax_save_file_write(0, &r) && !fax_save_file_write(4, &r));
    write_text("faxanadu_file3.txt", "version = 1\ngarbage\n");
    CHECK(fax_save_file_read(3, &back) == FAX_SAVE_MALFORMED);
    write_text("faxanadu_file3.txt", "version = 9\n");
    CHECK(fax_save_file_read(3, &back) == FAX_SAVE_NEWER);
    saves_on = false;   /* --no-save */
    CHECK(!fax_save_file_write(2, &r) && !fax_save_file_erase(2) && exists("faxanadu_file2.txt"));
    saves_on = true;
    CHECK(fax_save_file_erase(2) && !exists("faxanadu_file2.txt") && fax_save_file_read(2, &back) == FAX_SAVE_MISSING);
    remove_files();
}

static FaxSaveEvent events[8];
static unsigned event_count;
static void listener(FaxSaveEvent e) {
    if (event_count < 8) events[event_count++] = e;
    if (e == FAX_SAVE_WRITING) CHECK(fax_save_set_int("elixir.charge", 2));
}

static void api(void) {
    stock(); remove_files();
    event_count = 0;
    CHECK(fax_save_enabled() && fax_save_file() == 0);
    CHECK(!fax_save_set("elixir.charge", "1") && !fax_save_get("version"));   /* no file in play */
    fax_save_begin_new(2);
    CHECK(fax_save_file() == 2 && events[0] == FAX_SAVE_NEW_GAME);
    CHECK(!fax_save_set("gold", "1") && !fax_save_set("spawn", "1") && !fax_save_set("Bad", "1"));
    CHECK(fax_save_set("block.tutorial", "1") && fax_save_get_int("block.tutorial", 0) == 1);
    CHECK(fax_save_get_int("absent", -5) == -5 && fax_save_set("odd", "x") && fax_save_get_int("odd", 7) == 7);
    CHECK(fax_save_set_int("zone.checkpoint", -12) && fax_save_get_int("zone.checkpoint", 0) == -12);
    FaxSaveProgress p = sample();
    fax_save_progress_apply(&p);
    CHECK(fax_save_commit(5) && events[1] == FAX_SAVE_WRITING);
    FaxSaveRecord r;
    CHECK(fax_save_file_read(2, &r) == FAX_SAVE_OK);
    CHECK(!strcmp(fax_save_record_get(&r, "block.tutorial"), "1") && !strcmp(fax_save_record_get(&r, "elixir.charge"), "2"));
    CHECK(!strcmp(fax_save_record_get(&r, "spawn"), "3") && !strcmp(fax_save_record_get(&r, "furthest"), "5"));
    fax_save_end();
    CHECK(fax_save_file() == 0 && !fax_save_commit(5));
    fax_save_begin_continue(2, &r);
    CHECK(events[2] == FAX_SAVE_CONTINUED && fax_save_get_int("elixir.charge", 0) == 2);
    fax_save_resume(3);   /* a save state with file 3 in play, which has no file: an empty record, no event */
    CHECK(fax_save_file() == 3 && event_count == 3 && !fax_save_get("block.tutorial"));
    fax_save_set_enabled(false);
    CHECK(fax_save_file() == 0 && !fax_save_get("version"));
    remove_files();
}

/* ---- the title's menu ---- */

static void write_sample(int file) {
    FaxSaveRecord r;
    FaxSaveProgress p = sample();
    fax_save_record_clear(&r);
    fax_save_progress_put(&r, &p);
    CHECK(fax_save_file_write(file, &r));
}
static void row(const Menu *m, int n, const char *text, bool cursor, bool dim) {
    char got[MENU_COLS + 1];
    bool c, d;
    menu_row(m, n, got, &c, &d);
    if (strcmp(got, text) || c != cursor || d != dim) {
        fprintf(stderr, "row %d: \"%s\" cursor %d dim %d, expected \"%s\" %d %d\n", n, got, c, d, text, cursor, dim);
        CHECK(0);
    }
    ++checks;
}

static void menu_rows(void) {
    stock(); remove_files();
    write_sample(2);
    write_text("faxanadu_file3.txt", "nonsense\n");
    enter_title();
    CHECK(title_up && slots[0].status == FAX_SAVE_MISSING && slots[1].status == FAX_SAVE_OK &&
          slots[2].status == FAX_SAVE_MALFORMED);
    CHECK(menu.mode == TOP && menu.cursor == 1);                   /* on CONTINUE: a file can be continued */
    for (int n = 0; n < MENU_ROWS; ++n) row(&menu, n, "", false, false);   /* the two choices are not rows */
    Menu m = { NEW_PICK, 0, 0 };
    row(&m, 0, "} 1 Empty", true, false);
    row(&m, 1, "", false, false);
    row(&m, 2, "  2 Adept        Conflate", false, false);
    row(&m, 3, "    Gold 11259375 Exp 4900", false, false);
    slots[1].p.gold = 1234567;
    row(&m, 3, "    Gold 1234567 Exp 4900", false, false);       /* Exp under the town */
    slots[1].p.gold = 0xABCDEF;
    row(&m, 4, "  3 Damaged file", false, false);
    row(&m, 5, "", false, false);
    row(&m, 6, "", false, false);                                 /* a blank before the question */
    row(&m, 7, "  New game in which file?", false, false);
    CHECK(MENU_ROWS == 8);
    m = (Menu){ CONFIRM, 0, 1 };
    row(&m, 2, "} 2 Adept        Conflate", true, false);
    row(&m, 7, "  Erase file 2? A yes B no", false, false);
    m = (Menu){ CONTINUE_PICK, 1, 0 };                            /* only file 2 can be continued: the rest grey */
    row(&m, 0, "  1 Empty", false, true);
    row(&m, 1, "", false, true);
    row(&m, 2, "} 2 Adept        Conflate", true, false);
    row(&m, 3, "    Gold 11259375 Exp 4900", false, false);
    row(&m, 4, "  3 Damaged file", false, true);
    row(&m, 7, "  Continue which file?", false, false);
    slots[2].status = FAX_SAVE_NEWER;
    row(&m, 4, "  3 From a newer version", false, true);
    remove_files();
}

static void menu_presses(void) {
    stock(); remove_files();
    write_sample(2);
    write_text("faxanadu_file3.txt", "nonsense\n");
    scan();
    bool moved;
    int file;
    /* The two choices: Select, Left, Right, Up and Down all toggle. */
    Menu m = top_menu();
    CHECK(m.mode == TOP && m.cursor == 1);
    CHECK(menu_press(&m, BUTTON_SELECT, &moved, &file) == STAY && moved && m.cursor == 0);
    CHECK(menu_press(&m, BUTTON_RIGHT, &moved, &file) == STAY && moved && m.cursor == 1);
    CHECK(menu_press(&m, BUTTON_LEFT, &moved, &file) == STAY && m.cursor == 0);
    CHECK(menu_press(&m, BUTTON_DOWN, &moved, &file) == STAY && m.cursor == 1);
    CHECK(menu_press(&m, BUTTON_B, &moved, &file) == STAY && !moved && m.mode == TOP);
    /* CONTINUE: only saved files, the cursor skipping the rest. */
    CHECK(menu_press(&m, BUTTON_A, &moved, &file) == STAY && moved && m.mode == CONTINUE_PICK && m.cursor == 1);
    CHECK(menu_press(&m, BUTTON_DOWN, &moved, &file) == STAY && !moved && m.cursor == 1);   /* none other */
    CHECK(menu_press(&m, BUTTON_UP, &moved, &file) == STAY && !moved && m.cursor == 1);
    CHECK(menu_press(&m, BUTTON_START, &moved, &file) == CONTINUE && file == 2);
    m.cursor = 2;                                                  /* a damaged file is never continued */
    CHECK(menu_press(&m, BUTTON_A, &moved, &file) == STAY && file == 0);
    CHECK(menu_press(&m, BUTTON_B, &moved, &file) == STAY && moved && m.mode == TOP && m.cursor == 1);
    /* NEW GAME: the cursor starts on the first empty file, which starts at once. */
    m.cursor = 0;
    CHECK(menu_press(&m, BUTTON_START, &moved, &file) == STAY && m.mode == NEW_PICK && m.cursor == 0);
    CHECK(menu_press(&m, BUTTON_UP, &moved, &file) == STAY && moved && m.cursor == 2);   /* wraps */
    CHECK(menu_press(&m, BUTTON_DOWN, &moved, &file) == STAY && m.cursor == 0);
    CHECK(menu_press(&m, BUTTON_A, &moved, &file) == NEW_GAME && file == 1 && !moved);
    /* A used file asks first; B keeps it. */
    m = (Menu){ NEW_PICK, 1, 0 };
    CHECK(menu_press(&m, BUTTON_A, &moved, &file) == STAY && moved && m.mode == CONFIRM && m.target == 1);
    CHECK(menu_press(&m, BUTTON_DOWN, &moved, &file) == STAY && !moved && m.mode == CONFIRM && m.target == 1);
    CHECK(menu_press(&m, BUTTON_B, &moved, &file) == STAY && moved && m.mode == NEW_PICK && m.cursor == 1);
    CHECK(exists("faxanadu_file2.txt") && slots[1].status == FAX_SAVE_OK);
    CHECK(menu_press(&m, BUTTON_B, &moved, &file) == STAY && m.mode == TOP && m.cursor == 0);
    /* A damaged file asks too; A erases it, and the new game starts in it. */
    m = (Menu){ NEW_PICK, 2, 0 };
    CHECK(menu_press(&m, BUTTON_A, &moved, &file) == STAY && m.mode == CONFIRM && m.target == 2);
    CHECK(menu_press(&m, BUTTON_A, &moved, &file) == NEW_GAME && file == 3);
    CHECK(!exists("faxanadu_file3.txt") && slots[2].status == FAX_SAVE_MISSING && exists("faxanadu_file2.txt"));
    /* --no-save: the erase fails, and the new game still starts. */
    m = (Menu){ CONFIRM, 1, 1 };
    saves_on = false;
    CHECK(menu_press(&m, BUTTON_A, &moved, &file) == NEW_GAME && file == 2 && exists("faxanadu_file2.txt"));
    saves_on = true;
    /* Nothing to continue: CONTINUE is grey, and the cursor stays on NEW GAME. */
    remove_files();
    scan();
    m = top_menu();
    CHECK(m.cursor == 0 && !can_continue());
    CHECK(menu_press(&m, BUTTON_SELECT | BUTTON_RIGHT, &moved, &file) == STAY && !moved && m.cursor == 0);
    m.cursor = 1;                                                  /* had the files changed under it */
    CHECK(menu_press(&m, BUTTON_A, &moved, &file) == STAY && m.mode == NEW_PICK && m.cursor == 0);
    m = (Menu){ NEW_PICK, 0, 0 };
    CHECK(menu_press(&m, BUTTON_B, &moved, &file) == STAY && m.mode == TOP && m.cursor == 0);
    /* Every file used: NEW GAME's cursor starts on file 1. */
    write_sample(1); write_sample(2); write_sample(3);
    scan();
    m = (Menu){ TOP, 0, 0 };
    CHECK(menu_press(&m, BUTTON_A, &moved, &file) == STAY && m.mode == NEW_PICK && m.cursor == 0);
    m = (Menu){ TOP, 1, 0 };
    CHECK(menu_press(&m, BUTTON_A, &moved, &file) == STAY && m.mode == CONTINUE_PICK && m.cursor == 0);
    CHECK(menu_press(&m, BUTTON_UP, &moved, &file) == STAY && moved && m.cursor == 2);
    CHECK(menu_press(&m, BUTTON_DOWN, &moved, &file) == STAY && m.cursor == 0);
    remove_files();
}

/* Where the menu goes: NEW GAME over CONTINUE, from x 96 on rows 151 and 165,
 * in the band between the logo (to 143) and the notices (from 184), the
 * original's START and CONTINUE (row 160) hidden but for the arrow; a pick
 * over the original's choices and the notices (rows 160-239), the band above
 * it black. */
static uint32_t at(const uint32_t *px, int w, int x, int y) { return px[y * w + x]; }
static void placement(void) {
    stock(); remove_files();
    enter_title();
    static uint32_t picture[256 * 240], out[256 * 240];
    for (int i = 0; i < 256 * 240; ++i) picture[i] = 0x12345678;
    int w = 256, h = 240;
    seen = 5;
    CHECK(fax_files_present(picture, &w, &h) == picture);   /* the title's loop did not run */
    seen = 0;
    drawn.lines = 0;
    sprite_x = ARROW_X; sprite_y = NEW_GAME_Y;              /* the arrow beside NEW GAME: clear of row 160 */
    const uint32_t *got = fax_files_present(picture, &w, &h);
    CHECK(got != picture && drawn.lines == 0);              /* no box on the two choices */
    memcpy(out, got, sizeof out);
    CHECK(MENU_X == 96 && NEW_GAME_Y == 151 && CONTINUE_Y == 165 && ARROW_X == 84);
    /* NEW GAME, in the palette START's column has (1): its letters' pixel
     * value 3 is palette entry 7; the space between its words is left. */
    CHECK(at(out, w, 96, 151) == cyc_render_color(7) && at(out, w, 96 + 23, 151 + 7) == cyc_render_color(7));
    CHECK(at(out, w, 96 + 24, 151) == 0x12345678 && at(out, w, 96 + 31, 158) == 0x12345678);   /* the space */
    CHECK(at(out, w, 96 + 32, 151) == cyc_render_color(7) && at(out, w, 159, 158) == cyc_render_color(7));
    CHECK(at(out, w, 95, 151) == 0x12345678 && at(out, w, 160, 151) == 0x12345678);
    CHECK(at(out, w, 96, 150) == 0x12345678 && at(out, w, 96, 159) == 0x12345678);
    /* CONTINUE, with nothing to continue: grey, below it, left-aligned. */
    CHECK(at(out, w, 96, 165) == cyc_render_color(MENU_DIM_COLOR) && at(out, w, 159, 172) == cyc_render_color(MENU_DIM_COLOR));
    CHECK(at(out, w, 95, 168) == 0x12345678 && at(out, w, 160, 172) == 0x12345678 && at(out, w, 96, 173) == 0x12345678);
    /* The original's START and CONTINUE (columns 8-23 of row 160) are black. */
    CHECK(at(out, w, 64, 160) == cyc_render_color(0) && at(out, w, 191, 164) == cyc_render_color(0));
    CHECK(at(out, w, 95, 167) == cyc_render_color(0) && at(out, w, 160, 167) == cyc_render_color(0));
    CHECK(at(out, w, 63, 160) == 0x12345678 && at(out, w, 192, 167) == 0x12345678 && at(out, w, 64, 168) == 0x12345678);
    CHECK(at(out, w, ARROW_X, NEW_GAME_Y) == 0x12345678);   /* the game's own arrow, not drawn again */
    /* With the arrow beside CONTINUE, it crosses row 160: drawn again over the
     * black. With a file to continue, CONTINUE is white. */
    write_sample(1);
    scan();
    sprite_y = CONTINUE_Y - 1;
    memcpy(out, fax_files_present(picture, &w, &h), sizeof out);
    CHECK(at(out, w, 96, 165) == cyc_render_color(7) && at(out, w, 159, 172) == cyc_render_color(7));
    CHECK(at(out, w, ARROW_X, CONTINUE_Y - 1) == SPRITE && at(out, w, ARROW_X + 7, 167) == SPRITE);
    CHECK(at(out, w, ARROW_X - 1, 164) == cyc_render_color(0) && at(out, w, ARROW_X + 8, 164) == cyc_render_color(0));
    sprite_y = -1;
    /* A pick: the box, and the band between it and the logo black. */
    menu.mode = NEW_PICK;
    memcpy(out, fax_files_present(picture, &w, &h), sizeof out);
    CHECK(drawn.x == 16 && drawn.y == 160 && drawn.cols == MENU_COLS && drawn.rows == MENU_ROWS);
    CHECK(drawn.y + (drawn.rows + 2) * FAX_TEXT_CELL == 240);   /* the frame's bottom on the last row */
    CHECK(drawn.lines == MENU_ROWS && drawn.first_line_y == 168 && drawn.last_line_y == 224);
    CHECK(at(out, w, 0, 144) == cyc_render_color(0) && at(out, w, 255, 159) == cyc_render_color(0));
    CHECK(at(out, w, 96, 151) == cyc_render_color(0) && at(out, w, 0, 143) == 0x12345678);
    static uint32_t wide[512 * 240];
    w = 512;
    CHECK(fax_files_present(wide, &w, &h) != wide && drawn.x == 128 + 16 && drawn.y == 160);
    menu.mode = TOP;
    memcpy(wide, fax_files_present(wide, &w, &h), sizeof wide);
    CHECK(wide[NEW_GAME_Y * 512 + 128 + MENU_X] == cyc_render_color(7));   /* NEW GAME, in the native picture */
    CHECK(wide[CONTINUE_Y * 512 + 128 + MENU_X] == cyc_render_color(7));
    remove_files();
}

/* The title's loop: the game sees Start only when a file is chosen. */
static void press(uint8_t buttons) { ram[JOY1_CHANGED] = buttons; }
static void title(void) {
    stock(); remove_files();
    FaxSaveRecord r;
    FaxSaveProgress p = sample();
    fax_save_record_clear(&r);
    fax_save_progress_put(&r, &p);
    fax_save_record_set(&r, "gurus", "Eolis Mascon");
    CHECK(fax_save_file_write(3, &r));
    title_up = false;
    /* The two choices: the routine runs on, without Select or Start, and draws
     * its arrow where the hook puts it: 12 pixels before the chosen line, its
     * lit rows (34 below the Y it is given, as the stock $7E lights 160) on
     * the line's. */
    press(BUTTON_SELECT);                                          /* from CONTINUE, where it opens */
    CHECK(title_hook(START_SCREEN_INPUT) == 0 && title_up && menu.mode == TOP && menu.cursor == 0);
    CHECK(ram[JOY1_CHANGED] == 0 && ram[START_SCREEN_CHOICE] == 0);
    cpu.x = 0x34; cpu.y = 0x7E;
    CHECK(arrow_hook(ARROW_LOADED) == 0 && cpu.x == 84 && cpu.y == NEW_GAME_Y - 34 && cpu.y == 117);
    press(BUTTON_DOWN);                                            /* the routine has no use for Down */
    CHECK(title_hook(START_SCREEN_INPUT) == 0 && menu.cursor == 1 && ram[START_SCREEN_CHOICE] == 1);
    CHECK(ram[JOY1_CHANGED] == BUTTON_DOWN);
    cpu.x = 0x74; cpu.y = 0x7E;
    CHECK(arrow_hook(ARROW_LOADED) == 0 && cpu.x == 84 && cpu.y == CONTINUE_Y - 34 && cpu.y == 131);
    /* CONTINUE's pick: handled, no arrow; Start stays hidden until a file. */
    press(BUTTON_START);
    CHECK(title_hook(START_SCREEN_INPUT) == 1 && ram[JOY1_CHANGED] == 0 && menu.mode == CONTINUE_PICK && menu.cursor == 2);
    cpu.x = 0x74; cpu.y = 0x7E;
    CHECK(arrow_hook(ARROW_LOADED) == 0 && cpu.x == 0x74 && cpu.y == 0x7E);   /* not on the choices: left alone */
    press(BUTTON_A);
    CHECK(title_hook(START_SCREEN_INPUT) == 1 && ram[JOY1_CHANGED] == (BUTTON_A | BUTTON_START));
    CHECK(ram[START_SCREEN_CHOICE] == 1 && continuing == 3 && fax_save_file() == 3 && !title_up && furthest == 5);
    /* Continue: no mantra screen, the file's progress in RAM ... */
    CHECK(mantra_screen_hook(PASSWORD_SCREEN_SHOW) == 1 && ram[TEMPLE_SPAWN_POINT] == 3 && ram[PLAYER_TITLE] == 4);
    /* ... Player_SetInitialExpAndGold resets gold, experience and the next title ... */
    ram[GOLD] = ram[GOLD + 1] = ram[GOLD + 2] = 0; ram[EXPERIENCE] = 0; ram[NEXT_PLAYER_TITLE] = 4;
    /* ... and they come back before Player_Spawn. */
    CHECK(start_spawn_hook(START_SPAWN) == 0 && ram[GOLD + 2] == 0xAB && ram[EXPERIENCE] == (4900 & 255) &&
          ram[NEXT_PLAYER_TITLE] == 5 && continuing == 0);
    /* Respawning (here, continuing) saves the file, keeping its spawn point. */
    ram[AREA_CURRENT_SCREEN] = temple_screen[3];
    ram[GOLD] = 1;
    CHECK(spawned_hook(SPAWNED) == 0);
    CHECK(fax_save_file_read(3, &r) == FAX_SAVE_OK && !strcmp(fax_save_record_get(&r, "spawn"), "3") &&
          !strcmp(fax_save_record_get(&r, "gurus"), "Eolis Mascon") && !strcmp(fax_save_record_get(&r, "furthest"), "5"));
    /* NEW GAME in an empty file: at once. */
    enter_title();
    CHECK(menu.cursor == 1);
    press(BUTTON_LEFT);
    title_hook(START_SCREEN_INPUT);
    press(BUTTON_A);
    CHECK(title_hook(START_SCREEN_INPUT) == 1 && menu.mode == NEW_PICK && menu.cursor == 0 && ram[JOY1_CHANGED] == BUTTON_A);
    press(BUTTON_START);
    CHECK(title_hook(START_SCREEN_INPUT) == 1 && ram[JOY1_CHANGED] == BUTTON_START && ram[START_SCREEN_CHOICE] == 0);
    CHECK(fax_save_file() == 1 && continuing == 0 && furthest == 0 && !exists("faxanadu_file1.txt"));
    CHECK(mantra_screen_hook(PASSWORD_SCREEN_SHOW) == 0 && start_spawn_hook(START_SPAWN) == 0);
    /* NEW GAME over file 3: asked, B cancels, then A erases it and starts. */
    enter_title();
    menu = (Menu){ NEW_PICK, 2, 0 };
    press(BUTTON_A);
    CHECK(title_hook(START_SCREEN_INPUT) == 1 && menu.mode == CONFIRM && ram[JOY1_CHANGED] == BUTTON_A && title_up);
    press(BUTTON_B);
    CHECK(title_hook(START_SCREEN_INPUT) == 1 && menu.mode == NEW_PICK && exists("faxanadu_file3.txt"));
    press(BUTTON_A);
    title_hook(START_SCREEN_INPUT);
    press(BUTTON_A);
    CHECK(title_hook(START_SCREEN_INPUT) == 1 && ram[JOY1_CHANGED] == (BUTTON_A | BUTTON_START));
    CHECK(ram[START_SCREEN_CHOICE] == 0 && fax_save_file() == 3 && continuing == 0 && !exists("faxanadu_file3.txt"));
    /* A file damaged since the title showed it is not continued, nor started over. */
    write_sample(3);
    enter_title();
    CHECK(slots[2].status == FAX_SAVE_OK);
    write_text("faxanadu_file3.txt", "broken\n");
    menu = (Menu){ CONTINUE_PICK, 2, 0 };
    press(BUTTON_A);
    CHECK(title_hook(START_SCREEN_INPUT) == 1 && ram[JOY1_CHANGED] == BUTTON_A && fax_save_file() == 0);
    CHECK(slots[2].status == FAX_SAVE_MALFORMED && title_up && menu.mode == CONTINUE_PICK);
    /* Off, the title is the game's own. */
    deactivate();
    press(BUTTON_START);
    CHECK(title_hook(START_SCREEN_INPUT) == 0 && ram[JOY1_CHANGED] == BUTTON_START);
    cpu.x = 0x34; cpu.y = 0x7E;
    CHECK(arrow_hook(ARROW_LOADED) == 0 && cpu.x == 0x34 && cpu.y == 0x7E);
    remove_files();
}

/* ---- a Guru's choice ---- */

static char said[16][64];
static unsigned said_n;
static void say(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vsnprintf(said[said_n++], sizeof said[0], fmt, ap); va_end(ap);
}
static void message(uint8_t id) {
    if (id != SLOT_ID) { say("msg %02X", id); return; }
    char text[64], *o = text;
    for (unsigned i = 0; i < SLOT_LEN; ++i) {
        uint8_t c = cpu_read((uint16_t)(SLOT + i));
        if (c == 0xFF) break;
        *o++ = c == 0xFD ? ' ' : c == 0xFE ? '/' : (char)c;
    }
    *o = 0;
    say("%s", text);
}
static void jump(uint16_t to) {
    ram[ISCRIPT_ADDR] = (uint8_t)to; ram[ISCRIPT_ADDR + 1] = (uint8_t)(to >> 8); ram[ISCRIPT_OFFSET] = 0;
}
/* Run a script from `at`. Each question or menu takes the next answer: 'A' or
 * 'B' for a question, '0', '1' or 'B' for the Buy/Sell menu. */
static void run(uint16_t at, const char *answers) {
    said_n = 0;
    ram[TEXTBOX_CLOSED] = 0;
    jump(at);
    for (int steps = 0; steps < 64; ++steps) {
        next_action_hook(0x826E);
        uint16_t p = (uint16_t)((ram[ISCRIPT_ADDR] | ram[ISCRIPT_ADDR + 1] << 8) + ram[ISCRIPT_OFFSET]);
        uint8_t op = cpu_read(p), arg = cpu_read((uint16_t)(p + 1));
        uint16_t addr1 = (uint16_t)(arg | cpu_read((uint16_t)(p + 2)) << 8);
        uint16_t addr2 = (uint16_t)(cpu_read((uint16_t)(p + 2)) | cpu_read((uint16_t)(p + 3)) << 8);
        unsigned len = 1;
        switch (op) {
        case END: say("end"); ram[TEXTBOX_CLOSED] = 1; return;
        case UNSKIPPABLE_MESSAGE: case SHOW_MESSAGE: message(arg); len = 2; break;
        case SET_SPAWN_POINT: if (arg > ram[TEMPLE_SPAWN_POINT]) ram[TEMPLE_SPAWN_POINT] = arg; len = 2; break;
        case MANTRA: say("mantra"); break;
        case BUY_SELL_MENU:
            message(cpu_read(BUY_SELL_MESSAGE));   /* its message ID is an immediate operand */
            CHECK(*answers);
            if (*answers == 'B') { ++answers; say("end"); ram[TEXTBOX_CLOSED] = 1; return; }
            if (*answers++ == '0') { jump(addr1); continue; }
            len = 3; break;
        case QUESTION_JUMP_IF_ACCEPTED:
            message(arg);
            CHECK(*answers);
            if (*answers++ == 'A') { jump(addr2); continue; }
            len = 4; break;
        case 0x17: jump(addr1); continue;
        default: fprintf(stderr, "unmodelled action %02X at $%04X\n", op, p); exit(1);
        }
        ram[ISCRIPT_OFFSET] = (uint8_t)(ram[ISCRIPT_OFFSET] + len);
    }
    CHECK(!"the script did not end");
}
static void expect(const char *const *lines) {
    unsigned n = 0;
    for (; lines[n]; ++n) {
        if (n >= said_n || strcmp(said[n], lines[n])) {
            fprintf(stderr, "line %u: said \"%s\", expected \"%s\"\n", n, n < said_n ? said[n] : "", lines[n]);
            for (unsigned i = 0; i < said_n; ++i) fprintf(stderr, "  %s\n", said[i]);
            exit(1);
        }
        ++checks;
    }
    CHECK(n == said_n);
}
#define EXPECT(...) do { static const char *const lines_[] = { __VA_ARGS__, NULL }; expect(lines_); } while (0)
#define MENU "Remember my/journey./Send me to/another Guru."
#define QUESTION "Shall I remember/your journey?"
#define SAVED "I will remember/your journey."

/* A temple Guru as in the stock scripts: SET_SPAWN_POINT here, then the
 * mantra (JUMP ISCRIPT_COMMON_GURU_PASSWORD) or a new title (..._NEW_RANK). */
static void temple_guru(uint8_t here, bool new_title, const char *answers) {
    const uint8_t script[] = { SET_SPAWN_POINT, here, 0x17, new_title ? 0x00 : 0xFC, new_title ? 0xA6 : 0xA5 };
    memcpy(rom + prg(12, 0xA000), script, sizeof script);
    ram[AREA_CURRENT_AREA] = BUILDINGS;
    ram[AREA_CURRENT_SCREEN] = temple_screen[here];
    run(0xA000, answers);
    fax_files_frame_end();
}
/* A file in play: saved with Apolune's Guru, Mascon the furthest temple. */
static void in_play(void) {
    stock(); remove_files();
    fax_save_begin_new(1);
    furthest = 3;
    ram[TEMPLE_SPAWN_POINT] = 1;
    ram[GOLD] = 42;
}

static void choices(void) {
    FaxSaveRecord r;
    /* Save from the menu: Conflate becomes the spawn point, then travel offers. */
    in_play(); travel_offers = travel_served = true;
    unsigned saved_before = saves;
    static const uint8_t travel_say[] = { SHOW_MESSAGE, SLOT_ID, END };   /* Guru travel's SAY step */
    for (unsigned i = 0; i < 3; ++i) cyc_mod_set_prg_read_override(prg(12, (uint16_t)(TRAVEL_SAY + i)), 0xFF, travel_say[i]);
    temple_guru(5, false, "0");
    EXPECT("msg 22", MENU, SAVED, "end");
    CHECK(ram[TEMPLE_SPAWN_POINT] == 5 && furthest == 5 && travel_begun == 0 && saves == saved_before + 1);
    CHECK(fax_save_file_read(1, &r) == FAX_SAVE_OK && !strcmp(fax_save_record_get(&r, "spawn"), "5") &&
          !strcmp(fax_save_record_get(&r, "gold"), "42"));
    CHECK(cpu_read(BUY_SELL_MESSAGE) == BUY_SELL_MESSAGE_ID && guru == GURU_IDLE);   /* the shops' menu is theirs again */

    /* Travel from the menu: the towns at once; nothing saved, the spawn point stays. */
    in_play(); travel_offers = true;
    temple_guru(5, false, "1");
    EXPECT("msg 22", MENU, "end");
    CHECK(travel_begun == 1 && ram[TEMPLE_SPAWN_POINT] == 1 && furthest == 5 && !exists("faxanadu_file1.txt"));

    /* B leaves: no save, no travel, and talking did not raise the spawn point (#43). */
    in_play(); travel_offers = true;
    temple_guru(6, false, "B");
    EXPECT("msg 22", MENU, "end");
    CHECK(travel_begun == -1 && ram[TEMPLE_SPAWN_POINT] == 1 && furthest == 6 && !exists("faxanadu_file1.txt"));
    CHECK(guru == GURU_IDLE && cpu_read(BUY_SELL_MESSAGE) == BUY_SELL_MESSAGE_ID);
    CHECK(cpu_read(STEP) == BUY_SELL_MENU && cpu_read(OWN_SAY) == SHOW_MESSAGE);   /* Guru travel off: our own step */

    /* Nowhere to send the hero: the question. */
    in_play();
    temple_guru(2, false, "A");
    EXPECT("msg 22", QUESTION, SAVED, "end");
    CHECK(ram[TEMPLE_SPAWN_POINT] == 2 && furthest == 3 && travel_begun == -1 && exists("faxanadu_file1.txt"));
    in_play();
    temple_guru(2, false, "B");
    EXPECT("msg 22", QUESTION, "end");
    CHECK(ram[TEMPLE_SPAWN_POINT] == 1 && !exists("faxanadu_file1.txt"));

    /* After a new title, and at Eolis's Guru with the Ring. The new title's
     * line scrolled the box (5 lines): each of our lines keeps its rows. */
    in_play(); travel_offers = true;
    ram[TEXTBOX_LINE_SCROLL] = 1;
    CHECK(scroll_hook(CLEAR_SCROLLED) == 0 && ram[TEXTBOX_LINE_SCROLL] == 1);   /* not ours: left alone */
    ram[TEXTBOX_LINE_SCROLL] = 0;
    CHECK(scroll_hook(CLEAR_SCROLLED) == 0 && ram[TEXTBOX_LINE_SCROLL] == 0);
    ram[TEXTBOX_LINE_SCROLL] = 1;
    ram[AREA_CURRENT_AREA] = BUILDINGS; ram[AREA_CURRENT_SCREEN] = temple_screen[4];
    jump(GURU_NEW_RANK_END);
    next_action_hook(0x826E);
    CHECK(keep_rows && rows == 1);
    ram[TEXTBOX_LINE_SCROLL] = 0;                   /* Messages_Load zeroes it for the menu ... */
    CHECK(scroll_hook(CLEAR_SCROLLED) == 0 && ram[TEXTBOX_LINE_SCROLL] == 1);   /* ... and it is put back */
    ram[TEXTBOX_CLOSED] = 1;
    fax_files_frame_end();
    CHECK(!keep_rows && guru == GURU_IDLE);
    in_play(); travel_offers = true;
    temple_guru(4, true, "0");
    EXPECT("msg 23", MENU, SAVED, "end");
    CHECK(ram[TEMPLE_SPAWN_POINT] == 4 && furthest == 4);
    in_play();
    ram[AREA_CURRENT_AREA] = BUILDINGS; ram[AREA_CURRENT_SCREEN] = temple_screen[0];
    run(0xA0C6, "A");
    EXPECT("msg 2D", QUESTION, SAVED, "end");
    CHECK(ram[TEMPLE_SPAWN_POINT] == 0 && furthest == 3);

    /* No file in play (a save state from the original game): no choice and no
     * mantra; Guru travel offers if it can, else the script ends. */
    stock(); remove_files();
    temple_guru(5, false, "");
    EXPECT("msg 22", "end");
    CHECK(ram[TEMPLE_SPAWN_POINT] == 5 && travel_begun == -1);   /* the stock raise: no file keeps a spawn point */
    stock(); remove_files(); travel_offers = true;
    temple_guru(5, false, "");
    EXPECT("msg 22", "end");                                      /* the model's travel ends the script */
    CHECK(travel_begun == 0);
    temple_guru(4, true, "");                                     /* after a new title the original shows none */
    EXPECT("msg 23", "end");
    /* The remaster off: the original's mantra. */
    in_play(); deactivate();
    temple_guru(5, false, "");
    EXPECT("msg 22", "mantra", "end");

    /* A layout where our step would go is refused: no save, and still no mantra. */
    in_play();
    rom[prg(12, STEP)] = 0x00;
    temple_guru(5, false, "");
    EXPECT("msg 22", "end");
    CHECK(failed && !fax_files_guru_choice());
    temple_guru(3, false, "");
    EXPECT("msg 22", "end");
    remove_files();
}

/* ---- save states ---- */

static void save_states(void) {
    in_play(); travel_offers = true;
    /* Mid-menu: the menu's step and text come back, though overrides are not machine state. */
    ram[AREA_CURRENT_AREA] = BUILDINGS; ram[AREA_CURRENT_SCREEN] = temple_screen[5];
    jump(GURU_MANTRA);
    next_action_hook(0x826E);
    CHECK(guru == GURU_MENU);
    uint8_t buf[16];
    int n = state_get(buf, sizeof buf);
    CHECK(n == RECORD && state_validate(buf, n));
    memset(overridden, 0, sizeof overridden);
    guru = GURU_IDLE; furthest = 0;
    CHECK(state_set(buf, n));
    fax_files_state_loaded();
    CHECK(guru == GURU_MENU && furthest == 3 && fax_save_file() == 1 && cpu_read(STEP) == BUY_SELL_MENU);
    said_n = 0; message(SLOT_ID);
    EXPECT(MENU);
    buf[8] = GURU_PHASES;
    CHECK(!state_validate(buf, n) && !state_set(buf, n));
    buf[8] = 0; buf[1] = 4;
    CHECK(!state_validate(buf, n));
    /* On the title: the menu comes back, CONTINUE's place only with a file to continue. */
    stock(); remove_files();
    enter_title();
    menu = (Menu){ CONFIRM, 2, 2 };
    n = state_get(buf, sizeof buf);
    CHECK(buf[0] == VERSION && buf[2] == 1 && state_validate(buf, n));
    menu = (Menu){ TOP, 0, 0 };
    CHECK(state_set(buf, n) && menu.mode == CONFIRM && menu.cursor == 2 && menu.target == 2 && title_up);
    buf[3] = TOP; buf[4] = 1;
    CHECK(state_set(buf, n));
    fax_files_state_loaded();
    CHECK(menu.mode == TOP && menu.cursor == 0);                  /* nothing to continue */
    buf[3] = MODES;
    CHECK(!state_validate(buf, n));
    buf[3] = TOP; buf[4] = 3;
    CHECK(!state_validate(buf, n));
    /* Version 1 had the file select ("Erase a file" its row 3): the two choices come back. */
    buf[0] = 1; buf[3] = 2; buf[4] = 3;
    CHECK(state_validate(buf, n) && state_set(buf, n) && menu.mode == TOP && menu.cursor == 0 && title_up);
    buf[3] = 3;
    CHECK(!state_validate(buf, n));
    CHECK(state_set(buf, 0) && guru == GURU_IDLE && !title_up);
    fax_files_state_loaded();
    CHECK(fax_save_file() == 0);
    remove_files();
}

int main(void) {
    char dir[] = "/tmp/fax-files-test-XXXXXX";
    const char *base = getenv("FAX_FILES_TEST_DIR");
    if (base) { mkdir(base, 0700); CHECK(chdir(base) == 0); }
    else { CHECK(mkdtemp(dir) && chdir(dir) == 0); }
    CHECK(fax_save_listen(listener));
    record_fields();
    record_text();
    versions();
    progress();
    gurus();
    machine();
    files_on_disk();
    api();
    menu_rows();
    menu_presses();
    placement();
    title();
    choices();
    save_states();
    if (!base) { CHECK(chdir("/") == 0); rmdir(dir); }
    printf("files_test: %u checks passed\n", checks);
    return 0;
}
