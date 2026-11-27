/* Seams: the plan for one big world across Seamless's seams (#166,
 * docs/SEAMLESS-COMBAT.md). USA 57dd23d1, chipx86 byte-exact disassembly.
 *
 * Slice 0, seams lose nothing. Two rules in bank 14's sprite loop
 * (Sprites_UpdateAll, $8000):
 *
 *   - **A neighbouring room's copy touches nothing.** Seamless runs each
 *     neighbour's actors on an isolated copy of the machine, against a hero
 *     projected to that room's edge (fax_world.c's shadow()). His
 *     invincibility there keeps enemies and their shots off him, but
 *     CurrentSprite_CheckHitPlayer ($890A) sends every touch to its
 *     category's handler without looking at it: a coin or bread at the edge
 *     was taken (Player_HandleTouchBreadOrCoin, $8968) and its gold or HP
 *     thrown away with the copy's RAM, and an item there
 *     (Player_HandleTouchItem, $88FB) or a person who talks when touched
 *     (Player_HandleTouchNPC) started a script that waits for frames, which
 *     an isolated call cannot run: the call failed, Seamless dropped the
 *     room's tick, and the room stood still while he stood beside it. A hook
 *     at the routine's entry returns at once in those ticks
 *     (fax_world_ticking()), so the live game is the only place the hero is
 *     touched. The coin waits until he walks into its room, which is then
 *     the live game's.
 *
 *   - **The weapon's reach in 16 bits** (the original's bug). Player_DrawWeapon
 *     ($B7D6) places the weapon in 16 bits, PlayerPosXPlus10 ($CE) low and
 *     Maybe_Something_WeaponPosX ($CF) high, against the screen's
 *     Screen_Maybe_ScrollXCounter ($9F); Sprites_IsSpriteOutOfWeaponRange
 *     ($88CB) then compares only $CE with the sprite's X, in 8 bits. So a
 *     swing whose box reaches past the screen's edge, or wraps past 255 with
 *     the reach added, hits a monster at the other edge: from Eolis $04's x
 *     4 facing left, the Long Sword kills a Zozura at its x 236, 232 pixels
 *     behind him. A hook at the routine's entry works the X test out in 16
 *     bits, and when the 8-bit test would hit only by wrapping round the
 *     screen, returns "out of range" itself, as the X test's failure leaves
 *     it; otherwise the original runs. The Y test can't wrap inside a room
 *     (its 208 lines leave the weapon's box below 256), so it is left alone.
 *
 * Slice 1, seams are open ground. Movers (monsters, category 0; their shots,
 * 6; and the people who walk, $34 $41 $42 $44 $45 of category 2) go through
 * a seam side by side, whichever way they walk, between any two rooms, the
 * live game's included:
 *
 *   - **Dormant until seen.** Every mover, and the big enemies (7), stands
 *     still until the first frame it comes within 16 pixels of the view
 *     (fax_world_view(), the 424x240 picture): until then hooks at
 *     Sprites_UpdateBehavior ($A66B) and CurrentSprite_UpdateState ($8BD2)
 *     return at once for it, so it neither moves, seeks him nor is drawn,
 *     in his room as in a neighbour's. Once seen it stays awake (a bit per
 *     slot) until it dies or a load refreshes its room. A shot wholly out of
 *     the view by 16 pixels is removed between frames, as the original
 *     removes one at its screen's edge.
 *   - **The edges open.** The game calls a move past a screen's side blocked
 *     in four places, each hooked: walking left (FUN_PRG14__86bd, $86BD: X
 *     $F1 and up, wrapped past 0), walking right (Sprites_MoveRight, $86A7:
 *     X plus the hitbox's width carries), and flying or a shot
 *     (Sprite_Maybe_TurnAroundIfAtScreenEdgeHoriz after its move, $842A: X
 *     $F0 and up). There, for an awake mover, a joined room that way whose
 *     actors run, with fewer than 5 actors (and movers on their way in), is
 *     open ground: the blocks across are tested in that room's own
 *     ScreenBuffer and BlockProperties, as the game tests its own, and the
 *     slot is marked as through that side. A walker's floor tests that look
 *     past a side, CurrentSprite_CanMoveInDirection's up and down test
 *     ($873A, across its hitbox's columns) and Sprites_CheckPlacementInvalidAtY's
 *     ($855E, its leading column), read the room the column is in the same
 *     way while it straddles a seam. Anything else, or anyone else, finds the
 *     seam a wall as before.
 *   - **The move**, between frames (fax_seams_step(), once every room has
 *     run): a slot whose origin is across (marked left and X 128 and up, or
 *     marked right and X below 128) goes, every byte of it, to a free slot of
 *     the room it entered (the highest, as Sprites_PopulateNextAvailableSprite
 *     takes them), and its old slot is emptied. Side by side the bytes stay
 *     as they are: room 5's x 250 is room 4's x -6, the same byte.
 *   - **Drawn whole.** Hooks at Sprite_Draw_Standard ($F0EC) and
 *     Sprite_Draw_FlippedHoriz ($F181), inside a sprite loop, play the
 *     routine's tile loop in C and keep each cell it skips for lying past the
 *     screen's side (with the Y it would have, the tile, the attributes),
 *     and the slot of each cell it does put in OAM. Seamless paints the
 *     first in the next room's space. A slot whose origin crossed in this
 *     frame is drawn a screen over (its Sprite_TileInfo_OffsetX moved by
 *     one), so its cells are all past the side and painted where it is.
 *   - **Tiles from home.** A slot moving into a room with its entity takes
 *     that slot's CurrentSprites_PPUOffsets; otherwise Seamless paints it
 *     from the CHR of the room its tiles are in, which the block keeps.
 *   - **Towns stay safe** (Cameron's call on #166). A town room is a screen
 *     of a town (fax_zone.c's Eolis and the seven of AREA_TOWNS) whose list
 *     in bank 11 holds no monster: in Eolis $01-$03 and $06-$07, not $04
 *     and $05 (the Zozuras' path) or $08 (the Monodron's, by the way out).
 *     A seam between a town room and one that isn't is a town's edge, and
 *     a wall for every mover, as a full room's is: monsters and their shots
 *     turn back there, and the people who walk stay in.
 *
 * A load within the area (Screen_LoadSpriteInfo, $C1B4, in play) has
 * Seamless forget every other room (fax_world_screen_loaded()).
 *
 * Slice 2, fights across a seam. Everything that touches the hero happens in
 * the live game, through the game's own code, with every feature's hooks:
 *
 *   - **Visitors.** Once the live Sprites_UpdateAll has returned, at Game_MainLoop's
 *     JSR GameLoop_CountdownItems ($DB68), the actors of the rooms joined beside
 *     his that are within reach of him or his spell (monsters, bread and
 *     coins, shots, big enemies; hit counter 0 and no magic hit, as the loop
 *     lets interact) are put in the live room's free slots, nearest first,
 *     its own slots hidden ($FF), and the hook returns into the loop at
 *     @_spritesLoop ($800F) with a prepared stack whose last RTS lands on
 *     $DB68 again. The behaviour and draw hooks return at once for a
 *     visitor, so the pass runs only the loop's interactions: the weapon, the
 *     spell, the shield and the touch, for real. Then each visitor's bytes
 *     go home but its position, and the live slots are put back as they
 *     were. Its room's next tick does the knockback, the death's animation
 *     and the drop.
 *   - **Told apart.** The interaction tests are 8-bit subtractions, which a
 *     visitor's byte passes (room 5's x 250 is room 4's x -6) but which can't
 *     tell a monster across a seam from one at the far side of the room.
 *     Hooks at the entries of Sprite_CheckHitByCastMagic ($8ADC),
 *     Player_CheckShieldHitByMagic ($877C) and CurrentSprite_CheckHitPlayer
 *     ($890A), and slice 0's at the weapon's, work the X test out in 16 bits
 *     (a visitor's X from its side, a slot through a side from its mark) and
 *     refuse a match found only by wrapping round the room. A weapon off the
 *     screen gets its Y and reach placed too (Player_DrawWeapon's BNE at
 *     $B81A), so it hits across a seam at its true height.
 *   - **Copies touch nothing**, the shield included: in a neighbour's tick
 *     the shield check returns at once too (it compared that room's shots
 *     with the live room's shield).
 *   - **A neighbouring room's sounds.** Sound_PlayEffect ($D0E4) in a tick
 *     keeps the effect when the slot that played it is in the view; the live
 *     game plays it at the next pass's site, with a committed call.
 *   - **Sugata's flash** (fax_sugata.c) in a tick, in reach of his true
 *     place, waits at its damage ($AB44) for the live game: the next pass
 *     takes that Sugata as a visitor and enters $AB44 for it first.
 *
 * Slice 3, the spell flies on. The spell (CastMagic_*) stays the live
 * game's; Seams keeps its side, the room it is in, -1, 0 or 1 from his, worked
 * out from its X byte's moves (a few pixels a frame) since the cast, when
 * Player_CastMagic ($BA5B) finds no spell and the next one starts at his X:
 *
 *   - **Walls in the room it is in.** CastMagic_CheckDirection ($BD8F) is
 *     done in C for the sideways moves: the blocks at its X (moving left) or
 *     X plus its width (right), in the room that pixel lies in, the rows
 *     CastMagic_CheckDirection_CheckImpassable tests; past a side with no
 *     room joined it disperses, as at the original's screen edge.
 *   - **It ends at the view's edge,** in his own room too (Cameron's call on
 *     #166): CastMagic_UpdateXPosition's two off-screen tests ($BCD1, $BCF2)
 *     set C once its leading edge passes the view (the 424-wide picture,
 *     worked out again at the cast's entry, from his place this frame) or a
 *     side with nothing joined. So it never reaches a monster that is still
 *     dormant, which wakes 16 pixels before the view; and as a second guard
 *     Sprite_CheckHitByCastMagic refuses a dormant sleeper. Thunder's and
 *     Fire's after a first hit keep the original's own rule too, a byte read
 *     past CAST_MAGIC_MAX_SCREEN_WIDTHS ($B4, $02).
 *   - **Hits.** Its X in 16 bits for the told-apart test and for choosing
 *     visitors: across a seam it meets that room's actors as visitors.
 *   - **Drawn on its side.** Its draw (inside Player_CastMagic, before the
 *     sprite loop) is drawn a screen over by its side, and slice 1's replay
 *     marks its OAM cells as the spell's and keeps those past the side.
 *     CastMagic_CalculateVisibility ($C315), its behind-the-background bits,
 *     reads the blocks of the room each of its two pixels lies in.
 *   - **A crossing keeps it.** Seamless's adopt() asks fax_seams_adopted(),
 *     which moves its side by the rooms' places; a crossing up or down still
 *     ends it.
 *
 * Slice 5, one above the other. Slices 1 to 3 across seams where one room
 * sits above another (a room is 208 tall, so a room above's y 200 is his
 * room's -8, a byte of 248 there):
 *
 *   - **Fliers and fallers go through.** The tests that call a move past a
 *     room's top or bottom blocked are hooked as the sides' are: moving down,
 *     CurrentSprite_CanMoveInDirection's row at the hitbox's bottom ($871C:
 *     the original blocks it from $F0, and reads the ScreenBuffer's rows 13
 *     and 14, the screen's undrawn data, and 15, zero); moving up, its row at Y
 *     ($8731: $F0 and up, wrapped past the top, blocked); the fliers that
 *     bounce off a screen's top and bottom without a block test
 *     (Sprite_CapToBlockOppositeFalling, $85AD: Y $E0 and up); a fall
 *     (CurrentSprite_HandleFall's CMP #$C0 at $865B, after its 8 pixels: the
 *     original removes a faller at y 192, "off the screen"); and the
 *     walkers' floor test ($855E), at Y + its height + 1. With a room joined
 *     that way, an awake mover's row is that room's, its blocks tested in
 *     the room each column lies in, corners included, and the slot is marked
 *     through the top or bottom. A full room or a town's edge is a wall (a
 *     floor) there; with nothing joined the original decides. The walkers'
 *     and fliers' sideways tests read a straddler's rows across both rooms.
 *   - **The move.** A slot whose origin is past the bottom (marked, Y 208 and
 *     up) or the top (marked, Y $80 and up) moves to the room there between
 *     frames, its Y less or plus 208, the rest of its bytes as they are (its
 *     behaviour's BehaviorState_YFull is a speed, not a place). One in from
 *     below straddles its new room's bottom, and is marked so.
 *   - **Drawn whole up and down.** A cell past the room's top or bottom is
 *     kept as one past a side is, and painted in the room joined there: the
 *     game skips a cell whose screen Y carries past 255, and the NES hides
 *     one at line 240 and down. A slot whose origin crossed the top this
 *     frame is drawn from its wrapped byte, a screen below: all its cells
 *     are skipped, so none lands on the status bar, and are kept where they
 *     truly lie.
 *   - **Visitors from above and below.** The visitors' pass takes the actors
 *     of the rooms joined above and below his too, at their Y less or plus
 *     208 (its byte his room's: -8 is 248). The interaction tests' Y needs
 *     no telling apart: within reach of him (48 pixels), no Y test wraps.
 *   - **His true Y.** Rooms above and below see the hero at his true Y in
 *     their pixels, held to 0-239 (Seamless's shadow()).
 *   - Spells stay as slice 3 left them: one at a seam above or below ends,
 *     and so does one when he crosses up or down.
 *
 * Each room's state is a block in Seamless's record (version 6); the pass,
 * the sounds still to play and the spell's side are this feature's own
 * record (version 2), since a frame can end inside the pass (the main loop
 * lags on a busy screen): then, between frames, the machine shows the live
 * slots as the game has them, the pass's kept until the next frame begins.
 * All of it is part of the remaster: with it off, the original's walls and
 * its ghost hit stay. See docs/GAME_INTERNALS.md, "Weapon reach" and "Seams:
 * actors across rooms", and "Features and what they use". */
#include "fax_seams.h"
#include "fax_world.h"
#include "fax_zone.h"
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

/* Bank 14 */
/* Sprites_UpdateAll: LDA Sprites_UpdatesPaused, BNE @_spritesLoop, LDA DurationHourGlass */
#define SPRITES_UPDATE_ALL   0x8000u
#define SPRITES_LOOP         0x800Fu /* Sprites_UpdateAll's @_spritesLoop, past INC SpriteUpdateCounter: LDX #$07 */
#define SPRITES_LOOP_DONE    0x8049u /* Sprites_UpdateAll, past the slots' loop: LDA #$00, STA Sprites_PPUOffset, RTS */
#define CHECK_SHIELD         0x877Cu /* Player_CheckShieldHitByMagic: LDA SelectedShield, CMP #$03, BCS RETURN_87CA */
/* Sprite_CheckHitByCastMagic: LDA CastMagic_Type, CMP #$05, BCS RETURN_8AD7,
 * LDX CurrentSpriteIndex */
#define CHECK_MAGIC          0x8ADCu
#define CAST_WIDTHS          0x8B73u /* CAST_MAGIC_HIT_WIDTHS: a spell's width, by CastMagic_Type */
#define CAST_HEIGHTS_HIT     0x8B78u /* CAST_MAGIC_HIT_HEIGHTS: its height */
/* Player_CastMagic: LDA CastMagic_Type, BPL CastMagic_RunSpellHandler,
 * JSR Player_IsClimbing */
#define PLAYER_CAST_MAGIC    0xBA5Bu
/* CastMagic_UpdateXPosition moving left, after JSR CastMagic_CheckDirection
 * and LDA CastMagic_XPos_Full: CMP #$F0, RTS */
#define CAST_LEFT_EDGE       0xBCD1u
/* moving right, after LDX CastMagic_Type, LDA CastMagic_XPos_Full:
 * CMP CAST_MAGIC_MAX_SCREEN_WIDTHS,X, RTS */
#define CAST_RIGHT_EDGE      0xBCF2u
/* CAST_MAGIC_MAX_SCREEN_WIDTHS: $F0 $F0 $E0 $F0 $F0, then
 * CastMagic_UpdateYPosition's code, read for the types past 4 */
