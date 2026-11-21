"""What the movement features' end-to-end tests share (falling_pose.py,
forgiving_jumps.py).

- A hidden window over TCP, as the other features' tests drive the game, to
  get somewhere with ledges and ladders: a new game, then (an external
  prerequisite, not coverage) a save-state RAM fixture that sends the hero to
  Apolune's temple by dying there, with a Long Sword and a Small Shield, out
  of the temple, and left out of town to Trunk (area 1). Screen 7 has a ledge with a clear 80-pixel drop, screen 6 a
  ladder hanging over the floor.
- Headless runs of the same build from those save states, with a button
  schedule and a per-frame log of RAM (--input, --frame-log), for anything
  that depends on the exact frame a button goes down. Each takes the remaster
  on or off through the mod selection (off is the original game), and
  screenshots come from them too.
"""
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
import fax_e2e  # noqa: E402
from fax_e2e import fixture  # noqa: E402  (falling_pose.py and forgiving_jumps.py import it from here)

# RAM
JOY, AREA, SCROLL_DIRECTION, SCREEN = 0x16, 0x24, 0x54, 0x63
X, Y, TICK, FLAGS, STATUS, JC = 0x9E, 0xA1, 0xA3, 0xA4, 0xA5, 0xA6
IFRAMES, HP, TEMPLE_SPAWN_POINT, PLAYER_IS_DEAD = 0xAD, 0x431, 0x439, 0x438
DURATION_WING_BOOTS, MESSAGE_ID, SPRITE_ENTITY, SHADOW_OAM = 0x429, 0x213, 0x2CC, 0x700
JUMPING, FALLING, CAN_CLIMB, CLIMBING, MOVING, RIGHT, ATTACKING = 0x01, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80
KNOCKBACK, WING_BOOTS = 0x02, 0x80
SELECTED_WEAPON, SELECTED_ARMOR, SELECTED_SHIELD, PLAYER_CUR_WEAPON = 0x3BD, 0x3BE, 0x3BF, 0x3C8
LONG_SWORD, LEATHER_ARMOR, SMALL_SHIELD = 1, 0, 0
APOLUNE, TRUNK, BUILDINGS, NEGATIVE_THOUGHTS = 1, 1, 4, 0x24
LEDGE_SCREEN, LADDER_SCREEN = 7, 6
PACKAGE, FEATURE = "faxanadu.remastered", "remastered"          # the one switch: off is the original game


def selection(original=False):
    """The features for set_features(): the remaster on (the default), or off for the original game."""
    return {(PACKAGE, FEATURE): False} if original else {}


def no_actors():
    return {SPRITE_ENTITY + i: 0xFF for i in range(8)}


class Game(fax_e2e.Game):
    """The build, copied to OUT/game, in a hidden window driven over TCP
    (fax_e2e.Game), with what the movement tests add."""

    def select(self, original=False):
        """The mod selection: the remaster on (the default), or off for the original."""
        self.set_features(selection(original))

    def settle(self):
        """Until the screen has stopped scrolling (Screen_ScrollDirection $FF):
        the hero is not updated while it scrolls."""
        self.until("the scroll to end", lambda: self.ram(SCROLL_DIRECTION) == 0xFF, 600)
        self.wait(10)

    def to_trunk(self):
        """Save states with the hero in Apolune's temple, at its door, and on
        Trunk's screens 7 and 6, standing on the floor: fixtures, made (if not
        cached) before the game starts."""
        return {"temple": self.state("apolune-temple"), "apolune": self.state("apolune"),
                LEDGE_SCREEN: self.state("trunk-ledge"), LADDER_SCREEN: self.state("trunk-ladder")}

    def mods(self, name, original=False):
        """A mod catalog for headless runs: the build's packages, with the
        remaster on, or off for the original."""
        root = self.out / name
        if not root.exists():
            shutil.copytree(self.out / "game" / "mods" / "packages", root / "packages")
            (root / "state.toml").write_text(fax_e2e.features_toml(selection(original)))
        return root

    def continues_at(self, state):
        """The frame a save state continues at: the uint32 at its offset 28."""
        return struct.unpack_from("<I", Path(state).read_bytes(), 28)[0]

    def headless(self, state, schedule, frames, mods, screenshot=None, save=None):
        """Run from `state` for `frames` frames, holding buttons on a schedule
        of (frame, buttons) pairs counted from the state's frame ("RIGHT+A",
        "-" for none). Returns each frame's CPU RAM, after the frame. With
        `save`, (frame, path): also save a state after that frame."""
        start = self.continues_at(state)
        run = Path(tempfile.mkdtemp(prefix="run-", dir=self.out))
        mods = shutil.copytree(mods, run / "mods")                  # its own: a run writes its selection back
        (run / "input.txt").write_text("".join(f"{start + at} {buttons}\n" for at, buttons in sorted(schedule)))
        log = run / "frames.bin"
        args = [str(self.exe), str(self.rom), "--load-state", str(state), "--input", str(run / "input.txt"),
                "--frames", str(start + frames), "--frame-log", str(log),
                "--frame-log-frames", f"{start}:{start + frames - 1}", "--mods-root", str(mods), "--no-save"]
        if screenshot:
            args += ["--screenshot", str(screenshot)]
        if save:
            args += ["--save-state", f"{start + save[0]}:{save[1]}"]
        done = subprocess.run(args, capture_output=True, text=True)
        assert done.returncode == 0, done.stdout + done.stderr
        data = log.read_bytes()
        shutil.rmtree(run)
        assert data[:8] == b"CYCFRAME"
        rams, pos = [], 12
        while pos < len(data):
            head = struct.unpack_from("<9I", data, pos)
            pos += 36 + 32 + 256
            rams.append(data[pos:pos + head[3]])
            pos += head[3] + sum(head[4:9])
        assert len(rams) == frames, (len(rams), frames)
        return rams

    def headless_all(self, runs):
        """headless(*run) for each run at once, each its own process; their
        results, in order."""
        runs = list(runs)
        with ThreadPoolExecutor(max_workers=min(len(runs), os.cpu_count() or 4)) as pool:
            return list(pool.map(lambda run: self.headless(*run), runs))



