/* Save files: the record and the files. USA 57dd23d1, chipx86 byte-exact
 * disassembly.
 *
 * A save is a versioned record of named fields, not a machine save state
 * (those hash the PRG, so they belong to one build). It holds everything the
 * mantra held (Password_EncodeGameState, bank 12: the temple spawn point, the
 * title, the special items, the quests and the five inventories with their
 * selections), plus what the mantra loses: the exact gold and experience and
 * the title earned but not yet granted. With Save files the spawn point is the
 * Guru the file was last saved with, so the furthest temple reached, which
 * the original kept there, is a field of its own. Features add fields of
 * their own (Guru travel's visited Gurus, and others through fax_save_set).
 * Each file is text, one `key = value` a line, so a file stays readable, and
 * fields the build does not know survive it.
 * See docs/GAME_INTERNALS.md, "Save files". */
#include "fax_save.h"
#include "cyc_mod.h"
#include "cyc_host_extras.h"
#ifdef CYC_WITH_SDL
#include <SDL.h>
#endif
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* RAM */
#define EXPERIENCE         0x0390u /* 16-bit */
#define GOLD               0x0392u /* 24-bit */
#define SPECIAL_ITEMS      0x042Cu
#define QUESTS             0x042Du
#define PLAYER_TITLE       0x0437u
#define TEMPLE_SPAWN_POINT 0x0439u
#define NEXT_PLAYER_TITLE  0x04EDu
#define SELECTED           0x03BDu /* SelectedWeapon, SelectedArmor, SelectedShield, SelectedMagic, SelectedItem */
#define COUNTS             0x03C2u /* NumberOfWeapons ... NumberOfItems */
static const uint16_t lists[FAX_SAVE_LISTS] = { 0x039D, 0x03A1, 0x03A5, 0x03A9,
                                                0x03AD }; /* WeaponInventory ... ItemInventory */

const char *const fax_save_list_key[FAX_SAVE_LISTS] = { "weapons", "armor", "shields", "spells", "items" };
const char *const fax_save_selected_key[FAX_SAVE_LISTS] = { "selected_weapon", "selected_armor", "selected_shield",
                                                            "selected_spell", "selected_item" };
const uint8_t fax_save_list_max[FAX_SAVE_LISTS] = { 4, 4, 4, 4, 8 };
static const char *const towns[FAX_SAVE_TEMPLES] = { "Eolis",  "Apolune",  "Forepaw",  "Mascon",
                                                     "Victim", "Conflate", "Daybreak", "Dartmoor" };
static const char *const core[] = {
    "version",         "title",          "next_title",      "experience",     "gold",         "spawn",  "furthest",
    "special_items",   "quests",         "weapons",         "armor",          "shields",      "spells", "items",
    "selected_weapon", "selected_armor", "selected_shield", "selected_spell", "selected_item"
};

const char *fax_save_town(unsigned temple) { return temple < FAX_SAVE_TEMPLES ? towns[temple] : "?"; }

bool fax_save_core_key(const char *key) {
    for (unsigned i = 0; i < sizeof core / sizeof core[0]; ++i)
        if (!strcmp(key, core[i])) return true;
    return false;
}

/* ---- the record ---- */

bool fax_save_key_ok(const char *key) {
    size_t n = key ? strlen(key) : 0;
    if (!n || n >= FAX_SAVE_KEY) return false;
    for (; *key; ++key)
        if (!(islower((unsigned char)*key) || isdigit((unsigned char)*key) || strchr("_.-", *key))) return false;
    return true;
}
bool fax_save_value_ok(const char *value) {
    if (!value || strlen(value) >= FAX_SAVE_VALUE) return false;
    for (; *value; ++value)
        if (*value < 0x20 || *value > 0x7E) return false;
    return true;
}

