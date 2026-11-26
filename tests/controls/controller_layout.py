"""Play the controller layout (#147) end to end in a hidden window.

    python3 tests/controls/controller_layout.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/controls
    python3 tests/controls/controller_layout.py ... --part pad

The game runs uncapped and the script waits on the game's own state, never on
the clock, on a reserved TCP port (tests/e2e/fax_e2e.py). Controller input goes
in where a player's does: TCP `pad` holds buttons, or pushes a trigger, of an
SDL virtual controller, and `key` holds a key. Six parts, each its own game,
all but the last in Seamless:

pad        From Eolis town: the bindings are the layout's (Magic C / Y, Use
           item S / B, Block V / RT, Map M / BACK, Quest log L / LT, the Select
           menu RETURN / START). Start opens the Select menu, the game unpaused,
           and Start closes it again, back to play, no pause. Back opens the map
           and not the menu (the map's button keeps the NES Select up); Back
           closes it. The left trigger opens the quest log and closes it.
           Return opens the menu as Start does, and Backslash (the NES Select)
           still opens it too.
shop       In Eolis's key shop, the buy list up: Back, the NES Select outside
           play, opens no map and changes nothing, as Select in the
           original's shops (since #163 it no longer switches lists).
hands      From Eolis town, the Small Shield on and a Red Potion selected: the
           right trigger held raises Block (the hero stands), and let go lowers
           it. B uses the Red Potion (the selected item gone, its line typing)
           without swinging the sword; in the air, B does nothing (the item
           stays, the press lapses). S on the keyboard uses it too, and the
           stock Down+B still does.
switch-pro A virtual Switch Pro Controller (Nintendo's USB ids, which SDL
           gives Nintendo's labels): its lower face button jumps, as A does on
           every pad, though its print says B, with the launcher skipped. Read
           by its label it would be Use item.
migration  A config.ini an older build wrote (no layout): each binding still on
           its old default moves to the layout's, once (Block to RT, the map to
           Back, the quest log to LT; the engine's Menu to R3, Disk and the
           save states off the controller), and the ones the player changed
           stay (Magic on LB, Fast-forward on L3). Rewind's move to LB gives
           way to the player's Magic there: it keeps F8 alone. The file says
           layout 2 at once; set back to B in it, Block stays.
original   With the remaster off (the stock view): Start pauses and Start
           unpauses; Back opens the original's menu (six rows) and B closes it;
           B, S and the left trigger do nothing.
"""
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
from fax_e2e import (Game, act, box_open, hold, page_through, parse_args, ram, shop_list, tcp, type_out,  # noqa: E402
                     until, wait, walk)

AREA, SCREEN, SCREEN_READY, PLAYER_X, PLAYER_FLAGS, PAUSED = 0x24, 0x63, 0x0E, 0x9E, 0xA4, 0x120
AREA_SAVED_SCREEN, PLAYER_SAVED_POS = 0x3D6, 0x3D8                  # where a building's way out leads
BUILDINGS, KEY_SHOP_DOOR = 4, 0x9D
TEXTBOX_X, TEXTBOX_CLOSED, STRINGS_COUNT = 0x208, 0x20C, 0x21F         # X, Y, Width, Height; Arg_StringsCount
SELECTED_SHIELD, SELECTED_ITEM, ITEM_INVENTORY, NUMBER_OF_ITEMS = 0x3BF, 0x3C1, 0x3AD, 0x3C6
ATTACKING, JUMPING = 0x80, 0x01                                       # Player_Flags
SMALL_SHIELD, RED_POTION, NO_ITEM = 0x00, 0x10, 0xFF
MENU_WIDTH = 0x0A
PACKAGE = ("faxanadu.dx", "dx")
SWITCH_PRO = dict(vendor=0x057E, product=0x2009)                     # Nintendo's USB ids
PARTS = ("pad", "shop", "hands", "switch-pro", "migration", "original")

