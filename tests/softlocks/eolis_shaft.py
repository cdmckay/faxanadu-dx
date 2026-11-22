"""Play the Eolis shaft softlock end to end in a hidden window (#38).

    python3 tests/softlocks/eolis_shaft.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/shaft

The game runs uncapped and the script waits on the game's own state, never on
the clock, on a reserved TCP port (tests/e2e/fax_e2e.py). Each part starts on
Eolis's last screen ($08, the gate to the Trunk), out of a building's door
edited to lead there (the eolis-8 fixture), with Wing Boots selected and no
enemy on the screen (external prerequisites, not coverage: in a game they
come to Eolis with a death before any temple Guru, which respawns there).

Eolis $00 and $08 have no screen right of them, and their right wall (column
14, rows 3-11) stands one block short of the edge: column 15 is open from the
sky to the bottom, through the floor. The hero uses the Wing Boots, flies up
over the wall (Up, then Right with A held) and lets go: he falls down column
15 to Y $C0 (Player_SetPosAtBottomEdge, no screen below).

1. original (the remaster off): once the flight ends (DurationWingBoots
   poked to its last second, an external prerequisite: the flight itself was
   played), A, Up and Right leave him where he is; Left walks him along the
   open row under the floor, into the town's screen ($01), whose edge stops
   him, still at Y $C0, where A does nothing either. No enemy can reach him
   there: a softlock, which only a reset ends.
2. remastered: the same flight, and when it ends Recharging items keeps the
   pair and starts its cooldown (its record's seconds poked to the last one,
   as recharging_items.py plays a whole cooldown). Then Down+B flies again,
   up the shaft and left over the wall, and he lands on Eolis's floor.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
import fax_e2e  # noqa: E402
from fax_e2e import Game, parse_args, ram, tcp, until, until_typed, wait  # noqa: E402

PARTS = ("original", "remastered")
PACKAGE = ("faxanadu.remastered", "remastered")               # the one switch: off is the original game
AREA, SCREEN, HERO_X, HERO_Y, FLAGS, STATUS = 0x24, 0x63, 0x9E, 0xA1, 0xA4, 0xA5
TEXTBOX_CLOSED, ENTITIES, HP = 0x20C, 0x2CC, 0x431
SELECTED_ITEM, DURATION_WING_BOOTS = 0x3C1, 0x429
SAVED_AREA, SAVED_SCREEN, SAVED_PALETTE, SAVED_POS, AREA_REGION = 0x3D5, 0x3D6, 0x3D7, 0x3D8, 0x435
EOLIS, BUILDINGS, TOWN_SCREEN, GATE_SCREEN = 0, 4, 0x01, 0x08
WING_BOOTS, FLYING = 0x0F, 0x80
NPCS = (0x34, 0x45)                                        # SPRITE_NPC entities: the town's people
SHAFT_X, BOTTOM_Y, FLOOR_Y, WALL_X = 240, 0xC0, 144, 224    # column 15; Y $C0; standing on row 11; column 14


@fax_e2e.recipe("eolis-8", parent="eolis-meat-shop")
def _eolis_8(g):
    """Out of Eolis's meat shop through its door edited to lead onto Eolis
    $08 at block $95 (Area_Saved*, which Game_ExitBuilding sets the outside up
    from), standing on the floor once the scroll has settled."""
    g.poke({SAVED_AREA: EOLIS, SAVED_SCREEN: GATE_SCREEN, SAVED_PALETTE: 0, SAVED_POS: 0x95, AREA_REGION: EOLIS})
    g.wait(3)
    for _ in range(60):
        if g.ram(AREA) != BUILDINGS:
            break
        g.tcp("action", name="right", frames=10)
        g.wait(10)
    g.until("Eolis $08", lambda: (g.ram(AREA), g.ram(SCREEN)) == (EOLIS, GATE_SCREEN), 600)
    g.until("the scroll to end", lambda: g.ram(0x54) == 0xFF, 600)   # Screen_ScrollDirection
    g.wait(20)


def hero():
    return ram(SCREEN), ram(HERO_X), ram(HERO_Y)


def press_until(button, what, test, frames=60, tries=5):
    """Press `button` until `test` holds; a press that lands before the game
    reads the pad is lost on a loaded machine, so one that changed nothing in
    `frames` is pressed again."""
    for _ in range(tries):
        tcp("action", name=button, frames=4)
        try:
            until(what, test, frames)
            return
        except AssertionError:
            pass
    raise AssertionError(f"{what}: {tries} presses of {button} did nothing")


def use_wing_boots():
    """Down+B: "I've used Wing Boots.", closed with A; then flying."""
    for _ in range(5):
        tcp("action", name="down", frames=20)
        wait(4)
        tcp("action", name="b", frames=4)
        try:
            until("I've used Wing Boots.", lambda: ram(TEXTBOX_CLOSED) == 0, 60)
            break
        except AssertionError:
            pass
    until_typed("the line to type out", 600)
    press_until("a", "the box to close", lambda: ram(TEXTBOX_CLOSED) != 0)
    until("flying", lambda: ram(STATUS) & FLYING, 60)


def fly(steps):
    """Fly with A held throughout: for each (direction, done) hold the
    direction (with Up kept held to stay high while moving sideways) until
    done(), bounded by frames. Each button is its own pad hold, renewed
    before it lapses."""
    holds = {}

    def keep(buttons):
        now = fax_e2e.frame()
        for b in ("a", "dpup", "dpleft", "dpright"):
            if b in buttons and now - holds.get(b, -1000) > 20:
                tcp("pad", buttons=b, frames=40)
                holds[b] = now
            elif b not in buttons and b in holds:
                tcp("pad", buttons=b, frames=1)
                del holds[b]

    for buttons, what, done in steps:
        end = fax_e2e.frame() + 900
        while not done():
            assert fax_e2e.frame() < end, f"flying: timed out waiting for {what}"
            keep(buttons)
    keep(set())


def into_the_shaft():
    """Up to the sky, right over the wall to the edge, let go: down column 15
    to the bottom."""
    fly([({"a", "dpup"}, "the sky", lambda: ram(HERO_Y) <= 8),
         ({"a", "dpup", "dpright"}, "the screen's edge", lambda: ram(HERO_X) >= SHAFT_X)])
    until("the bottom of the shaft", lambda: (ram(HERO_X), ram(HERO_Y)) == (SHAFT_X, BOTTOM_Y), 300)


def under_the_floor():
    """Hold Left until he stops: the screens he passes, each with the lowest
    X reached, all at Y $C0."""
    track, last = [], None
    end = fax_e2e.frame() + 1500
    while fax_e2e.frame() < end:
        tcp("action", name="left", frames=12)
        wait(12)
        here = hero()
        assert here[2] == BOTTOM_Y, here
        if not track or track[-1][0] != here[0]:
            track.append([here[0], here[1]])
        track[-1][1] = min(track[-1][1], here[1])
        if here == last:
            return [tuple(t) for t in track]
        last = here
    raise AssertionError(f"still walking under the floor: {track}")


def end_flight():
    """The flight's last second, then "The power of the Wing Boots is gone.", closed with A."""
    fax_e2e._current().poke({DURATION_WING_BOOTS: 0})
    until("the power gone", lambda: ram(TEXTBOX_CLOSED) == 0, 200)
    until_typed("the line to type out", 600)
    press_until("a", "the box to close", lambda: ram(TEXTBOX_CLOSED) != 0)
    until("not flying", lambda: not ram(STATUS) & FLYING, 60)


