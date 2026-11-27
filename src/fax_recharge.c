/* Recharging items. USA 57dd23d1, chipx86 byte-exact disassembly.
 *
 * The Wing Boots and the Hour Glass are kept after use, and both run on MP
 * (#124): each takes MP as it goes, and neither has a cooldown, so each can
 * be used again the moment its run ends.
 * - The Wing Boots fly while MP lasts, and take it only while they lift the
 *   hero: while A is held (#148). With them on and A held, the game flies
 *   him (Player_CheckHandleVertMovement); let go and he falls. So each played
 *   frame with A held counts toward the next MP, which goes every
 *   FRAMES_PER_MP[band] of them: 2, 1.6, 1.33 and 1 MP a second (the game's,
 *   64 frames) by title band, so a full bar of 80 flies 40, 50, 60 and 80 s.
 *   Walking, standing and falling with them on take nothing, and T: holds;
 *   A held on the ground counts, as the button is the one rule. They stay on
 *   until MP runs out (at 0 MP, however it went, the flight ends through the
 *   game's own end), Down+B again ends them the same way, in the air with A
 *   held (flying, the game treats Down as a climb, which clears the jumping
 *   and falling flags its item use refuses on) or standing, or he dies.
 * - The Hour Glass freezes for the game's own 15 s, and bosses still never
 *   freeze, but it costs no HP (the original halved it): the freeze takes an
 *   MP every GLASS_FRAMES_PER_MP frames, 40 MP for the whole 15 s, and ends
 *   at 15 s, at 0 MP, or on Down+B again, through the game's own end (and
 *   the area's music). On a boss's screen ScreenEvents_HandleBoss
 *   still cuts it (LimitItemDurations): the drain stops there, and the
 *   game's own second ends it, as in the original.
 * With 0 MP, Down+B on either does nothing but play the game's "invalid
 * choice" sound, as a spell he can't afford does nothing. MP comes from
 * kills (fax_mana.c), while either drains too, and a Guru fills it. Both at
 * once drain together, and each ends when MP runs out.
 * T: shows the selected item's seconds left while it runs: the flight's or
 * the freeze's, at the MP there is and what drains it, rounded up (a full
 * bar's 40 s shows 40). The flight's are seconds of A held, so they hold
 * while it is not; with both running, the freeze's count the flight's drain
 * as if A were held from now on, so it never ends before T: says. Otherwise
 * it is what the original shows: the flight's while the Wing Boots fly,
 * whatever is selected, else 00 (#119).
 * Turning either on or off yourself shows no text box (#151): the game's
 * "I've used ..." and "The power of ... is gone." are not shown, so play goes
 * on. The item's sound plays both ways, and the status bar's item box lights
 * while it runs (fax_item_box.c). An end the player did not cause says so
 * below the HUD, in Zone names' box, without stopping play: "Out of magic"
 * (either, or both at once), "The Hour Glass ran out" (its 15 s), "Bosses end
 * the Hour Glass" (LimitItemDurations on a boss's screen). The first use of
 * each, once ever, still shows a box, with the item's lesson in place of
 * "I've used ...": "Hold A to fly. / Flying uses / your magic." and "Time
 * stops for / enemies, using / your magic.", remembered in
 * faxanadu_recharge.txt beside the game (fax_remember.h), as Block's tutorial
 * is.
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
 * clear (with Found items no shop sells one any more, fax_found.h). The Spring
 * of Trunk spends a charged one instead of taking it (#138): the spring's quest
 * goes as the original's, and the hero keeps the Elixir, spent, as after a
 * revive, until a Guru or a find charges it. A spent one it refuses, as it
 * refuses a hero with none: its CHECK_FOR_ITEM reads the bit.
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
 *   GameLoop_CountdownItems in Game_MainLoop), with the item's sound; with 0
 *   MP the use is refused with the "invalid choice" sound. The routine was
 *   entered from GameLoop_CheckUseCurrentItem's jump table, so returning goes
 *   back to the main loop, as its RTS would. Otherwise it turns on: the
 *   routine goes on past its script ($83, $82), at the JSR
 *   Player_ClearSelectedItem after it, so no box; at the first use the script
 *   runs, and its message's action is steered to the item's lesson below.
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
 *   item runs: that frame of the run, for the Wing Boots one that counts only
 *   with A held (Joy1_ButtonMask, as GameLoop_UpdatePlayer read it in the
 *   same pass). MP goes on schedule, the bar drawn by
 *   the game's own Player_SetMP in a committed call (when the PPU buffer
 *   has room, else at the first frame that has), the duration is set to the
 *   seconds left, and the game's second is skipped (an RTS). At the run's end the site jumps to the routine's DEC with the
 *   duration at 0, so it wraps to $FF and the game ends the item as it
 *   always did (and the Hour Glass, the area's music). Another value of
 *   DurationHourGlass than this module set is the game's own
 *   LimitItemDurations: the freeze is left to the game's second.
 * - @_wingBootsDone's LDA #$96 and Game_DecHourGlassDuration's LDA #$97, the
 *   "... is gone" scripts every end reaches: skipped (the boots' RTS after it,
 *   the Hour Glass's area music), and an end the player did not cause put
 *   below the HUD (fax_recharge_present, through fax_banner_draw, for
 *   NOTICE_SHOWN played frames).
 * - GameLoop_CountdownItems, once a main-loop frame before the game's own
 *   countdowns: T: as above, queued through the PPU buffer as
 *   UI_DrawTimeValue writes it, and a bar still to draw.
 * - Player_UseElixir: the Elixir is spent. IScripts_InvokeNextAction, at the
 *   Spring of Trunk's CONSUME_ITEM Elixir ($A1BB, ISCRIPT_SPRING_OF_TRUNK's
 *   step after "2 more springs", which the script reaches only with the bit
 *   set): the Elixir is spent, and the game's own step then clears its bit,
 *   as Player_UseElixir does. At the
 *   first action of a Guru's script: a spent Elixir is charged again, HP and
 *   MP are filled (fax_mana_guru, MP from kills'), and his line is chosen. It is shown before his next action that no other feature
 *   steers (a temple Guru's CHECK_UPDATE_PLAYER_TITLE, right after
 *   SET_SPAWN_POINT; Eolis's Guru's first, CHECK_FOR_ITEM): the script goes
 *   to the SAY step, and at its END comes back where it was. At a first
 *   use's SHOW_UNSKIPPABLE_MESSAGE ($A675, $A671) the lesson goes to the SAY
 *   step the same way, and comes back past the message, at the script's END.
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
#include "fax_banner.h"
#include "fax_items.h"
#include "fax_mana.h"
#include "fax_remember.h"
#include "fax_save.h"
#include "fax_text.h"
#include "fax_travel.h"
#include "cyc_mod.h"
#include "cyc_ring.h"
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
#define PLAYER_USE_ELIXIR           0xC4CAu /* LDA SpecialItems, AND #$F7, STA SpecialItems: Player_ReduceHP's revive */
#define PLAYER_USE_WING_BOOTS       0xC579u /* LDA #$83, JSR MMC1_LoadBankAndJump: the "used Wing Boots" script */
/* @_afterFarJump, after the script: JSR Player_ClearSelectedItem, then
 * LDA #$1A, JSR Sound_PlayEffect */
