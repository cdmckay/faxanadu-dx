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
| `$001A` | `InterruptCounter` | Counts every NMI, lag frames included. Read it in the same `read_ram` as other state to time things to the frame over TCP |
| `$0024` | `Area_CurrentArea` | 4 = inside a building |
| `$0063` | `Area_CurrentScreen` | |
| `$009E`, `$00A1` | `Player_PosX_Block`, `Player_PosY` | |
| `$00DB-$00DD` | `IScriptOrCHRAddr`, `IScriptOffset` | The interpreter reads the next action at addr + offset |
| `$0100` | `CurrentROMBank` | 14 in the main loop. The stack shares this page |
| `$0127` | `SoundEffect_State_Counter` | The playing sound effect's tick; the typing sound runs 1-4 |
| `$0212` | `TextBox_PlayTextSound` | The typing sound plays for a character if set |
| `$0213` | `MessageID` | 0 once a message has typed out; otherwise stale after a conversation |
| `$0214` | `Message_ProcessedLength` | Bytes of the message read, from an offset, not 0. Wraps with the address |
| `$0215` | `TextBox_MessagePaused` | Nonzero while a `$FC` pause waits for A |
| `$021D` | `TextBox_Timer` | Characters type when it is a multiple of 4. `Messages_Load` zeroes it |
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
- TCP `key` (an SDL key name) and `pad` reach the game as window events, as
  a real key or controller does, so the host extras' `event` sees them. The
  window keeps `config.ini` beside the executable and writes it at first
  start; its `[Game]` keys go to the host extras' `load_setting`, and
  `save_settings` writes them back whenever the window saves.
- `--uncapped` (with `--exit-after N`) runs the window as fast as it can:
  about 290 fps, 5x real time. The cycle-accurate machine is the limit; even
  headless runs at about 370 fps.
- A save state's RAM is in its `HW  ` chunk at offset 23, 2 KiB, with no
  checksum. Edit it to set up test fixtures (`tests/travel/guru_travel.py`).
- A site at a **branch** sees the flags the instruction before set, and can
  steer the branch by changing them (and the register they came from) with
  `cyc_mod_set_regs`, then returning 0. Forgiving jumps does this twice, so
  the game's own code does the rest.
- Each TCP command is served on a frame boundary, so a polling loop sees the
  game a few frames apart. To time something to the frame over TCP, read
  `InterruptCounter` with the state in one `read_ram`. A save state's frame,
  which a headless run from it continues, is the `uint32` at offset 28.
- TCP input lands at no particular frame. For anything that depends on the
  frame a button goes down, run the same build headless from a save state:
  `--load-state S --input FILE --frames N --frame-log LOG --mods-root DIR`.
  `--input` lines are `<frame> <BUTTONS>` (`RIGHT+A`, `-` for none), in the
  host's frame numbers, which a loaded state continues; a run with
  `--frames 0` prints the state's frame. The frame log holds every frame's
  CPU RAM (after a `CYCFRAME` and version header, per frame: 9 words, the
  palette, OAM, then RAM). `--screenshot` saves the last frame.
  `tests/movement/play.py` wraps this.
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

## Weapon damage (bank 14)

The sprite loop calls `Player_HitSpriteWithWeapon` (`$8804`) for each live
sprite every frame, except while the sprite's hit counter counts down. It
lands a hit while a swing is on animation frame 1 or 2, on a visible enemy in
reach (`Sprites_IsSpriteOutOfWeaponRange`). A hit builds its damage in
`Temp_00` (`$00`):

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