@fax_e2e.recipe("apolune-temple", parent="gate")
def _apolune_temple(g):
    """Died at Eolis's gate with Apolune as the temple, and a sword and
    shield, so the weapon and shield take poses too (the respawn's screen
    setup loads their tiles): at Apolune's Guru, facing left."""
    dying = g.out / "dying.cycstate"
    g.tcp("save_state", path=str(dying))
    fixture(dying, dying, {SPRITE_ENTITY + 7: 0xFF, TEMPLE_SPAWN_POINT: APOLUNE, PLAYER_IS_DEAD: 1, HP: 0x50,
                           SELECTED_WEAPON: LONG_SWORD, PLAYER_CUR_WEAPON: LONG_SWORD,
                           SELECTED_ARMOR: LEATHER_ARMOR, SELECTED_SHIELD: SMALL_SHIELD})
    g.load(dying)
    g.until("the death message", lambda: g.ram(MESSAGE_ID) == NEGATIVE_THOUGHTS, 600)
    fax_e2e.page_through("Apolune's temple", lambda: g.ram(AREA) == BUILDINGS and not g.ram(PLAYER_IS_DEAD) and
                         g.ram(MESSAGE_ID) != NEGATIVE_THOUGHTS)
    g.wait(60)


@fax_e2e.recipe("apolune", parent="apolune-temple")
def _apolune(g):
    """Out of the temple, at its door in Apolune."""
    for _ in range(40):
        if g.ram(AREA) != BUILDINGS:
            break
        g.tcp("action", name="right", frames=30)
        g.wait(31)
    g.until("Apolune", lambda: g.ram(AREA) != BUILDINGS, 300)
    g.settle()


def _left_to(g, screen):
    """Walk left until Trunk's `screen`, topping HP up on the way."""
    for _ in range(200):
        if (g.ram(AREA), g.ram(SCREEN)) == (TRUNK, screen):
            g.settle()
            return
        if g.ram(HP) < 0x30:
            g.poke({HP: 0x50})
        g.tcp("action", name="left", frames=30)
        g.wait(31)
    raise AssertionError(f"did not reach Trunk {screen}: {g.ram(AREA)}/{g.ram(SCREEN)}")


@fax_e2e.recipe("trunk-ledge", parent="apolune")
def _trunk_ledge(g):
    """Left out of Apolune to Trunk's screen 7, with its ledge."""
    _left_to(g, LEDGE_SCREEN)


@fax_e2e.recipe("trunk-ladder", parent="trunk-ledge")
def _trunk_ladder(g):
    """On left to Trunk's screen 6, with its ladder."""
    _left_to(g, LADDER_SCREEN)


def hero_sprites(ram):
    """The hero's sprites drawn this frame (shield, body, weapon), from the
    shadow OAM, relative to his position and in a fixed order: what the pose
    looks like. The game rotates sprite order from frame to frame."""
    hx, hy = ram[X], ram[Y]
    seen = []
    for i in range(4, 256, 4):                      # sprite 0 is the status bar's
        y, tile, attr, x = ram[SHADOW_OAM + i:SHADOW_OAM + i + 4]
        if y < 0xEF and abs(x - hx) < 32 and 0 <= y - hy < 64:
            seen.append((y - hy, tile, attr, x - hx))
    return tuple(sorted(seen))