#define BOOTS_USED                  0xC581u
/* STA DurationWingBoots, after LDA TITLE_TO_WINGBOOTS_DURATION,X;
 * then JSR UI_DrawTimeValue draws A */
#define WING_BOOTS_SET_FLIGHT       0xC592u
/* Game_DecWingBootsDuration, running, after it sets Player_StatusFlag's $80:
 * LDA InterruptCounter, AND #$3F, BNE @_return (not the game's second) */
#define BOOTS_SECOND                0xC5A8u
/* DEC DurationWingBoots, LDA, BMI @_wingBootsDone ("... is gone"), else
 * JMP UI_DrawTimeValue */
#define BOOTS_SECOND_DEC            0xC5AEu
/* @_wingBootsDone, Player_StatusFlag's $80 cleared: LDA #$96, JSR
 * MMC1_LoadBankAndJump, "The power of the Wing Boots is gone.", then RTS */
#define BOOTS_GONE                  0xC5BFu
#define PLAYER_USE_HOUR_GLASS       0xC5C8u /* LDA #$82, JSR MMC1_LoadBankAndJump: the "used Hour Glass" script */
/* @_afterFarJump, after the script: JSR Player_ClearSelectedItem, then
 * LDA #$1A, JSR Sound_PlayEffect */
#define GLASS_USED                  0xC5D0u
#define HOUR_GLASS_COST             0xC5D8u /* LSR Player_HP_U, ROR Player_HP_L: the original's half of HP */
#define HOUR_GLASS_DRAW_HP          0xC5DEu /* JSR UI_DrawPlayerHP, then LDA #$0F, STA DurationHourGlass, the music */
/* Game_DecHourGlassDuration, running: LDA InterruptCounter, AND #$3F,
 * BNE @_return */
#define GLASS_SECOND                0xC5F0u
/* DEC DurationHourGlass, BPL @_return, else "... is gone" and
 * Areas_DefaultMusic */
#define GLASS_SECOND_DEC            0xC5F6u
/* it wrapped: LDA #$97, JSR MMC1_LoadBankAndJump, "The power of the Hour
 * Glass is gone." */
#define GLASS_GONE                  0xC5FBu
#define GLASS_GONE_MUSIC            0xC603u /* @_afterFarJump: LDA Areas_DefaultMusic, STA Music_Current, RTS */
#define GAME_CLEAR_TIMED_ITEMS      0xC6AFu /* LDA #$FF, STA DurationGlove, ...: from Game_Start and Player_Spawn */
#define GAME_LOOP_COUNTDOWN         0xC88Fu /* GameLoop_CountdownItems: JSR Game_DecHourGlassDuration, ... */
#define PLAYER_PICK_UP_ITEM         0xC8CDu /* LDX NumberOfItems, CPX #$08, BCS: A is the item */
#define SOUND_PLAY_EFFECT           0xD0E4u /* STA Temp_SoundIDToPlay, ...: queues sound A, keeps X and Y */
#define PLAYER_SET_MP               0xFA85u /* CMP #$51, BCC, LDA #$50, STA Player_MP, LDY #$01: draws the MP bar */
#define GAME_SHOW_START_SCREEN      0xFC65u /* LDX #$FF, TXS: the title, from Game_InitStateForStartScreen only */
/* Bank 12 */
#define ISCRIPTS_INVOKE_NEXT_ACTION 0x826Eu /* JSR IScripts_LoadByte: the action at IScriptOrCHRAddr + IScriptOffset */
#define EOLIS_GURU_FIRST            0xA0BDu /* ISCRIPT_EOLIS_GURU's first action: CHECK_FOR_ITEM Ring of Elf */
/* ISCRIPT_SPRING_OF_TRUNK ($A1AB): 00, 0B 01 A1C0 (done: on), 02 59, 12 92 A1B9 (the Elixir: on), 03 5B, 00. Its
 * CHECK_FOR_ITEM jumps (IScripts_JumpToNextAddr: IScriptOrCHRAddr $A1B9, IScriptOffset 0) to
 * @_ISCRIPT_SPRING_OF_TRUNK_HAS_ELIXIR: 01 5A ("2 more springs"), then this: */
#define SPRING_CONSUME              0xA1BBu /* 10 92, CONSUME_ITEM the Elixir; then 0E 01, SET_QUEST 1, and 00 */
/* ISCRIPT_USED_HOURGLASS ($A670) and ISCRIPT_USED_WINGBOOTS ($A674), scripts $82 and $83: 00 (GENERIC), 01 AC or 01
 * AD (SHOW_UNSKIPPABLE_MESSAGE "I've used ..."), 00. IScripts_Begin starts each at IScriptOffset 1, the message: */
#define GLASS_USED_LINE             0xA671u
#define BOOTS_USED_LINE             0xA675u
#define SAY                         FAX_TRAVEL_SAY /* $AE00, Guru travel's step: 03 C3 00, show the slot, then END */
/* Bank 13 */
#define SLOT                        FAX_TRAVEL_SLOT /* $B45B: message $C3, the line slot */
/* RAM */
#define INTERRUPT_COUNTER           0x001Au /* +1 each NMI; the game's second is when & $3F == 0 */
#define ISCRIPT_ADDR                0x00DBu /* IScriptOrCHRAddr; the next action is at it + IScriptOffset ($DD) */
#define ISCRIPT_OFFSET              0x00DDu
#define PPU_BUFFER_READ             0x001Fu /* PPUBuffer_ReadOffset */
#define PPU_BUFFER_WRITE            0x0020u /* PPUBuffer_WriteOffset */
#define JOY1_BUTTON_MASK            0x0016u /* Joy1_ButtonMask: held, read in the NMI; A $80 */
#define PLAYER_MP                   0x039Au /* at most 80 */
#define ITEM_INVENTORY              0x03ADu
#define SELECTED_ITEM               0x03C1u
#define NUMBER_OF_ITEMS             0x03C6u
#define DURATION_WING_BOOTS         0x0429u /* $FF when not running */
#define DURATION_HOUR_GLASS         0x042Au
#define SPECIAL_ITEMS               0x042Cu
#define PLAYER_TITLE                0x0437u /* 0-15 */
#define PLAYER_IS_DEAD              0x0438u
#define BAR_WIDTH                   0x04EEu /* UI_MPAndHPBarWidth: 10 tiles, set by UI_DrawHUDSprites */
#define PPU_BUFFER                  0x0500u /* 256 bytes, circular: [length][addr hi][addr lo][tiles] */
/* Values */
#define BIT_ELIXIR                  0x08u
#define BUTTON_A                    0x80u /* Joy1_ButtonMask's A: with Player_StatusFlag's $80, flying */
#define SOUND_INVALID_CHOICE        0x0Du
#define SOUND_ITEM                  0x1Au   /* the two routines' "special item sound", after their scripts */
#define HUD_TIME                    0x2058u /* UI_DrawTimeValue's two digits */
#define PPU_BUFFER_ROOM             0x24u   /* PPUBuffer_HasCapacity: at least 36 bytes free, or empty */
#define BAR_COMMAND                 3u      /* a PPU buffer command's length and address */
#define MAX_ITEMS                   8
/* 06 temple: each temple Guru's first action (Conflate's after a
 * CHECK_FOR_ITEM); CHECK_UPDATE_PLAYER_TITLE follows it */
