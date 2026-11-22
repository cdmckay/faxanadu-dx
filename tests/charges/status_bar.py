"""Play the recharging items on the status bar end to end, in Seamless.

    python3 tests/charges/status_bar.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/charges

Seamless's 424-wide view lays the status bar and a row of three slots past
its item box out centred together: the Wing Boots, the Hour Glass and the
Elixir. Each shows in the status bar's colours when ready, grey when used,
and not at all when the hero does not have it. The checks are on the
screenshots' pixels in each slot (colour, grey, or the band's black) and on
the feature's TCP state; the setup reuses Recharging items' end-to-end test
(tests/recharge/recharging_items.py): walking out of a shop whose door was
edited to lead where each check is, with RAM edits for what the hero holds.

1. layout: nothing owned, the row's three slots are empty, but the status
   bar is already where the row puts it, 32 pixels left of centre, so it does
   not move when the first item comes. All three ready: three in colour. The
   Select menu lines up under the status bar.
2. boots: the Wing Boots ready; used, they grey at once and blink through
   "I've used Wing Boots." and after it, then stay grey while they fly and
   while they recharge; ready again, in colour.
3. glass: the Hour Glass the same: grey while it freezes and recharges.
4. elixir: the Elixir charged; a Zozura's hit at 1 HP revives the hero, and
   it is spent at once, grey and blinking through "I've used Elixir.", then
   grey.
5. guru: spent in Eolis's temple, grey; talking to the Guru restores it, in
   colour from his first line, with his box lined up under the status bar.
6. original: the remaster off, nothing past the status bar, which is where
   the original has it.
"""
from pathlib import Path
import struct
import sys
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "recharge"))
import fax_e2e  # noqa: E402
from fax_e2e import Game, act, box_open, page_through, parse_args, ram, tcp, type_out, until, wait  # noqa: E402
import recharging_items as recharge  # noqa: E402  (its recipes, and its walk out of a shop)

PARTS = ("layout", "boots", "glass", "elixir", "guru", "original")
PACKAGE = ("faxanadu.remastered", "remastered")
ITEMS = ("boots", "glass", "elixir")                       # the slots, left to right
ROW, SLOT, ITEM, TOP = 64, 24, 16, 16                      # fax_charges.h
HUD, NATIVE_W = 32, 256
NONE, READY, USED = 0, 1, 2
BLINK = 96                                                 # FAX_CHARGES_BLINK
OPENING = 24                                               # frames from the use to its line's box: the blink counts them
SELECTED_ITEM, ITEM_INVENTORY, NUMBER_OF_ITEMS = 0x3C1, 0x3AD, 0x3C6
SPECIAL_ITEMS, HP, HP_FRACTION, MP, MESSAGE_ID = 0x42C, 0x431, 0x432, 0x39A, 0x213
DURATION_WING_BOOTS, DURATION_HOUR_GLASS, PLAYER_TITLE = 0x429, 0x42A, 0x437
PLAYER_FLAGS = 0xA4
WING_BOOTS, HOUR_GLASS, NO_ITEM, BIT_ELIXIR = 0x0F, 0x0D, 0xFF, 0x08
USED_HOUR_GLASS, USED_WING_BOOTS, USED_ELIXIR = 0xAC, 0xAD, 0xAF
ELIXIR_LINE = 0xC3                                         # the slot a Guru's Elixir line shows from
WING_BOOTS_GONE, HOUR_GLASS_GONE = 0xC0, 0xC1
EOLIS, PALETTE_EOLIS = 0, 0x00


def png(path):
    """A screenshot's (width, height, rows of RGB bytes). The host writes
    them stored and unfiltered (cyc_png.c), so each row is a slice."""
    data = Path(path).read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos, idat = 8, b""
    while pos < len(data):
        size, kind = struct.unpack_from(">I4s", data, pos)
        body = data[pos + 8:pos + 8 + size]
        if kind == b"IHDR":
            width, height, depth, color = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and color == 2, (depth, color)
        elif kind == b"IDAT":
            idat += body
        pos += 12 + size
    raw, stride = zlib.decompress(idat), width * 3 + 1
    assert all(raw[y * stride] == 0 for y in range(height)), "a filtered row"
    return width, height, [raw[y * stride + 1:(y + 1) * stride] for y in range(height)]


