#pragma once
/* The quest log's quests (fax_quest_table.c): a curated list, in the game's
 * voice, of the requests the game's people make. Each quest names the lines
 * that give it (message IDs, each in one interaction script), the line of the
 * person who is its next step, if any, its objective in the text box's 16
 * columns, and when it is complete, read from the game's flags, items and
 * places, never guessed. Plain data and pure functions over a snapshot of the
 * game, so the unit tests run them without the emulator.
 * See docs/GAME_INTERNALS.md, "Quests". */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    FAX_QUEST_ELF, FAX_QUEST_KING, FAX_QUEST_MATTOCK, FAX_QUEST_WALL, FAX_QUEST_TRUNK, FAX_QUEST_BOOTS,
    FAX_QUEST_SKY, FAX_QUEST_FIRST, FAX_QUEST_MASCON, FAX_QUEST_POISON, FAX_QUEST_METEORITE, FAX_QUEST_GEAR,
    FAX_QUEST_DWORF, FAX_QUEST_CANE, FAX_QUEST_SLAYER, FAX_QUEST_DEMON, FAX_QUEST_EVIL,
    FAX_QUESTS
};
enum {
    FAX_QUEST_COLS = 16,     /* the text box's columns: a name or an objective line */
    FAX_QUEST_LINES = 4,     /* objective lines, at most */
    FAX_QUEST_GIVERS = 4,    /* lines that give one quest, at most */
    FAX_QUEST_PATH = 12,     /* messages one walk through a script collects, at most */
};

/* How far the hero has got, from the places he has been to (the map, the
 * zone he is in) and the furthest temple. Each implies the ones before. */
enum {
    FAX_REACHED_EOLIS, FAX_REACHED_FOREPAW, FAX_REACHED_MIST, FAX_REACHED_BRANCH, FAX_REACHED_DARTMOOR,
    FAX_REACHED_ZENIS, FAX_REACHED_ENDING
};

/* What the game knows, as RAM holds it. */
enum { FAX_QUEST_WEAPONS, FAX_QUEST_ARMOR, FAX_QUEST_SHIELDS, FAX_QUEST_MAGIC, FAX_QUEST_ITEMS, FAX_QUEST_LISTS };
typedef struct {
    uint8_t quests;                          /* Quests ($042D) */
    uint8_t special;                         /* SpecialItems ($042C) */
    uint8_t title, next_title;               /* PlayerTitle ($0437), NextPlayerTitle ($04ED) */
    uint32_t gold;                           /* Gold ($0392-$0394) */
    uint8_t count[FAX_QUEST_LISTS];          /* NumberOfWeapons ($03C2) ... NumberOfItems ($03C6) */
    uint8_t list[FAX_QUEST_LISTS][8];        /* WeaponInventory ($039D) ... ItemInventory ($03AD) */
    uint8_t selected[FAX_QUEST_LISTS];       /* SelectedWeapon ($03BD) ... SelectedItem ($03C1), $FF none */
    uint8_t reached;                         /* FAX_REACHED_* */
} FaxQuestGame;

typedef struct {
    const char *id;                          /* its name in the save file's fields: [a-z] */
    const char *name;                        /* at most 16 characters */
    const char *objective;                   /* up to 4 lines of 16, '\n' between them */
    uint8_t give[FAX_QUEST_GIVERS];          /* messages that give it, 0 after the last */
    uint8_t giver[FAX_QUEST_GIVERS];         /* the script each is in (ISCRIPT_ADDRS index) */
    uint8_t step, step_script;               /* the next step's message and script, or 0 */
} FaxQuest;

extern const FaxQuest fax_quests[FAX_QUESTS];

/* The quest is complete in this game. */
bool fax_quest_done(unsigned quest, const FaxQuestGame *g);
/* All of them, a bit per quest. */
uint32_t fax_quest_done_all(const FaxQuestGame *g);
/* The quests message `msg` gives, a bit per quest (0 for chatter). */
uint32_t fax_quest_given_by(uint8_t msg);
/* A line of the King's Plea's progress ("Springs: 2 of 3"), or false for
 * any other quest. */
bool fax_quest_progress(unsigned quest, const FaxQuestGame *g, char out[FAX_QUEST_COLS + 1]);

/* The hero has `item` (a full item ID: category in the top 3 bits, as the
 * scripts and shops write it), as Player_LacksItem ($8CF7, bank 12) asks:
 * special items by their SpecialItems bit, the rest selected or listed. */
bool fax_quest_has(const FaxQuestGame *g, uint8_t item);

/* The messages script `script` would show if the hero talked now, as the
 * interpreter would walk it: its branches on items, gold, quests and titles
 * taken as the game would take them, and every question answered with A.
 * `bank12` is the 16 KiB of bank 12 ($8000-$BFFF). Returns how many, up to
 * FAX_QUEST_PATH; 0 for a script number out of range or a script it cannot
 * read. */
unsigned fax_quest_walk(const uint8_t *bank12, unsigned script, const FaxQuestGame *g,
                        uint8_t out[FAX_QUEST_PATH]);

/* The mark over a person whose script shows `path`: '!' for a quest he gives
 * that is not given or complete, '?' if he is the next step of one given and
 * not complete ('?' first), else 0. `given` and `done` are bits per quest. */
char fax_quest_mark(const uint8_t *path, unsigned n, uint32_t given, uint32_t done);
