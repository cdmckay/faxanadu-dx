"""Play the title end to end in a hidden window and check it: DX on the logo,
and NEW GAME over CONTINUE.

    python3 tests/title/title_dx.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/title

The game runs uncapped (about 5x real time; --realtime to watch it at normal
speed) and the script waits on the game's own frames, never on the clock, on a
reserved TCP port (tests/e2e/fax_e2e.py), so any number can go in parallel.

Copies the build to OUT/game, then:
  1. With no saved file, screenshots the title with the remaster on and, on
     the same frame, with the whole remaster off (the original), and compares
     them pixel for pixel. The original is the stock title: START and CONTINUE
     side by side on row 160, in the title's letters (CHR_LETTERS, read from
     the ROM), the game's arrow before START, black between them and the
     logo, and no DX. The remaster's is the original's but for:
       - DX on the logo, just under FAXANADU's letters (lit on rows 116-122),
         centred on it: pixel for pixel the title's letters, in the light
         yellow of the logo's palette, with a black outline a pixel wide all
         round (the 8 neighbours); the logo shows everywhere else;
       - the tile with the tip of the logo's F (x 48-55, y 40-47), which the
         graphics fixes draw in the logo's palette (tests/graphics/
         graphics_fixes.py checks it);
       - the band between the logo and the notices (y 144-183): black, the
         original's START and CONTINUE hidden, but for NEW GAME over CONTINUE
         from x 96 on rows 151 and 165, in the title's letters and the
         original's white, CONTINUE grey with nothing to continue, and the
         game's own arrow, the original's pixel for pixel, 12 pixels before
         the chosen line.
     Down, Up, Select and Right leave the cursor and the arrow on NEW GAME:
     the grey CONTINUE is skipped.
  2. Opens NEW GAME's pick and moves its cursor: DX stays as it was, and the
     band above the pick is black (no arrow left beside NEW GAME).
  3. Starts a new game in file 1: the word goes at once and stays gone.
  4. With a saved file, the title opens on CONTINUE, now white, with the arrow
     beside it (across the original's hidden row); Up moves it to NEW GAME and
     Down back, each picture the expected one.
--shots DIR copies the pictures worth showing there, named after
--shot-prefix.
"""
from pathlib import Path
import shutil
import struct
import sys
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
from fax_e2e import FILES_NEW, FILES_TOP, Game, act, choose_file, frame, parser, tcp, until, wait  # noqa: E402

WORD = "DX"
LETTERS = 16 + 10 * 16384 + 0x0240                                  # CHR_LETTERS: bank 10, $8240, A to Z (StartScreen_Draw)
NATIVE = 256
LOGO_ROWS, LOGO_LEFT, LOGO_RIGHT = (16, 144), 16, 240               # the logo and its border, natively
F_TIP = (48, 40)                                                    # the tile with the F's tip (graphics fixes)
WORD_AT = (120, 116)                                                # DX's top-left, natively
BAND = (144, 184)                                                   # between the logo and the notices
START_AT, CONTINUE_AT, ARROW_AT = (64, 160), (128, 160), (52, 160)  # the original's choices and its arrow, lit from 160
MENU_X, NEW_GAME_Y, CONTINUE_Y, ARROW_GAP = 96, 151, 165, 12        # the remaster's
BLACK = (0, 0, 0)
DX = ("faxanadu.dx", "dx")


def title():
    return tcp("title_state")


def files():
    return tcp("files_state")


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


def text_mask(rom, text):
    """The pixels of `text` (A-Z and spaces) in the title's own letters, the
    tiles StartScreen_Draw puts at $E0-$F9 of its pattern table: {(dx, dy)}
    from its top left. They are drawn in one color, 3."""
    lit_ = set()
    for i, ch in enumerate(text):
        if ch == " ":
            continue
        tile = rom[LETTERS + (ord(ch) - ord("A")) * 16:LETTERS + (ord(ch) - ord("A")) * 16 + 16]
        for dy in range(8):
            for dx in range(8):
                value = (tile[dy] >> (7 - dx) & 1) | (tile[dy + 8] >> (7 - dx) & 1) << 1
                assert value in (0, 3), f"{ch}'s tile has color {value}"
                if value:
                    lit_.add((i * 8 + dx, dy))
    return lit_


def outline(mask):
    """The pixels next to `mask`'s, the diagonals too, that are not its own."""
    return {(x + dx, y + dy) for x, y in mask for dx in (-1, 0, 1) for dy in (-1, 0, 1)} - mask


def rgb(argb):
    return (argb >> 16 & 255, argb >> 8 & 255, argb & 255)


def lit(px, origin, x0, y0, x1, y1):
    """The pixels that are not black in native [x0, x1) x [y0, y1): {(x, y): color}, natively."""
    return {(x, y): px[y][origin + x] for y in range(y0, y1) for x in range(x0, x1) if px[y][origin + x] != BLACK}


