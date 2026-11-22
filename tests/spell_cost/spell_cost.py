"""Play Spell cost end to end: the M: bar shows what the selected spell will cost.

    python3 tests/spell_cost/spell_cost.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/spell-cost

From the saved game outside Eolis's gate (the fixture "gate"), with MP and
the selected spell set through save-state RAM (an external prerequisite, not
coverage), a real cast (Up+B, Deluge) draws the bar for the MP it leaves: the
game draws the bar only when Player_SetMP runs, so a poke of MP alone would
leave it as it was. The checks are on the screenshots' pixels: the M: bar's
four fill lines, one pixel an MP from the band's column 3, each pixel the
bar's green, the lighter or darker shade the feature reports, or the empty
bar's black, and nothing else in the status bar band changed.

1. cost: at 40 MP, each spell selected in turn: the last 2, 4, 6, 10 and 16
   pixels of the fill (Deluge, Thunder, Fire, Death, Tilte) a step lighter,
   the 40 - cost before them the plain green. No spell selected: the plain
   bar. Then Fire cast at 40: once the game redraws the bar for 34, its last
   6 pixels are the lighter ones, so the span tracks MP.
2. short: at 8 MP, Tilte (16) and Death (10) will not go off, and the whole
   fill is a step darker; Fire (6) can, and its last 6 pixels are lighter.
3. drain: at 40 MP with Fire selected, the Hour Glass (Down+B) takes an MP
   every 24 frames (#124): at each MP the game has drawn the bar for, Fire's
   6 pixels are the fill's last, lower and lower as it drains.
4. original: the remaster off, Fire cast at 42: the bar is the game's, 36
   pixels of the plain green and nothing lighter or darker.
"""
from pathlib import Path
import struct
import sys
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
from fax_e2e import Game, batch, box_open, fixture, page_through, parse_args, ram, tcp, until, wait  # noqa: E402

PARTS = ("cost", "short", "drain", "original")
PACKAGE = ("faxanadu.dx", "dx")
MP, SELECTED_MAGIC, CAST_MAGIC_TYPE = 0x39A, 0x3C0, 0x2B3
DELUGE, THUNDER, FIRE, DEATH, TILTE, NO_SPELL = 0, 1, 2, 3, 4, 0xFF
SELECTED_ITEM, HOUR_GLASS, PLAYER_FLAGS, MESSAGE_ID, USED_HOUR_GLASS = 0x3C1, 0x0D, 0xA4, 0x213, 0xAC
GLASS_STEP = 24                                            # fax_recharge.c: GLASS_FRAMES_PER_MP
NAMES = ("deluge", "thunder", "fire", "death", "tilte")
COSTS = (2, 4, 6, 10, 16)                                  # MAGIC_COSTS ($B7A9, bank 14)
BAR_X, FILL_TOP, FILL_LINES, BAR_MAX = 24, 18, 4, 80       # fax_spell_cost.h; UI_MPAndHPBarWidth 10 tiles
BORDER = (17, 22)                                          # the bar's white lines above and below its fill
HUD = 32
BLACK = (0, 0, 0)


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


def rgb(value):
    return (value >> 16 & 255, value >> 8 & 255, value & 255)


class Bar:
    """A screenshot's M: bar: each pixel of its fill, what the four fill
    lines show there, against the band's left edge as Seamless laid it out."""

    def __init__(self, game, name, path=None):
        self.path = path or game.shot(name)
        self.width, self.height, self.rows = png(self.path)
        self.x = tcp("fax_world")["hud_x"] + BAR_X

    def at(self, x, y):
        return tuple(self.rows[y][x * 3:x * 3 + 3])

    def fill(self):
        """Each of the bar's BAR_MAX pixels: the one color its four fill lines show."""
        out = []
        for i in range(BAR_MAX):
            seen = {self.at(self.x + i, FILL_TOP + r) for r in range(FILL_LINES)}
            assert len(seen) == 1, (self.path.name, i, seen)
            out.append(seen.pop())
        return out

    def lit(self):
        """How many of the bar's pixels are filled: the MP the game last drew it for."""
        return sum(c != BLACK for c in self.fill())

    def border(self):
        return {self.at(self.x + i, y) for i in range(BAR_MAX) for y in BORDER}

    def band(self):
        return self.rows[:HUD]


def expect(plain, shade, mp, span):
    """The fill a player should see: `span` (from, to) in `shade`, the rest of the MP's pixels `plain`, then black."""
    lo, hi = span
    return [shade if lo <= i < hi else plain if i < mp else BLACK for i in range(BAR_MAX)]


def runs(fill):
    """A fill as runs of one color: [(color, count), ...], for messages."""
    out = []
    for c in fill:
        if out and out[-1][0] == c:
            out[-1][1] += 1
        else:
            out.append([c, 1])
    return [("%02x%02x%02x" % c, n) for c, n in out]


