/* Recharging items. USA 57dd23d1, chipx86 byte-exact disassembly.
 *
 * The Wing Boots and the Hour Glass are kept after use, and both run on MP
 * (#124): each takes MP as it goes, and neither has a cooldown, so each can
 * be used again the moment its run ends.
 * - The Wing Boots fly while MP lasts. Each played frame of the flight
 *   counts toward the next MP, which goes every FRAMES_PER_MP[band] frames:
 *   2, 1.6, 1.33 and 1 MP a second (the game's, 64 frames) by title band, so
 *   a full bar of 80 flies 40, 50, 60 and 80 s. At 0 MP the flight ends with
 *   the game's own "The power of the Wing Boots is gone.". Down+B again
 *   ends it the same way: in the air with A held (flying, the game treats
 *   Down as a climb, which clears the jumping and falling flags its item use
 *   refuses on) or standing.
 * - The Hour Glass freezes for the game's own 15 s, and bosses still never
 *   freeze, but it costs no HP (the original halved it): the freeze takes an
 *   MP every GLASS_FRAMES_PER_MP frames, 40 MP for the whole 15 s, and ends
 *   at 15 s, at 0 MP, or on Down+B again, with the game's own "The power of
 *   the Hour Glass is gone.". On a boss's screen ScreenEvents_HandleBoss
 *   still cuts it (LimitItemDurations): the drain stops there, and the
 *   game's own second ends it, as in the original.
 * With 0 MP, Down+B on either does nothing but play the game's "invalid
 * choice" sound, as a spell he can't afford does nothing. MP comes from
 * kills (fax_mana.c), while either drains too, and a Guru fills it. Both at
 * once drain together, and each ends when MP runs out.
 * T: shows the selected item's seconds left while it runs: the flight's or
 * the freeze's, at the MP there is and what drains it, rounded up (a full
 * bar's 40 s shows 40). Otherwise it is what the original shows: the
 * flight's while the Wing Boots fly, whatever is selected, else 00 (#119).
 * Dying ends both (a respawn clears timed items); a Guru travel journey does
 * not. Finding a pair or an Hour Glass in the world while you own one adds
 * nothing: one is all he needs. The game's shops would sell a second
 * (Shop_GetPlayerHasSelectedItem refuses no Items you have); Quick shops
 * (fax_shops.c) lists one you have as owned instead, and ignores A on it
 * (fax_recharge_on).
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
 * The status bar shows whether the Elixir is charged, left of its M: and P:
 * bars (fax_charges.c, through fax_recharge_charge).
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
 * - Player_UseWingBoots / Player_UseHourGlass entry: one that runs is ended
 *   (at this frame's countdown: GameLoop_CheckUseCurrentItem comes before
 *   GameLoop_CountdownItems in Game_MainLoop); with 0 MP the use is refused
 *   with the sound. The routine was entered from
 *   GameLoop_CheckUseCurrentItem's jump table, so returning goes back to
 *   the main loop, as its RTS would.
 * - Player_ClearSelectedItem from those two routines: skipped, so the item
 *   stays selected (the use-up list in fax_items.h, shared with reusable tools).
 * - Player_UseWingBoots' STA DurationWingBoots: the flight begins, and A,
 *   which UI_DrawTimeValue draws next, is its seconds, not
 *   TITLE_TO_WINGBOOTS_DURATION's.
 * - Player_UseHourGlass's LSR Player_HP_U: the freeze begins, and the
 *   halving is jumped over, to the JSR UI_DrawPlayerHP after it (the cost,
 *   in one place: cost_hook).
 * - Game_DecHourGlassDuration's and Game_DecWingBootsDuration's LDA
 *   InterruptCounter, which each reaches once a main-loop frame while its
 *   item runs: that frame of the run. MP goes on schedule, the bar drawn by
 *   the game's own Player_SetMP in a committed call (when the PPU buffer
 *   has room, else at the first frame that has), the duration is set to the
 *   seconds left, and the game's second is skipped (an RTS). At the run's end the site jumps to the routine's DEC with the
 *   duration at 0, so it wraps to $FF and the game ends the item as it
 *   always did, with its line (and the Hour Glass, the area's music).
 *   Another value of DurationHourGlass than this module set is the game's
 *   own LimitItemDurations: the freeze is left to the game's second.
 * - GameLoop_CountdownItems, once a main-loop frame before the game's own
 *   countdowns: T: as above, queued through the PPU buffer as
 *   UI_DrawTimeValue writes it, and a bar still to draw.
 * - Player_UseElixir: the Elixir is spent. IScripts_InvokeNextAction, at the
 *   first action of a Guru's script: a spent Elixir is charged again, HP and
 *   MP are filled (fax_mana_guru, MP from kills'), and his line is chosen. It is shown before his next action that no other feature
 *   steers (a temple Guru's CHECK_UPDATE_PLAYER_TITLE, right after
 *   SET_SPAWN_POINT; Eolis's Guru's first, CHECK_FOR_ITEM): the script goes
 *   to the SAY step, and at its END comes back where it was.
 * - Player_PickUpItem: a duplicate is not added.
 * - Game_ClearTimedItems (every spawn but a Guru journey): both runs end.
 * - Game_ShowStartScreen (power-on or reset, before a new game or a file):
 *   everything forgotten.
 * Hooks do nothing inside Seamless's isolated simulation of other rooms. The
 * state is the save-state record, and Save files keeps the Elixir's charge
 * and whether a Guru has told the hero in the file in play (on_save):
 * written whenever the file is, read back when it is continued, reset by a
 * new game. Talking to any Guru charges the Elixir before he speaks, so
 * whichever of Save files' menu options is picked, or none. See
 * docs/GAME_INTERNALS.md. */
