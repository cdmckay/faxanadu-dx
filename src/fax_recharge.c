/* Recharging items. USA 57dd23d1, chipx86 byte-exact disassembly.
 *
 * The Wing Boots and the Hour Glass are kept after use and recharge: each use
 * gives its time (flight that grows with rank, 30 to 60 s; the Hour Glass's own
 * 15 s freeze and half-HP cost), then a cooldown (60 s each) before the next. Using
 * one that is not ready does nothing but play the game's "invalid choice"
 * sound. Dying makes both ready (a respawn clears timed items); a Guru travel
 * journey does not. Finding a pair or an Hour Glass in the world while you own
 * one makes it ready at once instead of adding a second. The game's shops
 * would sell a second (Shop_GetPlayerHasSelectedItem refuses no Items you
 * have); Quick shops (fax_shops.c) lists one you have as owned instead, and
 * ignores A on it (fax_recharge_on).
 *
 * The Elixir is kept after it revives you, spent, and only talking to a Guru
 * recharges it: no timer, and a death leaves it spent. SpecialItems' Elixir
 * bit stays the game's own "a revive is in reserve", so Player_ReduceHP
 * decides as it always did, and the item menu shows the Elixir exactly when it
 * is charged. A pickup gives one to a spent Elixir, a recharge. A spent Elixir
 * is still the hero's, as a charged one is, so buying one never recharges it:
 * Quick shops lists it as owned and ignores A on it (fax_recharge_elixir_spent),
 * where the game's shop would have sold one to a spent Elixir, its bit being
 * clear. The Spring of Trunk still takes a charged one for good.
 *
 * The Guru says so. A Guru who charges a spent Elixir opens the conversation
 * with "Your Elixir is restored. Come back when used.", every time; the
 * first time the hero talks to one while carrying a charged Elixir, he opens
 * with "If your Elixir is used, a Guru will restore it.", once.
 * Either line tells him (charge.elixir_told), so once he has heard the
 * restored line he never hears the other. Then the Guru goes on as he would
 * have: his greeting, Save files' choice, Guru travel. The line is one more
 * native message in his box, through Guru travel's SAY step and message
 * $C3's slot (fax_travel.h), as Block's tutorial and the quest log show
 * theirs. B on it ends the conversation, as on any of his lines.
 *
 * How, on the unchanged ROM:
 * - Player_UseWingBoots / Player_UseHourGlass entry: refuse when not ready.
 * - Player_ClearSelectedItem from those two routines: skipped, so the item
 *   stays selected (the use-up list in fax_items.h, shared with reusable tools).
 * - Player_UseWingBoots' STA DurationWingBoots: the flight from SCHEDULE, not
 *   TITLE_TO_WINGBOOTS_DURATION.
 * - GameLoop_CountdownItems, once a main-loop frame before the game's own
 *   countdowns: a run that has ended starts its cooldown, cooldowns count down
 *   on the game's own second (InterruptCounter & $3F == 0), and the HUD's T:
 *   shows a recharging item's cooldown, blinking, through the PPU buffer as
 *   UI_DrawTimeValue writes it. Flight time stays the game's steady count.
 * - Player_UseElixir: the Elixir is spent. IScripts_InvokeNextAction, at the
 *   first action of a Guru's script: a spent Elixir is charged again, and his
 *   line is chosen. It is shown before his next action that no other feature
 *   steers (a temple Guru's CHECK_UPDATE_PLAYER_TITLE, right after
 *   SET_SPAWN_POINT; Eolis's Guru's first, CHECK_FOR_ITEM): the script goes
 *   to the SAY step, and at its END comes back where it was.
 * - Player_PickUpItem: a duplicate recharges instead of being added.
 * - Game_ClearTimedItems (every spawn but a Guru journey): both ready, or a
 *   continued file's cooldowns.
 * - Game_ShowStartScreen (power-on or reset, before a new game or a file):
 *   everything forgotten.
 * Hooks do nothing inside Seamless's isolated simulation of other rooms. The
 * state is the save-state record, and Save files keeps the cooldowns, the
 * Elixir's charge and whether a Guru has told the hero in the file in play
 * (on_save): written whenever the file is, read back when it is continued,
 * reset by a new game. Talking to any Guru charges the Elixir before he
 * speaks, so whichever of Save files' menu options is picked, or none. See
 * docs/GAME_INTERNALS.md. */