#define CAST_MAX_WIDTHS      0xBCF6u
/* CastMagic_CheckDirection: TXA, BEQ _Left, DEX, BEQ _Right (X 0 left,
 * 1 right; 2 and 3, up and down, give 0) */
#define CAST_CHECK_DIRECTION 0xBD8Fu
#define CAST_MOVE_WIDTHS     0xBD9Du /* CAST_MAGIC_WIDTHS: a spell's width less 1, by CastMagic_Type (12) */
#define CAST_HEIGHTS         0xBDA9u /* CAST_MAGIC_HEIGHTS: its height less 1 (12) */
#define FLASH_HIT            0xAB44u /* SpriteBehavior_FlashScreenHitPlayer's damage: LDA #$04, JSR Sound_PlayEffect */
/* Player_DrawWeapon: BNE RETURN_B7D5, after STA Maybe_Something_WeaponPosX,
 * CMP Screen_Maybe_ScrollXCounter; then the Y and the reach */
#define WEAPON_OFF_SCREEN    0xB81Au
/* Sprite_Maybe_TurnAroundIfAtScreenEdgeHoriz, after JSR Sprite_MoveForwardX:
 * LDA CurrentSprites_XPos_Full,X, CMP #$F0, BCC @_returnFalse */
#define FLIER_EDGE           0x842Au
/* Sprites_CheckPlacementInvalidAtY: JSR Area_ConvertPixelsToBlockPos,
 * JMP Area_IsBlockImpassableOrLadder (Arg_PixelPosX = X + 0 or 15 by facing) */
#define PLACEMENT_BLOCK      0x855Eu
/* Sprites_MoveRight: LDX CurrentSprites_HitBoxTypes,Y, LDA CurrentSprites_XPos_Full,Y,
 * CLC, ADC SPRITE_HITBOX_WIDTHS,X, STA Arg_PixelPosX, BCC (test), else blocked */
#define MOVE_RIGHT           0x86A7u
/* FUN_PRG14__86bd: LDA CurrentSprites_XPos_Full,Y, CMP #$F1, BCS (blocked),
 * STA Arg_PixelPosX, then the test (Sprites_MoveRight__86c6) */
#define MOVE_LEFT            0x86BDu
/* CurrentSprite_CanMoveInDirection, up and down: LDA CurrentSprites_XPos_Full,Y,
 * STA Arg_PixelPosX, JSR Area_ConvertPixelsToBlockPos, then the hitbox's columns */
#define MOVE_VERTICAL        0x873Au
/* CurrentSprite_CanMoveInDirection moving down, past DEX, BEQ: LDX
 * CurrentSprites_HitBoxTypes,Y, LDA CurrentSprites_YPos,Y, CLC, ADC
 * SPRITE_HITBOX_HEIGHTS,X, STA Arg_PixelPosY, CMP #$F0, BCC $873A */
#define MOVE_DOWN            0x871Cu
/* ... moving up (@LAB_PRG14__8731): LDA CurrentSprites_YPos,Y, CMP #$F0,
 * BCS (blocked), STA Arg_PixelPosY, then $873A */
#define MOVE_UP              0x8731u
/* Sprite_CapToBlockOppositeFalling: LDA CurrentSprites_YPos,X, CMP #$E0,
 * BCC @_return (C clear: not at the screen's top or bottom) */
#define CAP_VERTICAL         0x85ADu
/* CurrentSprite_HandleFall, after LDA CurrentSprites_YPos,X, CLC, ADC #$08,
 * STA CurrentSprites_YPos,X: CMP #$C0, BCS @_clearSprite, LDX #$03, JSR
 * CurrentSprite_CanMoveInDirection */
#define FALL_EDGE            0x865Bu
/* Sprites_IsSpriteOutOfWeaponRange: LDA CurrentSprites_Entities,X, ASL A,
 * TAY, LDA MAYBE_SPRITE_EXTENTS,Y, CLC, ADC Maybe_WeaponRange_X, STA Temp_00 */
#define OUT_OF_WEAPON_RANGE  0x88CBu
/* CurrentSprite_CheckHitPlayer: LDX CurrentSpriteIndex,
 * LDA CurrentSprites_Entities,X, CMP #$FF, BEQ RETURN_8909, JSR ... */
#define CHECK_HIT_PLAYER     0x890Au
/* CurrentSprite_UpdateState: LDA IScript_PortraitID, BPL @_return, LDX CurrentSpriteIndex */
#define UPDATE_STATE         0x8BD2u
#define UPDATE_BEHAVIOR 0xA66Bu /* Sprites_UpdateBehavior: LDA DurationHourGlass, BMI, LDY CurrentSprites_Entities,X */
#define SPRITE_EXTENTS  0xB407u /* MAYBE_SPRITE_EXTENTS: (EX, EY) per entity */
#define HITBOX_WIDTHS   0xB4D1u /* SPRITE_HITBOX_WIDTHS: a hitbox type's width less 1 */
#define HITBOX_HEIGHTS  0xB4D8u /* SPRITE_HITBOX_HEIGHTS: its height less 1 */
#define CATEGORIES      0xB544u /* SPRITE_CATEGORIES_BY_ENTITY */
/* Bank 11 */
#define AREA_SPRITE_ADDRESSES 0x8210u /* per area, a pointer to its screens' sprite lists, a word a screen */
/* Bank 15 */
#define LOAD_SPRITE_INFO      0xC1B4u /* Screen_LoadSpriteInfo: LDA $0100, PHA, LDX #$0B, JSR MMC1_UpdateROMBank */
/* CastMagic_CalculateVisibility: LDA #$00, STA Temp_MovingSpriteVisibility,
 * LDA CastMagic_YPos_Full, STA Arg_PixelPosY */
#define CAST_VISIBILITY       0xC315u
/* GameLoop_CheckUseCurrentItem: LDA Player_Flags, AND #$05, BNE; both main
 * loops call it right after Player_CastMagic */
#define CHECK_USE_ITEM        0xC478u
/* Sound_PlayEffect: STA Temp_SoundIDToPlay, ...: A the effect; keeps X and Y,
 * memory only */
#define SOUND_PLAY_EFFECT     0xD0E4u
#define MAIN_LOOP_COUNTDOWN   0xDB68u /* Game_MainLoop: JSR GameLoop_CountdownItems, after JSR Sprites_UpdateAll */
#define BLOCK_IMPASSABLE      0xE8D9u /* BLOCK_PROPERTY_IMPASSABLE_MAP: 1 for a solid block property */
#define DRAW_STANDARD         0xF0ECu /* Sprite_Draw_Standard: LDA #$00, STA Temp_Sprites_TileYOffset, TAY */
#define DRAW_FLIPPED       0xF181u /* Sprite_Draw_FlippedHoriz: LDA #$00, STA Temp_05, LDY Temp_Sprites_ColsRemaining */
#define VISIBILITY_MASK    0xF224u /* DRAW_SPRITE_VISIBILITY_MASK: 1, 2 (standard) */
#define VISIBILITY_FLIPPED 0xF226u /* BYTE_ARRAY_PRG15_MIRROR__f226: 2, 1 (flipped) */
#define TILE_POSITIONS     0xF23Du /* SPRITE_TILE_BLOCK_POSITIONS: 0, 8, 16 ... */
/* RAM */
#define TEMP_00            0x0000u   /* the X test's limit, EX + the reach */
#define TEMP_ADDR_L        0x0002u   /* Temp_Addr_L: the flip's pivot (Sprite_Draw) */
#define START_SLOT_RANGE   0x001Cu   /* Sprites_StartSlotRange */
#define SPRITE_SLOT        0x0025u   /* Sprites_SpriteSlot: the next OAM entry, as Sprites_IncNextSpriteSlot counts */
#define MOVING_VISIBILITY  0x0026u   /* MovingSpriteVisibility */
#define FLIP_MASK          0x0029u   /* CurrentSprite_FlipMask */
#define PPU_OFFSET         0x0033u   /* Sprites_PPUOffset */
#define SLOTS_FULL         0x0039u   /* Sprites_SlotsFull */
#define READ_ADDR          0x003Au   /* Sprite_TileInfo_ReadAddr_L/_U */
#define NORM_X             0x003Cu   /* Temp_Sprites_NormXPos */
#define NORM_Y             0x003Du   /* Temp_Sprites_NormYPos */
#define OFFSET_X           0x003Eu   /* Sprite_TileInfo_OffsetX: the high byte of a cell's X, 0 on the screen */
#define OFFSET_Y           0x003Fu   /* Sprite_TileInfo_OffsetY */
#define COLS               0x0040u   /* Temp_Sprites_ColsRemaining: columns less 1 */
#define ROWS               0x0041u   /* Temp_Sprites_RowsRemaining: rows less 1 */
#define HERO_X             0x009Eu   /* Player_PosX_Block: his left edge, in the live room */
#define SCROLL_X           0x009Fu   /* Screen_Maybe_ScrollXCounter: the screen's high byte of X */
#define HERO_Y             0x00A1u   /* Player_PosY: his top */
#define PIXEL_X            0x00B5u   /* Arg_PixelPosX */
#define PIXEL_Y            0x00B6u   /* Arg_PixelPosY */
#define BLOCKS_RESULT      0x00B7u   /* Blocks_Result: nonzero, blocked */
#define TEMP_VISIBILITY    0x00B8u   /* Temp_MovingSpriteVisibility */
#define SLOT_X             0x00BAu   /* CurrentSprites_XPos_Full, 8 slots */
#define SLOT_Y             0x00C2u   /* CurrentSprites_YPos */
#define WEAPON_X           0x00CEu   /* PlayerPosXPlus10: the weapon's X, low byte */
#define WEAPON_X_HIGH      0x00CFu   /* Maybe_Something_WeaponPosX: its high byte */
#define WEAPON_RANGE_X     0x00D2u   /* Maybe_WeaponRange_X */
#define CURRENT_ROM_BANK   0x0100u   /* CurrentROMBank */
#define CAST_TYPE          0x02B3u   /* CastMagic_Type: 0-4 a spell on, 5-11 one after its first hit, $FF none */
#define CAST_FLAGS         0x02B4u   /* CastMagic_Flags: $40 moving right */
#define CAST_X             0x02B6u   /* CastMagic_XPos_Full */
#define CAST_Y             0x02B8u   /* CastMagic_YPos_Full */
#define SLOT_ARRAYS        0x02BCu   /* CurrentSprites_XPos_Frac ... CurrentSprites_Values: 23 arrays of 8, to $0373 */
#define ENTITIES           0x02CCu   /* CurrentSprites_Entities */
#define BEHAVIORS          0x02D4u   /* CurrentSprites_Behaviors: the behaviour running, bit 7 once finished */
#define FLAGS              0x02DCu   /* CurrentSprites_Flags: bit 0 facing right, $40 the behaviour under way */
#define BEHAVIOR_DATA2     0x02ECu   /* CurrentSprites_BehaviorData2 */
#define HITBOX             0x0304u   /* CurrentSprites_HitBoxTypes */
#define PPU_OFFSETS        0x032Cu   /* CurrentSprites_PPUOffsets */
#define HIT_BY_MAGIC       0x0334u   /* CurrentSprites_HitByMagicBehavior: bit 7 set, none */
#define HIT_COUNTER        0x034Cu   /* CurrentSprites_HitCounter: frames of a weapon's knockback left */
#define CURRENT_SPRITE_INDEX 0x0378u /* CurrentSpriteIndex */
#define SPRITE_X             0x037Fu /* Something_Maybe_WeaponDistanceX: the caller's copy of the sprite's X */
#define SHIELD_X             0x039Bu /* Player_ShieldPositionX: his X plus 0 or 8 (SHIELD_SPRITE_OFFSETS_X), 8 bits */
#define BOX_LEFT             0x03E2u /* SpriteBox_Left: Sprite_GetBounds' X plus its table's offset, 8 bits */
#define BOX_WIDTH            0x03E4u /* SpriteBox_Width */
#define SPRITES_PAUSED       0x0426u /* Sprites_UpdatesPaused: the loop skips every interaction */
#define PLAYER_IS_DEAD       0x0438u /* PlayerIsDead */
#define BLOCK_PROPERTIES     0x043Cu /* BlockProperties: a nibble per block id */
#define SCREEN_BUFFER        0x0600u /* ScreenBuffer: 16 blocks a row */
/* Flags */
enum { FLAG_C = 0x01, FLAG_Z = 0x02, FLAG_V = 0x40, FLAG_N = 0x80 };

enum {
    EMPTY = 0xFF,
    SHOT = 6,
    BIG = 7,
    PEOPLE = 2,
    BREAD_OR_COIN = 1,
    THROUGH_LEFT = 1,
    THROUGH_RIGHT = 2, /* a slot let through its room's side this frame, or straddling it */
    THROUGH_UP = 4,
    THROUGH_DOWN = 8, /* ... its top or bottom (slice 5): marks, one each way at most */
    SIDEWAYS = THROUGH_LEFT | THROUGH_RIGHT,
    UPRIGHT = THROUGH_UP | THROUGH_DOWN,
    ROOM_H = 208,   /* a room's height: one above another's y 200 is its -8 */
    FALLING = 0x80, /* CurrentSprites_Flags: moving down (Sprite_CalculateNewVertPos) */
    ROOM_LIMIT = 5, /* a room takes no mover once it holds this many (the most a list holds) */
    PAD = 16,       /* the view's margin: what an actor draws past its hitbox */
    VIEW_W = 424,
    VIEW_H = 208, /* the view below the status bar */
    CELLS = 48,
    ROOMS = 83,
    LIST_MAX = 16, /* entries a list may hold here (the ROM's longest has 5) */
    REACH = 48,    /* a visitor's box within this of the hero's (the weapon's offset and reach
                                               are 40 at most) */
    HERO_W = 16,
    HERO_H = 32,
    SLOT_BYTES = 25, /* a slot's every byte: X, Y and the 23 arrays */
    FIRST_KEPT = 4,  /* of those, the first a visitor takes home: the entity (past the
                                               positions and their fractions) */
    TOUCH_PAD = 11,
    SHIELD_PAD = 7, /* what CurrentSprite_CheckHitPlayer and Player_CheckShieldHitByMagic add */
    SUGATA = 0x18,
    BEHAVIOR_FLASH = 0x1F,
    BEHAVIOR_READY = 0x40,
    HEARD = 8,               /* a frame's neighbours' sounds kept at most */
    SPELL = FAX_SEAMS_SPELL, /* the owner, less 1, of the spell's OAM cells and cells past a side */
    SPELL_TYPES = 12,        /* CastMagic_Type's values, each spell and its after-hit kinds */
    MOVING_RIGHT = 0x40,
    HIDDEN = 0x10,   /* CastMagic_Flags; CurrentSprites_Flags (Sprites_IsSpriteHidden) */
    HIDDEN_Y = 0xEF, /* OAM Y from which the NES shows no line of a sprite: line 240 and down */
};

/* A room's block in Seamless's record. */
typedef struct {
    uint8_t awake;          /* a bit per slot: seen once, so it runs */
    uint8_t count;          /* cells past the room's sides, in its last finished sprite loop */
    uint8_t through[8];     /* 0, THROUGH_LEFT or THROUGH_RIGHT */
    uint8_t tiles[8];       /* 0: the room's own CHR; else 1 + the room whose CHR has the slot's tiles */
    uint8_t drawn_tiles[8]; /* tiles as they were when the slot was last drawn: the picture's */
    uint8_t owner[64];      /* per OAM entry: 1 + the slot that drew it, 0 none */
    FaxSeamsCell cell[CELLS];
    uint8_t flash; /* a bit per slot: its Sugata's flash waits for the live game (slice 2) */
} Block;
_Static_assert(sizeof(Block) <= FAX_WORLD_SEAMS_BYTES, "Seams' block fits Seamless's record");

