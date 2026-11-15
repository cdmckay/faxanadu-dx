"""Play Quick shops end to end in a hidden window and screenshot the lists.

    python3 tests/shops/quick_shops.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/shops

The game runs uncapped (about 5x real time; --realtime to watch it at normal
speed) and the script waits on the game's own state, never on the clock, so
each run takes a free TCP port, and any number can go in parallel.

Copies the build to OUT, starts a new game over TCP, walks through Eolis's gate
into the meat shop and saves a state there. Save-state RAM edits then set up
what each scenario needs (external prerequisites, not coverage): where the
shop's door leads (as tests/speaker/speaker_names.py walks into each Eolis
building), the gold, and what the hero carries.

1. Eolis's tool shop (Hand Dagger 400, Red Potion 160, Elixir 320, Deluge
   400), with 700 Golds, the Hand Dagger in hand and, unequipped, the Long
   Sword and the Dragon Slayer, and the J and A keys. Up opens the buy list at
   once, and every later press is a choice: Down to the Red Potion and A three
   times buys three (the list stays up, the cursor on the potion), Down twice
   and A on the Deluge says "This is not enough Golds." and A goes back to the
   list. Select opens the sell list: everything carried, the Items first, at
   half a shop's price (a Red Potion 80, this shop's; the J key 50, a key
   shop's; the Long Sword 800, Forepaw's), the A key at 100 (no shop sells it)
   and the Dragon Slayer with no price. A sells the J key, which this shop does
   not sell; on the Dragon Slayer the hint says "Not for sale" and A does
   nothing; a Red Potion sells. B leaves at once, with no closing line. Gold
   and inventory are checked after each step. Next door, the Key Seller's
   list opens the same way, and he buys a Red Potion for 80 (half the lowest
   tool shop's price).
2. A full inventory (8 items and 3 more weapons) is two pages: Down from the
   bottom of the first turns to the second, a sale there keeps the page, and
   Select goes back to buying; selling the last item empties the list ("You
   have nothing to buy.", the game's own line) and the list goes back to
   buying.
3. With Quick shops off, the same Up shows the shop's greeting and then the
   Buy/Sell menu's own line, as before (Save files serves another line for
   that menu at a Guru's, and only there).

Each list is checked in the picture too: the list's frame, and its hint line
inside it, in the text color.
"""
from pathlib import Path
import struct
import sys
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
from fax_e2e import Game, act, fixture, frame, parse_args, ram, tcp, until, wait  # noqa: E402

AREA, SCREEN, PLAYER_X = 0x24, 0x63, 0x9E
TEXTBOX_CLOSED, MESSAGE_ID = 0x20C, 0x213
GOLD, HP = 0x392, 0x431
WEAPONS, ITEMS = 0x39D, 0x3AD                                       # WeaponInventory, ItemInventory
SELECTED_WEAPON, SELECTED_ITEM = 0x3BD, 0x3C1
NUMBER_OF_WEAPONS, NUMBER_OF_ITEMS = 0x3C2, 0x3C6
PORTRAIT = 0x3C7
AREA_SAVED_AREA, AREA_SAVED_SCREEN, PLAYER_SAVED_POS = 0x3D5, 0x3D6, 0x3D8
BUILDINGS, NO_PORTRAIT = 4, 0xFF
HAND_DAGGER, LONG_SWORD, GIANT_BLADE, DRAGON_SLAYER = 0x00, 0x01, 0x02, 0x03
KEY_A, KEY_K, KEY_Q, KEY_J, MATTOCK, RED_POTION = 0x04, 0x05, 0x06, 0x07, 0x09, 0x10
DELUGE, ELIXIR = 0x60, 0x92
ITEM = 0x80                                                         # the Items category, in a full ID
NOT_ENOUGH_GOLDS, NOTHING_TO_SELL, GREETING, BUY_OR_SELL = 0x03, 0x12, 0x0F, 0x10   # message IDs
LIST_FRAME = (80, 64)                                               # the list's top-left frame tile, tile 10,8
HINT = (96, 208)                                                    # its hint line: tile 12,26
PAGE_ROW, LINE_END = 72, 224                                        # the page: row 9, ending at column 27
PACKAGE = ("faxanadu.enhancement.quick-shops", "quick-shops")


