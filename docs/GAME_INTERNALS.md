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
- `--uncapped` (with `--exit-after N`) runs the window as fast as it can:
  about 290 fps, 5x real time. The cycle-accurate machine is the limit; even
  headless runs at about 370 fps.
- A save state's RAM is in its `HW  ` chunk at offset 23, 2 KiB, with no
  checksum. Edit it to set up test fixtures (`tests/travel/guru_travel.py`).
- A hook site can be any instruction, a branch included. At a branch the
  callback sees the flags the instruction before set, and steers the branch by
  changing them (and the register they came from) with `cyc_mod_set_regs`.
  Forgiving jumps does this twice, so the game's own code does the rest.
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