static int find(const FaxSaveRecord *r, const char *key) {
    for (int i = 0; i < r->count; ++i)
        if (!strcmp(r->field[i].key, key)) return i;
    return -1;
}
const char *fax_save_record_get(const FaxSaveRecord *r, const char *key) {
    int i = find(r, key);
    return i < 0 ? NULL : r->field[i].value;
}
bool fax_save_record_set(FaxSaveRecord *r, const char *key, const char *value) {
    if (!fax_save_key_ok(key) || (value && !fax_save_value_ok(value))) return false;
    int i = find(r, key);
    if (!value) {
        if (i >= 0)
            memmove(&r->field[i], &r->field[i + 1], (size_t)(r->count - i - 1) * sizeof r->field[0]), --r->count;
        return true;
    }
    if (i < 0) {
        if (r->count >= FAX_SAVE_FIELDS) return false;
        i = r->count++;
        snprintf(r->field[i].key, FAX_SAVE_KEY, "%s", key);
    }
    snprintf(r->field[i].value, FAX_SAVE_VALUE, "%s", value);
    return true;
}
void fax_save_record_clear(FaxSaveRecord *r) {
    r->count = 0;
    char v[8];
    snprintf(v, sizeof v, "%d", FAX_SAVE_VERSION);
    fax_save_record_set(r, "version", v);
}

int fax_save_record_format(const FaxSaveRecord *r, char *out, size_t cap) {
    size_t n = 0;
    int k = snprintf(out, cap, "# Faxanadu DX save file: named fields, one a line.\n");
    if (k < 0 || (size_t)k >= cap) return -1;
    n = (size_t)k;
    for (int i = 0; i < r->count; ++i) {
        k = snprintf(out + n, cap - n, "%s = %s\n", r->field[i].key, r->field[i].value);
        if (k < 0 || (size_t)k >= cap - n) return -1;
        n += (size_t)k;
    }
    return (int)n;
}

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') ++s;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
    return s;
}
/* Strict decimal, within [0, max]. */
static bool number(const char *s, unsigned long max, unsigned long *out) {
    if (!s || !isdigit((unsigned char)*s)) return false;
    char *end;
    unsigned long v = strtoul(s, &end, 10);
    if (*end || v > max) return false;
    *out = v;
    return true;
}

/* Migrations, by the version each upgrades from. None yet: the first one,
 * from version 1, goes at index 1 and rewrites a version 1 record's fields
 * into version 2's. */
typedef bool (*Migration)(FaxSaveRecord *r);
static const Migration migrations[FAX_SAVE_VERSION + 1] = { NULL };

FaxSaveStatus fax_save_record_parse(FaxSaveRecord *r, const char *text) {
    r->count = 0;
    char line[FAX_SAVE_KEY + FAX_SAVE_VALUE + 16];
    while (*text) {
        size_t n = strcspn(text, "\n");
        if (n >= sizeof line) return FAX_SAVE_MALFORMED;
        memcpy(line, text, n);
        line[n] = 0;
        text += n + (text[n] == '\n');
        char *s = trim(line);
        if (!*s || *s == '#') continue;
        char *eq = strchr(s, '=');
        if (!eq) return FAX_SAVE_MALFORMED;
        *eq = 0;
        char *key = trim(s), *value = trim(eq + 1);
        if (find(r, key) >= 0 || !fax_save_record_set(r, key, value)) return FAX_SAVE_MALFORMED;
    }
    unsigned long version;
    if (!number(fax_save_record_get(r, "version"), 0xFFFF, &version) || version < 1) return FAX_SAVE_MALFORMED;
    if (version > FAX_SAVE_VERSION) return FAX_SAVE_NEWER;
    for (; version < FAX_SAVE_VERSION; ++version) {
        if (!migrations[version] || !migrations[version](r)) return FAX_SAVE_MALFORMED;
        char v[8];
        snprintf(v, sizeof v, "%lu", version + 1);
        fax_save_record_set(r, "version", v);
    }
    return FAX_SAVE_OK;
}

/* The next space-separated word of *s into out (at most cap - 1 characters);
 * false at the end. */
static bool word(const char **s, char *out, size_t cap) {
    while (**s == ' ') ++*s;
    if (!**s) return false;
    size_t n = 0;
    for (; **s && **s != ' '; ++*s)
        if (n + 1 < cap) out[n++] = **s;
    out[n] = 0;
    return true;
}

/* ---- the progress ---- */