/* What a sprite loop drew, until Seamless captures the room it ran:
 * [0] the live game's, [1] a neighbour's tick. */
typedef struct {
    uint8_t owner[64], count, tiles[8];
    FaxSeamsCell cell[CELLS];
} Drawn;
static Drawn drawn[2];
static bool looping[2];
static int loop_room[2] = { -1, -1 };

static bool enabled;
static unsigned skipped;           /* CheckHitPlayer calls skipped in neighbours' ticks */
static unsigned refused;           /* weapon matches refused: they hit only by wrapping (live game) */
static int last_weapon_x = 0x7FFF; /* the last refused one's weapon X, in 16 bits from the screen's left */
static unsigned last_sprite_x, last_entity = 0xFF;
/* Slice 1's counts, for the TCP command. */
static unsigned steps, moves, kept_out, town_edge, removed, woken, frozen, pushed_back;
/* Slice 5's: falls carried on past the original's y 192 (into a room
 * below), and moves up or down. */
static unsigned fell_on, moves_up, moves_down;
static int last_woken[4] = { -1, -1, -1, -1 }; /* room, slot, entity, step */
enum { LOG = 16 };
static int move_log[LOG][6]; /* the last moves: from, slot, to, slot, entity, step */

/* Slice 2: the visitors' pass, while it is under way (open), and the
 * neighbours' sounds. Both are in this feature's record. */
typedef struct {
    int8_t room;
    uint8_t slot, live, flash;
    int16_t shift;
} Visitor;
static struct {
    uint8_t open, count;
    uint8_t swapped;              /* a frame ended inside the pass: the machine holds `saved`, and `view` waits */
    Visitor v[8];                 /* each from slot `slot` of `room`, in live slot `live`; its live X is its byte
                                   + `shift`; `flash`: Sugata's damage is run for it first */
    uint8_t saved[8][SLOT_BYTES]; /* the live slots as they were */
    uint8_t view[8][SLOT_BYTES];  /* the pass's slots, while a frame's end has them out */
} pass;
typedef struct {
    uint8_t effect, room, slot;
} Sound;
static Sound heard[HEARD], pending[HEARD]; /* this frame's ticks'; to play at the next pass's site */
static unsigned heard_count, pending_count;
/* Slice 2's counts, for the TCP command. */
static unsigned passes, visits, waited, flash_runs, flashes_due, broken;
static unsigned touch_refused, magic_refused, shield_refused, shield_skipped, weapon_placed, heard_total, played;
static int visit_log[LOG][6];   /* the last visits: room, slot, live slot, entity, after, step */
static int sound_log[HEARD][4]; /* the last sounds played: room, slot, effect, step */
static int live_shift(unsigned s);

/* Slice 3: the spell's side, the room it is in less his (-1, 0 or 1), with
 * its X byte when last seen (so its X in 16 bits is side * 256 + byte); in
 * this feature's record. resync: take the byte as his room's at the next look
 * (a record of version 1). casting: Player_CastMagic is under way, so a draw
 * is the spell's. */
static struct {
    int8_t side;
    uint8_t last, resync, casting;
} spell = { 0, 0, 1, 0 };
static Drawn spell_drawn; /* what the spell drew this frame, for his room's capture */
/* Slice 3's counts, for the TCP command. */
static unsigned spell_walls, spell_view_ends, spell_side_ends, spell_crossed, spell_cleared, magic_dormant;
static unsigned spell_visibility, spell_drawn_abroad;
static int spell_end[5] = { 0, 0, 0, -1, -1 }; /* the last end: its X in 16 bits, the edge, why (1 the view,
                                                         2 a side with nothing joined, 3 its own rule), step, right */
static int spell_x16(void);
static bool dormant(unsigned s);

/* ---- slice 0 ---- */

/* The weapon's X in 16 bits from the screen's left edge: its low byte, and
 * its high byte against the screen's (-1 left of the screen, 1 right of it). */
static int weapon_x16(uint8_t low, uint8_t high, uint8_t screen) {
    return (int)(int8_t)(uint8_t)(high - screen) * 256 + low;
}

/* Sprites_IsSpriteOutOfWeaponRange's X test, as the game does it in 8 bits:
 * in range when (weapon x + reach - sprite x) mod 256 < extent + reach. */
static bool hits_8bit(uint8_t weapon_x, uint8_t reach, uint8_t sprite_x, uint8_t extent) {
    return (uint8_t)(weapon_x + reach - sprite_x) < (uint8_t)(extent + reach);
}

/* Whether the 8-bit X test hits only by wrapping round the screen: the same
 * sum in 16 bits is outside 0-255, so the box and the sprite are a screen
 * apart. The sprite's X in 16 bits too: on the screen (0-255), but past a
 * side for a visitor or a slot let through it (slice 2). */
static bool wraps(int weapon_x, uint8_t reach, int sprite_x, uint8_t extent) {
    int d = weapon_x + reach - sprite_x;
    return hits_8bit((uint8_t)weapon_x, reach, (uint8_t)sprite_x, extent) && (d < 0 || d > 255);
}

/* Sprites_IsSpriteOutOfWeaponRange's entry. X is the sprite's slot; its X is
 * in Something_Maybe_WeaponDistanceX (Player_HitSpriteWithWeapon stores it
 * just before the JSR). Bank 14 is mapped, so a peek reads the extents the
 * routine would. A refusal leaves what the X test's failure leaves (A the
 * 8-bit difference, Y the entity times 2, Temp_00 the limit, the flags of
 * its SBC and CMP) but with C set: out of range. The one caller, at $8838,
 * reads only C (BCC, else JMP @_return). */
static int reach_hook(uint16_t addr) {
    (void)addr;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    uint8_t entity = cyc_mod_peek((uint16_t)(ENTITIES + (regs.x & 7u)));
    uint8_t index = (uint8_t)(entity << 1);
    uint8_t extent = cyc_mod_peek((uint16_t)(SPRITE_EXTENTS + index));
    uint8_t reach = cyc_mod_peek(WEAPON_RANGE_X), sprite_x = cyc_mod_peek(SPRITE_X);
    int weapon_x = weapon_x16(cyc_mod_peek(WEAPON_X), cyc_mod_peek(WEAPON_X_HIGH), cyc_mod_peek(SCROLL_X));
    int sprite_x16 = sprite_x + (cyc_mod_isolated() ? 0 : live_shift(regs.x & 7u));
    if (!wraps(weapon_x, reach, sprite_x16, extent)) return 0; /* the original decides */
    uint8_t limit = (uint8_t)(extent + reach), sum = (uint8_t)(weapon_x + reach);
    uint8_t difference = (uint8_t)(sum - sprite_x), compare = (uint8_t)(difference - limit);
    uint8_t p = (uint8_t)(regs.p & ~(FLAG_C | FLAG_Z | FLAG_V | FLAG_N));
    if ((sum ^ sprite_x) & (sum ^ difference) & 0x80) p |= FLAG_V; /* SBC's overflow */
    if (compare & 0x80) p |= FLAG_N;                               /* CMP's N; never Z: difference < limit */
    regs.a = difference;
    regs.y = index;
    regs.p = (uint8_t)(p | FLAG_C);
    cyc_mod_poke(TEMP_00, limit);
    cyc_mod_set_regs(&regs);
    if (!cyc_mod_isolated()) { /* a neighbour's copy never swings (shadow()), but count only the live game */
        ++refused;
        last_weapon_x = weapon_x;
        last_sprite_x = sprite_x;
        last_entity = entity;
    }
    return 1;
}

/* Return from a hooked routine with X the current slot, its N and Z as LDX
 * CurrentSpriteIndex leaves them, and A and C as given (A < 0: kept). */
static void leave(unsigned slot, int a, int carry) {
    CycModRegs regs;
    cyc_mod_regs(&regs);
    regs.x = (uint8_t)slot;
    if (a >= 0) regs.a = (uint8_t)a;
    regs.p = (uint8_t)((regs.p & ~(FLAG_Z | FLAG_N)) | (regs.x ? 0 : FLAG_Z) | (regs.x & FLAG_N));
    if (carry >= 0) regs.p = (uint8_t)((regs.p & ~FLAG_C) | (carry ? FLAG_C : 0));
    cyc_mod_set_regs(&regs);
}

/* The 8-bit overlap test the interactions make, `a` + `pad` - `left` (mod
 * 256) below `width` + `pad` (mod 256), finds a match only by wrapping round
 * the room: in 16 bits the difference is outside 0-255, the two a room
 * apart. A width of $80 and up is no box but one of the game's tricks
 * (Sprite_GetBounds' tables for phases of Sir Gawaine, the Maskman and the
 * Wolfman, $F0 and $F8, whose sum with the pad carries): the original decides those. */
static bool wrapped(int a, int left, uint8_t width, uint8_t pad) {
    if (width >= 0x80) return false;
    int d = a + pad - left;
    return (uint8_t)d < (uint8_t)(width + pad) && (d < 0 || d > 255);
}
/* The current sprite's box's left (SpriteBox_Left, which Sprite_GetBounds
 * set from its X and its table's offset, -16 to 48, in 8 bits) in 16 bits. */
int fax_seams_box_left(void) {
    unsigned s = cyc_mod_peek(CURRENT_SPRITE_INDEX) & 7u;
    uint8_t x = cyc_mod_peek((uint16_t)(SLOT_X + s));
    return fax_seams_x16(s) + (int8_t)(uint8_t)(cyc_mod_peek(BOX_LEFT) - x);
}

/* CurrentSprite_CheckHitPlayer's entry: in a neighbouring room's tick the
 * routine returns at once, with X as its first instruction (LDX
 * CurrentSpriteIndex) leaves it; CurrentSprite_UpdateState, next in the
 * loop, loads X again anyway. In the live game (slice 2) it returns the
 * same way when its X test would match only by wrapping round the room (the
 * hero's left, from 0 to 255, against the sprite's box in 16 bits); its
 * failure leaves nothing anyone reads. Anywhere else the original runs. */
static int touch_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) {
        if (!fax_world_ticking()) return 0;
        leave(cyc_mod_peek(CURRENT_SPRITE_INDEX), -1, -1);
        ++skipped;
        return 1;
    }
    unsigned s = cyc_mod_peek(CURRENT_SPRITE_INDEX) & 7u;
    if (cyc_mod_peek((uint16_t)(ENTITIES + s)) == EMPTY ||
        !wrapped(cyc_mod_peek(HERO_X), fax_seams_box_left(), cyc_mod_peek(BOX_WIDTH), TOUCH_PAD))
        return 0;
    leave(s, -1, -1);
    ++touch_refused;
    return 1;
}

/* ---- slice 1: the rooms and their slots ---- */

static uint8_t rom11(unsigned addr) { return fax_world_rom(11, addr); }
static uint8_t rom14(unsigned addr) { return fax_world_rom(14, addr); }
static uint8_t rom15(unsigned addr) { return fax_world_rom(15, addr); }
static Block *block(int room) { return (Block *)fax_world_seams_block(room); }

/* The room whose loop the machine is running, or -1 when there is none or
 * the live machine holds another (a load under way: no view). */
static int running(void) {
    int room = fax_world_seams_room(), x, y;
    if (room >= 0 && !cyc_mod_isolated() && !fax_world_view(&x, &y)) return -1;
    return room;
}

/* A room's RAM: the machine's own while it runs that room (a neighbour in
 * its tick, the active room otherwise), else the room's record. */
static bool here(int room) {
    return room >= 0 && room == (cyc_mod_isolated() ? fax_world_seams_room() : fax_world_active_room());
}
static uint8_t rd(int room, unsigned addr) {
    if (here(room)) return cyc_mod_peek((uint16_t)addr);
    const uint8_t *ram = fax_world_room_record(room);
    return ram ? ram[addr & 0x7FF] : 0;
}
static void wr(int room, unsigned addr, uint8_t value) {
    if (here(room)) {
        cyc_mod_poke((uint16_t)addr, value);
        return;
    }
    uint8_t *ram = fax_world_room_record(room);
    if (ram) ram[addr & 0x7FF] = value;
}

static unsigned category(uint8_t entity) { return entity <= 0x65 ? rom14(CATEGORIES + entity) : EMPTY; }
static bool walker(uint8_t e) { return e == 0x34 || e == 0x41 || e == 0x42 || e == 0x44 || e == 0x45; }
/* Goes through seams: monsters, their shots, the people who walk. */
static bool mover(uint8_t e) {
    unsigned c = category(e);
    return c == 0 || c == SHOT || (c == PEOPLE && walker(e));
}
/* Dormant until seen: the movers, and the big enemies, who stay home. */
static bool sleeper(uint8_t e) { return mover(e) || category(e) == BIG; }
static unsigned hitbox(int room, unsigned s) {
    unsigned h = rd(room, HITBOX + s);
    return h > 6 ? 6 : h;
}
static unsigned width_less_1(int room, unsigned s) { return rom14(HITBOX_WIDTHS + hitbox(room, s)); }
static unsigned height_less_1(int room, unsigned s) { return rom14(HITBOX_HEIGHTS + hitbox(room, s)); }

/* A slot's X in 16 bits from its room's left edge: past the left side once
 * let through it and wrapped, past the right once it wrapped the other way. */
static int x16_of(const Block *b, unsigned s, uint8_t x) {
    if ((b->through[s] & THROUGH_LEFT) && x >= 128) return x - 256;
    if ((b->through[s] & THROUGH_RIGHT) && x < 128) return x + 256;
    return x;
}
static int x16(int room, unsigned s) { return x16_of(block(room), s, rd(room, SLOT_X + s)); }
/* Its Y in 16 bits from its room's top (slice 5): past the top once let
 * through it and wrapped ($80 and up), or, not through the bottom, at $F0
 * and up, which the game's own tests take as wrapped past the top; past the
 * bottom, 208 and up, is the byte itself. */
static int y16_of(const Block *b, unsigned s, uint8_t y) {
    unsigned t = b->through[s];
    return ((t & THROUGH_UP) && y >= 128) || (!(t & THROUGH_DOWN) && y >= 0xF0) ? y - 256 : y;
}
static int y16(int room, unsigned s) { return y16_of(block(room), s, rd(room, SLOT_Y + s)); }

/* Whether the slot's hitbox, with `pad` pixels round it, meets the view. */
static bool in_view(int room, unsigned s, int pad) {
    int vx, vy, px, py;
    if (!fax_world_view(&vx, &vy)) return false;
    fax_world_place(room, &px, &py);
    unsigned h = hitbox(room, s);
    int x = px + x16(room, s), y = py + y16(room, s);
    int w = rom14(HITBOX_WIDTHS + h) + 1, t = rom14(HITBOX_HEIGHTS + h) + 1;
    return x + w + pad > vx && x - pad < vx + VIEW_W && y + t + pad > vy && y - pad < vy + VIEW_H;
}

/* ScreenBuffer_IsBlockImpassable for the block at `index` of a room: its
 * block id's property nibble, through BLOCK_PROPERTY_IMPASSABLE_MAP. With
 * `ladder`, Area_IsBlockImpassableOrLadder: a ladder (property 2) too. */
static uint8_t property(int room, unsigned index) { /* ScreenBuffer_LoadBlockProperty */
    uint8_t id = rd(room, SCREEN_BUFFER + (index & 0xFF));
    uint8_t bits = rd(room, BLOCK_PROPERTIES + (id >> 1));
    return id & 1 ? bits >> 4 : bits & 15;
}
static uint8_t solid(int room, unsigned index, bool ladder) {
    uint8_t property_ = property(room, index);
    if (ladder && property_ == 2) return 1;
    return rom15(BLOCK_IMPASSABLE + property_);
}
/* The room `dx` rooms across and `dy` down from `room` (each -1, 0 or 1)
 * through the joined rooms whose actors run, sideways then up or down or
 * the other way round (the map is a grid: both lead to one place); -1 for
 * none. */