#include "fax_recharge.h"
#include "fax_items.h"
#include "fax_save.h"
#include "fax_travel.h"
#include "cyc_mod.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#ifdef CYC_WITH_SDL
#include "cyc_tcp.h"
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Fixed bank */
#define PLAYER_USE_ELIXIR       0xC4CAu  /* LDA SpecialItems, AND #$F7, STA SpecialItems: Player_ReduceHP's revive */
#define PLAYER_USE_WING_BOOTS   0xC579u  /* LDA #$83, JSR MMC1_LoadBankAndJump: the "used Wing Boots" script */
#define WING_BOOTS_SET_FLIGHT   0xC592u  /* STA DurationWingBoots, after LDA TITLE_TO_WINGBOOTS_DURATION,X;
                                            then JSR UI_DrawTimeValue draws A */
#define PLAYER_USE_HOUR_GLASS   0xC5C8u  /* LDA #$82, JSR MMC1_LoadBankAndJump: the "used Hour Glass" script */
#define GAME_CLEAR_TIMED_ITEMS  0xC6AFu  /* LDA #$FF, STA DurationGlove, ...: from Game_Start and Player_Spawn */
#define GAME_LOOP_COUNTDOWN     0xC88Fu  /* GameLoop_CountdownItems: JSR Game_DecHourGlassDuration, ... */
#define PLAYER_PICK_UP_ITEM     0xC8CDu  /* LDX NumberOfItems, CPX #$08, BCS: A is the item */
#define SOUND_PLAY_EFFECT       0xD0E4u  /* STA Temp_SoundIDToPlay, ...: queues sound A, keeps X and Y */
#define GAME_SHOW_START_SCREEN  0xFC65u  /* LDX #$FF, TXS: the title, from Game_InitStateForStartScreen only */
/* Bank 12 */
#define ISCRIPTS_INVOKE_NEXT_ACTION 0x826Eu  /* JSR IScripts_LoadByte: the action at IScriptOrCHRAddr + IScriptOffset */
#define EOLIS_GURU_FIRST        0xA0BDu  /* ISCRIPT_EOLIS_GURU's first action: CHECK_FOR_ITEM Ring of Elf */
#define SAY                     FAX_TRAVEL_SAY      /* $AE00, Guru travel's step: 03 C3 00, show the slot, then END */
/* Bank 13 */
#define SLOT                    FAX_TRAVEL_SLOT     /* $B45B: message $C3, the line slot */
/* RAM */
#define INTERRUPT_COUNTER       0x001Au  /* +1 each NMI; the game's second is when & $3F == 0 */
#define ISCRIPT_ADDR            0x00DBu  /* IScriptOrCHRAddr; the next action is at it + IScriptOffset ($DD) */
#define ISCRIPT_OFFSET          0x00DDu
#define PPU_BUFFER_READ         0x001Fu  /* PPUBuffer_ReadOffset */
#define PPU_BUFFER_WRITE        0x0020u  /* PPUBuffer_WriteOffset */
#define PPU_BUFFER              0x0500u  /* 256 bytes, circular: [length][addr hi][addr lo][tiles] */
#define ITEM_INVENTORY          0x03ADu
#define SELECTED_ITEM           0x03C1u
#define NUMBER_OF_ITEMS         0x03C6u
#define DURATION_WING_BOOTS     0x0429u  /* $FF when not running */
#define DURATION_HOUR_GLASS     0x042Au
#define SPECIAL_ITEMS           0x042Cu
#define PLAYER_TITLE            0x0437u  /* 0-15 */
/* Values */
#define BIT_ELIXIR              0x08u
#define SOUND_INVALID_CHOICE    0x0Du
#define HUD_TIME                0x2058u  /* UI_DrawTimeValue's two digits */
#define PPU_BUFFER_ROOM         0x24u    /* PPUBuffer_HasCapacity: at least 36 bytes free, or empty */
#define TILE_BLANK              0x00u    /* the HUD's blank tile */
#define MAX_ITEMS               8
#define ISCRIPT_SET_SPAWN_POINT 0x06u    /* 06 temple: each temple Guru's first action (Conflate's after a
                                            CHECK_FOR_ITEM); CHECK_UPDATE_PLAYER_TITLE follows it */
#define ISCRIPT_SHOW_MESSAGE    0x03u
#define ISCRIPT_END             0x00u
#define TEMPLES                 8
enum { SLOT_LEN = FAX_TRAVEL_SLOT_LEN, SLOT_ID = FAX_TRAVEL_SLOT_ID };

enum { BOOTS, GLASS, TIMED };   /* the two timed items */
enum { BANDS = 4 };             /* title bands: PlayerTitle / 4 */