# What a build before the layout wrote, cut to the bindings: no BindingsLayout, no ControlsLayout. The player
# had moved Fast-forward to the left stick's press and Magic to the left shoulder.
LAYOUT1 = """[Display]
WindowScale = 1
[Keyboard.Shortcuts]
disk = D
menu = Escape
fast_forward = Tab
screenshot = F12
fullscreen = F11
state_menu = F7
rewind = F8
[Gamepad.Shortcuts]
disk = leftshoulder
menu = rightshoulder
fast_forward = leftstick
screenshot = none
fullscreen = none
state_menu = back+leftstick
rewind = back+rightstick
[Game]
ViewMode = 0
MagicKey = C
MagicButton = leftshoulder
BlockKey = V
BlockButton = b
MapKey = M
MapButton = lefttrigger+
QuestLogKey = L
QuestLogButton = rightstick
"""


def controls():
    return tcp("controls_state")


def bindings():
    return {name: tcp(cmd)["binding"] for name, cmd in (("magic", "magic_state"), ("use", "use_item_state"),
                                                        ("block", "block_state"), ("map", "map_state"),
                                                        ("quests", "quest_state"))}


def menu_open(rows=8):
    return ram(TEXTBOX_CLOSED) == 0 and ram(TEXTBOX_X + 2) == MENU_WIDTH and ram(STRINGS_COUNT) == rows


