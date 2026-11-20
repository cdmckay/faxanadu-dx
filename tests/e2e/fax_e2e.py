"""What every end-to-end test shares: a reserved TCP port, the game in a hidden
window, the TCP client, save-state RAM fixtures and screenshots.

A test imports it with a sys.path line, so it still runs directly
(`python3 tests/x/y.py --build build-cycle --rom ... --out ...`):

    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
    from fax_e2e import Game, act, fixture, frame, parse_args, ram, tcp, typed, until, wait  # noqa: E402

    def main():
        args = parse_args(__doc__)       # --build --rom --out, --game-arg, --port, --realtime
        game = Game.from_args(args)      # checks the ROM, copies the build to OUT/game
        try:
            game.launch()                # a hidden window, uncapped, on a reserved port
            until("the title", lambda: frame() > 700)
            tcp("pad", buttons="start", frames=10)
            game.shot("title")           # OUT/00-title.png
        except BaseException:
            if game.running():
                game.shot("failure")
            raise
        finally:
            game.close()

Never pick a port yourself: Game.launch() reserves one (see Ports).

Count game frames, never seconds: wait() and until() count the game's frames,
so a test runs the same however loaded the machine is. A command is served
on a frame boundary and the script can lag any number of frames behind the
game, so input that must land on a given frame goes in with the save state
it starts from, in one batch(), or runs headless from that state with --input
(tests/movement/play.py's headless()).

Game, one per test
    Game(build, rom, out, game_args=(), realtime=False, port=0), or Game.from_args(args)
        Checks the ROM is the original Faxanadu (USA), empties OUT, copies the
        build's FaxanaduRecomp, assets/ and mods/ to OUT/game (so sidecars such
        as faxanadu_gurus.txt and config.ini stay there) and opens OUT/game.log.
    .launch(*extra)   Start the game: FaxanaduRecomp ROM --hidden --tcp PORT, the
                      --game-arg options, then `extra`, then --uncapped
                      --exit-after 100000000 unless realtime, with the hidden-window
                      environment (.env). Waits for this game's own "listening on"
                      line, then returns its first frame. If it cannot listen or
                      exits first, it is relaunched on another port (3 tries).
                      Relaunching keeps the port.
    .close()          Quit over TCP (kill if that fails) and wait for the process.
    .running()        The game process is alive.
    .shot(name)       Screenshot the game's picture to OUT/NN-name.png, NN counting
                      up from 00. Returns the Path; the file name goes in .shots.
    .set_features({(package_id, feature_id): enabled})
                      Write OUT/game/mods/state.toml, the launcher's mod selection.
    .poke(writes, records=None)
                      Edit RAM in the running game: save a state, fixture() it,
                      load it back.
    .tcp() .frame() .wait() .ram() .until()   As the functions below, on this game.
    .out .exe .rom .image (the ROM's bytes) .env .log .port .proc .shots
    .exe              What launch() runs: OUT/game/FaxanaduRecomp. Point it at
                      another copy to run that one (Save files does).
    .timeout          Seconds tcp() waits for a reply (30).

Functions, on the game launched last
    tcp(cmd, **kw)                  One command; its reply. RuntimeError unless ok.
    batch((cmd, {args}), ...)       Several commands in one write; their replies.
                                    The game serves them all before its next
                                    frame, so a load_state then an action holds
                                    the button from the state's first frame.
    frame()                         The frame counter (ping).
    wait(frames)                    Until `frames` more frames have run.
    ram(addr, n=1)                  CPU RAM: an int for one byte, else bytes.
    until(what, test, frames=1200)  Poll `test` until it is true; AssertionError
                                    "timed out waiting for {what}" after `frames`.
    act(name, frames=6, after=8)    Hold an NES button (TCP action) for `frames`,
                                    then wait `frames + after`.
    typed()                         The text box waits for a button: MessageID
                                    clears, or TextBox_MessagePaused is set.

Save states and the mod selection
    chunks(data)                    A CYCSTATE's chunks: (tag, offset, size).
    fixture(source, target, writes, records=None)
                                    Copy a save state, changing CPU RAM
                                    ({address: byte}, in its HW chunk at offset 23)
                                    and bytes of mods' records ({record name:
                                    {offset: byte}}, e.g. "faxanadu.zone-names").
    features_toml(features)         state.toml text for {(package_id, feature_id): enabled}.
    parse_args(doc) / parser(doc)   The common options, with `doc` as the help.

Ports
    reserve_port(port=0) -> int     Hold a port for this process until it exits.
    release_port(port)

    A port is reserved with an exclusive flock() on LOCKS/<port>.lock, where
    LOCKS is $XDG_RUNTIME_DIR/faxanadu-e2e-ports (or faxanadu-e2e-ports-<uid> in
    the temp dir), shared by every test and every suite on the machine. It is
    held until the test ends, across relaunches. The kernel drops a flock when
    its process dies, so a lock left by a killed test is free again. Ports are
    drawn at random from PORTS, below the ephemeral range that Linux (32768+)
    and macOS (49152+) hand out to bind(0) and to outgoing connections, so no
    client socket can take one; a port something else is bound to is skipped.
    POSIX only (fcntl), as the tests are.
"""
import argparse
import atexit
import errno
import fcntl
import hashlib
import inspect
import json
import os
from pathlib import Path
import random
import re
import resource
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import zlib

