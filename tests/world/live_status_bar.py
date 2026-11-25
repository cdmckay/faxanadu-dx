"""Seamless's status bar during conversations, end to end (#132).

    python3 tests/world/live_status_bar.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/live-hud
    python3 tests/world/live_status_bar.py ... --part king

In a conversation, a shop or a script Seamless keeps the world in its pose,
but its status bar band is the game's own picture's, frame by frame, so gold
counts as it is paid, as in the original. The checks read the band off the
presented picture's pixels: its 7 gold digits (row 3 from x 128, the HUD's
digit tiles $30-$39 from the ROM, bank 10), and the HP and MP bars (one
pixel a point from x 24, counted on a fill line: MP's line 19, HP's 27).
The purchase and the gift are played headless from save states, every
frame's RAM logged and its presented picture written (--present-out), with
the game's own picture beside it (--screenshot): the band shows a frame's
gold a frame late, as the game draws it (UI_DrawGoldValue queues it, the next
NMI sends it). The setup edits save-state RAM (external prerequisites, not
coverage): where a shop's door leads, the gold, HP and MP.

tool-shop  Eolis's tool shop with 700 Golds: Up, his greeting, Buy and its
           line open the buy list (Quick shops, fax_e2e's shop_list()), Down
           to Deluge (400), a save state there. A buys it:
           on every frame of the count-down the band's digits are the gold
           the game had that frame or the one before, so they read every
           amount from 700 down to 300, 10 a frame, on frames in a row, all
           with the Tool Seller's portrait up; and on every frame the band is
           the game's picture's top 32 lines, pixel for pixel. One picture
           mid-count is kept.
king       The King, to a hero with no gold, at his plea's end (a save
           state): A, and the band counts 0 up to 3000, 20 a frame, through
           his conversation (sampled every 4th frame, each the gold of that
           frame or the one before, and the game's own band). One picture
           mid-count is kept.
guru       Eolis's temple at 32 HP and 5 MP, both bars drawn (MP from kills'
           fixture): the band's bars read 32 and 5. Talking to the Guru fills
           both to 80 at his first action (fax_mana.c), and the game draws
           the bars a frame or two into his conversation: while his first
           line is still up, the band's bars read 80 and 80.
"""
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "block"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "movement"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "mana"))
import harness  # noqa: E402  (walking, a building's way out)
import play  # noqa: E402  (headless runs' mods and frames)
import fax_e2e  # noqa: E402
from fax_e2e import batch, box_open, fixture, page_to, parse_args, ram, tcp, until, walk  # noqa: E402
import mp_from_kills  # noqa: E402,F401  (its fixture: "mana-eolis-temple")

PARTS = ("tool-shop", "king", "guru")
AREA, POS_X, ENTITIES = 0x24, 0x9E, 0x2CC
MESSAGE_ID, GOLD, MP, PORTRAIT, SPECIAL_ITEMS, HP = 0x213, 0x392, 0x39A, 0x3C7, 0x42C, 0x431
AREA_SAVED_AREA, AREA_SAVED_SCREEN, PLAYER_SAVED_POS = 0x3D5, 0x3D6, 0x3D8
BUILDINGS, EOLIS, NO_PORTRAIT, KING, TOOLS_SALESMAN = 4, 0, 0xFF, 0, 9
TOOL_SHOP_SCREEN, TOOL_SHOP_DOOR = 3, 0x9A                       # EOLIS_DOOR_LOCATIONS: building $04
KINGS_SCREEN, KINGS_DOOR, KINGS_STAIRS, RING_OF_ELF = 7, 0x98, 48, 0x80
PLEA = 0x35                                                       # the King's message
ZONE_NAMES = "faxanadu.zone-names"                                # its record: the area and screen, at 5 and 6
START_GOLD, DELUGE_PRICE, DELUGE_ROW, GIFT = 700, 400, 1, 3000                 # Eolis's tools: dagger, Deluge
FULL = 80
HUD, NATIVE_W, VIEW_W, ITEM = 32, 256, 424, 16
HUD_X = (VIEW_W - NATIVE_W - ITEM) // 2 + ITEM                    # the band's column 0 (fax_world_hud_x): 92
GOLD_X, GOLD_Y, DIGITS = 128, 24, 7                               # UI_DrawGoldValue: PPU $2070, 7 digits
BAR_X, BAR_W, MP_LINE, HP_LINE = 24, 80, 19, 27                   # a fill line of each bar
PRESS = [(0, "-"), (2, "A"), (7, "-")]                           # a new press of A on a run's frame 2


class Game(harness.Game, play.Game):
    """Block's harness (walking) with the movement tests' headless runs' mods."""


