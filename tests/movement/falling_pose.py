"""Play the falling pose end to end.

    python3 tests/movement/falling_pose.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/pose

A hidden window over TCP gets the hero to Trunk (see play.py), runs uncapped
and waits on the game's own state, on a free port. The frame-by-frame checks
run the same build headless from save states made there, with RAM fixtures
for the hero's place and kit, and compare his sprites (the shadow OAM) with
the remaster on and off (off is the original game):

1. In the window: walking off Trunk's ledge, the body and gear pose routines
   answer the jump pose all the way down (falling_pose_state counts them);
   with the remaster off in the mod selection, never.
2. Frame by frame: on every frame he falls without jumping (and is not
   attacking, climbing or on the frame he lands), his sprites are a jump's,
   for his facing; on every other frame they are the original's. Walking off
   the ledge (the original shows him mid-step); a jump off it (as before
   through the arc, then the jump pose on the fall that follows instead of
   the walk pose); a swing in the air; the Wing Boots, hovering with A held
   and falling without; knockback on the floor and off the ledge; climbing
   a ladder and stepping off it.
3. Screenshots mid-fall, on and off.
"""
import argparse
from pathlib import Path
import sys

from play import (ATTACKING, CAN_CLIMB, CLIMBING, DURATION_WING_BOOTS, FALLING, FLAGS, HP, IFRAMES, JUMPING,
                  KNOCKBACK, LADDER_SCREEN, LEDGE_SCREEN, RIGHT, STATUS, X, Y, Game, fixture, hero_sprites, no_actors)