ROM_CRC32 = 0x57DD23D1                     # Faxanadu (USA), without its 16-byte header
HIDDEN_ENV = dict(NESRECOMP_NO_LAUNCHER="1", SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
UNCAPPED = ["--uncapped", "--exit-after", "100000000"]
PORTS = range(20000, 32768)
_runtime = os.environ.get("XDG_RUNTIME_DIR")
LOCKS = (Path(_runtime) / "faxanadu-e2e-ports" if _runtime and os.path.isdir(_runtime)
         else Path(tempfile.gettempdir()) / f"faxanadu-e2e-ports-{os.getuid()}")
REPO = Path(__file__).resolve().parents[2]
FIXTURES = REPO / "build-tests" / "fixtures"  # the saved games tests start from, made from your ROM
LAUNCH_TRIES = 3
LISTEN_TIMEOUT = 60                        # seconds for the game to open its TCP port
MESSAGE_ID = 0x213                         # MessageID, Message_ProcessedLength, TextBox_MessagePaused


# Ports

_held = {}                                 # port: the open, flock()ed lock file


def _lock(port):
    """flock LOCKS/<port>.lock; the descriptor, or None if another process holds it."""
    path = LOCKS / f"{port}.lock"
    while True:
        fd = os.open(path, os.O_RDWR | os.O_CREAT, 0o600)
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as e:
            os.close(fd)
            if e.errno in (errno.EAGAIN, errno.EACCES):
                return None
            raise
        try:
            st = os.stat(path)
        except FileNotFoundError:
            st = None
        mine = os.fstat(fd)
        if st and (st.st_dev, st.st_ino) == (mine.st_dev, mine.st_ino):
            os.ftruncate(fd, 0)
            os.write(fd, f"{os.getpid()}\n".encode())
            return fd
        os.close(fd)                       # released and removed as we opened it: lock the new file


def _bindable(port):
    with socket.socket() as s:
        try:
            s.bind(("127.0.0.1", port))
            return True
        except OSError:
            return False


def reserve_port(port=0):
    """Hold a TCP port for this process until it exits, or until release_port().
    With `port`, that one (RuntimeError if another test holds it); otherwise a
    random one from PORTS that no test holds and nothing is bound to."""
    LOCKS.mkdir(mode=0o700, parents=True, exist_ok=True)
    if port in _held:
        return port
    for candidate in [port] if port else random.sample(PORTS, 400):
        fd = _lock(candidate)
        if fd is None:
            continue
        if _bindable(candidate) or port:
            _held[candidate] = fd
            return candidate
        _unlock(candidate, fd)
    if port:
        raise RuntimeError(f"port {port} is held by another end-to-end test ({LOCKS / f'{port}.lock'})")
    raise RuntimeError(f"no free port in {PORTS.start}-{PORTS.stop - 1}")


def _unlock(port, fd):
    try:
        os.unlink(LOCKS / f"{port}.lock")  # while still locked, so a waiting opener sees it go
    except FileNotFoundError:
        pass
    os.close(fd)


def release_port(port):
    fd = _held.pop(port, None)
    if fd is not None:
        _unlock(port, fd)


@atexit.register
def _release_all():
    for port in list(_held):
        release_port(port)


_started = time.monotonic()


class _Stamped:
    """stdout with the seconds since the test started before each line, and
    flushed at once: FAX_E2E_STAMP=1 shows where a test's time goes."""

    def __init__(self, stream):
        self.stream, self.fresh = stream, True

    def write(self, text):
        for part in text.splitlines(keepends=True):
            if self.fresh:
                self.stream.write(f"[{time.monotonic() - _started:6.1f} s] ")
            self.stream.write(part)
            self.fresh = part.endswith("\n")
        self.stream.flush()
        return len(text)

    def flush(self):
        self.stream.flush()


if os.environ.get("FAX_E2E_STAMP"):
    sys.stdout = _Stamped(sys.stdout)


@atexit.register
def _report_time():
    """What the test cost, if it launched a game: its wall time, and the CPU
    its games (and other children) and the script itself used."""
    if not _launched:
        return
    games, script = resource.getrusage(resource.RUSAGE_CHILDREN), resource.getrusage(resource.RUSAGE_SELF)
    print(f"time: {time.monotonic() - _started:.1f} s, games {games.ru_utime + games.ru_stime:.1f} s CPU, "
          f"script {script.ru_utime + script.ru_stime:.1f} s CPU", flush=True)


# The game

_game = None                               # the game launched last: the functions' target
_launched = False                          # this process has launched a game


def _current():
    if _game is None:
        raise RuntimeError("no game is running: call Game.launch() first")
    return _game


class Game:
    """The build, copied to OUT/game, in a hidden window driven over TCP."""

    def __init__(self, build, rom, out, game_args=(), realtime=False, port=0):
        self.rom = Path(rom).resolve()
        self.image = self.rom.read_bytes()
        assert zlib.crc32(self.image[16:]) == ROM_CRC32, "the original Faxanadu (USA) ROM is required"
        self.build = Path(build).resolve()
        self.out = Path(out).resolve()
        shutil.rmtree(self.out, ignore_errors=True)
        (self.out / "game").mkdir(parents=True)
        shutil.copy2(Path(build) / "FaxanaduRecomp", self.out / "game")
        for folder in ("assets", "mods"):
            shutil.copytree(Path(build) / folder, self.out / "game" / folder)
        self.exe = self.out / "game" / "FaxanaduRecomp"
        self.env = dict(os.environ, **HIDDEN_ENV)
        self.log_path = self.out / "game.log"
        self.log = open(self.log_path, "w")
        self.game_args, self.realtime = list(game_args), realtime
        self.port, self.fixed_port = port, bool(port)
        self.proc, self.shots = None, []
        self.timeout = 30                  # seconds to wait for a reply

    @classmethod
    def from_args(cls, args):
        """From parse_args(): --build, --rom, --out, --game-arg, --realtime, --port."""
        return cls(args.build, args.rom, args.out, getattr(args, "game_arg", ()),
                   getattr(args, "realtime", False), getattr(args, "port", 0))

    def launch(self, *extra, at=None):
        """Start the game. With `at`, a fixture's name: load that saved game
        at once (it is made first if it is not cached), and return the frame
        it continues at."""
        global _game, _launched
        start_at = self.state(at) if at else None
        _launched = True
        options = [part for arg in self.game_args for part in arg.split("=", 1)] + list(extra)
        if not self.realtime:
            options += UNCAPPED
        for attempt in range(1 if self.fixed_port else LAUNCH_TRIES):
            self.port = reserve_port(self.port)
            start = self.log_path.stat().st_size
            self.proc = subprocess.Popen([str(self.exe), str(self.rom), "--hidden", "--tcp", str(self.port), *options],
                                         env=self.env, stdout=self.log, stderr=subprocess.STDOUT)
            _game = self
            problem = self._listening(start)
            if not problem:
                for _ in range(100):
                    try:
                        frame = self.frame()
                        break
                    except OSError:
                        time.sleep(0.1)
                else:
                    raise AssertionError("the game did not start")
                if start_at:
                    self.load(start_at)
                    frame = self.frame()
                return frame
            self._stop()
            if self.fixed_port:
                raise AssertionError(f"the game {problem} (see {self.log_path})")
            print(f"fax_e2e: the game {problem}; relaunching on another port", flush=True)
            release_port(self.port)
            self.port = 0
        raise AssertionError(f"the game did not start in {LAUNCH_TRIES} tries (see {self.log_path})")

    def _listening(self, start):
        """Wait for this game's TCP line in its log, from byte `start`: None once it
        listens on our port, else what went wrong."""
        ok = f"[cyc tcp] listening on 127.0.0.1:{self.port}\n"
        taken = f"[cyc tcp] cannot listen on port {self.port}\n"
        end = time.monotonic() + LISTEN_TIMEOUT
        while True:
            exited = self.proc.poll()
            with open(self.log_path, "rb") as f:
                f.seek(start)
                text = f.read().decode(errors="replace")
            if ok in text:
                return None
            if taken in text:
                return f"could not listen on port {self.port}"
            if exited is not None:
                return f"exited ({exited}) before listening on port {self.port}"
            if time.monotonic() > end:
                return f"did not listen on port {self.port} in {LISTEN_TIMEOUT} s"
            time.sleep(0.02)

    def _stop(self):
        if self.proc.poll() is None:
            self.proc.kill()
        self.proc.wait(10)

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

    def tcp(self, cmd, **kw):
        with socket.create_connection(("127.0.0.1", self.port), 5) as s:
            s.settimeout(self.timeout)
            f = s.makefile("rwb", buffering=0)
            f.write((json.dumps(dict(id=1, cmd=cmd, **kw)) + "\n").encode())
            reply = json.loads(f.readline())
        if not reply.get("ok"):
            raise RuntimeError(f"{cmd}: {reply}")
        return reply

    def batch(self, *commands):
        """Send (cmd, {args}) commands in one write and return their replies.
        The game reads every line it has and serves them all before its next
        frame, so they take effect together: a load_state then an action
        holds the button from the loaded state's first frame, frame-exact,
        however far the script lags."""
        with socket.create_connection(("127.0.0.1", self.port), 5) as s:
            s.settimeout(self.timeout)
            f = s.makefile("rwb", buffering=0)
            f.write(b"".join((json.dumps(dict(id=i + 1, cmd=cmd, **kw)) + "\n").encode()
                             for i, (cmd, kw) in enumerate(commands)))
            replies = [json.loads(f.readline()) for _ in commands]
        for (cmd, _), reply in zip(commands, replies):
            if not reply.get("ok"):
                raise RuntimeError(f"{cmd}: {reply}")
        return replies

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

    def shot(self, name):
        path = self.out / f"{len(self.shots):02d}-{name}.png"
        self.tcp("screenshot", layer="picture", path=str(path))
        self.shots.append(path.name)
        return path

    def load(self, path):
        """Load a save state."""
        self.tcp("load_state", path=str(path))

    def save(self, name):
        """Save a state to OUT/name.cycstate; its Path."""
        path = self.out / f"{name}.cycstate"
        self.tcp("save_state", path=str(path))
        return path

    def state(self, name):
        """The fixture `name` for this build and these game options: the
        cached save state, made first (with its parents) if it is not there.
        Each is made once, under a lock, however many tests ask at once."""
        tag, key = _fixture_key(self, name)
        path = FIXTURES / f"{name}{tag}-{key}.cycstate"
        if path.exists():
            return path
        FIXTURES.mkdir(parents=True, exist_ok=True)
        with open(path.with_suffix(".lock"), "w") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            if not path.exists():
                self._make(name, path)
                for old in FIXTURES.glob(f"{name}{tag}-*.cycstate"):     # the same fixture, made stale
                    if old != path and re.fullmatch(re.escape(f"{name}{tag}-") + "[0-9a-f]{16}", old.stem):
                        old.unlink(missing_ok=True)
                        old.with_suffix(".lock").unlink(missing_ok=True)
        return path

    def _make(self, name, path):
        """Play the fixture `name`'s recipe in a game of its own, and save it at `path`."""
        global _game
        parent, how = _recipes[name]
        start_at = self.state(parent) if parent else None
        before, began = _game, time.monotonic()
        work = FIXTURES / "work" / f"{path.stem}-{os.getpid()}"
        maker = type(self)(self.build, self.rom, work, self.game_args)
        try:
            maker.launch()
            if start_at:
                maker.load(start_at)
            how(maker)
            maker.tcp("save_state", path=str(path.with_suffix(".tmp")))
            os.replace(path.with_suffix(".tmp"), path)
        finally:
            maker.close()
            _game = before
        shutil.rmtree(work, ignore_errors=True)
        print(f"fixture: made {name} in {time.monotonic() - began:.1f} s", flush=True)

    def set_features(self, features):
        (self.out / "game" / "mods" / "state.toml").write_text(features_toml(features))

    def poke(self, writes, records=None):
        """Edit RAM (and mods' records) in the running game: save a state,
        edit it with fixture(), load it back."""
        path = self.out / "poke.cycstate"
        self.tcp("save_state", path=str(path))
        fixture(path, path, writes, records)
        self.tcp("load_state", path=str(path))


# Fixtures: the saved games tests start from

_recipes = {}                              # name: (parent fixture or None, how(game) plays to it)


def recipe(name, parent=None):
    """Register how the fixture `name` is made: a game launched (from power-on,
    or with `parent`'s state loaded) is handed to the decorated function, which
    plays to the spot; the state there is saved. A test then starts there with
    Game.launch(at=name). Save states hold CHR RAM copied from the ROM, so they
    are made from your ROM and cached in build-tests/fixtures, never committed."""
    def register(how):
        _recipes[name] = (parent, how)
        return how
    return register


_digests = {}


def _digest(path):
    """sha256 of a file, or of every file under a folder, by name and content."""
    path = Path(path)
    if path not in _digests:
        h = hashlib.sha256()
        for f in sorted(path.rglob("*")) if path.is_dir() else [path]:
            if f.is_file():
                h.update(str(f.relative_to(path) if path.is_dir() else f.name).encode())
                h.update(f.read_bytes())
        _digests[path] = h.hexdigest()
    return _digests[path]


def _fixture_key(game, name):
    """(tag, key) for the fixture `name` in `game`: the tag names the game
    options it was made with; the key changes with anything that could make
    the state stale: the ROM, the build's game and mods, the options, and the
    source files of the recipes from power-on to it (with the constants and
    helpers they use)."""
    if name not in _recipes:
        raise KeyError(f"no recipe for the fixture {name!r}")
    parts = [f"{zlib.crc32(game.image):08x}", _digest(game.build / "FaxanaduRecomp"), _digest(game.build / "mods"),
             " ".join(game.game_args)]
    chain = name
    while chain:
        parent, how = _recipes[chain]
        parts.append(f"{chain}\n{_digest(inspect.getsourcefile(how))}")
        chain = parent
    tag = "".join("-" + "".join(c if c.isalnum() else "-" for c in arg.lstrip("-")) for arg in game.game_args)
    return tag, hashlib.sha256("\n".join(parts).encode()).hexdigest()[:16]


@recipe("gate")
def _gate(g):
    """A new game (Start on the title: file 1, with Save files), outside
    Eolis's gate once the opening has played."""
    g.until("the title", lambda: g.frame() > 700)
    g.tcp("pad", buttons="start", frames=10)
    g.until("the Eolis gate", lambda: g.ram(0x24) == 0 and g.frame() > 1900)   # Area_CurrentArea


@recipe("eolis-town", parent="gate")
def _eolis_town(g):
    """Right through the hero's thoughts at the opening, through Eolis's
    gate, into the town (screen 1)."""
    x_pos = 0x9E                                                      # Player_PosX_Block
    for _ in range(200):                                              # the lines stop him; A goes on
        if g.ram(x_pos) >= 184:
            break
        x = g.ram(x_pos)
        g.tcp("action", name="right", frames=8)
        g.wait(8)
        if g.ram(x_pos) == x:
            g.tcp("action", name="a", frames=6)
            g.wait(26)
    g.tcp("action", name="up", frames=4)
    g.wait(14)
    g.until("Eolis town", lambda: (g.ram(0x24), g.ram(0x63)) == (0, 1), 600)   # Area_CurrentArea, _Screen
    g.wait(60)


@recipe("eolis-meat-shop", parent="eolis-town")
def _eolis_meat_shop(g):
    """Left to the meat shop's door (block column 3) and in."""
    for _ in range(200):
        if 41 <= g.ram(0x9E) <= 56:
            break
        g.tcp("action", name="left", frames=2)
        g.wait(2)
    g.tcp("action", name="up", frames=4)
    g.wait(14)
    g.until("the meat shop", lambda: g.ram(0x24) == 4)               # BUILDINGS
    g.wait(30)


@recipe("eolis-zozuras", parent="gate")
def _eolis_zozuras(g):
    """Through Eolis's gate (the intro's trigger dropped, the hero at the
    door, EOLIS_DOOR_LOCATIONS' $9C), left out of town to screen 4, as its
    two Zozuras crawl in (slots 6 and 7, the nearer in 7)."""
    screen, entities, zozura = 0x63, 0x2CC, 0x47                     # Area_CurrentScreen, CurrentSprites_Entities
    assert (g.ram(0x24), g.ram(screen), g.ram(entities + 7)) == (0, 0, 0x4F), "the intro's trigger at the gate"
    g.poke({entities + 7: 0xFF, 0x9E: 195})                           # SPRITE_TRIGGER_INTRO; Player_PosX_Block
    g.wait(10)
    g.tcp("action", name="up", frames=6)
    g.until("the town", lambda: g.ram(screen) == 1, 600)
    g.wait(30)
    for _ in range(60):
        if g.ram(screen) == 4:
            break
        g.tcp("action", name="left", frames=30)
        g.wait(31)
    g.until("the Zozuras", lambda: g.ram(screen) == 4 and tuple(g.ram(entities + 6, 2)) == (zozura, zozura), 300)


def tcp(cmd, **kw):
    return _current().tcp(cmd, **kw)


def batch(*commands):
    return _current().batch(*commands)


def frame():
    return _current().frame()


def wait(frames):
    _current().wait(frames)


def ram(addr, n=1):
    return _current().ram(addr, n)


def until(what, test, frames=1200):
    _current().until(what, test, frames)


def act(name, frames=6, after=8):
    tcp("action", name=name, frames=frames)
    wait(frames + after)


def typed():
    """The text box is waiting for a button: the message has typed out
    (MessageID clears) or reached a pause (TextBox_MessagePaused)."""
    message, _, paused = ram(MESSAGE_ID, 3)
    return message == 0 or paused != 0


# Save states, the mod selection, options

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


def fixture(source, target, writes, records=None):
    """Copy a save state, changing CPU RAM bytes (its HW chunk, offset 23) and,
    if given, bytes of mods' save-state records, by record name."""
    data = bytearray(Path(source).read_bytes())
    pos, size = next((p, n) for t, p, n in chunks(data) if t == b"HW  ")
    assert size == 2072, "Unqualified hardware ABI"
    for address, value in writes.items():
        data[pos + 23 + address] = value
    named = {name.encode(): edits for name, edits in (records or {}).items()}
    for tag, p, n in chunks(data):
        if tag == b"MOD ":
            for offset, value in named.get(bytes(data[p:p + 64]).rstrip(b"\0"), {}).items():
                data[p + 64 + offset] = value
    Path(target).write_bytes(data)


def features_toml(features):
    """mods/state.toml: every feature at its default but those in `features`,
    {(package_id, feature_id): enabled}."""
    return "format_version = 1\n" + "".join(
        f'\n[[feature]]\npackage_id = "{package}"\nid = "{feature}"\nenabled = {"true" if on else "false"}\n'
        for (package, feature), on in features.items())


def parser(doc, parts=()):
    """The common options; with `parts`, a test's independent parts, also
    --part (repeatable; every part if none) and --list-parts, which prints
    them, one a line, and exits (tools/test.sh runs each as its own game)."""
    if parts and "--list-parts" in sys.argv[1:]:
        sys.__stdout__.write("".join(f"{part}\n" for part in parts))
        sys.exit(0)
    ap = argparse.ArgumentParser(description=doc, formatter_class=argparse.RawDescriptionHelpFormatter)
    if parts:
        ap.add_argument("--part", action="append", choices=list(parts), help="run only this part (repeatable)")
        ap.add_argument("--list-parts", action="store_true", help="print the parts, one a line, and exit")
    for key in ("build", "rom", "out"):
        ap.add_argument("--" + key, type=Path, required=True)
    ap.add_argument("--game-arg", action="append", default=[],
                    help="extra game option, e.g. --game-arg=--world-view=seamless (repeatable)")
    ap.add_argument("--port", type=int, default=0, help="TCP port (default: a reserved free one)")
    ap.add_argument("--realtime", action="store_true", help="run at normal speed instead of uncapped")
    return ap


def parse_args(doc, argv=None, parts=()):
    """parser(doc, parts)'s options. With parts, args.wants(name) says
    whether this run plays that part; --list-parts prints them and exits."""
    return chosen(parser(doc, parts).parse_args(argv), parts)


def chosen(args, parts):
    """Give args from a parser with `parts` its wants(name)."""
    picked = set(getattr(args, "part", None) or parts)
    args.wants = lambda name: name in picked
    return args