def gold_bytes(value):
    return {GOLD: value & 0xFF, GOLD + 1: value >> 8 & 0xFF, GOLD + 2: value >> 16 & 0xFF}


def gold_of(r):
    return int.from_bytes(r[GOLD:GOLD + 3], "little")


def go_in(g, name, screen, door, writes):
    """From a building's state, walk out onto Eolis's door `door` (block YX)
    on `screen`, and go in."""
    target = g.out / f"{name}.cycstate"
    fixture(g.save("inside"), target, {AREA_SAVED_AREA: EOLIS, AREA_SAVED_SCREEN: screen, PLAYER_SAVED_POS: door,
                                       HP: FULL, **writes}, records={ZONE_NAMES: {5: EOLIS, 6: screen}})
    g.load(target)
    g.walk_out()
    assert g.where() == (EOLIS, screen), g.where()
    g.poke({ENTITIES + i: 0xFF for i in range(8)})                  # nobody on the street at the door
    column = (door & 15) * 16
    for _ in range(4):                                              # Up counts only on the frame it goes down
        g.walk_to(column - 6, column + 4)
        g.act("up", 4, 10)
        try:
            g.until("the door", lambda: g.ram(AREA) == BUILDINGS, 120)
            break
        except AssertionError:
            pass
    g.until("the building", lambda: g.ram(AREA) == BUILDINGS and g.settled(), 600)
    g.wait(30)


@fax_e2e.recipe("live-hud-deluge", parent="eolis-meat-shop")
def _deluge(g):
    """At Eolis's tool shop's counter with 700 Golds: Up and the conversation
    opened the buy list and Down put the cursor on Deluge, and the list waits
    for a button."""
    def waiting(what):
        """The buy list up, drawn, and waiting for a button (as Quick shops' test waits on it)."""
        g.until(what, lambda: (lambda s: s["shown"] == "buy" and s["list_up"])(g.tcp("shops_state")), 900)
        waits = g.tcp("shops_state")["waits"]
        g.until("the list to wait for a button", lambda: g.tcp("shops_state")["waits"] > waits, 300)
        g.wait(2)

    go_in(g, "tool-shop", TOOL_SHOP_SCREEN, TOOL_SHOP_DOOR, gold_bytes(START_GOLD))
    walk("left")                                                    # to the counter
    fax_e2e.shop_list("buy")
    g.wait(2)
    g.act("down", 4, 6)
    waiting("the buy list again")
    s = g.tcp("shops_state")
    assert s["cursor"] == DELUGE_ROW, s
    assert s["list"][DELUGE_ROW] == 0x60 and s["prices"][DELUGE_ROW] == DELUGE_PRICE, s


@fax_e2e.recipe("live-hud-plea-end", parent="eolis-meat-shop")
def _plea_end(g):
    """In the King's room with the Ring of Elf and no gold: up his stairs, Up
    at him, and his plea typed out, waiting for A before he pays."""
    go_in(g, "kings-room", KINGS_SCREEN, KINGS_DOOR, {SPECIAL_ITEMS: RING_OF_ELF, **gold_bytes(0)})
    for _ in range(60):                                             # up his stairs: Left and A
        if g.ram(POS_X) <= KINGS_STAIRS:
            break
        g.tcp("action", name="left", frames=24)
        g.tcp("action", name="a", frames=12)
        g.wait(30)
    for _ in range(5):                                              # Up counts only on the frame it goes down
        g.act("up", 4, 10)
        try:
            g.until("the King", lambda: g.ram(PORTRAIT) != NO_PORTRAIT, 120)
            break
        except AssertionError:
            pass
    g.until("his plea", lambda: g.ram(MESSAGE_ID) == PLEA, 600)
    page_to(lambda s: g.ram(MESSAGE_ID) == 0, "the plea's end", "gold_state")
    assert gold_of(g.ram(0, 0x440)) == 0, "nothing is paid until the plea is let go"


def png(path):
    """A picture's (width, bytes a pixel, rows of bytes). The host writes them
    stored and unfiltered (cyc_png.c), so each row is a slice."""
    data = Path(path).read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos, idat = 8, b""
    while pos < len(data):
        size, kind = struct.unpack_from(">I4s", data, pos)
        body = data[pos + 8:pos + 8 + size]
        if kind == b"IHDR":
            width, height, depth, color = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and color in (2, 6), (depth, color)
        elif kind == b"IDAT":
            idat += body
        pos += 12 + size
    bpp = 3 if color == 2 else 4
    raw, stride = zlib.decompress(idat), width * bpp + 1
    assert all(raw[y * stride] == 0 for y in range(height)), f"{Path(path).name}: a filtered row"
    return width, bpp, [raw[y * stride + 1:(y + 1) * stride] for y in range(height)]