#define ISCRIPT_SET_SPAWN_POINT     0x06u
#define ISCRIPT_SHOW_MESSAGE        0x03u
#define SHOW_MESSAGE_LEN            2u /* 01 or 03, and the message */
#define ISCRIPT_END                 0x00u
#define ISCRIPT_CONSUME_ITEM        0x10u
#define SPECIAL_ELIXIR              0x92u /* the Elixir's full item ID */
#define TEMPLES                     8
enum { SLOT_LEN = FAX_TRAVEL_SLOT_LEN, SLOT_ID = FAX_TRAVEL_SLOT_ID };

enum { BOOTS = FAX_RECHARGE_BOOTS, GLASS = FAX_RECHARGE_GLASS, TIMED }; /* the two timed items */
enum { BANDS = 4 };                                                     /* title bands: PlayerTitle / 4 */

/* What the two cost, decided in #124 as starting points for Cameron's
 * playtest: retuning means changing these and nothing else. Times are in
 * played frames, those a pass of the main loop counts down (60 a second, no
 * pause, box or menu), and the game's second, what T: counts, is 64 of
 * them; whole frames an MP, so every cost is exact. The Wing Boots take an
 * MP every FRAMES_PER_MP frames of A held, by the title band he holds as it goes
 * (PlayerTitle / 4): 80 MP flies 40, 50, 60 and 80 s, so higher titles
 * still fly longer. The original flew 40, 30, 20 and 10 s by band, #27's
 * recharging pair 30 to 60. The Hour Glass freezes for FREEZE_SECONDS, the
 * game's own 15 (Player_UseHourGlass, LDA #$0F), and takes an MP every
 * GLASS_FRAMES_PER_MP frames, at every title: 40 MP for a whole freeze (8
 * every 3 s), where the original took half his HP at use. */
enum { SECOND = 64 };
static const uint8_t FRAMES_PER_MP[BANDS] = {
    32, /* Novice to Fighter: 2 MP a second, 40 s on a full bar */
    40, /* Adept to Warrior: 1.6 MP a second, 50 s */
    48, /* Swordman to Myrmidon: 1.33 MP a second, 60 s */
    64, /* Champion to Lord: 1 MP a second, 80 s */
};
enum { GLASS_FRAMES_PER_MP = 24 }; /* 2.67 MP a second: 40 MP for the 15 s */
enum { FREEZE_SECONDS = 15, FREEZE = FREEZE_SECONDS * SECOND };
enum { MOST_SECONDS = 99 }; /* T: has two digits */

static const uint8_t ITEM_ID[TIMED] = { FAX_ITEM_WING_BOOTS, FAX_ITEM_HOUR_GLASS };

/* A flight: running, and the played frames with A held until it takes the
 * next MP. */
typedef struct {
    uint8_t running; /* begun, and neither out of MP nor ended */
    uint8_t next;    /* played frames with A held until the next MP goes, 1-64 */
    uint8_t land;    /* used again: it ends at this frame's countdown */
} Flight;
/* A freeze: how far it has run, and the played frames until it takes the
 * next MP. */
typedef struct {
    uint8_t running; /* begun, and neither over, out of MP nor ended */
    uint8_t land;    /* used again: it ends at this frame's countdown */
    uint16_t played; /* played frames of it so far, 0-FREEZE */
    uint8_t next;    /* played frames until the next MP goes, 1-GLASS_FRAMES_PER_MP */
    uint8_t shown;   /* DurationHourGlass as this module last set it: another value is the game's */
} Freeze;
/* The state: everything a save state or a save slot must keep. */
typedef struct {
    Flight flight;
    Freeze freeze;
    uint8_t bar;           /* MP taken, and its bar still to draw (the PPU buffer was full) */
    uint8_t elixir_spent;  /* it revived the hero; a Guru charges it again */
    uint8_t elixir_told;   /* a Guru has said that he restores it (either line) */
    uint8_t ending[TIMED]; /* why each run is ending (ENDED_*), for its end's notice: noted as this module
                                  ends it, or at LimitItemDurations' cut, which the game's second ends */
} Charge;

/* What a Guru says about the Elixir, in his own box, and each timed item's
 * lesson at its first use, in its use script's: 16 columns, 4 lines; '\n'
 * breaks a line. */
enum { LINE_NONE, LINE_RESTORED, LINE_CAN, LINE_BOOTS, LINE_GLASS, LINES };
static const char *const line_text[LINES] = { "", "Your Elixir is\nrestored. Come\nback when used.",
                                              "If your Elixir\nis used, a Guru\nwill restore it.",
                                              "Hold A to fly.\nFlying uses\nyour magic.",
                                              "Time stops for\nenemies, using\nyour magic." };
/* One conversation's line: a Guru's waits for the action it goes before, an
 * item's lesson for the message it takes the place of (PENDING), then shows
 * (SAYING) with the script parked at `resume`. */
enum { TALK_IDLE, TALK_PENDING, TALK_SAYING, TALK_PHASES };
typedef struct {
    uint8_t phase, line;
    uint16_t at;     /* PENDING: the action the line goes before, or a lesson takes the place of */
    uint16_t resume; /* SAYING: IScriptOrCHRAddr and IScriptOffset to go back to */
    uint8_t offset;
} Talk;

/* Why a run ends: Down+B (the player's own), out of MP, its time up; and,
 * for a run this module did not end, LimitItemDurations' cut on a boss's
 * screen, or (none noted) the game's own timer. */
enum { ENDED_NONE, ENDED_BY_HAND, ENDED_MAGIC, ENDED_TIME, ENDED_BOSS, ENDINGS };
/* What an end the player did not cause says, below the HUD in Zone names'
 * box, as play goes on. The last, the game's own timer for the Wing Boots,
 * comes only from a save state made before Recharging items. */
