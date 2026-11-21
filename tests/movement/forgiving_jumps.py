"""Play forgiving jumps (coyote time and jump buffering) end to end.

    python3 tests/movement/forgiving_jumps.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/jumps

A hidden window over TCP gets the hero to Trunk (see play.py), uncapped, on a
free port. The frame-exact checks run the same build headless from save
states made there, with RAM fixtures for the hero's place and kit, and a
button schedule, and read his position and flags every frame, with the
remaster on and off (off is the original game):

1. In the window, at normal speed: A pressed 2 frames after walking off
   Trunk's ledge jumps (forgiving_jumps_state counts a coyote jump), and A
   pressed 3 frames before landing a jump on the floor jumps again on landing
   (a buffered jump). With the remaster off in the mod selection, neither.
   Each press goes in with the load of a state made headless the frame
   before it (fax_e2e's batch()), so it lands on that frame however far
   the script lags.
2. Coyote time. A on the last frame on the ledge is the original's best jump;
   a frame later the original just falls. With the feature on, A on any of
   the first 5 frames off the ledge jumps: every frame he is where the best
   jump is sideways, and no higher; the peak and the landing are reported. A
   on the 6th frame falls as before.
3. Jump buffering. A pressed 1-6 frames before the first frame back on the
   floor jumps on that frame, exactly as the original does with A pressed
   then; 7 frames before, nothing.
4. As before: the Wing Boots (A is flying), knockback (no coyote time after
   it), stepping off a ladder (no coyote time), and jumping out of a door
   (a press made in the temple does not jump in the town).
5. Screenshots: a coyote jump near its top, and the original game's fall
   (the remaster off) with the same late press.
"""
import argparse
from pathlib import Path
import sys

from play import (CAN_CLIMB, CLIMBING, DURATION_WING_BOOTS, FALLING, FLAGS, HP, IFRAMES, JC, JUMPING, KNOCKBACK,
                  LADDER_SCREEN, LEDGE_SCREEN, RIGHT, STATUS, X, Y, AREA, Game, fixture, no_actors)

COYOTE_FRAMES, BUFFER_FRAMES = 5, 6


