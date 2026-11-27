"""Play The view shows only rooms (#166's slice 4) end to end, headless, every
frame.

    python3 tests/world/only_rooms.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/only-rooms

Each part starts from a fixture made out of Eolis's meat shop, its door edited
to lead onto a screen of a tower (tests/block/harness.py's warp(), an
external prerequisite, not coverage), under Ointment so nothing knocks him
about. The hidden window, at that save, gives the map: every room's place on
it (fax_world_room's xy and owned) and the camera (fax_world's). Then the
walk is played headless from the same save, a button schedule from its
frame, with Seamless's per-frame trace (NESRECOMP_WORLD_TRACE: the room, and
the camera the frame's picture has with the hero's X and Y it is worked out
from, after every frame), and on
every frame the view, 424x208 below the status bar at the camera, is checked
against the places:

1. trunk: the Tower of Trunk, out of a door onto $10, its view's top left
   black before (the old camera, centred on him and kept in the box round
   the rooms, worked out here from the same frames, lies over an empty place
   from the first). Right through $11 into $12, and back: the rows above and
   below hold rooms only over one column each, so the view keeps his row, and
   on no frame lies over an empty place.
2. ladder: down $10's ladder into $0F, the seam whose rows' rooms share only
   that column ($10-$12 over $0D-$0F): no view over rooms shows him there, so
   the view lies over an empty place only while his body is across the seam,
   and keeps his row on either side of it.
3. mist: the Tower of Suffer (the Mist's $30-$3D), on $37's upper ledge,
   where the view reaches up into $32 above him, then right along it into
   $38, with no room over $39 (an empty corner): the view comes down to his
   row as he walks toward it, before the corner is in sight; he drops through
   $38's gap and walks on into $39 under the empty corner, and back left to
   $37. On no frame does the view lie over an empty place, and it reached
   into the row above at the start.

In every part the camera moves at most a few pixels a frame (MOST, 8: a
knockback's speed; through $10's seam, where the view swings a row and its
rooms in the 32 pixels he straddles it, 7, and elsewhere 1 or 2), and only
when he does (it is a function of his place, so Seams' view, which actors
wake in, never moves without him), and his body is always in the view. The
trace's camera matches fax_world's in the window at the start.

Screenshots: the first frame out of the door onto $10, and the Mist's empty
corner, as the window presents them (headless --present-out).
"""
import csv
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "block"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "movement"))
import fax_e2e  # noqa: E402
from fax_e2e import batch, parse_args  # noqa: E402
import harness  # noqa: E402  (warp(): out of a building onto any screen)
import play  # noqa: E402  (headless runs from a save state)

TRUNK, MIST, REGION_TRUNK, REGION_MIST = 1, 2, 1, 2
PALETTE_TOWER, PALETTE_SUFFER = 0x07, 0x0B
POS_X, POS_Y = 0x9E, 0xA1
OINTMENT = 0x427                                          # DurationOintment: below $80, no touch gets through
ROOM_W, ROOM_H, VIEW_W, VIEW_H = 256, 208, 424, 208
MOST = 8                                                  # pixels a frame, each way: a knockback's speed
COUNTS = (9, 64, 83, 14, 10, 40, 32, 20)                  # screens per area
PARTS = ("trunk", "ladder", "mist")


class Game(harness.Game, play.Game):
    """harness.Game's warp(), and play.Game's headless runs and mod catalogs."""

    def traced(self, state, schedule, frames, mods, picture=None):
        """Play `frames` frames headless from `state`, holding buttons on a
        schedule of (frame, buttons) from its frame, as play.Game.headless
        does: each frame's line of Seamless's trace, as a dict of ints (None
        where blank). With `picture`, also what the window presents after the
        last frame."""
        start = self.continues_at(state)
        run = Path(tempfile.mkdtemp(prefix="trace-", dir=self.out))
        mods = shutil.copytree(mods, run / "mods")
        (run / "input.txt").write_text("".join(f"{start + at} {buttons}\n" for at, buttons in sorted(schedule)))
        trace = run / "world.csv"
        args = [str(self.exe), str(self.rom), "--load-state", str(state), "--input", str(run / "input.txt"),
                "--frames", str(start + frames), "--mods-root", str(mods), "--no-save"]
        if picture:
            args += ["--present-out", str(picture)]
        done = subprocess.run(args, capture_output=True, text=True, env=dict(os.environ, NESRECOMP_WORLD_TRACE=str(trace)))
        assert done.returncode == 0, done.stdout + done.stderr
        with open(trace, newline="") as f:
            rows = [{k: (int(v) if v not in ("", None) and k != "simulation_ms" else None) for k, v in r.items()}
                    for r in csv.DictReader(f)]
        shutil.rmtree(run)
        assert len(rows) == frames, (len(rows), frames)
        return rows


