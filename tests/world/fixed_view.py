"""Seamless's view end to end: one 424x240 picture, whatever the window, and
the host's one fixed scale for it (#107).

    python3 tests/world/fixed_view.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/view

Plays Seamless, the remaster's view, in Eolis's town, from a saved game:

1. At the test window's size (424x240, config.ini's scale 1 of the view),
   the world canvas (fax_world's size) and the picture are 424x240.
2. The Select menu (Start, with the remaster: #147) holds the picture still,
   the town around it. Resized to 1920x1080, 1280x720, a tall 1080x1920 and
   a narrow 300x600, the canvas and the picture are still 424x240: the
   window changes how big the view is, never what it shows.
3. At each size, what the window presents (the ui layer: the whole drawable)
   is that picture by the host's fixed scale (cyc_present_fit.h, which fit()
   below follows), though config.ini has IntegerScale on (the default) and
   LinearFilter on too: neither applies to Seamless's view.
   - 1920x1080: sharp 4.5x, 1908x1080 at (6, 0): nearest-neighbour to 5x,
     then linear down. Every output pixel that falls inside one picture
     pixel is that pixel's exact colour, as nearest-neighbour's would be (a
     tile's inside, at 4.5x); the others, at most one at each edge between
     two picture pixels, lie between those two. Black bars either side.
   - 1280x720: 3x, 1272x720 at (4, 0), each pixel a 3x3 block.
   - 1080x1920: letterboxed, 1080x611 at (0, 654), sharp at 2.55x.
   - 300x600: smaller than the view, so the whole of it, linearly down to
     300x169 at (0, 215).
   (presented() checks each against the picture, pixel by pixel.)
4. F4's connected-map overview grows the canvas to the region, and the
   1920x1080 window presents the whole of it by the same scale; F4 again
   brings back 424x240.
5. The original (the remaster off) keeps the player's Display settings: at
   1920x1080 its 256x240 picture is IntegerScale's 4x, 1024x960 at
   (448, 60), each pixel a 4x4 block.
"""
from pathlib import Path
import struct
import sys
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
from fax_e2e import CONFIG, Game, box_open, parse_args  # noqa: E402

VIEW = (424, 240)
PAUSED = 0x120                       # Game_PausedState
RW, RH, HUD = 256, 208, 32           # a room's playfield, and the HUD above it
PACKAGE = ("faxanadu.dx", "dx")      # the one switch: off is the original game
LEAN = 64                            # 1/64 of a texel: how early a software filter's sample may land (samples())


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


