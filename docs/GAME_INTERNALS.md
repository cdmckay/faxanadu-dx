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
  Only a pause sets `TextBox_MessagePaused` ($FF), and only continuing past
  one (or the mantra box) clears it, so it can hold a stale nonzero value: $9F
  all through a new game's intro. For a line with no pause, wait on
  `MessageID` alone.
- Between two messages the box is wiped and refilled. A screenshot taken then
  shows half of each.

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
- The mantra stores each list entry in 4 bits and the selections in 5, so
  only the IDs below `$10` survive it in the item list.
- Using an item (Down-B, `GameLoop_CheckUseCurrentItem` `$C478`) jumps on
  `SelectedItem`. The consumables end with `JSR Player_ClearSelectedItem`
  (`$C4BF`: `SelectedItem = $FF`, then `UI_ClearSelectedItemPic`): the Red
  Potion at `$C53B`, the Wing Boots at `$C581`, the Hour Glass at `$C5D0`,
  the Mattock at `$C64C`.

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
  enemies.

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