/* Flight time and cooldowns, in the game's seconds (64 frames, what T:
 * counts), by title band, as the game indexes TITLE_TO_WINGBOOTS_DURATION.
 * Decided in #27: flight grows with rank, 30, 40, 50 and 60 s, where the
 * original's table shrinks (40, 30, 20, 10 s); both cooldowns are 60 s at
 * every rank. Retuning means changing these rows and nothing else. A flight
 * takes the row for the title held when it starts, a cooldown the row for the
 * title held when it begins. The Hour Glass's 15 s is the game's own
 * (Player_UseHourGlass, LDA #$0F). The lowest band's 30 s is enough for the
 * Spring of Sky (docs/GAME_INTERNALS.md, "Recharging items"). */
typedef struct { uint8_t flight, cooldown[TIMED]; } Timing;
static const Timing SCHEDULE[BANDS] = {
    { 30, { 60, 60 } },   /* Novice to Fighter */
    { 40, { 60, 60 } },   /* Adept to Warrior */
    { 50, { 60, 60 } },   /* Swordman to Myrmidon */
    { 60, { 60, 60 } },   /* Champion to Lord */
};
enum { MOST_SECONDS = 99 };     /* T: has two digits */

static const uint8_t ITEM_ID[TIMED] = { FAX_ITEM_WING_BOOTS, FAX_ITEM_HOUR_GLASS };
static const uint16_t DURATION[TIMED] = { DURATION_WING_BOOTS, DURATION_HOUR_GLASS };

/* The state: everything a save state or a save slot must keep. */
typedef struct {
    uint8_t running[TIMED];    /* used, and its time has not run out */
    uint8_t cooldown[TIMED];   /* the game's seconds until ready; 0 when ready */
    uint8_t elixir_spent;      /* it revived the hero; a Guru charges it again */
    uint8_t elixir_told;       /* a Guru has said that he restores it (either line) */
} Charge;

/* What a Guru says about the Elixir, in his own box: 16 columns, 4 lines;
 * '\n' breaks a line. */
enum { LINE_NONE, LINE_RESTORED, LINE_CAN, LINES };
static const char *const guru_line[LINES] = {
    "", "Your Elixir is\nrestored. Come\nback when used.", "If your Elixir\nis used, a Guru\nwill restore it." };
/* One Guru's conversation: his line waits for the action it goes before
 * (PENDING), then shows (SAYING) with the script parked at `resume`. */
enum { TALK_IDLE, TALK_PENDING, TALK_SAYING, TALK_PHASES };
typedef struct {
    uint8_t phase, line;
    uint16_t at;               /* PENDING: the action the line goes before */
    uint16_t resume;           /* SAYING: IScriptOrCHRAddr and IScriptOffset to go back to */
    uint8_t offset;
} Talk;

/* What the HUD's T: should show. */
typedef struct { int value; bool blank; } Show;   /* value -1: the game's own (flight) count */
enum { SHOW_GAME = -1 };

static bool enabled;
static Charge charge;
/* A continued file's cooldowns, put back at the spawn that follows. */
typedef struct { uint8_t pending, cooldown[TIMED]; } Resume;
static Resume resume;
static Show drawn;                 /* what this module last queued on T: */
static bool drawn_valid;
static Talk talk;
static bool lines_failed;          /* the line could not be supplied: the Gurus charge it silently */
static unsigned flights, freezes, refused, kept, pickups, revives, guru_recharges, spawns;   /* for TCP */
static unsigned lines_said[LINES];

/* ---- the rules, on plain values ---- */

static const Timing *timing(uint8_t title) { return &SCHEDULE[(title >> 2) & (BANDS - 1)]; }
static bool ready(const Charge *c, int i) { return !c->running[i] && !c->cooldown[i]; }
static bool cooling(const Charge *c, int i) { return !c->running[i] && c->cooldown[i]; }
static void make_ready(Charge *c, int i) { c->running[i] = 0; c->cooldown[i] = 0; }

/* One main-loop frame, before the game counts its own durations down. */
static void tick(Charge *c, const uint8_t duration[TIMED], uint8_t counter, uint8_t title) {
    for (int i = 0; i < TIMED; ++i) {
        if (c->running[i] && duration[i] >= 0x80) {          /* its time ran out: "... are gone" */
            c->running[i] = 0;
            c->cooldown[i] = timing(title)->cooldown[i];
        } else if (cooling(c, i) && (counter & 0x3F) == 0) {
            --c->cooldown[i];
        }
    }
}

/* T: shows the selected item's cooldown if it is recharging, else the other
 * recharging item's (the Wing Boots first), blinking: off 16 frames in 64.
 * While the Wing Boots fly, T: is the game's flight count. 0 is the game's
 * idle "00". */
