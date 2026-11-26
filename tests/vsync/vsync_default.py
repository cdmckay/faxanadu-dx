"""VSync on by default, end to end, in a hidden window (#181).

    python3 tests/vsync/vsync_default.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/vsync

The engine keeps VSync in config.ini ([Display] VSync), with the display
defaults the file was written with ([Display] DisplayDefaults, 2 since VSync
is on by default). Each game here starts from power-on: only its config.ini
matters, and the engine reads it before the first frame.

1. A config.ini as a build before wrote it, VSync off as its default was and
   no DisplayDefaults: the game turns VSync on, says so over TCP (the state's
   vsync_requested), and has written config.ini at once with VSync = 1 and
   DisplayDefaults = 2, before it closes.
2. VSync turned off in that file, as the launcher's Graphics page does: the
   next game keeps it off, and so does the one after.
3. No config.ini, a fresh profile: VSync on, and the file the game writes
   says so, with DisplayDefaults = 2.

In every one the hidden window never waits on VSync (the state's
vsync_active), so no test game is paced by the display, and it logs why.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
from fax_e2e import Game, parse_args  # noqa: E402

# What a build before #181 wrote to [Display] and [Input], VSync on its old
# default: the rest of the file keeps its defaults.
OLD = ("[Display]\nWindowScale = 1\nFullscreen = 0\nIntegerScale = 1\nLinearFilter = 0\nVSync = 0\n"
       "HdPackEnabled = 1\nHdPackDir = \n[Input]\n# the default bindings' layout this file was written with\n"
       "BindingsLayout = 2\n")
HIDDEN = "vsync off: the window is hidden"


def display(config):
    """config.ini's [Display] keys."""
    keys, section = {}, None
    for line in config.read_text().splitlines():
        line = line.strip()
        if line.startswith("["):
            section = line
        elif section == "[Display]" and "=" in line and not line.startswith("#"):
            key, value = line.split("=", 1)
            keys[key.strip()] = value.strip()
    return keys


def play(game, config):
    """One game: its VSync as the engine reads it, and config.ini as it left it
    (written at once if the load moved anything, and again as it quits)."""
    start = game.log_path.stat().st_size
    game.launch()
    state = game.tcp("state")
    assert state["vsync_active"] is False, state           # hidden: never waits on the display
    saved = display(config)                               # what the load wrote, before the quit's save
    game.close()
    with open(game.log_path, errors="replace") as f:
        f.seek(start)
        log = f.read()
    assert (HIDDEN in log) == state["vsync_requested"], log
    return state["vsync_requested"], saved, display(config)


def main():
    args = parse_args(__doc__)
    game = Game.from_args(args)
    config = game.out / "game" / "config.ini"
    try:
        config.write_text(OLD)
        on, saved, after = play(game, config)
        assert on and saved["VSync"] == "1" and saved["DisplayDefaults"] == "2", saved
        assert after["VSync"] == "1" and after["DisplayDefaults"] == "2", after
        print("ok: an old config.ini's VSync = 0 turns on, and the game writes VSync = 1, DisplayDefaults = 2 at once")

        config.write_text(config.read_text().replace("\nVSync = 1\n", "\nVSync = 0\n"))
        for _ in range(2):
            on, saved, after = play(game, config)
            assert not on and saved["VSync"] == "0" and after["VSync"] == "0", (saved, after)
            assert after["DisplayDefaults"] == "2", after
        print("ok: VSync = 0 set after that stays off, game after game")

        config.unlink()
        on, saved, after = play(game, config)
        assert on and saved["VSync"] == "1" and saved["DisplayDefaults"] == "2", saved
        assert after == saved, (saved, after)
        print("ok: a fresh profile gets VSync = 1, DisplayDefaults = 2")
        print("ok: no hidden game waited on VSync")
    finally:
        game.close()


if __name__ == "__main__":
    main()