def original_title(path, rom):
    """The original's title, checked to be the stock one; its pixels padded to
    Seamless's 424 columns (the native picture centred, as the remaster's
    title is), its white and its arrow ({(dx, dy) from ARROW_AT: color})."""
    w, h, px = png(path)
    assert (w, h) == (NATIVE, 240), (w, h)
    # START and CONTINUE side by side on row 160, the arrow before START, and
    # black from the logo down to them: the stock layout.
    choices = lit(px, 0, 60, BAND[0], NATIVE, 176)
    want = {(START_AT[0] + dx, START_AT[1] + dy) for dx, dy in text_mask(rom, "START")} | \
        {(CONTINUE_AT[0] + dx, CONTINUE_AT[1] + dy) for dx, dy in text_mask(rom, "CONTINUE")}
    assert set(choices) == want, f"{path.name}: START / CONTINUE are not the stock ones ({len(set(choices) ^ want)} pixels)"
    white = {choices[p] for p in want}
    assert len(white) == 1, white
    arrow = {(x - ARROW_AT[0], y - ARROW_AT[1]): c for (x, y), c in lit(px, 0, 0, BAND[0], 60, 176).items()}
    assert arrow and all(0 <= dx < 8 and 0 <= dy < 8 for dx, dy in arrow), f"{path.name}: no arrow before START: {arrow}"
    assert not lit(px, 0, 0, BAND[0], NATIVE, START_AT[1]), f"{path.name}: the original draws between the logo and START"
    pad = (424 - NATIVE) // 2
    padded = [[BLACK] * pad + row + [BLACK] * (424 - NATIVE - pad) for row in px]
    print(f"ok: {path.name}: the original's title: START / CONTINUE side by side on row 160, the arrow at "
          f"x {ARROW_AT[0]}, black between them and the logo, no DX")
    return padded, white.pop(), arrow


def expected(original, origin, rom, state, white, arrow, cursor, grey):
    """The remaster's title, from the original's: DX and its outline on the
    logo, the band black but for NEW GAME over CONTINUE (`grey` its color, or
    None for white) and the arrow beside line `cursor`."""
    px = [row[:] for row in original]
    for y in range(*BAND):
        for x in range(NATIVE):
            px[y][origin + x] = BLACK
    for text, y, color in (("NEW GAME", NEW_GAME_Y, white), ("CONTINUE", CONTINUE_Y, grey or white)):
        for dx, dy in text_mask(rom, text):
            px[y + dy][origin + MENU_X + dx] = color
    row = CONTINUE_Y if cursor else NEW_GAME_Y
    for (dx, dy), color in arrow.items():
        px[row + dy][origin + MENU_X - ARROW_GAP + dx] = color
    mask = text_mask(rom, WORD)
    x0, y0 = state["x"], state["y"]
    for dx, dy in outline(mask):
        px[y0 + dy][x0 + dx] = rgb(state["outline_argb"])
    for dx, dy in mask:
        px[y0 + dy][x0 + dx] = rgb(state["argb"])
    return px


def compare(path, want, origin):
    """The screenshot at `path` is `want`, but for the F's tip."""
    w, h, px = png(path)
    assert (w, h) == (len(want[0]), len(want)), (w, h)
    tip = {(origin + F_TIP[0] + dx, F_TIP[1] + dy) for dy in range(8) for dx in range(8)}
    diff = [(x - origin, y) for y in range(h) for x in range(w) if px[y][x] != want[y][x] and (x, y) not in tip]
    assert not diff, f"{path.name}: {len(diff)} native pixels differ from the expected title, first {diff[:6]}"
    return px


