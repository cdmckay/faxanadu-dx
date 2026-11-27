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
            game.launch(at="gate")       # a hidden window, uncapped, on a reserved port,
            until("the town", ...)       # started from a saved game (see Fixtures)
            game.shot("town")            # OUT/00-town.png
        except BaseException:
            if game.running():
                game.shot("failure")
            raise
        finally:
            game.close()

Never pick a port yourself: Game.launch() reserves one (see Ports). Start
from a fixture, not from power-on, unless the boot is what is checked.

Count game frames, never seconds: wait() and until() count the game's frames,
so a test runs the same however loaded the machine is. A command is served
on a frame boundary and the script can lag any number of frames behind the
game, so input that must land on a given frame goes in with the save state
it starts from, in one batch(), or runs headless from that state with --input
(tests/movement/play.py's headless()).

Game, one per test
    Game(build, rom, out, game_args=(), realtime=False, port=0), or Game.from_args(args)
        Checks the ROM is the original Faxanadu (USA), empties OUT, installs the
        build in OUT/game (install(): FaxanaduRecomp hard-linked, assets/ and
        mods/ copied, a config.ini at window scale 1, and LEARNED, the Wing
        Boots' and the Hour Glass's lessons already learned; sidecars such as
        faxanadu_gurus.txt and config.ini stay there) and opens OUT/game.log.
    .launch(*extra, at=None)
                      Start the game: FaxanaduRecomp ROM --hidden --tcp PORT, the
                      --game-arg options, then `extra`, then --uncapped
                      --exit-after 100000000 unless realtime, with the hidden-window
                      environment (.env). Waits for this game's own "listening on"
                      line, then returns its first frame. If it cannot listen or
                      exits first, it is relaunched on another port (3 tries).
                      Relaunching keeps the port. With `at`, a fixture's name, the
                      fixture is made first if need be, then loaded at once. A
                      game already running is an error: close() it first.
    .state(name)      The fixture `name` for this build and these options: its
                      cached save state's Path, made first if it is not there.
    .load(path) .save(name)
                      load_state; save_state to OUT/name.cycstate (its Path).
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
    .line             The one TCP connection to the game, kept open (opened by the
                      first command, closed by close()): a command a frame.

Functions, on the game launched last
    tcp(cmd, **kw)                  One command; its reply. RuntimeError unless ok.
    batch((cmd, {args}), ...)       Several commands in one write; their replies.
                                    The game serves them all before its next
                                    frame, so a load_state then an action holds
                                    the button from the state's first frame. One
                                    that ends with a load_state returns once two
                                    frames of the state have run.
    frame()                         The frame counter (ping).
    wait(frames)                    Until `frames` more frames have run: the game's
                                    wait command, which answers at the end of the
                                    last, so not a frame more.
    ram(addr, n=1)                  CPU RAM: an int for one byte, else bytes.
    until(what, test, frames=1200)  Poll `test` until it is true; AssertionError
                                    "timed out waiting for {what}" after `frames`.
    act(name, frames=6, after=8)    Hold an NES button (TCP action) for `frames`,
                                    then wait `frames + after`.
    typed()                         The text box waits for a button: MessageID
                                    clears, or TextBox_MessagePaused is set.
    conversation(cmd)               A conversation's state (cmd's reply) and typed(),
                                    one frame's, in one batch; a Guru's save
                                    filling HP and MP is no typed page.
    page_to(there, what, cmd)       A on each typed page until there(state) holds
                                    on a typed one; never A on that page. A is
                                    held while each page types.
    type_out(what, frames, pauses)  until(typed), with A held while the page types,
                                    from a frame where a new press cannot turn it;
                                    let go before it returns. pauses=False: only
                                    MessageID counts (a stale pause flag).
    page_through(what, done, frames, button)
                                    Until done(): `button` on each page the box
                                    waits on, and A held while each types.
    hold_a() / let_go(button="a")   Hold A until let go (HOLD_FRAMES at most); let
                                    go, once a frame has run with it up.
    hurry_page(pauses)              Hold A from the next frame if a new press there
                                    cannot turn the page (a save state of a frame
                                    that typed a character, loaded with A down):
                                    whether it did.
    box_open()                      A text box (or the Select menu) is open.
    shop_list(mode="buy", frames=1200)
                                    Up on a Tool or Key Seller, and through his
                                    conversation (Quick shops: the original's)
                                    to his "buy" or "sell" list, up and waiting
                                    for a button: its shops_state.
    shop_leave(frames=1200)         B in that list: the end, or, after a purchase
                                    or a sale, "Thank you for shopping." typed
                                    out and waiting for A: whether he thanked.
    guru_send(choose=True, frames=1200)
                                    At a Guru's choice (Save files), Down to "Send
                                    me to another Guru." and A: Guru travel's menu
                                    opens.
    accept_offer(frames=1200)       A on Guru travel's offer, typed out: its menu
                                    opens.
    travel_menu(frames=1200)        Until Guru travel's menu of towns (#206) is
                                    up and waits for a button: its travel_state
                                    ("menu": the temples, "shown": the rows the
                                    screen shows, "cursor").
    travel_to(temple, leave=True, frames=1200)
                                    In that menu: Down to `temple`'s row, then A,
                                    each pressed again while the menu still waits
                                    for it; returns once he is to go there, or
                                    with `leave` once A has ended the departure
                                    line, the journey to follow.
    menu_row(name, game=None)       The row of `name` ("WEAPON" ... "PLAYER",
                                    "QUESTS", "MAP") in the Select menu: the
                                    remaster's eight, QUESTS and MAP on top
                                    (#146), or with it off the original's six.
    dissolve(frames=600)            Until the Guru speaks after a death: a recipe
                                    that dies ends with it, so a test starts there.
    hold(button, done, frames=600)  Hold a button until done(), then let go at
                                    once (an action replaces the button's hold).
    step_toward(x_min, x_max, stop) One step of a walk to [x_min, x_max]: held
                                    until 8 pixels short, then taps of 2 frames.
    walk(button, stop)              Hold a direction until stop() or the hero
                                    stops moving (a wall, a counter, a person).
    title_menu(frames=2000)         Wait for Save files' menu on the title (NEW GAME
                                    and CONTINUE); its files_state.
    choose_file(new, file)          From the title's menu: NEW GAME (new) or
                                    CONTINUE, then file 1-3, saying yes to NEW
                                    GAME's "Erase file N?" if it asks. Returns
                                    once the file is in play. FILES_TOP,
                                    FILES_NEW, FILES_CONTINUE and FILES_CONFIRM
                                    are files_state's modes.

Save states and the mod selection
    chunks(data)                    A CYCSTATE's chunks: (tag, offset, size).
    fixture(source, target, writes, records=None)
                                    Copy a save state, changing CPU RAM
                                    ({address: byte}, in its HW chunk at offset 23)
                                    and bytes of mods' records ({record name:
                                    {offset: byte}}, e.g. "faxanadu.zone-names").
    quiet_neighbours(source)        fixture() records that empty every other
                                    room's sprite slots in Seamless's record:
                                    with Seams a neighbour's monsters come
                                    through the seams.
    features_toml(features)         state.toml text for {(package_id, feature_id): enabled}.
    parse_args(doc, parts=()) / parser(doc, parts=())
                                    The common options, with `doc` as the help. With
                                    a test's parts, --part (repeatable) and
                                    --list-parts too, and args.wants(name).

