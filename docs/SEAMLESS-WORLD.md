# Seamless world

Owner request: 2026-10-27. Tracking: `beads-2dw.17.2`; shared CIRAM upload:
`beads-2dw.1.108`. Isolated branch: `feat/seamless-world-20261027`, based on
released Faxanadu `6a297c2`. It began as an experimental local-play preview,
a package of its own. **Since #110 (2026-11-22) it is part of the remaster:**
a `[[plugin]]` (`faxanadu.seamless-world`) of the one Faxanadu DX
feature, so the remaster always plays in it, and with the remaster off the
game is the original in its stock 256x240 view. Every remaster end-to-end
test plays in it. What only a full playthrough would show is under "Still
open", below.

## Playable behavior

Ordinary reciprocal screen connections form a continuous world: the camera follows the player, adjacent
terrain is visible, and neighboring native actors advance before entry.
Room handoffs keep those actor instances rather than respawning them.
Horizontal movement and climbing continue through supported seams without
the original stop-and-scroll sequence.

**One fixed view: 424x240** (#107), whatever the window. That is the
original's full 240 lines, its HUD and a room's 208, so exactly as much shows
above and below the hero as on the NES, and 53 of its 8-pixel tiles across,
the width Sonic Mania widens to for the same idea. Nothing changes it: there
is no zoom and no option. **The view shows only rooms** (#166's slice 4,
`fax_camera.c`): the camera follows the hero, and from the place centred on
him moves the least it must to keep off places of the map that hold no
room. Sideways it stays over his row's run of rooms (any room beside his
fills the rest: 256 + 256 is more than 424), centred on a room alone in its
row, an arena or a shaft, black at its sides, as before. Up and down it
follows him into the next row only as far as the columns it spans hold rooms
there, and that limit eases in, a line for each pixel of the span's
clearance, so walking toward an empty corner the view comes down to his row
a pixel for each of his, before the corner would be in sight. Near a ladder
or an opening between rooms one above the other it moves sideways onto the
columns both rows hold rooms in, so it follows him through without black;
where the two rows share only his column (25 of the 101 such seams), black
can't be helped, and the view keeps his row until his body reaches the seam,
then follows him through in the 32 pixels he straddles it. It is a function
of his place and the map, so it is the same however often a frame asks, and
a save state holds nothing of it. Before, it clamped to the box round the
rooms, black wherever the box held no room (142 places beside a room). The
inventory, text boxes and the original's own pictures (death, scripts, region
loads) sit where the native picture would, centred. The status bar band, its
top 32 lines, is the one exception (#113, #117): it and the Elixir left of its
`M:` and `P:` bars (`fax_charges.c`) are centred together, so the band's
columns start at x 92, 8 pixels right of the room's, and the Elixir is at x
76-91 (`fax_world_hud_x`).
The host scales the view by its one fixed rule (#107), whatever its Display
settings say: Seamless asks for it (`sharp_scale` in `cyc_host_extras.h`,
the rule in the engine's `cyc_present_fit.h`), so `IntegerScale` and
`LinearFilter` apply only to the original's 256x240 picture, the remaster
off. The rule: the largest scale that fits the window's drawable, in pixels
(a HiDPI window has twice its points), keeping 424:240, with bars only where
the window's shape differs. A whole-number scale is nearest-neighbour: 3x,
6x and 9x at 720p, 1440p and 4K (1272x720, 2544x1440, 3816x2160, so 4, 8
and 12 pixels of bar each side). Any other is sharp: nearest-neighbour up to
the next whole multiple, then linear down to the fit. At 1080p that is 4.5x,
1908x1080: the view drawn at 5x (2120x1200), then down. Each pixel keeps its
exact colour but for one blended output pixel at most at each edge, where
the 4.5-pixel grid cuts it (every other edge, at 4.5x), as Shovel Knight
scales. A window smaller than the view shows all of it, linearly smaller.
F4 toggles a connected-map overview, capped at the preview's 4096x2048
presentation limit. Empty space outside the connected geometry remains
empty; terrain is not stretched or repeated to fill it.

Doors, region changes and connections that cannot be embedded consistently
retain native portal transitions. **A load's room shows where it stays**
(#204): Seamless lays the map out from the new room on the first frame the
game's picture shows it (its status bar, its screen set up), so from that
frame the room is at its place under the camera, the rooms round it drawn
with it, and until the game's main loop runs there (a few frames, while it
loads the sprites' tiles) it is the game's own picture there, as through a
death, sprites coming in as the loop draws them. The room a load leaves stays
where it was for as long as the game's picture shows it (a town's edge has
no fade: its first frame still shows the old room). Whatever moves, moves
while the screen is dark. Before, a door that says a key's line and a zone
respawn showed the room centred, the game's own picture, until the loop's
first tick, and it jumped to its place then: out of Eolis's way out, 84
pixels left as the Trunk came in; and a town's edge centred the old room for
its first frame. Temples and the other buildings past area 4's first 10
screens are on no map, and show the game's picture centred, as before.

**Boss screens are arenas** (#110). A boss's screen (`SCREEN_EVENT_BOSS` in
its sprite list: Trunk `$15` and `$3B`, Mist `$34` and `$4C`, Branch `$07`
and `$1D`, Dartmoor `$15`) and the final boss's (Zenis `$00`) join no
neighbour while the boss lives: the hero comes in through the original's own
screen change (a scroll, or a door) and fights on that one screen, centred in
the view, as in the original, and none of its actors runs before he enters.
Once the game's own handler (`ScreenEvents_HandleBoss`) finds no boss in the
room's slots and clears its event, Seamless lays the map out again from that
room and it joins the world; walking out is then a handoff. A boss's room not
entered since its area loaded has its boss, as the original reloads it, but
one killed stays dead while the hero stays in the area, as every room's
actors do in Seamless. `fax_world_room` reports each room's `event` and
whether it is on the map (`owned`), and `fax_world` whether the active room
is an `arena`. `tests/world/boss_arenas.py` walks into every one. Original key checks, scripts and progress
remain in charge. Start pause freezes the expanded world without changing its
aspect ratio. The native inventory appears over the frozen expanded world;
only its text boxes cover the scene, so actors remain visible both inside and
outside the original NES bounds. Dialogue retains a completed world pose and camera throughout the
opening, text, and closing; native text and portrait panels cover that scene.
**The status bar is not part of that pose** (#132): its band is the game's own
picture's, taken at every frame's end that shows it (`fax_text_hud_showing`),
in play, conversations, shops and scripts alike, so a purchase's gold counts
down as it is paid, the King's gift counts up, and HP, MP and `T:` follow the
game, as in the original; the Elixir (`fax_charges.c`) and the spell cost
(`fax_spell_cost.c`) are drawn over it from the game's state as before. The
band is live in a Start pause and the Select menu too: a pause changes
nothing on it, so it looks as frozen as the world, and the map and the quest
log cover the whole picture. A frame without the status bar (the screen off while
one loads) keeps the last band shown, and that band is the
`faxanadu.world.hud` save-state record. Until #132 the band was copied only
on frames of play, so a conversation showed the one before it throughout.
Death retains its native animation inside the fixed canvas.
Neighboring terrain uses the current region background palette, including
native fades. All visible actors use the active world scene's sprite palette;
cached building or portrait palettes cannot recolor neighboring NPCs/enemies.
Every sprite of a room is drawn, with no limit to a line, in the order the
game drew it, the first in front: the hero over everyone, the same on every
frame. A frame the game lagged keeps the last finished frame's room, as the
NES keeps its last sprites then (No sprite flicker, #161).
Native controls: Select opens inventory; B closes it; Start pauses/unpauses.
The painted gate in the first Trunk room has no registered Up destination in
the USA game. The preview preserves that behavior; its registered-door setup
provides an actual unlocked Up interaction.

## Implementation and reuse

Faxanadu's verified USA disassembly and prior native loader/actor fixtures
were reused. The approach follows the Zelda seamless-world room-instance
prototype (`beads-2dw.18.3`) but uses Faxanadu's actual neighbor tables,
256x208 playfields, native ladders and screen-specific actors. A whole-game
rewrite would replace already functioning combat, scripted doors and mantra
progression; reusing the original routines is the smaller coherent change.

`fax_world.c` calls the ROM's screen decoder and actor/image loaders inside
complete isolated machines. Each room owns actor arrays, local timers/RNG,
terrain, collision borders, graphics and palette. The real player, stack,
inventory, music, global events and persistent saves retain their ownership.
Only explicit room-local fields transfer at a seam. Native actor routines
run in a visible-area halo, while inactive rooms retain their records.
A neighbouring room's copy touches nothing of the hero's (Seams lose
nothing, `fax_seams.c`, #166): its sprite loop skips
`CurrentSprite_CheckHitPlayer` (`fax_world_ticking()`), so a coin, bread, an
item or a person at its edge waits for him to walk in, and the live game
takes it then. Before, the hero projected to that edge took drops there and
their gold was lost with the copy, and an item or a person who talks when
touched made every tick of the room fail, freezing it.

**Seams are open ground** (`fax_seams.c`, #166's slice 1). Monsters, their
shots and the people who walk go through side-by-side seams both ways,
between any two rooms, the live game's included: an actor whose origin
crosses moves, every byte of its slot, to a free slot of the room it entered,
between frames (`fax_seams_step()`, called once the neighbours have ticked).
Each stands still until the view first comes within 16 pixels of it, in his
room or a neighbour's, and stays awake from then on, so what he hasn't seen
never comes for him and what has seen him follows; a shot wholly out of the
view by 16 pixels goes. A room takes no mover once it holds 5 actors. Seams
keeps the cells an actor draws past its room's side, and `actors()` paints
them in the room joined there; one that came from a room with no tiles for
it is painted from its home room's CHR. The hero a neighbour's actors see is
clamped to its x 0-255 (240 before). A load within the area (a door, a zone
respawn, the original's scroll into an arena: `Screen_LoadSpriteInfo` in
play) forgets every other room of the area, so each comes back fresh from
its list as it comes near. Each room keeps Seams' block in its record (384
bytes in version 5; 512 since slice 5, version 6).

**Fights across a seam** (`fax_seams.c`, #166's slice 2). Everything that
touches the hero still happens in the live game: once its sprite loop is
over, the actors of the rooms joined beside his that are within reach of him
or his spell visit it, in its free slots with its own hidden, and the loop
runs once more for them, interactions only (their behaviour and drawing are
their room's). So his sword and fist hit a monster across a seam (its
experience and MP his, its death and drop played out in its own room), it
and its shots hurt him, his shield takes those shots, and he takes a coin or
bread lying across the edge. The interaction tests compare true positions in
16 bits, so a sprite at the far side of his room is never taken for one
across the seam. A neighbouring room's sounds from what is in the view play
in the live game; a neighbour's Sugata warns and flashes there, and the live
game takes its flash when he is in its reach. The pass and the sounds still
to play are Seams' own record (`faxanadu.seams`).

**The spell flies on** (`fax_seams.c`, #166's slice 3). His spell stays the
live game's, and Seams keeps its side, the room it is in beside his: it
flies on through a side-by-side seam, ends at a wall of the room it is in,
and meets that room's monsters as visitors. Every spell ends at the edge of
the view, in his own room too, or at a side with nothing joined, so it never
reaches a monster he hasn't seen. It is drawn on its side, behind that
room's foreground, and a crossing keeps it (`adopt()` asks Seams; up or down
it still ends). Its side is in Seams' record, version 2.

**One above the other** (`fax_seams.c`, #166's slice 5). All of that holds
up and down too: a flier goes through a room's top or bottom into the room
joined there, and a monster that walks off a ledge over an opening falls on
into the room below and lands there, where the original removed it as it
reached its room's last row. A full room or a town's edge below is a floor.
A mover's Y moves by 208 between rooms (a room above's y 200 is his room's
-8); one straddling a top or bottom is drawn whole, its cells painted in
the room joined there; the rooms above and below his visit the live game's
sprite loop, so his sword reaches a monster across the seam above him; and
their actors see his true Y, held to 0-239. Spells still end at a seam
above or below, and when he crosses up or down. Each room's Seams block is
512 bytes now: Seamless's record is version 6.

The original player starts a right scroll when his right edge crosses at
x=241. This adapter retains the source until his origin wraps, preserving
continuous position. Wrapped floor/ladder samples read the correct neighboring
terrain. Vertical handoffs account for the original early scroll thresholds.
An isolated native player pose supplies tiles the NES raster clips at seams.

Native nametables also update at a handoff, preserving HUD data, so menus
and subsequent scripted transitions see the current terrain. The shared
`cyc_mod_ciram_upload` API validates bounds before any write, refuses isolated
calls, refreshes PPU caches and advances no emulated time. It addresses physical
CIRAM; the trusted caller remains responsible for layout and mirroring.

All content-qualified hooks and the payload CRC bind this profile to Faxanadu
USA `57dd23d1`. No ROM, disassembly, extracted graphics or map data is shipped.
World instance state and pending seam/collision policy serialize in a validated
mod record. Old save states need not be compatible; save files are
separate. The feature marks its session local-only.

## Validation and limits

Raw commands, inputs, per-frame world journals, hashes, complete states, PNGs
and receipts are retained under `G:/Temp/Codex/faxanadu-seamless-20261027/qa`.
All automated launches were hidden and muted with native saves disabled.

| Check | Result / scope |
| --- | --- |
| Horizontal crossings | Representative left/right exits in Trunk, Mist, Branch, Dartmoor and Zenis; native/interpreter state and trace agreement, destination reached, no stop-and-scroll in qualified per-frame journals |
| Vertical ladder | Real Trunk 3->4 upward ladder and 4->3 downward return, with no scrolling mode; downward native/interpreter state agreement |
| Neighbor actors | Actual room positions/ticks change before entry and remain in the adopted instance; no cross-room combat claim |
| Native door rules | Trunk 13->11 normal door; Trunk 11->13 refuses without J key, opens after a granted key and native use dialogue; native/interpreter state agreement |
| Inventory / native terrain | Native Select inventory and B close after a seamless handoff; native background remains the destination room |
| Pause / inventory aspect | Original Start and Select inputs retain the expanded picture and frozen room records, with native/interpreter agreement; host-paused wide/portrait resizes preserve the paused frame and neighboring ticks |
| Camera / scripts | Camera bounds checked at real wide/portrait drawable sizes (then adaptive); death/animation/message frames retain the canvas. Real NPC and locked-door dialogue retain completed actors and stable pixels throughout the message, preserve guest execution against the original, and reproduce exactly through save/load; shop portrait panels and closing also checked |
| Town palette | Eight connected Eolis rooms match native terrain samples; stale white instance palettes cannot recolor terrain. Static setup probes were neutral; the actual death/retry route reproduces stale neighboring NPC-room palettes in the old build, and the candidate corrects them with an identical complete guest core state |
| Sprite palette | Actual death/retry A/B preserves actor OAM, CHR and complete guest core state. All 357 changed sprite pixels in two affected NPC rooms map from stale building colors to the native outdoor palette; poisoning neighboring sprite palettes no longer changes NPC or enemy colors |
| Current-build restore | Supported right seam: complete final state and rendered picture reproduce exactly after save/load; native/interpreter state and rolling trace match |
| Fixed view (#107) | `tests/world/fixed_view.py`, in the suite, with config.ini's IntegerScale and LinearFilter both on: the canvas and picture are 424x240 at 424x240, 1920x1080, 1280x720, 1080x1920 and 300x600 windows, and what each presents (the ui layer, the whole drawable) is checked pixel by pixel against the picture: 1920x1080 sharp 4.5x, 1908x1080 at (6, 0), 1,628,160 pixels exactly their picture pixel's colour and the other 432,480 between their two; 1280x720 3x nearest, every pixel; 1080x1920 letterboxed, sharp at 2.55x; 300x600 the whole view, linearly smaller; black bars around each. F4's overview grows to the region and is presented whole at 1920x1080; F4 again restores 424x240. With the remaster off, the original's 256x240 at 1920x1080 is IntegerScale's 4x, 1024x960. The engine's `present_fit_test` (also in the suite) checks the fit at #107's sizes and every drawable up to 4K. The adaptive Fit it replaced was checked at 1536x720, 600x1200 and 3840x720 |
| Boss arenas (#110) | `tests/world/boss_arenas.py`, in the suite, a part per boss's screen: beside it, it is on no map; in by the original's scroll (or a door, for walled King Grieve and the final boss), the room alone with the boss handler running; the boss's slot emptied, the handler ends and the room joins its neighbour; out again by a handoff. The final boss's kill begins the ending, which reaches the King's room |
| Live status bar (#132) | `tests/world/live_status_bar.py`, in the suite, read off the presented picture's pixels (the band's gold digits, and its bars' fill): buying the Elixir at Eolis's tool shop, every frame of the count-down shows the gold the game had a frame before, every amount from 700 down to 380, and every frame's band is the game's own picture's; the King's gift counts 0 up to 3000 the same way; at Eolis's Guru the bars read 32 HP and 5 MP, and 80 and 80 during his first line. Without the fix all three fail. A probe through the shop's opening, a Start pause and the Select menu found the band equal to the game's own on every frame |
| Open seams (#166, slice 1) | `tests/seams/open_ground.py`, in the suite, a part each: unseen Zozuras in two rooms stand still 300 frames; one wakes on the first read within 16 pixels of the view and keeps coming once out of it; Eolis's Zozuras follow him into `$05` and back through `$04` into `$03`; the town's walking woman walks into his room, the walking man in and out; a fireball flies through a seam and goes once out of the view; a room of 5 keeps its seam a wall; a straddling Zozura's pixels on both sides of the seam; a respawn's load brings room 2 back fresh; the intro's trigger by a neighbour's edge leaves it ticking; a save mid-crossing replays alike in two fresh games |
| Fights across a seam (#166, slice 2) | `tests/seams/fights.py`, in the suite, a part each: from `$04` the Long Sword kills `$05`'s Zozura (its experience and MP his, its death and its bread in `$05`); a Monodron's coin drops in `$05` and its sound plays; he takes a coin across the seam without crossing; a Zozura straddling the seam hurts him; a punch reaches across it; a Sugata in the Mist's `$1F` flashes him on `$1E` in its reach, and not out of it; a save mid-swing replays alike in two fresh games; a frame that ends inside the pass on Zenis 16 reads as the room's own and replays alike. `tests/world/boss_arenas.py` checks that no move or visit comes from an arena |
| One above the other (#166, slice 5) | `tests/seams/vertical.py`, in the suite, a part each: a Hornet in Trunk `$2D`'s ladder shaft follows him down `$29`'s ladder, through `$2D`'s bottom and into `$29` at its Y less 208; one straddling that seam is drawn whole, its pixels above and below it; on the Mist's `$10`, at the top of a jump, the Long Sword kills a Hornet of `$07` above (at its y 203) in `$07`, its experience his, and leaves one at `$07`'s y 180 alone; a dormant Hornet of `$07` stands still until the frame the view comes within 16 pixels of it, from Seamless's trace, and wakes on that frame; a Hornet flying up out of `$10` moves into `$07` at its Y plus 208; a Zozura falls from Trunk `$3C` into `$37` and lands on its floor; a save mid-crossing replays alike in two fresh games. `tests/seams/seams_test.c` checks every test past a top or bottom against the blocks of the joined world |
| The view shows only rooms (#166, slice 4) | `tests/world/camera_test.c`, in the suite, lays out every area from every room as `topology()` does (54 maps), and walks the hero a pixel at a time along every row at every height, with and without an opening above, and through every seam one above the other at each of its 16 block columns: his body always in the view; standing in a room with a room beside it, the view over no place without a room; across a seam, black only where no view over both rows' rooms shows him; the view moving at most 1 pixel for each of his walking, 4 under an opening, 6 climbing and 7 across a seam whose rows share one column; and with no empty place the old camera's view. `tests/world/only_rooms.py`, in the suite, headless, every frame from the trace against `fax_world_room`'s places: the Tower of Trunk from `$10` to `$12` and back (the old camera black from the first frame, the new one never), down `$10`'s ladder into `$0F` (black only on the 42 frames his body is across the seam), and the Tower of Suffer from `$37`'s ledge past the empty corner over `$39` (the view reaching up at the start and down to his row before the corner); at most 2, 7 and 5 pixels a frame, and never without him moving |
| Arrivals (#204) | `tests/world/arrivals.py`, in the suite, headless, every frame of each load from a few frames before it, the presented picture against the game's own: where the game's playfield lies in the view. On each frame the game still shows the room a load leaves, that room is where it was; from the first frame the new room shows, as long as the hero stands, it is at one place, the trace's for it, through Seamless's own frames after. A part each: Eolis's way out into the Trunk with the J key's line (shown 8 frames before Seamless composed it in play, the room at the view's left edge), left out of Apolune by the town's edge onto the Trunk's `$07` (the old room's last frame), through the Tower of Trunk's door into `$0D` (13), dead on `$0E` and back at that door by zone respawn (13), and out of Eolis's meat shop (taken in play by its first frame). Without the fix the first four fail: the new room 84 pixels right, or the old room 84 right on the town's first frame. Also checked by hand the same way: the Trunk's door into the Mist (15 frames, the room at x 168) and the Mist's into the Branch (9), the Mist's door back, the Trunk's climb both ways, a death into a temple and out of it, Guru travel's arrival and a new game's first screen, all where they stay from their first frame |
| Shared CIRAM API | Invalid bounds/NULL and isolated upload refused; upload/restore succeeds; serialized complete machine unchanged after the probe |
| Catalog / prepared helper | Feature activation without CLI override; hidden helper loads and pauses all prepared states; owner configuration restored to normal audio |

Assistance supplied player positions, health/protection, original screen-loader
entries and the explicitly granted J key. It did not set door success, quest
completion, enemy HP or victory flags. Initial failed Dartmoor/Zenis probes
had already fallen out of their source rooms during setup; corrected entrances
were checked against baseline movement and retained separately. The initial
invalid fixtures are not accepted coverage. Branch's return probe naturally
falls onto a lower platform after crossing; equal final Y is not required.

Component timings are exposed through TCP and retained in the receipts. They
are simulation/render/upload costs, not total host frame-time measurements.
Normal views run a bounded halo; wide/map views activate more rooms and cost
more. Initial region decoding and native portal delays remain separate costs.

Still open (#110 made Seamless the remaster's only view, so these are the
risks a playtest must cover):

- **Seams** (#166; the plan, toward one big world, is
  [SEAMLESS-COMBAT.md](SEAMLESS-COMBAT.md)). Since slice 1 monsters, shots
  and walking people go through side-by-side seams, dormant until first
  seen, and are drawn whole across them; since slice 2 they fight him across
  side-by-side seams, and since slice 3 his spell flies on through them and
  ends at the view's edge; since slice 5 all of it one above the other but
  the spell, which still ends at a seam above or below. Encounters change
  most: the Mist's Hornets, once seen, drift between its rows of open sky
  over him; a monster he has seen follows
  him room after room, Eolis's Zozuras into its town, and towns are busier;
  that wants a playtest. Fixed by slice 0 (Seams lose nothing, #177): the
  live game's ghost hit at the far side of his own room, the drops a copy
  took at its edge, and the rooms an item or a person there froze.
- **Black where no room is** (fixed by #166's slice 4 but where it can't
  be helped): while he straddles one of the 25 seams one above the other
  whose rows share only his column (Trunk `$10` over `$0F`, `$13` over `$12`,
  and in the Mist's towers, Branch, Dartmoor and Zenis), and beside the 12
  shafts and 18 rooms that have no room beside them in their row, as beside
  an arena. The camera's feel (where it eases up and down, and sideways near
  a ladder) wants a playtest.
- **Secrets and breakable terrain** (the Mattock's walls, the path to
  Mascon's fountain): the suite covers the fountain's ladder (the ladder
  fixes' test), breaking Trunk `$0C`'s Mattock wall in its own room (the keys
  test) and the wall's block drawn in a neighbour
  (`tests/graphics/graphics_fixes.py`). No Mattock wall is at a seam: both
  breakable blocks (`$63`) are in column 3. A broken one stays broken while
  the hero stays in the area, since the room's record keeps its
  `ScreenBuffer`; the original brings it back at every load. After an area
  load with the path to Mascon open, Trunk `$28` seen from `$27` shows the
  fountain's stone and no ladder until he crosses, when the stone clears and
  the ladder drops.
- **One-in-four items** (the Mattock, Red Potion, Poison, Glove and Ointment
  of GAME_INTERNALS.md's *Items in the world*) in a room first met as a
  neighbour are decided in its copy, against a count no seam moves, so they
  show only when it happens to be 3: Trunk `$0C`'s Red Potion was gone with
  the count at 0 and kept at 3.
- **Death and retry**: zone respawn's test dies and comes back in rooms Seamless
  joined (Eolis, a screen into the Tower of Trunk); not every region. A load
  (a door within the area, a zone respawn) spawns its own room fresh and,
  since slice 1, every other room of the area too, as each comes near. A
  hero who dies straddling a seam is cut at it while he dissolves: the death
  shows the game's own picture over his room.
- **The ending**: `tests/world/boss_arenas.py` kills the final boss and checks
  the ending reaches the King's room; not the credits.
- Complete progression and mantra audit; full-map costs and other hosts. No
  full playthrough, netplay, legacy-host or non-USA claim is made.

## Reproduction and prepared playtest

Build with the pinned engine and normal `tools/build.ps1` pipeline. The engine
includes this preview's bounded CIRAM upload. SDL-free builds retain a portable timing fallback
and omit SDL/TCP event integration; the standalone SDL-free object compile
also passes. Only Windows x64 has been built for this preview so far.

```powershell
python tests/world/integration.py --exe <FaxanaduRecomp.exe> --rom <USA-ROM> `
  --state <current-build-supported-right-exit.cycstate> --out <QA-directory>
python tests/world/dialogue.py --exe <FaxanaduRecomp.exe> --rom <USA-ROM> `
  --npc-state <stock-Up-contact.cycstate> --door-state <stock-locked-door.cycstate> `
  --out <dialogue-QA-directory>
```

The fixture must place the native player at x=240 on a supported right exit.
Prepared outdoor fixtures must enter through `Game_SetupAndLoadOutsideArea`
or explicitly supply the native destination palette when using the horizontal
area-change loader. The latter retains its caller's palette argument; using it
alone can produce mechanically valid geometry with incorrect colors. Earlier
preview states had that preparation defect and are superseded.
The test records source/binary identities, executes native/interpreter routes,
checks every logged scroll mode and compares current-build replay.
The dialogue test requires Pillow and visible, nonblinking player fixtures.
Its original/candidate and interpreter comparisons cover the actual dialogue
input, pixel stability, opening pose, closing, palette isolation and restore.
An existing clipped-player scratch draw failure at `$B982` occurs once in both
the preceding and current builds with the x=3 NPC fixture; it is retained in
the raw logs and is not evidence of a new regression or a fully audited edge
renderer. The normal seam regression reports no failures.

Developer CLI: `--world-view off|seamless|atlas` overrides the remaster's
choice for a run (the suite never uses it: it plays the remaster as players
get it). There is no size option: tests play the 424x240 view a player sees. TCP: `fax_world` (its `size` is the
canvas's),
`fax_world_room` and `fax_world_probe`. `NESRECOMP_WORLD_TRACE=<path>` explicitly
enables the per-frame CSV, a line after each frame: the live room and hero,
the camera the picture has with the hero it is worked out from, the view
actors wake in, where the room's top-left sits in the picture (`origin_x`,
`origin_y`) and whether it is still arriving (the game's own picture there,
#204); ordinary launches create no world journal.

The task's `preview/Open-preview.ps1` starts a normal window, normal audio and
a localhost checklist. The first setup is paused. Tests can be switched and
resumed without an agent, and individual Pass/Fail/Deferred notes persist in
`observations.json`. The helper disables native saves and uses its own profile.
The owner accepted checkpoint `2cc7cb4` as passing a preliminary playtest on
2026-10-27 and authorized its experimental release. Earlier individual failures
and subsequent candidate evidence remain separate; this is not a full gameplay
or progression qualification.