static Show hud_show(const Charge *c, uint8_t boots_duration, uint8_t selected, uint8_t counter) {
    if (boots_duration < 0x80) return (Show){ SHOW_GAME, false };
    int i = -1;
    for (int k = 0; k < TIMED && i < 0; ++k) if (selected == ITEM_ID[k] && cooling(c, k)) i = k;
    for (int k = 0; k < TIMED && i < 0; ++k) if (cooling(c, k)) i = k;
    if (i < 0) return (Show){ 0, false };
    return (Show){ c->cooldown[i], (counter & 0x30) == 0x30 };
}

/* What a Guru says as he greets the hero: that he restored a spent Elixir,
 * every time; that he can, the first time the hero brings a charged one and
 * no Guru has said either; else nothing (no Elixir, or told already). */
static int guru_says(const Charge *c, bool charged) {
    if (c->elixir_spent) return LINE_RESTORED;
    return charged && !c->elixir_told ? LINE_CAN : LINE_NONE;
}

/* A line in the dialogue font ($FD a space, $FE a new line), $FF-terminated
 * and padded to the slot; false if it would not fit the box (16 columns, 4
 * lines) or the slot. */
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

/* ---- the machine ---- */

static uint8_t q(uint16_t addr) { return cyc_mod_peek(addr); }
static int timed_index(uint8_t item) {
    for (int i = 0; i < TIMED; ++i) if (ITEM_ID[i] == item) return i;
    return -1;
}
/* The hero has `item`: selected, or in the first NumberOfItems slots
 * (Player_LacksItem's test). */
static bool owns(uint8_t item) {
    if (q(SELECTED_ITEM) == item) return true;
    unsigned n = q(NUMBER_OF_ITEMS);
    if (n > MAX_ITEMS) n = MAX_ITEMS;
    for (unsigned i = 0; i < n; ++i) if (q((uint16_t)(ITEM_INVENTORY + i)) == item) return true;
    return false;
}

/* Queue T:'s two tiles in the PPU buffer, as UI_DrawTimeValue does (length 2,
 * PPU $2058, ASCII digits), if PPUBuffer_HasCapacity would allow it; the NMI
 * draws them. False when the buffer is too full this frame. */
static bool queue_time(Show s) {
    uint8_t r = q(PPU_BUFFER_READ), w = q(PPU_BUFFER_WRITE), room = (uint8_t)(r - w);
    if (room && room < PPU_BUFFER_ROOM) return false;
    int v = s.value < 0 ? 0 : s.value > MOST_SECONDS ? MOST_SECONDS : s.value;
    uint8_t bytes[5] = { 2, HUD_TIME >> 8, HUD_TIME & 0xFF,
                         s.blank ? TILE_BLANK : (uint8_t)('0' + v / 10),
                         s.blank ? TILE_BLANK : (uint8_t)('0' + v % 10) };
    for (unsigned k = 0; k < sizeof bytes; ++k) cyc_mod_poke((uint16_t)(PPU_BUFFER + (uint8_t)(w + k)), bytes[k]);
    cyc_mod_poke(PPU_BUFFER_WRITE, (uint8_t)(w + sizeof bytes));
    return true;
}
static void draw_hud(uint8_t counter) {
    Show s = hud_show(&charge, q(DURATION_WING_BOOTS), q(SELECTED_ITEM), counter);
    if (s.value == SHOW_GAME) { drawn_valid = false; return; }
    bool changed = !drawn_valid || s.value != drawn.value || s.blank != drawn.blank;
    bool refresh = s.value > 0 && (counter & 0x0F) == 0;   /* in case the game redrew the HUD */
    if ((changed || refresh) && queue_time(s)) { drawn = s; drawn_valid = true; }
}

/* GameLoop_CountdownItems: once a main-loop frame. */
static int tick_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) return 0;
    uint8_t counter = q(INTERRUPT_COUNTER), duration[TIMED] = { q(DURATION_WING_BOOTS), q(DURATION_HOUR_GLASS) };
    tick(&charge, duration, counter, q(PLAYER_TITLE));
    if (charge.elixir_spent && (q(SPECIAL_ITEMS) & BIT_ELIXIR)) charge.elixir_spent = 0;   /* found */
    draw_hud(counter);
    return 0;
}

/* Player_UseWingBoots, Player_UseHourGlass: refuse with the game's "invalid
 * choice" sound when not ready. The routine was entered from
 * GameLoop_CheckUseCurrentItem's jump table, so returning goes back to the
 * main loop, as its RTS would. */
