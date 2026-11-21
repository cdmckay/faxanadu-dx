/* The quest log. USA 57dd23d1, chipx86 byte-exact disassembly.
 *
 * The quests are fax_quest_table.c's: requests the game's people make, each
 * given by lines (message IDs) and complete by the game's own state.
 *
 * In dialogue. A hook at Messages_Load ($F3F5) sees every message as it
 * loads: a line that gives a quest not yet given (nor complete) gives it. At
 * IScriptAction_EndScript ($82B4), which every way out of a script takes,
 * after Block's hook there, the quests the script completed and those it gave
 * are said in the same box, one line each ("Quest complete" or "New quest",
 * then the name), through Guru travel's SAY step and message $C3's slot, as
 * Block's tutorial does: the script is pointed at the step and returns through
 * IScripts_InvokeNextAction, so its END comes back here, and the box closes
 * after the last. While a line is up, the hook at $F439, after Save files',
 * keeps the box's line scroll, so a line after one that scrolled (the King's)
 * is not shown rotated. An item a pickup's script announces ("I've got the
 * Black Onyx") is added only after the script, so the message says which
 * item is coming. A quest that completes outside a conversation (a place
 * reached, the rock pushed) says so below the HUD, in the zone banner's box,
 * once any zone name has gone.
 *
 * The marks. Over each person on screen (a sprite of the NPC category, whose
 * CurrentSprites_Values is his script) the script is walked as the game would
 * walk it now: a quest one of its lines gives, not given and not complete,
 * puts a "!" over him; a quest under way whose next step he is, a "?". Drawn
 * at present time, in both views: in Seamless at each room's place, the
 * neighbouring rooms' people from their copies (fax_world_room_ram).
 *
 * The log. A Quest log button (L, or the right stick; QuestLogKey and
 * QuestLogButton in config.ini [Game]) opens it through the game's own pause,
 * as the map does, at the same two branches of GameLoop_CheckPauseGame
 * ($E02F, $E040), after the map's hooks: the active quests with their
 * objectives, then the completed ones, in pages; Left and Right turn them, B,
 * Start or the button close it.
 *
 * Kept. The quests given, and those seen complete, are fields of the file in
 * play (quests.given, quests.done); everything else is read from the game. A
 * file without them (a mantra's) counts what it has done as given.
 * See docs/GAME_INTERNALS.md, "Quests". */
#include "fax_quests.h"
#include "fax_quest_table.h"
#include "fax_banner.h"
#include "fax_block.h"
#include "fax_map.h"
#include "fax_save.h"
#include "fax_text.h"
#include "fax_travel.h"
#include "fax_world.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#ifdef CYC_WITH_SDL
#include "cyc_input.h"
#include "cyc_tcp.h"
#include <SDL.h>
#endif
#include <stdlib.h>
#include <string.h>

#ifndef FAX_QUESTS_LOG
#define FAX_QUESTS_LOG(...) fprintf(stderr, __VA_ARGS__)
#endif

extern uint32_t hw_palette_argb[512];   /* the host's NES colors, emphasis in bits 6-8 */

/* RAM */
#define SCREEN_READY_STATE      0x000Eu   /* 0 once a screen is set up */
#define INTERRUPT_HANDLERS_ON   0x0013u   /* Game_InterruptHandlersEnabled */
#define JOY1_CHANGED            0x0019u   /* newly pressed: B $40, Start $10, Left $02, Right $01 */
#define AREA_CURRENT_AREA       0x0024u
#define SCREEN_SCROLL_DIRECTION 0x0054u   /* $FF unless scrolling to the next screen */
#define AREA_CURRENT_SCREEN     0x0063u
#define CURRENT_SPRITES_XPOS    0x00BAu   /* CurrentSprites_XPos_Full: a sprite's box's left, 8 slots */
#define CURRENT_SPRITES_YPOS    0x00C2u   /* its top, under the HUD */
#define ISCRIPT_ADDR            0x00DBu   /* IScriptOrCHRAddr; the next action is at it + IScriptOffset */
#define ISCRIPT_OFFSET          0x00DDu
#define CURRENT_ROM_BANK        0x0100u   /* 14 in the main loop, 12 while a script or the menu runs */
enum { MAIN_LOOP_BANK = 14 };
#define GAME_PAUSED_STATE       0x0120u
#define TEXTBOX_CLOSED          0x020Cu
#define TEXTBOX_LINE_SCROLL     0x021Au   /* TextBox_LineScrollOffset */
#define CURRENT_SPRITES_ENTITIES 0x02CCu  /* $FF: an empty slot */
#define CURRENT_SPRITES_VALUES  0x036Cu   /* an NPC's script */
#define GOLD                    0x0392u   /* 24 bits */
#define INVENTORY_LISTS         0x039Du   /* weapons, armor, shields, magic (4 each), items (8) */
#define INVENTORY_SELECTED      0x03BDu   /* SelectedWeapon ... SelectedItem */
#define INVENTORY_COUNTS        0x03C2u   /* NumberOfWeapons ... NumberOfItems */
#define SPECIAL_ITEMS           0x042Cu
#define QUESTS                  0x042Du
#define PLAYER_TITLE            0x0437u
#define PLAYER_IS_DEAD          0x0438u
#define TEMPLE_SPAWN_POINT      0x0439u
#define NEXT_PLAYER_TITLE       0x04EDu
/* Bank 15 */
#define MESSAGES_LOAD           0xF3F5u   /* STA MessageID: A is the message */
#define CLEAR_SCROLLED          0xF439u   /* TextBox_ClearPasswordSize, after STA TextBox_LineScrollOffset */
#define PAUSE_CHECK             0xE02Fu   /* GameLoop_CheckPauseGame's BEQ: Start not new, no pause */
#define PAUSE_LOOP              0xE040u   /* the pause loop's BEQ: Start not new, another frame paused */
/* Bank 12 */
#define ISCRIPT_ACTION_END_SCRIPT   0x82B4u
#define ISCRIPTS_INVOKE_NEXT_ACTION 0x826Eu
/* Bank 14 */
#define SPRITE_CATEGORIES       0xB544u   /* SPRITE_CATEGORIES_BY_ENTITY */

