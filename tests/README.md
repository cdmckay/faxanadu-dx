# Tests

`tools/test.sh` runs every check: allocations, the C unit tests, the game
build, then every end-to-end test at once. `tools/test.sh --quick` skips the
end-to-end runs. While developing, run `--quick` and your own end-to-end test
directly; run the full suite before a PR.

## End-to-end tests and `e2e/fax_e2e.py`

An end-to-end test plays a feature in a hidden window and drives it over TCP.
Everything the tests share is in [e2e/fax_e2e.py](e2e/fax_e2e.py), whose
docstring lists its API: `Game` (copy the build, launch, close, screenshot,
mod selection), the TCP client (`tcp`, `frame`, `wait`, `ram`, `until`, `act`,
`typed`), save-state fixtures (`chunks`, `fixture`) and the common options
(`parse_args`). A test keeps only its own logic.

```python
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
from fax_e2e import Game, fixture, frame, parse_args, ram, tcp, until  # noqa: E402

def main():
    args = parse_args(__doc__)
    game = Game.from_args(args)
    out = game.out
    try:
        game.launch()
        ...
        game.set_features({(PACKAGE, FEATURE): False})     # mods/state.toml
        game.close()
        game.launch()
        ...
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()
```

The functions talk to the game launched last. `tests/movement/play.py`
subclasses `Game` for the movement tests' headless runs.

### Ports

Never pick a port in a test. `Game.launch()` reserves one with
`reserve_port()`, which:

- takes an exclusive `flock` on `<port>.lock` in
  `$XDG_RUNTIME_DIR/faxanadu-e2e-ports` (or the temp dir), shared by every
  test in every suite on the machine, and holds it until the test exits, so
  relaunches keep the port. The kernel drops the lock if the test is killed;
- draws from 20000-32767, below the ephemeral ports that `bind(0)` and
  outgoing connections get (32768+ on Linux, 49152+ on macOS), and skips a
  port anything is bound to.

The game keeps running when it cannot listen, so `launch()` waits for its own
`[cyc tcp] listening on 127.0.0.1:N` line in `game.log` before it sends a
command. If the game reports `cannot listen` or exits first, it is relaunched
on another port, and the test prints `fax_e2e: ... relaunching on another
port`. `e2e/fax_e2e_test.py` checks all of this.

### Moving an older test onto the helper

1. Delete its copies of `tcp`, `free_port`, `frame`, `wait`, `ram`, `until`,
   `act`, `typed`, `chunks` and `fixture`, the `PORT` global, the argument
   parsing, the ROM check, the copy of the build, `env`, `log`, and its
   `launch`, `close` and `shot` closures. Keep any that differ (the keys test
   keeps its own `typed`).
2. Add the `sys.path` line and import what it uses from `fax_e2e`.
3. In `main`: `args = parse_args(__doc__)`, `game = Game.from_args(args)`,
   `out = game.out`; `game.image` is the ROM's bytes.
4. Call `game.launch(*extra)`, `game.close()`, `game.shot(name)`, and
   `game.set_features(...)` for `state.toml`; print `game.shots`; guard the
   failure screenshot with `game.running()`.
5. Zone names' record: `fixture(src, dst, writes, records={"faxanadu.zone-names": {5: area, 6: screen}})`.