static bool field_number(const FaxSaveRecord *r, const char *key, unsigned long max, unsigned long fallback,
                         unsigned long *out) {
    const char *v = fax_save_record_get(r, key);
    if (!v) {
        *out = fallback;
        return true;
    }
    return number(v, max, out);
}
/* An entry or a selection: 0-254; "none" is $FF. */
static bool field_entry(const char *v, uint8_t *out) {
    unsigned long n;
    if (!strcmp(v, "none")) {
        *out = 0xFF;
        return true;
    }
    if (!number(v, 0xFE, &n)) return false;
    *out = (uint8_t)n;
    return true;
}

bool fax_save_progress_get(const FaxSaveRecord *r, FaxSaveProgress *p) {
    memset(p, 0, sizeof *p);
    unsigned long v;
    if (!field_number(r, "title", 15, 0, &v)) return false;
    p->title = (uint8_t)v;
    if (!field_number(r, "next_title", 15, p->title, &v)) return false;
    p->next_title = (uint8_t)v;
    if (!field_number(r, "experience", 0xFFFF, 0, &v)) return false;
    p->experience = (uint16_t)v;
    if (!field_number(r, "gold", 0xFFFFFF, 0, &v)) return false;
    p->gold = (uint32_t)v;
    if (!field_number(r, "spawn", FAX_SAVE_TEMPLES - 1, 0, &v)) return false;
    p->spawn = (uint8_t)v;
    if (!field_number(r, "furthest", FAX_SAVE_TEMPLES - 1, p->spawn, &v)) return false;
    p->furthest = (uint8_t)(v > p->spawn ? v : p->spawn);
    if (!field_number(r, "special_items", 0xFF, 0, &v)) return false;
    p->special_items = (uint8_t)v;
    if (!field_number(r, "quests", 0xFF, 0, &v)) return false;
    p->quests = (uint8_t)v;
    for (int c = 0; c < FAX_SAVE_LISTS; ++c) {
        const char *list = fax_save_record_get(r, fax_save_list_key[c]);
        const char *sel = fax_save_record_get(r, fax_save_selected_key[c]);
        p->selected[c] = 0xFF;
        if (sel && !field_entry(sel, &p->selected[c])) return false;
        if (!list) continue;
        char t[FAX_SAVE_VALUE];
        while (word(&list, t, sizeof t)) {
            if (p->count[c] >= fax_save_list_max[c] || !field_entry(t, &p->list[c][p->count[c]]) ||
                p->list[c][p->count[c]] == 0xFF)
                return false;
            ++p->count[c];
        }
    }
    return true;
}

bool fax_save_progress_put(FaxSaveRecord *r, const FaxSaveProgress *p) {
    char v[FAX_SAVE_VALUE];
    bool ok = true;
#define PUT(key, fmt, x) (snprintf(v, sizeof v, fmt, x), ok = fax_save_record_set(r, key, v) && ok)
    PUT("title", "%u", p->title);
    PUT("next_title", "%u", p->next_title);
    PUT("experience", "%u", p->experience);
    PUT("gold", "%lu", (unsigned long)(p->gold & 0xFFFFFF));
    PUT("spawn", "%u", p->spawn);
    PUT("furthest", "%u", p->furthest);
    PUT("special_items", "%u", p->special_items);
    PUT("quests", "%u", p->quests);
#undef PUT
    for (int c = 0; c < FAX_SAVE_LISTS; ++c) {
        size_t n = 0;
        v[0] = 0;
        for (int i = 0; i < p->count[c] && i < fax_save_list_max[c]; ++i)
            n += (size_t)snprintf(v + n, sizeof v - n, "%s%u", i ? " " : "", p->list[c][i]);
        ok = fax_save_record_set(r, fax_save_list_key[c], v) && ok;
        if (p->selected[c] == 0xFF) snprintf(v, sizeof v, "none");
        else snprintf(v, sizeof v, "%u", p->selected[c]);
        ok = fax_save_record_set(r, fax_save_selected_key[c], v) && ok;
    }
    return ok;
}