enum { BANK = 16384, ENTITIES = 0x65, SLOTS = 8, SPRITE_NPC = 2, NO_SCRIPT = 0xFF, SCRIPTS = 0x98, PERSON_W = 16 };
enum { AREA_EOLIS, AREA_TRUNK, AREA_MIST, AREA_TOWNS, AREA_BUILDINGS, AREA_BRANCH, AREA_DARTMOOR, AREA_ZENIS, AREAS };
enum { END_KINGS_ROOM = 0x44 };           /* building $44: the King at the end (ISCRIPT_KING_END_GAME) */
enum { BUTTON_B = 0x40, BUTTON_START = 0x10, BUTTON_LEFT = 0x02, BUTTON_RIGHT = 0x01, P_Z = 0x02 };
enum { SLOT_LEN = FAX_TRAVEL_SLOT_LEN };
/* Messages_Load's pickups: "I've got ..." and "I'm holding ...", $B0-$BA. */
enum { GOT_ELIXIR = 0xB0, GOT_RED_POTION, GOT_MATTOCK, GOT_WING_BOOTS, GOT_HOUR_GLASS, GOT_BATTLE_SUIT,
       GOT_BATTLE_HELMET, GOT_DRAGON_SLAYER, GOT_BLACK_ONYX, GOT_PENDANT, GOT_MAGICAL_ROD };

static const uint16_t list_addr[FAX_QUEST_LISTS] = { 0x039D, 0x03A1, 0x03A5, 0x03A9, 0x03AD };
static const uint8_t list_max[FAX_QUEST_LISTS] = { 4, 4, 4, 4, 8 };
static const unsigned area_screens[AREAS] = { 9, 64, 83, 14, 0, 40, 32, 20 };

/* The defaults as SDL numbers them: SDL_SCANCODE_L, and the right stick's
 * button (SDL_CONTROLLER_BUTTON_RIGHTSTICK, 8) in the host's binding encoding
 * (cyc_input.h CYC_PAD_BUTTON: 1 + button). */
enum { DEFAULT_KEY = 15, DEFAULT_PAD = 1 + 8 };
enum { OPEN_WINDOW = 30 };   /* frames a press waits for the main loop's pause check */

/* ---- state ---- */

typedef enum { QUIET, SAYING } Phase;
enum { SAY_NEW = 1, SAY_DONE = 2 };   /* a line: kind << 5 | quest */

static bool enabled, failed, decoded;
static uint8_t bank12[BANK], categories[ENTITIES];
static uint32_t given, done;              /* a bit per quest */
static uint32_t pending_new, pending_done;   /* lines to say at the script's end */
static uint8_t saying;                    /* the line in the slot while SAYING, else 0 */
static Phase phase;
static uint8_t rows;                      /* the box's line scroll when our line began */
static uint8_t pickup;                    /* the pickup message this script showed, or 0 */
static bool quiet = true;                 /* take what is complete as it is, saying nothing */
static bool seeded = true;                /* the file had quests.given; false: count done ones as given */
static uint32_t banner_pending;           /* quests done outside a conversation, to show below the HUD */
static int banner_quest = -1;             /* the one showing */
static unsigned banner_age;
static bool open, closing;
static int page;
static uint64_t frames, want;
static unsigned opens, closes, turns, presses, lines_said, banners_shown, age;
static char label[80] = "Quest log: L / RS";

static uint8_t q(uint16_t a) { return cyc_mod_peek(a); }
static uint32_t prg(unsigned bank, uint16_t addr) { return bank * 16384u + (addr - 0x8000u); }

/* ---- the cart: bank 12's scripts, bank 14's sprite categories ---- */

static bool decode(void) {
    if (decoded) return true;
    if (failed) return false;
    size_t need = cyc_mod_copy_cart_payload(NULL, 0);
    uint8_t *cart = need >= 16u * BANK ? malloc(need) : NULL;
    bool ok = cart && cyc_mod_copy_cart_payload(cart, need) == need;
    if (ok) {
        memcpy(bank12, cart + 12 * BANK, BANK);
        memcpy(categories, cart + 14 * BANK + (SPRITE_CATEGORIES - 0x8000u), sizeof categories);
        /* The King's script (8) shows his plea first, and his sprite ($3A) is a person. */
        FaxQuestGame none = { 0 };
        uint8_t path[FAX_QUEST_PATH];
        ok = fax_quest_walk(bank12, 8, &none, path) == 1 && path[0] == 0x35 && categories[0x3A] == SPRITE_NPC;
    }
    free(cart);
    if (!ok || !(fax_text_loaded() || fax_text_load())) {
        FAX_QUESTS_LOG("[quests] the cart's scripts are not the original's; the quest log is off\n");
        failed = true;
        return false;
    }
    decoded = true;
    return true;
}

/* ---- the game, now ---- */

static unsigned level_at(unsigned area, unsigned screen) {
    switch (area) {
    case AREA_MIST: return FAX_REACHED_MIST;
    case AREA_BRANCH: return FAX_REACHED_BRANCH;
    case AREA_DARTMOOR: return FAX_REACHED_DARTMOOR;
    case AREA_ZENIS: return FAX_REACHED_ZENIS;
    case AREA_TOWNS:   /* two screens a town: Apolune, Forepaw, Mascon, Victim, Conflate, Daybreak, Dartmoor */
        return screen < 2 ? FAX_REACHED_EOLIS : screen < 4 ? FAX_REACHED_FOREPAW : screen < 8 ? FAX_REACHED_MIST
             : screen < 12 ? FAX_REACHED_BRANCH : FAX_REACHED_DARTMOOR;
    case AREA_BUILDINGS: return screen == END_KINGS_ROOM ? FAX_REACHED_ENDING : FAX_REACHED_EOLIS;
    }
    return FAX_REACHED_EOLIS;
}
/* The furthest temple's town: Forepaw (2), Mascon's in the Mist (3), Victim
 * (4), Conflate (5) and Daybreak (6) off the Branch, Dartmoor (7). */