def path(rams):
    """Where he is and what he is doing, every frame."""
    return [(r[X], r[Y], r[FLAGS], r[AREA]) for r in rams]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for key in ("build", "rom", "out"):
        ap.add_argument("--" + key, type=Path, required=True)
    ap.add_argument("--game-arg", action="append", default=[], help="extra game option for the window (repeatable)")
    args = ap.parse_args()
    game = Game(args.build, args.rom, args.out, args.game_arg)
    out = game.out
    try:
        states = game.to_trunk()
        game.launch()
        ledge, floor, ladder = out / "ledge.cycstate", out / "floor.cycstate", out / "ladder.cycstate"
        fixture(states[LEDGE_SCREEN], ledge, {**no_actors(), X: 8, Y: 80, FLAGS: RIGHT, HP: 0x50})
        fixture(states[LEDGE_SCREEN], floor, {**no_actors(), X: 40, Y: 160, FLAGS: RIGHT, JC: 0x20, HP: 0x50})
        fixture(states[LADDER_SCREEN], ladder, {**no_actors(), X: 224, Y: 136, FLAGS: CAN_CLIMB | CLIMBING, HP: 0x50})
        temple = out / "temple-exit.cycstate"
        fixture(states["temple"], temple, {X: 200})
        assert game.tcp("forgiving_jumps_state")["enabled"], "the feature is on by default"
        game.close()

        # 1. Live, at normal speed: a late press and an early one, each on its
        # frame: the states they start from are made headless.
        on = game.mods("mods-on")
        walk = [(0, "-"), (5, "RIGHT")]
        W = next(i for i, p in enumerate(path(game.headless(ledge, walk, 60, on))) if p[1] > 80)   # off the ledge
        walked_off = out / "walked-off.cycstate"                       # after his first frame off it
        game.headless(ledge, walk, W + 1, on, save=(W, walked_off))
        jump = [(0, "-"), (5, "A"), (6, "-")]
        once = path(game.headless(floor, jump, 80, on))
        ground = next(i for i in range(6, 80) if not once[i][2] & JUMPING) + 1   # back on the floor
        landing = out / "landing.cycstate"                             # 4 frames before it
        game.headless(floor, jump, ground - 3, on, save=(ground - 4, landing))

        def live(enabled):
            """The coyote and buffered jumps the late and the early press gave."""
            game.realtime = True
            game.launch()
            state = game.tcp("forgiving_jumps_state")
            assert state["enabled"] == enabled and state["coyote_frames"] == COYOTE_FRAMES, state
            game.batch(("load_state", dict(path=str(walked_off))), ("action", dict(name="right", frames=60)),
                       ("action", dict(name="a", frames=2)))
            game.until("the floor", lambda: game.ram(Y) == 160 and not game.ram(FLAGS) & (FALLING | JUMPING), 300)
            coyote = game.tcp("forgiving_jumps_state")["coyote"] - state["coyote"]
            state = game.tcp("forgiving_jumps_state")
            game.batch(("load_state", dict(path=str(landing))), ("action", dict(name="a", frames=2)))
            game.wait(30)
            buffered = game.tcp("forgiving_jumps_state")["buffered"] - state["buffered"]
            game.close()
            return coyote, buffered

        coyote, buffered = live(True)
        assert coyote == 1 and buffered == 1, (coyote, buffered)
        print("ok: in the window, A just after walking off the ledge jumped, and A just before landing jumped on landing")
        game.select(original=True)
        coyote, buffered = live(False)
        assert coyote == 0 and buffered == 0
        print("ok: with the remaster off, neither")
        game.select()
        game.realtime = False

        # 2. Coyote time, frame by frame.
        on, off = game.mods("mods-on"), game.mods("mods-off", original=True)
        walk = [(0, "-"), (5, "RIGHT")]
        frames = 110

        def press(at, held=1, base=walk, button="RIGHT"):
            return base + [(at, button + "+A"), (at + held, button)]

        plain = path(game.headless(ledge, walk, frames, off))
        W = next(i for i, p in enumerate(plain) if p[1] > 80)      # the first frame off the ledge
        land = next(i for i in range(W, frames) if plain[i][1] == 160) + 1   # the first frame back on the floor
        assert plain[W - 1][1] == 80 and plain[W][1] == 88 and land == W + 10, (W, land)
        # Every run below, at once: the best jump, the original with A late,
        # the feature with A late, A one frame too late, and A on landing.
        runs = ([(ledge, press(W - 1), frames, off)] +
                [(ledge, press(W + k), frames, off) for k in range(COYOTE_FRAMES + 1)] +
                [(ledge, press(W + k), frames, on) for k in range(COYOTE_FRAMES)] +
                [(ledge, press(W + COYOTE_FRAMES), frames, on), (ledge, press(land), frames, off)])
        got = [path(r) for r in game.headless_all(runs)]
        best, originals = got[0], got[1:COYOTE_FRAMES + 2]
        coyotes, (late, on_landing) = got[COYOTE_FRAMES + 2:-2], got[-2:]
        assert best[W - 1][2] & JUMPING and best[W - 1][1] == 72, "A on the last frame on the ledge jumps"
        for k, original in enumerate(originals):
            assert original == plain, f"the original: A {k + 1} late"
        print(f"ok: the original jumps with A on frame {W - 1}, the last on the ledge; from frame {W} A does nothing")

        def peak(p):
            return min(y for _, y, _, _ in p[W:])

        def lands(p):
            """The frame and x where he comes down on the floor, after leaving the ledge."""
            i = next(i for i in range(W, frames) if p[i][1] == 160 and p[i - 1][1] < 160)
            return i, p[i][0]

        print(f"   best jump: peak y={peak(best)} (32 above the ledge), lands on frame {lands(best)[0]} at x={lands(best)[1]}")
        for k, t in enumerate(coyotes):
            assert t[:W + k] == plain[:W + k] and t[W + k][2] & JUMPING, f"A on frame {W + k} jumps"
            for i in range(frames):
                assert t[i][0] == best[i][0], f"k={k}, frame {i}: x {t[i][0]}, the best jump's {best[i][0]}"
                assert t[i][1] >= best[i][1], f"k={k}, frame {i}: y {t[i][1]} above the best jump's {best[i][1]}"
            assert peak(t) == peak(best) + 8 * (k + 1), (k, peak(t))
            assert lands(t)[0] <= lands(best)[0] and lands(t)[1] <= lands(best)[1]
            print(f"ok: A {k + 1} frame(s) late jumps: peak y={peak(t)} ({8 * (k + 1)} below the best), "
                  f"lands on frame {lands(t)[0]} at x={lands(t)[1]}; never above or beyond the best jump")
        assert late[:land] == plain[:land] and late[land:] == on_landing[land:]
        print(f"ok: A {COYOTE_FRAMES + 1} frames late falls as before; then, {land - W - COYOTE_FRAMES} frames "
              "before landing, it is a buffered press and he jumps on landing")

        # 3. Jump buffering, from a jump on the floor.
        jump = [(0, "-"), (5, "A"), (6, "-")]
        once = path(game.headless(floor, jump, 80, off))
        ground = next(i for i in range(6, 80) if not once[i][2] & JUMPING) + 1
        assert once[ground - 1][1] == 160 and ground == 5 + 32, ground
        early = [jump + [(ground - j, "A"), (ground - j + 1, "-")] for j in range(BUFFER_FRAMES + 2)]
        got = [path(r) for r in game.headless_all([(floor, jump + [(ground, "A"), (ground + 1, "-")], 80, off)] +
                                                  [(floor, e, 80, mods) for e in early for mods in (on, off)])]
        again = got[0]
        assert again[ground][2] & JUMPING
        for j in range(BUFFER_FRAMES + 2):
            t_on, t_off = got[1 + 2 * j], got[2 + 2 * j]
            if 1 <= j <= BUFFER_FRAMES:
                assert t_on[:ground] == once[:ground] and t_on[ground:] == again[ground:], f"A {j} early"
                assert t_off == once
            else:
                assert t_on == t_off, f"A {j} early"
        print(f"ok: A 1-{BUFFER_FRAMES} frames before the first frame on the floor jumps then, as the original does "
              f"with A on that frame; {BUFFER_FRAMES + 1} before, or on it, as before")

        # 4. As before: each case the same off and on, the runs of a step at once.
        def case(name, state, writes=None):
            if writes:
                fixture(state, out / f"case-{name}.cycstate", writes)
                state = out / f"case-{name}.cycstate"
            return state

        def same(cases):
            """(state, schedule, frames, what) cases, each run off and on at once:
            their paths, each the same both ways."""
            got = [path(r) for r in game.headless_all(
                [(state, schedule, frames, mods) for state, schedule, frames, _ in cases for mods in (off, on)])]
            for i, (_, _, _, what) in enumerate(cases):
                assert got[2 * i] == got[2 * i + 1], what
            return got[::2]

        boots = case("boots", ledge, {DURATION_WING_BOOTS: 0x40})
        knock = {FLAGS: 0, STATUS: KNOCKBACK, IFRAMES: 0x3C}
        knocked = case("knock", ledge, knock)
        got = same([(boots, walk + [(W + k, "RIGHT+A"), (W + k + 25, "RIGHT")], 90, f"Wing Boots, A {k + 1} late")
                    for k in range(COYOTE_FRAMES)] + [(knocked, [(0, "-")], 40, "knockback")])
        for k, t in enumerate(got[:COYOTE_FRAMES]):
            assert t[W + k + 10][1] - t[W + k + 9][1] == 1, "hovering"
        print("ok: Wing Boots: A in the air is flying, as before")
        pushed = got[-1]
        drop = next(i for i, p in enumerate(pushed) if p[1] > 80)
        climbed = path(game.headless(ladder, [(0, "-"), (5, "LEFT"), (30, "-")], 40, off))
        off_ladder = next(i for i, p in enumerate(climbed) if p[2] & FALLING)
        door = [(0, "RIGHT"), (35, "RIGHT+A"), (36, "RIGHT"), (60, "RIGHT+A"), (61, "RIGHT")]
        got = same([(knocked, [(0, "-"), (drop + k, "A"), (drop + k + 1, "-")], 40, f"knocked off, A {k + 1} late")
                    for k in range(4)] +
                   [(ladder, [(0, "-"), (5, "LEFT"), (30, "-"), (off_ladder, "LEFT+A"), (off_ladder + 1, "LEFT")], 40,
                     "off a ladder"), (temple, door, 110, "jumping out of the temple")])
        print(f"ok: knocked off the ledge (in the air from frame {drop}), A on the next 4 frames does nothing, as before")
        print(f"ok: stepping off a ladder (frame {off_ladder}), A at once does nothing, as before")
        out_door = got[-1]
        assert out_door[59][2] & JUMPING and out_door[59][3] == 4 and out_door[100][3] == 3
        print("ok: jumping out of the temple's door with A pressed again just before: no jump in the town, as before")

        # 5. Screenshots: A 3 frames late, near the top of the coyote jump, and the original's fall.
        shots = out / "shots"
        shots.mkdir(exist_ok=True)
        game.headless_all([(ledge, press(W + 2), W + 12, on, shots / "coyote-jump.png"),
                           (ledge, press(W + 2), W + 6, off, shots / "late-press-original.png")])
        print("screenshots:", ", ".join(str(p) for p in sorted(shots.glob("*.png"))))
    except BaseException:
        if game.proc and game.proc.poll() is None:
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
