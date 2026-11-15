"""The game in a hidden window over TCP, for Block's and Sugata's flash's end-to-end tests.

The game runs uncapped and the tests wait on the game's own state, never on
the clock, on a free TCP port, so any number can run in parallel. Save-state
RAM is edited (an external prerequisite, not coverage) to set up fixtures:
a shield, gold, or where a building's door leads (Area_Saved*, as
tests/zones/zone_names.py does) to walk out anywhere.
"""
import json
import os
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import time
import zlib

AREA, SCREEN, POS_X, POS_Y = 0x24, 0x63, 0x9E, 0xA1
PLAYER_FLAGS, FACING_RIGHT = 0xA4, 0x40
ENTITIES, SPRITE_X, SPRITE_Y, SPRITE_HP = 0x2CC, 0xBA, 0xC2, 0x344
TEXTBOX_CLOSED, MESSAGE_ID, TEXTBOX_PAUSED = 0x20C, 0x213, 0x215
GOLD = 0x392
SHIELD_INVENTORY, SELECTED_SHIELD, NUMBER_OF_SHIELDS = 0x3A5, 0x3BF, 0x3C4
AREA_SAVED_AREA, AREA_SAVED_SCREEN, SCREEN_SAVED_PALETTE, PLAYER_SAVED_POS = 0x3D5, 0x3D6, 0x3D7, 0x3D8
HP, AREA_REGION, PLAYER_IS_DEAD, TEMPLE_SPAWN_POINT = 0x431, 0x435, 0x438, 0x439
INVINCIBILITY = 0xAD
BUILDINGS, TOWNS = 4, 3
SMALL, LARGE, MAGIC, HELMET, NO_SHIELD = 0, 1, 2, 3, 0xFF


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
    """Copy a save state, changing RAM (`writes`: address -> byte)."""
    data = bytearray(Path(source).read_bytes())
    pos, size = next((p, n) for t, p, n in chunks(data) if t == b"HW  ")
    assert size == 2072, "Unqualified hardware ABI"
    for address, value in writes.items():
        data[pos + 23 + address] = value
    Path(target).write_bytes(data)


class Game:
    def __init__(self, build, rom, out, game_args=(), realtime=False, port=0):
        image = Path(rom).read_bytes()
        assert zlib.crc32(image[16:]) == 0x57DD23D1, "the original Faxanadu (USA) ROM is required"
        self.rom, self.out = Path(rom).resolve(), Path(out).resolve()
        self.args = [part for arg in game_args for part in arg.split("=", 1)]
        self.realtime, self.port = realtime, port or free_port()
        shutil.rmtree(self.out, ignore_errors=True)
        (self.out / "game").mkdir(parents=True)
        shutil.copy2(Path(build) / "FaxanaduRecomp", self.out / "game")
        for folder in ("assets", "mods"):
            shutil.copytree(Path(build) / folder, self.out / "game" / folder)
        self.config = self.out / "game" / "config.ini"
        self.log = open(self.out / "game.log", "w")
        self.proc = None
        self.shots = []

    # ---- the process ----
    def launch(self, *extra):
        extra = self.args + list(extra)
        if not self.realtime:
            extra += ["--uncapped", "--exit-after", "100000000"]
        env = dict(os.environ, NESRECOMP_NO_LAUNCHER="1", SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        self.proc = subprocess.Popen([str(self.out / "game" / "FaxanaduRecomp"), str(self.rom), "--hidden",
                                      "--tcp", str(self.port), *extra], env=env, stdout=self.log,
                                     stderr=subprocess.STDOUT)
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

    def running(self):
        return self.proc is not None and self.proc.poll() is None

    # ---- TCP ----
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
            time.sleep(0.003)

    def ram(self, addr, n=1):
        data = bytes.fromhex(self.tcp("read_ram", addr=addr, len=n)["hex"])
        return data[0] if n == 1 else data

    def until(self, what, test, frames=1200):
        end = self.frame() + frames
        while self.frame() < end:
            if test():
                return
            time.sleep(0.003)
        raise AssertionError(f"timed out waiting for {what}")

    def act(self, name, frames=6, after=8):
        self.tcp("action", name=name, frames=frames)
        self.wait(frames + after)

    def save(self, name):
        path = self.out / f"{name}.cycstate"
        self.tcp("save_state", path=str(path))
        return path

    def load(self, path):
        self.tcp("load_state", path=str(path))

    def shot(self, name):
        path = self.out / f"{len(self.shots):02d}-{name}.png"
        self.tcp("screenshot", layer="picture", path=str(path))
        self.shots.append(path)
        return path

    # ---- the game ----
    def where(self):
        return self.ram(AREA), self.ram(SCREEN)

    def hp(self):
        return self.ram(HP)

    def typed(self):
        """The text box is waiting for a button: the message has typed out
        (MessageID clears) or reached a pause (TextBox_MessagePaused)."""
        message, _, paused = self.ram(MESSAGE_ID, 3)
        return message == 0 or paused != 0

    def box_open(self):
        return self.ram(TEXTBOX_CLOSED) == 0

    def settled(self):
        """A screen has loaded and is not scrolling (docs/GAME_INTERNALS.md)."""
        return self.ram(0x0E) == 0 and self.ram(0x54) == 0xFF and self.ram(0x13) != 0

    def new_game_at_gate(self):
        """A new game, through the opening, to the gate of Eolis (a door)."""
        self.until("the title", lambda: self.frame() > 700)
        self.tcp("pad", buttons="start", frames=10)
        self.until("the opening", lambda: self.frame() > 1900)
        self.act("right", 60, 10)
        for _ in range(60):
            if self.ram(POS_X) > 54:
                break
            if self.typed():
                self.act("a", 4, 8)
            else:
                self.wait(10)
            self.act("right", 6, 2)
        for _ in range(200):
            if self.ram(POS_X) >= 192:
                break
            self.act("right", 6, 2)
        assert self.where() == (0, 0) and 188 <= self.ram(POS_X) < 200, (self.where(), self.ram(POS_X))

    def walk_to(self, x_min, x_max):
        """Walk until the hero's X is in [x_min, x_max], paging through any text."""
        for _ in range(600):
            if self.box_open():
                if self.typed():
                    self.act("a", 6, 14)
                else:
                    self.wait(4)
                continue
            x = self.ram(POS_X)
            if x_min <= x <= x_max:
                return
            self.act("right" if x < x_min else "left", 2, 0)
        raise AssertionError(f"could not reach x {x_min}-{x_max}")

    def walk_out(self):
        """Walk right out of a building, back to where its door leads."""
        for _ in range(200):
            if self.ram(AREA) != BUILDINGS:
                self.until("the screen to load", self.settled, 600)
                return
            self.act("right", 2, 0)
        raise AssertionError("could not walk out")

    def warp(self, inside, name, area, screen, palette, pos, region, writes=None):
        """From a save state inside a building, walk out at (area, screen, pos YX)."""
        target = self.out / f"{name}.cycstate"
        fixture(inside, target, {AREA_SAVED_AREA: area, AREA_SAVED_SCREEN: screen, SCREEN_SAVED_PALETTE: palette,
                                 PLAYER_SAVED_POS: pos, AREA_REGION: region, HP: 0x50, **(writes or {})})
        self.load(target)
        self.walk_out()
        assert self.where() == (area, screen), self.where()
        return target
