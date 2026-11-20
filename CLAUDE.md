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

- Work from an issue, on a branch, through a PR on **cdmckay/faxanadu-remaster**.
  `gh` defaults to the upstream parent for a fork, so always pass
  `-R cdmckay/faxanadu-remaster` (and `--base master` for PRs).
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

Its end-to-end runs start every game at once, a core each, so they take a
machine-wide lock: a second checkout's suite prints that it is waiting, then
runs. Several workers share this machine, so while developing run
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
  `tools/test.sh`. Give each end-to-end run its own free port and output
  folder: several sessions may run the suite at once.
- A feature is a mod package under `mods/preloaded/packages/`, so players can
  turn it off in the launcher.
- Match the game's voice and limits for any new dialogue: 16 columns, 4 lines,
  plain words.
