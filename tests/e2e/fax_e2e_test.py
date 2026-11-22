"""Check fax_e2e's port reservation, its relaunch on a taken port and its fixture keys.

    python3 tests/e2e/fax_e2e_test.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/e2e-helper

1. A reserved port is held against every other process until it is released.
2. A port held by a process that is killed is free again.
3. A game that cannot listen on its port (here, another socket is listening
   there) is relaunched on another, and no command reaches the other socket.
4. A fixture's key covers the test modules its recipe uses, not only the
   recipe's own file: ladders.py's Trunk $0C recipe walks with play.py's
   Game.settle(), so play.py is among its sources.
"""
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fax_e2e  # noqa: E402
from fax_e2e import Game, parse_args, release_port, reserve_port, tcp  # noqa: E402

HOLD = """
import sys, time
sys.path.insert(0, {here!r})
import fax_e2e
port = fax_e2e.reserve_port({port})
print(port, flush=True)
time.sleep(60)
"""


def other(port=0):
    """A second process that reserves `port` (any, for 0) and holds it: (process, its port or None)."""
    env = {k: v for k, v in os.environ.items() if k != "FAX_E2E_STAMP"}    # its line is the port alone
    child = subprocess.Popen([sys.executable, "-c", HOLD.format(here=str(Path(__file__).resolve().parent), port=port)],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env)
    line = child.stdout.readline().strip()
    if not line:
        child.wait(10)
        return child, None
    return child, int(line)


def main():
    args = parse_args(__doc__)

    # 1. Held against other processes until released.
    port = reserve_port()
    assert port in fax_e2e.PORTS, port
    assert (fax_e2e.LOCKS / f"{port}.lock").exists()
    child, got = other(port)
    assert got is None and "held by another end-to-end test" in child.stderr.read(), "a held port is refused"
    release_port(port)
    assert not (fax_e2e.LOCKS / f"{port}.lock").exists(), "releasing removes the lock file"
    child, got = other(port)
    assert got == port, "a released port can be taken"
    print(f"ok: port {port} is held against another process until it is released")

    # 2. A killed holder's port is free again.
    os.kill(child.pid, signal.SIGKILL)
    child.wait(10)
    assert (fax_e2e.LOCKS / f"{port}.lock").exists(), "the killed holder left its lock file"
    assert reserve_port(port) == port, "a dead process's lock is free"
    release_port(port)
    print(f"ok: the lock a killed process left on port {port} is free again")

    # 3. A taken port: relaunched on another, and the other listener hears nothing.
    squatter = socket.socket()
    squatter.bind(("127.0.0.1", 0))
    squatter.listen(8)
    squatter.settimeout(0)
    taken = squatter.getsockname()[1]
    game = Game(args.build, args.rom, args.out)
    try:
        game.port = reserve_port(taken)                 # as if the port had been taken after it was picked
        game.launch()
        assert game.port != taken, game.port
        assert tcp("ping")["frame"] >= 0
        log = game.log_path.read_text()
        assert f"cannot listen on port {taken}" in log and f"listening on 127.0.0.1:{game.port}" in log, log
        try:
            squatter.accept()
            raise AssertionError("a command went to the other listener")
        except BlockingIOError:
            pass
        assert taken not in fax_e2e._held and game.port in fax_e2e._held
        print(f"ok: the game could not listen on port {taken}, was relaunched on {game.port}, "
              "and nothing reached the other listener")
    finally:
        game.close()
        squatter.close()

    # 4. A fixture's key covers the modules its recipe uses (no game needed).
    tests = Path(__file__).resolve().parents[1]
    sys.path[:0] = [str(tests / "movement"), str(tests / "ladders")]
    import ladders                                        # registers "ladders-trunk-0c", with play.py's Game
    names = {p.relative_to(tests).as_posix() for p in fax_e2e._sources(ladders._trunk_0c)}
    assert {"ladders/ladders.py", "movement/play.py", "e2e/fax_e2e.py"} <= names, names
    print("ok: the ladders-trunk-0c fixture's key covers ladders.py, play.py (Game.settle()) and fax_e2e.py")


if __name__ == "__main__":
    sys.exit(main())
