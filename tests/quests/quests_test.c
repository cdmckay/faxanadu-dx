/* Unit tests for the quest log's quests (fax_quest_table.c), without the
 * emulator.
 *
 * The table: every name and objective fits the text box's 16 columns (and an
 * objective its 4 lines), each quest has a line that gives it, and the save
 * file's ids are distinct. Each quest is checked complete, or not, against
 * game states: the flags, special items, inventory and places that complete
 * it, and those that do not.
 *
 * With the real ROM's path as the argument it also checks the table against
 * the scripts themselves: every line that gives a quest, and every next step,
 * is in the script the table names and in no other; walking a script as the
 * game would gives the lines the game shows in each state (the King with and
 * without gold, the spring of Trunk's old man with and without the Elixir,
 * the Mist house man before and after the meteorite); and the marks over
 * them: a "!" over the King until his quest is given, a "?" over Eolis's Guru
 * while the Ring of Elf is under way, and over the Mist house man holding the
 * Black Onyx. */
#include "fax_quest_table.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (!(c)) {                                                                                                    \
            ++failures;                                                                                                \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c);                                      \
        }                                                                                                              \
    } while (0)

enum { WEAPONS = FAX_QUEST_WEAPONS, ARMOR = FAX_QUEST_ARMOR, SHIELDS = FAX_QUEST_SHIELDS, ITEMS = FAX_QUEST_ITEMS };
#define BIT(q) (1u << (q))

static FaxQuestGame new_game(void) {
    FaxQuestGame g;
    memset(&g, 0, sizeof g);
    for (unsigned c = 0; c < FAX_QUEST_LISTS; ++c) g.selected[c] = 0xFF;
    g.selected[WEAPONS] = 0; /* the Hand Dagger in hand */
    return g;
}
static void give(FaxQuestGame *g, unsigned list, uint8_t id) { g->list[list][g->count[list]++] = id; }

/* ---- the table ---- */

static void table_fits(void) {
    for (unsigned q = 0; q < FAX_QUESTS; ++q) {
        const FaxQuest *f = &fax_quests[q];
        CHECK(f->name && strlen(f->name) > 0 && strlen(f->name) <= FAX_QUEST_COLS);
        unsigned lines = 1, col = 0;
        for (const char *s = f->objective; *s; ++s) {
            if (*s == '\n') {
                ++lines;
                col = 0;
                continue;
            }
            CHECK(*s >= 0x20 && *s < 0x7F && !strchr("#$%&()*+/:;<=>@[\\]^", *s)); /* glyphs the font has */
            ++col;
            CHECK(col <= FAX_QUEST_COLS);
        }
        CHECK(lines <= FAX_QUEST_LINES);
        CHECK(f->give[0] && f->giver[0]);
        for (unsigned i = 0; i < FAX_QUEST_GIVERS; ++i) CHECK(!f->give[i] == !f->giver[i] || !f->give[i]);
        CHECK(!f->step == !f->step_script);
        for (const char *c = f->id; *c; ++c) CHECK(*c >= 'a' && *c <= 'z');
        for (unsigned r = 0; r < q; ++r) CHECK(strcmp(fax_quests[r].id, f->id) && strcmp(fax_quests[r].name, f->name));
        CHECK(fax_quest_given_by(f->give[0]) & BIT(q));
    }
    CHECK(fax_quest_given_by(0x35) == BIT(FAX_QUEST_KING));                         /* the King's plea */
    CHECK(fax_quest_given_by(0x51) == (BIT(FAX_QUEST_BOOTS) | BIT(FAX_QUEST_SKY))); /* the nurse: both */
    CHECK(fax_quest_given_by(0xA2) == BIT(FAX_QUEST_EVIL)); /* the Fraternal Guru, as the Demon's Ring's step */
    CHECK(fax_quest_given_by(0x02) == 0 && fax_quest_given_by(0x36) == BIT(FAX_QUEST_KING));
    CHECK(fax_quest_given_by(0) == 0 && fax_quest_given_by(0x2B) == 0); /* chatter: "There are many rings" */
}

/* ---- complete ---- */