enum { NOTICE_NONE, NOTICE_MAGIC, NOTICE_GLASS_TIME, NOTICE_BOSS, NOTICE_BOOTS_TIME, NOTICES };
static const char *const notice_text[NOTICES] = { "", "Out of magic", "The Hour Glass ran out",
                                                  "Bosses end the Hour Glass", "The Wing Boots ran out" };
/* Played frames (main-loop passes, so it holds through a pause, a box or a
 * menu): fade in, hold, fade out, as Zone names'. */
enum { NOTICE_IN = 8, NOTICE_HOLD = 120, NOTICE_OUT = 32, NOTICE_SHOWN = NOTICE_IN + NOTICE_HOLD + NOTICE_OUT };
typedef struct {
    uint8_t line; /* NOTICE_* */
    uint8_t age;  /* played frames it has shown, 0-NOTICE_SHOWN */
} Notice;
/* The lessons are learned once, ever: a field each in faxanadu_recharge.txt. */
#define SIDECAR "faxanadu_recharge.txt"
static const char *const LEARNED[TIMED] = { "recharge.boots_taught", "recharge.glass_taught" };

static bool enabled;
static Charge charge;
static unsigned drawn; /* what this module last queued on T: */
static bool drawn_valid;
static Talk talk;
static Notice notice;
static bool taught[TIMED]; /* the item's lesson has been said (remembered beside the game) */
static bool notice_drawn;  /* TCP: the last present drew the notice */
static bool lines_failed;  /* the line could not be supplied: the Gurus charge it silently, and a
                                      first use shows the game's own line */
static bool legacy;        /* the record loaded was from before #124: adopt its runs once RAM is in */
static unsigned flights, freezes, refused, kept, pickups, revives, guru_recharges, spawns, cuts, springs; /* TCP */
static unsigned landings[TIMED], lines_said[LINES];
static unsigned quiet_uses, gones_skipped, notices[NOTICES]; /* TCP: turned on with no box; "... is gone" not shown */
static unsigned lift_frames, rest_frames; /* TCP: a flight's played frames with A held, and without */
static bool lifting;                      /* TCP: A held at the flight's last frame */
/* For TCP: the frame boundaries the last freeze and the last flight were over at, as ping counts them. A run
 * ends inside a frame, whose number is cyc_ring_frame (the frames completed) while it runs, so this is the
 * next. A test reads its end here: a read that finds it over can come any number of frames later on a loaded
 * machine (#144, #155). */
static unsigned long freeze_ended, flight_ended;

/* ---- the rules, on plain values ---- */

static uint8_t frames_per_mp(uint8_t title) { return FRAMES_PER_MP[(title >> 2) & (BANDS - 1)]; }
static bool ready(const Charge *c, int i) { return i == BOOTS ? !c->flight.running : !c->freeze.running; }

/* Seconds for T:, rounded up: 40 while up to 40 s are left, 1 in the last. */
static unsigned seconds(unsigned frames) {
    unsigned s = (frames + SECOND - 1) / SECOND;
    return s > MOST_SECONDS ? MOST_SECONDS : s;
}

/* A flight begins: the first MP goes a whole step of A held after it. */
static Flight take_off(uint8_t title) { return (Flight){ 1, frames_per_mp(title), 0 }; }
/* A freeze begins, the same. */
static Freeze freeze_begin(void) { return (Freeze){ 1, 0, 0, GLASS_FRAMES_PER_MP, FREEZE_SECONDS }; }

/* Played frames until `mp` MP runs out, at the drains running: the flight's
 * (an MP at `next`, then every FRAMES_PER_MP), as if A were held from now on,
 * and the freeze's (the same, every GLASS_FRAMES_PER_MP, as long as it lasts:
 * its last MP goes on its last frame). NEVER if nothing drains it to 0. */
enum { NEVER = 0xFFFFu };
static unsigned to_empty(const Charge *c, uint8_t mp, uint8_t title) {
    const Flight *f = &c->flight;
    const Freeze *z = &c->freeze;
    unsigned fly_at = f->running ? f->next : NEVER, step = frames_per_mp(title);
    unsigned freeze_end = z->running && z->played < FREEZE ? (unsigned)(FREEZE - z->played) : 0u;
    unsigned glass_at = z->running && z->next <= freeze_end ? z->next : NEVER;
    for (unsigned left = mp; left;) {
        unsigned t = fly_at < glass_at ? fly_at : glass_at;
        if (t == NEVER) return NEVER;
        if (t == fly_at) {
            --left;
            fly_at += step;
        }
        if (t == glass_at && left) {
            --left;
            glass_at += GLASS_FRAMES_PER_MP;
            if (glass_at > freeze_end) glass_at = NEVER;
        }
        if (!left) return t;
    }
    return 0;
}
/* Played frames left in the flight, of A held: until MP runs out. */
static unsigned flight_left(const Charge *c, uint8_t mp, uint8_t title) {
    if (!c->flight.running) return 0;
    unsigned t = to_empty(c, mp, title);
    return t == NEVER ? 0u : t;
}
/* Played frames left in the freeze: until its 15 s are up, or MP runs out
 * (a flight's drain counted as if A were held, so the freeze never ends
 * sooner). */
static unsigned freeze_left(const Charge *c, uint8_t mp, uint8_t title) {
    const Freeze *z = &c->freeze;
    if (!z->running || z->played >= FREEZE) return 0;
    unsigned t = to_empty(c, mp, title), end = (unsigned)(FREEZE - z->played);
    return t < end ? t : end;
}
/* One played frame of a flight, `*mp` its MP: with A held (`lift`), it
 * counts toward the next MP, which goes on schedule; without, nothing. True
 * when the flight ends now: used again, or out of MP, A or not. */
static bool fly(Flight *f, uint8_t *mp, uint8_t title, bool lift) {
    if (f->land || !*mp) return true;
    if (!lift) return false;
    if (f->next > 1) {
        --f->next;
        return false;
    }
    f->next = frames_per_mp(title);
    return --*mp == 0;
}
/* One played frame of a freeze, the same: true when it ends now, used
 * again, out of MP, or its time up (its last MP goes on its last frame). */
