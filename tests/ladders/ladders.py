"""Play the ladder fixes and holding on to ladders end to end (#36).

    python3 tests/ladders/ladders.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/ladders

Hidden windows over TCP make the save states (fixtures: tests/movement/play.py
gets the hero to Trunk's screen 6 and its ladder; the recipes here walk him
out of Apolune's temple through a door edited to lead to Trunk $27 or $0C, an
external prerequisite, not coverage). The frame-exact checks run the same
build headless from those states, with RAM fixtures for the hero's place,
quests and a hit, and a button schedule, and read RAM every frame, with the
remaster on and off (off is the original game). Each part is its own game:

1. top: Up held on Trunk 6's ladder. The original climbs to Y=$40, his feet
   one pixel out of the top block, drops 8 pixels, is caught, and does it
   again for as long as Up is held. With the remaster he climbs the same way
   to Y=$41 and stays there, on the ladder, never falling; let go of Up and A
   jumps at once. In the window, ladders_state counts the climbs it stopped;
   with the remaster off it is off.
2. climb: the Trunk's climb, the ladder entrance between screens $0C and $16
   (AREA_SCREEN_COMPARATORS): the top block of $0C's ladder is a climbable
   transition block, so Up still takes him up into $16, and Down on $16's
   ladder back to $0C, with the remaster on. The way up to that ladder is a
   jump from the top of the one beside it: from $0C's floor, onto column 14's
   ladder, up to its top, let go of Up, walk left, jump, and Up on column
   13's. The original jumps there only if Up is let go right after one of its
   drops (the drop clears climbing, and climbing refuses A); a moment later
   he walks off the top instead. With the remaster it works whenever he lets
   go.
3. knockback: a hit on Trunk 6's ladder (Player_SetDamagedBySprite's state:
   invincibility $3C, knockback, facing the enemy). The original knocks him
   24 pixels off it and he falls to the floor; with the remaster he stays
   where he was, holding on, and climbs on afterwards.
   In the window, ladder_hold_state counts the three updates it held.
4. fountain: with the path to Mascon open, the hero walks from $27 into the
   fountain's screen ($28) and straight back out. In the original, $28's
   screen event runs on the pass he leaves in and drops the ladder into $27's
   blocks, through its floor; with the remaster $27 is as it loaded, and
   staying on $28 still drops the ladder there.

Screenshots of each, on and off, go in OUT.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "movement"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
import fax_e2e  # noqa: E402
from fax_e2e import parse_args  # noqa: E402
from play import (AREA, CAN_CLIMB, CLIMBING, FALLING, FLAGS, HP, IFRAMES, JUMPING, KNOCKBACK, LADDER_SCREEN, RIGHT,
                  SCREEN, STATUS, X, Y, Game, fixture, no_actors)  # noqa: E402

AREA_SAVED_AREA, AREA_SAVED_SCREEN, SCREEN_SAVED_PALETTE, PLAYER_SAVED_POS = 0x3D5, 0x3D6, 0x3D7, 0x3D8
AREA_REGION, QUESTS, SPECIAL_EVENT, SCREEN_BUFFER = 0x435, 0x42D, 0x42E, 0x600
PPU_BUFFER_READ, PPU_BUFFER_WRITE = 0x1F, 0x20                    # PPUBuffer_ReadOffset, PPUBuffer_WriteOffset
TRUNK, TRUNK_PALETTE, TRUNK_REGION, BUILDINGS = 1, 0x06, 1, 4
SPRINGS_AND_PATH = 0x27                     # the three springs done, and the path to Mascon open (Quests $20)
LADDER_BLOCK, AIR = 0x20, 0x42
FOUNTAIN_LADDER = [0x22 + 0x10 * row for row in range(7)]   # Game_DropLadderToMascon: column 2, rows 2-8
FOUNTAIN_STONES = [0x56, 0x66, 0x57, 0x67]                 # Game_OpenPathToMascon: the cover, moved, then cleared
PARTS = ("top", "climb", "knockback", "fountain")


def _out_to(g, screen, pos):
    """Out of the temple, through its door edited to lead to Trunk `screen`,
    standing at block `pos` (YX), once the screen's tiles are written: until
    the PPU buffer is empty (PPUBuffer_ReadOffset at PPUBuffer_WriteOffset)
    the game reads no input, so a fixture saved a frame earlier would miss a
    press its tests make a few frames in."""
    g.poke({AREA_SAVED_AREA: TRUNK, AREA_SAVED_SCREEN: screen, SCREEN_SAVED_PALETTE: TRUNK_PALETTE,
            PLAYER_SAVED_POS: pos, AREA_REGION: TRUNK_REGION, HP: 0x50})
    for _ in range(40):
        if g.ram(AREA) != BUILDINGS:
            break
        g.tcp("action", name="right", frames=30)
        g.wait(31)
    g.until(f"Trunk {screen:#x}", lambda: (g.ram(AREA), g.ram(SCREEN)) == (TRUNK, screen), 600)
    g.settle()
    g.until("the screen's tiles to be written", lambda: g.ram(PPU_BUFFER_READ) == g.ram(PPU_BUFFER_WRITE), 120)
    g.wait(2)


@fax_e2e.recipe("ladders-trunk-27", parent="apolune-temple")
def _trunk_27(g):
    """On Trunk $27's floor, a screen right of the fountain's ($28)."""
    _out_to(g, 0x27, 0x91)


@fax_e2e.recipe("ladders-trunk-0c", parent="apolune-temple")
def _trunk_0c(g):
    """On Trunk $0C's floor, under the ladder up to $16."""
    _out_to(g, 0x0C, 0xAE)


