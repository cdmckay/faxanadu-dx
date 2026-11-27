"""Play Run (#198) end to end in a hidden window.

    python3 tests/run/run.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/run
    python3 tests/run/run.py ... --part ground

The game runs uncapped and the script waits on the game's own frames, never
on the clock, on a reserved TCP port (tests/e2e/fax_e2e.py). Every run starts
from a saved game in Eolis's town (screen $01) and goes left, through the
town, away from the Monodron on $08. Input goes in where a player's does,
batched with the load of the state it starts from, so it is down from the
state's first frame: TCP `action` holds the D-pad and A, `pad` pushes the left
trigger of an SDL virtual controller, and `key` holds Left Shift. Each
position is read once he has stopped (he stops the frame the D-pad lets go),
so no read has to land on a given frame.

ground    In Seamless. From a standstill, 40 frames of Left with the left
          trigger held cover 1.5 times the ground of 40 frames of Left alone,
          to the 256th of a pixel, and Left Shift the same; he speeds up as
          the walk does (the acceleration after both is the same). At his top
          speed (the save state edited to it), 20 frames run 45 pixels, 2.25
          a frame, where 20 frames walk 30. A jump from his top speed with the
          trigger held lands exactly where one without it does: in the air he
          keeps the walk's speed (and takes no D-pad), so running reaches no
          gap or ledge sooner.
original  With the remaster off (the stock view): the left trigger and Left
          Shift change nothing; the walk is the walk.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
from fax_e2e import Game, batch, chunks, fixture, parse_args, ram, tcp, wait  # noqa: E402

X_FRAC, SCREEN, PLAYER_Y, PLAYER_FLAGS, ACCEL = 0x9D, 0x63, 0xA1, 0xA4, 0xA9    # ACCEL: 8.8, with _U at $AA
JUMPING, FALLING, MOVING, RIGHT = 0x01, 0x04, 0x20, 0x40                         # Player_Flags
TOP_SPEED, RUN_TOP = 0x180, 0x240                                                # Player_MoveAcceleration's top; Run's step there
PACKAGE = ("faxanadu.dx", "dx")
PARTS = ("ground", "original")
# A TCP hold of N frames is down for N - 1: the host counts it down once
# before the frame it first reaches.
WALK = 40                                                                        # frames from a standstill
TOP = 20                                                                         # frames at the top speed
NOVICE_40 = 40 * 0xC0 + 2 * (40 * 41 // 2)                                       # $C2 + $C4 + ... + $110: a Novice's 40 updates


def state_ram(path, addr, n=1):
    """CPU RAM in a save state file (its HW chunk, from offset 23)."""
    data = Path(path).read_bytes()
    pos = next(p for tag, p, size in chunks(data) if tag == b"HW  ")
    return data[pos + 23 + addr:pos + 23 + addr + n]


def place():
    """Where he is, 8.8 (Player_PosX_Block, _Frac), and on which screen."""
    frac, block = ram(X_FRAC, 2)
    return block << 8 | frac, ram(SCREEN)


def run_state():
    return tcp("run_state")


def main():
    args = parse_args(__doc__, parts=PARTS)
    game = Game.from_args(args)

    def go(state, frames, *also, button="left"):
        """From `state`, `button` held `frames` frames from its first, with `also`
        (Run's trigger or key) the same frames; then where he stopped."""
        batch(("load_state", dict(path=str(state))), *also, ("action", dict(name=button, frames=frames + 1)))
        wait(frames + 12)
        assert not ram(PLAYER_FLAGS) & (JUMPING | FALLING), "in the air"
        return place()

    trigger = ("pad", dict(buttons="lefttrigger+", frames=WALK + 1))
    shift = ("key", dict(name="Left Shift", frames=WALK + 1))

    try:
        if args.wants("ground"):
            town = game.state("eolis-town")
            game.launch(at="eolis-town")
            frac, block = state_ram(town, X_FRAC, 2)
            start = block << 8 | frac
            assert state_ram(town, SCREEN)[0] == 1
            s = run_state()
            assert s["enabled"] and s["binding"] == "LEFT SHIFT / LT" and s["scale"] == 384, s

            walk, screen = go(town, WALK)
            walk_accel = ram(ACCEL) | ram(ACCEL + 1) << 8
            ran, run_screen = go(town, WALK, trigger)
            run_accel = ram(ACCEL) | ram(ACCEL + 1) << 8
            s = run_state()
            shifted, shift_screen = go(town, WALK, shift)
            walked, run = start - walk, start - ran
            assert screen == run_screen == shift_screen == 1, (screen, run_screen, shift_screen)
            assert walked == NOVICE_40, (walked, NOVICE_40)
            assert run == walked * 3 // 2, (run, walked)
            assert shifted == ran, (shifted, ran)
            assert walk_accel == run_accel == 0xC0 + 2 * WALK, (hex(walk_accel), hex(run_accel))
            assert s["runs"] == WALK and s["walks"] == 0 and s["step"] == run_accel * 3 // 2, s
            print(f"ok: from a standstill, {WALK} frames run {run / 256:.2f} pixels where they walk "
                  f"{walked / 256:.2f}, 1.5 times to the 256th, with the left trigger or Left Shift; the "
                  f"acceleration after both is ${run_accel:X}")

            # At the top speed: the town state, moving left at Player_MoveAcceleration's top.
            flags = state_ram(town, PLAYER_FLAGS)[0]
            top = game.out / "top-speed.cycstate"
            fixture(town, top, {ACCEL: TOP_SPEED & 0xFF, ACCEL + 1: TOP_SPEED >> 8,
                                PLAYER_FLAGS: (flags & ~RIGHT & 0xFF) | MOVING})
            before = run_state()["runs"]
            walk_top, _ = go(top, TOP)
            run_top, _ = go(top, TOP, ("pad", dict(buttons="lefttrigger+", frames=TOP + 1)))
            s = run_state()
            assert start - walk_top == TOP * TOP_SPEED, (start - walk_top, TOP * TOP_SPEED)
            assert start - run_top == TOP * RUN_TOP, (start - run_top, TOP * RUN_TOP)
            assert s["step"] == RUN_TOP and s["runs"] == before + TOP, s
            print(f"ok: at his top speed {TOP} frames run {(start - run_top) / 256:g} pixels, 2.25 a frame, where "
                  f"they walk {(start - walk_top) / 256:g}, 1.5")

            # A jump from the top speed, the trigger held through it or not.
            before = run_state()
            batch(("load_state", dict(path=str(top))), ("pad", dict(buttons="lefttrigger+", frames=40)),
                  ("action", dict(name="a", frames=8)))
            wait(50)
            held, held_screen = place()
            held_y = ram(PLAYER_Y)
            after = run_state()
            game.shot("landed")
            batch(("load_state", dict(path=str(top))), ("action", dict(name="a", frames=8)))
            wait(50)
            let_go, let_go_screen = place()
            assert ram(PLAYER_Y) == held_y == state_ram(top, PLAYER_Y)[0], (ram(PLAYER_Y), held_y)
            assert (held, held_screen) == (let_go, let_go_screen), (held, held_screen, let_go, let_go_screen)
            assert start - held > 40 * 256, start - held                     # he did jump along
            assert after["runs"] == before["runs"] and after["walks"] >= before["walks"] + 30, (before, after)
            print(f"ok: a jump from his top speed lands {(start - held) / 256:.2f} pixels on with the trigger held "
                  f"or not, the same to the 256th: in the air ({after['walks'] - before['walks']} updates) Run "
                  f"keeps the walk's speed")
            game.close()

        if args.wants("original"):
            town = game.state("eolis-town")
            game.set_features({PACKAGE: False})
            game.launch(at="eolis-town")
            assert not run_state()["enabled"]
            walk, _ = go(town, WALK)
            ran, _ = go(town, WALK, trigger)
            shifted, _ = go(town, WALK, shift)
            assert walk == ran == shifted, (walk, ran, shifted)
            assert run_state()["runs"] == 0
            print("ok: with the remaster off, the left trigger and Left Shift change nothing: the walk is the walk")
            game.close()
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    main()
