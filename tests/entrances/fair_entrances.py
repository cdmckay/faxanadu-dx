"""Play Fair entrances end to end: each enemy it moves, at the way in that
made its placement unfair, with the feature off and on.

    python3 tests/entrances/fair_entrances.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/entrances
    python3 tests/entrances/fair_entrances.py ... --game-arg=--world-view=seamless

A hidden window over TCP starts a new game, and (an external prerequisite, not
coverage) a save-state RAM fixture kills the hero with Mascon as his furthest
temple and a Long Sword, Leather Armor and a Small Shield, so the death path
wakes him in that temple. From there, headless runs of the same build (with
the same --game-arg options, so the same world view) play each way in, frame
by frame, as the game does it:

- Mascon's temple: walking out of it, as after every death there. It opens on
  the Mist's screen 6.
- A building's way out: the temple's door edited to lead where the building's
  does (Area_SavedArea, Area_SavedScreen, Screen_SavedPalette and
  Player_SavedPosXY, which Game_EnterBuilding sets from where the hero stood
  at the building's door), then walking out.
- A door within an area: the temple's door edited to lead to the door's block
  on its own screen, walking out, and Up pressed on his first update there;
  for Branch's K-key door with the key selected and its message paged with A,
  which stops before he arrives.

Nothing is pressed from the arrival on. Each is played with Fair entrances
off, then on (the mod selection, every other feature as by default):

1. Off, the enemy is at its stock block on the frame before his first update,
   and hits him (Player_InvincibilityPhase $3C, HP down) within HIT frames of
   that update: before anyone could react.
2. On, the enemy starts at its new block, and nothing hits him for SAFE
   frames.
3. With Seamless on, walking from the Mist's screen 5 toward screen 6 has
   Seamless load screen 6 on an isolated machine, where hooks do not run:
   its Sir Gawaine starts at the new block there too (the feature's
   "seamless" count), read from Seamless's own room record.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "movement"))
from play import (Game, fixture, AREA, SCREEN, X, Y, IFRAMES, HP, TEMPLE_SPAWN_POINT, PLAYER_IS_DEAD,  # noqa: E402
                  MESSAGE_ID, SPRITE_ENTITY, SELECTED_WEAPON, SELECTED_ARMOR, SELECTED_SHIELD, PLAYER_CUR_WEAPON,
                  LONG_SWORD, LEATHER_ARMOR, SMALL_SHIELD, BUILDINGS, NEGATIVE_THOUGHTS)

SPRITE_X, SPRITE_Y, SHADOW_OAM = 0xBA, 0xC2, 0x700
X_FRACTION, FLAGS, STATUS = 0x9D, 0xA4, 0xA5                        # Player_PosX_Frac, Player_Flags, _StatusFlag
HOLD = {"right": "RIGHT", "left": "LEFT", "up": "UP", "down": "DOWN", "drop": None}
SAVED_AREA, SAVED_SCREEN, SAVED_PALETTE, SAVED_POS = 0x3D5, 0x3D6, 0x3D7, 0x3D8
SELECTED_ITEM, AREA_REGION = 0x3C1, 0x435
SCROLL_DIRECTION, SCREEN_READY, INTERRUPTS_ON = 0x54, 0x0E, 0x13
MASCON = 3
KEY_K = 0x05                                                        # ITEMINVENTORY_KEY_K
PACKAGE, FEATURE = "faxanadu.enhancement.fair-entrances", "fair-entrances"
HIT, SAFE = 16, 30          # frames after his first update: the latest stock hit, and the moved enemies' margin
TRUNK, MIST, BRANCH, DARTMOOR = 1, 2, 5, 6
REGION = {TRUNK: 1, MIST: 2, BRANCH: 3, DARTMOOR: 4}
GAWAINE, RAIDEN, SUGATA, IKEDA, ZOMBIE = 0x1F, 0x04, 0x18, 0x26, 0x06
LAMPREY, MASKMAN, GIANT_STRIDER, WOLFMAN = 0x28, 0x20, 0x1E, 0x21

# Each spot: what it is, the screen (area, screen, palette), the way in, and
# the enemy (entity, stock block, new block; YX as bank 11 stores them).
# A way in is ("temple",), ("building", start YX), or ("door", source screen,
# its palette, the door's block, the key to select or None).
SPOTS = [
    ("Mascon's temple", (MIST, 0x06, 0x0A), ("temple",), (GAWAINE, 0x89, 0x9E)),
    ("Mist $06, building $22's way out", (MIST, 0x06, 0x0A), ("building", 0x87), (GAWAINE, 0x89, 0x9E)),
    ("the Tower of Mist's way out", (MIST, 0x17, 0x0A), ("door", 0x45, 0x0B, 0xA0, None), (RAIDEN, 0xA5, 0xA9)),
    ("Mist $1F, building $26's way out", (MIST, 0x1F, 0x0A), ("building", 0x94), (SUGATA, 0x95, 0x97)),
    ("the tower on Mist $20's way out", (MIST, 0x20, 0x0A), ("door", 0x50, 0x0B, 0xA2, None), (GAWAINE, 0x96, 0x46)),
    ("Mist $29, building $20's way out", (MIST, 0x29, 0x0A), ("building", 0x7C), (IKEDA, 0x7D, 0x7E)),
    ("Trunk $06, building $20's way out", (TRUNK, 0x06, 0x06), ("building", 0x5A), (ZOMBIE, 0x5B, 0x5C)),
    ("Branch $0E, the door from $0F", (BRANCH, 0x0E, 0x08), ("door", 0x0F, 0x09, 0x60, None), (LAMPREY, 0x6D, 0x6A)),
    ("Branch $13, the door from $14", (BRANCH, 0x13, 0x09), ("door", 0x14, 0x08, 0x80, KEY_K), (MASKMAN, 0x8C, 0x9A)),
    ("Dartmoor $02, building $22's way out", (DARTMOOR, 0x02, 0x0C), ("building", 0x4A), (GIANT_STRIDER, 0x49, 0xA9)),
    ("Dartmoor $03, building $24's way out", (DARTMOOR, 0x03, 0x0C), ("building", 0xAB), (LAMPREY, 0xAA, 0xA8)),
    ("Dartmoor $08, building $25's way out", (DARTMOOR, 0x08, 0x0C), ("building", 0x41), (GAWAINE, 0x42, 0x46)),
    ("the Castle of Fraternal's way out", (DARTMOOR, 0x0C, 0x0C), ("door", 0x10, 0x0D, 0x90, None), (WOLFMAN, 0xAB, 0xAD)),
]


def visible(ram):
    """Sprites the update drew this frame, the status bar's sprite 0 included."""
    return sum(1 for k in range(0, 256, 4) if ram[SHADOW_OAM + k] < 0xEF)


