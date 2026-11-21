/* The quest log's quests. USA 57dd23d1, chipx86 byte-exact disassembly.
 * Drafted from every interaction script and the messages they show: each
 * quest is a request someone makes, with the lines that make it (message IDs,
 * each in one script only), the person who is its next step, its objective in
 * the game's 16 columns, and when it is complete, read from Quests ($042D),
 * SpecialItems ($042C), the inventory and the places reached. Everything else
 * the game's people say is chatter. See docs/GAME_INTERNALS.md, "Quests",
 * for the lines left out and why. */
#include "fax_quest_table.h"
#include <stdio.h>
#include <string.h>

/* Quests ($042D), as ISCRIPTS_QUEST_BITS and the pickups set them */
enum { Q_SPRING_OF_TRUNK = 0x01, Q_SPRING_OF_SKY = 0x02, Q_SPRING_OF_JOKER = 0x04, Q_ALL_SPRINGS = 0x07,
       Q_WING_BOOTS_FROM_TOWER = 0x08, Q_MATTOCK = 0x10, Q_MASCON_OPENED = 0x20 };
/* SpecialItems ($042C), SPECIAL_ITEM_BITMASKS ($9AE5, bank 12) */
enum { S_RING_OF_ELF = 0x80, S_RING_OF_RUBY = 0x40, S_RING_OF_DWORF = 0x20, S_DEMONS_RING = 0x10, S_ELIXIR = 0x08,
       S_MAGICAL_ROD = 0x04, S_PENDANT = 0x02, S_BLACK_ONYX = 0x01 };
/* Full item IDs (InventoryItem in chipx86's DEFS.asm) */
enum { WEAPON_DRAGON_SLAYER = 0x03, ARMOR_BATTLE_SUIT = 0x23, SHIELD_BATTLE_HELMET = 0x43, KEY_A = 0x84,
       MATTOCK = 0x89, ITEM_WING_BOOTS = 0x8F };

/* Who says what (bank 11's sprite lists, AREA_SPRITE_ADDRESSES, indexed by
 * area; a building's door from bank 3's door tables): script $03 is the woman
 * walking on Eolis screen 2, $04 Eolis's Guru, $08 the King (building $07),
 * $12 and $13 the two men in Apolune's building $0A, $21 the man walking on
 * Forepaw's screen 3, $22 the man in Forepaw's house ($12), $23 the nurse in
 * its hospital ($11), $24 the Guri (building $15, off Trunk $2D), $25-$27 the
 * old men of the springs (Trunk $25, $3D, $3F), $33 the man in Mascon's house
 * ($1A), $36 the woman in the house on Mist $16 ($21), $42 the woman in
 * Victim's house ($25), $46 the man in the house on Mist $1F ($22), $50 the
 * man walking on Conflate's screen 9, $51 the man in Conflate's house ($29),
 * $55 Conflate's Guru ($2B), $61 the man in Daybreak's house ($34), $63 the
 * nurse in its meat shop ($31), $64 and $65 the man and the woman in its
 * tavern ($32), $71-$72 the man and woman in the house on Dartmoor $08 ($3A),
 * $73 the man in Dartmoor town's building $3D, $74 the Guru in the Castle of
 * Fraternal ($42). */