def shops():
    return tcp("shops_state")


def gold():
    lo, mid, hi = ram(GOLD, 3)
    return lo | mid << 8 | hi << 16


def items():
    return list(ram(ITEMS, 8)[:ram(NUMBER_OF_ITEMS)])


def weapons():
    return list(ram(WEAPONS, 4)[:ram(NUMBER_OF_WEAPONS)])


def typed():
    """A message has typed out (MessageID clears)."""
    return ram(MESSAGE_ID) == 0


def waiting():
    """The text box waits for a button: typed out, or at a pause."""
    message, _, paused = ram(MESSAGE_ID, 3)
    return message == 0 or paused != 0


def carrying(gold_amount, weapon_list, item_list, weapon=HAND_DAGGER):
    """RAM writes for a hero with this gold, weapon in hand, and lists."""
    w = {GOLD: gold_amount & 0xFF, GOLD + 1: gold_amount >> 8 & 0xFF, GOLD + 2: gold_amount >> 16,
         SELECTED_WEAPON: weapon, NUMBER_OF_WEAPONS: len(weapon_list), NUMBER_OF_ITEMS: len(item_list),
         SELECTED_ITEM: 0xFF, HP: 0x50}
    w.update({WEAPONS + i: v for i, v in enumerate(weapon_list)})
    w.update({ITEMS + i: v for i, v in enumerate(item_list)})
    return w


def png(path):
    """A screenshot's pixels: (width, height, rows of (r, g, b) tuples)."""
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos, idat = 8, b""
    while pos < len(data):
        size, kind = struct.unpack_from(">I4s", data, pos)
        body = data[pos + 8:pos + 8 + size]
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", body)
            assert depth == 8 and color in (2, 6) and not interlace, (depth, color, interlace)
        elif kind == b"IDAT":
            idat += body
        pos += 12 + size
    bpp = 3 if color == 2 else 4
    raw, stride, rows, prior = zlib.decompress(idat), width * bpp, [], bytearray(width * bpp)
    for y in range(height):
        kind, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b, c = prior[i], prior[i - bpp] if i >= bpp else 0
            p = a + b - c
            pred = (0, a, b, (a + b) // 2,
                    a if abs(p - a) <= abs(p - b) and abs(p - a) <= abs(p - c) else b if abs(p - b) <= abs(p - c) else c)[kind]
            line[i] = (line[i] + pred) & 0xFF
        rows.append([tuple(line[x * bpp:x * bpp + 3]) for x in range(width)])
        prior = line
    return width, height, rows


def check_hint(path, text, page=None):
    """The list in the picture: its frame's corner where Shop_Draw puts it,
    its hint line holding `text`'s letters in one color on black, and the
    page, if any, at the right of the row over its first entry."""
    width, height, px = png(path)
    ox = (width - 256) // 2 if width >= 256 else 0                 # where Seamless puts the native picture
    corner = [px[LIST_FRAME[1] + r][ox + LIST_FRAME[0] + c] for r in range(8) for c in range(8)]
    assert len(set(corner)) > 1, f"{path.name}: no frame at the list's corner"
    lines = [(HINT[0], HINT[1], text)] + ([(LINE_END - 8 * len(page), PAGE_ROW, page)] if page else [])
    for x, y, line in lines:
        for i, ch in enumerate(line):
            cell = [px[y + r][ox + x + i * 8 + c] for r in range(8) for c in range(8)]
            lit = {p for p in cell if p != (0, 0, 0)}
            assert (ch == " ") == (not lit), f"{path.name}: {ch!r} in {line!r} is {'lit' if lit else 'dark'}"
            assert len(lit) <= 1, f"{path.name}: {line!r} is not one color"


def check_no_price(path, entry):
    """Entry `entry` of the list shows no price: its price cells (Shop_Draw:
    column 23, 5 digits, on the entry's second row) are the list's inside, as
    the blank row over the first entry is."""
    width, height, px = png(path)
    ox = (width - 256) // 2 if width >= 256 else 0
    inside = px[PAGE_ROW][ox + 96]
    y = 88 + entry * 16
    cells = {px[y + r][ox + 184 + c] for r in range(8) for c in range(40)}
    assert cells == {inside}, f"{path.name}: entry {entry} shows a price"