static unsigned level_of_temple(unsigned t) {
    return t >= 7 ? FAX_REACHED_DARTMOOR : t >= 5 ? FAX_REACHED_BRANCH : t >= 3 ? FAX_REACHED_MIST
         : t >= 2 ? FAX_REACHED_FOREPAW : FAX_REACHED_EOLIS;
}
static unsigned reached(void) {
    unsigned level = level_at(q(AREA_CURRENT_AREA), q(AREA_CURRENT_SCREEN));
    unsigned t = q(TEMPLE_SPAWN_POINT), gurus = fax_travel_visited();
    if (t > 7) t = 0;
    for (unsigned g = 7; g > t; --g) if (gurus >> g & 1) { t = g; break; }
    if (level_of_temple(t) > level) level = level_of_temple(t);
    for (unsigned area = 0; area < AREAS; ++area)
        for (unsigned s = 0; s < area_screens[area]; ++s) {
            unsigned l = level_at(area, s);
            if (l > level && fax_map_seen(area, s)) { level = l; break; }
        }
    return level;
}
static void game_now(FaxQuestGame *g) {
    memset(g, 0, sizeof *g);
    g->quests = q(QUESTS); g->special = q(SPECIAL_ITEMS);
    g->title = q(PLAYER_TITLE); g->next_title = q(NEXT_PLAYER_TITLE);
    g->gold = (uint32_t)q(GOLD) | (uint32_t)q(GOLD + 1) << 8 | (uint32_t)q(GOLD + 2) << 16;
    for (unsigned c = 0; c < FAX_QUEST_LISTS; ++c) {
        g->count[c] = q((uint16_t)(INVENTORY_COUNTS + c));
        g->selected[c] = q((uint16_t)(INVENTORY_SELECTED + c));
        for (unsigned i = 0; i < list_max[c]; ++i) g->list[c][i] = q((uint16_t)(list_addr[c] + i));
    }
    g->reached = (uint8_t)reached();
}
/* The item a pickup's script announces, as Player_PickUp adds it after the
 * script: special items by their bit, the Battle Suit, Battle Helmet and
 * Dragon Slayer to their lists (the last entry of a full list replaced), and
 * an Item through Player_PickUpItem, which refuses one more than 8. */
static void with_pickup(FaxQuestGame *g) {
    static const struct { uint8_t msg, special, list, id; } got[] = {
        { GOT_ELIXIR, 0x08, 0, 0 }, { GOT_BLACK_ONYX, 0x01, 0, 0 }, { GOT_PENDANT, 0x02, 0, 0 },
        { GOT_MAGICAL_ROD, 0x04, 0, 0 }, { GOT_BATTLE_SUIT, 0, FAX_QUEST_ARMOR, 3 },
        { GOT_BATTLE_HELMET, 0, FAX_QUEST_SHIELDS, 3 }, { GOT_DRAGON_SLAYER, 0, FAX_QUEST_WEAPONS, 3 },
        { GOT_MATTOCK, 0, FAX_QUEST_ITEMS, 0x09 }, { GOT_WING_BOOTS, 0, FAX_QUEST_ITEMS, 0x0F },
        { GOT_HOUR_GLASS, 0, FAX_QUEST_ITEMS, 0x0D },
    };
    for (unsigned i = 0; i < sizeof got / sizeof got[0]; ++i) {
        if (got[i].msg != pickup) continue;
        if (got[i].special) { g->special |= got[i].special; return; }
        unsigned c = got[i].list, n = g->count[c] > list_max[c] ? list_max[c] : g->count[c];
        if (n < list_max[c]) g->list[c][n] = got[i].id, g->count[c] = (uint8_t)(n + 1);
        else if (c != FAX_QUEST_ITEMS) g->list[c][n - 1] = got[i].id;
        return;
    }
}

/* Settled on a screen, in the main loop (bank 14), not paused, alive: no
 * script, menu or text box running. Those wait for their frames with bank 12
 * or 13 mapped (the text is read from bank 13). TextBox_Closed cannot tell:
 * a screen leaves one of its tiles there, 0 on some (docs/GAME_INTERNALS.md,
 * "The text box"). */
static bool in_play(void) {
    return q(SCREEN_READY_STATE) == 0 && q(INTERRUPT_HANDLERS_ON) && q(SCREEN_SCROLL_DIRECTION) == 0xFF &&
           q(CURRENT_ROM_BANK) == MAIN_LOOP_BANK && !q(GAME_PAUSED_STATE) && !q(PLAYER_IS_DEAD) &&
           q(AREA_CURRENT_AREA) < AREAS;
}

/* What is complete now joins what was: newly complete quests that were given
 * are said, in the box during a script, else below the HUD. */
static void take_done(bool in_script) {
    FaxQuestGame g;
    game_now(&g);
    if (in_script) with_pickup(&g);
    uint32_t now = fax_quest_done_all(&g) & ~done;
    done |= now;
    if (quiet) {
        quiet = false;
        if (!seeded) { given |= done; seeded = true; }
        return;
    }
    now &= given;
    if (in_script) { pending_done |= now; pending_new &= ~now; }
    else banner_pending |= now;
}

/* ---- the line slot ---- */

static bool encode(const char *s, uint8_t out[SLOT_LEN]) {
    unsigned n = 0, col = 0, row = 1;
    for (; *s; ++s) {
        uint8_t v;
        if (*s == '\n') v = 0xFE, col = 0, ++row;
        else if (*s == ' ') v = 0xFD, ++col;
        else if (*s > 0x20 && *s < 0x7F) v = (uint8_t)*s, ++col;
        else return false;
        if (col > FAX_QUEST_COLS || row > 4 || n + 1 >= SLOT_LEN) return false;
        out[n++] = v;
    }
    while (n < SLOT_LEN) out[n++] = 0xFF;
    return true;
}
static void line_text(uint8_t line, char out[48]) {
    unsigned quest = line & 31;
    snprintf(out, 48, "%s\n%s", line >> 5 == SAY_DONE ? "Quest complete" : "New quest",
             quest < FAX_QUESTS ? fax_quests[quest].name : "");
}
/* Our line into message $C3's slot, and Guru travel's SAY step that shows it. */
static bool serve(uint8_t line) {
    static const uint8_t say[] = { 0x03, FAX_TRAVEL_SLOT_ID, 0x00 };   /* SHOW_MESSAGE $C3, END */
    char text[48];
    uint8_t bytes[SLOT_LEN];
    line_text(line, text);
    if (!encode(text, bytes)) return false;
    for (unsigned i = 0; i < sizeof say; ++i)
        if (!cyc_mod_set_prg_read_override(prg(12, (uint16_t)(FAX_TRAVEL_SAY + i)), 0xFF, say[i])) return false;
    for (unsigned i = 0; i < SLOT_LEN; ++i)
        if (!cyc_mod_set_prg_read_override(prg(13, (uint16_t)(FAX_TRAVEL_SLOT + i)), 0xFF, bytes[i])) return false;
    return true;
}
static uint8_t next_line(void) {
    for (unsigned k = 0; k < 2; ++k) {
        uint32_t *set = k == 0 ? &pending_done : &pending_new;
        for (unsigned quest = 0; quest < FAX_QUESTS; ++quest)
            if (*set >> quest & 1) { *set &= ~(1u << quest); return (uint8_t)((k == 0 ? SAY_DONE : SAY_NEW) << 5 | quest); }
    }
    return 0;
}

