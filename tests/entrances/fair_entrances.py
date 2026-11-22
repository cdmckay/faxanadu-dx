"""Play Fair entrances end to end: each enemy it moves, at the way in that
made its placement unfair, in the original game and in the remaster.

    python3 tests/entrances/fair_entrances.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/entrances
    python3 tests/entrances/fair_entrances.py ... --part edges --game-arg=--world-view=seamless
    python3 tests/entrances/fair_entrances.py ... --only 'Mist $08'     # the spots whose name has this

Its parts: doors (doors, buildings and temples), edges (screen edges, ladders
and drops) and seamless-room (Seamless only). Every way in starts from the
fixture entrances-mascon-temple: (an external prerequisite, not coverage) the
hero, killed at Eolis's gate with Mascon as his furthest temple and a Long
Sword, Leather Armor and a Small Shield, wakes at Mascon's Guru. That saved
game, edited into a fixture, plays headless, frame by frame, with the same
--game-arg options (so the same world view):

- Mascon's temple: walking out of it, as after every death there. It opens on
  the Mist's screen 6.
- A building's way out: the temple's door edited to lead where the building's
  does (Area_SavedArea, Area_SavedScreen, Screen_SavedPalette and
  Player_SavedPosXY, which Game_EnterBuilding sets from where the hero stood
  at the building's door), then walking out.
- A door within an area: the temple's door edited to lead to the door's block
  on its own screen, walking out, and Up pressed on his first update there;
  for Branch's K-key door with the key selected and its message paged with A.
- A screen's edge, a ladder or a drop from the screen above: the temple's
  door edited to lead onto the screen beside it, walked out of, and
  (prerequisites, not coverage) that screen's enemies cleared, his
  invincibility spent, HP full and him put at the edge; then holding the way
  in (nothing, for a drop) across it.

Nothing else is pressed from the arrival on. Each is played with the remaster
off (the original game, the whole mod switched off), then on:

1. Off, the enemy is at its stock block on the frame before his first update,
   and hits him (Player_InvincibilityPhase $3C, HP down) within the spot's
   frames of that update, where no move he starts once he could react (13
   frames in) escapes it: docs/GAME_INTERNALS.md has each measurement.
2. On, the enemy starts at its new block, and nothing hits him for SAFE
   frames.
3. With Seamless on, an edge is no blind way in: the room beyond is on screen,
   its actors running, before he crosses. Instead (edges), walking to the
   edge in the window has the enemy start at its new block in the room
   Seamless loads (the move's use count). And (seamless-room) walking from the
   Mist's screen 5 toward screen 6 has Seamless load screen 6 on an isolated
   machine, where hooks do not run: its Sir Gawaine starts at the new block
   there too, read from Seamless's own room record.
"""
from concurrent.futures import ThreadPoolExecutor
import itertools
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import threading

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
import fax_e2e  # noqa: E402
from fax_e2e import Game, chosen, chunks, features_toml, fixture, hold, parser, ram, until, wait  # noqa: E402

# RAM
AREA, SCREEN, SCREEN_READY, SCROLL_DIRECTION = 0x24, 0x63, 0x0E, 0x54
X, X_FRACTION, Y, FLAGS, STATUS, IFRAMES = 0x9E, 0x9D, 0xA1, 0xA4, 0xA5, 0xAD
SPRITE_X, SPRITE_Y, SPRITE_ENTITY, SHADOW_OAM = 0xBA, 0xC2, 0x2CC, 0x700
MESSAGE_ID, SELECTED_WEAPON, SELECTED_ARMOR, SELECTED_SHIELD = 0x213, 0x3BD, 0x3BE, 0x3BF
SELECTED_ITEM, PLAYER_CUR_WEAPON = 0x3C1, 0x3C8
SAVED_AREA, SAVED_SCREEN, SAVED_PALETTE, SAVED_POS = 0x3D5, 0x3D6, 0x3D7, 0x3D8
HP, AREA_REGION, PLAYER_IS_DEAD, TEMPLE_SPAWN_POINT = 0x431, 0x435, 0x438, 0x439
LONG_SWORD, LEATHER_ARMOR, SMALL_SHIELD, KEY_K = 1, 0, 0, 0x05     # ITEMINVENTORY_KEY_K
BUILDINGS, NEGATIVE_THOUGHTS, MASCON = 4, 0x24, 3
PACKAGE, FEATURE = "faxanadu.remastered", "remastered"              # the one switch: off is the original game
HOLD = {"right": "RIGHT", "left": "LEFT", "up": "UP", "down": "DOWN", "drop": None}
SAFE = 30                   # frames after his first update with the fix on, and nothing hits him
# InterruptCounter and SpriteUpdateCounter, which enemy scripts time
# themselves by, as the temple's state had them when the ways in were
# measured: from another phase, a way in's first hit can come at another
# frame (docs/GAME_INTERNALS.md, "Enemy placement and fair entrances").
INTERRUPT_COUNTER, SPRITE_UPDATE_COUNTER = 0x1A, 0x383
PHASE = {INTERRUPT_COUNTER: 0xF3, SPRITE_UPDATE_COUNTER: 0x3E}
TRUNK, MIST, BRANCH, DARTMOOR, ZENIS = 1, 2, 5, 6, 7
REGION = {TRUNK: 1, MIST: 2, BRANCH: 3, DARTMOOR: 4, ZENIS: 5}
RAIDEN, ZOMBIE, HORNET, BIHORUDA, YUINARU, SNOWMAN = 0x04, 0x06, 0x07, 0x08, 0x0B, 0x0C
ROKUSUTAHN, CHARRON, GERIBUTA, SUGATA, GIANT_STRIDER, GAWAINE = 0x11, 0x15, 0x17, 0x18, 0x1E, 0x1F
MASKMAN, WOLFMAN, YAREEKA, IKEDA, LAMPREY, RIPASHEIKU, ZOZURA = 0x20, 0x21, 0x22, 0x26, 0x28, 0x2D, 0x47
SIR_GAWAINE = GAWAINE