def main():
    args = parse_args(__doc__)
    game = Game.from_args(args)
    out = game.out

    def launch(on=True):
        game.set_features({PACKAGE: on})
        game.launch()
        assert shops()["enabled"] == on, shops()

    def walk_to(x_min, x_max):
        for _ in range(600):
            if ram(TEXTBOX_CLOSED) == 0:
                if waiting():
                    act("a", 6, 14)
                else:
                    wait(4)
                continue
            x = ram(PLAYER_X)
            if x_min <= x <= x_max:
                return
            act("right" if x < x_min else "left", 2, 0)
        raise AssertionError(f"could not reach x {x_min}-{x_max}")

    def walk_out():
        for _ in range(300):
            if ram(AREA) != BUILDINGS:
                return
            act("right", 2, 0)
        raise AssertionError("could not walk out")

    def go_in(source, name, screen, door, writes):
        """From inside a building, walk out onto Eolis's door `door` (block YX)
        on `screen`, and go in."""
        target = out / f"{name}.cycstate"
        fixture(source, target, {AREA_SAVED_AREA: 0, AREA_SAVED_SCREEN: screen, PLAYER_SAVED_POS: door, **writes})
        tcp("load_state", path=str(target))
        walk_out()
        until(f"Eolis screen {screen}", lambda: (ram(AREA), ram(SCREEN)) == (0, screen))
        wait(30)
        column = (door & 15) * 16
        walk_to(column - 6, column + 4)
        act("up", 4, 10)
        until("the building", lambda: ram(AREA) == BUILDINGS)
        wait(40)

    def to_counter(stop=0):
        """Walk left to the counter, or to x `stop`."""
        for _ in range(60):
            before = ram(PLAYER_X)
            if before <= stop:
                return
            act("left", 8, 2)
            if ram(PLAYER_X) == before:
                return
        raise AssertionError("no counter")

    def list_up(mode, what=None):
        """Wait until the `mode` list is on screen, with its hint, and has stopped redrawing."""
        until(what or f"the {mode} list", lambda: (lambda s: s["shown"] == mode and s["list_up"] and
                                                    s["hint"] is not None)(shops()), 900)
        waits = shops()["waits"]
        until("the list to wait for a button", lambda: shops()["waits"] > waits, 300)
        wait(2)
        state = shops()
        assert state["shown"] == mode and state["list_up"], state
        return state

    def press(button, mode=None):
        """One press in a list; with `mode`, wait for that list again."""
        act(button, 4, 6)
        return list_up(mode) if mode else None

    def buy(expect_cursor):
        before = shops()["bought"]
        act("a", 4, 6)
        until("the purchase", lambda: shops()["bought"] == before + 1, 1200)
        state = list_up("buy")
        assert state["cursor"] == expect_cursor, state
        return state

    def sell():
        before = shops()["sold"]
        act("a", 4, 6)
        until("the sale", lambda: shops()["sold"] == before + 1, 2400)
        return list_up("sell")

    def speaker_on_list():
        state = tcp("speaker_state")
        assert state["shown"] and (state["col"], state["row"]) == (10, 5), state

    try:
        launch()
        until("the title", lambda: frame() > 700)
        tcp("pad", buttons="start", frames=10)
        until("the Eolis gate", lambda: ram(AREA) == 0 and frame() > 1900)
        walk_to(186, 196)                                   # the gate, column 12
        act("up", 4, 10)
        until("Eolis", lambda: (ram(AREA), ram(SCREEN)) == (0, 1))
        wait(30)
        walk_to(41, 56)                                     # the meat shop, column 3
        act("up", 4, 10)
        until("the meat shop", lambda: ram(AREA) == BUILDINGS)
        wait(30)
        start = out / "eolis-meat-shop.cycstate"
        tcp("save_state", path=str(start))

        # 1. Eolis's tool shop (building 4: screen 3, block $9A).
        go_in(start, "tool-shop", 3, 0x9A, carrying(700, [DRAGON_SLAYER, LONG_SWORD], [KEY_J, KEY_A]))
        to_counter()
        message_before = ram(MESSAGE_ID)
        act("up", 4, 6)
        state = list_up("buy", "the buy list, straight away")
        assert state["list"] == [HAND_DAGGER, ITEM | RED_POTION, ELIXIR, DELUGE], state
        assert state["prices"] == [400, 160, 320, 400] and state["cursor"] == 0, state
        assert state["hint"] == "Select to sell" and state["page_label"] is None, state
        assert ram(MESSAGE_ID) == message_before, "a line was loaded before the list"
        speaker_on_list()
        check_hint(game.shot("buy-list"), "Select to sell")
        print("ok: Up opens the buy list at once, with its hint, under the Tool Seller's name")

        press("down")
        for _ in range(3):
            buy(1)
        assert gold() == 700 - 3 * 160, gold()
        assert items() == [KEY_J, KEY_A, RED_POTION, RED_POTION, RED_POTION], items()
        print("ok: A three times buys three Red Potions, the list staying up and the cursor on them")

        press("down")
        press("down")
        assert shops()["cursor"] == 3, shops()
        act("a", 4, 6)
        until("This is not enough Golds.", lambda: ram(MESSAGE_ID) == NOT_ENOUGH_GOLDS, 600)
        until("the line to type out", typed, 600)
        game.shot("not-enough-golds")
        assert gold() == 220 and not shops()["list_up"], (gold(), shops())
        act("a", 4, 6)
        state = list_up("buy", "the buy list, after the message")
        assert state["cursor"] == 3 and state["messages"] == 1, state
        print("ok: the Deluge is not enough Golds, in the game's words, and A goes back to the list")

        state = press("select", "sell")
        assert state["list"] == [ITEM | RED_POTION] * 3 + [ITEM | KEY_A, ITEM | KEY_J, LONG_SWORD, DRAGON_SLAYER], state
        assert state["prices"] == [80, 80, 80, 100, 50, 800, 0], state
        assert state["hint"] == "Select to buy" and state["pages"] == 1 and state["page_label"] is None, state
        speaker_on_list()
        check_hint(game.shot("sell-list"), "Select to buy")
        print("ok: Select opens the sell list: everything carried, at half a shop's price, 100 for the A key")

        for _ in range(4):
            press("down")
        assert shops()["cursor"] == 4, shops()
        game.shot("sell-a-key-shops-item")
        state = sell()
        assert gold() == 220 + 50, gold()
        assert items() == [KEY_A, RED_POTION, RED_POTION, RED_POTION], items()
        assert state["list"] == [ITEM | RED_POTION] * 3 + [ITEM | KEY_A, LONG_SWORD, DRAGON_SLAYER], state
        assert state["cursor"] == 4, state
        print("ok: the J key, which only key shops sell, sells here for 50, and the list stays up")

        press("down")
        state = shops()
        assert state["cursor"] == 5 and state["hint"] == "Not for sale", state
        path = game.shot("dragon-slayer-not-for-sale")
        check_hint(path, "Not for sale")
        check_no_price(path, 5)
        act("a", 4, 30)
        state = shops()
        assert state["refused"] == 1 and state["list_up"] and state["shown"] == "sell", state
        assert gold() == 270 and weapons() == [DRAGON_SLAYER, LONG_SWORD], (gold(), weapons())
        game.shot("dragon-slayer-kept")
        print("ok: the Dragon Slayer is not for sale, and A on it keeps it")

        for _ in range(5):
            press("up")
        assert shops()["cursor"] == 0, shops()
        sell()
        assert gold() == 270 + 80, gold()
        assert items() == [KEY_A, RED_POTION, RED_POTION], items()
        print("ok: a Red Potion sells for 80, half this shop's price")

        act("b", 4, 6)
        until("the conversation's end", lambda: ram(PORTRAIT) == NO_PORTRAIT and ram(TEXTBOX_CLOSED) != 0, 300)
        assert ram(MESSAGE_ID) == 0, "a closing line"
        game.shot("left")
        print("ok: B leaves at once, with no closing line")

        # The key shop next door (building 3: screen 3, block $9D): the same.
        go_in(start, "key-shop", 3, 0x9D, carrying(0, [], [RED_POTION]))
        to_counter(stop=80)                                 # he stands at block $84, x 64
        act("up", 4, 6)
        state = list_up("buy", "the Key Seller's list, straight away")
        assert state["list"] == [ITEM | KEY_J] and state["prices"] == [100], state
        state = press("select", "sell")
        assert state["list"] == [ITEM | RED_POTION] and state["prices"] == [80], state
        game.shot("key-shop-sell-list")
        act("b", 4, 6)
        until("the conversation's end", lambda: ram(PORTRAIT) == NO_PORTRAIT, 300)
        print("ok: the Key Seller's list opens at once, and he buys a Red Potion for 80, a tool shop's half")

        # 2. Two pages: 8 items, and the Long Sword, the Giant Blade and the Dragon Slayer.
        many = [KEY_J, KEY_Q, KEY_K, MATTOCK, RED_POTION, RED_POTION, KEY_A, RED_POTION]
        go_in(start, "two-pages", 3, 0x9A, carrying(100, [LONG_SWORD, GIANT_BLADE, DRAGON_SLAYER], many))
        to_counter()
        act("up", 4, 6)
        list_up("buy")
        state = press("select", "sell")
        assert state["pages"] == 2 and state["page"] == 0 and len(state["list"]) == 8, state
        assert state["list"] == [ITEM | v for v in reversed(many)], state
        assert state["hint"] == "Select to buy" and state["page_label"] == "1 of 2", state
        check_hint(game.shot("sell-page-1"), "Select to buy", "1 of 2")
        for _ in range(7):
            press("down")
        state = press("down", "sell")
        until("the second page", lambda: shops()["page"] == 1, 300)
        state = list_up("sell")
        assert state["list"] == [DRAGON_SLAYER, GIANT_BLADE, LONG_SWORD] and state["cursor"] == 0, state
        assert state["prices"] == [0, 4250, 800] and state["hint"] == "Not for sale", state
        assert state["page_label"] == "2 of 2", state
        path = game.shot("sell-page-2")
        check_hint(path, "Not for sale", "2 of 2")
        check_no_price(path, 0)
        press("down")
        state = sell()
        assert gold() == 100 + 4250 and weapons() == [LONG_SWORD, DRAGON_SLAYER], (gold(), weapons())
        assert state["page"] == 1 and state["list"] == [DRAGON_SLAYER, LONG_SWORD], state
        state = press("select", "buy")
        assert state["cursor"] == 0, state
        print("ok: eleven things are two pages, Down from the bottom turns the page, and Select buys again")
        act("b", 4, 6)
        until("the conversation's end", lambda: ram(PORTRAIT) == NO_PORTRAIT, 300)

        # The last thing to sell: then the list is empty, and the shop says so.
        go_in(start, "last-one", 3, 0x9A, carrying(0, [], [KEY_J]))
        to_counter()
        act("up", 4, 6)
        list_up("buy")
        state = press("select", "sell")
        assert state["list"] == [ITEM | KEY_J], state
        before = state["sold"]
        act("a", 4, 6)
        until("the sale", lambda: shops()["sold"] == before + 1, 1200)
        until("You have nothing to buy.", lambda: ram(MESSAGE_ID) == NOTHING_TO_SELL, 600)
        until("the line to type out", typed, 600)
        assert gold() == 50 and items() == [], (gold(), items())
        game.shot("nothing-to-sell")
        act("a", 4, 6)
        list_up("buy", "the buy list, with nothing to sell")
        print("ok: selling the last thing gives the game's own line, and back to buying")
        act("b", 4, 6)
        until("the conversation's end", lambda: ram(PORTRAIT) == NO_PORTRAIT, 300)
        game.close()

        # 3. Off: the shop is as it was.
        launch(on=False)
        tcp("load_state", path=str(out / "tool-shop.cycstate"))
        walk_out()
        until("Eolis screen 3", lambda: (ram(AREA), ram(SCREEN)) == (0, 3))
        wait(30)
        walk_to(154, 164)
        act("up", 4, 10)
        until("the building", lambda: ram(AREA) == BUILDINGS)
        wait(40)
        to_counter()
        act("up", 4, 6)
        until("the greeting", lambda: ram(MESSAGE_ID) == GREETING, 600)
        assert not shops()["active"], shops()
        until("the greeting to type out", typed, 600)
        act("a", 4, 6)
        until("the Buy/Sell menu's own line", lambda: ram(MESSAGE_ID) == BUY_OR_SELL, 600)
        print("ok: with Quick shops off, the Tool Seller greets you and asks Buy or Sell as before "
              "(Save files' line for the Guru's menu stays the Guru's)")
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
