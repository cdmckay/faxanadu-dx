/* Unit tests for Save files (fax_save.c, fax_files.c), without the emulator or
 * a ROM. The modules run against a model of the machine: CPU RAM, the stock
 * bytes of the Guru scripts and the Buy/Sell menu's message operand, unused ROM
 * ($FF), the engine's read overrides and isolated calls, with the mantra's
 * routines modelled for two known mantras. A small interpreter plays the
 * interaction-script actions the Gurus use, calling the hook before each as
 * IScripts_InvokeNextAction does. Files are written in a scratch directory. */
#define _POSIX_C_SOURCE 200809L
#include "fax_save.c"
#include "fax_files.c"
#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned checks;
#define CHECK(test) do{++checks;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(1);}}while(0)

/* ---- the machine ---- */
static uint8_t ram[0x800], isolated_ram[0x800];
static uint8_t rom[16 * 16384];
static uint8_t over[16 * 16384];
static bool overridden[16 * 16384];
static CycModRegs cpu = { .s = 0xFD };
static bool isolated, saves_on = true;

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
bool cyc_mod_isolate_begin(void) { CHECK(!isolated); isolated = true; memcpy(isolated_ram, ram, sizeof ram); return true; }
void cyc_mod_isolate_end(void) { CHECK(isolated); isolated = false; memcpy(ram, isolated_ram, sizeof ram); }
size_t cyc_mod_copy_cart_payload(uint8_t *out, size_t capacity) {
    if (out && capacity >= sizeof rom) memcpy(out, rom, sizeof rom);
    return sizeof rom;
}
bool cyc_mod_call_commit(uint16_t routine, CycModRegs *regs) { (void)regs; CHECK(routine == SOUND_PLAY_EFFECT); return true; }
bool cyc_host_saves_enabled(void) { return saves_on; }

