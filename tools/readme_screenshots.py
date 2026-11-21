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
from a new game to the feature's moment with the end-to-end tests' helper
(tests/e2e/fax_e2e.py, and tests/block/harness.py's Game for getting around),
building any save-state fixtures it needs from your ROM as the tests do, and
takes its pictures with shot(). Every scene runs at once, each in its own
process with its own game on a port the helper reserves; its log and raw
pictures go to OUT/<scene>/ (build-tests/readme-shots by default).

A scene's pictures are cropped to what matters, scaled 2x (nearest
neighbour) and written as an indexed PNG: an NES picture has well under 256
colors, so nothing is lost and the files stay small. Two or more pictures
(the original and the remaster, or a before and after) go side by side, each
with its caption above it in the game's own font.

Like the test suite's end-to-end runs, it takes the machine-wide end-to-end
lock (tools/test.sh), so the two never slow each other's games down.

The pictures come from play over TCP, so a scene's timing can vary by a frame
or two from run to run; regenerate only the scenes you changed (--only).
Never commit a save state: states hold the cartridge's graphics. Commit only
the PNGs.

A new feature adds its scene here and its picture to the README (CLAUDE.md).
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import struct
import subprocess
import sys
import time
import traceback
import zlib

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tests" / "e2e"))
sys.path.insert(0, str(REPO / "tests" / "block"))
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
AREA, SCREEN, POS_X, POS_Y = 0x24, 0x63, 0x9E, 0xA1
SPRITE_ENTITIES = 0x2CC                     # CurrentSprites_Entities, 8 slots ($FF: none)
TEXTBOX_CLOSED, MESSAGE_ID, TEXTBOX_PAUSED = 0x20C, 0x213, 0x215
EXPERIENCE, GOLD = 0x390, 0x392             # 2 and 3 bytes, low first
SPECIAL_ITEMS, TITLE, NEXT_TITLE = 0x42C, 0x437, 0x4ED
HP, AREA_REGION, PLAYER_IS_DEAD, TEMPLE_SPAWN_POINT = 0x431, 0x435, 0x438, 0x439
SELECTED_WEAPON, SELECTED_ARMOR, SELECTED_SHIELD, SELECTED_MAGIC, SELECTED_ITEM = 0x3BD, 0x3BE, 0x3BF, 0x3C0, 0x3C1
PLAYER_CUR_WEAPON = 0x3C8
AREA_SAVED_AREA, AREA_SAVED_SCREEN, SCREEN_SAVED_PALETTE, PLAYER_SAVED_POS = 0x3D5, 0x3D6, 0x3D7, 0x3D8
BUILDINGS, TOWNS = 4, 3
NEGATIVE_THOUGHTS = 0x24                    # the Guru's words after a death
EOLIS, APOLUNE, CONFLATE = 0, 1, 5          # temples (TEMPLE_SPAWN_POINT)
GURU_MENU, GURU_QUESTION, GURU_SAVED = 1, 2, 3   # files_state["guru"]: fax_files.c's GuruPhase


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
    """tests/block/harness.py's Game (fax_e2e.Game with ways to get around),
    with what the scenes share."""

    def settle(self, frames=600):
        """Until the screen has loaded and stopped scrolling."""
        self.until("the screen to settle", self.settled, frames)

    def page(self, limit=60):
        """Press A through text until the box closes."""
        for _ in range(limit):
            if not self.box_open():
                return
            if self.typed():
                self.act("a", 6, 14)
            else:
                self.wait(4)
        raise AssertionError("the text box did not close")

    def die_into(self, temple, writes=None):
        """From wherever the hero is, die and wake at `temple`'s Guru
        (TEMPLE_SPAWN_POINT), paging through his words: a way to travel."""
        self.poke({TEMPLE_SPAWN_POINT: temple, PLAYER_IS_DEAD: 1, HP: 0x50, **(writes or {})})
        self.until("the Guru's words", lambda: self.ram(MESSAGE_ID) == NEGATIVE_THOUGHTS, 900)
        self.page()
        self.until("the temple", lambda: self.ram(AREA) == BUILDINGS and not self.ram(PLAYER_IS_DEAD), 600)
        self.wait(30)


class Scene:
    """What a scene function gets: the game, and shot() to take its pictures."""

    def __init__(self, name, game):
        self.name, self.game, self.panels = name, game, []

    def shot(self, caption=None, box=(0, 0, 256, 240), name=None):
        """Screenshot the game's picture (what the window shows, with the
        remaster's drawing on it), cropped to `box` (x0, y0, x1, y1)."""
        return self.picture(self.game.shot(name or (caption or self.name).lower().replace(" ", "-")), caption, box)

    def picture(self, path, caption=None, box=None):
        """A picture from a file (a headless run's screenshot, say)."""
        width, rows = read_png(path)
        self.panels.append((crop(rows, box or (0, 0, width, len(rows))), caption))
        return path

    def write(self, target):
        rows = scale(compose(self.game.image, self.panels), SCALE)
        write_indexed_png(target, rows)
        return len(rows[0]), len(rows)


SCENES = {}


def scene(name):
    """Register a scene: docs/screenshots/<name>.png."""
    def register(fn):
        assert name not in SCENES, name
        SCENES[name] = fn
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


# DEV ONLY (merged into this file before the PR): scenes being written in
# tools/readme_scenes_*.py, run in this module's namespace.
for _extra in sorted(Path(__file__).parent.glob("readme_scenes_*.py")):
    try:
        exec(compile(_extra.read_text(), str(_extra), "exec"))
    except Exception:
        print(f"skipping {_extra.name}:", file=sys.stderr)
        traceback.print_exc()


# ---- running them ----

def run_scene(name, args):
    """Play one scene in this process and write its PNG."""
    game = Game(args.build, args.rom, args.out)
    s = Scene(name, game)
    try:
        SCENES[name](s)
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()
    target = args.docs / f"{name}.png"
    target.parent.mkdir(parents=True, exist_ok=True)
    w, h = s.write(target)
    print(f"{name}: {target.relative_to(REPO) if target.is_relative_to(REPO) else target} {w}x{h}, "
          f"{target.stat().st_size} bytes, from {', '.join(game.shots)}")


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
                    help="each scene's log, raw pictures and game (default build-tests/readme-shots)")
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
                print(f"ok   {lines[-1] if lines else name} ({seconds:.0f} s)", flush=True)
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