# Each spot: what it is, the screen (area, screen, palette), the way in, the
# enemy (entity, stock block, new block; YX as bank 11 stores them), and the
# latest frame its stock placement first hits him. A way in is ("temple",),
# ("building", start YX), ("door", source screen, its palette, the door's
# block, the key to select or None), or ("edge", kind, source screen, its
# palette, column, row), kind being left, right, up, down or drop.
SPOTS = [
    ("Mascon's temple", (MIST, 0x06, 0x0A), ("temple",), (GAWAINE, 0x89, 0x9E), 16),
    ("Mist $06, building $22's way out", (MIST, 0x06, 0x0A), ("building", 0x87), (GAWAINE, 0x89, 0x9E), 16),
    ("the Tower of Mist's way out", (MIST, 0x17, 0x0A), ("door", 0x45, 0x0B, 0xA0, None), (RAIDEN, 0xA5, 0xA9), 16),
    ("Mist $1F, building $26's way out", (MIST, 0x1F, 0x0A), ("building", 0x94), (SUGATA, 0x95, 0x97), 16),
    ("the tower on Mist $20's way out", (MIST, 0x20, 0x0A), ("door", 0x50, 0x0B, 0xA2, None), (GAWAINE, 0x96, 0x46),
     16),
    ("Mist $29, building $20's way out", (MIST, 0x29, 0x0A), ("building", 0x7C), (IKEDA, 0x7D, 0x7E), 16),
    ("Trunk $06, building $20's way out", (TRUNK, 0x06, 0x06), ("building", 0x5A), (ZOMBIE, 0x5B, 0x5C), 16),
    ("Branch $0E, the door from $0F", (BRANCH, 0x0E, 0x08), ("door", 0x0F, 0x09, 0x60, None), (LAMPREY, 0x6D, 0x6A),
     16),
    ("Branch $13, the door from $14", (BRANCH, 0x13, 0x09), ("door", 0x14, 0x08, 0x80, KEY_K), (MASKMAN, 0x8C, 0x9D),
     16),
    ("Dartmoor $02, building $22's way out", (DARTMOOR, 0x02, 0x0C), ("building", 0x4A),
     (GIANT_STRIDER, 0x49, 0xA9), 16),
    ("Dartmoor $03, building $24's way out", (DARTMOOR, 0x03, 0x0C), ("building", 0xAB), (LAMPREY, 0xAA, 0xA8), 16),
    ("Dartmoor $08, building $25's way out", (DARTMOOR, 0x08, 0x0C), ("building", 0x41), (GAWAINE, 0x42, 0x46), 16),
    ("the Castle of Fraternal's way out", (DARTMOOR, 0x0C, 0x0C), ("door", 0x10, 0x0D, 0x90, None),
     (WOLFMAN, 0xAB, 0xAD), 16),
    # Screen edges, ladders and drops: each move at its earliest stock hit that the
    # moves leave clear.
    ("Trunk $0D, walking left in from $0E: Zozura $5E", (TRUNK, 0x0D, 0x06), ("edge", "left", 0x0E, 0x06, 0, 4), (ZOZURA, 0x5E, 0x7E), 10),
    ("Trunk $17, walking right in from $16: Yuinaru $5F", (TRUNK, 0x17, 0x06), ("edge", "right", 0x16, 0x06, 15, 4), (YUINARU, 0x5F, 0x5E), 10),
    ("Trunk $1D, dropping in from $26 at column 10: Yuinaru $CC", (TRUNK, 0x1D, 0x06), ("edge", "drop", 0x26, 0x06, 10, 9), (YUINARU, 0xCC, 0xBA), 29),
    ("Trunk $1E, dropping in from $1D at column 10: Yuinaru $3B", (TRUNK, 0x1E, 0x06), ("edge", "drop", 0x1D, 0x06, 10, 9), (YUINARU, 0x3B, 0x2C), 12),
    ("Trunk $1E, dropping in from $1D at column 7: Charron $99", (TRUNK, 0x1E, 0x06), ("edge", "drop", 0x1D, 0x06, 7, 9), (CHARRON, 0x99, 0x9B), 24),
    ("Trunk $20, dropping in from $23 at column 3: Ikeda $95", (TRUNK, 0x20, 0x06), ("edge", "drop", 0x23, 0x06, 3, 9), (IKEDA, 0x95, 0x7F), 20),
    ("Trunk $20, dropping in from $23 at column 7: Ikeda $99", (TRUNK, 0x20, 0x06), ("edge", "drop", 0x23, 0x06, 7, 9), (IKEDA, 0x99, 0x8F), 20),
    ("Trunk $21, dropping in from $22 at column 7: Charron $88", (TRUNK, 0x21, 0x06), ("edge", "drop", 0x22, 0x06, 7, 9), (CHARRON, 0x88, 0x9B), 22),
    ("Trunk $23, dropping in from $24 at column 4: Yuinaru $35", (TRUNK, 0x23, 0x06), ("edge", "drop", 0x24, 0x06, 4, 9), (YUINARU, 0x35, 0x33), 12),
    ("Trunk $24, dropping in from $25 at column 5: Charron $35", (TRUNK, 0x24, 0x06), ("edge", "drop", 0x25, 0x06, 5, 9), (CHARRON, 0x35, 0x45), 12),
    ("Trunk $30, dropping in from $36 at column 4: Charron $45", (TRUNK, 0x30, 0x06), ("edge", "drop", 0x36, 0x06, 4, 9), (CHARRON, 0x45, 0x46), 14),
    ("Trunk $32, dropping in from $38 at column 3: Snowman $23", (TRUNK, 0x32, 0x06), ("edge", "drop", 0x38, 0x06, 3, 9), (SNOWMAN, 0x23, 0x33), 10),
    ("Trunk $32, dropping in from $38 at column 8: Charron $99", (TRUNK, 0x32, 0x06), ("edge", "drop", 0x38, 0x06, 8, 9), (CHARRON, 0x99, 0xA8), 24),
    ("Trunk $37, dropping in from $3C at column 5: Charron $95", (TRUNK, 0x37, 0x06), ("edge", "drop", 0x3C, 0x06, 5, 9), (CHARRON, 0x95, 0x94), 24),
    ("Mist $04, climbing down from $2A: Ikeda $22", (MIST, 0x04, 0x0A), ("edge", "down", 0x2A, 0x0A, 2, 9), (IKEDA, 0x22, 0x34), 12),
    ("Mist $07, dropping in from $01 at column 9: Gawaine $38", (MIST, 0x07, 0x0A), ("edge", "drop", 0x01, 0x0A, 9, 9), (SIR_GAWAINE, 0x38, 0x48), 13),
    ("Mist $08, dropping in from $02 at column 8: Hornet $28", (MIST, 0x08, 0x0A), ("edge", "drop", 0x02, 0x0A, 8, 9), (HORNET, 0x28, 0x46), 10),
    ("Mist $08, dropping in from $02 at column 10: Hornet $4B", (MIST, 0x08, 0x0A), ("edge", "drop", 0x02, 0x0A, 10, 9), (HORNET, 0x4B, 0x3D), 13),
    ("Mist $08, dropping in from $02 at column 8: Gawaine $98", (MIST, 0x08, 0x0A), ("edge", "drop", 0x02, 0x0A, 8, 9), (SIR_GAWAINE, 0x98, 0xA5), 10),
    ("Mist $09, dropping in from $03 at column 3: Hornet $38", (MIST, 0x09, 0x0A), ("edge", "drop", 0x03, 0x0A, 3, 9), (HORNET, 0x38, 0x28), 39),
    ("Mist $0F, dropping in from $05 at column 11: Bihoruda $4C", (MIST, 0x0F, 0x0A), ("edge", "drop", 0x05, 0x0A, 11, 9), (BIHORUDA, 0x4C, 0x4A), 13),
    ("Mist $12, dropping in from $0C at column 6: Hornet $68", (MIST, 0x12, 0x0A), ("edge", "drop", 0x0C, 0x0A, 6, 9), (HORNET, 0x68, 0x66), 17),
    ("Mist $1A, dropping in from $19 at column 4: Raiden $25", (MIST, 0x1A, 0x0A), ("edge", "drop", 0x19, 0x0A, 4, 9), (RAIDEN, 0x25, 0x35), 10),
    ("Mist $25, dropping in from $1D at column 8: Yareeka $29", (MIST, 0x25, 0x0A), ("edge", "drop", 0x1D, 0x0A, 8, 9), (YAREEKA, 0x29, 0x2A), 18),
    ("Mist $27, dropping in from $20 at column 10: Lamprey $3A", (MIST, 0x27, 0x0A), ("edge", "drop", 0x20, 0x0A, 10, 9), (LAMPREY, 0x3A, 0x39), 12),
    ("Mist $48, dropping in from $3F at column 10: Ripasheiku $3A", (MIST, 0x48, 0x0A), ("edge", "drop", 0x3F, 0x0A, 10, 9), (RIPASHEIKU, 0x3A, 0x37), 12),
    ("Mist $49, dropping in from $40 at column 10: Rokusutahn $9C", (MIST, 0x49, 0x0A), ("edge", "drop", 0x40, 0x0A, 10, 9), (ROKUSUTAHN, 0x9C, 0xAC), 24),
    ("Mist $49, dropping in from $40 at column 10: Raiden $A8", (MIST, 0x49, 0x0A), ("edge", "drop", 0x40, 0x0A, 10, 9), (RAIDEN, 0xA8, 0xB7), 24),
    ("Branch $04, dropping in from $06 at column 12: Maskman $5A", (BRANCH, 0x04, 0x08), ("edge", "drop", 0x06, 0x08, 12, 9), (MASKMAN, 0x5A, 0x6A), 35),
    ("Branch $19, dropping in from $14 at column 11: Giant Strider $29", (BRANCH, 0x19, 0x08), ("edge", "drop", 0x14, 0x08, 11, 9), (GIANT_STRIDER, 0x29, 0x39), 36),
    ("Branch $19, dropping in from $14 at column 4: Giant Strider $56", (BRANCH, 0x19, 0x08), ("edge", "drop", 0x14, 0x08, 4, 9), (GIANT_STRIDER, 0x56, 0x54), 21),
    ("Dartmoor $03, dropping in from $01 at column 8: Yuinaru $58", (DARTMOOR, 0x03, 0x0C), ("edge", "drop", 0x01, 0x0C, 8, 9), (YUINARU, 0x58, 0x56), 16),
    ("Zenis $03, dropping in from $10 at column 2: Geributa $A5", (ZENIS, 0x03, 0x0F), ("edge", "drop", 0x10, 0x0F, 2, 9), (GERIBUTA, 0xA5, 0xB5), 26),
    ("Zenis $03, dropping in from $10 at column 12: Geributa $AA", (ZENIS, 0x03, 0x0F), ("edge", "drop", 0x10, 0x0F, 12, 9), (GERIBUTA, 0xAA, 0xBA), 28),
    ("Zenis $0E, dropping in from $09 at column 7: Gawaine $29", (ZENIS, 0x0E, 0x0F), ("edge", "drop", 0x09, 0x0F, 7, 9), (SIR_GAWAINE, 0x29, 0x38), 11),
    ("Zenis $10, dropping in from $0B at column 6: Zozura $A6", (ZENIS, 0x10, 0x0F), ("edge", "drop", 0x0B, 0x0F, 6, 9), (ZOZURA, 0xA6, 0xA4), 26),
    ("Zenis $10, dropping in from $0B at column 6: Zozura $A8", (ZENIS, 0x10, 0x0F), ("edge", "drop", 0x0B, 0x0F, 6, 9), (ZOZURA, 0xA8, 0xA5), 26),
    ("Zenis $10, dropping in from $0B at column 8: Zozura $AA", (ZENIS, 0x10, 0x0F), ("edge", "drop", 0x0B, 0x0F, 8, 9), (ZOZURA, 0xAA, 0xAE), 26),
    ("Zenis $12, dropping in from $0D at column 7: Rokusutahn $48", (ZENIS, 0x12, 0x0F), ("edge", "drop", 0x0D, 0x0F, 7, 9), (ROKUSUTAHN, 0x48, 0x58), 17),
]