static bool freeze_frame(Freeze *z, uint8_t *mp) {
    if (z->land || !*mp) return true;
    ++z->played;
    if (z->next > 1) --z->next;
    else {
        z->next = GLASS_FRAMES_PER_MP;
        --*mp;
    }
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

/* The Spring of Trunk is about to take the Elixir: the script's next action,
 * at `at`, is its CONSUME_ITEM Elixir, and the Elixir is charged
 * (SpecialItems' bit, which its CHECK_FOR_ITEM asked for). Then it spends it
 * instead. No other script takes the Elixir. */
static bool spring_spends(uint16_t at, uint8_t action, uint8_t item, uint8_t special) {
    return at == SPRING_CONSUME && action == ISCRIPT_CONSUME_ITEM && item == SPECIAL_ELIXIR && (special & BIT_ELIXIR);
}

/* Why a run this module ends now ends: Down+B, the player's own; for the
 * freeze, its 15 s up (its last MP can go on their last frame: still its
 * time); else out of MP. */
static int flight_ending(const Flight *f) { return f->land ? ENDED_BY_HAND : ENDED_MAGIC; }
static int freeze_ending(const Freeze *z) {
    return z->land ? ENDED_BY_HAND : z->played >= FREEZE ? ENDED_TIME : ENDED_MAGIC;
}
/* What the end of `item`'s run says below the HUD: nothing when the player
 * ended it; else why. A time up is the Hour Glass's 15 s, or the game's own
 * timer (ENDED_NONE: a run from a save state made before this feature). */
static int notice_for(int item, int why) {
    switch (why) {
    case ENDED_BY_HAND:
        return NOTICE_NONE;
    case ENDED_MAGIC:
        return NOTICE_MAGIC;
    case ENDED_BOSS:
        return NOTICE_BOSS;
    default:
        return item == GLASS ? NOTICE_GLASS_TIME : NOTICE_BOOTS_TIME;
    }
}
/* The notice's strength, 0-255, at `age` played frames. */
static unsigned notice_alpha(unsigned age) {
    if (age < NOTICE_IN) return (age + 1) * 255u / NOTICE_IN;
    if (age < NOTICE_IN + NOTICE_HOLD) return 255;
    if (age < NOTICE_SHOWN) return (NOTICE_SHOWN - age) * 255u / (NOTICE_OUT + 1);
    return 0;
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
    for (int i = 0; i < TIMED; ++i)
        if (ITEM_ID[i] == item) return i;
    return -1;
}
/* The hero has `item`: selected, or in the first NumberOfItems slots
 * (Player_LacksItem's test). */
static bool owns(uint8_t item) {
    if (q(SELECTED_ITEM) == item) return true;
    unsigned n = q(NUMBER_OF_ITEMS);
    if (n > MAX_ITEMS) n = MAX_ITEMS;
    for (unsigned i = 0; i < n; ++i)
        if (q((uint16_t)(ITEM_INVENTORY + i)) == item) return true;
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
    if ((changed || refresh) && queue_time(v)) {
        drawn = v;
        drawn_valid = true;
    }
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
    cyc_mod_poke((uint16_t)(0x100u | regs.s), (uint8_t)(r >> 8));
    regs.s = (uint8_t)(regs.s - 1);
    cyc_mod_poke((uint16_t)(0x100u | regs.s), (uint8_t)r);
    regs.s = (uint8_t)(regs.s - 1);
    cyc_mod_set_regs(&regs);
    return 1;
}

/* GameLoop_CountdownItems: once a main-loop frame, before the game's own
 * countdowns (and so before this frame of a run). */
static int tick_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) return 0;
    if (charge.flight.running && q(DURATION_WING_BOOTS) >= 0x80) charge.flight = (Flight){ 0 }; /* gone already */
    if (charge.freeze.running && q(DURATION_HOUR_GLASS) >= 0x80) charge.freeze = (Freeze){ 0 };
    if (charge.elixir_spent && (q(SPECIAL_ITEMS) & BIT_ELIXIR)) charge.elixir_spent = 0; /* found */
    if (notice.line != NOTICE_NONE && notice.age < NOTICE_SHOWN) ++notice.age;           /* a played frame of it */
    draw_bar();
    draw_hud(true);
    return 0;
}

/* Player_UseWingBoots, Player_UseHourGlass: one that runs ends, at this
 * frame's countdown, with the item's sound and no box; with no MP either is
 * refused with the game's "invalid choice" sound. The routine was entered
 * from GameLoop_CheckUseCurrentItem's jump table, so returning goes back to
 * the main loop, as its RTS would. Otherwise it turns on, past its script
 * (no box) to its JSR Player_ClearSelectedItem; but at the item's first use
 * the script runs, and its message is the lesson (guru_hook). Turning on
 * puts away a notice still showing: it would say what is no longer so. */
static int use_hook(uint16_t addr) {
    if (cyc_mod_isolated()) return 0;
    int i = addr == PLAYER_USE_WING_BOOTS ? BOOTS : GLASS;
    uint8_t *land = i == BOOTS ? &charge.flight.land : &charge.freeze.land;
    if (i == BOOTS ? charge.flight.running : charge.freeze.running) {
        *land = 1;
        call(SOUND_PLAY_EFFECT, SOUND_ITEM);
        return 1;
    }
    if (!q(PLAYER_MP)) {
        call(SOUND_PLAY_EFFECT, SOUND_INVALID_CHOICE);
        ++refused;
        return 1;
    }
    notice = (Notice){ 0 };
    if (!taught[i] && !lines_failed) {
        talk = (Talk){ TALK_PENDING, (uint8_t)(i == BOOTS ? LINE_BOOTS : LINE_GLASS),
                       i == BOOTS ? BOOTS_USED_LINE : GLASS_USED_LINE, 0, 0 };
        return 0;
    }
    ++quiet_uses;
    return return_via(i == BOOTS ? BOOTS_USED : GLASS_USED);
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
    charge.ending[BOOTS] = ENDED_NONE;
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
    charge.ending[GLASS] = ENDED_NONE;
    ++freezes;
    return return_via(HOUR_GLASS_DRAW_HP);
}

/* Game_DecHourGlassDuration's LDA InterruptCounter, once a main-loop frame
 * while DurationHourGlass runs: a frame of the freeze. A value this module
 * did not set is LimitItemDurations' 0 on a boss's screen: the drain stops,
 * and the game's own second ends it (and its end says so: ENDED_BOSS). */
static int glass_second_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated() || !charge.freeze.running) return 0;
    Freeze *z = &charge.freeze;
    if (q(DURATION_HOUR_GLASS) != z->shown) {
        *z = (Freeze){ 0 };
        charge.ending[GLASS] = ENDED_BOSS;
        ++cuts;
        draw_hud(false);
        return 0;
    }
    uint8_t was = q(PLAYER_MP), mp = was;
    bool ends = freeze_frame(z, &mp);
    if (mp != was) set_mp(mp);
    if (ends) {
        if (z->land) ++landings[GLASS];
        freeze_ended = cyc_ring_frame + 1ul;
        charge.ending[GLASS] = (uint8_t)freeze_ending(z);
        *z = (Freeze){ 0 };
        cyc_mod_poke(DURATION_HOUR_GLASS, 0);
        draw_hud(false);
        return return_via(GLASS_SECOND_DEC); /* 0 wraps: the game's end (gone_hook) */
    }
    z->shown = (uint8_t)seconds(freeze_left(&charge, mp, q(PLAYER_TITLE)));
    cyc_mod_poke(DURATION_HOUR_GLASS, z->shown);
    draw_hud(false);
    return 1; /* not the game's second: RTS */
}

