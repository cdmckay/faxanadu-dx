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
import json
import os
from pathlib import Path
import re
import shutil
import socket
import struct
import subprocess
import time
import zlib

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
    """mods/state.toml with the remaster on (the default), or off for the original game."""
    return "format_version = 1\n" + (
        f'\n[[feature]]\npackage_id = "{PACKAGE}"\nid = "{FEATURE}"\nenabled = false\n' if original else "")


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def chunks(data):
    assert data[:8] == b"CYCSTATE"
    pos = 32
    while pos < len(data):
        tag, size = struct.unpack_from("<4sI", data, pos)
        pos += 8
        yield tag, pos, size
        pos += size
        if tag == b"END ":
            return


def fixture(source, target, writes):
    """A copy of a save state with CPU RAM bytes changed (the HW chunk, offset 23)."""
    data = bytearray(Path(source).read_bytes())
    pos, size = next((p, n) for t, p, n in chunks(data) if t == b"HW  ")
    assert size == 2072, "Unqualified hardware ABI"
    for address, value in writes.items():
        data[pos + 23 + address] = value
    Path(target).write_bytes(data)


def no_actors():
    return {SPRITE_ENTITY + i: 0xFF for i in range(8)}


class Game:
    """The build, copied to OUT/game, in a hidden window driven over TCP."""

    def __init__(self, build, rom, out, game_args=(), realtime=False):
        self.rom, self.out, self.game_args, self.realtime = Path(rom).resolve(), Path(out), list(game_args), realtime
        image = self.rom.read_bytes()
        assert zlib.crc32(image[16:]) == 0x57DD23D1, "the original Faxanadu (USA) ROM is required"
        shutil.rmtree(self.out, ignore_errors=True)
        (self.out / "game").mkdir(parents=True)
        shutil.copy2(Path(build) / "FaxanaduRecomp", self.out / "game")
        for folder in ("assets", "mods"):
            shutil.copytree(Path(build) / folder, self.out / "game" / folder)
        self.exe = self.out / "game" / "FaxanaduRecomp"
        self.port, self.proc, self.shots = 0, None, []
        self.log = open(self.out / "game.log", "w")

    def tcp(self, cmd, **kw):
        with socket.create_connection(("127.0.0.1", self.port), 5) as s:
            s.settimeout(30)
            f = s.makefile("rwb", buffering=0)
            f.write((json.dumps(dict(id=1, cmd=cmd, **kw)) + "\n").encode())
            reply = json.loads(f.readline())
        if not reply.get("ok"):
            raise RuntimeError(f"{cmd}: {reply}")
        return reply

    def frame(self):
        return self.tcp("ping")["frame"]

    def wait(self, frames):
        end = self.frame() + frames
        while self.frame() < end:
            time.sleep(0.005)

    def ram(self, addr, n=1):
        data = bytes.fromhex(self.tcp("read_ram", addr=addr, len=n)["hex"])
        return data[0] if n == 1 else data

    def until(self, what, test, frames=1200):
        end = self.frame() + frames
        while self.frame() < end:
            if test():
                return
            time.sleep(0.005)
        raise AssertionError(f"timed out waiting for {what}")

    def launch(self, *extra):
        self.port = free_port()
        extra = [part for arg in self.game_args for part in arg.split("=", 1)] + list(extra)
        if not self.realtime:
            extra += ["--uncapped", "--exit-after", "100000000"]
        env = dict(os.environ, NESRECOMP_NO_LAUNCHER="1", SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        self.proc = subprocess.Popen([str(self.exe), str(self.rom), "--hidden", "--tcp", str(self.port), *extra],
                                     env=env, stdout=self.log, stderr=subprocess.STDOUT)
        for _ in range(100):
            try:
                return self.frame()
            except OSError:
                time.sleep(0.1)
        raise AssertionError("the game did not start")

    def close(self):
        if not self.proc:
            return
        try:
            self.tcp("quit")
        except Exception:
            self.proc.kill()
        self.proc.wait(10)
        self.proc = None

    def shot(self, name):
        path = self.out / f"{len(self.shots):02d}-{name}.png"
        self.tcp("screenshot", layer="picture", path=str(path))
        self.shots.append(path)
        return path

    def select(self, original=False):
        """The mod selection: the remaster on (the default), or off for the original."""
        (self.out / "game" / "mods" / "state.toml").write_text(selection(original))

    def settle(self):
        """Until the screen has stopped scrolling (Screen_ScrollDirection $FF):
        the hero is not updated while it scrolls."""
        self.until("the scroll to end", lambda: self.ram(SCROLL_DIRECTION) == 0xFF, 600)
        self.wait(10)

    def to_trunk(self):
        """From a new game to Trunk: returns save states with the hero on screen
        7 and screen 6, standing on the floor."""
        self.until("the title", lambda: self.frame() > 700)
        self.tcp("pad", buttons="start", frames=10)
        self.until("the Eolis gate", lambda: self.frame() > 1900)
        gate = self.out / "gate.cycstate"
        self.tcp("save_state", path=str(gate))
        dying = self.out / "dying.cycstate"
        # A sword and shield, so the weapon and shield take poses too; the
        # respawn's screen setup loads their tiles.
        fixture(gate, dying, {SPRITE_ENTITY + 7: 0xFF, TEMPLE_SPAWN_POINT: APOLUNE, PLAYER_IS_DEAD: 1, HP: 0x50,
                              SELECTED_WEAPON: LONG_SWORD, PLAYER_CUR_WEAPON: LONG_SWORD,
                              SELECTED_ARMOR: LEATHER_ARMOR, SELECTED_SHIELD: SMALL_SHIELD})
        self.tcp("load_state", path=str(dying))
        self.until("the death message", lambda: self.ram(MESSAGE_ID) == NEGATIVE_THOUGHTS, 600)
        for _ in range(60):
            if self.ram(AREA) == BUILDINGS and not self.ram(PLAYER_IS_DEAD) and self.ram(MESSAGE_ID) != NEGATIVE_THOUGHTS:
                break
            message, _, paused = self.ram(MESSAGE_ID, 3)
            if message == 0 or paused:
                self.tcp("action", name="a", frames=6)
                self.wait(14)
            else:
                self.wait(10)
        self.wait(60)
        temple = self.out / "temple.cycstate"                           # at Apolune's Guru, facing left
        self.tcp("save_state", path=str(temple))
        for _ in range(40):                                  # out of the temple, into Apolune
            if self.ram(AREA) != BUILDINGS:
                break
            self.tcp("action", name="right", frames=30)
            self.wait(31)
        self.until("Apolune", lambda: self.ram(AREA) != BUILDINGS, 300)
        self.settle()
        states = {"temple": temple, "apolune": self.out / "apolune.cycstate"}   # in the temple; at its door
        self.tcp("save_state", path=str(states["apolune"]))
        for _ in range(200):                                 # left, out of town, to Trunk's screens 7 and 6
            if (self.ram(AREA), self.ram(SCREEN)) == (TRUNK, LEDGE_SCREEN) and LEDGE_SCREEN not in states:
                self.settle()
                states[LEDGE_SCREEN] = self.out / "trunk7.cycstate"
                self.tcp("save_state", path=str(states[LEDGE_SCREEN]))
            if (self.ram(AREA), self.ram(SCREEN)) == (TRUNK, LADDER_SCREEN):
                self.settle()
                states[LADDER_SCREEN] = self.out / "trunk6.cycstate"
                self.tcp("save_state", path=str(states[LADDER_SCREEN]))
                break
            if self.ram(HP) < 0x30:
                topped = self.out / "topped.cycstate"
                self.tcp("save_state", path=str(topped))
                fixture(topped, topped, {HP: 0x50})
                self.tcp("load_state", path=str(topped))
            self.tcp("action", name="left", frames=30)
            self.wait(31)
        assert LEDGE_SCREEN in states and LADDER_SCREEN in states, f"did not reach Trunk: {self.ram(AREA)}/{self.ram(SCREEN)}"
        return states

    def mods(self, name, original=False):
        """A mod catalog for headless runs: the build's packages, with the
        remaster on, or off for the original."""
        root = self.out / name
        if not root.exists():
            shutil.copytree(self.out / "game" / "mods" / "packages", root / "packages")
            (root / "state.toml").write_text(selection(original))
        return root

    def headless(self, state, schedule, frames, mods, screenshot=None):
        """Run from `state` for `frames` frames, holding buttons on a schedule
        of (frame, buttons) pairs counted from the state's frame ("RIGHT+A",
        "-" for none). Returns each frame's CPU RAM, after the frame."""
        probe = subprocess.run([str(self.exe), str(self.rom), "--load-state", str(state), "--frames", "0", "--no-save"],
                               capture_output=True, text=True)
        start = int(re.search(r"continuing at frame (\d+)", probe.stdout).group(1))
        run = self.out / "run"
        run.mkdir(exist_ok=True)
        (run / "input.txt").write_text("".join(f"{start + at} {buttons}\n" for at, buttons in sorted(schedule)))
        log = run / "frames.bin"
        args = [str(self.exe), str(self.rom), "--load-state", str(state), "--input", str(run / "input.txt"),
                "--frames", str(start + frames), "--frame-log", str(log),
                "--frame-log-frames", f"{start}:{start + frames - 1}", "--mods-root", str(mods), "--no-save"]
        if screenshot:
            args += ["--screenshot", str(screenshot)]
        done = subprocess.run(args, capture_output=True, text=True)
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