#include "fax_recharge.h"
#include "fax_items.h"
#include "fax_mana.h"
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
#define BOOTS_SECOND            0xC5A8u  /* Game_DecWingBootsDuration, running, after it sets Player_StatusFlag's $80:
                                            LDA InterruptCounter, AND #$3F, BNE @_return (not the game's second) */
#define BOOTS_SECOND_DEC        0xC5AEu  /* DEC DurationWingBoots, LDA, BMI @_wingBootsDone ("... is gone"), else
                                            JMP UI_DrawTimeValue */
#define PLAYER_USE_HOUR_GLASS   0xC5C8u  /* LDA #$82, JSR MMC1_LoadBankAndJump: the "used Hour Glass" script */
#define HOUR_GLASS_COST         0xC5D8u  /* LSR Player_HP_U, ROR Player_HP_L: the original's half of HP */
#define HOUR_GLASS_DRAW_HP      0xC5DEu  /* JSR UI_DrawPlayerHP, then LDA #$0F, STA DurationHourGlass, the music */
#define GLASS_SECOND            0xC5F0u  /* Game_DecHourGlassDuration, running: LDA InterruptCounter, AND #$3F,
                                            BNE @_return */
#define GLASS_SECOND_DEC        0xC5F6u  /* DEC DurationHourGlass, BPL @_return, else "... is gone" and
                                            Areas_DefaultMusic */
#define GAME_CLEAR_TIMED_ITEMS  0xC6AFu  /* LDA #$FF, STA DurationGlove, ...: from Game_Start and Player_Spawn */
#define GAME_LOOP_COUNTDOWN     0xC88Fu  /* GameLoop_CountdownItems: JSR Game_DecHourGlassDuration, ... */
#define PLAYER_PICK_UP_ITEM     0xC8CDu  /* LDX NumberOfItems, CPX #$08, BCS: A is the item */
#define SOUND_PLAY_EFFECT       0xD0E4u  /* STA Temp_SoundIDToPlay, ...: queues sound A, keeps X and Y */
#define PLAYER_SET_MP           0xFA85u  /* CMP #$51, BCC, LDA #$50, STA Player_MP, LDY #$01: draws the MP bar */
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
#define PLAYER_MP               0x039Au  /* at most 80 */
#define ITEM_INVENTORY          0x03ADu
#define SELECTED_ITEM           0x03C1u
#define NUMBER_OF_ITEMS         0x03C6u
#define DURATION_WING_BOOTS     0x0429u  /* $FF when not running */
#define DURATION_HOUR_GLASS     0x042Au
#define SPECIAL_ITEMS           0x042Cu
#define PLAYER_TITLE            0x0437u  /* 0-15 */
#define BAR_WIDTH               0x04EEu  /* UI_MPAndHPBarWidth: 10 tiles, set by UI_DrawHUDSprites */
#define PPU_BUFFER              0x0500u  /* 256 bytes, circular: [length][addr hi][addr lo][tiles] */
/* Values */
#define BIT_ELIXIR              0x08u
#define SOUND_INVALID_CHOICE    0x0Du
#define HUD_TIME                0x2058u  /* UI_DrawTimeValue's two digits */
#define PPU_BUFFER_ROOM         0x24u    /* PPUBuffer_HasCapacity: at least 36 bytes free, or empty */
#define BAR_COMMAND             3u       /* a PPU buffer command's length and address */
#define MAX_ITEMS               8
#define ISCRIPT_SET_SPAWN_POINT 0x06u    /* 06 temple: each temple Guru's first action (Conflate's after a
                                            CHECK_FOR_ITEM); CHECK_UPDATE_PLAYER_TITLE follows it */
#define ISCRIPT_SHOW_MESSAGE    0x03u
#define ISCRIPT_END             0x00u
#define TEMPLES                 8
enum { SLOT_LEN = FAX_TRAVEL_SLOT_LEN, SLOT_ID = FAX_TRAVEL_SLOT_ID };

enum { BOOTS = FAX_RECHARGE_BOOTS, GLASS = FAX_RECHARGE_GLASS, TIMED };   /* the two timed items */
enum { BANDS = 4 };             /* title bands: PlayerTitle / 4 */

/* What the two cost, decided in #124 as starting points for Cameron's
 * playtest: retuning means changing these and nothing else. Times are in
 * played frames, those a pass of the main loop counts down (60 a second, no
 * pause, box or menu), and the game's second, what T: counts, is 64 of
 * them; whole frames an MP, so every cost is exact. The Wing Boots take an
 * MP every FRAMES_PER_MP frames, by the title band he holds as it goes
 * (PlayerTitle / 4): 80 MP flies 40, 50, 60 and 80 s, so higher titles
 * still fly longer. The original flew 40, 30, 20 and 10 s by band, #27's
 * recharging pair 30 to 60. The Hour Glass freezes for FREEZE_SECONDS, the
 * game's own 15 (Player_UseHourGlass, LDA #$0F), and takes an MP every
 * GLASS_FRAMES_PER_MP frames, at every title: 40 MP for a whole freeze (8
 * every 3 s), where the original took half his HP at use. */