static int room_by(int room, int dx, int dy) {
    if (room < 0) return -1;
    int h = dx ? fax_world_joined(room, dx > 0) : room;
    int a = dy && h >= 0 ? fax_world_joined(h, dy > 0 ? 3 : 2) : h;
    if (a >= 0 || !dx || !dy) return a;
    int v = fax_world_joined(room, dy > 0 ? 3 : 2);
    return v >= 0 ? fax_world_joined(v, dx > 0) : -1;
}
/* The room pixel column `px` lies in, from `room`'s left edge: the room
 * joined left below 0, right from 256; -1 for none. */
static int room_at(int room, int px) { return room_by(room, px < 0 ? -1 : px >= 256 ? 1 : 0, 0); }
/* The block pixel (px, py) of `room` (16 bits from its top-left) lies in:
 * the room, through the rooms joined to it, and in *index the block's
 * there. Up and down a room is 208 tall; with no room joined that way the
 * row is the room's own, as the game's 8-bit Y reads it (its ScreenBuffer's
 * rows 13-15 past the bottom, row 15 down wrapping past the top). -1 for
 * none sideways, or further than a room. */
static int block_of(int room, int px, int py, unsigned *index) {
    *index = 0;
    if (px < -256 || px >= 512 || py < -ROOM_H || py >= 2 * ROOM_H) return -1;
    int dx = px < 0 ? -1 : px >= 256 ? 1 : 0, dy = py < 0 ? -1 : py >= ROOM_H ? 1 : 0;
    int r = dy ? room_by(room, dx, dy) : -1;
    if (r < 0) r = room_by(room, dx, 0), dy = 0;
    *index = (((unsigned)(py - dy * ROOM_H) & 0xF0u) | (((unsigned)px & 0xFFu) >> 4)) & 0xFF;
    return r;
}
/* ScreenBuffer_IsBlockImpassable at pixel (px, py) of `room`, in the room it
 * lies in: solid where none is. */
static uint8_t solid_at(int room, int px, int py, bool ladder) {
    unsigned index;
    int r = block_of(room, px, py, &index);
    return r < 0 ? 1 : solid(r, index, ladder);
}
/* Sprites_MoveRight__86c6's test at pixel column px of `room` (16 bits,
 * past its sides too): the block at Y, then one a row further down for
 * every 16 of the hitbox's height, and one more if Y is not on a block's
 * edge, until one is solid, each in the room it lies in (slice 5: up and
 * down too). */
static uint8_t rows_test(int room, int px, int y, unsigned hb) {
    int at = y & ~15;
    uint8_t result = solid_at(room, px, at, false);
    for (int t = rom14(HITBOX_HEIGHTS + hb); !result && t >= 16; t -= 16) result = solid_at(room, px, at += 16, false);
    if (!result && (y & 15)) result = solid_at(room, px, at + 16, false);
    return result;
}
/* The same test along a row, CurrentSprite_CanMoveInDirection's up and down
 * one ($873A): at pixel row py of `room`, across slot s's hitbox's columns
 * (one for every 16 of its width, and one more if X is not on a block's
 * edge), until one is solid. */
static uint8_t row_test(int room, unsigned s, int py) {
    int x = x16(room, s), px = x & ~15;
    uint8_t result = solid_at(room, px, py, false);
    for (int t = (int)width_less_1(room, s); !result && t >= 16; t -= 16) result = solid_at(room, px += 16, py, false);
    if (!result && (x & 15)) result = solid_at(room, px + 16, py, false);
    return result;
}

/* A town room: a screen of a town's zone (fax_zone.c, by the dialogue's
 * names: Eolis's $01-$08 and AREA_TOWNS's fourteen) whose sprite list, as
 * Screen_LoadSpriteInfo reads it from bank 11's AREA_SPRITE_ADDRESSES (an
 * entity and a block a sprite, to a $FF), holds no monster (category 0) or
 * big enemy (7). The same rooms are exactly those with a door into a
 * building and no monster on their list. */
bool fax_seams_town(int room) {
    int area = fax_world_area();
    if (room < 0 || area < 0) return false;
    const FaxZone *zone = fax_zone(fax_zone_at((unsigned)area, (unsigned)room));
    if (!zone || zone->kind != FAX_ZONE_TOWN) return false;
    unsigned at = AREA_SPRITE_ADDRESSES + 2u * (unsigned)area;
    unsigned base = rom11(at) | rom11(at + 1) << 8;
    unsigned list = rom11(base + 2u * (unsigned)room) | rom11(base + 2u * (unsigned)room + 1) << 8;
    if (base < 0x8000u || list < 0x8000u || list >= 0xC000u) return false;
    for (unsigned n = 0; n < LIST_MAX; n++) {
        uint8_t e = rom11(list + 2 * n);
        if (e == EMPTY) break;
        unsigned c = category(e);
        if (c == 0 || c == BIG) return false;
    }
    return true;
}

/* Whether `to` is full for a mover coming from slot s of `from`: its actors
 * (dormant ones included) and the movers on their way in from its sides,
 * top and bottom make ROOM_LIMIT. */
static bool full(int to, int from, unsigned s) {
    static const uint8_t toward[4] = { THROUGH_RIGHT, THROUGH_LEFT, THROUGH_DOWN, THROUGH_UP };
    unsigned n = 0;
    for (unsigned k = 0; k < 8; k++) n += rd(to, ENTITIES + k) < 0x80;
    for (int side = 0; side < 4; side++) {
        int nb = fax_world_joined(to, side);
        const Block *o = nb >= 0 ? block(nb) : NULL;
        if (!o) continue;
        for (unsigned k = 0; k < 8; k++)
            if (!(nb == from && k == s) && (o->through[k] & toward[side])) ++n;
    }
    return n >= ROOM_LIMIT;
}

/* The running room and its block when slot s is an awake mover there. */
static bool awake_mover(unsigned s, int *room, Block **b) {
    *room = running();
    *b = *room >= 0 ? block(*room) : NULL;
    return *b && ((*b)->awake >> s & 1) && mover(cyc_mod_peek(ENTITIES + s));
}
/* Whether a mover in slot s of `room` may go through into `to`, entering it
 * now (not already on its way): `to` runs, the seam is no town's edge (a
 * town room on one side only), and `to` isn't full. */
static bool may_enter(int to, int room, unsigned s, bool entering) {
    if (to < 0) return false;
    if (entering && fax_seams_town(to) != fax_seams_town(room)) {
        ++town_edge;
        return false;
    }
    if (entering && full(to, room, s)) {
        ++kept_out;
        return false;
    }
    return true;
}
/* Whether the awake mover in slot s of `room` may go on through its side
 * dx, dy (one of them 0) into the room joined there (may_enter), and, while
 * it straddles a side the other way, into the room diagonally across too,
 * which its box then reaches. */
static bool may_pass(int room, const Block *b, unsigned s, int dx, int dy) {
    unsigned t = b->through[s];
    unsigned mark = dx < 0 ? THROUGH_LEFT : dx > 0 ? THROUGH_RIGHT : dy < 0 ? THROUGH_UP : THROUGH_DOWN;
    bool entering = !(t & mark);
    if (!may_enter(room_by(room, dx, dy), room, s, entering)) return false;
    int ox = dx ? 0 : t & THROUGH_LEFT ? -1 : t & THROUGH_RIGHT ? 1 : 0;
    int oy = dy ? 0 : t & THROUGH_UP ? -1 : t & THROUGH_DOWN ? 1 : 0;
    return (!ox && !oy) || may_enter(room_by(room, dx + ox, dy + oy), room, s, entering);
}
/* A block test's result as the game's own leaves it: Blocks_Result, the
 * pixel it looked at, and X the slot. */
static void tested(unsigned s, int px, uint8_t py, uint8_t result) {
    cyc_mod_poke(PIXEL_X, (uint8_t)px);
    cyc_mod_poke(PIXEL_Y, py);
    cyc_mod_poke(BLOCKS_RESULT, result);
    leave(s, result, -1);
}

/* ---- slice 1: the hooks ---- */

/* Sprites_UpdateAll's entry and its loop's end: which room's loop runs, and
 * what it draws, for the live game (outside isolation) and a tick. */
static void pass_end(void);
static void swap_in(void);
static int loop_hook(uint16_t addr) {
    (void)addr;
    unsigned c = cyc_mod_isolated();
    if (!c && pass.open) {
        ++broken;
        swap_in();
        pass_end();
    } /* never: the pass ends at its own site */
    if (!c) fax_world_update_view(), spell.casting = 0;
    looping[c] = true;
    loop_room[c] = running();
    memset(&drawn[c], 0, sizeof drawn[c]);
    return 0;
}
static int loop_done_hook(uint16_t addr) {
    (void)addr;
    looping[cyc_mod_isolated()] = false;
    return 0;
}

static int visitor_of(unsigned s);
/* Sprites_UpdateBehavior's entry: a sleeper not yet seen wakes as it comes
 * within PAD of the view, and stands still till then. A visitor (slice 2)
 * never runs its behaviour in the live game: it moves in its own room. */
static int behavior_hook(uint16_t addr) {
    (void)addr;
    int room = running();
    Block *b = room >= 0 ? block(room) : NULL;
    unsigned s = cyc_mod_peek(CURRENT_SPRITE_INDEX) & 7;
    uint8_t e = cyc_mod_peek(ENTITIES + s);
    if (!cyc_mod_isolated() && visitor_of(s) >= 0) {
        leave(s, -1, -1);
        return 1;
    }
    if (!b || (b->awake >> s & 1) || !sleeper(e)) return 0;
    if (in_view(room, s, PAD)) {
        b->awake |= (uint8_t)(1u << s);
        ++woken;
        last_woken[0] = room, last_woken[1] = (int)s, last_woken[2] = e, last_woken[3] = (int)steps;
        return 0;
    }
    ++frozen;
    leave(s, -1, -1);
    return 1;
}
/* CurrentSprite_UpdateState's entry: a dormant sleeper isn't drawn either
 * (it is out of the view), nor a visitor (its room draws it). */
static int state_hook(uint16_t addr) {
    (void)addr;
    int room = running();
    Block *b = room >= 0 ? block(room) : NULL;
    unsigned s = cyc_mod_peek(CURRENT_SPRITE_INDEX) & 7;
    if (!cyc_mod_isolated() && visitor_of(s) >= 0) {
        leave(s, -1, -1);
        return 1;
    }
    if (!b || (b->awake >> s & 1) || !sleeper(cyc_mod_peek(ENTITIES + s))) return 0;
    leave(s, -1, -1);
    return 1;
}

/* Whether slot s's rows [Y, Y + height] reach past its room's top or
 * bottom into a room joined there (slice 5): its sideways tests then read
 * rows of two rooms. */
static bool rows_across(int room, const Block *b, unsigned s) {
    int y = y16_of(b, s, cyc_mod_peek(SLOT_Y + s)), h = (int)height_less_1(room, s);
    return (y < 0 && room_by(room, 0, -1) >= 0) || (y + h >= ROOM_H && room_by(room, 0, 1) >= 0);
}

/* FUN_PRG14__86bd: the test walking left. Y is the slot. */
static int left_hook(uint16_t addr) {
    (void)addr;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    unsigned s = regs.y & 7;
    int room;
    Block *b;
    if (!awake_mover(s, &room, &b)) return 0;
    uint8_t x = cyc_mod_peek(SLOT_X + s), y = cyc_mod_peek(SLOT_Y + s);
    int px = x16_of(b, s, x), py = y16_of(b, s, y);
    if (px == x && x >= 0xF1 && !(b->through[s] & THROUGH_RIGHT)) px = x - 256; /* wrapped past the left side */
    if (px >= 0 && px < 0xF1) {
        if (!rows_across(room, b, s)) return 0; /* the original's test */
    } else if (px < 0 && !may_pass(room, b, s, -1, 0)) {
        return 0;
    } /* (straddling the right side, 241 and up: its own last column) */
    uint8_t result = rows_test(room, px, py, hitbox(room, s));
    if (!result && px < 0) b->through[s] |= THROUGH_LEFT;
    tested(s, px, y, result);
    return 1;
}
/* Sprites_MoveRight: the test walking right, at the hitbox's right edge. */
static int right_hook(uint16_t addr) {
    (void)addr;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    unsigned s = regs.y & 7;
    int room;
    Block *b;
    if (!awake_mover(s, &room, &b)) return 0;
    uint8_t x = cyc_mod_peek(SLOT_X + s), y = cyc_mod_peek(SLOT_Y + s);
    int left = x16_of(b, s, x), px = left + width_less_1(room, s), py = y16_of(b, s, y);
    if (left >= 0 && px < 256) {
        if (!rows_across(room, b, s)) return 0; /* the original's test */
    } else if (px >= 256 ? !may_pass(room, b, s, 1, 0) : room_at(room, px) < 0) {
        return 0; /* (through the left side, its right edge still past it: the room there) */
    }
    uint8_t result = rows_test(room, px, py, hitbox(room, s));
    if (!result && px >= 256) b->through[s] |= THROUGH_RIGHT;
    tested(s, px, y, result);
    return 1;
}
/* CurrentSprite_CanMoveInDirection's up and down test, with Arg_PixelPosY
 * set: the row there, across the hitbox's columns, for a slot past a side,
 * whose columns lie in two rooms. (Rows past the top or bottom with a room
 * joined there are up_hook's and down_hook's.) */
static int vertical_hook(uint16_t addr) {
    (void)addr;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    unsigned s = regs.y & 7;
    int room;
    Block *b;
    if (!awake_mover(s, &room, &b)) return 0;
    int left = x16_of(b, s, cyc_mod_peek(SLOT_X + s));
    int ext = width_less_1(room, s);
    if (left >= 0 && left + ext < 256) return 0; /* the original's test */
    uint8_t py = cyc_mod_peek(PIXEL_Y), y = cyc_mod_peek(SLOT_Y + s);
    uint8_t result = row_test(room, s, y16_of(b, s, y) + (uint8_t)(py - y));
    tested(s, left, py, result);
    return 1;
}
/* CurrentSprite_CanMoveInDirection moving down ($871C, Y the slot), slice 5:
 * the row at the hitbox's bottom (Y plus its height less one). Past the
 * room's bottom, with a room joined below, it is that room's row (less
 * 208), across the hitbox's columns in the room each lies in, and the slot
 * is marked through the bottom; where the mover may not go (a full room, a
 * town's edge) the seam is a floor: blocked. With nothing joined below the
 * original decides (it blocks from $F0, and reads its ScreenBuffer's rows
 * 13-15 till then). Leaves what the game's test leaves. */
static uint8_t down_test(int room, Block *b, unsigned s, int py) {
    if (!may_pass(room, b, s, 0, 1)) return 1;
    uint8_t result = row_test(room, s, py);
    if (!result) b->through[s] |= THROUGH_DOWN;
    return result;
}
static int down_hook(uint16_t addr) {
    (void)addr;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    unsigned s = regs.y & 7;
    int room;
    Block *b;
    if (!awake_mover(s, &room, &b)) return 0;
    uint8_t x = cyc_mod_peek(SLOT_X + s), y = cyc_mod_peek(SLOT_Y + s);
    int py = y16_of(b, s, y) + (int)height_less_1(room, s);
    if (py < ROOM_H || room_by(room, 0, 1) < 0) return 0; /* the original's (and $873A's across a side) */
    uint8_t result = down_test(room, b, s, py);
    tested(s, x, (uint8_t)py, result);
    return 1;
}
/* ... moving up ($8731): the row at Y. The original calls a Y of $F0 and up
 * (wrapped past the top) blocked; with a room joined above it is that
 * room's row (plus 208), and the slot is marked through the top; a full
 * room or a town's edge stays the original's wall. */
