# Seamless's seams: the plan for one big world

Issue #166. A plan, built a slice at a time: slices 0 and 1 are in the game.
Where building slice 1 found the plan wrong, the plan below says what was
built, and "Slice 1, as built" lists the changes.

**The goal**, as Cameron put it: "it should just feel like 1 big world." A
player should never be able to tell where one room ends and the next begins.
The only edges left are the original game's own: doors, region changes, and a
boss arena while its boss lives.

This doc starts with what the game does at a seam today, checked in the game,
then lists everything else that gives a seam away, recommends a design,
answers the issue's questions, and cuts the work into slices that each run
end to end. [SEAMLESS-WORLD.md](SEAMLESS-WORLD.md) describes Seamless
itself; the addresses are chipx86's labels (`tools/disassembly.sh`), and
what was learned about the game is also in
[GAME_INTERNALS.md](GAME_INTERNALS.md).

**In short.** Today a seam is an invisible wall. Monsters and walking people
wait at it, shots vanish at it, a spell ends at it, the sword never reaches
across it, and a sprite drawn past it is cut off. Where no room joins, the
view shows a black block with straight edges. The plan keeps the rule
Seamless already has, that the live game owns the hero, and makes a seam
open ground:

1. Monsters, their shots and the people who walk go through a seam whichever
   way they walk, between any two rooms, and are drawn whole while they
   straddle it. Each is dormant until he first sees it: until then it
   stands still, so nothing comes at him from a room he hasn't looked into.
   Once seen, it stays awake.
2. Monsters near a seam *visit* the live game's sprite loop, so the sword,
   touches, shots and drops work across it, and a neighbouring room's sounds
   play.
3. The spell stays the live game's and flies on into the next room.
4. The camera never shows black where no room is.

Then come seams one above the other, what a neighbouring room sees of the
hero, and three loose ends: seven slices after slice 0, about 2,100 lines of
C as estimated, and likely twice that, as slice 0 took. Slice 0 (nothing
lost at a seam, no ghost hit) is done: #177. Slice 1 (#178, seams are open
ground, mechanism 1) is done too.

## How Seamless runs a neighbouring room today

`src/fax_world.c` keeps a `Room` record for each room of the area on the map:
its 2 KB of RAM, its CHR, its palette and its place. The live game runs only
the hero's room. Each frame, after the live frame (`fax_world_step`), every
other room within the view's halo runs one tick on an isolated copy of the
machine (`tick_room`):

1. The copy starts as the live machine. `load()` writes the room's own RAM
   over it: the bytes in `make_mask()`'s list, which are the eight sprite
   slots (`CurrentSprites_XPos_Full` and `_YPos`, `$BA-$C9`, and every
   `CurrentSprites_*` array from `$02BC` to `$0373`), the screen
   (`ScreenBuffer`, `$0600-$06FF`), its neighbours' edge blocks
   (`LastColumnLeftScreen` and the rest, `$03E6-$0425`), `BlockProperties`,
   the neighbour links (`$66-$69`), `Screen_ExtraInfoAddr`, the multi-part
   actors' handlers (`Sprite12BodyPartHandler`, `$0379-$037E`),
   `SpriteUpdateCounter`, `Random_Offset` and
   `CurrentScreen_SpecialEventID`.
2. `shadow()` sets up the hero the room's actors see: his position in the
   room's coordinates, **clamped to x 0-240 and y 0-176**, so a room to his
   left always sees him at its x 240, however far into his own room he is;
   `Player_InvincibilityPhase` 200; `Player_AttackPhaseAnimFrame` 0;
   `CastMagic_Type` `$FF` (no spell); no buttons.
3. It runs `Screen_ResetForGamePlay` (`$CB17`) and `Sprites_UpdateAll`
   (`$8000`), then `capture()` keeps the RAM. Only the room's own bytes
   matter: next tick they are written over a fresh copy of the live machine,
   so anything the tick did to the hero, his experience, gold, HP, sounds or
   inventory is thrown away. Since slice 0, the copy's sprite loop never
   touches him (`fax_seams.c`, at `CurrentSprite_CheckHitPlayer`).

The halo is the view's half-width plus a room sideways (468 pixels) and half
its height plus a room up and down (312), from his room's origin. So only the
rooms next to his tick, diagonals included; rooms two away stand still.

When the hero crosses, `seam_hook` at `Area_ScrollTo` (`$E8E9`) swallows the
original's scroll, and `draw_hook` at `Player_DrawShield` (`$B982`) adopts the
next room once his origin wraps: `adopt()` keeps the old room's RAM, loads the
new room's own bytes into the live machine, copies its sprite tiles
(`$0900-$0FFF`) in, and clears the spell (`CastMagic_Type` `$FF`).

So **no actor ever moves between machines**: each room keeps its own eight
slots. The spell (`CastMagic_*`, `$02B3-$02BB`) is not room-local: only the
live game has one. Seamless draws every actor itself, each room's from that
room's OAM and CHR, so outside a death the NES's own sprite picture is not
what you see.

## What happens at a seam today

Checked at Eolis `$04` and `$05`, the first fight: room 5 is left of room 4,
and each has two Zozuras (`$47`, 9 HP). Throwaway scripts on `fax_e2e`
started from the `eolis-zozuras` fixture, took room 4's Zozuras out, equipped
a Long Sword through the Select menu, gave Deluge and 80 MP, and walked the
hero left on room 4. Headless runs logged the live RAM every frame;
neighbouring rooms were read from Seamless's save-state record (each
`Room`'s RAM) and its TCP commands. The PR has the screenshots.

### Spells stop at the seam

Deluge cast left from x 43 starts at x 40 and moves 3 pixels a frame. Its
last frame is at x 1; on the next, `CastMagic_Type` is `$FF`. Room 5's
Zozuras, 14 to 30 pixels past the seam, are untouched.

`CastMagic_UpdateXPosition` (`$BCAF`) moves the spell and asks
`CastMagic_CheckDirection` (`$BD8F`): moving left, `CastMagic_CheckDirection_Left`
(`$BD3C`) disperses it once its X wraps past 0 (`$F0` and up); moving right,
`CastMagic_CheckDirection_Right` (`$BD4A`) once X plus its width
(`CAST_MAGIC_WIDTHS`, `$BD9D`) carries past 255. Its wall check
(`CastMagic_CheckDirection_CheckImpassable`, `$BD58`) reads only the room's
own `ScreenBuffer`. It hits only the live room's slots
(`Sprite_CheckHitByCastMagic`, `$8ADC`, from the sprite loop), and copies
have no spell at all. So a spell ends at the edge of the hero's 256-pixel
room, in mid-air in Seamless's view.

