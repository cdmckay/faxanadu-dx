"""Play What a neighbouring room sees of him (#166's slice 6) end to end,
headless.

    python3 tests/seams/sees_him.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/sees-him

Every part plays headless from a save state, frame exact however loaded the
machine is: each frame's CPU RAM (the live room's slots), states saved after
chosen frames (the neighbours' records) and Seamless's per-frame trace
(NESRECOMP_WORLD_TRACE), as tests/seams/vertical.py does. Nothing is
pressed: what a neighbour's actor does is all the check.

The parts start from fixtures made out of Eolis's meat shop, its door edited
to lead onto a screen (tests/block/harness.py's warp(), an external
prerequisite, not coverage), under Ointment (DurationOintment below $80: no
touch gets through): seams-mist-51 (the Mist's $51, on its floor at x 96,
$50 left of it and $52 right, an Ishiisu on each floor and a Fire Giant on
each upper floor), seams-mist-2b (the Mist's $2B, $2A left of it with Nash,
$2C right), seams-branch-21 (the Branch's $21, $20 left of it with a
Magman, $22 above it with another) and seams-zenis-0f (the fortress of
Zenis's $0F, $05 left of it with a Zorugeriru by the seam). Each edits its
save: the live game's
RAM, and rooms in Seamless's record (faxanadu.world, tests/seams/open_ground.py's
Record: each room's RAM, and Seams' block, its first byte a wake bit per
slot, then a mark per slot, 1 through the left side, 2 the right). The rooms'
other monsters are taken out, so only the one checked acts.

1. ishiisu: $50's Ishiisu at its x 226, awake, by the seam, the hero on
   $51 at x 40 facing away from it, 70 pixels from it. The hero a copy
   holds is at the seam, 29 pixels from it, and there it would stand and
   wait; it walks on through the seam into $51 (Seams' move $50 to $51), its
   X up on the way, and stands still only once truly within 32 pixels of
   him, where it waits while he faces away.
2. fire-giant: $50's Fire Giant on the upper floor at its x 220, awake. With
   the hero on $51's floor at x 60 (truly 80 pixels from its right, the
   copy's held hero 19) it waits, standing still for 120 frames; at x 8
   (truly 28) it charges, its script's distance test (< 32) met.
3. nash: Nash of $2A, awake, the hero on $2B's ledge at (196, 96) facing
   right (so the view shows the room's top, where Nash floats). When its
   countdown ends it reappears beside him, in his room: in $2B (the live
   game's) at his X less 32 (behind him), its own height kept, not by the
   seam in $2A where the copy's held hero would put it (x 223); on that
   frame $2A holds it no more, and none of $2A's shadow OAM is its.
4. magman: the Magman of $20, awake, the hero on $21 at x 96 facing left: it
   appears in $21, 48 pixels in front of him at his height (48, 144), not in
   $20 by the seam (x 207) or wrapped round it.
5. magman-above: the Magman of $22, above $21, the same: in $21 at (48,
   144), not at the bottom of $22 (the copy's held Y, 239).
6. magman-live: a Magman in $21 itself, the hero at its x 20 facing left: in
   front of him is past $21's left side, which the game's X wraps round to
   x 228 of $21; instead it goes into $20 at its x 228 (marked through the
   side, then moved there between frames).
7. zorugeriru: the Zorugeriru of Zenis $05, awake, at its x 224 by the
   seam, the hero on $0F at x 96: its minion drops in $0F at his X, at the
   top (y 32), not at $05's x 255 where the copy holds him; $05 keeps none.
8. save-load: saved by the game 20 frames before Nash's countdown ends: two
   fresh games, headless, run 60 frames from it to the same machine and
   records, Nash beside him in $2B.

Screenshots: Nash beside him in $2B, the Magman in front of him in $21, and
the Ishiisu waiting by him in $51 (the window's picture, Seamless's 424 view,
headless: --present-out).
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "block"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "movement"))
import fax_e2e  # noqa: E402
from fax_e2e import fixture, parse_args  # noqa: E402
import harness  # noqa: E402  (warp(): out of a building onto any screen)
import play  # noqa: E402  (headless runs and mod catalogs)
from seams_lose_nothing import hw_ram  # noqa: E402
from open_ground import Record  # noqa: E402

MIST, BRANCH, REGION_MIST, REGION_BRANCH, PALETTE_MIST, PALETTE_BRANCH = 2, 5, 2, 3, 0x0A, 0x08
ZENIS, REGION_ZENIS, PALETTE_ZENIS = 7, 5, 0x0F
SCREEN, HERO_X, HERO_Y, PLAYER_FLAGS = 0x63, 0x9E, 0xA1, 0xA4    # Area_CurrentScreen, Player_PosX_Block, _PosY, _Flags
SPRITE_X, SPRITE_Y = 0xBA, 0xC2                                  # CurrentSprites_XPos_Full, _YPos
ENTITIES, PHASES = 0x2CC, 0x2E4                                  # CurrentSprites_*
SLOT_ARRAYS = [SPRITE_X, SPRITE_Y] + list(range(0x2BC, 0x374, 8))
FACING_RIGHT = 0x40                                              # Player_Flags
NASH, ISHIISU, FIRE_GIANT, MAGMAN, ZORUGERIRU, EMPTY = 0x0D, 0x0F, 0x0E, 0x23, 0x31, 0xFF
OWNER = 26                       # Seams' block: the wake, the cells' count, marks, tiles, drawn tiles, then an owner an OAM entry
PARTS = ("ishiisu", "fire-giant", "nash", "magman", "magman-above", "magman-live", "zorugeriru", "save-load")
NEAR = 32                        # the Ishiisu waits for him within 32 pixels (its CMP #$20)
NASH_HERO = (196, 96)            # on $2B's ledge (row 8, x 192-223): the view shows the room's top, Nash's height
MINION = 0x03                    # a Zorugeriru's dropping minion (SpriteBehavior_Garbled3)


class Game(harness.Game, play.Game):
    """harness.Game's warp(), and play.Game's headless runs and mod catalogs."""


@fax_e2e.recipe("seams-mist-51", parent="eolis-meat-shop")
def _mist_51(g):
    """Out of Eolis's meat shop onto the Mist's $51, its door edited to lead
    there, at block $96: on its floor, the Ishiisus' floor."""
    g.warp(g.save("shop"), "mist-51", MIST, 0x51, PALETTE_MIST, 0x96, REGION_MIST, {0x427: 0x7F})
    g.wait(20)