static int up_hook(uint16_t addr) {
    (void)addr;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    unsigned s = regs.y & 7;
    int room;
    Block *b;
    if (!awake_mover(s, &room, &b)) return 0;
    uint8_t x = cyc_mod_peek(SLOT_X + s), y = cyc_mod_peek(SLOT_Y + s);
    int py = y16_of(b, s, y);
    if (py >= 0 || room_by(room, 0, -1) < 0 || !may_pass(room, b, s, 0, -1)) return 0;
    uint8_t result = row_test(room, s, py);
    if (!result) b->through[s] |= THROUGH_UP;
    tested(s, x, y, result);
    return 1;
}
/* Sprite_CapToBlockOppositeFalling ($85AD, X the slot), the fliers that turn
 * at a screen's top and bottom with no block test (Giant Bees, Yuinaru, the
 * Execution Hood): at Y $E0 and up it snaps Y to a block and returns C set.
 * Moving down with its box past the bottom, or up with Y wrapped past the
 * top, into a room joined there that it may enter, it goes through, marked,
 * and the routine returns C clear ("not at the edge") from $E0 on. */
static int cap_hook(uint16_t addr) {
    (void)addr;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    unsigned s = regs.x & 7;
    int room;
    Block *b;
    if (!awake_mover(s, &room, &b)) return 0;
    uint8_t y = cyc_mod_peek(SLOT_Y + s);
    if (cyc_mod_peek(FLAGS + s) & FALLING) {
        int py = y16_of(b, s, y) + (int)height_less_1(room, s);
        if (py >= ROOM_H && !(b->through[s] & THROUGH_DOWN) && room_by(room, 0, 1) >= 0 && may_pass(room, b, s, 0, 1))
            b->through[s] |= THROUGH_DOWN;
        if (!(b->through[s] & THROUGH_DOWN) || y < 0xE0) return 0;
    } else {
        if (y16_of(b, s, y) >= 0 || room_by(room, 0, -1) < 0 || !may_pass(room, b, s, 0, -1)) return 0;
        b->through[s] |= THROUGH_UP;
    }
    regs.a = y;
    regs.p = (uint8_t)((regs.p & ~(FLAG_C | FLAG_Z | FLAG_N)) | (y == 0xE0 ? FLAG_Z : 0) | ((y - 0xE0) & FLAG_N));
    cyc_mod_set_regs(&regs);
    return 1;
}
/* CurrentSprite_HandleFall's CMP #$C0 ($865B), its 8 pixels down already
 * added (A the new Y): at $C0 and up the original removes the faller, off
 * the screen. With a room joined below, an awake mover falls on: the rest
 * of the routine in C, the down test (the row at the hitbox's bottom, in
 * the room below past 208, marked through it; the seam a floor where it may
 * not go there) and, blocked, Y snapped to its block. X the slot. */
static int fall_hook(uint16_t addr) {
    (void)addr;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    unsigned s = cyc_mod_peek(CURRENT_SPRITE_INDEX) & 7;
    int room;
    Block *b;
    if (regs.a < 0xC0 || !awake_mover(s, &room, &b) || room_by(room, 0, 1) < 0) return 0;
    uint8_t x = cyc_mod_peek(SLOT_X + s), y = cyc_mod_peek(SLOT_Y + s);
    int py = y16_of(b, s, y) + (int)height_less_1(room, s);
    uint8_t result = py >= ROOM_H ? down_test(room, b, s, py) : row_test(room, s, py);
    cyc_mod_poke(PIXEL_X, x);
    cyc_mod_poke(PIXEL_Y, (uint8_t)py);
    cyc_mod_poke(BLOCKS_RESULT, result);
    if (result) cyc_mod_poke(SLOT_Y + s, (uint8_t)(y & 0xF0));
    else ++fell_on;
    leave(s, result ? (y & 0xF0) : 0, -1); /* A as the routine leaves it: the snapped Y, or Blocks_Result 0 */
    return 1;
}
/* Sprites_CheckPlacementInvalidAtY's block (Arg_PixelPosY set, the X there
 * X + 15 facing right, X facing left): the room it lies in, past a side,
 * and (slice 5) past the bottom or top into a room joined there; where the
 * mover may not go below, the seam is its floor. */
static int placement_hook(uint16_t addr) {
    (void)addr;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    unsigned s = regs.x & 7;
    int room;
    Block *b;
    if (!awake_mover(s, &room, &b)) return 0;
    int px = x16_of(b, s, cyc_mod_peek(SLOT_X + s)) + (cyc_mod_peek(FLAGS + s) & 1 ? 15 : 0);
    uint8_t py = cyc_mod_peek(PIXEL_Y), y = cyc_mod_peek(SLOT_Y + s);
    int at = y16_of(b, s, y) + (uint8_t)(py - y);
    int dy = at >= ROOM_H ? 1 : at < 0 ? -1 : 0;
    if (dy && room_by(room, 0, dy) < 0) dy = 0;
    if (px >= 0 && px < 256 && !dy) return 0; /* the original's */
    if (room_at(room, px) < 0) return 0;
    unsigned index;
    int to = block_of(room, px, at, &index);
    uint8_t result = to < 0 || (dy > 0 && !may_pass(room, b, s, 0, 1)) ? 1 : solid(to, index, true);
    cyc_mod_poke(PIXEL_X, (uint8_t)px);
    regs.a = result;
    regs.x = (uint8_t)index;
    regs.p = (uint8_t)((regs.p & ~(FLAG_Z | FLAG_N)) | (result ? 0 : FLAG_Z));
    cyc_mod_set_regs(&regs);
    return 1;
}
/* Sprite_Maybe_TurnAroundIfAtScreenEdgeHoriz, after its move: X $F0 and up
 * is the screen's side for a flier or a shot, facing right its right side,
 * facing left wrapped past its left (unless it straddles the right one).
 * Returns "not at the edge", C clear, as the original's @_returnFalse. */
static int flier_hook(uint16_t addr) {
    (void)addr;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    unsigned s = regs.x & 7;
    int room;
    Block *b;
    if (!awake_mover(s, &room, &b)) return 0;
    uint8_t x = cyc_mod_peek(SLOT_X + s);
    if (x < 0xF0) return 0;
    if (cyc_mod_peek(FLAGS + s) & 1) {
        if (!(b->through[s] & SIDEWAYS)) {
            if (!may_pass(room, b, s, 1, 0)) return 0;
            b->through[s] |= THROUGH_RIGHT;
        }
    } else if (!(b->through[s] & SIDEWAYS)) {
        if (!may_pass(room, b, s, -1, 0)) return 0;
        b->through[s] |= THROUGH_LEFT;
    }
    regs.a = x;
    regs.p = (uint8_t)((regs.p & ~(FLAG_C | FLAG_Z | FLAG_N)) | (x == 0xF0 ? FLAG_Z : 0) | ((x - 0xF0) & FLAG_N));
    cyc_mod_set_regs(&regs);
    return 1;
}

/* Sprite_Draw_Standard's or _FlippedHoriz's tile loop, played from the state
 * Sprite_Draw left (the disassembly's, cell for cell): each cell it puts in
 * OAM gets the slot as its owner, and each it skips for lying past the
 * screen's side, top or bottom (its X or Y carrying past 255), or puts where
 * the NES hides it (OAM Y 239 and up, line 240 and down: past the room's
 * bottom), is kept as a cell past the side, its Y moved by `dy` (-256 for a
 * slot drawn from its byte wrapped past the top: its true place).
 * Sprites_IncNextSpriteSlot's count (two halves, $20 apart) gives the OAM
 * entry. */
static void play_draw(Drawn *d, unsigned s, bool flipped, int dy) {
    uint8_t cols = cyc_mod_peek(COLS), rows = cyc_mod_peek(ROWS);
    uint8_t normx = cyc_mod_peek(NORM_X), normy = cyc_mod_peek(NORM_Y);
    uint8_t offx = cyc_mod_peek(OFFSET_X), offy = cyc_mod_peek(OFFSET_Y);
    uint16_t addr = (uint16_t)(cyc_mod_peek(READ_ADDR) | cyc_mod_peek(READ_ADDR + 1) << 8);
    uint8_t flip = cyc_mod_peek(FLIP_MASK), visible = cyc_mod_peek(MOVING_VISIBILITY), ppu = cyc_mod_peek(PPU_OFFSET);
    uint8_t slot = cyc_mod_peek(SPRITE_SLOT), range = cyc_mod_peek(START_SLOT_RANGE), full = cyc_mod_peek(SLOTS_FULL);
    if (cols > 15 || rows > 15) return;
    if (flipped) { /* the pivot: X moves by its double less the width, in 16 bits */
        uint8_t shift = (uint8_t)((cyc_mod_peek(TEMP_ADDR_L) << 1) - (cols + 1) * 8);
        unsigned sum = (unsigned)shift + normx;
        normx = (uint8_t)sum;
        offx = (uint8_t)(offx + (shift & 0x80 ? 0xFF : 0) + (sum >> 8));
    }
    unsigned y = 0;
    for (unsigned row = 0; row <= rows; row++)
        for (unsigned k = 0; k <= cols; k++) {
            unsigned tx = flipped ? cols - k : k;
            uint8_t tile = cyc_mod_peek((uint16_t)(addr + y));
            if (tile == 0xFF) {
                y += 1;
                continue;
            }
            unsigned lo = normx + cyc_mod_peek((uint16_t)(TILE_POSITIONS + tx));
            unsigned hi = offx + (lo >> 8);
            bool fits_x = (hi & 0xFF) == 0;
            unsigned ylo = normy + cyc_mod_peek((uint16_t)(TILE_POSITIONS + row)) + (fits_x ? hi >> 8 : 0);
            unsigned yhi = offy + (ylo >> 8);
            bool fits_y = (yhi & 0xFF) == 0;
            uint8_t attr = (uint8_t)(cyc_mod_peek((uint16_t)(addr + y + 1)) ^ flip);
            if (visible & cyc_mod_peek((uint16_t)((flipped ? VISIBILITY_FLIPPED : VISIBILITY_MASK) + (tx & 1))))
                attr |= 0x20;
            bool drawn_ = fits_x && fits_y && !full;
            if (drawn_) {
                d->owner[(((unsigned)slot << 2 ^ range) >> 2) & 63] = (uint8_t)(s + 1);
                slot ^= 0x20;
                if (!(slot & 0x20) && ++slot >= 0x20) full = 1;
            }
            if ((drawn_ ? (ylo & 0xFF) >= HIDDEN_Y : !fits_x || !fits_y) && d->count < CELLS) {
                int cy = (int)(int8_t)(uint8_t)yhi * 256 + (int)(ylo & 0xFF) + dy;
                d->cell[d->count++] = (FaxSeamsCell){ (int16_t)((int)(int8_t)(uint8_t)hi * 256 + (lo & 0xFF)),
                                                      (int16_t)cy, (uint8_t)(tile + ppu), attr, (uint8_t)s };
            }
            y += 2;
        }
}
/* Sprite_Draw_Standard's and Sprite_Draw_FlippedHoriz's entries, in a sprite
 * loop: a slot whose origin is across a side is drawn a screen over, then
 * the loop is played for its cells. Inside Player_CastMagic (slice 3) the
 * draw is the spell's: drawn a screen over by its side, its cells kept as
 * the spell's. */
static int draw_hook(uint16_t addr) {
    unsigned c = cyc_mod_isolated();
    if (!c && spell.casting && !looping[0] && fax_world_active_room() >= 0) {
        spell_x16();
        if (spell.side) {
            cyc_mod_poke(OFFSET_X, (uint8_t)(cyc_mod_peek(OFFSET_X) + (uint8_t)spell.side));
            ++spell_drawn_abroad;
        }
        play_draw(&spell_drawn, SPELL, addr == DRAW_FLIPPED, 0);
        return 0;
    }
    if (!looping[c]) return 0;
    Block *b = loop_room[c] >= 0 ? block(loop_room[c]) : NULL;
    if (!b) return 0;
    unsigned s = cyc_mod_peek(CURRENT_SPRITE_INDEX) & 7;
    int x = x16_of(b, s, cyc_mod_peek(SLOT_X + s));
    if (x < 0 || x >= 256) cyc_mod_poke(OFFSET_X, (uint8_t)(cyc_mod_peek(OFFSET_X) + (x < 0 ? 0xFF : 1)));
    drawn[c].tiles[s] = b->tiles[s];
    /* Through the top and wrapped (slice 5): the game draws it from its byte,
     * a screen below, so it puts none of its cells in OAM (none on the
     * status bar); they are kept where they lie. */
    play_draw(&drawn[c], s, addr == DRAW_FLIPPED, y16_of(b, s, cyc_mod_peek(SLOT_Y + s)) < 0 ? -256 : 0);
    return 0;
}
/* Screen_LoadSpriteInfo in play: a screen loads its list. */
static int load_hook(uint16_t addr) {
    (void)addr;
    if (!cyc_mod_isolated()) fax_world_screen_loaded(), spell.side = 0; /* a spell left on is in the room loaded */
    return 0;
}

/* ---- slice 1: between frames ---- */

/* Slot s of `from`, its origin across its side dx, dy (one or both: a
 * corner), to a free slot of the room there: every byte, its Y less or plus
 * 208 up or down (slice 5), its tiles, its wake, and a mark for each side of
 * its new room it straddles (its right one, in from the right; its bottom,
 * in from below). With nowhere to go (no free slot, the room gone) it is put
 * back inside its room, turned round, as at a wall. */
static void move(int from, unsigned s, int dx, int dy) {
    Block *bf = block(from);
    int to = room_by(from, dx, dy), d = -1;
    uint8_t x = rd(from, SLOT_X + s), e = rd(from, ENTITIES + s);
    int y = y16(from, s);
    unsigned ext = width_less_1(from, s), height = height_less_1(from, s);
    Block *bt = to >= 0 ? block(to) : NULL;
    for (int k = 7; bt && k >= 0 && d < 0; k--)
        if (rd(to, ENTITIES + (unsigned)k) == EMPTY) d = k;
    if (d < 0) {
        if (dx) {
            wr(from, SLOT_X + s, dx > 0 ? (uint8_t)((255 - ext) & 0xF0) : 0);
            wr(from, FLAGS + s, (uint8_t)(rd(from, FLAGS + s) ^ 1));
            bf->through[s] &= (uint8_t)~SIDEWAYS;
        }
        if (dy) {
            wr(from, SLOT_Y + s, dy > 0 ? (uint8_t)((ROOM_H - 1 - height) & 0xF0) : 0);
            wr(from, FLAGS + s, (uint8_t)(rd(from, FLAGS + s) ^ FALLING));
            bf->through[s] &= (uint8_t)~UPRIGHT;
        }
        ++pushed_back;
        return;
    }
    unsigned u = (unsigned)d;
    int ny = y - dy * ROOM_H;
    wr(to, SLOT_X + u, x);
    wr(to, SLOT_Y + u, (uint8_t)ny);
    for (unsigned a = SLOT_ARRAYS; a < SLOT_ARRAYS + 23 * 8; a += 8) wr(to, a + u, rd(from, a + s));
    unsigned k = 0;
    while (k < 8 && (k == u || rd(to, ENTITIES + k) != e)) ++k;
    if (k < 8) { /* its entity's tiles are where that slot's are */
        wr(to, PPU_OFFSETS + u, rd(to, PPU_OFFSETS + k));
        bt->tiles[u] = bt->tiles[k];
    } else {
        int home = bf->tiles[s] ? bf->tiles[s] - 1 : from;
        bt->tiles[u] = home == to ? 0 : (uint8_t)(home + 1);
    }
    bt->awake |= (uint8_t)(1u << u);
    bt->through[u] = (uint8_t)((dx <= 0 && x + ext >= 256 ? THROUGH_RIGHT : 0) |
                               (ny >= 0 && ny + (int)height >= ROOM_H ? THROUGH_DOWN : 0));
    wr(from, ENTITIES + s, EMPTY);
    bf->through[s] = bf->tiles[s] = 0;
    bf->awake &= (uint8_t)~(1u << s);
    bf->flash &= (uint8_t)~(1u << s);
    bt->flash &= (uint8_t)~(1u << u); /* a flash waiting comes back to its damage there, and asks again */
    moves_up += dy < 0, moves_down += dy > 0;
    int note[6] = { from, (int)s, to, d, e, (int)steps };
    memcpy(move_log[moves++ % LOG], note, sizeof note);
}