**A spell also vanishes when the hero crosses a seam.** Deluge cast right
from x 46, then the hero walked left into room 5: on the frame he crossed,
`CastMagic_Type` went to `$FF` with the spell at room 4's x 241. `adopt()`
clears it.

### Monsters pile up at the seam

With the hero standing at room 4's x 43, room 5's two Zozuras spent 800
frames between their room's x 222 and 240, on top of each other (231 and 231,
240 and 240), pressed against the seam.

They chase the projected hero, held at room 5's x 240 wherever he stands, and
the seam is a wall to them. A sprite's sideways move
(`MoveSpriteHorizIfPossible`, `$84C4`) asks
`CurrentSprite_CanMoveInDirection` (`$8710`), which calls a move off the
screen blocked: left once X is `$F1` or more (`FUN_PRG14__86bd`, `$86BD`),
right once X plus the hitbox width carries (`Sprites_MoveRight`, `$86A7`).
The sprite is snapped back to the block edge, and
`Sprite_MoveHorizAndTurnAroundIfNeeded` (`$8419`) turns it round. A Zozura
turns, re-aims at the hero and comes back, so the two oscillate at the seam.

### The sword never hits across the seam

Six Long Sword swings from room 4's x 4, facing left, with room 5's Zozuras at
its x 228-237: the weapon's box (`PlayerPosXPlus10`, `$CE`) was at 244, then
236, which is room 5's x 244 and 236, right over them. Both kept 9 HP and a
hit counter of 0, and experience stayed 0. **The hit never lands in the
copy.** `Player_HitSpriteWithWeapon` (`$8804`) hits only in attack phases 1
and 2, and `shadow()` sets the copy's `Player_AttackPhaseAnimFrame` to 0.

Had it landed in the copy, the hit, the knockback and the death would have
stayed (they are room-local), and the experience and the sound would have
been thrown away.

**The live game's own check hit a monster at the far side of his own room**,
the original's ghost hit. `Player_DrawWeapon` (`$B7D6`) keeps the weapon's X
as 16 bits (low byte `PlayerPosXPlus10`, `$CE`, high byte
`Maybe_Something_WeaponPosX`, `$CF`), but `Sprites_IsSpriteOutOfWeaponRange`
(`$88CB`) compares the low byte only. It also takes the weapon's last Y on
the screen: while the weapon is off it, `Player_DrawWeapon` returns before
the Y and the reach, so `Maybe_Something_WeaponPosY` (`$D0`) keeps the last
on-screen frame's value. A Zozura put at room 4's x 236, 232 pixels behind
the hero, died to one swing, for 30 experience; the original does the same.
In Seamless he also stands at x 241-255 of a room before he crosses, where a
swing to the right wraps the same way, and the monster he kills is off the
view. **Slice 0 fixed it in the remaster** (#177: the X test in 16 bits, in
`fax_seams.c`); the original keeps it. GAME_INTERNALS.md, *Weapon reach*, has
the details.

### Also found, now fixed