static int use_hook(uint16_t addr) {
    if (cyc_mod_isolated()) return 0;
    int i = addr == PLAYER_USE_WING_BOOTS ? BOOTS : GLASS;
    if (!ready(&charge, i)) {
        CycModRegs regs;
        cyc_mod_regs(&regs);
        regs.a = SOUND_INVALID_CHOICE;
        cyc_mod_call_commit(SOUND_PLAY_EFFECT, &regs);
        ++refused;
        return 1;
    }
    charge.running[i] = 1;
    if (i == BOOTS) ++flights; else ++freezes;
    return 0;
}

/* Player_ClearSelectedItem from the Wing Boots' or Hour Glass's routine:
 * keep the item. The routine only clears SelectedItem and the picture, and
 * preserves A. */
static int keep_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) return 0;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    if (fax_use_up_keeper(fax_jsr_caller(regs.s, cyc_mod_peek)) != FAX_KEPT_BY_RECHARGE) return 0;
    ++kept;
    return 1;
}

/* Player_UseWingBoots' STA DurationWingBoots: the flight from SCHEDULE. A is
 * also what UI_DrawTimeValue draws next. */
static int flight_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) return 0;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    regs.a = timing(q(PLAYER_TITLE))->flight;
    cyc_mod_set_regs(&regs);
    return 0;
}

/* Player_UseElixir: it revived the hero, and is now spent. */
static int elixir_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) return 0;
    charge.elixir_spent = 1;
    ++revives;
    return 0;
}

/* Player_PickUpItem with A the item: a pair of Wing Boots or an Hour Glass
 * the hero owns already makes it ready instead of filling a slot. Returning
 * goes back to Player_HandleTouchItem, which removes the sprite. */
static int pick_up_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) return 0;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    int i = timed_index(regs.a);
    if (i < 0 || !owns(regs.a)) return 0;
    make_ready(&charge, i);
    ++pickups;
    return 1;
}

/* Game_ClearTimedItems: a respawn, a new game, or continuing (a mantra or a
 * file): both ready, but a continued file's cooldowns are put back. */
static int spawn_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) return 0;
    for (int i = 0; i < TIMED; ++i) {
        make_ready(&charge, i);
        if (resume.pending) charge.cooldown[i] = resume.cooldown[i];   /* continuing a file */
    }
    resume = (Resume){ 0 };
    ++spawns;
    return 0;
}

/* Game_ShowStartScreen: a new game, a mantra or a file follows. A mantra
 * holds the Elixir only if it was charged; a file says if it is spent. */
static int title_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) return 0;
    memset(&charge, 0, sizeof charge);
    resume = (Resume){ 0 };
    talk = (Talk){ 0 };
    drawn_valid = false;
    return 0;
}

/* The Guru whose conversation begins with the action at `at`, or -1: a
 * temple Guru's script reaches SET_SPAWN_POINT (06 temple), the only scripts
 * that use it, before he speaks (Conflate's after a CHECK_FOR_ITEM); Eolis's
 * Guru sets none, so his first action. */
static int guru_begins(uint16_t at) {
    if (q(at) == ISCRIPT_SET_SPAWN_POINT && q((uint16_t)(at + 1)) < TEMPLES) return q((uint16_t)(at + 1));
    return at == EOLIS_GURU_FIRST ? 0 : -1;
}
/* Where his line goes: before the next action no other feature steers. A
 * temple Guru's SET_SPAWN_POINT must run as it is (Save files holds the
 * spawn point at it, and Guru travel meets him there), so the line goes
 * before the CHECK_UPDATE_PLAYER_TITLE that follows it in all seven; Eolis's
 * Guru's first action is his CHECK_FOR_ITEM, which no one steers. */
static uint16_t line_goes_before(uint16_t first) {
    return q(first) == ISCRIPT_SET_SPAWN_POINT ? (uint16_t)(first + 2) : first;
}

static uint32_t prg(unsigned bank, uint16_t addr) { return bank * 16384u + (addr - 0x8000u); }
/* The line into message $C3's slot, and Guru travel's SAY step that shows
 * it, as Block's tutorial and the quest log serve them: the same three step
 * bytes whoever serves them. */