const FaxQuest fax_quests[FAX_QUESTS] = {
    [FAX_QUEST_ELF] = { "elf", "The Ring of Elf", "See the Guru of\nEolis before you\nsee the King.",
                        { 0x2A }, { 0x03 }, 0x2C, 0x04 },
    [FAX_QUEST_KING] = { "king", "The King's Plea", "Bring the water\nback to the Elf\nfountain, our\nlife source.",
                         { 0x35, 0x36, 0x4C }, { 0x08, 0x08, 0x21 }, 0, 0 },
    [FAX_QUEST_MATTOCK] = { "mattock", "The Mattock", "Defeat the Dwarf\nin the Tower of\nTrunk and take\nhis Mattock.",
                            { 0x40 }, { 0x12 }, 0, 0 },
    [FAX_QUEST_WALL] = { "wall", "The Wall", "Break the wall\nwith a Mattock\nand go on to the\nnext town.",
                         { 0x41, 0x42 }, { 0x13, 0x13 }, 0, 0 },
    [FAX_QUEST_TRUNK] = { "trunk", "Spring of Trunk", "Bring Elixir to\nthe spring in\nthe Tower of\nFortress.",
                          { 0x50, 0x59 }, { 0x23, 0x26 }, 0x5A, 0x26 },
    [FAX_QUEST_BOOTS] = { "boots", "The Wing Boots", "Defeat the Dwarf\nwho lives in the\ntower for his\nWing Boots.",
                          { 0x51 }, { 0x23 }, 0, 0 },
    [FAX_QUEST_SKY] = { "sky", "Spring of Sky", "Fly up with Wing\nBoots to the\nfountain in the\nsky.",
                        { 0x4E, 0x51, 0x54 }, { 0x22, 0x23, 0x24 }, 0x57, 0x25 },
    [FAX_QUEST_FIRST] = { "first", "The First Spring", "Take the Joker\nkey from the\nGuri and find\nthe first spring",
                          { 0x55, 0x58 }, { 0x24, 0x25 }, 0x5D, 0x27 },
    [FAX_QUEST_MASCON] = { "mascon", "Path to Mascon", "Push the rock\naway from the\nfountain with\nthe Ring of Ruby",
                           { 0x4F, 0x52, 0x5E }, { 0x22, 0x23, 0x27 }, 0, 0 },
    [FAX_QUEST_POISON] = { "poison", "The Poison", "Go to Mascon and\nfind the poison\nthe Dwarfs hide.",
                           { 0x5C, 0x66, 0x6D }, { 0x26, 0x33, 0x36 }, 0, 0 },
    [FAX_QUEST_METEORITE] = { "meteorite", "The Meteorite",
                              "Defeat the Dwarf\nin the Tower of\nMist, then bring\nthe meteorite.",
                              { 0x7D }, { 0x46 }, 0x7E, 0x46 },
    [FAX_QUEST_GEAR] = { "gear", "Battle Gear", "Find the Battle\nSuit and Helmet\nin the World of\nBranch.",
                         { 0x81, 0x8F, 0x95 }, { 0x50, 0x61, 0x64 }, 0, 0 },
    [FAX_QUEST_DWORF] = { "dworf", "Ring of Dworf", "Bring the Battle\nSuit to the Guru\nof Conflate.",
                          { 0x83, 0x94 }, { 0x51, 0x63 }, 0x8C, 0x55 },
    [FAX_QUEST_CANE] = { "cane", "The Magic Cane", "Find the magic\ncane above the\ntower of Pillar.",
                         { 0x73, 0x90, 0x98 }, { 0x42, 0x61, 0x65 }, 0, 0 },
    [FAX_QUEST_SLAYER] = { "slayer", "Dragon Slayer", "Take the Dragon\nSlayer from the\nking of the\nDwarfs.",
                           { 0x9B }, { 0x71 }, 0, 0 },
    [FAX_QUEST_DEMON] = { "demon", "The Demon's Ring", "Go to the Guru\nin the Castle of\nFraternal with\nDragon Slayer.",
                          { 0x9D, 0x9E, 0x9F }, { 0x72, 0x72, 0x73 }, 0xA2, 0x74 },
    [FAX_QUEST_EVIL] = { "evil", "The Evil One", "Defeat the Evil\nOne in Zenis\nwith the Dragon\nSlayer.",
                         { 0x9C, 0xA2 }, { 0x71, 0x74 }, 0, 0 },
};

/* ---- the inventory, as Player_LacksItem reads it ---- */

static const uint8_t special_item_id[8] = { 0x80, 0x81, 0x82, 0x83, 0x92, 0x8A, 0x93, 0x94 };
static const uint8_t special_item_bit[8] = { S_RING_OF_ELF, S_RING_OF_RUBY, S_RING_OF_DWORF, S_DEMONS_RING,
                                             S_ELIXIR, S_MAGICAL_ROD, S_PENDANT, S_BLACK_ONYX };

bool fax_quest_has(const FaxQuestGame *g, uint8_t item) {
    for (unsigned i = 0; i < 8; ++i)
        if (item == special_item_id[i]) return (g->special & special_item_bit[i]) != 0;
    unsigned cat = item >> 5, id = item & 0x1F;
    if (cat >= FAX_QUEST_LISTS) return false;
    if (g->selected[cat] == id) return true;
    unsigned count = g->count[cat] > 8 ? 8 : g->count[cat];
    for (unsigned i = 0; i < count; ++i) if (g->list[cat][i] == id) return true;
    return false;
}