class Band:
    """The status bar band of a picture, its column 0 at `x0`: read off its pixels."""

    def __init__(self, path, x0, glyphs):
        self.width, self.bpp, self.rows = png(path)
        self.x0, self.glyphs = x0, glyphs

    def lit(self, x, y):
        return self.rows[y][(self.x0 + x) * self.bpp:(self.x0 + x) * self.bpp + 3] != b"\0\0\0"

    def gold(self):
        """The 7 gold digits, as a number; None if a cell is no digit."""
        text = ""
        for k in range(DIGITS):
            cell = tuple(tuple(self.lit(GOLD_X + 8 * k + c, GOLD_Y + r) for c in range(8)) for r in range(8))
            if cell not in self.glyphs:
                return None
            text += str(self.glyphs.index(cell))
        return int(text)

    def bar(self, line):
        """A bar's value: its lit pixels on a fill line, one a point."""
        return sum(self.lit(BAR_X + i, line) for i in range(BAR_W))

    def pixels(self):
        return [self.rows[y][self.x0 * self.bpp:(self.x0 + NATIVE_W) * self.bpp] for y in range(HUD)]

    def same_pixels(self, other):
        """The same 256 x 32 band, pixel for pixel (RGB)."""
        def rgb(band):
            return [bytes(b for i, b in enumerate(row) if i % band.bpp < 3) for row in band.pixels()]
        return rgb(self) == rgb(other)