static void done_from_state(void) {
    FaxQuestGame g = new_game();
    CHECK(fax_quest_done_all(&g) == 0);

    g.special = 0x80; /* the Ring of Elf */
    CHECK(fax_quest_done_all(&g) == BIT(FAX_QUEST_ELF));

    g.quests = 0x01 | 0x02; /* two springs: the King's plea is not done */
    CHECK(fax_quest_done(FAX_QUEST_TRUNK, &g) && fax_quest_done(FAX_QUEST_SKY, &g) &&
          !fax_quest_done(FAX_QUEST_KING, &g));
    CHECK(fax_quest_done(FAX_QUEST_BOOTS, &g)); /* past the Spring of Sky, the boots were had */
    char line[FAX_QUEST_COLS + 1];
    CHECK(fax_quest_progress(FAX_QUEST_KING, &g, line) && !strcmp(line, "2 of 3 springs"));
    CHECK(!fax_quest_progress(FAX_QUEST_ELF, &g, line));
    g.quests |= 0x04;
    CHECK(fax_quest_done(FAX_QUEST_KING, &g) && fax_quest_done(FAX_QUEST_FIRST, &g));

    g = new_game(); /* the Mattock: held, selected, or Ripasheiku's */
    CHECK(!fax_quest_done(FAX_QUEST_MATTOCK, &g));
    give(&g, ITEMS, 0x09);
    CHECK(fax_quest_done(FAX_QUEST_MATTOCK, &g) && !fax_quest_done(FAX_QUEST_WALL, &g));
    g = new_game();
    g.selected[ITEMS] = 0x09;
    CHECK(fax_quest_done(FAX_QUEST_MATTOCK, &g));
    g = new_game();
    g.quests = 0x10;
    CHECK(fax_quest_done(FAX_QUEST_MATTOCK, &g));
    g = new_game();
    g.count[ITEMS] = 0;
    g.list[ITEMS][0] = 0x09; /* a stale entry past the count is not held */
    CHECK(!fax_quest_done(FAX_QUEST_MATTOCK, &g));
    g = new_game();
    g.reached = FAX_REACHED_FOREPAW; /* past the wall: both, the Mattock used up */
    CHECK(fax_quest_done(FAX_QUEST_MATTOCK, &g) && fax_quest_done(FAX_QUEST_WALL, &g));
    CHECK(!fax_quest_done(FAX_QUEST_MASCON, &g));

    g = new_game();
    g.quests = 0x08; /* Zorugeriru's boots */
    CHECK(fax_quest_done(FAX_QUEST_BOOTS, &g));
    g = new_game();
    give(&g, ITEMS, 0x0F); /* a pair from Forepaw's shop */
    CHECK(fax_quest_done(FAX_QUEST_BOOTS, &g));

    g = new_game();
    g.quests = 0x20; /* the rock pushed */
    CHECK(fax_quest_done(FAX_QUEST_MASCON, &g));
    g = new_game();
    g.reached = FAX_REACHED_MIST;
    CHECK(fax_quest_done(FAX_QUEST_MASCON, &g) && !fax_quest_done(FAX_QUEST_POISON, &g));

    g = new_game();
    g.special = 0x01; /* the Black Onyx: the poison found */
    CHECK(fax_quest_done(FAX_QUEST_POISON, &g) && !fax_quest_done(FAX_QUEST_METEORITE, &g));
    g.selected[ITEMS] = 0x04; /* the Ace key: the meteorite brought back */
    CHECK(fax_quest_done(FAX_QUEST_METEORITE, &g));

    g = new_game();
    give(&g, ARMOR, 0x03); /* the Battle Suit alone */
    CHECK(!fax_quest_done(FAX_QUEST_GEAR, &g));
    g.selected[SHIELDS] = 0x03; /* and the Battle Helmet */
    CHECK(fax_quest_done(FAX_QUEST_GEAR, &g) && !fax_quest_done(FAX_QUEST_DWORF, &g));
    g.special = 0x20;
    CHECK(fax_quest_done(FAX_QUEST_DWORF, &g));

    g = new_game();
    g.special = 0x04;
    CHECK(fax_quest_done_all(&g) == BIT(FAX_QUEST_CANE));

    g = new_game();
    g.selected[WEAPONS] = 0x03; /* the Dragon Slayer in hand */
    CHECK(fax_quest_done(FAX_QUEST_SLAYER, &g) && !fax_quest_done(FAX_QUEST_DEMON, &g));
    g = new_game();
    g.special = 0x10; /* the Demon's Ring */
    CHECK(fax_quest_done(FAX_QUEST_DEMON, &g) && fax_quest_done(FAX_QUEST_SLAYER, &g) &&
          !fax_quest_done(FAX_QUEST_EVIL, &g));
    g.reached = FAX_REACHED_ENDING;
    CHECK(fax_quest_done(FAX_QUEST_EVIL, &g));
}

/* ---- the scripts (with the ROM) ---- */

enum { SCRIPTS = 0x98 };
static const uint8_t params[24] = { 0, 1, 1, 1, 2, 2, 1, 1, 2, 2, 1, 3, 3, 2, 1, 2, 1, 2, 3, 1, 0, 0, 3, 2 };
static uint8_t bank12[16384];

static unsigned script_addr(unsigned s) { return bank12[0x9F6B + s - 0x8000] | bank12[0xA003 + s - 0x8000] << 8; }