/* The mantra's routines, for two mantras the "game" knows. */
typedef struct { const char *mantra; uint8_t title, spawn, special, quests; uint8_t items[3], items_n, selected_item; } Known;
static const Known known[] = {
    { "Mantra1", 4, 5, 0x82, 0x03, { 4, 9, 16 }, 3, 7 },
    { "Mantra2", 1, 1, 0x00, 0x00, { 16 }, 1, 0xFF },
};
static const Known *checked;
static int mantra_calls;
bool cyc_mod_call(uint16_t routine, CycModRegs *regs) {
    CHECK(isolated);
    regs->p &= (uint8_t)~1;
    if (routine == MMC1_SWITCH_BANK) { CHECK(regs->x == 12); return true; }
    if (routine == PASSWORD_CHECK) {
        ++mantra_calls; checked = NULL;
        for (unsigned k = 0; k < sizeof known / sizeof known[0]; ++k) {
            size_t n = strlen(known[k].mantra);
            bool same = ram[ENTERED_LENGTH] == n;
            for (size_t i = 0; same && i < n; ++i) same = ram[ENTERED_CHARS + i] == mantra_value(known[k].mantra[i]);
            if (same) checked = &known[k];
        }
        if (!checked) regs->p |= 1;
        return true;
    }
    if (routine == PASSWORD_LOAD) {
        CHECK(checked);
        const Known *k = checked;
        ram[PLAYER_TITLE] = k->title; ram[TEMPLE_SPAWN_POINT] = k->spawn;
        ram[SPECIAL_ITEMS] = k->special; ram[QUESTS] = k->quests;
        for (int c = 0; c < FAX_SAVE_LISTS; ++c) { ram[COUNTS + c] = 0; ram[SELECTED + c] = 0xFF; }
        ram[COUNTS + FAX_SAVE_ITEMS] = k->items_n;
        memcpy(&ram[lists[FAX_SAVE_ITEMS]], k->items, k->items_n);
        ram[SELECTED + FAX_SAVE_ITEMS] = k->selected_item;
        return true;
    }
    if (routine == SET_INITIAL_EXP_AND_GOLD) {   /* PLAYER_TITLE_EXP_NEEDED and PLAYER_TITLE_GOLD for titles 1 and 4 */
        unsigned t = ram[PLAYER_TITLE], exp = t == 4 ? 4800 : t == 1 ? 1000 : 0, gold = t == 4 ? 1600 : t == 1 ? 500 : 0;
        ram[NEXT_PLAYER_TITLE] = (uint8_t)t;
        ram[EXPERIENCE] = (uint8_t)exp; ram[EXPERIENCE + 1] = (uint8_t)(exp >> 8);
        ram[GOLD] = (uint8_t)gold; ram[GOLD + 1] = (uint8_t)(gold >> 8); ram[GOLD + 2] = 0;
        return true;
    }
    CHECK(!"unmodelled routine");
    return false;
}

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
void fax_text_box_rom(uint32_t *px, int w, int h, int x, int y, int cols, int rows, const uint32_t colors[4], unsigned alpha) {
    (void)px; (void)w; (void)h; (void)x; (void)y; (void)cols; (void)rows; (void)colors; (void)alpha;
}
void fax_text_draw_color(uint32_t *px, int w, int h, int x, int y, const char *s, uint32_t color, unsigned alpha) {
    (void)px; (void)w; (void)h; (void)x; (void)y; (void)s; (void)color; (void)alpha;
}
uint32_t cyc_render_color(int entry) { return (uint32_t)entry; }
int cyc_render_native_origin(int width) { return (width - 256) / 2; }
const uint32_t *cyc_render_present(int *width, int *height) { *width = 0; *height = 0; return NULL; }
static const char *cli;
const char *faxanadu_cli_mantra(void) { return cli; }

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
    travel_offers = travel_served = false; travel_begun = -1; cli = NULL;
    deactivate(); activate();
    names_loaded = false;
}
static void remove_files(void) {
    static const char *const names[] = { "faxanadu_file1.txt", "faxanadu_file2.txt", "faxanadu_file3.txt",
                                         "faxanadu.srm", "faxanadu_gurus.txt", "faxanadu_imported.txt" };
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

static void mantras(void) {
    stock();
    FaxSaveProgress p;
    ram[0x0100] = 0x5A;
    CHECK(fax_save_progress_from_mantra("Mant ra1", &p) && !isolated);   /* spaces are dropped, as on the screen */
    CHECK(p.title == 4 && p.next_title == 4 && p.experience == 4800 && p.gold == 1600 && p.spawn == 5 && p.furthest == 5);
    CHECK(p.special_items == 0x82 && p.count[FAX_SAVE_ITEMS] == 3 && p.list[FAX_SAVE_ITEMS][2] == 16 && p.selected[4] == 7);
    CHECK(ram[PLAYER_TITLE] == 0 && ram[0x0100] == 0x5A);   /* the machine is as it was */
    CHECK(!fax_save_progress_from_mantra("Unknown", &p) && !isolated);
    CHECK(!fax_save_progress_from_mantra("Ma", &p) && !fax_save_progress_from_mantra("bad!", &p));
    CHECK(!fax_save_progress_from_mantra("AAAAAAAAAAAAAAAAAAAAAAAAA", &p));   /* 25 characters */
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

/* ---- the file select ---- */

static void file_select(void) {
    stock(); remove_files();
    FaxSaveRecord r;
    FaxSaveProgress p = sample();
    fax_save_record_clear(&r);
    fax_save_progress_put(&r, &p);
    CHECK(fax_save_file_write(2, &r));
    write_text("faxanadu_file3.txt", "nonsense\n");
    enter_title();
    CHECK(title_up && slots[0].status == FAX_SAVE_MISSING && slots[1].status == FAX_SAVE_OK &&
          slots[2].status == FAX_SAVE_MALFORMED);
    char text[MENU_COLS + 1];
    bool cursor;
    menu_row(&menu, 0, text, &cursor);
    CHECK(cursor && !strcmp(text, "} 1 New game"));
    menu_row(&menu, 3, text, &cursor);
    CHECK(!cursor && !strcmp(text, "  2 Adept        Conflate"));
    menu_row(&menu, 4, text, &cursor);
    CHECK(!strcmp(text, "    Gold 11259375 Exp 4900"));
    slots[1].p.gold = 1234567;
    menu_row(&menu, 4, text, &cursor);
    CHECK(!strcmp(text, "    Gold 1234567 Exp 4900"));   /* Exp under the town */
    slots[1].p.gold = 0xABCDEF;
    menu_row(&menu, 6, text, &cursor);
    CHECK(!strcmp(text, "  3 Damaged file"));
    menu_row(&menu, 9, text, &cursor);
    CHECK(!strcmp(text, "  Erase a file"));

    bool moved;
    Menu m = { PICK, 0, 0 };
    CHECK(menu_press(&m, BUTTON_UP, &moved) == 0 && moved && m.cursor == ERASE_ROW);   /* wraps */
    CHECK(menu_press(&m, BUTTON_DOWN, &moved) == 0 && m.cursor == 0);
    CHECK(menu_press(&m, BUTTON_START, &moved) == 1);                                 /* a new game in file 1 */
    m.cursor = 1;
    CHECK(menu_press(&m, BUTTON_A, &moved) == 2);                                     /* continue file 2 */
    m.cursor = 2;
    CHECK(menu_press(&m, BUTTON_A, &moved) == 0 && m.mode == PICK);                   /* a damaged file */
    CHECK(menu_press(&m, BUTTON_B | BUTTON_SELECT, &moved) == 0 && !moved);
    /* Erase: the cursor starts on the first file with something in it. */
    m.cursor = ERASE_ROW;
    CHECK(menu_press(&m, BUTTON_A, &moved) == 0 && m.mode == ERASE && m.cursor == 1);
    menu_row(&m, 9, text, &cursor);
    CHECK(!strcmp(text, "  Erase which file?"));
    CHECK(menu_press(&m, BUTTON_UP, &moved) == 0 && m.cursor == 0);
    CHECK(menu_press(&m, BUTTON_A, &moved) == 0 && m.mode == ERASE);                  /* nothing to erase */
    CHECK(menu_press(&m, BUTTON_B, &moved) == 0 && m.mode == PICK && m.cursor == ERASE_ROW);
    CHECK(menu_press(&m, BUTTON_A, &moved) == 0 && m.mode == ERASE && m.cursor == 1);
    CHECK(menu_press(&m, BUTTON_A, &moved) == 0 && m.mode == CONFIRM && m.target == 1);
    menu_row(&m, 9, text, &cursor);
    CHECK(!strcmp(text, "  Erase file 2? A yes B no"));
    menu_row(&m, 3, text, &cursor);
    CHECK(cursor);
    CHECK(menu_press(&m, BUTTON_DOWN, &moved) == 0 && m.mode == CONFIRM && m.target == 1);
    CHECK(menu_press(&m, BUTTON_B, &moved) == 0 && m.mode == ERASE);
    CHECK(menu_press(&m, BUTTON_A, &moved) == 0 && m.mode == CONFIRM);
    CHECK(menu_press(&m, BUTTON_A, &moved) == 0 && m.mode == PICK && m.cursor == 1);
    CHECK(!exists("faxanadu_file2.txt") && slots[1].status == FAX_SAVE_MISSING);
    m.cursor = ERASE_ROW;
    CHECK(menu_press(&m, BUTTON_A, &moved) == 0 && m.mode == ERASE && m.cursor == 2);  /* the damaged one is left */
    remove_files();
}

/* The title's loop: the game sees Start only when a file is chosen. */
static void title(void) {
    stock(); remove_files();
    FaxSaveRecord r;
    FaxSaveProgress p = sample();
    fax_save_record_clear(&r);
    fax_save_progress_put(&r, &p);
    fax_save_record_set(&r, "gurus", "Eolis Mascon");
    CHECK(fax_save_file_write(3, &r));
    ram[JOY1_CHANGED] = BUTTON_START | BUTTON_SELECT;
    menu = (Menu){ PICK, 0, 0 };
    title_up = true;
    menu.cursor = 2;
    ram[JOY1_CHANGED] = BUTTON_SELECT;
    CHECK(title_hook(START_SCREEN_INPUT) == 1 && ram[JOY1_CHANGED] == 0 && title_up);
    ram[JOY1_CHANGED] = BUTTON_A;
    scan();
    CHECK(title_hook(START_SCREEN_INPUT) == 1 && ram[JOY1_CHANGED] == (BUTTON_A | BUTTON_START));
    CHECK(ram[START_SCREEN_CHOICE] == 1 && continuing == 3 && fax_save_file() == 3 && !title_up && furthest == 5);
    /* Continue: no mantra screen, the file's progress in RAM ... */
    CHECK(mantra_screen_hook(PASSWORD_SCREEN_SHOW) == 1 && ram[TEMPLE_SPAWN_POINT] == 3 && ram[PLAYER_TITLE] == 4);
    /* ... Player_SetInitialExpAndGold resets gold, experience and the next title ... */
    ram[GOLD] = ram[GOLD + 1] = ram[GOLD + 2] = 0; ram[EXPERIENCE] = 0; ram[NEXT_PLAYER_TITLE] = 4;
    /* ... and they come back before Player_Spawn. */
    CHECK(start_spawn_hook(START_SPAWN) == 0 && ram[GOLD + 2] == 0xAB && ram[EXPERIENCE] == (4900 & 255) &&
          ram[NEXT_PLAYER_TITLE] == 5 && continuing == 0);
    CHECK(mantra_screen_hook(PASSWORD_SCREEN_SHOW) == 0);   /* the mantra screen, if anything else shows it */
    /* Respawning (here, continuing) saves the file, keeping its spawn point. */
    ram[AREA_CURRENT_SCREEN] = temple_screen[3];
    ram[GOLD] = 1;
    CHECK(spawned_hook(SPAWNED) == 0);
    CHECK(fax_save_file_read(3, &r) == FAX_SAVE_OK && !strcmp(fax_save_record_get(&r, "spawn"), "3") &&
          !strcmp(fax_save_record_get(&r, "gurus"), "Eolis Mascon") && !strcmp(fax_save_record_get(&r, "furthest"), "5"));
    /* A new game in an empty file. */
    enter_title();
    menu.cursor = 0;
    ram[JOY1_CHANGED] = BUTTON_START;
    CHECK(title_hook(START_SCREEN_INPUT) == 1 && ram[JOY1_CHANGED] == BUTTON_START && ram[START_SCREEN_CHOICE] == 0);
    CHECK(fax_save_file() == 1 && continuing == 0 && furthest == 0 && !exists("faxanadu_file1.txt"));
    CHECK(mantra_screen_hook(PASSWORD_SCREEN_SHOW) == 0 && start_spawn_hook(START_SPAWN) == 0);
    /* Off, the title is the game's own. */
    deactivate();
    ram[JOY1_CHANGED] = BUTTON_START;
    CHECK(title_hook(START_SCREEN_INPUT) == 0 && ram[JOY1_CHANGED] == BUTTON_START);
    remove_files();
}

/* ---- importing mantras ---- */

static void importing(void) {
    stock(); remove_files();
    /* The first run: faxanadu.srm's mantra and the visited Gurus into file 1. */
    write_text("faxanadu.srm", "Mant ra1\n");
    write_text("faxanadu_gurus.txt", "Eolis\nApolune\n");
    enter_title();
    CHECK(imports == 1 && slots[0].status == FAX_SAVE_OK && slots[0].p.title == 4 && slots[0].p.gold == 1600);
    FaxSaveRecord r;
    CHECK(fax_save_file_read(1, &r) == FAX_SAVE_OK);
    CHECK(!strcmp(fax_save_record_get(&r, "gurus"), "Eolis Apolune Conflate"));   /* and the furthest Guru */
    CHECK(!strcmp(fax_save_record_get(&r, "imported_mantra"), "Mantra1") && !strcmp(fax_save_record_get(&r, "items"), "4 9 16"));
    CHECK(exists("faxanadu_imported.txt"));
    /* Not again, even into an emptied file. */
    fax_save_file_erase(1);
    enter_title();
    CHECK(imports == 1 && slots[0].status == FAX_SAVE_MISSING);
    /* A new mantra (--password, or the launcher's Mantra field) goes into the
     * first empty file; a rejected one is noted, not imported. */
    fax_save_file_write(1, &r);
    cli = "Mantra2";
    enter_title();
    CHECK(imports == 2 && slots[1].status == FAX_SAVE_OK && slots[1].p.title == 1 && slots[1].p.selected[4] == 0xFF);
    CHECK(fax_save_file_read(2, &r) == FAX_SAVE_OK && !strcmp(fax_save_record_get(&r, "gurus"), "Eolis Apolune"));
    cli = "Wrong";
    int calls = mantra_calls;
    enter_title();
    CHECK(imports == 2 && mantra_calls == calls + 1 && slots[2].status == FAX_SAVE_MISSING);
    enter_title();
    CHECK(mantra_calls == calls + 1);
    /* No empty file: kept for later. */
    write_text("faxanadu_file3.txt", "version = 1\n");
    write_text("faxanadu.srm", "Mantra2x\n");
    calls = mantra_calls;
    enter_title();
    CHECK(mantra_calls == calls && !was_imported("Mantra2x"));
    /* --no-save imports nothing. */
    remove_files();
    saves_on = false;
    write_text("faxanadu.srm", "Mantra1\n");
    enter_title();
    CHECK(slots[0].status == FAX_SAVE_MISSING && !exists("faxanadu_imported.txt"));
    saves_on = true;
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

    /* After a new title, and at Eolis's Guru with the Ring. */
    in_play(); travel_offers = true;
    temple_guru(4, true, "0");
    EXPECT("msg 23", MENU, SAVED, "end");
    CHECK(ram[TEMPLE_SPAWN_POINT] == 4 && furthest == 4);
    in_play();
    ram[AREA_CURRENT_AREA] = BUILDINGS; ram[AREA_CURRENT_SCREEN] = temple_screen[0];
    run(0xA0C6, "A");
    EXPECT("msg 2D", QUESTION, SAVED, "end");
    CHECK(ram[TEMPLE_SPAWN_POINT] == 0 && furthest == 3);

    /* No file in play, or Save files off: the mantra, as before. */
    stock(); remove_files();
    temple_guru(5, false, "");
    EXPECT("msg 22", "mantra", "end");
    CHECK(ram[TEMPLE_SPAWN_POINT] == 5);
    in_play(); deactivate();
    temple_guru(5, false, "");
    EXPECT("msg 22", "mantra", "end");

    /* A layout where our step would go is refused: the mantra shows. */
    in_play();
    rom[prg(12, STEP)] = 0x00;
    temple_guru(5, false, "");
    EXPECT("msg 22", "mantra", "end");
    CHECK(failed && !fax_files_guru_choice());
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
    mantras();
    files_on_disk();
    api();
    file_select();
    title();
    importing();
    choices();
    save_states();
    if (!base) { CHECK(chdir("/") == 0); rmdir(dir); }
    printf("files_test: %u checks passed\n", checks);
    return 0;
}