@fax_e2e.recipe("rooms-trunk-10", parent="eolis-meat-shop")
def _trunk_10(g):
    """Out of Eolis's meat shop onto the Tower of Trunk's $10, its door
    edited to lead there, at block $97 (x 112 on the floor), under Ointment."""
    g.warp(g.save("shop"), "trunk-10", TRUNK, 0x10, PALETTE_TOWER, 0x97, REGION_TRUNK, {OINTMENT: 0x7F})
    g.poke({POS_X: 112, POS_Y: 144})        # a step taken while the screen loaded
    g.wait(30)


@fax_e2e.recipe("rooms-trunk-ladder", parent="eolis-meat-shop")
def _trunk_ladder(g):
    """The same onto $10 at block $92: x 32, on the ladder down to $0F."""
    g.warp(g.save("shop"), "trunk-ladder", TRUNK, 0x10, PALETTE_TOWER, 0x92, REGION_TRUNK, {OINTMENT: 0x7F})
    g.poke({POS_X: 32, POS_Y: 144})         # on the ladder, as walking out may have stepped him off it
    g.wait(30)


@fax_e2e.recipe("rooms-mist-37", parent="eolis-meat-shop")
def _mist_37(g):
    """The same onto the Mist's $37, in the Tower of Suffer, at block $3D:
    x 208 on its upper ledge (row 5)."""
    g.warp(g.save("shop"), "mist-37", MIST, 0x37, PALETTE_SUFFER, 0x3D, REGION_MIST, {OINTMENT: 0x7F})
    g.poke({POS_X: 208, POS_Y: 48})
    g.wait(30)


def places(area):
    """Every room on the map at this frame (fax_world_room's owned and xy),
    and fax_world, read in one batch: {room: (col, row)}, and the camera."""
    replies = batch(("fax_world", {}), *(("fax_world_room", dict(room=n)) for n in range(COUNTS[area])))
    world, rooms = replies[0], replies[1:]
    assert world["area"] == area, world
    return {r["room"]: tuple(r["xy"]) for r in rooms if r["owned"]}, tuple(world["camera"]), world


