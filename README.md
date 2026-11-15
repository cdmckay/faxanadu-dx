# Faxanadu Remaster

A remaster of Faxanadu (NES) that keeps as much of the real game as possible,
and fixes what has aged.

## Goals

- **The real game underneath.** Faxanadu's own code runs as native code,
  recompiled from your ROM, so the world, music, story and feel are the
  original's. Features steer the game from outside; they don't rewrite it.
- **Quality of life.** Conveniences the 1987 game lacks, like travelling
  between Gurus and saving without writing down mantras. Where the game can
  offer them itself, it does, in its own text box and voice.
- **Combat, tuned with care.** Combat may be adjusted to be a bit more fun,
  without losing what makes Faxanadu feel like Faxanadu.
- **The original, one switch away.** Each change is a mod you can turn off in
  the launcher.

Built on Matthew Stanley's [FaxanaduRecomp](https://github.com/mstan/FaxanaduRecomp)
and his [NESRecomp](https://github.com/mstan/nesrecomp) static recompiler.
How the game works, as far as we've learned it, is in
[docs/GAME_INTERNALS.md](docs/GAME_INTERNALS.md).

> **Status: Playable.** The game runs from title screen through credits. No outstanding known bugs. Not 100% playtested — minor edge cases may exist, but normal gameplay is fully functional.

[![FaxanaduRecomp showcase](media/title_screen.png)](https://www.youtube.com/watch?v=lLXSnK3HVW4)

## Default cycle controls

Default cycle controls: arrows move, Z is A, X is B, Enter is Start and
Backslash is Select. Escape opens the menu, Tab fast-forwards, and F8/F9
save/load the cycle state. Use Controls to remap inputs in `config.ini`.
Older `keybinds.ini` and F5/F6/F7 instructions below apply to the legacy host.

## Quick Start

1. Download `FaxanaduRecomp-windows-x64.zip` from [Releases](../../releases)
2. Extract and run `FaxanaduRecomp.exe`
3. Select your Faxanadu (USA) ROM when prompted — the path is saved for future launches

**Guru travel** is on by default (Mods to turn it off). After a temple's Guru
gives your mantra, he offers to send your spirit to any other Guru you have
visited: A to go, B to hear the next town. HP, MP, gold and your spawn point
are kept. The Gurus you have met are saved in `faxanadu_gurus.txt` next to the
game.

**Death keeps gold** is on by default (Mods to turn it off). When you die, the
Guru brings you back with all your gold and experience, instead of your
title's starting amounts. The rest of death is as before: full HP and MP at
your furthest temple, and the timed items are gone.

**Pendant fix** is on by default (Mods to turn it off). The Pendant was meant
to make your weapon hit a quarter harder, but a reversed check in the
original gave that bonus to everyone without it, and finding the Pendant took
it away. Now your weapon does its base damage until you find the Pendant, and
the original's full damage once you have it. The Glove adds its bonus as
before.

Optional **Seamless** appears first in Mods, marked **Experimental** and
disabled by default. It joins connected rooms, advances neighboring actors,
and adapts the world view to your window with Fit to screen. Mouse wheel
changes pixel zoom; F4 shows the connected map. Dialogue and pause preserve
the expanded view. It passed a preliminary owner playtest; broader gameplay
coverage and cross-room combat/projectiles remain incomplete. Seamless is
local play only. See [details](docs/SEAMLESS-WORLD.md).

## Controls

| NES Button | Keyboard |
|------------|----------|
| D-Pad      | Arrow keys |
| A          | Z |
| B          | X |
| Start      | Enter |
| Select     | Right Shift |

## Mantra (Password) Features

Faxanadu uses a mantra system instead of battery-backed saves. FaxanaduRecomp adds convenience features around this:

**Auto-load** — On startup, the game reads `saves.txt` (next to the exe) and auto-fills the most recent mantra on the password entry screen. No need to write down or retype passwords.

**CLI override** — `--password STRING` overrides the `saves.txt` mantra for a single session.

```
FaxanaduRecomp.exe [ROM] --password "k8fPcv?,TwSYzGZQhMIQhCEA"
```

**saves.txt format** — One mantra per line, most recent first. Human-readable and copy-pasteable. You can edit this file manually to add or change your saved mantra.

> **Note:** Auto-saving mantras from the priest dialog is not yet implemented. For now, write the mantra shown by the priest into `saves.txt` manually, or pass it via `--password`.

## Save States

| Key | Action |
|-----|--------|
| F5  | Toggle turbo (fast-forward) |
| F6  | Save state → `C:\temp\quicksave.sav` |
| F7  | Load state ← `C:\temp\quicksave.sav` |

## ROM

| Field | Value |
|-------|-------|
| Title | Faxanadu (USA) |
| CRC32 | `57DD23D1` (ROM data only, iNES header excluded) |
| MD5   | `b3bb9296b5e1e0c83e90b6da9e8a41aa` (ROM data only) |
| SHA-1 | `5b05c8859f356013d37f0545f5de5fa1693da5da` (ROM data only) |

## Building from Source

The build recompiles your ROM at configure time, so it needs the original
`Faxanadu (USA)` image (game data CRC32 `57DD23D1`). `Faxanadu (USA) (Rev A)`
is a different revision and is rejected. Put the ROM at the repo root as
`Faxanadu (USA).nes`; `*.nes` is gitignored.

```bash
git clone https://github.com/cdmckay/faxanadu-remaster
cd faxanadu-remaster

# Windows
setup.bat

# Linux / macOS
./setup.sh
```

This initializes the pinned [nesrecomp](https://github.com/mstan/nesrecomp)
submodule and links the Nestopia oracle core.

### Windows

Prerequisites: Windows 10+, Visual Studio 2022, CMake 3.20+ (SDL2 is bundled)

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DNESRECOMP_ROM="Faxanadu (USA).nes"
cmake --build build --config Release
```

### Linux

The Nix devShell in `flake.nix` provides CMake, Python, SDL2, GL, FreeType and
HarfBuzz. With [direnv](https://direnv.net/), `direnv allow` loads it whenever
you enter the repo; otherwise run the commands inside `nix develop`.

```bash
direnv allow
cmake -S . -B build-cycle -DCMAKE_BUILD_TYPE=Release -DNESRECOMP_ROM="$PWD/Faxanadu (USA).nes"
cmake --build build-cycle -j"$(nproc)"
build-cycle/FaxanaduRecomp "Faxanadu (USA).nes"
```

Without Nix, install the same packages from your distribution (on Debian:
`cmake python3 libsdl2-dev libgl-dev libfreetype-dev libharfbuzz-dev`).
nixpkgs' SDL2 is sdl2-compat on SDL3, which runs natively on Wayland and draws
the window's title bar with libdecor on GNOME. Classic SDL2 prefers X11 (through
XWayland); run with `SDL_VIDEODRIVER=wayland` to get native Wayland there.

Headless runs are handy for checks: `--frames 600 --screenshot shot.png`
plays 600 frames without a window and saves the last one.

## Architecture

This is a **static recompiler**, not an emulator. The 6502 machine code in the ROM
has been translated to C by [NESRecomp](nesrecomp/) and compiled to native x64.

| File | Purpose |
|------|---------|
| `extras.c` | Faxanadu-specific hooks (mantra injection, auto-load) |
| `game.cfg` | Recompiler config (dispatch tables, trampolines) |
| `baserom_annotations.csv` | Address annotations for generated code |
| `generated/faxanadu_full.c` | Recompiled 6502 code (committed) |
| `generated/faxanadu_dispatch.c` | Dispatch table (committed) |
| `reference/` | Reference screenshots for visual regression |

## License

For this project's code covered by the [PolyForm Noncommercial License](LICENSE),
[PERMISSIONS.md](PERMISSIONS.md) grants standing permission for voluntary
donations, sponsorships, and advertising revenue, including support for broader
development initiatives. Payment must not buy access to the software, games
built with it, or their updates, features, or game content, including early access.

Matthew Stanley has authority from the relevant copyright holders to grant
these permissions for the project's PolyForm-covered code. Third-party
components and code explicitly licensed under other terms retain those terms.

PolyForm Noncommercial 1.0.0 — see [`LICENSE`](LICENSE). Third-party
components retain their own licenses.

---

<p align="center">
  <sub><b>R.A.I.D. — Retro AI Development</b> · a Discord for AI-assisted retro reverse-engineering, decomp &amp; recomp</sub>
</p>

<p align="center">
  <a href="https://discord.gg/Ad9BwSzctP"><img src=".github/raid-discord.png" alt="Join the Retro AI Development (R.A.I.D.) Discord" width="200"></a>
</p>

Cycle Windows builds use `tools/build.ps1 -Rom <original-ROM>`. Create a ROM-free
ZIP with `tools/make_release.ps1 -Rom <original-ROM>`; `-SkipBuild -BuildDir
build-cycle` packages an existing production cycle build. Legacy C remains
available through explicit CMake selection.


The October 2026 USA coverage pass exercises original entries for all six
outside regions and encounters all eight real boss species, including King
Grieve and Shadow Eura. Authored screen and sprite metadata were bound by
reproducing both complete 16 KiB data banks against the supplied USA ROM.
The probes assist screen selection, player health and entrance position;
original screen initialization, actors and combat routines still run.
These are encounter checks, with some routes ending in player death.

All eight boss routes match native/interpreter machine hashes, pictures and
complete states. After adding 958 observed ROM instruction starts, all eight
routes and the earlier six region routes replay exactly against their
pre-rebuild native and interpreter results on engine `0498965`. Earlier
intro/region coverage added 1,103 valid starts. Item collection, mantra
interactions and the ending require separate qualification. Areas 3 and 4
are the towns/buildings profiles; dungeon screens belong to the surrounding
regions. This build uses stock rendering.