enum { SECOND = 64 };
static const uint8_t FRAMES_PER_MP[BANDS] = {
    32,     /* Novice to Fighter: 2 MP a second, 40 s on a full bar */
    40,     /* Adept to Warrior: 1.6 MP a second, 50 s */
    48,     /* Swordman to Myrmidon: 1.33 MP a second, 60 s */
    64,     /* Champion to Lord: 1 MP a second, 80 s */
};
enum { GLASS_FRAMES_PER_MP = 24 };   /* 2.67 MP a second: 40 MP for the 15 s */
enum { FREEZE_SECONDS = 15, FREEZE = FREEZE_SECONDS * SECOND };
enum { MOST_SECONDS = 99 };     /* T: has two digits */

static const uint8_t ITEM_ID[TIMED] = { FAX_ITEM_WING_BOOTS, FAX_ITEM_HOUR_GLASS };

/* A flight: running, and the played frames until it takes the next MP. */
typedef struct {
    uint8_t running;           /* begun, and neither out of MP nor ended */
    uint8_t next;              /* played frames until the next MP goes, 1-64 */
    uint8_t land;              /* used again: it ends at this frame's countdown */
} Flight;
/* A freeze: how far it has run, and the played frames until it takes the
 * next MP. */
typedef struct {
    uint8_t running;           /* begun, and neither over, out of MP nor ended */
    uint8_t land;              /* used again: it ends at this frame's countdown */
    uint16_t played;           /* played frames of it so far, 0-FREEZE */
    uint8_t next;              /* played frames until the next MP goes, 1-GLASS_FRAMES_PER_MP */
    uint8_t shown;             /* DurationHourGlass as this module last set it: another value is the game's */
} Freeze;
/* The state: everything a save state or a save slot must keep. */
typedef struct {
    Flight flight;
    Freeze freeze;
    uint8_t bar;               /* MP taken, and its bar still to draw (the PPU buffer was full) */
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

static bool enabled;
static Charge charge;
static unsigned drawn;             /* what this module last queued on T: */
static bool drawn_valid;
static Talk talk;
static bool lines_failed;          /* the line could not be supplied: the Gurus charge it silently */
static bool legacy;                /* the record loaded was from before #124: adopt its runs once RAM is in */
static unsigned flights, freezes, refused, kept, pickups, revives, guru_recharges, spawns, cuts;   /* for TCP */
static unsigned landings[TIMED], lines_said[LINES];

/* ---- the rules, on plain values ---- */

static uint8_t frames_per_mp(uint8_t title) { return FRAMES_PER_MP[(title >> 2) & (BANDS - 1)]; }
static bool ready(const Charge *c, int i) { return i == BOOTS ? !c->flight.running : !c->freeze.running; }

/* Seconds for T:, rounded up: 40 while up to 40 s are left, 1 in the last. */
static unsigned seconds(unsigned frames) {
    unsigned s = (frames + SECOND - 1) / SECOND;
    return s > MOST_SECONDS ? MOST_SECONDS : s;
}

/* A flight begins: the first MP goes a whole step after it. */
static Flight take_off(uint8_t title) { return (Flight){ 1, frames_per_mp(title), 0 }; }
/* A freeze begins, the same. */
static Freeze freeze_begin(void) { return (Freeze){ 1, 0, 0, GLASS_FRAMES_PER_MP, FREEZE_SECONDS }; }

/* Played frames until `mp` MP runs out, at the drains running: the flight's
 * (an MP at `next`, then every FRAMES_PER_MP) and the freeze's (the same,
 * every GLASS_FRAMES_PER_MP, as long as it lasts: its last MP goes on its
 * last frame). NEVER if nothing drains it to 0. */
enum { NEVER = 0xFFFFu };
static unsigned to_empty(const Charge *c, uint8_t mp, uint8_t title) {
    const Flight *f = &c->flight;
    const Freeze *z = &c->freeze;
    unsigned fly_at = f->running ? f->next : NEVER, step = frames_per_mp(title);
    unsigned freeze_end = z->running && z->played < FREEZE ? (unsigned)(FREEZE - z->played) : 0u;
    unsigned glass_at = z->running && z->next <= freeze_end ? z->next : NEVER;
    for (unsigned left = mp; left; ) {
        unsigned t = fly_at < glass_at ? fly_at : glass_at;
        if (t == NEVER) return NEVER;
        if (t == fly_at) { --left; fly_at += step; }
        if (t == glass_at && left) { --left; glass_at += GLASS_FRAMES_PER_MP; if (glass_at > freeze_end) glass_at = NEVER; }
        if (!left) return t;
    }
    return 0;
}
/* Played frames left in the flight: until MP runs out. */
static unsigned flight_left(const Charge *c, uint8_t mp, uint8_t title) {
    if (!c->flight.running) return 0;
    unsigned t = to_empty(c, mp, title);
    return t == NEVER ? 0u : t;
}
/* Played frames left in the freeze: until its 15 s are up, or MP runs out. */
static unsigned freeze_left(const Charge *c, uint8_t mp, uint8_t title) {
    const Freeze *z = &c->freeze;
    if (!z->running || z->played >= FREEZE) return 0;
    unsigned t = to_empty(c, mp, title), end = (unsigned)(FREEZE - z->played);
    return t < end ? t : end;
}
/* One played frame of a flight, `*mp` its MP: one goes on schedule. True when
 * the flight ends now: used again, or out of MP. */
static bool fly(Flight *f, uint8_t *mp, uint8_t title) {
    if (f->land || !*mp) return true;
    if (f->next > 1) { --f->next; return false; }
    f->next = frames_per_mp(title);
    return --*mp == 0;
}
/* One played frame of a freeze, the same: true when it ends now, used
 * again, out of MP, or its time up (its last MP goes on its last frame). */
static bool freeze_frame(Freeze *z, uint8_t *mp) {
    if (z->land || !*mp) return true;
    ++z->played;
    if (z->next > 1) --z->next;
    else { z->next = GLASS_FRAMES_PER_MP; --*mp; }
    return !*mp || z->played >= FREEZE;
}

/* What T: shows: the selected item's seconds while it runs. Else what the
 * original shows: the flight's while the Wing Boots fly, whatever is
 * selected (the stock game draws them whatever is selected), else 00. The
 * Hour Glass unselected shows nothing, as the original's never does. */
static unsigned hud_value(const Charge *c, uint8_t selected, uint8_t mp, uint8_t title) {
    if (selected == FAX_ITEM_HOUR_GLASS && c->freeze.running) return seconds(freeze_left(c, mp, title));
    return c->flight.running ? seconds(flight_left(c, mp, title)) : 0u;
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
static bool queue_time(unsigned v) {
    uint8_t r = q(PPU_BUFFER_READ), w = q(PPU_BUFFER_WRITE), room = (uint8_t)(r - w);
    if (room && room < PPU_BUFFER_ROOM) return false;
    if (v > MOST_SECONDS) v = MOST_SECONDS;
    uint8_t bytes[5] = { 2, HUD_TIME >> 8, HUD_TIME & 0xFF, (uint8_t)('0' + v / 10), (uint8_t)('0' + v % 10) };
    for (unsigned k = 0; k < sizeof bytes; ++k) cyc_mod_poke((uint16_t)(PPU_BUFFER + (uint8_t)(w + k)), bytes[k]);
    cyc_mod_poke(PPU_BUFFER_WRITE, (uint8_t)(w + sizeof bytes));
    return true;
}
/* T: as hud_value says, queued when it changes, and with `refresh` every 16
 * frames while it is not 0, since the game draws 00 there whenever it draws
 * the whole HUD (UI_DrawHUD). */
static void draw_hud(bool refresh) {
    unsigned v = hud_value(&charge, q(SELECTED_ITEM), q(PLAYER_MP), q(PLAYER_TITLE));
    bool changed = !drawn_valid || v != drawn;
    refresh = refresh && v > 0 && (q(INTERRUPT_COUNTER) & 0x0F) == 0;
    if ((changed || refresh) && queue_time(v)) { drawn = v; drawn_valid = true; }
}

/* The MP bar. Player_SetMP draws it as two PPU buffer commands that wait
 * for room first, which a committed call cannot do (no NMI runs): so only
 * when the buffer has room for both at once, the test MP from kills makes
 * (fax_mana.c): empty, or the room PPUBuffer_HasCapacity asks for after the
 * first command. */
static bool bar_fits(void) {
    uint8_t room = (uint8_t)(q(PPU_BUFFER_READ) - q(PPU_BUFFER_WRITE));
    return room == 0 || room >= PPU_BUFFER_ROOM + BAR_COMMAND + q(BAR_WIDTH) + 1;
}
/* `routine` with A = `a`, committed; the CPU's registers as they were. */
static bool call(uint16_t routine, uint8_t a) {
    CycModRegs regs, in;
    cyc_mod_regs(&regs);
    in = regs;
    in.a = a;
    bool ok = cyc_mod_call_commit(routine, &in);
    cyc_mod_set_regs(&regs);
    return ok;
}
static void draw_bar(void) {
    if (charge.bar && bar_fits() && call(PLAYER_SET_MP, q(PLAYER_MP))) charge.bar = 0;
}
/* MP taken: in RAM at once, so every cost is on time, and the bar as soon as
 * it fits. */
static void set_mp(uint8_t mp) {
    cyc_mod_poke(PLAYER_MP, mp);
    charge.bar = 1;
    draw_bar();
}
/* Make the hook's return go on at `to`: push to - 1 as a JSR would (an RTS
 * adds one), and handle the call. */
static int return_via(uint16_t to) {
    CycModRegs regs;
    cyc_mod_regs(&regs);
    uint16_t r = (uint16_t)(to - 1);
    cyc_mod_poke((uint16_t)(0x100u | regs.s), (uint8_t)(r >> 8)); regs.s = (uint8_t)(regs.s - 1);
    cyc_mod_poke((uint16_t)(0x100u | regs.s), (uint8_t)r);        regs.s = (uint8_t)(regs.s - 1);
    cyc_mod_set_regs(&regs);
    return 1;
}

/* GameLoop_CountdownItems: once a main-loop frame, before the game's own
 * countdowns (and so before this frame of a run). */
static int tick_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) return 0;
    if (charge.flight.running && q(DURATION_WING_BOOTS) >= 0x80) charge.flight = (Flight){ 0 };   /* gone already */
    if (charge.freeze.running && q(DURATION_HOUR_GLASS) >= 0x80) charge.freeze = (Freeze){ 0 };
    if (charge.elixir_spent && (q(SPECIAL_ITEMS) & BIT_ELIXIR)) charge.elixir_spent = 0;   /* found */
    draw_bar();
    draw_hud(true);
    return 0;
}

