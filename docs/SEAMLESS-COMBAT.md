# Combat across Seamless's seams: the plan

Issue #166. A plan for review, not a build: nothing here is in the game yet.
It starts with what the game does at a seam today, checked in the game, then
recommends a design, answers the issue's questions, and cuts the work into
slices that each run end to end. [SEAMLESS-WORLD.md](SEAMLESS-WORLD.md)
describes Seamless itself; the addresses are chipx86's labels
(`tools/disassembly.sh`), and what was learned about the game is also in
[GAME_INTERNALS.md](GAME_INTERNALS.md).

**In short.** Today nothing crosses a seam. The copy of a neighbouring room
blanks the hero's swing, so it never lands there; a spell disperses at the
edge of the hero's room; and monsters wait at the seam, because the hero they
see is pinned there. Meanwhile the live game's swing wraps round and hits
monsters behind him. The recommendation keeps the rule Seamless already
has, that the live game owns the hero. Monsters near a seam *visit* the live
game for its sprite loop's interactions; the spell stays the live game's and
flies on into the next room; and a chaser moves into the room it walks into.
That is six slices. The fallback is the same slices without the moves:
everything but chasers following.

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
   inventory is thrown away.

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

### The sword never hits across the seam, and the live game hits behind him

Six Long Sword swings from room 4's x 4, facing left, with room 5's Zozuras at
its x 228-237: the weapon's box (`PlayerPosXPlus10`, `$CE`) was at 244, then
236, which is room 5's x 244 and 236, right over them. Both kept 9 HP and a
hit counter of 0, and experience stayed 0. **The hit never lands in the
copy.** `Player_HitSpriteWithWeapon` (`$8804`) hits only in attack phases 1
and 2, and `shadow()` sets the copy's `Player_AttackPhaseAnimFrame` to 0.
SEAMLESS-WORLD.md's "his swing there can hit them in that room's copy" was
wrong, and is corrected.

Had it landed in the copy, the hit, the knockback and the death would have
stayed (they are room-local), and the experience and the sound would have
been thrown away.