uint8_t fax_save_gurus_parse(const char *text) {
    uint8_t visited = 0;
    char t[FAX_SAVE_VALUE];
    while (text && word(&text, t, sizeof t))
        for (int i = 0; i < FAX_SAVE_TEMPLES; ++i)
            if (!strcmp(t, towns[i])) visited |= (uint8_t)(1u << i);
    return visited;
}
void fax_save_gurus_format(uint8_t visited, char out[FAX_SAVE_VALUE]) {
    size_t n = 0;
    out[0] = 0;
    for (int i = 0; i < FAX_SAVE_TEMPLES; ++i)
        if (visited >> i & 1) n += (size_t)snprintf(out + n, FAX_SAVE_VALUE - n, "%s%s", n ? " " : "", towns[i]);
}

/* ---- the machine ---- */

void fax_save_progress_capture(FaxSaveProgress *p, unsigned furthest) {
    memset(p, 0, sizeof *p);
    p->title = cyc_mod_peek(PLAYER_TITLE) & 15;
    p->next_title = cyc_mod_peek(NEXT_PLAYER_TITLE) & 15;
    p->experience = (uint16_t)(cyc_mod_peek(EXPERIENCE) | cyc_mod_peek(EXPERIENCE + 1) << 8);
    p->gold =
        (uint32_t)cyc_mod_peek(GOLD) | (uint32_t)cyc_mod_peek(GOLD + 1) << 8 | (uint32_t)cyc_mod_peek(GOLD + 2) << 16;
    p->spawn = cyc_mod_peek(TEMPLE_SPAWN_POINT) & 7;
    p->furthest = (uint8_t)(furthest < FAX_SAVE_TEMPLES && furthest > p->spawn ? furthest : p->spawn);
    p->special_items = cyc_mod_peek(SPECIAL_ITEMS);
    p->quests = cyc_mod_peek(QUESTS);
    for (int c = 0; c < FAX_SAVE_LISTS; ++c) {
        p->selected[c] = cyc_mod_peek((uint16_t)(SELECTED + c));
        unsigned n = cyc_mod_peek((uint16_t)(COUNTS + c));
        if (n > fax_save_list_max[c]) n = fax_save_list_max[c];
        for (unsigned i = 0; i < n; ++i) {
            uint8_t e = cyc_mod_peek((uint16_t)(lists[c] + i));
            if (e != 0xFF) p->list[c][p->count[c]++] = e;
        }
    }
}

void fax_save_progress_apply(const FaxSaveProgress *p) {
    cyc_mod_poke(PLAYER_TITLE, p->title);
    cyc_mod_poke(NEXT_PLAYER_TITLE, p->next_title);
    cyc_mod_poke(EXPERIENCE, (uint8_t)p->experience);
    cyc_mod_poke(EXPERIENCE + 1, (uint8_t)(p->experience >> 8));
    cyc_mod_poke(GOLD, (uint8_t)p->gold);
    cyc_mod_poke(GOLD + 1, (uint8_t)(p->gold >> 8));
    cyc_mod_poke(GOLD + 2, (uint8_t)(p->gold >> 16));
    cyc_mod_poke(TEMPLE_SPAWN_POINT, p->spawn);
    cyc_mod_poke(SPECIAL_ITEMS, p->special_items);
    cyc_mod_poke(QUESTS, p->quests);
    for (int c = 0; c < FAX_SAVE_LISTS; ++c) {
        cyc_mod_poke((uint16_t)(SELECTED + c), p->selected[c]);
        cyc_mod_poke((uint16_t)(COUNTS + c), p->count[c]);
        for (int i = 0; i < p->count[c]; ++i) cyc_mod_poke((uint16_t)(lists[c] + i), p->list[c][i]);
    }
}

/* ---- files ---- */