/* Player_UseWingBoots, Player_UseHourGlass: one that runs ends, at this
 * frame's countdown; with no MP either is refused with the game's "invalid
 * choice" sound. The routine was entered from GameLoop_CheckUseCurrentItem's
 * jump table, so returning goes back to the main loop, as its RTS would. */
static int use_hook(uint16_t addr) {
    if (cyc_mod_isolated()) return 0;
    uint8_t *land = addr == PLAYER_USE_WING_BOOTS ? &charge.flight.land : &charge.freeze.land;
    if (addr == PLAYER_USE_WING_BOOTS ? charge.flight.running : charge.freeze.running) { *land = 1; return 1; }
    if (q(PLAYER_MP)) return 0;
    call(SOUND_PLAY_EFFECT, SOUND_INVALID_CHOICE);
    ++refused;
    return 1;
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

/* Player_UseWingBoots' STA DurationWingBoots: the flight begins. A, which the
 * game stores and UI_DrawTimeValue draws, is its seconds on the MP it has. */
static int flight_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) return 0;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    uint8_t title = q(PLAYER_TITLE);
    charge.flight = take_off(title);
    regs.a = (uint8_t)seconds(flight_left(&charge, q(PLAYER_MP), title));
    cyc_mod_set_regs(&regs);
    ++flights;
    return 0;
}

