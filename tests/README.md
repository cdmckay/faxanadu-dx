# Tests

`tools/test.sh` runs every check: allocations, the C unit tests, the game
build, then every end-to-end test. `tools/test.sh --quick` skips the
end-to-end runs. While developing, run `--quick` and your own end-to-end test
directly; run the full suite before a PR. The end-to-end part takes a
machine-wide `flock` (`$XDG_RUNTIME_DIR/faxanadu-remastered-e2e.lock`), so a
second checkout's suite waits for the first's games to finish.

The end-to-end runs (a test, or each part of one) go through a queue: at most
`E2E_JOBS` play at once (default: one a hardware thread; `E2E_JOBS=0` for no
limit), the longest first by how long each took last time
(`build-tests/e2e-times.txt`). The suite ends with each run's finish and its
time. A game runs uncapped and takes a whole thread from launch to close, so
the suite is bound by the frames all its games play: a run that waits, walks
or reads more than its check needs makes the whole suite later.

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
        game.set_features({("faxanadu.remastered", "remastered"): False})   # the original game
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

The functions talk to the game launched last. `tests/movement/play.py` and
`tests/block/harness.py` subclass `Game` with what their tests add.

### Fixtures: start where the check is

Booting to a playable hero takes about 1,900 frames (the title, Start, the
opening), so a test starts from a saved game instead: `game.launch(at="gate")`
loads the fixture `gate` as the window opens. A fixture is made from your ROM
by a recipe, once, and cached in `build-tests/fixtures`:

```python
@fax_e2e.recipe("respawn-temple", parent="gate")
def _respawn_temple(g):
    """The hero dead outside Eolis's gate: back in Eolis's temple."""
    g.poke({PLAYER_IS_DEAD: 1})
    ...                                   # play to the spot with g and the module's functions
```

The state is saved when the recipe returns. It is keyed on the ROM, the
build's game and mods, the game options (Seamless has its own) and the source
files of its recipes, so changing any of them makes it again. Several tests
asking at once wait for one to make it. Never commit a save state: it holds
CHR RAM copied from the ROM. Ask for a fixture (`game.state(name)`) before the
test's own game launches: an uncapped game burns a thread while it waits.

### Parts

A test that takes much longer than the others splits into parts, each
starting from a fixture and needing nothing from the others:

```python
PARTS = ("eolis", "trunk", "tower-castle")
args = parse_args(__doc__, parts=PARTS)
...
if args.wants("trunk"):
    game.launch(at="eolis-meat-shop")
    ...
```

`--list-parts` prints them, `--part NAME` (repeatable) runs some, and without
it a test runs every part, in order. Mark it `--parts` in `tools/test.sh`'s
table and the suite runs each as its own game. A check that compared with an
earlier part's numbers (a banner count, a respawn count) counts from its own
part's start.

### Waiting no longer than the check

- Walk with `step_toward()` (held until a few pixels short, then taps) and
  walk out of a building with `hold("right", ...)`: tapping a direction for 2
  frames keeps the hero at his slowest speed.
- In a loop that presses A on every typed page, wait with `hurry()` instead
  of `wait()`: the remaster's Hold A types 4x, and the A that turns the page is
  still a new press. Not where a page must stop or B answers.
- Run independent headless runs at once with `play.py`'s `headless_all()`.

### Frames, not seconds

The script can lag any number of frames behind the game when the machine is
loaded, so a test waits on the game: `wait()` and `until()` count its frames.
Never count on two TCP commands landing a few frames apart. When a button
must go down on a given frame, make a save state just before it (headless,
`--save-state F:PATH`; `play.py`'s `headless(..., save=...)`) and send the
load and the press together:

```python
batch(("load_state", dict(path=str(state))), ("action", dict(name="a", frames=2)))
```

The game serves every line it has before its next frame, so A goes down on the
state's first frame. Anything needing more than one timed press runs headless
from a state with `--input`. `game.poke(writes)` edits RAM in the running game
(save, `fixture()`, load), for example to clear a screen's enemies.

### Ports

Never pick a port in a test. The suite lock keeps two suites apart, but one
suite starts every game at once, and tests run directly alongside a suite, so
`Game.launch()` reserves a port with `reserve_port()`, which:

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
4. Call `game.launch(*extra)` (or `game.launch(at=fixture)`), `game.close()`,
   `game.shot(name)`, and `game.set_features(...)` for `state.toml`; print
   `game.shots`; guard the failure screenshot with `game.running()`.
5. Zone names' record: `fixture(src, dst, writes, records={"faxanadu.zone-names": {5: area, 6: screen}})`.

## The README's screenshots

A feature's picture in the README is a scene in
[../tools/readme_screenshots.py](../tools/readme_screenshots.py), played on this
helper: usually its end-to-end test's setup, cut down to the moment worth
showing. `python3 tools/readme_screenshots.py --only <scene>` writes
`docs/screenshots/<scene>.png`. Its `Game.replay` plays a moment headless from
a save state, frame exact, with the remaster's drawing in the picture
(`--present-out`), and `Game.shot_lit` waits out a text box's blinking marks.
