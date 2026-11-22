#!/usr/bin/env python3
"""Make the README's screenshots: docs/screenshots/<scene>.png, for the
features the README shows in pictures, Seamless, the quest log and the map
(CLAUDE.md says which features get one). The remaster plays in Seamless, so
its pictures are its 424x240 view; the original's are the stock 256x240.

    python3 tools/readme_screenshots.py                         # every scene
    python3 tools/readme_screenshots.py --only map,quest-log    # just these
    python3 tools/readme_screenshots.py --list                  # the scenes

Run it in the Nix devShell, from anywhere in the repo, after building
build-cycle (see the README). --rom defaults to "Faxanadu (USA).nes" at the
repo root.

Each scene is a function below, registered with @scene: it plays the game
to the feature's moment with the end-to-end tests' helper (tests/e2e/fax_e2e.py,
with tests/block/harness.py's Game for getting around), as the feature's own
test does, and takes its pictures with shot() or picture(). It starts from
saved games, as the tests do: the helper's fixtures ("eolis-meat-shop",
"eolis-town"), made from your ROM once and cached in build-tests/fixtures,
and edited with fixture() to start where a scene needs. Every scene runs at
once, each in its own process, any game on a port the helper reserves; its
log, save states and raw pictures go to OUT/<scene>/
(build-tests/readme-shots by default).

A scene's pictures are cropped to what matters, scaled 2x (nearest
neighbour) and written as an indexed PNG: an NES picture has well under 256
colors, so nothing is lost and the files stay small. Two pictures (the
original and the remaster, or a before and after) go side by side, each with
its caption above it in the game's own font, which has no "+" or ":".

Like the test suite's end-to-end runs, it takes the machine-wide end-to-end
lock (tools/test.sh), so the two never slow each other's games down.

Same every run: a blinking mark is waited for until it is lit
(Game.shot_lit, and the map scene's loop for the hero's box), and where a
moment must be frame exact, Game.replay plays it headless from a fixture.
Seamless also shows the next room's actors, which keep moving, so its
picture differs a little from run to run. A new build makes the fixtures
again, and a new fixture can differ by a frame: regenerate only the scenes
you changed (--only), so the others don't churn. Never commit a save state:
states hold the cartridge's graphics.

A feature with something new to see adds its scene here and its picture to
the README; CLAUDE.md says which do.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
import traceback
import zlib

REPO = Path(__file__).resolve().parents[1]
for folder in ("e2e", "block"):
    sys.path.insert(0, str(REPO / "tests" / folder))
import fax_e2e  # noqa: E402
from fax_e2e import fixture  # noqa: E402
import harness  # noqa: E402

SCALE = 2
GAP = 8                                     # native pixels between two pictures
CAPTION_ROWS = 14                           # native rows above each picture for its caption
WHITE, BLACK = (0xFF, 0xFF, 0xFF), (0, 0, 0)
GLYPHS = 16 + 13 * 16384                    # TILE_GLYPHS_START: the font, bank 13 $8000, from $20
REMASTER = ("faxanadu.remastered", "remastered")
LOCK = Path(os.environ.get("XDG_RUNTIME_DIR") or os.environ.get("TMPDIR") or "/tmp") / "faxanadu-remastered-e2e.lock"
SCENE_TIMEOUT = 30 * 60                     # seconds

# RAM (docs/GAME_INTERNALS.md; the names are the disassembly's)
AREA, POS_X = 0x24, 0x9E
SPRITE_ENTITIES = 0x2CC                     # CurrentSprites_Entities ($FF: none)
AREA_SAVED_AREA, AREA_SAVED_SCREEN, SCREEN_SAVED_PALETTE, PLAYER_SAVED_POS = 0x3D5, 0x3D6, 0x3D7, 0x3D8
SPECIAL_ITEMS, HP, AREA_REGION = 0x42C, 0x431, 0x435

# The game's values
AREA_EOLIS, AREA_TRUNK, AREA_MIST, TOWNS, BUILDINGS = 0, 1, 2, 3, 4
PALETTE_TRUNK, PALETTE_MIST, PALETTE_TOWN = 6, 0x0A, 0x1B
RING_OF_ELF = 0x80                          # a SpecialItems bit


# ---- pictures ----

def read_png(path):
    """An 8-bit RGB or RGBA PNG (the game's screenshots): (width, rows of (r, g, b))."""
    data = Path(path).read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
    pos, idat = 8, b""
    while pos < len(data):
        size, kind = struct.unpack_from(">I4s", data, pos)
        body = data[pos + 8:pos + 8 + size]
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", body)
            assert depth == 8 and color in (2, 6) and not interlace, (path, depth, color, interlace)
        elif kind == b"IDAT":
            idat += body
        pos += 12 + size
    bpp = 3 if color == 2 else 4
    raw, stride, rows, prior = zlib.decompress(idat), width * bpp, [], bytearray(width * bpp)
    for y in range(height):
        start = y * (stride + 1)
        kind, line = raw[start], bytearray(raw[start + 1:start + 1 + stride])
        if kind == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif kind == 2:
            for i in range(stride):
                line[i] = (line[i] + prior[i]) & 0xFF
        elif kind == 3:
            for i in range(stride):
                line[i] = (line[i] + ((line[i - bpp] if i >= bpp else 0) + prior[i]) // 2) & 0xFF
        elif kind == 4:
            for i in range(stride):
                a, b, c = (line[i - bpp], prior[i], prior[i - bpp]) if i >= bpp else (0, prior[i], 0)
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 0xFF
        rows.append([tuple(line[x:x + 3]) for x in range(0, stride, bpp)])
        prior = line
    return width, rows


def write_indexed_png(path, rows):
    """Rows of (r, g, b) as a palette PNG at the smallest bit depth that holds them."""
    colors = sorted({px for row in rows for px in row})
    assert len(colors) <= 256, f"{len(colors)} colors"
    index = {c: i for i, c in enumerate(colors)}
    depth = next(d for d in (1, 2, 4, 8) if len(colors) <= 1 << d)
    per = 8 // depth
    raw = bytearray()
    for row in rows:
        raw.append(0)
        for x in range(0, len(row), per):
            byte = 0
            for i, px in enumerate(row[x:x + per]):
                byte |= index[px] << (8 - depth * (i + 1))
            raw.append(byte)

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n"
                           + chunk(b"IHDR", struct.pack(">IIBBBBB", len(rows[0]), len(rows), depth, 3, 0, 0, 0))
                           + chunk(b"PLTE", bytes(c for color in colors for c in color))
                           + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def crop(rows, box):
    x0, y0, x1, y1 = box
    return [row[x0:x1] for row in rows[y0:y1]]


def scale(rows, k):
    return [[px for px in row for _ in range(k)] for row in rows for _ in range(k)]


def lit(rows, box):
    """Anything but black inside `box` (x0, y0, x1, y1)."""
    x0, y0, x1, y1 = box
    return any(rows[y][x] != BLACK for y in range(y0, y1) for x in range(x0, x1))


def text(rom, words, color=WHITE):
    """`words` in the game's font (8x8, one byte a row), on black: rows of pixels."""
    rows = [[BLACK] * (8 * len(words)) for _ in range(8)]
    for i, ch in enumerate(words):
        glyph = rom[GLYPHS + (ord(ch) - 0x20) * 8:GLYPHS + (ord(ch) - 0x20) * 8 + 8]
        for dy in range(8):
            for dx in range(8):
                if glyph[dy] >> (7 - dx) & 1:
                    rows[dy][i * 8 + dx] = color
    return rows


def paste(canvas, rows, x, y):
    for dy, row in enumerate(rows):
        canvas[y + dy][x:x + len(row)] = row


def compose(rom, panels):
    """Pictures (rows, caption) side by side, native size, each with its
    caption centred above it when there is more than one."""
    if len(panels) == 1:
        return panels[0][0]
    captioned = any(caption for _, caption in panels)
    top = CAPTION_ROWS if captioned else 0
    width = sum(len(rows[0]) for rows, _ in panels) + GAP * (len(panels) - 1)
    height = top + max(len(rows) for rows, _ in panels)
    canvas = [[BLACK] * width for _ in range(height)]
    x = 0
    for rows, caption in panels:
        w = len(rows[0])
        if caption:
            label = text(rom, caption)
            assert len(label[0]) <= w, f"the caption {caption!r} is wider than its picture"
            paste(canvas, label, x + (w - len(label[0])) // 2, (CAPTION_ROWS - 8) // 2 - 1)
        paste(canvas, rows, x, top)
        x += w + GAP
    return canvas


# ---- playing the game ----

class Game(harness.Game):
    """tests/block/harness.py's Game (fax_e2e.Game with fixtures, and ways to
    get around: walk_to, walk_out, warp, save, load), with what the scenes
    share."""

    # Getting somewhere
    def walk(self, direction, test, what, steps=200):
        """Walk `direction` until `test()` holds."""
        for _ in range(steps):
            if test():
                return
            self.tcp("action", name=direction, frames=4)
            self.wait(4)
        raise AssertionError(f"walking {direction} never reached {what}: at {self.where()}, x={self.ram(POS_X)}")

    # Waiting
    def page_until(self, what, test, tries=80):
        """Page through any text with A until `test()` holds."""
        for _ in range(tries):
            if test():
                return
            if self.box_open() and self.typed():
                self.act("a", 6, 14)
            else:
                self.wait(10)
        raise AssertionError(f"timed out paging to {what}")

    def clear_actors(self, writes=None):
        """Send the screen's actors away (and make `writes`): a clean picture, or a safe walk."""
        self.poke({**{SPRITE_ENTITIES + i: 0xFF for i in range(8)}, **(writes or {})})

    # Pictures
    def shot_lit(self, box, name, tries=40):
        """Screenshot, retaken until something is drawn in `box`: a blinking
        mark (a text box's arrow or prompt, the status bar's T:), so every
        run catches it lit. Its path."""
        for attempt in range(tries):
            path = self.shot(f"{name}-{attempt}")
            if lit(read_png(path)[1], box):
                return path
            self.wait(3)
        raise AssertionError(f"{name}: nothing ever showed in {box}")

    def catalog(self, original=False):
        """A mod catalog for headless runs: the build's packages, the remaster
        on, or off for the original."""
        root = self.out / ("mods-original" if original else "mods-remastered")
        if not root.exists():
            shutil.copytree(self.out / "game" / "mods" / "packages", root / "packages")
            (root / "state.toml").write_text(fax_e2e.features_toml({REMASTER: False} if original else {}))
        return root

    def replay(self, state, schedule, frames, original=False, picture=None, save=None):
        """Play `frames` frames from `state` headless: no window and no TCP, so
        frame exact and the same every run. Buttons are held on a schedule of
        (frame, buttons) counted from the state ("B", "RIGHT+A", "-" for none).
        Returns each frame's CPU RAM. With `picture`, also writes what the
        window would show after the last frame (the picture with the
        remaster's drawing on it); with `save`, a save state after it. Needs
        no window: a scene that only replays a fixture launches none."""
        mods = self.catalog(original)
        start = continues_at(state)
        run = self.out / "replay"
        run.mkdir(exist_ok=True)
        (run / "input.txt").write_text("".join(f"{start + at} {buttons}\n" for at, buttons in sorted(schedule)))
        log = run / "frames.bin"
        cmd = [str(self.exe), str(self.rom), "--load-state", str(state), "--input", str(run / "input.txt"),
               "--frames", str(start + frames), "--frame-log", str(log),
               "--frame-log-frames", f"{start}:{start + frames - 1}", "--mods-root", str(mods), "--no-save"]
        if picture:
            cmd += ["--present-out", str(picture)]
        if save:
            cmd += ["--save-state", f"{start + frames - 1}:{save}"]
        done = subprocess.run(cmd, capture_output=True, text=True)
        assert done.returncode == 0, done.stdout + done.stderr
        data = log.read_bytes()
        log.unlink()
        assert data[:8] == b"CYCFRAME"
        rams, pos = [], 12
        while pos < len(data):
            head = struct.unpack_from("<9I", data, pos)
            pos += 36 + 32 + 256
            rams.append(data[pos:pos + head[3]])
            pos += head[3] + sum(head[4:9])
        assert len(rams) == frames, (len(rams), frames)
        return rams


def continues_at(state):
    """The frame a save state continues at: the uint32 at its offset 28."""
    return struct.unpack_from("<I", Path(state).read_bytes(), 28)[0]


class Scene:
    """What a scene function gets: the game, and shot() and picture() to take
    its pictures."""

    def __init__(self, name, game):
        self.name, self.game, self.panels, self.sources = name, game, [], []

    def shot(self, caption=None, box=(0, 0, 256, 240), name=None, overlay=False):
        """Screenshot the game's picture (what the window shows, with the
        remaster's drawing on it), cropped to `box` (x0, y0, x1, y1), in the
        native picture's place (picture())."""
        return self.picture(self.game.shot(name or (caption or self.name).lower().replace(" ", "-")), caption, box,
                            overlay=overlay)

    def picture(self, path, caption=None, box=None, native=True, overlay=False):
        """A picture from a file: a screenshot, or a headless run's, cropped
        to `box`. With `native`, the box is in the NES's 256x240 picture,
        where the remaster's Seamless shows it in its wider view: under the
        status bar, as it shows buildings and text boxes, where the game says
        it put them (fax_e2e.native_x(), so call this right after the
        screenshot); with `overlay`, centred, as it shows the remaster's own
        screens (the map, the quest log)."""
        width, rows = read_png(path)
        if box and native:
            ox = (width - 256) // 2 if overlay else fax_e2e.native_x(width)
            box = (box[0] + ox, box[1], box[2] + ox, box[3])
        self.panels.append((crop(rows, box or (0, 0, width, len(rows))), caption))
        self.sources.append(Path(path).name)
        return path

    def write(self, target):
        rows = scale(compose(self.game.image, self.panels), SCALE)
        write_indexed_png(target, rows)
        return len(rows[0]), len(rows)


SCENES = {}


def scene(name, game=Game):
    """Register a scene: docs/screenshots/<name>.png, played with a `game`."""
    def register(fn):
        assert name not in SCENES, name
        SCENES[name] = (fn, game)
        return fn
    return register


# ---- the scenes, in the README's order ----

@scene("seamless")
def seamless(s):
    """Seamless: in Eolis's town, near the right edge of its first screen,
    the original (the remaster off, its stock 256x240 view) and the remaster,
    which plays in Seamless and joins the next screen on, with its actors.
    Those keep moving (Seamless runs them in their own room), so this picture
    differs a little from run to run: where they are, or whether one is in
    view. Seamless's view is 424x240, whatever the window."""
    g = s.game
    for caption, on in (("Original", False), ("Remastered", True)):
        g.set_features({} if on else {REMASTER: False})
        g.launch(at="eolis-town")
        assert g.tcp("fax_world")["enabled"] == on
        g.walk("right", lambda: g.ram(POS_X) >= 196, "x 196")
        g.clear_actors()                                        # this screen's walker, so the original's picture is the same
        g.wait(30)
        path = g.shot(caption.lower())
        width, _ = read_png(path)
        s.picture(path, caption, (0, 8, width, 232), native=False)
        g.close()


# Conversations: the name tab, the portrait and the text box.
DIALOGUE = (8, 96, 248, 200)


# The quest log's save-state record (src/fax_quests.c, as tests/quests/quest_log.py writes it): a version,
# then the quests given and those seen done, a bit each in fax_quest_table.h's order, 4 bytes each.
QUEST_RECORD, QUEST_GIVEN, QUEST_DONE = "faxanadu.quests", 1, 5
QUEST_BIT = {"elf": 0, "king": 1, "mattock": 2, "wall": 3, "trunk": 4, "sky": 6, "meteorite": 10}
KING_BOX, MIST_MAN_BOX = (0, 96, 112, 208), (24, 112, 136, 208)   # around the King and his stairs; the man on Mist $1F
LAST_MARK = (216, 170, 240, 186)            # the flashing mark at the dialogue box's corner, after its last line
BLACK_ONYX = 0x01                           # a SpecialItems bit


def quest_bits(base, names):
    """The quest record's 4 bytes at `base` for a set of quests."""
    bits = sum(1 << QUEST_BIT[n] for n in names)
    return {base + i: bits >> (8 * i) & 0xFF for i in range(4)}


def into_kings_room(g, shop):
    """From the meat shop, out of the King's door on screen 7 (with the Ring)
    and in."""
    room = g.out / "kings-room.cycstate"
    fixture(shop, room, {AREA_SAVED_AREA: AREA_EOLIS, AREA_SAVED_SCREEN: 7, PLAYER_SAVED_POS: 0x98, HP: 0x50,
                         SPECIAL_ITEMS: RING_OF_ELF}, records={"faxanadu.zone-names": {5: AREA_EOLIS, 6: 7}})
    g.load(room)
    g.walk_out()
    assert g.where() == (AREA_EOLIS, 7), g.where()
    g.wait(30)
    g.walk_to(122, 132)                                         # the King's door, column 8
    g.act("up", 4, 10)
    g.until("the King's room", lambda: g.ram(AREA) == BUILDINGS)
    g.wait(40)


def quest_mark(g, script):
    """The mark quest_state says is over the person whose script is `script`."""
    return next((m for m in g.tcp("quest_state")["marks"] if m["script"] == script), None)


@scene("quest-log")
def quest_log(s):
    """The quest log: the King's line, then "New quest" and the quest's name
    in his box; and the log itself, partway to Mascon: in Forepaw with the
    Ring of Elf, the wall broken and the spring of Trunk revived, the King's
    Plea and the Spring of Sky under way, four quests done."""
    g = s.game
    shop = g.state("eolis-meat-shop")
    g.launch(at="eolis-meat-shop")
    into_kings_room(g, shop)
    for _ in range(60):                                         # up his stairs, to x 48
        if g.ram(POS_X) <= 48 or g.box_open():
            break
        g.tcp("action", name="left", frames=24)
        g.tcp("action", name="a", frames=12)
        g.wait(30)
    g.act("up", 4, 10)
    g.until("the King", g.box_open, 600)
    g.page_until("the new quest", lambda: g.tcp("quest_state")["saying"] and g.typed(), 200)
    g.wait(10)
    s.picture(g.shot_lit(LAST_MARK, "new-quest"), None, DIALOGUE)
    forepaw = g.out / "forepaw.cycstate"                        # out of a door on Forepaw's first screen
    records = {**quest_bits(QUEST_GIVEN, ["elf", "king", "mattock", "wall", "trunk", "sky"]),
               **quest_bits(QUEST_DONE, ["elf", "mattock", "wall", "trunk"])}
    fixture(shop, forepaw, {AREA_SAVED_AREA: TOWNS, AREA_SAVED_SCREEN: 2, SCREEN_SAVED_PALETTE: PALETTE_TOWN,
                            PLAYER_SAVED_POS: 0x99, AREA_REGION: AREA_TRUNK, HP: 0x50, SPECIAL_ITEMS: RING_OF_ELF,
                            0x42D: 0x01}, records={QUEST_RECORD: records})                    # Quests: the spring of Trunk
    g.load(forepaw)
    g.walk_out()
    g.wait(20)
    g.tcp("key", name="L", frames=2)
    g.until("the log", lambda: g.tcp("quest_state")["open"], 300)
    g.wait(4)
    state = g.tcp("quest_state")
    assert state["active"] == ["king", "sky"] and state["pages"] == 1, state
    s.shot(name="log", overlay=True)


@scene("quest-marks")
def quest_marks(s):
    """The quest log's marks: a "!" over the King of Eolis before the hero
    speaks to him, and none after, his quest given; and a "?" over the man in
    the house on Mist $1F, who sent the hero for the meteorite, now that he
    holds it (the Black Onyx)."""
    g = s.game
    shop = g.state("eolis-meat-shop")
    g.launch(at="eolis-meat-shop")
    into_kings_room(g, shop)
    g.until("the King's \"!\"", lambda: quest_mark(g, 0x08), 120)
    assert quest_mark(g, 0x08)["mark"] == "!"
    s.shot("Before", KING_BOX, name="king-before")
    for _ in range(60):                                         # up his stairs, to x 48
        if g.ram(POS_X) <= 48 or g.box_open():
            break
        g.tcp("action", name="left", frames=24)
        g.tcp("action", name="a", frames=12)
        g.wait(30)
    g.act("up", 4, 10)
    g.until("the King", g.box_open, 600)
    g.page_until("the end of his words", lambda: not g.box_open() and g.tcp("quest_state")["in_play"], 200)
    g.walk("right", lambda: g.ram(POS_X) >= 88, "the foot of his stairs")   # out of the way
    g.wait(30)
    assert quest_mark(g, 0x08) is None and g.tcp("quest_state")["given"] == ["king"]
    s.shot("After", KING_BOX, name="king-after")
    house = g.out / "mist-house.cycstate"                       # the door on Mist $1F, block $94
    fixture(shop, house, {AREA_SAVED_AREA: AREA_MIST, AREA_SAVED_SCREEN: 0x1F, SCREEN_SAVED_PALETTE: PALETTE_MIST,
                          PLAYER_SAVED_POS: 0x94, AREA_REGION: AREA_MIST, HP: 0x50,
                          SPECIAL_ITEMS: RING_OF_ELF | BLACK_ONYX},
            records={"faxanadu.zone-names": {5: AREA_MIST, 6: 0x1F}, QUEST_RECORD: quest_bits(QUEST_GIVEN, ["meteorite"])})
    g.load(house)
    g.walk_out()
    g.wait(30)
    g.walk_to(58, 68)                                           # the house's door, column 4
    g.act("up", 4, 10)
    g.until("the house", lambda: g.ram(AREA) == BUILDINGS)
    g.wait(40)
    g.until("the man's \"?\"", lambda: quest_mark(g, 0x46), 120)
    assert quest_mark(g, 0x46)["mark"] == "?"
    s.shot("Waiting", MIST_MAN_BOX, name="mist-house")



# The map's save-state record (src/fax_map.c, as tests/map/explored_map.py writes it): a version, then the
# screens seen, those left upward and those left downward (11 bytes an area each), then the doors used (6 an area).
MAP_RECORD, MAP_SEEN, MAP_UP, MAP_DOORS = "faxanadu.map", 1, 1 + 88, 1 + 264
# An early game's journey: all of Eolis, then the Trunk from its first screen past Apolune (the towns' 0 and
# 1), up the climb ($0C to $16), through Forepaw (the towns' 2 and 3) and on east to $22. The rest of the Trunk
# is unseen.
MAP_SEEN_SCREENS = {AREA_EOLIS: set(range(9)), AREA_TRUNK: set(range(0x0D)) | set(range(0x16, 0x1B)) |
                    set(range(0x1D, 0x23)), TOWNS: {0, 1, 2, 3}}
MAP_UP_SCREENS = {AREA_TRUNK: {0x03, 0x0C, 0x16, 0x17, 0x1E, 0x21}}   # the ladders he took, kept on the screen below
# The doors he used (by their place in the area's door locations): Eolis's all; on the Trunk the tool shop on
# $06 and the Tower of Trunk's door on $0B; the key shop and temple of Apolune, the tool shop and temple of
# Forepaw.
MAP_DOOR_SETS = {AREA_EOLIS: set(range(48)), AREA_TRUNK: {1, 7}, TOWNS: {1, 3, 7, 8}}
MAP_ITEMS_LEFT = {(AREA_TRUNK, 0x18, 0x48), (AREA_TRUNK, 0x22, 0x4B)}   # the Glove on $18, the Red Potion on $22
MAP_HERE = 0x19                                                 # the Trunk's screen left of Forepaw


def map_bits(base, size, sets):
    """The map record's bytes for {area: items}, at `base`, `size` bytes an area."""
    out = {}
    for area, members in sets.items():
        for i in members:
            out[base + area * size + i // 8] = out.get(base + area * size + i // 8, 0) | 1 << (i % 8)
    return out


def most_common(rows, boxes):
    """The commonest color inside the walls of map_state's `boxes` [area, screen, x, y, w, h, seen]."""
    counts = {}
    for _, _, x, y, w, h, _ in boxes:
        for row in rows[y + 1:y + h - 1]:
            for px in row[x + 1:x + w - 1]:
                counts[px] = counts.get(px, 0) + 1
    return max(counts, key=counts.get)


@scene("map")
def map_(s):
    """The map, on the Trunk's page: the screens of an early game's journey
    seen (the map's record set, as tests/map/explored_map.py's file part
    does), the climb and the ladders he took, the doors he used (two Gurus'
    crosses, four doors), the dots of the two items still lying on screens
    he has seen, and the hero on the screen left of Forepaw, his box caught
    lit as it blinks. Eolis is seen too, so the page has its arrows."""
    g = s.game
    shop = g.state("eolis-meat-shop")
    g.launch(at="eolis-meat-shop")
    record = {**map_bits(MAP_SEEN, 11, MAP_SEEN_SCREENS), **map_bits(MAP_UP, 11, MAP_UP_SCREENS),
              **map_bits(MAP_DOORS, 6, MAP_DOOR_SETS)}
    trunk = g.out / "trunk.cycstate"                            # out of the shop, its door edited to lead there
    fixture(shop, trunk, {AREA_SAVED_AREA: AREA_TRUNK, AREA_SAVED_SCREEN: MAP_HERE, SCREEN_SAVED_PALETTE: PALETTE_TRUNK,
                          PLAYER_SAVED_POS: 0xA1, AREA_REGION: AREA_TRUNK, HP: 0x50}, records={MAP_RECORD: record})
    g.load(trunk)
    g.walk_out()
    assert g.where() == (AREA_TRUNK, MAP_HERE), g.where()
    g.clear_actors()
    g.tcp("key", name="M", frames=2)
    g.until("the map", lambda: g.tcp("map_state")["open"], 300)
    g.wait(4)
    state = g.tcp("map_state")
    assert state["page_name"] == "Trunk" and state["here"] == [AREA_TRUNK, MAP_HERE] and state["pages_seen"] == 2, state
    boxes = [b for b in state["boxes"] if b[6]]
    assert len(boxes) == len(MAP_SEEN_SCREENS[AREA_TRUNK]) + len(MAP_SEEN_SCREENS[TOWNS]), boxes
    dots = {tuple(state["items"][d[0]][1:4]) for d in state["dots"]}
    assert dots == MAP_ITEMS_LEFT, (dots, state["dots"])
    here = [b for b in boxes if b[:2] == [AREA_TRUNK, MAP_HERE]]
    rooms = [b for b in boxes if b[0] == AREA_TRUNK and b[1] != MAP_HERE]
    for attempt in range(8):                                    # his box blinks every 16 frames: catch it lit
        path = g.shot(f"map-{attempt}")
        _, rows = read_png(path)
        if most_common(rows, here) != most_common(rows, rooms):
            break
        g.wait(8)
    else:
        raise AssertionError("the hero's box never lit")
    s.picture(path, box=(0, 0, 256, 240), overlay=True)          # the map's own screen, where Seamless centres it


# ---- running them ----

def run_scene(name, args):
    """Play one scene in this process and write its PNG."""
    fn, game_class = SCENES[name]
    game = game_class(args.build, args.rom, args.out)
    s = Scene(name, game)
    try:
        fn(s)
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()
    target = args.docs / f"{name}.png"
    target.parent.mkdir(parents=True, exist_ok=True)
    w, h = s.write(target)
    shown = target.relative_to(REPO) if target.is_relative_to(REPO) else target
    print(f"{name}: {shown} {w}x{h}, {target.stat().st_size} bytes, from {', '.join(s.sources)}")


def spawn(name, args):
    """Run a scene in a child process; (name, ok, seconds, log path)."""
    out = args.out / name
    out.mkdir(parents=True, exist_ok=True)
    log = args.out / f"{name}.log"
    start = time.monotonic()
    with open(log, "w") as f:
        try:
            ok = subprocess.run([sys.executable, __file__, "--scene", name, "--build", str(args.build),
                                 "--rom", str(args.rom), "--out", str(out), "--docs", str(args.docs)],
                                stdout=f, stderr=subprocess.STDOUT, timeout=SCENE_TIMEOUT).returncode == 0
        except subprocess.TimeoutExpired:
            f.write(f"\ntimed out after {SCENE_TIMEOUT} s\n")
            ok = False
    return name, ok, time.monotonic() - start, log


def lock():
    """The suite's end-to-end lock, held until this process exits."""
    import fcntl
    fd = os.open(LOCK, os.O_WRONLY | os.O_CREAT, 0o600)
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        print(f"waiting for another suite's end-to-end runs to finish ({LOCK})", flush=True)
        fcntl.flock(fd, fcntl.LOCK_EX)
    return fd


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", type=Path, default=REPO / "build-cycle")
    ap.add_argument("--rom", type=Path, default=REPO / "Faxanadu (USA).nes")
    ap.add_argument("--out", type=Path, default=REPO / "build-tests" / "readme-shots",
                    help="each scene's log, save states and raw pictures (default build-tests/readme-shots)")
    ap.add_argument("--docs", type=Path, default=REPO / "docs" / "screenshots", help="where the PNGs go")
    ap.add_argument("--only", help="comma-separated scenes (default: all)")
    ap.add_argument("--jobs", type=int, default=0, help="scenes at once (default: all)")
    ap.add_argument("--list", action="store_true", help="list the scenes and exit")
    ap.add_argument("--no-lock", action="store_true", help="don't wait for other suites' end-to-end runs")
    ap.add_argument("--scene", help=argparse.SUPPRESS)        # internal: run one scene in this process
    args = ap.parse_args()
    for key in ("build", "rom", "out", "docs"):
        setattr(args, key, getattr(args, key).resolve())
    if args.list:
        print("\n".join(SCENES))
        return 0
    if args.scene:
        run_scene(args.scene, args)
        return 0
    names = [n.strip() for n in args.only.split(",")] if args.only else list(SCENES)
    unknown = [n for n in names if n not in SCENES]
    if unknown:
        ap.error(f"unknown scene(s): {', '.join(unknown)} (--list)")
    if not (args.build / "FaxanaduRecomp").exists():
        ap.error(f"no build at {args.build}: build build-cycle first (see the README)")
    if not args.no_lock:
        lock()
    print(f"{len(names)} scene(s) to {args.docs}; logs in {args.out}", flush=True)
    failed = []
    with ThreadPoolExecutor(args.jobs or len(names)) as pool:
        for name, ok, seconds, log in pool.map(lambda n: spawn(n, args), names):
            lines = log.read_text(errors="replace").splitlines()
            if ok:
                summary = next((line for line in reversed(lines) if line.startswith(f"{name}: ")), name)
                print(f"ok   {summary} ({seconds:.0f} s)", flush=True)
            else:
                failed.append(name)
                print(f"FAIL {name} ({seconds:.0f} s), {log}:", flush=True)
                print("\n".join("     " + line for line in lines[-15:]), flush=True)
    if failed:
        print(f"{len(failed)} scene(s) failed: {', '.join(failed)}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception:
        traceback.print_exc()
        sys.exit(1)