/* ---- the hooks ---- */

/* Messages_Load: A is the message. A line that gives a quest gives it. */
static int message_hook(uint16_t addr) {
    (void)addr;
    if (!enabled || failed || cyc_mod_isolated() || !decoded) return 0;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    uint8_t msg = regs.a;
    if (msg == FAX_TRAVEL_SLOT_ID) return 0;                   /* ours, or another feature's line */
    pickup = msg >= GOT_ELIXIR && msg <= GOT_MAGICAL_ROD ? msg : 0;
    uint32_t quests = fax_quest_given_by(msg) & ~given;
    if (!quests) return 0;
    if (quiet) take_done(false);                               /* the first moment after a load or a continue */
    FaxQuestGame g;
    game_now(&g);
    quests &= ~(done | fax_quest_done_all(&g));
    given |= quests;
    pending_new |= quests;
    return 0;
}

/* IScriptAction_EndScript, after Block's: say what this script completed and
 * gave, a line at a time, as the script's next action. */
static int end_script_hook(uint16_t addr) {
    (void)addr;
    if (!enabled || failed || cyc_mod_isolated() || !decoded) return 0;
    if (phase == SAYING) { phase = QUIET; saying = 0; }        /* our line has been said (or dismissed) */
    take_done(true);
    uint8_t line = next_line();
    if (!line) return 0;
    if (!serve(line)) {
        FAX_QUESTS_LOG("[quests] cannot supply the quest log's line; its lines are off\n");
        failed = true;
        return 0;
    }
    rows = q(TEXTBOX_LINE_SCROLL) & 3;
    cyc_mod_poke(ISCRIPT_ADDR, (uint8_t)FAX_TRAVEL_SAY);
    cyc_mod_poke(ISCRIPT_ADDR + 1, (uint8_t)(FAX_TRAVEL_SAY >> 8));
    cyc_mod_poke(ISCRIPT_OFFSET, 0);
    fax_block_return_via(ISCRIPTS_INVOKE_NEXT_ACTION);
    phase = SAYING;
    saying = line;
    ++lines_said;
    return 1;
}

/* TextBox_ClearPasswordSize has zeroed the line scroll as our line loads:
 * put back the offset the box was left with (see Save files' scroll_hook). */
static int scroll_hook(uint16_t addr) {
    (void)addr;
    if (enabled && !cyc_mod_isolated() && phase == SAYING) cyc_mod_poke(TEXTBOX_LINE_SCROLL, rows);
    return 0;
}

/* ---- the log's pages ---- */

enum { NATIVE_W = 256, NATIVE_H = 240, TITLE_Y = 16, FIRST_ROW = 4, PAGE_ROWS = 22, FOOT_Y = 216, COLUMN = 7 };
enum { ROW_NAME, ROW_OBJECTIVE, ROW_HEADER, ROW_DONE, ROW_NOTE };
typedef struct { uint8_t kind, quest; char text[FAX_QUEST_COLS + 1]; } Row;
typedef struct { Row row[PAGE_ROWS]; unsigned count; } Page;
enum { MAX_PAGES = 8 };
static Page pages[MAX_PAGES];
static unsigned page_count;

static void add(Page *p, uint8_t kind, uint8_t quest, const char *text, size_t len) {
    if (p->count >= PAGE_ROWS) return;
    Row *r = &p->row[p->count++];
    r->kind = kind; r->quest = quest;
    if (len > FAX_QUEST_COLS) len = FAX_QUEST_COLS;
    memcpy(r->text, text, len);
    r->text[len] = 0;
}
/* Active quests, a block each (its name, its objective, a blank row), kept
 * whole on a page; then the completed ones' names. */
static void lay_out(void) {
    FaxQuestGame g;
    game_now(&g);
    memset(pages, 0, sizeof pages);
    page_count = 1;
    Page *p = &pages[0];
    uint32_t active = given & ~done, finished = given & done;
    if (!given) add(p, ROW_NOTE, 0, "No quests yet.", 14);
    for (unsigned quest = 0; quest < FAX_QUESTS; ++quest) {
        if (!(active >> quest & 1)) continue;
        const FaxQuest *fq = &fax_quests[quest];
        unsigned lines = 1;
        for (const char *s = fq->objective; *s; ++s) lines += *s == '\n';
        char progress[FAX_QUEST_COLS + 1];
        bool more = fax_quest_progress(quest, &g, progress);
        unsigned need = 1 + lines + more + 1;
        if (p->count && p->count + need - 1 > PAGE_ROWS && page_count < MAX_PAGES) p = &pages[page_count++];
        add(p, ROW_NAME, (uint8_t)quest, fq->name, strlen(fq->name));
        for (const char *s = fq->objective; *s;) {
            size_t len = strcspn(s, "\n");
            add(p, ROW_OBJECTIVE, (uint8_t)quest, s, len);
            s += len;
            if (*s) ++s;
        }
        if (more) add(p, ROW_OBJECTIVE, (uint8_t)quest, progress, strlen(progress));
        if (p->count < PAGE_ROWS) add(p, ROW_NOTE, 0, "", 0);
    }
    if (finished) {
        if (p->count + 2 > PAGE_ROWS && page_count < MAX_PAGES) p = &pages[page_count++];
        add(p, ROW_HEADER, 0, "Completed", 9);
        for (unsigned quest = 0; quest < FAX_QUESTS; ++quest) {
            if (!(finished >> quest & 1)) continue;
            if (p->count == PAGE_ROWS) {
                if (page_count == MAX_PAGES) break;
                p = &pages[page_count++];
            }
            add(p, ROW_DONE, (uint8_t)quest, fax_quests[quest].name, strlen(fax_quests[quest].name));
        }
    }
    if (page >= (int)page_count) page = (int)page_count - 1;
}

/* ---- the pause: the map's two branches ---- */

static void steer(bool fall_through) {   /* the BEQ: on past it (Z clear), or taken (Z set) */
    CycModRegs regs;
    cyc_mod_regs(&regs);
    if (fall_through) { regs.a = BUTTON_START; regs.p = (uint8_t)(regs.p & ~P_Z); }
    else { regs.a = 0; regs.p = (uint8_t)(regs.p | P_Z); }
    cyc_mod_set_regs(&regs);
}
static void show(void) {
    want = 0;
    if (fax_map_showing()) return;                 /* the map opened first */
    lay_out();
    page = 0;
    open = true; closing = false; age = 0;
    ++opens;
}

