# Faxanadu internals

What we know about how Faxanadu (USA, game data CRC32 `57DD23D1`) works, and
about the engine we run it on, gathered while building features. Add to it
whenever you learn something, in the same PR as the code that relies on it,
and correct it when it turns out wrong. Addresses are CPU addresses; "bank N"
means the 16 KiB PRG bank mapped at `$8000-$BFFF` (bank 15 is fixed at
`$C000-$FFFF`).

## Sources

- **chipx86's disassembly** ([chipx86/faxanadu](https://github.com/chipx86/faxanadu)):
  annotated ca65 sources that rebuild the ROM byte for byte. `tools/disassembly.sh`
  fetches, assembles and verifies it, and writes `build-tests/disassembly/labels.txt`
  with every label's address. Read the code in `build-tests/disassembly/src/`.
  The repo's Mesen `.mlb` is older than the sources and its addresses are wrong.
- **chipx86's notes** ([Faxanadu Disassembly](https://notes.chipx86.com/Projects/Reverse-Engineering+Projects/Faxanadu+Disassembly/!+Overview)),
  notably *Interaction Scripts*. One error there: it says script parameters are
  big-endian. They are little-endian (`IScripts_JumpToNextAddr` reads the low
  byte first), as its own King example shows.
- **faxanatools** ([chipx86/faxanatools](https://github.com/chipx86/faxanatools)):
  Python tooling for messages, scripts, shops and passwords.

## ROM layout

MMC1, 16 PRG banks of 16 KiB, 8 KiB CHR RAM, no PRG RAM.

| Bank | Holds |
|---|---|
| 5 | Sound |
| 12 | Interaction scripts and their interpreter, the password (mantra) code |
| 13 | Message strings and the text renderer's glyphs |
| 14 | Gameplay logic (actors, combat, magic) |
| 15 | Fixed: main loop, spawning, screen loading, the text box |

**Unused ROM** (all `$FF` in the stock image). Claim space in
`mods/allocations.toml` before using any:

| Bank | Range | Size | Notes |
|---|---|---|---|
| 12 | `$AD8A-$BFFF` | 4,726 bytes | Guru travel uses `$AE00-$AE06` (and lands on `$AE07` without reading it), Save files `$AE10-$AE17` |
| 13 | `$B45B-$BFFF` | 2,981 bytes | Keep `$B45A` `$FF` (see Messages). Guru travel and Save files share `$B45B-$B48A` |

**Existing bytes** a feature serves with other values (a tuned table entry,
say) are claimed too, as `overrides` with their stock values, which
`tools/check_allocations.py --rom` checks. The Taller dagger serves `$B898`
(bank 14), Save files the Buy/Sell menu's message operand, `$8631` (bank 12).
Two features may claim the same bytes only through a `[[shared_rom]]` entry
saying why, as Guru travel and Save files do for the line slot.

**Rev A** (`Faxanadu (USA) (Rev A)`, CRC32 `E71DB268`) matches the original in
banks 5, 14 and 15, but bank 12 differs from `$9000` (the password code moved,
and data tables after it shifted). Not supported: the build rejects it.

## RAM worth knowing

| Address | Name | Notes |
|---|---|---|
| `$0019` | `Joy1_ChangedButtonMask` | Buttons newly pressed this frame. A=`$80`, B=`$40`, Select=`$20`, Start=`$10`, Up=`$08` |
| `$001A` | `InterruptCounter` | Counts every NMI, lag frames included. Read it in the same `read_ram` as other state to time things to the frame over TCP |
| `$0024` | `Area_CurrentArea` | 4 = inside a building |
| `$0063` | `Area_CurrentScreen` | |
| `$009E`, `$00A1` | `Player_PosX_Block`, `Player_PosY` | |
| `$00DB-$00DD` | `IScriptOrCHRAddr`, `IScriptOffset` | The interpreter reads the next action at addr + offset |
| `$0100` | `CurrentROMBank` | 14 in the main loop. The stack shares this page |
| `$0127` | `SoundEffect_State_Counter` | The playing sound effect's tick; the typing sound runs 1-4 |
| `$0208-$020B` | `TextBox_X`, `TextBox_Y`, `TextBox_Width`, `TextBox_Height` | The box last opened or closed, in tiles (see *The text box*) |
| `$0212` | `TextBox_PlayTextSound` | The typing sound plays for a character if set |
| `$0213` | `MessageID` | 0 once a message has typed out; otherwise stale after a conversation, and still stale (with `$0215`) as the next script's portrait opens, until its first message loads |
| `$0214` | `Message_ProcessedLength` | Bytes of the message read, from an offset, not 0. Wraps with the address |
| `$0215` | `TextBox_MessagePaused` | Nonzero while a `$FC` pause waits for A |
| `$021D` | `TextBox_Timer` | Characters type when it is a multiple of 4. `Messages_Load` zeroes it |
| `$02CC-$02D3` | `CurrentSprites_Entities` | Each sprite slot's entity, `$FF` empty (see *What killed the hero*) |
| `$0378` | `CurrentSpriteIndex` | The slot whose behavior or collision is running |
| `$0390`, `$0392-$0394` | `Experience`, `Gold` | Gold is 24-bit little-endian |
| `$039A` | `Player_MP` | |
| `$03C7` | `IScript_PortraitID` | The portrait showing, `$FF` for none (see *Portraits*) |
| `$042C` | `SpecialItems` | Bit flags; `$08` the Elixir, which saves a death once |
| `$042D` | `Quests` | Bit flags |
| `$0431`, `$0432` | `Player_HP_U`, `Player_HP_L` | |
| `$0438` | `PlayerIsDead` | Set it to 1 and the main loop runs the death path |
| `$0439` | `TempleSpawnPoint` | Where death respawns and a mantra continues, 0-7. In the original, the furthest temple reached; with Save files, the temple of the Guru last saved with |
| `$04BC`, `$04BD` | `Arg_PlayerHealthDelta_L`, `_U` | The HP `Player_ReduceHP` takes: fraction, whole |
| `$0600-$061F` | `Password_EnteredChars` | The mantra screen's characters, as 6-bit values (shares `ScreenBuffer`) |
| `$0666` | `Password_EnteredCharsLength` | |
| `$0687` | `DAT_0687` | The title's choice: 0 Start, 1 Continue |

## Interaction scripts (bank 12)

Every conversation, shop and item pickup is a script: an entity byte (`$80+`
picks the portrait: `$80` King, `$81` Guru, `$82` Martial Artist, `$83`
Magician, `$84` Doctor, `$85` Nurse, ...; `$00` no portrait), then actions.
Pressing **Up** while touching an NPC runs its script
(`Player_CheckHandlePressUpOnNPC`). Scripts run to completion inside the main
loop's sprite update, with the screen frozen.

`IScripts_InvokeNextAction` (`$826E`) reads the next action byte at
`IScriptOrCHRAddr + IScriptOffset` and dispatches through `IScriptActions_L/U`
with the RTS trick, so actions are not subroutines. Each action ends by jumping
back to it. That makes it the one place to observe or redirect a script
between actions.

| Action | Params | Effect |
|---|---|---|
| `$00` | | End: close the box |
| `$01` | msg | Message B cannot skip |
| `$02` | msg | Question: A continues, B ends the script |
| `$03` | msg | Message |
| `$04` | addr | Jump if the player's title went up |
| `$06` | temple | Set spawn point. **Only raises** `TempleSpawnPoint` |
| `$08` | items | A shop's buy list (see *Shops*) |
| `$0F` | addr | The shops' Buy/Sell menu: message `$10`, then a cursor on its lines 1 and 3. A on the first choice jumps to addr, A on the second goes on to the next action, B ends the script |
| `$11` | items | A shop's sell list |
| `$14` | | Show the mantra (`IScriptAction_ShowPassword`): encodes it, types it a character a frame, waits for A |
| `$16` | msg, addr | Question: A jumps to addr, B goes on to the next action. Unused by the game, works |
| `$17` | addr | Jump |

The full table is in chipx86's notes. The Gurus' scripts:

- Each temple Guru: `06 temple`, `04 NEW_RANK`, `17 PASSWORD`.
  `ISCRIPT_COMMON_GURU_PASSWORD` (`$A5FC`) is `03 22, 14, 00`, and
  `ISCRIPT_COMMON_GURU_NEW_RANK` (`$A600`) is `01 23, 00`.
- Eolis's Guru (`$A0BC`) sets no spawn point. With the Ring of Elf he says
  `03 2D, 00` at `$A0C6`.
- Death runs script `$1F` ("Don't have negative thoughts", message `$24`),
  not a Guru's.
- Only `ISCRIPT_COMMON_GURU_PASSWORD` uses action `$14`. Without a new title
  every temple Guru says message `$22` ("You need peace of mind. I will
  meditate with you.") and shows the mantra; with one, he says `$23` and no
  mantra. Eolis's Guru never shows it.

**The Buy/Sell menu** (`IScriptAction_ShowBuySellMenu`, `$8630`) is any
two-choice menu: its labels are just message `$10`'s four lines ("Come here /
to buy. / Come here / to sell."), and `Menu_UpdateAndDraw` draws the cursor
two lines apart, a column left of the text. The message ID is the immediate of
its first instruction, `LDA #$10`, at `$8631`: a read override there shows any
message instead, so Save files sets it for its one menu and puts it back at
the end of the frame, once the menu has read it. Immediates are read at run
time, so overriding one works.

### Portraits

`IScripts_Begin` copies a script's entity byte to `Temp_0201` (`$0201`, a
shared temporary) and, for `$80` and up, has `IScripts_LoadPortraitTiles`
store it **less its top bit** in `IScript_PortraitID` (`$03C7`): 0 King, 1
Guru, 2 Martial Artist, 3 Magician, 4 Doctor, 5 Nurse, 6 Pink Shirt Guy, 7
Smoking Guy, 8 Meat Salesman, 9 Tools Salesman, 10 Key Salesman. Then it frames
the portrait (`IScripts_OpenForPortrait`) and opens the dialogue box.

- The ID holds through every action of the script: messages, questions, the
  Buy/Sell menu, shop lists, the mantra and Guru travel's lines.
  `IScriptAction_EndScript` sets it back to `$FF` (`IScripts_ClearPortraitImage`)
  before it clears both boxes (`TextBox_ClearForPortraitAndText`).
  `Screen_ClearSprites` also sets it to `$FF`, so it is `$FF` whenever no
  portrait shows. A script without a portrait (`$00`) leaves it alone.
- **At power-on RAM is 0**: on the title screen `IScript_PortraitID` reads 0
  (the King) and `TextBox_Closed` 0 (a box open). Check the area too.
- Every script with a portrait runs in a building (`AREA_BUILDINGS`); bank 11's
  sprite lists for area 4 put each on its building. **No script shows the Pink
  Shirt Guy's or the Smoking Guy's portrait.**

| Portrait | Buildings (`Area_CurrentScreen` in area 4) |
|---|---|
| King | `$07` (Eolis, the King's room) |
| Guru | The 8 temples (*Temples and spawning*), and `$42` in the Castle of Fraternal (Dartmoor `$1E`, block `$A7`) |
| Martial Artist | `$06` (Eolis) |
| Magician | `$05` (Eolis), `$1C` (the "magic hall outside town", Mist `$29`) |
| Doctor | `$0C`, `$11`, `$1B`, `$24`, `$2C`, `$3F`: the hospitals |
| Nurse | `$0C`, `$11`, `$1B` (hospitals), `$31` (Daybreak's meat shop) |
| Meat Salesman | `$00`, `$0E`, `$17`, `$26`, `$2A`, `$31`, `$3E` |
| Tools Salesman | `$04`, `$08`, `$0F`, `$14` (Trunk `$06`), `$18`, `$20` (Mist `$0D`), `$2D`, `$30`, `$41`, `$45` |
| Key Salesman | `$03`, `$09`, `$13`, `$19`, `$27`, `$2F`, `$40` |

The secret "Guru" of the Tower of Fortress ($48, $58 "the Guri") is the man in
building `$15`, a temple room on Trunk `$2D`, but his script (`ISCRIPT_A18A`)
has no portrait. Nor do the King at the end (`ISCRIPT_KING_END_GAME`, "Glad
you're back.", building `$44`) and the Smoking Man of `$3D` (`ISCRIPT_A331`);
the priest of `$3B` has no script. The Nurse in Daybreak's meat shop is the
`SPRITE_NPC_NURSE` sprite, as in the hospitals.

To list who is where: read area 4's sprite lists (`SPRITE_INFO_VICTIM_SCREEN_*`,
one area off), map each `ISCRIPT_ENTRYPOINT_*` through `ISCRIPT_ADDRS_L` to its
script, and read the script's first byte.

How the dialogue names them: "the king" / "The King is waiting for you." ($27,
$33), "The Guru of Conflate" ($94) and "the Guru in the Castle of Fraternal"
($9D), "a martial art lesson" ($19), "I shall give magic" and "I'll teach you"
from the Magician ($15, $17), "Our doctor can help." ($45), "I sell keys." and
"I sell tools." ($0E, $0F). Nobody names the nurse or the meat salesman.

**Eolis's buildings, for tests** (`EOLIS_DOOR_LOCATIONS`; a door's block is
`YX`, so x = (block & 15) × 16):

| Building | Kind | Door | NPC (`YX`) |
|---|---|---|---|
| `$00` | Meat shop | screen 1, block `$93` | `$84`, behind the counter: walk left until stopped |
| `$01` | House | screen 2, `$9E` | |
| `$02` | Temple | screen 2, `$95` | Guru `$82` |
| `$03` | Key shop | screen 3, `$9D` | `$84` |
| `$04` | Tool shop | screen 3, `$9A` | `$73`, behind the counter (stops at x=80) |
| `$05` | Magic shop | screen 6, `$9C`, Ring of Elf | `$71` |
| `$06` | Martial arts | screen 6, `$9A`, Ring of Elf | `$87`, mid-room: stop at x 124 |
| `$07` | King's room | screen 7, `$98`, Ring of Elf | `$72`, atop stairs: jump up them (Left and A) |

The King's guard (`ISCRIPT_EOLIS_KINGS_GUARD`, no portrait) stands at `$9A` on
screen 7, two blocks right of the King's door. The Eolis Guru gives the Ring
of Elf (`SpecialItems` `$80`) the first time; the King gives 1500 gold only to
a hero with none.

## Messages (bank 13)

`Messages_Load` finds message N by counting `$FF` terminators from
`MESSAGE_STRINGS` (`$8300`). There is no lookup table. Messages 1-193 end at
`$B3xx`, followed by glyph data that contains no `$FF`, so the scan counts the
glyph block as message `$C2`, ending at the first unused byte, `$B45A`. A new
message therefore starts at `$B45B` with ID `$C3`.

Encoding: `$21-$7E` ASCII, `$FD` space, `$FE` newline (ignored at the start
of a line), `$FC` pause, `$FB` the player's title, `$FF` end.
faxanatools' `messages.py` decodes and encodes them; `fax_travel.c`'s
`encode()` checks the box limits below.

## The text box

- 16 columns, 4 lines. Text wraps by character at column 16, so the game's
  own strings drop the space at a line end ("atthe"). Write lines of at most
  16 characters with explicit newlines.
- Past the 4th line, the box scrolls up a line at a time.
- One character every 4 frames.
- `$FC` waits for A with a flashing ▼, then keeps going in the same box.
- Questions end with a flashing "?". A continues; B dismisses at any point,
  even mid-text. Both read `Joy1_ChangedButtonMask`, so a held button acts
  once.
- The box is waiting for a button when `MessageID` is 0 (typed out) or
  `TextBox_MessagePaused` is set. Wait on that rather than on a timer.
  Only a pause sets `TextBox_MessagePaused` ($FF), and only continuing past
  one (or the mantra box) clears it, so it can hold a stale nonzero value: $9F
  all through a new game's intro. For a line with no pause, wait on
  `MessageID` alone.
- Between two messages the box is wiped and refilled. A screenshot taken then
  shows half of each.
- **A message after one that scrolled shows rotated.** The box's rows show
  CHR line blocks from `TextBox_LineScrollOffset` (`$021A`) on; a scroll
  lays the rows out again and advances it. `Messages_Load` falls into
  `TextBox_ClearPasswordSize` (`$F434`), which zeroes the offset, but nothing
  lays the rows out again until a new box opens
  (`IScripts_FillPlaceholderText`). So after a message of more than 4 lines
  (a new title's, `$23`, has 5), the next message in the same box starts on
  the wrong row: "journey. / Send me to / another Guru. / Remember my".
  Putting the offset back right after the zeroing (`$F439`) fixes it, as
  Save files does for its lines and Guru travel's after them. Guru travel's
  intro after a new title, with no save file in play, still shows rotated.
- The font is one bit a pixel in bank 13 at `TILE_GLYPHS_START` (`$8000`),
  8 bytes a character from `$20` to `$7F` (`A` is at `$8108`). The glyph data
  ends where the messages begin (`$8300`). `TextBox_WriteChar` copies a glyph
  into both bit planes of a pattern tile, so text is color 3 of the box's
  palette.
- `TextBox_Open` frames a box with background tiles 1 (top left), 2 (top and
  bottom), 3 (top right), 4 (sides), 5 (bottom left) and 6 (bottom right)
  around tile 0 (black), from the playfield's background pattern table.
  `UI_DrawHUDSprites` copies those (60 tiles in all) from bank 10,
  `HUD_TILES_START` (`$8140`), to PPU `$1000`, so tile t is at
  `$8140 + t * 16` in ROM; `fax_text_box_rom` draws the frame from there on
  screens whose pattern tables hold something else, like the title.
  `TextBox_Closed` (`$020C`) is 0 while a box or the Select menu is open, and
  nonzero once one has closed (also before the first).
- **Where boxes go**, in tiles of the 256 x 240 screen (the HUD is rows 0-3),
  as `TextBox_X`, `TextBox_Y`, `TextBox_Width`, `TextBox_Height` (`$0208-$020B`)
  hold them; the frame is inside the size, and text starts 2 tiles in:

  | Box | Set by | X, Y | Size |
  |---|---|---|---|
  | Portrait | `TextBox_SetCoordsForPortrait` | 2, 14 | 8 x 10 |
  | Dialogue, with a portrait | `TextBox_SetCoordsForNPC` | 10, 16 | 20 x 8 |
  | Dialogue, without | `TextBox_SetCoordsForNPC` | 10, 6 if `Player_PosY` >= 64, else 10, 16 | 20 x 8 |
  | A shop's list (buy or sell) | `Shop_Draw` | 10, 8 | 20 x 20 |
  | Both closed at a script's end | `TextBox_ClearForPortraitAndText` | 2, 8 | 30 x 20 |
  | The Select menu | `PlayerMenu_Show` | 2, 6 | 10 x 14 |

  A shop's list covers the dialogue box and rises above it; picking from it
  (`Maybe_Shop_DrawTextBox`) closes the list and opens the dialogue box again.
  The portrait box stays up throughout.
- `TextBox_Open` and `TextBox_Close` only queue nametable writes in the PPU
  buffer, top row first, and the queue drains over the next NMIs (waiting when
  it is full), so a box appears and goes over a few frames. `TextBox_Closed`
  changes when the writes are queued, not when they show. To know a box is on
  screen, read its corner tiles from the nametable, through the line's scroll
  (`cyc_render_nametable`, `cyc_render_line_scroll_x/y`; `fax_speaker.c`).
- The HUD and every box share one palette, `UI_AttributeDataIndex` (`$038D`,
  0-3), which `Screen_LoadUIPalette` sets from the screen's palette
  (`PALETTE_INDEX_TO_UI_BG_ATTRIBUTE_INDEX`, bank 11). The frame's colors
  differ from place to place for that reason.

### How text types (bank 15)

- Every message loop (`IScriptAction_ShowMessage`, `..._ShowUnskippableMessage`,
  the questions, shops, `IScripts_ShowFinalMessage`) runs once a frame:
  `IScripts_UpdatePortraitAnimation` (which waits for the frame), then
  `TextBox_ShowNextChar` (`$F466`), then a `TextBox_CheckShouldContinue*`.
- `TextBox_ShowNextChar` increments `TextBox_Timer`, sets
  `TextBox_PlayTextSound` to 1 and calls `TextBox_ShowNextChar_IfReady`
  (`$F491`), which nothing else calls. That returns unless a message is
  loaded, it isn't paused and the timer is a multiple of 4; then it reads one
  message byte and draws it. So the game's pace is one byte every 4 frames,
  `$FC` and the end each taking a byte's turn. Nothing else reads the timer.
- Each character queues 19 bytes to the PPU buffer: one 16-byte glyph tile of
  CHR RAM, at `$1400 + line * $100 + column * 16`. Writers wait for buffer
  space (`PPUBuffer_WaitForCapacity`) rather than overflow it. One character
  a frame keeps up: Hold A to speed up text types the Guru's greeting at
  exactly a byte a frame, with no stalls.
- The continue checks read only `Joy1_ChangedButtonMask`, which the NMI sets
  to `held & ~previous`. A on a ▼ or a finished message goes on, and B
  dismisses a skippable one, only on the frame the button goes down. A held
  button never acts again. Shop menus (`IScripts_WaitForMenuInput`) and the
  mantra's ▲ work the same way.
- The typing sound (sound effect 1, noise) lasts 4 ticks: 2 of a soft hiss,
  then 2 of a sharp clack (chipx86 traces the clack to an out-of-bounds table
  read). At the game's pace each character starts it just as the last one
  ends. Started every frame, it never gets past the hiss.
- After `Messages_Load` sets `MessageID`, the first `TextBox_ShowNextChar`
  comes about 12 frames later, while the box opens. Those are lag frames, so
  the pad isn't read either. Time typing from the first character, not from
  `MessageID`.
- The mantra types a character a frame through its own
  `PasswordScreen_ShowNextChar`, not `TextBox_ShowNextChar`.

## Temples and spawning (bank 15)

| Temple | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| Town | Eolis | Apolune | Forepaw | Mascon | Victim | Conflate | Daybreak | Dartmoor |
| Interior screen (area 4) | `$02` | `$0B` | `$10` | `$1E` | `$23` | `$2B` | `$33` | `$3C` |

- `Game_SpawnInTemple` (`$DD61`) lays out the temple at `TempleSpawnPoint`.
  That's the interior, and the town and position you come out at, all from
  per-temple tables after it. It only touches memory.
- `Player_Spawn` (`$DB0A`) sets HP and MP to 80, clears a quest bit (the
  Mattock barrier) and timed items, then does `JSR Game_SpawnInTemple` (`$DB1D`),
  `JSR Game_SetupEnterBuilding`, `JMP Game_MainLoop`.
- `Player_HandleDeath` (`$D8EC`) resets the stack and runs the death script.
  It then sets gold and experience from the title, then `Screen_FadeToBlack`
  (`$DA2F`), `Game_InitStateForSpawn` (`$DA7D`), `Player_Spawn`.
- You arrive at x=224 facing left; the Guru stands on the left. Walk out
  through the right edge.
- `Game_Start` (`$DB26`) runs only for a new game. A mantra continues through
  `Player_Spawn`.
- `Player_Spawn`'s `JSR Game_SpawnInTemple` is at `$DB1D` and returns to
  `$DB20` (`JSR Game_SetupEnterBuilding`): every respawn passes there, after a
  death, a Guru travel journey or a continue. `Game_SpawnInTemple` sets
  `Area_CurrentScreen` to the temple's interior (`$DDD5,X`), but not yet
  `Area_CurrentArea`.
- `Game_MainLoop` (`$DB45`) resets the stack and maps bank 14 each frame.
  `GameLoop_CheckShowPlayerMenu` (`$E016`) is called from it after sprites
  update, so it runs right after any conversation ends.

## The engine (nesrecomp's cycle host)

What a feature can rely on, and the traps:

- **Never change the cart image at runtime.** Save states store a hash of the
  whole PRG (`cyc_prg_hash`) and refuse to load if it differs ("save state of
  another program"). Supply data through read overrides instead.
- **Read overrides** (`cyc_mod_set_prg_read_override`) change what the CPU
  reads at one PRG byte. They are not machine state: they are not hashed or
  saved, so restore whatever you serve from your save-state record. There
  are 64, shared by every feature, and an entry, once made, is never freed
  (setting it back to the stock byte keeps the slot). Guru travel takes 55
  (its 48-byte line slot and 7 step bytes), the Taller dagger 1, and Save
  files shares the slot and adds 5 (4 step bytes and the menu's operand), or
  8 if Guru travel's steps are not served (its own 3-byte SAY): 61 to 64 of
  64. Block borrows Guru travel's SAY step and slot, and Quick shops takes
  none. `tools/check_allocations.py` counts every byte claimed in
  `mods/allocations.toml` once and fails past 64, so the claims, now 64, are
  the budget. A new message for the text box should share the slot, and
  anything more needs the table made larger or Guru travel's slot cut to its
  longest line.
- An override works on an **immediate operand** in used ROM (the recompiler
  reads immediates at run time), not only on data: Save files changes the
  Buy/Sell menu's `LDA #$10` for one menu. Put such a byte back as soon as it
  has been read, so nothing else sees it.
- **Hook sites** (`game.toml` `[[mod_function_hook]]`, keyed by the bytes
  there) call C before an instruction. Returning nonzero returns from the
  routine as its RTS would, from the current S. A callback can set S and the
  stack first to return somewhere else. That's how Guru travel enters the
  respawn path. When a site handles, later sites at the same address are
  skipped that time, so declare shared addresses in `mods/allocations.toml`.
  Sites at one address run in the order `game.toml` declares them:
  `cyc_hooks_fire` (`nesrecomp/runner/cyc/cyc_hooks.c`) walks
  `cyc_native_hook_sites`, which the recompiler generates in `game.toml`'s
  order, and stops at the first that handles or leaves the address. The link
  order of `HOST_EXTRAS` plays no part. Every feature is on at once, so a
  shared address's `[[shared_site]]` lists its hooks in that order and says
  why that order is right; `$AB44` depends on it (Sugata's flash's site before
  Block's). The legacy backend builds no features.
- **A jump from any site.** Handling pulls the return address from S + 1 and
  S + 2 and goes on at it + 1. So a site anywhere can go anywhere in the same
  routine or another: push the target - 1 (high byte first, as a JSR does)
  and return 1, and S ends where it was. Pushing two targets runs a routine
  first and returns into the second (Quick shops closes a list that way), and
  overwriting a subroutine's own return address at S + 1 leaves it at its
  caller's level. The scheduler runs any PC, mid-routine or not.
- A hook site can be **any instruction**, not only a routine's entry. Each
  instruction is its own label in the generated code, so a site where a
  branch and a fall-through meet fires on both paths (the Pendant fix's
  does). Return 0 from such a site: nonzero would return from the routine.
- `cyc_mod_peek` reads ROM as the bank is mapped now, **without** read
  overrides. A feature that reads a table another feature serves through an
  override sees the stock byte.
- `cyc_mod_call_commit` runs a routine that only touches memory, at once,
  and keeps its effects.
- Mods activate **before** the cart loads. Set up in the extras' `power_on`,
  which runs after the cart loads and before any save state is restored.
- A headless run loads mods only with `--mods-root DIR`. A hidden window
  needs `NESRECOMP_NO_LAUNCHER=1 SDL_VIDEODRIVER=dummy` and `--hidden`.
- TCP (`--tcp PORT`): `pad` takes SDL controller names (`a`, `start`,
  `dpleft`). `action` takes NES names (`a`, `left`, `up`). Durations are in
  frames, so a test that waits on frames and game state, never the clock,
  runs the same uncapped.
- With `--tcp N` the window prints `[cyc tcp] listening on 127.0.0.1:N`, or
  `[cyc tcp] cannot listen on port N` and **runs on without TCP**. A client
  that connects to N then talks to whatever else holds it, another test's
  game included. `tests/e2e/fax_e2e.py` waits for its own game's line.
- TCP `action` returns at once and holds the button for its frames, so two
  `action`s sent back to back are held together: `left` then `a` jumps left
  (`tests/speaker/speaker_names.py` climbs the King's stairs that way).
- TCP `key` (an SDL key name) and `pad` reach the game as window events, as
  a real key or controller does, so the host extras' `event` sees them. The
  window keeps `config.ini` beside the executable and writes it at first
  start; its `[Game]` keys go to the host extras' `load_setting`, and
  `save_settings` writes them back whenever the window saves.
- `--uncapped` (with `--exit-after N`) runs the window as fast as it can:
  about 290 fps, 5x real time. The cycle-accurate machine is the limit; even
  headless runs at about 370 fps.
- A save state's RAM is in its `HW  ` chunk at offset 23, 2 KiB, with no
  checksum. Edit it to set up test fixtures (`fixture()` in
  `tests/e2e/fax_e2e.py`).
- A site at a **branch** sees the flags the instruction before set, and can
  steer the branch by changing them (and the register they came from) with
  `cyc_mod_set_regs`, then returning 0. Forgiving jumps does this twice, so
  the game's own code does the rest.
- Each TCP command is served on a frame boundary, so a polling loop sees the
  game a few frames apart. To time something to the frame over TCP, read
  `InterruptCounter` with the state in one `read_ram`. A save state's frame,
  which a headless run from it continues, is the `uint32` at offset 28.
- **A TCP reply takes about 3 frames at normal speed,** on a kept-open
  connection too: `cyc_tcp.c`'s `send_line` sends the line and its newline
  apart, and Nagle's algorithm holds the newline until the client's delayed
  ACK (up to 40 ms on Linux). Commands sent together on one connection are
  served at the same frame boundary, so a `read_ram` sent with others is one
  moment with them (`tests/block/block_shield.py` reads Block's glint this
  way). Read a kept-open socket buffered: a 4 KB reply a byte at a time is
  slower still.
- **Which frame a screenshot shows.** TCP `screenshot` (layer `picture`)
  between two frames shows the sprites of the shadow OAM the frame before
  wrote, which the PPU took at the last frame's start. In Seamless world it
  shows the last frame's own: `fax_world.c` captures the active room when
  the main loop's pass ends.
- TCP input lands at no particular frame. For anything that depends on the
  frame a button goes down, run the same build headless from a save state:
  `--load-state S --input FILE --frames N --frame-log LOG --mods-root DIR`.
  `--input` lines are `<frame> <BUTTONS>` (`RIGHT+A`, `-` for none), in the
  host's frame numbers, which a loaded state continues; a run with
  `--frames 0` prints the state's frame. The frame log holds every frame's
  CPU RAM (after a `CYCFRAME` and version header, per frame: 9 words, the
  palette, OAM, then RAM). `--screenshot` saves the last frame, and
  `--save-state F:PATH` saves a state after frame F.
  `tests/movement/play.py` wraps this.
- The window's TCP server reads every line it has and serves them all before
  the next frame (`cyc_tcp_poll`, then the held inputs tick, then the frame).
  So a `load_state` and an `action` sent in one write hold the button from the
  loaded state's first frame, exactly: TCP input that lands on a chosen frame,
  in the window. `batch()` in `tests/e2e/fax_e2e.py` sends them;
  `tests/movement/forgiving_jumps.py` presses on the 2nd frame off a ledge
  that way.
- Save a state after a screen has finished scrolling
  (`Screen_ScrollDirection` `$54` is `$FF`): the main loop does not update
  the hero while it scrolls, so a state saved mid-scroll ignores input for a
  second after loading.
- With the Seamless world on, the hero's routines can run in an isolated
  scope where `cyc_mod_isolated()` is true. A hook that keeps state must leave
  it alone there.

## Far calls (`MMC1_LoadBankAndJump`)

Code in the fixed bank reaches a routine in a switchable bank with
`JSR MMC1_LoadBankAndJump` (`$F859`) and three inline bytes: the bank, then
the routine's address - 1, little-endian. `MMC1_LoadBankAndJump` saves A, X and
Y, pops the JSR's return address, and pushes, in order: the address past the
inline bytes - 1 (where the caller resumes), `CurrentROMBank`, `MMC1_PopBank` - 1
(`$F8C5`) and the routine's address - 1. It switches the bank, restores A, X
and Y, and RTSes into the routine. So at the entry of a far-called routine:

| Stack | Holds |
|---|---|
| S+1, S+2 | `$F8C5`: the routine's RTS goes to `MMC1_PopBank` (`$F8C6`: `PLA`, `TAX`, `JMP MMC1_UpdateROMBank`), which maps the caller's bank back and returns to the caller |
| S+3 | The caller's bank |
| S+4, S+5 | The caller's resume address - 1 |

- The resume address names the call site exactly, which tells apart two far
  calls to the same routine (Death keeps gold does).
- A hook that handles a far-called routine's entry returns through
  `MMC1_PopBank`, as the routine's own RTS would, so skipping one is safe.

## Titles, experience and gold

| Address | Name | Notes |
|---|---|---|
| `$0390-$0391` | `Experience` | 16-bit little-endian |
| `$0437` | `PlayerTitle` | The title the hero holds, 0 (Novice) to 15 |
| `$04ED` | `NextPlayerTitle` | The title earned. Ahead of `PlayerTitle` until a Guru grants it |
| `$0427-$042A` | `DurationOintment`, `DurationGlove`, `DurationWingBoots`, `DurationHourGlass` | `$FF` when not running |

- `Player_CheckReachedNextTitle` raises `NextPlayerTitle` by one when
  `Experience` reaches `PLAYER_TITLE_EXP_NEEDED[NextPlayerTitle]`. A Guru's
  `04` action then makes `PlayerTitle` `NextPlayerTitle` and jumps to his new
  rank line.
- `Player_SetInitialExpAndGold` (`$95B1`, bank 12) sets `NextPlayerTitle` to
  `PlayerTitle`, and for title t from 1 sets `Experience` to
  `PLAYER_TITLE_EXP_NEEDED[t-1]` (`$F749`, words) and `Gold` to
  `PLAYER_TITLE_GOLD[t-1]` (`$F767`, words; the top byte 0). A Novice gets 0
  of both. The tables start 1000 experience and 500 gold for title 1, 3500 and
  1200 for title 3.
- It is far-called from two places, both with S at `$FA` on entry, since both
  callers set S to `$FF` first:
  - `Player_HandleDeath`, at `$D99A`, resuming at `$D9A0` (`JSR Screen_FadeToBlack`).
    So in the original, death also takes back a title earned but not yet granted.
  - `Game_ShowStartScreen`, after the mantra screen, at `$FC8F`, resuming at
    `$FC95` (`JMP Player_Spawn`). A mantra holds neither gold nor experience.
- `PlayerIsDead` is cleared near the start of `Player_HandleDeath`, before the
  death script, so it cannot tell the two calls apart.

**The start screen and the mantra screen.** Select toggles `$0687`
(0 Start, 1 Continue); Start takes it. On the mantra screen the cursor is
`Password_CursorRow` (`$0663`) and `Password_CursorCol` (`$0662`), and
`Password_EnteredCharsLength` (`$0666`) counts the characters typed. The last
row (5) holds →, ←, DEL and END at columns 0-3, and Up from row 0 wraps to it.
A on END (row 5, column 3) checks the mantra and continues.

## The title screen (bank 15, bank 12)

`Game_InitStateForStartScreen` (`$DA6A`) sets S to `$FF`, initializes the
inventory and runs `Game_InitStateForSpawn`, then `Game_ShowStartScreen`
(`$FC65`) far-calls `StartScreen_Draw` and loops at `@_waitForInput` (`$FC6E`)
once a frame: `WaitForNextFrame`, `Screen_ResetSpritesForNonGame`, a far call
to `StartScreen_CheckHandleInput` (`$9F44`, bank 12), then at
`@_afterCheckHandleInputFarJump` (`$FC7A`) a check of
`Joy1_ChangedButtonMask` for Start.

- `StartScreen_CheckHandleInput` toggles `$0687` on Select (with sound `$0B`)
  and draws the arrow, sprite `$AD`, at x `$34` or `$74`, y `$7E`. Nothing
  else calls it, so a hook that handles it owns the title's input: the arrow
  is not drawn (the loop resets sprites each frame), and clearing Start in
  `Joy1_ChangedButtonMask` there keeps the loop from seeing it.
- Start with `$0687` = 0: music 8, then `@_startGame` (`$FC98`):
  `SplashAnimation_RunIntro`, `Player_SetStartGameState` (`$9570`: everything
  zero or none, but `SelectedArmor` 0) and `JMP Game_Start`.
- With `$0687` = 1: `PasswordScreen_Show` (`$909D`, far call), then
  `Player_SetInitialExpAndGold` (far call from `$FC8F`), then at `$FC95`
  `JMP Player_Spawn`. `PasswordScreen_Show` returns only once
  `Password_Load` has succeeded, and leaves nothing else that
  `Player_Spawn` needs: skipping it (handling its far call) after putting the
  progress in RAM continues as a mantra would.
- The picture: the logo and its border fill y 16-143; START and CONTINUE are
  on y 160-166 and the three notices on 184, 200 and 216, so y 144-239 is
  free for a box of 12 cells. The background palettes are `$0F $05 $18 $38`
  (the logo's border), `$0F $17 $27 $30` (the white text), `$0F $0F $16 $28`
  and `$0F $0F $00 $10`; `UI_AttributeDataIndex` is 1. The playfield's
  pattern table holds the title's tiles, not the text box's, so a box drawn
  there takes the frame from ROM (`HUD_TILES_START`, bank 10; see The text
  box).
- The titles' names are 16 space-padded characters each at
  `PLAYER_TITLE_STRINGS` (`$F649`, bank 15): Novice, Aspirant, Battler,
  Fighter, Adept, Chevalier, Veteran, Warrior, Swordman, Hero, Soldier,
  Myrmidon, Champion, Superhero, Paladin, Lord.
- The dialogue font has a right-pointing triangle at `}` (`$7D`), and blank
  glyphs for `#$%&()*+/:;<=>@[\]^`.

## The mantra (bank 12)

`Password_GenerateState` (`$96FE`) packs the game into
`Password_EncodedState` (`$04CD`), a checksum byte and a 5-bit count of 6-bit
characters, then `Password_EncodeGameState` (`$976C`), padded to a byte. Each
character is one of 64 (`A-Z`, `a-z`, `0-9`, `,`, `?`, in that order). The
fields, in order:

| Field | RAM | Bits |
|---|---|---|
| Spawn point | `TempleSpawnPoint` (`$0439`) | 3 |
| Title | `PlayerTitle` (`$0437`) | 4 |
| Special items | `SpecialItems` (`$042C`) | 8 |
| Quests | `Quests` (`$042D`) | 8 |
| Selected weapon, armor, shield | `$03BD-$03BF` | 1 (set) + 2 each |
| Selected spell | `SelectedMagic` (`$03C0`) | 1 + 3 |
| Selected item | `SelectedItem` (`$03C1`) | 1 + 5 |
| Weapons, armor, shields | `$039D`, `$03A1`, `$03A5`; counts `$03C2-$03C4` | count 3, entries 2 |
| Spells | `$03A9`; count `$03C5` | count 3, entries 3 |
| Items | `$03AD`; count `$03C6` | count 4, entries 5 |

An unset selection (`$FF`) is a single 0 bit. `Password_Load` (`$95E9`)
reads the same fields back and fails (C set) if a list holds more than 4
entries (8 items); before it, `Password_Maybe_CheckPasswordLength` (`$9850`)
repacks the typed characters from `Password_EnteredChars` and fails unless the
count matches `Password_EnteredCharsLength` and the checksum sums to 0. Both
only touch memory, so a mantra decodes in an isolated call: write the
characters' values and the length, switch to bank 12 (`$CC1A`, X = 12), call
the two, then `Player_SetInitialExpAndGold`.

**Not in the mantra:** gold, experience and `NextPlayerTitle` (continuing
sets them from the title), HP and MP (`Player_Spawn` fills them), the timed
items, and where the hero stood. The mantra's spawn point is
`TempleSpawnPoint`, the furthest temple in the original.

## Weapon damage (bank 14)

The sprite loop calls `Player_HitSpriteWithWeapon` (`$8804`) for each live
sprite every frame, except while the sprite's hit counter counts down. It
lands a hit while `Player_AttackPhaseAnimFrame` is 1 or 2, on a visible enemy
in reach (`Sprites_IsSpriteOutOfWeaponRange`; see *Weapon reach* below). A
hit builds its damage in `Temp_00` (`$00`):

| Address | Code | Does |
|---|---|---|
| `$886C` | `LDY Player_CurWeapon`, `LDA WEAPON_STRENGTHS,Y`, `STA Temp_00` | The weapon's strength, from `$B7A5` |
| `$8874` | `LDA SpecialItems`, `AND #$02`, `BNE @_checkGlove` | **The Pendant Bug**: the branch skips the bonus *with* the Pendant |
| `$887B` | `LDA Temp_00`, `LSR`, `LSR`, `CLC`, `ADC Temp_00`, `STA Temp_00` | +25%, rounded down: without the Pendant |
| `$8884` | `@_checkGlove`: `LDA DurationGlove`, `BMI @_updateSpriteHP` | Both paths meet here |
| `$8889` | `LDA Temp_00`, `CLC`, `ADC WEAPON_TO_GLOVE_STRENGTH,Y`, `STA Temp_00` | The Glove's bonus, from `$88C7`, while it runs |
| `$8891` | `LDA CurrentSprites_HP,X`, `SEC`, `SBC Temp_00`, `STA`, `BCS @_return` | A borrow kills the enemy. Damage equal to its HP leaves it alive at 0 |

So a hit does the weapon's base strength, plus a quarter of it, plus the
Glove's bonus while the Glove runs:

| Weapon | Base | Stock, no Pendant | Stock, Pendant | Glove adds |
|---|---|---|---|---|
| Hand Dagger | 4 | 5 | 4 | 2 |
| Long Sword | 8 | 10 | 8 | 4 |
| Giant Blade | 12 | 15 | 12 | 8 |
| Dragon Slayer | 16 | 20 | 16 | 10 |

The designers meant the quarter for the Pendant. With the **Pendant fix**,
a hit does the base without it and the stock "no Pendant" damage with it,
and the Glove adds its bonus after either.

- From the branch to `@_checkGlove` only A, the flags and `Temp_00` change,
  and only `Temp_00` matters after: `LDA DurationGlove` reloads A and N/Z, C
  is set (`CLC`, `SEC`) before anything reads it, and nothing reads V. Y is
  still `Player_CurWeapon`, and X the sprite being hit.
- `WEAPON_STRENGTHS` is read only here. Magic damage is another routine
  (`Player_HitEnemyWithMagic`), where the Magical Rod adds 50%.

| Address | Name | Notes |
|---|---|---|
| `$00A4` | `Player_Flags` | Bit 7 set while a swing lasts. B (a new press) starts one |
| `$00AE` | `Player_AttackPhaseAnimFrame` | 0, 1, 2, and stays 2 after the swing. Test `Player_Flags` instead |
| `$00BA`, `$00C2` | `CurrentSprites_XPos_Full`, `CurrentSprites_YPos` | 8 sprite slots each, like the rows below |
| `$02CC` | `CurrentSprites_Entities` | `$FF` for an empty slot |
| `$0344` | `CurrentSprites_HP` | Set from `SPRITE_ENTITIES_HP` (`$B5A9`, by entity) when the sprite loads |
| `$034C` | `CurrentSprites_HitCounter` | 8 on a hit; no new hit until it counts down |
| `$03BD`, `$03C8` | `SelectedWeapon`, `Player_CurWeapon` | 0 Hand Dagger to 3 Dragon Slayer. `Player_CurWeapon` is `$FF` in buildings |
| `$0428` | `DurationGlove` | `$FF` when not running |
| `$042C` | `SpecialItems` | Bits: `$01` Black Onyx, `$02` Pendant, `$04` Magical Rod, `$08` Elixir, `$10` Demon's Ring, `$20` Ring of Dworf, `$40` Ring of Ruby, `$80` Ring of Elf |

Ripasheiku drops the Pendant (`SpriteBehavior_PendantDroppedFromRipasheiku`,
which removes the drop if the bit is already set).

**A kill can straddle a frame.** On a borrow, the HP is stored (wrapped, `$FF`
for 9 - 10) before the enemy becomes its death effect (`$13`, in
`Sprite_ShowNormalEnemyDeathByWeapon`). In between, the hit plays its sound
and `Player_AddExperienceFromSprite` redraws the experience through
`PPUBuffer_QueueCommandOrLength`, which waits for buffer space
(`PPUBuffer_WaitForCapacity`). That wait, or a lag frame, can put a frame
boundary between the two (a host frame ends at VBlank, wherever the CPU is),
so a read over TCP can see the wrapped HP with the entity unchanged. A test
that tells a kill from a hit should read the entity and HP in one `read_ram`,
call a wrapped HP a kill, and wait (in frames) for the entity to change
(`tests/pendant/pendant_fix.py`).

**Eolis, for tests.** A new game starts on Eolis (area 0) screen 0, outside
the gate, with the intro's trigger (`SPRITE_TRIGGER_INTRO`, `$4F`) in sprite
slot 7: walking right sets it off. The gate is a door (`EOLIS_DOOR_LOCATIONS`,
block `$9C`); Up from x=195 goes into the town, screen 1, at x=144. The
screens run 7-6-5-4-3-2-1-8 from left to right (`EOLIS_SCROLL_DATA`, bank 3;
screen 0 has no neighbours). Screens 4 and 5 each have two Zozura (`$47`,
9 HP) crawling right along the ground, and screen 8 a Monodron (`$2A`,
10 HP). Walking left from the town, the hero comes onto screen 4 at x=240.
The stock Hand Dagger passes over a Zozura (see *Weapon reach*); facing it,
the swords hit one from up to 31 pixels left of the hero (his x minus its x),
and it hurts him from 15. A Zozura that reaches a hero at the screen's right
edge knocks him onto the next screen.

## Weapon reach (bank 14)

A swing has three phases (`PLAYER_ATTACK_ANIMATION_PHASES`, bank 15): 8
frames thrusting, 3 holding, 8 withdrawing. `Player_AttackPhaseAnimFrame`
counts them 0, 1, 2, and `Maybe_Player_CalcAnimFrame` (`$B927`) maps them to
pose frames 4, 5 and 6 (`BYTE_ARRAY_PRG14__b97b`). Hits land in phases 1 and
2, so on pose frames 5 and 6.

Each frame `Player_DrawWeapon` (`$B7D6`) places the weapon and loads its
reach:

- **Where:** `PlayerPosXPlus10` (`$CE`) is the hero's x plus a per-pose
  offset from `PTR_ARRAY_PRG14__b89f` (`$B89F`), one table per weapon with
  and without a shield (`Player_ArmorLookupIndex` bit 0), by facing.
  `Maybe_Something_WeaponPosY` (`$D0`) is his y plus a per-pose offset from
  `PTR_ARRAY_PRG14__b90f` (`$B90F`), by weapon. In the hit frames:

  | Weapon | Pose 5 (phase 1) | Pose 6 (phase 2) |
  |---|---|---|
  | Hand Dagger | x+14 right, x-8 left; y+8 | x+24 right, x-16 left; y+6 |
  | Hand Dagger with a shield | x+16 right, x-8 left; y+8 | the same as without |
  | Long Sword, Giant Blade, Dragon Slayer | x+16 right, x-16 left; y+8 | x+24 right, x-24 left; y+6 |

- **How far:** `Maybe_WeaponRange_X/Y` (`$D2`, `$D3`) from
  `MAYBE_WEAPON_RANGES` (`$B897`), an (X, Y) pair per weapon, by
  `Player_CurWeapon`. Nothing else reads the table.

  | Weapon | X | Y |
  |---|---|---|
  | Hand Dagger | `$08` | `$04` (`$B898`) |
  | Long Sword, Giant Blade, Dragon Slayer | `$10` | `$08` |

  It returns before loading them when a portrait is up, when there is no
  weapon (`Player_CurWeapon` `$FF`), or when the weapon is off the screen, so
  `$D2`/`$D3` then keep the last weapon's values.

`Sprites_IsSpriteOutOfWeaponRange` (`$88CB`) compares the sprite's top-left
(`CurrentSprites_XPos_Full`, `CurrentSprites_YPos`, copied to
`$037F`/`$0380`) with that box, using the sprite's extents (EX, EY) from
`MAYBE_SPRITE_EXTENTS` (`$B407`, a byte pair per entity). In 8-bit
arithmetic, it hits when

    (weapon x + X - sprite x) mod 256 < EX + X
    (weapon y + Y - sprite y) mod 256 < EY + Y

that is, when the sprite overlaps the box from the weapon's position to X
pixels right of it and Y pixels below it, whichever way the hero faces. So
**the Y reach only stretches the box down**: its top is the weapon's y
whatever the reach. With the hero at y, the box spans y+8 to y+8+Y in pose 5
and y+6 to y+6+Y in pose 6: y+8 to y+12 for the stock dagger, y+8 to y+16 for
the swords.

- **The dagger's short swing.** A Zozura (16 by 16) on the hero's ground has
  its top at y+16: the swords touch its top row in pose 5, and the stock
  dagger's box ends 4 pixels above it. The same goes for any enemy 16 pixels
  tall by its extents whose top is that low: the Hornet, Bihoruda, Lilith,
  Yuinaru and `SPRITE_ENEMY_FIRE_GIANT`, and the 32-by-16 Rokusutahn and
  Borabohra. Enemies 24 or 32 tall standing on his ground reach up into every
  weapon's box.
- **Across,** facing left, a weapon's box reaches past the hero's left edge
  by 8 (dagger) or 16 (swords) pixels in pose 5 and 16 or 24 in pose 6;
  facing right, by 7 or 17, then 17 or 25.
- Extents seen: 16 by 32 for most enemies, 16 by 16 for the small fliers and
  the Zozura, 32 by 24 for Zoradohna and Pakukame, 32 by 32 Ripasheiku, 48 by
  32 Shadow Eura, 64 by 16 King Grieve.

With the **Taller dagger**, `$B898` is served as `$08` through a read
override, so the dagger's box is as tall as the swords' and reaches their
low enemies, from the same short distance as before.

## Shields and enemy magic (bank 14)

The shield slot holds `SelectedShield` (`$03BF`): 0 Small Shield, 1 Large
Shield, 2 Magic Shield, 3 the Battle Helmet, `$FF` none (shop and script IDs
`$40-$43`). Shields are only sold in shops (the Small at Apolune's tool shop
for 800, the Magic Shield for 7,600 or 9,800); no conversation gives one. The
Battle Helmet is picked up (below).

Each frame the sprite loop checks every live sprite against the shield, then
against the hero:

| Address | Routine | Does |
|---|---|---|
| `$877C` | `Player_CheckShieldHitByMagic` | Skips `SelectedShield` 3 and up (the helmet, none) and animation frames 4-6 (a swing). Only enemy magic (`SPRITE_CATEGORIES_BY_ENTITY` 6), against the shield's box at `Player_ShieldPositionX/Y` (`$039B`, `$039C`), which `Player_DrawShield` sets each frame |
| `$87CB` | `Player_HandleShieldHitByMagic` | `JSR Player_HandleHitByMagic`, then (`$87CE`) removes the shot and plays sound `$0C`, the shield's |
| `$87DC` | `Player_HandleHitByMagic` | Lets `SPRITE_ENTITY_DAMAGES[entity] >> (SelectedShield + 1)` through: half with the Small Shield, a quarter with the Large, an eighth with the Magic. Non-zero: `Player_SetDamagedBySprite` (60 iframes, face the shot, knockback) and `Player_ReduceHP` |
| `$8999` | `Player_HandleTouchEnemyMagic` | Magic that touches the hero: nothing while `Player_InvincibilityPhase` or Ointment runs, else `Player_HandleTouchEnemy` (sound 4, the full damage) and the shot is removed |
| `$B982` | `Player_DrawShield` | Draws nothing for the helmet or none, so `Player_ShieldPositionX/Y` keep stale values then |

- **The Battle Helmet never stops magic in the original.** It lives in the
  shield slot, but the shield check skips it and no shield is drawn. A shot
  at shield height takes its full damage.
- **The Battle Helmet has no graphic at all.** With `SelectedShield` 3 or up,
  `Player_LoadArmorTilesToPPU` loads the armor's tiles without the shield arm,
  `Player_DrawBody` picks the same no-shield poses
  (`PLAYER_BODY_TILEINFO_START_OFFSETS`), and `Player_LoadShieldTilesToPPU`
  loads nothing: with the helmet the hero looks exactly as with no shield.
- **Picking up the Battle Helmet** (`Player_PickUpBattleHelmet`, `$C70A`)
  runs IScript `$8C`, `ISCRIPT_GOT_BATTLE_HELMET` (`$A698`, bank 12: generic
  box, `SHOW_UNSKIPPABLE_MESSAGE` `$B6` "I've got the Battle Helmet.", END),
  plays sound 8, and only then adds the helmet to `ShieldInventory`. Its
  sprite (`$59`) lies in Zoradohna's room, Branch (area 5) screen 7 at x=64,
  with Zoradohna (`$2E`) in slot 6 and the helmet in slot 7. Its behavior
  (`$2C`) clears the room's sprites if the hero has the helmet already, and
  hides it while Zoradohna is on screen. Walking out of a building with the
  door's `Area_Saved*` edited to area 5, screen 7, palette `$08`
  (`PALETTE_BRANCH`), position `$9C`, region 3 arrives there; clearing slot 6
  in the save state shows the helmet.
- **Ointment.** `DurationOintment` (`$0427`) is `$FF` when none runs; while
  one runs, bit 7 is clear (the last second is `$00`).
  `Game_DecOintmentDuration` (`$C8B4`, from `GameLoop_CountdownItems`)
  counts it down when `InterruptCounter & $3F` is 0, every 64 frames.
- **Iframes and knockback.** A hit sets `Player_InvincibilityPhase` (`$00AD`)
  to `$3C` and `Player_StatusFlag` (`$00A5`) bit `$02`, knockback.
  `Player_HandleIFrames` (`$E0E8`) counts the phase down each frame and
  clears knockback when it reaches `$39`, three frames after the hit. So
  knockback needs the phase: with it at 0 the bit is cleared at once.

**How the shield is drawn.** Each pass, after
`Screen_ResetSpritesForGamePlay`, the main loop draws the shield, then the
body, then the weapon: `JSR Player_DrawShield`, `Player_DrawBody` (`$EBEE`),
`Player_DrawWeapon` (`$B7D6`), in that order everywhere the hero is drawn
(`Game_MainLoop` and its two scroll loops, `EndGame_MainLoop`,
`Game_DrawScreenInFrozenState`). `Player_DrawShield`:

1. Returns if a portrait shows (`IScript_PortraitID` positive) or the slot
   holds the helmet or nothing.
2. Takes the pose from `Maybe_Player_CalcAnimFrame` into `Temp_00` (0-3
   upright, 4-6 a swing, 7 a ladder; see *The pose*).
3. Sets the box: `Player_ShieldPositionX` = `Player_PosX_Block` +
   `SHIELD_SPRITE_OFFSETS_X[pose × 2 + facing right]` (`$B9DD`: 0 facing left
   and 8 facing right in poses 0-5, the other way round in pose 6, 0 both ways
   on a ladder), and `Player_ShieldPositionY` = `Player_PosY` + 8.
4. Copies the box to `Arg_DrawSprite_PosX/Y` (`$27`, `$28`), the flip from his
   facing, and `Sprites_PPUOffset` `$30`.
5. At `$B9C3` (`LDA Temp_00`), runs `Player_CalculateVisibility` at
   `Arg_DrawSprite_PosX/Y`, which puts the sprites behind foreground blocks.
6. Draws appearance `MAYBE_SHIELD_SPRITE_PPU_OFFSETS[pose]` (`$B9D5`: `$60`,
   or `$61` and `$62` in a swing, `$42` on a ladder, the back view) with
   `Sprite_SetPlayerAppearanceAddr`.

- **The shield's sprites** are a column of two 8×8 tiles, `$30` and `$31`, in
  sprite palette 1. Each shield's 6 tiles load to PPU `$0300` on a screen's
  setup (`Screen_SetupNew`, so walking through Eolis's gate loads a shield set
  in a save state). The Small, Large and Magic Shields differ only in length,
  5 pixels wide, seen edge on.
- **Sprite slots.** `Sprite_Draw` writes Y, tile, attributes, X at
  `Sprites_SpriteSlot` (`$25`) × 4 EOR `Sprites_StartSlotRange` (`$1C`) in
  the shadow OAM (`$0700`), then `Sprites_IncNextSpriteSlot` alternates
  halves: 1, 33, 2, 34, ... (slot 0 is the status bar's). The hero's parts
  each take a run of slots, so the body's are those between `Sprites_SpriteSlot`
  at `Player_DrawBody` and at `Player_DrawWeapon`. OAM Y is the drawing Y
  + 32, the HUD.
- **Sprite palettes.** Palette 0 is the hero's (`$18 $26 $30`), palette 1 the
  shield's (`$00 $25 $30`: grey, pink, white). All three sprite palette sets
  (bank 11, `$81C0` + 16 × n, loaded by `Screen_LoadSpritePalette`) have these
  two; palettes 2 and 3 are the area's actors'.
- **Moving the shield's picture** without moving what it stops: change
  `Arg_DrawSprite_PosX/Y` at `$B9C3`, where the box is already set. Block
  once drew the shield 6 pixels up and 2 out there while it was held (#53);
  it looked wrong, and the glint replaced it (#58).
- **Where the shield and the body overlap,** which is in front alternates
  each frame: `Sprites_StartSlotRange` flips which half of the shadow OAM
  the parts' slots land in, and the lower OAM index is drawn in front. A
  picture of the hero standing still matches only one from a frame of the
  same parity, at those pixels.

**Zenis, for tests.** On the fortress of Zenis's screen 16 (area 7, palette
`$0F`, region 5, position `$91`), an Execution Hood casts a shot (`$51`,
8 HP) at the hero from the right. Editing the shot's Y in a save state aims
it at the shield (150, the hero on the ledge at 144) or his head (136).

## Sugata's flash (bank 14)

Sugata (entity `$18`, 22 HP, 4 HP contact damage) lives only in the Mist
(area 2), on screens 30, 31, 78 and 79 (`SPRITE_INFO_FOREPAW`, bank 11: the
sprite tables are named by town, the Mist's by Forepaw). Screen 30 is reached
from a building door edited to area 2, screen 30, palette `$0A`
(`PALETTE_MIST`), position `$92`, region 2; Sugata is in slot 6.

`BSCRIPTS_ENEMY_SUGATA` (`$AF98`): face the hero, walk toward him 30 ticks,
wait 30, walk 40, wait 10, creep 20, then behavior `$1F`,
`SpriteBehavior_FlashScreenHitPlayer` (`$AB26`), and start over. It is the
only user of `$1F`:

| Address | Code | Does |
|---|---|---|
| `$AB26` | `JSR Sprite_IsBehaviorNotReady`, `BNE $AB39` | The first call goes on; later ones skip to the countdown |
| `$AB2B` | `LDA #$02`, `STA CurrentSprites_BehaviorData2,X` | Two calls |
| `$AB30` | `LDA PPU_Mask`, `ORA #$01`, `STA PPU_Mask` | Greyscale, which the NMI writes to `$2001` |
| `$AB36` | `JSR Sprite_SetBehaviorReady` | |
| `$AB39` | `DEC CurrentSprites_BehaviorData2,X`, `BNE` return | |
| `$AB3E` | `LDA PPU_Mask`, `AND #$FE`, `STA PPU_Mask` | Greyscale off: it showed for about two frames |
| `$AB44` | `LDA #$04`, `JSR Sound_PlayEffect`; `$3C` to `Player_InvincibilityPhase`; knockback; `$0A00` to `Arg_PlayerHealthDelta`; `JSR Player_ReduceHP` | 10 HP, wherever the hero is |
| `$AB60` | `LDX CurrentSpriteIndex`, `JMP Sprite_FinishBehavior` | |

- It checks no position, hitbox, shield, jump, iframes or Ointment: every
  other hit checks the last two.
- Killing Sugata clears the greyscale bit (in `Player_HitEnemyWithMagic`'s
  `@_reduceEnemyHP`, `$81FD`, and the sword's kill), so a flash cut short
  cannot leave the screen grey.
- `@_reduceEnemyHP` (`$81FD`) takes `Temp_00` from `CurrentSprites_HP,X`; a
  borrow kills (sound 3, experience, the death effect, `$14`). Damage equal
  to the HP leaves the enemy alive at 0. It can be entered alone with X the
  sprite, as a spell's hit would after its sound (2) and defense maths.
- `SpriteUpdateHandler_Enemy_Sugata` (`$929F`) draws Sugata; returning
  through `SpriteUpdateHandler_Invisible` (`$8C98`) hides it that frame. The
  presented picture lags the sprites by a frame or two.

## What killed the hero (bank 14, bank 15)

**Every loss of HP goes through `Player_ReduceHP` (`$C08E`, fixed bank)**,
which subtracts `Arg_PlayerHealthDelta` (`$04BC` fraction, `$04BD` whole) from
`Player_HP` (`$0432` fraction, `$0431` whole) as one 16-bit number. A borrow
kills: it zeroes the HP and sets `PlayerIsDead`, unless `SpecialItems`
(`$042C`) bit `$08`, the Elixir, is set, when it jumps to `Player_UseElixir`
instead. HP equal to the damage leaves him alive at 0. Nothing else lowers
HP (the other writes to `$0431` are the spawn's 80, `Game_LoadFirstLevel`,
healing and the HUD's cap). It has exactly four callers, found by scanning the
ROM for `JSR`/`JMP $C08E`:

| Caller | Call | Damage | The hit's source |
|---|---|---|---|
| `Player_ApplyDamage` (`$8A79`, bank 14) | `JMP` at `$8AD4` | `SPRITE_ENTITY_DAMAGES[entity]` less armor | The sprite at `CurrentSpriteIndex` (`$0378`): an enemy's, a boss's or a shot's touch, through `Player_HandleTouchEnemy` (`$89AE`) |
| `Player_HandleHitByMagic` (`$87DC`, bank 14) | `JMP` at `$87FD` | A shot's damage, shifted down by the shield | The shot at `CurrentSpriteIndex`, on the shield |
| `SpriteBehavior_FlashScreenHitPlayer` (bank 14) | `JSR` at `$AB5D`, resumes at `$AB60` | 10 | Sugata's flash |
| `Player_PickUpPoison` (`$C83C`, fixed bank) | `JSR` at `$C85D`, resumes at `$C860` | 16 | The Poison, after its "I've touched Poison." (message `$BB`, IScript `$91`) |

So at `Player_ReduceHP`'s entry the word at S + 1 tells the Poison and the
flash apart (the two `JMP` callers leave their caller's return there), and
`CurrentSprites_Entities[CurrentSpriteIndex]` (`$02CC` + slot, 8 slots, `$FF`
empty) is everything else. `CurrentSprite_CheckHitPlayer` sends a touch to a
handler by `SPRITE_CATEGORIES_BY_ENTITY` (`$B544`): 0 enemy and 7 boss to
`Player_HandleTouchEnemy`, 6 magic to `Player_HandleTouchEnemyMagic`; the
others (bread, NPCs, effects, triggers, items) do no harm.

**Shots and rocks are sprites spawned by other sprites, and every such spawn
goes through `Sprite_Maybe_ResetState` (`$A202`, bank 14)** with X the
spawner's slot and Y the new one, its entity already stored. All seven of its
callers store a new entity at `CurrentSprites_Entities,Y` and `JMP` there:

| Spawned | Damage | By | Code |
|---|---|---|---|
| `$03` (a falling rock, chipx86's "Enemy: ?") | 31 | Zorugeriru (`$31`) | `SpriteBehavior_Zorugeriru__9e13` |
| `$09` Lilith | 6 | Pakukame (`$30`) | `SpriteBehavior_Pakukame` |
| `$0A` (fire) | 28 | Ripasheiku (`$2D`), King Grieve (`$32`) | `Sprite_CastMagic` (`$A0F6`), from both behaviors |
| `$51` (a shot) | 8 | Nash (`$0D`), Ishiisu (`$0F`), Execution Hood (`$10`) | `FUN_PRG14__a0a0`, which falls into `SpriteBehavior_ExecutionHood__a0ad` |
| `$53` (fire) | 28 | Shadow Eura (`$33`) | `SpriteBehavior_ShadowEura__a12d` |
| `$54` (a fireball) | 8 | Charron (`$15`) | `BScript_Action_CastMagic` (`$9239`), behavior-script action 7. `FUN_PRG14__a1cc` also makes one and has no caller |

- **chipx86's comments in two tables are off by one.**
  `SPRITE_CATEGORIES_BY_ENTITY`'s skip Zorugeriru, so from `$31` on each
  comment names the entity before (its values are right: `$31`-`$33` are
  bosses, `$46` and `$47` enemies). `SPRITE_BSCRIPTS`' put Charron's script at
  `$14` (the lightning ball): entity `$15`, Charron, runs
  `BSCRIPTS_ENEMY_UNUSED_21`, the script that casts. The `SPRITE_*` constants
  in `DEFS.asm` and `SPRITE_UPDATE_HANDLERS`' comments are right.
- **The death plays out over 256 frames.** `Player_HandleDeath` (`$D8EC`)
  plays sound `$16` and loops once a frame until `Screen_TransitionCounter`
  (`$03DB`) wraps, dissolving the hero (`Player_DrawDeathAnimation`), then
  runs the Guru's words. Nothing reads the pad in between.
- **The NES manual names no enemy.** The USA instruction booklet (27 pages,
  archive.org's `NESManuals` scan) names places, items and titles: of what
  hurts, only the Poison ("The hero's power will decrease by a fixed
  amount"). The names below are chipx86's `SPRITE_*` constants, written as a
  player would say one of them; StrategyWiki's enemy list spells them the
  same, but for "Mask Man" and "Wolf Man".

**The names Slain by uses** (`fax_slain.c`), for every entity that can hurt
the hero. The unused enemies (`$12`, the snake boss's round body, `$16`,
`$1D`, `$24`, `$25`, `$27`, `$29`, `$2B`, `$46`) appear on no screen and are
spawned by nothing, so they have none.

| Entity | chipx86 | Shown | Where, for a test |
|---|---|---|---|
| `$04` | `SPRITE_ENEMY_RAIDEN` | a Raiden | |
| `$05` | `SPRITE_ENEMY_NECRON_AIDES` | a Necron Aide (one of them) | |
| `$06` | `SPRITE_ENEMY_ZOMBIE` | a Zombie | Trunk `$06` |
| `$07` | `SPRITE_ENEMY_HORNET` | a Hornet | |
| `$08` | `SPRITE_ENEMY_BIHORUDA` | a Bihoruda | |
| `$09` | `SPRITE_ENEMY_LILITH` | a Lilith | |
| `$0B` | `SPRITE_ENEMY_YUINARU` | a Yuinaru | |
| `$0C` | `SPRITE_ENEMY_SNOWMAN` | a Snowman | |
| `$0D` | `SPRITE_ENEMY_NASH` | a Nash | Mist `$2A`, its only screen |
| `$0E` | `SPRITE_ENEMY_FIRE_GIANT` | a Fire Giant | |
| `$0F` | `SPRITE_ENEMY_ISHIISU` | an Ishiisu | |
| `$10` | `SPRITE_ENEMY_EXECUTION_HOOD` | an Execution Hood | Zenis `$10`, its only screen (see *Shields and enemy magic*) |
| `$11` | `SPRITE_BOSS_ROKUSUTAHN` | a Rokusutahn | |
| `$15` | `SPRITE_ENEMY_CHARRON` | a Charron | |
| `$17` | `SPRITE_ENEMY_GERIBUTA` | a Geributa | |
| `$18` | `SPRITE_ENEMY_SUGATA` | a Sugata (its touch or its flash) | Mist `$1E` (see *Sugata's flash*) |
| `$19` | `SPRITE_ENEMY_GRIMLOCK` | a Grimlock | |
| `$1A` | `SPRITE_ENEMY_GIANT_BEES` | a Giant Bee (one of them) | |
| `$1B` | `SPRITE_ENEMY_MYCONID` | a Myconid | |
| `$1C` | `SPRITE_ENEMY_NAGA` | a Naga | |
| `$1E` | `SPRITE_ENEMY_GIANT_STRIDER` | a Giant Strider | |
| `$1F` | `SPRITE_ENEMY_SIR_GAWAINE` | a Sir Gawaine | |
| `$20` | `SPRITE_ENEMY_MASKMAN` | a Maskman | |
| `$21` | `SPRITE_ENEMY_WOLFMAN` | a Wolfman | |
| `$22` | `SPRITE_ENEMY_YAREEKA` | a Yareeka | |
| `$23` | `SPRITE_ENEMY_MAGMAN` | a Magman | |
| `$26` | `SPRITE_ENEMY_IKEDA` | an Ikeda | |
| `$28` | `SPRITE_ENEMY_LAMPREY` | a Lamprey | |
| `$2A` | `SPRITE_ENEMY_MONODRON` | a Monodron | Trunk `$00`, where the Trunk's door from Eolis arrives |
| `$2C` | `SPRITE_ENEMY_TAMAZUTSU` | a Tamazutsu | |
| `$2D` | `SPRITE_BOSS_RIPASHEIKU` | a Ripasheiku | Trunk `$15`, the Tower of Trunk's top: a building's door edited to area 1, screen `$15`, palette `$07`, position `$91`, region 1 arrives in its room |
| `$2E` | `SPRITE_BOSS_ZORADOHNA` | a Zoradohna | |
| `$2F` | `SPRITE_BOSS_BORABOHRA` | a Borabohra | |
| `$30` | `SPRITE_BOSS_PAKUKAME` | a Pakukame | |
| `$31` | `SPRITE_BOSS_ZORUGERIRU` | a Zorugeriru (and its rocks) | Trunk `$3B` |
| `$32` | `SPRITE_BOSS_KING_GRIEVE` | King Grieve (his fire too) | Dartmoor `$15`, `$16` |
| `$33` | `SPRITE_BOSS_SHADOW_EURA` | Shadow Eura (its fire too) | Zenis `$00` |
| `$47` | `SPRITE_ENEMY_ZOZURA` | a Zozura | Zenis `$10`, Trunk `$02` |
| `$03`, `$0A`, `$51`, `$53`, `$54` | rocks and shots | whoever spawned it, from the table above | |
| | `Player_PickUpPoison` | Poison | message `$BB`, "I've touched Poison." |

"a" or "an" goes by the name's first sound; King Grieve and Shadow Eura, the
two with a name of their own, take none. The longest line, "Slain by an
Execution Hood", is 26 characters: 30 tiles with the banner's blanks and
frame, inside the screen's 32.

## The hero's movement (bank 15)

`GameLoop_UpdatePlayer` (`$E0CA`) runs once a frame, unless the screen is
scrolling: `Player_CheckHandleAttack`, `Player_CheckHandleVertMovement`
(`$E2C8`), `Player_CheckHandleJump` (`$E174`, which despite its name is the
sideways movement and knockback), `Player_HandleIFrames`, then doors, the
breakable block, the Mascon block and screen edges. The main loop then draws
the shield, the body and the weapon, and only later updates the other actors,
which is when the weapon hits and magic meets the shield.

| Address | Name | Notes |
|---|---|---|
| `$0016` | `Joy1_ButtonMask` | Buttons held. `$0019` has those newly pressed |
| `$0054` | `Screen_ScrollDirection` | `$FF` when the screen is not scrolling |
| `$009D`, `$009E` | `Player_PosX_Frac`, `Player_PosX_Block` | 8.8 fixed point |
| `$00A0` | `Player_ClimbFlySpeedModifier` | The fraction for climbing and Wing Boots flight |
| `$00A1` | `Player_PosY` | His top: he is 32 pixels tall, so standing on block row 12 he is at 160 |
| `$00A3` | `Player_MovementTick` | Advances only while walking on the ground or climbing; picks the walk frame |
| `$00A4` | `Player_Flags` | Below |
| `$00A5` | `Player_StatusFlag` | `$01` attack held, `$02` knockback, `$80` Wing Boots running |
| `$00A6` | `Player_JumpCurveIndex` | 0-31 through a jump; 32 after one |
| `$00A9`, `$00AA` | `Player_MoveAcceleration` | Walking speed, 8.8: `$C0` from a standstill, up to `$180` |
| `$00AD` | `Player_InvincibilityPhase` | `$3C` on a hit, down to 0. Knockback ends at `$39` |
| `$0700-$07FF` | Shadow OAM | DMA'd each frame. The game rotates sprite order from frame to frame |

**`Player_Flags`** (`$A4`), as the code uses it, not quite as the labels say:

| Bit | Name | Meaning |
|---|---|---|
| `$01` | `PLAYER_JUMPING` | In a jump's curve. Set when one starts; cleared on landing, at the curve's end, by climbing, and by knockback |
| `$02` | holding to climb | Set with a jump, cleared on a frame on the ground without a new A press. Nothing we found reads it |
| `$04` | `PLAYER_FALLING_OFF` | **In the air without jumping**: walking off a ledge, stepping off a ladder, knocked off one, or the rest of a jump that outlasts its curve. Not only ladders, as its name suggests |
| `$08` | can climb | A climbable block at his head or feet (`Player_CheckIfOnLadder` sets it every frame) |
| `$10` | climbing | Up or Down pressed in front of a ladder; cleared away from one |
| `$20` | moving | Walking, or carried sideways in the air |
| `$40` | facing right | |
| `$80` | attacking | Through the swing's 8 + 3 + 8 frames |

**On the ground** means `Player_CheckIfPassable` (`$E6C8`) finds the way down
blocked: the block under his feet, at `Y + $20`, from `X + 4`, and the next
block too when `X & 15` is 4-11, so his feet are 8 pixels wide. (The call in
`Player_HandleVertLadderOrJump` loads the direction into A instead of X, as
chipx86 notes, so it checks whatever X holds; in practice that is down.)

**Each frame without a jump**, `Player_HandleVertLadderOrJump` (`$E399`)
asks that at `$E3AD`. With ground below (`$E3B2` branches on the answer),
`Player_GrabClimbLadderOrJump` clears `PLAYER_FALLING_OFF` and runs
`Player_UpdateClimbOrJump`. In the air, `@_setFalling` (`$E3D1`) sets
`PLAYER_FALLING_OFF`; in front of a ladder he is caught (the same grab path),
and otherwise he drops **8 pixels, every frame, from the first**: no gravity,
no speed-up (1 pixel with the Wing Boots and A held). `@_checkCapFall`
(`$E423`) then rounds his Y onto the block if he has landed. The flag stays
set on that landing frame and is cleared the next.

**A jump** starts only in `Player_UpdateClimbOrJump` (`$E444`), reached on
the ground or in front of a ladder: A newly pressed (`LDX
Joy1_ChangedButtonMask`, `BPL` at `$E44B`), not climbing, and the block above
his head passable. It sets `$03` and runs `PLAYER_JUMP_CURVE` (`$E4D6`), one
step a frame: up 8, 4, 4, 4, 4, 2, 2, 1, 1, 1, 1 (32 pixels in 11 frames),
level for 10, and back down the same way, 32 frames in all, the first step on
the frame A goes down. A block overhead stops the rise and skips to the
curve's middle. He lands when the block below is solid during the falling
half (`@_setLandedOnBlock` clears `PLAYER_JUMPING` that frame). A jump that
runs out of curve in the air carries on as an ordinary fall.

So A on the last frame on a ledge jumps, and A one frame later, in the air,
does nothing. A on the frame he lands is lost too: the jump check next runs
the frame after.

**Sideways** (`Player_CheckHandleJump`): on the ground, or with the Wing
Boots, the D-pad walks him (and a swing on the ground stops him). In the air
(`Player_Flags & $05`) there is no control: if moving, he keeps going the
way he faces, still speeding up, and otherwise stops. So from the last frame
on a ledge, his x is the same frame by frame whether he jumped or fell.

**Knockback** (`Player_StatusFlag` `$02`) lasts 3 frames, from a hit's `$3C`
to `$39`. `Player_HandleKnockback` sets `Player_Flags` to just his facing bit,
flipped, which drops jumping, falling and the rest, and pushes him 8 pixels a
frame the other way.

**Ladders.** On one he hangs: in the air, the grab path catches him, so
there is no fall. Up and Down climb, at `$A0`/256 and `$C0`/256 pixels a
frame. `Player_IsClimbing` (`$ECF6`) is can climb, climbing, and either lined
up with the ladder or not falling. Stepping off one sideways is a fall like
any other.

**In the air** is `Player_Flags & $05` to the rest of the game too:
`GameLoop_CheckUseCurrentItem` refuses items then.

**Doors.** Up at Apolune's temple door in mid-jump does nothing; on the
ground it goes in. Jumping out of the temple's right edge carries the jump
into the town, where he lands on his first update.

**The pose.** `Player_GetBodySpriteFrameOffset` (`$ECAC`, for the body) and
`Maybe_Player_CalcAnimFrame` (`$B927`, bank 14, for the weapon, the shield and
`Player_CheckShieldHitByMagic`) apply the same rule: jumping, frame 3 (or the
swing's 4-6); climbing, 7; swinging, 4-6; the Wing Boots with A held, 3;
moving, the walk cycle 0, 1, 2, 1 from `Player_MovementTick`; otherwise 0.
Only a jump or Wing Boots flight shows frame 3, so a fall looked like walking
on air, frozen mid-step, since the walk tick stops in the air. The shield sits
in the same place in frames 0-3, and the weapon only hits while swinging, so
the pose in the air is the picture alone. The jump pose is 8 sprites to the
walk's 7, which changes the sprite rotation the next frame.

## Input and casting

| Address | Name | Notes |
|---|---|---|
| `$0016` | `Joy1_ButtonMask` | Buttons held. The bits of `Joy1_ChangedButtonMask`, plus Down=`$04`, Left=`$02`, Right=`$01` |
| `$0018` | `Joy1_PrevButtonMask` | The held mask at the read before. While the screen scrolls, `Game_UpdatePlayerOnScroll` copies it back into `Joy1_ButtonMask` |
| `$00A4` | `Player_Flags` | `$80` attacking, `$40` facing right, `$20` moving, `$10` climbing, `$08` a ladder to climb, `$04` falling (in the air without jumping; see The hero's movement), `$02` holding to climb, `$01` jumping |
| `$02B3` | `CastMagic_Type` | The spell on screen, `$FF` for none. Each spell's handler takes it back to `$FF` when it hits or leaves |
| `$039A` | `Player_MP` | At most 80 |
| `$03A9-$03AC` | `MagicInventory` | Spells owned. `NumberOfMagicSpells` (`$03C5`) counts them |
| `$03C0` | `SelectedMagic` | 0 Deluge, 1 Thunder, 2 Fire, 3 Death, 4 Tilte; `$FF` none |

- **The pad is read once a frame, in the NMI.** `Input_HandleOnInterrupt`
  copies `Joy1_ButtonMask` to `Joy1_PrevButtonMask`, reads the pad into
  `Joy1_ButtonMask`, and sets `Joy1_ChangedButtonMask` to the buttons held now
  and not before. Each pass of the main loop starts with `WaitForNextFrame`,
  which waits for a fresh NMI, so a newly pressed button is seen by one pass.
  On a lag frame, an NMI the loop did not wait for
  (`Game_InterruptsHandledLatch` still set) skips the read altogether: the
  masks keep their values, and a button pressed and let go within lag frames
  is never seen. A cycle-host frame ends at the start of VBlank (scanline
  241, dot 1), so each frame begins with that read, when there is one.
- **Who reads the masks, in `Game_MainLoop`'s order:**
  - `GameLoop_UpdatePlayer` (`$E0CA`): `Player_CheckHandleAttack` swings the
    sword on a new B, unless climbing. `Player_CheckHandleEnterDoor` goes
    through a door on a new Up.
  - `Player_CastMagic` (`$BA5B`, bank 14, `JSR` at `$DB5F`): see below.
  - `GameLoop_CheckUseCurrentItem` (`$C478`, `JSR` at `$DB62`): Down held and
    a new B use the selected item, unless jumping or falling.
  - `Sprites_UpdateAll`: touching an NPC, `Player_CheckHandlePressUpOnNPC`
    runs its script when the changed mask is exactly Up (`$08`).
  - `GameLoop_CheckShowPlayerMenu` (Select) and `GameLoop_CheckPauseGame`
    (Start).
- **So one Up from a test can go unseen.** It counts on one pass only, and
  `GameLoop_UpdatePlayer` skips the door check while the screen scrolls
  (`Screen_ScrollDirection` below 4) and finds no door off its block, where an
  enemy's knockback can leave the hero. Press until the door's line or the
  speaker's portrait shows (`tests/zones/zone_names.py`,
  `tests/speaker/speaker_names.py`).
- **Casting.** `Player_CastMagic` runs the spell's handler instead while one
  is on screen (`CastMagic_Type` positive), and does nothing while climbing
  (`Player_IsClimbing`: `Player_Flags` `$08` and `$10`). Otherwise it casts
  when Up is held and B newly pressed: `SelectedMagic` below 5, and
  `Player_ReduceMP` (fixed bank) takes `MAGIC_COSTS` (`$B7A9`, bank 14: 2, 4,
  6, 10, 16) or refuses. The spell starts from the hero and flies the way he
  faces.
- **Up+B is clumsy by design.** The sword swings on the same new B, so the
  stock combo always swings and casts. At a door the new Up goes through it
  first, in `GameLoop_UpdatePlayer`, and `Game_SetupEnterScreen` waits for the
  next frame, whose read no longer has B new: Up+B at a door enters it and
  casts nothing. Next to an NPC, a new Up alone talks.
- `EndGame_MainLoop` calls `Player_CastMagic` too (`JSR` at `$DC0B`), with the
  buttons `EndGame_MovePlayerTowardKing` plays.
- On Eolis screen 0 (see *Eolis, for tests*), the intro's trigger stops the
  hero at x=53 for a long monologue that A pages through. Up goes through the
  gate from anywhere in x=188-199.

## Areas, screens and zones

`Area_CurrentArea` (`$24`) is one of eight areas, each a set of screens
(`Area_CurrentScreen`, `$63`) laid out in bank 3. **Towers and castles are not
areas of their own**: they are screens of their region's area, through doors
that change the palette. Only shops, houses, temples and the King's room are
`AREA_BUILDINGS`.

| Area | Screens | Holds |
|---|---|---|
| 0 `AREA_EOLIS` | 9 | Eolis. Screen 0 is outside the gate, where the intro plays |
| 1 `AREA_TRUNK` | 64 | The Trunk, the Tower of Trunk, the Tower of Fortress, the Joker spring |
| 2 `AREA_MIST` | 83 | The Mist, the Tower of Suffer, the Tower of Mist, two unnamed towers |
| 3 `AREA_TOWNS` | 14 | The towns from Apolune to Dartmoor, two screens each |
| 4 `AREA_BUILDINGS` | 10 layouts | Shops, houses, temples and the King's room |
| 5 `AREA_BRANCH` | 40 | The Branch |
| 6 `AREA_DARTMOOR` | 32 | Dartmoor, the Castle of Fraternal, King Grieve's room |
| 7 `AREA_ZENIS` | 20 | The fortress of Zenis |

Bank 3's `AREA_TABLES` points, for each area, at:

- `<AREA>_SCROLL_DATA`: 4 bytes a screen, the screens to the left, right,
  above and below (`$FF`: none). Its length / 4 is the area's screen count.
  The links are not always both ways.
- `<AREA>_DOOR_LOCATIONS`: 4 bytes a door to a `$FF`: the screen, the block
  (`YX`), a door index and the start position (`YX`) on the other side. An
  index under `$20` is a door to another screen of the same area, `$20` and
  up a building, and `$FE` / `$FF` a door to another region.
- `<AREA>_DOOR_DESTINATIONS`: 4 bytes a door index (index - `$20` in
  `AREA_TOWNS`): the screen, the palette and the key it takes. For a building
  the screen is the building's number and the palette its kind
  (`BUILDING_PALETTES`: 0 King's room, 1 temple, 2 hospital, ... 9 magic shop).

Other ways between screens and areas, in bank 15:

- Off a region's edge into a town and back: `AREA_TOWN_TRANSITIONS_DATA`, 5
  bytes each (screen, area, screen, start, palette).
- Doors between regions (`$FE` back, `$FF` on) look up
  `Area_Region` (`$0435`, `REGION_*`) × 2 + (index & 1) in
  `MAYBE_REGION_TRANSITION_MAP`, `MAYBE_SCREEN_TRANSITION_MAP` and
  `DOOR_LOOKUP_REQUIREMENTS`. `DOOR_OUTSIDE_REGION_INDEXES` turns the region
  into an area.
- `AREA_SCREEN_COMPARATORS`: the climb between Trunk screens `$0C` and `$16`.
- Inside a building, `Area_CurrentArea` is 4 and `Area_CurrentScreen` the
  building's number (the temples' are in "Temples and spawning").
  `Game_EnterBuilding` keeps where the hero stood in `Area_SavedArea`
  (`$03D5`), `Area_SavedScreen` (`$03D6`), `Screen_SavedPalette` (`$03D7`)
  and `Player_SavedPosXY` (`$03D8`, block `YX`), and walking out loads exactly
  those (`Game_ExitBuilding`). A respawn or a Guru travel journey sets them to
  the temple's town. Edited in a save state, they put the hero anywhere
  (`tests/zones/zone_names.py`).
- The door out of Eolis (screen 8) takes the "J" key in hand
  (`SelectedItem`, `$03C1`, = 7): "There is a mark of "Jack" by the key hole."
- A screen has finished loading and is not scrolling when
  `Screen_ReadyState` (`$0E`) is 0, `Game_InterruptHandlersEnabled` (`$13`)
  is set and `Screen_ScrollDirection` (`$54`) is `$FF`. On the title screen
  `$54` is 0. `Game_PausedState` (`$0120`) is 1 while Start pauses.

Who and what is on each screen is in bank 11, `AREA_SPRITE_ADDRESSES`: for
each screen, (entity, `YX`) pairs to a `$FF`, then the NPCs' interaction
script numbers to a `$FF` (or `SCREEN_HAS_SPECIAL_EVENT` and an event).
chipx86's labels for these lists are one area off (`SPRITE_INFO_APOLUNE` is
area 1's); the table is indexed by area.

### Zones (`fax_zone.c`)

Zone names and zone respawn share this table. The names are the dialogue's
(message numbers in bank 13); places it never names stay part of the region
around them.

| Area | Screens | Zone | Named in |
|---|---|---|---|
| 0 | `$01-$08` | Eolis (town) | `$2A` "the Elf town of Eolis" |
| 1 | `$00-$0C`, `$16-$28`, `$3E-$3F` | Trunk (region) | `$59` "the spring of Trunk" |
| 1 | `$0D-$15` | Tower of Trunk | `$3D` |
| 1 | `$29-$3D` | Tower of Fortress | `$48`, `$50` |
| 2 | `$00-$2F`, `$4D-$52` | Mist (region) | `$6D`, `$7D` ("Tower of Mist") |
| 2 | `$30-$3E` | Tower of Suffer | `$66`, `$67` |
| 2 | `$3F-$4C` | Tower of Mist | `$6D`, `$7D` |
| 3 | `$00-$01` | Apolune | `$3B` |
| 3 | `$02-$03` | Forepaw | `$4A` |
| 3 | `$04-$05` | Mascon | `$60` |
| 3 | `$06-$07` | Victim | `$70` |
| 3 | `$08-$09` | Conflate | `$80` |
| 3 | `$0A-$0B` | Daybreak | `$8D`, which spells it "Daydreak" |
| 3 | `$0C-$0D` | Dartmoor (town) | `$99` "the town of Dartmoor" |
| 5 | `$00-$27` | Branch (region) | `$81` "the World of Branch" |
| 6 | `$00-$0F` | Dartmoor (region) | `$8C` "the door to Dartmoor" |
| 6 | `$10-$1F` | Castle of Fraternal | `$99`, `$9D` |
| 7 | `$00-$13` | Fortress of Zenis | `$9A` "the fortress of Zenis" |

No zone: area 4 (buildings) and Eolis's screen 0. How each place was told
apart:

- The doors and edges above split each area into places, and where the
  dialogue puts things says which is which. Apolune's "turn right and go
  straight" reaches the door on Trunk `$0B`, and the Mattock is on `$15`.
  Forepaw's "spring in the tower of Fortress" and its "dried up fountain"
  are on Trunk `$3D` and `$28`. The Wing Boots ("Look for them in the Tower
  of Suffer") are on Mist `$32`, and the tower's door is on screen 0, above
  the Guru on screen 6. The meteorite, the Black Onyx Victim's house wants
  back, is on Mist `$4C`.
- Temple 3, Guru travel's "Mascon", is in the Mist on screen 6, outside the
  town: its door opens on the Mist.
- The Joker spring (Trunk `$3E-$3F`, behind the Joker key's door on `$1E`)
  holds the third spring, which the game calls only "the first spring";
  chipx86 names it `QUEST_SPRING_OF_JOKER`.
- Mist `$4D-$4F` (door on screen 1, Q key) and `$50-$52` (door on `$20`,
  K key) are towers the game never names.
- The Branch's doors only switch to `PALETTE_LEFT_TRUNK`, the darker side of
  the tree. "The tower of Pillar" (`$98`) is somewhere in it; nothing places
  it.
- Dartmoor's screens look like castle walls, and "The castle of Dartmoor is a
  complex maze" (`$96`) may mean them, but the region takes the name of "the
  door to Dartmoor" the Ring of Dworf opens. The town shares the name, so
  walking between them shows it once.
- King Grieve's room (Dartmoor `$15-$16`) is inside the Castle of Fraternal.
- Mist `$0A`, `$0B`, `$23`, `$24` and Dartmoor `$0F` are linked to nothing.
- chipx86 calls area 7 `REGION_EVIL_FORTRESS`; the dialogue says "the
  fortress of Zenis" and "the Evil One in the fortress".
- **No scroll link crosses a zone.** Every left, right, up and down link in
  the scroll data stays inside its zone, so a zone is only ever entered
  through a door, a region's door, a town's edge or a building's way out, all
  of which load the screen. Decoded from bank 3: `AREAS_TABLE_PTR` (`$8000`)
  holds the offset of `AREA_TABLES` from `$8000`; that holds, per area, the
  offset of `<AREA>_AREA_DATA`, five more offsets: block attributes, block
  properties, scroll data, door locations, door destinations
  (`Game_LoadAreaTable` adds `$80` to each high byte).

## Entrances and zone respawn

**Every screen load passes through `Screen_SetupNew` (`$DD13`)**: from
`Screen_Load` (a door within the area through `Game_SetupEnterScreen`, the
Trunk's climb, a region's door through `Game_LoadCurrentArea`, a town's edge
through `Game_EnterAreaHandler`, a building's way out through
`Game_ExitBuilding`, the first level) and from `Game_EnterBuilding`. Seamless's
room handoffs and a scroll to the next screen do not load one. At its entry,
the way in is complete in RAM:

| Address | Name | Holds |
|---|---|---|
| `$0024` | `Area_CurrentArea` | The area, already the new one |
| `$0064` | `Area_LoadingScreenIndex` | The screen it makes `Area_CurrentScreen` |
| `$0065` | `Screen_DestPaletteOrIndex` | The palette it makes `Screen_PaletteIndex` (`$03D0`) |
| `$006C` | `Screen_StartPosYX` | Where it puts the hero: Y in the high nibble, X in the low, in blocks |
| `$0435` | `Area_Region` | `REGION_*`: which region's doors (`DOOR_LOOKUP_REQUIREMENTS` and the rest) apply. A town's edge leaves it alone |
| `$03D1` | `Areas_DefaultMusic` | The music, already set: by `Game_LoadCurrentArea` or `Game_EnterAreaHandler` from `AREA_TO_MUSIC_TABLE`, by `Game_BeginExitBuilding` from `Area_Music_Outside`, or for a door within the area from the destination palette (below) |
| `$00A4` | `Player_Flags` `$40` | His facing. A door keeps it; a region's door faces him right (`Player_SetInitialState`), and so does a building's way out |

A door within the area changes the music only when the destination palette is
one of seven (`PALETTE_OUTSIDE`, `_TOWER`, `_MIST`, `_SUFFER`, `_DARTMOOR`,
`_FRATERNAL`, `_KING_GRIEVES_ROOM`, at `$E569`): the towers and the castle get
`MUSIC_MAYBE_TOWER` (`$06`). The Branch's doors (palettes 8 and 9) keep the
music. The tiles are the area's (`AREA_TO_TILES_INDEXES`): towers have no
tiles of their own.

**The way into each tower, castle and region**, from the door tables (a
region's door: `MAYBE_SCREEN_TRANSITION_MAP` and the door's start byte):

| Zone | Door | Takes | Arrives at | Palette, music | Way back out |
|---|---|---|---|---|---|
| Tower of Trunk | Trunk `$0B`, block `$A9` | J key | `$0D`, `$91` | `$07`, `$06` | `$0D` block `$90` |
| Tower of Fortress | Trunk `$28`, `$3D` | J key | `$29`, `$93` | `$07`, `$06` | `$29` `$92` |
| Tower of Suffer | Mist `$00`, `$9A` | Q key | `$3E`, `$91` | `$0B`, `$06` | `$3E` `$90` |
| Tower of Mist | Mist `$17`, `$A6` | K key | `$45`, `$A1` | `$0B`, `$06` | `$45` `$A0` |
| Castle of Fraternal | Dartmoor `$0C`, `$48` | nothing | `$10`, `$91` | `$0D`, `$06` | `$10` `$90` |
| Fortress of Zenis | Dartmoor `$0E`, `$AE` (region) | Demon's Ring | Zenis `$08`, `$A2` | `$0F`, `$10` | `$08` `$A1` (region) |
| Eolis | the gate, Eolis `$00`, `$9C` | nothing | `$01`, `$99` | `$00`, `$07` | none |
| Trunk | Eolis `$08`, `$9C` (region) | J key | Trunk `$00`, `$A1` | `$06`, `$03` | none: the Trunk has no door back to Eolis |
| Mist | Trunk `$28`, `$10` (region) | Q key | Mist `$11`, `$9E` | `$0A`, `$05` | `$11` `$9F` (region) |
| Branch | Mist `$1F`, `$9C` (region) | A key | Branch `$00`, `$A6` | `$08`, `$04` | `$00` `$A5` (region) |
| Dartmoor | Branch `$27`, `$A8` (region) | Ring of Dworf | Dartmoor `$00`, `$9E`, a row above the floor: he drops to `$AE` | `$0C`, `$02` | `$00` `$AF` (region) |

The towns and the regions also meet at the towns' edges
(`AREA_TOWN_TRANSITIONS_DATA`). Every door back out takes nothing and is a
step from where the hero arrives, so no entrance shuts him in. No boss screen
(`SCREEN_EVENT_BOSS` in bank 11's sprite lists: Trunk `$15` and `$3B`, Mist
`$34` and `$4C`, Branch `$07` and `$1D`, Dartmoor `$15`; `SCREEN_EVENT_FINAL_BOSS`
on Zenis `$00`, behind the door on `$01`) is an arrival screen or next to one.
chipx86's sprite-list labels are one area off and number screens in decimal:
`SPRITE_INFO_APOLUNE_SCREEN_59` is Trunk `$3B`. The Castle of Fraternal's first screen (`$10`) does hold a
Rokusutahn (`SPRITE_BOSS_ROKUSUTAHN`, one of the big enemies that roam many
screens), with the door out at the hero's back.

**`Game_ExitBuilding` (`$DE66`) is the game's loader for any place outside.**
It plays `Areas_DefaultMusic`, sets `Player_CurWeapon` from `SelectedWeapon`,
loads the area table and tiles for `Area_SavedArea`, and loads
`Area_SavedScreen` with `Screen_SavedPalette` and `Player_SavedPosXY` (the
same YX format as `Screen_StartPosYX`) through `Screen_Load`. With
`Area_Region`, the music and the facing set as the way in left them, it
replays any entrance. `Game_SetupExitBuilding` (`$DABE`) wraps it as the
other `Game_Setup*` do and returns, so a prepared stack can return into it and
then into `Game_MainLoop`.

**Zone respawn** (`fax_respawn.c`) uses four sites, none shared, so the order
of hooks at one address never matters to it:

- `Screen_SetupNew` (`$DD13`): a load into a different zone records the way
  in as the checkpoint. A building (area 4) changes nothing: its way out
  leads back where the hero stood. A load outdoors in no zone (Eolis screen 0,
  where a new game starts) forgets it.
- `$D9A0`, `Player_HandleDeath`'s `JSR Screen_FadeToBlack` right after the
  `Player_SetInitialExpAndGold` far call: reached only by a death, after the
  Guru's words. The RTS from `MMC1_PopBank` lands there, and a hook there runs.
- `$DB1D`, `Player_Spawn`'s `JSR Game_SpawnInTemple`, with S at `$FF` on the
  death path. A death in the checkpoint's zone skips the call: the hook pushes
  `$DB1F`, as the JSR would, and returns, so the routine is entered and left at
  once and `Player_Spawn` goes on at `$DB20`, where Save files saves the file at
  every respawn. Anything else lays out the temple at `TempleSpawnPoint`, as
  the original does (Save files keeps the Guru last saved with there): a file
  or a mantra continued (`$FC95` jumps to `Player_Spawn`), Guru travel's
  journey (its prepared stack RTSes into `$DB1D`, and a hook there runs for it
  too), and a death with no checkpoint or one for another zone (a death in a
  building, which is in no zone). Each forgets the checkpoint, so walking out
  of the temple records the next.
- `Game_SetupEnterBuilding` (`$DAAF`), called from `$DB20` (the return address
  is `$DB23`, `JMP Game_MainLoop`) after a skipped call: it writes the
  checkpoint to the `Saved*` variables, `Area_Region`, `Areas_DefaultMusic`
  and the facing, pushes `Game_SetupExitBuilding` - 1 and returns into it. Its
  RTS goes back to `Player_Spawn`'s `JMP Game_MainLoop`.

`Player_Spawn` has already set HP and MP to 80, cleared the Mattock bit and
the timed items (the Ointment too), and the death path has restored the
selected equipment, so the respawn keeps all of the original's death but the
place. Sugata's flash (Mist `$1E-$1F`, `$4E-$4F`) is a death like any other:
`$1F` is where the Branch's door back arrives, so dying to it there comes back
beside it, as walking through that door does.

The checkpoint is not kept in a save file. A file continues at the Guru it
was saved with, through the temple, and any temple spawn forgets the
checkpoint (the temple's door is the next way in), so a stored one would
never be read.

## Items and the inventory

Five categories, each with a count, a selected entry and a list. Entries are
the item's ID within its category (`ITEMINVENTORY_*` for items: the rings
`$00-$03`, the A, K, Q, J and Jo keys `$04-$08`, the Mattock `$09`, ..., the
Red Potion `$10`). Shops and scripts add the category in the top 3 bits
(`KEY_J` is `$87`).

| Category | List | Max (`MAX_INVENTORY_SLOTS`, `$847D` bank 12) | Selected | Count |
|---|---|---|---|---|
| Weapons | `$039D` | 4 | `$03BD` `SelectedWeapon` | `$03C2` |
| Armor | `$03A1` | 4 | `$03BE` | `$03C3` |
| Shields | `$03A5` | 4 | `$03BF` | `$03C4` |
| Magic | `$03A9` | 4 | `$03C0` | `$03C5` |
| Items | `$03AD` `ItemInventory` | 8 | `$03C1` `SelectedItem` | `$03C6` `NumberOfItems` |

- **The selected item is not in the list.** `Player_Equip` (`$8CA9`, bank 12)
  takes the new item out of the list and appends the old selection to it.
  So "the player has X" means X is selected or in the first `NumberOfItems`
  slots; `Player_LacksItem` (`$8CF7`) checks both. `$FF` selects nothing.
- `Player_RemoveItem` (`$9A6A`, bank 12) shifts the later entries down and
  decrements the count, leaving a stale byte after the end. `Player_AddToInventory`
  overwrites the last entry when the category is full; `Player_PickUpItem`
  (`$C8CD`) refuses when it is full.
- The rings, the Elixir, the Magical Rod, the Pendant and the Black Onyx are
  bits in `SpecialItems` (`$042C`), not list entries.
- **The HUD picture** of the selected item is four CHR RAM tiles at PPU
  `$13C0`. `Player_SetItem` (`$FC0B`) queues them through the PPU buffer;
  `UI_ClearSelectedItemPic` (`$C8DC`) queues blanks; `UI_DrawSelectedItem`
  writes `PPUDATA` directly and only runs with the whole HUD. Writing
  `SelectedItem` redraws nothing.
- The mantra stores each item list entry and the selected item in 5 bits
  (the list's count takes 4), so every item survives it, the Red Potion
  (`$10`) to the Fire Crystal (`$15`) included. (An earlier note here said 4
  bits an entry; that was the count.)
- Using an item (Down-B, `GameLoop_CheckUseCurrentItem` `$C478`) jumps on
  `SelectedItem`. The consumables end with `JSR Player_ClearSelectedItem`
  (`$C4BF`: `SelectedItem = $FF`, then `UI_ClearSelectedItemPic`): the Red
  Potion at `$C53B`, the Wing Boots at `$C581`, the Hour Glass at `$C5D0`,
  the Mattock at `$C64C`.

## Shops (bank 12)

There are 17 shops: 10 tool shops (which also sell the weapons, armor,
shields and magic) and 7 key shops. Meat, magic, the doctor and the martial
artist are single offers, scripted with questions and `$05` (pay). Every shop
runs one script layout:

| Offset | Bytes | Does |
|---|---|---|
| base | `89` / `8A` | The Tools or Key Salesman's portrait |
| +1 | `03 0F` / `03 0E` | "Hello. I sell tools. What would you like?" (keys: `$0E`) |
| +3 | `0F BUY` | `IScriptAction_ShowBuySellMenu` (`$8630`): "Come here to buy. Come here to sell." (`$10`); Buy jumps to BUY |
| +6 | `11 ITEMS` | `IScriptAction_ShowSellMenu` (`$8660`) |
| +9 | `03 02, 00` | "Thank you for shopping.", end |
| BUY (+12) | `08 ITEMS` | `IScriptAction_OpenShop` (`$83D8`) |
| BUY + 3 | `03 02, 00` | |

`ITEMS` is a list of item (full ID), price (16-bit), to a `$FF`. The scripts
are `ISCRIPT_ADDRS_L/U` (`$9F6B`/`$A003`, 152 entries) 12, 13, 25, 26, 29, 40,
43, 57, 59, 60, 72, 74, 87, 102, 104, 117 and 119, from `$A37A` (tools) and
`$A4FC` (keys), their lists from `$A42E` and `$A57A`.

| Shop | Sells (Golds) |
|---|---|
| Eolis tools | Hand Dagger 400, Red Potion 160, Elixir 320, Deluge 400 |
| Apolune tools | Small Shield 800, Hand Dagger 500, Deluge 500, Red Potion 300 |
| "Before Apolune" (chipx86's name) | Magic Shield 7600, Death 12500 |
| Forepaw tools | Long Sword 1600, Studded Mail 2500, Small Shield 1100, Red Potion 400, Wing Boots 2800 |
| Mascon tools | Large Shield 3500, Thunder 1200, Mattock 800, Red Potion 600 |
| "After Mascon" | Death 9800, Hour Glass 5600, Elixir 4300, Red Potion 300 |
| Victim tools | Full Plate 5200, Mattock 1200, Red Potion 800 |
| Conflate tools | Giant Blade 8500, Magic Shield 9800, Wing Boots 5500, Red Potion 1000 |
| Daybreak tools | Tilte 15000, Giant Blade 11500, Red Potion 1200 |
| Dartmoor tools | Giant Blade 13000, Red Potion 2000 |
| Key shops | J: Eolis 100, Apolune 140, Forepaw 200, Mascon 500. Q: Forepaw 500, Mascon 600, Victim 1200, Daybreak 1000. K: Victim 1500, Daybreak 1200, Dartmoor 1300 |

No shop sells the Leather Armor, the Fire magic (the Magician's, 3000 by
script), the A and Jo keys (gifts), the Poison, or the last fight's gear.

| Address | Name | Notes |
|---|---|---|
| `$0202-$0203` | `Temp_0202/3` | The price being paid or earned, as the gold counts |
| `$021E` | `Menu_CursorPos` | The list's cursor |
| `$021F` | `Arg_StringsCount` | Entries in the list |
| `$0220-$0227` | `DataArray` | The list's items, **8 at most**. Shared with the Select menu and screen scrolling |
| `$0228-$022F`, `$0230-$0237` | `ShopItemCostsL/U` | Their prices |

**The buy list** (`IScriptAction_OpenShop`, `$83D8`):

1. "What would you like?" (`$14`) types out (`$83D8-$83E8`); B on it ends
   the script. Then `$83EA` reads the list's address, `Shop_Populate`
   (`$8501`) copies items and prices, `Shop_Draw` (`$99F8`) opens the list
   (10,8, 20 x 20, see *The text box*) and puts the cursor at the top.
2. `IScripts_WaitForMenuInput` (`$84ED`) loops a frame at a time:
   `IScripts_UpdatePortraitAnimation`, `Menu_UpdateAndDraw` (`$9056`: Up and
   Down move the cursor, **wrapping** at both ends, with sound `$0B`), then A
   returns C clear and B C set. Select and the rest are ignored.
3. B ends the script at once (`$83FF`: `JMP IScriptAction_EndScript`), with
   no line.
4. A: `Shop_GetPlayerHasSelectedItem` (`$8482`) refuses a weapon, armor,
   shield or spell the hero has (in hand or in its list), and a SpecialItems
   item (rings, Elixir, Magical Rod, Pendant, Black Onyx) he has. Other items
   can be bought again and again, until their list is full
   (`MAX_INVENTORY_SLOTS`, `$847D`: 4, 4, 4, 4, 8). Either refusal closes the
   list and ends with "You can't carry any more." (`$06`, at `$8469`).
   Then `IScripts_CheckEnoughMoney`; short, "This is not enough Golds."
   (`$03`, `IScripts_ShowNotEnoughMoney`, `$837C`).
5. Bought: `Player_AddToInventory`, then `IScripts_ProgressivelySubtractGold`
   takes 10 Golds a frame and then waits 60 frames, then (`$8463`)
   `Maybe_Shop_DrawTextBox` (`$8471`: two frames, close the list, open the
   dialogue box) and `JMP IScripts_InvokeNextAction`: BUY + 3, "Thank you for
   shopping.", and the end. One purchase per conversation.

`IScripts_ShowFinalMessage` (`$837E`, A = the message) types a message and,
at `$838E`, on A or B, `JMP IScriptAction_EndScript`. The shops' refusals and
the empty sell list end the conversation through it.

**The sell list** (`IScriptAction_ShowSellMenu`, `$8660`): after reading the
list's address it scans **only the Items list** (`LDX #$04` at `$866F`, `CPX
#$05` at `$86B8`), from its last entry to its first, and offers each item that
is in this shop's list, at **half this shop's price** (`LSR` high byte, `ROR`
low byte, at `$86A5`: rounded down). So in the original a shop buys only the
Items it sells (Red Potions, Wing Boots, the Hour Glass, the Mattock, and the
J, Q and K keys), never a weapon, armor, shield or spell, and never the
selected item, which is in no list. Selling a Red Potion bought in Eolis (160)
in Dartmoor pays 1000. At `$86BC`, none: "You have nothing to buy." (`$12`,
sic) through `IScripts_ShowFinalMessage`. Else "Which one would like to
sell?" (`$13`), `Shop_Draw`, the same wait; B ends the script (`$8701`), A
(`$86E0`) takes the price, `Player_RemoveItem` (which for a SpecialItems ID
clears its bit), `IScripts_ProgressivelyAddGold`, then (`$86F8`) closes the
list, reopens the dialogue box and invokes SELL + 3.

**`Shop_Draw`** draws each entry on two rows from row 10: the item's picture
at column 12, its name at column 15, and on the next row its price at column
23, 5 digits by `UI_DrawDigitsNoLeadingZeroes`, which blanks leading zeros but
**always draws the units**: a price of 0 shows "0". Eight entries fill rows
10-25; rows 9 and 26 inside the frame stay empty. The gold counts by 10s, and
every price in the game is a multiple of 20, so every half is a multiple of
10. A price that is not would overshoot: `IScripts_ProgressivelyAddGold` pays
the next 10 up, and its count then runs on from `$FF` down, about 250 frames
instead of its usual 60.

**The font has no `:` or `/`**: `TILE_GLYPHS_START` leaves `#$%&()*+/:;<=>@[\]^`
blank. It has `-`, `.`, `,`, `'`, `!`, `?` and `"`.

**What a shop buys, with Quick shops.** A shop buys any item carried that is
in an inventory list: half its own price where it sells the item (the
original's rule), otherwise half the lowest price any shop asks (so selling
where a shop does not sell an item never pays more than buying it anywhere
costs), otherwise 100 Golds (the A and Jo keys, the Leather Armor, Fire, the
Poison), as the 40th Anniversary Edition pays. Never:

| Item | Why |
|---|---|
| Dragon Slayer | "Defeat the Evil one with Dragon Slayer." (`$9C`); King Grieve drops it, and only while you have none |
| Battle Suit, Battle Helmet | Worn for the last fight, only with the Dragon Slayer (`$8B`); Zoradohna drops them, likewise |
| The rings of Elf, Ruby, Dworf, the Demon's Ring | Doors, the fountain's rock, Dartmoor and the fortress |
| Black Onyx | The man by the gate to Branch gives the A key only to its holder |
| Elixir | The Spring of Trunk takes it (`ISCRIPT_SPRING_OF_TRUNK` consumes it) |
| Pendant, Magical Rod | One of a kind |

The SpecialItems ones are bits, in no list, so no sell list could show them;
they are refused even if one were. The selected item and what is equipped are
in no list either, as in the original. The rest are safe to sell: every other
key, the Mattock, the Wing Boots and the potions can be bought again, and the
A and Jo keys' givers hand out another to a hero without one.

## Doors and keys

Up at a door: `Player_CheckHandleEnterDoor` (`$E526`) has
`Area_SetStateFromDoorDestination` (`$E7C5`) find the door and set
`CurrentDoor_KeyRequirement` (`$042B`), then calls
`Game_RunDoorRequirementHandler` (`$EB2F`, from `$E539`) and enters the door
only if the requirement reads 0 afterwards. A door to the outside
(`Player_EnterDoorToOutside`, `$E5B2`) does the same with the requirement from
`DOOR_LOOKUP_REQUIREMENTS` (`$E5DB`), indexed by `Area_Region` (`$0435`) × 2,
+1 for the door forward.

| Requirement | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| Needs | A key | K key | Q key | J key | Jo key | Ring of Elf | Ring of Dworf | Demon's Ring |
| Handler | `$EB51` | `$EB61` | `$EB71` | `$EB81` | `$EB91` | `$EBA1` | `$EBB1` | `$EBC1` |
| Without it | script `$7D` | `$7C` | `$7B` | `$02` | `$7E` | `$7F` | `$7F` | `$7F` |

- A key handler compares `SelectedItem` with its key (`$04-$08`). On a match,
  `Game_UnlockDoorWithUsableItem` (`$EBD1`) runs script `$84`, which only says
  "I've used key." (`01 AE 00`); then `SelectedItem = $FF` and
  `JSR UI_ClearSelectedItemPic` at `$EBDE`. Clearing the selection is what
  uses the key up: a selected key is in no list. It falls into
  `Game_UnlockDoor` (`$EBE1`), which zeroes the requirement and plays the
  sound. Without the key, the script says "There is a mark of "Jack" by the
  key hole." (and Queen, King, Ace, Joker) and the door stays shut.
- A ring door checks a `SpecialItems` bit and never uses the ring up.
- **Doors lock again.** The requirement comes from ROM each time Up is
  pressed, so every pass from a door's locked side costs a key.
- Door data is in bank 3, per area. `*_DOOR_LOCATIONS` holds 4-byte records
  (screen, block `YX`, destination, start `YX`), ending in `$FF`. A
  destination below `$20` is a screen of the same area, `$20` and up a
  building, `$FE`/`$FF` the region behind/ahead. `*_DOOR_DESTINATIONS` holds
  4 bytes per destination, at destination × 4 (Towns: (destination − `$20`) × 4,
  wrapping in 8 bits): screen, palette or building, key requirement, unused.

**Every key door, and where each key comes from** (from the ROM's door tables;
key shops from `*_KEY_SHOP_ITEMS`, gifts from the interaction scripts):

| Key | Doors | Source |
|---|---|---|
| J | Eolis's exit (screen `$08`) to Trunk; Trunk `$0B` → the Tower of Trunk (`$0D`); Trunk `$28` → `$29` | Key shops: Eolis 100, Apolune 140, Forepaw 200, Mascon 500 |
| Q | Trunk's gate to Mist (screen `$28`); Mist `$00` → `$3E`; Mist `$01` → `$4F` | Forepaw 500, Mascon 600, Victim 1200, Daybreak 1000 |
| K | Mist `$17` → `$45`, `$20` → `$50`; Branch `$0A` → `$09`, `$0E` → `$0F`, `$14` → `$13`, `$19` → `$18` | Victim 1500, Daybreak 1200, Dartmoor 1300 |
| A | Mist's gate to Branch (screen `$1F`), the one A door | A gift: the man in the house on Mist `$1F` (Buildings `$22`, script `$46`, `ISCRIPT_A27C`) gives one if you hold the Black Onyx and no A key, each time you ask |
| Jo | Trunk `$1E` → `$3E`, the one Jo door ("the first spring") | A gift: the man in the house on Trunk `$2D` (Buildings `$15`, script `$24`, `ISCRIPT_A18A`) gives one once the Spring of Sky is done and you hold no Jo key, each time you ask |

The rings: Elf for three Eolis houses (screens `$06`, `$07`), Dworf for
Branch's gate to Dartmoor, the Demon's Ring for Dartmoor's gate to the Evil
Fortress and the two doors on Dartmoor `$0E`. No sprite in the game is a key
pickup.

**Reusable keys break no sequence.** A shop key could already be bought in
any number. The A and Jo keys are gifts, but each opens only the one door its
giver sends you to, and the giver hands out another whenever you hold none, so
a kept key opens nothing a fresh one would not. No key needs to stay
consumable.

## The Mattock and breakable walls

- `Player_UseMattock` (`$C616`) looks at the block in front of the hero (1
  pixel left of him, or 16 pixels right, on `Player_PosY`'s row) and acts only if it
  is `USE_MATTOCK_BLOCK_TRANSITIONS[Area_CurrentArea × 4]` (`$C68F`). It runs
  script `$81` ("I've used Mattock."), calls `Player_ClearSelectedItem` at
  `$C64C` (which uses it up), then replaces that block and the one below
  over four steps of 4 frames.
- Only Trunk (area 1) has a group: `$63` → `$85`, `$86`, `$42`. Every other
  group is `$00`, so elsewhere the Mattock "breaks" air (block `$00`) into air:
  the message shows and, in the original, the Mattock is gone for nothing.
  chipx86's comments name the groups after towns; they are by area.
- Block property `$0B` (`BLOCK_MAYBE_BREAKABLE_BY_MATTOCK`) is on block `$63`
  in Trunk, and also `$84`, `$85` in Mist, `$5C` in Branch and `$61` in
  Dartmoor. The Mattock code never reads properties, and `$0B` is impassable,
  so those are plain solid blocks.
- **Block `$63` is in two screens** (all 64 Trunk screens decoded): `$0C`,
  x 3, rows 10-11, the wall beside the Tower of Trunk on the way east; and
  `$2D`, x 3, row 11, a floor block beside the ladder down to `$29`, which
  opens nothing.
- **A broken wall comes back** whenever its screen loads again (checked in
  the game): breaking it only edits `ScreenBuffer`, and screens decompress
  from ROM. So in the original, every pass through it costs a Mattock.
- `QUEST_MATTOCK` (`Quests` bit `$10`) only records that Ripasheiku's Mattock
  was picked up (`Player_PickUpMattockWithQuest`, `$C74A`); its sprite
  removes itself while the bit is set. `Player_Spawn` clears the bit, so after
  a death that Mattock is back. It does not open or close any wall.
- **Where Mattocks come from:** one lies in the Tower of Trunk at `$0E`
  (sprite `$50`, shown on one counted visit in four once the screen's enemies
  are dead: `SpriteBehavior_RandomlyShowItem_50`, `Screen_Is3Of4Visits`),
  Ripasheiku drops one at the tower's top (`$15`, sprite `$5B`), another
  lies in Branch `$18`, and the tool shops of Mascon (800) and Victim (1200)
  sell them.

**A reusable Mattock breaks no sequence.** Both walls are in Trunk, before the
shops, but the tower holds Mattocks that come back (the lying one, and
Ripasheiku's after any death), and the tower comes before either wall. A kept
Mattock breaks nothing a fetched one could not.

## Wing Boots, the Hour Glass and the Elixir

| Address | Name | Notes |
|---|---|---|
| `$001A` | `InterruptCounter` | +1 each NMI. The game's second is a frame where `& $3F` is 0: 64 frames |
| `$00A5` | `Player_StatusFlag` | `$80` while the Wing Boots fly. `Game_DecWingBootsDuration` sets it every frame the duration runs and clears it when it runs out; `Game_InitStateForSpawn` zeroes it |
| `$0429`, `$042A` | `DurationWingBoots`, `DurationHourGlass` | Seconds left, `$FF` when not running |

**Using them** (Down+B, `GameLoop_CheckUseCurrentItem`, which refuses while
jumping or falling, `Player_Flags & $05`):

- `Player_UseWingBoots` (`$C579`): script `$83` ("I've used Wing Boots."),
  `JSR Player_ClearSelectedItem` at `$C581`, sound `$1A`, then
  `DurationWingBoots` = `TITLE_TO_WINGBOOTS_DURATION[PlayerTitle / 4]` (`$C599`:
  40, 30, 20, 10) with the `STA` at `$C592`, then `JSR UI_DrawTimeValue`, which
  draws A.
- `Player_UseHourGlass` (`$C5C8`): script `$82`, `JSR Player_ClearSelectedItem`
  at `$C5D0`, sound `$1A`, halves HP (`LSR Player_HP_U`, `ROR Player_HP_L`),
  `DurationHourGlass` = 15, music `$0B`.
- Every main-loop frame `GameLoop_CountdownItems` (`$C88F`) runs
  `Game_DecHourGlassDuration`, `Game_DecWingBootsDuration`, then the Glove's
  and the Ointment's. Each takes a second off on the game's second, so a
  duration of N lasts between N and N+1 seconds; wrapping to `$FF` runs "The
  power of the Wing Boots (Hour Glass) is gone." (messages `$C0`, `$C1`).
- **T:** is two ASCII tiles at PPU `$2058`. Only the Wing Boots draw it
  (`UI_DrawTimeValue`, queued through the PPU buffer at `$0500`: length 2,
  `$20`, `$58`, the digits); the Hour Glass never shows a time.
- `Game_ClearTimedItems` (`$C6AF`) sets all four durations to `$FF`. It runs
  from `Game_Start` and `Player_Spawn`, so every death ends them, but not on a
  Guru travel journey, which enters `Player_Spawn` past it: a flight or a
  freeze carries on in the other temple.
- **Shops never sell what you hold.** `Shop_GetPlayerHasSelectedItem` (`$8482`, bank 12) refuses
  any item already selected or listed, and a special item whose bit is set,
  with "You can't carry any more." (message `$06`). In the original, then, a
  second pair of Wing Boots or a second Hour Glass came only from the world:
  `Player_PickUpItem` adds a duplicate like anything else.

| Item | Shops | In the world (bank 11's lists: area, screen, block `YX`) |
|---|---|---|
| Wing Boots | Forepaw 2,800, Conflate 5,500 | Sprite `$55`: Trunk `$2D` (`$41`), Mist `$11` (`$5E`), `$1B` (`$35`), `$32` (`$48`), Branch `$1C` (`$A9`), `$22` (`$92`). Sprite `$5C`: Trunk `$3B` (`$3D`), Zorugeriru's |
| Hour Glass | After Mascon 5,600 | Sprite `$56`: Trunk `$13` (`$4D`), Mist `$30` (`$32`), Branch `$16` (`$72`), Dartmoor `$04` (`$5E`), `$12` (`$32`) |
| Elixir | Eolis 320, after Mascon 4,300 | Sprite `$4D`: Trunk `$3C` (`$32`), Mist `$41` (`$A1`), beside Zoradohna (`$2E`) |

- Sprites `$55` and `$56` show on one counted visit in four, like the
  Mattock (`SpriteBehavior_RandomlyShowItem_55` and `_51`,
  `Screen_Is3Of4Visits`). Sprite `$5C` is the tower quest's pair
  (`SpriteBehavior_WingBootsDroppedByZorugeriru`): it removes itself once
  `QUEST_WING_BOOTS_FROM_TOWER` (`$08`) is set, and
  `Player_PickUpWingBootsWithQuest` sets it. The Elixirs always show.
- Touching one, `Player_HandleTouchItem` (`$88FB`, bank 14) reaches
  `Player_PickUpWingBoots` (`$C6D8`, script `$89`) or `Player_PickUpHourGlass`
  (`$C6BE`, script `$8A`), which end with `LDA #item`, `JMP Player_PickUpItem`
  (`$C8CD`: append to the list if there is room) after the script has shown
  "I'm holding ...". `Player_PickUpElixir` (`$C864`, script `$86`) ORs
  `SpecialItems`' bit and never calls `Player_PickUpItem`.
- **No one gives them.** The nurse in Mascon's hospital (building `$1B`,
  script `$37`, `ISCRIPT_A226`) only talks about the Hour Glass: what it does
  without one, "The Hour Glass will exhaust your energy" with one. The Mist
  house woman (`ISCRIPT_OVERWORLD_MIST_HOUSE_WOMAN`) talks of the Pendant and
  the meteorite.
- **The Spring of Trunk takes the Elixir, it does not give it.**
  `ISCRIPT_SPRING_OF_TRUNK` asks for "your medicine"; with the Elixir it
  `CONSUME_ITEM`s it and completes `QUEST_SPRING_OF_TRUNK`, without one it
  says "Come back with the medicine."

**The Hour Glass and bosses.** While `DurationHourGlass` runs,
`Sprites_UpdateBehavior` (`$A66B`, bank 14) skips every sprite whose
`SPRITE_CATEGORIES_BY_ENTITY` is 0 (an enemy), and `Sprites_UpdateAll` stops
`SpriteUpdateCounter`. Bosses are category 7, so **bosses never freeze**
(checked in the game: Ripasheiku's place, phase and countdowns keep changing
while `SpriteUpdateCounter`, `$0383`, stands still). Their behaviors run, but
whatever a boss times by `SpriteUpdateCounter` reads the stopped counter:
Ripasheiku casts in his last phase on frames whose low 6 bits read `$20`, so
from the code, an Hour Glass that stops the counter there would have him try
every frame of that phase, and one that stops it elsewhere, never (not
checked; one test run lost the hero there under the Hour Glass). He also
stands for up to about 4 s between moves (`CurrentSprites_BehaviorData2`,
`$02EC`, counting down), so watching his place alone can mistake him for
frozen. And on the first frame on a
boss's screen, `ScreenEvents_HandleBoss` (`$EF9E`) calls `LimitItemDurations`
(`$EFBF`), which sets
a running Ointment or Hour Glass to 0, so one used before the boss's screen
ends within a second of walking onto it (checked too).

**The Elixir.** When `Player_ReduceHP` (`$C08E`) takes the last HP it checks
`SpecialItems & $08`: set, it jumps to `Player_UseElixir` (`$C4CA`), which
clears the bit, runs script `$85` ("I've used Elixir.") and refills HP and MP;
clear, it sets `PlayerIsDead`. The menu's Player page draws a picture for each
`SpecialItems` bit, the Elixir's included, on its last row. A mantra keeps
the bit.

**Flying.** While `Player_StatusFlag` has `$80` and A is held,
`Player_CheckHandleVertMovement` (`$E2C8`) treats the hero as on a ladder
(`Player_CheckIfOnLadder`, then the climb): Up rises a pixel a frame, and
`Player_CheckHandleJump` lets Left and Right move him in the air. Let go of A
and he falls. A test that flies must hold A without a break: renewing short
TCP `action` holds made the hero bob up and down, and
`tests/recharge/recharging_items.py` renews long `pad` holds before they lapse.

**The Spring of Sky needs the Wing Boots** ("They say a fountain is up in the
sky, but you need Wing Boots..."). Its man (`$3B`) stands on the fountain's
ledge in Trunk `$25`, at the top of a shaft that rises from the floor of `$20`
("you'll come to a place where the sky opens above") through `$23` and `$24`.
The usual takeoff is that floor. The way up: the shaft at x 48, round the right
end of the ledge across `$24` (row 5, open at columns 13-14), up the right side
of `$25` and left along its top to the man. Flown in the end-to-end test with
the enemies of `$20`, `$23` and `$24` cleared, it takes about 1,035 frames
(16 of the game's seconds) from leaving the message box to landing by him, so
the lowest band's 30 s is enough, with 15 s to spare. The original gave a
Novice 40 s.

The dialogue also says the Wing Boots are needed "to get to the misty town of
Victim" (Mist `$22` and `$25` lead into it). One candidate: the ladder at the
top left of Mist `$04`, up to `$2A` and on through `$2B` and `$2C` to `$22`,
starts high above the floor, and a probe flew from the floor into `$2A` in
about 180 frames (3 s). Whether that is the climb the dialogue means, and the
rest of the way to Victim, were not checked.

**Recharging breaks no sequence** (the check from #6):

- It gives no item, so the first moment you can own Wing Boots is unchanged.
  The earliest sources (table above) are around Forepaw: its shop, and the
  Tower of Fortress (Trunk `$2D`, one visit in four), whose entrance is "right
  above this town".
- Flying again is not new reach. The original sells pairs in two shops and
  lets found pairs stack, so it gave any number of flights for gold. A kept
  pair flies again a minute after its flight runs out.
- Flights longer than the original's longest (40 s, a Novice's) come only from
  Swordman (50 s) and Champion (60 s). Not checked: whether any place needs one
  flight of more than 40 s. Nothing in the dialogue points at one, and the late
  regions are gated by keys and rings.
- The Hour Glass reaches nothing, and a kept Elixir is a revive, not a way in.

## Screens and blocks

- A screen is 16 × 16 blocks in `ScreenBuffer` (`$0600`, row × 16 + column);
  the last row is always zero. `Area_LoadBlocks` (`$D1F6`) decompresses one
  from the area's bank (`AREA_TO_SCREEN_DATA_BANKS`: Trunk and Branch bank 1,
  the buildings, Dartmoor and the Evil Fortress bank 2, the rest bank 0),
  through the word
  table at the bank's `$8000` (entry `AREA_TO_BANK_OFFSET`), then a word per
  screen; words are offsets from `$8000`. The data is a bit stream, high bit
  first: 2 bits per block, `0` copy the block to the left, `1` the one above,
  `2` the one above-left, `3` read 8 more bits as the block.
- The screen's neighbours are in `Area_ScreenToTheLeft/Right/Above/Below`
  (`$0066-$0069`), loaded from the area's scroll data. Walking off an edge
  makes the neighbour `Area_CurrentScreen` and loads it.
- **For tests:** set `Area_ScreenToTheRight` in a save state and walk off the
  right edge to reach any screen of the same area the game's own way.
  `CurrentSprites_Entities` (`$02CC`, 8 slots) set to `$FF` clears a screen's
  enemies (see *Eolis, for tests*).

## Features and what they use

`mods/allocations.toml` lists each feature's ROM, hook sites and shared
addresses, and `tools/check_allocations.py` enforces it.

Every feature below is a trusted plugin of the one Faxanadu Remastered mod
(`mods/preloaded/packages/faxanadu.remastered`, feature `remastered`), so
they are on or off together, and may rely on each other. Each still checks
its own `enabled` flag, which the mod runtime sets for all of them at once:
"with X off" below means with the remaster off, or X turning itself off on an
unexpected ROM layout. Seamless (`fax_world.c`) is its own package.

- **Guru travel** (`fax_travel.c`). After the mantra or a new title, the Guru
  says "All Gurus / meditate as one." and offers "Shall I send / you to
  another / Guru?", then asks about each Guru you have visited up to the
  furthest temple ("To the Guru of / <town>?"). It steers the conversation at
  `IScripts_InvokeNextAction` and supplies two script steps and one message
  slot through read overrides. The journey is the respawn path without its
  penalties, with `TempleSpawnPoint` set only during the committed
  `Game_SpawnInTemple` call, so the spawn point is kept. The furthest temple
  is the highest visited Guru's (or `TempleSpawnPoint`, if further), since
  with Save files the spawn point is the Guru last saved with. With a file in
  play, Save files asks first and starts Guru travel through
  `fax_travel_begin`; the visited Gurus are then the file's `gurus` field.
  Without Save files they persist in `faxanadu_gurus.txt`; a new game resets
  them.
- **Death keeps gold** (`fax_death.c`). Dying brings the hero back with the
  gold, experience and earned title he had. It hooks
  `Player_SetInitialExpAndGold` and returns at once when the far call's resume
  address is `Player_HandleDeath`'s, and leaves the start screen's call alone.
  It keeps no state.
- **Pendant fix** (`fax_pendant.c`). Weapons hit a quarter harder only with
  the Pendant. It hooks `@_checkGlove` (`$8884`, bank 14), where both sides
  of the reversed branch meet, and sets `Temp_00` from
  `WEAPON_STRENGTHS[Y]` and the Pendant bit before the Glove adds its bonus.
  It keeps no state.
- **Magic button** (`fax_magic.c`). A bindable button (C, or Y on a
  controller) casts the selected spell. On a press, it ORs Up held and B new
  into the joypad masks at `Player_CastMagic`'s entry, when `Game_MainLoop`
  called it, and puts the real masks back at `GameLoop_CheckUseCurrentItem`'s
  entry, so no door, sword, item, NPC or next pad read sees them. If the NMI
  read the pad in between (a lag frame), it recomputes the changed mask
  against the real one. A press lasts one frame, as a pad read does. The
  bindings are `MagicKey` and `MagicButton` in `config.ini` [Game], in the
  host's own key and button names.

- **Zone names** (`fax_banner.c`, with `fax_zone.c` and `fax_text.c`).
  Entering a zone with a different name shows it below the HUD for 2 s, in a
  text box drawn over the presented picture (after Seamless composes its
  own). No hooks and no ROM: it reads RAM in `frame_end` once the screen has
  settled. A temple whose way out is not where the hero last stood means a
  respawn or a Guru travel journey, and makes the next zone's name show. The
  banner is held while a box is open or the game is paused. Its save-state
  record keeps the banner as it was. Its drawing, `fax_banner_draw`, takes any
  line, so Slain by draws in the same box.

- **Slain by** (`fax_slain.c`, drawn by `fax_banner.c`). When a hit takes the
  hero's last HP, the banner says what it came from, "Slain by a Sugata", from
  that frame through the dissolve, fading out at frame 248 of its 256, before
  the Guru speaks, and puts away any zone name showing. Two hooks, both only
  observing (see *What killed the hero*): at `Player_ReduceHP` (`$C08E`) it
  names the source of a hit that kills (HP below the damage, no Elixir), and
  at `Sprite_Maybe_ResetState` (`$A202`) it keeps each slot's spawner, which
  names a shot or a rock; a slot forgets it when its sprite goes. A shot whose
  spawn it never saw (with Seamless on, a neighboring room's) is named by the
  only kind of caster of it on screen, or not at all. A death nothing caused
  names nothing. Both hooks keep no state in Seamless's isolated rooms. Its
  save-state record is the line and its age, and each slot's spawn (21
  bytes). It loads the game's font itself, so it does not rely on Zone names
  having drawn first.

- **Speaker names** (`fax_speaker.c`, with `fax_zone.c` and `fax_text.c`).
  While a portrait's frame is on screen, the speaker's name sits on a tab
  above the dialogue box, at its left edge (tiles 10, 13-15), in the game's
  font and frame; on a shop's list it moves up onto the list (rows 5-7). The
  King and the Gurus are "of" a place: a temple's town by temple number (so
  the Guru whose door opens on the Mist is Mascon's, as in Guru travel), else
  the zone at the building's door (`Area_SavedArea`, `Area_SavedScreen`),
  a tower or castle by its own name ("Guru of Fraternal"). A name over 16
  characters loses the tab's blank cells, so no tab is wider than the dialogue
  box. No hooks, no ROM, no state: it reads RAM and the nametable when the
  picture is presented, and draws after Seamless and zone names.

- **Auto keys** (`fax_keys.c`). Up at a locked door opens it with its key from
  anywhere in the inventory. At `Game_RunDoorRequirementHandler` it lends the
  key to `SelectedItem` when the key is listed but not selected, so the game's
  own check and "I've used key." run. At `UI_ClearSelectedItemPic` called from
  `$EBDE` it takes the key out of the list (unless reusable tools is on), puts
  the old selection back and skips the clear: the HUD never stopped showing
  it. The lent key is saved with save states.
- **Reusable tools** (`fax_keys.c`). Keys and the Mattock are not used up. At
  the same `UI_ClearSelectedItemPic` call it puts the selected key back in
  `SelectedItem` and skips the clear; it skips `Player_ClearSelectedItem` when
  `Player_UseMattock` calls it (from `$C64C`) and for no other item. Both
  features' sites at `$C8DC` run one callback, which does the work of whichever
  are on. It keeps no state of its own.
- **Falling pose** (`fax_falling.c`). In the air without a jump (falling, not
  jumping, swinging or climbing, and not on the frame he lands), both pose
  routines return frame 3, the jump pose. It hooks their entries, and
  observes `@_setFalling` and `@_checkCapFall`'s branch to tell a landing from
  a fall. Its save-state record is that one bit.
- **Forgiving jumps** (`fax_jumps.c`). Coyote time: A within 5 updates of
  walking off a block he could have jumped from makes the ground check at
  `$E3B2` find ground, after adding this update's 8-pixel fall step itself.
  The game's own jump then starts from where he is, so the arc is the best
  original jump's (A on the last frame on the ledge), later and 8 pixels lower
  per frame of delay: never above it, and every frame at the same x. Whether
  he could have jumped from the ledge (the block over his head) is asked of
  `Player_CheckIfPassable` in an isolated call. Jump buffering: A newly
  pressed in the air counts for 6 updates; on the first update on the ground
  within them, the jump check at `$E44B` sees A, so the jump is the one the
  original makes with A pressed then. Neither applies with the Wing Boots
  running, in knockback, while climbing, or across a change of screen or
  area. Nor while Block is up (`fax_block_raised()`, false with Block off):
  Block takes A away, and a press kept from just before it went up would hand
  A back on landing, so each update with Block up drops it (TCP
  `forgiving_jumps_state` counts those as `blocked`). It counts updates at
  `Player_CheckHandleVertMovement`'s entry, not frames, and keeps a 7-byte
  save-state record.
- **Hold A to speed up text** (`fax_text_speed.c`). While A is held (and not
  on the frame it goes down), a hook at `TextBox_ShowNextChar_IfReady` rounds
  `TextBox_Timer` up to a multiple of 4, so a character types every frame,
  and clears `TextBox_PlayTextSound` on all but every 4th frame, so the
  typing sound keeps its rhythm. Page breaks and questions still wait for a
  new press. It writes nothing else and keeps no state. It reads only the A
  bit of the joypad masks, which the Magic button never changes (it ORs in
  Up and B, for `Player_CastMagic` alone, outside any message loop), and
  writes neither mask.
- **Taller dagger** (`fax_dagger.c`). The Hand Dagger's Y reach,
  `MAYBE_WEAPON_RANGES+1` (`$B898`, bank 14), is served as `$08` instead of
  `$04` through a read override that matches only bank 14's byte while it
  holds `$04`, so its box is as tall as the swords' (see *Weapon reach*). Its
  X reach stays `$08`. No hooks. Its save-state record is empty: the load
  callback only serves the byte again. `mods/allocations.toml` claims the byte
  as an `overrides` entry, which `tools/check_allocations.py` checks against
  its stock value.
- **Recharging items** (`fax_recharge.c`, with the use-up list in
  `fax_items.h`). The Wing Boots and the Hour Glass are kept and recharge, and
  the Elixir is kept and a Guru charges it again. All its sites are in the
  fixed bank:
  - `Player_UseWingBoots` and `Player_UseHourGlass`, at entry: not ready, it
    plays the "invalid choice" sound (`$0D`, `Sound_PlayEffect` in a committed
    call) and returns to the main loop as the routine's RTS would.
  - `Player_ClearSelectedItem` (`$C4BF`), shared with reusable tools: skipped
    when the JSR came from `$C581` or `$C5D0`, so the item stays selected and
    drawn. `fax_items.h` lists every caller and who keeps its item.
  - The `STA DurationWingBoots` at `$C592`: A becomes the flight for the title
    band, from the one table in `fax_recharge.c`, and `UI_DrawTimeValue` draws
    it.
  - `GameLoop_CountdownItems`, before the game's own countdowns: a run that
    has ended (its duration back at `$FF`) starts a 60 s cooldown; cooldowns
    count down on the game's second; and T: shows the selected item's
    cooldown (else the Wing Boots', else the Hour Glass's), blank 16 frames in
    64, queued through the PPU buffer as `UI_DrawTimeValue` queues it, when
    `PPUBuffer_HasCapacity` would allow it. A spent Elixir whose bit came back
    (bought or found) is charged.
  - `Player_UseElixir`: the Elixir is spent. `SpecialItems`' bit stays the
    game's own "a revive is in reserve", so `Player_ReduceHP`, the Player page,
    the shops and the Spring of Trunk all go by whether it is charged: a shop
    sells one only to a spent Elixir, which charges it at the shop's price.
  - `Player_PickUpItem`, with A the Wing Boots or the Hour Glass and the hero
    holding one (selected or listed): ready at once, and nothing added, even
    with the list full. Found during a run, the run goes on and no cooldown
    follows it.
  - `Game_ClearTimedItems`: both ready (a death, a new game, a mantra; not a
    Guru travel journey), or a continued file's cooldowns (below). Zone
    respawn brings the hero back through `Player_Spawn` too (it skips only the
    `JSR Game_SpawnInTemple` after this call), so a death readies both
    wherever he comes back.
    `Game_ShowStartScreen`: everything forgotten. A mantra keeps a charged
    Elixir (the bit) and loses a spent one.
  - `IScripts_InvokeNextAction` (`$826E`, bank 12), observing only: at a
    Guru's first action (a temple Guru's `SET_SPAWN_POINT`, which only their
    seven scripts use, or `$A0BD`, Eolis's Guru's `CHECK_FOR_ITEM`), a spent
    Elixir is charged, before he speaks. So any Guru conversation counts: the
    plain mantra one with Save files and Guru travel off, and with them on,
    whichever of Save files' choices is picked, or B. Their hooks at the same
    address never redirect those two actions (Save files undoes the spawn
    point's raise at the next action), so the three run in any order.
  - Save files: the file in play keeps `recharge.wingboots` and
    `recharge.hourglass` (the game's seconds until ready, 0 when ready; a run
    not yet over counts as its whole cooldown) and `recharge.elixir` (`spent`,
    or no field: the core `special_items` says whether it is charged). They
    are written whenever the file is (`FAX_SAVE_WRITING`: a Guru's save, and
    every spawn's, so a death writes them ready), read back on
    `FAX_SAVE_CONTINUED`, and reset by `FAX_SAVE_NEW_GAME`. Continuing spawns
    the hero through `Player_Spawn`, whose `Game_ClearTimedItems` would make
    both ready, so the continued cooldowns wait for that call and are put
    back there, before the spawn's save writes the file again.
  - Its hooks do nothing inside Seamless's isolated simulations. Its 9-byte
    save-state record (a version, each timed item's running flag and cooldown,
    the Elixir spent, and a continued file's cooldowns waiting for the spawn)
    is its whole state while playing.
- **Block** (`fax_block.c`). A bindable button (V, or B on a controller)
  raises whatever the shield slot holds, the Battle Helmet included. The host
  input callback clears the d-pad, A and B while it is up and a pass of
  `Game_MainLoop` reached `GameLoop_CountdownItems` (`$C88F`, from `$DB68`)
  in the last frame, so text boxes and menus keep their buttons. At
  `Player_HandleHitByMagic` it lets nothing through. At
  `Player_HandleTouchEnemyMagic`, magic whose box's middle is on the side the
  hero faces returns through `$87CE` (removed, sound `$0C`): with the helmet,
  which has no shield box, this is the only way magic is stopped. At `$AB44`
  it stops Sugata's flash from any side with the shield's sound and finishes
  the behavior through `$AB60`. Contact damage is untouched. The tutorial
  hooks `IScriptAction_EndScript` (`$82B4`, which every way out of a script
  takes): if the hero owns no shield-slot item before the script and does
  now, or the script is the Battle Helmet's pickup (`IScriptOrCHRAddr` =
  `$A698`, before the helmet is added), it points the script at Guru
  travel's SAY step with its line in message `$C3`'s slot and returns through
  `IScripts_InvokeNextAction`, so the line is one more native message in the
  same box. The line names the binding of the device last used. That it was
  said is a named field (`block.taught`) behind `remembered()`/`remember()`:
  a line in `faxanadu_block.txt` beside the game until save slots keep named
  fields. Its save-state record is what the hero owned and whether the line
  is showing. The tutorial sets the SAY step's and the slot's read overrides
  itself, so it speaks with Guru travel off too.
  It shows. With any shield-slot item, in any pose, Block going up makes the
  hero glint: at `Player_DrawWeapon`'s entry the body's slots, noted at
  `Player_DrawBody`'s, take palette 1, the shield's colors, for draws 1-4 and
  9-12 of 12, and stop at once if Block is let go. The shield's own sprites,
  already in palette 1, and its box are the game's (see *How the shield is
  drawn*). The glint only draws. Seamless world's redraw of the hero past a room's edge runs in an
  isolated machine without hooks, so that part of him is the stock pose (the
  falling pose's is the same). The glint is not in the save-state record: a
  load starts it over, and a button held through the load does not start one.
- **Sugata's flash** (`fax_sugata.c`). At `$AB26`, before the behavior is
  under way, it skips the call for 30 frames, and Sugata's draw (`$929F`)
  returns through `SpriteUpdateHandler_Invisible` 4 frames in 8, counted back
  from the flash so the warning ends with Sugata shown. At `$AB36` it clears
  the greyscale the behavior just set and records a band: the presented
  picture (after Seamless composes it, at the active room's origin, and
  before the zone banner) greys only the columns within 40 pixels of Sugata's
  middle, under the HUD, as the PPU would (each color becomes the grey of its
  palette row, `entry & $30`), for 8 frames, and dims them by a third through
  the warning. At `$AB44` it decides: the hero's 16 pixels outside the band,
  no damage; Block up with the Magic Shield, 12 HP to Sugata through
  `@_reduceEnemyHP` with sound 2 and a Deluge's knockback; Block up with
  anything else, it leaves the site to Block; Ointment running, the flash's
  own sound, iframes and knockback without `Player_ReduceHP`; otherwise the
  game's 10 HP. Two reflections kill Sugata (more than its 22 HP). Its
  save-state record is each slot's warning count and the band.
- **Quick shops** (`fax_shops.c`). Up on a shopkeeper opens his buy list at
  once, Select switches to the sell list and back, a purchase or a sale keeps
  the list up with the cursor where it was, and B leaves. A shop buys
  anything carried (see *Shops*), 8 to a page: Up from the top or Down from
  the bottom turns the page. No ROM and no read overrides. It steers the
  script at `IScripts_InvokeNextAction` (its first action goes to BUY; BUY + 3
  and SELL + 3 go back to their list) and jumps by return at seven sites of
  its own: OpenShop's entry, past "What would you like?" to `$83EA`;
  `$8463`, past the purchase's close; `$86BC`, where it replaces the sell
  scan's list with a page of everything carried and goes on at `Shop_Draw`
  (`$86D8`), past "Which one would like to sell?", or, with nothing to sell
  and a list open, through `Maybe_Shop_DrawTextBox` first; `$86E0`, back to
  the wait for an item no shop buys; `$86F8`, past the sale's close;
  `$84F0`, in `IScripts_WaitForMenuInput` before the menu update, where it
  puts the cursor back, Select or a page turn goes through
  `Sound_PlayEffect` (`$0B`) to the other step, and B goes through
  `Maybe_Shop_DrawTextBox` to `IScriptAction_EndScript`, so the list is closed
  when the script ends, as after the original's purchase, and a line Block
  adds there (its tutorial, after the first shield) shows in the dialogue box;
  and `$838E`, where a shop's message goes back to the list. Presses while a
  list is drawn, or the gold counts, are lost, as in the original: the game
  reads the pad only in the list's wait. The hint line ("Select to sell", "Select to
  buy", "Not for sale") is drawn on the list's row 26 and the page ("1 of 2")
  on row 9, at present time, while the list's four frame corners show; an
  unsellable item's price cells are painted the box's inside. Its save-state
  record holds the conversation (17 bytes).
- **Zone respawn** (`fax_respawn.c`). Dying comes back at the last entrance
  into the zone (tower, castle, town or region) the hero died in, after the
  Guru's words, instead of at the temple. A hook at `Screen_SetupNew` records
  the way in whenever a load enters a different zone; a hook after the death
  script marks the death; `Player_Spawn`'s `JSR Game_SpawnInTemple` is skipped
  for a death in the checkpoint's zone, and its `JSR Game_SetupEnterBuilding`
  then runs `Game_SetupExitBuilding` with the entrance in the `Saved*`
  variables (Save files' save at `$DB20` in between runs as at any respawn).
  With no checkpoint, or after continuing a file or a Guru travel journey, the
  temple is laid out as before, and the checkpoint is forgotten until the hero
  walks out. Its save-state record holds the checkpoint and where a death is
  (see "Entrances and zone respawn").

- **Save files** (`fax_save.c`, `fax_files.c`). Three files instead of
  mantras; see "Save files" below.

## Save files

**The files** are `faxanadu_file1.txt` to `faxanadu_file3.txt` beside the
game, each a record of named fields: text, one `key = value` a line, `#`
comments. A file is written whole to a `.tmp` and renamed over the old one,
so a crash never leaves half a file. `--no-save` writes nothing. A record
must have a `version`; a newer one than the build knows shows as "From a newer
version" and is never written over (erase it to reuse the file), and anything
malformed shows as "Damaged file". Older versions migrate through the
`migrations` table in `fax_save.c` (none yet). Fields the build does not know
are kept, in place, through every save.

**The core fields**, written from RAM at each save:

| Field | From | Notes |
|---|---|---|
| `version` | | 1 |
| `title`, `next_title` | `PlayerTitle`, `NextPlayerTitle` | 0-15 |
| `experience`, `gold` | `Experience`, `Gold` | Exact; gold is 24-bit |
| `spawn` | `TempleSpawnPoint` | The temple of the Guru last saved with: where the file continues and death respawns |
| `furthest` | Save files' own count | The furthest temple reached: the original's `TempleSpawnPoint`. The file select shows its town |
| `special_items`, `quests` | `SpecialItems`, `Quests` | Bit flags, decimal |
| `weapons`, `armor`, `shields`, `spells`, `items` | The five lists, up to their counts | Entries in decimal, space-separated |
| `selected_weapon` ... `selected_item` | `SelectedWeapon` ... `SelectedItem` | A number, or `none` |

Other fields belong to features: Guru travel's `gurus` (the visited Gurus,
town names), recharging items' `recharge.wingboots`, `recharge.hourglass` and
`recharge.elixir` (see its entry in *Features and what they use*), and
`imported_mantra` on a file imported from a mantra. Missing
core fields take a new game's values. HP, MP and the timed items are not
kept: continuing fills HP and MP and clears the timed items, as a mantra did.

**The file select.** A hook at `StartScreen_CheckHandleInput` runs it instead
of the title's own input (see The title screen), and the file select is drawn
over the picture below the logo. Up and Down move, A or Start take a file:
an empty one starts a new game through the stock Start path (nothing is
written until the first save), a saved one continues. "Erase a file" picks a
file, then asks "Erase file N? A yes B no"; B steps back.

**Continuing** takes the stock Continue path: the hook at
`PasswordScreen_Show` puts the file's progress where `Password_Load` would and
skips the mantra screen, `Player_SetInitialExpAndGold` runs (Death keeps gold
lets it, since it is the start screen's call), and the hook at `$FC95`
(`JMP Player_Spawn`) puts back the exact gold, experience and next title.
`Player_Spawn` does the rest: full HP and MP, the timed items and the Mattock
quest bit cleared (as after a death), and the temple at `TempleSpawnPoint`.

**A Guru's choice.** Where a Guru would show the mantra (`$A5FE`), after a new
title (`$A602`) and at Eolis's Guru with the Ring of Elf (`$A0C8`), with a
file in play, the interpreter goes to a step in unused ROM (bank 12,
`$AE10`):

- If Guru travel has another Guru to offer: the Buy/Sell menu, showing
  "Remember my / journey. / Send me to / another Guru." (message `$C3`
  through the operand override). The first saves; the second starts Guru
  travel at its intro, then the towns at once; B leaves.
- Otherwise: the question "Shall I remember / your journey?" (action `$16`).
  A saves; B leaves.
- Saving makes this Guru's temple `TempleSpawnPoint`, raises the furthest
  temple, writes the file, and shows "I will remember / your journey."
  (through Guru travel's SAY step, or Save files' own at `$AE15`). Then, if
  Guru travel has somewhere to offer, its intro and offer follow.
- **The spawn point moves only on a save** (#43). `SET_SPAWN_POINT` (`$06`)
  still runs, so Guru travel meets the Guru there, but Save files puts
  `TempleSpawnPoint` back at the next action and keeps the raise as the
  furthest temple.

Without a file in play (Save files off, or turned on in the middle of a game)
the Gurus show the mantra as before.

Its lines come after the Guru's own, in the same box, and a new title's line
scrolls: while they (and Guru travel's after them) are up, the hook at
`$F439` keeps the box's line scroll offset (see The text box).

**Saving on respawn.** The hook at `$DB20` (after `Game_SpawnInTemple` in
`Player_Spawn`) saves the file at every respawn: after a death, a Guru travel
journey and continuing. It keeps the spawn point, so quitting never loses more
than the last stretch, and only a Guru moves where the file continues.

**Importing mantras.** The first time the title shows with Save files
(`faxanadu_imported.txt` absent), the mantra in `faxanadu.srm` goes into file
1 (or the first empty file), with the Gurus in `faxanadu_gurus.txt`. After
that, a mantra not imported before goes into the first empty file: a new one
in `faxanadu.srm` (which the launcher's Mantra field edits) or `--password`.
`faxanadu_imported.txt` lists the mantras imported, so each goes in once;
delete it to import again. A mantra is decoded by the game's own routines in
an isolated call (see The mantra), so it gets the title's gold and experience,
as continuing from it did; its spawn point is also its furthest temple, and
that Guru counts as visited. With Save files on, `faxanadu.srm` is no longer
written every 15 seconds.

**For other features: `fax_save.h`.** Keep your state in the file in play:

```c
#include "fax_save.h"

static void on_save(FaxSaveEvent event) {
    if (event == FAX_SAVE_WRITING) fax_save_set_int("elixir.charge", charge);           /* about to be written */
    else if (event == FAX_SAVE_CONTINUED) charge = fax_save_get_int("elixir.charge", 0); /* a file continued */
    else charge = 0;                                                                     /* FAX_SAVE_NEW_GAME */
}
NES_MOD_CONSTRUCTOR(register_elixir) { if (!fax_save_listen(on_save)) abort(); }
```

- `fax_save_get(key)` / `fax_save_set(key, value)` (and `_int`) read and
  write a field of the file in play; `NULL` removes one. Set returns false
  with no file in play, and for the core fields above. Name keys after the
  feature: `elixir.charge`, `block.tutorial`, `zone.checkpoint`. Keys are
  `[a-z0-9_.-]`, up to 31 characters; values printable ASCII, up to 95; a
  record holds 64 fields.
- A value set is written at the next save (a Guru's or a respawn's). Set it in
  `FAX_SAVE_WRITING` from your own state, rather than when it changes, and
  read it back in `FAX_SAVE_CONTINUED`: a save state does not reload the
  file's fields into your feature (it rereads the file into the record, but
  sends no event), so your own save-state record stays the authority while
  playing.
- `fax_save_enabled()` says Save files is on; `fax_save_file()` gives the file
  in play (1-3), or 0 on the title, without a file, or with Save files off.
  Without a file, keep your state as you did before (a sidecar, or nothing).