**The live game's own check hits a monster at the far side of his own
room**, the original's bug. `Player_DrawWeapon` (`$B7D6`) keeps the weapon's
X as 16 bits (low byte `PlayerPosXPlus10`, `$CE`, high byte
`Maybe_Something_WeaponPosX`, `$CF`) and stops before the Y and the reach
when the high byte is not the screen's (`Screen_Maybe_ScrollXCounter`,
`$9F`), but `Sprites_IsSpriteOutOfWeaponRange` (`$88CB`) compares the low
byte only. With a live Zozura put at room 4's x 236, 232 pixels behind the
hero, the first swing hit it in its first hit phase (HP 9 to 1, hit counter
8, sound 2, knocked back) and again in its second, which killed it, for 30
experience. With the remaster off, the original kills it with the first hit
(the Pendant Bug's quarter), for the same 30. In the original only a hero at
a screen's edge with a monster at the other edge meets it. In Seamless he
also stands at x 241-255 of a room before he crosses, where a swing to the
right wraps the same way; and the monster he kills is off the view, while the
one in front of him takes nothing.

### Also found

- **Monsters across a seam can't hurt him.** In the copy he is invincible
  (200), so `Player_HandleTouchEnemy` (`$89AE`) returns at once, and the
  copy's damage would be thrown away anyway. They hurt him only once he
  crosses into them (#130).
- **The copy takes drops at its edge, and the gold is lost.**
  `CurrentSprite_CheckHitPlayer` (`$890A`) sends every touch to its
  category's handler with no invincibility check, and
  `Player_HandleTouchBreadOrCoin` (`$8968`) has none either. A coin put at
  room 5's x 236, on its floor, was gone within two frames, and the live
  game's gold never moved. The same coin at x 100 stayed. A monster killed
  near a seam drops just there.
- **An item at a neighbour's edge freezes that room.** The Dragon Slayer
  (`$5A`) put at room 5's x 240: every tick of room 5 failed from then on
  (Seamless's `failures` count rose by one a frame; its ticks stopped), so its
  Zozuras froze, and stayed frozen with the hero walked to x 121. The pickup
  (`Player_HandleTouchItem`, `$88FB`) can't run in an isolated call, so the
  whole tick is dropped. Nothing was taken.

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

## The recommendation: the live game decides, monsters visit it, chasers move

One rule from Seamless's own design carries over: the live game owns the
hero, so **everything that touches him happens in the live game, through the
game's own code**. Three mechanisms follow from it.

### 1. Visitors: monsters across a seam take part in the live game's sprite loop

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
  enemies (7), not people (2, 4) or items (5): an item's pickup runs an
  interaction script with its line in a text box (the Dragon Slayer's is
  `$8D`, through `IScripts_Begin`), so it is taken in its own room, which he
  is in by then. Only actors
  the loop would let interact: hit counter 0 and no magic-hit behaviour
  (`Sprites_UpdateAll` skips the interactions for the others too).
- **Where.** At its position in the live room's coordinates: the same X byte
  side by side; Y plus or minus 208 one above the other. Seamless notes which
  side each visitor came from.
- **Told apart.** Each interaction check compares true positions, in 16
  bits: the weapon's X (`$CE`/`$CF`), the spell's with its side (below), the
  hero's, and a visitor's with its side. Hooks at
  `Sprites_IsSpriteOutOfWeaponRange` (`$88CB`), the overlap tests of
  `Sprite_CheckHitByCastMagic` (`$8ADC`), `Player_CheckShieldHitByMagic`
  (`$877C`) and `CurrentSprite_CheckHitPlayer` (`$890A`) refuse a match the
  8-bit test found only by wrapping round the room. This ends the ghost hit
  too, in the remaster: a fix for the original's bug, under "Bugs fixed".
  With the remaster off, the original keeps it.
- **Back home.** Every byte of the visitor's slot goes back to its room but
  its position: HP, the hit counter, the magic-hit behaviour and its
  invincibility, its facing, and a death (the entity turned into the death
  effect) or a removal (a shot on the shield, a coin taken). The room's next
  tick does the knockback, the death's animation and the drop, as it would
  for a hit in the hero's own room.
- **Slots.** Visitors take the slots the room's own actors leave free during
  the pass, nearest first. One that finds none waits a frame.
- **Copies stop touching the hero.** In an isolated tick,
  `CurrentSprite_CheckHitPlayer` returns at once: the live game is the only
  place he is touched. That ends the lost drops and the frozen rooms.
- **Features that place something by a slot** (the damage numbers, which draw
  at the hit's room and slot) ask Seamless which room the slot's actor is in.

### 2. The spell flies on, still the live game's

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
- Seamless draws it on its side, unclipped, as `player_edge()` draws the
  hero's tiles past the NES raster.
- A crossing keeps it: `adopt()` stops clearing `CastMagic_Type`, and the
  side changes with the hero's room.
- Spells fly sideways. Tilte's drop and a spell at a seam above or below end
  as now.

### 3. Chasers follow: a monster moves to the room it walks into

A monster (category 0) or a shot (6) pressed against a joined seam, **toward
the hero**, goes through it. Once its origin is across, it moves to that
room's machine.

- **The edge.** Hooks at the edge checks, `FUN_PRG14__86bd` (left),
  `Sprites_MoveRight` (right) and `Sprite_Maybe_TurnAroundIfAtScreenEdgeHoriz`
  (`$8427`, the fliers' and the shots'), make the seam passable when: a joined room lies
  that way; the hero is that way; the blocks across are passable (each room
  already holds its neighbours' edge columns); that room has 2 slots free; and
  the monster is in its home room, or the seam leads home (one room's leash).
  Otherwise the seam stays a wall, as now. In copies the projected hero is
  clamped to x 255 instead of 240, so a chaser keeps coming until it crosses.
- **The move.** Between frames, in `fax_world_step`: every per-slot byte
  (`$BA`/`$C2`, and every array from `$02BC` to `$0373`: the fractions,
  entity, behaviours, flags, phases, behaviour data and states, hitbox, the
  behaviour's target position, tiles, magic-hit state, HP, hit counter,
  behaviour script and value) goes to a free slot of the room it entered, and
  its old slot is emptied. Side by side the bytes are kept as they are; one
  above the other, its Y and its target's Y move by 208. Into the live game
  through `cyc_mod_poke`, as `load()` writes it. A monster always walks
  toward the hero, so the live game only ever receives them.
- **Its tiles.** If the room it enters has its entity, it takes that slot's
  tile offset. Otherwise Seamless draws the slot from its home room's CHR,
  noting which OAM entries the slot wrote (a hook around
  `CurrentSprite_UpdateState`).
- **Its home.** Seamless keeps each slot's home room in its save-state
  record (version 5).
- **Never moved:** people, items, big enemies (category 7), multi-part
  actors (`Sprite12BodyPartHandler`, `$0379-$037E`), and anything in a boss
  arena.

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
- **Chasers moved as soon as they near the seam.** In the room they enter,
  their X would wrap to the far side, and their behaviours would send them
  the wrong way. They move when their origin crosses, and fight across the
  seam as visitors until then.

## The issue's questions

**How a monster or a spell moves between machines, the live game's
included.** A monster moves whole: its slot's every byte, including entity,
position, HP, state and timers, goes to a free slot in the room its origin
entered, between frames, and its old slot is emptied (mechanism 3). Its
position keeps its bytes side by side, and its Y moves by 208 one above the
other. A spell never moves: it is always the live game's, with a side
(mechanism 2). Monsters across a seam that he fights don't move either: they
visit the live game for its sprite loop's interactions and go home with the
result (mechanism 1).

**A full destination.** The seam is a wall for that monster: it turns round
there as it does today, and fights across it as a visitor. The test is 2
free slots, so the room keeps one for its own monsters' shots. A visitor
with no slot that frame waits a frame.

**A monster that leaves its home room.** It does not respawn at home while it
lives; its home slot stays empty. Killed away from home, it stays dead, as
every kill does in Seamless until the area reloads. It goes at most one room
from home. A room the game loads fresh (a door into it, a death's respawn,
an area change) spawns its whole list as the original does, and Seamless
first removes the monsters whose home it is from every other room, so none
meets itself.

**Save states, rewind and Guru travel mid-crossing.** A move happens between
frames, whole, so a state always has a monster in exactly one room; one
straddling a seam belongs to the room its origin is in. Visitors exist only
inside the frame's second pass, so no state holds one. A spell abroad is the
live game's `CastMagic_*` plus its side in Seamless's record. Rewind keeps
and loads whole save states (`cyc_state_save`, Seamless's record with them),
so the same holds. Guru travel and a death's respawn go through a temple, a
building, which is an area change: Seamless empties every room, as now, and
nothing travels with him.

**Doors, boss rooms and regions.** None of them is a seam, so nothing crosses
them: a door, a region change and a town's edge are the original's own
loads, and monsters never followed through those. A boss arena joins no
neighbour while its boss lives (SEAMLESS-WORLD.md), so its edges stay walls
for everything, visitors and spells included. Big enemies, Rokusutahn (in 19
rooms' lists) among them, stay in their rooms; they can be hit across a seam
and hit back across it.

**The original's encounters afterwards.** In the original a screen's edge
ends every fight: walking off scrolls away from everything on the screen,
and walking back brings it all back fresh. Nothing reaches across an edge,
because nothing is beyond it. Today, in Seamless, monsters beyond a seam
can't hurt him or be hurt, gather at the seam, and hit him as he steps in
(#130); a spell dies at the seam; a drop there can vanish.

Afterwards a seam is open ground. Harder: monsters and their shots across a
seam hurt him; a chaser follows him a room, so stepping over a seam escapes
nothing; a bigger fight forms where two rooms' monsters meet. Easier: he
strikes first across a seam instead of walking into a waiting pile; spells
reach the monsters he can see. Respawns are as Seamless has them now. Bosses
are as in the original. The first fight to play is Eolis's Zozuras; then
Trunk's Hornets (fliers), the Mist's Charrons (fireballs), Rokusutahn
(staying home), and Dartmoor and Zenis, where most big enemies stand by a
seam.

## What it costs, and the risks

About 1,400 lines of C and six end-to-end tests, in its own feature file
(`src/fax_seams.c`, its own hook prefix and allocations), with `fax_world.c`
giving it what it needs: a room's RAM to write, a room's side of the hero,
and calls at a tick and at a crossing. `fax_world.c` is 626 lines now.

- **The second pass runs the game's interaction code on slots it never held
  together.** The checks read the slot, the hero and the spell, not the room,
  but a hit handler or a feature's hook may read more. Each kind of visitor
  gets its own test.
- **Slot pressure.** Visitors and arrivals use free slots only, and arrivals
  leave one. A busy room's fight across a seam can wait a frame.
- **One frame's lag.** A visitor's position is from its room's tick after
  the last frame. At 1 to 3 pixels a frame it doesn't show, but a frame-exact
  test of a hit across a seam must count it.
- **Encounters change** as above, so they need a playtest, not only tests.
- **Save states.** The record goes to version 5; older Seamless states don't
  load into it (SEAMLESS-WORLD.md: they need not), save files are unaffected.
- **Tiles from home** (a monster in a room without its kind's tiles) is new
  drawing code, in the compositor alone. During a death Seamless shows the
  game's own picture, where such a monster has the room's tiles, not its own.
- **One above the other** adds Y conversion everywhere; it comes last.

## Slices, each end to end

Each is its own PR, with its tests, its GAME_INTERNALS notes and its line in
the README.

| Slice | What it does | Its end-to-end test | Size |
|---|---|---|---|
| 0. Seams lose nothing | Copies stop touching the hero (no lost drops, no frozen rooms). The weapon's reach in 16 bits (no ghost hit; the original keeps it) | A coin at room 5's edge stays, and he takes it walking in; a swing at room 4's x 4 leaves a Zozura at its x 236 alone, and the original's kills it | Small, about 100 lines |
| 1. The sword across a seam | The visitors' pass, side by side, for the weapon | From room 4, he kills a room 5 Zozura: its experience, its death in room 5, its drop | Medium, about 300 |
| 2. Monsters strike back | Visitors' touches, shots on him and his shield, bread and coins | A room 5 Zozura hurts him across the seam; he takes a coin across it | Small, about 120 |
| 3. Spells fly on | The spell's side, walls across, the view's edge, its picture, visitors in its path, kept through a crossing | Deluge ends at a wall across a seam and kills a Zozura across one; Fire still flickers; save and load mid-flight | Medium, about 250 |
| 4. Chasers follow | Moves between machines, the edge hooks, the leash, homes, record version 5, tiles from home | A Zozura follows him from room 5 into room 4; a full room's seam stays a wall; a door into its home takes it back; save and load mid-crossing | Large, about 450 |
| 5. One above the other | Slices 1-4 across vertical seams: Y converted, fliers and fallers | A Hornet follows him down a Trunk ladder; his sword hits one across a seam above | Medium, about 200 |

Boss rooms are covered by `tests/world/boss_arenas.py`, which every slice
runs, with a check that no visitor comes from an arena.

## The fallback

**Slices 0-3, and 5 without the moves: everything but chasers following.**
Spells fly on and hit across a seam, the sword hits across it, monsters hit
back across it, nothing is lost at it. A seam stays a wall for monsters, as
in the original's screen edge, but now one they fight across. It needs no
moves between machines, no homes, no tiles from home, and no new
save-state record beyond the spell's side: about 900 lines, and no change to
which room a monster is in. If the moves are built later, they go on top
unchanged.

The smallest fix, the issue's example, is slice 0 and slice 3 without
visitors: spells fly to a wall or the view's edge and hit nothing across. I
don't recommend stopping there: the sword is the hero's main weapon, and
fights at seams are everywhere (132 of 171 side-by-side seams have monsters
on both sides).