def climbing(ram):
    """Player_IsClimbing"""
    f = ram[FLAGS]
    return bool(f & CAN_CLIMB) and (ram[X] & 0x0F == 0 or not f & FALLING) and bool(f & CLIMBING)


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
        fixture(states[LEDGE_SCREEN], floor, {**no_actors(), X: 120, Y: 160, FLAGS: RIGHT, HP: 0x50})
        fixture(states[LADDER_SCREEN], ladder, {**no_actors(), X: 224, Y: 136, FLAGS: CAN_CLIMB | CLIMBING, HP: 0x50})

        # 1. Live: on by default, and the pose routines answer the jump pose while he falls.
        state = game.tcp("falling_pose_state")
        assert state["enabled"], "the feature is on by default"
        game.tcp("load_state", path=str(ledge))
        game.tcp("action", name="right", frames=60)
        game.until("the floor", lambda: game.ram(Y) == 160 and not game.ram(FLAGS) & FALLING, 600)
        state = game.tcp("falling_pose_state")
        assert state["body"] >= 9 and state["gear"] >= 18, state
        print(f"ok: in the window, the fall off the ledge drew {state['body']} body and {state['gear']} weapon "
              "and shield poses as the jump pose")
        game.close()
        game.select(original=True)
        game.launch()
        assert not game.tcp("falling_pose_state")["enabled"], "the remaster is off"
        game.tcp("load_state", path=str(ledge))
        game.tcp("action", name="right", frames=60)
        game.until("the floor", lambda: game.ram(Y) == 160 and not game.ram(FLAGS) & FALLING, 600)
        assert game.tcp("falling_pose_state")["body"] == 0
        print("ok: with the remaster off, the pose routines are the game's")
        game.close()
        game.select()

        # 2. Frame by frame, headless.
        on, off = game.mods("mods-on"), game.mods("mods-off", original=True)
        walk = [(0, "-"), (5, "RIGHT")]
        plain = game.headless(ledge, walk, 60, off)
        W = next(i for i, r in enumerate(plain) if r[Y] > 80)      # the first frame off the ledge
        assert plain[W - 1][Y] == 80 and plain[W][Y] == 88, (W, plain[W][Y])

        def pair(state, schedule, frames, writes=None):
            if writes:
                fixture(state, out / "case.cycstate", writes)
                state = out / "case.cycstate"
            return game.headless(state, schedule, frames, off), game.headless(state, schedule, frames, on)

        # The jump pose, facing right and left: a jump's sprites.
        jump_off, jump_on = pair(ledge, walk + [(W - 1, "RIGHT+A"), (W, "RIGHT")], 130)
        left_off, _ = pair(floor, [(0, "-"), (5, "LEFT+A"), (6, "LEFT")], 40, {FLAGS: 0})
        jump_pose = {}
        for rams, facing in ((jump_off, RIGHT), (left_off, 0)):
            poses = {hero_sprites(r) for r in rams if r[FLAGS] & JUMPING and not r[FLAGS] & ATTACKING}
            assert len(poses) == 1, f"a jump has one pose: {len(poses)}"
            jump_pose[facing] = poses.pop()

        def check(name, rams_off, rams_on, changed_at_least):
            """Every frame: the jump pose where the rule says, the original's elsewhere."""
            changed = 0
            for i in range(len(rams_off) - 1):
                r, nxt = rams_off[i], rams_off[i + 1]
                assert r[X:Y + 1] == rams_on[i][X:Y + 1] and r[FLAGS] == rams_on[i][FLAGS], f"{name}: he moved differently"
                landed = r[FLAGS] & FALLING and not nxt[FLAGS] & FALLING
                rule = (r[FLAGS] & FALLING and not r[FLAGS] & (JUMPING | ATTACKING) and not climbing(r) and not landed)
                was, now = hero_sprites(r), hero_sprites(rams_on[i])
                if rule:
                    assert now == jump_pose[r[FLAGS] & RIGHT], f"{name}, frame {i}: not the jump pose"
                    changed += was != now
                else:
                    assert now == was, f"{name}, frame {i}: the pose changed"
            assert changed >= changed_at_least and (changed_at_least or changed == 0), f"{name}: {changed} frames changed"
            print(f"ok: {name}: {changed} frames in the jump pose instead of the original's, the rest as before")
            return changed

        assert check("walking off the ledge", *pair(ledge, walk, 60), 9) == 9    # 80 pixels, 8 a frame; then the landing frame
        arc = sum(1 for r in jump_off if r[FLAGS] & JUMPING)
        check("a jump off the ledge, and the fall after its arc", jump_off, jump_on, 9)
        assert arc == 32
        check("a swing in the air", *pair(ledge, walk + [(W + 2, "RIGHT+B"), (W + 3, "RIGHT")], 60), 1)
        check("Wing Boots: hovering with A, then falling", *pair(
            ledge, walk + [(W + 2, "RIGHT+A"), (W + 30, "RIGHT")], 90, {DURATION_WING_BOOTS: 0x40}), 1)
        check("knockback on the floor", *pair(floor, [(0, "-")], 30, {FLAGS: 0, STATUS: KNOCKBACK, IFRAMES: 0x3C}), 0)
        check("knocked off the ledge", *pair(ledge, [(0, "-")], 30, {FLAGS: 0, STATUS: KNOCKBACK, IFRAMES: 0x3C}), 1)
        ladder_off, ladder_on = pair(ladder, [(0, "-"), (5, "UP"), (21, "DOWN"), (37, "LEFT"), (60, "-")], 80)
        climb = sum(1 for r in ladder_off if climbing(r))
        assert climb > 30, climb
        check(f"a ladder: {climb} frames climbing, then stepping off", ladder_off, ladder_on, 1)

        # 3. Screenshots mid-fall.
        shots = out / "shots"
        shots.mkdir(exist_ok=True)
        game.headless(ledge, walk, W + 5, on, shots / "mid-fall-jump-pose.png")
        game.headless(ledge, walk, W + 5, off, shots / "mid-fall-original.png")
        print("screenshots:", ", ".join(str(p) for p in sorted(shots.glob("*.png"))))
    except BaseException:
        if game.proc and game.proc.poll() is None:
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