void fax_seams_step(void) {
    if (!enabled || fax_world_active_room() < 0) return;
    int vx, vy;
    bool view = fax_world_view(&vx, &vy);
    ++steps;
    /* The neighbours' sounds of this frame, for the live game to play at its
     * next pass's site (those of a frame it never reached are dropped). */
    memcpy(pending, heard, sizeof pending);
    pending_count = heard_count;
    heard_count = 0;
    for (int room = 0; room < ROOMS; room++) {
        Block *b = fax_world_room_live(room) ? block(room) : NULL;
        if (!b) continue;
        for (unsigned s = 0; s < 8; s++) {
            uint8_t e = rd(room, ENTITIES + s);
            if (e == EMPTY) {
                b->through[s] = b->tiles[s] = 0;
                b->awake &= (uint8_t)~(1u << s);
                b->flash &= (uint8_t)~(1u << s);
                continue;
            }
            if (view && category(e) == SHOT && !in_view(room, s, PAD)) { /* out of the view: gone */
                wr(room, ENTITIES + s, EMPTY);
                b->through[s] = b->tiles[s] = 0;
                b->awake &= (uint8_t)~(1u << s);
                b->flash &= (uint8_t)~(1u << s);
                ++removed;
                continue;
            }
            unsigned t = b->through[s];
            if (!t) continue;
            uint8_t x = rd(room, SLOT_X + s), y = rd(room, SLOT_Y + s);
            int dx = 0, dy = 0;
            if (t & THROUGH_LEFT) {
                if (x >= 128) dx = -1;
                else t &= ~(unsigned)THROUGH_LEFT;
            } else if (t & THROUGH_RIGHT) {
                if (x < 128) dx = 1;
                else if (x + width_less_1(room, s) < 256) t &= ~(unsigned)THROUGH_RIGHT; /* back inside */
            }
            if (t & THROUGH_UP) {
                if (y >= 128) dy = -1;
                else t &= ~(unsigned)THROUGH_UP;
            } else if (t & THROUGH_DOWN) {
                if (y >= ROOM_H) dy = 1;
                else if (y + height_less_1(room, s) < ROOM_H) t &= ~(unsigned)THROUGH_DOWN; /* back inside */
            }
            b->through[s] = (uint8_t)t;
            if (dx || dy) move(room, s, dx, dy);
        }
    }
}

/* ---- slice 2: where a slot is ---- */

static int visitor_of(unsigned s) {
    if (!pass.open) return -1;
    for (unsigned i = 0; i < pass.count; i++)
        if (pass.v[i].live == s) return (int)i;
    return -1;
}
/* A live slot's X in 16 bits less its byte: a visitor's from its side, the
 * room's own from its mark (-256, 0 or 256). */
static int live_shift(unsigned s) {
    int v = visitor_of(s);
    if (v >= 0) return pass.v[v].shift;
    Block *b = block(fax_world_active_room());
    uint8_t x = cyc_mod_peek((uint16_t)(SLOT_X + s));
    return b ? x16_of(b, s, x) - x : 0;
}
int fax_seams_x16(unsigned slot) {
    slot &= 7u;
    uint8_t x = cyc_mod_peek((uint16_t)(SLOT_X + slot));
    if (!cyc_mod_isolated()) return x + live_shift(slot);
    Block *b = block(fax_world_seams_room());
    return b ? x16_of(b, slot, x) : x;
}
int fax_seams_room_x16(int room, unsigned slot) {
    if (room == fax_world_seams_room() && (!cyc_mod_isolated() || fax_world_ticking())) return fax_seams_x16(slot);
    Block *b = block(room);
    return b ? x16(room, slot & 7u) : 0;
}
int fax_seams_slot_room(unsigned slot) {
    int active = fax_world_active_room();
    if (active < 0 || cyc_mod_isolated()) return -1;
    int v = visitor_of(slot & 7u);
    if (v >= 0) return pass.v[v].room;
    int x = fax_seams_x16(slot);
    return x < 0 ? fax_world_joined(active, 0) : x >= 256 ? fax_world_joined(active, 1) : active;
}
int fax_seams_slot_y(unsigned slot) {
    int v = cyc_mod_isolated() ? -1 : visitor_of(slot & 7u);
    return v >= 0 ? rd(pass.v[v].room, SLOT_Y + pass.v[v].slot) : cyc_mod_peek((uint16_t)(SLOT_Y + (slot & 7u)));
}
bool fax_seams_hero_x(int *x) {
    if (!cyc_mod_isolated()) {
        *x = cyc_mod_peek(HERO_X);
        return true;
    }
    int room = fax_world_seams_room(), hx, hy, px, py, ax, ay;
    if (room < 0 || !fax_world_ticking() || !fax_world_hero(&hx, &hy)) return false;
    fax_world_place(room, &px, &py);
    fax_world_place(fax_world_active_room(), &ax, &ay);
    if (py != ay) return false; /* above or below his row: none of slice 2's */
    *x = hx - px;
    return true;
}
bool fax_seams_visiting(void) { return pass.open != 0; }

/* In a neighbour's tick, at Sugata's flash's damage: the room is joined
 * beside the live one, so the live game takes the flash, at its next pass. */
bool fax_seams_flash(void) {
    int room = fax_world_seams_room(), active = fax_world_active_room();
    Block *b = block(room);
    if (!enabled || !fax_world_ticking() || !b || active < 0 ||
        (fax_world_joined(active, 0) != room && fax_world_joined(active, 1) != room))
        return false;
    unsigned s = cyc_mod_peek(CURRENT_SPRITE_INDEX) & 7u;
    if (!(b->flash >> s & 1)) ++flashes_due;
    b->flash |= (uint8_t)(1u << s);
    return true;
}

/* ---- slice 2: told apart ---- */

/* Sprite_CheckHitByCastMagic's entry: its X test (the spell's X plus its
 * width, less the sprite's X, below the two widths) in 16 bits, the spell's
 * X from its side (slice 3). A refusal leaves what the X test's failure
 * leaves: X the slot, A the difference, C set (Sprite_GetBounds, next in the
 * loop, reads X). It also refuses a hit on a sleeper not yet seen, which the
 * view's edge keeps the spell from anyway (slice 3's second guard).
 * Neighbours' copies have no spell. */
static int magic_hook(uint16_t addr) {
    (void)addr;
    uint8_t type = cyc_mod_peek(CAST_TYPE);
    if (cyc_mod_isolated() || type >= 5) return 0;
    unsigned s = cyc_mod_peek(CURRENT_SPRITE_INDEX) & 7u, c = category(cyc_mod_peek((uint16_t)(ENTITIES + s)));
    if (c != 0 && c != BIG) return 0;
    uint8_t width = rom14(CAST_WIDTHS + type), hb = cyc_mod_peek((uint16_t)(HITBOX + s)),
            sw = rom14(HITBOX_WIDTHS + hb);
    int x = fax_seams_x16(s), at = spell_x16();
    bool wrap = wrapped(at, x, sw, width); /* SPRITE_HITBOX_WIDTHS,Y as the game reads it */
    if (!wrap) {
        /* The game's own test, which would hit: then only a sleeper not yet seen is refused. */
        uint8_t height = rom14(CAST_HEIGHTS_HIT + type), sh = rom14(HITBOX_HEIGHTS + hb);
        uint8_t dy = (uint8_t)(cyc_mod_peek(CAST_Y) + height - cyc_mod_peek((uint16_t)(SLOT_Y + s)));
        bool hits = (uint8_t)(at + width - x) < (uint8_t)(sw + width) && dy < (uint8_t)(sh + height) &&
                    !(cyc_mod_peek((uint16_t)(FLAGS + s)) & HIDDEN);
        if (!hits || !dormant(s)) return 0;
    }
    leave(s, (uint8_t)(at + width - x), 1);
    ++*(wrap ? &magic_refused : &magic_dormant);
    return 1;
}
/* Player_CheckShieldHitByMagic's entry. In a neighbour's tick it returns at
 * once: his shield there is the live room's (Player_DrawShield never runs in
 * a copy), so it took that room's shots where his shield would be in his.
 * In the live game its X test (the shield's X plus 7, less the shot's box,
 * below the box's width plus 7) is worked out in 16 bits: the shield is his
 * X plus 0 or 8, which carries past 255 at a seam. CurrentSprite_CheckHitPlayer,
 * next, loads X itself. */
static int shield_hook(uint16_t addr) {
    (void)addr;
    unsigned s = cyc_mod_peek(CURRENT_SPRITE_INDEX) & 7u;
    if (cyc_mod_isolated()) {
        if (!fax_world_ticking()) return 0;
        leave(s, -1, -1);
        ++shield_skipped;
        return 1;
    }
    if (category(cyc_mod_peek((uint16_t)(ENTITIES + s))) != SHOT) return 0;
    uint8_t hero = cyc_mod_peek(HERO_X);
    int shield = hero + (uint8_t)(cyc_mod_peek(SHIELD_X) - hero);
    if (!wrapped(shield, fax_seams_box_left(), cyc_mod_peek(BOX_WIDTH), SHIELD_PAD)) return 0;
    leave(s, -1, -1);
    ++shield_refused;
    return 1;
}
/* Player_DrawWeapon's BNE after comparing the weapon's high byte with the
 * screen's: off the screen it returns before the Y and the reach, which keep
 * the last on-screen frame's. In the live game the branch is not taken, so
 * they are this frame's wherever the weapon is (it hits across a seam at its
 * true height); the routine's next test, before the draw, still returns. */
static int weapon_hook(uint16_t addr) {
    (void)addr;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    if (cyc_mod_isolated() || (regs.p & FLAG_Z)) return 0;
    regs.p |= FLAG_Z;
    cyc_mod_set_regs(&regs);
    ++weapon_placed;
    return 0;
}

/* ---- slice 2: sounds ---- */

/* Sound_PlayEffect in a neighbour's tick: the effect, when the slot playing
 * it is in the view. The copy's own play is thrown away with it. */
static int sound_hook(uint16_t addr) {
    (void)addr;
    if (!cyc_mod_isolated() || !fax_world_ticking()) return 0;
    int room = fax_world_seams_room();
    unsigned s = cyc_mod_peek(CURRENT_SPRITE_INDEX) & 7u;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    ++heard_total;
    if (room < 0 || cyc_mod_peek((uint16_t)(ENTITIES + s)) == EMPTY || !in_view(room, s, 0) || heard_count >= HEARD)
        return 0;
    heard[heard_count++] = (Sound){ regs.a, (uint8_t)room, (uint8_t)s };
    return 0;
}
static void play_pending(void) {
    for (unsigned i = 0; i < pending_count; i++) {
        CycModRegs regs, call;
        cyc_mod_regs(&regs);
        call = regs;
        call.a = pending[i].effect;
        bool ok = cyc_mod_call_commit(SOUND_PLAY_EFFECT, &call);
        cyc_mod_set_regs(&regs);
        if (!ok) continue;
        int note[4] = { pending[i].room, pending[i].slot, pending[i].effect, (int)steps };
        memcpy(sound_log[played++ % HEARD], note, sizeof note);
    }
    pending_count = 0;
}

/* ---- slice 2: the visitors' pass ---- */

/* A slot's k-th byte (0 X, 1 Y, then the 23 arrays). */
static unsigned byte_at(unsigned k, unsigned s) {
    return k == 0 ? SLOT_X + s : k == 1 ? SLOT_Y + s : SLOT_ARRAYS + (k - 2) * 8u + s;
}
/* The gap between [a, a + n) and [b, b + m): 0 when they meet. */
static int gap(int a, int n, int b, int m) { return a + n <= b ? b - (a + n) : b + m <= a ? a - (b + m) : 0; }
/* A Sugata whose flash waits at its damage: its behaviour the flash's, under
 * way. */
static bool flash_waits(int room, unsigned k) {
    return rd(room, ENTITIES + k) == SUGATA && rd(room, BEHAVIORS + k) == BEHAVIOR_FLASH &&
           (rd(room, FLAGS + k) & BEHAVIOR_READY);
}

typedef struct {
    int room, slot, gap, shift, y; /* y: its Y in his room's pixels (slice 5: from above or below, less or plus 208) */
    bool flash;
} Candidate;

/* The first arrival at the site: the visitors chosen, put in, and the loop
 * entered (1), or none (0). */
static int pass_begin(void) {
    int active = running();
    if (active < 0 || cyc_mod_peek(CURRENT_ROM_BANK) != 14 || cyc_mod_peek(SPRITES_PAUSED) ||
        cyc_mod_peek(PLAYER_IS_DEAD))
        return 0;
    int hx = cyc_mod_peek(HERO_X), hy = cyc_mod_peek(HERO_Y);
    bool spell_on = cyc_mod_peek(CAST_TYPE) < 5;
    int cx = spell_on ? spell_x16() : 0, cy = cyc_mod_peek(CAST_Y);
    Candidate c[16];
    unsigned n = 0;
    for (int side = 0; side < 4; side++) { /* left, right, and (slice 5) above and below */
        int nb = fax_world_joined(active, side);
        Block *b = nb >= 0 ? block(nb) : NULL;
        if (!b) continue;
        for (unsigned k = 0; k < 8; k++) {
            uint8_t e = rd(nb, ENTITIES + k);
            if (e >= 0x80) continue;
            bool due = b->flash >> k & 1;
            if (due && (side > 1 || !flash_waits(nb, k))) { /* a flash is taken only from his row */
                b->flash &= (uint8_t)~(1u << k);
                due = false;
            }
            unsigned cat = category(e);
            if ((cat != 0 && cat != BREAD_OR_COIN && cat != SHOT && cat != BIG) || rd(nb, HIT_COUNTER + k) ||
                !(rd(nb, HIT_BY_MAGIC + k) & 0x80))
                continue; /* the loop lets these interact (and moves the rest) */
            unsigned h = hitbox(nb, k);
            int x = x16(nb, k) + (side == 0 ? -256 : side == 1 ? 256 : 0);
            int y = y16(nb, k) + (side == 2 ? -ROOM_H : side == 3 ? ROOM_H : 0);
            int w = rom14(HITBOX_WIDTHS + h) + 1, t = rom14(HITBOX_HEIGHTS + h) + 1;
            int g = gap(x, w, hx, HERO_W);
            bool near = g <= REACH && gap(y, t, hy, HERO_H) <= REACH;
            if (!near && spell_on) near = gap(x, w, cx, 32) <= PAD && gap(y, t, cy, 32) <= PAD;
            if (!near && !due) continue;
            Candidate it = { nb, (int)k, due ? -1 : g, x - rd(nb, SLOT_X + k), y, due };
            unsigned at = n++;
            while (at > 0 && c[at - 1].gap > it.gap) {
                c[at] = c[at - 1];
                at--;
            } /* nearest first */
            c[at] = it;
        }
    }
    if (!n) return 0;
    uint8_t own[8];
    unsigned free_slots[8], nfree = 0;
    for (int s = 7; s >= 0; s--) {
        own[s] = cyc_mod_peek((uint16_t)(ENTITIES + s));
        if (own[s] == EMPTY) free_slots[nfree++] = (unsigned)s;
    }
    unsigned count = n < nfree ? n : nfree;
    waited += n - count;
    if (!count) return 0;
    for (unsigned s = 0; s < 8; s++) {
        for (unsigned k = 0; k < SLOT_BYTES; k++) pass.saved[s][k] = cyc_mod_peek((uint16_t)byte_at(k, s));
        if (own[s] != EMPTY) cyc_mod_poke((uint16_t)(ENTITIES + s), EMPTY); /* the room's own: hidden */
    }
    for (unsigned i = 0; i < count; i++) {
        unsigned live = free_slots[i];
        pass.v[i] = (Visitor){ (int8_t)c[i].room, (uint8_t)c[i].slot, (uint8_t)live, c[i].flash, (int16_t)c[i].shift };
        for (unsigned k = 0; k < SLOT_BYTES; k++)
            cyc_mod_poke((uint16_t)byte_at(k, live), rd(c[i].room, byte_at(k, (unsigned)c[i].slot)));
        cyc_mod_poke((uint16_t)(SLOT_Y + live), (uint8_t)c[i].y); /* above: its -8 is 248; below: its 8 is 216 */
    }
    pass.count = (uint8_t)count;
    pass.open = 1;
    ++passes;
    visits += count;
    /* Into the loop, past its INC SpriteUpdateCounter, its RTS back here; a
     * Sugata's flash (the nearest: first) runs its damage before it, from
     * the behaviour's own code, its JMP Sprite_FinishBehavior's RTS into the
     * loop. */
    CycModRegs regs;
    cyc_mod_regs(&regs);
    uint16_t to[3] = { MAIN_LOOP_COUNTDOWN - 1, SPRITES_LOOP - 1, FLASH_HIT - 1 };
    unsigned pushes = pass.v[0].flash ? 3 : 2;
    for (unsigned i = 0; i < pushes; i++) {
        cyc_mod_poke((uint16_t)(0x100u | regs.s), (uint8_t)(to[i] >> 8));
        regs.s = (uint8_t)(regs.s - 1);
        cyc_mod_poke((uint16_t)(0x100u | regs.s), (uint8_t)to[i]);
        regs.s = (uint8_t)(regs.s - 1);
    }
    if (pass.v[0].flash) {
        Block *b = block(pass.v[0].room);
        b->flash &= (uint8_t)~(1u << pass.v[0].slot);
        cyc_mod_poke(CURRENT_SPRITE_INDEX, pass.v[0].live);
        regs.x = pass.v[0].live;
        ++flash_runs;
    }
    cyc_mod_set_regs(&regs);
    return 1;
}
/* The pass over: each visitor home with every byte but its position (its
 * HP, hit counter, magic hit, facing, a death or a removal), and the live
 * slots as they were. */
