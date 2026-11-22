"""Play the Elixir on the status bar end to end, in Seamless.

    python3 tests/charges/status_bar.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/charges

Seamless's 424-wide view lays the status bar band out with the Elixir's
place left of it, against its M: and P: bars, the two centred together. Only
the band moves: the rooms, and the boxes and banners over them, stay centred
in the view. The Elixir shows in the status bar's colours when charged, grey
when spent, and not at all when the hero does not have it; the Wing Boots
and the Hour Glass are not drawn. The checks are on the screenshots' pixels
(the Elixir's place in colour, grey, or the band's black, and black either
side of the band) and on the feature's TCP state; the setup reuses
Recharging items' end-to-end test (tests/recharge/recharging_items.py):
walking out of a shop whose door was edited to lead where each check is,
with RAM edits for what the hero holds.

1. layout: nothing owned, the Elixir's place is empty, but the status bar is
   already where the Elixir puts it, 8 pixels right of centre, so it does not
   move when he finds one. The Wing Boots, the Hour Glass and the Elixir
   ready: the Elixir in colour, left of the M: and P: bars and level with
   them, and nothing else. The Select menu stays on the room, centred in the
   view as before. The Wing Boots used, then the Hour Glass too, both
   running on MP: still only the Elixir.
2. elixir: the Elixir charged; a Zozura's hit at 1 HP revives the hero, and
   it is spent at once, grey and blinking through "I've used Elixir.", then
   grey.
3. guru: spent in Eolis's temple, grey; talking to the Guru restores it, in
   colour from his first line. The temple, one screen, is centred in the
   view, and his boxes are on it.
4. original: the remaster off, nothing either side of the status bar, which
   is where the original has it.
"""
from pathlib import Path
import struct
import sys
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "recharge"))
from fax_e2e import Game, act, box_open, page_through, parse_args, ram, tcp, type_out, until, wait  # noqa: E402
import recharging_items as recharge  # noqa: E402  (its recipes, and its walk out of a shop)