static bool serve_line(int which) {
    static const uint8_t say[] = { ISCRIPT_SHOW_MESSAGE, SLOT_ID, ISCRIPT_END };
    uint8_t bytes[SLOT_LEN];
    if (which <= LINE_NONE || which >= LINES || !encode(guru_line[which], bytes)) return false;
    for (unsigned i = 0; i < sizeof say; ++i)
        if (!cyc_mod_set_prg_read_override(prg(12, (uint16_t)(SAY + i)), 0xFF, say[i])) return false;
    for (unsigned i = 0; i < SLOT_LEN; ++i)
        if (!cyc_mod_set_prg_read_override(prg(13, (uint16_t)(SLOT + i)), 0xFF, bytes[i])) return false;
    return true;
}
static uint16_t word(uint16_t addr) { return (uint16_t)(q(addr) | q((uint16_t)(addr + 1)) << 8); }
static void go(uint16_t script, uint8_t offset) {
    cyc_mod_poke(ISCRIPT_ADDR, (uint8_t)script);
    cyc_mod_poke(ISCRIPT_ADDR + 1, (uint8_t)(script >> 8));
    cyc_mod_poke(ISCRIPT_OFFSET, offset);
}
/* Say `which` now, as the script's next action: park the script, and point
 * it at the SAY step. Either line tells the hero. */
static void say(int which) {
    if (!serve_line(which)) {
        fprintf(stderr, "[recharge] cannot supply the Guru's line; the Gurus restore the Elixir silently\n");
        lines_failed = true;
        return;
    }
    talk = (Talk){ TALK_SAYING, (uint8_t)which, 0, word(ISCRIPT_ADDR), q(ISCRIPT_OFFSET) };
    go(SAY, 0);
    charge.elixir_told = 1;
    ++lines_said[which];
}

/* IScripts_InvokeNextAction: a Guru's conversation begins, so charge a spent
 * Elixir before he speaks, whatever he goes on to offer (the mantra, Save
 * files' choice, Guru travel) and however it ends, and choose his line. The
 * line goes before an action no other hook here steers (line_goes_before),
 * and the script comes back to it at the SAY step's END ($AE02), where Guru
 * travel, with no conversation of its own in this script, and Save files,
 * whose saved line cannot be up yet, leave it be. This hook runs last at the
 * address, so each of the others saw the Guru's own action first. B on the
 * line ends the script, as on any of his lines: the next action seen is then
 * another script's. */
static int guru_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) return 0;
    uint16_t at = (uint16_t)(word(ISCRIPT_ADDR) + q(ISCRIPT_OFFSET));
    Talk was = talk;
    talk = (Talk){ 0 };
    if (was.phase == TALK_SAYING && at == SAY + 2) {        /* his line has been read: back to his script */
        go(was.resume, was.offset);
        return 0;
    }
    if (was.phase == TALK_PENDING && at == was.at) {
        say(was.line);
        return 0;
    }
    if (guru_begins(at) < 0) return 0;
    int line = guru_says(&charge, q(SPECIAL_ITEMS) & BIT_ELIXIR);
    if (charge.elixir_spent) {
        cyc_mod_poke(SPECIAL_ITEMS, (uint8_t)(q(SPECIAL_ITEMS) | BIT_ELIXIR));
        charge.elixir_spent = 0;
        ++guru_recharges;
    }
    if (line == LINE_NONE || lines_failed) return 0;
    uint16_t before = line_goes_before(at);
    if (before == at) say(line);
    else talk = (Talk){ TALK_PENDING, (uint8_t)line, before, 0, 0 };
    return 0;
}

/* For Quick shops (fax_shops.c), which lists what the hero owns. */
bool fax_recharge_on(void) { return enabled; }
bool fax_recharge_elixir_spent(void) { return enabled && charge.elixir_spent; }

/* ---- the file in play (Save files, fax_save.h) ---- */

/* The fields: how many of the game's seconds until each timed item is ready
 * (0: ready; a run that has not ended counts as its whole cooldown, since
 * continuing spawns the hero with no timed item running), whether the
 * Elixir is spent ("spent", or no field), and whether a Guru has told the
 * hero he restores it ("yes", or no field). Whether it is charged is the
 * file's own special items. */
static const char *const FIELD[TIMED] = { "recharge.wingboots", "recharge.hourglass" };
#define FIELD_ELIXIR "recharge.elixir"
#define FIELD_TOLD "recharge.elixir_told"

static uint8_t until_ready(const Charge *c, int i, uint8_t title) {
    return c->running[i] ? timing(title)->cooldown[i] : c->cooldown[i];
}
static void on_save(FaxSaveEvent event) {
    if (!enabled) return;
    if (event == FAX_SAVE_NEW_GAME) {
        memset(&charge, 0, sizeof charge);
        resume = (Resume){ 0 };
    } else if (event == FAX_SAVE_CONTINUED) {
        /* Continuing spawns the hero, and Game_ClearTimedItems would make
         * both ready: spawn_hook puts these back instead. */
        resume.pending = 1;
        for (int i = 0; i < TIMED; ++i) {
            long v = fax_save_get_int(FIELD[i], 0);
            resume.cooldown[i] = (uint8_t)(v < 0 ? 0 : v > MOST_SECONDS ? MOST_SECONDS : v);
        }
        const char *e = fax_save_get(FIELD_ELIXIR), *t = fax_save_get(FIELD_TOLD);
        charge.elixir_spent = e && !strcmp(e, "spent");
        charge.elixir_told = t && !strcmp(t, "yes");
    } else {
        for (int i = 0; i < TIMED; ++i) fax_save_set_int(FIELD[i], until_ready(&charge, i, q(PLAYER_TITLE)));
        fax_save_set(FIELD_ELIXIR, charge.elixir_spent ? "spent" : NULL);
        fax_save_set(FIELD_TOLD, charge.elixir_told ? "yes" : NULL);
    }
}