def fit(pw, ph, ow, oh):
    """cyc_present_fit.h's fit of a pw x ph picture in an ow x oh drawable:
    (x, y, w, h, the whole multiple drawn nearest-neighbour, the filter)."""
    by_width = ow * ph <= oh * pw
    num, den = (ow, pw) if by_width else (oh, ph)
    w = max(ow if by_width else pw * oh // ph, 1)
    h = max(ph * ow // pw if by_width else oh, 1)
    if num < den:
        return (ow - w) // 2, (oh - h) // 2, w, h, 1, "linear"
    if num % den == 0:
        return (ow - w) // 2, (oh - h) // 2, w, h, num // den, "nearest"
    return (ow - w) // 2, (oh - h) // 2, w, h, num // den + 1, "sharp"


def samples(n, k, out, linear=True):
    """Along one axis, n picture pixels drawn nearest-neighbour at k each,
    then (linear) linearly to `out` pixels, as a GPU's (and SDL's software)
    linear filter samples, at each output pixel's centre: the two picture
    pixels each output pixel mixes, (a, a) where it is exactly one. Without
    the linear step (a whole multiple, out = n * k), nearest-neighbour.

    A centre on an edge between two picture pixels, or a hair past it
    (LEAN: some at 2.55x; none at 4.5x, where the nearest is an 18th of a
    texel past), is the later one's, but SDL's software filter (the hidden
    window's) steps across in 16.16 fixed point, rounded down, and lands a
    little early: it takes a little of the earlier one, so it counts as
    mixing the two."""
    if not linear:
        return [(i // k, i // k) for i in range(out)]
    texels, mixes = n * k, []
    for i in range(out):
        # the centre, (i + 0.5) * texels / out - 0.5 texels in, as t0 + rem / (2 * out)
        t0, rem = divmod(max((2 * i + 1) * texels - out, 0), 2 * out)
        a, b = t0 // k, min(t0 + 1, texels - 1) // k
        if b != a and rem:
            mixes.append((a, b))
        elif rem * LEAN < 2 * out and t0 and (t0 - 1) // k != a:
            mixes.append(((t0 - 1) // k, a))
        else:
            mixes.append((a, a))
    return mixes


def presented(label, window, picture):
    """Check that `window` (the ui layer) presents `picture` (the picture
    layer, the same frame) by the fixed scale: black outside the fit; inside
    it, each output pixel exactly the picture pixel it falls in, or, where it
    mixes two (or four), between them. Returns the fit."""
    ww, wh, wrows = window
    pw, ph, prows = picture
    x0, y0, fw, fh, k, kind = fit(pw, ph, ww, wh)
    xs, ys = samples(pw, k, fw, kind != "nearest"), samples(ph, k, fh, kind != "nearest")
    whole = [(x, a) for x, (a, b) in enumerate(xs) if a == b]     # the columns inside one picture column
    black, exact, mixed = bytes(3 * ww), 0, 0

    def near(got, colours, x, y):
        assert all(min(c[ch] for c in colours) <= got[ch] <= max(c[ch] for c in colours) for ch in range(3)), \
            f"{label}: ({x0 + x}, {y}) is #{got.hex()}, not {' or between '.join(c.hex() for c in colours)}"

    for y in range(wh):
        row = wrows[y]
        if not y0 <= y < y0 + fh:
            assert row == black, f"{label}: row {y}, outside the picture, is not black"
            continue
        assert row[:3 * x0] + row[3 * (x0 + fw):] == black[:3 * (ww - fw)], f"{label}: row {y}'s bars are not black"
        ya, yb = ys[y - y0]
        line = [row[3 * x:3 * x + 3] for x in range(x0, x0 + fw)]
        if ya == yb:
            source = prows[ya]
            want = [source[3 * a:3 * a + 3] for a in range(pw)]
            if [line[x] for x, a in whole] != [want[a] for x, a in whole]:
                for x, a in whole:
                    near(line[x], [want[a]], x, y)
            exact += len(whole)
            for x, (a, b) in enumerate(xs):
                if a != b:
                    near(line[x], [want[a], want[b]], x, y)
            mixed += fw - len(whole)
        else:
            for x, (a, b) in enumerate(xs):
                near(line[x], [prows[r][3 * c:3 * c + 3] for r in (ya, yb) for c in {a, b}], x, y)
            mixed += fw
    print(f"{label}: {kind} at {fw / pw:.2f}x" + (f" from {k}x" if kind == "sharp" else "") +
          f", {fw}x{fh} at ({x0}, {y0}): {exact} pixels exact, {mixed} mixed")
    return x0, y0, fw, fh, k, kind


def main():
    args = parse_args(__doc__)
    g = Game.from_args(args)

    def size():
        return tuple(g.tcp("fax_world")["size"])

    def check_view(label):
        g.wait(4)                                   # the resize's event, then a frame presented after it
        picture = png(g.shot(label))
        assert size() == VIEW and picture[:2] == VIEW, (label, size(), picture[:2])
        print(f"{label}: canvas {size()[0]}x{size()[1]}, picture {picture[0]}x{picture[1]}")
        return picture

    def window(label, w, h):
        """Resize the window to w x h; (what it presents, its picture)."""
        g.tcp("window_size", w=w, h=h)
        picture = check_view(label)
        assert g.tcp("video")["drawable"] == [w, h]
        shown = g.out / f"{len(g.shots):02d}-presented-{label}.png"
        g.tcp("screenshot", layer="ui", path=str(shown))
        g.shots.append(shown.name)
        return png(shown), picture

    # fit() is cyc_present_fit.h's, at #107's sizes (its unit test has more).
    assert fit(424, 240, 1920, 1080) == (6, 0, 1908, 1080, 5, "sharp")
    assert fit(424, 240, 1280, 720) == (4, 0, 1272, 720, 3, "nearest")
    assert fit(424, 240, 2560, 1440) == (8, 0, 2544, 1440, 6, "nearest")
    assert fit(424, 240, 3840, 2160) == (12, 0, 3816, 2160, 9, "nearest")
    assert fit(424, 240, 1080, 1920) == (0, 654, 1080, 611, 3, "sharp")
    assert fit(424, 240, 300, 600) == (0, 215, 300, 169, 1, "linear")

    try:
        # The player's Display settings at their most: neither applies to Seamless's view.
        (g.out / "game" / "config.ini").write_text(CONFIG + "IntegerScale = 1\nLinearFilter = 1\n")
        g.launch(at="eolis-town")
        g.until("Seamless in play", lambda: g.tcp("fax_world")["playing"], 120)
        check_view("window-424x240")
        g.tcp("action", name="start", frames=4)                         # the Select menu, over the frozen town
        g.until("the Select menu", box_open, 120)
        g.wait(4)
        still = png(g.shot("still"))

        for w, h in ((1920, 1080), (1280, 720), (1080, 1920), (300, 600)):
            shown, picture = window(f"{w}x{h}", w, h)
            assert picture == still, "the Select menu did not hold the picture still"
            presented(f"{w}x{h}", shown, picture)

        g.tcp("window_size", w=1920, h=1080)
        check_view("1920x1080-again")
        g.tcp("key", name="F4", frames=2)
        g.until("the map overview", lambda: size() != VIEW, 30)
        w, h = size()
        assert w % RW == 0 and (h - HUD) % RH == 0 and (w, h) != VIEW, (w, h)
        g.wait(4)
        picture = png(g.shot("overview"))
        assert picture[:2] == (w, h)
        shown = g.out / f"{len(g.shots):02d}-presented-overview.png"
        g.tcp("screenshot", layer="ui", path=str(shown))
        g.shots.append(shown.name)
        presented(f"F4: the overview, {w}x{h}, at 1920x1080", png(shown), picture)
        g.tcp("key", name="F4", frames=2)
        g.until("the view back", lambda: size() == VIEW, 30)
        check_view("after-overview")
        g.close()
        saved = (g.out / "game" / "config.ini").read_text()           # written back with the settings it ran with
        assert "IntegerScale = 1\n" in saved and "LinearFilter = 1\n" in saved, saved

        # The original: the player's Display settings, as by default (IntegerScale on, LinearFilter off).
        (g.out / "game" / "config.ini").write_text(CONFIG)
        g.set_features({PACKAGE: False})
        g.launch(at="eolis-town")
        g.wait(10)
        assert not g.tcp("fax_world")["enabled"]
        g.tcp("action", name="start", frames=4)                         # the original's own pause
        g.until("the original's pause", lambda: g.ram(PAUSED) != 0, 60)
        g.tcp("window_size", w=1920, h=1080)
        g.wait(4)
        picture = png(g.shot("original"))
        assert picture[:2] == (256, 240), picture[:2]
        shown = g.out / f"{len(g.shots):02d}-presented-original-1920x1080.png"
        g.tcp("screenshot", layer="ui", path=str(shown))
        g.shots.append(shown.name)
        ww, wh, rows = png(shown)
        pw, ph, prows = picture
        x0, y0, scale = 448, 60, 4                                      # 1024x960, centred
        black = bytes(3 * ww)
        for y in range(wh):
            if not y0 <= y < y0 + scale * ph:
                assert rows[y] == black, f"original: row {y} is not black"
                continue
            want = black[:3 * x0] + b"".join(prows[(y - y0) // scale][3 * x:3 * x + 3] * scale for x in range(pw))
            assert rows[y] == want + black[:3 * (ww - x0 - scale * pw)], f"original: row {y} is not 4x"
        print("the original, 1920x1080: its 256x240 at IntegerScale's 4x, 1024x960 at (448, 60), black around")
        print("screenshots:", ", ".join(g.shots))
    except BaseException:
        if g.running():
            g.shot("failure")
        raise
    finally:
        g.close()


if __name__ == "__main__":
    main()