**Eolis, for tests.** A new game starts on Eolis (area 0) screen 0, outside
the gate, with the intro's trigger (`SPRITE_TRIGGER_INTRO`, `$4F`) in sprite
slot 7: walking right sets it off. The gate is a door (`EOLIS_DOOR_LOCATIONS`,
block `$9C`); Up from x=195 goes into the town, screen 1, at x=144. The
screens run 7-6-5-4-3-2-1-8 from left to right (`EOLIS_SCROLL_DATA`, bank 3;
screen 0 has no neighbours). Screens 4 and 5 each have two Zozura (`$47`,
9 HP) crawling right along the ground, and screen 8 a Monodron (`$2A`,
10 HP). Walking left from the town, the hero comes onto screen 4 at x=240.
The stock Hand Dagger passes over a Zozura; the swords hit one about 30
pixels left of the hero (his x minus its x). A Zozura that reaches a hero at
the screen's right edge knocks him onto the next screen.

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
| Dartmoor | Branch `$27`, `$A8` (region) | Ring of Dworf | Dartmoor `$00`, `$9E` | `$0C`, `$02` | `$00` `$AF` (region) |

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

**Zone respawn** (`fax_respawn.c`) uses three sites:

- `Screen_SetupNew` (`$DD13`): a load into a different zone records the way
  in as the checkpoint. A building (area 4) changes nothing: its way out
  leads back where the hero stood. A load outdoors in no zone (Eolis screen 0,
  where a new game starts) forgets it.
- `$D9A0`, `Player_HandleDeath`'s `JSR Screen_FadeToBlack` right after the
  `Player_SetInitialExpAndGold` far call: reached only by a death, after the
  Guru's words. The RTS from `MMC1_PopBank` lands there, and a hook there runs.
- `$DB1D`, `Player_Spawn`'s `JSR Game_SpawnInTemple`, with S at `$FF` on the
  death path: a death in the checkpoint's zone writes it to the `Saved*`
  variables, `Area_Region`, `Areas_DefaultMusic` and the facing, puts
  `Game_SetupExitBuilding` - 1 and `Game_MainLoop` - 1 at `$01FC-$01FF`, sets
  S to `$FB` and returns. Anything else lays out the temple, as the original
  does: a mantra (`$FC95` jumps to `Player_Spawn`), Guru travel's journey
  (its prepared stack RTSes into `$DB1D`, and a hook there runs for it too),
  and a death with no checkpoint or one for another zone (a death in a
  building, which is in no zone). Each forgets the checkpoint, so walking out
  of the temple records the next.

`Player_Spawn` has already set HP and MP to 80, cleared the Mattock bit and
the timed items, and the death path has restored the selected equipment, so
the respawn keeps all of the original's death but the place.

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
  enemies (see *Eolis, for tests*).

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
  record keeps the banner as it was.

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
  area. It counts updates at `Player_CheckHandleVertMovement`'s entry, not
  frames, and keeps a 7-byte save-state record.
- **Hold A to speed up text** (`fax_text_speed.c`). While A is held (and not
  on the frame it goes down), a hook at `TextBox_ShowNextChar_IfReady` rounds
  `TextBox_Timer` up to a multiple of 4, so a character types every frame,
  and clears `TextBox_PlayTextSound` on all but every 4th frame, so the
  typing sound keeps its rhythm. Page breaks and questions still wait for a
  new press. It writes nothing else and keeps no state. It reads only the A
  bit of the joypad masks, which the Magic button never changes (it ORs in
  Up and B, for `Player_CastMagic` alone, outside any message loop), and
  writes neither mask.
- **Zone respawn** (`fax_respawn.c`). Dying comes back at the last entrance
  into the zone (tower, castle, town or region) the hero died in, after the
  Guru's words, instead of at the furthest temple. A hook at `Screen_SetupNew`
  records the way in whenever a load enters a different zone; a hook after the
  death script marks the death; at `Player_Spawn`'s `JSR Game_SpawnInTemple`
  a death in the checkpoint's zone returns into `Game_SetupExitBuilding` with
  the entrance in the `Saved*` variables. With no checkpoint, or after a
  mantra or a Guru travel journey, the temple is laid out as before, and the
  checkpoint is forgotten until the hero walks out. Mantras are unchanged. Its
  save-state record holds the checkpoint and whether a death is on its way to
  `Player_Spawn` (see "Entrances and zone respawn").