/* ---- complete ---- */

bool fax_quest_done(unsigned quest, const FaxQuestGame *g) {
    switch (quest) {
    case FAX_QUEST_ELF: return g->special & S_RING_OF_ELF;
    case FAX_QUEST_KING: return (g->quests & Q_ALL_SPRINGS) == Q_ALL_SPRINGS;
    case FAX_QUEST_MATTOCK:
        return fax_quest_has(g, MATTOCK) || (g->quests & Q_MATTOCK) || g->reached >= FAX_REACHED_FOREPAW;
    case FAX_QUEST_WALL: return g->reached >= FAX_REACHED_FOREPAW;
    case FAX_QUEST_TRUNK: return g->quests & Q_SPRING_OF_TRUNK;
    case FAX_QUEST_BOOTS:
        return (g->quests & (Q_WING_BOOTS_FROM_TOWER | Q_SPRING_OF_SKY)) || fax_quest_has(g, ITEM_WING_BOOTS);
    case FAX_QUEST_SKY: return g->quests & Q_SPRING_OF_SKY;
    case FAX_QUEST_FIRST: return g->quests & Q_SPRING_OF_JOKER;
    case FAX_QUEST_MASCON: return (g->quests & Q_MASCON_OPENED) || g->reached >= FAX_REACHED_MIST;
    case FAX_QUEST_POISON: return (g->special & S_BLACK_ONYX) || g->reached >= FAX_REACHED_BRANCH;
    case FAX_QUEST_METEORITE: return fax_quest_has(g, KEY_A) || g->reached >= FAX_REACHED_BRANCH;
    case FAX_QUEST_GEAR: return fax_quest_has(g, ARMOR_BATTLE_SUIT) && fax_quest_has(g, SHIELD_BATTLE_HELMET);
    case FAX_QUEST_DWORF: return g->special & S_RING_OF_DWORF;
    case FAX_QUEST_CANE: return g->special & S_MAGICAL_ROD;
    case FAX_QUEST_SLAYER:
        return fax_quest_has(g, WEAPON_DRAGON_SLAYER) || (g->special & S_DEMONS_RING) ||
               g->reached >= FAX_REACHED_ZENIS;
    case FAX_QUEST_DEMON: return g->special & S_DEMONS_RING;
    case FAX_QUEST_EVIL: return g->reached >= FAX_REACHED_ENDING;
    }
    return false;
}

uint32_t fax_quest_done_all(const FaxQuestGame *g) {
    uint32_t done = 0;
    for (unsigned q = 0; q < FAX_QUESTS; ++q) if (fax_quest_done(q, g)) done |= 1u << q;
    return done;
}

uint32_t fax_quest_given_by(uint8_t msg) {
    uint32_t quests = 0;
    if (!msg) return 0;
    for (unsigned q = 0; q < FAX_QUESTS; ++q)
        for (unsigned i = 0; i < FAX_QUEST_GIVERS && fax_quests[q].give[i]; ++i)
            if (fax_quests[q].give[i] == msg) quests |= 1u << q;
    return quests;
}

bool fax_quest_progress(unsigned quest, const FaxQuestGame *g, char out[FAX_QUEST_COLS + 1]) {
    if (quest != FAX_QUEST_KING) return false;
    unsigned n = !!(g->quests & Q_SPRING_OF_TRUNK) + !!(g->quests & Q_SPRING_OF_SKY) + !!(g->quests & Q_SPRING_OF_JOKER);
    snprintf(out, FAX_QUEST_COLS + 1, "%u of 3 springs", n);
    return true;
}

/* ---- walking a script, as IScripts_InvokeNextAction would ---- */