def visible(ram):
    """Sprites the update drew this frame, the status bar's sprite 0 included."""
    return sum(1 for k in range(0, 256, 4) if ram[SHADOW_OAM + k] < 0xEF)


def scrolled_in(rams, area, screen):
    """The first update on (area, screen) after a scroll: the frame after the
    one the scroll ends on there."""
    return next((i + 1 for i, r in enumerate(rams)
                 if (r[AREA], r[SCREEN]) == (area, screen) and r[SCROLL_DIRECTION] == 0xFF
                 and r[SCREEN_READY] == 0), None)


def first_hit(rams, first):
    """Frames from `first` to the first hit (invincibility rising to $3C), or None."""
    return next((i - first for i in range(first, len(rams))
                 if rams[i][IFRAMES] == 0x3C and rams[i - 1][IFRAMES] != 0x3C), None)


def slug(name):
    return re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")


class Runs:
    """Headless runs of the copied build from save states, with a button
    schedule and a per-frame RAM log (tests/movement/play.py's headless, with
    the world view's --game-arg options), safe to run several at once."""

    def __init__(self, game, game_args):
        self.game, self.exe, self.rom, self.out = game, game.exe, game.rom, game.out
        self.args = [part for arg in game_args for part in arg.split("=", 1)]
        self.ids = itertools.count()
        self.lock = threading.Lock()

    def mods(self, on):
        """A mod catalog: the remaster on, or off for the original game. One
        per thread: a run publishes its mod state into it."""
        root = self.out / f"mods-{'on' if on else 'off'}-{threading.get_ident()}"
        if not root.exists():
            shutil.copytree(self.out / "game" / "mods" / "packages", root / "packages")
            (root / "state.toml").write_text(features_toml({} if on else {(PACKAGE, FEATURE): False}))
        return root

    def run(self, state, schedule, frames, on, save=None):
        """Run from `state` for `frames` frames, holding buttons on a schedule
        of (frame, buttons) pairs counted from the state's frame ("RIGHT+A",
        "-" for none). Returns each frame's CPU RAM, after the frame. With
        `save`, (frame, path): also save a state after that frame."""
        with self.lock:
            work = self.out / "runs" / str(next(self.ids))
        work.mkdir(parents=True)
        start = struct.unpack_from("<I", Path(state).read_bytes(), 28)[0]      # the frame it continues at
        (work / "input.txt").write_text("".join(f"{start + at} {buttons}\n" for at, buttons in sorted(schedule)))
        log = work / "frames.bin"
        cmd = [str(self.exe), str(self.rom), "--load-state", str(state), "--input", str(work / "input.txt"),
               "--frames", str(start + frames), "--frame-log", str(log),
               "--frame-log-frames", f"{start}:{start + frames - 1}", "--mods-root", str(self.mods(on)),
               "--no-save", *self.args]
        if save:
            cmd += ["--save-state", f"{start + save[0]}:{save[1]}"]
        done = subprocess.run(cmd, capture_output=True, text=True, cwd=work)
        assert done.returncode == 0, done.stdout + done.stderr
        data = log.read_bytes()
        shutil.rmtree(work)
        assert data[:8] == b"CYCFRAME"
        rams, pos = [], 12
        while pos < len(data):
            head = struct.unpack_from("<9I", data, pos)
            pos += 36 + 32 + 256
            rams.append(data[pos:pos + head[3]])
            pos += head[3] + sum(head[4:9])
        assert len(rams) == frames, (len(rams), frames)
        return rams