PARTS = ("layout", "elixir", "guru", "original")
PACKAGE = ("faxanadu.dx", "dx")
ITEM, TOP = 16, 16                                         # fax_charges.h: the Elixir's width, and its top row
HUD, NATIVE_W, VIEW_W = 32, 256, 424
HUD_X = (VIEW_W - NATIVE_W - ITEM) // 2 + ITEM             # the band's column 0: the Elixir and it centred together
NONE, READY, USED = 0, 1, 2
BLINK = 96                                                 # FAX_CHARGES_BLINK
OPENING = 24                                               # frames from the use to its line's box: the blink counts them
SELECTED_ITEM, ITEM_INVENTORY, NUMBER_OF_ITEMS = 0x3C1, 0x3AD, 0x3C6
SPECIAL_ITEMS, HP, HP_FRACTION, MP, MESSAGE_ID = 0x42C, 0x431, 0x432, 0x39A, 0x213
PLAYER_FLAGS = 0xA4
WING_BOOTS, HOUR_GLASS, NO_ITEM, BIT_ELIXIR = 0x0F, 0x0D, 0xFF, 0x08
USED_HOUR_GLASS, USED_WING_BOOTS, USED_ELIXIR = 0xAC, 0xAD, 0xAF
ELIXIR_LINE = 0xC3                                         # the slot a Guru's Elixir line shows from
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
    """A screenshot: where the status bar band is (hud_x, as Seamless says it
    laid it out), where the scene's native columns are (scene_x: centred),
    and what the Elixir's place holds."""

    def __init__(self, game, name):
        self.game, self.path = game, game.shot(name)
        self.width, self.height, self.rows = png(self.path)
        self.hud_x = tcp("fax_world")["hud_x"] if self.width > NATIVE_W else 0
        self.scene_x = (self.width - NATIVE_W) // 2

    def drop(self):
        self.game.shots.remove(self.path.name)
        self.path.unlink()

    def lit(self, x0, x1, y0=TOP, y1=HUD):
        return [pixel(self.rows, x, y) for y in range(y0, y1) for x in range(max(x0, 0), min(x1, self.width))
                if pixel(self.rows, x, y) != (0, 0, 0)]

    def elixir(self):
        """What the Elixir's place, against the band's left edge, shows:
        "empty" (black), "grey" (every lit pixel grey), or "colour"."""
        x = self.hud_x - ITEM
        lit = self.lit(x, self.hud_x)
        if not lit:
            return "empty"
        assert len(lit) >= 40, (self.path.name, len(lit))         # a picture, not a stray pixel
        return "grey" if all(r == g == b for r, g, b in lit) else "colour"

    def rows_lit(self):
        """The rows the Elixir's place has anything in."""
        x = self.hud_x - ITEM
        return sorted({y for y in range(HUD) for c in range(x, self.hud_x) if pixel(self.rows, c, y) != (0, 0, 0)})

    def nothing_else(self):
        """Black either side of the band and the Elixir's place, all 32 lines: no Wing Boots, no Hour Glass."""
        return not self.lit(0, self.hud_x - ITEM, 0, HUD) and not self.lit(self.hud_x + NATIVE_W, self.width, 0, HUD)

    def status_bar_left(self):
        """The leftmost lit pixel of the status bar's text rows, right of the Elixir's place: its "M"."""
        return min(x for x in range(self.hud_x, self.width) for y in range(16, 24) if pixel(self.rows, x, y) != (0, 0, 0))


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
        """Screenshot `name` and what the Elixir's place shows; with `want`,
        retaken until it shows that (a blink's on or off phase). Either way,
        nothing else is drawn beside the status bar."""
        for _ in range(frames // 2):
            p = Picture(game, name)
            assert p.nothing_else(), f"{name}: something beside the status bar but the Elixir"
            if want is None or p.elixir() == want:
                return p
            p.drop()
            wait(2)
        raise AssertionError(f"{name}: the Elixir's place never showed {want}")

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

    def blinks(frames=48):
        """Over `frames` frames the Elixir is spent and blinking: drawn on some, not on others."""
        seen = set()
        for _ in range(frames // 2):
            s = charges()["elixir"]
            assert s["charge"] == USED and s["blink"] > 0, s
            seen.add(s["shown"])
            wait(2)
        assert seen == {True, False}, f"the Elixir should blink: {seen}"

    def settles():
        """The blink ends, at most BLINK frames after any box has closed, and the Elixir stays grey."""
        until("the Elixir's blink to end", lambda: charges()["elixir"]["blink"] == 0, BLINK + 60)
        for _ in range(8):
            s = charges()["elixir"]
            assert s["charge"] == USED and s["shown"], s
            wait(2)

    try:
        shop = game.state("eolis-meat-shop")
        temple = game.state("recharge-temple") if args.wants("guru") else None

        if args.wants("layout"):
            launch(at="eolis-town")
            wait(4)
            s, world = charges(), tcp("fax_world")
            assert s["hud"] and s["width"] == ITEM and s["elixir"]["charge"] == NONE, s
            assert world["size"] == [VIEW_W, 240] and world["hud_x"] == HUD_X, world
            p = look("nothing-owned", "empty")
            left = p.status_bar_left()
            assert left == p.hud_x + 8, (left, p.hud_x)              # the "M" in column 1, from its left edge
            assert s["x"] == p.hud_x - ITEM and s["y"] == TOP, s
            print(f"ok: no Elixir, its place is empty, and the status bar is already at x {p.hud_x}, "
                  f"{p.hud_x - p.scene_x} right of the rooms' x {p.scene_x}, with room for it")
            walk_to("all-ready", 1, 0x99, {SELECTED_ITEM: WING_BOOTS, ITEM_INVENTORY: HOUR_GLASS, NUMBER_OF_ITEMS: 1,
                                            SPECIAL_ITEMS: BIT_ELIXIR, MP: 0x50})   # both run on MP
            r = recharge.state()
            assert r["boots_ready"] and r["glass_ready"], r
            s = charges()
            assert s["elixir"]["charge"] == READY and s["elixir"]["shown"], s
            p = look("all-ready", "colour")
            assert p.status_bar_left() == left and p.hud_x == world["hud_x"], "the status bar moved"
            rows = p.rows_lit()
            assert rows[0] >= 16 and rows[-1] <= 31 and rows[-1] - rows[0] >= 12, rows   # level with the M: and P: rows
            assert not p.lit(p.hud_x, p.hud_x + 8), "the band's blank column 0, between the Elixir and the M and P"
            print(f"ok: the Wing Boots, the Hour Glass and the Elixir his: only the Elixir, in colour at x "
                  f"{p.hud_x - ITEM}-{p.hud_x - 1}, lines {rows[0]}-{rows[-1]}, 8 pixels left of the M and P; "
                  "the status bar did not move")
            act("select", 4, 30)
            assert box_open(), "the Select menu"
            p = look("menu", "colour")
            # PlayerMenu_Show's box at tile 2, 6 of the scene's native columns, centred as before: its left
            # side in column 2, the black inside it from column 3 (the text from 4), the town's sky in column 1.
            x = p.scene_x
            assert p.lit(x + 16, x + 24, 56, 64) and not p.lit(x + 24, x + 32, 56, 64), "the menu's left side"
            assert len(p.lit(x + 8, x + 16, 56, 64)) == 64, "the town outside the menu"
            act("b", 4, 30)
            print(f"ok: the Select menu stays on the room at x {x}, centred as before, and the Elixir stays")
            use()
            until("the Wing Boots' line", lambda: ram(MESSAGE_ID) == USED_WING_BOOTS, 120)
            for _ in range(24):
                s = charges()["elixir"]
                assert s["charge"] == READY and s["shown"] and s["blink"] == 0, s
                wait(2)
            look("boots-used", "colour")
            page_through("the box to close", lambda: not box_open(), 3000)
            assert recharge.state()["boots_running"] == 1, recharge.state()   # the flight begins after the line
            game.poke({SELECTED_ITEM: HOUR_GLASS, ITEM_INVENTORY: WING_BOOTS})   # the Select menu's swap
            use()
            until("the Hour Glass's line", lambda: ram(MESSAGE_ID) == USED_HOUR_GLASS, 120)
            page_through("the box to close", lambda: not box_open(), 3000)
            r = recharge.state()
            assert r["boots_running"] == 1 and r["glass_running"] == 1, r
            look("both-running", "colour")
            print("ok: the Wing Boots used, then the Hour Glass, both running on MP: nothing drawn for them, and "
                  "the Elixir steady")

        if args.wants("elixir"):
            launch()
            walk_to("elixir", 4, 0x9C, {SPECIAL_ITEMS: BIT_ELIXIR})
            assert charges()["elixir"]["charge"] == READY, charges()
            look("elixir-charged", "colour")
            game.poke({HP: 1, HP_FRACTION: 0})
            until("a Zozura's hit", lambda: ram(MESSAGE_ID) == USED_ELIXIR, 2400)
            s = charges()["elixir"]
            assert s["charge"] == USED and BLINK - OPENING <= s["blink"] <= BLINK, s
            blinks()
            look("elixir-revived", "grey")
            look("elixir-revived-blink-off", "empty")
            print("ok: the Elixir revives the hero and is spent at once, grey and blinking through \"I've used Elixir.\" "
                  f"({s['blink']} of {BLINK} frames of blinking left as the line came up)")
            page_through("the box to close", lambda: not box_open(), 3000)
            until("HP and MP to refill", lambda: (ram(HP), ram(MP)) == (0x50, 0x50), 1200)
            settles()
            look("elixir-spent", "grey")
            print("ok: after the line it blinks a moment more, then stays grey: he owns it, but it will not catch him")

        if args.wants("guru"):
            launch(at="recharge-temple")
            wait(4)
            assert charges()["elixir"]["charge"] == USED, charges()
            p = look("temple-spent", "grey")
            # The temple is one screen wide: centred in the view, black either side below the band, its walls
            # at both of its edges.
            x = p.scene_x
            assert not p.lit(0, x, HUD, 240) and not p.lit(x + NATIVE_W, p.width, HUD, 240), "the temple is centred"
            assert p.lit(x, x + 8, HUD, 240) and p.lit(x + NATIVE_W - 8, x + NATIVE_W, HUD, 240), "the temple's edges"
            print(f"ok: the one-screen temple is centred at x {x}, under a status bar band at x {p.hud_x}")
            act("left", 170, 10)                                        # to the Guru
            act("up", 4, 10)
            until("the Guru to restore it", lambda: recharge.state()["guru_recharges"] == 1, 300)
            assert charges()["elixir"]["charge"] == READY and charges()["elixir"]["blink"] == 0, charges()
            until("his line", lambda: ram(MESSAGE_ID) == ELIXIR_LINE, 300)
            type_out("his line", 900, pauses=False)                    # no pause in it; the flag may be stale
            assert recharge.state()["line"] == 1, recharge.state()   # "Your Elixir is restored."
            p = look("elixir-restored", "colour")
            # His dialogue box (TextBox_SetCoordsForNPC with a portrait: tile 10, 16, 20 x 8) is on the
            # temple: its left side in the scene's column 10, the black inside from column 11.
            x = p.scene_x
            assert p.lit(x + 80, x + 88, 136, 184) and not p.lit(x + 88, x + 96, 136, 184), "his box on the room"
            page_through("the box to close", lambda: not box_open(), 3000)
            print("ok: Eolis's Guru restores the spent Elixir, in colour from his first line")

        if args.wants("original"):
            launch(on=False, at="eolis-town")
            game.poke({SELECTED_ITEM: WING_BOOTS, ITEM_INVENTORY: HOUR_GLASS, NUMBER_OF_ITEMS: 1,
                       SPECIAL_ITEMS: BIT_ELIXIR})
            wait(4)
            s = charges()
            assert not s["enabled"] and s["width"] == 0 and s["elixir"]["charge"] == NONE and s["x"] == -1, s
            p = Picture(game, "original")
            centred = (p.width - NATIVE_W) // 2 if p.width > NATIVE_W else 0
            assert p.hud_x == centred and p.status_bar_left() == centred + 8, (p.hud_x, p.status_bar_left())
            assert not p.lit(0, centred, 0, HUD) and not p.lit(centred + NATIVE_W, p.width, 0, HUD), \
                "nothing either side of the status bar"
            print(f"ok: with the remaster off, nothing either side of the status bar, which is where the original "
                  f"has it (x {centred} of a {p.width}-wide picture)")
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