/* Player_UseHourGlass's LSR Player_HP_U, the original's cost: the freeze
 * begins, and costs no HP. It goes on past the halving, at the JSR
 * UI_DrawPlayerHP, which draws HP as it is; the routine then sets the game's
 * 15 s and the music. Its cost is MP, taken as it runs (glass_second_hook). */
static int cost_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) return 0;
    charge.freeze = freeze_begin();
    ++freezes;
    return return_via(HOUR_GLASS_DRAW_HP);
}

/* Game_DecHourGlassDuration's LDA InterruptCounter, once a main-loop frame
 * while DurationHourGlass runs: a frame of the freeze. A value this module
 * did not set is LimitItemDurations' 0 on a boss's screen: the drain stops,
 * and the game's own second ends it. */
static int glass_second_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated() || !charge.freeze.running) return 0;
    Freeze *z = &charge.freeze;
    if (q(DURATION_HOUR_GLASS) != z->shown) {
        *z = (Freeze){ 0 };
        ++cuts;
        draw_hud(false);
        return 0;
    }
    uint8_t was = q(PLAYER_MP), mp = was;
    bool ends = freeze_frame(z, &mp);
    if (mp != was) set_mp(mp);
    if (ends) {
        if (z->land) ++landings[GLASS];
        *z = (Freeze){ 0 };
        cyc_mod_poke(DURATION_HOUR_GLASS, 0);
        draw_hud(false);
        return return_via(GLASS_SECOND_DEC);   /* 0 wraps: "The power of the Hour Glass is gone." */
    }
    z->shown = (uint8_t)seconds(freeze_left(&charge, mp, q(PLAYER_TITLE)));
    cyc_mod_poke(DURATION_HOUR_GLASS, z->shown);
    draw_hud(false);
    return 1;                                  /* not the game's second: RTS */
}

/* Game_DecWingBootsDuration's LDA InterruptCounter, once a main-loop frame
 * while DurationWingBoots runs (Player_StatusFlag's $80 just set): a frame of
 * the flight. */
static int boots_second_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated() || !charge.flight.running) return 0;
    uint8_t title = q(PLAYER_TITLE), was = q(PLAYER_MP), mp = was;
    bool ends = fly(&charge.flight, &mp, title);
    if (mp != was) set_mp(mp);
    if (ends) {
        if (charge.flight.land) ++landings[BOOTS];
        charge.flight = (Flight){ 0 };
        cyc_mod_poke(DURATION_WING_BOOTS, 0);
        draw_hud(false);
        return return_via(BOOTS_SECOND_DEC);   /* 0 wraps: "The power of the Wing Boots is gone." */
    }
    cyc_mod_poke(DURATION_WING_BOOTS, (uint8_t)seconds(flight_left(&charge, mp, title)));
    draw_hud(false);
    return 1;                                  /* not the game's second: RTS */
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
 * the hero owns already is not added: one is all he needs. Returning goes
 * back to Player_HandleTouchItem, which removes the sprite. */