def state():
    return tcp("spell_cost_state")


def up_b():
    """Up+B the stock way: both go down in the same frame, Up held a little longer."""
    batch(("action", dict(name="up", frames=10)), ("action", dict(name="b", frames=4)))


def main():
    args = parse_args(__doc__, parts=PARTS)
    game = Game.from_args(args)
    out = game.out

    def launch(on=True):
        game.close()
        game.set_features({PACKAGE: on})
        game.launch(at="gate")
        assert state()["enabled"] == on, state()

    def load(path):
        batch(("load_state", dict(path=str(path))))

    def cast_to(mp, spell, name):
        """From the gate with mp + the spell's cost MP and `spell` selected, cast
        it: the game draws the bar for `mp`. Saved once the spell is gone and
        the bar drawn, so another spell can be cast from it."""
        start = out / f"{name}-from.cycstate"
        fixture(game.state("gate"), start, {MP: mp + COSTS[spell], SELECTED_MAGIC: spell, CAST_MAGIC_TYPE: 0xFF})
        load(start)
        up_b()
        until("the cast", lambda: ram(MP) < mp + COSTS[spell], 60)
        until("the spell to end", lambda: ram(CAST_MAGIC_TYPE) == 0xFF, 600)
        wait(4)
        assert ram(MP) == mp, f"{ram(MP)} MP, not {mp}"
        return game.save(name)

    def selecting(base, spell, name):
        """The state `base` with `spell` selected, two frames on: its bar."""
        path = out / f"{name}.cycstate"
        fixture(base, path, {SELECTED_MAGIC: spell})
        load(path)
        wait(2)
        s = state()
        assert s["mp"] == ram(MP) and s["selected"] == spell and s["hud"], s
        return s, Bar(game, name)

    def only_the_fill(bar, plain, span):
        """Nothing in the status bar band but the span's fill pixels differs from the plain bar's picture."""
        lo, hi = span
        for y, (row, base) in enumerate(zip(bar.band(), plain.band())):
            if row == base:
                continue
            for x in range(bar.width):
                if row[x * 3:x * 3 + 3] != base[x * 3:x * 3 + 3]:
                    assert FILL_TOP <= y < FILL_TOP + FILL_LINES and lo <= x - bar.x < hi, \
                        (bar.path.name, "changed at", x, y)

    try:
        gate = game.state("gate")
        assert gate.exists()

        if args.wants("cost"):
            launch()
            mp40 = cast_to(40, DELUGE, "mp40")
            s, plain = selecting(mp40, NO_SPELL, "no-spell")
            green, bar_green = plain.fill()[0], s["fill"]
            assert s["kind"] == "none" and not s["shown"] and s["x"] == plain.x, s
            assert plain.fill() == expect(green, None, 40, (0, 0)), runs(plain.fill())
            assert green == rgb(s["fill_rgb"]) and green != BLACK and plain.border() == {(255, 255, 255)}, \
                (green, s, plain.border())
            print(f"ok: no spell selected, the bar is the game's: 40 pixels of its green {s['fill']:#04x} "
                  f"(#{'%02x%02x%02x' % green}) from x {plain.x}, then black")
            for spell in (DELUGE, THUNDER, FIRE, DEATH, TILTE):
                cost = COSTS[spell]
                s, bar = selecting(mp40, spell, f"{NAMES[spell]}-at-40")
                light = rgb(s["shade_rgb"])
                assert s["kind"] == "cast" and (s["from"], s["to"]) == (40 - cost, 40) and s["shown"], s
                assert s["cost"] == cost and s["fill"] == bar_green and s["shade"] == s["fill"] + 0x10, s
                assert sum(light) > sum(green) and light[1] == max(light), (light, green)   # lighter, still green
                assert bar.fill() == expect(green, light, 40, (40 - cost, 40)), runs(bar.fill())
                assert bar.border() == plain.border(), "the bar's border"
                only_the_fill(bar, plain, (40 - cost, 40))
            print(f"ok: at 40 MP each spell's cost is the fill's last pixels, a step lighter ({s['shade']:#04x}, "
                  f"#{'%02x%02x%02x' % light}): " +
                  ", ".join(f"{NAMES[k]} {40 - COSTS[k]}-39" for k in range(5)) + "; nothing else changed")
            # Fire cast at 40: the span follows MP once the game has drawn the bar for 34.
            path = out / "fire-from-40.cycstate"
            fixture(mp40, path, {SELECTED_MAGIC: FIRE})
            load(path)
            up_b()
            until("Fire's cast", lambda: ram(MP) == 34, 60)
            wait(6)
            s, bar = state(), Bar(game, "fire-cast-to-34")
            assert s["kind"] == "cast" and (s["from"], s["to"]) == (28, 34) and s["mp"] == 34, s
            assert bar.fill() == expect(green, light, 34, (28, 34)), runs(bar.fill())
            print("ok: Fire cast at 40 takes the bar to 34, and its cost moves with it: pixels 28-33 lighter")

        if args.wants("short"):
            launch()
            mp8 = cast_to(8, DELUGE, "mp8")
            s, plain = selecting(mp8, NO_SPELL, "no-spell-at-8")
            green = plain.fill()[0]
            assert s["kind"] == "none" and plain.fill() == expect(green, None, 8, (0, 0)), (s, runs(plain.fill()))
            for spell in (TILTE, DEATH):
                s, bar = selecting(mp8, spell, f"{NAMES[spell]}-at-8")
                dark = rgb(s["shade_rgb"])
                assert s["kind"] == "short" and (s["from"], s["to"]) == (0, 8) and s["shown"], s
                assert s["shade"] == s["fill"] - 0x10 and sum(dark) < sum(green) and dark[1] == max(dark), (s, dark)
                assert bar.fill() == expect(green, dark, 8, (0, 8)), runs(bar.fill())
                only_the_fill(bar, plain, (0, 8))
            print(f"ok: at 8 MP Tilte (16) and Death (10) will not go off: the whole fill a step darker "
                  f"({s['shade']:#04x}, #{'%02x%02x%02x' % dark})")
            s, bar = selecting(mp8, FIRE, "fire-at-8")
            assert s["kind"] == "cast" and (s["from"], s["to"]) == (2, 8), s
            assert bar.fill() == expect(green, rgb(s["shade_rgb"]), 8, (2, 8)), runs(bar.fill())
            print("ok: at 8 MP Fire (6) can: its pixels 2-7 lighter, 0-1 the plain green")

        if args.wants("drain"):
            launch()
            mp40 = cast_to(40, DELUGE, "mp40")
            path = out / "glass.cycstate"
            fixture(mp40, path, {SELECTED_MAGIC: FIRE, SELECTED_ITEM: HOUR_GLASS})
            load(path)
            until("the hero to stand", lambda: not ram(PLAYER_FLAGS) & 0x05, 120)
            tcp("action", name="down", frames=60)                    # Down+B, the selected item
            wait(6)
            tcp("action", name="b", frames=4)
            until("I've used Hour Glass.", lambda: ram(MESSAGE_ID) == USED_HOUR_GLASS, 120)
            page_through("the box to close", lambda: not box_open(), 3000)
            assert tcp("recharge_state")["glass_running"], tcp("recharge_state")
            light = seen = None
            for _ in range(3):
                start = ram(MP)
                until("an MP to drain", lambda: ram(MP) < start, GLASS_STEP + 4)
                for _ in range(4):
                    wait(2)                                        # the game draws the bar the frame after
                    # The picture and what the feature drew in it, from the same frame: one the bar
                    # shows its MP in (a frame the MP went, its bar still to come, is taken again).
                    path = out / f"{len(game.shots):02d}-hour-glass-{ram(MP)}.png"
                    _, s = batch(("screenshot", dict(layer="picture", path=str(path))), ("spell_cost_state", {}))
                    bar = Bar(game, None, path)
                    if bar.lit() == s["mp"]:
                        break
                    path.unlink()
                game.shots.append(path.name)
                mp, light = s["mp"], rgb(s["shade_rgb"])
                green = rgb(s["fill_rgb"])
                assert s["kind"] == "cast" and (s["from"], s["to"]) == (mp - 6, mp) and bar.lit() == mp, (s, bar.lit())
                assert bar.fill() == expect(green, light, mp, (mp - 6, mp)), runs(bar.fill())
                assert seen is None or mp < seen, (mp, seen)
                seen = mp
            print(f"ok: the Hour Glass drains MP, and Fire's 6 pixels stay the fill's last, down to {seen} MP "
                  f"(pixels {seen - 6}-{seen - 1})")

        if args.wants("original"):
            launch(on=False)
            start = out / "original-from.cycstate"
            fixture(gate, start, {MP: 42, SELECTED_MAGIC: FIRE, CAST_MAGIC_TYPE: 0xFF})
            load(start)
            up_b()
            until("Fire's cast", lambda: ram(MP) == 36, 60)
            wait(6)
            s, bar = state(), Bar(game, "original")
            assert not s["enabled"] and s["x"] == -1 and s["kind"] == "none" and not s["shown"], s
            fill = bar.fill()
            green = fill[0]
            assert green != BLACK and fill == expect(green, None, 36, (0, 0)), runs(fill)
            print(f"ok: with the remaster off, Fire cast at 42 leaves the game's bar: 36 pixels of its green "
                  f"(#{'%02x%02x%02x' % green}) from x {bar.x}, nothing lighter or darker")
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
