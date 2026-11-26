"""Play B backing out of menus and text (#160) end to end in a hidden window.

    python3 tests/controls/b_backs_out.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/back
    python3 tests/controls/b_backs_out.py ... --part shop

The game runs uncapped and the script waits on the game's own state, never on
the clock, on a reserved TCP port (tests/e2e/fax_e2e.py). Controller input
goes in where a player's does: TCP `pad` holds buttons of an SDL virtual
controller. Its B is Use item in play and the NES B outside it, each press
one or the other (use_item_state's presses and backs); its X is the NES B
everywhere, the host's binding. Seven parts, each its own game, all but the
last in Seamless:

menu      From Eolis town, a Hand Dagger in his weapons and a Red Potion
          selected: Start opens the Select menu and B closes it; B closes the
          weapons list and the PLAYER page; X still closes the list. B closes
          the map (Back) and the quest log (the left trigger), which pause.
          Each B is a back press and none a press of Use item: the potion
          stays. B held as the menu closes stays up in play: no item, no
          sword, until it is let go; the next press uses the potion.
shop      In Eolis's meat shop: B on the Meat Seller's question closes it,
          nothing taken, and so does X. Next door, B leaves the key shop's buy
          list, and so does X.
title     From power-on, on the title's menu: A opens NEW GAME's pick of
          files, and B goes back to the two choices; X does too.
guru      In Conflate's temple, other Gurus visited: A through the Guru's
          words and his menu (which saves), and B on Guru travel's offer ends
          the conversation where it is.
play      From Eolis town, a Red Potion selected: B uses it without swinging
          the sword, and held through the potion's line, never closes it.
door      At Eolis's exit door without its J key, Deluge his spell and 80 MP,
          Up held: the door's line ("mark of Jack"). A ends it, and B goes down
          the frame after, at the read its script's end waits for (a save
          state made headless, frame exact), so the pass that goes on to the
          cast still has it new: B is a back press, taken out of that pass,
          and nothing is cast. X there casts Deluge, as Up+B does.
original  With the remaster off (the stock view): Back opens the original's
          menu; B leaves it open and X closes it.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "movement"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "keys"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "travel"))
from fax_e2e import (FILES_NEW, FILES_TOP, act, batch, box_open, hold, page_to, parse_args, ram, shop_list,  # noqa: E402
                     tcp, title_menu, type_out, until, wait, walk)
import auto_and_reusable_keys  # noqa: E402,F401  (registers the fixture "keys-eolis-exit")
import guru_travel  # noqa: E402,F401  (registers the fixture "travel-conflate-temple")
from play import Game  # noqa: E402  (fax_e2e's, with headless runs)

AREA, SCREEN, SCREEN_READY, PLAYER_X, PLAYER_FLAGS, PAUSED = 0x24, 0x63, 0x0E, 0x9E, 0xA4, 0x120
TEXTBOX_X, TEXTBOX_CLOSED, MESSAGE_ID, STRINGS_COUNT = 0x208, 0x20C, 0x213, 0x21F
MENU_CURSOR, CAST_MAGIC_TYPE = 0x21E, 0x2B3
GOLD, MP, WEAPONS, MAGIC_INVENTORY = 0x392, 0x39A, 0x39D, 0x3A9
ITEM_INVENTORY, SELECTED_MAGIC, SELECTED_ITEM = 0x3AD, 0x3C0, 0x3C1
NUMBER_OF_WEAPONS, NUMBER_OF_MAGIC_SPELLS, NUMBER_OF_ITEMS, PORTRAIT = 0x3C2, 0x3C5, 0x3C6, 0x3C7
AREA_SAVED_SCREEN, PLAYER_SAVED_POS = 0x3D6, 0x3D8                  # where a building's way out leads
ATTACKING = 0x80                                                     # Player_Flags
HAND_DAGGER, RED_POTION, DELUGE, NO_ITEM, NO_SPELL, NO_PORTRAIT = 0x00, 0x10, 0, 0xFF, 0xFF, 0xFF
BUILDINGS, KEY_SHOP_DOOR = 4, 0x9D
MENU_WIDTH, LIST_WIDTH, PLAYER_WIDTH = 0x0A, 0x14, 0x1A              # PlayerMenu_Show's, _ShowSubmenu's, _ShowStatusMenu's
DRIED_MEAT_50, MARK_OF_JACK = 0x07, 0x29                             # the Meat Seller's question; the J door's line
MEDITATE_WITH_YOU, OFFER, CONFLATE_TEMPLE = 0x22, 1, 0x2B            # the Guru's greeting; fax_travel.c's offer
PACKAGE = ("faxanadu.dx", "dx")
PARTS = ("menu", "shop", "title", "guru", "play", "door", "original")


def state():
    return tcp("use_item_state")


def press(buttons, frames=4):
    """Buttons of the virtual controller."""
    tcp("pad", buttons=buttons, frames=frames)


def menu_open(rows=8):
    return box_open() and ram(TEXTBOX_X + 2) == MENU_WIDTH and ram(STRINGS_COUNT) == rows


def done_talking():
    return ram(PORTRAIT) == NO_PORTRAIT and not box_open()


def main():
    args = parse_args(__doc__, parts=PARTS)
    game = Game.from_args(args)

    def walks():
        """Back in play: Left moves the hero."""
        x = ram(PLAYER_X)
        assert hold("left", lambda: ram(PLAYER_X) != x, 120), "the hero does not move"

    def counts(backs, presses=0):
        s = state()
        assert (s["backs"], s["presses"]) == (backs, presses), s

    def open_menu(rows=8):
        press("start")
        until("the menu", lambda: menu_open(rows) and ram(0x100) == 12, 300)
        wait(4)

    def menu_closed(what):
        """The box closed and the game reading the pad in play again: a press
        made while the box's last writes drain is never read."""
        until(f"{what} to close", lambda: not box_open() and state()["read"] == "play", 120)
        wait(10)

    def to_row(row):
        while ram(MENU_CURSOR) != row:
            act("down" if (row - ram(MENU_CURSOR)) % 8 <= 4 else "up", 4, 6)

    def never(what, test, frames):
        end = game.frame() + frames
        while game.frame() < end:
            assert not test(), what

    try:
        if args.wants("menu"):
            game.launch(at="eolis-town")
            game.poke({WEAPONS: HAND_DAGGER, NUMBER_OF_WEAPONS: 1, ITEM_INVENTORY: RED_POTION, NUMBER_OF_ITEMS: 1,
                       SELECTED_ITEM: RED_POTION})
            wait(10)
            assert state()["read"] == "play", state()
            open_menu()
            assert state()["read"] == "menu", state()
            press("b")
            menu_closed("the menu")
            counts(1)
            walks()
            print("ok: B closes the Select menu, a back press, not Use item")

            open_menu()
            to_row(2)                                                   # WEAPON
            act("a", 4, 6)
            until("the weapons list", lambda: ram(TEXTBOX_X + 2) == LIST_WIDTH and box_open(), 300)
            wait(20)
            press("b")
            menu_closed("the list")
            counts(2)
            open_menu()
            to_row(7)                                                   # PLAYER
            act("a", 4, 6)
            until("the PLAYER page", lambda: ram(TEXTBOX_X + 2) == PLAYER_WIDTH and box_open(), 300)
            wait(20)
            press("b")
            menu_closed("the PLAYER page")
            counts(3)
            open_menu()
            to_row(2)
            act("a", 4, 6)
            until("the weapons list", lambda: ram(TEXTBOX_X + 2) == LIST_WIDTH and box_open(), 300)
            wait(20)
            press("x")
            menu_closed("the list")
            counts(3)
            walks()
            print("ok: B closes the weapons list and the PLAYER page; X still closes the list")

            for n, (button, screen, cmd) in enumerate((("back", "the map", "map_state"),
                                                       ("lefttrigger+", "the quest log", "quest_state")), 4):
                press(button)
                until(screen, lambda: tcp(cmd)["open"] and ram(PAUSED) != 0, 120)
                wait(10)
                press("b")
                until(f"{screen} to close", lambda: not tcp(cmd)["open"] and ram(PAUSED) == 0 and
                      state()["read"] == "play", 120)
                wait(10)
                counts(n)
            walks()
            print("ok: B closes the map and the quest log, which pause")

            open_menu()
            press("b", 240)
            menu_closed("the menu")
            assert state()["pad"] == "done", state()
            never("B held swung the sword", lambda: ram(PLAYER_FLAGS) & ATTACKING, 40)
            assert ram(SELECTED_ITEM) == RED_POTION
            counts(6)
            until("B let go", lambda: state()["pad"] == "up", 300)
            press("b")
            until("the potion's line", lambda: ram(MESSAGE_ID) != 0 and box_open(), 120)
            counts(6, 1)
            assert state()["shown"] == 1, state()
            print("ok: B held as the menu closes uses nothing in play, and the next press uses the potion")
            game.close()

        if args.wants("shop"):
            game.launch(at="eolis-meat-shop")
            game.poke({GOLD: 500 & 0xFF, GOLD + 1: 500 >> 8, GOLD + 2: 0})
            walk("left")                                                # to the counter: held until he stops

            def question():
                for _ in range(5):                                      # Up counts only on the frame it goes down
                    act("up", 4, 10)
                    try:
                        until("the Meat Seller", lambda: ram(PORTRAIT) != NO_PORTRAIT, 120)
                        break
                    except AssertionError:
                        pass
                else:
                    raise AssertionError("the Meat Seller does not answer")
                until("his question", lambda: ram(MESSAGE_ID) == DRIED_MEAT_50, 300)
                type_out("his question", 900, pauses=False)

            for button in ("b", "x"):
                question()
                press(button)
                until("the box to close", done_talking, 300)
                assert int.from_bytes(ram(GOLD, 3), "little") == 500, "nothing taken"
            counts(1)
            print("ok: B on the Meat Seller's question closes it, nothing taken, and so does X")

            # The key shop next door (screen 3, its door at block $9D): out of the meat shop onto it, and in.
            game.poke({AREA_SAVED_SCREEN: 3, PLAYER_SAVED_POS: KEY_SHOP_DOOR})
            hold("right", lambda: ram(AREA) != BUILDINGS)
            until("Eolis screen 3", lambda: (ram(AREA), ram(SCREEN)) == (0, 3) and ram(SCREEN_READY) == 0, 600)
            wait(30)
            column = (KEY_SHOP_DOOR & 15) * 16
            for _ in range(200):
                x = ram(PLAYER_X)
                if column - 6 <= x <= column + 4:
                    break
                act("right" if x < column - 6 else "left", 2, 2)
            for _ in range(3):                                          # Up counts only on the frame it goes down
                act("up", 4, 10)
                try:
                    until("the key shop", lambda: ram(AREA) == BUILDINGS, 120)
                    break
                except AssertionError:
                    pass
            wait(40)
            walk("left", lambda: ram(PLAYER_X) <= 80)                   # he stands at block $84, x 64

            def buy_list():
                shop_list("buy")                                        # his greeting, Buy, its line (Quick shops)
                wait(2)

            for button in ("b", "x"):
                buy_list()
                press(button)
                until("the shop to end", done_talking, 300)
                assert not tcp("shops_state")["list_up"]
            counts(2)
            print("ok: B leaves the key shop's buy list, and so does X")
            game.close()

        if args.wants("title"):
            game.launch()
            assert title_menu()["mode"] == FILES_TOP
            for n, button in enumerate(("b", "x")):
                act("a")
                until("NEW GAME's pick", lambda: tcp("files_state")["mode"] == FILES_NEW, 120)
                wait(4)
                assert state()["read"] == "menu", state()
                press(button)
                until("the two choices", lambda: tcp("files_state")["mode"] == FILES_TOP, 120)
            counts(1)
            print("ok: on the title, B goes back from NEW GAME's pick of files to the two choices, and so does X")
            game.close()

        if args.wants("guru"):
            temple = game.state("travel-conflate-temple")
            (game.out / "game" / "faxanadu_gurus.txt").write_text("Eolis\nApolune\nMascon\nConflate\n")
            game.launch()
            tcp("load_state", path=str(temple))
            act("left", 170, 10)
            act("up", 4, 10)
            until("the Guru's greeting", lambda: ram(MESSAGE_ID) == MEDITATE_WITH_YOU)
            page_to(lambda s: s["text"] == OFFER, "Guru travel's offer")   # A on his menu remembers the journey
            press("b")
            until("the conversation to end", done_talking, 600)
            assert tcp("travel_state")["dest"] == -1 and (ram(AREA), ram(SCREEN)) == (BUILDINGS, CONFLATE_TEMPLE)
            counts(1)
            print("ok: B on Guru travel's offer ends the conversation where it is")
            game.close()

        if args.wants("play"):
            game.launch(at="eolis-town")
            game.poke({ITEM_INVENTORY: RED_POTION, NUMBER_OF_ITEMS: 1, SELECTED_ITEM: RED_POTION})
            wait(10)
            press("b", 900)
            until("the press", lambda: state()["shown"] == 1, 60)
            assert not ram(PLAYER_FLAGS) & ATTACKING, "B swung the sword"
            until("the potion's line", lambda: ram(MESSAGE_ID) != 0 and box_open(), 120)
            until("it typed out", lambda: ram(MESSAGE_ID) == 0, 600)
            never("B held closed the potion's line", lambda: not box_open(), 60)
            assert state()["pad"] == "done", state()
            counts(0, 1)
            print("ok: B uses the selected Red Potion without swinging the sword, and held, never closes its line")
            game.close()

        if args.wants("door"):
            mods = game.mods("mods-on")
            game.launch(at="keys-eolis-exit")
            game.poke({MAGIC_INVENTORY: DELUGE, NUMBER_OF_MAGIC_SPELLS: 1, SELECTED_MAGIC: DELUGE, MP: 80})
            wait(10)
            tcp("action", name="up", frames=900)
            until("the door's line", lambda: ram(MESSAGE_ID) == MARK_OF_JACK, 300)
            until("it typed out", lambda: ram(MESSAGE_ID) == 0, 600)
            wait(2)
            line = game.save("line")
            # Headless, frame exact: A ends the line, and the state after that frame waits for its script's end.
            end = game.out / "end.cycstate"
            game.headless(line, [(0, "UP+A"), (1, "UP")], 1, mods, save=(0, end))
            casts = {}
            for button in ("b", "x"):
                batch(("load_state", dict(path=str(end))), ("action", dict(name="up", frames=60)),
                      ("pad", dict(buttons=button, frames=4)))
                until("the line to close", lambda: not box_open(), 120)
                wait(40)
                casts[button] = (ram(MP), ram(CAST_MAGIC_TYPE), state()["kept"], state()["backs"])
            assert casts["b"] == (80, NO_SPELL, 1, 1), casts
            assert casts["x"][0] < 80 and casts["x"][2:] == (1, 1), casts
            assert state()["presses"] == 0
            print("ok: B on the read the door's line ends at is a back press, and nothing is cast after it; X "
                  "there casts Deluge, as Up+B does")
            game.close()

        if args.wants("original"):
            game.set_features({PACKAGE: False})
            game.launch(at="eolis-town")
            wait(10)
            assert not state()["enabled"]
            press("back")
            until("the original's menu", lambda: menu_open(6), 120)
            wait(4)
            press("b")
            wait(30)
            assert menu_open(6), "B closed the original's menu"
            press("x")
            until("the original's menu to close", lambda: not box_open(), 120)
            print("ok: with the remaster off, B leaves the original's menu open, and X closes it")
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
