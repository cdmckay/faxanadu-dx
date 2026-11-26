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
is no zoom and no option. The camera follows the hero and clamps to the
connected region's edges, centring a region narrower than the view. The
inventory, text boxes and the original's own pictures (death, scripts, region
loads) sit where the native picture would, centred. The status bar band, its
top 32 lines, is the one exception (#113, #117): it and the Elixir left of its
`M:` and `P:` bars (`fax_charges.c`) are centred together, so the band's
columns start at x 92, 8 pixels right of the room's, and the Elixir is at x
76-91 (`fax_world_hud_x`).
The host scales the view to the window, keeping its shape, as its Display
settings say: with IntegerScale (the default), the largest whole multiple
that fits, so 3x, 6x and 9x fill 720p, 1440p and 4K, and 1080p shows 4x,
1696x960, with bars. #107's sharp 4.5x at 1080p (nearest-neighbour up to 5x,
then a smooth scale down) needs a hook in the host, which has none for it.
F4 toggles a connected-map overview, capped at the preview's 4096x2048
presentation limit. Empty space outside the connected geometry remains
empty; terrain is not stretched or repeated to fill it.

Doors, region changes and connections that cannot be embedded consistently
retain native portal transitions.

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
| Fixed view (#107) | `tests/world/fixed_view.py`, in the suite: the canvas and picture are 424x240 at 424x240, 1920x1080 and 300x600 windows; at 1920x1080 the window shows them at 4x (1696x960) with black around; F4's overview grows to the region and F4 again restores 424x240. The adaptive Fit it replaced was checked at 1536x720, 600x1200 and 3840x720 |
| Boss arenas (#110) | `tests/world/boss_arenas.py`, in the suite, a part per boss's screen: beside it, it is on no map; in by the original's scroll (or a door, for walled King Grieve and the final boss), the room alone with the boss handler running; the boss's slot emptied, the handler ends and the room joins its neighbour; out again by a handoff. The final boss's kill begins the ending, which reaches the King's room |
| Live status bar (#132) | `tests/world/live_status_bar.py`, in the suite, read off the presented picture's pixels (the band's gold digits, and its bars' fill): buying the Elixir at Eolis's tool shop, every frame of the count-down shows the gold the game had a frame before, every amount from 700 down to 380, and every frame's band is the game's own picture's; the King's gift counts 0 up to 3000 the same way; at Eolis's Guru the bars read 32 HP and 5 MP, and 80 and 80 during his first line. Without the fix all three fail. A probe through the shop's opening, a Start pause and the Select menu found the band equal to the game's own on every frame |
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
  [SEAMLESS-COMBAT.md](SEAMLESS-COMBAT.md)). A seam is still an invisible
  wall. Enemies, shots and people do not move between rooms. A neighbouring
  room's actors run on an isolated machine against a projected hero, clamped
  to that room's edge, so its monsters gather at the seam he is nearest and
  its walking people wait there (Eolis `$02`'s walking woman, in the
  `eolis-town` fixture); a shot vanishes at it, and a sprite drawn past it is
  cut off. Monsters can't hurt him across it, and his swing can't hit them:
  the copy sets his attack phase to 0, so a hit never lands in it. A spell
  ends at the seam, and one in flight vanishes when he crosses. A
  neighbouring room's sounds are lost with its copy. This changes encounter
  timing compared with stock. Fixed by the plan's slice 0 (Seams lose
  nothing, #177): the live game's ghost hit at the far side of his own room,
  the drops a copy took at its edge, and the rooms an item or a person there
  froze.
- **Black where no room is.** The camera clamps to the box around the rooms
  joined to his, so where that box holds no room the view is black, a block
  with straight edges (out of the Tower of Trunk's door onto `$10`: the top
  left). 142 such places lie beside a room, in Trunk, the Mist, Branch,
  Dartmoor and Zenis.
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
  (a door within the area, a zone respawn) spawns its own room fresh but
  leaves the rooms beside it as he left them. A hero who dies straddling a
  seam is cut at it while he dissolves: the death shows the game's own
  picture over his room.
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
enables the per-frame CSV; ordinary launches create no world journal.

The task's `preview/Open-preview.ps1` starts a normal window, normal audio and
a localhost checklist. The first setup is paused. Tests can be switched and
resumed without an agent, and individual Pass/Fail/Deferred notes persist in
`observations.json`. The helper disables native saves and uses its own profile.
The owner accepted checkpoint `2cc7cb4` as passing a preliminary playtest on
2026-10-27 and authorized its experimental release. Earlier individual failures
and subsequent candidate evidence remain separate; this is not a full gameplay
or progression qualification.
