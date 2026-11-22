"""Play REMASTERED on the title end to end in a hidden window and check it.

    python3 tests/title/title_remastered.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/title

The game runs uncapped (about 5x real time; --realtime to watch it at normal
speed) and the script waits on the game's own frames, never on the clock, on a
reserved TCP port (tests/e2e/fax_e2e.py), so any number can go in parallel.

Copies the build to OUT/game, then:
  1. Screenshots the title with the remaster on (REMASTERED over NEW GAME
     and CONTINUE) and, on the same frame, with the whole remaster off (the
     original title, START / CONTINUE and the notices), and compares them. The
     logo is the same to the pixel, but for the tile with the tip of its F
     (x 48-55, y 40-47), which the graphics fixes draw in the logo's palette
     instead of the stars' grey (tests/graphics/graphics_fixes.py checks its
     colors). Between it and what is below (y 144-159),
     the original is black and the remaster differs only by the word: pixel
     for pixel the glyphs of the game's font (read from the ROM), in the light
     yellow of the logo's palette, centered under the logo, with at least 4
     black rows above it and below it, down to NEW GAME and CONTINUE in the
     one picture and to START / CONTINUE in the other, both from y 160.
  2. Opens NEW GAME's file pick, and moves its cursor: the word stays as it
     was.
  3. Starts a new game in file 1: the word goes at once and stays gone.
With the remaster on, Save files is always on, so the word never shows over
START / CONTINUE. To show where it would sit, the run also writes the original
title with the word's pixels painted in from the remaster's picture
(start-continue-composite.png): a composite, not a screenshot.
--shots DIR copies the pictures worth showing there, named after
--shot-prefix.
"""
from pathlib import Path
import shutil
import struct
import sys
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
from fax_e2e import FILES_NEW, Game, act, choose_file, frame, parser, tcp, until, wait  # noqa: E402

WORD = "REMASTERED"
GLYPHS = 16 + 13 * 16384                                            # TILE_GLYPHS_START: bank 13, $8000, from $20
LOGO_ROWS, LOGO_LEFT, LOGO_RIGHT = (16, 144), 16, 240               # the logo and its border, natively
F_TIP = (48, 40)                                                    # the tile with the F's tip (graphics fixes)
BAND = (144, 160)                                                   # black between the logo and the title's choices
CLEAR = 4                                                           # rows of black the word keeps from the art
REMASTER = ("faxanadu.remastered", "remastered")


def title():
    return tcp("title_state")


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


def write_png(path, rows):
    """Rows of (r, g, b) tuples as an RGB PNG."""
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    raw = b"".join(b"\0" + bytes(c for px in row for c in px) for row in rows)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", len(rows[0]), len(rows), 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def word_mask(rom):
    """The pixels of WORD in the game's font: {(dx, dy)} from its top left."""
    lit = set()
    for i, ch in enumerate(WORD):
        glyph = rom[GLYPHS + (ord(ch) - 0x20) * 8:GLYPHS + (ord(ch) - 0x20) * 8 + 8]
        for dy in range(8):
            for dx in range(8):
                if glyph[dy] >> (7 - dx) & 1:
                    lit.add((i * 8 + dx, dy))
    return lit


def rgb(argb):
    return (argb >> 16 & 255, argb >> 8 & 255, argb & 255)


def first_below(px, origin, row):
    """The first row from `row` down with anything but black in the native picture."""
    return min(y for y in range(row, len(px)) for x in range(origin, origin + 256) if px[y][x] != (0, 0, 0))


def compare(remaster, original, state, mask):
    """The remaster's title against the original's on the same frame: the same
    logo, and between it and what is below, only the word, where it should be."""
    w, h, on = png(remaster)
    w2, h2, off = png(original)
    # The remaster plays in Seamless, whose picture is 424x240 with the title,
    # as every scene it does not lay out, at the native picture's place:
    # centred. The original's is the NES's 256x240: centred the same here.
    pad = (w - w2) // 2
    off = [[(0, 0, 0)] * pad + row + [(0, 0, 0)] * (w - w2 - pad) for row in off]
    assert (w, h) == (424, h2) and w2 == 256 and w == state["width"], ((w, h), (w2, h2), state)
    color = rgb(state["argb"])
    x0, y0 = state["x"], state["y"]
    # The logo, and everything above the band, untouched but for the F's tip.
    origin = next(x for x in range(w) if any(off[y][x] != (0, 0, 0) for y in range(*LOGO_ROWS))) - LOGO_LEFT
    tip = {(origin + F_TIP[0] + dx, F_TIP[1] + dy) for dy in range(8) for dx in range(8)}
    above = [(x, y) for y in range(BAND[0]) for x in range(w) if on[y][x] != off[y][x] and (x, y) not in tip]
    assert not above, f"{remaster.name}: {len(above)} pixels above y {BAND[0]} differ from the original, first {above[0]}"
    # In the band: the original is black, and the remaster only adds the word.
    lit = [(x, y) for y in range(*BAND) for x in range(w) if off[y][x] != (0, 0, 0)]
    assert not lit, f"{original.name}: the original draws in y {BAND[0]}-{BAND[1] - 1}, at {lit[0]}"
    diff = {(x, y) for y in range(*BAND) for x in range(w) if on[y][x] != off[y][x]}
    expect = {(x0 + dx, y0 + dy) for dx, dy in mask}
    assert diff == expect, f"{remaster.name}: {len(diff ^ expect)} pixels differ from the word in the game's font"
    assert all(on[y][x] == color for x, y in diff), f"{remaster.name}: the word is not all {color}"
    top, bottom = min(y for _, y in diff), max(y for _, y in diff)
    left, right = min(x for x, _ in diff), max(x for x, _ in diff)
    # Where the logo ends, and what is below it in each picture.
    logo_bottom = max(y for y in range(BAND[0]) for x in range(origin + LOGO_LEFT, origin + LOGO_RIGHT)
                      if off[y][x] != (0, 0, 0))
    menu_top, start_top = first_below(on, origin, bottom + 1), first_below(off, origin, BAND[0])
    assert logo_bottom == 143 and start_top == 160 and menu_top == 160, (logo_bottom, start_top, menu_top)
    assert top - logo_bottom > CLEAR and menu_top - bottom > CLEAR, (logo_bottom, top, bottom, menu_top)
    # Centered under the logo, to a pixel, in the logo's own color.
    centre = (left + right) / 2 - origin
    assert abs(centre - (LOGO_LEFT + LOGO_RIGHT - 1) / 2) <= 1, (left, right, origin)
    assert any(off[y][x] == color for y in range(*LOGO_ROWS) for x in range(w)), f"{color} is not in the logo"
    print(f"ok: {remaster.name}: REMASTERED at ({x0}, {y0}) in {color}, rows {top}-{bottom}; the logo is the "
          f"original's but for its F's tip; {top - logo_bottom - 1} black rows under the logo, {menu_top - bottom - 1} above "
          f"NEW GAME and CONTINUE, as above the original's START / CONTINUE")
    return [[on[y][x] if (x, y) in diff else off[y][x] for x in range(w)] for y in range(h)]