def black(at, x, y):
    """The places with no room that the view at (x, y) lies over."""
    held = set(at.values())
    return [(c, r) for r in range(y // ROOM_H, (y + VIEW_H - 1) // ROOM_H + 1)
            for c in range(x // ROOM_W, (x + VIEW_W - 1) // ROOM_W + 1) if (c, r) not in held]


def old_camera(at, hx, hy):
    """The camera before this slice: centred on him, kept in the box round
    the rooms, centred in it where it is narrower (fax_world.c's camera_at)."""
    cols, rows = [c for c, _ in at.values()], [r for _, r in at.values()]
    l, r, t, b = min(cols), max(cols), min(rows), max(rows)
    x, y = hx + 8 - VIEW_W // 2, hy + 16 - VIEW_H // 2
    xmax, ymax = (r + 1) * ROOM_W - VIEW_W, (b + 1) * ROOM_H - VIEW_H
    x = (l * ROOM_W + xmax) // 2 if xmax < l * ROOM_W else min(max(x, l * ROOM_W), xmax)
    y = ymax if ymax < t * ROOM_H else min(max(y, t * ROOM_H), ymax)
    return x, y


def walk(at, rows, area):
    """Each frame's (hero's top-left in world pixels, camera), checked: in
    play the whole walk, his body in the view, the camera moving at most MOST
    a frame each way, and only when he moves; and the most it moved."""
    out, last, most = [], None, 0
    for i, row in enumerate(rows):
        assert row["area"] == area and row["room"] in at and row["camera_x"] is not None, (i, row)
        c, r = at[row["room"]]
        hero = (c * ROOM_W + row["shown_x"], r * ROOM_H + row["shown_y"])      # as the picture has him
        cam = (row["camera_x"], row["camera_y"])
        assert cam[0] <= hero[0] and hero[0] + 16 <= cam[0] + VIEW_W and cam[1] <= hero[1] and \
            hero[1] + 32 <= cam[1] + VIEW_H, (i, "his body out of the view", hero, cam)
        if last:
            moved = max(abs(cam[0] - last[1][0]), abs(cam[1] - last[1][1]))
            assert moved <= MOST, (i, f"the camera moved {moved} pixels in a frame", last, (hero, cam))
            most = max(most, moved)
            assert hero != last[0] or cam == last[1], (i, "the camera moved without him", last, (hero, cam))
        last = (hero, cam)
        out.append(last)
    return out, most


def main():
    args = parse_args(__doc__, parts=PARTS)
    game = Game.from_args(args)
    out = game.out
    try:
        trunk = game.state("rooms-trunk-10") if args.wants("trunk") else None
        ladder = game.state("rooms-trunk-ladder") if args.wants("ladder") else None
        mist = game.state("rooms-mist-37") if args.wants("mist") else None
        mods = game.mods("mods-on")

        def map_at(name, state, area):
            """The map and fax_world's camera in the window at `state`; the
            trace's camera for the same frame matches it."""
            if not game.running():
                game.launch(at=name)
            game.load(state)
            game.wait(2)
            at, camera, world = places(area)
            first = game.traced(state, [], 3, mods)
            assert (first[-1]["camera_x"], first[-1]["camera_y"]) == camera, (first[-1], world)
            return at

        if args.wants("trunk"):
            at = map_at("rooms-trunk-10", trunk, TRUNK)
            assert at[0x10][1] == at[0x11][1] == at[0x12][1] and at[0x0F] == (at[0x10][0], at[0x10][1] + 1) and \
                at[0x13] == (at[0x12][0], at[0x12][1] - 1), at
            shot = out / f"{len(game.shots):02d}-trunk-10.png"
            game.traced(trunk, [], 1, mods, picture=shot)
            game.shots.append(str(shot))
            # Right through $11 into $12, then back to $10.
            rows = game.traced(trunk, [(0, "RIGHT"), (430, "LEFT"), (860, "-")], 880, mods)
            frames, most = walk(at, rows, TRUNK)
            screens = [r["room"] for r in rows]
            assert 0x12 in screens and screens[-1] == 0x10, sorted(set(screens))
            bad = [(i, cam, black(at, *cam)) for i, (_, cam) in enumerate(frames) if black(at, *cam)]
            assert not bad, ("the view lies over an empty place", bad[:5])
            row_top = at[0x10][1] * ROOM_H
            assert all(cam[1] == row_top for _, cam in frames), "the view keeps his row"
            before = [i for i, (hero, _) in enumerate(frames) if black(at, *old_camera(at, *hero))]
            assert before and before[0] == 0, ("the old camera showed black out of the door", before[:3])
            print(f"ok: Trunk $10 to $12 and back, {len(frames)} frames: the view never lies over an empty place "
                  f"(the old camera's did on {len(before)} of them, from the first), and keeps his row; it moved at "
                  f"most {most} pixels a frame")

        if args.wants("ladder"):
            at = map_at("rooms-trunk-ladder", ladder, TRUNK)
            rows = game.traced(ladder, [(0, "DOWN")], 360, mods)
            frames, most = walk(at, rows, TRUNK)
            seam = at[0x0F][1] * ROOM_H
            assert rows[-1]["room"] == 0x0F and frames[-1][0][1] > seam + 40, rows[-1]
            across = [i for i, (hero, _) in enumerate(frames) if seam - 32 < hero[1] < seam]
            dark = [i for i, (_, cam) in enumerate(frames) if black(at, *cam)]
            assert dark and set(dark) <= set(across), ("black only while his body is across the seam",
                                                       dark[:3], across[:1], across[-1:])
            assert all(cam[1] in (seam - ROOM_H, seam) for i, (_, cam) in enumerate(frames) if i not in across), \
                "the view keeps his row on either side of the seam"
            print(f"ok: down $10's ladder into $0F: black on {len(dark)} of the {len(across)} frames his body is "
                  f"across the seam, where no view over rooms shows him, and on no other; it moved at most {most} pixels a "
                  f"frame, following him through")

        if args.wants("mist"):
            at = map_at("rooms-mist-37", mist, MIST)
            assert at[0x37][1] == at[0x38][1] == at[0x39][1] and at[0x32] == (at[0x37][0], at[0x37][1] - 1) and \
                (at[0x39][0], at[0x39][1] - 1) not in at.values(), at
            rows = game.traced(mist, [(0, "RIGHT"), (300, "LEFT"), (800, "-")], 820, mods)
            frames, most = walk(at, rows, MIST)
            screens = [r["room"] for r in rows]
            assert 0x39 in screens and screens[-1] == 0x37, sorted(set(screens))
            bad = [(i, cam, black(at, *cam)) for i, (_, cam) in enumerate(frames) if black(at, *cam)]
            assert not bad, ("the view lies over an empty place", bad[:5])
            row_top = at[0x37][1] * ROOM_H
            above = [row_top - cam[1] for _, cam in frames]
            assert above[0] > 0 and max(above) > 0, ("the view reached into the row above at the start", above[:5])
            under = [i for i, (hero, cam) in enumerate(frames) if hero[0] // ROOM_W == at[0x39][0]]
            assert under and all(above[i] <= 0 for i in under), "under the empty corner, the view keeps off it"
            glide = [i for i in range(1, len(frames)) if above[i] < above[i - 1] and frames[i][0][1] == frames[i - 1][0][1]]
            assert glide, "the view came down to his row as he walked"
            # The corner, as the window shows it: him in $39, under it.
            shot = out / f"{len(game.shots):02d}-mist-corner.png"
            game.traced(mist, [(0, "RIGHT")], under[0] + 40, mods, picture=shot)
            game.shots.append(str(shot))
            print(f"ok: the Mist's $37 to $39 and back, {len(frames)} frames: the view never lies over an empty place; "
                  f"it reached {above[0]} lines into the row above on $37's ledge and came down to his row over "
                  f"{len(glide)} frames of his walk, before the empty corner over $39; it moved at most {most} pixels a frame")
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    main()
