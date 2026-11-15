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