static int pick_up_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) return 0;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    if (timed_index(regs.a) < 0 || !owns(regs.a)) return 0;
    ++pickups;
    return 1;
}

/* Game_ClearTimedItems: a respawn, a new game, or continuing (a mantra or a
 * file): both runs end, as the game sets their durations to $FF. */
static int spawn_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) return 0;
    charge.flight = (Flight){ 0 };
    charge.freeze = (Freeze){ 0 };
    ++spawns;
    return 0;
}

/* Game_ShowStartScreen: a new game, a mantra or a file follows. A mantra
 * holds the Elixir only if it was charged; a file says if it is spent. */
static int title_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) return 0;
    memset(&charge, 0, sizeof charge);
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

/* IScripts_InvokeNextAction: a Guru's conversation begins, so fill HP and MP
 * (MP from kills, fax_mana.c) and charge a spent Elixir before he speaks, whatever he goes on to offer (the mantra, Save
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
    fax_mana_guru();
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

/* For the status bar (fax_charges.c). A spent Elixir found again has its bit
 * set before tick_hook forgets that it was spent, so the bit decides first. */
static FaxCharge charge_of(const Charge *c, int item, bool owned, bool elixir_bit) {
    if (item == FAX_RECHARGE_ELIXIR) return elixir_bit ? FAX_CHARGE_READY : c->elixir_spent ? FAX_CHARGE_USED : FAX_CHARGE_NONE;
    if (!owned) return FAX_CHARGE_NONE;
    return ready(c, item) ? FAX_CHARGE_READY : FAX_CHARGE_USED;
}
FaxCharge fax_recharge_charge(int item) {
    if (!enabled || item < 0 || item >= FAX_RECHARGE_ITEMS) return FAX_CHARGE_NONE;
    bool owned = item < TIMED && owns(ITEM_ID[item]);
    return charge_of(&charge, item, owned, q(SPECIAL_ITEMS) & BIT_ELIXIR);
}

/* ---- the file in play (Save files, fax_save.h) ---- */

/* The fields: whether the Elixir is spent ("spent", or no field), and
 * whether a Guru has told the hero he restores it ("yes", or no field).
 * Whether it is charged is the file's own special items. Continuing spawns
 * the hero with no timed item running, so nothing of the Wing Boots or the
 * Hour Glass is kept: #27's cooldowns (OLD_FIELD) are read no more, and the
 * next write drops them from the file. */
static const char *const OLD_FIELD[TIMED] = { "recharge.wingboots", "recharge.hourglass" };
#define FIELD_ELIXIR "recharge.elixir"
#define FIELD_TOLD "recharge.elixir_told"

static void on_save(FaxSaveEvent event) {
    if (!enabled) return;
    if (event == FAX_SAVE_NEW_GAME) {
        memset(&charge, 0, sizeof charge);
    } else if (event == FAX_SAVE_CONTINUED) {
        const char *e = fax_save_get(FIELD_ELIXIR), *t = fax_save_get(FIELD_TOLD);
        charge.elixir_spent = e && !strcmp(e, "spent");
        charge.elixir_told = t && !strcmp(t, "yes");
    } else {
        for (int i = 0; i < TIMED; ++i) fax_save_set(OLD_FIELD[i], NULL);
        fax_save_set(FIELD_ELIXIR, charge.elixir_spent ? "spent" : NULL);
        fax_save_set(FIELD_TOLD, charge.elixir_told ? "yes" : NULL);
    }
}

/* ---- the save-state record ---- */

/* Version 3 (#124): [0] 3, the flight: [1] running, [2] frames to the next
 * MP, [3] ending; the freeze: [4] running, [5] ending, [6-7] its frames so
 * far, [8] frames to its next MP, [9] DurationHourGlass as set; [10] the MP
 * bar still to draw; [11] the Elixir spent, [12] a Guru has told the hero,
 * and the Guru's line: [13] its phase, [14] which, [15-16] the action it goes
 * before, [17-18] and [19] where the script goes back to. Version 2, before
 * #124: [1] the Wing Boots running, [2] their cooldown, [3] the Hour Glass
 * running, [4] its cooldown, [5] the Elixir spent, [6] a continued file's
 * cooldowns waiting for the spawn, [7] and [8] those cooldowns, then [9]-[16]
 * as [12]-[19] here. Version 1 was version 2's [0]-[8]. The cooldowns are
 * dropped, and a run going on a timer goes on as this module runs one
 * (adopt). */