class Runs:
    """Headless runs of the copied build from save states, with a button
    schedule and a per-frame RAM log, as tests/movement/play.py's headless."""

    def __init__(self, game, game_args):
        self.game, self.exe, self.rom, self.out = game, game.exe, game.rom, game.out
        self.args = [part for arg in game_args for part in arg.split("=", 1)]
        self.count = 0

    def mods(self, on):
        root = self.out / f"mods-{'on' if on else 'off'}"
        if not root.exists():
            shutil.copytree(self.out / "game" / "mods" / "packages", root / "packages")
            (root / "state.toml").write_text(
                f'format_version = 1\n\n[[feature]]\npackage_id = "{PACKAGE}"\nid = "{FEATURE}"\n'
                f'enabled = {"true" if on else "false"}\n')
        return root

    def run(self, state, schedule, frames, on, screenshot=None, save=None):
        probe = subprocess.run([str(self.exe), str(self.rom), "--load-state", str(state), "--frames", "0",
                                "--no-save", *self.args], capture_output=True, text=True)
        start = int(re.search(r"continuing at frame (\d+)", probe.stdout).group(1))
        self.count += 1
        work = self.out / "run"
        work.mkdir(exist_ok=True)
        (work / "input.txt").write_text("".join(f"{start + at} {buttons}\n" for at, buttons in sorted(schedule)))
        log = work / "frames.bin"
        cmd = [str(self.exe), str(self.rom), "--load-state", str(state), "--input", str(work / "input.txt"),
               "--frames", str(start + frames), "--frame-log", str(log),
               "--frame-log-frames", f"{start}:{start + frames - 1}", "--mods-root", str(self.mods(on)),
               "--no-save", *self.args]
        if screenshot:
            cmd += ["--screenshot", str(screenshot)]
        if save:                                                    # (frame, path): a save state after that frame
            cmd += ["--save-state", f"{start + save[0]}:{save[1]}"]
        done = subprocess.run(cmd, capture_output=True, text=True)
        assert done.returncode == 0, done.stdout + done.stderr
        data = log.read_bytes()
        log.unlink()
        assert data[:8] == b"CYCFRAME"
        rams, pos = [], 12
        while pos < len(data):
            head = struct.unpack_from("<9I", data, pos)
            pos += 36 + 32 + 256
            rams.append(data[pos:pos + head[3]])
            pos += head[3] + sum(head[4:9])
        assert len(rams) == frames, (len(rams), frames)
        return rams


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for key in ("build", "rom", "out"):
        ap.add_argument("--" + key, type=Path, required=True)
    ap.add_argument("--game-arg", action="append", default=[],
                    help="extra game option, e.g. --game-arg=--world-view=seamless (repeatable)")
    ap.add_argument("--realtime", action="store_true", help="run the window at normal speed instead of uncapped")
    ap.add_argument("--only", action="append", default=[], help="only the spots whose name contains this")
    args = ap.parse_args()
    seamless = any("seamless" in a for a in args.game_arg)
    game = Game(args.build, args.rom, args.out, args.game_arg, args.realtime)
    out = game.out
    runs = Runs(game, args.game_arg)
    try:
        game.launch()
        assert game.tcp("entrances_state")["enabled"], "Fair entrances is on by default"
        # A new game, then a death with Mascon as the furthest temple.
        game.until("the title", lambda: game.frame() > 700)
        game.tcp("pad", buttons="start", frames=10)
        game.until("the Eolis gate", lambda: game.frame() > 1900)
        gate = out / "gate.cycstate"
        game.tcp("save_state", path=str(gate))
        dying = out / "dying.cycstate"
        fixture(gate, dying, {SPRITE_ENTITY + 7: 0xFF, TEMPLE_SPAWN_POINT: MASCON, PLAYER_IS_DEAD: 1, HP: 0x50,
                              SELECTED_WEAPON: LONG_SWORD, PLAYER_CUR_WEAPON: LONG_SWORD,
                              SELECTED_ARMOR: LEATHER_ARMOR, SELECTED_SHIELD: SMALL_SHIELD})
        game.tcp("load_state", path=str(dying))
        game.until("the Guru's words", lambda: game.ram(MESSAGE_ID) == NEGATIVE_THOUGHTS, 600)
        for _ in range(60):
            if game.ram(AREA) == BUILDINGS and not game.ram(PLAYER_IS_DEAD) and \
                    game.ram(MESSAGE_ID) != NEGATIVE_THOUGHTS:
                break
            message, _, paused = game.ram(MESSAGE_ID, 3)
            if message == 0 or paused:
                game.tcp("action", name="a", frames=6)
                game.wait(14)
            else:
                game.wait(10)
        game.wait(60)
        temple = out / "temple.cycstate"                    # at Mascon's Guru, facing left
        game.tcp("save_state", path=str(temple))
        assert (game.ram(AREA), game.ram(SAVED_AREA), game.ram(SAVED_SCREEN), game.ram(SAVED_POS)) == \
            (BUILDINGS, MIST, 0x06, 0x89), "Mascon's temple opens on the Mist's screen 6, at block $89"

        # How long the walk out of the temple takes, holding Right: the same
        # from every fixture of this state.
        rams = runs.run(temple, [(0, "RIGHT")], 120, False)
        exit_frame = next(i for i, r in enumerate(rams) if r[AREA] != BUILDINGS)

        def arrive(state, area, screen, schedule, frames, on):
            """Play `schedule`, and return the frames from the first update on
            (area, screen), and the one before it."""
            rams = runs.run(state, schedule, frames, on)
            came = next((i for i, r in enumerate(rams) if (r[AREA], r[SCREEN]) == (area, screen)), None)
            assert came is not None, f"never reached {area}:{screen:#04x}: at {rams[-1][AREA]}:{rams[-1][SCREEN]:#04x}"
            blank = next(i for i in range(max(came - 1, 0), len(rams)) if visible(rams[i]) <= 1)
            first = next(i for i in range(blank, len(rams)) if visible(rams[i]) > 1)
            return rams, first

        def door_schedule(state, src, door, key):
            """The walk out of the temple to `door` on `src`, and Up on the
            first update there (paging the key's message after it), up to the
            frame the door's screen starts loading."""
            walk = [(0, "RIGHT"), (exit_frame, "-")]
            rams, first = arrive(state, src[0], src[1], walk, exit_frame + 120, False)
            sched = walk + [(first, "UP"), (first + 4, "-")]
            if key is not None:
                for k in range(20):                          # "I've used key.": A, 6 frames, every 16
                    sched += [(first + 10 + 16 * k, "A"), (first + 16 + 16 * k, "-")]
            rams = runs.run(state, sched, first + 700, False)
            left = next(i for i in range(first, len(rams)) if (rams[i][AREA], rams[i][SCREEN]) != src)
            assert rams[left][IFRAMES] == 0, f"hit on {src} before the door"
            return [step for step in sched if step[0] < left] + [(left, "-")], left

        seamless_edges = []

        def edge_state(tag, area, way):
            """The hero at the edge of the source screen, its enemies gone, his
            invincibility spent and HP full (prerequisites, not coverage): from
            the walk out of the temple to there, saved a second later."""
            _, kind, src, src_palette, col, row = way
            there = out / f"{tag}-src.cycstate"
            fixture(temple, there, {SAVED_AREA: area, SAVED_SCREEN: src, SAVED_PALETTE: src_palette,
                                    SAVED_POS: row << 4 | col, AREA_REGION: REGION[area], HP: 0x50})
            raw = out / f"{tag}-raw.cycstate"
            ready = out / f"{tag}-edge.cycstate"
            walk = [(0, "RIGHT"), (exit_frame, "-")]
            clear = {**{SPRITE_ENTITY + k: 0xFF for k in range(8)}, IFRAMES: 0, HP: 0x50}
            if kind == "drop":
                # He falls on his first update there: saved as the screen
                # finishes loading, before it.
                rams = runs.run(there, walk, exit_frame + 60, False)
                f = next(i for i in range(exit_frame, len(rams)) if (rams[i][AREA], rams[i][SCREEN]) == (area, src)
                         and rams[i][SCREEN_READY] == 0 and rams[i][SCROLL_DIRECTION] == 0xFF)
                runs.run(there, walk, f + 1, False, save=(f, raw))
                fixture(raw, ready, clear)
            else:
                runs.run(there, walk, exit_frame + 60, False, save=(exit_frame + 59, raw))
                fixture(raw, ready, {**clear, STATUS: 0, X: col * 16, X_FRACTION: 0, Y: row * 16,
                                     FLAGS: 0x00 if kind == "left" else 0x40})
            return ready

        def scrolled_in(rams, area, screen):
            """The first update on (area, screen) after a scroll: the frame after
            the one the scroll ends on there."""
            return next(i + 1 for i, r in enumerate(rams)
                        if (r[AREA], r[SCREEN]) == (area, screen) and r[SCROLL_DIRECTION] == 0xFF
                        and r[SCREEN_READY] == 0)

        for name, (area, screen, palette), way, (entity, stock, moved) in SPOTS:
            if args.only and not any(o in name for o in args.only):
                continue
            tag = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")
            state = out / f"{tag}.cycstate"
            if way[0] == "temple":
                state = temple
                sched, frames = [(0, "RIGHT"), (exit_frame, "-")], exit_frame + 120
            elif way[0] == "edge":
                state = edge_state(tag, area, way)
                key = HOLD[way[1]]
                sched, frames = ([(0, key)] if key else []), 420
                if seamless:
                    seamless_edges.append((name, state, key, (area, screen, entity, stock, moved)))
                    continue
            elif way[0] == "building":
                fixture(temple, state, {SAVED_AREA: area, SAVED_SCREEN: screen, SAVED_PALETTE: palette,
                                        SAVED_POS: way[1], AREA_REGION: REGION[area], HP: 0x50})
                sched, frames = [(0, "RIGHT"), (exit_frame, "-")], exit_frame + 120
            else:
                _, src, src_palette, door, key = way
                writes = {SAVED_AREA: area, SAVED_SCREEN: src, SAVED_PALETTE: src_palette, SAVED_POS: door,
                          AREA_REGION: REGION[area], HP: 0x50}
                if key is not None:
                    writes[SELECTED_ITEM] = key
                fixture(temple, state, writes)
                sched, left = door_schedule(state, (area, src), door, key)
                frames = left + 120
            seen = {}
            for on in (False, True):
                if way[0] == "edge":
                    rams = runs.run(state, sched, frames, on)
                    first = scrolled_in(rams, area, screen)
                else:
                    rams, first = arrive(state, area, screen, sched, frames + SAFE + 40, on)
                before = rams[first - 1]
                slots = [k for k in range(8) if before[SPRITE_ENTITY + k] == entity and
                         before[SPRITE_Y + k] | before[SPRITE_X + k] >> 4 in (stock, moved)]
                assert len(slots) == 1, f"{name}: the enemy's slot ({entity:#04x}) " + str(
                    [(before[SPRITE_ENTITY + k], before[SPRITE_X + k], before[SPRITE_Y + k]) for k in range(8)])
                k = slots[0]
                at = before[SPRITE_Y + k] | before[SPRITE_X + k] >> 4
                assert at == (moved if on else stock), f"{name}: the enemy starts at {at:#04x}, feature {on}"
                assert before[IFRAMES] == 0, f"{name}: still invincible on arrival"
                hit = next((i - first for i in range(first, len(rams))
                            if rams[i][IFRAMES] == 0x3C and rams[i - 1][IFRAMES] != 0x3C), None)
                hp = (rams[first - 1][HP], rams[-1][HP])
                seen[on] = hit
                if on:
                    assert hit is None or hit >= SAFE, f"{name}: hit {hit} frames after arriving, with the fix"
                else:
                    assert hit is not None and hit <= HIT and hp[1] < hp[0], \
                        f"{name}: stock hit {hit} frames after arriving (HP {hp})"
            hero = (rams[first][X], rams[first][Y])
            print(f"ok: {name} (hero at {hero}): stock {entity:#04x} at {stock:#04x} hits him {seen[False]} "
                  f"frames after his first update; moved to {moved:#04x}, "
                  + ("nothing hits him" if seen[True] is None else f"the first hit is {seen[True]} frames in")
                  + f" ({SAFE} frames watched)", flush=True)

        if seamless_edges:
            # With Seamless on, an edge is no blind way in: the room beyond is
            # on screen and its actors run before he crosses. What matters is
            # that the enemy starts at its new block there too, whichever
            # machine loads the room (Seamless's isolated one, or the game's
            # own scroll where a seam is not joined): the move's use count.
            table = [tuple(m) for m in game.tcp("entrances_state")["table"]]
            for name, state, key, (area, screen, entity, stock, moved) in seamless_edges:
                i = table.index((area, screen, entity, stock, moved))
                used = game.tcp("entrances_state")["uses"][i]
                game.tcp("load_state", path=str(state))
                for _ in range(60):
                    if game.tcp("entrances_state")["uses"][i] > used:
                        break
                    if key:
                        game.tcp("action", name=key.lower(), frames=8)
                    game.wait(8)
                after = game.tcp("entrances_state")["uses"][i]
                assert after > used, f"{name}: the move was not made ({used} -> {after})"
                print(f"ok: {name}: with Seamless, the room beyond is loaded with {entity:#04x} at {moved:#04x}",
                      flush=True)

        if seamless:
            # 3. Seamless's neighbouring room, loaded on an isolated machine.
            mist5 = out / "mist5.cycstate"
            fixture(temple, mist5, {SAVED_AREA: MIST, SAVED_SCREEN: 0x05, SAVED_PALETTE: 0x0A, SAVED_POS: 0x9C,
                                    AREA_REGION: REGION[MIST], HP: 0x50})
            before = game.tcp("entrances_state")
            game.tcp("load_state", path=str(mist5))
            for _ in range(200):
                if game.ram(AREA) != BUILDINGS:
                    break
                game.tcp("action", name="right", frames=2)
                game.wait(2)
            game.until("the Mist's screen 5", lambda: (game.ram(AREA), game.ram(SCREEN)) == (MIST, 0x05), 600)
            room = None
            for _ in range(400):
                room = game.tcp("fax_world_room", room=6)
                if room["live"] and GAWAINE in room["entities"]:
                    break
                time.sleep(0.005)
            assert room and room["live"], room
            after = game.tcp("entrances_state")
            assert after["seamless"] > before["seamless"], (before, after)
            last = after.get("last_room", {})
            assert (last.get("area"), last.get("screen"), last.get("entity"), last.get("from"), last.get("to")) == \
                (MIST, 0x06, GAWAINE, 0x89, 0x9E), after
            # Where he is now: he started at the new block and has moved at
            # most a pixel a tick since (or the stock one, 80 pixels off).
            xs = [x for e, x in zip(room["entities"], room["x"]) if e == GAWAINE]
            moved_x = (0x9E & 0x0F) << 4
            assert any(abs(x - moved_x) <= room["ticks"] + 8 for x in xs), (room, xs)
            print(f"ok: Seamless loads the Mist's screen 6 beside screen 5 with Sir Gawaine moved to $9E "
                  f"(x={xs} after {room['ticks']} ticks), on its isolated machine")
        print(f"all fair entrances checks passed ({runs.count} headless runs)")
    finally:
        game.close()


if __name__ == "__main__":
    main()