/* ---- the save-state record ---- */

/* Version 2: [0] 2, [1] the Wing Boots running, [2] their cooldown, [3] the
 * Hour Glass running, [4] its cooldown, [5] the Elixir spent, [6] a continued
 * file's cooldowns waiting for the spawn, [7] and [8] those cooldowns, [9] a
 * Guru has told the hero, and the Guru's line: [10] its phase, [11] which,
 * [12-13] the action it goes before, [14-15] and [16] where the script goes
 * back to. Version 1 was [0]-[8], with nothing told and no line. */
enum { RECORD_VERSION = 2, RECORD = 17, RECORD_V1 = 9 };
static int state_get(uint8_t *buf, int cap) {
    if (cap < RECORD) return -1;
    const uint8_t rec[RECORD] = { RECORD_VERSION, charge.running[BOOTS], charge.cooldown[BOOTS],
                                  charge.running[GLASS], charge.cooldown[GLASS], charge.elixir_spent,
                                  resume.pending, resume.cooldown[BOOTS], resume.cooldown[GLASS],
                                  charge.elixir_told, talk.phase, talk.line, (uint8_t)talk.at,
                                  (uint8_t)(talk.at >> 8), (uint8_t)talk.resume, (uint8_t)(talk.resume >> 8),
                                  talk.offset };
    memcpy(buf, rec, sizeof rec);
    return RECORD;
}
static int record_validate(const uint8_t *buf, int len) {
    if (len == 0) return 1;
    bool v1 = len == RECORD_V1 && buf[0] == 1, v2 = len == RECORD && buf[0] == RECORD_VERSION;
    if (!v1 && !v2) return 0;
    if (buf[1] > 1 || buf[2] > MOST_SECONDS || buf[3] > 1 || buf[4] > MOST_SECONDS || buf[5] > 1 || buf[6] > 1 ||
        buf[7] > MOST_SECONDS || buf[8] > MOST_SECONDS)
        return 0;
    return v1 || (buf[9] <= 1 && buf[10] < TALK_PHASES && buf[11] < LINES && (buf[10] == TALK_IDLE) == (buf[11] == LINE_NONE));
}
/* Length 0 (a state from before the feature) means everything ready and
 * nothing spent. */
static int state_set(const uint8_t *buf, int len) {
    if (!record_validate(buf, len)) return 0;
    memset(&charge, 0, sizeof charge);
    resume = (Resume){ 0 };
    talk = (Talk){ 0 };
    if (len) {
        charge.running[BOOTS] = buf[1]; charge.cooldown[BOOTS] = buf[2];
        charge.running[GLASS] = buf[3]; charge.cooldown[GLASS] = buf[4];
        charge.elixir_spent = buf[5];
        resume = (Resume){ buf[6], { buf[7], buf[8] } };
    }
    if (len == RECORD) {
        charge.elixir_told = buf[9];
        talk = (Talk){ buf[10], buf[11], (uint16_t)(buf[12] | buf[13] << 8), (uint16_t)(buf[14] | buf[15] << 8),
                       buf[16] };
    }
    drawn_valid = false;                           /* the loaded picture has its own T: */
    return 1;
}

/* After the whole state is in: the line's overrides are not machine state, and
 * Guru travel's record may have put its own last line back in the slot. */
void fax_recharge_loaded(void) {
    if (!enabled || talk.phase != TALK_SAYING || lines_failed || serve_line(talk.line)) return;
    fprintf(stderr, "[recharge] cannot supply the Guru's line; the Gurus restore the Elixir silently\n");
    lines_failed = true;
}

/* ---- activation ---- */

static const char *const hook_ids[] = {
    "faxanadu.recharge.elixir", "faxanadu.recharge.keep", "faxanadu.recharge.use-boots",
    "faxanadu.recharge.flight", "faxanadu.recharge.use-glass", "faxanadu.recharge.spawn",
    "faxanadu.recharge.tick", "faxanadu.recharge.pick-up", "faxanadu.recharge.title", "faxanadu.recharge.guru" };