- **Monsters across a seam can't hurt him.** In the copy he is invincible
  (200), so `Player_HandleTouchEnemy` (`$89AE`) returns at once, and the
  copy's damage would be thrown away anyway. They hurt him only once he
  crosses into them (#130). Still true: slice 2 is about it.
- **The copy took drops at its edge, and the gold was lost.**
  `CurrentSprite_CheckHitPlayer` (`$890A`) sends every touch to its
  category's handler with no invincibility check, and
  `Player_HandleTouchBreadOrCoin` (`$8968`) has none either. A coin put at
  room 5's x 236 was gone within two frames, and the live game's gold never
  moved. **Fixed by slice 0**: the copy's sprite loop skips the routine, and
  the coin waits until he walks into its room.
- **An item or a person at a neighbour's edge froze that room.** The Dragon
  Slayer (`$5A`) put at room 5's x 240: every tick of room 5 failed from then
  on (Seamless's `failures` count rose by one a frame), so its Zozuras froze.
  The pickup (`Player_HandleTouchItem`, `$88FB`) runs an interaction script,
  which waits for frames an isolated call never has. A person who talks when
  touched (`Player_HandleTouchNPC`) did the same. **Fixed by slice 0**, the
  same hook. Its test checks the item; no test checks the person yet (slice
  1's does).

## Everything else that gives a seam away

Beyond combat, from the "Still open" list of SEAMLESS-WORLD.md and the code.
Checked in hidden games where marked; the scripts were throwaway, on
`fax_e2e`, from its fixtures.

**Tells, and where they go:**

- **Walking people wait at the seam.** In the `eolis-town` fixture (the hero
  on room 1), room 2's walking woman (`$45`) walked from x 85 to x 240, the
  seam with his room, pressed against it, and turned back, about 160 pixels
  from him (checked). Four of the five people who walk use
  `SPRITE_BEHAVIOR_MOVE_TOWARD_PLAYER` (`BSCRIPTS_NPC_WALKING_WOMAN_01` and
  `_02`, `$44` and `$45`; the King's guard, `$42`; the lady with the cup,
  `$41`), which despite its name walks the way the person faces: `_02`
  walks 30 pixels, turns, walks 40, turns, drifting 10 a cycle until a wall
  turns her (found building slice 1); the walking man (`$34`) walks 45
  pixels, stands, and faces the hero, so he keeps coming back past him.
  Eolis has four of them and the other towns nine, all in joined rooms.
  **Slice 1 (done).**
- **Shots vanish at the seam.** `SpriteBehavior_EnemyMagic` (`$A0CB`) moves a
  shot 2 pixels a frame and removes it (entity `$FF`) when
  `Sprite_Maybe_TurnAroundIfAtScreenEdgeHoriz` (`$8427`) finds it at the
  screen's edge. So a fireball that misses him vanishes at the seam behind
  him, at most 212 pixels away, and a neighbour's shot at him vanishes at the
  seam in front. **Slice 1.**
- **A sprite drawn past its room's edge is cut at the seam.**
  `Sprite_Draw_Standard` (`$F0EC`) and `Sprite_Draw_FlippedHoriz` (`$F181`)
  skip a cell that does not fit on the 256-pixel screen. The Maskman, Sir
  Gawaine and the Wolfman draw up to 16 pixels left of their origin, and King
  Grieve 8: by a room's left edge, that part is missing. Every actor that
  crosses a seam would straddle it for a few frames, cut the same way.
  **Slice 1.**
- **The projected hero is clamped to the room's edge.** Four effects:
  neighbours' monsters pile at the seam (above; **slice 1**); behaviours that
  measure his distance act as if he stood at the seam (**slice 6**); Nash and
  Magman, who place themselves beside him, appear by the seam, or for
  Magman, whose X wraps, at the far side of the room (**slice 6**); rooms
  above and below see him on their bottom or top row (**slice 5**).
- **A neighbouring room's sounds are lost** with the copy's RAM: the coin's
  sound (`$10`, `Sprites_ReplaceWithCoinDrop`, `$AC75`) when a monster killed
  across a seam drops one, Pakukame's (`$12`) as it spawns (Dartmoor `$13`,
  `$14` and `$1F`, Zenis `$07` and `$0B`). **Slice 2.**
- **Sugata's flash never reaches across a seam.** In a copy, `fax_sugata.c`
  skips its warning and its range, and the copy's greyscale (`PPU_Mask`,
  `$0B`) and damage are thrown away. It lives on Mist `$1E`, `$1F`, `$4E` and
  `$4F`, each joined sideways. **Slice 2.**
- **The view shows black where no room is.** The camera clamps to the box
  around the rooms joined to his, and wherever that box holds no room the
  view is black, a block with straight edges at a room's side or corner.
  There are 142 such empty places, each beside a room's side or corner:
  Trunk 31, Mist 64, Branch 21, Dartmoor 20, Zenis 6, and none in Eolis or
  the towns (from the ROM's scroll data, laid out as `topology()` does).
  Out of the Tower of Trunk's door onto `$10`, the view's top left was black
  (checked). **Slice 4.**
- **A load leaves the rooms beside it as they were.** A door within the area,
  or a zone respawn after a death (`fax_respawn.c`, in the same area), loads
  its room fresh from its list, but Seamless keeps every other room's record:
  the room he arrives in has its monsters back, the next one has whatever he
  left (kills, a pile at the seam, a drop). **Slice 1.**
- **One-in-four items in a room first met as a neighbour hardly ever show.**
  The Mattock (`$50`), Red Potion (`$5D`), Poison (`$5E`), Glove (`$5F`) and
  Ointment (`$60`) decide on their first frame, with `Screen_Is3Of4Visits`
  (`$A529`) and `Screen_TrackedVisitCount` (`$043A`). In a copy that frame
  reads the live count, and its change is thrown away; and the live game
  never runs it for a room it adopts. So no seam ever moves the count, and
  such an item shows only when the count happens to be 3. Out of Eolis's
  meat shop onto Trunk `$0B`, its door edited: with the count at 0, `$0C`'s
  Red Potion was gone from its slot at once; at 3 it stayed; the count was
  unchanged both times (checked). Not a seam's look, but a seam's doing.
  **Slice 7.**
- **The fountain's ladder from afar.** After an area load with the path to
  Mascon open (`Quests` `$20`), Trunk `$28` is decoded from ROM: seen from
  `$27`, its fountain has the stone and no ladder. Only when he crosses does
  `ScreenEvents_HandlePathToMasconEvent` (`$EF69`) clear the stone and drop
  the ladder, a block every 4 frames, the game standing still. **Slice 7.**
- **A death at a seam.** `Player_HandleDeath` (`$D8EC`) clears every sprite
  (`Screen_ClearSprites`) and draws only the hero dissolving, and Seamless
  then shows the game's own picture over his room, without `player_edge()`.
  A hero who dies at x 241-255 is cut at the seam for the dissolve's 256
  frames. Everyone else vanishing is the original's, in every room alike.
  **Slice 7.**

**Checked, and not tells:**

- **Breakable walls.** None is at a seam: both Mattock blocks (`$63`) are in
  column 3 (Trunk `$0C` and `$2D`; GAME_INTERNALS.md, *The Mattock and
  breakable walls*). A broken wall stays broken while the hero stays in the
  area, since the room's record keeps its `ScreenBuffer`; the original
  brings it back at every load, and after slice 1 a load into its rooms does
  too.
- **Palettes.** Joined rooms share their palettes in the original too: a
  palette changes only at a door. Seamless paints every room with the live
  palettes, fades included.
- **Sprite flicker and limits.** Every room's sprites are drawn, with no
  limit to a line, in the order the game drew them (#161), and a copy's tick
  never lags. The one limit is a room's 62 OAM cells a frame, which only a
  crowded room could reach; slice 1's cap keeps a room within what the
  original's busiest screens hold.
- **The Hour Glass.** A copy starts as the live machine, with its
  `DurationHourGlass`, and `Sprites_UpdateAll` reads it: neighbours freeze
  with his room.
- **Items by a seam.** None lies within 16 pixels of a joined seam, so none
  is touched late.
- **Big enemies stay home** (category 7, in no move). Most are listed well
  inside their room, and Rokusutahn, the one that walks most, paces a block
  toward him and back. Four are listed within 32 pixels of an edge (King
  Grieve on Dartmoor `$16`, a Rokusutahn on Dartmoor `$1E`, Pakukame on
  Zenis `$0B`, Zorugeriru on Zenis `$05`); one that paces there can turn at
  the seam as at a screen's edge. Rare, and moving the most tile-hungry
  actors (32 pixels square, King Grieve up to 96 wide) buys little.

## What any design has to live with

- **A room is 256 by 208 in 8-bit coordinates.** Side by side, two rooms
  share the low byte: room 5's x 230 is room 4's x -26, the same byte. One
  above the other, they don't (208 is not 256).
- **The actors' behaviours compare their position with the hero's in 8
  bits**, so a room's actors need him inside their range, and that is why
  `shadow()` clamps him.
- **The interaction checks are 8-bit subtractions** (weapon reach, a spell's
  hit, the shield, a touch). They work across a seam, the low bytes being
  the same, but can't tell a monster just across it from one at the far side
  of the room: that is the ghost hit.
- **Everything that touches the hero has to happen in the live game.** Copies
  throw his state away, isolated calls can't run what waits for a frame (the
  Dragon Slayer froze a room), and many features skip isolated calls on
  purpose: MP from kills hooks `Player_AddExperienceFromSprite` (`$8B87`),
  the Pendant fix `@_checkGlove` in `Player_HitSpriteWithWeapon` (`$8884`),
  the damage numbers the HP subtractions (`$8891`, `$81FD`, `$C091`), and
  Slain by `Player_ReduceHP` (`$C08E`). A committed call
  (`cyc_mod_call_commit`) runs no hooks, so it can't stand in for the game's
  own path either.
- **Each room's enemy tiles are its own.** `GameLoop_LoadSpriteImages`
  (`$C28D`) loads each slot's image in slot order and keeps where it went in
  `CurrentSprites_PPUOffsets` (`$032C`), so the same entity sits at
  different tiles in different rooms. Seamless draws each room's actors from
  that room's CHR.
- **The NES draws no sprite past its screen's edge** (`Sprite_Draw` skips
  the cell), so a room's OAM never holds what lies past its edge.
- **Only the rooms next to his tick.** And a seam between two copies is in
  view only where it lines up with one of his room's seams, near a corner of
  four rooms (44 corners have all four joined): the view, 424 wide and a
  room tall, never reaches a seam two rooms away on his row.
- **A room's list holds at most 5 sprites**, and from 5 actors the live game
  starts to lag now and then (Eolis's town, #161).

**From the ROM** (bank 3's scroll links that go both ways, boss arenas
aside, which is about what Seamless joins; bank 11's lists): 171 seams side
by side (7 of them in the towns) and 102 one above the other. 132 of the 171
have monsters on both sides, and in 80 of those both sides share a kind; 79
of the 102 have monsters on both sides, 31 sharing a kind. No room's list has
more than 5 sprites, so each room starts with at least 3 of its 8 slots
free. Category 7 (`SPRITE_CATEGORIES_BY_ENTITY`,
`$B544`) is the big enemies: Rokusutahn (`$11`, in 19 lists), Ripasheiku,
Zoradohna, Borabohra, Pakukame, Zorugeriru, King Grieve and Shadow Eura, 48
list entries in all; 46 side-by-side seams and 32 others have one beside
them.

## The recommendation

One rule from Seamless's own design carries over: the live game owns the
hero, so **everything that touches him happens in the live game, through the
game's own code**. The mechanisms follow from it and from the goal. Seams
one above the other (slice 5) are mechanisms 1 to 3 again, with Y
converted.

### 1. Seams are open ground: actors walk through, both ways (slice 1)

**Movers** are the actors that cross: monsters (category 0), their shots (6),
and the people who walk (category 2: `$34`, `$41`, `$42`, `$44`, `$45`). A
mover whose move would carry it off its room's edge into a joined room goes
through, whichever way it walks. Once its origin is across, it moves to that
room's machine.

- **Dormant until seen.** An actor is dormant until the first frame any
  part of it is in the view, the 424x240 picture he sees (Seamless's
  camera): as built, the first frame its hitbox with 16 pixels round it
  meets the view (no actor draws more than 16 pixels past its box but the
  big enemies, who stay home), so it wakes up to 16 pixels early and is
  never seen standing; with F4's map up it is still the 424x240 view. While
  dormant, its behaviour doesn't run: hooks at
  `Sprites_UpdateBehavior` (`$A66B`) and `CurrentSprite_UpdateState`
  (`$8BD2`) return at once for it, so it neither moves nor seeks him, and
  isn't drawn, being out of sight. That holds in the copies and in the live
  game alike: the view is centred on him, so at a seam up to 50 pixels of
  his own room's far side are out of it. **Once seen, it stays awake**, out
  of the view too, until it dies or a load or an area change refreshes its
  room: a monster he outruns keeps coming. Its wake is a bit per slot in
  Seamless's record (version 5), and goes with the slot through a seam.
  This covers monsters, big ones included, their shots and the people who
  walk; standing people, items, bread and coins run as now.
- **A shot that leaves the view is removed** (entity `$FF`) once it is
  wholly outside it by 16 pixels, as the original removes one at its
  screen's edge, so none hits him that he couldn't see coming.
- **The edge.** Hooks at the edge checks, `FUN_PRG14__86bd` (left),
  `Sprites_MoveRight` (right) and `Sprite_Maybe_TurnAroundIfAtScreenEdgeHoriz`
  (after its move, `$842A`, the fliers' and the shots'), make a seam
  passable for an awake mover when a joined room lies that way whose actors
  run, the blocks across are passable, and the room across holds fewer than
  5 actors, counting the movers on their way in. The blocks across are
  tested in that room's own `ScreenBuffer` and `BlockProperties`, from its
  record: the neighbours' edge columns each room holds are one column for
  the hero's tests, and a straddling sprite covers more. Two floor tests
  read past a side for a sprite that straddles one, and are hooked the same
  way (found building slice 1): `CurrentSprite_CanMoveInDirection`'s up and
  down test (`$873A`, across the hitbox's columns, which would wrap into the
  next row) and `Sprites_CheckPlacementInvalidAtY`'s block ahead (`$855E`,
  X + 15 facing right, which wraps to the room's left column: a Zozura that
  found no floor there turned back at the seam). Otherwise the seam stays a
  wall, as now. In copies the projected hero is clamped to x 255 instead of
  240, so a monster coming for him keeps coming until it crosses.
- **Any room to any room.** The live game sends as well as receives: a
  monster walking away from him, one knocked back over a seam, and a shot
  that missed leave his room for the next. Copies trade with each other the
  same way.
- **The move.** Between frames, in `fax_world_step`: every per-slot byte
  (`$BA`/`$C2`, and every array from `$02BC` to `$0373`: the fractions,
  entity, behaviours, flags, phases, behaviour data and states, hitbox, the
  behaviour's target position, tiles, magic-hit state, HP, hit counter,
  behaviour script and value) goes to a free slot of the room it entered,
  and its old slot is emptied. Side by side the bytes are kept as they are;
  one above the other (slice 5), its Y and its target's Y move by 208. In
  and out of the live game through `cyc_mod_poke`, as `load()` writes it. A
  person's conversation goes with the slot (`CurrentSprites_Values`), and
  the hero talks to them as at home: Up while touching them
  (`Player_CheckHandlePressUpOnNPC`).
- **Drawn whole.** A hook at the entry of each of `Sprite_Draw_Standard`
  and `Sprite_Draw_FlippedHoriz` replays the routine's tile loop in C from
  the state `Sprite_Draw` left, and keeps each cell it skips off the screen
  (its X and Y past the edge, tile and attributes) in a list beside the
  room's OAM, in copies and in the live game; a hook where the loop skips a
  cell would fire for every cell drawn. Seamless paints those cells in the
  next room's space, and tests the behind-the-background bit against the
  blocks of the room each pixel falls in. A mover straddling a seam is then
  drawn whole, and so are the Maskman, Sir Gawaine, the Wolfman and King
  Grieve by a room's left edge. A slot whose origin crossed in this frame,
  before it moves, is drawn a screen over (`Sprite_TileInfo_OffsetX` moved
  by one), so it isn't drawn at the far side of the room it is leaving.
- **Its tiles.** If the room it enters has its entity, it takes that slot's
  tile offset, and that slot's tiles' room. Otherwise Seamless draws the
  slot from the CHR of the room its tiles are in, and keeps that room for
  the slot in its save-state record (version 5). The same replay notes
  which OAM entries each slot wrote. (A first build took the offset alone,
  and a second Zozura into Eolis `$03` was drawn from `$03`'s CHR at `$05`'s
  offset: a jumble.)
- **A load refreshes the rooms joined to it.** When the original loads a
  screen of the same area (`Screen_LoadSpriteInfo` in play: a door, a zone
  respawn, the original's scroll into an arena), Seamless forgets every
  other room of the area, so each is decoded and spawned fresh from its
  list as it comes into the halo, as the original's screens are at every
  load. So nothing meets itself (a monster that wandered out, and its room's
  fresh one), no homes need keeping, and a retry after a death finds the
  rooms around it as fresh as the one it starts in. An area change (a
  building, a region's door, a temple) already empties every room. Bosses
  come back as they do in the original: at a load, not at a seam.
- **Towns stay safe** (Cameron's call on #166). A seam between a town
  room and one that isn't is a wall for every mover, as a full room's is: a
  monster or its shot turns back at a town's edge, and the people who walk
  stay in. A town room is a screen of a town's zone (`fax_zone.c`) whose
  list holds no monster: Eolis `$01-$03` and `$06-$07` and every screen of
  the towns past it, so only Eolis's three edges are seams
  (GAME_INTERNALS.md, *Eolis's screens, and town rooms*). Without it,
  Eolis's Zozuras, once seen, followed him into the town.
- **Never moved:** standing people, items, the intro's trigger (category
  4), big enemies (7) and multi-part actors (`Sprite12BodyPartHandler`,
  `$0379-$037E`). Arenas join nothing while their boss lives.

#### Both ways, and dormant until seen

The first version of this plan moved a monster only toward the hero, at most
one room from home, and the live game only ever received. Against "one big
world" that leaves two tells:

- **Anything walking away from him turns at an invisible line.** In his room
  that is a Zozura walking past him, a monster knocked back, and a fireball
  that missed; in every town, the walking people, as Eolis's woman does
  today. One-way moves keep all of those.
- **A follower stops dead one room from home,** at the line he has just
  crossed, in plain view.

Any leash enforced at a seam is a wall a player can see. The second version
dropped the leash altogether, and that has a tell of its own: every room
next to his runs its monsters against him, seen or not, so they come at him
from rooms he can't see and pile up on him. Cameron's call, on #173: "they
should only start going towards him once he's in frame ... if it's just
wandering around that's fine but they should be kind of dormant until the
hero sees them." So:

- **Every joined seam is open to movers, both ways:** the live game to a
  copy, a copy to the live game, and a copy to a copy. A seam between two
  copies is in view only near a corner of four rooms; elsewhere a mover
  going through it is out of sight, so it matters there and for movers
  following him.
- **Dormant until seen** (mechanism 1, above). An actor he has never seen
  stands still, in his room or a neighbour's. Standing still rather than
  wandering: out of the view nobody can tell the difference, and a copy's
  monsters steer by the hero, so wandering blind would need a made-up hero
  that the game's behaviours would chase. Only an awake actor goes through a
  seam.
- **Once seen, it stays awake** (Cameron: "once they see you they should
  stay alive"). A monster he has seen keeps coming after he outruns it, and
  follows him through the seams. What bounds it is what bounds everything
  in the original: it dies, a load refreshes the rooms joined to the one
  loaded, or an area change empties them all. And only the rooms next to his
  tick, so an awake monster that falls two rooms behind him stands still
  until he comes back.
- **Two limits are left: a full room, and a town's edge.** A room takes no
  mover while it holds 5 actors, the most any list holds, dormant ones
  included. That is also where the live game starts to lag, as Eolis's town
  does. The seam is a wall for that mover then: a tell, and rare, since it
  needs two busy rooms to meet in view. And a town's edge is a wall for
  every mover (towns stay safe, above): Eolis's monsters pace there while
  he is in the town, as the original's did at any screen's side. That one
  is a choice: the town is a refuge.

**Its honest cost:**

- **Encounters change the most.** A monster he has seen follows him room
  after room until it dies, a load comes, or he outruns it by two rooms. A
  walk through a busy stretch can gather a train of what he has seen, up to
  5 in his room. The original let him escape everything by walking off a
  screen; here only a door does, or speed. What he hasn't seen never comes
  for him, so nothing piles on him from rooms he hasn't looked into. This
  needs a playtest.
- **The view is a new edge.** The original ran everything on his screen;
  here an actor up to 50 pixels inside his own room's far side, never yet in
  the view, stands still until he turns toward it. Nobody sees it stand, but
  a monster there waits instead of coming.
- **Towns get busier.** The people who walk, once seen, come toward him from
  the town's rooms beside his and gather round him for a while, then wander
  off, never out of the town. Any shop puts them home (an area change).
- **More moves.** The live game gives actors away as well as takes them, and
  people wander into rooms whose CHR has no tiles for them, so tiles from
  home are drawn more often.
- **Size.** About 100 lines over the one-way moves (sending from the live
  game, people, the cap, the refresh), less the leash and the homes it
  needed, and about 80 more for dormancy: two hooks, the view test, the
  wake bits, and the shots' removal.

It goes in **slice 1**, right after slice 0. It removes the tell a player
sees most, things waiting at invisible lines in every town and most fights,
and it shrinks the fighting across a seam, which the visitors handle, to the
moments a monster straddles it.

### 2. Visitors: monsters across a seam take part in the live game's sprite loop (slice 2)

Every frame, once the live `Sprites_UpdateAll` returns (`$DB68`, the `JSR
GameLoop_CountdownItems` after it), Seamless runs a second pass of the
sprite loop for **visitors**: the neighbouring rooms' actors within reach of
the hero or his spell.

- **How.** A hook at `$DB68` hides the room's own slots, puts the visitors in
  slots, and returns into the loop at `@_spritesLoop` (`$800F`, past `INC
  SpriteUpdateCounter`) with a prepared stack, as Guru travel enters its
  respawn path (`main_loop_hook` in `fax_travel.c`). The loop's `RTS` lands
  on `$DB68` again; the hook then writes each visitor back to its room,
  restores the room's own slots and lets `GameLoop_CountdownItems` run. Hooks
  at `Sprites_UpdateBehavior` (`$A66B`) and `CurrentSprite_UpdateState`
  (`$8BD2`) return at once for a visitor: it moves and is drawn in its own
  room. So the pass runs only the loop's interactions,
  `Player_HitSpriteWithWeapon`, `Sprite_CheckHitByCastMagic`,
  `Sprite_GetBounds`, `Player_CheckShieldHitByMagic` and
  `CurrentSprite_CheckHitPlayer`, for real, with every feature's hooks.
- **Who.** Enemies (category 0), bread and coins (1), shots (6) and big
  enemies (7), not people (2), the intro's trigger (4) or items (5): an
  item's pickup runs an interaction script with its line in a text box, so
  it is taken in its own room, which he is in by then. Only actors the loop
  would let interact: hit counter 0 and no magic-hit behaviour
  (`Sprites_UpdateAll` skips the interactions for the others too).
- **Where.** At its position in the live room's coordinates: the same X byte
  side by side; Y plus or minus 208 one above the other. Seamless notes which
  side each visitor came from.
- **Told apart.** Each interaction check compares true positions, in 16
  bits: the weapon's X (`$CE`/`$CF`), the spell's with its side (below), the
  hero's, and a visitor's with its side. Slice 0's hook at
  `Sprites_IsSpriteOutOfWeaponRange` (`$88CB`) learns the visitor's side;
  hooks at the overlap tests of `Sprite_CheckHitByCastMagic` (`$8ADC`),
  `Player_CheckShieldHitByMagic` (`$877C`) and `CurrentSprite_CheckHitPlayer`
  (`$890A`) refuse a match the 8-bit test found only by wrapping round the
  room.
- **Back home.** Every byte of the visitor's slot goes back to its room but
  its position: HP, the hit counter, the magic-hit behaviour and its
  invincibility, its facing, and a death (the entity turned into the death
  effect) or a removal (a shot on the shield, a coin taken). The room's next
  tick does the knockback, the death's animation and the drop, as it would
  for a hit in the hero's own room.
- **Slots.** Visitors take the slots the room's own actors leave free during
  the pass, nearest first. One that finds none waits a frame.
- **A neighbouring room's sounds.** A hook at `Sound_PlayEffect` (`$D0E4`)
  in a copy's tick keeps each effect and the slot that played it. After the
  tick, Seamless plays it in the live game through the game's own
  `Sound_PlayEffect` (memory only, a committed call) when that slot is in
  the view, so a coin dropped across a seam rings.
- **Sugata's flash.** `fax_sugata.c` asks Seamless for a Sugata's room and
  place, and handles a copy's flash at `$AB44` as one in his room: the
  warning blink, the band where Sugata stands, the 10 HP in reach (40
  pixels) of his true place, the Magic Shield's reflection.
- **Features that place something by a slot** (the damage numbers, which draw
  at the hit's room and slot) ask Seamless which room the slot's actor is in.

### 3. The spell flies on, still the live game's (slice 3)

The spell stays in the live game: `CastMagic_*` is not room-local, and the
live game must decide what it hits. Seamless keeps its side, the room it is
in relative to the hero's.

- At `CastMagic_CheckDirection_Left` and `_Right`, where its X wraps, and at
  `CastMagic_UpdateXPosition`'s off-screen test after them, a joined room
  that way changes its side instead of ending it.
- While it is abroad, its wall checks read that room's `ScreenBuffer`
  (through the RAM read hook `collision_read` already uses for the hero), and
  that room's actors near it are visitors, so `Sprite_CheckHitByCastMagic`
  meets them with its own hit handler and damage
  (`Player_HitEnemyWithMagic`, `$81A7`).
- It ends where it would in one big room: at a wall, on a monster, or at the
  edge of the view (the camera), whichever comes first. Each spell keeps its
  own rule, and Fire its flicker (its finish handler draws it 2 frames in 4
  by `InterruptCounter`, untouched).
- Seamless draws it on its side: its cells in the live room's OAM are moved
  a room over, and slice 1's lists give the cells past the edge.
- A crossing keeps it: `adopt()` stops clearing `CastMagic_Type`, and the
  side changes with the hero's room.
- Spells fly sideways. Tilte's drop and a spell at a seam above or below end
  as now.

### 4. The view shows only rooms (slice 4)

The camera clamps to the box around the rooms joined to his. Instead, it
keeps the view over places that hold a room:

- From the position centred on him, it moves the least it must so that no
  part of the view is over an empty place. The view is exactly a room tall
  (208), so his own room's row always fits; sideways, any room beside his
  fills the rest (256 + 256 is more than 424). A room with no room on either
  side, as a boss arena is, stays centred, black at its sides, as now.
- Where a limit changes as he walks (a column with an empty place comes into
  view), the camera glides to it a few pixels a frame, and looks ahead by
  the distance the glide needs, so it is never caught showing black.
- All in `fax_world_present` and its camera; nothing in the game changes.

### 5. What a neighbouring room sees of him (slice 6)

- **His distance.** Behaviours that measure how far he is use
  `Sprite_CalcDistanceXToPlayer` (`$82F8`) and `Sprite_CalcDistanceYToPlayer`
  (`$831B`): the script ops `SpriteOp_CheckDistanceToPlayer_X` and `_Y`
  (`$A7F8`, `$A82A`), the Giant Bees, the Naga, Sir Gawaine and the Wolfman.
  `SpriteBehavior_Ishiisu` (`$9129`, within 32 pixels) and `FUN_PRG14__9aa1`
  (64) do the same sum inline. In a copy he is at the seam for all of them,
  so a monster near it acts as if he stood beside it, however far he is.
  Hooks there give a copy's actor his true distance, capped at 255.
- **Nash and Magman** place themselves by him: `SpriteBehavior_Nash`
  (`$9060`, Mist `$2A`) 32 pixels beside him, `SpriteBehavior_Magman`
  (`$95C0`, Branch `$20` and `$22`) 48 pixels in front of or behind him, at
  his height. In a copy that is by the projected hero, at the seam, and
  Magman's X, which does not stop at the screen's edge, wraps to the far
  side of its room. In a copy, Seamless puts the one that places itself
  beside his true place, in his room: mechanism 1's move.
- **Above and below**, rooms see him on their bottom or top row: slice 5
  gives fliers and fallers his true Y the same way.

### 6. Loose ends (slice 7)

- **One-in-four items count a room met as a neighbour.** After a copy's
  tick, its change to `Screen_TrackedVisitCount` carries to the live game,
  so meeting a room counts one visit, until a load refreshes it.
- **The fountain's ladder from afar.** A room fixup
  (`fax_world_set_room_fixup`, which grows from one function to a list:
  the graphics fixes use it) writes the moved stone and the ladder into
  `$28`'s decoded blocks and clears its event while `Quests` `$20` is set,
  so `$27` shows the ladder and nothing runs as he crosses.
- **A death at a seam.** Seamless paints his cells past the edge in a death
  too: slice 1's lists from `Player_DrawBody` (`$EBEE`) and
  `Player_DrawDeathAnimation` (`$D9D6`), or `player_edge()`'s isolated
  redraw with those two.

### Why not the alternatives

- **Hits in the copy, their effects carried back.** Let each copy see the
  real swing, the hero's true position and the spell, and replay what it did
  to him (experience, damage, sounds) in the live game. It starts smaller:
  stop blanking the swing, and the sword lands. But every feature that hooks
  a hit or a kill skips isolated calls on purpose and would have to learn to
  count a copy's (MP from kills, the damage numbers, the Pendant fix, Slain
  by, and the next one written); an item's pickup can't run there at all;
  and two copies and the live game can each hit him in one frame, to be
  replayed in some order. "Exactly as in his own room" would be a promise
  kept by hand, feature by feature. Visitors keep it by construction, since
  the live game runs its own code. The cost is more machinery up front: a
  second pass of the loop, with the room's slots hidden.
- **The spell moving into the next room's copy.** The copy would then decide
  what the spell hits, with the same problem: the kill's experience and MP
  thrown away. The spell stays put and the monsters come to it.
- **One machine for the whole view.** A room's coordinates are 8 bits and
  its 8 slots are all the game has; a 424-pixel view of up to three rooms
  doesn't fit in one.
- **Movers moved as soon as they near the seam.** In the room they enter,
  their X would wrap to the far side, and their behaviours would send them
  the wrong way. They move when their origin crosses, and are drawn whole
  and fight across the seam as visitors until then.
- **One-way moves, or a leash at a seam** (this plan's first version). Each
  keeps an invisible wall in view: see "Both ways, and dormant until seen".
- **Every actor awake** (its second version). Every room next to his ran its
  monsters against him unseen, so they came at him from rooms he hadn't
  looked into and piled up on him.
- **Dormant whenever out of the view**, so a monster he outruns gives up.
  Cameron's call is that one that has seen him stays after him.
- **Wandering until seen** instead of standing still. Nobody sees the
  difference, and a copy's monsters steer by the hero: wandering would need
  a made-up hero, which the game's hero-relative behaviours would chase.
- **Homes for every monster.** The first version kept each slot's home room
  so that a door into it could take its wanderers back. Refreshing the rooms
  joined to a load does the same with nothing kept, and matches what the
  original does at a load.

## The issue's questions

**How a monster or a spell moves between machines, the live game's
included.** A mover moves whole: its slot's every byte, including entity,
position, HP, state and timers, goes to a free slot in the room its origin
entered, between frames, and its old slot is emptied (mechanism 1). It moves
whichever way it walks, into the live game, out of it, or between copies.
Its position keeps its bytes side by side, and its Y moves by 208 one above
the other. A spell never moves: it is always the live game's, with a side
(mechanism 3). Monsters across a seam that he fights don't move either: they
visit the live game for its sprite loop's interactions and go home with the
result (mechanism 2).

**A full destination.** A room holding 5 actors, dormant ones included,
takes no mover: the seam is a wall for it, it turns there as it does today, and fights across it as a
visitor. 5 is the most any list holds, keeps 3 slots for shots and drops,
and is where the live game starts to lag. A visitor with no slot that frame
waits a frame.

**A monster that leaves its home room.** Only one he has seen moves at all.
It goes where it walks, out of the view too, and stays there until a load
refreshes the rooms joined to the one loaded, or an area change empties them
all. Killed away from home, it stays dead until then too, as every kill does
in Seamless. Its home room does not spawn it again meanwhile. One left two
rooms behind stands still, since only the rooms next to his tick.

**Save states, rewind and Guru travel mid-crossing.** A move happens between
frames, whole, so a state always has a mover in exactly one room; one
straddling a seam belongs to the room its origin is in. Visitors exist only
inside the frame's second pass, so no state holds one. A spell abroad is the
live game's `CastMagic_*` plus its side in Seamless's record, and each slot
away from its tiles has the room they are in (version 5). Rewind keeps and
loads whole save states (`cyc_state_save`, Seamless's record with them), so
the same holds. Guru travel and a temple's respawn go through a building,
an area change: Seamless empties every room, as now, and nothing travels
with him. A zone respawn is a load: the rooms joined to it are refreshed.

**Doors, boss rooms and regions.** None of them is a seam, so nothing crosses
them: a door, a region change and a town's edge are the original's own
loads, and nothing followed through those in the original. Eolis's town
edges are seams, and walls for monsters and townspeople alike (towns stay
safe). A boss arena
joins no neighbour while its boss lives (SEAMLESS-WORLD.md), so its edges
stay walls for everything, visitors and spells included. Big enemies stay in
their rooms; they can be hit across a seam and hit back across it.

**The original's encounters afterwards.** In the original a screen's edge
ends every fight: walking off scrolls away from everything on the screen,
and walking back brings it all back fresh. Nothing reaches across an edge,
because nothing is beyond it. Today, in Seamless, monsters beyond a seam
can't hurt him or be hurt, gather at the seam, and hit him as he steps in
(#130); a spell dies at the seam.

Afterwards a seam is open ground. Harder: monsters and their shots across a
seam hurt him; a monster he has seen follows him until it dies, a load
comes, or he outruns it by two rooms; a bigger fight forms where two rooms'
monsters meet. Easier: he strikes first across a seam instead of walking
into a waiting pile; spells reach the monsters he can see; nothing he hasn't
seen comes for him; a door still ends everything, and no monster follows
him into a town. Towns are livelier: their
walkers wander between rooms and come to him. Bosses are as in the original. The first fight to play is Eolis's
Zozuras; then Eolis's town, Trunk's Hornets (fliers), the Mist's Charrons
(fireballs) and Sugatas, Rokusutahn (staying home), and Dartmoor and Zenis,
where most big enemies stand by a seam.

## What it costs, and the risks

About 2,100 lines of C as estimated, and eight end-to-end tests, slice 0's
among them. Read that as a floor: slice 0, estimated at 100, came to about
200 lines of C (`fax_seams.c` is half comments) and 540 of tests, so the
whole is more likely 4,000 lines of C, and as much again in tests. All but
the camera live in `src/fax_seams.c` (slice 0's file, its hook prefix and
allocations), with `fax_world.c` giving it what it needs: a room's RAM to
write, a room's side of the hero, calls at a tick, a crossing and a load,
and the drawn-whole cells to paint. The camera is `fax_world.c`'s own.
`fax_world.c` is about 630 lines now.

- **Encounters change the most:** followers once seen, trains of up to 5,
  busier towns, and actors standing still until first seen, his own room's
  far side included. They need a playtest, not only tests
  (see "Both ways, and dormant until seen").
- **The second pass runs the game's interaction code on slots it never held
  together.** The checks read the slot, the hero and the spell, not the room,
  but a hit handler or a feature's hook may read more. Each kind of visitor
  gets its own test.
- **Slot pressure and lag.** A room takes movers up to 5 actors, and at 5
  the live game lags now and then, as the original's busiest screens do.
  Visitors use free slots only, and can wait a frame.
- **One frame's lag.** A visitor's position is from its room's tick after
  the last frame. At 1 to 3 pixels a frame it doesn't show, but a frame-exact
  test of a hit across a seam must count it.
- **Save states.** The record goes to version 5; older Seamless states don't
  load into it (SEAMLESS-WORLD.md: they need not), save files are unaffected.
- **Tiles from home** is new drawing code, in the compositor alone. During a
  death Seamless shows the game's own picture, where such a monster has the
  room's tiles, not its own.
- **The camera's feel** changes where it glides to keep empty places out of
  view: a playtest call too.
- **One above the other** adds Y conversion everywhere; it comes after the
  sideways slices.

## Slices, each end to end

Each is its own PR, with its tests, its GAME_INTERNALS notes and its line in
the README. They are in the order a player notices them, with each slice's
needs before it. The sizes are lines of C as first estimated; slice 0 took
twice its estimate (see "What it costs").

| Slice | What it does | Its end-to-end test | Size |
|---|---|---|---|
| 0. Seams lose nothing (**done**, #177) | Copies stop touching the hero (no lost drops, no frozen rooms). The weapon's reach in 16 bits (no ghost hit; the original keeps it) | A coin at room 5's edge stays, and he takes it walking in; the Dragon Slayer there leaves room 5 ticking; a swing at room 4's x 4 leaves a Zozura at its x 236 alone, and the original's kills it | Small: estimated at 100 lines, built in about 200 |
| 1. Seams are open ground (**done**, #178) | Monsters, shots and walking people go through side-by-side seams both ways, between any two rooms, up to 5 actors a room; dormant until first seen, then awake for good (a shot leaving the view is removed); drawn whole across a seam; tiles from home; a load refreshes the rooms joined to it; record version 5 | Eolis's walking woman walks from room 2 into his room 1, and the walking man in and out (`tests/seams/open_ground.py`, as all here); a room 5 Zozura follows him back through room 4 to the town's edge (room 3's side) and turns back there, none ever in room 3, and room 4's follow him into room 5; the walking man at room 1's side by room 8 turns back there; a Zozura never seen stands still, wakes the first frame it is in view, and keeps coming after it leaves the view; unseen monsters in two rooms don't gather at him; a missed fireball flies on through a seam, and one leaving the view is gone; a room holding 5 keeps its seam a wall, one holding 4 doesn't; a straddling Zozura's pixels on both sides of the seam; a zone respawn's load brings the rooms beside it back fresh; the intro's trigger, a person who talks when touched, at a neighbour's edge leaves its room ticking (slice 0's hook); a save mid-crossing replays alike in two fresh games | Large: estimated at 730 lines, built in about 850 lines of C (`fax_seams.c` half comments) and 1,050 of tests |
| 2. Fights across a seam | The visitors' pass, side by side: the sword, touches, shots on him and his shield, bread and coins; a neighbouring room's sounds; Sugata's flash | From room 4, he kills a room 5 Zozura: its experience and MP, its death in room 5, its coin and the coin's sound; a Zozura straddling the seam hurts him; he takes a coin across it; a Sugata across a Mist seam flashes him in reach | Medium, about 470 |
| 3. Spells fly on | The spell's side, walls across, the view's edge, its picture, visitors in its path, kept through a crossing | Deluge ends at a wall across a seam and kills a Zozura across one; Fire still flickers; save and load mid-flight | Medium, about 230 |
| 4. The view shows only rooms | The camera keeps the view over rooms, gliding where its limits change | Walking the Tower of Trunk from `$10`, and a Mist stretch with an empty corner: on no frame does the view lie over an empty place (`fax_world`'s camera against `fax_world_room`'s places), and it moves a few pixels a frame at most | Medium, about 150 |
| 5. One above the other | Slices 1-3 across seams one above the other: Y converted, fliers and fallers, drawn whole up and down, his true Y for the rooms above and below | A Hornet follows him down a Trunk ladder; his sword hits one across a seam above | Medium, about 250 |
| 6. What a neighbouring room sees of him | His true distance in a copy; Nash and Magman beside his true place | An Ishiisu across a seam waits until he is within its 32 pixels; Nash reappears beside him, in his room | Small, about 120 |
| 7. Loose ends | One-in-four items count a room met as a neighbour; the fountain's ladder from afar; a death at a seam drawn whole | Rooms of one-in-four items met in turn move the count, and one item in four shows; from `$27` with `Quests` `$20`, `$28` has its ladder and nothing runs as he crosses; a death at x 248 dissolves whole across the seam | Small, about 90 |

Slice 1 goes first because waiting at invisible lines is what a player sees
most, in every town and most fights, and because it makes the fighting
across a seam brief; slice 2 then closes that gap, and slice 3 needs its
visitors. Black where no room is (4) shows without a fight, but Cameron's
report named the combat first. Seams one above the other (5) are fewer, and
fights there rarer; 6 and 7 are narrow.

### Slice 1, as built (#178)

One PR: the moves would look wrong without drawing whole and tiles from home
(a mover vanishing for the frame it crosses, and a person walking into a room
without its tiles drawn as a jumble), so they went together. Where the plan
changed:

- **Dormancy** (Cameron, during the build): dormant until first seen, then
  awake until it dies or a load; shots out of the view go. Its wake is a
  bit per slot in the record.
- **Two more hooks**, the floor tests that read past a side
  (`$873A`, `$855E`), and the flier's at `$842A`, after its move, not at
  `$8427`. The blocks across come from the neighbour's own record.
- **Drawn whole** by replaying the draw's tile loop at its two entries, which
  also notes each OAM entry's slot for tiles from home; a slot taking another
  of its kind's tile offset takes that slot's tiles' room too.
- **A load** is `Screen_LoadSpriteInfo` in play, and it forgets every other
  room of the area.
- **The hero's wrapped cells.** Seamless shifted every OAM cell at x 0-31 of
  his room a screen right while he stood past x 240, meant for his own;
  a monster there (now often, one just through the left seam) was drawn
  in the next room. Only cells no actor drew move now.
- **Towns stay safe** (Cameron's call on #166): Eolis's town is joined to
  the screens past it, so its Zozuras, once seen, followed him into the
  town. Now a town's edge is a wall for every mover; a town room is worked
  out from `fax_zone.c`'s zones and the rooms' lists.
- **Encounters.** The walking woman's script drifts her one way (*Tells*),
  so her test walks her in and the walking man in and out.
- **Cost.** Headless, 3,000 frames from the same save, slice 1's hooks on
  against off: Eolis's town 3.41 against 3.43 ms of CPU a frame, Apolune
  3.45 against 3.45, within noise; the neighbours' ticks took a third of the
  cycles in Eolis's town, its unseen people standing still. Seamless's step
  stays at 0.06-0.2 ms.

Boss rooms are covered by `tests/world/boss_arenas.py`, which every slice
runs, with a check that no mover or visitor comes from an arena.

## The fallback

This plan's first version offered slices 0-3 and 5 without the moves. It is
dropped: it keeps every seam a wall for monsters, the very tell "one big
world" rules out.