@fax_e2e.recipe("seams-mist-2b", parent="eolis-meat-shop")
def _mist_2b(g):
    """The same onto the Mist's $2B at block $96, right of $2A, Nash's room."""
    g.warp(g.save("shop"), "mist-2b", MIST, 0x2B, PALETTE_MIST, 0x96, REGION_MIST, {0x427: 0x7F})
    g.wait(20)


@fax_e2e.recipe("seams-branch-21", parent="eolis-meat-shop")
def _branch_21(g):
    """The same onto the Branch's $21 at block $96, right of $20 and under
    $22, each with a Magman."""
    g.warp(g.save("shop"), "branch-21", BRANCH, 0x21, PALETTE_BRANCH, 0x96, REGION_BRANCH, {0x427: 0x7F})
    g.wait(20)


@fax_e2e.recipe("seams-zenis-0f", parent="eolis-meat-shop")
def _zenis_0f(g):
    """The same onto the fortress of Zenis's $0F at block $96, right of $05,
    where a Zorugeriru hangs by the seam."""
    g.warp(g.save("shop"), "zenis-0f", ZENIS, 0x0F, PALETTE_ZENIS, 0x96, REGION_ZENIS, {0x427: 0x7F})
    g.wait(20)


def slots(ram, entity):
    return [k for k in range(8) if ram[ENTITIES + k] == entity]


