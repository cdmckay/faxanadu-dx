"""Seamless's view end to end: one 424x240 picture, whatever the window.

    python3 tests/world/fixed_view.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/view

Plays Seamless, the remaster's view, in
Eolis's town, from a saved game:

1. At the test window's size (424x240, config.ini's scale 1 of the view),
   the world canvas (fax_world's size) and the picture are 424x240.
2. Resized to 1920x1080, then to a narrow 300x600, they are still 424x240:
   the window changes how big the view is, never what it shows.
3. At 1920x1080, what the window presents (the ui layer) is that picture as
   the host scales it: at its default IntegerScale, 4x, 1696x960, every pixel
   a 4x4 block, and black around it. #107's sharp 4.5x needs a hook in the
   host, so this checks the 4x until then. A real window centres it, at
   (112, 60); the hidden window's software renderer draws it at the top left
   (the stock view too), so either place is taken. Start's pause holds the
   picture still, so both layers show the same frame.
4. F4's connected-map overview grows the canvas to the region; F4 again
   brings back 424x240.
"""
from pathlib import Path
import struct
import sys
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
from fax_e2e import Game, parse_args  # noqa: E402

VIEW = (424, 240)
PAUSED = 0x120                       # Game_PausedState
RW, RH, HUD = 256, 208, 32           # a room's playfield, and the HUD above it


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

    try:
        g.launch(at="eolis-town")
        g.until("Seamless in play", lambda: g.tcp("fax_world")["playing"], 120)
        check_view("window-424x240")

        g.tcp("window_size", w=1920, h=1080)
        check_view("window-1920x1080")
        g.act("start")
        g.until("the pause", lambda: g.ram(PAUSED) != 0, 60)
        g.wait(4)
        pw, ph, picture = png(g.shot("paused"))
        shown = g.out / f"{len(g.shots):02d}-presented-1920x1080.png"
        g.tcp("screenshot", layer="ui", path=str(shown))
        g.shots.append(shown.name)
        g.wait(2)
        assert png(g.shot("paused-again"))[2] == picture, "the pause did not hold the picture still"
        ww, wh, window = png(shown)
        assert (ww, wh) == (1920, 1080), (ww, wh)
        scale = 4
        assert (scale * pw, scale * ph) == (1696, 960)
        wide = [b"".join(row[3 * x:3 * x + 3] * scale for x in range(pw)) for row in picture]

        def presented_at(x0, y0):
            """The picture at 4x with its top left at (x0, y0), and black elsewhere."""
            black = bytes(3 * ww)
            for y in range(wh):
                row = window[y]
                if not y0 <= y < y0 + scale * ph:
                    if row != black:
                        return False
                    continue
                if (row[:3 * x0] + row[3 * (x0 + scale * pw):] != black[:3 * (ww - scale * pw)] or
                        row[3 * x0:3 * (x0 + scale * pw)] != wide[(y - y0) // scale]):
                    return False
            return True
        at = next((xy for xy in ((112, 60), (0, 0)) if presented_at(*xy)), None)
        assert at, "the window does not show the picture at 4x"
        print(f"1920x1080: the picture at 4x, 1696x960, at {at}, black around it")
        g.act("start")
        g.until("the pause to end", lambda: g.ram(PAUSED) == 0, 60)

        g.tcp("window_size", w=300, h=600)
        check_view("window-300x600")

        g.tcp("key", name="F4", frames=2)
        g.until("the map overview", lambda: size() != VIEW, 30)
        w, h = size()
        assert w % RW == 0 and (h - HUD) % RH == 0 and (w, h) != VIEW, (w, h)
        assert png(g.shot("overview"))[:2] == (w, h)
        print(f"F4: the overview, {w}x{h}")
        g.wait(4)
        g.tcp("key", name="F4", frames=2)
        g.until("the view back", lambda: size() == VIEW, 30)
        check_view("after-overview")
        print("screenshots:", ", ".join(g.shots))
    except BaseException:
        if g.running():
            g.shot("failure")
        raise
    finally:
        g.close()


if __name__ == "__main__":
    main()
