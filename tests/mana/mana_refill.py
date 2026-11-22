"""Play MP refill end to end.

    python3 tests/mana/mana_refill.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/mana

From the saved game outside Eolis's gate (the fixture "gate"), with Deluge
given and MP set through save-state RAM and the feature's record (an external
prerequisite, not coverage): 60 MP (or 61), its wait just begun.

Headless runs of the build from there, with Up+B on a schedule and each
frame's RAM logged, check the timing to the frame:

1. A cast (Deluge, 2 MP) takes MP to 58. It stays there for the 240 frames of
   the wait, then climbs 1 MP every 30 frames.
2. With the remaster off, as the original, MP stays where the cast left it.
3. The MP bar shows it: once the first MP comes back, the bar's tiles and its
   partial tile are what the game draws itself for 59 MP (a cast from 61).
4. A cast during the wait starts it again, and so does one while MP climbs.

In the window, over TCP, from just after that cast from 61 (59 MP, the bar
drawn, the wait begun):

5. With the hero's thoughts in a text box (the intro, at x 53), no frame
   counts: MP and the wait hold still for longer than the wait, and once the
   box closes the wait goes on from where it was and MP climbs.
6. Paused (Start), and on the map, the same.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "movement"))
from fax_e2e import act, box_open, chunks, fixture, hold, page_through, parse_args, ram, tcp, until, wait  # noqa: E402
import play  # noqa: E402  (fax_e2e.Game with headless runs)

MP, SELECTED_MAGIC, MAGIC_INVENTORY, NUMBER_OF_MAGIC_SPELLS = 0x39A, 0x3C0, 0x3A9, 0x3C5
GAME_PAUSED_STATE, POS_X = 0x120, 0x9E
DELUGE, DELUGE_COST = 0, 2
WAIT, STEP = 240, 30                          # fax_mana.c: frames from a spend to the first MP, then per MP
RECORD = "faxanadu.mana-refill"               # its save-state record: [1] seen, [2] MP seen, [3] frames left
BAR, BAR_TILES = 0x43, 11                     # the MP bar: nametable $2043, UI_MPAndHPBarWidth + 1 tiles
PARTIAL = 0x10D0                              # its partial tile, $0D, in the pattern table at $1000
FULL_TILE, PARTIAL_TILE, EMPTY_TILE, END_TILE = 0x09, 0x0D, 0x07, 0x0B
THOUGHTS_X = 53                               # the intro's trigger stops the hero here
CAST_AT = 10                                  # the frame of a headless run Up+B goes down on


def cast_at(at):
    """Up+B the stock way: Up and B down together on frame `at`, Up held a little longer."""
    return [(at, "UP+B"), (at + 4, "UP"), (at + 10, "-")]


def gains(mps):
    return [i for i in range(1, len(mps)) if mps[i] > mps[i - 1]]


def drops(mps):
    return [i for i in range(1, len(mps)) if mps[i] < mps[i - 1]]


def due(casts, frames):
    """The frames MP should come back on in a run of `frames` frames, with
    casts on the frames `casts` and MP never full: WAIT frames after each
    cast, then every STEP, until the next cast."""
    ends = casts[1:] + [frames]
    return [c + WAIT + k * STEP for c, end in zip(casts, ends) for k in range(frames) if c + WAIT + k * STEP < end]


class State:
    """A save state's CPU RAM, CHR RAM and nametables (its PPU chunk ends with CIRAM)."""

    def __init__(self, path):
        data = Path(path).read_bytes()
        c = {tag: (pos, size) for tag, pos, size in chunks(data)}
        pos, _ = c[b"HW  "]
        self.ram = data[pos + 23:pos + 23 + 2048]
        pos, size = c[b"CHRR"]
        self.chr = data[pos:pos + size]
        pos, size = c[b"PPU "]
        self.ciram = data[pos:pos + size][-0x1000:]

    def bar(self):
        """The MP bar: its nametable tiles, and its partial tile's pattern."""
        return bytes(self.ciram[BAR:BAR + BAR_TILES]), bytes(self.chr[PARTIAL:PARTIAL + 16])


def held_still(before, after, mp):
    """Two mana_state replies: MP, the wait and the frames played the same, nothing given."""
    keys = ("mp", "left", "played", "refills")
    return after["mp"] == mp and all(after[k] == before[k] for k in keys)