def pixel(rows, x, y):
    return tuple(rows[y][x * 3:x * 3 + 3])


class Picture:
    """A screenshot of the status band: where the status bar is, and what
    each of the row's slots holds."""

    def __init__(self, game, name):
        self.game, self.path = game, game.shot(name)
        self.width, self.height, self.rows = png(self.path)
        self.native_x = fax_e2e.native_x(self.width)

    def drop(self):
        self.game.shots.remove(self.path.name)
        self.path.unlink()

    def lit(self, x0, x1, y0=TOP, y1=HUD):
        return [pixel(self.rows, x, y) for y in range(y0, y1) for x in range(max(x0, 0), min(x1, self.width))
                if pixel(self.rows, x, y) != (0, 0, 0)]

    def slot(self, i):
        """What slot `i` shows: "empty" (the band's black), "grey" (every lit
        pixel grey), or "colour"."""
        x = self.native_x + NATIVE_W + i * SLOT
        lit = self.lit(x, x + SLOT)
        if not lit:
            return "empty"
        assert len(lit) >= 40, (self.path.name, i, len(lit))      # a picture, not a stray pixel
        return "grey" if all(r == g == b for r, g, b in lit) else "colour"

    def slots(self):
        return tuple(self.slot(i) for i in range(3))

    def status_bar_left(self):
        """The leftmost lit pixel of the status bar's text rows: its "M"."""
        return min(x for x in range(self.width) for y in range(16, 24) if pixel(self.rows, x, y) != (0, 0, 0))


def charges():
    return tcp("charges_state")