/* Every message any path through a script can show, as a set. */
static void all_messages(unsigned script, uint8_t seen[256]) {
    static uint8_t visited[16384];
    memset(visited, 0, sizeof visited);
    unsigned todo[64], n = 0;
    todo[n++] = script_addr(script) + 1;
    while (n) {
        unsigned pc = todo[--n];
        while (pc >= 0x8000 && pc < 0xC000 && !visited[pc - 0x8000]) {
            visited[pc - 0x8000] = 1;
            unsigned op = bank12[pc - 0x8000];
            if (op >= 24) break;
            const uint8_t *p = &bank12[pc + 1 - 0x8000];
            if (op == 1 || op == 2 || op == 3 || op == 0x16) seen[p[0]] = 1;
            unsigned target = 0;
            if (op == 4 || op == 0x0D || op == 0x0F || op == 0x17) target = p[0] | p[1] << 8;
            if (op == 0x0B || op == 0x0C || op == 0x12 || op == 0x16) target = p[1] | p[2] << 8;
            if (target && n < 64) todo[n++] = target;
            if (op == 0 || op == 0x15 || op == 0x17) break;
            pc += 1 + params[op];
        }
    }
}

static void lines_in_their_scripts(void) {
    static uint8_t in[SCRIPTS][256];
    for (unsigned s = 0; s < SCRIPTS; ++s) all_messages(s, in[s]);
    /* The scripts some entries share (the intro, 1E and others) count once, where the table puts them. */
    for (unsigned q = 0; q < FAX_QUESTS; ++q) {
        const FaxQuest *f = &fax_quests[q];
        for (unsigned i = 0; i <= FAX_QUEST_GIVERS; ++i) {
            uint8_t msg = i < FAX_QUEST_GIVERS ? f->give[i] : f->step;
            uint8_t script = i < FAX_QUEST_GIVERS ? f->giver[i] : f->step_script;
            if (!msg) continue;
            CHECK(in[script][msg]);
            for (unsigned s = 0; s < SCRIPTS; ++s)
                if (s != script && script_addr(s) != script_addr(script) && in[s][msg]) {
                    fprintf(stderr, "message $%02X (%s) is in script $%02X as well as $%02X\n", msg, f->id, s, script);
                    CHECK(0);
                }
        }
    }
}

static unsigned walk(unsigned script, const FaxQuestGame *g, uint8_t path[FAX_QUEST_PATH]) {
    return fax_quest_walk(bank12, script, g, path);
}
static bool path_is(unsigned script, const FaxQuestGame *g, const uint8_t *want, unsigned n) {
    uint8_t path[FAX_QUEST_PATH];
    unsigned got = walk(script, g, path);
    if (got == n && !memcmp(path, want, n)) return true;
    fprintf(stderr, "script $%02X shows", script);
    for (unsigned i = 0; i < got; ++i) fprintf(stderr, " $%02X", path[i]);
    fprintf(stderr, "\n");
    return false;
}
static char mark(unsigned script, const FaxQuestGame *g, uint32_t given, uint32_t done) {
    uint8_t path[FAX_QUEST_PATH];
    unsigned n = walk(script, g, path);
    return fax_quest_mark(path, n, given, done | fax_quest_done_all(g));
}