#define ISCRIPT_ADDRS_L 0x9F6Bu   /* bank 12: the scripts' addresses, low bytes, then */
#define ISCRIPT_ADDRS_U 0xA003u   /* their high bytes */
enum { SCRIPTS = 0x98 };
/* IScript actions (IScriptAction_* in bank 12) */
enum {
    END = 0x00, SAY_UNSKIPPABLE = 0x01, ASK = 0x02, SAY = 0x03, IF_NEW_TITLE = 0x04, PAY_GOLD = 0x05,
    SET_SPAWN_POINT = 0x06, ADD_ITEM = 0x07, OPEN_SHOP = 0x08, ADD_GOLD = 0x09, ADD_MP = 0x0A, IF_QUEST = 0x0B,
    IF_TITLE = 0x0C, IF_GOLD = 0x0D, SET_QUEST = 0x0E, BUY_SELL_MENU = 0x0F, CONSUME_ITEM = 0x10, SELL_MENU = 0x11,
    IF_ITEM = 0x12, ADD_HP = 0x13, SHOW_MANTRA = 0x14, END_GAME = 0x15, ASK_JUMP = 0x16, JUMP = 0x17, ACTIONS
};
/* Each action's parameter bytes. */
static const uint8_t params[ACTIONS] = { 0, 1, 1, 1, 2, 2, 1, 1, 2, 2, 1, 3, 3, 2, 1, 2, 1, 2, 3, 1, 0, 0, 3, 2 };
static const uint8_t quest_bits[3] = { Q_SPRING_OF_TRUNK, Q_SPRING_OF_SKY, Q_SPRING_OF_JOKER };   /* ISCRIPTS_QUEST_BITS */

static int at(const uint8_t *bank, unsigned addr) {
    return addr >= 0x8000u && addr < 0xC000u ? bank[addr - 0x8000u] : -1;
}

unsigned fax_quest_walk(const uint8_t *bank12, unsigned script, const FaxQuestGame *g,
                        uint8_t out[FAX_QUEST_PATH]) {
    if (!bank12 || script >= SCRIPTS) return 0;
    unsigned pc = (unsigned)at(bank12, ISCRIPT_ADDRS_L + script) | (unsigned)at(bank12, ISCRIPT_ADDRS_U + script) << 8;
    if (at(bank12, pc) < 0) return 0;
    ++pc;                                                /* past the portrait byte */
    unsigned n = 0;
    for (unsigned steps = 0; steps < 64; ++steps) {
        int op = at(bank12, pc);
        if (op < 0 || op >= ACTIONS) return n;
        int p[3] = { 0, 0, 0 };
        for (unsigned i = 0; i < params[op]; ++i) if ((p[i] = at(bank12, pc + 1 + i)) < 0) return n;
        unsigned next = pc + 1 + params[op], jump = 0;
        bool taken = false;
        switch (op) {
        case END: case END_GAME: return n;
        case SAY_UNSKIPPABLE: case ASK: case SAY: case ASK_JUMP:
            if (n < FAX_QUEST_PATH && p[0]) out[n++] = (uint8_t)p[0];
            if (op == ASK_JUMP) { jump = (unsigned)(p[1] | p[2] << 8); taken = true; }   /* A on the question */
            break;
        case IF_NEW_TITLE: jump = (unsigned)(p[0] | p[1] << 8); taken = g->next_title != g->title; break;
        case IF_GOLD: jump = (unsigned)(p[0] | p[1] << 8); taken = g->gold != 0; break;
        case IF_QUEST:
            jump = (unsigned)(p[1] | p[2] << 8);
            taken = p[0] >= 1 && p[0] <= 3 && (g->quests & quest_bits[p[0] - 1]);
            break;
        case IF_TITLE: jump = (unsigned)(p[1] | p[2] << 8); taken = (unsigned)p[0] <= g->title; break;
        case IF_ITEM: jump = (unsigned)(p[1] | p[2] << 8); taken = fax_quest_has(g, (uint8_t)p[0]); break;
        case BUY_SELL_MENU: jump = (unsigned)(p[0] | p[1] << 8); taken = true; break;   /* A: buy */
        case JUMP: jump = (unsigned)(p[0] | p[1] << 8); taken = true; break;
        default: break;   /* gold, items, quests, shops, the mantra: on to the next action */
        }
        pc = taken ? jump : next;
    }
    return n;
}

char fax_quest_mark(const uint8_t *path, unsigned n, uint32_t given, uint32_t done) {
    char mark = 0;
    for (unsigned q = 0; q < FAX_QUESTS; ++q) {
        uint32_t bit = 1u << q;
        if (done & bit) continue;
        const FaxQuest *quest = &fax_quests[q];
        for (unsigned i = 0; i < n; ++i) {
            if ((given & bit) && quest->step && path[i] == quest->step) return '?';
            if (!(given & bit))
                for (unsigned k = 0; k < FAX_QUEST_GIVERS && quest->give[k]; ++k)
                    if (path[i] == quest->give[k]) mark = '!';
        }
    }
    return mark;
}