enum { RECORD_VERSION = 3, RECORD = 20, RECORD_V2 = 17, RECORD_V1 = 9, OLD_MOST_SECONDS = 99 };
static int state_get(uint8_t *buf, int cap) {
    if (cap < RECORD) return -1;
    const Flight *f = &charge.flight;
    const Freeze *z = &charge.freeze;
    const uint8_t rec[RECORD] = { RECORD_VERSION, f->running, f->next, f->land, z->running, z->land,
                                  (uint8_t)z->played, (uint8_t)(z->played >> 8), z->next, z->shown, charge.bar,
                                  charge.elixir_spent, charge.elixir_told, talk.phase, talk.line, (uint8_t)talk.at,
                                  (uint8_t)(talk.at >> 8), (uint8_t)talk.resume, (uint8_t)(talk.resume >> 8),
                                  talk.offset };
    memcpy(buf, rec, sizeof rec);
    return RECORD;
}
static bool talk_valid(const uint8_t *t) {   /* told, phase, line */
    return t[0] <= 1 && t[1] < TALK_PHASES && t[2] < LINES && (t[1] == TALK_IDLE) == (t[2] == LINE_NONE);
}
static int record_validate(const uint8_t *buf, int len) {
    if (len == 0) return 1;
    if (len == RECORD && buf[0] == RECORD_VERSION) {
        unsigned played = buf[6] | buf[7] << 8;
        return buf[1] <= 1 && buf[2] <= SECOND && (!buf[1] || buf[2] >= 1) && buf[3] <= 1 && buf[4] <= 1 &&
               buf[5] <= 1 && played <= FREEZE && buf[8] <= GLASS_FRAMES_PER_MP && (!buf[4] || buf[8] >= 1) &&
               buf[9] <= FREEZE_SECONDS && buf[10] <= 1 && buf[11] <= 1 && talk_valid(buf + 12);
    }
    bool v1 = len == RECORD_V1 && buf[0] == 1, v2 = len == RECORD_V2 && buf[0] == 2;
    if (!v1 && !v2) return 0;
    if (buf[1] > 1 || buf[2] > OLD_MOST_SECONDS || buf[3] > 1 || buf[4] > OLD_MOST_SECONDS || buf[5] > 1 ||
        buf[6] > 1 || buf[7] > OLD_MOST_SECONDS || buf[8] > OLD_MOST_SECONDS)
        return 0;
    return v1 || talk_valid(buf + 9);
}
/* Length 0 (a state from before the feature) means nothing running and
 * nothing spent. */
static int state_set(const uint8_t *buf, int len) {
    if (!record_validate(buf, len)) return 0;
    memset(&charge, 0, sizeof charge);
    talk = (Talk){ 0 };
    legacy = false;
    const uint8_t *told = NULL;
    if (len == RECORD) {
        charge.flight = (Flight){ buf[1], buf[2], buf[3] };
        charge.freeze = (Freeze){ buf[4], buf[5], (uint16_t)(buf[6] | buf[7] << 8), buf[8], buf[9] };
        charge.bar = buf[10];
        charge.elixir_spent = buf[11];
        told = buf + 12;
    } else if (len) {
        charge.flight.running = buf[1];
        charge.freeze.running = buf[3];
        charge.elixir_spent = buf[5];
        legacy = buf[1] || buf[3];
        if (len == RECORD_V2) told = buf + 9;
    }
    if (told) {
        charge.elixir_told = told[0];
        talk = (Talk){ told[1], told[2], (uint16_t)(told[3] | told[4] << 8), (uint16_t)(told[5] | told[6] << 8),
                       told[7] };
    }
    drawn_valid = false;                           /* the loaded picture has its own T: */
    return 1;
}

/* A run from a record before #124, on the game's timer, now that RAM is in:
 * a flight goes on from the MP it has, and a freeze for the seconds its
 * DurationHourGlass has left, each now taking MP. One the game had already
 * ended is over. */
static void adopt(void) {
    uint8_t boots = q(DURATION_WING_BOOTS), glass = q(DURATION_HOUR_GLASS);
    if (charge.flight.running) charge.flight = boots < 0x80 ? take_off(q(PLAYER_TITLE)) : (Flight){ 0 };
    if (!charge.freeze.running) return;
    if (glass >= 0x80) { charge.freeze = (Freeze){ 0 }; return; }
    unsigned left = glass > FREEZE_SECONDS ? FREEZE_SECONDS * SECOND : glass ? glass * SECOND : 1u;
    charge.freeze = (Freeze){ 1, 0, (uint16_t)(FREEZE - left), GLASS_FRAMES_PER_MP,
                              glass > FREEZE_SECONDS ? FREEZE_SECONDS : glass };
    cyc_mod_poke(DURATION_HOUR_GLASS, charge.freeze.shown);
}

/* After the whole state is in: a record from before #124 adopts its runs;
 * the line's overrides are not machine state, and Guru travel's record may
 * have put its own last line back in the slot. */
void fax_recharge_loaded(void) {
    if (!enabled) return;
    if (legacy) { legacy = false; adopt(); }
    if (talk.phase != TALK_SAYING || lines_failed || serve_line(talk.line)) return;
    fprintf(stderr, "[recharge] cannot supply the Guru's line; the Gurus restore the Elixir silently\n");
    lines_failed = true;
}

/* ---- activation ---- */

static const char *const hook_ids[] = {
    "faxanadu.recharge.elixir", "faxanadu.recharge.keep", "faxanadu.recharge.use-boots",
    "faxanadu.recharge.flight", "faxanadu.recharge.boots-second", "faxanadu.recharge.use-glass",
    "faxanadu.recharge.glass-cost", "faxanadu.recharge.glass-second", "faxanadu.recharge.spawn",
    "faxanadu.recharge.tick", "faxanadu.recharge.pick-up", "faxanadu.recharge.title", "faxanadu.recharge.guru" };