/* GameLoop_CheckPauseGame's BEQ, after the map's: a press pauses as Start does. */
static int check_hook(uint16_t addr) {
    (void)addr;
    if (!enabled || cyc_mod_isolated() || !want || !decoded) return 0;
    show();
    if (open) steer(true);
    return 0;
}

/* The pause loop's BEQ, once a frame while paused, after the map's. */
static int loop_hook(uint16_t addr) {
    (void)addr;
    if (!enabled || cyc_mod_isolated() || !decoded) return 0;
    if (!open) {
        if (want && !fax_map_showing()) { show(); if (open) steer(false); }   /* pressed during the game's own pause */
        return 0;
    }
    uint8_t changed = q(JOY1_CHANGED);
    bool close = closing || (changed & (BUTTON_B | BUTTON_START));
    closing = false;
    int was = page;
    if (!close && (changed & BUTTON_LEFT) && page > 0) --page;
    else if (!close && (changed & BUTTON_RIGHT) && page + 1 < (int)page_count) ++page;
    if (page != was) ++turns;
    if (close) { open = false; ++closes; }
    steer(close);
    return 0;
}

static void press(void) {
    if (!enabled) return;
    ++presses;
    if (open) closing = true;
    else if (!fax_map_showing()) want = frames + OPEN_WINDOW;
}

bool fax_quests_showing(void) { return enabled && open; }

/* ---- each frame ---- */

enum { BANNER_IN = 8, BANNER_HOLD = 150, BANNER_OUT = 32, BANNER_SHOWN = BANNER_IN + BANNER_HOLD + BANNER_OUT };

void fax_quests_frame_end(void) {
    if (!enabled) return;
    ++frames;
    if (want && frames >= want) want = 0;
    if (!decode()) return;
    if (open) ++age;
    if (q(TEXTBOX_CLOSED)) pickup = 0;
    if (!in_play()) return;
    if (phase == SAYING) { phase = QUIET; saying = 0; }        /* the box closed under our line */
    take_done(false);
    banner_pending |= pending_done;               /* a completion no script's end said */
    pending_new = pending_done = 0;
    if (banner_quest < 0) {
        if (!banner_pending || fax_banner_busy()) return;   /* after any zone name */
        for (unsigned quest = 0; quest < FAX_QUESTS; ++quest)
            if (banner_pending >> quest & 1) { banner_quest = (int)quest; banner_pending &= ~(1u << quest); break; }
        banner_age = 0;
        ++banners_shown;
    } else if (++banner_age >= BANNER_SHOWN) {
        banner_quest = -1;
    }
}

/* ---- the save file ---- */

static void put_set(char *out, uint32_t set) {
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < FAX_QUESTS; i += 4) *out++ = hex[set >> i & 15];
    *out = 0;
}
static bool get_set(const char *in, uint32_t *set) {
    uint32_t got = 0;
    unsigned i = 0;
    for (; *in; ++in, i += 4) {
        unsigned d = *in >= '0' && *in <= '9' ? (unsigned)(*in - '0') : *in >= 'a' && *in <= 'f' ? (unsigned)(*in - 'a' + 10) : 16;
        if (d > 15 || i >= 32) return false;
        got |= (uint32_t)d << i;
    }
    *set = got & ((1u << FAX_QUESTS) - 1);
    return true;
}

static void forget(void) {
    given = done = pending_new = pending_done = banner_pending = 0;
    banner_quest = -1; banner_age = 0;
    phase = QUIET; saying = 0; pickup = 0;
}

static void on_save(FaxSaveEvent event) {
    if (!enabled) return;
    if (event == FAX_SAVE_WRITING) {
        char text[FAX_SAVE_VALUE];
        put_set(text, given); fax_save_set("quests.given", text);
        put_set(text, done); fax_save_set("quests.done", text);
        return;
    }
    forget();
    quiet = true;
    seeded = true;
    if (event != FAX_SAVE_CONTINUED) return;
    const char *g = fax_save_get("quests.given"), *d = fax_save_get("quests.done");
    uint32_t set;
    if (g && get_set(g, &set)) given = set;
    else { seeded = false; if (g) FAX_QUESTS_LOG("[quests] quests.given is damaged; the log starts from what is done\n"); }
    if (d && get_set(d, &set)) done = set;
}

/* ---- drawing ---- */

enum { HUD = 32, BLACK = 0x0F, MARK_INK = 0x27, NAME_INK = 0x37, DONE_INK = 0x10 };   /* gold, pale gold, grey */

static uint32_t nes(unsigned c) {
    unsigned mask = cyc_render_line_mask(239);
    return hw_palette_argb[((c & 63) | (mask >> 5) << 6) & 511] | 0xFF000000u;
}
static uint32_t *canvas;
static size_t capacity;
static const uint32_t *begin(const uint32_t *picture, int *w, int *h, int *width, int *height, bool copy) {
    const uint32_t *from = picture;
    if (from) *width = *w, *height = *h;
    else from = cyc_render_present(width, height);
    if (!from || *width <= 0 || *height <= 0) return NULL;
    size_t n = (size_t)*width * (size_t)*height;
    if (n > capacity) {
        uint32_t *grown = realloc(canvas, n * sizeof *grown);
        if (!grown) return NULL;
        canvas = grown; capacity = n;
    }
    if (copy && from != canvas) memcpy(canvas, from, n * sizeof *canvas);
    return from;
}

/* A character of the font made bold (each pixel and the one to its right),
 * in `ink`, outlined in `edge`, its top-left at (x, y), nothing above `top`. */
static bool bold(const uint8_t *g, int r, int c) {
    return r >= 0 && r < 8 && c >= 0 && c < 9 && ((c < 8 && (g[r] >> (7 - c) & 1)) || (c > 0 && (g[r] >> (8 - c) & 1)));
}
static void glyph(uint32_t *px, int w, int h, int x, int y, char ch, uint32_t ink, uint32_t edge, int top) {
    const uint8_t *g = fax_text_glyph(ch);
    if (!g) return;
    for (int r = -1; r <= 8; ++r)
        for (int c = -1; c <= 9; ++c) {
            int py = y + r, pxl = x + c;
            if (py < top || py < 0 || py >= h || pxl < 0 || pxl >= w) continue;
            bool on = bold(g, r, c), near = false;
            for (int dr = -1; dr <= 1 && !on && !near; ++dr)
                for (int dc = -1; dc <= 1 && !near; ++dc) near = bold(g, r + dr, c + dc);
            if (on || near) px[(size_t)py * (size_t)w + (size_t)pxl] = on ? ink : edge;
        }
}