def main():
    args = parse_args(__doc__, parts=PARTS)
    game = Game.from_args(args)
    config = game.out / "game" / "config.ini"

    def walks():
        """Back in play: Left moves the hero."""
        x = ram(PLAYER_X)
        assert hold("left", lambda: ram(PLAYER_X) != x, 120), "the hero does not move"

    def press(what, frames=4, **kw):
        """A controller button or trigger, or a key (key=True)."""
        if kw.pop("key", False):
            tcp("key", name=what, frames=frames)
        else:
            tcp("pad", buttons=what, frames=frames, **kw)

    def potion_used(shot=None):
        """The Red Potion's line ("I've used..."), then, once it is read, the item gone and HP filled."""
        until("the potion's line", box_open, 60)
        type_out("the potion's line")
        if shot:
            game.shot(shot)
        page_through("the potion's line", lambda: ram(SELECTED_ITEM) == NO_ITEM, 900)

    def never_paused(frames):
        end = game.frame() + frames
        while game.frame() < end:
            assert ram(PAUSED) == 0, "the game paused"

    try:
        if args.wants("pad"):
            game.launch(at="eolis-town")
            wait(10)
            assert bindings() == {"magic": "C / Y", "use": "S / B", "block": "V / RT", "map": "M / BACK",
                                  "quests": "L / LT"}, bindings()
            c = controls()
            assert c["enabled"] and c["layout"] == 2 and c["menu_binding"] == "RETURN / START", c
            print("ok: Magic C / Y, Use item S / B, Block V / RT, Map M / BACK, Quest log L / LT, and the Select "
                  "menu RETURN / START")

            opened = tcp("select_menu_state")["opened"]
            press("start")
            until("the menu", lambda: menu_open(), 120)
            never_paused(20)
            assert tcp("select_menu_state")["opened"] == opened + 1 and controls()["starts"] == 1, controls()
            game.shot("start-menu")
            press("start")
            until("the menu to close", lambda: ram(TEXTBOX_CLOSED) != 0, 120)
            never_paused(30)
            assert controls()["closes"] == 1, controls()
            walks()
            print("ok: Start opens the Select menu and closes it, and the game never pauses")

            press("back")
            until("the map", lambda: tcp("map_state")["open"] and ram(PAUSED) != 0, 120)
            wait(10)
            assert tcp("select_menu_state")["opened"] == opened + 1, "Back opened the menu too"
            assert controls()["kept"] == 1, controls()
            game.shot("back-map")
            press("back")
            until("the map to close", lambda: not tcp("map_state")["open"] and ram(PAUSED) == 0, 120)
            wait(10)
            assert ram(TEXTBOX_CLOSED) != 0 and tcp("select_menu_state")["opened"] == opened + 1
            walks()
            print("ok: Back opens the map, not the Select menu, and closes it")

            press("lefttrigger+")
            until("the quest log", lambda: tcp("quest_state")["open"] and ram(PAUSED) != 0, 120)
            wait(10)
            game.shot("trigger-quest-log")
            press("lefttrigger+")
            until("the quest log to close", lambda: not tcp("quest_state")["open"] and ram(PAUSED) == 0, 120)
            walks()
            print("ok: the left trigger opens the quest log and closes it")

            press("Return", key=True)
            until("the menu from Return", lambda: menu_open(), 120)
            press("Return", key=True)
            until("the menu to close", lambda: ram(TEXTBOX_CLOSED) != 0, 120)
            never_paused(20)
            press("\\", key=True)
            until("the menu from Backslash", lambda: menu_open(), 120)
            assert tcp("select_menu_state")["opened"] == opened + 3
            act("b", 4, 6)
            until("the menu to close", lambda: ram(TEXTBOX_CLOSED) != 0, 120)
            print("ok: Return opens and closes the menu as Start does, and Backslash still opens it")
            game.close()

        if args.wants("shop"):
            game.launch(at="eolis-meat-shop")
            # The meat shop asks; the key shop next door (screen 3, its door at block $9D) has lists. Out of the
            # meat shop onto that door (Area_SavedScreen, Player_SavedPos), and in, with something to sell.
            game.poke({AREA_SAVED_SCREEN: 3, PLAYER_SAVED_POS: KEY_SHOP_DOOR, ITEM_INVENTORY: RED_POTION,
                       NUMBER_OF_ITEMS: 1})
            hold("right", lambda: ram(AREA) != BUILDINGS)
            until("Eolis screen 3", lambda: (ram(AREA), ram(SCREEN)) == (0, 3) and ram(SCREEN_READY) == 0, 600)
            wait(30)
            column = (KEY_SHOP_DOOR & 15) * 16
            for _ in range(200):
                x = ram(PLAYER_X)
                if column - 6 <= x <= column + 4:
                    break
                act("right" if x < column - 6 else "left", 2, 2)
            for _ in range(3):                                                   # Up counts only on the frame it goes down
                act("up", 4, 10)
                try:
                    until("the key shop", lambda: ram(AREA) == BUILDINGS, 120)
                    break
                except AssertionError:
                    pass
            wait(40)
            walk("left", lambda: ram(PLAYER_X) <= 80)                           # he stands at block $84, x 64

            before = shop_list("buy")
            waits = before["waits"]
            press("back")
            until("frames in the list", lambda: tcp("shops_state")["waits"] > waits + 20, 300)
            after = tcp("shops_state")
            game.shot("back-in-the-list")
            assert after["shown"] == "buy" and after["list_up"] and after["cursor"] == before["cursor"], after
            assert after["list"] == before["list"] and after["step"] == "buy", after
            assert not tcp("map_state")["open"] and ram(PAUSED) == 0
            print("ok: in a shop's list, Back (the NES Select there) opens no map and changes nothing")
            game.close()

        if args.wants("hands"):
            game.launch(at="eolis-town")
            game.poke({SELECTED_SHIELD: SMALL_SHIELD, SELECTED_ITEM: RED_POTION})
            wait(10)
            start = game.save("potion")

            press("righttrigger+", 40)
            until("Block up", lambda: tcp("block_state")["raised"], 30)
            x = ram(PLAYER_X)
            tcp("action", name="left", frames=10)
            wait(12)
            assert tcp("block_state")["raised"] and ram(PLAYER_X) == x, "raised, the hero stands"
            until("Block down", lambda: not tcp("block_state")["raised"], 60)
            print("ok: the right trigger held raises Block, and let go lowers it")

            used = tcp("use_item_state")
            press("b")
            until("the press", lambda: tcp("use_item_state")["shown"] == used["shown"] + 1, 60)
            assert not ram(PLAYER_FLAGS) & ATTACKING, "B swung the sword"
            potion_used("b-red-potion")
            print("ok: B uses the selected Red Potion, without swinging the sword")

            game.load(start)
            tcp("action", name="a", frames=20)                          # a jump: in the air
            until("in the air", lambda: ram(PLAYER_FLAGS) & JUMPING, 30)
            before = tcp("use_item_state")
            press("b", 2)
            until("the press to lapse", lambda: tcp("use_item_state")["lapsed"] > before["lapsed"], 30)
            assert ram(SELECTED_ITEM) == RED_POTION and tcp("use_item_state")["shown"] == before["shown"]
            print("ok: in the air, B uses nothing, as Down+B would not")
            wait(20)                                                    # A's hold over, or the state jumps again

            game.load(start)
            wait(10)
            press("S", key=True)
            potion_used()
            game.load(start)
            wait(10)
            tcp("action", name="down", frames=10)
            tcp("action", name="b", frames=4)
            potion_used()
            print("ok: S uses it too, and the stock Down+B still does")
            game.close()

        if args.wants("switch-pro"):
            game.launch(at="eolis-town")
            wait(10)
            game.poke({SELECTED_ITEM: RED_POTION})
            wait(4)
            press("a", 8, **SWITCH_PRO)                                 # its lower face button, printed B
            until("a jump", lambda: ram(PLAYER_FLAGS) & JUMPING, 30)
            wait(20)
            assert ram(SELECTED_ITEM) == RED_POTION and tcp("use_item_state")["presses"] == 0
            print("ok: a Switch Pro Controller's lower face button, printed B, jumps as A, and uses no item")
            game.close()

        if args.wants("migration"):
            game.state("eolis-town")                                    # made first, with its own config.ini
            config.write_text(LAYOUT1)
            game.launch(at="eolis-town")
            wait(10)
            assert bindings() == {"magic": "C / LB", "use": "S / B", "block": "V / RT", "map": "M / BACK",
                                  "quests": "L / LT"}, bindings()
            assert controls()["moved"] == 3 and controls()["unbound"] == 0, controls()
            text = config.read_text()                                   # written at start, before any play
            for line in ("BindingsLayout = 2", "ControlsLayout = 2", "BlockButton = righttrigger+",
                         "MapButton = back", "QuestLogButton = lefttrigger+", "MagicButton = leftshoulder",
                         "UseItemKey = S", "UseItemButton = b"):
                assert re.search(rf"^{re.escape(line)}$", text, re.M), (line, text)
            pads = text[text.index("[Gamepad.Shortcuts]"):text.index("[Game]")]
            assert pads.split("\n")[1:8] == ["disk = none", "menu = rightstick", "fast_forward = leftstick",
                                             "screenshot = none", "fullscreen = none", "state_menu = none",
                                             "rewind = none"], pads
            print("ok: an old config.ini moves each binding still on its old default to the layout's, once, and "
                  "keeps the player's (Magic on LB, Fast-forward on L3); Rewind gives LB way to Magic")
            game.close()
            config.write_text(re.sub(r"^BlockButton = righttrigger\+$", "BlockButton = b", config.read_text(),
                                     flags=re.M))
            game.launch()
            wait(10)
            assert tcp("block_state")["binding"] == "V / B" and controls()["moved"] == 0, controls()
            game.close()
            assert re.search(r"^BlockButton = b$", config.read_text(), re.M)
            print("ok: in a file of the layout, Block set back to B stays there")

        if args.wants("original"):
            game.set_features({PACKAGE: False})
            game.launch(at="eolis-town")
            wait(10)
            assert not controls()["enabled"] and not tcp("use_item_state")["enabled"]
            press("start")
            until("the pause", lambda: ram(PAUSED) != 0, 60)
            assert ram(TEXTBOX_CLOSED) != 0, "Start opened a menu"
            wait(10)
            press("start")
            until("the game to go on", lambda: ram(PAUSED) == 0, 60)
            press("back")
            until("the original's menu", lambda: menu_open(6), 120)
            game.shot("original-back-menu")
            act("b", 4, 6)
            until("the menu to close", lambda: ram(TEXTBOX_CLOSED) != 0, 120)
            game.poke({SELECTED_ITEM: RED_POTION})
            wait(4)
            for what, key in (("b", False), ("S", True), ("lefttrigger+", False)):
                press(what, key=key)
                wait(30)
                assert ram(SELECTED_ITEM) == RED_POTION and ram(PAUSED) == 0 and not box_open(), what
            print("ok: with the remaster off, Start pauses and Back opens the original's menu; B, S and the left "
                  "trigger do nothing")
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