def main():
    ap = parser(__doc__)
    ap.add_argument("--shots", type=Path, help="copy the pictures worth showing here")
    ap.add_argument("--shot-prefix", default="", help="prefix for the copied pictures' names")
    args = ap.parse_args()
    g = Game.from_args(args)
    rom = g.image
    mask = text_mask(rom, WORD)
    assert len(mask) > 40, "no letters where the title's tiles should be"
    worth = {}

    def launch(remaster):
        """Start the game with the whole remaster on or off."""
        g.set_features({DX: remaster})
        g.launch()
        assert title()["enabled"] == remaster and files()["enabled"] == remaster, title()

    def shot(name, keep=None):
        path = g.shot(name)
        if keep:
            worth[keep] = path
        return path

    def title_at():
        """With the remaster on: wait for the title, then a little; the frame."""
        until("the title", lambda: title()["shown"], 2000)
        wait(10)
        assert title()["shown"] and files()["shown"], "the title's menu is not up"
        return frame()

    def press(button, cursor):
        act(button)
        state = files()
        assert (state["mode"], state["cursor"]) == (FILES_TOP, cursor), (button, state)
        wait(2)                                                     # the arrow shows a frame after it is drawn

    try:
        # 1. The remaster's title, with nothing to continue (checked against
        # the original's below, on the same frame: the title is still from the
        # frame it is drawn, and drawn on the same frame in every run).
        launch(True)
        at = title_at()
        state = title()
        origin = (state["width"] - NATIVE) // 2
        assert state["word"] == WORD and (state["x"] - origin, state["y"]) == WORD_AT, state
        assert (files()["cursor"], files()["can_continue"]) == (0, False), files()
        no_file = shot("title-no-file", "title-no-file")
        for button in ("down", "up", "select", "right"):
            press(button, 0)
        skipped = shot("title-no-file-pressed")
        # 2. NEW GAME's pick, and its cursor moving: the word stays as it was.
        _, _, before = png(no_file)
        for button, want in (("start", 0), ("down", 1)):
            act(button)
            until(f"the pick's cursor on file {want + 1}",
                  lambda: (lambda s: s["mode"] == FILES_NEW and s["cursor"] == want)(files()), 120)
            wait(2)
            assert title()["shown"]
            _, _, px = png(shot(f"pick-{button}"))
            assert all(px[y][x] == before[y][x] for y in range(BAND[0]) for x in range(len(px[0]))), "the word changed"
            assert not lit(px, origin, 0, BAND[0], NATIVE, 160), "the band above the pick is not black"
        print("ok: opening NEW GAME's pick and moving its cursor leave the word as it was; the band above it is black")
        # 3. A new game in file 1: the word goes at once, and stays gone.
        choose_file(True, 1)
        until("the title to go", lambda: not title()["shown"], 10)
        wait(20)
        assert not title()["shown"]
        wait(1500)
        assert not title()["shown"] and title()["seen"] == 255
        _, _, px = png(shot("eolis"))
        assert not all(px[state["y"] + dy][state["x"] + dx] == rgb(state["argb"]) for dx, dy in mask), "DX is drawn"
        print("ok: starting a game takes the word away; it does not come back in the game")
        g.close()

        # The original, on the same frame.
        launch(False)
        until(f"frame {at}", lambda: frame() >= at, at + 600)
        assert not title()["shown"] and not files()["shown"]
        original, white, arrow = original_title(shot("original-title", "original-title"), rom)
        g.close()
        _, _, px = png(no_file)
        grey = next(px[CONTINUE_Y + dy][origin + MENU_X + dx] for dx, dy in sorted(text_mask(rom, "C")))
        assert grey not in (BLACK, white), f"CONTINUE is {grey}, not grey"
        assert any(original[y][x] == grey for y in range(*LOGO_ROWS) for x in range(len(original[0]))), \
            f"{grey} is not the stars' grey"
        want = expected(original, origin, rom, state, white, arrow, 0, grey)
        compare(no_file, want, origin)
        compare(skipped, want, origin)
        x0, y0 = state["x"] - origin, state["y"]
        rows = sorted({y0 + dy for _, dy in mask})
        left, right = min(x0 + dx for dx, _ in mask), max(x0 + dx for dx, _ in mask)
        assert (rows[0], rows[-1]) == (116, 122) and abs((left + right) / 2 - (LOGO_LEFT + LOGO_RIGHT - 1) / 2) <= 1
        assert rgb(state["outline_argb"]) == BLACK
        assert any(original[y][x] == rgb(state["argb"]) for y in range(*LOGO_ROWS) for x in range(len(original[0])))
        print(f"ok: {no_file.name}: DX at ({x0}, {y0}) on the logo, lit on rows {rows[0]}-{rows[-1]}, x {left}-{right}, "
              f"in the logo's {rgb(state['argb'])}, outlined in black; the logo is the original's elsewhere, but for "
              f"its F's tip")
        print(f"ok: {no_file.name}: NEW GAME (white) over CONTINUE (grey, {grey}) from x {MENU_X} on rows "
              f"{NEW_GAME_Y} and {CONTINUE_Y}, the original's START / CONTINUE hidden, its arrow at x "
              f"{MENU_X - ARROW_GAP} beside NEW GAME; the notices are the original's")
        print("ok: Down, Up, Select and Right skip the grey CONTINUE: the cursor and the arrow stay on NEW GAME")

        # 4. A saved file: the title opens on CONTINUE; Up and Down move.
        (g.out / "game" / "faxanadu_file1.txt").write_text("version = 1\ntitle = 3\ngold = 1234\nspawn = 2\nfurthest = 2\n")
        launch(True)
        title_at()
        assert (files()["cursor"], files()["can_continue"]) == (1, True), files()
        compare(shot("title-file", "title-file"), expected(original, origin, rom, state, white, arrow, 1, None), origin)
        press("up", 0)
        compare(shot("title-file-up"), expected(original, origin, rom, state, white, arrow, 0, None), origin)
        press("down", 1)
        compare(shot("title-file-down"), expected(original, origin, rom, state, white, arrow, 1, None), origin)
        print("ok: with a saved file the title opens on CONTINUE, white, the arrow beside it across the original's "
              "hidden row; Up moves it to NEW GAME and Down back")
        print("screenshots:", ", ".join(g.shots))
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
