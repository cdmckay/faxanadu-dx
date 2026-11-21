#!/usr/bin/env python3
"""Make the README's screenshots: docs/screenshots/<scene>.png, one for each
feature in its "What's new" list.

    python3 tools/readme_screenshots.py                         # every scene
    python3 tools/readme_screenshots.py --only zone-names,block # just these
    python3 tools/readme_screenshots.py --list                  # the scenes

Run it in the Nix devShell, from anywhere in the repo, after building
build-cycle (see the README). --rom defaults to "Faxanadu (USA).nes" at the
repo root.

Each scene is a function below, registered with @scene: it plays the game
to the feature's moment with the end-to-end tests' helper (tests/e2e/fax_e2e.py,
with tests/block/harness.py's Game for getting around and
tests/movement/play.py's for the ledge), as the feature's own test does, and
takes its pictures with shot() or picture(). It starts from saved games, as
the tests do: the helper's fixtures ("gate", "eolis-town", "eolis-meat-shop",
"eolis-zozuras", play.py's "trunk-ledge") and the "readme-*" recipes below,
made from your ROM once and cached in build-tests/fixtures. Every scene runs
at once, each in its own process, any game on a port the helper reserves;
its log, save states and raw pictures go to OUT/<scene>/
(build-tests/readme-shots by default).

A scene's pictures are cropped to what matters, scaled 2x (nearest
neighbour) and written as an indexed PNG: an NES picture has well under 256
colors, so nothing is lost and the files stay small. Two pictures (the
original and the remaster, or a before and after) go side by side, each with
its caption above it in the game's own font, which has no "+" or ":".

Like the test suite's end-to-end runs, it takes the machine-wide end-to-end
lock (tools/test.sh), so the two never slow each other's games down.

Same every run: where a moment must be frame exact, a scene plays it
headless from a fixture (Game.replay, or play.py's headless), and a blinking
mark is waited for until it is lit (Game.shot_lit), so with the fixtures
cached most scenes come out byte for byte the same each run. A few play over
TCP to a short-lived moment and can land a frame apart (Block's glint, the
Magic button's spell, Sugata's flash), and Seamless also shows the next
room's actors, which keep moving. A new build, or an edit to a recipe's
file (this one), makes the fixtures again, and a new fixture can differ by a
frame: regenerate only the scenes you changed (--only), so the others don't
churn. Never commit a save state: states hold the cartridge's graphics.

A new feature adds its scene here and its picture to the README (CLAUDE.md).
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import sys
import time
import traceback
import zlib

REPO = Path(__file__).resolve().parents[1]
for folder in ("e2e", "block", "movement", "graphics"):
    sys.path.insert(0, str(REPO / "tests" / folder))
import fax_e2e  # noqa: E402
from fax_e2e import fixture  # noqa: E402
import graphics_fixes  # noqa: E402  (its fixtures: trunk-studded, door-myconid, door-fortress)
import harness  # noqa: E402
import play  # noqa: E402

SCALE = 2
GAP = 8                                     # native pixels between two pictures
CAPTION_ROWS = 14                           # native rows above each picture for its caption
WHITE, BLACK = (0xFF, 0xFF, 0xFF), (0, 0, 0)
GLYPHS = 16 + 13 * 16384                    # TILE_GLYPHS_START: the font, bank 13 $8000, from $20
REMASTER = ("faxanadu.remastered", "remastered")
LOCK = Path(os.environ.get("XDG_RUNTIME_DIR") or os.environ.get("TMPDIR") or "/tmp") / "faxanadu-remastered-e2e.lock"
SCENE_TIMEOUT = 30 * 60                     # seconds

# RAM (docs/GAME_INTERNALS.md; the names are the disassembly's)
INTERRUPT_COUNTER = 0x1A                    # animates portraits and blinks a text box's marks
AREA, SCREEN, SCREEN_TO_THE_RIGHT = 0x24, 0x63, 0x67
POS_X, POS_Y, MOVEMENT_TICK, PLAYER_FLAGS, PLAYER_STATUS = 0x9E, 0xA1, 0xA3, 0xA4, 0xA5
JUMPING, FALLING, FACING_RIGHT, ATTACKING = 0x01, 0x04, 0x40, 0x80   # Player_Flags
INVINCIBILITY, ATTACK_PHASE = 0xAD, 0xAE
SPRITE_X, SPRITE_Y = 0xBA, 0xC2             # CurrentSprites_X/Y, 8 slots
TEXTBOX_CLOSED, MESSAGE_ID, MESSAGE_PROCESSED, TEXTBOX_PAUSED = 0x20C, 0x213, 0x214, 0x215
SPRITE_ENTITIES = 0x2CC                     # CurrentSprites_Entities ($FF: none)
SPRITE_FLAGS, SPRITE_FACING_RIGHT = 0x2DC, 0x01
SPRITE_HP, SPRITE_HIT_COUNTER = 0x344, 0x34C
EXPERIENCE, GOLD, MP = 0x390, 0x392, 0x39A  # 2 and 3 bytes, low first
WEAPONS, MAGIC_SPELLS, ITEMS = 0x39D, 0x3A9, 0x3AD
SELECTED_WEAPON, SELECTED_ARMOR, SELECTED_SHIELD, SELECTED_MAGIC, SELECTED_ITEM = 0x3BD, 0x3BE, 0x3BF, 0x3C0, 0x3C1
NUMBER_OF_WEAPONS, NUMBER_OF_MAGIC_SPELLS, NUMBER_OF_ITEMS = 0x3C2, 0x3C5, 0x3C6
PLAYER_CUR_WEAPON = 0x3C8
AREA_SAVED_AREA, AREA_SAVED_SCREEN, SCREEN_SAVED_PALETTE, PLAYER_SAVED_POS = 0x3D5, 0x3D6, 0x3D7, 0x3D8
DURATION_OINTMENT, DURATION_GLOVE, DURATION_WING_BOOTS = 0x427, 0x428, 0x429   # $FF: none
SPECIAL_ITEMS, HP, HP_FRACTION = 0x42C, 0x431, 0x432
AREA_REGION, TITLE, PLAYER_IS_DEAD, TEMPLE_SPAWN_POINT, NEXT_TITLE = 0x435, 0x437, 0x438, 0x439, 0x4ED
SCREEN_BUFFER = 0x600                       # the screen's blocks

# The game's values
AREA_EOLIS, AREA_TRUNK, AREA_MIST, TOWNS, BUILDINGS, AREA_ZENIS = 0, 1, 2, 3, 4, 7
EOLIS, APOLUNE, MASCON, CONFLATE = 0, 1, 3, 5   # temples: TEMPLE_SPAWN_POINT, and Guru travel's
TEMPLE_SCREEN = [0x02, 0x0B, 0x10, 0x1E, 0x23, 0x2B, 0x33, 0x3C]   # DEST_SCREEN_FOR_TEMPLE_SPAWN
PALETTE_EOLIS, PALETTE_TRUNK, PALETTE_TOWER, PALETTE_MIST, PALETTE_ZENIS, PALETTE_TOWN = 0, 6, 7, 0x0A, 0x0F, 0x1B
HAND_DAGGER, LONG_SWORD, SMALL_SHIELD, DELUGE = 0, 1, 0, 0
KEY_J, MATTOCK, WING_BOOTS, RED_POTION, NOTHING = 0x07, 0x09, 0x0F, 0x10, 0xFF
PENDANT, RING_OF_ELF = 0x02, 0x80           # SpecialItems bits
SOLDIER = 3                                 # a title
EXECUTION_HOOD, SUGATA, ZOZURA, INTRO_TRIGGER, HOOD_SHOT = 0x10, 0x18, 0x47, 0x4F, 0x51   # sprite entities
MEDITATE_WITH_YOU, NEGATIVE_THOUGHTS, MARK_OF_JACK = 0x22, 0x24, 0x29   # messages: the Guru's greeting; his
USED_MATTOCK, USED_WING_BOOTS, USED_KEY, WING_BOOTS_GONE = 0xAB, 0xAD, 0xAE, 0xC0   # words after a death; ...
GURU_MENU, GURU_SAVED = 1, 3                # files_state["guru"]: fax_files.c's GuruPhase
TRAVEL_OFFER, TRAVEL_TO_GURU = 1, 4         # travel_state["text"]: fax_travel.c's lines (TO_GURU + temple)
EOLIS_GATE_DOOR_X = 195


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


def differ(a, b, box):
    """How many pixels of two pictures differ inside `box`."""
    x0, y0, x1, y1 = box
    return sum(1 for y in range(y0, y1) for x in range(x0, x1) if a[y][x] != b[y][x])


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
    share. A recipe's game is one of these too."""

    # Getting somewhere
    def die_into(self, temple, writes=None):
        """From wherever the hero is, die and wake at `temple`'s Guru
        (TEMPLE_SPAWN_POINT), paging through his words: a way to travel."""
        self.poke({TEMPLE_SPAWN_POINT: temple, PLAYER_IS_DEAD: 1, HP: 0x50, **(writes or {})})
        self.until("the Guru's words", lambda: self.ram(MESSAGE_ID) == NEGATIVE_THOUGHTS, 900)
        for _ in range(60):
            if not self.box_open():
                break
            if self.typed():
                self.act("a", 6, 14)
            else:
                self.wait(4)
        self.until("the temple", lambda: self.ram(AREA) == BUILDINGS and not self.ram(PLAYER_IS_DEAD), 600)
        self.wait(30)

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

    def at(self, area, screen):
        """At (area, screen), loaded, not scrolling, no box open, alive."""
        return self.where() == (area, screen) and self.settled() and not self.box_open() \
            and not self.ram(PLAYER_IS_DEAD)

    def standing(self):
        """On the ground (neither jumping nor falling), at the same Y two frames apart."""
        y, flags = self.ram(POS_Y), self.ram(PLAYER_FLAGS)
        if flags & (JUMPING | FALLING):
            return False
        self.wait(2)
        return not self.ram(PLAYER_FLAGS) & (JUMPING | FALLING) and self.ram(POS_Y) == y

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