static void pass_end(void) {
    for (unsigned i = 0; i < pass.count; i++) {
        const Visitor *v = &pass.v[i];
        uint8_t before = rd(v->room, ENTITIES + v->slot), after = cyc_mod_peek((uint16_t)(ENTITIES + v->live));
        for (unsigned k = FIRST_KEPT; k < SLOT_BYTES; k++)
            wr(v->room, byte_at(k, v->slot), cyc_mod_peek((uint16_t)byte_at(k, v->live)));
        int note[6] = { v->room, v->slot, v->live, before, after, (int)steps };
        memcpy(visit_log[(visits - pass.count + i) % LOG], note, sizeof note);
    }
    for (unsigned s = 0; s < 8; s++)
        for (unsigned k = 0; k < SLOT_BYTES; k++) cyc_mod_poke((uint16_t)byte_at(k, s), pass.saved[s][k]);
    pass.open = 0;
    pass.count = 0;
}
/* Game_MainLoop's JSR GameLoop_CountdownItems: the visitors' pass, once the
 * live loop is over (the first arrival) and once it is (the second). */
static int visit_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) return 0;
    if (pass.open) {
        swap_in();
        pass_end();
        return 0;
    }
    if (running() >= 0) play_pending();
    else pending_count = 0;
    return pass_begin();
}

/* A frame that ends inside the pass (a lag frame: the main loop overran
 * it) leaves the machine between frames with the live room's own slots
 * hidden and visitors in its free ones. Whatever reads it then (its save
 * states, the other features' frame ends, the TCP commands) is to see the
 * room as the game has it, so the frame's end puts the live slots back and
 * keeps the pass's, and the next frame's beginning puts the pass's back
 * before the machine runs on in it. */
void fax_seams_frame_end(void) {
    if (!pass.open || pass.swapped) return;
    for (unsigned s = 0; s < 8; s++)
        for (unsigned k = 0; k < SLOT_BYTES; k++) {
            pass.view[s][k] = cyc_mod_peek((uint16_t)byte_at(k, s));
            cyc_mod_poke((uint16_t)byte_at(k, s), pass.saved[s][k]);
        }
    pass.swapped = 1;
}
static void swap_in(void) {
    if (!pass.open || !pass.swapped) return;
    for (unsigned s = 0; s < 8; s++)
        for (unsigned k = 0; k < SLOT_BYTES; k++) cyc_mod_poke((uint16_t)byte_at(k, s), pass.view[s][k]);
    pass.swapped = 0;
}
void fax_seams_frame_begin(void) { swap_in(); }

/* ---- slice 3: the spell flies on ---- */

/* The spell's X in 16 bits from his room's left edge: its side and its byte,
 * which moves a few pixels a frame (4 at most), so the byte's change since
 * the last look is its move, and a wrap past 0 or 255 changes its side. */
static int spell_x16(void) {
    uint8_t b = cyc_mod_peek(CAST_X);
    if (spell.resync) spell.side = 0, spell.last = b, spell.resync = 0;
    int x = spell.side * 256 + spell.last + (int8_t)(uint8_t)(b - spell.last);
    spell.side = (int8_t)((x + 1024) / 256 - 4); /* floor(x / 256) */
    spell.last = b;
    return x;
}
/* The room pixel column `px` (from his room's left edge) lies in: his, or
 * the one joined on that side; -1 for none, or further than one room. */
static int spell_room(int px) {
    int active = fax_world_active_room();
    if (px >= 0 && px < 256) return active;
    if (px >= -256 && px < 0) return fax_world_joined(active, 0);
    return px >= 256 && px < 512 ? fax_world_joined(active, 1) : -1;
}
/* In the live game, with Seamless's view of this frame. */
static bool spell_live(void) {
    int x, y;
    return enabled && !cyc_mod_isolated() && fax_world_active_room() >= 0 && fax_world_view(&x, &y);
}
/* A sleeper not yet seen in live slot s: the room's own, or a visitor (its
 * wake is its room's). */
static bool dormant(unsigned s) {
    if (!sleeper(cyc_mod_peek((uint16_t)(ENTITIES + s)))) return false;
    int v = visitor_of(s);
    const Block *b = block(v >= 0 ? pass.v[v].room : fax_world_active_room());
    return b && !(b->awake >> (v >= 0 ? pass.v[v].slot : s) & 1);
}

/* Player_CastMagic's entry, in the live game: the view of this frame (he has
 * moved; the spell moves next), the draws to come are the spell's, and with
 * no spell on (CastMagic_Type $FF) a cast starts at his X, in his room: its
 * side 0, its byte his X (the cast's ADC adds 0, or 1 with C). */
static int cast_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) return 0;
    fax_world_update_view();
    spell.casting = 1;
    memset(&spell_drawn, 0, sizeof spell_drawn);
    if (cyc_mod_peek(CAST_TYPE) & 0x80) spell.side = 0, spell.last = cyc_mod_peek(HERO_X), spell.resync = 0;
    return 0;
}
/* GameLoop_CheckUseCurrentItem's entry: Player_CastMagic is over (an item's
 * use draws the hero again, Game_DrawScreenInFrozenState). */
static int cast_done_hook(uint16_t addr) {
    (void)addr;
    if (!cyc_mod_isolated()) spell.casting = 0;
    return 0;
}

/* CastMagic_CheckDirection, moving left (X 0) or right (X 1), in C: its
 * pixel is the spell's X moving left and X plus its width (CAST_MAGIC_WIDTHS)
 * moving right, in 16 bits. Past a side with no room joined (or further than
 * one room) it disperses (Blocks_Result 1), as CastMagic_SetShouldDisperse
 * does at the original's screen edge; otherwise
 * CastMagic_CheckDirection_CheckImpassable's rows, in the room the pixel lies
 * in: at its Y, one row down if CAST_MAGIC_HEIGHTS is 16 or more, then one
 * more if Y is off a block's edge, until one is solid. It leaves what the
 * game's leaves: Blocks_Result and A the result, Arg_PixelPosX and _Y,
 * Temp_00, X the last block's index. Up and down are the original's (0). */
static int direction_hook(uint16_t addr) {
    (void)addr;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    uint8_t type = cyc_mod_peek(CAST_TYPE);
    if (regs.x > 1 || type >= SPELL_TYPES || !spell_live()) return 0;
    int x = spell_x16(), px = regs.x ? x + rom14(CAST_MOVE_WIDTHS + type) : x;
    uint8_t y = cyc_mod_peek(CAST_Y), result = 1;
    unsigned index = ((y & 0xF0u) | (((unsigned)px & 0xFF) >> 4)) & 0xFF;
    int room = spell_room(px);
    cyc_mod_poke(PIXEL_X, (uint8_t)px);
    if (room >= 0) {
        cyc_mod_poke(PIXEL_Y, y);
        cyc_mod_poke(TEMP_00, (uint8_t)(y & 0xF0));
        result = solid(room, index, false);
        if (!result && rom14(CAST_HEIGHTS + type) >= 0x10) result = solid(room, index = (index + 16) & 0xFF, false);
        if (!result && (y & 15)) result = solid(room, index = (index + 16) & 0xFF, false);
        if (result && room != fax_world_active_room()) ++spell_walls;
    }
    cyc_mod_poke(BLOCKS_RESULT, result);
    regs.a = result;
    regs.x = (uint8_t)index;
    regs.p = (uint8_t)((regs.p & ~(FLAG_Z | FLAG_N)) | (result ? 0 : FLAG_Z));
    cyc_mod_set_regs(&regs);
    return 1;
}

/* CastMagic_UpdateXPosition's off-screen test, after the move and the
 * direction check, moving left ($BCD1, CMP #$F0) or right ($BCF2, CMP
 * CAST_MAGIC_MAX_SCREEN_WIDTHS,X): C set ends the spell (every update
 * handler then clears it). In one big world its edge is the view's: moving
 * left, once its X is left of the view's left edge, and moving right once
 * its X plus its width reaches the view's right edge, as the original's
 * ends once X wraps past 0 or X plus its width reaches 256. A side with no
 * room joined is the original's edge still. And Thunder's and Fire's after
 * a first hit (types 6 and 7) keep the original's own rule moving right, a
 * byte read past the table ($B4, $02 of CastMagic_UpdateYPosition's code):
 * Thunder ends at x 180 of the room it is in, Fire at once. Returns from the
 * routine with A the X byte and C as decided. */
static int edge_hook(uint16_t addr) {
    uint8_t type = cyc_mod_peek(CAST_TYPE);
    if (type >= SPELL_TYPES || !spell_live()) return 0;
    int active = fax_world_active_room(), vx, vy, px, py;
    fax_world_view(&vx, &vy);
    fax_world_place(active, &px, &py);
    bool right = addr == CAST_RIGHT_EDGE;
    int x = spell_x16(), w = rom14(CAST_MOVE_WIDTHS + type) + 1;
    uint8_t byte = cyc_mod_peek(CAST_X), limit = right ? rom14(CAST_MAX_WIDTHS + type) : 0xF0;
    int why = 0, edge = 0;
    if (right) {
        edge = vx + VIEW_W - px;
        if (x + w >= 256 && fax_world_joined(active, 1) < 0) why = 2, edge = 256;
        else if (x + w >= edge || x + w >= 512) why = 1;
        else if (type >= 5 && byte >= limit) why = 3, edge = limit;
    } else {
        edge = vx - px;
        if (x < 0 && fax_world_joined(active, 0) < 0) why = 2, edge = 0;
        else if (x < edge || x < -256) why = 1;
    }
    if (why) {
        ++*(why == 1 ? &spell_view_ends : &spell_side_ends);
        int note[5] = { x, edge, why, (int)steps, right };
        memcpy(spell_end, note, sizeof note);
    }
    CycModRegs regs;
    cyc_mod_regs(&regs);
    uint8_t d = (uint8_t)(byte - limit);
    regs.a = byte;
    regs.p = (uint8_t)((regs.p & ~(FLAG_C | FLAG_Z | FLAG_N)) | (why ? FLAG_C : 0) | (why && !d ? FLAG_Z : 0) |
                       (d & FLAG_N));
    cyc_mod_set_regs(&regs);
    return 1;
}

/* CastMagic_CalculateVisibility's entry: MovingSpriteVisibility, the spell's
 * behind-the-background bits, from the block properties at X + 4 (bit 0 for
 * 4, 9 or $0D) and X + 12 (bit 1 for 4 alone: the routine's BNE skips the
 * other two), swapped moving right. When either pixel is past his room's
 * side, in the room it lies in (none there: in front), else the original's.
 * It leaves the routine's registers and its temps. */
static int visibility_hook(uint16_t addr) {
    (void)addr;
    uint8_t type = cyc_mod_peek(CAST_TYPE);
    if (type >= SPELL_TYPES || !spell_live()) return 0;
    int x = spell_x16(), at[2] = { x + 4, x + 12 };
    if (at[0] >= 0 && at[1] < 256) return 0;
    uint8_t y = cyc_mod_peek(CAST_Y), v = 0;
    for (unsigned k = 0; k < 2; k++) {
        int room = spell_room(at[k]);
        uint8_t p = room < 0 ? 0 : property(room, (y & 0xF0u) | (((unsigned)at[k] & 0xFF) >> 4));
        if (p == 4 || (!k && (p == 9 || p == 0x0D))) v |= (uint8_t)(1u << k);
    }
    cyc_mod_poke(TEMP_VISIBILITY, v);
    if ((cyc_mod_peek(CAST_FLAGS) & MOVING_RIGHT) && v && v != 3) v ^= 3;
    cyc_mod_poke(MOVING_VISIBILITY, v);
    cyc_mod_poke(PIXEL_X, (uint8_t)at[1]);
    cyc_mod_poke(PIXEL_Y, y);
    cyc_mod_poke(TEMP_00, (uint8_t)(y & 0xF0));
    leave(cyc_mod_peek(CURRENT_SPRITE_INDEX), v, -1);
    CycModRegs regs;
    cyc_mod_regs(&regs);
    regs.p = (uint8_t)((regs.p & ~(FLAG_Z | FLAG_N)) | (v ? 0 : FLAG_Z)); /* STA MovingSpriteVisibility's A */
    cyc_mod_set_regs(&regs);
    ++spell_visibility;
    return 1;
}

void fax_seams_adopted(int from, int to) {
    bool on = cyc_mod_peek(CAST_TYPE) < 0x80;
    if (!enabled) {
        cyc_mod_poke(CAST_TYPE, 0xFF);
        return;
    } /* the plan's slices off: as Seamless always did */
    spell_x16();
    int fx, fy, tx, ty;
    fax_world_place(from, &fx, &fy);
    fax_world_place(to, &tx, &ty);
    int side = spell.side + (fx - tx) / 256;
    if (fy != ty || side < -1 || side > 1) { /* up or down, or too far: it ends, as it did */
        cyc_mod_poke(CAST_TYPE, 0xFF);
        spell.side = 0;
        spell_cleared += on;
        return;
    }
    spell.side = (int8_t)side;
    spell_crossed += on;
}

/* ---- slice 2: this feature's record ---- */