static bool save_path(const char *name, char out[1100]) {
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
bool fax_save_file_path(int file, char out[1100]) {
    char name[32];
    if (file < 1 || file > FAX_SAVE_FILES) return false;
    snprintf(name, sizeof name, "faxanadu_file%d.txt", file);
    return save_path(name, out);
}

FaxSaveStatus fax_save_file_read(int file, FaxSaveRecord *r) {
    char path[1100];
    static char text[FAX_SAVE_TEXT + 1];
    fax_save_record_clear(r);
    if (!fax_save_file_path(file, path)) return FAX_SAVE_MISSING;
    FILE *f = fopen(path, "rb");
    if (!f) return FAX_SAVE_MISSING;
    size_t n = fread(text, 1, sizeof text, f);
    fclose(f);
    if (n > FAX_SAVE_TEXT) return FAX_SAVE_MALFORMED;
    text[n] = 0;
    if (strlen(text) != n) return FAX_SAVE_MALFORMED;
    return fax_save_record_parse(r, text);
}

bool fax_save_file_write(int file, const FaxSaveRecord *r) {
    char path[1100], temp[1120];
    static char text[FAX_SAVE_TEXT];
    if (!cyc_host_saves_enabled() || !fax_save_file_path(file, path)) return false;
    int n = fax_save_record_format(r, text, sizeof text);
    if (n < 0) return false;
    snprintf(temp, sizeof temp, "%s.tmp", path);
    FILE *f = fopen(temp, "wb");
    if (!f) return false;
    bool ok = fwrite(text, 1, (size_t)n, f) == (size_t)n;
    if (fclose(f) != 0) ok = false;
#ifdef _WIN32
    extern bool faxanadu_replace_file(const char *, const char *);
    if (ok) ok = faxanadu_replace_file(temp, path);
#else
    if (ok) ok = rename(temp, path) == 0;
#endif
    if (!ok) remove(temp);
    return ok;
}

bool fax_save_file_erase(int file) {
    char path[1100];
    if (!cyc_host_saves_enabled() || !fax_save_file_path(file, path)) return false;
    return remove(path) == 0;
}

/* ---- the file in play ---- */

static bool save_files;
static int active;
static FaxSaveRecord current;
static FaxSaveListener listeners[8];
static unsigned listener_count;

bool fax_save_listen(FaxSaveListener listener) {
    if (!listener || listener_count >= sizeof listeners / sizeof listeners[0]) return false;
    listeners[listener_count++] = listener;
    return true;
}
static void tell(FaxSaveEvent event) {
    for (unsigned i = 0; i < listener_count; ++i) listeners[i](event);
}

void fax_save_set_enabled(bool on) {
    save_files = on;
    if (!on) active = 0;
}
bool fax_save_enabled(void) { return save_files; }
int fax_save_file(void) { return save_files ? active : 0; }
const FaxSaveRecord *fax_save_current(void) { return &current; }

const char *fax_save_get(const char *key) { return fax_save_file() ? fax_save_record_get(&current, key) : NULL; }
bool fax_save_set(const char *key, const char *value) {
    if (!fax_save_file() || !fax_save_key_ok(key) || fax_save_core_key(key)) return false;
    return fax_save_record_set(&current, key, value);
}
long fax_save_get_int(const char *key, long fallback) {
    const char *v = fax_save_get(key);
    if (!v || !*v) return fallback;
    char *end;
    long n = strtol(v, &end, 10);
    return *end ? fallback : n;
}
bool fax_save_set_int(const char *key, long value) {
    char v[24];
    snprintf(v, sizeof v, "%ld", value);
    return fax_save_set(key, v);
}

void fax_save_begin_new(int file) {
    active = file;
    fax_save_record_clear(&current);
    tell(FAX_SAVE_NEW_GAME);
}
void fax_save_begin_continue(int file, const FaxSaveRecord *r) {
    active = file;
    current = *r;
    tell(FAX_SAVE_CONTINUED);
}
void fax_save_resume(int file) {
    active = file;
    if (!file || fax_save_file_read(file, &current) != FAX_SAVE_OK) fax_save_record_clear(&current);
}
void fax_save_end(void) {
    active = 0;
    fax_save_record_clear(&current);
}

bool fax_save_commit(unsigned furthest) {
    if (!fax_save_file()) return false;
    tell(FAX_SAVE_WRITING);
    FaxSaveProgress p;
    fax_save_progress_capture(&p, furthest);
    char v[8];
    snprintf(v, sizeof v, "%d", FAX_SAVE_VERSION);
    fax_save_record_set(&current, "version", v);
    return fax_save_progress_put(&current, &p) && fax_save_file_write(active, &current);
}