def main():
    args = parse_args(__doc__, parts=PARTS)
    game = Game.from_args(args)
    try:
        for part in PARTS:
            if not args.wants(part):
                continue
            original = part == "original"
            game.set_features({PACKAGE: not original})
            game.launch(at="eolis-8")
            game.poke({SELECTED_ITEM: WING_BOOTS, HP: 0x50, **{ENTITIES + i: 0xFF for i in range(8)}})
            wait(5)
            use_wing_boots()
            into_the_shaft()
            game.shot(f"{part}-in-the-shaft")
            end_flight()
            assert hero() == (GATE_SCREEN, SHAFT_X, BOTTOM_Y), hero()
            if original:
                stuck = game.save("stuck")
                for button in ("a", "up", "right"):
                    game.load(stuck)
                    tcp("action", name=button, frames=60)
                    wait(70)
                    assert hero() == (GATE_SCREEN, SHAFT_X, BOTTOM_Y), (button, hero())
                game.load(stuck)
                track = under_the_floor()
                assert [s for s, _ in track] == [GATE_SCREEN, TOWN_SCREEN], track
                for _ in range(3):
                    tcp("action", name="a", frames=20)
                    wait(30)
                    assert ram(HERO_Y) == BOTTOM_Y, hero()
                here = list(ram(ENTITIES, 8))
                assert all(b == 0xFF or NPCS[0] <= b <= NPCS[1] for b in here), f"no enemy on the screen: {here}"
                assert ram(SELECTED_ITEM) == 0xFF, "the Wing Boots are used up"
                game.shot("original-stuck")
                print(f"ok: the original leaves the hero under Eolis's floor once the flight ends: A, Up and Right "
                      f"do nothing, Left walks him under the floor to the town's edge ({track}) and no further, "
                      f"never above Y $C0; the Wing Boots are gone and no enemy comes")
            else:
                # The cooldown starts on the main loop's next pass of GameLoop_CountdownItems after the box.
                until("the cooldown to start", lambda: not tcp("recharge_state")["boots_running"], 120)
                state = tcp("recharge_state")
                assert ram(SELECTED_ITEM) == WING_BOOTS and not state["boots_ready"], state
                assert state["boots_cooldown"] > 1, state
                game.poke({}, records={"faxanadu.recharge": {2: 1}})       # the cooldown's last second
                until("the pair ready", lambda: tcp("recharge_state")["boots_ready"], 200)
                use_wing_boots()
                fly([({"a", "dpup"}, "the sky", lambda: ram(HERO_Y) <= 8),
                     ({"a", "dpup", "dpleft"}, "over the wall", lambda: ram(HERO_X) <= WALL_X - 40)])
                until("on Eolis's floor", lambda: ram(HERO_Y) == FLOOR_Y and not ram(FLAGS) & 0x05, 300)
                assert ram(SCREEN) == GATE_SCREEN and ram(HERO_X) < WALL_X, hero()
                game.shot("remastered-out")
                print("ok: with the remaster the Wing Boots are kept and recharge: Down+B flies him up the shaft "
                      "and over the wall, back onto Eolis's floor")
            game.close()
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