static void hooks(bool on) {
    for (unsigned i = 0; i < sizeof hook_ids / sizeof hook_ids[0]; ++i) nes_mod_set_function_hook_enabled(hook_ids[i], on);
}
static void deactivate(void) {
    enabled = false;
    memset(&charge, 0, sizeof charge);
    resume = (Resume){ 0 };
    talk = (Talk){ 0 };
    lines_failed = false;
    drawn_valid = false;
    flights = freezes = refused = kept = pickups = revives = guru_recharges = spawns = 0;
    memset(lines_said, 0, sizeof lines_said);
    hooks(false);
}
static void activate(void) {
    enabled = true;
    hooks(true);
    nes_mod_set_local_only("Recharging items", 1);
}

#ifdef CYC_WITH_SDL
static void recharge_state(int id, const char *line) {
    (void)line;
    char fields[900];
    Show s = hud_show(&charge, q(DURATION_WING_BOOTS), q(SELECTED_ITEM), q(INTERRUPT_COUNTER));
    int n = snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"boots_ready\":%s,\"boots_running\":%u,\"boots_cooldown\":%u,"
             "\"glass_ready\":%s,\"glass_running\":%u,\"glass_cooldown\":%u,"
             "\"elixir_spent\":%s,\"elixir_charged\":%s,\"flights\":%u,\"freezes\":%u,\"refused\":%u,"
             "\"kept\":%u,\"pickups\":%u,\"revives\":%u,\"guru_recharges\":%u,\"spawns\":%u,"
             "\"hud\":%d,\"hud_blank\":%s,\"hud_drawn\":%d,\"hud_drawn_blank\":%s",
             enabled ? "true" : "false", ready(&charge, BOOTS) ? "true" : "false", charge.running[BOOTS],
             charge.cooldown[BOOTS], ready(&charge, GLASS) ? "true" : "false", charge.running[GLASS],
             charge.cooldown[GLASS], charge.elixir_spent ? "true" : "false",
             q(SPECIAL_ITEMS) & BIT_ELIXIR ? "true" : "false", flights, freezes, refused, kept, pickups, revives,
             guru_recharges, spawns, s.value, s.blank ? "true" : "false", drawn_valid ? drawn.value : SHOW_GAME,
             drawn_valid && drawn.blank ? "true" : "false");
    /* The Guru's line: told, talk (0 none, 1 waiting, 2 showing), line (0 none, 1 restored, 2 can), how many of
     * each he has said, and whether the lines are off (their overrides could not be set). */
    snprintf(fields + n, sizeof fields - (size_t)n,
             ",\"elixir_told\":%s,\"talk\":%u,\"line\":%u,\"restored_lines\":%u,\"can_lines\":%u,\"lines_failed\":%s",
             charge.elixir_told ? "true" : "false", talk.phase, talk.line, lines_said[LINE_RESTORED],
             lines_said[LINE_CAN], lines_failed ? "true" : "false");
    cyc_tcp_ok(id, fields);
}
void fax_recharge_tcp_setup(void) {
    cyc_tcp_register("recharge_state",
                     "Recharging items: readiness, cooldowns, the Elixir's charge, what T: shows, counts",
                     recharge_state);
}
#else
void fax_recharge_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_recharge) {
    static const struct { uint16_t addr; NESModFunctionEntryCallback cb; } sites[] = {
        { PLAYER_USE_ELIXIR, elixir_hook }, { FAX_PLAYER_CLEAR_SELECTED_ITEM, keep_hook },
        { PLAYER_USE_WING_BOOTS, use_hook }, { WING_BOOTS_SET_FLIGHT, flight_hook },
        { PLAYER_USE_HOUR_GLASS, use_hook }, { GAME_CLEAR_TIMED_ITEMS, spawn_hook },
        { GAME_LOOP_COUNTDOWN, tick_hook }, { PLAYER_PICK_UP_ITEM, pick_up_hook },
        { GAME_SHOW_START_SCREEN, title_hook }, { ISCRIPTS_INVOKE_NEXT_ACTION, guru_hook } };
    for (unsigned i = 0; i < sizeof sites / sizeof sites[0]; ++i)
        if (!nes_mod_register_function_entry_plugin(hook_ids[i], sites[i].addr, sites[i].cb)) abort();
    if (!nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.recharge", activate) ||
        !nes_mod_register_savestate_hook("faxanadu.recharge", state_get, state_set) ||
        !nes_mod_register_savestate_validator("faxanadu.recharge", record_validate) || !fax_save_listen(on_save))
        abort();
}