class Spots:
    """Each spot's way in, from the temple's saved game, and its two checks."""

    def __init__(self, runs, temple, exit_frame):
        self.runs, self.out, self.temple, self.exit_frame = runs, runs.out, temple, exit_frame
        self.walk = [(0, "RIGHT"), (exit_frame, "-")]

    def leading_to(self, tag, area, screen, palette, pos, more=None):
        """The temple's saved game with its door edited to lead to `pos` on
        (area, screen), and the frame counters set to where the measurements
        had them (PHASE)."""
        state = self.out / f"{tag}.cycstate"
        fixture(self.temple, state, {SAVED_AREA: area, SAVED_SCREEN: screen, SAVED_PALETTE: palette,
                                     SAVED_POS: pos, AREA_REGION: REGION[area], HP: 0x50, **PHASE,
                                     **(more or {})})
        return state

    def arrive(self, state, area, screen, schedule, frames, on):
        """Play `schedule`; the frames, and the first update on (area, screen)
        after a building's or door's screen load (the frame its sprites are
        first drawn)."""
        rams = self.runs.run(state, schedule, frames, on)
        came = next((i for i, r in enumerate(rams) if (r[AREA], r[SCREEN]) == (area, screen)), None)
        assert came is not None, f"never reached {area}:{screen:#04x}: at {rams[-1][AREA]}:{rams[-1][SCREEN]:#04x}"
        blank = next((i for i in range(max(came - 1, 0), len(rams)) if visible(rams[i]) <= 1), None)
        first = blank and next((i for i in range(blank, len(rams)) if visible(rams[i]) > 1), None)
        assert first, f"no first update on {area}:{screen:#04x} within {frames} frames"
        return rams, first

    def door(self, state, src, door, key, on):
        """The walk out of the temple to `door` on `src`, Up on the first
        update there (and the key's message paged after it), and the frame the
        door's screen starts loading: about 28 frames after Up, 140 with the
        key's message."""
        _, first = self.arrive(state, src[0], src[1], self.walk, self.exit_frame + 60, on)
        sched = self.walk + [(first, "UP"), (first + 4, "-")]
        if key is not None:
            for k in range(10):                          # "I've used key.": A, 6 frames, every 16
                sched += [(first + 10 + 16 * k, "A"), (first + 16 + 16 * k, "-")]
        rams = self.runs.run(state, sched, first + (180 if key is not None else 40), on)
        left = next((i for i in range(first, len(rams)) if (rams[i][AREA], rams[i][SCREEN]) != src), None)
        assert left is not None, f"never went through the door on {src}"
        assert rams[left][IFRAMES] == 0, f"hit on {src} before the door"
        return [step for step in sched if step[0] < left] + [(left, "-")], left

    def source(self, tag, area, way):
        """The temple's saved game, leading onto the edge's source screen."""
        _, kind, src, src_palette, col, row = way
        return self.leading_to(f"{tag}-src", area, src, src_palette, row << 4 | col)

    def edge(self, tag, area, way):
        """The hero at the edge of the source screen, its enemies gone, his
        invincibility spent and HP full (prerequisites, not coverage): from
        the walk out of the temple to there."""
        _, kind, src, src_palette, col, row = way
        there = self.source(tag, area, way)
        raw, ready = self.out / f"{tag}-raw.cycstate", self.out / f"{tag}-edge.cycstate"
        clear = {**{SPRITE_ENTITY + k: 0xFF for k in range(8)}, IFRAMES: 0, HP: 0x50}
        if kind == "drop":
            # He falls on his first update there: saved on the frame before
            # it, once the screen's sprites are in their slots (a frame or two
            # after the screen is ready), and cleared then.
            _, first = self.arrive(there, area, src, self.walk, self.exit_frame + 50, False)
            self.runs.run(there, self.walk, first, False, save=(first - 1, raw))
            fixture(raw, ready, clear)
        else:
            self.runs.run(there, self.walk, self.exit_frame + 50, False, save=(self.exit_frame + 49, raw))
            fixture(raw, ready, {**clear, STATUS: 0, X: col * 16, X_FRACTION: 0, Y: row * 16,
                                 FLAGS: 0x00 if kind == "left" else 0x40})
        return ready

    def check(self, spot):
        """Play the spot with the remaster off, then on. Returns its report line."""
        name, (area, screen, palette), way, (entity, stock, moved), latest = spot
        tag = slug(name)
        if way[0] == "edge":
            state = self.edge(tag, area, way)
            key = HOLD[way[1]]
            # The scroll ends 90-110 frames in.
            plans = {on: (([(0, key)] if key else []), 150 + latest) for on in (False, True)}
        elif way[0] == "temple":
            state = self.leading_to(tag, area, screen, palette, 0x89)                # its own door
            plans = {on: (self.walk, self.exit_frame + 60 + latest) for on in (False, True)}   # in 35-46
        elif way[0] == "building":
            state = self.leading_to(tag, area, screen, palette, way[1])
            plans = {on: (self.walk, self.exit_frame + 60 + latest) for on in (False, True)}
        else:
            _, src, src_palette, door, key = way
            state = self.leading_to(tag, area, src, src_palette, door, {SELECTED_ITEM: key} if key else None)
            plans, lefts = {}, {}
            for on in (False, True):        # the remaster's keys can change the door's timing
                sched, lefts[on] = self.door(state, (area, src), door, key, on)
                plans[on] = (sched, lefts[on] + 50 + latest)                        # in 20-36
        # Off, long enough for the first update and the stock hit; on, the
        # same way in (the moves change no timing before the first update),
        # then SAFE frames.
        seen, first = {}, None
        for on in (False, True):
            sched, frames = plans[on]
            if on:
                frames = first + SAFE + 2 + (lefts[True] - lefts[False] if way[0] == "door" else 0)
            if way[0] == "edge":
                rams = self.runs.run(state, sched, frames, on)
                first = scrolled_in(rams, area, screen)
                assert first is not None, f"{name}: never reached {area}:{screen:#04x}"
            else:
                rams, first = self.arrive(state, area, screen, sched, frames, on)
            assert first + (SAFE if on else latest) < len(rams), f"{name}: the first update came at {first}"
            before = rams[first - 1]
            slots = [k for k in range(8) if before[SPRITE_ENTITY + k] == entity and
                     before[SPRITE_Y + k] | before[SPRITE_X + k] >> 4 in (stock, moved)]
            assert len(slots) == 1, f"{name}: the enemy's slot ({entity:#04x}) " + str(
                [(before[SPRITE_ENTITY + k], before[SPRITE_X + k], before[SPRITE_Y + k]) for k in range(8)])
            k = slots[0]
            at = before[SPRITE_Y + k] | before[SPRITE_X + k] >> 4
            assert at == (moved if on else stock), f"{name}: the enemy starts at {at:#04x}, remaster on: {on}"
            assert before[IFRAMES] == 0, f"{name}: still invincible on arrival"
            hit = first_hit(rams, first)
            seen[on] = hit
            if on:
                assert hit is None or hit >= SAFE, f"{name}: hit {hit} frames after arriving, with the fix"
            else:
                hp = (rams[first - 1][HP], rams[-1][HP])
                assert hit is not None and hit <= latest and hp[1] < hp[0], \
                    f"{name}: stock hit {hit} frames after arriving (HP {hp}), expected by {latest}"
        hero = (rams[first][X], rams[first][Y])
        return (f"ok: {name} (hero at {hero}): in the original, {entity:#04x} at {stock:#04x} hits him "
                f"{seen[False]} frames after his first update; moved to {moved:#04x}, "
                + ("nothing hits him" if seen[True] is None else f"the first hit is {seen[True]} frames in")
                + f" (watched {SAFE})")