def main():
    args = parse_args(__doc__, parts=PARTS)
    game = Game.from_args(args)
    out = game.out

    def wanted(*names):
        return any(args.wants(n) for n in names)

    try:
        mist51 = game.state("seams-mist-51") if wanted("ishiisu", "fire-giant") else None
        mist2b = game.state("seams-mist-2b") if wanted("nash", "save-load") else None
        branch21 = game.state("seams-branch-21") if wanted("magman", "magman-above", "magman-live") else None
        zenis0f = game.state("seams-zenis-0f") if wanted("zorugeriru") else None
        mods = game.mods("mods-on")

        def edited(case, source, writes, edits):
            path = out / f"{case}.cycstate"
            fixture(source, path, writes, {"faxanadu.world": edits})
            return path

        def clear(rec, edits, room, keep=()):
            """Room `room`'s actors out of its record, but slots `keep`."""
            edits.update({rec.base(room) + ENTITIES + k: EMPTY for k in range(8) if k not in keep})

        def block(rec, data, room):
            return data[rec.start + rec.block(room):rec.start + rec.block(room) + 512]

        # ---- the Mist's $50-$52: the Ishiisu and the Fire Giant ----
        def mist(hero_x, keep50):
            """seams-mist-51's edits: every monster of $50, $51 and $52 out but
            $50's `keep50` (entity: (x, its slot found here)), awake there, the
            hero on $51's floor at hero_x facing right. Returns (writes,
            edits, {entity: slot})."""
            data = mist51.read_bytes()
            live, rec = hw_ram(data), Record(data)
            assert live[SCREEN] == 0x51, hex(live[SCREEN])
            r50, edits, kept = rec.ram(0x50), {}, {}
            for entity, x in keep50.items():
                s = slots(r50, entity)
                assert s, (hex(entity), list(r50[ENTITIES:ENTITIES + 8]))
                kept[entity] = s[0]
                edits[rec.base(0x50) + SPRITE_X + s[0]] = x
            clear(rec, edits, 0x50, kept.values())
            clear(rec, edits, 0x52)
            edits[rec.block(0x50)] = sum(1 << s for s in kept.values())
            writes = {ENTITIES + k: EMPTY for k in range(8)}
            writes.update({HERO_X: hero_x, PLAYER_FLAGS: FACING_RIGHT})
            return writes, edits, kept

        if args.wants("ishiisu"):
            writes, edits, kept = mist(40, {ISHIISU: 226})
            s = kept[ISHIISU]
            path = edited("ishiisu", mist51, writes, edits)
            frames = 200
            saves = [(f, out / f"ishiisu-{f:03d}.cycstate") for f in (0, 10, 20, 30)]
            rams, _ = game.play(path, [], frames, mods, saves=saves)
            xs = []
            for f, p in saves:
                r = Record(p.read_bytes()).ram(0x50)
                if r[ENTITIES + s] == ISHIISU:
                    xs.append(r[SPRITE_X + s])
                p.unlink()
            assert len(xs) >= 3 and all(a < b for a, b in zip(xs, xs[1:])), ("in $50 it walks on to the seam", xs)
            assert 226 - 255 + NEAR > 0, "the copy's held hero (x 255 there) is within its 32 pixels at the start"
            hero = [r[HERO_X] for r in rams]
            assert all(r[SCREEN] == 0x51 and h == 40 for r, h in zip(rams, hero)), "he stands on $51 at x 40"
            here = [next(iter(slots(r, ISHIISU)), None) for r in rams]
            first = next(f for f, k in enumerate(here) if k is not None)
            k = here[first]
            path_x = [rams[f][SPRITE_X + k] if here[f] == k else None for f in range(first, frames)]
            assert all(x is not None for x in path_x), "once in $51 it stays there"
            # Where it first stands still (its X the same for 8 frames), it is
            # within 32 pixels of him, and not one frame before: it moves on
            # every 4 frames (0.75 pixels a frame) until then.
            still = next(i for i in range(len(path_x) - 8) if len(set(path_x[i:i + 8])) == 1)
            x_wraps = [x if x < 128 else x - 256 for x in path_x]   # its first frames straddle $51's left side
            gaps = [40 - x for x in x_wraps]
            assert gaps[still] < NEAR and all(g >= NEAR for g in gaps[:max(still - 2, 0)]), (still, gaps[:still + 2])
            assert all(len(set(x_wraps[i:i + 4])) > 1 for i in range(0, max(still - 4, 0))), "it walks until then"
            assert len(set(path_x[still:])) == 1, "and waits there while he faces away"
            png = out / f"{len(game.shots):02d}-ishiisu-waits.png"
            game.play(path, [], first + still + 30, mods, picture=png)
            game.shots.append(png.name)
            print(f"ok: $50's Ishiisu by the seam (its x 226, 29 pixels from the hero the copy holds at the seam) walks "
                  f"on ($50 x {xs}), into $51 on frame {first}, and stands still only at $51's x {path_x[still]}, "
                  f"{gaps[still]} pixels from him (truly within 32), waiting there for {len(path_x) - still} frames")

        if args.wants("fire-giant"):
            def charge(case, hero_x, frames):
                writes, edits, kept = mist(hero_x, {FIRE_GIANT: 220})
                s = kept[FIRE_GIANT]
                path = edited(case, mist51, writes, edits)
                saves = [(f, out / f"{case}-{f:03d}.cycstate") for f in (0, frames // 2, frames - 1)]
                game.play(path, [], frames, mods, saves=saves)
                xs = []
                for _, p in saves:
                    r = Record(p.read_bytes()).ram(0x50)
                    xs.append(r[SPRITE_X + s] if r[ENTITIES + s] == FIRE_GIANT else None)
                    p.unlink()
                return xs
            xs = charge("fire-giant-far", 60, 120)
            assert xs == [220] * 3, ("80 pixels from him truly, it waits", xs)
            near = charge("fire-giant-near", 8, 60)
            assert near[0] == 220 and near[-1] != 220, ("28 pixels from him, it charges (on, or into $51)", near)
            near = ["in $51" if x is None else x for x in near]
            print(f"ok: $50's Fire Giant at x 220 waits while he is on $51 at x 60 (80 pixels truly, 19 from the "
                  f"copy's held hero): x {xs}; with him at x 8 (28) it charges: x {near}")

        # ---- Nash, the Mist's $2A beside $2B ----
        def nash_edits():
            data = mist2b.read_bytes()
            live, rec = hw_ram(data), Record(data)
            assert live[SCREEN] == 0x2B, hex(live[SCREEN])
            s = slots(rec.ram(0x2A), NASH)
            assert s, list(rec.ram(0x2A)[ENTITIES:ENTITIES + 8])
            edits = {}
            clear(rec, edits, 0x2A, s)
            clear(rec, edits, 0x2C)
            edits[rec.block(0x2A)] = 1 << s[0]
            writes = {ENTITIES + k: EMPTY for k in range(8)}
            writes.update({HERO_X: NASH_HERO[0], HERO_Y: NASH_HERO[1], PLAYER_FLAGS: FACING_RIGHT})
            return writes, edits, s[0], rec.ram(0x2A)[SPRITE_Y + s[0]]

        def nash_run(path, frames):
            """The frame Nash first is in the live game, and the frames' RAM."""
            rams, _ = game.play(path, [], frames, mods)
            seen = [f for f, r in enumerate(rams) if slots(r, NASH)]
            return (seen[0] if seen else None), rams

        if wanted("nash", "save-load"):
            writes, edits, s, nash_y = nash_edits()
            nash_path = edited("nash", mist2b, writes, edits)
            f, rams = nash_run(nash_path, 420)
            assert f is not None, "Nash never came into $2B"

        if args.wants("nash"):
            k = slots(rams[f], NASH)[0]
            x, y = rams[f][SPRITE_X + k], rams[f][SPRITE_Y + k]
            assert (x, y) == (NASH_HERO[0] - 32, nash_y), ("behind him, 32 pixels, at its own height", x, y, nash_y)
            assert all(r[SCREEN] == 0x2B for r in rams), "he is in $2B throughout"
            saves = [(f - 1, out / "nash-before.cycstate"), (f, out / "nash-at.cycstate")]
            game.play(nash_path, [], f + 1, mods, saves=saves)
            before, at = (Record(p.read_bytes()) for _, p in saves)
            assert before.ram(0x2A)[ENTITIES + s] == NASH and NASH not in at.ram(0x2A)[ENTITIES:ENTITIES + 8], \
                "it left $2A between frames"
            owned = block(at, at.data, 0x2A)[OWNER:OWNER + 64]
            assert s + 1 not in owned, ("none of $2A's OAM is Nash's that frame", list(owned))
            # It shows itself in phase 2, faces him and throws its shot: from his room.
            shown = next(g for g in range(f, len(rams)) if rams[g][PHASES + k] == 2)
            png = out / f"{len(game.shots):02d}-nash-beside-him.png"
            game.play(nash_path, [], shown + 20, mods, picture=png)
            game.shots.append(png.name)
            print(f"ok: Nash of $2A reappears beside him, in his room: in $2B's slot {k} on frame {f}, at ({x}, {y}), "
                  f"32 pixels behind him at its own height (the copy's held hero would have put it at $2A's x 223), "
                  f"none of it drawn in $2A that frame; it shows itself on frame {shown}")

        if args.wants("save-load"):
            mid = out / "nash-mid.cycstate"
            game.play(nash_path, [], f - 19, mods, saves=[(f - 20, mid)])
            after = []
            for i in range(2):
                p = out / f"after-{i}.cycstate"
                game.play(mid, [], 61, mods, saves=[(60, p)])
                after.append(p.read_bytes())
            a, b = after
            ra, rb = Record(a), Record(b)
            assert hw_ram(a) == hw_ram(b), "the machine"
            for n in (0x2A, 0x2B, 0x2C):
                assert ra.ram(n) == rb.ram(n), f"room {n:#x}'s record"
                assert block(ra, a, n) == block(rb, b, n), f"room {n:#x}'s block"
            live = hw_ram(a)
            assert slots(live, NASH) and NASH not in ra.ram(0x2A)[ENTITIES:ENTITIES + 8], "Nash in $2B, the live game's"
            print("ok: saved 20 frames before Nash's countdown ends, two fresh games run 60 frames from it to the same "
                  "machine and records, Nash beside him in $2B")

        # ---- Magman, the Branch's $20, $21 and $22 ----
        def branch(keep, hero_x=96, hero_y=None, facing=0):
            """seams-branch-21's edits: every monster of $20, $21, $22 and $1E
            out but `keep` ((room, slot) awake), the hero on $21 at hero_x
            facing `facing`. Returns (writes, edits, rec, live)."""
            data = branch21.read_bytes()
            live, rec = hw_ram(data), Record(data)
            assert live[SCREEN] == 0x21, hex(live[SCREEN])
            edits = {}
            for n in (0x20, 0x22, 0x1E):
                clear(rec, edits, n, [s for r, s in keep if r == n])
                edits[rec.block(n)] = sum(1 << s for r, s in keep if r == n)
            writes = {ENTITIES + k: EMPTY for k in range(8)}
            writes.update({HERO_X: hero_x, PLAYER_FLAGS: facing})
            if hero_y is not None:
                writes[HERO_Y] = hero_y
            return writes, edits, rec, live

        def magman_of(room):
            s = slots(Record(branch21.read_bytes()).ram(room), MAGMAN)
            assert s, f"a Magman in {room:#x}"
            return s[0]

        def appears(case, writes, edits, frames=300):
            """Run; the first frame a Magman is in the live game, its place, and
            the frames' RAM."""
            path = edited(case, branch21, writes, edits)
            rams, _ = game.play(path, [], frames, mods)
            seen = [g for g, r in enumerate(rams) if slots(r, MAGMAN)]
            assert seen, f"{case}: no Magman ever in $21"
            r = rams[seen[0]]
            k = slots(r, MAGMAN)[0]
            assert all(q[SCREEN] == 0x21 for q in rams), "he is in $21 throughout"
            return path, seen[0], (r[SPRITE_X + k], r[SPRITE_Y + k]), rams

        if args.wants("magman"):
            s = magman_of(0x20)
            writes, edits, _, live = branch([(0x20, s)])
            path, f, place, rams = appears("magman", writes, edits)
            assert place == (96 - 48, live[HERO_Y]), ("48 pixels in front of him, at his height", place)
            png = out / f"{len(game.shots):02d}-magman-in-front.png"
            game.play(path, [], f + 10, mods, picture=png)
            game.shots.append(png.name)
            print(f"ok: the Magman of $20 appears in $21 on frame {f}, at {place}: 48 pixels in front of him at his "
                  f"height (the copy's held hero would have put it at $20's x 207)")

        if args.wants("magman-above"):
            s = magman_of(0x22)
            writes, edits, _, live = branch([(0x22, s)])
            _, f, place, _ = appears("magman-above", writes, edits)
            assert place == (96 - 48, live[HERO_Y]), ("in front of him at his height, in his room", place)
            print(f"ok: the Magman of $22, above $21, appears in $21 on frame {f}, at {place} (the copy holds him at "
                  f"$22's y 239)")

        if args.wants("magman-live"):
            data = branch21.read_bytes()
            rec, s20 = Record(data), magman_of(0x20)
            r20 = rec.ram(0x20)
            writes, edits, _, _ = branch([], hero_x=20, hero_y=112, facing=0)
            for a in SLOT_ARRAYS:                                        # $20's Magman, in $21's slot 5
                writes[a + 5] = r20[a + s20]
            edits[rec.block(0x21)] = 1 << 5                              # awake: slot 5 of the live room
            path = edited("magman-live", branch21, writes, edits)
            frames = 300
            rams, _ = game.play(path, [], frames, mods)
            gone = [g for g, r in enumerate(rams) if MAGMAN not in r[ENTITIES:ENTITIES + 8]]
            assert gone and all(r[SCREEN] == 0x21 and r[HERO_X] == 20 for r in rams), "it left $21; he stands"
            g = gone[0]
            assert rams[g - 1][PHASES + 5] % 2 == 0, "hidden (an even phase) in $21 until then"
            at = out / "magman-live-at.cycstate"
            game.play(path, [], g + 1, mods, saves=[(g, at)])
            r20 = Record(at.read_bytes()).ram(0x20)
            k = slots(r20, MAGMAN)
            assert k and (r20[SPRITE_X + k[0]], r20[SPRITE_Y + k[0]]) == (228, 112) and r20[PHASES + k[0]] % 2, \
                ("in $20 at its x 228, shown (an odd phase)", k and (r20[SPRITE_X + k[0]], r20[SPRITE_Y + k[0]]))
            print(f"ok: a Magman of $21, him at x 20 facing left: 48 pixels in front of him is past $21's left side, "
                  f"where the game's X wraps round to $21's x 228; it shows itself in $20 at its x 228 instead, "
                  f"on frame {g} (put past the side, marked, and moved between frames)")
        if args.wants("zorugeriru"):
            data = zenis0f.read_bytes()
            live, rec = hw_ram(data), Record(data)
            assert live[SCREEN] == 0x0F, hex(live[SCREEN])
            z = slots(rec.ram(0x05), ZORUGERIRU)
            assert z, list(rec.ram(0x05)[ENTITIES:ENTITIES + 8])
            edits = {}
            clear(rec, edits, 0x05, z)
            for n in (0x10, 0x0A):
                clear(rec, edits, n)
            edits[rec.block(0x05)] = 1 << z[0]
            writes = {ENTITIES + k: EMPTY for k in range(8)}
            writes.update({HERO_X: 96})
            path = edited("zorugeriru", zenis0f, writes, edits)
            rams, _ = game.play(path, [], 200, mods)
            seen = [g for g, r in enumerate(rams) if slots(r, MINION)]
            assert seen and all(r[SCREEN] == 0x0F and r[HERO_X] == 96 for r in rams), "a minion in $0F; he stands"
            f = seen[0]
            k = slots(rams[f], MINION)[0]
            x, y = rams[f][SPRITE_X + k], rams[f][SPRITE_Y + k]
            assert x == 96 and 32 <= y < 48, ("dropped at his X from the top", x, y)
            at = out / "zorugeriru-at.cycstate"
            game.play(path, [], f + 1, mods, saves=[(f, at)])
            assert MINION not in Record(at.read_bytes()).ram(0x05)[ENTITIES:ENTITIES + 8], "none left in $05"
            print(f"ok: the Zorugeriru of $05 drops its minion on him in $0F: on frame {f} at ({x}, {y}), his X, "
                  f"not at $05's x 255 by the seam where the copy holds him")
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