/* Game_DecWingBootsDuration's LDA InterruptCounter, once a main-loop frame
 * while DurationWingBoots runs (Player_StatusFlag's $80 just set): a frame of
 * the flight, which counts toward MP only with A held. The mask is the one
 * this pass's Player_CheckHandleVertMovement flew him by (the NMI reads the
 * pad, and Magic button's Up+B is put back before GameLoop_CheckUseCurrentItem). */
static int boots_second_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated() || !charge.flight.running) return 0;
    uint8_t title = q(PLAYER_TITLE), was = q(PLAYER_MP), mp = was;
    lifting = (q(JOY1_BUTTON_MASK) & BUTTON_A) != 0;
    ++*(lifting ? &lift_frames : &rest_frames);
    bool ends = fly(&charge.flight, &mp, title, lifting);
    if (mp != was) set_mp(mp);
    if (ends) {
        if (charge.flight.land) ++landings[BOOTS];
        flight_ended = cyc_ring_frame + 1ul;
        charge.ending[BOOTS] = (uint8_t)flight_ending(&charge.flight);
        charge.flight = (Flight){ 0 };
        cyc_mod_poke(DURATION_WING_BOOTS, 0);
        draw_hud(false);
        return return_via(BOOTS_SECOND_DEC); /* 0 wraps: the game's end (gone_hook) */
    }
    cyc_mod_poke(DURATION_WING_BOOTS, (uint8_t)seconds(flight_left(&charge, mp, title)));
    draw_hud(false);
    return 1; /* not the game's second: RTS */
}

/* A notice below the HUD: it takes the banner's place, as Slain by does. Out
 * of MP with both running ends both, in one pass or, when the flight took the
 * last MP, the freeze in the next (its site runs first): they share the line,
 * which is not shown again while it shows. Nothing else can say the same
 * thing twice without a use between, which puts the notice away. */
static void notify(int line) {
    if (line == NOTICE_NONE || (line == notice.line && notice.age < NOTICE_SHOWN)) return;
    notice = (Notice){ (uint8_t)line, 0 };
    ++notices[line];
    fax_banner_hide();
}

/* @_wingBootsDone's LDA #$96 and Game_DecHourGlassDuration's LDA #$97: a run
 * has ended (its countdown wrapped to $FF, $80 cleared for the boots), and
 * the game would show "The power of ... is gone." in a box that stops play.
 * Not shown: the boots' routine returns, as the RTS after the script does,
 * and the Hour Glass's goes on to the area's music after it. An end the
 * player did not cause says why below the HUD. */
static int gone_hook(uint16_t addr) {
    if (cyc_mod_isolated()) return 0;
    int i = addr == BOOTS_GONE ? BOOTS : GLASS;
    int why = charge.ending[i];
    charge.ending[i] = ENDED_NONE;
    ++gones_skipped;
    notify(notice_for(i, why));
    return i == BOOTS ? 1 : return_via(GLASS_GONE_MUSIC);
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
 * file): both runs end, as the game sets their durations to $FF, with no
 * "... is gone", and any notice goes. */
static int spawn_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) return 0;
    charge.flight = (Flight){ 0 };
    charge.freeze = (Freeze){ 0 };
    memset(charge.ending, ENDED_NONE, sizeof charge.ending);
    notice = (Notice){ 0 };
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
    notice = (Notice){ 0 };
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
    if (which <= LINE_NONE || which >= LINES || !encode(line_text[which], bytes)) return false;
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
static bool lesson(int which) { return which == LINE_BOOTS || which == LINE_GLASS; }
static void lines_lost(void) {
    fprintf(stderr, "[recharge] cannot supply its lines; the Gurus restore the Elixir silently, and an item's "
                    "first use shows the game's own line\n");
    lines_failed = true;
}
/* Say `which` now, as the script's next action: park the script, and point
 * it at the SAY step. A Guru's line goes before the action the script is at,
 * and either tells the hero; an item's lesson takes the place of its use
 * script's message, so the script goes on past it, and is learned for good. */