/* The marks: who is where, and what each one's script says now. */
typedef struct { uint8_t area, room, slot, script; char mark; int16_t x, y; } Mark;
enum { MAX_MARKS = 32 };
static Mark marks[MAX_MARKS];
static unsigned mark_count;
static int16_t mark_px[MAX_MARKS][2];

static void room_marks(unsigned area, unsigned room, const uint8_t *ram, const FaxQuestGame *g, uint32_t complete) {
    for (unsigned s = 0; s < SLOTS && mark_count < MAX_MARKS; ++s) {
        uint8_t entity = ram ? ram[CURRENT_SPRITES_ENTITIES + s] : q((uint16_t)(CURRENT_SPRITES_ENTITIES + s));
        uint8_t script = ram ? ram[CURRENT_SPRITES_VALUES + s] : q((uint16_t)(CURRENT_SPRITES_VALUES + s));
        if (entity >= ENTITIES || categories[entity] != SPRITE_NPC || script == NO_SCRIPT || script >= SCRIPTS) continue;
        uint8_t path[FAX_QUEST_PATH];
        unsigned n = fax_quest_walk(bank12, script, g, path);
        char mark = fax_quest_mark(path, n, given, complete);
        if (!mark) continue;
        uint8_t x = ram ? ram[CURRENT_SPRITES_XPOS + s] : q((uint16_t)(CURRENT_SPRITES_XPOS + s));
        uint8_t y = ram ? ram[CURRENT_SPRITES_YPOS + s] : q((uint16_t)(CURRENT_SPRITES_YPOS + s));
        /* Over the middle of his 16 pixels (the King's and the Gurus' boxes are wider, to the right). */
        marks[mark_count++] = (Mark){ (uint8_t)area, (uint8_t)room, (uint8_t)s, script, mark,
                                      (int16_t)(x + PERSON_W / 2 - FAX_TEXT_CELL / 2), (int16_t)(y - FAX_TEXT_CELL - 4) };
    }
}

/* Where native pixel (x, y) is in a presented picture w x h, scaled to fit
 * and centered, as Seamless shows a building it does not lay out (fax_world.c). */
static void to_picture(int w, int h, int *x, int *y) {
    int nw = w < NATIVE_W ? w : NATIVE_W, nh = NATIVE_H * nw / NATIVE_W;
    if (nh > h) { nh = h; nw = NATIVE_W * nh / NATIVE_H; }
    *x = (w - nw) / 2 + *x * nw / NATIVE_W;
    *y = *y * nh / NATIVE_H;
}

const uint32_t *fax_quests_marks_present(const uint32_t *picture, int *w, int *h) {
    mark_count = 0;
    if (!enabled || failed || !decoded || open || !in_play()) return picture;
    FaxQuestGame g;
    game_now(&g);
    uint32_t complete = done | fax_quest_done_all(&g);
    unsigned area = q(AREA_CURRENT_AREA), here = q(AREA_CURRENT_SCREEN);
    room_marks(area, here, NULL, &g, complete);
    int ox, oy;
    bool world = picture && fax_world_origin(&ox, &oy);
    if (world)   /* the people in the rooms around, from Seamless's copies */
        for (unsigned room = 0; room < 256 && mark_count < MAX_MARKS; ++room) {
            const uint8_t *ram = room == here ? NULL : fax_world_room_ram((int)area, (int)room);
            if (ram) room_marks(area, room, ram, &g, complete);
        }
    if (!mark_count) return picture;
    int width = 0, height = 0;
    if (!begin(picture, w, h, &width, &height, true)) { mark_count = 0; return picture; }
    uint32_t ink = nes(MARK_INK), edge = nes(BLACK);
    int top = width < NATIVE_W ? HUD * width / NATIVE_W : HUD;
    for (unsigned i = 0; i < mark_count; ++i) {
        const Mark *m = &marks[i];
        int x = m->x, y = m->y + HUD;
        if (world) {
            int rx, ry;
            if (!fax_world_room_origin(m->area, m->room, &rx, &ry)) { mark_px[i][0] = mark_px[i][1] = -1; continue; }
            x = rx + m->x; y = ry + m->y;
        } else if (width >= NATIVE_W) {   /* the original view, or Seamless showing the native picture */
            x += cyc_render_native_origin(width);
        } else {
            to_picture(width, height, &x, &y);
        }
        mark_px[i][0] = (int16_t)x; mark_px[i][1] = (int16_t)y;
        glyph(canvas, width, height, x, y, m->mark, ink, edge, top);
    }
    *w = width; *h = height;
    return canvas;
}

/* "Quest complete" and the name, in a box below the HUD, as the zone banner. */
static unsigned banner_alpha(void) {
    if (banner_age < BANNER_IN) return (banner_age + 1) * 255u / BANNER_IN;
    if (banner_age < BANNER_IN + BANNER_HOLD) return 255;
    if (banner_age < BANNER_SHOWN) return (BANNER_SHOWN - banner_age) * 255u / (BANNER_OUT + 1);
    return 0;
}
const uint32_t *fax_quests_banner_present(const uint32_t *picture, int *w, int *h) {
    if (!enabled || failed || banner_quest < 0 || open || !in_play()) return picture;
    unsigned alpha = banner_alpha();
    if (!alpha) return picture;
    int width = 0, height = 0;
    if (!begin(picture, w, h, &width, &height, true)) return picture;
    const char *line[2] = { "Quest complete", fax_quests[banner_quest].name };
    int cols = FAX_QUEST_COLS + 2, box_w = (cols + 2) * FAX_TEXT_CELL;
    int hud = width < NATIVE_W ? HUD * width / NATIVE_W : HUD;
    int x = (width - box_w) / 2, y = hud + FAX_TEXT_CELL;
    fax_text_box(canvas, width, height, x, y, cols, 2, alpha);
    for (int r = 0; r < 2; ++r)
        fax_text_draw(canvas, width, height, x + (box_w - fax_text_width(line[r])) / 2,
                      y + (1 + r) * FAX_TEXT_CELL, line[r], alpha);
    *w = width; *h = height;
    return canvas;
}

/* The log: black over the whole picture, the text box's frame over the
 * native 256 x 240 centered in it, "Quests" on row 2, a page of rows from row
 * 4, the page's number on row 27. */