# The edges by region, so the suite plays them at once.
EDGE_PARTS = {TRUNK: "edges-trunk", MIST: "edges-mist", BRANCH: "edges-south", DARTMOOR: "edges-south",
              ZENIS: "edges-south"}
PARTS = ("doors", "edges-trunk", "edges-mist", "edges-south", "seamless-room")


def part(spot):
    return EDGE_PARTS[spot[1][0]] if spot[2][0] == "edge" else "doors"


@fax_e2e.recipe("entrances-mascon-temple", parent="gate")
def _mascon_temple(g):
    """Died at Eolis's gate with Mascon as his furthest temple, a Long Sword,
    Leather Armor and a Small Shield: at Mascon's Guru, facing left, once the
    Guru's words are paged."""
    g.poke({SPRITE_ENTITY + 7: 0xFF, TEMPLE_SPAWN_POINT: MASCON, PLAYER_IS_DEAD: 1, HP: 0x50,
            SELECTED_WEAPON: LONG_SWORD, PLAYER_CUR_WEAPON: LONG_SWORD,
            SELECTED_ARMOR: LEATHER_ARMOR, SELECTED_SHIELD: SMALL_SHIELD})
    until("the Guru's words", lambda: ram(MESSAGE_ID) == NEGATIVE_THOUGHTS, 600)
    fax_e2e.page_through("Mascon's temple", lambda: ram(AREA) == BUILDINGS and not ram(PLAYER_IS_DEAD) and
                         ram(MESSAGE_ID) != NEGATIVE_THOUGHTS)
    wait(60)