Fixtures: the saved games tests start from
    @recipe(name, parent=None)      Register how a fixture is made: a game of its
                                    own is launched (with `parent`'s state loaded),
                                    handed to the function, which plays to the spot,
                                    and the state there is saved. "gate" (a new
                                    game outside Eolis's gate), "eolis-town",
                                    "eolis-meat-shop", "eolis-zozuras" and
                                    "conflate-words" (dead outside the gate, with
                                    Conflate's temple his spawn point, as the Guru
                                    speaks) are here; tests add their own.

    Save states hold CHR RAM copied from the ROM, so they are made from your
    ROM and never committed. Each is cached in build-tests/fixtures as
    NAME[-options]-KEY.cycstate, made once under a lock however many tests ask
    at once. KEY covers the ROM, the build's game and mods, the game options and
    the source files of the recipes from power-on to it, with those of the
    test modules they use (a Game subclass's settle(), a shared walk), so any
    change to them makes it again (and drops the stale one). A recipe uses only its game and
    the functions above, never the test's own game.

Parts
    A long test is split into parts that start from fixtures and need nothing
    from each other; tools/test.sh runs each as its own game, at once (its
    table marks the test --parts). Without --part a test runs every part, in
    order, in one process.

Time
    A test that launched a game prints "time: W s, games G s CPU, script S s
    CPU" as it exits. With FAX_E2E_STAMP=1 each line it prints starts with the
    seconds since it started; with FAX_E2E_PROFILE=1 it also prints the lines
    of the test its frames went to.

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
if os.environ.get("FAX_E2E_FIXTURES"):        # another build's, kept apart (tools/test.sh --sanitize)
    FIXTURES = Path(os.environ["FAX_E2E_FIXTURES"])
LAUNCH_TRIES = 3
LISTEN_TIMEOUT = 60                        # seconds for the game to open its TCP port
PAGE_FRAMES = 4                            # a typed page read again this many frames on (page_to())
STOPPED_FRAMES = 6                         # frames without moving that walk() takes for a stop
LOADED_FRAMES = 2                          # frames a load_state that ends a batch lets run (batch())
WAIT_CHUNK = 600                           # frames a wait command asks for at most (well inside .timeout)
CONFIG = "[Display]\nWindowScale = 1\n"     # config.ini a test's game starts with; the game adds the rest
# faxanadu_recharge.txt a test's game starts with: the Wing Boots' and the Hour
# Glass's lessons learned, so their first Down+B shows no box, as every later
# one (#151). tests/recharge/recharging_items.py's "lessons" removes it.
LEARNED = ("faxanadu_recharge.txt", "recharge.boots_taught\nrecharge.glass_taught\n")
QUICKACK = getattr(socket, "TCP_QUICKACK", None)   # Linux
TEXTBOX_X = 0x208                          # TextBox_X, _Y, _Width, _Height: the box last opened or closed
TEXTBOX_CLOSED = 0x20C                     # TextBox_Closed: 0 while a box is open
ISCRIPT_PORTRAIT_ID = 0x3C7                # IScript_PortraitID: $FF once a conversation has ended
MESSAGE_ID = 0x213                         # MessageID, Message_ProcessedLength, TextBox_MessagePaused
TEXTBOX_TIMER = 0x21D                      # a character types on a frame that leaves it a multiple of 4
PLAYER_IS_DEAD = 0x438                     # PlayerIsDead: set it, and the main loop runs the death path
TEMPLE_SPAWN_POINT = 0x439                 # TempleSpawnPoint: the temple a death comes back at, 0-7
NEGATIVE_THOUGHTS = 0x24                   # the Guru's words after a death (message $24)
HOLD_FRAMES = 6000                         # frames hold_a() holds A for, unless let go first


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


class _Profile:
    """FAX_E2E_PROFILE=1: where a test's frames go. After each command the
    game's frame is read, and the frames since the last read are put down to
    the test's own line that sent it (the innermost outside this helper); the
    lines that cost the most are printed as the test exits. A load_state's
    frames go to the next command (a state carries its own count). The reads
    cost a frame here and there, so the totals run a little over."""

    def __init__(self):
        self.frames, self.last, self.busy = {}, {}, False

    def sample(self, game, loaded=False):
        if self.busy:
            return
        if loaded:                         # a state carries its own frame count: start again from it
            self.last.pop(id(game), None)
        self.busy = True
        try:
            now = game.line.send((("ping", {}),))[0].get("frame")
        finally:
            self.busy = False
        last = self.last.get(id(game))
        if last is not None and now is not None and now >= last:
            here = Path(__file__).resolve()
            site = next((f"{Path(f.filename).name}:{f.lineno}" for f in inspect.stack()[2:]
                         if Path(f.filename).resolve() != here), "?")
            self.frames[site] = self.frames.get(site, 0) + now - last
        self.last[id(game)] = now

    def report(self):
        total = sum(self.frames.values())
        if not total:
            return
        print(f"profile: {total} frames; by line:", flush=True)
        for site, n in sorted(self.frames.items(), key=lambda kv: -kv[1])[:25]:
            print(f"  {n:6d}  {site}", flush=True)


_profile = _Profile() if os.environ.get("FAX_E2E_PROFILE") else None
if _profile is not None:
    atexit.register(_profile.report)


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
        install(build, self.out / "game")
        self.exe = self.out / "game" / "FaxanaduRecomp"
        self.env = dict(os.environ, **HIDDEN_ENV)
        self.log_path = self.out / "game.log"
        self.log = open(self.log_path, "w")
        self.game_args, self.realtime = list(game_args), realtime
        self.port, self.fixed_port = port, bool(port)
        self.proc, self.shots = None, []
        self.timeout = 30                  # seconds to wait for a reply
        self.line = None                   # the TCP connection to the game, kept open (_Line)

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
        if self.running():
            raise RuntimeError("the game is already running: close() it first")
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
            self._hang_up()
            if _profile is not None:
                _profile.last.pop(id(self), None)
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
        self._hang_up()
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
        self._hang_up()
        self.proc.wait(10)
        self.proc = None

    def _hang_up(self):
        if self.line:
            self.line.close()
            self.line = None

    def running(self):
        return self.proc is not None and self.proc.poll() is None

    def tcp(self, cmd, **kw):
        return self.batch((cmd, kw))[0]

    def batch(self, *commands):
        """Send (cmd, {args}) commands in one write and return their replies.
        The game reads every line it has and serves them all before its next
        frame, so they take effect together: a load_state then an action
        holds the button from the loaded state's first frame, frame-exact,
        however far the script lags. A batch that ends with a load_state
        returns once LOADED_FRAMES frames of the state have run: a button
        first pressed in a loaded state's first two frames can go unseen
        (Up at a door does nothing), and nothing reads before a frame of it
        has run."""
        if self.line is None:
            self.line = _Line(self.port, self.timeout)
        self.line.timeout = self.timeout
        settle = commands[-1][0] == "load_state"
        try:
            replies = self.line.send(commands + ((("wait", dict(frames=LOADED_FRAMES)),) if settle else ()))
        except BaseException:
            self._hang_up()                # a reply may still be coming: never read it as another's
            raise
        replies = replies[:len(commands)]
        if _profile is not None and commands[-1][0] != "quit":
            _profile.sample(self, any(cmd == "load_state" for cmd, _ in commands))
        for (cmd, _), reply in zip(commands, replies):
            if not reply.get("ok"):
                raise RuntimeError(f"{cmd}: {reply}")
        return replies

    def frame(self):
        return self.tcp("ping")["frame"]

    def wait(self, frames):
        """Until `frames` more frames have run: the game answers at the end of
        the last one (its wait command), so not a frame more."""
        while frames > 0:
            self.tcp("wait", frames=min(frames, WAIT_CHUNK))
            frames -= WAIT_CHUNK

    def ram(self, addr, n=1):
        data = bytes.fromhex(self.tcp("read_ram", addr=addr, len=n)["hex"])
        return data[0] if n == 1 else data

    def until(self, what, test, frames=1200):
        end = self.frame() + frames
        while True:
            if test():
                return
            if self.frame() >= end:
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

    def act(self, name, frames=6, after=8):
        """Hold an NES button for `frames`, and wait `frames + after`, from the frame it goes down."""
        self.batch(("action", dict(name=name, frames=frames)), ("wait", dict(frames=frames + after)))

    def set_features(self, features):
        (self.out / "game" / "mods" / "state.toml").write_text(features_toml(features))

    def poke(self, writes, records=None):
        """Edit RAM (and mods' records) in the running game: save a state,
        edit it with fixture(), load it back."""
        path = self.out / "poke.cycstate"
        self.tcp("save_state", path=str(path))
        fixture(path, path, writes, records)
        self.tcp("load_state", path=str(path))


def install(build, folder):
    """The build's game in `folder`, for a test's own copy: FaxanaduRecomp
    hard-linked (one copy of the program in memory however many games run,
    which plays them about 10% faster than separate copies; a copy if the
    link fails), assets/ and mods/ copied, and a config.ini with the window at
    scale 1: the hidden window's scaling blit is a tenth of a frame at 3.
    Seamless's view is 424x240 at any window size, so it runs at scale 1 too.
    And LEARNED: the two timed items' lessons, said once ever, already said."""
    build, folder = Path(build), Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    try:
        os.link(build / "FaxanaduRecomp", folder / "FaxanaduRecomp")
    except OSError:
        shutil.copy2(build / "FaxanaduRecomp", folder)
    for sub in ("assets", "mods"):
        shutil.copytree(build / sub, folder / sub)
    (folder / "config.ini").write_text(CONFIG)
    (folder / LEARNED[0]).write_text(LEARNED[1])


class _Line:
    """The one TCP connection to a game, kept open: the game serves every
    line it has at its next frame boundary, so a command takes a frame
    rather than the two a new connection costs, and commands sent together
    are served at the same one. The game sends a reply and its newline apart,
    and Nagle's algorithm holds the newline until the first part is
    acknowledged, which a delayed ACK puts off by up to 40 ms: TCP_QUICKACK
    acknowledges at once, and where there is none, an empty line (which the
    game ignores) carries the acknowledgement. Each command has its own id,
    so a reply to one abandoned (a timeout) is never taken for another's."""

    def __init__(self, port, timeout):
        self.sock = socket.create_connection(("127.0.0.1", port), 5)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.timeout, self.next_id, self.buf = timeout, 1, b""

    def send(self, commands):
        first = self.next_id
        self.next_id += len(commands)
        self.sock.settimeout(self.timeout)
        self.sock.sendall(b"".join((json.dumps(dict(id=first + i, cmd=cmd, **kw)) + "\n").encode()
                                   for i, (cmd, kw) in enumerate(commands)))
        replies = []
        while len(replies) < len(commands):
            reply = json.loads(self._line())
            if reply.get("id") == first + len(replies):
                replies.append(reply)
        return replies

    def _line(self):
        while b"\n" not in self.buf:
            if QUICKACK is not None:
                self.sock.setsockopt(socket.IPPROTO_TCP, QUICKACK, 1)
            data = self.sock.recv(65536)
            if not data:
                raise ConnectionError("the game closed the connection")
            self.buf += data
            if QUICKACK is None and b"\n" not in self.buf:
                self.sock.sendall(b"\n")
        line, self.buf = self.buf.split(b"\n", 1)
        return line

    def close(self):
        self.sock.close()


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


def _sources(how):
    """The source files a recipe's state depends on: its own, with the
    constants and helpers defined there, and those of every test module,
    class or function its module uses (a Game subclass's settle(), a
    shared walk), wherever they are defined."""
    tests = REPO / "tests"
    files = {Path(inspect.getsourcefile(how)).resolve()}
    for value in list(how.__globals__.values()):
        if not (inspect.ismodule(value) or inspect.isclass(value) or inspect.isfunction(value)):
            continue
        try:
            source = inspect.getsourcefile(value)
        except TypeError:                                   # built in
            continue
        if source and Path(source).resolve().is_relative_to(tests):
            files.add(Path(source).resolve())
    return sorted(files)


def _fixture_key(game, name):
    """(tag, key) for the fixture `name` in `game`: the tag names the game
    options it was made with; the key changes with anything that could make
    the state stale: the ROM, the build's game and mods, the options, and the
    source files of the recipes from power-on to it, with those of the test
    modules they use (_sources())."""
    if name not in _recipes:
        raise KeyError(f"no recipe for the fixture {name!r}")
    parts = [f"{zlib.crc32(game.image):08x}", _digest(game.build / "FaxanaduRecomp"), _digest(game.build / "mods"),
             " ".join(game.game_args)]
    chain = name
    while chain:
        parent, how = _recipes[chain]
        parts.append(chain + "".join(f"\n{source.name} {_digest(source)}" for source in _sources(how)))
        chain = parent
    tag = "".join("-" + "".join(c if c.isalnum() else "-" for c in arg.lstrip("-")) for arg in game.game_args)
    return tag, hashlib.sha256("\n".join(parts).encode()).hexdigest()[:16]


@recipe("gate")
def _gate(g):
    """A new game (NEW GAME on the title, then file 1, with Save files),
    outside Eolis's gate once the opening has played."""
    g.until("the title", lambda: g.frame() > 700)
    g.act("start")                                                    # NEW GAME: nothing to continue
    g.until("NEW GAME's pick", lambda: g.tcp("files_state")["mode"] == FILES_NEW, 120)
    g.act("start")                                                    # file 1, empty: a new game at once
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


@recipe("conflate-words", parent="gate")
def _conflate_words(g):
    """Dead outside Eolis's gate, before any entrance, with Conflate's temple
    (temple 5) his spawn point: as the Guru speaks, the dissolve over. For a
    test that dies to get to Conflate's Guru, or reads the Guru's words."""
    g.poke({TEMPLE_SPAWN_POINT: 5, PLAYER_IS_DEAD: 1})
    dissolve()


@recipe("eolis-zozuras", parent="eolis-meat-shop")
def _eolis_zozuras(g):
    """On Eolis's screen 4 as its two Zozuras crawl in (slots 6 and 7, the
    nearer in 7, fresh at the screen's left, YX $A9 and $A4), the hero at its
    right facing them, on the ground. He walks out of the meat shop, its door
    edited to lead there (Area_SavedScreen 4, Player_SavedPos $9E), so the
    screen loads as a walk left onto it does in the original. The remaster
    plays in Seamless, which runs a screen's actors before the hero walks onto
    it, so a walk there would find them wherever they had crawled; the door's
    load is the same with the remaster on or off."""
    area, screen, entities, zozura = 0x24, 0x63, 0x2CC, 0x47          # Area_CurrentArea, _CurrentScreen, CurrentSprites_Entities
    g.poke({0x3D6: 4, 0x3D8: 0x9E})                                   # Area_SavedScreen, Player_SavedPos
    hold("right", lambda: g.ram(area) != 4)                           # BUILDINGS: out of the shop
    g.until("the Zozuras", lambda: (g.ram(area), g.ram(screen)) == (0, 4) and
            tuple(g.ram(entities + 6, 2)) == (zozura, zozura), 300)
    g.until("the screen to load", lambda: g.ram(0x0E) == 0 and g.ram(0x54) == 0xFF and g.ram(0x13) != 0, 300)
    for _ in range(10):                                               # a building's way out faces him right
        if not g.ram(0xA4) & 0x40:                                    # Player_Flags: facing right
            break
        g.tcp("action", name="left", frames=2)
        g.wait(4)
    assert not g.ram(0xA4) & 0x40, "facing the Zozuras"


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
    _current().act(name, frames, after)


FILES_TOP, FILES_NEW, FILES_CONTINUE, FILES_CONFIRM = 0, 1, 2, 3   # fax_files.c's Mode
GURU_MENU, GURU_FILLING = 1, 4                                     # fax_files.c's GuruPhase: his choice; a save's fill
TRAVEL_OFFERING, TRAVEL_MENU_PHASES = 2, (7, 8)                    # fax_travel.c's Phase: OFFER; OPENING, MENU
TRAVEL_OFFER, TRAVEL_SLOT_ID = 1, 0xC3                             # its offer's line; the message its lines are
MENU_CURSOR = 0x21E                                                # Menu_CursorPos: a menu's or a list's cursor


def title_menu(frames=2000):
    """Wait for Save files' menu on the title; its state (files_state)."""
    until("the title's menu", lambda: tcp("files_state")["shown"], frames)
    return tcp("files_state")


def choose_file(new, file):
    """From the title's menu (or one of its picks): NEW GAME (`new`) or
    CONTINUE, then file `file` (1-3), and A on NEW GAME's "Erase file N?" if
    the file is used. Returns once the file is in play."""
    for _ in range(3):                                                # B: back to the two choices
        if tcp("files_state")["mode"] == FILES_TOP:
            break
        act("b")
    for _ in range(2):
        if tcp("files_state")["cursor"] == (0 if new else 1):
            break
        act("select")
    assert tcp("files_state")["cursor"] == (0 if new else 1), "CONTINUE is grey: no file to continue"
    act("a")
    pick = FILES_NEW if new else FILES_CONTINUE
    until("the pick", lambda: tcp("files_state")["mode"] == pick, 60)
    for _ in range(3):
        if tcp("files_state")["cursor"] == file - 1:
            break
        act("down")
    assert tcp("files_state")["cursor"] == file - 1, f"file {file} cannot be chosen"
    act("a")
    if new and tcp("files_state")["mode"] == FILES_CONFIRM:
        act("a")
    until(f"file {file} in play", lambda: tcp("files_state")["file"] == file, 300)


def hold_a():
    """Hold A until let_go(), or HOLD_FRAMES frames: a new press if it was up.
    Hold A to speed up text (the remaster's) types a character every frame
    while A is held, not every 4th, and a held A never turns a page or answers
    a question: those take a new press."""
    tcp("action", name="a", frames=HOLD_FRAMES)


def let_go(button="a"):
    """Let go of a held button, and return once a frame has run with it up,
    so the next press of it is a new one."""
    act(button, 1, 0)


def _page(save=None, pauses=True):
    """The text box at one frame, and that frame: (waiting for a button as
    typed() reads it, TextBox_Timer, the frame). With `save`, a path, a save
    state of that frame is written too. Without `pauses`, only MessageID
    counts, for a line with no pause where TextBox_MessagePaused can be
    stale (from the intro)."""
    commands = ((("save_state", dict(path=str(save))),) if save else ()) + (
        ("read_ram", dict(addr=MESSAGE_ID, len=TEXTBOX_TIMER - MESSAGE_ID + 1)), ("ping", {}))
    *_, box, at = batch(*commands)
    data = bytes.fromhex(box["hex"])
    return data[0] == 0 or pauses and data[2] != 0, data[-1], at["frame"]


def hurry_page(pauses=True):
    """Hold A from the next frame, if a new press there cannot turn the page:
    whether it did. A new press turns a page on the frame it types out (its
    last character, the $FC pause or the end, on a frame that leaves
    TextBox_Timer a multiple of 4), or any frame after. So only while the page
    types, and only from a frame that typed a character: the next three type
    none, at the game's pace, and from the second A is held, not new, and
    types one a frame. The state of that frame is saved with the read, and
    loaded again with A down, which then goes down on the very next frame
    (batch()); on another frame, nothing is pressed: try again."""
    g = _current()
    path = g.out / "hurry.cycstate"
    waiting, timer, _ = _page(save=path, pauses=pauses)
    if waiting or timer & 3:
        return False
    g.batch(("load_state", dict(path=str(path))), ("action", dict(name="a", frames=HOLD_FRAMES)))
    return True


def type_out(what="the page to type out", frames=1200, pauses=True):
    """until() the text box waits for a button (typed()), holding A while the
    page types, from a frame where a new press cannot turn it (hurry_page()),
    then let go: the page types 4x and stops at its end, as Hold A does, and
    it may be a question. For a check, a screenshot or a press on a page.
    Without `pauses`, as _page()."""
    end, held = None, False
    try:
        while True:
            waiting, _, at = _page(pauses=pauses)
            end = at + frames if end is None else end
            if waiting:
                return
            if at >= end:
                raise AssertionError(f"timed out waiting for {what}")
            if not held:
                held = hurry_page(pauses)
    finally:
        if held:
            let_go()


def box_open():
    """A text box (or the Select menu) is open: TextBox_Closed is 0."""
    return ram(TEXTBOX_CLOSED) == 0


def _shop():
    """shops_state and MessageID, read in one batch: the same frame's."""
    state, box = batch(("shops_state", {}), ("read_ram", dict(addr=MESSAGE_ID, len=1)))
    return state, int(box["hex"][:2], 16)


SHOP_LIST_BOX = bytes((10, 8, 20, 20, 0))  # TextBox_X to TextBox_Closed: Shop_Draw's list, open
SHOP_PRESS_FRAMES = 60                     # a press a shop takes shows within these frames of the act


def _list_box():
    """Shop_Draw has opened a shop's list: the box last opened is the list's,
    and open. It is set as the list begins to draw, the frame A is taken."""
    return ram(TEXTBOX_X, 5) == SHOP_LIST_BOX


def _press_until(button, what, done, ready, frames=1200):
    """Press `button` until `done()`, read after each press, and again only
    while the game still waits for it there (`ready()`), so a press never
    lands on the step after: in a list, where A would buy. A press counts
    only on the frame it goes down, and a shop step can miss that frame (one
    that has just begun reading the pad, a lag frame), so one press is not
    always seen (#182); one seen shows within SHOP_PRESS_FRAMES. Bounded by
    `frames`."""
    end, presses = frame() + frames, 0
    while True:
        if presses:
            print(f"fax_e2e: {button} again for {what}: the game did not take it", flush=True)
        act(button, 4, 6)
        presses += 1
        try:
            until(what, done, SHOP_PRESS_FRAMES)
            return
        except AssertionError:
            pass
        if frame() >= end:
            raise AssertionError(f"timed out waiting for {what}")
        if not ready():                                 # moved on, but not there yet: no more presses
            until(what, done, max(end - frame(), 1))
            return


def shop_list(mode="buy", frames=1200):
    """Up on a Tool or Key Seller, standing at him, and through his
    conversation, the original's with the remaster on (Quick shops,
    fax_shops.c), to his `mode` list ("buy" or "sell"): A on his greeting,
    the Buy/Sell menu's choice (Down first for "sell"), A on the list's line.
    Returns once the list is on screen and waits for a button, with its
    shops_state. Each line types with A held (type_out()). Every wait is on
    the game: the conversation's start by shops_state's count of them, each
    step by its place in the script, read with MessageID in one batch. A
    press counts only on the frame it goes down, so each is pressed again if
    the game has not taken it and still waits for it: Up if he does not
    answer, and the menu's and each line's presses (_press_until())."""
    before = tcp("shops_state")["opened"]
    for _ in range(5):
        act("up", 4, 10)
        try:
            until("the shopkeeper's greeting", lambda: tcp("shops_state")["opened"] > before, 120)
            break
        except AssertionError:
            pass
    else:
        raise AssertionError("the shopkeeper does not answer")
    type_out("the greeting", frames, pauses=False)
    _press_until("a", "the Buy/Sell menu", lambda: _shop()[0]["step"] != "greeting",
                 lambda: (lambda s, m: s["step"] == "greeting" and m == 0)(*_shop()), frames)
    until("the Buy/Sell menu's line", lambda: (lambda s, m: s["step"] == "menu" and m == 0)(*_shop()), frames)
    in_menu = lambda: tcp("shops_state")["step"] == "menu"   # noqa: E731
    if mode == "sell":
        _press_until("down", "the cursor on Sell", lambda: tcp("shops_state")["cursor"] == 1, in_menu, frames)
    _press_until("a", f"the {mode} list's line", lambda: _shop()[0]["step"] == mode, in_menu, frames)
    type_out(f"the {mode} list's line", frames, pauses=False)
    _press_until("a", f"the {mode} list", _list_box,
                 lambda: (lambda s, m: s["step"] == mode and m == 0)(*_shop()) and not _list_box(), frames)
    until(f"the {mode} list", lambda: (lambda s: s["shown"] == mode and s["list_up"])(tcp("shops_state")), frames)
    waits = tcp("shops_state")["waits"]
    until("the list to wait for a button", lambda: tcp("shops_state")["waits"] > waits, 300)
    return tcp("shops_state")


def shop_leave(frames=1200):
    """B in a Tool or Key Seller's list (Quick shops). With nothing bought or
    sold this visit the conversation ends there, as the original's B: returns
    False once it has. Otherwise he says "Thank you for shopping." first:
    returns True once it has typed out (with A held), waiting for A, which
    goes on to the end or to a line a feature adds there (Block's). B is
    pressed again while the list still waits for it (_press_until())."""
    before = tcp("shops_state")["thanked"]
    _press_until("b", "B to leave the list",
                 lambda: tcp("shops_state")["thanked"] > before or ram(ISCRIPT_PORTRAIT_ID) == 0xFF,
                 lambda: (lambda s: s["list_up"] and not s["leaving"])(tcp("shops_state")), frames)
    if tcp("shops_state")["thanked"] == before:
        until("the conversation's end", lambda: ram(TEXTBOX_CLOSED) != 0, frames)
        return False
    until("his thanks", lambda: tcp("shops_state")["step"] == "thanks", frames)
    type_out("his thanks", frames, pauses=False)
    return True


def guru_send(choose=True, frames=1200):
    """At a Guru's choice (Save files' Buy/Sell menu: "Remember my journey."
    or "Send me to another Guru."), typed out: Down to the second, then
    (`choose`) A, which opens Guru travel's menu of towns at once (#206).
    Each press again while the choice still waits for it. Returns once the
    menu is opening, which travel_menu() waits for, or without `choose` once
    the cursor is on the second."""
    choice = lambda: tcp("files_state")["guru"] == GURU_MENU   # noqa: E731
    _press_until("down", "the cursor on Send me to another Guru.", lambda: ram(MENU_CURSOR) == 1, choice, frames)
    if choose:
        _press_until("a", "Guru travel's menu", lambda: tcp("travel_state")["phase"] in TRAVEL_MENU_PHASES,
                     lambda: choice() and ram(MENU_CURSOR) == 1, frames)


def accept_offer(frames=1200):
    """A on Guru travel's offer ("Shall I send you to another Guru?"), typed
    out (page_to() stops on it), until its menu of towns opens; again while
    the question still waits for it."""
    asking = lambda: (lambda s: s["phase"] == TRAVEL_OFFERING and s["text"] == TRAVEL_OFFER)(tcp("travel_state"))  # noqa: E731
    _press_until("a", "Guru travel's menu", lambda: tcp("travel_state")["phase"] in TRAVEL_MENU_PHASES, asking, frames)


def travel_menu(frames=1200):
    """Until Guru travel's menu of towns (#206) is up and waits for a button,
    and its travel_state: "menu", the temples a row each; "shown", the rows
    as the screen shows them; "cursor", the row the cursor is on. It is up
    ("menu_up") once its box is the box open and its rows are drawn and sent
    to the screen (the PPU buffer empty), which is when its wait reads the
    pad: A or B there is new only on the frame it goes down."""
    until("Guru travel's menu", lambda: tcp("travel_state")["menu_up"], frames)
    return tcp("travel_state")


def travel_to(temple, leave=True, frames=1200):
    """In Guru travel's menu of towns, up: Down until the cursor is on
    `temple`'s row (Menu_UpdateAndDraw wraps), then A, which sends the hero
    there. A press counts only on the frame it goes down, so each is pressed
    again while the menu still waits for it (_press_until()). Returns once he
    is to go (travel_state's "dest"), or with `leave`, once the Guru has said
    "Close your eyes and clear your mind." and A has ended the conversation:
    the journey follows. The line waits for the dialogue box, opened again
    over the menu, and MessageID reads 0 until it begins, so a press before
    then would be lost."""
    state = travel_menu(frames)
    row = state["menu"].index(temple)
    up = lambda: tcp("travel_state")["menu_up"]   # noqa: E731
    for _ in range(len(state["menu"])):
        cursor = tcp("travel_state")["cursor"]
        if cursor == row:
            break
        _press_until("down", f"the cursor off row {cursor}", lambda: tcp("travel_state")["cursor"] != cursor, up,
                     frames)
    assert tcp("travel_state")["cursor"] == row, f"the cursor never reached {temple}'s row"
    _press_until("a", f"the journey to temple {temple}", lambda: tcp("travel_state")["dest"] == temple, up, frames)
    state = tcp("travel_state")
    if leave:
        until("the departure", lambda: ram(MESSAGE_ID) == TRAVEL_SLOT_ID, frames)
        page_through("the conversation's end", lambda: ram(ISCRIPT_PORTRAIT_ID) == 0xFF, frames)
    return state


SELECT_MENU = ("QUESTS", "MAP", "WEAPON", "ARMOR", "SHIELD", "MAGIC", "ITEM", "PLAYER")   # the remaster's (#146)


def menu_row(name, game=None):
    """The row of `name` in the Select menu: the remaster's eight, QUESTS and
    MAP on top (fax_menu.c, on with the remaster), or the original's six from
    WEAPON. Asked of the game (`game`, or the one launched last), not of the
    menu on screen, so it holds before the menu has drawn its rows."""
    rows = SELECT_MENU if (game or _current()).tcp("select_menu_state")["enabled"] else SELECT_MENU[2:]
    return rows.index(name)


def page_through(what, done, frames=3000, button="a"):
    """Until done(): `button` on each page the text box waits on, and A held
    while each types (Hold A types 4x). For a conversation that takes the same
    button on every page: the Guru's words after a death, a line to close. With
    A, the A that turns a page is held on through the next, and let go and
    pressed again on the next that waits; with another button, A is held from
    a frame where a new press cannot turn a page (hurry_page()), and never let
    go between pages. A is let go at the end."""
    end = frame() + frames
    held = False
    try:
        while not done():
            data, at = batch(("read_ram", dict(addr=TEXTBOX_CLOSED, len=MESSAGE_ID + 3 - TEXTBOX_CLOSED)), ("ping", {}))
            if at["frame"] >= end:
                raise AssertionError(f"timed out paging to {what}")
            box = bytes.fromhex(data["hex"])
            closed, message, paused = box[0], box[MESSAGE_ID - TEXTBOX_CLOSED], box[-1]
            if closed:
                continue
            if message == 0 or paused:
                if button == "a":
                    if held:
                        let_go()
                    hold_a()
                    held = True
                else:
                    act(button, 2, 0)
                wait(PAGE_FRAMES)
            elif not held:
                held = hurry_page() if button != "a" else (hold_a() or True)
    finally:
        if held:
            let_go()


def dissolve(frames=600):
    """Until the Guru speaks after a death (MessageID $24), the dissolve over:
    for a recipe that dies, where the death is not what is checked, so a test
    starts after it. Set PlayerIsDead (and what the death needs) first."""
    until("the Guru's words", lambda: ram(MESSAGE_ID) == NEGATIVE_THOUGHTS, frames)


def hold(button, done, frames=600, renew=8):
    """Hold `button` (a TCP action) until done() is true, or `frames` frames
    have run, and let go at once: an action replaces the button's hold, so a
    1-frame one ends it, and it returns once that frame has run (Up at a door
    with Right still down goes nowhere). The hold is renewed before it lapses,
    so the button stays down. Whether done() came true."""
    end = frame() + frames
    while True:
        tcp("action", name=button, frames=renew + 4)
        lapse = min(frame() + renew, end)
        while frame() < lapse:
            if done():
                act(button, 1, 0)          # let go, and wait out the frame it is still down
                return True
        if frame() >= end:
            act(button, 1, 0)
            return done()


def step_toward(x_min, x_max, stop=lambda: False, x_addr=0x9E):
    """One step of a walk to [x_min, x_max] (X in Player_PosX_Block): held,
    accelerating to 1.5 pixels a frame, until within 8 pixels of it (or
    stop()), when it is further; else a tap of 2 frames, as the walks always
    made, so the last steps land in it the same way."""
    x = ram(x_addr)
    button = "right" if x < x_min else "left"
    if x_min - 8 <= x <= x_max + 8:
        act(button, 2, 0)
    else:
        hold(button, lambda: stop() or x_min - 8 <= ram(x_addr) <= x_max + 8)


def walk(button, stop=lambda: False, frames=600, x_addr=0x9E):
    """Hold `button` until stop() or the hero stops: his X (Player_PosX_Block)
    the same for STOPPED_FRAMES frames, against a wall, a counter or a
    person. Held, he walks at his full 1.5 pixels a frame. Whether stop()
    came true."""
    reads = []                             # (frame, X)

    def done():
        if stop():
            return True
        x, at = batch(("read_ram", dict(addr=x_addr, len=1)), ("ping", {}))
        reads.append((at["frame"], x["hex"]))
        since = [i for i, (f, _) in enumerate(reads) if f <= at["frame"] - STOPPED_FRAMES]
        return bool(since) and len({x for _, x in reads[since[-1]:]}) == 1
    hold(button, done, frames)
    return stop()


def conversation(cmd="travel_state"):
    """A conversation's state (the TCP command `cmd`'s reply, `id` dropped)
    and typed(), read in one batch: the same frame's, so a press sent on what
    they show lands on that page, never the next. A Guru's save filling HP and
    MP (#205) waits for no button, though the box shows a typed page (his
    choice), so it is no typed page."""
    reply, box, files = batch((cmd, {}), ("read_ram", dict(addr=MESSAGE_ID, len=3)), ("files_state", {}))
    message, _, paused = bytes.fromhex(box["hex"])
    reply.pop("id", None)
    return reply, (message == 0 or paused != 0) and files["guru"] != GURU_FILLING


def page_to(there, what, cmd="travel_state", tries=12, frames=1200):
    """Press A on each typed-out page until there(state) holds for a page
    that has typed out (`cmd`'s reply, conversation()), and return that
    state; never A on that page, which may be a question. A page counts as
    typed once two reads PAGE_FRAMES apart agree: as a line changes, the
    text box can still show the last page's pause for a frame or two.
    A is held while each page types (Hold A types 4x): the A that turns a
    page is held on through the next, the first is pressed only where it
    cannot turn a page (hurry_page()), and A is let go before returning."""
    held = False
    try:
        for _ in range(tries):
            end = frame() + frames
            while True:
                state, done = conversation(cmd)
                if done:
                    wait(PAGE_FRAMES)
                    again, still = conversation(cmd)
                    if still and again == state:
                        break
                elif frame() >= end:
                    raise AssertionError(f"timed out waiting for a page to type out ({what})")
                elif not held:
                    held = hurry_page()
            if there(state):
                return state
            if held:
                let_go()
            hold_a()                       # a new press: the page turns, and A stays down through the next
            held = True
        raise AssertionError(f"never reached {what}: {state}")
    finally:
        if held:
            let_go()


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


def quiet_neighbours(source):
    """Record edits for fixture() that empty every other room's sprite slots
    in Seamless's record (faxanadu.world: a 32-byte header, then 83 rooms of
    the same size, each with its RAM first), so only the live room's actors
    are left. With Seams (#166) a neighbouring room's monsters, once seen, come
    through the seams, so a test that sends a screen's actors away sends its
    neighbours' too."""
    data = Path(source).read_bytes()
    live = next(data[p + 23 + 0x63] for t, p, n in chunks(data) if t == b"HW  ")   # Area_CurrentScreen
    for tag, p, n in chunks(data):
        if tag == b"MOD " and bytes(data[p:p + 64]).rstrip(b"\0") == b"faxanadu.world":
            size = (n - 64 - 32) // 83
            return {"faxanadu.world": {32 + room * size + 0x2CC + slot: 0xFF
                                       for room in range(83) if room != live for slot in range(8)}}
    return {}


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
                    help="extra game option, e.g. --game-arg=--text-overrides=FILE (repeatable)")
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
