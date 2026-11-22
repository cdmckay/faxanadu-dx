# Tests

`tools/test.sh` runs every check: allocations, the C unit tests, the game
build, then every end-to-end test. `tools/test.sh --quick` skips the
end-to-end runs. While developing, run `--quick` and your own end-to-end test
directly; run the full suite before a PR. The end-to-end part takes a
machine-wide `flock` (`$XDG_RUNTIME_DIR/faxanadu-remastered-e2e.lock`), so a
second checkout's suite waits for the first's games to finish.

The end-to-end runs (a test, or each part of one) go through a queue: at most
`E2E_JOBS` play at once (default: one a hardware thread; `E2E_JOBS=0` for no
limit), the longest first by the CPU each used last time
(`build-tests/e2e-times.txt`), which, unlike its wall time, does not depend on
how busy the machine was. Meanwhile the allocation checks, unit tests and the
game's smoke run go on beside them, and print at the end. The suite ends with
each run's finish and its time. A game runs uncapped and takes a whole thread
from launch to close, so the suite is bound by the frames all its games play:
a run that waits, walks or reads more than its check needs makes the whole
suite later. `FAX_E2E_PROFILE=1` prints, as a test exits, the lines of it its
frames went to.

A test runs in Seamless too (`+sw` in the table) only where the view changes
what it checks: a feature that draws or places something on screen, or moves
the hero between rooms. Those runs fix Seamless's picture at 426x240
(`--game-arg=--world-size=426x240`), what it fits to a window at scale 3, so
their window, like every test game's, can be at scale 1 (the `config.ini`
`install()` writes): the hidden window's scaling blit is a tenth of a frame at
3. A test's game is the build's, hard-linked: one copy of the program in
memory for every game, which plays them about 10% faster than copies.

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
files of its recipes, with those of the test modules each uses (a `Game`
subclass's `settle()`, a shared walk), so changing any of them makes it
again. Several tests
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

- Walk with `step_toward()` (held until a few pixels short, then taps), walk
  out of a building with `hold("right", ...)`, and to a counter, a wall or a
  person with `walk("left", stop)` (held until he stops): tapping a direction
  for 2 frames keeps the hero at his slowest speed. `hold()` returns once the
  frame its button is still down has run: Up at a door with Right still down
  goes nowhere.
- Start nearer the check: a countdown the game runs (a flight, a freeze, a
  cooldown) can be poked down to its last seconds once its start is checked,
  as long as one run still plays the whole of one (recharging items' Wing
  Boots cooldown, all 60 s). An enemy can be moved in to just past where the
  check begins (the Pendant's Zozura).
- Hold A while text types: the remaster's Hold A types 4x, and a held A
  never turns a page or answers a question, which take a new press. Wait for
  a page with `type_out()` rather than `until(typed)` before a check, a
  screenshot or a press; reach a line with `page_to()`; and page through a
  conversation that takes the same button on every page (the Guru's words
  after a death, a line to close) with `page_through()`. A new press of A
  turns a page on the frame it types out, so where a page may be a question
  they press A only on a frame that cannot: `hurry_page()` saves a state as a
  character types, and loads it with A down, which then goes down on the next
  frame, one of three on which no character types. `page_to()` holds the A
  that turns a page on through the next. Without the remaster (the original's
  runs) a held A types at the game's pace, so the same calls do no harm.
  Bound a loop that waits on a typing page by frames: with A held there is no
  `wait()` in it, and the game serves several reads a frame.
- Start a death after its dissolve, unless the death is what is checked: the
  hero dissolves for 256 frames before the Guru speaks, and where he comes
  back, what he keeps and what is saved are all decided after that. A recipe
  sets PlayerIsDead (and whatever the death needs), ends with `dissolve()`,
  and the test loads the fixture as the Guru speaks (`conflate-words` in
  fax_e2e, `respawn-died-*` in zone respawn, which come in through the same
  door first). Each feature that checks deaths keeps one whole: Death keeps
  gold, zone respawn (Eolis, and after a Guru travel journey), Slain by
  (its line shows through the dissolve: the Monodron's, the one with nothing
  to blame and the original's play it to the Guru's words; the other killers
  stop once their line is checked at full strength, since it fades on its own
  clock), recharging items' Elixir, and Sugata's flash.
- Run independent headless runs at once with `play.py`'s `headless_all()`.

### Frames, not seconds

Each `Game` keeps one TCP connection to its game. The game serves every line
it has at its frame boundary, so a command costs a frame (a new connection a
command cost two), commands sent together (`batch()`) are served at the same
one, and `wait(n)` is served by the game itself: its `wait` command answers
at the end of the n-th frame, not a frame later. `act()` presses and waits in
one batch. The game keeps reading while lines arrive, so it can serve several
commands in one frame: bound a loop of reads by frames (`frame()`), never by a
count of reads.

The script can lag any number of frames behind the game when the machine is
loaded, so a test waits on the game: `wait()` and `until()` count its frames.
Never count on two TCP commands landing a few frames apart. To act on what a
conversation shows, read its state and the text box in one batch:
`conversation()` does, and `page_to(there, what)` presses A on each typed page
until `there(state)` holds on a typed one, and never presses on that page
(Guru travel's questions answer to A). A batch that ends with a `load_state`
lets two frames of the state run before it returns: a button first pressed
in a loaded state's first frame or two can go unseen (Up at a door did
nothing). When a button
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
