# Faxanadu DX

Faxanadu (NES) statically recompiled to native code with
[nesrecomp](https://github.com/mstan/nesrecomp), plus quality-of-life features.
A fork of mstan/FaxanaduRecomp; compatibility with upstream is not a goal.

## Faxanadu is a Metroidvania: lean into it

Faxanadu is one connected world, opened up by keys, tools and progress, that
keeps sending you back to places you have been. That is the remaster's design
direction. When you design a feature or choose between options:

- Make exploring and going back rewarding and easy: the map and its marks,
  Guru travel, and upgrades in earlier places that later progress opens (Eolis's
  Magician teaches his lesson only to those who have studied at the magic hall
  in the Mist).
- Gate an upgrade on progress that can't be ground out, such as a place
  reached, an item found or a quest done, rather than on experience or gold
  alone.
- Never make the world more linear, and never take away a reason to explore.

## Write down what you learn about the game

**Anything you learn about the game's structure goes in
[docs/GAME_INTERNALS.md](docs/GAME_INTERNALS.md), in the same PR as the code
that relies on it.** That covers RAM variables, routines and what they really
do, script and message formats, the text box, free ROM, engine traps, and
anything that cost you time to find out. Correct entries that turn out wrong.
The next person should never have to rediscover it.

## Workflow

- Work from an issue, on a branch, through a PR on **cdmckay/faxanadu-dx**.
  `gh` defaults to the upstream parent for a fork, so always pass
  `-R cdmckay/faxanadu-dx` (and `--base master` for PRs).
- Merge with a merge commit once `tools/test.sh` passes and the PR's test plan
  is run.
- The remote is HTTPS; push with `gh` as the credential helper if git has none.
- The `nesrecomp` submodule is cdmckay's fork of mstan/nesrecomp (#143). An
  engine fix goes on its `faxanadu-dx` branch, then the pin moves to it;
  `recomp-ui` still tracks mstan's.

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
tools/readme_screenshots.py --only NAME             # the README's screenshot of a feature
```

There is no CI: nothing builds without the ROM, which cannot be distributed.
`tools/test.sh` is the gate, so run it before every PR.

Its end-to-end runs play a game a hardware thread at once, longest first, so
they take a machine-wide lock: a second checkout's suite prints that it is
waiting, then runs. Several workers share this machine, so while developing run
`tools/test.sh --quick` plus your own feature's end-to-end test directly, and
the full suite once before the PR.

## Layout

- `src/`: the game's C. Each feature is `fax_<name>.c` and `.h`, listed in
  CMakeLists.txt's `HOST_EXTRAS`, and `cyc_extras.c` calls them from the
  cycle host. `override_text.c` serves text overrides; `extras.c`,
  `nes_snapshot.c`, `verify_mode.c` and `watchdog.c` are only for the legacy
  backend.
- `game.toml` and `cyc_seeds.txt`: the recompiler's inputs.
- `mods/`: the mod packages (`preloaded/`) and `allocations.toml`.
- `tests/<feature>/`: a feature's unit test (with its `CMakeLists.txt`) and
  end-to-end test; `tests/e2e/` is the shared helper.
- `tools/`: `test.sh`, the allocation checker, the disassembly, ROM checks,
  and `readme_screenshots.py`.
- `docs/`: GAME_INTERNALS.md and the other notes; `docs/screenshots/` holds
  the README's pictures. `assets/`: the launcher's box art.

## Building features

Features are C on an **unchanged ROM**. The game's code stays as the
recompiler produced it; features observe and steer it.

- **Never change the cart image at runtime.** Save states hash it. Serve data
  the game must read (script steps, messages) from unused ROM through read
  overrides, and restore them from your save-state record.
- Use the game's own routines where you can. Run memory-only routines with
  `cyc_mod_call_commit`, or enter them by returning from a hook with a
  prepared stack, as `src/fax_travel.c` does.
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
- **Put every feature in the README, with a screenshot only when there is
  something new to see.** A plain quality-of-life feature gets a bullet, a
  line or two, under "Also new"; a fix for one of the original's bugs gets a
  sentence in "Bugs fixed". Add a screenshot only for something visually
  interesting: a new screen, overlay or visual, as the quest log, the map and
  Seamless have. Never for behavior you can't see (experience that doesn't
  reset, say), nor to show that a glitch no longer happens. A feature that
  earns one gets its own section in "What's new" and a picture made by
  `tools/readme_screenshots.py`: add a `@scene` that plays the feature to its
  moment (reuse your end-to-end test's setup), run it with `--only <scene>`,
  look at `docs/screenshots/<scene>.png`, and commit the PNG. Never commit a
  save state. A PR that changes how a shown feature looks reruns its scene;
  rerun only the scenes you changed, so the others don't churn. This is about
  the README: a PR's test plan can still carry screenshots as evidence.
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
  apart, nor on reads taking a frame each: bound a loop by frames, and read
  what a press depends on in one batch (`page_to()` for a conversation).
- **End-to-end tests start from saved games.** `game.launch(at=...)` loads a
  fixture, made from your ROM by a recipe and cached in `build-tests/fixtures`;
  boot from power-on only where the boot is what is checked. **Never commit a
  save state:** it holds CHR RAM copied from the ROM. A test much longer than
  the rest splits into parts that run at once. The suite's time is the frames
  its games play, so walk, read and wait no longer than the check needs
  ([tests/README.md](tests/README.md)): hold A while text types
  (`type_out()`, `page_to()`, `page_through()`, which never turn a page
  they should not), and start a death after its dissolve, from a fixture,
  unless the death is what is checked.
- **Everything the remaster adds goes into the one Faxanadu DX mod**
  (`mods/preloaded/packages/faxanadu.dx`), never a new package: add a
  `[[plugin]]` under its `dx` feature. The launcher has one switch for
  it: on is the remaster, off is the original game. Seamless, the continuous
  world inherited from upstream (`src/fax_world.c`), is one of its plugins
  (#110): the remaster always plays in it.
- **Design for Seamless's 424x240 view: it is the remaster's only view.**
  Anything the remaster draws, places or moves on screen must fit and read
  there, and is tested there. The 256-wide stock view is only the original's
  (the remaster off), so nothing new needs to fit or be tested in it.
- **Features inside the mod may rely on each other.** They are always on
  together, so one may call another's functions or build on its behavior.
  Each still keeps its own C file and hook-site prefix, and
  `tools/check_allocations.py` treats every two as enabled together: an
  address two features hook needs a `[[shared_site]]` saying why they work in
  the order they run (game.toml's).
- **Tests play the remaster with it on, so in Seamless.** "The original"
  checks run with the whole mod off, in the stock view, never one feature
  off. A hidden test-only switch for one feature is a last resort, for a
  check that is otherwise impossible. Seamless runs a room's actors before
  the hero walks into it, so a fixture that needs a room's enemies as they
  load comes in by a door, the game's own load (`eolis-zozuras` does).
- Match the game's voice and limits for any new dialogue: 16 columns, 4 lines,
  plain words.
