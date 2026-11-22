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
| `$00BA`, `$00C2` | `CurrentSprites_XPos_Full`, `CurrentSprites_YPos` | Each sprite slot's box: its left, and its top under the HUD |
| `$00DB-$00DD` | `IScriptOrCHRAddr`, `IScriptOffset` | The interpreter reads the next action at addr + offset |
| `$0100` | `CurrentROMBank` | 14 in the main loop. The stack shares this page |
| `$0120` | `Game_PausedState` | Nonzero while the game is paused: Start, or the map (see *The map*) |
| `$0127` | `SoundEffect_State_Counter` | The playing sound effect's tick; the typing sound runs 1-4 |
| `$0200-$021D` | `Temp_0200` on | `Screen_LoadBlocksHoriz`'s column of tiles, over the text box's variables: after a screen is drawn or scrolls sideways, every one of them from `$0208` to `$021D` holds a tile of it (see *The text box*) |
| `$0208-$020B` | `TextBox_X`, `TextBox_Y`, `TextBox_Width`, `TextBox_Height` | The box last opened or closed, in tiles (see *The text box*) |
| `$020C` | `TextBox_Closed` | 0 while a box or the menu is open, 1 once it has closed; but a screen's tile, which can be 0, once a screen has been drawn or scrolled (see *The text box*) |
| `$0212` | `TextBox_PlayTextSound` | The typing sound plays for a character if set |
| `$0213` | `MessageID` | 0 once a message has typed out; otherwise stale after a conversation (a screen's tile, once one has been drawn), and still stale (with `$0215`) as the next script's portrait opens, until its first message loads |
| `$0214` | `Message_ProcessedLength` | Bytes of the message read, from an offset, not 0. Wraps with the address |
| `$0215` | `TextBox_MessagePaused` | Nonzero while a `$FC` pause waits for A; otherwise maybe a screen's tile |
| `$021D` | `TextBox_Timer` | Characters type when it is a multiple of 4. `Messages_Load` zeroes it |
| `$02CC-$02D3` | `CurrentSprites_Entities` | Each sprite slot's entity, `$FF` empty (see *What killed the hero*) |
| `$036C-$0373` | `CurrentSprites_Values` | Each slot's value from the screen's sprite list: a person's interaction script (see *Quests*), $FF none |
| `$0378` | `CurrentSpriteIndex` | The slot whose behavior or collision is running |
| `$0390`, `$0392-$0394` | `Experience`, `Gold` | Gold is 24-bit little-endian |
| `$039A` | `Player_MP` | At most 80; 0 at a new game (see *MP and its bar*) |
| `$03C7` | `IScript_PortraitID` | The portrait showing, `$FF` for none (see *Portraits*) |
| `$0427`, `$0428` | `DurationOintment`, `DurationGlove` | Seconds left, `$FF` when not running; an Ointment or Glove lying in the world is not there while its kind runs (see *Items in the world*) |
| `$042C` | `SpecialItems` | Bit flags; `$08` the Elixir, which saves a death once |
| `$042D` | `Quests` | Bit flags (see *Quests*) |
| `$0431`, `$0432` | `Player_HP_U`, `Player_HP_L` | |
| `$0438` | `PlayerIsDead` | Set it to 1 and the main loop runs the death path |
| `$0439` | `TempleSpawnPoint` | Where death respawns and a mantra continues, 0-7. In the original, the furthest temple reached; with Save files, the temple of the Guru last saved with |
| `$043A` | `Screen_TrackedVisitCount` | The one-visit-in-four items' counter (see *Items in the world*) |
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
| `$03` | msg | Message. B, at any point, ends the script (`IScriptAction_ShowMessage` jumps to `IScriptAction_EndScript`) |
| `$04` | addr | `CHECK_UPDATE_PLAYER_TITLE`: if `NextPlayerTitle` differs, take it and jump; else go on |
| `$05` | amount (16-bit) | Pay (`IScriptAction_SpendGold`, `$835B`): reads the amount into `Temp_0202/3`, copies it to `Temp_Int24`/`_M` (`$8367`) for `IScripts_CheckEnoughMoney`; too little, "This is not enough Golds." (message 3) and the script ends; else `IScripts_ProgressivelySubtractGold` takes it, 10 Golds a frame, then holds 60 frames (*Gold*, "Taken over time") |
| `$06` | temple | Set spawn point. **Only raises** `TempleSpawnPoint` |
| `$07` | item | Add an item (`IScriptAction_AddInventoryItem`, `$839F`): its `IScripts_LoadByte`, then at `$83A2` `PHA`, `PHA`, `Player_AddToInventory` (no room check: a full list loses its last entry), and the item's picture and name in the dialogue box, with the blinking ▲, until A (only A: `BPL` on `Joy1_ChangedButtonMask`); then the next action. Entered at `$83A2` with the item in A, it gives any item: the Potion Sellers do |
| `$08` | items | A shop's buy list (see *Shops*) |
| `$0F` | addr | The shops' Buy/Sell menu: message `$10`, then a cursor on its lines 1 and 3. A on the first choice jumps to addr, A on the second goes on to the next action, B ends the script |
| `$0A` | MP | Add MP (`IScriptAction_AddMP`, `$8580`), 1 at a time with its sound |
| `$13` | HP | Add HP (`IScriptAction_AddHP`), the same way |
| `$11` | items | A shop's sell list |
| `$14` | | Show the mantra (`IScriptAction_ShowPassword`): encodes it, types it a character a frame, waits for A |
| `$16` | msg, addr | Question: A jumps to addr, B goes on to the next action. Unused by the game, works: Guru travel's ASK step and the Magician's lesson ask with it |
| `$17` | addr | Jump |

The full table is in chipx86's notes. The Gurus' scripts:

- Each temple Guru: `06 temple`, `04 NEW_RANK`, `17 PASSWORD` (Apolune's,
  `$A603`: `81 06 01 04 00 A6 17 FC A5`).
  `ISCRIPT_COMMON_GURU_PASSWORD` (`$A5FC`) is `03 22, 14, 00`, and
  `ISCRIPT_COMMON_GURU_NEW_RANK` (`$A600`) is `01 23, 00`. Conflate's
  (`$A627`) checks first: `12 23 A634` (the Battle Suit), then the same
  three at `$A62C`; with the suit, `12 82 A62C` (the Ring of Dworf) goes back
  to them, and without the ring he gives it (`01 8C, 07 82, 00`) and sets no
  spawn point. So in all seven, `04` follows `06` directly, and nothing a
  Guru says comes before `06`.
- Eolis's Guru (`$A0BC`) sets no spawn point: `12 80 A0C6` (the Ring of
  Elf), else `01 2C, 07 80, 00` (he gives it). With the ring he says
  `03 2D, 00` at `$A0C6`.
- The Guru in the Castle of Fraternal (`ISCRIPT_FRATERNAL_GURU`, building
  `$42`) has the Guru's portrait but sets no spawn point: `12` for the Dragon
  Slayer, then the Demon's Ring. Dartmoor's temple Guru is
  `ISCRIPT_GURU_FINAL` (temple 7).
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
- **A portrait moves while a line types** (`MessageID` set, `TextBox_MessagePaused`
  0): `IScripts_UpdatePortraitAnimation` picks the eyes and mouth frame
  `(InterruptCounter >> 3) & 3`. Typed out or paused, it shows frame 0. A
  picture of a line mid-typing is the same every run only from a save state
  with `InterruptCounter` set, played headless.
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
"I sell tools." ($0E, $0F). Nobody names the nurse or the meat salesman;
with the remaster he sells Red Potions, so Speaker names calls him the Potion
Seller.

**Eolis's buildings, for tests** (`EOLIS_DOOR_LOCATIONS`; a door's block is
`YX`, so x = (block & 15) × 16):

| Building | Kind | Door | NPC (`YX`) |
|---|---|---|---|
| `$00` | Meat shop | screen 1, block `$93` | `$84`, behind the counter: walk left until stopped |
| `$01` | House | screen 2, `$9E` | |
| `$02` | Temple | screen 2, `$95` | Guru `$82` |
| `$03` | Key shop | screen 3, `$9D` | `$84` |
| `$04` | Tool shop | screen 3, `$9A` | `$73`, behind the counter (stops at x=80) |
| `$05` | Magic shop | screen 6, `$9C`, Ring of Elf | `$71`, on a step at the left wall: walk left, then jump left onto it (Left and A); beside him x is at most 32 |
| `$06` | Martial arts | screen 6, `$9A`, Ring of Elf | `$87`, mid-room: stop at x 124 |
| `$07` | King's room | screen 7, `$98`, Ring of Elf | `$72`, atop stairs: jump up them (Left and A) |

The King's guard (`ISCRIPT_EOLIS_KINGS_GUARD`, no portrait) stands at `$9A` on
screen 7, two blocks right of the King's door. The Eolis Guru gives the Ring
of Elf (`SpecialItems` `$80`) the first time; the King gives 1500 gold only to
a hero with none, as often as he comes with none (3000 with the remaster,
once; see *Gold*).

## Messages (bank 13)