const uint32_t *fax_quests_log_present(const uint32_t *picture, int *w, int *h) {
    if (!enabled || !open || !decoded) return picture;
    int width = 0, height = 0;
    if (!begin(picture, w, h, &width, &height, false)) return picture;
    for (size_t i = 0, n = (size_t)width * (size_t)height; i < n; ++i) canvas[i] = nes(BLACK);
    int ox = (width - NATIVE_W) / 2, oy = (height - NATIVE_H) / 2;
    if (oy < 0) oy = 0;
    lay_out();
    fax_text_box(canvas, width, height, ox, oy, NATIVE_W / FAX_TEXT_CELL - 2, NATIVE_H / FAX_TEXT_CELL - 2, 255);
    const char *title = "Quests";
    fax_text_draw(canvas, width, height, ox + (NATIVE_W - fax_text_width(title)) / 2, oy + TITLE_Y, title, 255);
    if (page_count > 1) {   /* the font's triangles: ~ points left, } right */
        if (page > 0) fax_text_draw(canvas, width, height, ox + 2 * FAX_TEXT_CELL, oy + TITLE_Y, "~", 255);
        if (page + 1 < (int)page_count)
            fax_text_draw(canvas, width, height, ox + NATIVE_W - 3 * FAX_TEXT_CELL, oy + TITLE_Y, "}", 255);
        char foot[16];
        snprintf(foot, sizeof foot, "%d of %u", page + 1, page_count);
        fax_text_draw(canvas, width, height, ox + (NATIVE_W - fax_text_width(foot)) / 2, oy + FOOT_Y, foot, 255);
    }
    const Page *p = &pages[page];
    for (unsigned i = 0; i < p->count; ++i) {
        const Row *r = &p->row[i];
        int x = ox + COLUMN * FAX_TEXT_CELL, y = oy + (int)(FIRST_ROW + i) * FAX_TEXT_CELL;
        switch (r->kind) {
        case ROW_NAME: fax_text_draw_color(canvas, width, height, x, y, r->text, nes(NAME_INK), 255); break;
        case ROW_OBJECTIVE: fax_text_draw(canvas, width, height, x + FAX_TEXT_CELL, y, r->text, 255); break;
        case ROW_HEADER: fax_text_draw(canvas, width, height, x, y, r->text, 255); break;
        case ROW_DONE: fax_text_draw_color(canvas, width, height, x + FAX_TEXT_CELL, y, r->text, nes(DONE_INK), 255); break;
        default: fax_text_draw(canvas, width, height, x, y, r->text, 255); break;
        }
    }
    *w = width; *h = height;
    return canvas;
}

/* ---- save states: everything above ---- */

enum { VERSION = 1, RECORD = 1 + 4 * 5 + 10 };
static void put32(uint8_t *p, uint32_t v) { for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i)); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static int state_get(uint8_t *buf, int cap) {
    if (cap < RECORD) return -1;
    uint8_t *p = buf;
    *p++ = VERSION;
    put32(p, given); p += 4; put32(p, done); p += 4; put32(p, pending_new); p += 4; put32(p, pending_done); p += 4;
    put32(p, banner_pending); p += 4;
    *p++ = (uint8_t)phase; *p++ = saying; *p++ = rows; *p++ = pickup;
    *p++ = (uint8_t)(quiet | seeded << 1); *p++ = (uint8_t)(banner_quest < 0 ? 0xFF : banner_quest);
    *p++ = (uint8_t)(banner_age > 255 ? 255 : banner_age);
    *p++ = open; *p++ = (uint8_t)page; *p++ = (uint8_t)(want > frames ? want - frames : 0);
    return (int)(p - buf);
}
static int state_validate(const uint8_t *buf, int len) {
    if (len == 0) return 1;
    if (len != RECORD || buf[0] != VERSION) return 0;
    const uint8_t *p = buf + 21;
    return p[0] <= SAYING && (p[1] == 0 || (p[1] & 31) < FAX_QUESTS) && p[2] <= 3 && p[4] <= 3 &&
           (p[5] == 0xFF || p[5] < FAX_QUESTS) && p[7] <= 1 && p[8] < MAX_PAGES;
}
static int state_set(const uint8_t *buf, int len) {
    if (!state_validate(buf, len)) return 0;
    open = closing = false; page = 0; want = 0;
    if (!len) { forget(); quiet = true; seeded = false; return 1; }
    const uint8_t *p = buf + 1;
    given = get32(p); done = get32(p + 4); pending_new = get32(p + 8); pending_done = get32(p + 12);
    banner_pending = get32(p + 16);
    p += 20;
    phase = p[0]; saying = p[1]; rows = p[2]; pickup = p[3];
    quiet = p[4] & 1; seeded = (p[4] >> 1) & 1;
    banner_quest = p[5] == 0xFF ? -1 : p[5]; banner_age = p[6];
    open = p[7]; page = p[8]; want = p[9] ? frames + p[9] : 0;
    if (open && decoded) lay_out();
    return 1;
}
void fax_quests_loaded(void) {
    if (enabled && !failed && phase == SAYING && saying && !serve(saying)) { phase = QUIET; saying = 0; }
}

/* ---- the bindings (config.ini [Game]) ---- */

const char *fax_quests_controls_label(void) { return label; }

#ifdef CYC_WITH_SDL
static int key = DEFAULT_KEY, pad = DEFAULT_PAD;   /* 0: unbound */
static bool axis_down;

static void describe(void) {   /* as the host shows its own shortcuts: "L / RS" */
    char hint[64] = "unbound";
    CycBinding both = { key, pad };
    if (key || pad) cyc_binding_hint(&both, hint, sizeof hint);
    snprintf(label, sizeof label, "Quest log: %s", hint);
}

bool fax_quests_load_setting(const char *name, const char *value) {
    int got;
    if (!strcmp(name, "QuestLogKey")) {
        if (cyc_key_parse(value, &got)) key = got;
        else fprintf(stderr, "[quests] QuestLogKey: SDL has no key named \"%s\"; keeping the default\n", value);
    } else if (!strcmp(name, "QuestLogButton")) {
        if (cyc_pad_parse(value, &got) && !CYC_PAD_IS_COMBO(got)) pad = got;
        else fprintf(stderr, "[quests] QuestLogButton: \"%s\" is not one controller button or axis; keeping the default\n", value);
    } else {
        return false;
    }
    describe();
    return true;
}
void fax_quests_save_settings(FILE *f) {
    char k[32], p[48];
    cyc_key_text(key, k, sizeof k);
    cyc_pad_text(pad, p, sizeof p);
    fprintf(f, "QuestLogKey = %s\nQuestLogButton = %s\n", k, p);
}