class MovementGame(play.Game, Game):
    """The scenes' Game with tests/movement/play.py's: to_trunk(), mods() and
    headless() (whose screenshot is the bare picture), for the ledge."""


def state_ram(path, addr):
    """A byte of CPU RAM in a save state (its HW chunk, as fixture() edits it)."""
    data = Path(path).read_bytes()
    return data[next(p for tag, p, _ in fax_e2e.chunks(data) if tag == b"HW  ") + 23 + addr]


def continues_at(state):
    """The frame a save state continues at: the uint32 at its offset 28."""
    return struct.unpack_from("<I", Path(state).read_bytes(), 28)[0]


class Line:
    """One TCP connection kept open, so commands sent together are served at
    one frame boundary (tests/block/block_shield.py's Line)."""

    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), 5)
        self.sock.settimeout(30)
        self.replies = self.sock.makefile("rb")

    def batch(self, *cmds):
        self.sock.sendall(b"".join((json.dumps(dict(id=i, cmd=cmd, **kw)) + "\n").encode()
                                   for i, (cmd, kw) in enumerate(cmds)))
        replies = [json.loads(self.replies.readline()) for _ in cmds]
        for (cmd, _), reply in zip(cmds, replies):
            if not reply.get("ok"):
                raise RuntimeError(f"{cmd}: {reply}")
        return replies

    def close(self):
        self.replies.close()                # the socket's descriptor stays open while its file is
        self.sock.close()


