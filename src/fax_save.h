#pragma once
/* Save files (fax_save.c): three files of named fields that replace the
 * mantra. A file is text, one `key = value` a line, with a `version`. The
 * game's progress is in the core fields below; any other field belongs to the
 * feature that sets it, through the API at the end of this header.
 * See docs/GAME_INTERNALS.md, "Save files". */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    FAX_SAVE_FILES = 3,   /* files 1-3 */
    FAX_SAVE_VERSION = 1, /* the record's version; a newer one is not read */
    FAX_SAVE_FIELDS = 64, /* fields a record holds */
    FAX_SAVE_KEY = 32,    /* a key's size, with its terminator */
    FAX_SAVE_VALUE = 96,  /* a value's size, with its terminator */
    FAX_SAVE_TEXT = FAX_SAVE_FIELDS * (FAX_SAVE_KEY + FAX_SAVE_VALUE + 4) + 128,
    FAX_SAVE_TEMPLES = 8,
};

/* ---- the record: named fields, as text ---- */

typedef struct {
    char key[FAX_SAVE_KEY], value[FAX_SAVE_VALUE];
} FaxSaveField;
typedef struct {
    int count;
    FaxSaveField field[FAX_SAVE_FIELDS];
} FaxSaveRecord;

typedef enum {
    FAX_SAVE_OK,
    FAX_SAVE_MISSING,   /* no file */
    FAX_SAVE_MALFORMED, /* a line that is not `key = value`, a bad value, or no version */
    FAX_SAVE_NEWER,     /* written by a newer version of the record */
} FaxSaveStatus;

void fax_save_record_clear(FaxSaveRecord *r); /* empty, at the current version */
const char *fax_save_record_get(const FaxSaveRecord *r, const char *key);
/* Set a field, keeping its place, or append it; NULL removes it. False for a
 * bad key or value, or a full record. */
bool fax_save_record_set(FaxSaveRecord *r, const char *key, const char *value);
bool fax_save_key_ok(const char *key);     /* [a-z0-9_.-], 1-31 characters */
bool fax_save_value_ok(const char *value); /* printable ASCII, at most 95 characters */
/* The text of a record, with a comment line first. Returns its length, or -1
 * if it does not fit. */
int fax_save_record_format(const FaxSaveRecord *r, char *out, size_t cap);
/* Parse a record's text. A record of an older version is migrated to the
 * current one; a newer one is refused, and so is anything malformed. */
FaxSaveStatus fax_save_record_parse(FaxSaveRecord *r, const char *text);

/* ---- the game's progress: the core fields, typed ---- */

enum { FAX_SAVE_WEAPONS, FAX_SAVE_ARMOR, FAX_SAVE_SHIELDS, FAX_SAVE_MAGIC, FAX_SAVE_ITEMS, FAX_SAVE_LISTS };
typedef struct {
    uint8_t title, next_title; /* PlayerTitle, NextPlayerTitle: 0-15 */
    uint16_t experience;
    uint32_t gold;                 /* 24 bits */
    uint8_t spawn;                 /* TempleSpawnPoint: the temple of the Guru the file was saved with,
                                         where it continues and death respawns */
    uint8_t furthest;              /* the furthest temple reached (the original's TempleSpawnPoint) */
    uint8_t special_items, quests; /* bit flags */
    uint8_t count[FAX_SAVE_LISTS]; /* inventory: entries in each list */
    uint8_t list[FAX_SAVE_LISTS][8];
    uint8_t selected[FAX_SAVE_LISTS]; /* $FF: none */
} FaxSaveProgress;

extern const char *const fax_save_list_key[FAX_SAVE_LISTS];     /* "weapons", ... "items" */
extern const char *const fax_save_selected_key[FAX_SAVE_LISTS]; /* "selected_weapon", ... */
extern const uint8_t fax_save_list_max[FAX_SAVE_LISTS];         /* 4, 4, 4, 4, 8 */
const char *fax_save_town(unsigned temple);                     /* "Eolis" ... "Dartmoor" */

/* Read the core fields, filling in defaults for missing ones (a new game's
 * Novice in Eolis). False if one is out of range. */
bool fax_save_progress_get(const FaxSaveRecord *r, FaxSaveProgress *p);
/* Write the core fields into a record, keeping its other fields. */
bool fax_save_progress_put(FaxSaveRecord *r, const FaxSaveProgress *p);

/* The visited Gurus (Guru travel's `gurus` field): town names, a bit per temple. */
uint8_t fax_save_gurus_parse(const char *text);
void fax_save_gurus_format(uint8_t visited, char out[FAX_SAVE_VALUE]);

/* ---- the machine ---- */

/* The progress in CPU RAM now, and the furthest temple, which RAM does not
 * hold while Save files keep TempleSpawnPoint at the saved Guru. */
void fax_save_progress_capture(FaxSaveProgress *p, unsigned furthest);
/* Put the progress into CPU RAM, as Password_Load does with a mantra, plus
 * the exact gold, experience and next title. */
void fax_save_progress_apply(const FaxSaveProgress *p);

/* ---- files: faxanadu_file1.txt to faxanadu_file3.txt beside the game ---- */

bool fax_save_file_path(int file, char out[1100]);
FaxSaveStatus fax_save_file_read(int file, FaxSaveRecord *r);
bool fax_save_file_write(int file, const FaxSaveRecord *r); /* replaced whole, never truncated */
bool fax_save_file_erase(int file);

/* ---- the file in play, and the API for other features ---- */

/* Features keep their own state in the file in play through these. Use keys
 * named after the feature, like "elixir.charge". The core fields above are
 * written from RAM at each save and cannot be set; any other field is kept as
 * it is, even one no feature knows. */
bool fax_save_enabled(void);               /* the Save files feature is on */
int fax_save_file(void);                   /* the file in play, 1-3, or 0 (on the title, or the feature is off) */
const char *fax_save_get(const char *key); /* NULL if the file has no such field */
bool fax_save_set(const char *key, const char *value); /* NULL removes; false with no file in play */
long fax_save_get_int(const char *key, long fallback);
bool fax_save_set_int(const char *key, long value);

/* What happens to the file in play. A listener sets its fields when the file
 * is about to be written (WRITING), and reads them back when it is continued
 * (CONTINUED) or resets them for a new game (NEW_GAME). */
typedef enum { FAX_SAVE_NEW_GAME, FAX_SAVE_CONTINUED, FAX_SAVE_WRITING } FaxSaveEvent;
typedef void (*FaxSaveListener)(FaxSaveEvent event);
bool fax_save_listen(FaxSaveListener listener); /* call from a constructor; up to 8 */

/* ---- for fax_files.c ---- */
void fax_save_set_enabled(bool on);
bool fax_save_core_key(const char *key);
void fax_save_begin_new(int file);                              /* a new game in `file` */
void fax_save_begin_continue(int file, const FaxSaveRecord *r); /* continuing `file` */
void fax_save_resume(int file); /* a save state put `file` in play: reread it, tell no one */
void fax_save_end(void);        /* back on the title: no file in play */
/* Write the file in play with the progress in RAM and the furthest temple. */
bool fax_save_commit(unsigned furthest);
const FaxSaveRecord *fax_save_current(void);