def state_ram(path):
    """A save state's CPU RAM (its HW chunk, from offset 23)."""
    data = Path(path).read_bytes()
    pos = next(p for tag, p, _ in chunks(data) if tag == b"HW  ")
    return data[pos + 23:pos + 23 + 0x800]


def walk_out(game, there, key, test):
    """Load `there` in the window, walk out of the temple, then hold `key`
    (nothing, for a drop) until `test()`: whether it came true."""
    game.load(there)
    hold("right", lambda: ram(AREA) != BUILDINGS, 300)
    if key:
        return hold(key.lower(), test, 480)
    try:
        until("the drop", test, 480)
        return True
    except AssertionError:
        return False


def main():
    ap = parser(__doc__, PARTS)
    ap.add_argument("--only", action="append", default=[], help="only the spots whose name contains this")
    ap.add_argument("--jobs", type=int, default=3, help="spots played at once (headless runs)")
    args = chosen(ap.parse_args(), PARTS)
    seamless = any("seamless" in a for a in args.game_arg)
    game = Game.from_args(args)
    runs = Runs(game, args.game_arg)
    try:
        temple = game.state("entrances-mascon-temple")
        game.launch()
        state = game.tcp("entrances_state")
        assert state["enabled"], "Fair entrances is on in the remaster"
        table = [tuple(m) for m in state["table"]]
        if not (seamless and any(args.wants(p) for p in PARTS if p != "doors")):
            game.close()                                   # the rest is headless
        ram0 = state_ram(temple)
        assert (ram0[AREA], ram0[SAVED_AREA], ram0[SAVED_SCREEN], ram0[SAVED_POS]) == \
            (BUILDINGS, MIST, 0x06, 0x89), "Mascon's temple opens on the Mist's screen 6, at block $89"

        # How long the walk out of the temple takes, holding Right: the same
        # from every fixture of this state, and in the original.
        rams = runs.run(temple, [(0, "RIGHT")], 40, False)
        exit_frame = next(i for i, r in enumerate(rams) if r[AREA] != BUILDINGS)
        spots = Spots(runs, temple, exit_frame)
        chosen_spots = [s for s in SPOTS if not args.only or any(o in s[0] for o in args.only)]
        for name, (area, screen, _), _, (entity, stock, moved), _ in chosen_spots:
            assert (area, screen, entity, stock, moved) in table, f"{name}: not in the feature's table"

        # With Seamless, an edge's room is on screen before he crosses: those
        # are checked in the window, below.
        played = [s for s in chosen_spots if args.wants(part(s)) and not (seamless and s[2][0] == "edge")]
        with ThreadPoolExecutor(max(1, args.jobs)) as pool:
            for line in pool.map(spots.check, played):
                print(line, flush=True)

        walked = [s for s in chosen_spots if seamless and s[2][0] == "edge" and args.wants(part(s))]
        if walked:
            # The enemy starts at its new block whichever machine loads the
            # room: Seamless's isolated one, or the game's own scroll where a
            # seam is not joined. The move's use count says it was made.
            for name, (area, screen, _), way, (entity, stock, moved), _ in walked:
                there = spots.source(slug(name), area, way)
                i = table.index((area, screen, entity, stock, moved))
                used = game.tcp("entrances_state")["uses"][i]
                made = walk_out(game, there, HOLD[way[1]], lambda: game.tcp("entrances_state")["uses"][i] > used)
                assert made, f"{name}: the move was not made ({used} -> {game.tcp('entrances_state')['uses'][i]})"
                print(f"ok: {name}: with Seamless, the room beyond is loaded with {entity:#04x} at {moved:#04x}",
                      flush=True)

        if seamless and args.wants("seamless-room"):
            # Seamless's neighbouring room, loaded on an isolated machine.
            mist5 = spots.leading_to("mist5", MIST, 0x05, 0x0A, 0x9C)
            before = game.tcp("entrances_state")
            room = {}

            def gawaine_live():
                room.update(game.tcp("fax_world_room", room=6))
                return room["live"] and GAWAINE in room["entities"]

            walk_out(game, mist5, None, lambda: (ram(AREA), ram(SCREEN)) == (MIST, 0x05))
            until("Seamless's room for the Mist's screen 6", gawaine_live, 400)
            after = game.tcp("entrances_state")
            # Seamless loads each neighbour so (the Mist's $0F below too): the
            # move on $06 is the one counted.
            i = table.index((MIST, 0x06, GAWAINE, 0x89, 0x9E))
            assert after["seamless"] > before["seamless"] and after["uses"][i] > before["uses"][i], (before, after)
            # Where he is now: he started at the new block and has moved at
            # most a pixel a tick since (or the stock one, 80 pixels off).
            xs = [x for e, x in zip(room["entities"], room["x"]) if e == GAWAINE]
            moved_x = (0x9E & 0x0F) << 4
            assert any(abs(x - moved_x) <= room["ticks"] + 8 for x in xs), (room, xs)
            print(f"ok: Seamless loads the Mist's screen 6 beside screen 5 with Sir Gawaine moved to $9E "
                  f"(x={xs} after {room['ticks']} ticks), on its isolated machine")
        print(f"all fair entrances checks passed ({next(runs.ids)} headless runs)")
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    main()