enum {
    RECORD_VERSION = 2,
    RECORD_VISITOR = 6,
    RECORD_V1 = 5 + 8 * RECORD_VISITOR + 2 * 8 * SLOT_BYTES + HEARD * 3,
    RECORD = RECORD_V1 + 4
}; /* version 2: the spell's side, last byte, resync, casting */
static int state_get(uint8_t *buf, int cap) {
    if (!enabled) return 0;
    if (cap < RECORD) return -1;
    memset(buf, 0, RECORD);
    buf[0] = RECORD_VERSION;
    buf[1] = pass.open;
    buf[2] = pass.count;
    buf[3] = (uint8_t)pending_count;
    buf[4] = pass.swapped;
    uint8_t *p = buf + 5;
    for (unsigned i = 0; i < 8; i++, p += RECORD_VISITOR) {
        const Visitor *v = &pass.v[i];
        p[0] = (uint8_t)v->room, p[1] = v->slot, p[2] = v->live, p[3] = v->flash;
        p[4] = (uint8_t)v->shift, p[5] = (uint8_t)((uint16_t)v->shift >> 8);
    }
    memcpy(p, pass.saved, sizeof pass.saved);
    p += sizeof pass.saved;
    memcpy(p, pass.view, sizeof pass.view);
    p += sizeof pass.view;
    for (unsigned i = 0; i < HEARD; i++, p += 3)
        p[0] = pending[i].effect, p[1] = pending[i].room, p[2] = pending[i].slot;
    p[0] = (uint8_t)spell.side, p[1] = spell.last, p[2] = spell.resync, p[3] = spell.casting;
    return RECORD;
}
static int state_validate(const uint8_t *buf, int len) {
    if (!len) return 1;
    /* Version 1 (slice 2) had no spell: a spell on then is in his room. */
    if (!buf || (!(len == RECORD && buf[0] == RECORD_VERSION) && !(len == RECORD_V1 && buf[0] == 1)) || buf[1] > 1 ||
        buf[2] > 8 || buf[3] > HEARD || buf[4] > buf[1])
        return 0;
    if (len == RECORD) {
        const uint8_t *q = buf + RECORD_V1;
        if ((int8_t)q[0] < -1 || (int8_t)q[0] > 1 || q[2] > 1 || q[3] > 1) return 0;
    }
    for (unsigned i = 0; i < buf[2]; i++) {
        const uint8_t *p = buf + 5 + i * RECORD_VISITOR;
        int16_t shift = (int16_t)(p[4] | p[5] << 8);
        if (p[0] >= ROOMS || p[1] > 7 || p[2] > 7 || p[3] > 1 ||
            (shift != -256 && shift != 0 && shift != 256 && shift != -512 && shift != 512))
            return 0;
    }
    return 1;
}
static void clear_pass(void) {
    memset(&pass, 0, sizeof pass);
    heard_count = pending_count = 0;
}
static void clear_spell(void) {
    spell.side = 0, spell.last = 0, spell.resync = 1, spell.casting = 0;
    memset(&spell_drawn, 0, sizeof spell_drawn);
}
static int state_set(const uint8_t *buf, int len) {
    if (!state_validate(buf, len)) return 0;
    clear_pass();
    clear_spell();
    if (!len) return 1;
    pass.open = buf[1];
    pass.count = buf[2];
    pending_count = buf[3];
    pass.swapped = buf[4];
    const uint8_t *p = buf + 5;
    for (unsigned i = 0; i < 8; i++, p += RECORD_VISITOR)
        pass.v[i] = (Visitor){ (int8_t)p[0], p[1], p[2], p[3], (int16_t)(p[4] | p[5] << 8) };
    memcpy(pass.saved, p, sizeof pass.saved);
    p += sizeof pass.saved;
    memcpy(pass.view, p, sizeof pass.view);
    p += sizeof pass.view;
    for (unsigned i = 0; i < HEARD; i++, p += 3) pending[i] = (Sound){ p[0], p[1], p[2] };
    if (len == RECORD) spell.side = (int8_t)p[0], spell.last = p[1], spell.resync = p[2], spell.casting = p[3];
    return 1;
}

/* ---- what Seamless asks ---- */

void fax_seams_captured(uint8_t *bytes, int how) {
    Block *b = (Block *)bytes;
    if (!b) return;
    if (how == FAX_SEAMS_FRESH) {
        memset(b, 0, sizeof *b);
        return;
    }
    unsigned c = how == FAX_SEAMS_TICK;
    if (!c && block(loop_room[0]) != b) { /* the live loop drew another room: nothing of this one's */
        memset(b->owner, 0, sizeof b->owner);
        b->count = 0;
        return;
    }
    memcpy(b->owner, drawn[c].owner, sizeof b->owner);
    memcpy(b->drawn_tiles, drawn[c].tiles, sizeof b->drawn_tiles);
    b->count = drawn[c].count;
    memcpy(b->cell, drawn[c].cell, sizeof b->cell);
    if (c || b != block(fax_world_active_room())) return;
    /* His room's: the spell's draw too, before the sprite loop's. */
    for (unsigned k = 0; k < 64; k++)
        if (spell_drawn.owner[k]) b->owner[k] = spell_drawn.owner[k];
    for (unsigned k = 0; k < spell_drawn.count && b->count < CELLS; k++) b->cell[b->count++] = spell_drawn.cell[k];
}
unsigned fax_seams_cells(const uint8_t *bytes, const FaxSeamsCell **cells) {
    const Block *b = (const Block *)bytes;
    *cells = b->cell;
    return b->count <= CELLS ? b->count : 0;
}
int fax_seams_oam_slot(const uint8_t *bytes, unsigned oam) {
    const Block *b = (const Block *)bytes;
    return oam < 64 && b->owner[oam] ? b->owner[oam] - 1 : -1;
}
int fax_seams_slot_tiles(const uint8_t *bytes, unsigned slot) {
    const Block *b = (const Block *)bytes;
    return slot < 8 && b->drawn_tiles[slot] ? b->drawn_tiles[slot] - 1 : -1;
}
bool fax_seams_valid(const uint8_t *bytes, unsigned rooms) {
    const Block *b = (const Block *)bytes;
    if (b->count > CELLS) return false;
    for (unsigned s = 0; s < 8; s++)
        if (b->through[s] > (SIDEWAYS | UPRIGHT) || (b->through[s] & SIDEWAYS) == SIDEWAYS ||
            (b->through[s] & UPRIGHT) == UPRIGHT || b->tiles[s] > rooms || b->drawn_tiles[s] > rooms)
            return false;
    for (unsigned k = 0; k < 64; k++)
        if (b->owner[k] > SPELL + 1) return false;
    for (unsigned k = 0; k < b->count; k++)
        if (b->cell[k].slot > SPELL) return false;
    return true;
}
void fax_seams_describe(const uint8_t *bytes, char *out, size_t size) {
    const Block *b = (const Block *)bytes;
    int t[8];
    for (unsigned s = 0; s < 8; s++) t[s] = b->tiles[s] ? b->tiles[s] - 1 : -1;
    snprintf(out, size,
             "\"awake\":%u,\"through\":[%u,%u,%u,%u,%u,%u,%u,%u],\"tiles\":[%d,%d,%d,%d,%d,%d,%d,%d],\"cells\":%u,"
             "\"flash\":%u",
             b->awake, b->through[0], b->through[1], b->through[2], b->through[3], b->through[4], b->through[5],
             b->through[6], b->through[7], t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7], b->count, b->flash);
}

/* ---- the plugin ---- */

static const struct {
    const char *id;
    uint16_t addr;
    NESModFunctionEntryCallback hook;
} hooks[] = {
    { "faxanadu.seams.reach", OUT_OF_WEAPON_RANGE, reach_hook },
    { "faxanadu.seams.touch", CHECK_HIT_PLAYER, touch_hook },
    { "faxanadu.seams.loop", SPRITES_UPDATE_ALL, loop_hook },
    { "faxanadu.seams.loop-done", SPRITES_LOOP_DONE, loop_done_hook },
    { "faxanadu.seams.behavior", UPDATE_BEHAVIOR, behavior_hook },
    { "faxanadu.seams.state", UPDATE_STATE, state_hook },
    { "faxanadu.seams.left", MOVE_LEFT, left_hook },
    { "faxanadu.seams.right", MOVE_RIGHT, right_hook },
    { "faxanadu.seams.vertical", MOVE_VERTICAL, vertical_hook },
    { "faxanadu.seams.placement", PLACEMENT_BLOCK, placement_hook },
    { "faxanadu.seams.flier", FLIER_EDGE, flier_hook },
    { "faxanadu.seams.draw", DRAW_STANDARD, draw_hook },
    { "faxanadu.seams.draw-flipped", DRAW_FLIPPED, draw_hook },
    { "faxanadu.seams.load", LOAD_SPRITE_INFO, load_hook },
    { "faxanadu.seams.visit", MAIN_LOOP_COUNTDOWN, visit_hook },
    { "faxanadu.seams.magic", CHECK_MAGIC, magic_hook },
    { "faxanadu.seams.shield", CHECK_SHIELD, shield_hook },
    { "faxanadu.seams.weapon", WEAPON_OFF_SCREEN, weapon_hook },
    { "faxanadu.seams.sound", SOUND_PLAY_EFFECT, sound_hook },
    { "faxanadu.seams.cast", PLAYER_CAST_MAGIC, cast_hook },
    { "faxanadu.seams.cast-done", CHECK_USE_ITEM, cast_done_hook },
    { "faxanadu.seams.spell-direction", CAST_CHECK_DIRECTION, direction_hook },
    { "faxanadu.seams.spell-left", CAST_LEFT_EDGE, edge_hook },
    { "faxanadu.seams.spell-right", CAST_RIGHT_EDGE, edge_hook },
    { "faxanadu.seams.spell-visibility", CAST_VISIBILITY, visibility_hook },
    { "faxanadu.seams.down", MOVE_DOWN, down_hook },
    { "faxanadu.seams.up", MOVE_UP, up_hook },
    { "faxanadu.seams.cap", CAP_VERTICAL, cap_hook },
    { "faxanadu.seams.fall", FALL_EDGE, fall_hook },
};
enum { HOOKS = sizeof hooks / sizeof hooks[0] };

static void deactivate(void) {
    enabled = false;
    skipped = refused = 0;
    last_weapon_x = 0x7FFF;
    last_sprite_x = 0;
    last_entity = 0xFF;
    steps = moves = kept_out = town_edge = removed = woken = frozen = pushed_back = 0;
    fell_on = moves_up = moves_down = 0;
    memset(move_log, 0, sizeof move_log);
    for (unsigned i = 0; i < 4; i++) last_woken[i] = -1;
    clear_pass();
    passes = visits = waited = flash_runs = flashes_due = broken = 0;
    touch_refused = magic_refused = shield_refused = shield_skipped = weapon_placed = heard_total = played = 0;
    memset(visit_log, 0, sizeof visit_log);
    memset(sound_log, 0, sizeof sound_log);
    clear_spell();
    spell_walls = spell_view_ends = spell_side_ends = spell_crossed = spell_cleared = magic_dormant = 0;
    spell_visibility = spell_drawn_abroad = 0;
    memset(spell_end, 0, sizeof spell_end);
    spell_end[3] = spell_end[4] = -1;
    looping[0] = looping[1] = false;
    loop_room[0] = loop_room[1] = -1;
    for (unsigned i = 0; i < HOOKS; i++) nes_mod_set_function_hook_enabled(hooks[i].id, 0);
}
static void activate(void) {
    enabled = true;
    for (unsigned i = 0; i < HOOKS; i++) nes_mod_set_function_hook_enabled(hooks[i].id, 1);
    nes_mod_set_local_only("Seams", 1);
}

#ifdef CYC_WITH_SDL
/* The last `count` of a ring of `size` entries of `n` ints, oldest first, as JSON arrays. */
static size_t ring_json(char *out, size_t cap, const int *ring, unsigned n, unsigned size, unsigned count) {
    size_t at = 0;
    out[0] = 0;
    for (unsigned i = count > size ? count - size : 0; i < count && at < cap; i++) {
        const int *m = ring + (i % size) * n;
        at += (size_t)snprintf(out + at, cap - at, "%s[", at ? "," : "");
        for (unsigned k = 0; k < n && at < cap; k++)
            at += (size_t)snprintf(out + at, cap - at, "%s%d", k ? "," : "", m[k]);
        if (at < cap) at += (size_t)snprintf(out + at, cap - at, "]");
    }
    return at;
}
static void seams_state(int id, const char *line) {
    (void)line;
    char fields[4200], log[700], visit[700], sound[300];
    int vx = 0, vy = 0;
    bool view = fax_world_view(&vx, &vy);
    uint8_t type = cyc_mod_peek(CAST_TYPE), byte = cyc_mod_peek(CAST_X);
    int x16 = spell.resync ? byte : spell.side * 256 + spell.last + (int8_t)(uint8_t)(byte - spell.last);
    ring_json(log, sizeof log, &move_log[0][0], 6, LOG, moves);
    ring_json(visit, sizeof visit, &visit_log[0][0], 6, LOG, visits);
    ring_json(sound, sizeof sound, &sound_log[0][0], 4, HEARD, played);
    snprintf(
        fields, sizeof fields,
        "\"enabled\":%s,\"skipped\":%u,\"refused\":%u,\"last_weapon_x\":%d,\"last_sprite_x\":%u,\"last_entity\":%u,"
        "\"steps\":%u,\"moves\":%u,\"kept_out\":%u,\"town_edge\":%u,\"pushed_back\":%u,\"removed\":%u,\"woken\":%u,\"frozen\":%u,"
        "\"view\":[%d,%d,%s],\"last_woken\":[%d,%d,%d,%d],\"log\":[%s],"
        "\"passes\":%u,\"visits\":%u,\"waited\":%u,\"visiting\":%s,\"broken\":%u,\"flashes_due\":%u,\"flash_runs\":%u,"
        "\"touch_refused\":%u,\"magic_refused\":%u,\"shield_refused\":%u,\"shield_skipped\":%u,\"weapon_placed\":%u,"
        "\"heard\":%u,\"played\":%u,\"pending\":%u,\"visit_log\":[%s],\"sound_log\":[%s],"
        "\"spell\":[%u,%u,%d,%d],\"spell_walls\":%u,\"spell_view_ends\":%u,\"spell_side_ends\":%u,"
        "\"spell_end\":[%d,%d,%d,%d,%d],\"spell_crossed\":%u,\"spell_cleared\":%u,\"magic_dormant\":%u,"
        "\"spell_visibility\":%u,\"spell_drawn_abroad\":%u,\"casting\":%s,"
        "\"moves_up\":%u,\"moves_down\":%u,\"fell_on\":%u",
        enabled ? "true" : "false", skipped, refused, last_weapon_x, last_sprite_x, last_entity, steps, moves, kept_out,
        town_edge, pushed_back, removed, woken, frozen, vx, vy, view ? "true" : "false", last_woken[0], last_woken[1],
        last_woken[2], last_woken[3], log, passes, visits, waited, pass.open ? "true" : "false", broken, flashes_due,
        flash_runs, touch_refused, magic_refused, shield_refused, shield_skipped, weapon_placed, heard_total, played,
        pending_count, visit, sound, type, byte,
        x16 >= 256 ? 1
        : x16 < 0  ? -1
                   : 0,
        x16, spell_walls, spell_view_ends, spell_side_ends, spell_end[0], spell_end[1], spell_end[2], spell_end[3],
        spell_end[4], spell_crossed, spell_cleared, magic_dormant, spell_visibility, spell_drawn_abroad,
        spell.casting ? "true" : "false", moves_up, moves_down, fell_on);
    cyc_tcp_ok(id, fields);
}
void fax_seams_tcp_setup(void) {
    cyc_tcp_register("seams_state",
                     "Seams: touches skipped in neighbouring rooms' copies, weapon hits refused for wrapping round the "
                     "screen; actors moved through seams, kept out of full rooms, kept at a town's edge, shots gone "
                     "out of the view, "
                     "sleepers woken and kept frozen, the view, the last move and wake; visitors' passes, visits and "
                     "their outcomes, matches refused for wrapping, neighbours' sounds heard and played; the spell "
                     "(type, X byte, side, X in 16 bits), its walls across a seam, its ends at the view's edge and at "
                     "a side, the last end, crossings kept, dormant sleepers it was kept from; moves up and down, "
                     "falls carried on into a room below",
                     seams_state);
}
#else
void fax_seams_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_seams) {
    for (unsigned i = 0; i < HOOKS; i++)
        if (!nes_mod_register_function_entry_plugin(hooks[i].id, hooks[i].addr, hooks[i].hook)) abort();
    if (!nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.seams", activate) ||
        !nes_mod_register_savestate_hook("faxanadu.seams", state_get, state_set) ||
        !nes_mod_register_savestate_validator("faxanadu.seams", state_validate))
        abort();
}