static void walks_and_marks(void) {
    FaxQuestGame g = new_game();
    /* The King: his plea with no gold, "nothing more" with some; a "!" until it is given. */
    CHECK(path_is(0x08, &g, (const uint8_t[]){ 0x35 }, 1));
    CHECK(mark(0x08, &g, 0, 0) == '!');
    CHECK(mark(0x08, &g, BIT(FAX_QUEST_KING), 0) == 0);
    g.gold = 1500;
    CHECK(path_is(0x08, &g, (const uint8_t[]){ 0x36 }, 1));
    CHECK(mark(0x08, &g, 0, 0) == '!');
    CHECK(mark(0x08, &g, 0, BIT(FAX_QUEST_KING)) == 0);

    /* Eolis: the woman sends him to the Guru; the Guru is the next step until the ring is his. */
    g = new_game();
    CHECK(path_is(0x03, &g, (const uint8_t[]){ 0x2A }, 1) && mark(0x03, &g, 0, 0) == '!');
    CHECK(mark(0x03, &g, BIT(FAX_QUEST_ELF), 0) == 0);
    CHECK(path_is(0x04, &g, (const uint8_t[]){ 0x2C }, 1));
    CHECK(mark(0x04, &g, 0, 0) == 0 && mark(0x04, &g, BIT(FAX_QUEST_ELF), 0) == '?');
    g.special = 0x80;
    CHECK(path_is(0x03, &g, (const uint8_t[]){ 0x2B }, 1) && mark(0x03, &g, 0, 0) == 0);
    CHECK(path_is(0x04, &g, (const uint8_t[]){ 0x2D }, 1) && mark(0x04, &g, BIT(FAX_QUEST_ELF), 0) == 0);

    /* The walking man and the guard point at the King, and are chatter. */
    CHECK(mark(0x01, &g, 0, 0) == 0 && mark(0x07, &g, 0, 0) == 0);

    /* The spring of Trunk: the question, then the medicine or not. */
    g = new_game();
    CHECK(path_is(0x26, &g, (const uint8_t[]){ 0x59, 0x5B }, 2));
    CHECK(mark(0x26, &g, 0, 0) == '!' && mark(0x26, &g, BIT(FAX_QUEST_TRUNK), 0) == 0);
    g.special = 0x08;
    CHECK(path_is(0x26, &g, (const uint8_t[]){ 0x59, 0x5A }, 2) && mark(0x26, &g, BIT(FAX_QUEST_TRUNK), 0) == '?');
    g.quests = 0x01; /* revived: he sends the hero to Mascon */
    CHECK(path_is(0x26, &g, (const uint8_t[]){ 0x5C }, 1) && mark(0x26, &g, BIT(FAX_QUEST_TRUNK), 0) == '!');

    /* Forepaw's nurse, by the springs revived. */
    g = new_game();
    CHECK(path_is(0x23, &g, (const uint8_t[]){ 0x50 }, 1));
    g.quests = 0x01;
    CHECK(path_is(0x23, &g, (const uint8_t[]){ 0x51 }, 1));
    g.quests = 0x03;
    CHECK(path_is(0x23, &g, (const uint8_t[]){ 0x52 }, 1));

    /* The man on Mist $1F: the meteorite, then the Ace key, then chatter. */
    g = new_game();
    CHECK(path_is(0x46, &g, (const uint8_t[]){ 0x7D }, 1) && mark(0x46, &g, 0, 0) == '!');
    CHECK(mark(0x46, &g, BIT(FAX_QUEST_METEORITE), 0) == 0);
    g.special = 0x01;
    CHECK(path_is(0x46, &g, (const uint8_t[]){ 0x7E }, 1) && mark(0x46, &g, BIT(FAX_QUEST_METEORITE), 0) == '?');
    CHECK(mark(0x46, &g, 0, 0) == 0); /* never given: no step to come back to */
    give(&g, ITEMS, 0x04);
    CHECK(path_is(0x46, &g, (const uint8_t[]){ 0x7F }, 1) && mark(0x46, &g, BIT(FAX_QUEST_METEORITE), 0) == 0);

    /* The Joker spring's old man revives it, then gives the Ring of Ruby. */
    g = new_game();
    CHECK(path_is(0x27, &g, (const uint8_t[]){ 0x5D }, 1) && mark(0x27, &g, BIT(FAX_QUEST_FIRST), 0) == '?');
    g.quests = 0x07;
    CHECK(path_is(0x27, &g, (const uint8_t[]){ 0x5E }, 1) && mark(0x27, &g, BIT(FAX_QUEST_FIRST), 0) == '!');

    /* A temple Guru: the mantra, or a new title; never a quest. */
    g = new_game();
    CHECK(path_is(0x1C, &g, (const uint8_t[]){ 0x22 }, 1));
    g.next_title = 1;
    CHECK(path_is(0x1C, &g, (const uint8_t[]){ 0x23 }, 1) && mark(0x1C, &g, 0, 0) == 0);

    /* Conflate's Guru gives the Ring of Dworf only for the Battle Suit. */
    g = new_game();
    CHECK(mark(0x55, &g, BIT(FAX_QUEST_DWORF), 0) == 0);
    g.selected[ARMOR] = 0x03;
    CHECK(path_is(0x55, &g, (const uint8_t[]){ 0x8C }, 1) && mark(0x55, &g, BIT(FAX_QUEST_DWORF), 0) == '?');

    /* A shop: its greeting and the buy list's thanks; no marks. */
    g = new_game();
    CHECK(path_is(0x0D, &g, (const uint8_t[]){ 0x0F, 0x02 }, 2) && mark(0x0D, &g, 0, 0) == 0);
    CHECK(walk(0x98, &g, (uint8_t[FAX_QUEST_PATH]){ 0 }) == 0); /* past the table */
}

int main(int argc, char **argv) {
    table_fits();
    done_from_state();
    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb");
        static uint8_t rom[16 + 16 * 16384];
        bool ok = f && fread(rom, 1, sizeof rom, f) == sizeof rom;
        if (f) fclose(f);
        if (!ok) {
            fprintf(stderr, "cannot read the ROM %s\n", argv[1]);
            return 2;
        }
        memcpy(bank12, rom + 16 + 12 * 16384, sizeof bank12);
        lines_in_their_scripts();
        walks_and_marks();
    } else {
        printf("quests: no ROM given; the checks against the scripts are skipped\n");
    }
    printf("quests: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