def main():
    args = parse_args(__doc__, parts=PARTS)
    game = Game.from_args(args)
    shop = None

    def launch(on=True, at=None):
        game.close()
        game.set_features({PACKAGE: on})
        game.launch(at=at)
        assert charges()["enabled"] == on, charges()

    def look(name, want=None, frames=40):
        """Screenshot `name` and what its slots show; with `want`, retaken
        until they show that (a blink's on or off phase)."""
        for _ in range(frames // 2):
            p = Picture(game, name)
            if want is None or p.slots() == want:
                return p
            p.drop()
            wait(2)
        raise AssertionError(f"{name}: the slots never showed {want}")

    def walk_to(name, screen, pos, writes=None, record=None):
        """Out of the meat shop onto Eolis's `screen`, holding what `writes` gives him."""
        recharge.warp_out(game, shop, name, EOLIS, screen, pos, PALETTE_EOLIS, 0,
                          {SPECIAL_ITEMS: 0, SELECTED_ITEM: NO_ITEM, NUMBER_OF_ITEMS: 0, **(writes or {})}, record)

    def use():
        """Down+B, once he stands (recharging_items' use())."""
        until("the hero to stand", lambda: not ram(PLAYER_FLAGS) & 0x05, 120)
        tcp("action", name="down", frames=60)
        wait(6)
        tcp("action", name="b", frames=4)
        wait(24)
        act("down", 1, 0)

    def dismiss(expect, frames=900):
        until(f"message ${expect:02X}", lambda: ram(MESSAGE_ID) == expect, frames)
        page_through("the box to close", lambda: not box_open(), 3000)

    def blinks(item, frames=48):
        """Over `frames` frames the item is used and blinking: drawn on some, not on others."""
        seen = set()
        for _ in range(frames // 2):
            s = charges()[item]
            assert s["charge"] == USED and s["blink"] > 0, s
            seen.add(s["shown"])
            wait(2)
        assert seen == {True, False}, f"{item} should blink: {seen}"

    def settles(item):
        """The blink ends, at most BLINK frames after any box has closed, and the item stays grey."""
        until(f"the {item}'s blink to end", lambda: charges()[item]["blink"] == 0, BLINK + 60)
        for _ in range(8):
            s = charges()[item]
            assert s["charge"] == USED and s["shown"], s
            wait(2)

    def cycle(item, name, slot, selected, used_line, gone_line, duration, cooldown_field):
        """Ready, used (grey and blinking), running, recharging, ready again: the Wing Boots or the Hour Glass."""
        want_ready = tuple("colour" if i == slot else "empty" for i in range(3))
        want_grey = tuple("grey" if i == slot else "empty" for i in range(3))
        want_off = ("empty",) * 3
        walk_to(f"{item}-ready", 1, 0x99, {SELECTED_ITEM: selected, PLAYER_TITLE: 0})
        game.poke({0x2CC + i: 0xFF for i in range(8)})            # no one about
        assert charges()[item]["charge"] == READY, charges()
        look(f"{item}-ready", want_ready)
        use()
        until(f"the {item}'s line", lambda: ram(MESSAGE_ID) == used_line, 120)
        s = charges()[item]
        assert s["charge"] == USED and BLINK - OPENING <= s["blink"] <= BLINK, s   # at once
        blinks(item)
        look(f"{item}-used", want_grey)
        look(f"{item}-used-blink-off", want_off)
        page_through("the box to close", lambda: not box_open(), 3000)
        settles(item)
        print(f"ok: {name} grey the moment they are used, blinking through their line and after it, then hold "
              f"steady ({s['blink']} of {BLINK} frames of blinking left as the line came up)")
        game.poke({duration: recharge.LAST_SECONDS})                # the game counts the run down itself
        dismiss(gone_line, (recharge.LAST_SECONDS + 2) * recharge.SECOND)
        until("the cooldown", lambda: recharge.state()[f"{item}_cooldown"] > 0, 60)
        s = charges()[item]
        assert s["charge"] == USED and s["blink"] == 0, s
        look(f"{item}-recharging", want_grey)
        game.poke({}, {recharge.RECORD.decode(): {cooldown_field: 2}})
        until(f"the {item} to be ready", lambda: charges()[item]["charge"] == READY, 4 * recharge.SECOND)
        wait(2)
        look(f"{item}-ready-again", want_ready)
        print(f"ok: {name} stay grey while they run and recharge, and are in colour again once ready")

    try:
        shop = game.state("eolis-meat-shop")
        temple = game.state("recharge-temple") if args.wants("guru") else None

        if args.wants("layout"):
            launch(at="eolis-town")
            wait(4)
            s, world = charges(), tcp("fax_world")
            assert s["hud"] and s["row"] == ROW and all(s[i]["charge"] == NONE for i in ITEMS), s
            assert world["size"] == [424, 240] and world["native_x"] == (424 - NATIVE_W - ROW) // 2, world
            p = look("nothing-owned", ("empty",) * 3)
            left = p.status_bar_left()
            assert left == p.native_x + 8, (left, p.native_x)        # the "M" in column 1, from its left edge
            assert not p.lit(p.native_x + NATIVE_W, p.width), "nothing past the status bar"
            print(f"ok: nothing owned, the row is empty, and the status bar is already at x {p.native_x}, "
                  f"{(424 - NATIVE_W) // 2 - p.native_x} left of centre, with room for the row")
            walk_to("all-ready", 1, 0x99, {SELECTED_ITEM: WING_BOOTS, ITEM_INVENTORY: HOUR_GLASS, NUMBER_OF_ITEMS: 1,
                                            SPECIAL_ITEMS: BIT_ELIXIR})
            s = charges()
            assert all(s[i]["charge"] == READY and s[i]["shown"] for i in ITEMS), s
            p = look("all-ready", ("colour",) * 3)
            assert p.status_bar_left() == left and p.native_x == world["native_x"], "the status bar moved"
            print("ok: all three ready, each in colour in its own slot; the status bar did not move")
            act("select", 4, 30)
            assert box_open(), "the Select menu"
            p = look("menu", ("colour",) * 3)
            # PlayerMenu_Show's box at tile 2, 6 lines up under the status bar: its left side in column 2, the
            # black inside it from column 3 (the text from 4), the town's sky outside it in column 1.
            x = p.native_x
            assert p.lit(x + 16, x + 24, 56, 64) and not p.lit(x + 24, x + 32, 56, 64), "the menu's left side"
            assert len(p.lit(x + 8, x + 16, 56, 64)) == 64, "the town outside the menu"
            act("b", 4, 30)
            print("ok: the Select menu lines up under the status bar, and the row stays")

        if args.wants("boots"):
            launch()
            cycle("boots", "the Wing Boots", 0, WING_BOOTS, USED_WING_BOOTS, WING_BOOTS_GONE, DURATION_WING_BOOTS, 2)

        if args.wants("glass"):
            launch()
            cycle("glass", "the Hour Glass", 1, HOUR_GLASS, USED_HOUR_GLASS, HOUR_GLASS_GONE, DURATION_HOUR_GLASS, 4)

        if args.wants("elixir"):
            launch()
            walk_to("elixir", 4, 0x9C, {SPECIAL_ITEMS: BIT_ELIXIR})
            assert charges()["elixir"]["charge"] == READY, charges()
            look("elixir-charged", ("empty", "empty", "colour"))
            game.poke({HP: 1, HP_FRACTION: 0})
            until("a Zozura's hit", lambda: ram(MESSAGE_ID) == USED_ELIXIR, 2400)
            s = charges()["elixir"]
            assert s["charge"] == USED and BLINK - OPENING <= s["blink"] <= BLINK, s
            blinks("elixir")
            look("elixir-revived", ("empty", "empty", "grey"))
            look("elixir-revived-blink-off", ("empty",) * 3)
            print("ok: the Elixir revives the hero and is spent at once, grey and blinking through \"I've used Elixir.\" "
                  f"({s['blink']} of {BLINK} frames of blinking left as the line came up)")
            page_through("the box to close", lambda: not box_open(), 3000)
            until("HP and MP to refill", lambda: (ram(HP), ram(MP)) == (0x50, 0x50), 1200)
            settles("elixir")
            look("elixir-spent", ("empty", "empty", "grey"))
            print("ok: after the line it blinks a moment more, then stays grey: he owns it, but it will not catch him")

        if args.wants("guru"):
            launch(at="recharge-temple")
            wait(4)
            assert charges()["elixir"]["charge"] == USED, charges()
            look("temple-spent", ("empty", "empty", "grey"))
            act("left", 170, 10)                                        # to the Guru
            act("up", 4, 10)
            until("the Guru to restore it", lambda: recharge.state()["guru_recharges"] == 1, 300)
            assert charges()["elixir"]["charge"] == READY and charges()["elixir"]["blink"] == 0, charges()
            until("his line", lambda: ram(MESSAGE_ID) == ELIXIR_LINE, 300)
            type_out("his line", 900, pauses=False)                    # no pause in it; the flag may be stale
            assert recharge.state()["line"] == 1, recharge.state()   # "Your Elixir is restored."
            p = look("elixir-restored", ("empty", "empty", "colour"))
            page_through("the box to close", lambda: not box_open(), 3000)
            print("ok: Eolis's Guru restores the spent Elixir, in colour from his first line")

        if args.wants("original"):
            launch(on=False, at="eolis-town")
            game.poke({SELECTED_ITEM: WING_BOOTS, ITEM_INVENTORY: HOUR_GLASS, NUMBER_OF_ITEMS: 1,
                       SPECIAL_ITEMS: BIT_ELIXIR})
            wait(4)
            s = charges()
            assert not s["enabled"] and s["row"] == 0 and all(s[i]["charge"] == NONE for i in ITEMS), s
            p = Picture(game, "original")
            centred = (p.width - NATIVE_W) // 2 if p.width > NATIVE_W else 0
            assert p.native_x == centred and p.status_bar_left() == centred + 8, (p.native_x, p.status_bar_left())
            assert not p.lit(centred + NATIVE_W, p.width, 0, HUD), "nothing past the status bar"
            print(f"ok: with the remaster off, nothing past the status bar, which is where the original has it "
                  f"(x {centred} of a {p.width}-wide picture)")
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