def main():
    args = parse_args(__doc__, parts=PARTS)
    g = Game.from_args(args)
    rom = g.image[16:]
    tiles = [rom[10 * 0x4000 + 0x140 + (0x30 + d) * 16:][:16] for d in range(10)]   # HUD_TILES_START, $30-$39
    glyphs = [tuple(tuple(bool((t[r] | t[r + 8]) >> (7 - c) & 1) for c in range(8)) for r in range(8)) for t in tiles]

    def headless(name, state, frames, every):
        """From `state`, A pressed on frame 2, `frames` frames: each frame's
        RAM, and every `every`th frame's presented picture and the game's own,
        as {run frame: (presented Band, the game's Band)}. The pictures are
        deleted once read, but for those kept()."""
        start = g.continues_at(state)
        run = g.out / f"run-{name}"
        mods = shutil.copytree(g.mods("headless-mods"), run / "mods")
        (run / "input.txt").write_text("".join(f"{start + at} {buttons}\n" for at, buttons in PRESS))
        log = run / "frames.bin"
        done = subprocess.run([str(g.exe), str(g.rom), "--load-state", str(state), "--input", str(run / "input.txt"),
                               "--frames", str(start + frames), "--frame-log", str(log),
                               "--frame-log-frames", f"{start}:{start + frames - 1}", "--mods-root", str(mods),
                               "--no-save", "--present-out", str(run / "present.png"), "--present-every", str(every),
                               "--screenshot", str(run / "game.png"), "--shot-every", str(every)],
                              capture_output=True, text=True)
        assert done.returncode == 0, done.stdout + done.stderr
        data = log.read_bytes()
        assert data[:8] == b"CYCFRAME"
        rams, pos = [], 12
        while pos < len(data):
            head = struct.unpack_from("<9I", data, pos)
            pos += 36 + 32 + 256
            rams.append(data[pos:pos + head[3]])
            pos += head[3] + sum(head[4:9])
        assert len(rams) == frames, (len(rams), frames)
        bands = {}
        for i in range(frames):
            shown, own = run / f"present_{start + i:05d}.png", run / f"game_{start + i:05d}.png"
            if shown.exists() and own.exists():
                bands[i] = (Band(shown, HUD_X, glyphs), Band(own, 0, glyphs))
                assert bands[i][0].width == VIEW_W, (shown.name, bands[i][0].width)
        return run, start, rams, bands

    def keep(run, start, i, name):
        """Keep run frame i's presented picture as OUT/NN-name.png."""
        path = g.out / f"{len(g.shots):02d}-{name}.png"
        shutil.copy(run / f"present_{start + i:05d}.png", path)
        g.shots.append(path.name)

    def counted(name, rams, bands, portrait, first, last, step):
        """Every sampled frame's band shows the gold that frame had, or the
        frame before, while `portrait`'s conversation is up, and is the game's
        own band; it reads amounts strictly between `first` and `last`, and
        ends at `last`. The frames sampled, by the gold shown."""
        golds = [gold_of(r) for r in rams]
        assert golds[0] == first and golds[-1] == last, (golds[0], golds[-1])
        paying = [i for i in range(1, len(golds)) if golds[i] != golds[i - 1]]
        assert paying and all(golds[i] - golds[i - 1] == step for i in paying), paying
        shown = {}
        for i, (band, own) in sorted(bands.items()):
            value = band.gold()
            assert value is not None, f"{name}: frame {i}'s band shows no gold digits"
            assert value in {golds[i], golds[max(i - 1, 0)]}, (name, i, value, golds[max(i - 1, 0)], golds[i])
            assert band.same_pixels(own), f"{name}: frame {i}'s band is not the game's own"
            assert rams[i][PORTRAIT] == portrait, (name, i, rams[i][PORTRAIT])
            shown.setdefault(value, i)
        mid = [v for v in shown if min(first, last) < v < max(first, last)]
        assert mid, f"{name}: the band never showed the gold mid-count: {sorted(shown)}"
        assert last in shown, f"{name}: the band never reached {last}: {sorted(shown)}"
        return shown, paying

    try:
        if args.wants("tool-shop"):
            run, start, rams, bands = headless("tool-shop", g.state("live-hud-deluge"), DELUGE_PRICE // 10 + 13, 1)
            shown, paying = counted("tool-shop", rams, bands, TOOLS_SALESMAN, START_GOLD,
                                    START_GOLD - DELUGE_PRICE, -10)
            every = list(range(START_GOLD, START_GOLD - DELUGE_PRICE - 1, -10))
            assert sorted(shown, reverse=True) == every, sorted(shown, reverse=True)
            frames = [shown[v] for v in every[1:]]
            assert frames == list(range(frames[0], frames[0] + len(frames))), frames
            assert len(paying) == DELUGE_PRICE // 10 and frames[0] == paying[0] + 1, (paying, frames)
            mid = START_GOLD - DELUGE_PRICE // 2
            keep(run, start, shown[mid], "tool-shop-mid-count")
            shutil.rmtree(run)
            print(f"ok: buying Deluge at Eolis's tool shop, the status bar's gold counts {START_GOLD} down to "
                  f"{START_GOLD - DELUGE_PRICE} through the purchase, a frame after the game's, every amount on "
                  f"frames in a row ({frames[0]}-{frames[-1]} of the run), the game's own band on every frame, "
                  "the Tool Seller's portrait up")

        if args.wants("king"):
            run, start, rams, bands = headless("king", g.state("live-hud-plea-end"), 170, 4)
            shown, paying = counted("king", rams, bands, KING, 0, GIFT, 20)
            assert len(paying) == GIFT // 20 and len(shown) >= 30, (len(paying), sorted(shown))
            keep(run, start, shown[min(v for v in shown if v >= GIFT // 2)], "king-mid-count")
            shutil.rmtree(run)
            print(f"ok: the King's gift counts the status bar's gold up from 0 to {GIFT} through his conversation, "
                  f"{len(shown)} amounts in {len(bands)} sampled frames, each a frame after the game's, the game's "
                  "own band")

        if args.wants("guru"):
            g.launch(at="mana-eolis-temple")
            g.wait(4)
            assert tcp("fax_world")["hud_x"] == HUD_X, tcp("fax_world")

            def look(name):
                """A screenshot, RAM and the text box at one frame boundary."""
                path = g.out / f"{len(g.shots):02d}-{name}.png"
                _, memory = batch(("screenshot", dict(path=str(path))), ("read_ram", dict(addr=0, len=0x440)))
                g.shots.append(path.name)
                return Band(path, HUD_X, glyphs), bytes.fromhex(memory["hex"])

            band, r = look("guru-before")
            assert (r[HP], r[MP]) == (0x20, 5), (r[HP], r[MP])
            assert (band.bar(HP_LINE), band.bar(MP_LINE)) == (0x20, 5), (band.bar(HP_LINE), band.bar(MP_LINE))
            walk("left")                                            # to the Guru, at the left
            assert not box_open()
            before = ram(MESSAGE_ID)
            g.act("up", 4, 0)
            until("the Guru's first line", lambda: box_open() and ram(MESSAGE_ID) != before, 300)
            first = ram(MESSAGE_ID)
            until("the bars drawn", lambda: tcp("mana_state")["bars"] == 0, 60)
            g.wait(4)                                               # the NMIs send what was queued before them
            band, r = look("guru-first-line")
            assert r[MESSAGE_ID] == first and r[0x20C] == 0, "still his first line"
            assert (r[HP], r[MP]) == (FULL, FULL), (r[HP], r[MP])
            assert (band.bar(HP_LINE), band.bar(MP_LINE)) == (FULL, FULL), (band.bar(HP_LINE), band.bar(MP_LINE))
            print("ok: at Eolis's Guru the status bar shows 32 HP and 5 MP, and during his first line both bars "
                  "full, as MP from kills fills them")
            g.close()
        print("screenshots:", ", ".join(g.shots))
    except BaseException:
        if g.running():
            g.shot("failure")
        raise
    finally:
        g.close()


if __name__ == "__main__":
    main()