static void hooks(bool on) {
    for (unsigned i = 0; i < sizeof hook_ids / sizeof hook_ids[0]; ++i) nes_mod_set_function_hook_enabled(hook_ids[i], on);
}
static void deactivate(void) {
    enabled = false;
    memset(&charge, 0, sizeof charge);
    talk = (Talk){ 0 };
    lines_failed = legacy = false;
    drawn_valid = false;
    flights = freezes = refused = kept = pickups = revives = guru_recharges = spawns = cuts = 0;
    memset(landings, 0, sizeof landings);
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
    char fields[1400];
    const Flight *f = &charge.flight;
    const Freeze *z = &charge.freeze;
    uint8_t mp = q(PLAYER_MP), title = q(PLAYER_TITLE);
    unsigned left = flight_left(&charge, mp, title), frozen = freeze_left(&charge, mp, title);
    int n = snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"boots_ready\":%s,\"boots_running\":%u,\"glass_ready\":%s,\"glass_running\":%u,"
             "\"flight_next\":%u,\"flight_left\":%u,\"flight_seconds\":%u,\"frames_per_mp\":%u,"
             "\"freeze_played\":%u,\"freeze_next\":%u,\"freeze_left\":%u,\"freeze_seconds\":%u,"
             "\"glass_frames_per_mp\":%u,\"glass_shown\":%u,\"bar\":%u,\"flights\":%u,\"freezes\":%u,"
             "\"refused\":%u,\"boots_landed\":%u,\"glass_landed\":%u,\"cuts\":%u,\"kept\":%u,\"pickups\":%u,"
             "\"revives\":%u,\"guru_recharges\":%u,\"spawns\":%u,\"hud\":%u,\"hud_drawn\":%d",
             enabled ? "true" : "false", ready(&charge, BOOTS) ? "true" : "false", f->running,
             ready(&charge, GLASS) ? "true" : "false", z->running, f->next, left, seconds(left), frames_per_mp(title),
             z->played, z->next, frozen, seconds(frozen), (unsigned)GLASS_FRAMES_PER_MP, z->shown, charge.bar, flights,
             freezes, refused, landings[BOOTS], landings[GLASS], cuts, kept, pickups, revives, guru_recharges, spawns,
             hud_value(&charge, q(SELECTED_ITEM), mp, title), drawn_valid ? (int)drawn : -1);
    /* The Elixir: spent, charged; the Guru's line: told, talk (0 none, 1 waiting, 2 showing), line (0 none, 1
     * restored, 2 can), how many of each he has said, and whether the lines are off (their overrides could not
     * be set). */
    snprintf(fields + n, sizeof fields - (size_t)n,
             ",\"elixir_spent\":%s,\"elixir_charged\":%s,\"elixir_told\":%s,\"talk\":%u,\"line\":%u,"
             "\"restored_lines\":%u,\"can_lines\":%u,\"lines_failed\":%s",
             charge.elixir_spent ? "true" : "false", q(SPECIAL_ITEMS) & BIT_ELIXIR ? "true" : "false",
             charge.elixir_told ? "true" : "false", talk.phase, talk.line, lines_said[LINE_RESTORED],
             lines_said[LINE_CAN], lines_failed ? "true" : "false");
    cyc_tcp_ok(id, fields);
}
void fax_recharge_tcp_setup(void) {
    cyc_tcp_register("recharge_state",
                     "Recharging items: the Wing Boots' flight, the Hour Glass's freeze, the Elixir's charge, what "
                     "T: shows, counts",
                     recharge_state);
}
#else
void fax_recharge_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_recharge) {
    static const struct { uint16_t addr; NESModFunctionEntryCallback cb; } sites[] = {
        { PLAYER_USE_ELIXIR, elixir_hook }, { FAX_PLAYER_CLEAR_SELECTED_ITEM, keep_hook },
        { PLAYER_USE_WING_BOOTS, use_hook }, { WING_BOOTS_SET_FLIGHT, flight_hook },
        { BOOTS_SECOND, boots_second_hook }, { PLAYER_USE_HOUR_GLASS, use_hook },
        { HOUR_GLASS_COST, cost_hook }, { GLASS_SECOND, glass_second_hook },
        { GAME_CLEAR_TIMED_ITEMS, spawn_hook }, { GAME_LOOP_COUNTDOWN, tick_hook },
        { PLAYER_PICK_UP_ITEM, pick_up_hook }, { GAME_SHOW_START_SCREEN, title_hook },
        { ISCRIPTS_INVOKE_NEXT_ACTION, guru_hook } };
    _Static_assert(sizeof sites / sizeof sites[0] == sizeof hook_ids / sizeof hook_ids[0], "a site for each id");
    for (unsigned i = 0; i < sizeof sites / sizeof sites[0]; ++i)
        if (!nes_mod_register_function_entry_plugin(hook_ids[i], sites[i].addr, sites[i].cb)) abort();
    if (!nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.recharge", activate) ||
        !nes_mod_register_savestate_hook("faxanadu.recharge", state_get, state_set) ||
        !nes_mod_register_savestate_validator("faxanadu.recharge", record_validate) || !fax_save_listen(on_save))
        abort();
}