static void say(int which) {
    if (!serve_line(which)) {
        lines_lost();
        return;
    }
    uint8_t resume = (uint8_t)(q(ISCRIPT_OFFSET) + (lesson(which) ? SHOW_MESSAGE_LEN : 0));
    talk = (Talk){ TALK_SAYING, (uint8_t)which, 0, word(ISCRIPT_ADDR), resume };
    go(SAY, 0);
    if (lesson(which)) {
        int i = which == LINE_BOOTS ? BOOTS : GLASS;
        taught[i] = true;
        fax_remember(SIDECAR, LEARNED[i]);
    } else {
        charge.elixir_told = 1;
    }
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
 * another script's. An item's first use (use_hook) left its lesson waiting
 * for its script's message, which it takes the place of the same way. */
static int guru_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) return 0;
    uint16_t at = (uint16_t)(word(ISCRIPT_ADDR) + q(ISCRIPT_OFFSET));
    if (spring_spends(at, q(at), q((uint16_t)(at + 1)), q(SPECIAL_ITEMS))) {
        charge.elixir_spent = 1; /* the step clears the bit: spent, as after a revive */
        ++springs;
    }
    Talk was = talk;
    talk = (Talk){ 0 };
    if (was.phase == TALK_SAYING && at == SAY + 2) { /* his line has been read: back to his script */
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
    if (item == FAX_RECHARGE_ELIXIR)
        return elixir_bit ? FAX_CHARGE_READY : c->elixir_spent ? FAX_CHARGE_USED : FAX_CHARGE_NONE;
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
#define FIELD_TOLD   "recharge.elixir_told"

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

/* ---- the notice below the HUD ---- */

/* An end the player did not cause, in Zone names' box below the HUD, over
 * the presented picture: not while a box of the game's is up (it holds, as
 * its age counts played frames), nor while the hero lies dead (Slain by has
 * the box). The font is the one Zone names loads. */
const uint32_t *fax_recharge_present(const uint32_t *picture, int *w, int *h) {
    notice_drawn = false;
    if (!enabled || notice.line == NOTICE_NONE || notice.age >= NOTICE_SHOWN || q(PLAYER_IS_DEAD) ||
        fax_text_box_showing())
        return picture;
    const uint32_t *out = fax_banner_draw(picture, w, h, notice_text[notice.line], notice_alpha(notice.age));
    notice_drawn = out != picture;
    return out;
}

/* ---- the save-state record ---- */

/* Version 4 (#151): [0] 4, the flight: [1] running, [2] frames to the next
 * MP, [3] ending; the freeze: [4] running, [5] ending, [6-7] its frames so
 * far, [8] frames to its next MP, [9] DurationHourGlass as set; [10] the MP
 * bar still to draw; [11] the Elixir spent, [12] a Guru has told the hero,
 * and the line (a Guru's or a lesson): [13] its phase, [14] which, [15-16]
 * the action it goes before, [17-18] and [19] where the script goes back
 * to; why each run is ending, [20] the flight's and [21] the freeze's; the
 * notice below the HUD, [22] which and [23] its age. Version 3 (#124) is
 * [0]-[19], with no notice. Version 2, before
 * #124: [1] the Wing Boots running, [2] their cooldown, [3] the Hour Glass
 * running, [4] its cooldown, [5] the Elixir spent, [6] a continued file's
 * cooldowns waiting for the spawn, [7] and [8] those cooldowns, then [9]-[16]
 * as [12]-[19] here. Version 1 was version 2's [0]-[8]. The cooldowns are
 * dropped, and a run going on a timer goes on as this module runs one
 * (adopt). */
enum { RECORD_VERSION = 4, RECORD = 24, RECORD_V3 = 20, RECORD_V2 = 17, RECORD_V1 = 9, OLD_MOST_SECONDS = 99 };
static int state_get(uint8_t *buf, int cap) {
    if (cap < RECORD) return -1;
    const Flight *f = &charge.flight;
    const Freeze *z = &charge.freeze;
    const uint8_t rec[RECORD] = { RECORD_VERSION,
                                  f->running,
                                  f->next,
                                  f->land,
                                  z->running,
                                  z->land,
                                  (uint8_t)z->played,
                                  (uint8_t)(z->played >> 8),
                                  z->next,
                                  z->shown,
                                  charge.bar,
                                  charge.elixir_spent,
                                  charge.elixir_told,
                                  talk.phase,
                                  talk.line,
                                  (uint8_t)talk.at,
                                  (uint8_t)(talk.at >> 8),
                                  (uint8_t)talk.resume,
                                  (uint8_t)(talk.resume >> 8),
                                  talk.offset,
                                  charge.ending[BOOTS],
                                  charge.ending[GLASS],
                                  notice.line,
                                  notice.age };
    memcpy(buf, rec, sizeof rec);
    return RECORD;
}
static bool talk_valid(const uint8_t *t) { /* told, phase, line */
    return t[0] <= 1 && t[1] < TALK_PHASES && t[2] < LINES && (t[1] == TALK_IDLE) == (t[2] == LINE_NONE);
}
static int record_validate(const uint8_t *buf, int len) {
    if (len == 0) return 1;
    if ((len == RECORD && buf[0] == RECORD_VERSION) || (len == RECORD_V3 && buf[0] == 3)) {
        unsigned played = buf[6] | buf[7] << 8;
        bool v3 = buf[1] <= 1 && buf[2] <= SECOND && (!buf[1] || buf[2] >= 1) && buf[3] <= 1 && buf[4] <= 1 &&
                  buf[5] <= 1 && played <= FREEZE && buf[8] <= GLASS_FRAMES_PER_MP && (!buf[4] || buf[8] >= 1) &&
                  buf[9] <= FREEZE_SECONDS && buf[10] <= 1 && buf[11] <= 1 && talk_valid(buf + 12);
        return v3 && (len == RECORD_V3 ||
                      (buf[20] < ENDINGS && buf[21] < ENDINGS && buf[22] < NOTICES && buf[23] <= NOTICE_SHOWN));
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
    notice = (Notice){ 0 };
    legacy = false;
    const uint8_t *told = NULL;
    if (len == RECORD || len == RECORD_V3) {
        charge.flight = (Flight){ buf[1], buf[2], buf[3] };
        charge.freeze = (Freeze){ buf[4], buf[5], (uint16_t)(buf[6] | buf[7] << 8), buf[8], buf[9] };
        charge.bar = buf[10];
        charge.elixir_spent = buf[11];
        told = buf + 12;
        if (len == RECORD) {
            charge.ending[BOOTS] = buf[20];
            charge.ending[GLASS] = buf[21];
            notice = (Notice){ buf[22], buf[23] };
        }
    } else if (len) {
        charge.flight.running = buf[1];
        charge.freeze.running = buf[3];
        charge.elixir_spent = buf[5];
        legacy = buf[1] || buf[3];
        if (len == RECORD_V2) told = buf + 9;
    }
    if (told) {
        charge.elixir_told = told[0];
        talk =
            (Talk){ told[1], told[2], (uint16_t)(told[3] | told[4] << 8), (uint16_t)(told[5] | told[6] << 8), told[7] };
    }
    drawn_valid = false; /* the loaded picture has its own T: */
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
    if (glass >= 0x80) {
        charge.freeze = (Freeze){ 0 };
        return;
    }
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
    if (legacy) {
        legacy = false;
        adopt();
    }
    if (talk.phase != TALK_SAYING || lines_failed || serve_line(talk.line)) return;
    lines_lost();
}

/* ---- activation ---- */

static const char *const hook_ids[] = {
    "faxanadu.recharge.elixir",     "faxanadu.recharge.keep",         "faxanadu.recharge.use-boots",
    "faxanadu.recharge.flight",     "faxanadu.recharge.boots-second", "faxanadu.recharge.use-glass",
    "faxanadu.recharge.glass-cost", "faxanadu.recharge.glass-second", "faxanadu.recharge.spawn",
    "faxanadu.recharge.tick",       "faxanadu.recharge.pick-up",      "faxanadu.recharge.title",
    "faxanadu.recharge.guru",       "faxanadu.recharge.boots-gone",   "faxanadu.recharge.glass-gone"
};
static void hooks(bool on) {
    for (unsigned i = 0; i < sizeof hook_ids / sizeof hook_ids[0]; ++i)
        nes_mod_set_function_hook_enabled(hook_ids[i], on);
}
static void deactivate(void) {
    enabled = false;
    memset(&charge, 0, sizeof charge);
    talk = (Talk){ 0 };
    notice = (Notice){ 0 };
    lines_failed = legacy = notice_drawn = false;
    drawn_valid = false;
    flights = freezes = refused = kept = pickups = revives = guru_recharges = spawns = cuts = springs = 0;
    quiet_uses = gones_skipped = 0;
    lift_frames = rest_frames = 0;
    lifting = false;
    freeze_ended = flight_ended = 0;
    memset(landings, 0, sizeof landings);
    memset(lines_said, 0, sizeof lines_said);
    memset(notices, 0, sizeof notices);
    memset(taught, 0, sizeof taught);
    hooks(false);
}
static void activate(void) {
    enabled = true;
    for (int i = 0; i < TIMED; ++i) taught[i] = fax_remembered(SIDECAR, LEARNED[i]);
    hooks(true);
    nes_mod_set_local_only("Recharging items", 1);
}

#ifdef CYC_WITH_SDL
static void recharge_state(int id, const char *line) {
    (void)line;
    char fields[2000];
    const Flight *f = &charge.flight;
    const Freeze *z = &charge.freeze;
    uint8_t mp = q(PLAYER_MP), title = q(PLAYER_TITLE);
    unsigned left = flight_left(&charge, mp, title), frozen = freeze_left(&charge, mp, title);
    /* lift_frames and rest_frames count every flight's played frames, with A held (they take MP) and without;
     * lifting is whether A was held at the last. */
    int n = snprintf(fields, sizeof fields,
                     "\"enabled\":%s,\"boots_ready\":%s,\"boots_running\":%u,\"glass_ready\":%s,\"glass_running\":%u,"
                     "\"flight_next\":%u,\"flight_left\":%u,\"flight_seconds\":%u,\"frames_per_mp\":%u,"
                     "\"freeze_played\":%u,\"freeze_next\":%u,\"freeze_left\":%u,\"freeze_seconds\":%u,"
                     "\"glass_frames_per_mp\":%u,\"glass_shown\":%u,\"bar\":%u,\"flights\":%u,\"freezes\":%u,"
                     "\"refused\":%u,\"boots_landed\":%u,\"glass_landed\":%u,\"cuts\":%u,\"kept\":%u,\"pickups\":%u,"
                     "\"revives\":%u,\"guru_recharges\":%u,\"spawns\":%u,\"hud\":%u,\"hud_drawn\":%d,"
                     "\"lift_frames\":%u,\"rest_frames\":%u,\"lifting\":%s",
                     enabled ? "true" : "false", ready(&charge, BOOTS) ? "true" : "false", f->running,
                     ready(&charge, GLASS) ? "true" : "false", z->running, f->next, left, seconds(left),
                     frames_per_mp(title), z->played, z->next, frozen, seconds(frozen), (unsigned)GLASS_FRAMES_PER_MP,
                     z->shown, charge.bar, flights, freezes, refused, landings[BOOTS], landings[GLASS], cuts, kept,
                     pickups, revives, guru_recharges, spawns, hud_value(&charge, q(SELECTED_ITEM), mp, title),
                     drawn_valid ? (int)drawn : -1, lift_frames, rest_frames, lifting ? "true" : "false");
    /* The Elixir: spent, charged; the Guru's line: told, talk (0 none, 1 waiting, 2 showing), line (0 none, 1
     * restored, 2 can), how many of each he has said, and whether the lines are off (their overrides could not
     * be set); the Elixirs the Spring of Trunk has spent; the frames the last freeze and the last flight were
     * over at (0: none yet). */
    n += snprintf(fields + n, sizeof fields - (size_t)n,
                  ",\"elixir_spent\":%s,\"elixir_charged\":%s,\"elixir_told\":%s,\"talk\":%u,\"line\":%u,"
                  "\"restored_lines\":%u,\"can_lines\":%u,\"lines_failed\":%s,\"springs\":%u,\"freeze_ended\":%lu,"
                  "\"flight_ended\":%lu",
                  charge.elixir_spent ? "true" : "false", q(SPECIAL_ITEMS) & BIT_ELIXIR ? "true" : "false",
                  charge.elixir_told ? "true" : "false", talk.phase, talk.line, lines_said[LINE_RESTORED],
                  lines_said[LINE_CAN], lines_failed ? "true" : "false", springs, freeze_ended, flight_ended);
    /* No box (#151): the uses that turned an item on without one, and the "... is gone" boxes not shown; each
     * item's lesson learned (remembered beside the game) and said this session (line 3 the boots', 4 the
     * glass's); the notice below the HUD: its line (null for none, or once shown out), its age in played frames
     * and strength, whether the last present drew it, and how many of each have come up (magic, the Hour Glass's
     * time, a boss, the boots' time). */
    const char *showing = notice.line != NOTICE_NONE && notice.age < NOTICE_SHOWN ? notice_text[notice.line] : NULL;
    snprintf(fields + n, sizeof fields - (size_t)n,
             ",\"quiet_uses\":%u,\"gones_skipped\":%u,\"boots_taught\":%s,\"glass_taught\":%s,"
             "\"boots_lessons\":%u,\"glass_lessons\":%u,\"notice\":%s%s%s,\"notice_age\":%u,\"notice_alpha\":%u,"
             "\"notice_drawn\":%s,\"notices\":[%u,%u,%u,%u]",
             quiet_uses, gones_skipped, taught[BOOTS] ? "true" : "false", taught[GLASS] ? "true" : "false",
             lines_said[LINE_BOOTS], lines_said[LINE_GLASS], showing ? "\"" : "", showing ? showing : "null",
             showing ? "\"" : "", notice.age, showing ? notice_alpha(notice.age) : 0u, notice_drawn ? "true" : "false",
             notices[NOTICE_MAGIC], notices[NOTICE_GLASS_TIME], notices[NOTICE_BOSS], notices[NOTICE_BOOTS_TIME]);
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
    static const struct {
        uint16_t addr;
        NESModFunctionEntryCallback cb;
    } sites[] = { { PLAYER_USE_ELIXIR, elixir_hook },
                  { FAX_PLAYER_CLEAR_SELECTED_ITEM, keep_hook },
                  { PLAYER_USE_WING_BOOTS, use_hook },
                  { WING_BOOTS_SET_FLIGHT, flight_hook },
                  { BOOTS_SECOND, boots_second_hook },
                  { PLAYER_USE_HOUR_GLASS, use_hook },
                  { HOUR_GLASS_COST, cost_hook },
                  { GLASS_SECOND, glass_second_hook },
                  { GAME_CLEAR_TIMED_ITEMS, spawn_hook },
                  { GAME_LOOP_COUNTDOWN, tick_hook },
                  { PLAYER_PICK_UP_ITEM, pick_up_hook },
                  { GAME_SHOW_START_SCREEN, title_hook },
                  { ISCRIPTS_INVOKE_NEXT_ACTION, guru_hook },
                  { BOOTS_GONE, gone_hook },
                  { GLASS_GONE, gone_hook } };
    _Static_assert(sizeof sites / sizeof sites[0] == sizeof hook_ids / sizeof hook_ids[0], "a site for each id");
    for (unsigned i = 0; i < sizeof sites / sizeof sites[0]; ++i)
        if (!nes_mod_register_function_entry_plugin(hook_ids[i], sites[i].addr, sites[i].cb)) abort();
    if (!nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.recharge", activate) ||
        !nes_mod_register_savestate_hook("faxanadu.recharge", state_get, state_set) ||
        !nes_mod_register_savestate_validator("faxanadu.recharge", record_validate) || !fax_save_listen(on_save))
        abort();
}