void fax_quests_event(const void *ev) {
    const SDL_Event *e = ev;
    if (e->type == SDL_KEYDOWN) {
        if (key && !e->key.repeat && e->key.keysym.scancode == key) press();
    } else if (e->type == SDL_CONTROLLERBUTTONDOWN) {
        if (CYC_PAD_IS_BUTTON(pad) && e->cbutton.button == pad - 1) press();
    } else if (e->type == SDL_CONTROLLERAXISMOTION && CYC_PAD_IS_AXIS(pad) && e->caxis.axis == (pad - 100) / 2) {
        int value = (pad - 100) & 1 ? e->caxis.value : -e->caxis.value;
        bool down = value >= 16384;
        if (down && !axis_down) press();
        axis_down = down;
    }
}

static size_t quest_ids(char *out, size_t cap, uint32_t set) {
    size_t n = (size_t)snprintf(out, cap, "[");
    bool first = true;
    for (unsigned quest = 0; quest < FAX_QUESTS && n < cap; ++quest)
        if (set >> quest & 1) {
            n += (size_t)snprintf(out + n, cap - n, "%s\"%s\"", first ? "" : ",", fax_quests[quest].id);
            first = false;
        }
    if (n < cap) n += (size_t)snprintf(out + n, cap - n, "]");
    return n;
}
static void quest_state(int id, const char *line) {
    (void)line;
    static char fields[8192];
    char set[512], text[48] = "";
    size_t n = 0;
    if (saying) line_text(saying, text);
    for (char *c = text; *c; ++c) if (*c == '\n') *c = '|';
    n += (size_t)snprintf(fields + n, sizeof fields - n,
                          "\"enabled\":%s,\"decoded\":%s,\"open\":%s,\"page\":%d,\"pages\":%u,\"opens\":%u,"
                          "\"closes\":%u,\"turns\":%u,\"presses\":%u,\"binding\":\"%s\",\"paused\":%u,"
                          "\"saying\":%s%s%s,\"lines\":%u,\"banner\":%s%s%s,\"banners\":%u,\"quiet\":%s,\"in_play\":%s",
                          enabled ? "true" : "false", decoded ? "true" : "false", open ? "true" : "false", page,
                          page_count, opens, closes, turns, presses, label + 11, q(GAME_PAUSED_STATE),
                          saying ? "\"" : "", saying ? text : "null", saying ? "\"" : "", lines_said,
                          banner_quest >= 0 ? "\"" : "", banner_quest >= 0 ? fax_quests[banner_quest].id : "null",
                          banner_quest >= 0 ? "\"" : "", banners_shown, quiet ? "true" : "false",
                          decoded && in_play() ? "true" : "false");
    quest_ids(set, sizeof set, given);
    n += (size_t)snprintf(fields + n, sizeof fields - n, ",\"given\":%s", set);
    quest_ids(set, sizeof set, done);
    n += (size_t)snprintf(fields + n, sizeof fields - n, ",\"done\":%s", set);
    quest_ids(set, sizeof set, given & ~done);
    n += (size_t)snprintf(fields + n, sizeof fields - n, ",\"active\":%s", set);
    quest_ids(set, sizeof set, pending_new);
    n += (size_t)snprintf(fields + n, sizeof fields - n, ",\"pending_new\":%s", set);
    quest_ids(set, sizeof set, pending_done | banner_pending);
    n += (size_t)snprintf(fields + n, sizeof fields - n, ",\"pending_done\":%s", set);
    if (decoded) {
        FaxQuestGame g;
        game_now(&g);
        n += (size_t)snprintf(fields + n, sizeof fields - n, ",\"reached\":%u", g.reached);
    }
    n += (size_t)snprintf(fields + n, sizeof fields - n, ",\"marks\":[");
    for (unsigned i = 0; i < mark_count && n < sizeof fields - 96; ++i)
        n += (size_t)snprintf(fields + n, sizeof fields - n, "%s{\"area\":%u,\"room\":%u,\"slot\":%u,\"script\":%u,"
                              "\"mark\":\"%c\",\"x\":%d,\"y\":%d}", i ? "," : "", marks[i].area, marks[i].room,
                              marks[i].slot, marks[i].script, marks[i].mark, mark_px[i][0], mark_px[i][1]);
    n += (size_t)snprintf(fields + n, sizeof fields - n, "],\"rows\":[");
    if (open && page < (int)page_count)
        for (unsigned i = 0; i < pages[page].count && n < sizeof fields - 64; ++i)
            n += (size_t)snprintf(fields + n, sizeof fields - n, "%s[%u,\"%s\"]", i ? "," : "", pages[page].row[i].kind,
                                  pages[page].row[i].text);
    snprintf(fields + n, sizeof fields - n, "]");
    cyc_tcp_ok(id, fields);
}
void fax_quests_tcp_setup(void) {
    cyc_tcp_register("quest_state",
                     "Quest log: quests given, done and active, the line saying, the marks drawn, the log's page", quest_state);
}
#else
bool fax_quests_load_setting(const char *name, const char *value) { (void)name; (void)value; return false; }
void fax_quests_save_settings(FILE *f) { (void)f; }
void fax_quests_event(const void *ev) { (void)ev; }
void fax_quests_tcp_setup(void) {}
#endif

static const char *const hook_ids[] = { "faxanadu.quests.message", "faxanadu.quests.end-script", "faxanadu.quests.scroll",
                                        "faxanadu.quests.check", "faxanadu.quests.loop" };
static void hooks(int on) {
    for (unsigned i = 0; i < sizeof hook_ids / sizeof hook_ids[0]; ++i) nes_mod_set_function_hook_enabled(hook_ids[i], on);
}
static void deactivate(void) {
    enabled = false;
    open = closing = false;
    page = 0; want = 0;
    opens = closes = turns = presses = lines_said = banners_shown = age = 0;
    forget();
    quiet = true; seeded = false;
    hooks(0);
}
static void activate(void) {
    enabled = true;
    hooks(1);
    nes_mod_set_local_only("Quest log", 1);   /* it pauses the game: host input a peer would not see */
}

NES_MOD_CONSTRUCTOR(register_quests) {
    if (!nes_mod_register_function_entry_plugin(hook_ids[0], MESSAGES_LOAD, message_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[1], ISCRIPT_ACTION_END_SCRIPT, end_script_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[2], CLEAR_SCROLLED, scroll_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[3], PAUSE_CHECK, check_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[4], PAUSE_LOOP, loop_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.quest-log", activate) ||
        !nes_mod_register_savestate_hook("faxanadu.quests", state_get, state_set) ||
        !nes_mod_register_savestate_validator("faxanadu.quests", state_validate) ||
        !fax_save_listen(on_save)) abort();
}