def absent(path, state, mask):
    """The word is not drawn where it would be."""
    w, h, px = png(path)
    color = rgb(state["argb"])
    x0, y0 = state["x"], state["y"]
    assert not all(px[y0 + dy][x0 + dx] == color for dx, dy in mask if y0 + dy < h and x0 + dx < w), \
        f"{path.name}: REMASTERED is drawn"


def main():
    ap = parser(__doc__)
    ap.add_argument("--shots", type=Path, help="copy the pictures worth showing here")
    ap.add_argument("--shot-prefix", default="", help="prefix for the copied pictures' names")
    args = ap.parse_args()
    g = Game.from_args(args)
    mask = word_mask(g.image)
    assert len(mask) > 100, "no glyphs where the font should be"
    worth = {}

    def launch(remaster):
        """Start the game with the whole remaster on or off."""
        g.set_features({REMASTER: remaster})
        g.launch()
        assert title()["enabled"] == remaster and tcp("files_state")["enabled"] == remaster, title()

    def shot(name, keep=None):
        path = g.shot(name)
        if keep:
            worth[keep] = path
        return path

    def title_at():
        """With the remaster on: wait for the title, then a little; the frame."""
        until("the title", lambda: title()["shown"], 2000)
        wait(10)
        assert title()["shown"] and tcp("files_state")["shown"], "the title's menu is not up"
        return frame()

    try:
        # 1. The remaster's title against the original's, on the same frame
        # (the title is still from the frame it is drawn, and drawn on the
        # same frame in every run).
        launch(True)
        at = title_at()
        state = title()
        remaster = shot("new-game-continue", "new-game-continue")
        # 2. NEW GAME's pick, and its cursor moving; the word stays as it was.
        _, _, a = png(remaster)
        for button, want in (("start", 0), ("down", 1)):
            act(button)
            until(f"the pick's cursor on file {want + 1}",
                  lambda: (lambda s: s["mode"] == FILES_NEW and s["cursor"] == want)(tcp("files_state")), 120)
            wait(10)
            assert title()["shown"]
            _, _, b = png(shot(f"pick-{button}"))
            assert a != b, f"{button} changed nothing"
            assert all(a[y][x] == b[y][x] for y in range(*BAND) for x in range(len(a[0]))), "the word changed"
            a = b
        print("ok: opening NEW GAME's pick and moving its cursor leave the word as it was")
        g.close()
        launch(False)
        until(f"frame {at}", lambda: frame() >= at, at + 600)
        assert not title()["shown"]
        original = shot("original-title", "original-title")
        g.close()
        composite = compare(remaster, original, state, mask)
        write_png(g.out / "start-continue-composite.png", composite)
        worth["start-continue-composite"] = g.out / "start-continue-composite.png"

        # 3. A new game in file 1: the word goes at once, and stays gone.
        launch(True)
        title_at()
        assert tcp("files_state")["cursor"] == 0
        choose_file(True, 1)
        until("the title to go", lambda: not title()["shown"], 10)
        wait(20)
        assert not title()["shown"]
        absent(shot("intro"), state, mask)
        wait(1500)
        assert not title()["shown"] and title()["seen"] == 255
        absent(shot("eolis"), state, mask)
        print("ok: starting a game takes the word away; it does not come back in the game")
        print("screenshots:", ", ".join(g.shots), "and start-continue-composite.png (a composite)")
        if args.shots:
            args.shots.mkdir(parents=True, exist_ok=True)
            for name, path in worth.items():
                shutil.copy2(path, args.shots / f"{args.shot_prefix}{name}.png")
    except BaseException:
        if g.running():
            g.shot("failure")
        raise
    finally:
        g.close()


if __name__ == "__main__":
    sys.exit(main())