class Scene:
    """What a scene function gets: the game, and shot() and picture() to take
    its pictures."""

    def __init__(self, name, game):
        self.name, self.game, self.panels, self.sources = name, game, [], []

    def shot(self, caption=None, box=(0, 0, 256, 240), name=None):
        """Screenshot the game's picture (what the window shows, with the
        remaster's drawing on it), cropped to `box` (x0, y0, x1, y1)."""
        return self.picture(self.game.shot(name or (caption or self.name).lower().replace(" ", "-")), caption, box)

    def picture(self, path, caption=None, box=None):
        """A picture from a file: a screenshot, or a headless run's."""
        width, rows = read_png(path)
        self.panels.append((crop(rows, box or (0, 0, width, len(rows))), caption))
        self.sources.append(Path(path).name)
        return path

    def stack(self, pictures):
        """Pictures (rows, caption) one above the other as one panel, each
        under its caption, left-aligned: for strips, so their numbers line up."""
        rows, width = [], max(len(r[0]) for r, _ in pictures)
        for i, (picture, caption) in enumerate(pictures):
            if i:
                rows += [[BLACK] * width for _ in range(GAP // 2)]
            line = [[BLACK] * width for _ in range(CAPTION_ROWS)]
            paste(line, text(self.game.image, caption), 2, (CAPTION_ROWS - 8) // 2)
            rows += line + [row + [BLACK] * (width - len(row)) for row in picture]
        self.panels.append((rows, None))

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

@scene("title")
def title(s):
    """REMASTERED on the title: the original's title, then the remaster's."""
    g = s.game
    g.set_features({REMASTER: False})
    g.launch()
    g.until("the title", lambda: g.frame() > 700, 1500)
    s.shot("Original", box=(0, 8, 256, 240))
    g.close()
    g.set_features({})
    g.launch()
    g.until("the title", lambda: g.tcp("title_state")["shown"], 2000)
    g.wait(10)
    s.shot("Remastered", box=(0, 8, 256, 240))


@scene("save-files")
def save_files(s):
    """Save files: a Guru's choice, then the file select with that file saved
    (tests/files/save_files.py's step 2b, then its step 3)."""
    g = s.game
    g.launch()
    g.until("the file select", lambda: g.tcp("files_state")["shown"], 2000)
    g.wait(10)
    g.act("down")                                               # file 2, so the select shows all three
    g.until("the cursor on file 2", lambda: g.tcp("files_state")["cursor"] == 1, 120)
    g.tcp("pad", buttons="start", frames=10)
    g.until("file 2 in play", lambda: g.tcp("files_state")["file"] == 2, 300)
    g.until("the Eolis gate", lambda: g.frame() > 1900, 3000)
    # Some progress (an external prerequisite): Adept earned, gold and experience,
    # then a death that wakes the hero at Conflate's Guru.
    g.poke({TITLE: 3, NEXT_TITLE: 4, EXPERIENCE: 4900 & 255, EXPERIENCE + 1: 4900 >> 8,
            GOLD: 123456 & 255, GOLD + 1: 123456 >> 8 & 255, GOLD + 2: 123456 >> 16, SPECIAL_ITEMS: 0x82})
    g.die_into(CONFLATE)
    g.act("left", 170, 10)                                      # to the Guru, and talk
    g.act("up", 4, 10)
    for _ in range(12):
        g.until("a page to type out", g.typed)
        if g.tcp("files_state")["guru"] == GURU_MENU:
            break
        g.act("a")
    else:
        raise AssertionError(f"the Guru never offered to remember: {g.tcp('files_state')}")
    g.wait(20)
    menu = g.shot("guru-menu")
    g.act("a")                                                  # "Remember my journey."
    g.until("the saved line", lambda: g.tcp("files_state")["guru"] == GURU_SAVED, 300)
    g.close()
    g.launch()
    g.until("the file select", lambda: g.tcp("files_state")["shown"], 2000)
    g.wait(10)
    g.act("down")                                               # the cursor on the saved file
    g.until("the cursor on file 2", lambda: g.tcp("files_state")["cursor"] == 1, 120)
    g.wait(10)
    s.shot("Your files", box=(0, 8, 256, 240))
    s.picture(menu, "A Guru", box=(0, 8, 256, 240))


# Conversations: the name tab, the portrait and the text box; and where the
# box blinks its end mark (the arrow that says more follows, or the question
# mark), on InterruptCounter & $10 (TextBox_DrawDownArrowTerminatorSymbol).
DIALOGUE, END_MARK = (8, 96, 248, 200), (144, 176, 176, 185)


@fax_e2e.recipe("readme-conflate-temple", parent="gate")
def _conflate_temple(g):
    """Died outside Eolis's gate with Conflate's temple his spawn point: back
    in Conflate's temple, after the Guru's words."""
    g.die_into(CONFLATE)
    assert g.where() == (BUILDINGS, TEMPLE_SCREEN[CONFLATE]), g.where()


def travel_line(g):
    return g.tcp("travel_state")["text"]


@scene("guru-travel")
def guru_travel(s):
    """Guru travel: the Guru's offer, and the first Guru he offers to send
    you to, with the Gurus of Eolis, Apolune, Mascon and Conflate met
    (tests/travel/guru_travel.py)."""
    g = s.game
    (g.out / "game" / "faxanadu_gurus.txt").write_text("Eolis\nApolune\nMascon\nConflate\n")
    g.launch(at="readme-conflate-temple")
    g.act("left", 170, 10)                                      # to the Guru, and talk
    g.act("up", 4, 10)
    g.until("the Guru's greeting", lambda: g.ram(MESSAGE_ID) == MEDITATE_WITH_YOU)
    for _ in range(12):                                         # A on each page, to the offer
        g.until("a page to type out", lambda: travel_line(g) == TRAVEL_OFFER or g.typed())
        if travel_line(g) == TRAVEL_OFFER:
            break
        g.act("a")
    g.until("the offer", lambda: travel_line(g) == TRAVEL_OFFER, 300)
    g.until("the offer to type out", g.typed)
    g.wait(10)
    s.picture(g.shot_lit(END_MARK, "offer"), "The offer", DIALOGUE)
    g.act("a")                                                  # yes: he names the Gurus you have met
    g.until("the first Guru", lambda: travel_line(g) == TRAVEL_TO_GURU + EOLIS)
    g.until("the question to type out", g.typed)
    g.act("b")                                                  # no: the next one
    g.until("Apolune", lambda: travel_line(g) == TRAVEL_TO_GURU + APOLUNE)
    g.until("the question to type out", g.typed)
    g.wait(10)
    s.picture(g.shot_lit(END_MARK, "apolune"), "B for the next Guru", DIALOGUE)


STATUS_BAR = (0, 8, 256, 32)                                    # M: and P: bars, E:, G:, T:, the item


@scene("death-keeps-gold")
def death_keeps_gold(s):
    """A Soldier with 5000 experience and 12345 gold dies; the Guru brings him
    back in Mascon's temple. The status bar after, in the original (his title's
    3500 and 1200) and the remaster (all kept)."""
    g = s.game
    hero_exp, hero_gold = 5000, 12345
    dying = g.out / "dying.cycstate"
    fixture(g.state("gate"), dying, {
        TITLE: SOLDIER, NEXT_TITLE: SOLDIER + 1,
        EXPERIENCE: hero_exp & 0xFF, EXPERIENCE + 1: hero_exp >> 8,
        GOLD: hero_gold & 0xFF, GOLD + 1: hero_gold >> 8 & 0xFF, GOLD + 2: hero_gold >> 16,
        HP: 0x10, MP: 0x08, TEMPLE_SPAWN_POINT: MASCON, PLAYER_IS_DEAD: 1})

    def back_in_mascon(name):
        g.load(dying)
        g.until("the Guru's words", lambda: g.ram(MESSAGE_ID) == NEGATIVE_THOUGHTS, 900)
        g.page_until("Mascon's temple", lambda: g.at(BUILDINGS, TEMPLE_SCREEN[MASCON]))
        g.wait(60)
        return g.shot(name)

    g.launch()
    remastered = back_in_mascon("remastered")
    kept = (int.from_bytes(g.ram(EXPERIENCE, 2), "little"), int.from_bytes(g.ram(GOLD, 3), "little"))
    assert kept == (hero_exp, hero_gold), kept
    g.close()
    g.set_features({REMASTER: False})
    g.launch()
    original = back_in_mascon("original")
    reset = (int.from_bytes(g.ram(EXPERIENCE, 2), "little"), int.from_bytes(g.ram(GOLD, 3), "little"))
    assert reset == (3500, 1200), reset
    s.sources += [original.name, remastered.name]
    s.stack([(crop(read_png(path)[1], STATUS_BAR), caption)
             for path, caption in ((original, "Original"), (remastered, "Remastered"))])


TOWER_OF_TRUNK = (AREA_TRUNK, 0x0D)                             # its first screen inside


@fax_e2e.recipe("readme-tower-of-trunk", parent="gate")
def _tower_of_trunk(g):
    """Died at Eolis's gate before any entrance: woken at Eolis's temple, out
    of its door edited to lead to the Tower of Trunk's (Trunk screen $0B,
    block $A9), the J key in hand, and in: standing just inside."""
    g.poke({PLAYER_IS_DEAD: 1, HP: 0x50})
    g.until("the Guru's words", lambda: g.ram(MESSAGE_ID) == NEGATIVE_THOUGHTS, 900)
    g.page_until("Eolis's temple", lambda: g.at(BUILDINGS, TEMPLE_SCREEN[EOLIS]))
    g.wait(30)
    g.warp(g.save("temple"), "tower-door", AREA_TRUNK, 0x0B, PALETTE_TRUNK, 0xA9, AREA_TRUNK, {SELECTED_ITEM: KEY_J})
    g.until("the Tower of Trunk's door", lambda: g.at(AREA_TRUNK, 0x0B), 600)
    g.clear_actors({HP: 0x50})
    for _ in range(6):                                          # in through the door (a knock can take the press)
        g.walk_to(0x09 * 16 - 7, 0x09 * 16 + 8)
        g.act("up", 4, 10)
        try:
            g.page_until("the tower", lambda: g.at(*TOWER_OF_TRUNK), 40)
            break
        except AssertionError:
            pass
    assert g.at(*TOWER_OF_TRUNK), g.where()
    g.until("the hero to stand", g.standing, 300)


@scene("zone-respawn")
def zone_respawn(s):
    """Dying just inside the Tower of Trunk: the original wakes him at Eolis's
    temple; the remaster brings him back at the tower's door."""
    g = s.game
    tower = TOWER_OF_TRUNK
    dying = g.out / "dying-inside.cycstate"
    fixture(g.state("readme-tower-of-trunk"), dying, {PLAYER_IS_DEAD: 1})
    g.launch()

    def died(where, name):
        g.load(dying)
        g.until("the Guru's words", lambda: g.ram(MESSAGE_ID) == NEGATIVE_THOUGHTS, 900)
        g.page_until(name, lambda: g.at(*where))
        g.until("the hero to stand", g.standing, 300)
        if where[0] != BUILDINGS:
            g.clear_actors({HP: 0x50})                          # no enemies wandering into the picture
        g.wait(20)
        return g.shot(name)

    remastered = died(tower, "remastered")
    g.close()
    g.set_features({REMASTER: False})
    g.launch()
    original = died((BUILDINGS, TEMPLE_SCREEN[EOLIS]), "original")
    box = (0, 32, 256, 232)
    s.picture(original, "Original", box)
    s.picture(remastered, "Remastered", box)


# The Fortress of Zenis's screen 16, where an Execution Hood casts at the hero.
SCREEN_16, REGION_ZENIS, SHIELD_HEIGHT = 16, 5, 150
HUD_HEIGHT = 32                                                 # Sprite_Draw adds it to a sprite's Y


@fax_e2e.recipe("readme-hood-shot", parent="eolis-meat-shop")
def _hood_shot(g):
    """Out of the meat shop, its door edited to lead to the Fortress of
    Zenis's screen 16 (tests/block/block.py), its other actors sent away but
    the Execution Hood, as the Hood's shot appears."""
    g.warp(g.save("shop"), "zenis", AREA_ZENIS, SCREEN_16, PALETTE_ZENIS, 0x91, REGION_ZENIS)
    g.wait(20)
    g.poke({SPRITE_ENTITIES + i: 0xFF for i, e in enumerate(g.ram(SPRITE_ENTITIES, 8)) if e != EXECUTION_HOOD})
    assert EXECUTION_HOOD in g.ram(SPRITE_ENTITIES, 8), "the Execution Hood is on Zenis's screen 16"
    g.until("the Hood's shot", lambda: HOOD_SHOT in g.ram(SPRITE_ENTITIES, 8), 900)


def flight(g, slot, hx, name, block):
    """Read the shot and picture the game as often as it answers, on one
    connection, for about 14 answers; with `block`, hold V from the first.
    Returns [(picture, the shot still there, its distance from the hero)]."""
    line = Line(g.port)
    frames = []
    try:
        if block:
            line.batch(("key", dict(name="V", frames=120)))
        for i in range(14):
            path = g.out / f"{name}-{i:02d}.png"
            replies = line.batch(("read_ram", dict(addr=SPRITE_ENTITIES + slot, len=1)),
                                 ("read_ram", dict(addr=SPRITE_X + slot, len=1)),
                                 ("screenshot", dict(layer="picture", path=str(path))))
            frames.append((path, int(replies[0]["hex"], 16) == HOOD_SHOT, int(replies[1]["hex"], 16) - hx))
    finally:
        line.close()
    if block:
        g.tcp("key", name="V", frames=1)
    g.wait(10)
    return frames


@scene("block")
def block(s):
    """The Execution Hood's shot coming at the hero's Small Shield, in the
    Fortress of Zenis (tests/block/block.py), the other actors sent away: not
    blocking, the hero in his own colors; blocking, he glints in the
    shield's. Both on the frame the shot is the same distance from him."""
    g = s.game
    cast = g.state("readme-hood-shot")
    slot = [state_ram(cast, SPRITE_ENTITIES + i) for i in range(8)].index(HOOD_SHOT)
    target = g.out / "shield-height.cycstate"
    alone = {SPRITE_ENTITIES + i: 0xFF for i in range(8) if i != slot}     # the Hood sent away, its shot kept
    fixture(cast, target, {**alone, SPRITE_Y + slot: SHIELD_HEIGHT, SELECTED_SHIELD: SMALL_SHIELD, HP: 0x50})
    # At normal speed, where a reply takes a few frames, not dozens: raise
    # Block as the shot comes, and picture every frame we can until one shows
    # the glint with the shot still in front of the shield.
    g.realtime = True
    g.launch()
    for attempt, lead in enumerate([44, 36, 52, 40, 48, 32, 56, 44, 40, 48] * 2):
        g.load(target)
        g.wait(3)
        hx, hy = g.ram(POS_X), g.ram(POS_Y)
        hero = (hx, hy + HUD_HEIGHT + 1, hx + 16, hy + HUD_HEIGHT + 33)    # his 16x32 box in the picture
        _, reference = read_png(g.shot(f"reference-{attempt}"))
        g.until("the shot to come near", lambda: g.ram(SPRITE_X + slot) - hx <= lead, 300)
        frames = flight(g, slot, hx, f"blocking-{attempt}", block=True)
        glints = [(distance, path) for path, there, distance in frames
                  if there and 16 <= distance <= 32                       # in front of the shield, clear of his box
                  and differ(read_png(path)[1], reference, hero) >= 60]   # his body in the shield's palette
        print(f"blocking, try {attempt}: lead {lead}: {[(t, d) for _, t, d in frames]}, glints {len(glints)}")
        if glints:
            distance, blocking = min(glints)
            break
    else:
        raise AssertionError("no picture of the glint with the shot in front of the shield")
    # Not blocking: the same load, pictured when the shot is as far from him.
    for attempt in range(30):
        g.load(target)
        g.wait(3)
        g.until("the shot to come near", lambda: g.ram(SPRITE_X + slot) - hx <= distance + 12, 300)
        frames = flight(g, slot, hx, f"open-{attempt}", block=False)
        near = sorted((abs(d - distance), path) for path, there, d in frames if there and d >= distance - 3)
        print(f"not blocking, try {attempt}: {[(t, d) for _, t, d in frames]}")
        if near and (near[0][0] == 0 or attempt >= 15 and near[0][0] <= 3):
            open_ = near[0][1]
            break
    else:
        raise AssertionError(f"no picture of the shot {distance} pixels from the hero")
    box = (max(0, hx - 16), hy + HUD_HEIGHT + 1 - 46, max(0, hx - 16) + 128, hy + HUD_HEIGHT + 1 - 46 + 96)
    s.picture(open_, "Not blocking", box)
    s.picture(blocking, "Blocking", box)
    print(f"the shot {distance} pixels in front of him: {open_.name}, {blocking.name}")


@scene("magic-button")
def magic_button(s):
    """Next to Conflate's Guru with Deluge: Up+B starts his conversation;
    the Magic button (C) casts, and he says nothing (tests/magic/magic_button.py)."""
    g = s.game
    g.launch(at="gate")
    g.die_into(CONFLATE, {SELECTED_MAGIC: DELUGE, MAGIC_SPELLS: DELUGE, NUMBER_OF_MAGIC_SPELLS: 1})
    g.until("Conflate's temple", lambda: g.where() == (BUILDINGS, TEMPLE_SCREEN[CONFLATE]), 300)
    g.wait(60)
    g.act("left", 170, 10)
    g.act("right", 2, 4)                                        # facing away from the Guru: a spell flies far
    guru = g.save("guru")
    mp = g.ram(MP)
    g.tcp("action", name="up", frames=10)
    g.tcp("action", name="b", frames=4)
    g.until("the Guru's greeting", lambda: g.ram(MESSAGE_ID) == MEDITATE_WITH_YOU, 120)
    g.until("the greeting to type out", g.typed, 600)
    g.wait(4)
    assert g.ram(MP) == mp, "Up+B cast instead of talking"
    s.shot("Up and B", box=(0, 32, 256, 224))
    g.load(guru)
    g.wait(4)
    g.tcp("key", name="C", frames=3)
    g.until("Deluge", lambda: g.ram(MP) != mp, 60)
    g.wait(12)
    assert g.ram(MESSAGE_ID) != MEDITATE_WITH_YOU and not g.box_open()
    s.shot("Magic button", box=(0, 32, 256, 224))


# The Mist's screen 30, where Sugata flashes.
SCREEN_30, SUGATA_REACH = 30, 40


@scene("sugata")
def sugata(s):
    """Sugata's warning (it blinks, the band its flash will reach dims) and
    its flash (only that band greys), the hero standing just out of it: the
    game's own captures (TCP sugata_capture), as tests/sugata/sugata_flash.py."""
    g = s.game
    shop = g.state("eolis-meat-shop")
    g.launch(at="eolis-meat-shop")
    g.warp(shop, "mist-30", AREA_MIST, SCREEN_30, PALETTE_MIST, 0x92, AREA_MIST)
    entities = list(g.ram(SPRITE_ENTITIES, 8))
    assert SUGATA in entities, entities
    slot = entities.index(SUGATA)
    g.until("Sugata's warning", lambda: g.tcp("sugata_state")["warning"] > 0, 600)
    warning = g.save("warning")
    center = g.ram(SPRITE_X + slot) + 8
    away = g.out / "warning-away.cycstate"
    fixture(warning, away, {POS_X: center + SUGATA_REACH + 16, HP: 0x50})
    for name, band, at, caption in (("warning", "warning", 28, "Warning"), ("flash", "flash", 0, "Flash")):
        before = g.tcp("sugata_state")["captured"]
        path = g.out / f"{len(g.shots):02d}-{name}.png"
        g.tcp("sugata_capture", path=str(path), band=band, at=at)
        g.load(away)
        g.until(f"the {name} picture", lambda: g.tcp("sugata_state")["captured"] > before, 120)
        g.shots.append(path.name)
        s.picture(path, caption, box=(0, 32, 256, 240))


# Two Zozura (9 HP, 7 of damage) crawling on Eolis screen 4, as
# tests/damage/damage_numbers.py, tests/pendant/pendant_fix.py and
# tests/dagger/taller_dagger.py reach them. Its tower, at about x 160-208,
# is drawn over sprites, so fights are staged left of it.
ZOZURA_SCREEN, NEAR, FAR, GROUND = 4, 7, 6, 144
SWING = ((0, "B"), (2, "-"))                                    # B on the save's first frame


@fax_e2e.recipe("readme-zozuras", parent="eolis-zozuras")
def _zozuras(g):
    """The Zozuras' screen settled, the hero standing at x 240 facing left and
    the two crawling toward him (zozura_case), with no damage number left
    from the walk."""
    g.until("the screen to settle", g.settled, 300)
    g.load(zozura_case(g, g.save("arrived"), "placed", {}))
    g.until("no numbers", lambda: g.tcp("damage_state")["live"] == 0, 90)


def facing(base, slot, right):
    """The Zozura in `slot`'s CurrentSprites_Flags in `base`, facing right or left."""
    flags = state_ram(base, SPRITE_FLAGS + slot)
    return flags | SPRITE_FACING_RIGHT if right else flags & ~SPRITE_FACING_RIGHT & 0xFF


def zozura_case(g, base, name, writes):
    """`base` with the hero standing at x 240 facing left, full HP, the Long
    Sword in hand, no Pendant, Glove, Ointment or armor, and the Zozura on the
    ground at 150 and 70 crawling toward him; then `writes`. Its path."""
    zozura = {}
    for slot, x in ((NEAR, 150), (FAR, 70)):
        zozura.update({SPRITE_X + slot: x, SPRITE_Y + slot: GROUND + 16, SPRITE_HIT_COUNTER + slot: 0,
                       SPRITE_FLAGS + slot: facing(base, slot, True)})
    path = g.out / f"{name}.cycstate"
    fixture(base, path, {POS_X: 240, POS_Y: GROUND, PLAYER_FLAGS: 0, PLAYER_STATUS: 0, INVINCIBILITY: 0,
                         PLAYER_CUR_WEAPON: LONG_SWORD, SELECTED_WEAPON: LONG_SWORD, HP: 0x50,
                         SELECTED_ARMOR: 0, SPECIAL_ITEMS: 0, DURATION_GLOVE: 0xFF, DURATION_OINTMENT: 0xFF,
                         **zozura, **writes})
    return path


@scene("pendant")
def pendant(s):
    """The same Long Sword swing at a Zozura (9 HP): 8 without the Pendant,
    leaving it 1 HP; 10 with it, a kill. Played headless from the Zozura's save."""
    g = s.game
    base = g.state("readme-zozuras")
    for caption, special in (("No Pendant", 0), ("Pendant", PENDANT)):
        name = caption.lower().replace(" ", "-")
        swing = zozura_case(g, base, name, {
            SPECIAL_ITEMS: special,
            POS_X: 100,                                         # left of the tower, which hides sprites
            SPRITE_X + NEAR: 100 - 26,                          # in the swing's reach
            SPRITE_ENTITIES + FAR: 0xFF})                       # the other out of the picture
        rams = g.replay(swing, SWING, 60)
        hit = next(i for i, ram in enumerate(rams)
                   if ram[SPRITE_ENTITIES + NEAR] != ZOZURA or ram[SPRITE_HP + NEAR] != 9)
        picture = g.out / f"{name}.png"
        g.replay(swing, SWING, hit + 1 + 12, picture=picture)
        s.picture(picture, caption, box=(40, 112, 144, 216))


@scene("damage-numbers")
def damage_numbers(s):
    """One swing hits a Zozura in front of the hero (a white 8), then the other
    bites him from behind (a pink 7). Played headless from the Zozura's save."""
    g = s.game
    base = g.state("readme-zozuras")
    hero = 100                                                  # left of the tower, which hides sprites
    fight = zozura_case(g, base, "fight", {
        POS_X: hero, PLAYER_FLAGS: FACING_RIGHT,
        SPRITE_X + NEAR: hero + 26,                             # in front of him, in the swing's reach
        SPRITE_FLAGS + NEAR: facing(base, NEAR, False),
        SPRITE_X + FAR: hero - 22,                              # behind him, crawling his way
        SPRITE_FLAGS + FAR: facing(base, FAR, True)})
    rams = g.replay(fight, SWING, 90)
    hit = next((i for i, ram in enumerate(rams) if ram[SPRITE_HP + NEAR] != 9), None)
    hurt = next((i for i, ram in enumerate(rams) if ram[HP] != 0x50), None)
    assert hit is not None and hurt is not None and hit < hurt, (hit, hurt)
    picture = g.out / "damage-numbers.png"                      # a frame after the bite: the 7 over him, the 8 risen
    g.replay(fight, SWING, hurt + 2, picture=picture)
    s.picture(picture, box=(32, 104, 176, 216))


@scene("taller-dagger")
def taller_dagger(s):
    """The Hand Dagger swung at a Zozura crawling in, on the first frame of
    the swing's low hit phase: the original's passes over it, the
    remaster's hits (tests/dagger/taller_dagger.py's Eolis screen 4). Each
    swing runs headless from one save state, so both are the same moment."""
    g = s.game
    target, tough, close = 7, 60, 23                            # the Zozura, its HP (it survives), the press
    case = g.out / "case.cycstate"
    fixture(g.state("readme-zozuras"), case, {PLAYER_CUR_WEAPON: HAND_DAGGER, SELECTED_WEAPON: HAND_DAGGER, HP: 0x50,
                           DURATION_OINTMENT: 0xFF, SPRITE_HP + target: tough})
    # When the Zozura is close, the press; then the first frame of the low hit phase.
    rams = g.replay(case, [], 300)
    near = next(i for i, m in enumerate(rams) if m[POS_X] - m[SPRITE_X + target] <= close)
    swing = [(near, "B"), (near + 2, "-")]
    rams = g.replay(case, swing, near + 40)
    low = next(i for i, m in enumerate(rams) if m[PLAYER_FLAGS] & ATTACKING and m[ATTACK_PHASE] == 1)
    for original, caption in ((True, "Original"), (False, "Remastered")):
        path = g.out / f"{caption.lower()}.png"
        rams = g.replay(case, swing, low + 2, original=original, picture=path)
        hit = min(m[SPRITE_HP + target] for m in rams) < tough
        assert hit != original, f"{caption}: the dagger {'hit' if original else 'missed'} the Zozura"
        s.picture(path, caption, box=(144, 152, 256, 216))


FOREPAW, FOREPAW_TOOLS_DOOR, FOREPAW_TOOL_SHOP = 0x02, 0x9D, 0x0F   # the towns' screen 2, its door, building $0F


def to_counter(g):
    """Walk left in a shop until the hero stops: at its counter."""
    for _ in range(60):
        before = g.ram(POS_X)
        g.act("left", 8, 2)
        if g.ram(POS_X) == before:
            return
    raise AssertionError("never reached the counter")


@fax_e2e.recipe("readme-forepaw-counter", parent="eolis-meat-shop")
def _forepaw_counter(g):
    """Out of the meat shop, its door edited to lead to Forepaw's tool shop's
    (the towns' screen 2), the Long Sword in hand; in, and to the counter."""
    forepaw = g.out / "forepaw.cycstate"
    fixture(g.save("shop"), forepaw, {AREA_SAVED_AREA: TOWNS, AREA_SAVED_SCREEN: FOREPAW,
                                      SCREEN_SAVED_PALETTE: PALETTE_TOWN, PLAYER_SAVED_POS: FOREPAW_TOOLS_DOOR,
                                      SELECTED_WEAPON: LONG_SWORD, PLAYER_CUR_WEAPON: LONG_SWORD, HP: 0x50})
    g.load(forepaw)
    for _ in range(300):
        if g.ram(AREA) != BUILDINGS:
            break
        g.act("right", 2, 0)
    g.until("Forepaw", lambda: g.where() == (TOWNS, FOREPAW))
    g.wait(30)
    # Back to the door. Not walk_to(): in the towns TextBox_Closed reads 0 with
    # no box open, so it would page forever (tests/indoor/indoor_equip.py).
    column = (FOREPAW_TOOLS_DOOR & 15) * 16
    for _ in range(600):
        x = g.ram(POS_X)
        if column - 6 <= x <= column + 4:
            break
        g.act("right" if x < column - 6 else "left", 2, 0)
    else:
        raise AssertionError("could not reach the tool shop's door")
    g.act("up", 4, 10)
    g.until("the tool shop", lambda: g.ram(AREA) == BUILDINGS)
    g.wait(40)
    to_counter(g)
    assert g.ram(PLAYER_CUR_WEAPON) == LONG_SWORD, "the sword is not in hand"


@scene("equip-indoors")
def equip_indoors(s):
    """A swing at the counter of Forepaw's tool shop with the Long Sword: the
    original empties his hand indoors, the remaster keeps the sword in it.
    Both swings run headless from one save state, as
    tests/indoor/indoor_equip.py does, on the same frame of the swing."""
    g = s.game
    counter = g.state("readme-forepaw-counter")
    stock = g.out / "counter-original.cycstate"
    fixture(counter, stock, {PLAYER_CUR_WEAPON: NOTHING})      # as the original leaves it, coming in
    swing = [(2, "B"), (5, "-")]
    rams = g.replay(counter, swing, 40)
    hit = next(i for i, m in enumerate(rams) if m[PLAYER_FLAGS] & ATTACKING and m[ATTACK_PHASE] == 2)
    for state, original, caption in ((stock, True, "Original"), (counter, False, "Remastered")):
        path = g.out / f"{caption.lower()}.png"
        g.replay(state, swing, hit + 3, original=original, picture=path)
        s.picture(path, caption, box=(0, 96, 160, 216))


# The map's save-state record (src/fax_map.c, as tests/map/explored_map.py writes it): a version, then the
# screens seen, those left upward and those left downward (11 bytes an area each), then the doors used (6 an area).
MAP_RECORD, MAP_SEEN, MAP_UP, MAP_DOORS = "faxanadu.map", 1, 1 + 88, 1 + 264
# An early game's journey: all of Eolis, then the Trunk from its first screen past Apolune (the towns' 0 and
# 1), up the climb ($0C to $16) and on to Forepaw (the towns' 2 and 3). The rest of the Trunk is unseen.
MAP_SEEN_SCREENS = {AREA_EOLIS: set(range(9)), AREA_TRUNK: set(range(0x0D)) | set(range(0x16, 0x1B)),
                    TOWNS: {0, 1, 2, 3}}
MAP_UP_SCREENS = {AREA_TRUNK: {0x03, 0x0C, 0x16, 0x17}}         # the ladders he took, kept on the screen below
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
    does), the climb and the ladders he took, every door on those screens
    used, and the hero on the screen left of Forepaw, his box caught lit as
    it blinks. Eolis is seen too, so the page has its arrows."""
    g = s.game
    shop = g.state("eolis-meat-shop")
    g.launch(at="eolis-meat-shop")
    record = {**map_bits(MAP_SEEN, 11, MAP_SEEN_SCREENS), **map_bits(MAP_UP, 11, MAP_UP_SCREENS),
              **map_bits(MAP_DOORS, 6, {area: set(range(48)) for area in MAP_SEEN_SCREENS})}   # drawn on seen screens
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
    s.picture(path)


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
    s.shot(name="log")


# Trunk's ledge (tests/movement/play.py's to_trunk): screen 7, a clear 80-pixel drop.
JUMP_BOX, POSE_BOX = (0, 96, 136, 232), (0, 128, 96, 224)
WALK_OFF = [(0, "-"), (5, "RIGHT")]                             # stand, then walk right off the ledge


def trunk_ledge(g):
    """Trunk's ledge (play.py's trunk-ledge fixture, as the movement tests
    use): with no actors, the hero facing right at its edge on the first
    step of his walk; and mod catalogs for headless runs with the remaster
    on and off."""
    ledge = g.out / "ledge.cycstate"
    fixture(g.state("trunk-ledge"), ledge, {**play.no_actors(), play.X: 8, play.Y: 80,
                                               play.FLAGS: play.RIGHT, play.TICK: 0, HP: 0x50})
    return ledge, g.mods("mods-on"), g.mods("mods-off", original=True)


def every_frame(g, state, schedule, frames, mods, name):
    """A headless run from `state` (play.py's headless()), with the bare
    picture of every frame: (each frame's RAM, each frame's picture path)."""
    folder = g.out / name
    folder.mkdir(exist_ok=True)
    rams = g.headless(state, schedule, frames, mods)
    start = continues_at(state)
    run = g.out / "run"
    run.mkdir(exist_ok=True)
    (run / "input.txt").write_text("".join(f"{start + at} {buttons}\n" for at, buttons in sorted(schedule)))
    done = subprocess.run([str(g.exe), str(g.rom), "--load-state", str(state), "--input", str(run / "input.txt"),
                           "--frames", str(start + frames), "--mods-root", str(mods), "--no-save",
                           "--screenshot", str(folder / "f.png"), "--shot-every", "1"],
                          capture_output=True, text=True)
    assert done.returncode == 0, done.stdout + done.stderr
    return rams, [folder / f"f_{start + i:05d}.png" for i in range(frames)]


def hero_box(ram):
    """Where the hero's sprites can be, from his position (generous)."""
    return ram[play.X] - 8, ram[play.Y] + 16, ram[play.X] + 32, ram[play.Y] + 80


def overlap(a, b):
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


def hero_pixels(rams, pictures, k):
    """The hero's pixels in frame k's picture, {(x, y): color}: those in his box
    that differ from a frame where he is elsewhere (nothing else moves). A
    picture shows the sprites of the frame before it."""
    box = hero_box(rams[k - 1])
    other = next(j for j in range(len(rams) - 1, 0, -1) if not overlap(box, hero_box(rams[j - 1])))
    _, here = read_png(pictures[k])
    _, there = read_png(pictures[other])
    x0, y0, x1, y1 = box
    return {(x, y): here[y][x] for y in range(max(y0, 0), min(y1, 240)) for x in range(max(x0, 0), min(x1, 256))
            if here[y][x] != there[y][x]}


def trail(rams, pictures, dots, ghosts, end):
    """Frame `end`'s picture with the hero's path drawn in: a dot at his feet
    on each of the frames `dots`, and him half see-through on each of `ghosts`."""
    _, canvas = read_png(pictures[end])
    for k in ghosts:
        for (x, y), color in hero_pixels(rams, pictures, k).items():
            canvas[y][x] = tuple((a + b) // 2 for a, b in zip(color, canvas[y][x]))
    for k in dots:
        cx, cy = rams[k][play.X] + 12, rams[k][play.Y] + 62
        for y in (cy, cy + 1):
            for x in (cx, cx + 1):
                canvas[y][x] = WHITE
    return canvas


@scene("forgiving-jumps", MovementGame)
def forgiving_jumps(s):
    """Forgiving jumps: A pressed 3 frames after walking off Trunk's ledge. The
    original falls; the remaster jumps from where he is. Frame exact, headless
    from a save state, as tests/movement/forgiving_jumps.py does. Each panel
    is the frame he has landed and walked on to, with his path drawn in (a
    dot at his feet each frame) and him half see-through where A went down
    and 10 frames later."""
    g = s.game
    ledge, on, off = trunk_ledge(g)
    plain = g.headless(ledge, WALK_OFF, 60, off)
    w = next(i for i, r in enumerate(plain) if r[play.Y] > 80)  # the first frame off the ledge
    late = WALK_OFF + [(w + 2, "RIGHT+A"), (w + 3, "RIGHT")]
    for caption, mods in (("Original", off), ("Remastered", on)):
        rams, pictures = every_frame(g, ledge, late, w + 44, mods, caption.lower())
        s.panels.append((crop(trail(rams, pictures, range(w - 6, w + 42), (w + 3, w + 13), w + 42), JUMP_BOX),
                         caption))
        s.sources.append(f"{caption.lower()}/")


@scene("falling-pose", MovementGame)
def falling_pose(s):
    """The falling pose: walking off Trunk's ledge, the same frame mid-fall,
    the original (a walking pose in the air) and the remaster (the jump pose).
    Frame exact, headless from a save state, as tests/movement/falling_pose.py does."""
    g = s.game
    ledge, on, off = trunk_ledge(g)
    plain = g.headless(ledge, WALK_OFF, 60, off)
    w = next(i for i, r in enumerate(plain) if r[play.Y] > 80)  # the first frame off the ledge
    for caption, mods in (("Original", off), ("Remastered", on)):
        path = g.out / f"{caption.lower()}.png"
        g.headless(ledge, WALK_OFF, w + 8, mods, path)          # clear of the ledge
        s.picture(path, caption, POSE_BOX)


# Ladders (tests/ladders/ladders.py): the fountain's screen and Trunk 6's ladder.
QUESTS, SPRINGS_AND_PATH = 0x42D, 0x27      # the three springs done, and the path to Mascon open
FOUNTAIN_BOX, LADDER_BOX = (0, 40, 176, 216), (136, 112, 256, 232)


@fax_e2e.recipe("readme-trunk-27", parent="apolune-temple")
def _readme_trunk_27(g):
    """Out of Apolune's temple, through its door edited to lead to Trunk $27,
    a screen right of the fountain's ($28)."""
    g.warp(g.save("temple"), "trunk-27", AREA_TRUNK, 0x27, PALETTE_TRUNK, 0x91, AREA_TRUNK)
    g.settle()


@scene("ladders", MovementGame)
def ladders(s):
    """Ladders, fixed: with the path to Mascon open, the hero walks left into
    the fountain's screen and straight back out to $27. The original drops the
    fountain's ladder into $27, through its floor; the remaster leaves $27 as it
    is. Frame exact, headless from a save state, as tests/ladders/ladders.py does."""
    g = s.game
    state = g.out / "trunk-27.cycstate"
    fixture(g.state("readme-trunk-27"), state, {**play.no_actors(), QUESTS: SPRINGS_AND_PATH, HP: 0x50})
    for caption, mods in (("Original", g.mods("mods-off", original=True)), ("Remastered", g.mods("mods-on"))):
        path = g.out / f"{caption.lower()}.png"
        g.headless(state, [(0, "LEFT"), (60, "RIGHT")], 300, mods, path)
        s.picture(path, caption, FOUNTAIN_BOX)


@scene("ladder-hold", MovementGame)
def ladder_hold(s):
    """Holding on to ladders: a hit on Trunk 6's ladder (the state the hit
    leaves: invincibility, knockback, facing). The original knocks him off and
    he falls; the remaster keeps him on it. Frame exact, headless from a save
    state, as tests/ladders/ladders.py does."""
    g = s.game
    hit = g.out / "hit.cycstate"
    fixture(g.state("trunk-ladder"), hit, {**play.no_actors(), play.X: 224, play.Y: 128, play.TICK: 0,
                                           play.FLAGS: play.CAN_CLIMB | play.CLIMBING | play.RIGHT, HP: 0x50,
                                           play.STATUS: play.KNOCKBACK, play.IFRAMES: 0x3C})
    for caption, mods in (("Original", g.mods("mods-off", original=True)), ("Remastered", g.mods("mods-on"))):
        path = g.out / f"{caption.lower()}.png"
        g.headless(hit, [(0, "-")], 5, mods, path)               # mid-fall in the original
        s.picture(path, caption, LADDER_BOX)


# Keys and the Mattock, from Eolis's town (tests/keys/auto_and_reusable_keys.py).
EOLIS_EXIT, MATTOCK_WALL = 0x08, 0x0C                           # Eolis's exit door (a J key); Trunk's Mattock wall
WALL_BLOCKS, WALL_BLOCK, WALL_STOP = (0xA3, 0xB3), 0x63, 0x20
TEXT_PROMPT = (216, 92, 232, 104)                               # the exit door's text box: its blinking prompt
MENU_ITEM_ROW = 4                                               # WEAPON ARMOR SHIELD MAGIC ITEM PLAYER


def items(g):
    """The unselected items, in order, and the selected one."""
    return list(g.ram(ITEMS, 8)[:g.ram(NUMBER_OF_ITEMS)]), g.ram(SELECTED_ITEM)


def enter(g, direction, area, screen):
    """Walk off the screen's edge into `screen`, wait for the scroll, clear its enemies."""
    g.walk(direction, lambda: g.where() == (area, screen), f"screen ${screen:02X}")
    g.until("the scroll", (lambda: g.ram(POS_X) < 0x20) if direction == "right" else (lambda: g.ram(POS_X) >= 0xE0),
            300)
    g.wait(10)
    g.clear_actors()


def item_menu(g):
    """The Select menu's item list."""
    g.act("select", 4, 30)
    for _ in range(MENU_ITEM_ROW):
        g.act("down", 4, 8)
    g.act("a", 4, 30)


def select_item(g, item):
    """Choose `item` in the item menu, as a player would."""
    listed, _ = items(g)
    item_menu(g)
    for _ in range(listed.index(item)):
        g.act("down", 4, 8)
    g.act("a", 4, 60)
    assert items(g)[1] == item, items(g)


def press_up(g, expect):
    """Up at a door; the message it shows, once typed out."""
    g.tcp("action", name="up", frames=4)
    seen = []

    def shown():
        seen.append(g.ram(MESSAGE_ID))
        return seen[-1] in expect
    g.until(f"one of messages {[hex(m) for m in expect]}", shown, 300)
    g.until("the message to type out", lambda: g.ram(MESSAGE_ID) == 0, 600)
    g.wait(4)
    return seen[-1]


def at_exit(g, keys, item):
    """From the town with the J key and the Mattock: `item` selected, Up at
    Eolis's exit door; the message it shows."""
    g.load(keys)
    select_item(g, item)
    enter(g, "right", AREA_EOLIS, EOLIS_EXIT)
    g.walk("right", lambda: g.ram(POS_X) >= 186, "the door")
    g.poke({POS_X: 188, MOVEMENT_TICK: 0})                      # the same place and step each run
    g.wait(4)
    return press_up(g, {USED_KEY, MARK_OF_JACK})


def through_exit(g):
    """A to the door's message: through it, into Trunk."""
    g.act("a", 6, 10)
    g.until("Trunk", lambda: g.where() == (AREA_TRUNK, 0) and g.ram(POS_X) < 0xF0, 900)
    g.wait(40)


def break_wall(g):
    """In Trunk, through to the Mattock's wall; walk up against it, Down+B,
    and wait for it to crumble."""
    g.clear_actors({SCREEN_TO_THE_RIGHT: MATTOCK_WALL})
    enter(g, "right", AREA_TRUNK, MATTOCK_WALL)
    assert [g.ram(SCREEN_BUFFER + b) for b in WALL_BLOCKS] == [WALL_BLOCK] * 2, "the wall is there"
    g.walk("right", lambda: g.ram(POS_X) >= WALL_STOP, "the wall")
    for _ in range(3):
        g.act("right", 4, 4)
    g.tcp("action", name="down", frames=20)
    g.wait(6)
    g.tcp("action", name="b", frames=4)
    g.until("I've used Mattock.", lambda: g.ram(MESSAGE_ID) == USED_MATTOCK, 300)
    g.until("the message to type out", lambda: g.ram(MESSAGE_ID) == 0, 600)
    g.act("a", 6, 10)
    g.until("the wall to crumble", lambda: g.ram(SCREEN_BUFFER + WALL_BLOCKS[0]) != WALL_BLOCK, 300)
    g.until("the box to close", lambda: not g.box_open(), 300)
    g.wait(30)


def with_keys(g):
    """Eolis's town with the J key and the Mattock, nothing selected: its save state."""
    keys = g.out / "keys.cycstate"
    fixture(g.state("eolis-town"), keys, {HP: 0x50, ITEMS: KEY_J, ITEMS + 1: MATTOCK, NUMBER_OF_ITEMS: 2,
                                      SELECTED_ITEM: NOTHING})
    return keys


@scene("keys")
def keys(s):
    """Keys that find themselves: the Mattock selected and the J key in the
    inventory, Up at Eolis's exit door. The original says the door bears the
    mark of Jack and stays shut; the remaster opens it with the key, and the
    Mattock stays selected (the status bar's item)."""
    g = s.game
    keys = with_keys(g)
    g.launch(at="eolis-town")
    assert at_exit(g, keys, MATTOCK) == USED_KEY
    remastered = g.shot_lit(TEXT_PROMPT, "remastered")
    g.close()
    g.set_features({REMASTER: False})
    g.launch(at="eolis-town")
    assert at_exit(g, keys, MATTOCK) == MARK_OF_JACK
    box = (0, 8, 256, 224)
    s.picture(g.shot_lit(TEXT_PROMPT, "original"), "Original", box)
    s.picture(remastered, "Remastered", box)


@scene("tools-that-last")
def tools_that_last(s):
    """Tools that last: a J key opens Eolis's exit door and the Mattock
    breaks Trunk's wall, then the item list. In the original both are used
    up ("NO ITEM", and the status bar's item empty); in the remaster the key
    is still in the list and the Mattock still selected."""
    g = s.game
    keys = with_keys(g)
    g.launch(at="eolis-town")
    assert at_exit(g, keys, MATTOCK) == USED_KEY
    through_exit(g)
    break_wall(g)
    assert items(g) == ([KEY_J], MATTOCK), items(g)
    item_menu(g)
    remastered = g.shot("remastered")
    g.close()
    g.set_features({REMASTER: False})
    g.launch(at="eolis-town")
    assert at_exit(g, keys, KEY_J) == USED_KEY
    through_exit(g)
    select_item(g, MATTOCK)
    break_wall(g)
    assert items(g) == ([], NOTHING), items(g)
    item_menu(g)
    box = (0, 8, 256, 216)
    s.shot("Original", box)
    s.picture(remastered, "Remastered", box)


T_DIGITS = (190, 16, 208, 24)                                   # the status bar's T: seconds
SECOND, NOVICE_FLIGHT = 64, 30                                  # frames in the game's second; a Novice's flight


def use_item(g):
    """Down+B: the selected item."""
    g.tcp("action", name="down", frames=24)
    g.wait(6)
    g.tcp("action", name="b", frames=4)
    g.wait(24)


def dismiss(g, message, frames=900):
    """Wait for `message`, then page through it until the box closes."""
    g.until(f"message ${message:02X}", lambda: g.ram(MESSAGE_ID) == message, frames)
    for _ in range(20):
        if not g.box_open():
            return
        g.until("the page to type out", g.typed, 900)
        g.act("a", 6, 14)
    raise AssertionError("the box did not close")


@scene("recharging")
def recharging(s):
    """Items that recharge: a Novice's Wing Boots in Eolis, flying (T: counts
    the flight down), then recharging (T: blinks the 60 s cooldown) with the
    pair still selected. As tests/recharge/recharging_items.py's part 1 does."""
    g = s.game
    shop = g.state("eolis-meat-shop")
    g.launch(at="eolis-meat-shop")
    pos = 0x99                                                  # walked out, so the HUD shows the pair
    g.warp(shop, "boots", AREA_EOLIS, 1, PALETTE_EOLIS, pos, 0,
           {SELECTED_ITEM: WING_BOOTS, NUMBER_OF_ITEMS: 0, TITLE: 0})
    g.clear_actors({POS_X: (pos & 0x0F) * 16, POS_Y: (pos >> 4) * 16})
    use_item(g)
    dismiss(g, USED_WING_BOOTS)
    assert g.tcp("recharge_state")["boots_running"] == 1, g.tcp("recharge_state")
    y = g.ram(POS_Y)
    g.tcp("action", name="a", frames=40)                        # up into the air
    g.tcp("action", name="up", frames=40)
    g.wait(36)
    assert g.ram(POS_Y) < y, "flying"
    box = (0, 8, 256, 224)
    s.shot("Flying", box)
    dismiss(g, WING_BOOTS_GONE, (NOVICE_FLIGHT + 2) * SECOND)
    g.until("the cooldown", lambda: g.tcp("recharge_state")["boots_cooldown"] > 0, 60)
    g.until("the hero on the floor", lambda: not g.ram(PLAYER_FLAGS) & FALLING, 600)
    g.until("T: showing", lambda: not g.tcp("recharge_state")["hud_drawn_blank"], 120)
    g.wait(2)
    s.picture(g.shot_lit(T_DIGITS, "recharging", 20), "Recharging", box)


# Picking up the Battle Suit (tests/inventory/inventory_fix.py).
ARMOR, NUMBER_OF_ARMORS, TEXTBOX_X, MENU_CURSOR = 0x3A1, 0x3C3, 0x208, 0x21E
SPRITE_PHASES, SPRITE_BEHAVIORS, SPRITE_HITBOX, SPRITE_HIT_BY_MAGIC = 0x2E4, 0x2D4, 0x304, 0x334
SPRITE_BEHAVIOR_L, SPRITE_BEHAVIOR_U = 0x354, 0x35C
HITBOX_TYPES, ENTITY_HP, BSCRIPTS = 0xB4DF, 0xB5A9, 0xAD2D      # bank 14, by entity
BATTLE_SUIT_ITEM, GOT_BATTLE_SUIT = 0x58, 0xB5                  # its sprite entity; "I've got the Battle Suit."
LEATHER_ARMOR, STUDDED_MAIL, FULL_PLATE, BATTLE_SUIT = 0, 1, 2, 3
MENU_WIDTH, LIST_WIDTH, MENU_ARMOR = 0x0A, 0x14, 1              # PlayerMenu_Show's box, ShowSubmenu's; its row


def bank14(rom, addr):
    return rom[16 + 14 * 0x4000 + addr - 0x8000]


def press_until(g, button, what, test, frames=60, tries=5):
    """Press `button` until `test()` holds, again if a loaded machine lost the
    press (inventory_fix.py's press_until)."""
    for _ in range(tries):
        g.tcp("action", name=button, frames=4)
        try:
            g.until(what, test, frames)
            return
        except AssertionError:
            pass
    raise AssertionError(f"{what}: {tries} presses of {button} did nothing")


@scene("battle-suit")
def battle_suit(s):
    """The Battle Suit picked up with the Leather Armor, the Studded Mail and
    the Full Plate listed (and the Hand Dagger worn, no weapon listed): the
    original counts the weapons, so the suit takes the first armor's place
    and the other three are gone; the remaster adds it after them. The
    item is put under the hero in Eolis's town, as the test does, and the
    picture is the Select menu's armor list."""
    g = s.game
    town = g.state("eolis-town")
    slot, rom = 7, g.image
    script = BSCRIPTS + 2 * BATTLE_SUIT_ITEM
    case = g.out / "battle-suit.cycstate"
    fixture(town, case, {
        **{SPRITE_ENTITIES + i: 0xFF for i in range(8)},
        SPRITE_ENTITIES + slot: BATTLE_SUIT_ITEM, SPRITE_X + slot: state_ram(town, POS_X),
        SPRITE_Y + slot: state_ram(town, POS_Y), SPRITE_PHASES + slot: 0, SPRITE_FLAGS + slot: 0,
        SPRITE_HIT_BY_MAGIC + slot: 0xFF, SPRITE_HITBOX + slot: bank14(rom, HITBOX_TYPES + BATTLE_SUIT_ITEM),
        SPRITE_HP + slot: bank14(rom, ENTITY_HP + BATTLE_SUIT_ITEM), SPRITE_BEHAVIOR_L + slot: bank14(rom, script),
        SPRITE_BEHAVIOR_U + slot: bank14(rom, script + 1), SPRITE_BEHAVIORS + slot: 0xFF,
        SELECTED_WEAPON: HAND_DAGGER, PLAYER_CUR_WEAPON: HAND_DAGGER, NUMBER_OF_WEAPONS: 0,
        SELECTED_ARMOR: NOTHING, NUMBER_OF_ARMORS: 3,
        ARMOR: LEATHER_ARMOR, ARMOR + 1: STUDDED_MAIL, ARMOR + 2: FULL_PLATE, ARMOR + 3: 0})
    for original, caption, expect in ((True, "Original", [BATTLE_SUIT]),
                                      (False, "Remastered", [LEATHER_ARMOR, STUDDED_MAIL, FULL_PLATE, BATTLE_SUIT])):
        g.set_features({REMASTER: False} if original else {})
        g.launch(at="eolis-town")
        g.load(case)
        g.until("the pickup's line", lambda: g.ram(MESSAGE_ID) == GOT_BATTLE_SUIT, 300)
        g.until("the line to type out", g.typed, 600)
        press_until(g, "a", "the box to close", lambda: g.ram(TEXTBOX_CLOSED) != 0)
        g.wait(4)
        armor = list(g.ram(ARMOR, 4)[:g.ram(NUMBER_OF_ARMORS)])
        assert armor == expect, f"{caption}: armor {armor}, expected {expect}"
        press_until(g, "select", "the menu", lambda: g.ram(TEXTBOX_CLOSED) == 0 and g.ram(TEXTBOX_X + 2) == MENU_WIDTH)
        g.wait(10)
        press_until(g, "down", "the cursor on the armor", lambda: g.ram(MENU_CURSOR) == MENU_ARMOR)
        press_until(g, "a", "the armor list", lambda: g.ram(TEXTBOX_X + 2) == LIST_WIDTH)
        g.wait(30)
        s.shot(caption, (0, 8, 256, 232))
        g.close()


@scene("graphics", MovementGame)
def graphics(s):
    """The original's graphics glitches, fixed: Studded Mail on Trunk 6's
    ladder (the chest's stale tile), the Fire spell's second frame (its
    bottom right), the Myconid's second frame (a foot), the Tower of
    Fortress's floor under its left pillar, and the title's F. The original's
    on the left, the remaster's on the right. Each but the title is played
    headless from tests/graphics/graphics_fixes.py's fixtures, frame exact:
    the second frames are pictured on a frame whose sprites and the one
    before's both show them."""
    g = s.game
    gf = graphics_fixes
    box_w, box_h = 112, 64
    pictures = {True: [], False: []}

    def around(x, y):
        """A box_w x box_h box in the native picture, around (x, y), on the picture."""
        x0, y0 = min(max(x - box_w // 2, 0), 256 - box_w), min(max(y - box_h // 2, 0), 240 - box_h)
        return x0, y0, x0 + box_w, y0 + box_h

    def both(name, state, schedule, frames, box):
        for original in (True, False):
            path = g.out / f"{name}-{'original' if original else 'remastered'}.png"
            g.replay(state, schedule, frames, original, picture=path)
            s.sources.append(path.name)
            pictures[original].append(crop(read_png(path)[1], box))

    def second_frame(rams, drawn, start):
        """The first frame from `start` on whose sprites, and the frame before's, `drawn` finds."""
        return next(f for f in range(start, len(rams)) if drawn(rams[f]) and drawn(rams[f - 1]))

    def oam(ram):
        """The sprites a frame drew (a hidden one keeps its old tile, at Y $EF and below the picture)."""
        return [s for s in (tuple(ram[gf.SHADOW_OAM + i:gf.SHADOW_OAM + i + 4]) for i in range(0, 256, 4))
                if s[0] < 0xEF]

    # Studded Mail climbing, the shield's tile at its chest.
    studded = g.out / "studded.cycstate"
    fixture(g.state("trunk-studded"), studded, {POS_X: 224, POS_Y: 136, PLAYER_FLAGS: gf.CAN_CLIMB | gf.CLIMBING,
                                                HP: 0x50, **{SPRITE_ENTITIES + i: 0xFF for i in range(8)}})
    rams = g.replay(studded, [(0, "UP"), (12, "-")], 40)
    both("studded", studded, [(0, "UP"), (12, "-")], 40, around(rams[-1][POS_X] + 8, 32 + rams[-1][POS_Y] + 16))
    # The Fire's second frame.
    fire = g.out / "fire.cycstate"
    fixture(g.state("trunk-ladder"), fire, {MAGIC_SPELLS: gf.FIRE, NUMBER_OF_MAGIC_SPELLS: 1,
                                            SELECTED_MAGIC: gf.FIRE, MP: 0x50, POS_X: 232,
                                            **{SPRITE_ENTITIES + i: 0xFF for i in range(8)}})
    rams = g.replay(fire, [(0, "UP+B"), (3, "-")], 70)
    # (in front of the background: CastMagic_CalculateVisibility puts it behind the trees)
    f = second_frame(rams, lambda r: sum(1 for _, t, a, _ in oam(r) if t in (0x7C, 0x7D) and not a & 0x23) == 4, 2)
    x = rams[f][0x2B6]                                             # CastMagic_XPos_Full
    both("fire", fire, [(0, "UP+B"), (3, "-")], f + 1, around(x + 8, 32 + rams[f][POS_Y] + 16))
    # The Myconid's second frame, out of a door onto Trunk $18.
    door = g.state("door-myconid")
    out = [(0, "RIGHT"), (40, "-")]
    rams = g.replay(door, out, 300)

    def myconid(ram):
        slot = next((i for i in range(8) if ram[SPRITE_ENTITIES + i] == gf.MYCONID), None)
        if slot is None or ram[AREA] != AREA_TRUNK:
            return False
        mx, my = ram[SPRITE_X + slot], ram[SPRITE_Y + slot]
        body = [t for y, t, _, x in oam(ram) if 0 <= x - mx < 16 and 0 <= y - 32 - my < 32 and y < 0xEF]
        return len(body) == 8 and max(body) - min(body) == 0x0D - 0x04
    f = second_frame(rams, myconid, 150)
    slot = next(i for i in range(8) if rams[f][SPRITE_ENTITIES + i] == gf.MYCONID)
    both("myconid", door, out, f + 1, around(rams[f][SPRITE_X + slot] + 8, 32 + rams[f][SPRITE_Y + slot] + 16))
    # The Tower of Fortress's floor, out of a door onto $2D.
    both("fortress", g.state("door-fortress"), out, 240, (0, 240 - box_h, box_w, 240))
    # The title's F (the window: a title takes a boot).
    for original in (True, False):
        g.set_features({REMASTER: False} if original else {})
        g.launch()
        g.until("the title", lambda: g.frame() > 700, 1500)
        path = g.shot("title-original" if original else "title-remastered")
        s.sources.append(path.name)
        pictures[original].append(crop(read_png(path)[1], (8, 16, 8 + box_w, 16 + box_h)))
        g.close()
    s.stack([(rows, "Original") for rows in pictures[True]])
    s.stack([(rows, "Remastered") for rows in pictures[False]])


def carrying(gold, weapons, inventory, weapon=HAND_DAGGER):
    """RAM writes for a hero with this gold, `weapon` in hand, and these weapons and items."""
    w = {GOLD: gold & 0xFF, GOLD + 1: gold >> 8 & 0xFF, GOLD + 2: gold >> 16, SELECTED_WEAPON: weapon,
         NUMBER_OF_WEAPONS: len(weapons), NUMBER_OF_ITEMS: len(inventory), SELECTED_ITEM: NOTHING, HP: 0x50}
    w.update({WEAPONS + i: v for i, v in enumerate(weapons)})
    w.update({ITEMS + i: v for i, v in enumerate(inventory)})
    return w


def shop_list(g, mode):
    """Until the `mode` list is on screen with its hint, and waits for a button."""
    g.until(f"the {mode} list", lambda: (lambda st: st["shown"] == mode and st["list_up"]
                                         and st["hint"] is not None)(g.tcp("shops_state")), 900)
    waits = g.tcp("shops_state")["waits"]
    g.until("the list to wait for a button", lambda: g.tcp("shops_state")["waits"] > waits, 300)
    g.wait(10)


@scene("quick-shops")
def quick_shops(s):
    """Quick shops: Forepaw's tool shop, its list up at once, the Wing Boots
    you carry OWNED; Select, and what you can sell (tests/shops/quick_shops.py)."""
    g = s.game
    shop = g.state("eolis-meat-shop")
    g.launch(at="eolis-meat-shop")
    target = g.out / "forepaw.cycstate"
    fixture(shop, target, {AREA_SAVED_AREA: TOWNS, AREA_SAVED_SCREEN: FOREPAW, SCREEN_SAVED_PALETTE: PALETTE_TOWN,
                           PLAYER_SAVED_POS: FOREPAW_TOOLS_DOOR, AREA_REGION: AREA_TRUNK,
                           **carrying(9000, [LONG_SWORD], [KEY_J, RED_POTION, WING_BOOTS])})
    g.load(target)
    g.walk_out()
    assert g.where() == (TOWNS, FOREPAW), g.where()
    g.wait(30)
    g.clear_actors()                                            # no townsfolk at the door to talk to
    g.wait(4)
    g.act("up", 4, 10)
    g.until("Forepaw's tool shop", lambda: g.where() == (BUILDINGS, FOREPAW_TOOL_SHOP))
    g.wait(40)
    to_counter(g)
    g.act("up", 4, 6)
    shop_list(g, "buy")
    state = g.tcp("shops_state")
    assert state["owned"][-1] and not any(state["owned"][:-1]), state
    box = (8, 32, 248, 224)
    s.shot("Buying", box)
    g.act("select", 4, 6)
    shop_list(g, "sell")
    s.shot("Selling", box)


@fax_e2e.recipe("readme-at-guru", parent="readme-conflate-temple")
def _at_guru(g):
    """Walked left to Conflate's Guru, standing before him."""
    g.act("left", 170, 10)


@scene("text-speed")
def text_speed(s):
    """Hold A to speed up text: the Guru's greeting, 40 frames after its first
    character, without A and with A held, frame exact from one state
    (tests/text_speed/text_speed.py)."""
    g = s.game
    # Headless, frame exact. While a line types, the portrait's eyes and
    # mouth move with InterruptCounter (IScripts_UpdatePortraitAnimation),
    # so that is set too.
    at_guru = g.out / "at-guru.cycstate"
    fixture(g.state("readme-at-guru"), at_guru, {INTERRUPT_COUNTER: 0})
    talk = [(0, "UP"), (4, "-")]
    rams = g.replay(at_guru, talk, 90)
    greeting = [k for k, ram in enumerate(rams) if ram[MESSAGE_ID] == MEDITATE_WITH_YOU]
    assert greeting, "no greeting in 90 frames of Up"
    first = next(k for k in greeting if rams[k][MESSAGE_PROCESSED] != rams[greeting[0]][MESSAGE_PROCESSED])
    typing = g.out / "typing.cycstate"                          # after the frame its first character types
    g.replay(at_guru, talk, first + 1, save=typing)
    for caption, button in (("A not held", "-"), ("A held", "A")):
        picture = g.out / f"{caption.lower().replace(' ', '-')}.png"
        g.replay(typing, [(0, button)], 40, picture=picture)
        s.picture(picture, caption, DIALOGUE)


@scene("zone-names")
def zone_names(s):
    """Zone names: the banner, walking into the Tower of Fortress
    (tests/zones/zone_names.py)."""
    g = s.game
    shop = g.state("eolis-meat-shop")
    g.launch(at="eolis-meat-shop")
    g.warp(shop, "fortress", AREA_TRUNK, 0x29, PALETTE_TOWER, 0x93, AREA_TRUNK)   # where the door on $28 arrives
    g.clear_actors()                                            # its enemies, for a picture that keeps still
    g.until("the banner", lambda: g.tcp("zone_state")["last"] == "Tower of Fortress", 600)
    g.until("the banner to fade in", lambda: g.tcp("zone_state")["alpha"] == 255, 120)
    g.wait(10)
    assert g.tcp("zone_state")["alpha"] == 255, g.tcp("zone_state")
    s.shot(None, (0, 8, 256, 232))


SLAIN_AFTER = 8                                                 # frames after the killing hit for the picture


@scene("slain-by")
def slain_by(s):
    """An Execution Hood's shot takes the hero's last HP in the Fortress of
    Zenis: "Slain by an Execution Hood", named after the Hood that cast it.
    The moment is played headless from a save state, so it is the same frame
    every run: the hit, then a few frames, while the Hood is still there."""
    g = s.game
    cast = g.state("readme-hood-shot")
    shot = [state_ram(cast, SPRITE_ENTITIES + i) for i in range(8)].index(HOOD_SHOT)
    dying = g.out / "dying.cycstate"                            # his last HP, the shot at his head
    fixture(cast, dying, {HP: 1, HP_FRACTION: 0, SPRITE_X + shot: state_ram(cast, POS_X) + 12,
                          SPRITE_Y + shot: state_ram(cast, POS_Y) - 8})
    rams = g.replay(dying, [], 120)
    hit = next((i for i, ram in enumerate(rams) if ram[PLAYER_IS_DEAD]), None)
    assert hit is not None, "the shot did not kill him"
    picture = g.out / "slain-by.png"
    g.replay(dying, [], hit + 1 + SLAIN_AFTER, picture=picture)
    s.picture(picture, box=(0, 32, 256, 232))


NO_PORTRAIT = 0xFF


@scene("speaker-names")
def speaker_names(s):
    """Speaker names: the King of Eolis, named on a tab above his words."""
    g = s.game
    shop = g.state("eolis-meat-shop")
    g.launch(at="eolis-meat-shop")
    room = g.out / "kings-room.cycstate"                        # the door on Eolis's screen 7, block $98, with the Ring
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
    for _ in range(60):                                         # up his stairs, to x 48
        if g.ram(POS_X) <= 48 or g.tcp("speaker_state")["portrait"] != NO_PORTRAIT:
            break
        g.tcp("action", name="left", frames=24)
        g.tcp("action", name="a", frames=12)
        g.wait(30)
    g.act("up", 4, 10)
    g.until("the King", lambda: g.tcp("speaker_state")["portrait"] != NO_PORTRAIT, 600)
    g.until("his first line to start typing", lambda: not g.typed(), 600)
    g.until("the tab", lambda: g.tcp("speaker_state")["shown"], 600)
    g.until("the line to type out", g.typed)
    g.wait(10)
    state = g.tcp("speaker_state")
    assert state["name"] == "King of Eolis" and state["shown"], state
    s.picture(g.shot_lit(END_MARK, "king"), None, DIALOGUE)


@scene("seamless")
def seamless(s):
    """Seamless: in Eolis's town, near the right edge of its first screen,
    the stock view and the Seamless one, which joins the next screen on, with
    its actors. Those keep moving (Seamless runs them in their own room), so
    this picture differs a little from run to run: where they are, or
    whether one is in view."""
    g = s.game
    for caption, options in (("Stock", []), ("Seamless", ["--world-view=seamless"])):
        g.game_args = options                                   # Seamless has fixtures of its own
        g.launch(at="eolis-town")
        g.walk("right", lambda: g.ram(POS_X) >= 196, "x 196")
        g.clear_actors()                                        # this screen's walker, so the stock picture is the same
        g.wait(30)
        path = g.shot(caption.lower())
        width, _ = read_png(path)
        s.picture(path, caption, (0, 8, width, 232))
        g.close()


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