`Messages_Load` finds message N by counting `$FF` terminators from
`MESSAGE_STRINGS` (`$8300`). There is no lookup table. Messages 1-193 end at
`$B3xx`, followed by glyph data that contains no `$FF`, so the scan counts the
glyph block as message `$C2`, ending at the first unused byte, `$B45A`. A new
message therefore starts at `$B45B` with ID `$C3`. The remaster's slot there
is 48 bytes (`$B45B-$B48A`, all the override budget allows), so a line of
its own is at most 47 bytes, newlines included, before its `$FF`: four full
rows of 16 do not fit. The Elixir lines were cut to fit ("Return whenever it
is used." did not).

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
- **The ▼ and "?" flash on `InterruptCounter`:** they are drawn only while
  `InterruptCounter & $10` (`TextBox_DrawDownArrowTerminatorSymbol`), 16
  frames on and 16 off, so a screenshot shows them on some runs and not
  others. In a portrait conversation they sit at about x 144-175, y 176-184
  of the picture; a box without a portrait (Eolis's exit door) flashes a ▲ at
  its bottom right, x 216-231, y 92-103. `tools/readme_screenshots.py`'s
  `shot_lit` retakes a screenshot until the mark is lit.
- The box is waiting for a button when `MessageID` is 0 (typed out) or
  `TextBox_MessagePaused` is set. Wait on that rather than on a timer.
  Only a pause sets `TextBox_MessagePaused` ($FF), and only continuing past
  one (or the mantra box) clears it, so it can hold a stale nonzero value: $9F
  all through a new game's intro, a tile of the gate's screen (see
  `TextBox_Closed` below). For a line with no pause, wait on `MessageID`
  alone.
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
  palette. Not every ASCII character has a glyph: `#`, `$`, `%`, `&`, `(`,
  `)`, `*`, `+`, `/`, `:`, `;`, `<`, `=`, `>`, `@` and `[` to `^` are blank.
  Some are shapes instead: `~` is a triangle pointing left and `}` one
  pointing right (the map's arrows), `` ` `` a solid block, `{` and `|` bars,
  `_` an underline.
- `TextBox_Open` frames a box with background tiles 1 (top left), 2 (top and
  bottom), 3 (top right), 4 (sides), 5 (bottom left) and 6 (bottom right)
  around tile 0 (black), from the playfield's background pattern table.
  `UI_DrawHUDSprites` copies those (60 tiles in all) from bank 10,
  `HUD_TILES_START` (`$8140`), to PPU `$1000`, so tile t is at
  `$8140 + t * 16` in ROM; `fax_text_box_rom` draws the frame from there on
  screens whose pattern tables hold something else, like the title.
- **`TextBox_Closed` (`$020C`) reads 0 with no box up on some screens.** It is
  0 while a box or the Select menu is open and 1 once one has closed, and only
  `TextBox_Open` and `TextBox_Close` store it by name, but the screen's tiles
  go through it. `Screen_LoadBlocksHoriz` (`$D503`) loads the next column of
  tiles a screen scrolls in, a top and a bottom tile from each of 15 blocks,
  into `$0200-$021D` (chipx86's `Temp_0200` on), and in the NMI
  `Screen_WriteBlockTilesColumnToPPU` (`$D673`) writes the first 26 down the
  nametable, rows 4-29. Every sideways scroll goes through it, and so does
  every screen the game draws: `Screen_SetupNew`'s `Area_ScrollScreenRight`
  (`$D0F6`) scrolls the new screen in from the right a column at a time. So
  after a door, a building, a respawn or a scroll, `$0200-$021D` hold the last
  column loaded (for a screen drawn whole, its right edge, column 31, from row
  4 down), over `TextBox_X` to `TextBox_Height`, `TextBox_Closed` (row 16),
  `MessageID`, `TextBox_MessagePaused`, `TextBox_LineScrollOffset` and
  `TextBox_Timer`. Opening a box and loading a message set them again, so the
  game is none the worse. `TextBox_Closed` is then row 16's tile: usually one
  of the area's ($80 and up), but on Forepaw's screen 2 (area 3) it is tile 0,
  so there it reads 0 with no box up (Eolis's gate leaves `$9F`, its town
  `$94`). Measured from save states: after walking out of a building there,
  and in the gate, town, meat shop and Zozura fixtures, `$0200-$0219` equal
  nametable column 31, rows 4-29. **Never take 0 alone for an open box.**
  `fax_text_box_showing` (`fax_text.c`) also wants a box's frame in the last
  frame: a top-left corner (tile 1) followed by the top edge (2, or 3 for a
  box two tiles wide), below the HUD. Below the HUD the screen shows tile 0
  and the area's tiles, which `Area_LoadTiles` (`$CEB8`) puts at PPU `$1800`
  or `$1A00` (tiles $80 and up), so those two tiles side by side are a box's.
  It looks for any box's corner, not just the one at `TextBox_X` and
  `TextBox_Y`: a submenu sets those a frame before its frame shows, over the
  menu's.
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
  (`fax_text_shown_tile`, on `cyc_render_nametable` and
  `cyc_render_line_scroll_x/y`; `fax_speaker.c`, `fax_text_box_showing`).
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
- **So a new A turns a page on the frame the page ends, and only then or
  after.** The continue check runs after that frame's character, so A first
  pressed on the frame that types the `$FC` or the end goes straight on. A
  frame that types a character leaves `TextBox_Timer` a multiple of 4, and
  the next three type none at the game's pace (the remaster's Hold A leaves
  the frame A goes down at that pace): A first pressed on one of them is
  held from the next, and the page types to its end and waits.
  `fax_e2e.hurry_page()` saves a state on such a frame and loads it with A
  down, which then goes down on the very next frame. Measured on the Guru's
  greeting: A pressed so is new on that frame (`Joy1_ChangedButtonMask`
  `$80`, the timer at the game's pace), then a character every frame to the
  end. While the box opens (the lag frames after `Messages_Load`, timer 0)
  the pad is not read, so A pressed then is first seen with the first
  character's timer at 1, safe the same way.
- The typing sound (sound effect 1, noise) lasts 4 ticks: 2 of a soft hiss,
  then 2 of a sharp clack. At the game's pace each character starts it just
  as the last one ends. Started every frame, it never gets past the hiss.
- **The clack is a bug** (chipx86). `SoundEffect_Message_OnTick` (`$86A8`,
  bank 5) writes `$1F` to `NOISE_VOL`, then `LDA SoundEffect_State_Counter`,
  `AND #$02`, `TAX` (`$86B2`), `LDA SOUNDEFFECT_MESSAGE_NOISE_LO,X`,
  `STA NOISE_LO`, and increments the counter (at 4, `NOISE_VOL` `$10` and
  the effect ends). The table (`$86D1`) is two bytes, `$10` and `$01`, so
  ticks 3 and 4 read index 2, past its end: `$A9`, the opcode of
  `SoundEffect_HitEnemy_Setup`'s `LDA #$14`. `NOISE_LO` is the noise
  channel's mode (bit 7) and period, so `$A9` is the short, metallic mode at
  period 9; `$01`, the table's own, is a high hiss. An `AND #$01` that became
  `AND #$02` to switch every other tick, with no padding byte, would explain
  it. Measured with the remaster off: `NOISE_LO` reads `$10`, `$10`, `$A9`,
  `$A9` for each character. Small fixes halves A at the `TAX`, so ticks 3 and
  4 read index 1: `$10`, `$10`, `$01`, `$01` (`tests/fixes/small_fixes.py`
  reads the APU's registers from a save state each frame: the chunk `APU ` is
  nesrecomp's `HwApu`, whose `reg[16]`, `$4000-$400F` as last written, starts
  at its byte 31).
- After `Messages_Load` sets `MessageID`, the first `TextBox_ShowNextChar`
  comes about 12 frames later, while the box opens. Those are lag frames, so
  the pad isn't read either. Time typing from the first character, not from
  `MessageID`.
- The mantra types a character a frame through its own
  `PasswordScreen_ShowNextChar`, not `TextBox_ShowNextChar` (see *The
  mantra*, *How a Guru shows it*).

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
  64. Block, the quest log, Recharging items' Elixir lines, the Magician's
  lesson and the Potion Sellers borrow Guru travel's SAY step and slot (the
  lesson and the Potion Sellers its ASK step too), and Quick shops and Double
  gold take none. `tools/check_allocations.py` counts every byte claimed in
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
  and keeps its effects. **The registers it returns with become the CPU's,**
  so a call made anywhere but a site that handles its routine saves them
  (`cyc_mod_regs`) and puts them back (`cyc_mod_set_regs`) after: P too, or a
  `BEQ` that was about to test the interrupted code's Z goes the other way.
  A routine that waits on the hardware (`PPUBuffer_WaitForCapacity`, a frame
  wait) never returns in it, since no NMI runs, so the call fails and nothing
  is committed: test what it would wait for first.
- **At a frame's end** (`frame_end`, at the start of VBlank, before the NMI)
  `cyc_mod_regs` gives the CPU's registers where the frame left it, and the
  stack at S + 1 says which `JSR` it is in: MP refill tells a frame the game
  played from S and the return address there (see *MP and its bar*), and
  gives MP from there with a committed call (`Player_AddMP`).
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
- A TCP `action` sets the button's hold to its frames, replacing what was
  left of it: a 1-frame `action` lets go early, and one sent before a hold
  lapses keeps the button down (`hold()` in `tests/e2e/fax_e2e.py`).
- TCP `action` returns at once and holds the button for its frames, so two
  `action`s sent back to back are held together: `left` then `a` jumps left
  (`tests/speaker/speaker_names.py` climbs the King's stairs that way).
- TCP `key` (an SDL key name) and `pad` reach the game as window events, as
  a real key or controller does, so the host extras' `event` sees them. The
  window keeps `config.ini` beside the executable and writes it at first
  start; its `[Game]` keys go to the host extras' `load_setting`, and
  `save_settings` writes them back whenever the window saves.
- `--uncapped` (with `--exit-after N`) runs the window as fast as it can:
  about 250 fps alone on mango (an HX 370 held to 2 GHz on battery in the
  power-saver profile), and about 125 fps a game with 24 at once, two to a
  core. The cycle-accurate machine is the limit: of a window's frame, about
  61% is the PPU, dot by dot, 9% the APU, 12% the CPU, and 14% presenting the
  picture (SDL's scaling blit, even to the dummy driver). Headless runs
  (`--frames`) skip presenting but are no faster under load: 24 at once made
  2,600 frames a second, 24 windows 3,000. An uncapped window takes a whole
  thread from launch to close, whatever its script is doing.
- **The window's scale costs a tenth of a frame.** `config.ini`'s
  `[Display] WindowScale` (3 by default) sizes the hidden window, and
  presenting scales the picture to it every uncapped frame: a frame in
  Eolis's town is 74 million instructions at scale 3 and 67 at 1 (`perf stat`,
  one window alone). A window starts from a `config.ini` holding only that
  key and writes the rest. Seamless's picture is a fixed 424x240 whatever the
  window (#107), so at scale 1 its window is the picture's own size. When it
  fitted its picture to the window, it needed `--world-size 426x240` for
  that: 70 million instructions a frame at scale 1 against 82 at 3.
- **The host scales a game's picture; the game cannot.** The picture a
  game's `present` returns becomes a texture of its size, drawn with
  `SDL_RenderSetLogicalSize` at that size, so SDL fits it to the window with
  bars. The player's `[Display]` settings choose how: `IntegerScale` (on by
  default) takes the largest whole multiple that fits, once the window is at
  least the picture's size, and `LinearFilter` (off) filters the whole
  texture. So Seamless's 424x240 is 4x (1696x960) at 1080p, and 3x, 6x and
  9x fill 720p, 1440p and 4K. The game has no hook between the texture and
  the window, so a two-filter scale such as sharp bilinear (nearest up to a
  whole multiple, then linear down) needs the host to change. Nor can a game
  hand over a picture already scaled to the window: the host also sizes the
  window as the picture times `WindowScale` on F11 and on every Display
  change (`apply_settings` in `cyc_sdl.c`), and every screenshot of the
  `picture` layer would be window-sized.
- In the hidden test window (`SDL_VIDEODRIVER=dummy`, so SDL's software
  renderer), the `ui` layer of a TCP `screenshot` (what the window presents)
  shows the scaled picture at the window's top left, not centred, in the
  stock view as in Seamless (`tests/world/fixed_view.py`). TCP `window_size`
  resizes that window, and the host follows it as it does a real one.
- **24 windows run faster from one program file than from 24 copies.** Each
  copy is its own page cache, so its own copy of the code in the L3: from
  hard links to one file, 24 windows ran 10% more instructions a cycle (2.75
  against 2.44). Under that load a frame costs about 2.3 times the cycles it
  does alone (IPC 2.3 against 4, at 2.5 GHz all-core), with the same
  instructions: the suite's frames cost what bare games' do; it is SMT and the
  clock, not the scripts (3% of the CPU).
- The window's exit line, `window benchmark: ... frames=N`, gives the
  machine's frame counter, which a loaded save state carries; `native_cycles`
  is the CPU cycles this launch ran, so `native_cycles / 29780.5` is the
  frames it played.
- A headless run writes the mod selection back to its `--mods-root` as it
  starts, so runs at once need a root each (else "mods: cannot publish mod
  state").
- A save state's RAM is in its `HW  ` chunk at offset 23, 2 KiB, with no
  checksum. Edit it to set up test fixtures (`fixture()` in
  `tests/e2e/fax_e2e.py`).
- A site at a **branch** sees the flags the instruction before set, and can
  steer the branch by changing them (and the register they came from) with
  `cyc_mod_set_regs`, then returning 0. Forgiving jumps does this twice, so
  the game's own code does the rest.
- Each TCP command is served at a frame boundary: on a kept-open connection,
  at the next; a new connection a command costs two frames, since the server
  takes one client at a time and drops it at the poll after it closes. The
  server keeps reading while lines arrive, so a quick client can be served
  several commands in one frame. To time something to the frame over TCP,
  read `InterruptCounter` with the state in one `read_ram`. A save state's
  frame, which a headless run from it continues, is the `uint32` at offset 28.
- **Acknowledge replies at once.** `cyc_tcp.c`'s `send_line` sends a line and
  its newline apart, and Nagle's algorithm holds the newline until the client
  acknowledges the line, which a delayed ACK puts off by up to 40 ms on Linux:
  on a kept-open connection a reply took about 3 frames at normal speed.
  `TCP_QUICKACK` on the client, set before each read, acknowledges at once
  (where there is none, any byte the client sends does; the server ignores an
  empty line), and the newline follows. `tests/e2e/fax_e2e.py` keeps one
  connection a game that way, a frame a command. Commands sent together are
  served at the same frame boundary, so a `read_ram` sent with others is one
  moment with them (`tests/block/block_shield.py` reads Block's glint this
  way). Read a kept-open socket buffered: a 4 KB reply a byte at a time is
  slower still.
- The extras' TCP `wait` (`src/cyc_extras.c`) answers once `frames` more
  frames have run, at the end of the last one: a frame-exact wait without
  polling. One runs at a time; the commands sent after it in the same write
  are served at once, not after it.
- **A press on a loaded state's first frame or two can go unseen.** From a
  state saved over TCP outside a building, Up held from the state's first
  frame for 4 frames did not go through the door, and from its third did
  (from its second, with Seamless). `fax_e2e`'s `batch()` lets two frames run
  after a load that ends a batch; `forgiving_jumps.py`'s A in the same write
  as its load is seen.
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
- **A headless picture with the remaster's drawing.** `--screenshot` saves
  the bare NES picture; `--present-out FILE` saves what the window would show
  after the last frame, with the remaster's drawing on it (banners, damage
  numbers, name tabs). `--load-state` makes a run headless, and Block and the
  Magic button come from SDL key and controller events, which a headless run
  has none of, so those can only be pressed over TCP.
  `tools/readme_screenshots.py`'s `Game.replay` wraps a headless run.
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
- The remaster plays in Seamless, so the hero's routines can run in an
  isolated scope where `cyc_mod_isolated()` is true. A hook that keeps state
  must leave it alone there.
- In Seamless a neighboring room's actors keep moving before the hero walks
  in, chasing a copy of him held at that room's edge, so where they are when
  he arrives depends on how long he took (Eolis $04's Zozuras gather at its
  right edge and knock him about as he crosses). A test that walks in should
  put them in place in its save state, as `tests/damage/damage_numbers.py`
  does, or come in by a door, whose load spawns them fresh, as the
  `eolis-zozuras` fixture does. In Seamless the walk itself has no scroll: a
  headless schedule that holds a direction through the original's scroll
  carries the hero rooms further.
- In Seamless, a neighboring room's actors live in `fax_world.c`'s copy of
  that room, not in `CurrentSprites_Entities`: setting those slots to `$FF`
  clears only the hero's room.

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
`Password_EnteredCharsLength` (`$0666`) counts the characters typed
(`Password_EnteredChars`, `$0600`, holds their 6-bit values), and
`Password_MaxChars` (`$0665`) is set once the screen is up. Rows 0-4 hold the
64 characters in order, 13 to a row (`PASSWORD_VALUES_FOR_CURSOR_POS`: row ×
13 + column; the 65th spot is empty), and A types the one under the cursor,
which stays put. The last row (5) holds →, ←, DEL and END at columns 0-3, and
Up from row 0 wraps to it. A on END (row 5, column 3) checks the mantra and
continues. A new game's mantra at Eolis's gate is `rFAAAAQAAA`
(`tests/dx/one_switch.py` types it with the remaster off).

## Gold

`Gold` (`$0392-$0394`) is 24 bits, little-endian. Four routines write it:

| Routine | Where | Does |
|---|---|---|
| `Player_HandleTouchCoin` | `$8B9A`, bank 14 | A coin touched: sound 9, then `Gold` + the coin's `CurrentSprites_Values` byte, carried into `Gold_M` and `Gold_U` and **not capped**: past `$FFFFFF` it wraps around to 0 and up |
| `Player_AddGold` | `$F9C1`, fixed | `Gold` + `Temp_Int24`/`_M` (16 bits; the top byte of the add is 0), **held at `$FFFFFF`** when it carries out: the game's cap. Only `IScripts_ProgressivelyAddGold` calls it |
| `Player_SubtractGold` | `$F9A5`, fixed | `Gold` − `Temp_Int24`/`_M` (16 bits; the borrow carries into `Gold_U`), **not floored**. Only `IScripts_ProgressivelySubtractGold` calls it, 10 a frame (a purchase, and scripts' `$05`), after the gold has been checked |
| `Player_SetInitialExpAndGold` | `$95B1`, bank 12 | The title's starting gold, after a mantra or a death (see *Titles, experience and gold*) |

Each ends in `UI_DrawGoldValue` (`$F9E7`), which draws **7 digits**
(`UI_DrawDigitsZeroPadded`, Y = 7): past 9,999,999 the HUD shows the low 7,
so the cap, 16,777,215, reads `6777215`. Nobody gets near either.

**Paid over time.** `IScripts_ProgressivelyAddGold` (`$8537`, bank 12) pays
the 16-bit amount in `Temp_0202/3` 10 Golds a frame (each turn waits a frame
in `IScripts_UpdatePortraitAnimation`), with the gold sound every fourth
frame, then waits 60 frames: once the amount is 0 it sets bit 15 (`DEC
Temp_0203` to `$FF`) and counts `Temp_0202` down from 60. So an amount must
be below `$8000` and a multiple of 10 (*Shops* says what one that is not
does). A paying turn takes 10 from `Temp_0202/3` (`SEC`, `SBC #$0A`, `SBC
#$00`), then puts the add in `Temp_Int24` (`LDA #$0A`, `STA Temp_Int24`,
`LDA #$00`, `STA Temp_Int24_M`) and calls `Player_AddGold` at `$8560`, which
adds `Temp_Int24`/`_M` and draws the gold; then `JMP` back to the top. So
1500 takes 150 frames (2.5 s) of counting. Two callers:
`IScriptAction_AddGold` (`$8525`, action `$09`), which reads the amount from
the script and calls it at `$8531`, and the sell list
(`IScriptAction_ShowSellMenu`, half the shop's price).

**Taken over time.** `IScripts_ProgressivelySubtractGold` (`$8321`, bank
12) is the add's mirror, without the sound. Each turn calls
`IScripts_UpdatePortraitAnimation` (a frame), then: with bit 15 of
`Temp_0202/3` clear and something left, it takes 10 from it (`$832E-$833E`:
`SEC`, `SBC #$0A`, `SBC #$00`), puts 10 in `Temp_Int24`/`_M` (`$833F-$8346`)
and calls `Player_SubtractGold` at `$8347` (`20 A5 F9`), then `JMP $8321`.
With nothing left, `DEC Temp_0203` (to `$FF`) and 60 into `Temp_0202`
(`$834D`), and from then each turn takes 1 from `Temp_0202` until 0, then
`RTS`. So an amount of n takes n / 10 frames of counting, a frame each, then
60: 3000 is 300 + 60 (measured, the magic hall's Fire). The amount must be
below `$8000` and a multiple of 10 (what one that is not does: *Shops*). Two
callers: `IScriptAction_SpendGold` (`JSR` at `$8376`, action `$05`), from a
script, and the buy list (`IScriptAction_OpenShop`, `JSR` at `$8460`).
During a `$05`, `IScriptOrCHRAddr` + `IScriptOffset` stands past its amount,
at the next action; during a purchase, in the shop's script, past the `08` and
its list's address.

**`IScripts_CheckEnoughMoney`** (`$87DC`) compares 24-bit `Gold` with the
16-bit `Temp_Int24`/`_M` and returns the carry: `Gold_U` not 0 is enough
(`CMP #$00`); else `Gold_M` against `Temp_Int24_M`, and only if they are
equal `Gold` against `Temp_Int24`. That is a correct compare, so any amount
up to `$7FFF` works: 29999 is short of 30000 and 30000 is enough (checked
with the Magician's lesson).

**`CHECK_GOLD`** (`IScriptAction_IfPlayerHasGold`, `$861F`, action `$0D`) is
`LDA Gold`, `ORA Gold_M`, `ORA Gold_U`, `BNE @_hasGold`; with no gold,
`JMP IScripts_SkipAddrAndInvoke` at `$862A` (two `IScripts_LoadByte`, then
`IScripts_InvokeNextAction`), and with any, `JMP IScripts_JumpToNextAddr` at
`$862D`. At either, `IScriptOrCHRAddr` + `IScriptOffset` is the address
word: `IScripts_InvokeNextAction` has read the action byte.

**The King** is the only `ADD_GOLD` in the game. `ISCRIPT_EOLIS_KING`
(`$A350`, bank 12, script `$08`) is `80`, `0D A35A` (`CHECK_GOLD`: any gold
jumps), `01 35` (his plea, which B cannot skip), `09 DC 05` (1500), `00`; at
`$A35A`, `03 36, 00` ("There is nothingmore I can do tohelp you.", then
"Looking forward/to good news."). So he gives to any hero with exactly 0
gold: again each time the gold is spent to 0. At his `CHECK_GOLD`, the
script stands at `$A352`; at the action's `JSR`, `IScriptOrCHRAddr` +
`IScriptOffset` is `$A359`, his `END`. The plea's message action
(`IScriptAction_ShowUnskippableMessage`) ends only on a new press of A once
the message has typed out (`TextBox_CheckShouldContinue`: `MessageID` 0 and
A in `Joy1_ChangedButtonMask`), and the count pays its first 10 the frame
after. The plea (message `$35`, `$8BB6` in bank 13) names the amount: "You
are our lasthope./I shall give you1500 Golds.", its digits at
`$8C81-$8C84`. The quest log's line for The King's Plea ("Bring the water
back to the Elf fountain, our life source.") names none. The log gives that
quest at either of his lines, `$35` or `$36`, and at `$4C` (the man walking
on Forepaw's screen 3), so The King's Plea given means he has spoken, not
that he has paid: a hero who comes with gold hears `$36` first.

**Coins come only from monsters.** No screen's sprite list (bank 11) places a
coin (`$02`) or bread (`$01`). A monster that dies by weapon or magic becomes
a death effect: `Sprite_SetDeathEntity` keeps its entity in
`CurrentSprites_InternalBehaviorStates` and puts `$13` in its slot, or `$64`
(the boss death) for a boss (`SPRITE_CATEGORIES_BY_ENTITY` 7). Its behavior
calls `Sprite_HandleDeathDropIfPossible` (`$AC21`): `SpriteBehavior_EnemyDeath`
once, 8 frames on, then empties the slot; `SpriteBehavior_BossDeath` at each
of its phases 1-4, so a boss leaves up to four. A drop needs a slot free
(`Sprites_HasMaxOnScreen` leaves the first in Y), so a monster that dies on a
screen with all 8 slots taken leaves nothing. `Sprite_RunDeathDropHandler`
(`$AC2D`) then looks the entity up in `SPRITE_ENTITY_DROP_VALUES_INDEX`
(`$B672`): `$FF` or `$40` and up, nothing; otherwise the index picks the
value in `SPRITE_DROP_VALUES` (`$ACED`), which goes in the drop's
`CurrentSprites_Values`, and the drop is a coin below `$30`
(`Sprites_ReplaceWithCoinDrop`, `$AC75`, sound `$10`) and bread from `$30`
(`Sprite_ReplaceWithBreadDrop`, `$AC9F`; its value is HP, through
`Player_AddHP`). Nothing is random: a kind of monster always leaves the same.
A coin bounces and then goes (`SPRITE_BEHAVIOR_BOUNCE_AND_EXPIRE`). Touching
it runs `Player_HandleTouchBreadOrCoin` (`$8968`), which empties the slot and
jumps to `Player_HandleTouchCoin`. chipx86's comments on the index table are
one off from `$31` on: his "King Grieve" is Zorugeriru (`$31`), his "Zozura"
the unused eyeball (`$46`), and his "Item: Glove" the Zozura (`$47`).

What each monster placed in the game leaves (entity, then Golds for a coin,
HP for bread; a boss up to four):

| Leaves | Monsters |
|---|---|
| A coin | Hornet `$07` 53, Raiden `$04` 56, Bihoruda `$08` 56, Monodron `$2A` 59, Geributa `$17` 78, Necron Aides `$05` 90, Fire Giant `$0E` 90, Nash `$0D` 98, Grimlock `$19` 98, Snowman `$0C` 100, Sugata `$18` 100, Ikeda `$26` 100, Charron `$15` 115, Giant Bees `$1A` 115, Ishiisu `$0F` 120, Tamazutsu `$2C` 120, Giant Strider `$1E` 128, Sir Gawaine `$1F` 150, Maskman `$20` 180, Wolfman `$21` 190, Magman `$23` 200, Execution Hood `$10` 230 |
| Coins (a boss) | Zorugeriru `$31` 20, Borabohra `$2F` 56, Rokusutahn `$11` 120, Pakukame `$30` 128, Ripasheiku `$2D` 180, Zoradohna `$2E` 220 |
| Bread | Zozura `$47` 8, Yuinaru `$0B` 8, Myconid `$1B` 8, Lamprey `$28` 14, Yareeka `$22` 16, Naga `$1C` 31 |
| Nothing | Zombie `$06`, King Grieve `$32`, Shadow Eura `$33` |

So Eolis's Zozuras leave bread; the Trunk's first coin is a Monodron's.

**With the remaster** (Double gold, `fax_gold.c`) the King pays 3000 and his
plea says so, counted 20 a frame in the original's 150 frames, and only once
a game: after it, he says "nothing more" whatever the hero's gold. Every coin
pays twice its value, held at the cap; see *Features and what they use*.
Selling, the title's gold and spending are the original's, but for the
Magician's lesson (`fax_lesson.c`), whose 30000 is taken 150 a frame.

## The title screen (bank 15, bank 12)

`Game_InitStateForStartScreen` (`$DA6A`) sets S to `$FF`, initializes the
inventory and runs `Game_InitStateForSpawn`, then `Game_ShowStartScreen`
(`$FC65`) far-calls `StartScreen_Draw` and loops at `@_waitForInput` (`$FC6E`)
once a frame: `WaitForNextFrame`, `Screen_ResetSpritesForNonGame`, a far call
to `StartScreen_CheckHandleInput` (`$9F44`, bank 12), then at
`@_afterCheckHandleInputFarJump` (`$FC7A`) a check of
`Joy1_ChangedButtonMask` for Start.

- `StartScreen_CheckHandleInput` toggles `$0687` on Select (with sound `$0B`)
  and draws the arrow, sprite appearance `$AD`, at x `$34` or `$74`
  (`BYTE_ARRAY_PRG12__9f69`, loaded by `LDX` at `$9F5F`), y `$7E` (`LDY` at
  `$9F62`), through `UI_DrawPromptInputSymbol` (`$FCA7`), which ends in
  `Sprite_SetAppearanceAddrFromOffset` and its bank switch. Nothing else calls
  it, so a hook there owns the title's input: one that handles it draws no
  arrow (the loop resets sprites each frame), one that clears Select and
  returns 0 keeps the arrow and stops the toggle, and clearing Start in
  `Joy1_ChangedButtonMask` there keeps the loop from seeing it. At `$9F64`
  (`LDA #$AD`) X and Y hold the arrow's place: Save files moves it there,
  beside NEW GAME or CONTINUE. (At `$9F62`, the `LDY`, setting Y is no use:
  the `LDY` runs after the hook.)
- **The arrow's place on screen.** Its X is the screen's: the arrow lights
  columns X to X + 6 (52-58 for START at 64, 12 pixels before the word). Its
  lit rows are 34 below the Y it is given: Y `$7E` (126) lights 160-166,
  START's row. So an arrow beside text lit from row R takes Y = R - 34. The
  sprite is drawn into the shadow OAM the frame the routine runs and shows
  the frame after: a picture shows the arrow the routine drew a frame
  before.
- Start with `$0687` = 0: music 8, then `@_startGame` (`$FC98`):
  `SplashAnimation_RunIntro`, `Player_SetStartGameState` (`$9570`: everything
  zero or none, but `SelectedArmor` 0) and `JMP Game_Start`.
- With `$0687` = 1: `PasswordScreen_Show` (`$909D`, far call), then
  `Player_SetInitialExpAndGold` (far call from `$FC8F`), then at `$FC95`
  `JMP Player_Spawn`. `PasswordScreen_Show` returns only once
  `Password_Load` has succeeded, and leaves nothing else that
  `Player_Spawn` needs: skipping it (handling its far call) after putting the
  progress in RAM continues as a mantra would.
- `@_afterCheckHandleInputFarJump` (`$FC7A`) runs once a frame for as long
  as the title is up, with Save files on or off (its handled
  `StartScreen_CheckHandleInput` returns there as the RTS would), and never
  after Start: a hook there that only notes the frame tells a feature the
  title is up. DX on the title does that.
- The picture: the logo and its border fill y 16-143; START (tile columns
  8-12) and CONTINUE (16-23) are on tile row 20, lit on y 160-166, and the
  three notices on 184, 200 and 216, black between (nothing else is drawn in
  y 144-183 but START and CONTINUE). The letters are tiles
  `$E0`-`$F9`, A to Z, of the row's pattern table, and `$FA` is the
  copyright sign: `StartScreen_Draw` copies them from `CHR_LETTERS` (bank 10
  `$8240`, 27 tiles). They are drawn in one color, 3, both bit planes alike,
  and their A to Z are the dialogue font's (bank 13, see The text box), pixel
  for pixel, lit on rows 0-6 of each cell. Under FAXANADU's letters, on y
  112-127, the tree's trunk is red and green dither from x 129 to about 153,
  with black to its left: the remaster's DX sits there, centred on the logo
  at x 120-134 on y 116-122, across the trunk's left edge, with a black
  outline. With the
  remaster the original's START and CONTINUE are hidden, NEW GAME and
  CONTINUE are a column from x 96, lit on y 151-157 and 165-171, and a file
  pick takes y 160-239, a box of 10 cells over the original's choices and the
  notices. The background
  palettes are `$0F $05 $18 $38` (the logo's border; `$38`, light
  yellow, is its highlights and DX's color),
  `$0F $17 $27 $30` (the white text), `$0F $0F $16 $28`
  and `$0F $0F $00 $10`; `UI_AttributeDataIndex` is 1. The playfield's
  pattern table holds the title's tiles, not the text box's, so a box drawn
  there takes the frame from ROM (`HUD_TILES_START`, bank 10; see The text
  box).
- `StartScreen_Draw` (`$9E21`, bank 12) copies the logo's 224 tiles (bank 10
  `$8E00`) to `$1000`, the letters and digits, then the attribute table
  (`START_SCREEN_PPU_ATTRIBUTES`, bank 10 `$9B60`, 64 bytes, to `$23C0`), all
  through `PPU_WriteTilesFromCHRRAM` (`$F89E`: X the bank, Y the tiles; its
  loop is `LDA (IScriptOrCHRAddr),Y` at `$F8B5`, `STA PPUDATA` at `$F8B7`),
  then writes the logo's nametable from `START_SCREEN_LOGO_TILES` directly.
  Its byte 9 (`$0C`) puts the F's tip in palette 3, the stars' greys: a bug
  (see *Graphics bugs*). The stars are palette 3's `$00`/`$10`, or its color
  1 (`$0F`, black, so unseen) in squares that look empty.
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

**How a Guru shows it.** The only script that shows a mantra is
`ISCRIPT_COMMON_GURU_PASSWORD` (`$A5FC`): `SHOW_MESSAGE` `$22` ("You need
peace of mind. I will meditate with you."), then `SHOW_PASSWORD` (`$14`).
After a new title (`ISCRIPT_COMMON_GURU_NEW_RANK`) and at Eolis's Guru the
original shows none. `IScriptAction_ShowPassword` (`$8737`, bank 12) runs
`Password_GenerateStateAndReset` and `TextBox_ClearPasswordSize`, then for
each character `PasswordScreen_ShowNextChar` (`$8757`): the next 6 bits,
`LDA PASSWORD_ENTERED_CHARS,Y` (`$8764`, "A-Za-z0-9,?") and `JSR
TextBox_ShowMessage` (`$875F`). That writes the glyph at column
`TextBox_CharPosForLine` (its PPU address's low byte is the column × 16, so
column 16 is column 0 again) and falls into
`TextBox_ShowMessage_HandleSpaceOrEndOfLine` (`$F502`): from column 16 on,
it reads the message's next byte, `(Message_StartAddr),Message_ProcessedLength`,
and starts a new line unless that is `$FF` (the end: `MessageID` 0) or `$FC`
(a pause).

- **The read is in the wrong bank** (chipx86). The pointer is what the
  greeting left, in bank 13, but bank 12 is mapped, so the check reads bank
  12 at the same address. Had it been `$FF` or `$FC`, no line would start:
  the characters after the 16th would overwrite the first line, and the
  second line would stay empty.
- **In the shipped game it never shows.** A message's loop reads its `$FF`
  and stops, and B ends the script, so the mantra always comes after the
  whole greeting: `Message_StartAddr` `$8700`, `Message_ProcessedLength`
  `$6B`, the next message's first byte (`$876B`, bank 13: the I of "I shall
  give you a title"). Bank 12 holds `$48` there, the H of
  `PASSWORD_ENTERED_CHARS`, so every mantra wraps right. Bank 12's `$8760`,
  the `$FF` of `JSR $F4FF`, would not: a save state with
  `Message_ProcessedLength` `$60` shows the original's second half over its
  first (`tests/fixes/small_fixes.py`).
- **A fix, should it be needed** (chipx86's suggestion; Small fixes had it
  until the remaster stopped showing mantras): a hook at
  `IScriptAction_ShowPassword`'s entry points the message at
  `PASSWORD_ENTERED_CHARS` (`Message_StartAddr` `$8764`,
  `Message_ProcessedLength` 0), so the check reads an A in bank 12 and always
  starts the new line. Nothing reads the pointer again before the next
  `Messages_Load` sets it.
- **The remaster shows no mantra.** Save files puts the Guru's choice where
  the mantra was, and with no file in play (a save state from the original)
  or if the choice fails, it skips the mantra (see *Save files*). With the
  remaster off, the Gurus show it as the original does.

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
| `$03BD`, `$03C8` | `SelectedWeapon`, `Player_CurWeapon` | 0 Hand Dagger to 3 Dragon Slayer. `Player_CurWeapon` is the weapon in hand: `$FF` in buildings in the original (see *Weapons and the menu in buildings*) |
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

**An item under the hero, for tests.** A save-state fixture can put any item
in a sprite slot as `Sprites_PopulateNextAvailableSprite` (`$C205`) would:
the entity, its x and y (`CurrentSprites_XPos_Full` `$BA`, `_YPos` `$C2`;
the hero's own `$9E` and `$A1` put it under him), `_Phases` (`$2E4`) and
`_Flags` (`$2DC`) 0, `_HitByMagicBehavior` (`$334`) and `_Behaviors`
(`$2D4`) `$FF`, and from bank 14 by entity `_HitBoxTypes` (`$304`, from
`SPRITE_ENTITIES_HITBOX_TYPES` `$B4DF`), `_HP` (`$344`, `SPRITE_ENTITIES_HP`
`$B5A9`) and `_BehaviorAddrs_L/U` (`$354`/`$35C`, `SPRITE_BSCRIPTS` `$AD2D`,
two bytes an entity). The game touches it on the first frames after the load
and runs the item's own pickup. Its picture is wrong (the screen's sprite
tiles are loaded for the screen's own sprites), which the pickup does not
mind. `tests/inventory/inventory_fix.py` drops the Battle Suit and the Dragon
Slayer in Eolis's town this way. The Select menu does not open on screen 0;
in the town it does, and B on one of its lists closes the whole menu.

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

## Weapons and the menu in buildings

In a building (`Area_CurrentArea` 4) the original keeps the weapon out of the
hero's hand, and the Select menu from equipping anything, in three places.
Nothing else is gated.

| Address | Code | Does |
|---|---|---|
| `$DE06` | `Game_EnterBuilding`: `LDA #$FF`, `STA Player_CurWeapon` | Clears the weapon in hand on every way in: a door (`Player_EnterDoorToInside`, then `Game_SetupEnterBuilding`), `Player_Spawn` (a death, a continue, Guru travel's journey) and the ending's walk to the King (`EndGame_MoveToKingsRoom`, building `$44`). `Game_ExitBuilding` (`$DE66`) sets it from `SelectedWeapon` again |
| `$EDED` | `Player_SetWeapon` (`$EDEC`): `LDA Area_CurrentArea`, `CMP #$04`, `BEQ @_cannotEquip` (`$EDF1`) | In a building it only stores `SelectedWeapon`: no `Player_CurWeapon`, no `Player_DrawSprite` |
| `$8B83` | `@_hasInput` in `PlayerMenu_HandleInventoryMenuInput` (bank 12): `LDA Area_CurrentArea`, `CMP #$04`, `BEQ PlayerMenu_HandleInvalidChoice` (`$8B88`) | A on any entry of any list (weapons, armor, shields, magic, items) plays the invalid-choice sound (`$0D`) and keeps the list up |

- **The menu opens indoors.** `GameLoop_CheckShowPlayerMenu` refuses only
  Eolis's screen 0; in a building only the choice is refused. The menu boxes
  are `PlayerMenu_Show`'s (2,6, 10 x 14) and, for a list,
  `PlayerMenu_ShowSubmenu`'s (4,8, 20 x 18); `PlayerMenu_SelectedInventory`
  (`$020E`) is the list, and `DataArray` its entries.
- **The hero swings indoors, bare-handed.** `Player_CheckHandleAttack` checks
  only climbing. `Player_DrawWeapon` and `Player_HitSpriteWithWeapon` return
  at once without `Player_CurWeapon`, so nothing is drawn or hit. Magic
  (`Player_CastMagic`) is not gated at all.
- **The weapon's tiles have room.** `Player_LoadWeaponTilesToPPU` loads them
  to PPU `$0380` (`$0340` for the Dragon Slayer, whose set has no shield), from
  `Screen_SetupNew`'s `Player_DrawSpriteImmediately` and from
  `Player_DrawSprite`. The common sprites load to `$0400-$08FF`
  (`Sprites_LoadCommon`) and a screen's own from `$0900`, indoors as out. In
  the shadow OAM the weapon is tiles `$38-$3F` in sprite palette 1.
- **Townsfolk cannot be hit.** `Player_HitSpriteWithWeapon` and a spell's
  `Sprite_CheckHitByCastMagic` (`$8ADC`) go on only for `SPRITE_CATEGORIES_BY_ENTITY`
  (`$B544`, bank 14) 0, an enemy, and 7, a boss. Every NPC (entities `$34-$45`:
  the sellers, the Gurus, the King, the townsfolk) is 2, whose collision
  handler is the one Up talks through. They have 0 HP (`SPRITE_ENTITIES_HP`),
  so were one an enemy, any hit would kill it. A swing at a seller across his
  counter or at a Guru overlaps him by the game's own reach test and changes
  nothing (`tests/indoor/indoor_equip.py`). chipx86's comments on the
  categories table name the entity after each byte's: the byte for `$4F`, the
  intro's trigger, says "Item: Mattock".
- **Enemies in buildings.** Eight buildings' sprite lists (bank 11, area 4)
  hold enemies, all behind doors off the Mist and Dartmoor. In the original
  the sword passes through them, since no weapon is in hand, and only magic
  hurts them; they hurt the hero as anywhere.

  | Building | Door | Holds |
  |---|---|---|
  | `$1D` | Mist `$06`, block `$93` | Sir Gawaine (`$1F`, 35 HP) at `$87` |
  | `$1F` | Mist `$06`, `$9B` | The Wing Boots, a Giant Strider |
  | `$35` | Dartmoor `$02`, `$42` | Sir Gawaine |
  | `$36` | Dartmoor `$02`, `$45` | A Grimlock |
  | `$37` | Dartmoor `$02`, `$4B` | A Red Potion, a Grimlock |
  | `$38` | Dartmoor `$02`, `$4E` | A Maskman |
  | `$39` | Dartmoor `$03`, `$AC` | Sir Gawaine |
  | `$43` | Dartmoor `$1F`, `$AD` | Ointment, a Wolfman |

  The Mist's screen 6 also holds temple 3's door (`$1E`, between the two).
- **For tests:** Forepaw's tool shop (Long Sword 1600) is building `$0F`,
  through the towns' (area 3) screen 2, block `$9D`, palette `$1B`
  (`PALETTE_TOWN`); its Tool Seller stands behind the counter as Eolis's does.
  A building's way in puts the hero at `BUILDING_START_POSITIONS` by its
  layout (x 224), facing left.

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
| `$87DC` | `Player_HandleHitByMagic` | Lets `SPRITE_ENTITY_DAMAGES[entity] >> (SelectedShield + 1)` through: half with the Small Shield, a quarter with the Large, an eighth with the Magic. Non-zero: `Player_SetDamagedBySprite` (60 iframes, face the shot, knockback) and `Player_ReduceHP`. It checks neither iframes nor Ointment |
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
- **Ointment does not cover the shield in the original.** Magic that touches
  the hero does nothing under Ointment (`Player_HandleTouchEnemyMagic`, and
  the shot flies on through him), but magic on the shield still goes
  through `Player_HandleHitByMagic`, which never looks at it: the Execution
  Hood's shot (8) takes 4 through the Small Shield, with knockback, Ointment
  or not. The 40th Anniversary Edition's notes list the same fix ("shield
  could no longer be hit while ointment was in use"). Small fixes skips
  `Player_HandleHitByMagic` while Ointment runs; the shield still takes the
  shot, with its sound.
- **Iframes do not cover the shield in the original either.** Nothing on the
  way from `Sprites_UpdateAll` to `Player_HandleHitByMagic` looks at
  `Player_InvincibilityPhase`, so a shot on the shield inside the 60 frames
  after a hit hurts again, while one that touches the hero then does nothing.
  On Zenis's screen 16 (below), the Hood's shot and a copy of it 16 pixels
  behind, both at the Small Shield's height, the hero at x 120: the first
  takes 4 and sets the phase to `$3C`; the second lands 20 frames later, with
  the phase at 41, takes 4 more, sets it back to `$3C` and knocks him back
  another 24 pixels. By the code, two shots in the same frame both hurt too.
  Small fixes also skips `Player_HandleHitByMagic` while the phase is not 0,
  checking it before Ointment as `Player_HandleTouchEnemyMagic` does: the
  shield takes the shot, with its sound, and the phase counts on from the
  first hit. The game sets the phase only on a hit (a sprite's touch or shot,
  Sugata's flash, the Poison) and clears it in `Player_HandleDeath` and
  `Player_SetInitialState`. Seamless gives the hero's copy in a neighboring
  room 200, in isolation, so with Small fixes shots stop at his shield there
  as they pass through his body.
- **The shield's sound** shows in `SoundEffect_Current` (`$00FB`), which
  `Sound_PlayEffect` sets to the sound queued that frame (`$0C`) and the
  sound engine marks playing with bit 7 on its next tick (`$8C`).
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

- **Where a hit leaves him.** A hit's knockback is 8 pixels a frame for three
  frames. From where he arrives (x 16), it carries him to the screen's left
  edge, and the screen changes; the floor to his right has a gap, and knocked
  back to x 48 he falls through it to screen 3. Moved to x 120 in the save
  state, two knockbacks leave him on the floor at 72.
- **A second shot** is a copy of the first in an empty slot: the same byte
  in every per-slot sprite array (`CurrentSprites_XPos_Full` `$BA`, `_YPos`
  `$C2`, and `$02BC`-`$036C` every 8 bytes, `_XPos_Frac` to `_Values`), with
  its X moved back.

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

## Damage, dealt and taken (banks 14, 15)

Every loss of HP, an enemy's or the hero's, is one subtraction in one of three
places. Each has its amount in memory just before it, which is where Damage
numbers reads it:

| Address | Where | Before it |
|---|---|---|
| `$8891` (bank 14) | `Player_HitSpriteWithWeapon`'s `@_updateSpriteHP`: `LDA CurrentSprites_HP,X`, `SEC`, `SBC Temp_00`, `STA`, `BCS @_return` | `Temp_00` the hit's damage (*Weapon damage*), X the sprite |
| `$81FD` (bank 14) | `Player_HitEnemyWithMagic`'s `@_reduceEnemyHP`, the same five instructions | `Temp_00` the spell's damage: `MAGIC_DAMAGE` (`$B7A0`: 6, 9, 24, 34, 38 for Deluge to Tilte), half again with the Magical Rod, then halved for a resisted spell and **0** for one resisted twice. A 0 still reaches the subtraction |
| `$C08E` (fixed) | `Player_ReduceHP`: `LDA Player_HP_L`, `SEC` (`$C091`), `SBC Arg_PlayerHealthDelta_L`, ..., `SBC Arg_PlayerHealthDelta_U`, `BCS` alive | `Arg_PlayerHealthDelta_U`/`_L` (`$04BD`/`$04BC`), whole HP and 256ths |

- **On an enemy** the subtraction is 8-bit and a borrow kills, so a kill's
  damage is usually more than the HP that was left (a Zozura left 1 HP takes a
  Long Sword's 8). The HP is stored wrapped.
- **On the hero**, HP is 8.8 fixed point (`Player_HP_U` whole, `Player_HP_L`
  the fraction). `Player_ReduceHP` is reached from four places (*What killed
  the hero* tells them apart):
  `Player_ApplyDamage` (`$8A79`, from `Player_HandleTouchEnemy`, an enemy's
  or a shot's touch, after its iframe and Ointment checks),
  `Player_HandleHitByMagic` (`$87DC`, a shot on the shield), Sugata's flash
  (`$AB44`, which checks neither) and `Player_PickUpPoison` (`$C83C`, 16 HP,
  after its message). A borrow sets the HP to 0 and uses the Elixir or sets
  `PlayerIsDead`.
- **Armor makes fractions.** `Player_ApplyDamage` takes
  `SPRITE_ENTITY_DAMAGES[entity]` (`$B6D7`) whole HP and, with armor 1-3,
  subtracts `ARMOR_DEFENSE_MULTIPLIERS` (`$8AD8`: 0, 1, 2, 4) eighths of it
  (Studded Mail 7/8, Full Plate 6/8, the Battle Suit 4/8), and the Black Onyx
  one eighth more. So a 3-damage enemy against Full Plate takes 2.25, and the
  HUD's bar drops by 2 or 3 as the fraction carries. A shot on the shield
  takes whole HP (`SPRITE_ENTITY_DAMAGES >> (SelectedShield + 1)`), none when
  that is 0, and the flash and the Poison whole HP too.
- **A weapon's hit knocks an enemy back.** It sets the enemy's facing
  (`CurrentSprites_Flags` bit 0) to the hero's and `CurrentSprites_HitCounter`
  (`$034C`) to 8. While the counter runs, `Sprites_UpdateAll` (`$8000`) runs
  no behavior or hit checks for it: each frame it decrements the counter and,
  for every enemy but Tamazutsu, Pakukame and Zorugeriru (`$2C`, `$30`,
  `$31`), moves it 2 pixels the way it faces
  (`Sprites_Something_SomethingAndMoveHoriz`, `MoveSpriteHorizIfPossible`):
  16 pixels away from the hero over the 8 frames, if nothing blocks it. Its
  place after a hit is where the hit landed plus 2 × (8 − the counter).
- **The box an enemy is hit in** is `CurrentSprites_XPos_Full`/`_YPos`, its
  top-left, and `MAYBE_SPRITE_EXTENTS` (`$B407`), width and height by entity
  × 2, for entities `$00-$64`.
- A Zozura's touch takes 7 (`SPRITE_ENTITY_DAMAGES[$47]`).

**The HUD's digits** are background tiles `$30-$39` (the HUD writes numbers in
ASCII): bank 10, `HUD_TILES_START` (`$8140`) + tile × 16. Each is a bold 7×7
glyph in color 3 with a blank last row and column; the 5 has one color-1
pixel.

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
  runs the Guru's words. Nothing reads the pad in between. Everything a
  death decides comes after the words (the gold and experience, the place,
  the respawn's save), so a save state taken as the Guru speaks (`MessageID`
  `$24`) carries the death on without the dissolve; the end-to-end tests
  start most deaths there (`fax_e2e.dissolve()`). Poking the counter near
  its wrap is not the same: `Screen_NextTransitionState` starts the fade to
  black when it reaches `$50` (`Screen_FadeOutStage` set to 0), so a death
  skipped past it never runs that fade before the Guru's words.
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
| `$10` | climbing | Up or Down pressed in front of a ladder; cleared away from one, and by knockback. While it is set on a ladder, A does not jump (see Ladders) |
| `$20` | moving | Walking, or carried sideways in the air |
| `$40` | facing right | |
| `$80` | attacking | Through the swing's 8 + 3 + 8 frames |

**On the ground** means `Player_CheckIfPassable` (`$E6C8`) finds the way down
blocked: the block under his feet, at `Y + $20`, from `X + 4`, and the next
block too when `X & 15` is 4-11, so his feet are 8 pixels wide. (The call in
`Player_HandleVertLadderOrJump` loads the direction into A instead of X, as
chipx86 notes, so it checks whatever X holds. It never changes the outcome:
see Ladders.)

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

**Ladders** have a section of their own, below.

**In the air** is `Player_Flags & $05` to the rest of the game too:
`GameLoop_CheckUseCurrentItem` refuses items then.

**Doors.** Up at Apolune's temple door in mid-jump does nothing; on the
ground it goes in. Jumping out of the temple's right edge carries the jump
into the town, where he lands on his first update. Walking out with the
direction still down as the next screen comes up leaves him walking there:
`Player_Flags` keeps `$20` (moving) at the door's spot after the screen is
set up, and a jump from that state carries him sideways. A save state for a
frame-exact run is taken once he stands still (`$25` clear at the same X and
Y some frames apart, `tests/movement/play.py`'s `settle()`): a state taken 10
frames after the scroll had `$20` set in 24 of 24 made at once, and
`tests/ladders/ladders.py`'s climb onto column 14's ladder failed from 16 of
them.

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

### Ladders

A block is **climbable** when its property (`Area_GetBlockProperty`) is `$02`
or `$0A` (`Area_IsBlockClimbable`, `$E8B2`). `$0A` is also one of the four
switch-screen properties (`$09`, `$0A`, `$0C`, `$0D`): the Trunk's climb
between screens `$0C` and `$16` (`AREA_SCREEN_COMPARATORS`) has a `$0A` block
at the top of `$0C`'s ladder (block `$80`, row 0, column 13) and at the foot
of `$16`'s (row 12, column 3), and `Player_CheckSwitchScreen` loads the other
screen (`Game_SetupEnterScreen`: `$16` at `$B3`, `$0C` at `$2D`) once the
block at his head is one, at both `X` and `X + 15`.

**Can climb** (`Player_Flags` `$08`) is set every update by
`Player_CheckIfOnLadder` (`$E752`) when the block at his head (`X + 7`, `Y`)
or his feet (`X + 7`, `Y + $1F`) is climbable, or the block right of either
when `X & 15` is 8. When neither is, it clears can climb and climbing. So he
is on a ladder from where his feet enter its top block to where his head
leaves its foot: on a ladder whose top block is row r, the highest he can be
is `Y = 16r - $1F`.

**On one he hangs**: in the air, the grab path catches him, so there is no
fall. Up and Down climb, at `$A0`/256 and `$C0`/256 pixels a frame, lined up
with the ladder (`X & 15` = 0; Player_CheckHandleJump walks him into line
first). `Player_CheckHandleClimbUp` (`$E301`) asks only that the block over
his head be passable (`BNE @_return` at `$E30C`), and
`Player_CheckHandleClimbDown` (`$E349`) that the one under his feet be:
neither asks whether he is still on the ladder. Off a ladder's foot he steps
into the air and falls, which is how you get off one. Stepping off sideways is
a fall like any other.

**Climbing** (`$10`) is set by Up or Down in front of a ladder and cleared
only away from one (`Player_CheckIfOnLadder`) and by knockback.
`Player_IsClimbing` (`$ECF6`) is can climb, climbing, and either lined up with
the ladder or not falling. While it holds, `Player_UpdateClimbOrJump` refuses
a jump and `Player_CheckHandleAttack` a swing. So once he has climbed, A does
nothing on the ladder until something clears climbing; hanging on one he has
only jumped onto, A jumps.

**The top of a ladder (the original's bob).** Up at a ladder's top block lifts
him a pixel past `16r - $1F`; the next update finds no climbable block at his
head or feet, clears can climb and climbing, and drops him 8 pixels, and the
ladder catches him. With Up held he bobs: about 13 updates up, one drop, for
as long as it is held, the pose switching out of the climb on each drop (to
the walk pose, or with the falling pose to the jump pose). Checked on Trunk
6's ladder (`tests/ladders/ladders.py`): up to `Y = $40`, dropped to `$48`,
caught. Short of a hit, the drop is also the only way the original clears
climbing at a top: let go of Up right after one and A jumps, any other time it
does nothing. The Trunk's own route up through `$0C` takes that jump (from the
top of column 14's ladder, moving left, onto column 13's, whose top is the
switch block). This is the ladder jank we found on this ROM; the
Retranslation's changelog does not say what its "Fix Ladder Jank" was.

**A hit on a ladder.** Knockback's three updates go to
`Player_HandleKnockback` (`$E153`), which sets `Player_Flags` to his facing
bit, dropping can climb and climbing, and pushes him 8 pixels an update: 24
pixels, off the ladder, and he falls. Only a wall behind him keeps him on it,
and then knockback has cleared climbing, so he hangs on the ladder and A
jumps: the speedrunners' "ladder reset".

**chipx86's "bad can-climb ladder check" never shows on this ROM.**
`Player_HandleVertLadderOrJump`'s ground check (`$E3AD`) is `LDA #$03` where
it means `LDX #$03`, so `Player_CheckIfPassable` takes the direction from X: 0
left, 1 right, 2 up, anything else down. X holds the block index
`Player_CheckIfOnLadder` looked at last (`Area_ConvertPixelsToBlockPos`: row ×
16 + column). It is 0-2 only when that block is at his head in row 0, columns
0-2, and only when it is climbable, since the routine stops looking at the
first climbable block; without one it ends on the block at his feet, row 1 or
below (`Y` is at most `$C0` there, so `Y + $1F` never wraps). With can climb
set, both answers lead to `Player_GrabClimbLadderOrJump` with the same flags:
blocked, directly; open, through `@_setFalling`, whose `AND #$08` sends him to
the same grab, which clears the falling bit again.
(`Area_CheckClimbAdjacentImpassable` in between acts only when `Y` is `$D0` or
more, which never happens.) So the check could only change what happens when
he is not on a ladder, and then it checks down. Ladders that reach row 0 in
columns 1 and 2 are common (Trunk `$16`, `$1E`, `$29`, Mist `$04`, `$05`,
Branch `$04`, Dartmoor `$03`, ...), so the wrong direction is checked often,
to no effect. Forgiving jumps reads this check's answer at `$E3B2`; there a
wrong "blocked" can arm its coyote time from such a hang, which only lets him
jump a few updates after stepping off, as A from the hang itself would.

## Input and casting

| Address | Name | Notes |
|---|---|---|
| `$0016` | `Joy1_ButtonMask` | Buttons held. The bits of `Joy1_ChangedButtonMask`, plus Down=`$04`, Left=`$02`, Right=`$01` |
| `$0018` | `Joy1_PrevButtonMask` | The held mask at the read before. While the screen scrolls, `Game_UpdatePlayerOnScroll` copies it back into `Joy1_ButtonMask` |
| `$00A4` | `Player_Flags` | `$80` attacking, `$40` facing right, `$20` moving, `$10` climbing, `$08` a ladder to climb, `$04` falling (in the air without jumping; see The hero's movement), `$02` holding to climb, `$01` jumping |
| `$02B3` | `CastMagic_Type` | The spell on screen, `$FF` for none. Each spell's handler takes it back to `$FF` when it hits or leaves |
| `$039A` | `Player_MP` | At most 80. In the original a cast is the only thing that lowers it; with the remaster the Wing Boots and the Hour Glass do too (see *MP and its bar*) |
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
- **After a screen loads, the first frames are lag frames.** Entering a
  screen (through a door, say) queues its sprites' tiles in the PPU buffer,
  and for about six frames the loop waits on the buffer while the NMI flushes
  it, so the pad is not read until `PPUBuffer_ReadOffset` (`$1F`) reaches
  `PPUBuffer_WriteOffset` (`$20`); the first frame after, it is. The scroll
  has ended by then (`Screen_ScrollDirection` `$FF`), so a save state taken
  ten frames after the scroll can still be a frame or two short of reading a
  press: `tests/ladders/ladders.py`'s recipes wait for the buffer to empty.
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

## The status bar (banks 15 and 10)

- **Its layout.** `UI_DrawHUDSprites` (`$F8EB`) fills PPU `$2000-$207F`,
  rows 0-3, with tile 0, then writes seven runs of `UI_STATUS_TILES`
  (`$F8D2`) at the addresses in `UI_STATUS_SYMBOL_PPU_ADDR_L` (`$F8CB`):
  `M:` at `$2041`, `P:` at `$2061`, `E:` at `$204E`, `G:` at `$206E`, `T:`
  at `$2056`, and the item box, `[`, two picture tiles and `]`, at `$205B`
  (tiles `$2C $3C $3D $2E`) over `$207B` (`$2D $3E $3F $2F`). Rows 0 and 1
  stay blank: everything is in rows 2 and 3, y 16-31 of the picture, with M
  over P (MP over HP). In pixels: `M` from x 8, `E:` from 112, `T:` from 176,
  the item box x 216-247 with its picture at 224-239, and column 31 (x
  248-255) blank. Then it calls `UI_DrawSelectedItem` (`$FBAF`), which
  writes the selected item's picture into tiles `$3C-$3F` (PPU `$13C0`), and
  copies the 60 status-bar and text-box tiles from bank 10
  (`HUD_TILES_START`) to `$1000`.
- **Its letters are not ASCII**, though its digits are (`$30-$39`): `M` is
  `$1C`, `P` `$1F`, `E` `$14`, `G` `$16`, `T` `$23`, and the colon after `M`
  and `P` is `$0A` where the other three have `$3A`.
- **Whether it shows.** A frame shows the status bar when its nametable has
  `P:` (`$1F $0A`) at `$2061` and the background is on in that row
  (`fax_text_hud_showing`, through the line's scroll as
  `fax_text_shown_tile` reads it): not on the title, the password screen or
  the ending, nor while the game has the screen off to load one.
- **Its palette** is the text box's, `UI_AttributeDataIndex` (see *The text
  box*), so its colors change from place to place: in Eolis's town, palette
  3, the MP bar is green, the HP bar pink, the text white.
- **The items' pictures** are four tiles each in bank 10.
  `ITEM_TILEMAP_INDEXES` (`$B4E4`) gives an item's tile numbers by
  `ITEMINVENTORY_*` × 4 (top left, top right, bottom left, bottom right), and
  tile t is the 16 bytes at `$8500` + t × 16 (`UI_Maybe_GetItemSpritePPUTileAddr`,
  `$FBF0`, which has no label for the table). `UI_DrawSelectedItem` copies them
  for the item box; `TextBox_LoadItemSourceTiles` (`$FC18`) for the menus,
  through a table of five lists, one a category (`$B4A0`, `$B4B0`, `$B4C0`,
  `$B4D0`, and the Items' `$B4E4`). The Wing Boots are tiles `$22-$25`, the
  Hour Glass `$1A-$1D`, the Elixir `$34-$37` (a bottle with two handles, 16 x
  15). The Player page's special items are Items-category IDs too
  (`SPECIAL_ITEM_TILE_START_IDS`, `$8E63`, bank 12): the Elixir's `$92` is
  `$80` | `ITEMINVENTORY_ELIXIR` (`$12`), so the page shows the same picture.
- **In Seamless** (`fax_world_hud_x`), the status bar band (lines 0-31) is
  copied from the native picture into the 424-wide view, with the Elixir's
  place (`fax_charges.c`, 16 pixels) against its left edge, and the two are
  centred as one group, whether the hero has an Elixir or not: the Elixir at
  x 76-91, the band's column 0 at x 92, not 84 (#117; #113 had a row of
  three items past the item box, and the band at x 52). The band's column 0
  is blank, so the Elixir stands 8 pixels from the `M` and the `P`, level
  with the two bars. Only the band moves. Everything below it stays centred
  in the view as before: a room narrower than the view (a building, an
  arena), the native picture's lines 32-239 (a region load, a script), its
  text boxes and menus, the speaker tabs, Quick shops' list, and the zone
  and quest banners. So in a building the band sits 8 pixels right of the
  room under it. The title and other screens without the status bar are
  centred whole. With the remaster off there is no Elixir and no Seamless.

## MP and its bar (bank 15)

- **A new game starts with no MP.** `Game_Start` (`$DB26`) stores 0 in
  `Player_MP` (`$DB31`); `Player_Spawn` (`$DB0A`: `LDA #$50`, `STA
  Player_HP_U`, `STA Player_MP`) fills HP and MP for a death, a continue or a
  mantra. In the original, MP otherwise comes only from the meat shops (30 HP
  and 30 MP, for 50 G in Eolis up to 800 G in Dartmoor: see *Shops*; with the
  remaster they sell Red Potions), Eolis's Magician (40
  MP for 200 G, `ISCRIPT_EOLIS_MARTIAL_ARTS_MAGIC_SHOP`; both through
  `IScriptAction_AddMP`, `$8580`, bank 12, 1 MP at a time with its sound) and
  the Elixir's fill (`Player_FillHPAndMP`, `$C4DA`, 4 at a time). It never
  comes back on its own. With the remaster, MP refill brings it back, and the
  Magician teaches a faster refill instead of selling MP (*Features*).
- **Only a cast takes MP.** The stores to `Player_MP` are `Game_Start`'s,
  `Player_Spawn`'s, `Player_ReduceMP` (`$C0C3`, from `Player_CastMagic`, the
  only one that lowers it), `Player_AddMP` (`$C0D9`: `CLC`, `ADC Player_MP`,
  capped at `$50`, then `JMP Player_SetMP`) and `Player_SetMP` (`$FA85`,
  capped at 80). So MP lower than a frame before means a spell was cast (or a
  new game began). With the remaster the Wing Boots and the Hour Glass take
  MP too (Recharging items stores it and draws the bar with `Player_SetMP`),
  and MP refill counts those as spends as well.
- **The bar is drawn only when MP changes through those routines.**
  `Player_SetMP` stores MP and falls into `UI_SetHUDBarValue` with Y = 1, the
  Magic bar, which queues two PPU buffer commands: the bar at `$2043`,
  `UI_MPAndHPBarWidth` (`$04EE`, 10, set by `UI_DrawHUDSprites`) + 1 tiles
  (MP / 8 full tiles `$09`, a partial `$0D` unless full, empty `$07` to the
  width, and the end `$0B`), then the partial tile's 16-byte pattern at PPU
  `$10D0`, filled MP & 7 pixels. Each command waits for room first
  (`PPUBuffer_QueueCommandOrLength` calls `PPUBuffer_WaitForCapacity`, and
  `PPUBuffer_HasCapacity` is true when `PPUBuffer_ReadOffset` -
  `PPUBuffer_WriteOffset` is 0 or at least `$24`), so the whole bar goes in
  at once when that room is 0 or at least 50 (14 bytes, then `$24` more). A
  poke of `Player_MP` leaves the bar as it was, so a test that pokes MP and
  shows the bar starts from a real cast (`tests/mana/mana_refill.py`,
  `tests/spell_cost/spell_cost.py`). `UI_DrawHUD` calls `Player_SetMP` too,
  so drawing the whole status bar draws it from `Player_MP`.
- **The bar in pixels.** One pixel an MP, from x 24 (column 3 of row 2) for
  at most 80 pixels (the width × 8, to which `UI_SetHUDBarValue` caps the
  value), then the end tile, whose white column is x 104. Its tiles are bank
  10's (`HUD_TILES_START`, copied to `$1000`): `$07` empty, `$09` full (the
  Power bar's is `$08`), `$0B` the end, and `$0D` the partial, whose pattern
  `UI_SetHUDBarValue` writes from `STATUS_BAR_PARTIAL_PPU_TILES` (fixed
  bank, `$FB2F`), filled from the left. Each has a border on its rows 1 and
  6, lines 17 and 22, in pixel value 3 (the palette's white, `$30`), and the
  fill on rows 2-5, lines 18-21; rows 0 and 7 are blank. The Magic bar's
  partial tile takes the table's two planes swapped, so its fill is pixel
  value 2 where the Power bar's is 1, as in `$09` and `$08`. So the MP bar's
  fill is color 2 of the status bar's palette (`UI_AttributeDataIndex` × 4
  + 2): `$19`, a middle green, in 25 of bank 11's 31 screen palettes, and
  `$1A` in one (14). The other five have `$0F` (4, loaded only by dead
  code), `$27` (16, the title's, and 28), `$25` (29) and `$1C` (30).
- **A spell goes off with MP equal to its cost.** `Player_ReduceMP`
  subtracts `MAGIC_COSTS` (`$B7A9`, bank 14: 2, 4, 6, 10, 16, by
  `SelectedMagic`) from `Player_MP` and refuses only on a borrow, so a spell
  needs at least its cost, and cast at exactly its cost leaves 0. Spell cost
  (*Features*) shows the cost on the bar.
- **The game darkens a color a row of the palette at a time.**
  `Screen_SetFadeOutPalette` (`$D0AD`) fades a screen out by subtracting
  `FADE_OUT_DELTA_TABLE` (`$D0E0`: `$10`, `$20`, `$30`, `$40`, by
  `Screen_FadeOutStage`) from each color of its palette, black (`$0F`) where
  that goes below 0: the bar's `$19` is `$09`, then black. A row up, `$29`,
  is the same hue lighter.
- **Where the main loop waits for its frames.** Every wait is a `JSR
  WaitForNextFrame` (`$CA25`: `LDA #$00`, `STA Game_InterruptsHandledLatch`
  (`$10`), then `LDA $10`, `BEQ` until the NMI sets it), each from its own
  address: `Game_MainLoop`'s top at `$DB4D` (it has just set S to `$FF`, so
  S is `$FD` inside, with `$DB4F` at `$01FE`), the screen-change and scroll
  paths after a pass at `$DB94`, `$DB9D`, `$DBAF`, `$DBC4` and `$DBCA`,
  `EndGame_MainLoop`'s at `$DBF6` (it sets no S), the pause loop's at `$E036`
  (under the `JSR GameLoop_CheckPauseGame` at `$DB74`, so S is `$FB`), and
  the text box's, the menu's, the death's and the screen loads' in their own
  routines. A frame that ends with the CPU in the first, S `$FD` and `$DB4F`
  on the stack, is one the game played: its pass finished and nothing held
  it. On a quiet screen every frame ends so; a lag frame ends inside the
  pass. MP refill counts those frames (`fax_mana.c`).

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
  `$54` is 0. `Game_PausedState` (`$0120`) is nonzero while Start pauses:
  `GameLoop_CheckPauseGame` stores 1, and the sound engine
  (`Music_HandleOnInterrupt`, bank 5) sets bit 7 once it has muted, so it
  reads `$81` from then on.

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

## The map

`fax_map_layout.c` reads the whole map from a copy of the cart once, and
`fax_map.c` keeps what the hero has seen and draws it.

**The screen links** (bank 3). `AREAS_TABLE_PTR` (`$8000`) holds the offset
of `AREA_TABLES` from `$8000`; that holds, per area, the offset of
`<AREA>_AREA_DATA`, five more offsets: block attributes, block properties,
then the three the map uses:

- `<AREA>_SCROLL_DATA`: 4 bytes a screen, the screens to the left, right,
  above and below, `$FF` for none. The door locations follow it directly, so
  its length is the area's screen count (9, 64, 83, 14, 10, 40, 32, 20).
  `Area_LoadScrollData` copies the current screen's four into
  `Area_ScreenToTheLeft/Right/Above/Below` (`$0066-$0069`).
- `<AREA>_DOOR_LOCATIONS`: 4 bytes a door to a `$FF`: screen, block (`YX`),
  index, start (`YX`).
- `<AREA>_DOOR_DESTINATIONS`: `Player_CheckIfDoorBlock` reads index × 4 (index
  - `$20` in the towns): screen, palette, key, unused. Entries no door's index
  names are other data: in Eolis, whose only door to a screen is index 0,
  entries 2-`$1F` are not destinations. `$FE` and `$FF` are a region's doors,
  never looked up here.

**Towns and the climb** (bank 15). Walking off a region's edge into a town is
not a scroll link: a block of property `$0C` or `$0D` sends
`Player_CheckSwitchScreen` to `Player_CheckSwitchScreen_SwitchAreaHoriz`, which
looks the screen up in `AREA_TOWN_TRANSITIONS_DATA` (`$EA9C`): per area, a
pointer to 5-byte entries to a `$FF` (screen, area, screen, start `YX`,
palette). The start says which side the town is on: X under 8 starts the hero
at its left edge, so it lies to the screen's right. Each town has an entry
back; the Trunk's first entry (screen 0 to Forepaw's left screen) has none,
and nothing takes it. Conflate's right screen (`$09`) and Dartmoor town's
(`$0D`) lead nowhere. A block of property `$09` or `$0A` looks the screen up in
`AREA_SCREEN_COMPARATORS` (`$EA37`) instead: 4-byte entries (screen, screen,
start `YX`, palette); only the Trunk has any, the climb between `$0C` and
`$16` (start Y `$B`: `$16` is above `$0C`).

**Not a grid.** The links are not always both ways, nor Euclidean:

- One way: Trunk `$1B` (left, right and up, to `$1A`, `$3E` and `$28`), Mist
  `$26` up to `$1F`, Zenis `$01` right, `$0C` down, `$11` up.
- Nothing leads to Trunk `$1B`, Mist `$0A`, `$0B`, `$23` and `$24`, or
  Dartmoor `$0F`. Eolis `$00`, outside the gate, is only where a new game
  starts.
- Mist `$2A` (above `$04`) has `$2B` to its right, but `$00` is above `$05`,
  in that place. Conflate, off Branch `$0D`'s right edge, would need `$16`'s
  cell for its second screen. The Castle of Fraternal's `$14`, `$19` and `$1A` cross, and the Fortress
  of Zenis wraps around like a torus: `$05`'s right is `$0F`, `$02`'s left is
  `$13`.

**The layout** (`fax_map_layout.c`). A page per region, tower and castle, in
the game's order: Eolis (with the gate), the Trunk, the Tower of Trunk, the
Tower of Fortress, the Mist, the Tower of Suffer, the Tower of Mist, the
Branch, Dartmoor, the Castle of Fraternal, the Fortress of Zenis. Its screens
are its zone's in `fax_zone.c` that anything leads to, and the towns at its
edges (all seven towns hang off one region each). A page is laid out by a
breadth-first walk from its lowest screen: each screen one cell over from the
one it was reached from, unless that cell is taken; scroll links (both ways)
first, then town edges and the climb once nothing else is left. A town goes
in whole, both screens side by side, or not at all. Whatever the walk cannot
reach (a tower in the region's zone behind a door, like the Joker spring or
Mist's two unnamed towers; a screen whose cell was taken) is laid out the same
way on its own and set down near what leads to it, a cell clear of everything
else: the nearest place, then below, then to the right. The pages, as
`tests/map/map_test` prints them with the ROM:

| Page | Screens | Cells | Page | Screens | Cells |
|---|---|---|---|---|---|
| Eolis | 9 | 8 x 3 | Tower of Mist | 14 | 9 x 2 |
| Trunk | 37 | 22 x 6 | Branch | 44 | 24 x 6 |
| Tower of Trunk | 9 | 7 x 3 | Dartmoor | 17 | 7 x 4 |
| Tower of Fortress | 21 | 6 x 4 | Castle of Fraternal | 16 | 11 x 3 |
| Mist | 54 | 23 x 10 | Fortress of Zenis | 20 | 5 x 7 |
| Tower of Suffer | 15 | 6 x 4 | | | |

**What the hero has seen** (`fax_map.c`, at each frame's end, once the screen
has settled and the game is not paused, so in both world views):

- A screen: any he stands on outside. In a building, the screen he went in
  from (`Area_SavedArea`, `Area_SavedScreen`).
- A ladder or a drop: a move to the screen a scroll link (or the climb) puts
  above or below, kept on the screen he left (up or down). A death (`PlayerIsDead`)
  forgets where he was, so coming back elsewhere is no move.
- A door, by where he was and where he is: to a screen of the same area that
  no link joins, the door on the one he left that leads there, and the one he
  arrives at that leads back; to another region (not a town), the region doors
  on both screens; in a building, the door on `Area_SavedScreen` that leads to
  it. A respawn, a Guru travel journey and continuing set those to the temple's
  town, so the temple he wakes in counts too. Guru travel's visited Gurus but
  Eolis's (set from the start of every game) count as their temples' doors.

**The pause** (bank 15, `GameLoop_CheckPauseGame`, called once a pass of
`Game_MainLoop`): `$E02B` `LDA Joy1_ChangedButtonMask`, `AND #$10`, `$E02F`
`BEQ` (to an RTS), `LDA #$01`, `STA Game_PausedState`; then
`@_waitForUnpause` (`$E036`): `JSR WaitForNextFrame`, `JSR Sprites_FlipRanges`,
`LDA Joy1_ChangedButtonMask`, `AND #$10`, `$E040` `BEQ @_waitForUnpause`,
`LDA #$00`, `STA Game_PausedState`, `RTS`. The instruction after either BEQ
loads A, N and Z again, so a hook there steers it with Z (and A) alone. The
map's press waits up to 30 frames for `$E02F`: there it clears Z, so the game pauses as for Start, the music held
(`Game_PausedState`) and every feature holding as for a pause. At `$E040`,
once a frame, it reads the pad: Left and Right turn the page, and B, Start
or Map close it, with Z cleared so the game unpauses; otherwise Z is set. A
press during the game's own pause opens it there. The menu, scrolling and
conversations never reach `$E02F`, so the map opens only in play.

**Where it is drawn.** Last in the present chain (`cyc_extras.c`), after
every feature and after Seamless composes its picture: black over the whole
picture, the text box's frame (`fax_text_box`, the area's own colors) over the
native 256 x 240 centered in it, the page's name on row 2 in the dialogue font
between `~` and `}` when there is more than one page, the grid in 240 x 176
from y 32, and a key on row 27. A screen is a box of the largest cell that
fits the page, from 16 x 13 down to 10 x 8 (each keeps a screen's 256 x 208
shape), a 1-pixel wall in `$22` around `$02` inside (a town's: `$28` around
`$08`), the current one blinking `$16` every 16 frames. Seen neighbours a link
or a town edge joins beside each other are open through both walls; above and
below, only a ladder he has taken opens a gap. On the boxes, as Metroid's map
has it, only what matters, each where its block is on its screen: a cross
(3 x 3, `$30`) for a Guru's door used, a block (3 x 3, `$27`) for any other
door used (a shop, a house, the hospital, a tower, a castle, another region),
and a dot (2 x 2, `$2A`) for an item still there (below). A town's doors are
3 to 5 blocks apart, so on the smaller boxes their marks run together: the
Gurus' crosses are drawn over the other doors, and the dots over both. The
key under the grid reads "Guru", "Door", "Item", each beside its mark at
twice the size, the dot rounded. TCP `map_state` gives every box's rectangle
in the last presented picture, each item with whether it shows and whether it
is taken, and every dot drawn.

**Items on the map.** The items are the world pickups in the screens' sprite
lists (*Items in the world*): 44, read from bank 11 with the rest of the map,
Poison left out (it hurts; a dot would send the hero to it). A building's
item is marked at the door that leads in. One shows on a screen seen while
the game would have it there:

- On the screen the hero is on (in a building, that building), while its
  sprite is in a slot (`CurrentSprites_Entities`), hidden or not: what is
  there, is.
- Elsewhere, by its kind. One that lies there at every visit (the Elixirs,
  `$4B` Red Potions, `$48` Gloves, `$4E` Ointments), until the hero has
  touched it; a Glove or an Ointment not while one of its kind runs (its
  behavior removes it then). One the game keeps state for, until that state
  says he has it (`SpecialItems`, `Quests`, the Battle Suit's armor list and
  the like, as its behavior checks); Ripasheiku's Mattock comes back after a
  death, as the game brings it back. One shown on one counted visit in four
  (`Screen_Is3Of4Visits`), never: it is there only while it is.
- The touch: a hook at `Player_HandleTouchItem` (`$88FB`, bank 14: `LDA
  CurrentSprites_Entities,X`, `JSR Player_PickUp`, then the slot emptied),
  which only observes X, the slot. The game keeps nothing for the items that
  lie there at every visit (each comes back whenever its screen loads), so
  the map keeps them taken itself, in the file. In the original a full item
  list refuses the item after its line but takes it off the screen all the
  same. With Kept finds the find stays lying instead: Kept finds' site at
  `$88FB` comes first in `game.toml` and handles the touch, so the map's is
  skipped, and a touch that reaches the map's site is a take.

**In the file.** One field an area that has anything seen: `map.eolis`,
`map.trunk`, `map.mist`, `map.towns`, `map.branch`, `map.dartmoor`,
`map.zenis`. The value is four sets, space-separated: the screens seen,
those left upward, those left downward, and the doors used (by their place in
the area's door locations). A set is hex digits, the first for items 0-3 with
item 0 its lowest bit: Eolis's gate and screens 1-2, with the gate's door
and the temple's (doors 0 and 3 of its 10), read `700 000 000 900`. Written when the file is (`FAX_SAVE_WRITING`),
read back on `FAX_SAVE_CONTINUED`, cleared for a new game; a damaged field
forgets that area only. Every field fits a value (the Mist's, everything set,
is 71 characters). And `map.items`, when any is taken: the items taken, as a
set by their place in *Items in the world*'s order (by area, a building's in
area 4 by its number, then screen, then place in the list); the Elixir on
Trunk `$3C`, the twelfth, alone reads `00800000000`. A damaged one shows
every item again.

## Quests

The quest log (`fax_quests.c`) works from a curated list of the game's
requests (`fax_quest_table.c`), drafted from every interaction script and the
messages they show (`tests/quests/quests_test` checks the list against the
scripts with the ROM).

**What the game tracks.** Little:

- `Quests` (`$042D`): `$01`, `$02` and `$04`, the springs of Trunk, of Sky
  and of the Joker door ("the first spring"), are the only bits a script can
  test or set: `IF_QUEST` (`$0B`) and `SET_QUEST` (`$0E`) take an index, 1-3,
  into `ISCRIPTS_QUEST_BITS` (`$01`, `$02`, `$04`). `$08` (Zorugeriru's Wing
  Boots) and `$10` (Ripasheiku's Mattock) are set by their pickups
  (`Player_PickUpWingBootsWithQuest`, `Player_PickUpMattockWithQuest`), before
  the pickup's script; `Player_Spawn` clears `$10`. `$20` is set when the rock
  is pushed (`Game_DropLadderToMascon`).
- `SpecialItems` (`$042C`, `SPECIAL_ITEM_BITMASKS`, `$9AE5` bank 12): `$80`
  Ring of Elf, `$40` Ruby, `$20` Dworf, `$10` the Demon's Ring, `$08` Elixir,
  `$04` Magical Rod, `$02` Pendant, `$01` Black Onyx (the meteorite, once
  held).
- Nothing records a conversation or a boss. A quest is complete by what they
  leave: an item, a flag, a place.

**Who says what.** A screen's sprite list (bank 11) gives each person on it
an interaction script: after the (entity, `YX`) pairs and their `$FF`, one
value a sprite, in order, which `Sprites_LoadSpriteValue` puts in
`CurrentSprites_Values` (`$036C`). For a person, a sprite whose
`SPRITE_CATEGORIES_BY_ENTITY` (`$B544`, bank 14) is 2 (`SPRITE_NPC`: entities
`$34-$45`), it is his script, which `Player_CheckHandlePressUpOnNPC` hands to
`IScripts_Begin`; for others it is a coin's value or an HP. Area 4's lists are
by building number, `$00-$45` (not by its 10 layouts). Where each quest's
people are is in `fax_quest_table.c`'s comment; a building's door is in its
area's door tables (*Areas, screens and zones*). Scripts on no screen: `$14`
("You need a key."), `$1B` (an END), `$45` (the magic lore `$4B` also says)
and `$70` (Dartmoor's "There is the Castle of Fraternal...", "You need the
Demon's Ring..."); `$02` and `$7B-$7F` are the doors', `$80-$97` the items',
and `$1E`, `$2D-$2F` and the others named `_INTRO` share the intro's.

**`MAYBE_SPRITE_EXTENTS`** (`$B407`, bank 14) are interaction boxes, not the
picture: the King's and the Gurus' (`$3A`, `$39`) are 48 x 32 and the
Martial Artist's and the Magician's 32, reaching right of the 16 pixels each
is drawn in from `CurrentSprites_XPos_Full`. Every person is drawn 16 wide.

**Every line passes `Messages_Load`** (`$F3F5`, fixed bank): `STA MessageID`
first, so A at its entry is the message, whatever shows it (a script's
messages and questions, a shop's lines, `IScripts_ShowFinalMessage`). A
pickup's script ("I've got the Black Onyx.", messages `$B0-$BA`) runs before
the item is added (*Items and the inventory*).

**Walking a script.** An action's parameter bytes: none for `$00`, `$14`,
`$15`; one for `$01`-`$03`, `$06`, `$07`, `$0A`, `$0E`, `$10`, `$13`; two for
`$04`, `$05`, `$08`, `$09`, `$0D`, `$0F`, `$11`, `$17`; three for `$0B`,
`$0C`, `$12`, `$16`. `IF_TITLE` (`$0C`) jumps when its title is at most
`PlayerTitle`; `IF_NEW_TITLE` (`$04`) when `NextPlayerTitle` differs from
`PlayerTitle` (and makes them equal); `IF_ITEM` (`$12`) as `Player_LacksItem`
asks: a special item by its bit, anything else selected or in its list's
first count entries. A question (`$02`) goes on with A and ends with B; `$16`
jumps with A. `fax_quest_walk` follows a script so, answering every question
with A, and lists the messages it would show.

**The quests**, in the game's order (script: message; the people are in
`fax_quest_table.c`):

| Quest | Given by | Next step | Complete when |
|---|---|---|---|
| The Ring of Elf | `$03`: `$2A` "Go to see the Guru before you see the king." | Eolis's Guru, `$04`: `$2C` | Ring of Elf |
| The King's Plea | the King, `$08`: `$35` (no gold) or `$36` (gold); Forepaw, `$21`: `$4C` "Find the 3 springs" | | all three springs (`Quests & 7`); the log counts them |
| The Mattock | Apolune, `$12`: `$40` (with the Small Shield) | | a Mattock held, `Quests & $10`, or Forepaw reached |
| The Wall | Apolune, `$13`: `$41`, `$42` | | Forepaw reached |
| Spring of Trunk | Forepaw's nurse, `$23`: `$50`; the old man, `$26`: `$59` | `$26`: `$5A` (with the Elixir) | `Quests & 1` |
| The Wing Boots | the nurse, `$23`: `$51` | | `Quests & 8`, Wing Boots held, or `Quests & 2` |
| Spring of Sky | `$22`: `$4E`; the nurse, `$23`: `$51`; the Guri, `$24`: `$54` | the old man, `$25`: `$57` | `Quests & 2` |
| The First Spring | the Guri, `$24`: `$55`; `$25`: `$58` | the old man, `$27`: `$5D` | `Quests & 4` |
| Path to Mascon | `$22`: `$4F`; the nurse, `$23`: `$52`; `$27`: `$5E` (the Ring of Ruby) | | `Quests & $20`, or the Mist reached |
| The Poison | `$26`: `$5C`; Mascon, `$33`: `$66`; `$36`: `$6D` | | Black Onyx, or the Branch reached |
| The Meteorite | Mist `$1F`'s man, `$46`: `$7D` | `$46`: `$7E` (with the Black Onyx; the Ace key) | the Ace key held, or the Branch reached |
| Battle Gear | Conflate, `$50`: `$81`; Daybreak, `$61`: `$8F`, `$64`: `$95` | | Battle Suit and Battle Helmet held |
| Ring of Dworf | Conflate, `$51`: `$83`; Daybreak's nurse, `$63`: `$94` | Conflate's Guru, `$55`: `$8C` (with the Battle Suit) | Ring of Dworf |
| The Magic Cane | Victim, `$42`: `$73`; Daybreak, `$61`: `$90`, `$65`: `$98` | | Magical Rod |
| Dragon Slayer | Dartmoor, `$71`: `$9B` | | Dragon Slayer held, the Demon's Ring, or Zenis reached |
| The Demon's Ring | `$72`: `$9D`, `$9E`; `$73`: `$9F` | the Fraternal Guru, `$74`: `$A2` (with the Dragon Slayer) | the Demon's Ring |
| The Evil One | `$71`: `$9C`; the Fraternal Guru, `$74`: `$A2` | | the end: building `$44`, the King's room of `ISCRIPT_KING_END_GAME` |

Places reached, each implying those before it: Forepaw (its screens, `$02-$03`
of the towns), the Mist (area 2, or Mascon and Victim, the towns' `$04-$07`),
the Branch (area 5, or Conflate and Daybreak, `$08-$0B`), Dartmoor (area 6,
or its town, `$0C-$0D`), Zenis (area 7), the end. They come from where the
hero is, the map's screens seen (`fax_map_seen`), and the furthest temple
(`TempleSpawnPoint`, or the highest Guru met, `fax_travel_visited`): Forepaw's
is 2, Mascon's (in the Mist) 3 and Victim's 4, Conflate's 5 and Daybreak's 6
(off the Branch), Dartmoor's 7. The towns hang off the regions so
(`AREA_TOWN_TRANSITIONS_DATA`): Apolune and Forepaw off the Trunk, Mascon and
Victim off the Mist, Conflate and Daybreak off the Branch, Dartmoor town off
the Dartmoor region.

**Lines left out**, each something someone says that tells the hero what to
do, and why it gives no quest:

- `$27` (Eolis, "I want you to see the king right away"), `$32`, `$33` (the
  guard, "The King is waiting for you."): they point at the King, whose "!"
  shows and whose own line gives his quest.
- `$2C` (Eolis's Guru, "If you are going to see the king, take this ring"):
  he gives the ring, unasked; it is The Ring of Elf's next step.
- `$2D` (Eolis's Guru, "Visit all Gurus on your way."): advice on titles,
  with nothing to finish; Guru travel already keeps the Gurus met.
- `$28`, `$2F`, `$3C` ("Go straight ahead and you'll find the town of
  Apolune.", "There is a fountain in the town of Forepaw.", "There is the
  town of Forepaw ahead of you."): directions along the way.
- `$3D` (Apolune, "There are Dwarfs all over in the Tower of Trunk. Go out of
  town, turn right..."): directions to the tower The Mattock and The Wall
  send the hero to.
- `$3F` (Apolune, "You should have a shield."), `$47` (the mantra), `$49`
  (8 items), `$39`, `$71`, `$86`, `$A0`: advice.
- `$48` (Apolune, "There is a secret Guru in the Tower of Fortress. You'll
  find important information and a key there."): said from the first title,
  long before the Guri has a key to give; his own lines give the Spring of Sky
  and The First Spring when it is time.
- `$5B` (the spring of Trunk, "Come back with the medicine.") and `$56` (the
  Guri, "If you don't have a Joker key, come back again."): reminders of
  quests already given.
- `$63` (Mascon, "Did you get the pendant?"), `$64` (Mascon's shops outside
  town), `$6A` ("You'll get to the Tower of Suffer soon."), `$6E` (the Hour
  Glass), `$72` (Victim, "There is a magic hall outside town."), `$85`
  (Conflate, "There should be something you can get."), `$96` ("The castle of
  Dartmoor is a complex maze."): hints at optional things or places, with no
  item or place that would say they are done; `$85` hints at the magic cane,
  which `$73`, `$90` and `$98` give.
- `$67` (Mascon, "You need Wing Boots in order to get to the misty town of
  Victim."): by Mascon the hero has flown to the Spring of Sky, and the
  remaster keeps his Wing Boots (*Wing Boots, the Hour Glass and the Elixir*).
- `$74` (Victim, "The door to the Capital town below is open. Go to it
  now."): a drunk's rumor; he says so himself ("It was only a rumor.") and
  gives the Full Plate.
- `$7C` (the meteorite becomes Black Onyx), `$62`, `$68`, `$6C`, `$A1`: lore.
- `$99`, `$9A` (Dartmoor): script `$70`, which no screen places.

**Saying them.** At `Messages_Load` a line that gives a quest neither given
nor complete gives it. At the script's end (`IScriptAction_EndScript`, after
Block's hook), the quests it completed and then those it gave are said, a
line each ("Quest complete" or "New quest", then the name; the font has no
`:`): the script is pointed at Guru travel's SAY step with the line in message
`$C3`'s slot and returns through `IScripts_InvokeNextAction`, so the step's
END comes back to the hook, and the box closes after the last line. A quest
given and complete in one script (the spring of Trunk's old man, asked with
the Elixir in hand) says only "Quest complete". The quests a script
completed are those complete by then and not before; a pickup's item counts
as added, as its message says it will be (its list full, an Item is not). A
quest completed outside a conversation (a place reached, the rock pushed)
says so in a box below the HUD, where zone names show, once any zone name
has gone. (Walking out of a building into Forepaw's screen 2, the screen
leaves `TextBox_Closed` at 0 with no box up, as *The text box* explains; the
zone banner used to hold Forepaw's name for it, until it asked the nametable
for a box too.)

After a message of more than four lines (the King's) the box's rows are
rotated (*The text box*); while a quest line loads, the hook at `$F439`
(after Save files') puts back the line scroll the box was left with.

**The marks.** Each frame's picture walks the script of each person on
screen as the game would now (and in Seamless, those in the rooms around it,
from Seamless's copies, `fax_world_room_ram`): a line that gives a quest
neither given nor complete puts a "!" over him; a quest under way whose next
step he is (the walk reaches its step), a "?", which comes first. The mark is
the font's "!" or "?", made bold, in `$27` (gold) outlined in `$0F`, its
bottom 4 pixels above his box, over the middle of his 16 pixels, and only
in play: settled, not paused, alive, and bank 14 mapped, the main loop's (a
script, a shop or the Select menu waits for its frames with bank 12 in, or 13
while a character of text is read).

**The log.** The Quest log button opens it through the game's pause, at the
map's two branches (*The map*), after the map's hooks; neither opens over the
other. Black over the picture, the text box's frame over the native 256 x 240,
"Quests" on row 2, and from row 4 the active quests, a block each (the name in
`$37`, its objective, the King's Plea's springs), kept whole on a page, then
"Completed" and the names of the quests done, in `$10`. Left and Right turn
the pages; B, Start or the button close it.

**In the file**: `quests.given` and `quests.done`, the quests given and those
seen complete, as hex digits, the first for quests 0-3 (quest 0 its lowest
bit), in `fax_quest_table.h`'s order. Complete is otherwise read from the game
each time, so a quest done since stays done; continuing a file takes what is
complete without saying it. A file without `quests.given` (from a mantra, or
before the quest log) counts every quest complete as given, so the log shows
what was done.

## Enemy placement and fair entrances (`fax_entrances.c`)

**Where enemies start.** Bank 11's list for the screen (see "Areas, screens
and zones") is read by `Screen_LoadSpriteInfo` (`$C1B4`) one (entity, `YX`)
pair at a time into `CurrentSprite_Entity` (`$038B`), `CurrentSprite_YPos`
(`$038A`, `YX & $F0`) and `CurrentSprite_XPos` (`$0389`, `YX << 4`), and
`Sprites_PopulateNextAvailableSprite` (`$C205`, its only caller) copies them
into the highest free of the 8 slots (`CurrentSprites_Entities`, `$02CC`;
`CurrentSprites_XPos_Full`, `$BA`; `CurrentSprites_YPos`, `$C2`). Every way
onto a screen loads it so: `Screen_SetupSprites` for every `Screen_Load`, and
`Game_MainLoop` itself for a scroll (`@LAB_PRG15_MIRROR__dbaf` horizontally,
before the scroll plays, and `__db91` vertically). The enemies do not move or
show until the hero's **first update** on the screen (the main loop's first
pass after the load or the scroll), and the first picture with them on it is
the frame after that.

Seamless loads a neighbouring room on an isolated machine with the same
routine (`fax_world.c`'s `initialize`, `call(0xc1b4)`), before it allows
hooks there, so a hook at `$C205` does not run for it.

**What counts as unfair** (#44): an enemy that hits the hero on entering a
screen before the player can possibly react. Measured for every way onto every
screen with enemies, headless, from a temple's save state (its door edited to
lead there, as `tests/zones/zone_names.py` does): every door within an area,
region's door, town's edge, building's way out (at both columns: the door's
and the one to its left, since `Game_EnterBuilding` keeps `Player_PosX_Block
& $F0` and Up finds the door at `X + 7`), temple's way out, the Trunk's climb
(`$0C` and `$16`), and every screen edge: walking left or right at each row
the hero can stand at the edge, climbing a ladder up or down through it, and
dropping through a hole, holding the way in and letting go at the first
update. With nothing pressed after the arrival, the first hit
(`Player_InvincibilityPhase` set to `$3C`) is timed from the first update.
For the early ones, each move (walk left or right, jump straight or either
way, swing, Up back through the door or into the building, Down) was then
started at every frame k after the first update, to find the latest k that
still avoids the hit. The first picture with the enemy shows on frame 1, and
a player needs about 12 frames (200 ms) to respond to it, so **the first
input that can be a reaction is at k = 13**: a way in is unfair when nothing
pressed from then on avoids the hit.

**The moves.** `fax_entrances.c` hooks `$C205` and rewrites
`CurrentSprite_XPos` and `CurrentSprite_YPos` for a listed (area, screen,
entity, placement) before the routine copies them; for Seamless's rooms,
`fax_entrances_room_loaded` moves the listed enemies still on their placement
in the slots, right after `initialize` loads them. No ROM is changed and no
read override is used. Each new block is the nearest tried where, from every
way onto the screen, nothing hits the hero within 30 frames of the first
update (or ever, within 240), and where a hit within 60 frames has an escape
from k = 13 (see the checks under "Screen edges, ladders and drops", which
every move passed). Zone respawn replays a zone's way in after a
death, so two of these came back at every death: leaving the Tower of Mist
enters the Mist (onto the Raiden), and leaving the Castle of Fraternal enters
Dartmoor (above the wolfmen).

| Screen | Way in (hero) | Enemy | Stock: hit, latest escape | Moved to | Then |
|---|---|---|---|---|---|
| Mist `$06` | Mascon's temple (144,128); building `$22` (112,128) | Sir Gawaine `$89` | 0, none; 0, none | `$9E` (224,144), the floor's right end | No hit from any way in |
| Mist `$17` | The Tower of Mist's way out (80,160) | Raiden `$A5`, on the spot | 0, none | `$A9` (144,160) | 36, which a swing at 13 escapes |
| Mist `$1F` | Building `$26` (64,144); (48,144) | Sugata `$95` | 4, Up at 4; 20, Left at 16 | `$97` (112,144) | 66, 82 |
| Mist `$20` | The way out of the tower on `$50-$52` (96,144) | Sir Gawaine `$96`, in the same closet | 0, none | `$46` (96,64), on the closet's roof | No hit |
| Mist `$29` | Building `$20` (192,112) | Ikeda `$7D` | 4, Up at 4 | `$7E` (224,112) | 104 (and 56 from (176,112), stock 162, which walking right at 13 escapes) |
| Trunk `$06` | Building `$20` (160,80) | Zombie `$5B` | 8, Up at 8 | `$5C` (192,80) | 50, which a swing at 13 escapes |
| Branch `$0E` | The door from `$0F` (224,96) | Lamprey `$6D` | 3, none | `$6A` (160,96) | No hit |
| Branch `$13` | The door from `$14` (224,128) | Maskman `$8C` | 16, none | `$9D` (208,144), beside the door | No hit |
| Dartmoor `$02` | Building `$22` (160,64); (176,64) | Giant Strider `$49` | 0, none; 16, Up at 16 | `$A9` (144,160), the lower floor | No hit from any door |
| Dartmoor `$03` | Building `$24` (176,160) | Lamprey `$AA` | 3, Right at 3 | `$A8` (128,160) | 105 |
| Dartmoor `$08` | Building `$25` (16,64), (32,64) | Sir Gawaine `$42`, on the door | 0, none | `$46` (96,64) | No hit |
| Dartmoor `$0C` | The Castle of Fraternal's way out (144,64): he drops to the floor | Wolfman `$AB` | 10, none (in the air) | `$AD` (208,160) | No hit |

Why those blocks and not nearer ones, where nearer ones were tried:

- Mist `$06`: Sir Gawaine's reach is wide (he hits at 32 pixels); `$9D`
  (208,144) hits building `$23`'s way out at 0, and the platform the temple
  and building `$22` share has no block clear of both.
- Mist `$20`: the closet the tower opens into is five blocks wide, with the
  ladder down to `$27` at its left; `$98` hits the arrival at 0 and `$94` the
  climb up from `$27` at 28, as the stock block does.
- Trunk `$06`: `$5D` (208,80) hits the walk in from `$07` at the platform's
  height at 26.
- Dartmoor `$02`: every block of the upper floor is near one of its four
  buildings' doors (`$47` hits building `$21`'s way out at 20, `$48` building
  `$22`'s at 16).
- Dartmoor `$0C`: the floor's left blocks (`$A1`, `$A2`, `$A4`-`$A6`) hit the
  walk in from `$0D` at 12-28; `$AC` still hits the landing at 10.
- Branch `$13`: `$9A` (160,144) is hit at 57 with nothing that escapes it, as
  are `$8B` and `$9B`; `$9C` at 41. `$9D`, a block from the door, is not
  hit within 240 frames.

**Near misses left alone** (fair: something pressed after k = 13 avoids the
hit, measured as the door survey did: nothing hits him for 90 frames after
the first update):

| Screen | Way in (hero) | First hit, latest escape |
|---|---|---|
| Dartmoor `$02` | Building `$23` (208,64) | 48, Right at 40 |
| Trunk `$06` | Building `$20` (144,80) | 50, a swing at 40 (and moved: 92) |
| Dartmoor `$02` | Building `$21` (80,64) | 52, Up at 50 |
| Branch `$00` | The Mist's region door (96,160) | 52, Left or a swing at 40 |
| Dartmoor `$03` | Building `$24` (192,160) | 54, Up or Right at 50 |
| Trunk `$29` | The door from `$28` (48,144) | 56, Left at 32 |

Every other door, building and temple is first hit 60 or more frames after
the first update, or not at all.

**Screen edges, ladders and drops.** Crossing an edge, the destination's
enemies are loaded as the scroll starts (horizontally) or ends (vertically),
but none moves or shows until the hero's first update there, so these are
blind ways in too. Each was timed clean: the hero walked to the source
screen's edge, then (a fixture) its enemies were cleared, his invincibility
zeroed, his HP filled, and he was put back at the edge, so nothing from the
source screen can hit him. Then he held the way in across it (Left, Right, Up
or Down on a ladder; nothing for a drop), either still holding it on his
first update there or letting go on it. Up to 60 frames, every hit was
checked for an escape as the doors were: each move (stand, walk either way,
jump straight or either way, swing, swing walking either way, Up, Down)
started at k = 13, and where none worked, at every k from 13 to the hit. A
drop has little to work with: the hero falls with only a little steering,
and a swing in the air reaches only what is beside him.

An edge is unfair only when it is so both ways, holding and letting go: a
player who lets go as the screen changes can stand still and see what is
there, so an edge that hits only a hero still walking is a near miss. A drop
has no such choice: once he falls, he lands where the hole is.

The enemy to move for an unfair way in is the one whose removal clears the
hit, not the one nearest the hero when it lands: dropping into the Mist's
`$07` at column 9 lands between a hornet and Sir Gawaine, and it is Sir
Gawaine. Each new block is the nearest where, from every way onto the screen,
the moved enemy makes no hit within 30 frames that the screen without it
would not have. Where a screen has two moves, the second was searched with
the first made: on the Mist's `$08` the hornet at `$28` sits under the drop
and hits first, hiding that the one at `$4B` would be next. Then every move
was checked with all of its screen's moves made at once, against every way
onto the screen (its doors and every edge, held and let go): a hit within 60
frames that the moves caused or made earlier fails it unless a move started
at k = 13 or later escapes it; every unfair way in had to come out clear of
it; and each move had to be needed, its way's hit coming back without it.

**The frame counters.** Enemy scripts time themselves by
`SpriteUpdateCounter` (`$0383`, counted up on each frame that sprites update,
and read by 63 sites for animation and behavior phases) and, in one behavior,
`InterruptCounter` (`$1A`). So a way in's first hit depends on the moment the
hero arrives as well as on where the enemy starts: dropping into the Mist's
`$07` at column 9, Sir Gawaine hits on frame 3 with the counters at one
phase and on frame 18 at another. Every measurement here was taken with the
counters as the temple's state had them (`InterruptCounter` `$F3`,
`SpriteUpdateCounter` `$3E`, 21 frames before he walks out), which the
end-to-end test sets too.

The fixed ways in were also played at seven more phases (the counters
shifted by 1, 2, 3, 4, 7, 16 and 37 frames on the frame before the first
update), stock and with the moves, and so were the near misses below and
every door way onto a screen with a door move. The stock first hit moves in
a few (Mist `$07`: 3 to 19; Zenis `$0E`: 1 to 17; Zenis `$12`: 6 to 12; the
Castle of Fraternal's way out: 10 to 26), but with the moves each stays clear
or keeps its escape at every phase, and each near miss stays one: where
holding the way in is hit at another frame (Branch `$20`: 0 to 7, Dartmoor
`$04`: 4 to 10, Mist `$4E`: 4 or 19), letting go still is not.

An escape here means nothing hits him for 120 frames after his first
update, as the edges were measured; the door survey's near misses below used
90.

The table gives the earliest stock hit of each way in a move fixes (held and
let go for an edge; a drop has one), by the hero's column for a drop, and the
first hit once the screen's moves are made.

| Screen | Enemy (stock block) | Ways in it fixes: first stock hit | Moved to | Then: first hit |
|---|---|---|---|---|
| Trunk `$0D` | Zozura `$5E` (224,80) | walking left from `$0E` (row 4): 0 held, 1 let go | `$7E` (224,112) | 72, none |
| Trunk `$17` | Yuinaru `$5F` (240,80) | walking right from `$16` (row 4): 0 held, 0 let go | `$5E` (224,80) | 78, none |
| Trunk `$1D` | Yuinaru `$CC` (192,192) | dropping from `$26` (columns 10, 11): 19, 19 | `$BA` (160,176) | none, none |
| Trunk `$1E` | Yuinaru `$3B` (176,48) | dropping from `$1D` (column 10): 2 | `$2C` (192,32) | none |
| Trunk `$1E` | Charron `$99` (144,144) | dropping from `$1D` (columns 7, 8): 14, 14 | `$9B` (176,144) | 67, 32 |
| Trunk `$20` | Ikeda `$95` (80,144) | dropping from `$23` (columns 2, 3, 4, 5): 15, 10, 10, 10 | `$7F` (240,112) | none, none, none, none |
| Trunk `$20` | Ikeda `$99` (144,144) | dropping from `$23` (columns 6, 7, 8, 9): 15, 10, 10, 10 | `$8F` (240,128) | none, none, none, 70 |
| Trunk `$21` | Charron `$88` (128,128) | dropping from `$22` (columns 7, 8, 9): 12, 12, 38 | `$9B` (176,144) | 141, 149, 157 |
| Trunk `$23` | Yuinaru `$35` (80,48) | dropping from `$24` (columns 4, 5): 2, 2 | `$33` (48,48) | none, none |
| Trunk `$24` | Charron `$35` (80,48) | dropping from `$25` (columns 4, 5): 4, 2 | `$45` (80,64) | none, none |
| Trunk `$30` | Charron `$45` (80,64) | dropping from `$36` (column 4): 4 | `$46` (96,64) | 30 |
| Trunk `$32` | Snowman `$23` (48,32) | dropping from `$38` (columns 3, 4): 0, 32 | `$33` (48,48) | none, none |
| Trunk `$32` | Charron `$99` (144,144) | dropping from `$38` (column 8): 14 | `$A8` (128,160) | 101 |
| Trunk `$37` | Charron `$95` (80,144) | dropping from `$3C` (column 5): 14 | `$94` (64,144) | 41 |
| Mist `$04` | Ikeda `$22` (32,32) | climbing down from `$2A` (column 2): 2 held, 3 let go | `$34` (64,48) | 32, 32 |
| Mist `$07` | Sir Gawaine `$38` (128,48) | dropping from `$01` (column 9): 3 | `$48` (128,64) | none |
| Mist `$08` | Hornet `$28` (128,32) | dropping from `$02` (columns 7, 8): 1, 0 | `$46` (96,64) | none, none |
| Mist `$08` | Hornet `$4B` (176,64) | dropping from `$02` (columns 9, 10, 11): 4, 3, 3 | `$3D` (208,48) | none, none, none |
| Mist `$08` | Sir Gawaine `$98` (128,144) | dropping from `$02` (columns 6, 7, 8): 14, 1, 0 | `$A5` (80,160) | none, none, none |
| Mist `$09` | Hornet `$38` (128,48) | dropping from `$03` (column 3): 29 | `$28` (128,32) | 86 |
| Mist `$0F` | Bihoruda `$4C` (192,64) | dropping from `$05` (columns 11, 12): 3, 3 | `$4A` (160,64) | 111, 111 |
| Mist `$12` | Hornet `$68` (128,96) | dropping from `$0C` (column 6): 7 | `$66` (96,96) | 102 |
| Mist `$1A` | Raiden `$25` (80,32) | dropping from `$19` (columns 3, 4, 5): 10, 0, 0 | `$35` (80,48) | none, none, none |
| Mist `$25` | Yareeka `$29` (144,32) | dropping from `$1D` (column 8): 8 | `$2A` (160,32) | 33 |
| Mist `$27` | Lamprey `$3A` (160,48) | dropping from `$20` (column 10): 2 | `$39` (144,48) | 110 |
| Mist `$48` | Ripasheiku `$3A` (160,48) | dropping from `$3F` (column 10): 2 | `$37` (112,48) | 83 |
| Mist `$49` | Rokusutahn `$9C` (192,144) | dropping from `$40` (column 10): 14 | `$AC` (192,160) | 176 |
| Mist `$49` | Raiden `$A8` (128,160) | dropping from `$40` (column 10): 14 | `$B7` (112,176) | 176 |
| Branch `$04` | Maskman `$5A` (160,80) | dropping from `$06` (column 12): 25 | `$6A` (160,96) | 96 |
| Branch `$19` | Giant Strider `$29` (144,32) | dropping from `$14` (columns 11, 12): 26, 32 | `$39` (144,48) | 91, 97 |
| Branch `$19` | Giant Strider `$56` (96,80) | dropping from `$14` (column 4): 11 | `$54` (64,80) | 124 |
| Dartmoor `$03` | Yuinaru `$58` (128,80) | dropping from `$01` (column 8): 6 | `$56` (96,80) | none |
| Zenis `$03` | Geributa `$A5` (80,160) | dropping from `$10` (column 2): 16 | `$B5` (80,176) | none |
| Zenis `$03` | Geributa `$AA` (160,160) | dropping from `$10` (column 12): 18 | `$BA` (160,176) | none |
| Zenis `$0E` | Sir Gawaine `$29` (144,32) | dropping from `$09` (columns 7, 8): 1, 1 | `$38` (128,48) | none, none |
| Zenis `$10` | Zozura `$A6` (96,160) | dropping from `$0B` (column 6): 16 | `$A4` (64,160) | 66 |
| Zenis `$10` | Zozura `$A8` (128,160) | dropping from `$0B` (columns 6, 7, 8, 9): 16, 16, 16, 16 | `$A5` (80,160) | 66, 109, 161, 161 |
| Zenis `$10` | Zozura `$AA` (160,160) | dropping from `$0B` (columns 8, 9): 16, 16 | `$AE` (224,160) | 161, 161 |
| Zenis `$12` | Rokusutahn `$48` (128,64) | dropping from `$0D` (columns 6, 7): 12, 7 | `$58` (128,80) | none, none |

Why some blocks are further off:

- Trunk `$20` is one room 16 blocks wide, and the drop from `$23` lands
  anywhere in columns 2 to 12, so both Ikedas, which leap at the hero, go to
  the right wall (`$7F`, `$8F`): every nearer block is hit from some column
  within 15 frames.
- Zenis `$10` is a pit: the shaft from `$0B` comes down columns 6 to 9 onto a
  floor eight blocks wide, with its three Zozuras under it. They go to its
  ends (`$A4`, `$A5`, `$AE`); with any of them nearer, some column is hit
  within 25 frames with nothing that escapes it.
- Mist `$48` is Ripasheiku's room, and the drop from `$3F` lands on him at
  column 10; three blocks left (`$37`) the first hit is 83 frames in.
- Mist `$49`: the drop from `$40` lands between Rokusutahn and a Raiden. With
  Rokusutahn moved alone, the Raiden hits at 28 with no escape, so both move.
- Branch `$04`, Branch `$19`'s upper Giant Strider, Mist `$08`'s Sir Gawaine
  (and Branch `$13`'s Maskman at its door): the nearest blocks clear for 30
  frames still left a hit before 60 that nothing escapes, so these went to
  the nearest block with no hit within 60 frames.

**Near misses left alone at edges.** Hit only while still holding the way
in, before any reaction; letting go on the first update avoids it, or leaves
time to:

| Screen | Way in (hero's row or column) | Holding: first hit | Letting go: first hit, escape at 13 |
|---|---|---|---|
| Mist `$2B` | Walking left in from `$2C` (row 4) | 0 | 17, Right |
| Mist `$4E` | Walking right in from `$4D` (row 10) | 4 | none |
| Branch `$01` | Walking left in from `$02` (row 7) | 9 | 65 |
| Branch `$09` | Walking right in from `$08` (row 8) | 16 | 16, a jump or Left |
| Branch `$20` | Walking left in from `$21` (row 2) | 7 | none |
| Dartmoor `$04` | Walking right in from `$05` (row 4) | 10 | none |
| Dartmoor `$1D` | Walking left in from `$17` (row 4) | 9 | 16, Right |
| Zenis `$05` | Walking left in from `$0F` (row 4) | 0 | 63 |
| Zenis `$13` | Walking right in from `$12` (row 10) | 13 | 18, Left |
| Zenis `$0E` | Climbing up from `$13` (column 6) | 8 | none |

Three drops hit later with an escape that only a well-timed move finds: the
Mist's `$12` from `$0C` at column 8 (41; a swing walking left at 13),
Dartmoor `$03` from `$01` at column 6 (36; a jump to the right at 25), and
Zenis `$11` from `$0C` at column 4 (32; a jump at 20). Every other edge,
ladder and drop arrival is first hit after 60 frames, or not at all, or has
an escape at 13.

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
- With no items, the Select menu's item list still opens, showing "NO ITEM".
- `Player_RemoveItem` (`$9A6A`, bank 12) shifts the later entries down and
  decrements the count, leaving a stale byte after the end. `Player_AddToInventory`
  (`$9AF7`, bank 12; shops and scripts) overwrites the last entry when the
  category is full; `Player_PickUpItem` (`$C8CD`) refuses when it is full,
  after the item's line, and the item is gone all the same (with the
  remaster, Kept finds leaves it lying: see *Softlocks*).
- The rings, the Elixir, the Magical Rod, the Pendant and the Black Onyx are
  bits in `SpecialItems` (`$042C`), not list entries.
- **Picking an item up.** Touching an item sprite runs
  `Player_HandleTouchItem` (`$88FB`, bank 14): `JSR Player_PickUp` (`$C764`),
  which jumps by entity to the item's routine, then the sprite is removed.
  Each routine runs its "I've got ..." script first (frame-frozen; A closes
  it), plays sound 8, then adds the item. The Items go through
  `Player_PickUpItem`. The Battle Suit (`$58`, `Player_PickUpBattleSuit`
  `$C6EA`), Battle Helmet (`$59`, `$C70A`) and Dragon Slayer (`$5A`, `$C72A`)
  each have their own copy of one tail: `LDX count`, `CPX #$04`, `BCC`,
  `LDX #$03`, `LDA #$03`, `STA list,X`, `INX`, `STX count`, which appends, or
  replaces the last entry of a full list.
- **The Battle Suit and Dragon Slayer bug** (chipx86's *Bugs*: "Armor/Weapon
  cap logic swapped"). The helmet's tail reads `NumberOfShields`; the suit's
  reads `NumberOfWeapons` (`LDX` at `$C6F7`) and the slayer's
  `NumberOfArmors` (`$C737`), and each then stores into, and sets the count
  of, its own list. So the item lands at the other list's count: with fewer
  weapons than armor listed, the Battle Suit overwrites an armor and the
  count drops the ones after it (Leather and Studded listed, no weapons: only
  the Battle Suit is left); with more, the count takes in the bytes after the
  list: `$00` (the Leather Armor) where nothing was ever written, or entries
  `Player_RemoveItem` left, which in the original means equipping from the
  list with nothing worn (`Player_Equip` adds no old selection then), and with
  Quick shops a sale too (two weapons listed, an empty armor list holding two
  sold Studded Mails' bytes: Studded, Studded, Battle Suit). The other list
  is untouched. The Dragon Slayer does
  the same to the weapons. Equal counts hide it. Both are one of a kind, so
  in play neither list holds four when its item comes: the cap only matters
  after the bug itself. Each drop's behaviour
  (`SpriteBehavior_BattleSuitDroppedByZoradohna` and the slayer's, bank 14)
  clears the screen's sprites if the hero has the item: worn
  (`SelectedArmor`; for the slayer the weapon in hand, `Player_CurWeapon`) or
  in the first `count` entries. The Battle Suit and Dragon Slayer fix (`fax_inventory.c`)
  sets X to the right count at each `CPX`.
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

## Items in the world (bank 11)

**The sprite lists.** `AREA_SPRITE_ADDRESSES` (`$8210`, bank 11) holds a
pointer per area (`Area_CurrentArea`) to its screens' pointers, a word a
screen; in area 4 a building's, by its number (the numbers run far past
area 4's 10 screens of scroll data). Each points to the screen's list as
`Screen_LoadSpriteInfo` reads it: an entity and a block `YX` a sprite, to a
`$FF`; then the values (`CurrentSprites_Values`, an NPC's script) to a `$FF`,
and maybe `$80` and the screen's event (see *Screen events*).
`Screen_LoadAllScreenInfo` loads nothing for an area whose first screen's
pointer has `$FF` for its high byte. The longest list has 5 sprites; 8
slots take them (`Sprites_PopulateNextAvailableSprite` skips what does not
fit). The disassembly's labels number the screens in decimal, and not always
the pointer's (`SPRITE_INFO_APOLUNE_SCREEN_39` sits among 34 and 35): read the
pointers.

**What decides an item is there** is its entity's behavior script
(`SPRITE_BSCRIPTS`, bank 14), which switches to one behavior on its first
frame:

- `BSCRIPTS_OBJ_POTIONLIKE` (`$4B` Red Potion, `$4C` Poison, `$4D` Elixir):
  `SpriteBehavior_Fall`. Always there; the game keeps no state, so it is back
  whenever its screen loads.
- `SpriteBehavior_Glove` (`$48`) and `_Ointment` (`$4E`): there unless one of
  its kind is running (`DurationGlove` `$0428`, `DurationOintment` `$0427`,
  `$FF` when not) as the screen loads; no state either.
- `SpriteBehavior_RandomlyShowItem_*` (`$50` Mattock, `$55` Wing Boots, `$56`
  Hour Glass, `$5D` Red Potion, `$5E` Poison, `$5F` Glove, `$60` Ointment):
  `Screen_Is3Of4Visits` counts `Screen_TrackedVisitCount` (`$043A`), the
  counter only these calls move, and removes the sprite on 3 of 4 counts, so
  each shows on one counted visit in four. No state: one taken can show
  again.
- Held state: `$49` Black Onyx (`SpecialItems` `$01`), `$4A` Pendant (`$02`),
  `$57` Magical Rod (`$04`) and `$5C` Zorugeriru's Wing Boots (`Quests` `$08`)
  remove themselves once set; `$5B` Ripasheiku's Mattock too (`Quests` `$10`),
  but `Player_Spawn` clears that bit, so it is back after a death. The `$58`
  Battle Suit, `$59` Battle Helmet and `$5A` Dragon Slayer clear the screen's
  sprites if the hero holds the item (item 3 of its list: worn or selected,
  for the slayer `Player_CurWeapon`, or in the list's first count entries).
  Each of these but the Rod also shows or hides itself by whether its boss
  (`$2D` Ripasheiku, `$2E` Zoradohna, `$31` Zorugeriru, `$32` King Grieve) is
  on the screen (`Maybe_IsSpriteEntityNotOnScreen`).

Touching one runs `Player_HandleTouchItem` (`$88FB`, bank 14; see *Items and
the inventory*), which empties the slot after `Player_PickUp`.

**Every item lying in the world** (`tests/map/map_test` checks the count and
some entries against the ROM; area, screen and block `YX`; "room" is a
building, marked on the map at its door):

| Item | Entity | There | Where |
|---|---|---|---|
| Elixir | `$4D` | every visit | Trunk `$3C` (`$32`), Mist `$41` (`$A1`) |
| Red Potion | `$4B` | every visit | Trunk `$22` (`$6E`), Mist `$4D` (`$31`), Dartmoor `$13` (`$AE`) |
| Glove | `$48` | every visit, no Glove running | Trunk `$18` (`$31`), Branch `$21` (`$32`), Zenis `$0A` (`$6D`) |
| Ointment | `$4E` | every visit, no Ointment running | Trunk `$37` (`$33`), Zenis `$06` (`$B2`) |
| Red Potion | `$5D` | one in four | Trunk `$0C` (`$3B`), `$21` (`$7E`), Mist `$50` (`$31`), room `$37` (`$98`) off Dartmoor `$02` |
| Mattock | `$50` | one in four | Trunk `$0E` (`$5D`), Branch `$18` (`$58`) |
| Wing Boots | `$55` | one in four | Trunk `$2D` (`$41`), Mist `$11` (`$5E`), `$1B` (`$35`), `$32` (`$48`), room `$1F` (`$A3`) off Mist `$06`, Branch `$1C` (`$A9`), `$22` (`$92`) |
| Hour Glass | `$56` | one in four | Trunk `$13` (`$4D`), Mist `$30` (`$32`), Branch `$16` (`$72`), Dartmoor `$04` (`$5E`), `$12` (`$32`) |
| Glove | `$5F` | one in four | Branch `$08` (`$4E`), Zenis `$11` (`$AC`) |
| Ointment | `$60` | one in four | Trunk `$1D` (`$41`), Mist `$47` (`$42`), room `$43` (`$95`) off Dartmoor `$1F`, Branch `$04` (`$4D`), `$10` (`$48`), Dartmoor `$1A` (`$BE`) |
| Black Onyx | `$49` | until `SpecialItems` `$01` | Mist `$4C` (`$A4`) |
| Pendant | `$4A` | until `SpecialItems` `$02` | Mist `$34` (`$9E`) |
| Magical Rod | `$57` | until `SpecialItems` `$04` | Branch `$1E` (`$B9`) |
| Battle Suit | `$58` | until held | Branch `$1D` (`$BD`) |
| Battle Helmet | `$59` | until held | Branch `$07` (`$A4`) |
| Dragon Slayer | `$5A` | until held | Dartmoor `$15` (`$9D`) |
| Mattock | `$5B` | until `Quests` `$10` (back after a death) | Trunk `$15` (`$AE`) |
| Wing Boots | `$5C` | until `Quests` `$08` | Trunk `$3B` (`$3D`) |
| Poison | `$4C` | every visit | Trunk `$29` (`$31`), `$35` (`$A2`), Branch `$0B` (`$7D`), `$17` (`$56`), `$1E` (`$4D`), Dartmoor `$0E` (`$B1`), `$13` (`$A6`), `$14` (`$66`), `$1D` (`$B2`), Zenis `$09` (`$BE`), `$0D` (`$3E`), `$0E` (`$A1`) |
| Poison | `$5E` | one in four | Mist `$1E` (`$68`), `$1F` (`$A1`) |

That is 44 items and 14 Poisons; Eolis and the towns have none. The three
rooms each hold an enemy with their item (a Giant Strider, a Grimlock, a
Wolfman). No ring and no key lies anywhere (the rings and keys come from
shops and people; see *Doors and keys*).

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
With the remaster, the Red Potion lines are gone from the buy lists: the meat
shops sell them (*The meat shops*, below).

**The meat shops** are scripts, not shops: the Meat Salesman (portrait 8),
in seven towns (Apolune has none), each 13 bytes in bank 12 from `$A4A1`:
`88, 02 msg, 05 price, 13 1E, 0A 1E, 03 02, 00`, the question "Dried meat to
/ bring energy / for 50 Golds." (messages `$07`-`$0D`; B on it ends the
script), pay, add 30 HP, add 30 MP, "Thank you for shopping.", end. Their
prices, by script: Eolis (`ISCRIPT_EOLIS_MEAT_SHOP`, `$A4A1`, building `$00`)
50, Forepaw (`$A4AE`, `$0E`) 150, Mascon (`$A4BB`, `$17`) 180, Victim
(`$A4C8`, `$26`) 250, Conflate (`$A4D5`, `$2A`) 350, Daybreak (`$A4E2`, `$31`)
500, Dartmoor (`$A4EF`, `$3E`) 800. Each script's pay step is at its base +
3, so after it the script stands at its `ADD_HP`, base + 6. Daybreak's two
shops share the towns' screen `$0A`: the tool shop's door is block `$96`, the
meat shop's `$99` (checked in the game; reading `TOWNS_DOOR_LOCATIONS` and
`TOWNS_DOOR_DESTINATIONS` entry by entry puts each a door to the left). With
the remaster they are the Potion Sellers (*Features*).

**The two Magicians** are scripts, not shops. Eolis's
(`ISCRIPT_EOLIS_MARTIAL_ARTS_MAGIC_SHOP`, `$A35D`, building `$05`; despite
the name, the Martial Artist has his own) is `83, 02 15, 05 C8 00, 0A 28, 01
16, 00`: the question "I shall give magic for 200 Golds." (B on it ends the
script), pay 200, add 40 MP, "Watch your remaining power level." (`01`, B
cannot skip it), end. The magic hall's (`ISCRIPT_AFTER_VICTIM_MAGIC_SHOP`,
`$A368`, building `$1C`) is `83, 12 62 A377, 02 17, 05 B8 0B, 07 62, 01 79,
00`, and with Fire, `03 18, 00` at `$A377` ("Don't rely too much on magic.").
Its steps: `CHECK_FOR_ITEM` Fire at `$A369`, the question at `$A36D` ("Do you
know the magic of fire? I'll teach you for 3000 Golds."), pay 3000 at `$A36F`,
`ADD_ITEM` Fire at `$A372`, "It can be magic of justice or a magic of
destruction. It all depends on who uses it." (`$79`, which B cannot skip) at
`$A374`, `END` at `$A376`. So the script reaches `$A372` only once 3000 is
paid, and `ADD_ITEM` always gives (`Player_AddToInventory` overwrites the last
spell of a full list): that step is where the hero learns Fire. Fire comes
from nowhere else. The hall is on the Mist's screen `$29`, its door at block
`$7C` (`MIST_DOOR_LOCATIONS`: `29 7C 20 9E`), with the same room and the
Magician on the same step (`$71`) as Eolis's; Victim's man calls it "a magic
hall outside town" (`$72`).
The pay steps in the game ask 50, 150, 180, 200 (twice), 250 (twice), 350,
500 (twice), 800, 1000, 1800, 2500 and 3000 (twice): a price none of them asks
has to be served, which the Magician's lesson and the Potion Sellers do in
`IScriptAction_SpendGold` (*Features*).

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
4. A (`$8402`, the `BCC` target after the wait at `$83FA`):
   `Shop_GetPlayerHasSelectedItem` (`$8482`) refuses a weapon, armor,
   shield or spell the hero has (in hand or in its list), and a SpecialItems
   item (rings, Elixir, Magical Rod, Pendant, Black Onyx) he has. Other items
   can be bought again and again, until their list is full
   (`MAX_INVENTORY_SLOTS`, `$847D`: 4, 4, 4, 4, 8): an Items ID has bit 7
   set, the routine compares it only with the eight special IDs, and returns
   C clear. So a hero holding Wing Boots, the Hour Glass, a key or the Mattock
   can buy another (checked in the game: Forepaw's shop sold a second pair).
   Either refusal closes the list and ends with "You can't carry any more."
   (`$06`, at `$8469`). Then `IScripts_CheckEnoughMoney`; short, "This is not
   enough Golds." (`$03`, `IScripts_ShowNotEnoughMoney`, `$837C`).
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
in no list either, as in the original.

**Nor what the hero has for good**: a key (the A, K, Q, J and Jo keys), the
Mattock, the Wing Boots and the Hour Glass, while the feature that keeps it is
on (see *What the hero owns* below). These neither buy nor sell: in the buy
list they are OWNED, in the sell list not for sale. Before, the 100 Golds made
a farm of the A and Jo keys: their givers hand out another to a hero without
one, every time he asks (see *Doors and keys*), so a key fetched and sold over
and over paid 100 each time. The rule is `fax_shops_not_for_sale`, asked of
the item and the hero as he is when the list is drawn and when A is pressed.
The rest are safe to sell: the potions and the Poison, and the weapons, armor,
shields and spells, which no feature keeps. Without its keeper a key, the
Mattock, the Wing Boots or the Hour Glass is used up, so it sells as before; in
the one mod the keepers are always on. With the mod off, the shops are the
original's: a key, the Mattock, the Wing Boots and the Hour Glass sell where a
shop sells them, at half its price.

An item not for sale is listed with no price (its price cells are painted the
box's inside, since `Shop_Draw` would write "0"), the hint reads "Not for
sale" while the cursor is on it, and A on it does nothing: a hook at `$86E0`
re-enters the wait at `$86DB` by return, before the price is taken.

**What the hero owns, with Quick shops.** An item he already has for good keeps
its row in every buy list, so the list, its cursor positions and its pages are
the shop's own, but shows **OWNED** where its price was; the hint reads
"Already owned" while the cursor is on it, and A on it does nothing: a hook at
`$8402`, before `Shop_GetPlayerHasSelectedItem`, re-enters the wait at `$83FA`
by return, so no gold is taken and no refusal shows. Owned
(`fax_shops_owned`):

| Item | Kept by | Owned when |
|---|---|---|
| Wing Boots, Hour Glass | Recharging items | He has it: selected, or in the Items list |
| The A, K, Q, J and Jo keys, the Mattock | Reusable tools | He has it |
| Elixir | Recharging items | Charged (its `SpecialItems` bit) or spent |

The rule asks each keeper whether it is on (`fax_recharge_on`,
`fax_reusable_tools_on`), so an item counts only while it is kept; in the one
mod they always are. A charged Elixir counts either way, since the game refuses
a second. Shops sell only the J, Q and K keys; the A and Jo keys are in the
rule for completeness. OWNED is five letters, in the five cells `Shop_Draw`
gives a price (column 23, an entry's second row, right-aligned), painted over
the price as an unsellable item's is. In the sell list an owned item is not for
sale (above). Weapons, armor, shields and spells the hero has are not marked:
the game refuses them ("You can't carry any more."), as before.

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
| A | Mist's gate to Branch (screen `$1F`), the one A door | A gift: the man in the house on Mist `$1F` (Buildings `$22`, script `$46`, `ISCRIPT_A27C`) gives one if you hold the Black Onyx and no A key, each time you ask. With Quick shops a held key is not for sale, so this is no farm (*Shops*) |
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
- The message waits for A first, so in the original the Mattock leaves the
  status bar only after A on "I've used Mattock.", as the wall crumbles.
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
  opens nothing. That one is drawn as the brown wall in the green floor;
  with the remaster it is the floor's `$08`, and not breakable (see
  *Graphics bugs*).
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

- **Down+B works in flight with A held.** While the Wing Boots fly and A is
  held, `Player_CheckHandleVertMovement` takes Down as a climb down
  (`Player_CheckHandleClimbDown`, `AND #$DA`), which clears the jumping and
  falling bits every frame it is held, before `Game_MainLoop` reaches
  `GameLoop_CheckUseCurrentItem` (the pass's order: `GameLoop_UpdatePlayer`,
  the draws, `Player_CastMagic`, `GameLoop_CheckUseCurrentItem`,
  `Sprites_UpdateAll`, `GameLoop_CountdownItems`, ...). So A+Down+B in the
  air reaches the selected item's routine; without A the hero falls, and it
  does not (checked in the game: `Player_Flags` `$42` with A and Down held).
  An item used there runs before that pass's countdowns. Over TCP, B is the
  NES button (`action b`): a controller's B is Block (`pad b`).

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
- The two countdowns at the game's second, byte for byte:
  `Game_DecWingBootsDuration` (`$C59D`) is `LDA DurationWingBoots`, `BMI
  @_return`, `LDA Player_StatusFlag`, `ORA #$80`, `STA`, then at `$C5A8` `LDA
  InterruptCounter`, `AND #$3F`, `BNE @_return`, `DEC DurationWingBoots`
  (`$C5AE`), `LDA`, `BMI @_wingBootsDone` (`$C5B9`: clear `$80`, script
  `$96`), else `JMP UI_DrawTimeValue`. `Game_DecHourGlassDuration` (`$C5EB`)
  is `LDA DurationHourGlass`, `BMI`, then at `$C5F0` `LDA InterruptCounter`,
  `AND #$3F`, `BNE @_return`, `DEC DurationHourGlass` (`$C5F6`), `BPL
  @_return`, else script `$97` and `Music_Current` = `Areas_DefaultMusic`.
  So a site at either `LDA InterruptCounter` runs once a main-loop frame
  while that item runs and decides its second: an RTS skips it, and a jump
  to the `DEC` with the duration at 0 ends the item with the game's own line
  (Recharging items does both).
- `Player_UseHourGlass`'s cost is `LSR Player_HP_U`, `ROR Player_HP_L` at
  `$C5D8`, then `JSR UI_DrawPlayerHP` (`$C0BD`) at `$C5DE`, `LDA #$0F`, `STA
  DurationHourGlass`, `LDA #$0B`, `STA Music_Current`.
- **T:** is two ASCII tiles at PPU `$2058`. Only the Wing Boots draw it
  (`UI_DrawTimeValue`, queued through the PPU buffer at `$0500`: length 2,
  `$20`, `$58`, the digits); the Hour Glass never shows a time.
  `UI_DrawHUD` draws `00` there whenever it draws the whole HUD.
- In the picture, T: is at x 176-188 and its seconds at x 192-207, y 16-23.
  With the remaster it shows the selected item's seconds left while it runs
  (`fax_recharge.c`).
- `Game_ClearTimedItems` (`$C6AF`) sets all four durations to `$FF`. It runs
  from `Game_Start` and `Player_Spawn`, so every death ends them, but not on a
  Guru travel journey, which enters `Player_Spawn` past it: a flight or a
  freeze carries on in the other temple.
- **Shops sell a second.** `Shop_GetPlayerHasSelectedItem` (`$8482`, bank
  12) refuses only a weapon, armor, shield or spell already selected or
  listed, and a special item whose bit is set ("You can't carry any more.",
  message `$06`). The Wing Boots and the Hour Glass are Items, so a shop sells
  a hero holding them another, until his list is full (checked in the game:
  Forepaw sold a second pair). An earlier note here said shops never sell
  what you hold; that holds only for the Elixir. `Player_PickUpItem` adds a
  duplicate too. With the remaster, a find he holds adds nothing, and Quick
  shops lists a pair or an Hour Glass he has as owned, and won't buy it (see
  *Shops*).

| Item | Shops | In the world (bank 11's lists: area, screen, block `YX`) |
|---|---|---|
| Wing Boots | Forepaw 2,800, Conflate 5,500 | Sprite `$55`: Trunk `$2D` (`$41`), Mist `$11` (`$5E`), `$1B` (`$35`), `$32` (`$48`), the room off Mist `$06` (building `$1F`, `$A3`), Branch `$1C` (`$A9`), `$22` (`$92`). Sprite `$5C`: Trunk `$3B` (`$3D`), Zorugeriru's |
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
the bit. With recharging items a spent Elixir is still the hero's
(`fax_recharge_elixir_spent`), so Quick shops lists it as owned, as it does a
charged one. Its bit is clear, so the game's own shop would sell one to a spent
Elixir; through Quick shops, buying one never charges it. A Guru, or one found
in the world, does, and a Guru says so (see *Features and what they use*).

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
#27's lowest band (30 s) was enough, with 15 s to spare. The original gave a
Novice 40 s. Flying on MP (#124), a Novice's full bar flies 40 s, and the
climb takes about 30 of its 80 MP (an MP every 32 frames: 979 frames and 30
MP in the end-to-end test), so he needs well under half a bar.

The dialogue also says the Wing Boots are needed "to get to the misty town of
Victim" (Mist `$22` and `$25` lead into it). One candidate: the ladder at the
top left of Mist `$04`, up to `$2A` and on through `$2B` and `$2C` to `$22`,
starts high above the floor, and a probe flew from the floor into `$2A` in
about 180 frames (3 s). Whether that is the climb the dialogue means, and the
rest of the way to Victim, were not checked.

**Recharging breaks no sequence** (the check from #6, again for #124's
flights on MP):

- It gives no item, so the first moment you can own Wing Boots is unchanged.
  The earliest sources (table above) are around Forepaw: its shop, and the
  Tower of Fortress (Trunk `$2D`, one visit in four), whose entrance is "right
  above this town".
- Flying again is not new reach. The original sells pairs in two shops and
  lets found pairs stack, so it gave any number of flights for gold. A kept
  pair flies again as soon as there is MP, which MP refill brings back.
- Flights longer than the original's longest (40 s, a Novice's) come only from
  Adept on (50 s on a full bar), Swordman (60 s) and Champion (80 s), and
  only on a full bar. Not checked: whether any place needs one flight of more
  than 40 s. Nothing in the dialogue points at one, and the late regions are
  gated by keys and rings.
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
- Drawing a screen or scrolling sideways loads its tiles a column at a time
  through `$0200-$021D`, over the text box's variables, and leaves the last
  column there: `TextBox_Closed` can read 0 with no box up (see *The text
  box*).
- **For tests:** set `Area_ScreenToTheRight` in a save state and walk off the
  right edge to reach any screen of the same area the game's own way.
  `CurrentSprites_Entities` (`$02CC`, 8 slots) set to `$FF` clears a screen's
  enemies (see *Eolis, for tests*).
- **Eolis screen 4's tower** (about x 160-208) is drawn in front of sprites:
  a hero or a Zozura behind it disappears. Stage a fight there left of x 150.
- `Area_LoadScrollData` (`$D127`) decompresses the four neighbours first,
  keeping only their edge row or column (`LastColumnLeftScreen` and the
  like), then the current screen last (`JSR Area_LoadBlocks` at `$D1BB`,
  returning to `$D1BE`). Every way onto a screen in the real machine passes
  there; Seamless decodes its rooms in isolated calls instead (`decode()`,
  `initialize()`), where no hooks run.
- **Out of a door onto any screen, for tests (both views):** in a building,
  set `Area_SavedArea` (`$03D5`), `Area_SavedScreen` (`$03D6`),
  `Screen_SavedPalette` (`$03D7`, the screen's palette index: Trunk's 6, the
  towers' 7) and `Player_SavedPosXY` (`$03D8`, a block `YX`) and walk out:
  `Game_ExitBuilding` sets the outside up from them. Unlike
  `Area_ScreenToTheRight`, it also works with Seamless, whose rooms follow the
  scroll data, not `$0066-$0069`.

## Screen events and the fountain's ladder (bank 15)

- A screen's sprite list in bank 11 can end with `$80` and an event ID
  (`Screen_LoadSpecialEventID`), kept in `CurrentScreen_SpecialEventID`
  (`$042E`, `$FF` for none) while the hero is on it. `Screen_LoadSpriteInfo`
  loads it with the screen's sprites: for a scroll, while the scroll is set
  up, so before the first main-loop pass on the new screen.
- `GameLoop_RunScreenEventHandlers` (`$EF4B`), late in each pass of
  `Game_MainLoop` (after `GameLoop_UpdatePlayer`, the sprites and the menu),
  RTS-dispatches the ID through `SPECIAL_SCREEN_EVENT_LOOKUP_TABLE` (`$EF63`):
  0 the path to Mascon, 1 a boss, 2 the final boss. It is not called while
  the screen scrolls.
- **Bosses depend on their screen.** A boss is its screen's event as much as
  its sprite, and nothing records one killed: loading the screen again
  brings it back. `ScreenEvents_HandleBoss` (`$EF9E`, event 1), on its first
  pass on the screen, sets the event's bit 7 (`$81`), calls
  `LimitItemDurations` (a running Ointment or Hour Glass ends) and sets
  `Music_Current` (`$FA`) to `$0A`, the boss music (the sound engine sets its
  bit 7 once it starts it, so it reads `$8A`). Every pass after,
  `Sprites_HasBoss` (`$EFF8`) looks for a category-7 entity
  (`SPRITE_CATEGORIES_BY_ENTITY`, `$B544`) in `CurrentSprites_Entities`;
  once none is left it puts `Areas_DefaultMusic` back and sets the event to
  `$FF`, so it never runs again on that load. Emptying the boss's slot
  (`$FF`) is a kill as far as the handler is concerned. The final boss's
  `ScreenEvents_HandleFinalBossKilled` (`$EFD4`, event 2) waits for every
  slot to be empty (`Sprites_HasCurrentSprites`, `$F00B`), then sets music `$07`,
  plays sound `$04` and jumps to `EndGame_Begin` (`$D9AC`): 120 frames of
  the frozen screen, a fade to black, 120 more, then the King's room
  (`EndGame_BeginKingsRoomSequence`). Checked in the game
  (`tests/world/boss_arenas.py`).
- **The boss screens and what is beside them** (bank 11's lists, bank 3's
  scroll tables): every `SCREEN_EVENT_BOSS` screen is joined both ways to a
  neighbour: Trunk `$15` to `$14` (left); `$3B` to `$3A`, `$3C` and `$36`
  (below); Mist `$34` to `$33` and `$39` (below); `$4C` to `$4B` and `$43`
  (above); Branch `$07` to `$08` (right); `$1D` to `$1C`; Dartmoor `$15` to
  `$16` (right). Dartmoor `$15`, King Grieve's room, is walled on all four
  sides (columns 0 and 15 and rows 0, 11 and 12 solid): the hero comes in
  by the door on `$14` (block `$9E`, palette `$0E`, `DARTMOOR_DOOR_LOCATIONS`
  entry 2) at block `$91`, and its own door there leads back to `$14`; `$16`
  beyond its right wall holds a second King Grieve
  (`$32` at `$41`) with no screen event. Zenis `$00`, the final boss's, is
  joined to nothing. Boss-category entities stand on other screens with no
  event, so with no boss music and no handler: Borabohra (`$2F`) on Trunk
  `$3A`, Ripasheiku (`$2D`) on Mist `$33` and `$48`, Zoradohna (`$2E`) on
  Zenis `$01`, King Grieve on Dartmoor `$16`, Rokusutahn (`$11`) on Mist
  `$39` and the Castle of Fraternal's `$10`.
- `LastColumnLeftScreen` (`$03E6`) and `FirstColumnInRightScreen`
  (`$03F6`), 16 bytes each, hold the screen to the left's last column of
  blocks and the screen to the right's first, one a row: `Area_LoadScrollData`
  copies them as a screen loads, so the edge the hero would walk across can be
  read before he does. `BlockProperties` (`$043C`, 128 bytes) holds the area's
  block properties two a byte (the even block's in the low nibble), and
  `BLOCK_PROPERTY_IMPASSABLE_MAP` says which stop him (1 solid, 5 breakable
  floor, 6 pushable, 7, 8, `$0B` the Mattock's walls).
- **The path to Mascon** is event 0, on Trunk `$28` only: the dried fountain
  ("right above this town", Forepaw), with the door to the Mist (block `$10`,
  the Q key) on a ledge at the top left. `$28`'s only neighbour is `$27`, to
  the right. Pushing the fountain's stone (`Player_CheckPushingBlock`, with the
  Ring of Ruby, `SpecialItems` `$40`, and the three springs, `Quests & 7`, for
  `$60` updates) runs
  `Game_OpenPathToMascon` (`$D6F5`): the stone (`$88`) moves aside, 30 frames
  pass, it clears, and `Game_DropLadderToMascon` (`$D76E`) puts a ladder block
  (`$20`) at column 2 of rows 2-8 (`$22` to `$82`), one every 4 frames, with
  sound `$17`, and sets `Quests` `$20` (on Trunk `$28` only). Both wait on
  `WaitForInterrupt`, so the main loop stands still meanwhile. The blocks go
  into `ScreenBuffer` and the nametable (`Area_SetBlocks`): screens load from
  ROM, so the ladder is not in `$28`'s data.
- So every later visit, `ScreenEvents_HandlePathToMasconEvent` (`$EF69`)
  runs on the first pass: with `Quests` `$20` it clears its ID, clears the
  stone (blocks `$56`, `$66`, moved to `$57`, `$67`, then those cleared too:
  air, `$42`) and drops the ladder again, without the 30 frames; without it,
  it clears its ID and returns.
- **The original's bug (the Retranslation's "fountain ladder bugfix", which
  this ROM has too):** that first pass runs `GameLoop_UpdatePlayer` before the
  event. If it walks him off the screen, `Area_ScrollTo` makes `$27` the
  current screen and `Area_LoadScrollData` loads `$27`'s blocks into
  `ScreenBuffer` at once, and the event, still `$28`'s, then writes into
  `$27`: the ladder at column 2, rows 2-8, through `$27`'s middle floor, and
  air at column 7 of rows 5-6, widening the gap in that floor. The scroll
  draws `$27` from `ScreenBuffer`, so both show, and the game goes by them: he
  can climb that ladder through the floor. They last until `$27` loads again.
  Walking left into `$28` and holding Right as the scroll ends does it: the
  scroll keeps the buttons it began with, and the first pass after it reads
  the pad (`tests/ladders/ladders.py`). A door on that pass is no trouble:
  `Screen_Load` loads the other screen's event with its sprites
  (`Screen_SetupSprites`), so only a scroll leaves `$28`'s behind, and `$28`
  has no screen above or below.
- `Game_DropLadderToMascon` checks for the Trunk region (`Area_Region` 1) and
  screen `$28` before setting `Quests` `$20`; that is the only guard, and it
  does not stop the blocks.

## Sprites and tile slots (banks 7 and 8)

- **`Sprite_Draw`** (`$F072`, fixed bank) draws an appearance from its tile
  info in bank 7 (callers map it: `Sprite_SetAppearanceAddrFromOffset` for
  entities and magic, `Sprite_SetPlayerAppearanceAddr` for the hero,
  `Sprite_DrawPortraitPartAppearance` for portraits). A header of 4 bytes:
  rows - 1 (high nibble) and columns - 1 (low), X offset, Y offset, the flip
  pivot; then, row by row, a cell each: a slot and an attribute (palette in
  bits 0-1, `$40` and `$80` the flips, XORed with the sprite's), or `$FF` for
  an empty cell, one byte. A cell's OAM tile is its slot plus
  `Sprites_PPUOffset` (`$33`): 0 for the hero and cast magic, `$90` for a
  portrait, an enemy's tile base for an enemy. `Sprite_TileInfo_ReadAddr`
  (`$3A`) points past the header, and Y walks the cells, so the byte read is
  `ReadAddr + Y` in both loops: `Sprite_Draw_Standard` (`$F0EC`) places the
  columns left to right and `Sprite_Draw_FlippedHoriz` (`$F181`) right to
  left, reading the same bytes in the same order. Each loads a cell's slot
  (`LDA (Sprite_TileInfo_ReadAddr_L),Y` at `$F12F` / `$F1E3`, then `CLC` at
  `$F131` / `$F1E5`) and its attribute (`$F139` / `$F1ED`, then `EOR
  CurrentSprite_FlipMask` at `$F13B` / `$F1EF`).
- **The hero's slots** are what his gear's loaders put in the sprite pattern
  table at `$0000`, in this order, each over the last (`Player_DrawSprite`
  from `Player_SetArmor`/`Weapon`/`Shield` and a death, through the PPU
  buffer; `Player_DrawSpriteImmediately` from `Screen_SetupNew`):
  - the armor, from its list in bank 8 (`TILES_PLAYER_ARMOR_ADDRS_INDEX`,
    `$8012`), `PLAYER_ARMOR_TILE_COUNTS` + 1 tiles from slot 0: Leather Armor
    and Studded Mail 52 (slots 0-51), Full Plate 53 (0-52), the Battle Suit
    51; with a shield (not the Battle Helmet) its `_WSHIELD` list, 40 (Full
    Plate 41). Tile `t` of a list is at `TILES_ARMOR_START` (`$81A4`) + 16t;
  - the weapon, at slot `$38` (`$0380`), or the Dragon Slayer's 9 tiles at
    `$34` (`$0340`), through `PLAYER_WEAPON_TILE_PPU_ADDRS`;
  - the shield, 6 tiles at slots `$30-$35` (`$0300`); the Small Shield's are
    its list's `00 01 06 07 0C 0D` (`TILES_SHIELDS_SMALL`, `$8192`) from
    `TILES_SHIELDS_START` (`$9104`).
  So a slot no loader of the current gear fills keeps what an earlier one
  left there.
- The appearances: the hero's are `TILEINFO_PLAYER_ADDRS` (`$A9F1`), 8 poses
  a gear set (0-2 walking, 3 jumping, 7 climbing), in the order of the armor
  lists; then the weapons' (64-98). Entities' are `TILEINFO_ENTITY_ADDRS`
  (`$9036`); `SPRITE_APPEARANCE_PHASE_OFFSETS` (bank 14) gives an entity's
  first (the Myconid, entity `$1B`: `$31`, so 49 and 50). Cast magic's is
  `SPRITE_MAGIC_IMAGE_ADDRS_U[CastMagic_Type]` + the frame
  (`CastMagic_SetAppearance`): Fire is 155 and 156.
- **The Fire spell flickers by design**: `CastMagic_FinishHandler_Fire` draws
  it only when `InterruptCounter` has bit 1 set, 2 frames in 4, and picks its
  frame from bit 2, so each frame shows 2 frames in 8. Thunder draws on the
  other two (bit 1 clear).

## Graphics bugs

What the original draws wrong on the USA ROM, found by chipx86's notes, the
Faxanadu 40th Anniversary Edition's notes ("minor animation bugs with Studded
Armor and Fire") and the Faxanadu Retranslation's patches (Japanese base,
version 1.05+: "Armor graphic bugfix", "Fix Fire", "Fix Myconid Palette",
"Fix Fortress tile", "Fix Jerky Vendor Portrait", "Nurse cross graphic
bugfix", title screen colors), each checked in the game with the remaster
off. The Retranslation's IPS patches land at the same bank addresses as the
USA ROM in banks 1, 7 and 8, which pins down what each of its fixes changes.
Graphics fixes (`fax_graphics.c`) fixes the five that show:

| Bug | Where | Stock | Fixed | Seen |
|---|---|---|---|---|
| Studded Mail's climbing pose | bank 7 `$ABF9`: `SPRITE_TILEINFO_PLAYER_ARMOR_STUDDED_07`, row 1, column 0's slot | `$34` | `$30` | The chest shows whatever slot `$34` holds: Studded Mail loads 0-51. From a Small Shield worn before, its tile `$0C`; after Full Plate, its `$88`; with the Dragon Slayer, its first tile |
| Fire's second frame | bank 7 `$A5D3`: appearance 156, row 3, column 1's slot | `$66` | `$7E` | Its first frame's top right tile at the bottom right, where its own top right's belongs, mirrored (both frames are symmetric top to bottom) |
| The Myconid's second frame | bank 7 `$95E6`: `SPRITE_TILEINFO_ENEMY_MYCONID_01`, row 3, column 1's attribute | `$02` | `$03` | One foot in palette 2 (`$1C`, `$33`: blue and pink) instead of 3 (`$27`, `$30`) every other frame |
| The title's F | bank 10 `$9B69`: `START_SCREEN_PPU_ATTRIBUTES[9]` | `$0C` | `$00` | The 16x16 square at x 48-63, y 32-47 is palette 3 (the stars' greys) and holds only tile `$03`, the tip of the F's swirl: 5 grey pixels, and 2 of the F's red invisible (palette 3's color 1 is black) |
| Tower of Fortress floor | Trunk `$2D`, block row 11, column 3 (the literal at bank 1 `$9BE5`) | `$63` | `$08` | Under the left pillar, the Mattock wall's brown block in the green `1B 08 1B 08` floor; the right pillar stands on floor |

- **chipx86 names the wrong tile for Studded Mail.** His note says the
  missing tile is `LEATHER_46` (`$77`), provided by the Leather Armor's list.
  But Studded Mail's climbing pose otherwise matches the one it has with a
  shield and Full Plate's (`75 76 / 7D 7E / 7F 80`, palette 1 for the body),
  and both of those put `$7D` there: slot `$30` in Studded Mail's list. The
  Retranslation makes the same change.
- **The Fortress block was breakable.** `$63` is Trunk's Mattock block, so in
  the original the Mattock breaks it (and the block below) into a pit that
  leads nowhere (see *The Mattock and breakable walls*). The fix replaces the
  block, as the Retranslation's one-byte patch does, so it is plain floor.
  The other `$63`s, the wall east of the Tower of Trunk (`$0C`), stay.
- **Not on the USA ROM:**
  - *Nurse cross.* The Retranslation flips the 4th cell of the nurse's
    portrait's last row (bank 7 `$B2E6`, `$00` to `$40`) and gives slot `$17`
    its cross tile (bank 8 `$92C8`, `$CD` to `$34`). On the USA ROM slot `$17`
    is `$CD`, a solid tile (the cross censored to a T above it), so the
    missing flip changes no pixel.
  - *Jerky vendor portrait.* Its 5 bytes (bank 8 `$99CE`,
    `SPRITE_TILES_PORTRAIT_MEAT_SALESMAN` tile `$66`, plane 0 rows 3-7) on the
    USA tile would remove the outline where the shirt strap meets the
    shoulder; the USA portrait draws that edge whole.
  - The Retranslation's other graphics changes (its title, fonts and save
    icon) are its own.
- **Seen in the code, not checked in the game:** the Dragon Slayer's tiles
  (slots `$34-$3C`) and a shield's (`$30-$35`) share `$34-$35`, and the
  shield loads last, so the Dragon Slayer's poses that use its first two
  tiles (appearances 90, 92 and 93) would draw shield tiles; and Full Plate's
  climbing pose uses slot `$34` (its `$88`), which the Dragon Slayer
  overwrites.

## Softlocks

What was looked for (#38): places and states the hero gets into and cannot
get out of, short of a reset, on the USA ROM with the remaster off.

**Sources.**

- The Faxanadu 40th Anniversary Edition (fax40.pages.dev) lists "two known
  (though obscure) softlocks in the original game" among its changes but
  never names them; its v0.13's "one softlock in Branch" is in its own new
  screens. Its romhacking.net page (hack 9683) refuses automated fetches
  (403) and its Discord is not public, so the two stay unnamed.
- SDA's *Game Mechanics and Glitches* page: speedrun tricks (timers paused,
  the King's gold, double hits), no softlock.
- The Retranslation's changelog (github.com/UnsavoryMaggot/Faxanadu-Retranslation):
  its "fountain ladder bugfix" is its only fix for getting stuck.
- TASVideos' four Faxanadu runs: wall clips ("useless"), ladder tricks,
  death as a shortcut; no softlock.
- minirevver.weebly.com's Faxanadu page: "if your inventory is full when
  picking up the mattock then it disappears ... and you can't get it again",
  and "one dead end dungeon in the tower of mist area".
- tcrf.net served the fetcher no article; TV Tropes and AtariAge refused it
  (403). chipx86's disassembly marks no softlock among its BUG notes.
- The game itself: every interaction script (152, from `ISCRIPT_ADDRS_L/U`)
  walked for a loop with no question or menu in it (none), and a search of
  every outside area's screens, below.

**The search.** Each area's screens are decoded from ROM (*Screens and
blocks*: the bank's word table, the 2-bit stream), its block properties
are bank 3's table at the area's metadata + 2, one byte a block, low nibble
(`Area_LoadBlockProperties`), impassable by `BLOCK_PROPERTY_IMPASSABLE_MAP`
(1, 5, 6, 7, 8, 11), climbable 2 and 10, and its links and doors are what
*The map* reads. A model of the hero, a block wide and two tall, walks,
falls (ladders catch him), jumps (32 pixels up, measured: two blocks; about
three blocks across at speed), climbs and takes doors; then flies, for the
Wing Boots. A place he can reach from which no door or edge into a town can
be reached is a trap. Block-level, the model misses pixel-exact routes (it
first flagged the Mist's corridor, below), so every place it found was then
played in the game. Two engine rules it has to follow:

- **The row below the playfield is the next screen's.** Rows 0-12 show. The
  ground check (`Player_CheckIfPassable` down, `Y + $20 >= $D0`) reads
  `FirstRowBelowScreen`, the screen below's row 0, or block 0
  (`ScreenBuffer_Clear`) where there is none; the check up (`Y - 1 >= $F0`)
  reads `LastRowAboveScreen`. Sideways moves and `Player_CheckIfOnLadder`
  read `ScreenBuffer`'s own rows 13 and 14, which hold whatever the screen's
  stream decodes there, often the next screen's first rows (screens share
  data). So a solid block in row 13 is not ground: Zenis `$08`'s pit at
  column 14 drops him into `$0D` (checked in the game).
- **With no screen below**, `Player_MoveDownScreen` holds him at Y `$C0`
  (`Player_SetPosAtBottomEdge`), his feet below the picture.

| | Trigger | The original | With the remaster |
|---|---|---|---|
| Eolis's edge shafts | Wing Boots in Eolis, over the right wall of `$00` or `$08` | Stuck below the floor (reproduced) | Avoided: the Wing Boots are kept |
| A find with a full Items list | A Mattock, Wing Boots, Hour Glass or Red Potion touched with 8 listed | Said and lost (reproduced) | Fixed: Kept finds |
| The fountain's ladder in `$27` | Leaving Trunk `$28` on its first pass | No trap | Fixed already: Ladder fixes |
| The Tower of Fortress's floor | The Mattock on Trunk `$2D`'s block `$63` | No trap | The block is floor: Graphics fixes |
| The Mist's corridor `$2D-$2F` | Down the ladders from `$25` or `$26` | No trap | |
| The Evil One's room | Zenis `$01`'s door into `$00` | By design | |

- **Eolis's edge shafts.** Eolis `$00` and `$08` have no screen to their
  right, and their right wall (column 14, rows 3-11) stops a block short of
  the edge: column 15 is open from the sky through the floor (row 11) to the
  bottom. Flying up over the wall and letting go drops the hero down it to Y
  `$C0`. There A, Up and Right do nothing, and Left walks him along the open
  row under the floor into the town's screen (`$01`), whose edge stops him,
  still at `$C0`; no enemy is on either screen. A reset is the only way out.
  To have the Wing Boots in Eolis, the hero must die before any temple Guru
  has set his spawn point (it starts at 0, Eolis), after buying them in
  Forepaw or finding them: obscure. With the remaster the pair is kept, and
  once MP refill has brought MP back Down+B flies him up the shaft and out
  (`tests/softlocks/eolis_shaft.py`). Eolis is the one place found where a
  flight ends in a trap: Branch's open row under `$10`'s floor, reached by
  flying left out of `$11`'s floorless bottom, holds him at Y `$C0` too, but
  walking right takes him back into `$11`, which drops him into `$16`
  (checked in the game).
- **A find with a full Items list** (minirevver's Mattock). See *Items and
  the inventory*. Ripasheiku's Mattock (`$5B`) and Zorugeriru's Wing Boots
  (`$5C`) set their `Quests` bit before the line, and their sprites remove
  themselves while it is set: the Mattock is gone until a death clears `$10`
  (`Player_Spawn`), the boots for good. Not a strict softlock: the Trunk's
  wall can still be broken with the Tower of Trunk's other Mattock (one visit
  in four) or Ripasheiku's after a death, and Wing Boots are sold. Kept finds
  leaves the find lying (*Features and what they use*).
- **The fountain's ladder** (*Screen events and the fountain's ladder*). In
  `$27`, the misplaced ladder (column 2, rows 2-8) is reached by a jump from
  the floor and climbs to the middle floor's upper side (row 1 at its top);
  walking off its top to the left goes into `$28` at Y 48, on the ledge right
  of the fountain, from which walking left drops him to `$28`'s floor.
  Checked in the game: no trap.
- **The Tower of Fortress's floor** (*Graphics bugs*). The Mattock, used from
  the ladder at column 2 (`Y` in row 11, facing right), breaks rows 11 and 12
  of column 3 into a pit one block wide. He drops in and stands on row 13
  (`$29`'s first row: solid), steps left into the ladder's column and climbs
  out. Checked in the game: no trap.
- **The Mist's corridor**, `$2D`, `$2E` and `$2F` under `$25` and `$26`: the
  ladders down from both end in the corridor's top rows, high over its
  floor, with no door. It is the way back that is exact: from the floor onto
  the low ledge at `$2D`'s left (row 10, columns 1-3), at `X` 32 a jump with
  Up held catches the ladder at column 2 (rows 4-6; the climb check looks at
  `X + 7`), whose top is beside the block at column 3, row 5; from that block
  a jump right with Up held, taking off at `X` 58, catches column 4's ladder,
  which climbs into `$25`. Checked in the game. Its two enemies (entity
  `$22`) can kill him besides.
- **The Evil One's room** (Zenis `$00`): the door from `$01` leads in and no
  door leads out; the doors drawn at its right edge are in no door table. The
  fight ends in a win (`ScreenEvents_HandleFinalBossKilled`, when every
  sprite slot is empty) or a death, by design.

## Features and what they use

Each feature's C is in `src/` (`src/fax_<name>.c`), and `src/cyc_extras.c`
calls each one from the host. `mods/allocations.toml` lists each feature's ROM,
hook sites and shared addresses, and `tools/check_allocations.py` enforces it.

Every feature below is a trusted plugin of the one Faxanadu DX mod
(`mods/preloaded/packages/faxanadu.dx`, feature `dx`), so
they are on or off together, and may rely on each other. Each still checks
its own `enabled` flag, which the mod runtime sets for all of them at once:
"with X off" below means with the remaster off, or X turning itself off on an
unexpected ROM layout. Seamless (`fax_world.c`), the continuous world, is
one of them since #110, so the remaster always plays in its 424x240 view.

- **Seamless** (`fax_world.c`, docs/SEAMLESS-WORLD.md). Joins rooms the
  scroll table links both ways into one world, running each neighbour's
  actors on an isolated machine. Its save-state records (`faxanadu.world`,
  `faxanadu.world.hud`) are checked whether it is on or not and set aside
  when it is off, so a state saved with the remaster on loads with it off,
  as the other features' do; one saved with it off has none, and Seamless
  lays the map out again from the room the state is in. **Boss screens are
  arenas:** when it lays a map out (`topology()`), it first reads every
  screen's event for the area once, with the game's own
  `Screen_LoadAllScreenInfo` (`$C154`, bank 11) on an isolated machine
  (`CurrentScreen_SpecialEventID` preset to `$FF`, which the routine leaves
  alone for an area with no sprite lists). A room with event 1 or 2 whose
  boss lives (not entered since the area loaded, or its copy's event not yet
  `$FF`) joins nothing, so the scroll into it, and out while the boss lives,
  is the original's. When the active room is such an arena and its event
  turns `$FF` (`ScreenEvents_HandleBoss` found no boss), Seamless lays the
  map out again from it, and so it does when the original's own load (a
  door in the same area) brings a boss back into a room whose copy had none:
  it captures the live room before it decides. The flag is kept in its record
  (byte 28).

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
  them. B on its intro ("All Gurus meditate as one.", a `03` message) ends
  the script with its conversation under way; it forgets it when the next
  script begins (`IScriptOffset` 1 at `IScripts_InvokeNextAction`, which
  `IScripts_Begin` leaves after the entity byte, and no feature's steering
  does: they all enter a step at 0). Before #99 it did not, so by the code
  the next END of the shared SAY step (`$AE02`: Block's tutorial at a shop,
  the quest log, the Elixir line at Eolis's Guru) would have been taken for
  its intro's and steered to its offer (`tests/travel/travel_test.c`,
  `b_on_the_intro`; not seen in play).
- **Death keeps gold** (`fax_death.c`). Dying brings the hero back with the
  gold, experience and earned title he had. It hooks
  `Player_SetInitialExpAndGold` and returns at once when the far call's resume
  address is `Player_HandleDeath`'s, and leaves the start screen's call alone.
  It keeps no state.
- **Pendant fix** (`fax_pendant.c`). Weapons hit a quarter harder only with
  the Pendant. It hooks `@_checkGlove` (`$8884`, bank 14), where both sides
  of the reversed branch meet, and sets `Temp_00` from
  `WEAPON_STRENGTHS[Y]` and the Pendant bit before the Glove adds its bonus.
  It keeps no state, so it also runs in Seamless's isolated rooms, where the
  hero's swing at a room's edge can hit a neighbouring room's actor in that
  room's copy (its `hits` count only the live game's).
- **Battle Suit and Dragon Slayer fix** (`fax_inventory.c`). Each of the
  two pickups adds the item at its own list's count. It hooks each routine's
  `CPX #$04` (`$C6FA`, `$C73A`), right after the `LDX` that read the other
  list's count, puts the right count in X (and N and Z as `LDX` sets them)
  and returns 0, so the game's own cap, store and count follow: a full list
  has its last entry replaced, as the Battle Helmet's pickup and the shops
  do. It keeps no state, so it also runs in Seamless's isolated rooms (it
  counts pickups only outside them). See *Items and the inventory*.
- **Small fixes** (`fax_fixes.c`). Three of the original's small bugs
  (#39, #82), in two hooks that keep no state, so each also runs in
  Seamless's isolated rooms (counting only outside them). At
  `SoundEffect_Message_OnTick`'s `TAX` (`$86B2`, bank 5) it halves A, so the
  typing sound's ticks 3 and 4 read the table's `$01`, not the `$A9` past its
  end (see *How text types*). At `Player_HandleHitByMagic` (`$87DC`, bank
  14), after Block's hook, it skips the routine while
  `Player_InvincibilityPhase` is not 0 or Ointment runs, so magic on the
  shield does nothing during iframes or under Ointment, as magic on the hero
  does (see *Shields and enemy magic*).
- **Kept finds** (`fax_finds.c`). With the Items list full (8 listed), a
  Mattock, Wing Boots, Hour Glass or Red Potion found lying stays there: a
  hook at `Player_HandleTouchItem`'s entry (`$88FB`, bank 14) skips the whole
  routine (no line, no `Quests` bit, the sprite kept) when the touched
  sprite's routine would end in `Player_PickUpItem` and be refused, and plays
  the "invalid choice" sound (`$0D`) once a touch (a latch on the slot and
  `InterruptCounter`, 8 frames). Wing Boots or an Hour Glass the hero already
  holds go through, for Recharging items to take at `Player_PickUpItem`. The
  rings, the Magical Rod and the Elixir are bits, the battle gear has its own
  lists, the Poison only hurts, and the Glove and Ointment are timed: none is
  touched. Nothing in Seamless's isolated rooms. No ROM, no save-state record
  (a stale latch costs one sound at most). See *Softlocks*.
- **Magic button** (`fax_magic.c`). A bindable button (C, or Y on a
  controller) casts the selected spell. On a press, it ORs Up held and B new
  into the joypad masks at `Player_CastMagic`'s entry, when `Game_MainLoop`
  called it, and puts the real masks back at `GameLoop_CheckUseCurrentItem`'s
  entry, so no door, sword, item, NPC or next pad read sees them. If the NMI
  read the pad in between (a lag frame), it recomputes the changed mask
  against the real one. A press lasts one frame, as a pad read does. The
  bindings are `MagicKey` and `MagicButton` in `config.ini` [Game], in the
  host's own key and button names.
- **MP refill** (`fax_mana.c`). Once no MP has been spent for 240 frames (4
  s), MP climbs back toward 80, 1 every 30 frames. No hooks and no ROM: at
  each frame's end it takes MP lower than at the last as a spend, which
  starts the wait again (the frame of the spend does not count), as does
  each frame the Wing Boots or the Hour Glass runs on MP
  (`fax_recharge_draining`: they take an MP only every 24 to 64 frames, and
  the lesson's step is 15, so without it MP could come back mid-run), and counts
  the wait down only on frames the game played, those that end with
  `Game_MainLoop` waiting at its top (S `$FD`, `$DB4F` on the stack, bank 14,
  not paused or dead; see *MP and its bar*). A pause, the map, the quest log,
  a text box, a menu, a screen change, a death, a lag frame and the title
  hold it still. Each MP is `Player_AddMP` with A = 1 in a committed call, so
  the game draws the bar; its registers are put back after, and when the PPU
  buffer has less than 50 bytes of room (not empty), the MP waits for the
  next frame that has it. A new game's MP climbs from 0 like any other. The
  wait and the MP last seen are its save-state record; MP is in no save file.
  Once the Magician's lesson is learned (`fax_lesson_learned()`), the wait is
  120 frames and the step 15; a wait running when it is learned is cut to
  120.
- **The Magician's lesson** (`fax_lesson.c`, #105). Eolis's Magician no
  longer sells 40 MP for 200: he teaches the hero, once, for 30000 Golds, to
  recover MP twice as fast (MP refill's 120 and 15), and only a hero who has
  studied at the magic hall in the Mist (bought Fire from its Magician).
  Since the Wing Boots and the Hour Glass run on MP (#124), it also means
  flying and freezing again sooner. His
  script (*The two Magicians*) is steered at `IScripts_InvokeNextAction`,
  through Guru travel's steps and message `$C3`'s slot, served with the same
  bytes:
  - at his first action (`$A35E`, the question), what he teaches, "I can
    teach you / to recover magic / faster.", at Guru travel's SAY step
    (`03 C3 00`, `$AE00`); B on it ends the script. Learned, "I have nothing
    / more to teach / you." there instead, then its END.
  - A on it (the script at the SAY step's END, `$AE02`): not studied, "First
    study at / the magic hall / in the Mist." at the SAY step again, then
    its END, so gold is never checked; studied, his price, "It will cost /
    30000 Golds.", at Guru travel's ASK step (`16 C3 00 AE`, `$AE03`): A
    jumps to the SAY step, B goes on to `$AE07`. Who he teaches is one
    predicate with its line beside it (`ready()`, `NOT_READY_TEXT`). Two
    lines in a row from the one slot work: the first has typed out before
    the second is served, and the next `SHOW_MESSAGE` loads the slot afresh.
  - A on the price (the script at `$AE00`): back to his own pay step
    (`IScriptOrCHRAddr` `$A35D`, `IScriptOffset` 3). A hook in
    `IScriptAction_SpendGold` at `$8367` (`LDA Temp_0202`, after the two
    `IScripts_LoadByte`), while the script stands past that step (`$A363`)
    with 200 read, writes 30000 to `Temp_0202/3`, which the next
    instructions copy to `Temp_Int24` and check (`IScripts_CheckEnoughMoney`
    compares all 16 bits: 29999 is refused, 30000 paid). A hook at
    `IScripts_ProgressivelySubtractGold`'s `JSR Player_SubtractGold`
    (`$8347`), while his pay step runs and the script still stands at
    `$A363`, takes 140 more from what is left (if that much is) and makes
    `Temp_Int24` 150, as Double gold's count does on the add side: the game's
    own subtract takes 150 a frame, so 30000 counts down in 200 frames, then
    the game's 60 (measured: A on frame 10, gold taken on frames 11-210, his
    line on 270). At the game's 10 it would be 3000 frames. A shop's
    purchase, the hall's Fire, the keys and the doctor stand elsewhere and
    count 10.
  - Paid (the script at his `ADD_MP`, `$A363`): learned, and "Your magic
    will / now return / faster." at the SAY step instead of the 40 MP and
    "Watch your remaining power level.".
  - B on the price (`$AE07`): the SAY step's END (`$AE02`), so the box
    closes, as B on his question did.

  **Studied.** The same hook at `$826E` sees the magic hall's Magician's
  script (`$A368`) reach its `ADD_ITEM` Fire (`$A372`), which it does only
  once 3000 is paid, and from then the hero has studied; it steers nothing
  there. The flag stays when Fire is sold (Quick shops buys it). The file in
  play keeps `lesson.studied` (`yes` or `no`, at every save) and
  `lesson.learned` (`yes`, or no field). A file saved before
  `lesson.studied` has studied if Fire (spell 2) is in its `spells` or
  `selected_spell` (a hero who sold Fire before then cannot be told apart).
  The save-state record is version 2: learned, the conversation's phase, his
  line, studied; a version 1 record or none takes studied from the spells in
  RAM once the state is in (`fax_lesson_loaded`), and his old offer (line 1)
  comes back as the price.

  It acts only in his script and at those steps in a conversation of his,
  which it forgets when any script begins (`IScriptOffset` 1), so a
  conversation that B or a lack of gold ended leaves nothing behind. Its
  site at `$826E` comes before Recharging items', which stays last. After a
  load his line is served again (`fax_lesson_loaded`, after Recharging
  items'). If the overrides cannot be set, he sells his MP as before for the
  session (the hall still counts). `lesson_state` over TCP. The price is ten
  times the hall's Fire and twice Daybreak's Tilte (15000), the dearest
  thing a shop sells: a late-game purchase, since he teaches only after the
  hall.
- **Potion Sellers** (`fax_potions.c`, #127). The seven meat shops sell a
  Red Potion instead of their dried meat, each at the price its town's tool
  shop asked (Eolis 160, Forepaw 400, Mascon 600, Victim 800, Conflate 1000,
  Daybreak 1200, Dartmoor 2000), and the tool shops stop selling them. The
  meat shops' scripts (*The meat shops*) are steered at
  `IScripts_InvokeNextAction`, as the Magician's lesson steers his, through
  Guru travel's steps and message `$C3`'s slot, served with the same bytes:
  - at the first action (base + 1, the question), his line, "A Red Potion
    to / restore you for / 160 Golds." with his town's price, at Guru
    travel's ASK step (`$AE03`): the game adds its flashing "?", as to the
    original's question. B goes on to `$AE07`, which goes to the SAY step's
    END (`$AE02`): the box closes.
  - A (the script at the SAY step, `$AE00`): with the Items list full
    (`NumberOfItems` 8, `MAX_INVENTORY_SLOTS`), the hook handles the call and
    enters `IScripts_ShowFinalMessage` (`$837E`) by return with A = 6: the
    game's "You can't carry any more.", then the end, as a tool shop refuses
    a ninth item. The room is checked before the gold, as a tool shop does.
    Else his own pay step (base + 3). A hook in `IScriptAction_SpendGold` at
    `$8367` (after the Magician's lesson's site there) makes his meat's price
    the potion's in `Temp_0202/3`, so the game takes it 10 a frame, or says
    "This is not enough Golds." and ends the script.
  - Paid (the script at base + 6, his `ADD_HP`): the hook points the script
    at his thanks (base + 10) and handles the call, entering
    `IScriptAction_AddInventoryItem` after its `IScripts_LoadByte` (`$83A2`)
    with A = `$90`, the Red Potion: `Player_AddToInventory` adds it, and the
    game's item box shows it, its picture and "RED POTION", until A, as a
    gift's does. Then "Thank you for shopping." and the end. No HP, no MP.

  It acts only in a conversation of his, which it forgets when any script
  begins (`IScriptOffset` 1); its site at `$826E` is after the Magician's
  lesson's and before Recharging items', which its two handled calls skip
  (that site acts only at a Guru's first action). It checks each script's
  bytes before steering it; one that differs, or a line that cannot be
  served, leaves the dried meat for the session, and then
  `fax_potions_selling()` is false and the tool shops keep their potions.
  Quick shops asks it at `OpenShop`'s `JSR Shop_Draw` (`$83F7`, after
  `Shop_Populate`): while it is true, a buy list loses its Red Potion and the
  entries after it move up (`fax_shops_without`), so the cursor, OWNED and
  "Not for sale" work as before. Nine tool shops sold one, "After Mascon"'s
  (300) among them. Selling a Red Potion is unchanged: half the shop's own
  price from its list in the ROM (Eolis 80, Dartmoor 1000), or at a key shop
  half the lowest (80). The save-state record (3 bytes: version, phase,
  shop) puts his question back in the slot after a load
  (`fax_potions_loaded`). `potions_state` over TCP. Seamless keeps the
  status bar it showed when play stopped (`frozen_hud`, `fax_world.c`), so
  during the conversation the gold does not count down on screen; it shows
  the new amount once the conversation ends, as after any counter's.
- **Double gold** (`fax_gold.c`, #102, #104). The King gives 3000 instead of
  1500, counted in the original's 150 frames, and once a game; every coin a
  monster leaves pays twice its value. Five hooks, each returning 0, with no
  ROM and no read overrides (the budget is full). Its one piece of state is
  whether the King has given; everything else is decided from RAM and the
  registers (see *Gold*).
  - At `IScriptAction_AddGold`'s `JSR IScripts_ProgressivelyAddGold`
    (`$8531`, bank 12), when the script stands at the King's `END` (`$A359`)
    and `Temp_0202/3` holds 1500, it writes 3000 there. From here he has
    given (outside Seamless's isolated rooms).
  - At `IScripts_ProgressivelyAddGold`'s `JSR Player_AddGold` (`$8560`, bank
    12), the routine's 10 already taken from `Temp_0202/3` and put in
    `Temp_Int24`, when the script still stands at the King's `END`, it takes
    10 more (if that much is left) and makes `Temp_Int24` 20: the game's own
    add pays 20 a frame, so his 3000 counts up in 150 frames, as the
    original's 1500 did, and the cap still holds. Selling reaches the routine
    from the sell list with the script in a shop's, so it keeps 10 a frame.
  - At `IScriptAction_IfPlayerHasGold`'s no-gold `JMP
    IScripts_SkipAddrAndInvoke` (`$862A`, bank 12), when the King has given
    and the script stands at his `CHECK_GOLD`'s word (`$A352`), it sets
    `IScriptOrCHRAddr` to `$A358` and `IScriptOffset` to 0: the skip's two
    loads (`$A358-$A359`, his `ADD_GOLD`'s high byte and `END`) leave the
    script at `$A35A`, where a hero with gold goes. So after his gift he says
    "There is nothingmore I can do tohelp you." to anyone. The quest log's
    marks walk his script by the gold, but both his lines give The King's
    Plea, so they are the same either way.
  - Whether he has given is the field `gold.king_given` (`yes` or `no`) of
    the file in play, written at every save, and its save-state record
    (version 1: the version, then 0 or 1). A new game has not given. A file
    from before the field, or with it damaged, has given if The King's Plea
    is in its `quests.given` (`fax_quests_given_in_file`): he has spoken,
    which in nearly every game is the plea his gift follows (see *Gold* for
    the rest). A file without `quests.given` either has not given. A save
    state from before the record takes it from the quest log's own record,
    once every record is in (`fax_gold_loaded`, from the host's
    `state_loaded`).
  - At the text box's reader, after `LDA (Temp_Int24),Y` (`$F4DF`, `CMP
    #$FF`, fixed bank), while `MessageID` is `$35` with bank 13 mapped, the
    byte read from `$8C81` or `$8C82` (`Temp_Int24` + Y, so past a page
    carried into `Message_StartAddr_U` too) and still the stock `1` or `5`
    becomes `3` or `0` in A: the plea says "I shall give you3000 Golds.".
    The compare after it sets the flags from A.
  - At `Player_HandleTouchCoin` before its add (`$8B9F`, `LDA Gold`, after
    the sound), it stores `Gold` as the gold it should end with less the
    coin's value: the gold plus twice the value (X picks the slot, as the
    game's `ADC CurrentSprites_Values,X` does), held at `$FFFFFF`. The game's
    own add of the value then lands there and never carries out of
    `Gold_U`, so where the original wraps past the cap, this stops at it.
    It runs in Seamless's isolated rooms too, on their scratch machine, and
    counts only outside them.

  Every coin in the game is a monster's, so nothing else is doubled: no
  gold lies in the world, and selling, the title's gold and spending are
  untouched. `gold_state` over TCP gives the gold, whether the King has
  given, the gifts doubled, his count's pays of 20 (`king_pays`) and the
  times he was sent on to "nothing more" (`nothing_more`), the plea's digits
  served and typed (`plea_digits`, on or off, for a test's picture), and the
  coins, what they paid and how many reached the cap.

- **Zone names** (`fax_banner.c`, with `fax_zone.c` and `fax_text.c`).
  Entering a zone with a different name shows it below the HUD for 2 s, in a
  text box drawn over the presented picture (after Seamless composes its
  own). No hooks and no ROM: it reads RAM in `frame_end` once the screen has
  settled. A temple whose way out is not where the hero last stood means a
  respawn or a Guru travel journey, and makes the next zone's name show. The
  banner is held while a box is open or the game is paused, and hidden behind
  boxes; a box is open by `TextBox_Closed` and the nametable together
  (`fax_text_box_showing`; see *The text box*). Its save-state record keeps
  the banner as it was. Its drawing, `fax_banner_draw`, takes any line, so
  Slain by draws in the same box.

- **Slain by** (`fax_slain.c`, drawn by `fax_banner.c`). When a hit takes the
  hero's last HP, the banner says what it came from, "Slain by a Sugata", from
  that frame through the dissolve, fading out at frame 248 of its 256, before
  the Guru speaks, and puts away any zone name showing. It hides behind a
  box, as zone names do (`fax_text_box_showing`). Two hooks, both only
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
  Meat Salesman is the Potion Seller (he sells Red Potions). The King and the
  Gurus are "of" a place: a temple's town by temple number (so
  the Guru whose door opens on the Mist is Mascon's, as in Guru travel), else
  the zone at the building's door (`Area_SavedArea`, `Area_SavedScreen`),
  a tower or castle by its own name ("Guru of Fraternal"). A name over 16
  characters loses the tab's blank cells, so no tab is wider than the dialogue
  box. No hooks, no ROM, no state: it reads RAM and the nametable when the
  picture is presented, and draws after Seamless and zone names.
- **DX on the title** (`fax_title.c`, with `fax_text.c`). The word on the
  logo, just under FAXANADU's letters, centred on the logo across the
  trunk's left edge (its cells x 120-135 natively, lit on 120-134 and y
  116-122), in the
  title's own letters (the dialogue font's A to Z are its letter tiles, pixel
  for pixel) and palette entry 3 (`$38`, the logo's light yellow), with a
  black outline (palette entry 0, the backdrop `$0F`) a pixel wide all round,
  the diagonals too, so it reads over the trunk's dither. Only the letters
  and the outline are drawn; the logo shows everywhere else. It is drawn
  over the presented picture after speaker names and before Save files'
  menu. A hook at `@_afterCheckHandleInputFarJump` (`$FC7A`) that only
  observes notes each frame the title's loop runs; the word shows on the
  presents after such a frame, so it goes with the frame Start leaves the
  loop. In a wider picture (Seamless, the wide modes) it follows the native
  picture's origin; a picture scaled smaller than the native one is left
  alone. Its save-state record is whether the loop ran last frame, so a state
  loaded on the title shows the word before a frame runs. No ROM.

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
- **Ladder fixes** (`fax_ladders.c`). The top of a ladder: at the BNE after
  `Player_CheckHandleClimbUp`'s passable check (`$E30C`), when there is nothing
  over his head but the step would take him off the ladder (the game's own
  `Player_CheckIfOnLadder` a pixel higher, in an isolated call), it takes the
  branch to the routine's return, as a solid block does, and clears climbing,
  as the original's drop did: he stays at the top (`16r - $1F`), never falls,
  and A jumps from there once Up is let go. Not while flying (the Wing Boots
  with A held), and not at `Y = 0`, where the step goes on to the screen
  above; the Trunk's climb's switch blocks are climbable, so it climbs into
  them as before. The fountain's ladder: at `ScreenEvents_HandlePathToMasconEvent`'s
  entry (`$EF69`), unless Trunk `$28` is the screen loaded, it clears
  `CurrentScreen_SpecialEventID` and returns, as the event's own "not yet"
  path does; the event runs on the next visit. No ROM, no save-state record.
- **Holding on to ladders** (`fax_ladder_hold.c`), the 40th Anniversary
  Edition's change: a hit no longer knocks him off a ladder. At
  `Player_HandleKnockback`'s entry (`$E153`) it returns at once when he has
  can climb, is not jumping and has no ground under him (`Player_CheckIfPassable`
  down, in an isolated call). He keeps his place, his flags and his hold; the
  hit's damage, invincibility and facing stand, and the d-pad does nothing for
  the three updates, as in any knockback. On the ground knockback is the
  game's, and in a jump past a ladder its first update is (it ends the jump).
  A feature of its own because it changes a rule rather than fixing one. No
  ROM, no save-state record.
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
  `fax_items.h`). The Wing Boots and the Hour Glass are kept, and both run on
  MP with no cooldown (#124, after #27's 60 s cooldowns), and the Elixir is
  kept and a Guru charges it again. Its sites are in the fixed bank, but for
  `$826E` in bank 12 (below). The costs are one table and two constants at
  the top of `fax_recharge.c`, in played frames (a pass of the main loop) a
  MP, so every cost is exact: the Wing Boots take an MP every 32, 40, 48 or
  64 frames by title band (2, 1.6, 1.33 and 1 MP a second of the game's 64
  frames: a full bar flies 40, 50, 60 and 80 s), and the Hour Glass one
  every 24 at any title (40 MP for its whole 15 s). Each takes its first MP
  a whole step after it starts, and its last on the frame MP reaches 0.
  - `Player_UseWingBoots` and `Player_UseHourGlass`, at entry: one that is
    running is ended, at this pass's countdown (`GameLoop_CheckUseCurrentItem`
    comes before `GameLoop_CountdownItems`), so Down+B again lands a flight
    (in the air with A held, or standing: *Using them*) or ends a freeze,
    with the game's own line. With 0 MP either plays the "invalid choice"
    sound (`$0D`, `Sound_PlayEffect` in a committed call), as a spell he
    can't afford does nothing. Both return to the main loop as the routine's
    RTS would.
  - `Player_ClearSelectedItem` (`$C4BF`), shared with reusable tools: skipped
    when the JSR came from `$C581` or `$C5D0`, so the item stays selected and
    drawn. `fax_items.h` lists every caller and who keeps its item.
  - The `STA DurationWingBoots` at `$C592`: the flight begins, after its
    line, and A becomes its seconds on the MP there is, which
    `UI_DrawTimeValue` draws.
  - The `LSR Player_HP_U` at `$C5D8`: the freeze begins, and the site jumps
    to the `JSR UI_DrawPlayerHP` at `$C5DE` (push `$C5DD`, return 1), past the
    original's halving: the freeze costs no HP.
  - The `LDA InterruptCounter` at `$C5F0` (`Game_DecHourGlassDuration`) and
    at `$C5A8` (`Game_DecWingBootsDuration`), each reached once a main-loop
    frame while its item runs: that frame of the run. MP goes on schedule
    (stored at once; its bar drawn with `Player_SetMP` in a committed call
    when the PPU buffer has room for both of its commands, else at the next
    frame that has, as MP refill gives its MP), the duration is set to the
    seconds left, rounded up, and the site returns 1, an RTS, so the game's
    own second never ticks. At the run's end (0 MP, the freeze's 15 s, or
    Down+B) it sets the duration to 0 and jumps to the `DEC` (`$C5F6`,
    `$C5AE`), which wraps it to `$FF`: the game's own end, "The power of the
    ... is gone." and, for the Hour Glass, the area's music. A
    `DurationHourGlass` it did not set is `LimitItemDurations`' 0 on a boss's
    screen: the freeze stops taking MP, and the site returns 0, so the
    game's own second ends it within a second, as in the original. Both
    running at once drain together, and each ends on the frame it finds MP at
    0 (the Hour Glass's site runs first in a pass).
  - `GameLoop_CountdownItems`, before the game's own countdowns: T: shows the
    selected item's seconds left while it runs, the flight's or the freeze's
    at the MP there is and all that drains it, rounded up (a full bar's 40 s
    shows 40). With anything else selected, or nothing, T: is the original's:
    the flight's while the Wing Boots fly, whatever is selected, else `00`
    (#119; the Hour Glass never showed a time). It is queued through the PPU
    buffer as `UI_DrawTimeValue` queues it, when `PPUBuffer_HasCapacity`
    would allow it, when it changes, and every 16 frames while not 0, since
    `UI_DrawHUD` draws `00` there. A bar still to draw is drawn. A spent
    Elixir whose bit came back (found) is charged.
  - MP refill (below) holds while either runs (`fax_recharge_draining`): no
    MP comes back during a flight or a freeze, and its wait starts from the
    run's end.
  - `Player_UseElixir`: the Elixir is spent. `SpecialItems`' bit stays the
    game's own "a revive is in reserve", so `Player_ReduceHP`, the Player page
    and the Spring of Trunk go by whether it is charged.
  - No shop charges a spent Elixir: Quick shops lists it as owned
    (`fax_recharge_elixir_spent`) and ignores A on it.
  - `Player_PickUpItem`, with A the Wing Boots or the Hour Glass and the hero
    holding one (selected or listed): nothing added, even with the list
    full; one is all he needs. Found during a run, the run goes on.
  - `Game_ClearTimedItems`: both runs end (a death, a new game, a mantra, a
    continued file; not a Guru travel journey, so a flight or a freeze
    carries on, taking MP, in the other temple). Zone respawn brings the hero
    back through `Player_Spawn` too (it skips only the `JSR
    Game_SpawnInTemple` after this call), so a death ends both wherever he
    comes back.
    `Game_ShowStartScreen`: everything forgotten. A mantra keeps a charged
    Elixir (the bit) and loses a spent one.
  - `IScripts_InvokeNextAction` (`$826E`, bank 12): at a Guru's first
    action (a temple Guru's `SET_SPAWN_POINT`, which only their seven scripts
    use, or `$A0BD`, Eolis's Guru's `CHECK_FOR_ITEM`), a spent Elixir is
    charged, before he speaks. So any Guru conversation counts: the plain
    mantra one with Save files and Guru travel off, and with them on,
    whichever of Save files' choices is picked, or B. Their hooks at the same
    address never redirect those two actions (Save files undoes the spawn
    point's raise at the next action). The Guru in the Castle of Fraternal
    sets no spawn point, and charges nothing.
  - **The Guru says so** (#99). At that first action his line is chosen:
    a spent Elixir, "Your Elixir is / restored. Come / back when used.",
    every time; a charged one and no Guru has told the hero yet, "If your
    Elixir / is used, a Guru / will restore it.", once; else none. Either
    line tells him (`recharge.elixir_told`), so after the restored line he
    never hears the other. It opens the conversation, before the Guru's own
    words (his greeting or a new title, Eolis's "Visit all Gurus", or the
    ring), and so before Save files' choice and Guru travel; the quest log's
    lines come at a script's end, and Block's never in a Guru's. The line is
    a native message in his box: the script is pointed at Guru travel's SAY
    step (`03 C3 00`, `$AE00`) with the line in message `$C3`'s slot, both
    served as Block and the quest log serve them, and at the step's END
    (`$AE02`) it is pointed back at the `IScriptOrCHRAddr` and
    `IScriptOffset` it had. Where: before the first action no other hook at
    this address steers. Eolis's Guru's `CHECK_FOR_ITEM` is that already.
    A temple Guru's `SET_SPAWN_POINT` is not (Guru travel meets him there,
    and Save files holds the spawn point at it: steering away and back would
    have it run unseen and raise the spawn point), so the line goes before the
    `CHECK_UPDATE_PLAYER_TITLE` that follows it in all seven, and the choice
    waits one action (`PENDING`). At `$AE02` Guru travel has no conversation
    of its own going (`meet()` at `SET_SPAWN_POINT`, or its reset when a
    script begins) and Save files acts there only after its saved line; this
    site is the last of the five at the address, so each of them sees the
    Guru's own actions and the step's END, never the SAY step it points at.
    B on the line ends the conversation, as on the Guru's greeting
    (`IScriptAction_ShowMessage`); the next action seen is then another
    script's, and the line is forgotten. A Guru greets a hero with no
    Elixir as before. If the overrides cannot be set, the Gurus charge it
    silently for the session.
  - Save files: the file in play keeps `recharge.elixir` (`spent`, or no
    field: the core `special_items` says whether it is charged) and
    `recharge.elixir_told` (`yes`, or no field: a Guru has said either line,
    so a continued file never hears "If your Elixir is used" again). They
    are written whenever the file is (`FAX_SAVE_WRITING`: a Guru's save, and
    every spawn's), read back on `FAX_SAVE_CONTINUED`, and reset by
    `FAX_SAVE_NEW_GAME`. Nothing of the Wing Boots or the Hour Glass is kept:
    continuing spawns the hero with no timed item running, and neither has a
    cooldown. #27's `recharge.wingboots` and `recharge.hourglass` (seconds
    until ready) are read no more, and its next write drops them from a file.
  - Its hooks do nothing inside Seamless's isolated simulations. Its 20-byte
    save-state record, version 3 (a version; the flight running, its frames
    to the next MP, and whether it is ending; the freeze running, ending, its
    frames so far, to its next MP, and the `DurationHourGlass` it set; the MP
    bar still to draw; the Elixir spent, whether a Guru has told the hero,
    and the Guru's line: waiting or showing, which, the action it goes
    before, and where the script goes back to) is its whole state while
    playing. A state saved while the line shows gets it back in the slot
    after the load (`fax_recharge_loaded`, after Guru travel's record has put
    its own last line there). Version 2 (17 bytes, with the cooldowns and a
    continued file's cooldowns) and version 1 (9 bytes, before the lines,
    nothing told) still load: the cooldowns are dropped, and a run going on
    the game's timer goes on as a run on MP, once RAM is in
    (`fax_recharge_loaded`): a flight from the MP there is, a freeze for the
    seconds its `DurationHourGlass` has left.
  - `fax_recharge_charge` tells where each item stands: not the hero's,
    ready, or used (the Wing Boots or the Hour Glass running, the Elixir
    spent). The status bar (Charges, below) shows the Elixir's.
    The Elixir's bit
    decides first, so one found while spent is ready the moment it is picked
    up, a frame before `GameLoop_CountdownItems` forgets it was spent.
- **Charges** (`fax_charges.c`, #113, #117). The status bar shows the
  Elixir left of its `M:` and `P:` bars, level with them (rows 16-31, its
  picture 16 x 15), since it refills both when it revives the hero
  (`Player_FillHPAndMP`), as Super Metroid shows a reserve tank beside
  Energy. It is drawn against the band's left edge, so 8 pixels from the `M`
  and the `P` (the band's column 0 is blank), with the item box's own picture
  (*The status bar*) in the status bar's palette when charged, in greys when
  spent (each color's NES grey by its brightness, a step darker, never
  black, so the shape stays: `fax_charges_grey`), and not at all when the
  hero does not have it; its place stays. The moment it revives him it greys
  and blinks, 8 frames on and 8 off (`InterruptCounter & $08`), for 96
  frames not counting those a text box shows, so through "I've used Elixir."
  and 1.5 s after. The Wing Boots and the Hour Glass are not drawn (#113 had
  them in a row past the item box, with the Elixir): they run on MP, and T:
  counts the selected one's run (Recharging items, above). Drawn at present time, after
  Seamless composes its picture and before every other feature's drawing,
  only while the frame shows the status bar (`fax_text_hud_showing`), and
  only where there is room: Seamless lays the status bar band out with the
  Elixir's place (`fax_charges_width`, `fax_world_hud_x`), and the stock
  256-wide view, the remaster off, has none. No hooks, no ROM, and nothing
  written to the machine. Its 3-byte save-state record, version 2 (a
  version, the Elixir's state at the last frame's end, its blink left)
  keeps a blink going across a load; a version 1 record (#113's 7 bytes,
  the three items' states, then their blinks) loads with the Elixir's, and
  a state without one starts with no blink, and an Elixir spent in it does
  not start one.
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
  save-state record is each slot's warning count and the band. A
  reflection's committed call runs no hooks, so it reports its 12 to Damage
  numbers itself (`fax_damage_enemy_hit`), before the call, while the sprite
  is still Sugata.
- **Damage numbers** (`fax_damage.c`). Every hit that takes HP shows how much,
  in the HUD's digits (see *Damage, dealt and taken*), rising a pixel every 3
  frames from 8 pixels above the box it landed on, centered (the enemy's
  extents box; the hero's 16 × 32), for 60 frames, fading over the last 16:
  color 3 of sprite palette 1, the shield's `$00 $25 $30`, which every area
  has: white (`$30`) for damage dealt, pink (`$25`) for damage taken, read
  from palette RAM so they fade with the screen, outlined in `$0F`. Three
  sites, all observing (return 0): `$8891` and `$81FD` take `Temp_00` and X,
  and `Player_ReduceHP`'s `SEC` (`$C091`, not its entry) takes
  `Arg_PlayerHealthDelta`, rounded to the nearest whole HP and at least 1, so
  the same enemy always shows the same number though the bar's drop varies
  with the fraction. An amount of 0 shows nothing, and a hit the game stops
  (iframes, Ointment, Block, a shield that takes all of a shot) never gets
  there. A kill shows the hit's whole damage. Inside an isolated machine
  (Seamless's neighboring rooms, where Sugata's flash still runs) nothing
  shows. A number belongs to the area and room it landed in: Seamless draws
  it at that room's place (`fax_world_room_origin`), so it stays put in the
  world as the camera moves, and the original view only on its own screen,
  and not while the screen scrolls; another area (a door, a building) drops
  it. A new number stays where its hit landed and moves any older one in the
  same room that it would touch out of the way, up if that one was higher,
  else down, with two rows between outlines; as every number rises at one
  pace, they never meet after. At most 16 show; a new one replaces the
  oldest. They hold while a box is open (`fax_text_box_showing`, not
  `TextBox_Closed` alone) or `Game_PausedState` is nonzero, hide while a box
  is open, never cover the HUD, and while
  `Player_HP_U` is 0 (from a killing hit until the respawn) keep below the
  zone banner's box (8 to 32 pixels under the HUD), where Slain by
  (`fax_slain.c`) says what killed him.
  Drawn after Sugata's band, so it does not grey them, and before the zone
  banner, which covers them. Its save-state record holds the numbers (16 bytes
  each), and a state loaded with the remaster off restores none.
- **Quick shops** (`fax_shops.c`). Up on a shopkeeper opens his buy list at
  once, Select switches to the sell list and back, a purchase or a sale keeps
  the list up with the cursor where it was, and B leaves. A shop buys
  anything carried (see *Shops*), 8 to a page: Up from the top or Down from
  the bottom turns the page. No ROM and no read overrides. It steers the
  script at `IScripts_InvokeNextAction` (its first action goes to BUY; BUY + 3
  and SELL + 3 go back to their list) and jumps by return at eight sites of
  its own: OpenShop's entry, past "What would you like?" to `$83EA`;
  `$8463`, past the purchase's close; `$86BC`, where it replaces the sell
  scan's list with a page of everything carried and goes on at `Shop_Draw`
  (`$86D8`), past "Which one would like to sell?", or, with nothing to sell
  and a list open, through `Maybe_Shop_DrawTextBox` first; `$86E0`, back to
  the wait for an item not for sale (one no shop buys, or one the hero owns);
  `$86F8`, past the sale's close;
  `$84F0`, in `IScripts_WaitForMenuInput` before the menu update, where it
  puts the cursor back, Select or a page turn goes through
  `Sound_PlayEffect` (`$0B`) to the other step, and B goes through
  `Maybe_Shop_DrawTextBox` to `IScriptAction_EndScript`, so the list is closed
  when the script ends, as after the original's purchase, and a line Block
  adds there (its tutorial, after the first shield) shows in the dialogue box;
  `$838E`, where a shop's message goes back to the list; and `$8402`, the buy
  list's A, back to the wait (`$83FA`) for an item the hero owns (see
  *Shops*), which it asks reusable tools and recharging items about
  (`fax_reusable_tools_on`, `fax_recharge_on`, `fax_recharge_elixir_spent`),
  as it does for the sell list's. A ninth site, at `$83F7` (`JSR Shop_Draw`,
  after `Shop_Populate`), only writes the list: while the Potion Sellers sell
  the Red Potions (`fax_potions_selling`), a tool shop's without it.
  Presses while a list is drawn, or the gold counts, are lost, as in the
  original: the game reads the pad only in the list's wait. The hint line
  ("Select to sell", "Select to buy", "Not for sale", "Already owned") is drawn
  on the list's row 26 and the page ("1 of 2") on row 9, at present time,
  while the list's four frame corners show; an unsellable item's price cells
  are painted the box's inside, and an owned item's too, with OWNED in them.
  Its save-state record holds the conversation (17 bytes).
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
- **Equip indoors** (`fax_indoor.c`). The Select menu equips anything in a
  building, and the hero keeps his weapon in hand there (see *Weapons and the
  menu in buildings*). At `$DE0B`, right after `Game_EnterBuilding` clears
  `Player_CurWeapon`, it sets it from `SelectedWeapon`, before
  `Screen_SetupNew` loads the hero's tiles; at `Player_SetWeapon`'s `BEQ`
  (`$EDF1`) and the menu's (`$8B88`, bank 12) it clears Z, so both go on as
  outdoors. The game then draws the weapon and swings it with its own code.
  When Zone respawn swaps `Game_SetupExitBuilding` in at `$DAAF`, `$DE0B` is
  not reached, and that loader sets the weapon itself. No ROM, no read
  overrides, no state.
- **Graphics fixes** (`fax_graphics.c`). The five bugs of *Graphics bugs*,
  each fixed where the game reads the byte, so it draws them right itself:
  - Sprites: at each of `Sprite_Draw`'s four loads (`$F131`, `$F13B`,
    `$F1E5`, `$F1EF`: the instruction after it), with bank 7 mapped
    (`CurrentROMBank`), when `Sprite_TileInfo_ReadAddr + Y` is `$ABF9`,
    `$A5D3` or `$95E6` and A its stock value, A becomes the right one (N and
    Z as the load would set them), so OAM gets the right slot or palette.
  - The title: at `PPU_WriteTilesFromCHRRAM`'s `STA PPUDATA` (`$F8B7`), with
    bank 10 mapped, the byte read from `$9B69` is written as `$00`.
  - The Tower of Fortress: at `$D1BE`, after `Area_LoadScrollData`'s load of
    the current screen and before anything draws or collides with it, Trunk
    `$2D`'s `ScreenBuffer[$B3]` goes from `$63` to `$08`. Seamless decodes its
    rooms in isolated calls, where hooks do not run, so it calls the fix's
    `fax_world_set_room_fixup` callback on each room it decodes
    (`decode()`, `initialize()`); the fix sets it while on.
  The hooks keep no state, so they run in Seamless's isolated rooms too (a
  neighbour's Myconid is fixed). TCP `graphics_fix_state` counts what each fix
  changed outside them. Seamless's redraw of the hero past a room's edge runs
  in an isolated machine without hooks, so there his climbing chest is the
  stock slot (as Block's glint and the falling pose are). No ROM, no read
  overrides, no state.

- **The map** (`fax_map.c`, laid out by `fax_map_layout.c`). A Map button (M,
  or LT on a controller; `MapKey` and `MapButton` in `config.ini` [Game])
  pauses the game on a map of the screens the hero has stood on, a page per
  region, tower and castle, with only Gurus, doors and the items still
  lying in the world marked; see *The map*. Three hook sites: the BEQs at
  `$E02F` and `$E040` in `GameLoop_CheckPauseGame`, shared with the quest log
  (whose hooks come after; neither opens over the other), where it changes
  only A and Z, and the game's own code pauses and unpauses; and
  `Player_HandleTouchItem` (`$88FB`, bank 14), after Kept finds' site there,
  where it only observes the slot touched (recharging items' pickup hook,
  `Player_PickUpItem`, runs later, inside `Player_PickUp`). Everything else
  it learns from RAM at the end of each frame and as it draws, at present
  time, after every other feature. Guru travel's visited Gurus
  (`fax_travel_visited`) count as their temples. Its save-state record
  (version 2; it still loads a version 1 record, with no item taken) is what
  the hero has seen (312 bytes), the last place, the map's page and whether it
  is open, and the items taken (8 bytes). No ROM, no read overrides.

- **Quest log** (`fax_quests.c`, the quests in `fax_quest_table.c`). Lines in
  dialogue, the log and the marks; see *Quests*. A hook at `Messages_Load`
  (`$F3F5`), which no other feature hooks, only observes A, the message. At
  `IScriptAction_EndScript` (`$82B4`), after Block's hook, it says its lines
  through Guru travel's SAY step and message `$C3`'s slot, as Block's tutorial
  does: it points the script at the step and returns through
  `IScripts_InvokeNextAction` (`fax_block_return_via`), so the step's END
  comes back to the hook. At `$F439`, after Save files' hook, it puts back
  the line scroll while its line loads. At `GameLoop_CheckPauseGame`'s two
  BEQs (`$E02F`, `$E040`), after the map's, it opens the log through the
  game's pause, as the map does; neither opens over the other. Its save-state
  record (31 bytes) holds the quests given and seen done, the lines still to
  say, the banner and the log's page. No ROM of its own and no read override
  of its own.

- **Fair entrances** (`fax_entrances.c`). The enemies placed where a way onto
  their screen puts the hero, so they hit him before anyone could react, start
  on another block (table in "Enemy placement and fair entrances"). A hook at
  `Sprites_PopulateNextAvailableSprite` (`$C205`) rewrites
  `CurrentSprite_XPos` and `CurrentSprite_YPos` for a listed placement before
  the slot is filled; Seamless calls `fax_entrances_room_loaded` after loading
  a neighbouring room on an isolated machine, where hooks do not run, and it
  moves the listed enemies in that machine's slots. No ROM, no read overrides,
  no state of its own: the positions are the game's sprite RAM, which save
  states keep.

- **Spell cost** (`fax_spell_cost.c`, #126). The `M:` bar shows what the
  selected spell will cost, as Castlevania: Belmont's Curse's MP bar does:
  the last N pixels of the fill, N its cost (`MAGIC_COSTS`, read from the
  cart image's bank 14), are drawn a row of the palette lighter (`$19` to
  `$29`), so the plain fill before them is what a cast leaves. With less MP
  than the cost the spell will not go off, and the whole fill is drawn a row
  darker (`$09`) instead; at exactly the cost it is all lighter. No spell
  selected (`SelectedMagic` `$FF`, or anything from 5, which
  `Player_CastMagic` will not cast): the game's bar. The span is worked out
  at every present from `Player_MP`, capped to the bar's 80 pixels, and
  `SelectedMagic`, so it follows MP however it changes (a cast, MP refill,
  the Wing Boots' and the Hour Glass's drain, a shop, the Elixir's fill) and
  the spell as the menu changes it. Only the
  bar's own fill is recolored: in the span, on its fill lines 18-21, the
  pixels showing the fill's color (*MP and its bar*), so in the frame or two
  before the game redraws the bar after MP changes, nothing is drawn where
  it shows no fill. The fill's color is the status bar palette's color 2,
  through the emphasis and greyscale its lines were drawn with; one that is
  black (a fade's last stages), or would darken to black, is left alone.
  Drawn at present time, over the status bar band wherever Seamless put it
  (`fax_world_hud_x`, plus 24), after Charges draws the Elixir and before
  every other feature, only while the frame shows the status bar
  (`fax_text_hud_showing`). It shows only the selected spell's cost, never
  an item's. No hooks, no ROM, no read overrides, no save-state record,
  nothing written to the machine.

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
| `furthest` | Save files' own count | The furthest temple reached: the original's `TempleSpawnPoint`. The title's file picks show its town |
| `special_items`, `quests` | `SpecialItems`, `Quests` | Bit flags, decimal |
| `weapons`, `armor`, `shields`, `spells`, `items` | The five lists, up to their counts | Entries in decimal, space-separated |
| `selected_weapon` ... `selected_item` | `SelectedWeapon` ... `SelectedItem` | A number, or `none` |

Other fields belong to features: Guru travel's `gurus` (the visited Gurus,
town names), recharging items' `recharge.elixir` and `recharge.elixir_told`
(its `recharge.wingboots` and `recharge.hourglass`, #27's cooldowns, are
dropped at a file's next write; see its entry in *Features and what they
use*), the map's
`map.eolis`, `map.trunk`, `map.mist`, `map.towns`, `map.branch`,
`map.dartmoor`, `map.zenis` and `map.items` (see *The map*), the quest log's
`quests.given` and `quests.done` (see *Quests*), and Double gold's
`gold.king_given` (see its entry). (`imported_mantra`, on a
file imported from a mantra before mantras went from the remaster, is kept
like any field no feature knows.) Missing core fields take a new game's
values. HP, MP and the timed items are not
kept: continuing fills HP and MP and clears the timed items, as a mantra did.

**The title's menu.** The title's two choices are NEW GAME over CONTINUE, a
centred column (as in Metroid's title) in place of the original's START and
CONTINUE side by side. A hook at `StartScreen_CheckHandleInput` reads the pad
instead of the title's own input (see The title screen):

- **The two choices.** At present time, the original's START and CONTINUE
  (columns 8-23 of row 20, y 160-167) are painted black, the frame's sprites
  that cross that row are drawn again over it (`cyc_render_sprites`, so the
  arrow beside CONTINUE stays whole), and NEW GAME and CONTINUE are drawn in
  the title's own letters (tiles `$E0`-`$F9`, read from the line's pattern
  table, in the palette the attribute table gives START's column, the
  original's white), only their letters' pixels. Both are 64 pixels wide,
  left-aligned at x 96 and so centred in the native picture, lit on y
  151-157 and 165-171: 7 black rows under the logo and 7 between them, so
  they read as one menu, and 12 above the first notice (which have 9 between
  them). With no file to continue, CONTINUE is grey (palette 3's `$00`) and
  the cursor never lands on it. Select, Left, Right, Up and Down all move
  between them (with two choices each toggles), and the title opens on
  CONTINUE when a file can be continued. The hook sets `$0687` to the
  choice, clears Select and Start from `Joy1_ChangedButtonMask` and lets the
  routine run on, so it toggles nothing and draws its own arrow; a second
  hook, at its `LDA #$AD` (`$9F64`, once X and Y hold the arrow's place),
  puts the arrow beside the chosen line: X `$54` (84), 12 pixels before the
  words as the stock arrows are, and Y 117 or 131 (lit on the line's rows,
  see The title screen). The arrow is a sprite through
  `Sprite_SetAppearanceAddrFromOffset`, which switches banks, so it cannot be
  drawn from a committed call.
- **A pick.** A or Start on either opens a pick of the three files, drawn
  over the picture: a box of 10 cells on y 160-239, over the original's
  choices and the notices, with two rows a file (its name, then its gold and
  experience), a blank and what it asks. The band above it (y 144-159) is
  painted black: the picture shows the sprites of the frame before, so on a
  pick's first frame the arrow would still be beside NEW GAME. The routine
  is handled there, so no arrow is drawn. Up and Down move, A or Start take
  a file, B goes back.
- **NEW GAME's pick** ("New game in which file?") opens on the first empty
  file. An empty one starts a new game at once, through the stock Start path
  (nothing is written until the first save); a used one, damaged or newer
  ones included, asks "Erase file N? A yes B no" on the last row. A erases it
  and starts the new game in it; B goes back to the pick.
- **CONTINUE's pick** ("Continue which file?") opens on the first saved file.
  Only saved files can be chosen: the others are grey and the cursor skips
  them.

Its save-state record (version 2) holds the menu; a version 1 record, from
the file select with "Erase a file" this replaced, comes back on the two
choices.

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

Without a file in play (a save state from the original game, or one made
before a file was chosen) or if the choice cannot be supplied, the Guru skips
the mantra: Save files sends the interpreter to Guru travel's intro if it has
a Guru to offer (`fax_travel_begin`), else to the script's END (`$A5FF`).
With the remaster on nothing shows a mantra; with it off the Gurus show it as
the original does.

Its lines come after the Guru's own, in the same box, and a new title's line
scrolls: while they (and Guru travel's after them) are up, the hook at
`$F439` keeps the box's line scroll offset (see The text box).

**Saving on respawn.** The hook at `$DB20` (after `Game_SpawnInTemple` in
`Player_Spawn`) saves the file at every respawn: after a death, a Guru travel
journey and continuing. It keeps the spawn point, so quitting never loses more
than the last stretch, and only a Guru moves where the file continues.

**No mantras.** The remaster neither reads nor writes mantras: no import
(`faxanadu.srm`, `faxanadu_gurus.txt` and `faxanadu_imported.txt` are left
alone), no `--password`, no Mantra field in the launcher, and no capture of
the game's mantra. With the remaster off, the title's Continue opens the
original's mantra screen, empty, to type one in.

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
