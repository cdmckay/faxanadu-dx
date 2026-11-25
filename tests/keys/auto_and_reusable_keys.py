"""Play auto keys and reusable tools end to end in a hidden window.

    python3 tests/keys/auto_and_reusable_keys.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/keys

The game runs uncapped and the script waits on the game's own state, never on
the clock, and takes a free TCP port, so any number can run in parallel.

Copies the build to OUT and starts each part at Eolis's exit door, from a
fixture that comes in by a door (keys-eolis-exit), and save-state RAM edits
then set up what the scenarios need (external prerequisites, not coverage):
full HP, the J key and the Mattock in the inventory, the screen's enemies
cleared before each walk, and, to reach another screen of the same area,
Area_ScreenToTheRight pointed at it, so that walking off the edge loads it the
game's own way. The Mattock is selected in the item menu, as a player would.

1. The remaster on (the default). Without a J key, Up at Eolis's exit door
   gives the door's own "mark of Jack" and the door stays shut. With the J key
   unselected and the Mattock selected, the door opens with "I've used key.",
   and afterwards the key is still in the inventory and the Mattock still
   selected. The same key then opens a second door, Trunk's tower door. The
   Mattock breaks the wall beside the tower and is still selected.
2. A selected key opens the door the game's own way, and stays selected.
3. The remaster off, the original: the unselected key leaves the door shut;
   selected, it opens the door and is used up. The Mattock is used up too.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
import fax_e2e  # noqa: E402
from fax_e2e import Game, act, fixture, hold, menu_row, parse_args, ram, tcp, type_out, until, wait  # noqa: E402

AREA, SCREEN, SCREEN_TO_THE_RIGHT, PLAYER_X, PLAYER_X_FRAC = 0x24, 0x63, 0x67, 0x9E, 0x9D
INVINCIBILITY, SCREEN_READY, SCROLL_DIRECTION, INTERRUPTS = 0xAD, 0x0E, 0x54, 0x13
MESSAGE_ID, HP, DURATION_OINTMENT = 0x213, 0x431, 0x427
AREA_SAVED_SCREEN, PLAYER_SAVED_POS = 0x3D6, 0x3D8
ITEM_INVENTORY, SELECTED_ITEM, NUMBER_OF_ITEMS = 0x3AD, 0x3C1, 0x3C6
SPRITE_ENTITIES, SCREEN_BUFFER = 0x2CC, 0x600                       # CurrentSprites_Entities, 8 slots
KEY_J, MATTOCK, NO_ITEM = 0x07, 0x09, 0xFF
USED_KEY, MARK_OF_JACK, USED_MATTOCK = 0xAE, 0x29, 0xAB              # message IDs
EOLIS, TRUNK, BUILDINGS = 0, 1, 4
EOLIS_EXIT = 0x08                                                   # the exit door: x=12, a J key
EXIT_DOOR_POS, EXIT_DOOR_X = 0x9C, 192                              # standing at it: block YX, Player_PosX_Block
TOWER_DOOR, WALL, TOWER = 0x0B, 0x0C, 0x0D                          # Trunk: a J door; the Mattock wall
WALL_BLOCKS = (0xA3, 0xB3)                                          # (3,10), (3,11): block $63 in screen $0C
WALL_BLOCK = 0x63
WALL_STOP = 0x20                                                    # the hero's x up against it
PACKAGE, FEATURE = "faxanadu.dx", "dx"                          # the one switch: off is the original game


def typed():
    """The message has typed out and the box waits for A: MessageID clears.
    The door and Mattock lines have no pause, and TextBox_MessagePaused can
    hold a stale value from the intro, so it is not consulted."""
    return ram(MESSAGE_ID) == 0


def place():
    return ram(AREA), ram(SCREEN)


def items():
    """The unselected items, in order, and the selected one."""
    return list(ram(ITEM_INVENTORY, 8)[:ram(NUMBER_OF_ITEMS)]), ram(SELECTED_ITEM)


@fax_e2e.recipe("keys-eolis-exit", parent="eolis-meat-shop")
def _eolis_exit(g):
    """At Eolis's exit door (screen 8, x 192), the screen's Monodron cleared.
    He walks out of the meat shop, its door edited to lead there
    (Area_SavedScreen 8, Player_SavedPos $9C), so the screen loads the game's
    own way, with the remaster on or off. The Monodron loads on the door's
    own block, so he walks out under Ointment, which no touch gets through,
    until it is cleared. A walk from the town is no way there: in Seamless the
    Monodron runs before he arrives and waits at the seam, and a touch as he
    crosses knocks him back into the town (#130)."""
    g.poke({AREA_SAVED_SCREEN: EOLIS_EXIT, PLAYER_SAVED_POS: EXIT_DOOR_POS, DURATION_OINTMENT: 0x40})
    hold("right", lambda: g.ram(AREA) != BUILDINGS)
    g.until("the exit door's screen", lambda: (g.ram(AREA), g.ram(SCREEN)) == (EOLIS, EOLIS_EXIT) and
            (g.ram(SCREEN_READY), g.ram(SCROLL_DIRECTION)) == (0, 0xFF) and g.ram(INTERRUPTS) != 0, 300)
    g.poke({DURATION_OINTMENT: 0xFF, PLAYER_X: EXIT_DOOR_X, PLAYER_X_FRAC: 0,
            **{SPRITE_ENTITIES + slot: 0xFF for slot in range(8)}})
    g.wait(10)
    assert (g.ram(AREA), g.ram(SCREEN), g.ram(PLAYER_X), g.ram(INVINCIBILITY)) == (EOLIS, EOLIS_EXIT, EXIT_DOOR_X, 0)
    assert list(g.ram(SPRITE_ENTITIES, 8)) == [0xFF] * 8, "the Monodron cleared"


PARTS = ("no-key", "unselected", "selected", "original")            # each its own game


def main():
    args = parse_args(__doc__, parts=PARTS)
    game = Game.from_args(args)
    out = game.out

    def game_up():
        """The game with the remaster on: the one running, if it is."""
        if not game.running():
            launch()

    def launch(dx=True, at=None):
        """Start the game with the remaster, and so both features, on or off."""
        game.set_features({(PACKAGE, FEATURE): dx})
        game.launch(at=at)
        state = tcp("keys_state")
        assert (state["auto"], state["reuse"]) == (dx, dx), state

    def clear_enemies():
        game.poke({SPRITE_ENTITIES + slot: 0xFF for slot in range(8)})

    def walk(direction, test, what, steps=200):
        if hold(direction, test, steps * 4):
            return
        raise AssertionError(f"walking {direction} never reached {what}: at {place()}, x={ram(PLAYER_X)}")

    def enter(direction, area, screen):
        """Walk off the screen's edge into `screen`, wait for the scroll, clear its enemies."""
        walk(direction, lambda: place() == (area, screen), f"screen ${screen:02X}")
        settled = (lambda: ram(PLAYER_X) < 0x20) if direction == "right" else (lambda: ram(PLAYER_X) >= 0xE0)
        until("the scroll", settled, 300)
        wait(10)
        clear_enemies()

    def walk_to(x, direction="right"):
        if direction == "right":
            walk(direction, lambda: ram(PLAYER_X) >= x, f"x={x}")
        else:
            walk(direction, lambda: ram(PLAYER_X) <= x, f"x={x}")

    def press_up(expect):
        """Up at a door; returns the message it shows, once typed out."""
        tcp("action", name="up", frames=4)
        seen = []

        def shown():
            seen.append(ram(MESSAGE_ID))
            return seen[-1] in expect
        until(f"one of messages {[hex(m) for m in expect]}", shown, 300)
        type_out("the message to type out", 600, pauses=False)
        return seen[-1]

    def dismiss_into(area, screen):
        act("a", 6, 10)
        until(f"area {area} screen ${screen:02X}", lambda: place() == (area, screen) and ram(PLAYER_X) < 0xF0, 900)
        wait(40)

    def dismiss_staying():
        here = place()
        act("a", 6, 40)
        until("the message box to close", lambda: ram(MESSAGE_ID) == 0, 300)
        wait(60)
        assert place() == here, f"the door stayed shut: {place()} != {here}"

    def item_menu():
        act("select", 4, 30)
        for _ in range(menu_row("ITEM")):
            act("down", 4, 8)
        act("a", 4, 30)

    def select(item):
        """Choose `item` in the item menu, as a player would."""
        listed, _ = items()
        item_menu()
        for _ in range(listed.index(item)):
            act("down", 4, 8)
        act("a", 4, 60)
        assert items()[1] == item, items()

    def break_wall():
        """Walk right up against the wall, then Down-B: the Mattock's message, and the wall crumbles."""
        assert [ram(SCREEN_BUFFER + b) for b in WALL_BLOCKS] == [WALL_BLOCK] * 2, "the wall is there"
        walk_to(WALL_STOP)
        for _ in range(3):
            act("right", 4, 4)
        assert ram(PLAYER_X) == WALL_STOP, f"the wall stops him at x={WALL_STOP}, not {ram(PLAYER_X)}"
        tcp("action", name="down", frames=20)
        wait(6)
        tcp("action", name="b", frames=4)
        until("I've used Mattock.", lambda: ram(MESSAGE_ID) == USED_MATTOCK, 300)
        type_out("the message to type out", 600, pauses=False)
        act("a", 6, 10)
        until("the wall to crumble", lambda: ram(SCREEN_BUFFER + WALL_BLOCKS[0]) != WALL_BLOCK, 300)
        wait(30)

    def open_eolis_exit(expect):
        """Up at Eolis's exit door, where the part's fixture stands him."""
        assert (place(), ram(PLAYER_X)) == ((EOLIS, EOLIS_EXIT), EXIT_DOOR_X), (place(), ram(PLAYER_X))
        return press_up(expect)

    try:
        # The hero at Eolis's exit door, with the J key and the Mattock, or the Mattock alone.
        door = game.state("keys-eolis-exit")
        keys = out / "keys.cycstate"
        fixture(door, keys, {HP: 0x50, ITEM_INVENTORY: KEY_J, ITEM_INVENTORY + 1: MATTOCK,
                             NUMBER_OF_ITEMS: 2, SELECTED_ITEM: NO_ITEM})
        no_key = out / "no-key.cycstate"
        fixture(door, no_key, {HP: 0x50, ITEM_INVENTORY: MATTOCK, NUMBER_OF_ITEMS: 1, SELECTED_ITEM: NO_ITEM})

        if args.wants("no-key"):
            game_up()
            # 1. The remaster on. Without the key, the door's own message.
            tcp("load_state", path=str(no_key))
            select(MATTOCK)
            assert open_eolis_exit({MARK_OF_JACK, USED_KEY}) == MARK_OF_JACK
            game.shot("no-key-mark-of-jack")
            dismiss_staying()
            print("ok: without a J key, Up at Eolis's exit door says there is the mark of Jack, and it stays shut")

        if args.wants("unselected"):
            game_up()
            # The J key unselected, the Mattock selected: the door opens.
            tcp("load_state", path=str(keys))
            select(MATTOCK)
            assert items() == ([KEY_J], MATTOCK), items()
            assert open_eolis_exit({MARK_OF_JACK, USED_KEY}) == USED_KEY
            game.shot("door-opens-key-unselected")
            dismiss_into(TRUNK, 0)
            assert items() == ([KEY_J], MATTOCK), items()
            state = tcp("keys_state")
            assert (state["auto_opened"], state["keys_kept"], state["lent"]) == (1, 1, False), state
            clear_enemies()
            item_menu()
            game.shot("key-still-in-inventory")
            act("b", 4, 30)
            assert items() == ([KEY_J], MATTOCK), items()
            print("ok: the unselected J key opened Eolis's exit door with \"I've used key.\"; "
                  "the key is still in the inventory and the Mattock still selected")

            # The same key opens a second door: Trunk's tower door.
            game.poke({SCREEN_TO_THE_RIGHT: TOWER_DOOR})
            enter("right", TRUNK, TOWER_DOOR)
            walk_to(138)
            assert press_up({MARK_OF_JACK, USED_KEY}) == USED_KEY
            game.shot("second-door-same-key")
            dismiss_into(TRUNK, TOWER)
            assert items() == ([KEY_J], MATTOCK), items()
            state = tcp("keys_state")
            assert (state["auto_opened"], state["keys_kept"]) == (2, 2), state
            print("ok: the same J key opened a second door, Trunk's tower, and is still in the inventory")

            # Out of the tower (no key needed) and on to the wall: the Mattock breaks it.
            clear_enemies()
            walk_to(8, "left")
            act("up", 4, 10)
            until("Trunk's tower door screen", lambda: place() == (TRUNK, TOWER_DOOR) and ram(PLAYER_X) < 0xF0, 900)
            wait(30)
            clear_enemies()
            enter("right", TRUNK, WALL)
            break_wall()
            game.shot("wall-broken-mattock-kept")
            assert [ram(SCREEN_BUFFER + b) for b in WALL_BLOCKS] != [WALL_BLOCK] * 2
            assert items() == ([KEY_J], MATTOCK), items()
            assert tcp("keys_state")["mattock_kept"] == 1
            walk_to(64)                                                 # through where the wall was
            print("ok: the Mattock broke the wall in Trunk and is still selected; the hero walks through")
            game.close()

        if args.wants("selected"):
            launch()
            # 2. A selected key: the game's own check opens the door, and the key stays selected.
            tcp("load_state", path=str(keys))
            select(KEY_J)
            assert open_eolis_exit({MARK_OF_JACK, USED_KEY}) == USED_KEY
            dismiss_into(TRUNK, 0)
            assert items() == ([MATTOCK], KEY_J), items()
            state = tcp("keys_state")
            assert (state["auto_opened"], state["keys_kept"]) == (0, 1), state
            print("ok: a selected J key opens the door and stays selected")
            game.close()

        if args.wants("original"):
            launch(dx=False)
            # 3. The remaster off: the original.
            tcp("load_state", path=str(keys))
            select(MATTOCK)
            assert open_eolis_exit({MARK_OF_JACK, USED_KEY}) == MARK_OF_JACK
            dismiss_staying()
            select(KEY_J)
            assert items() == ([MATTOCK], KEY_J), items()
            assert press_up({MARK_OF_JACK, USED_KEY}) == USED_KEY
            dismiss_into(TRUNK, 0)
            assert items() == ([MATTOCK], NO_ITEM), items()
            clear_enemies()
            select(MATTOCK)
            game.poke({SCREEN_TO_THE_RIGHT: WALL})
            enter("right", TRUNK, WALL)
            break_wall()
            game.shot("off-mattock-used-up")
            assert items() == ([], NO_ITEM), items()
            state = tcp("keys_state")
            assert (state["auto_opened"], state["keys_kept"], state["mattock_kept"]) == (0, 0, 0), state
            print("ok: with the remaster off, the unselected key leaves the door shut, and a selected key and the "
                  "Mattock are used up, as in the original")
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
