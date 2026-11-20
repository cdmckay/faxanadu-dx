# Faxanadu Remastered

Faxanadu (NES) statically recompiled to native code with
[nesrecomp](https://github.com/mstan/nesrecomp), plus quality-of-life features.
A fork of mstan/FaxanaduRecomp; compatibility with upstream is not a goal.

## Write down what you learn about the game

**Anything you learn about the game's structure goes in
[docs/GAME_INTERNALS.md](docs/GAME_INTERNALS.md), in the same PR as the code
that relies on it.** That covers RAM variables, routines and what they really
do, script and message formats, the text box, free ROM, engine traps, and
anything that cost you time to find out. Correct entries that turn out wrong.
The next person should never have to rediscover it.

## Workflow

- Work from an issue, on a branch, through a PR on **cdmckay/faxanadu-remastered**.
  `gh` defaults to the upstream parent for a fork, so always pass
  `-R cdmckay/faxanadu-remastered` (and `--base master` for PRs).
- Merge with a merge commit once `tools/test.sh` passes and the PR's test plan
  is run.
- The remote is HTTPS; push with `gh` as the credential helper if git has none.

## Build, run, test

The Nix devShell (`flake.nix`, loaded by direnv) has the toolchain. The build
recompiles the original `Faxanadu (USA)` ROM (CRC32 `57DD23D1`) at configure
time; keep it at the repo root as `Faxanadu (USA).nes` (gitignored).

```bash
cmake -S . -B build-cycle -DCMAKE_BUILD_TYPE=Release -DNESRECOMP_ROM="$PWD/Faxanadu (USA).nes"
cmake --build build-cycle -j"$(nproc)"
build-cycle/FaxanaduRecomp "Faxanadu (USA).nes"     # the launcher, then the game
tools/test.sh                                        # every check; --quick skips the game runs
tools/disassembly.sh                                 # chipx86's disassembly, verified, with labels
```

There is no CI: nothing builds without the ROM, which cannot be distributed.
`tools/test.sh` is the gate, so run it before every PR.

Its end-to-end runs play a game a hardware thread at once, longest first, so
they take a machine-wide lock: a second checkout's suite prints that it is
waiting, then runs. Several workers share this machine, so while developing run
`tools/test.sh --quick` plus your own feature's end-to-end test directly, and
the full suite once before the PR.

## Building features

Features are C on an **unchanged ROM**. The game's code stays as the
recompiler produced it; features observe and steer it.

- **Never change the cart image at runtime.** Save states hash it. Serve data
  the game must read (script steps, messages) from unused ROM through read
  overrides, and restore them from your save-state record.
- Use the game's own routines where you can. Run memory-only routines with
  `cyc_mod_call_commit`, or enter them by returning from a hook with a
  prepared stack, as `fax_travel.c` does.
- Name every address after its label in the disassembly, and comment what the
  stock bytes there are.
- **Declare what a feature takes** in `mods/allocations.toml`: its ROM, its
  hook-site prefix, and any address it shares with another feature and why
  that is safe. `tools/check_allocations.py` fails otherwise.
- **Test every feature two ways.** Unit tests in C cover its logic against a
  model of the machine (`tests/travel/travel_test.c`). An end-to-end test plays
  it in a hidden window over TCP (`tests/travel/guru_travel.py`). Add both to
  `tools/test.sh`. Give each end-to-end run its own output folder: several
  sessions may run the suite at once.
- **End-to-end tests use the shared helper,
  [tests/e2e/fax_e2e.py](tests/e2e/fax_e2e.py)**, for the port, launching the
  game, the TCP client, save-state fixtures and screenshots. Never write your
  own `free_port()`: the helper reserves each port for the whole test, against
  every suite on the machine, and relaunches the game if the port is taken
  anyway. [tests/README.md](tests/README.md) shows how.
- **End-to-end tests count game frames, never seconds.** The helper's
  `wait()` and `until()` count the game's frames, and the script can lag any
  number of frames behind the game on a loaded machine. Input that must land
  on a given frame goes in with the save state it starts from, in one
  `batch()` (a `load_state`, then the `action`), or runs headless from that
  state with `--input`. Never count on two TCP commands being a few frames
  apart.
- **End-to-end tests start from saved games.** `game.launch(at=...)` loads a
  fixture, made from your ROM by a recipe and cached in `build-tests/fixtures`;
  boot from power-on only where the boot is what is checked. **Never commit a
  save state:** it holds CHR RAM copied from the ROM. A test much longer than
  the rest splits into parts that run at once. The suite's time is the frames
  its games play, so walk, read and wait no longer than the check needs
  ([tests/README.md](tests/README.md)).
- **Everything the remaster adds goes into the one Faxanadu Remastered mod**
  (`mods/preloaded/packages/faxanadu.remastered`), never a new package: add a
  `[[plugin]]` under its `remastered` feature. The launcher has one switch for
  it: on is the remaster, off is the original game. Seamless, the view
  inherited from upstream, is the only other package.
- **Features inside the mod may rely on each other.** They are always on
  together, so one may call another's functions or build on its behavior.
  Each still keeps its own C file and hook-site prefix, and
  `tools/check_allocations.py` treats every two as enabled together: an
  address two features hook needs a `[[shared_site]]` saying why they work in
  the order they run (game.toml's).
- **Tests play the remaster with it on.** "The original" checks run with the
  whole mod off, never one feature. A hidden test-only switch for one feature
  is a last resort, for a check that is otherwise impossible.
- Match the game's voice and limits for any new dialogue: 16 columns, 4 lines,
  plain words.