def drops(rams):
    """The updates he fell on."""
    return [i for i, r in enumerate(rams) if r[FLAGS] & FALLING]


def blocks(ram):
    return ram[SCREEN_BUFFER:SCREEN_BUFFER + 0xD0]


def main():
    args = parse_args(__doc__, parts=PARTS)
    game = Game.from_args(args)
    out = game.out
    shots = out / "shots"
    try:
        wants = [part for part in PARTS if args.wants(part)]
        states = game.to_trunk() if {"top", "knockback"} & set(wants) else {}
        climb = game.state("ladders-trunk-0c") if "climb" in wants else None
        trunk_27 = game.state("ladders-trunk-27") if "fountain" in wants else None
        shots.mkdir(exist_ok=True)
        on, off = game.mods("mods-on"), game.mods("mods-off", original=True)

        def pair(state, schedule, frames, name=None):
            """The same run with the remaster off and on, at once; with `name`,
            a screenshot of each one's last frame."""
            return game.headless_all([(state, schedule, frames, mods, shots / f"{name}-{kind}.png" if name else None)
                                      for mods, kind in ((off, "original"), (on, "remastered"))])

        if "top" in wants:
            ladder = out / "ladder.cycstate"
            fixture(states[LADDER_SCREEN], ladder, {**no_actors(), X: 224, Y: 136, FLAGS: CAN_CLIMB | CLIMBING, HP: 0x50})
            hold = [(0, "-"), (5, "UP")]
            was, now = pair(ladder, hold, 240)
            peak = next(i for i, r in enumerate(was) if r[Y] == 0x40)
            assert was[peak + 1][Y] == 0x48 and was[peak + 1][FLAGS] & FALLING, "the original drops off the top"
            assert not was[peak + 1][FLAGS] & CAN_CLIMB and was[peak + 2][FLAGS] & CAN_CLIMB, "and is caught"
            fell = drops(was)
            assert len(fell) >= 6, fell
            top = next(i for i, r in enumerate(now) if r[Y] == 0x41)
            assert all((now[i][X], now[i][Y], now[i][FLAGS]) == (was[i][X], was[i][Y], was[i][FLAGS])
                       for i in range(top)), "the climb up to the top is the game's"
            assert not drops(now), drops(now)
            assert all(r[Y] == 0x41 and r[FLAGS] & (CAN_CLIMB | FALLING) == CAN_CLIMB for r in now[top + 1:])
            print(f"ok: Up held at the top of Trunk 6's ladder: the original drops 8 pixels {len(fell)} times in "
                  f"{len(was)} frames; the remaster stops him at Y=$41 and he stays there, on the ladder")
            jump = hold + [(200, "-"), (201, "A"), (203, "-")]
            _, now = pair(ladder, jump, 240)
            assert now[200][Y] == 0x41 and now[201][FLAGS] & JUMPING, "A at the top jumps"
            print("ok: let go of Up at the top, and A jumps on the frame it is pressed")
            # A picture shows the sprites of the frame before it: the original a frame before a drop (at Y=$40,
            # climbing) and on it (Y=$48, out of the climb), and the remaster then, at the top.
            game.headless_all([(ladder, hold, fell[2] + 1, off, shots / "top-original-peak.png"),
                               (ladder, hold, fell[2] + 2, off, shots / "top-original-drop.png"),
                               (ladder, hold, fell[2] + 2, on, shots / "top-remastered.png")])
            # In the window: on by default, counting the climbs it stopped; off with the remaster off.
            game.launch()
            game.load(ladder)
            game.tcp("action", name="up", frames=200)
            game.until("the ladder's top", lambda: game.ram(Y) == 0x41, 600)
            game.wait(30)
            state = game.tcp("ladders_state")
            assert state["enabled"] and state["tops"] > 0 and game.ram(Y) == 0x41, state
            game.close()
            game.select(original=True)
            game.launch()
            assert not game.tcp("ladders_state")["enabled"], "the remaster is off"
            game.close()
            game.select()
            print(f"ok: in the window, ladders_state counted {state['tops']} climbs stopped at the top; off with "
                  "the remaster off")

        if "climb" in wants:
            floor_0c = out / "floor-0c.cycstate"
            fixture(climb, floor_0c, {**no_actors(), HP: 0x50})
            hold = [(0, "-"), (5, "A"), (7, "-"), (12, "UP")]               # onto column 14's ladder, and up
            drop = next(i for i in drops(game.headless(floor_0c, hold, 200, off)) if i >= 120)   # the original's, at the top

            def route(release):
                return hold + [(release, "-"), (release + 1, "LEFT"), (release + 5, "LEFT+A"), (release + 7, "UP")]

            def reached(rams):
                return any((r[AREA], r[SCREEN]) == (TRUNK, 0x16) for r in rams)

            after_drop, later_off, later_on = game.headless_all(
                [(floor_0c, route(drop + 1), 330, off), (floor_0c, route(drop + 5), 330, off),
                 (floor_0c, route(drop + 5), 330, on)])
            assert reached(after_drop), "the original: let go right after a drop, jump, and up into $16"
            assert not reached(later_off) and not any(r[FLAGS] & JUMPING for r in later_off[drop + 5:]), \
                "the original: let go mid-climb, and A does not jump"
            assert reached(later_on), "the remaster: let go any time at the top, jump, and up into $16"
            print(f"ok: the way up through $0C: a jump from the top of column 14's ladder to column 13's; the "
                  f"original jumps only if Up is let go right after a drop (frame {drop}), the remaster whenever")
            on_13 = out / "climb-0c.cycstate"                            # on $0C's ladder, column 13, row 1
            fixture(climb, on_13, {**no_actors(), X: 0xD0, Y: 0x10, FLAGS: CAN_CLIMB | CLIMBING, HP: 0x50})
            up = game.headless(on_13, [(0, "-"), (5, "UP")], 200, on, save=(199, out / "climb-16.cycstate"))
            arrived = next((i for i, r in enumerate(up) if (r[AREA], r[SCREEN]) == (TRUNK, 0x16)), None)
            assert arrived is not None, "Up on $0C's ladder climbs into $16"
            assert not drops(up[:arrived]), "on the ladder all the way"
            down = game.headless(out / "climb-16.cycstate", [(0, "-"), (5, "DOWN")], 200, on,
                                 screenshot=shots / "climb-back-remastered.png")
            assert any((r[AREA], r[SCREEN]) == (TRUNK, 0x0C) for r in down), "Down on $16's ladder climbs to $0C"
            print(f"ok: the Trunk's climb: Up on $0C's ladder reaches $16 (frame {arrived}), and Down there "
                  "comes back to $0C, with the remaster on")

        if "knockback" in wants:
            hit = out / "hit.cycstate"
            fixture(states[LADDER_SCREEN], hit, {**no_actors(), X: 224, Y: 128, FLAGS: CAN_CLIMB | CLIMBING | RIGHT,
                                                 HP: 0x50, STATUS: KNOCKBACK, IFRAMES: 0x3C})
            schedule = [(0, "-"), (30, "UP")]
            was, now = pair(hit, schedule, 60)
            assert [r[X] for r in was[:3]] == [216, 208, 200], [r[X] for r in was[:3]]
            assert not was[1][FLAGS] & CAN_CLIMB and was[29][Y] == 160, "the original: off the ladder, to the floor"
            assert all((r[X], r[Y]) == (224, 128) and r[FLAGS] & (CAN_CLIMB | CLIMBING) == CAN_CLIMB | CLIMBING
                       for r in now[:30]), "held on"
            assert now[1][STATUS] & KNOCKBACK and not now[2][STATUS] & KNOCKBACK, "the knockback's three updates"
            assert now[59][Y] < 128 and now[59][X] == 224, "and climbs on afterwards"
            pair(hit, schedule, 5, "knockback")                          # the original falling; the remaster holding on
            game.launch()                                                # in the window: on by default, counting
            game.load(hit)
            game.wait(10)
            state = game.tcp("ladder_hold_state")
            assert state["enabled"] and state["held"] == 3 and (game.ram(X), game.ram(Y)) == (224, 128), state
            game.close()
            print("ok: hit on Trunk 6's ladder: the original knocks him 24 pixels off it and he falls to the floor; "
                  "the remaster keeps him where he was, holding on, and he climbs on (in the window, "
                  "ladder_hold_state counted the 3 updates)")

        if "fountain" in wants:
            path_open = out / "trunk-27.cycstate"
            fixture(trunk_27, path_open, {**no_actors(), QUESTS: SPRINGS_AND_PATH, HP: 0x50})
            # Into $28, then straight back out: the original's scroll back
            # has begun by 120 frames, and Seamless, which walks him without
            # one, has him on $27 by then, and he stops there.
            back = [(0, "LEFT"), (60, "RIGHT"), (120, "-")]
            was, now = pair(path_open, back, 300, "fountain")
            for rams in (was, now):
                screens = [r[SCREEN] for r in rams]
                assert 0x28 in screens and screens[-1] == 0x27, "into $28 and back to $27"
            loaded = blocks(was[0])
            assert all(loaded[b] != LADDER_BLOCK for b in FOUNTAIN_LADDER), "$27 has no ladder there"
            misplaced = blocks(was[-1])
            assert all(misplaced[b] == LADDER_BLOCK for b in FOUNTAIN_LADDER), "the original drops it into $27"
            assert misplaced[0x57] == misplaced[0x67] == AIR != loaded[0x57], "and opens $27's floor at column 7"
            assert blocks(now[-1]) == loaded, "the remaster leaves $27 as it loaded"
            stay = [(0, "LEFT"), (120, "-")]
            _, here = pair(path_open, stay, 300, "fountain-28")
            assert here[-1][SCREEN] == 0x28 and here[-1][SPECIAL_EVENT] == 0xFF
            assert all(blocks(here[-1])[b] == LADDER_BLOCK for b in FOUNTAIN_LADDER), "the ladder drops on $28"
            assert all(blocks(here[-1])[b] == AIR for b in FOUNTAIN_STONES), "the fountain's stone is gone"
            print("ok: in and straight back out of the fountain's screen: the original drops its ladder into $27 "
                  "(column 2, rows 2-8, through the floor, and a hole at column 7); the remaster leaves $27 as it "
                  "loaded, and on $28 the ladder still drops")

        print("screenshots:", ", ".join(str(p) for p in sorted(shots.glob("*.png"))))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
