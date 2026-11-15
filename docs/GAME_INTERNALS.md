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
| 12 | `$AD8A-$BFFF` | 4,726 bytes | Guru travel uses `$AE00-$AE06` |
| 13 | `$B45B-$BFFF` | 2,981 bytes | Keep `$B45A` `$FF` (see Messages). Guru travel uses `$B45B-$B48A` |

**Rev A** (`Faxanadu (USA) (Rev A)`, CRC32 `E71DB268`) matches the original in
banks 5, 14 and 15, but bank 12 differs from `$9000` (the password code moved,
and data tables after it shifted). Not supported: the build rejects it.

## RAM worth knowing

| Address | Name | Notes |
|---|---|---|
| `$0019` | `Joy1_ChangedButtonMask` | Buttons newly pressed this frame. A=`$80`, B=`$40`, Select=`$20`, Start=`$10`, Up=`$08` |
| `$0024` | `Area_CurrentArea` | 4 = inside a building |
| `$0063` | `Area_CurrentScreen` | |
| `$009E`, `$00A1` | `Player_PosX_Block`, `Player_PosY` | |
| `$00DB-$00DD` | `IScriptOrCHRAddr`, `IScriptOffset` | The interpreter reads the next action at addr + offset |
| `$0100` | `CurrentROMBank` | 14 in the main loop. The stack shares this page |
| `$0213` | `MessageID` | 0 once a message has typed out; otherwise stale after a conversation |
| `$0215` | `TextBox_MessagePaused` | Nonzero while a `$FC` pause waits for A |
| `$0390`, `$0392-$0394` | `Experience`, `Gold` | Gold is 24-bit little-endian |
| `$039A` | `Player_MP` | |
| `$042D` | `Quests` | Bit flags |
| `$0431`, `$0432` | `Player_HP_U`, `Player_HP_L` | |
| `$0438` | `PlayerIsDead` | Set it to 1 and the main loop runs the death path |
| `$0439` | `TempleSpawnPoint` | The furthest temple reached, 0-7 |

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
| `$14` | | Show the mantra |
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
- Between two messages the box is wiped and refilled. A screenshot taken then
  shows half of each.
- The font is one bit a pixel in bank 13 at `TILE_GLYPHS_START` (`$8000`),
  8 bytes a character from `$20` to `$7F` (`A` is at `$8108`). The glyph data
  ends where the messages begin (`$8300`). `TextBox_WriteChar` copies a glyph
  into both bit planes of a pattern tile, so text is color 3 of the box's
  palette.
- `TextBox_Open` frames a box with background tiles 1 (top left), 2 (top and
  bottom), 3 (top right), 4 (sides), 5 (bottom left) and 6 (bottom right)
  around tile 0 (black), from the playfield's background pattern table.
  `TextBox_Closed` (`$020C`) is 0 while a box or the Select menu is open, and
  nonzero once one has closed (also before the first).
- The HUD and every box share one palette, `UI_AttributeDataIndex` (`$038D`,
  0-3), which `Screen_LoadUIPalette` sets from the screen's palette
  (`PALETTE_INDEX_TO_UI_BG_ATTRIBUTE_INDEX`, bank 11). The frame's colors
  differ from place to place for that reason.

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
  are 64, shared by every feature.
- **Hook sites** (`game.toml` `[[mod_function_hook]]`, keyed by the bytes
  there) call C before an instruction. Returning nonzero returns from the
  routine as its RTS would, from the current S. A callback can set S and the
  stack first to return somewhere else. That's how Guru travel enters the
  respawn path. When a site handles, later sites at the same address are
  skipped that time, so declare shared addresses in `mods/allocations.toml`.
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
- `--uncapped` (with `--exit-after N`) runs the window as fast as it can:
  about 290 fps, 5x real time. The cycle-accurate machine is the limit; even
  headless runs at about 370 fps.
- A save state's RAM is in its `HW  ` chunk at offset 23, 2 KiB, with no
  checksum. Edit it to set up test fixtures (`tests/travel/guru_travel.py`).

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

## Features and what they use

`mods/allocations.toml` lists each feature's ROM, hook sites and shared
addresses, and `tools/check_allocations.py` enforces it.

- **Guru travel** (`fax_travel.c`). After the mantra or a new title, the Guru
  offers to send you to any other Guru you have visited. It steers the
  conversation at `IScripts_InvokeNextAction` and supplies two script steps
  and one message slot through read overrides. The journey is the respawn
  path without its penalties, with `TempleSpawnPoint` set only during the
  committed `Game_SpawnInTemple` call. Visited Gurus persist in
  `faxanadu_gurus.txt`; a new game resets them.
- **Death keeps gold** (`fax_death.c`). Dying brings the hero back with the
  gold, experience and earned title he had. It hooks
  `Player_SetInitialExpAndGold` and returns at once when the far call's resume
  address is `Player_HandleDeath`'s, and leaves the start screen's call alone.
  It keeps no state.
- **Zone names** (`fax_banner.c`, with `fax_zone.c` and `fax_text.c`).
  Entering a zone with a different name shows it below the HUD for 2 s, in a
  text box drawn over the presented picture (after Seamless composes its
  own). No hooks and no ROM: it reads RAM in `frame_end` once the screen has
  settled. A temple whose way out is not where the hero last stood means a
  respawn or a Guru travel journey, and makes the next zone's name show. The
  banner is held while a box is open or the game is paused. Its save-state
  record keeps the banner as it was.