def main():
    args = parse_args(__doc__)
    game = play.Game.from_args(args)
    out = game.out
    try:
        gate = game.state("gate")
        spells = {SELECTED_MAGIC: DELUGE, MAGIC_INVENTORY: DELUGE, NUMBER_OF_MAGIC_SPELLS: 1}

        def at_mp(mp):
            path = out / f"gate-{mp}mp.cycstate"
            fixture(gate, path, {**spells, MP: mp}, records={RECORD: {1: 1, 2: mp, 3: WAIT}})
            return path
        s60, s61 = at_mp(60), at_mp(61)
        on, off = game.mods("on"), game.mods("off", original=True)

        # 1. The wait, then 1 MP every STEP frames; 2. the original.
        frames = CAST_AT + WAIT + 3 * STEP + 10
        climb, original = game.headless_all([(s60, cast_at(CAST_AT), frames, on),
                                             (s60, cast_at(CAST_AT), frames, off)])
        mps = [r[MP] for r in climb]
        assert mps[0] == 60 and drops(mps) == [CAST_AT] and mps[CAST_AT] == 60 - DELUGE_COST, mps
        want = due([CAST_AT], frames)
        assert gains(mps) == want, f"MP came back at {gains(mps)}, not {want}"
        assert [mps[g] for g in want] == list(range(59, 59 + len(want))), [mps[g] for g in want]
        print(f"ok: Deluge on frame {CAST_AT} takes MP to 58; it holds {WAIT} frames, then climbs 1 MP every "
              f"{STEP} frames ({', '.join(f'{mps[g]} on {g}' for g in want)})")
        mps = [r[MP] for r in original]
        assert mps[0] == 60 and drops(mps) == [CAST_AT] and set(mps[CAST_AT:]) == {58}, mps
        print(f"ok: with the remaster off, MP stays at 58 for the {frames - CAST_AT} frames after the cast")

        # 3. The bar: 59 as the game draws it.
        first = want[0]
        before, after, cast = out / "before.cycstate", out / "after.cycstate", out / "cast-to-59.cycstate"
        game.headless_all([(s60, cast_at(CAST_AT), first - 1, on, None, (first - 2, before)),
                           (s60, cast_at(CAST_AT), first + 3, on, out / "refilled-to-59.png", (first + 2, after)),
                           (s61, cast_at(CAST_AT), CAST_AT + 6, on, out / "cast-to-59.png", (CAST_AT + 5, cast))])
        game.shots += ["refilled-to-59.png", "cast-to-59.png"]
        before, after, cast = State(before), State(after), State(cast)
        assert (before.ram[MP], after.ram[MP], cast.ram[MP]) == (58, 59, 59)
        assert after.bar()[0] == bytes([FULL_TILE] * 7 + [PARTIAL_TILE] + [EMPTY_TILE] * 2 + [END_TILE]), after.bar()
        assert after.bar() == cast.bar(), (after.bar(), cast.bar())
        assert before.bar() != cast.bar(), "the bar showed 58 before"
        print("ok: the MP bar shows 59 once it comes back, tile for tile as the game draws 59 itself")

        # 4. A cast starts the wait again: during the wait, and while MP climbs.
        second, third = CAST_AT + 200, CAST_AT + 200 + WAIT + STEP + STEP // 2
        frames = third + WAIT + 10
        mps = [r[MP] for r in game.headless(s60, cast_at(CAST_AT) + cast_at(second) + cast_at(third), frames, on)]
        d = drops(mps)
        assert d == [CAST_AT, second, third] and all(mps[i] == mps[i - 1] - DELUGE_COST for i in d), (d, mps)
        want = due(d, frames)
        assert want == [second + WAIT, second + WAIT + STEP, third + WAIT], want
        assert gains(mps) == want, f"MP came back at {gains(mps)}, not {want}"
        print(f"ok: a cast during the wait (frame {second}) starts it again, and so does one while MP climbs "
              f"(frame {third}): MP back on {', '.join(map(str, want))}")

        # 5. A text box: no frame counts.
        game.launch(at="gate")
        state = tcp("mana_state")
        assert state["enabled"] and (state["wait"], state["step"]) == (WAIT, STEP), state
        start = out / "cast-to-59.cycstate"
        game.load(start)
        assert hold("right", box_open, 300), "the hero's thoughts"
        assert ram(POS_X) <= THOUGHTS_X + 2, ram(POS_X)
        held = tcp("mana_state")
        assert held["mp"] == 59 and not held["playing"] and held["left"] < WAIT, held
        wait(WAIT)                                                    # longer than the wait has left
        still = tcp("mana_state")
        assert box_open() and held_still(held, still, 59), (held, still)
        game.shot("thoughts")
        page_through("the end of the hero's thoughts", lambda: not box_open())
        closed = tcp("mana_state")                                    # a few frames on: the wait goes on
        assert closed["refills"] == held["refills"] and closed["mp"] == 59, (held, closed)
        assert closed["left"] + closed["played"] == held["left"] + held["played"], (held, closed)
        until("MP to climb", lambda: ram(MP) == 60, held["left"] + 60)
        print(f"ok: in a text box MP and the wait hold still ({held['left']} frames left, for {WAIT} frames); "
              "once it closes the wait goes on, and MP climbs")

        # 6. Paused, and on the map.
        game.load(start)
        wait(30)
        act("start", 4, 0)
        until("the pause", lambda: ram(GAME_PAUSED_STATE) != 0, 60)
        held = tcp("mana_state")
        wait(WAIT)
        still = tcp("mana_state")
        assert ram(GAME_PAUSED_STATE) and not still["playing"] and held_still(held, still, 59), (held, still)
        game.shot("paused")
        act("start", 4, 0)
        until("the game to go on", lambda: ram(GAME_PAUSED_STATE) == 0, 60)
        tcp("key", name="M", frames=2)
        until("the map", lambda: tcp("map_state")["open"], 60)
        held = tcp("mana_state")
        wait(60)
        still = tcp("mana_state")
        assert held_still(held, still, 59), (held, still)
        game.shot("map")
        tcp("key", name="M", frames=2)
        until("the map to close", lambda: not tcp("map_state")["open"], 60)
        until("MP to climb", lambda: ram(MP) == 60, held["left"] + 60)
        print("ok: paused, and on the map, MP and the wait hold still; after, MP climbs")
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
