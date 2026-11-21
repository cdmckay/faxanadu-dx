"""Play Fair entrances end to end: each enemy it moves, at the way in that
made its placement unfair, in the original game and in the remaster.

    python3 tests/entrances/fair_entrances.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/entrances
    python3 tests/entrances/fair_entrances.py ... --game-arg=--world-view=seamless
    python3 tests/entrances/fair_entrances.py ... --only 'Mist $08'     # the spots whose name has this

A hidden window over TCP starts a new game, and (an external prerequisite, not
coverage) a save-state RAM fixture kills the hero with Mascon as his furthest
temple and a Long Sword, Leather Armor and a Small Shield, so the death path
wakes him in that temple. Every way in starts from that saved game, edited
into a fixture, and plays headless from it, frame by frame, with the same
--game-arg options (so the same world view):

- Mascon's temple: walking out of it, as after every death there. It opens on
  the Mist's screen 6.
- A building's way out: the temple's door edited to lead where the building's
  does (Area_SavedArea, Area_SavedScreen, Screen_SavedPalette and
  Player_SavedPosXY, which Game_EnterBuilding sets from where the hero stood
  at the building's door), then walking out.
- A door within an area: the temple's door edited to lead to the door's block
  on its own screen, walking out, and Up pressed on his first update there;
  for Branch's K-key door with the key selected and its message paged with A.
- A screen's edge, a ladder or a drop from the screen above: the temple's
  door edited to lead onto the screen beside it, walked out of, and
  (prerequisites, not coverage) that screen's enemies cleared, his
  invincibility spent, HP full and him put at the edge; then holding the way
  in (nothing, for a drop) across it.

Nothing else is pressed from the arrival on. Each is played with the remaster
off (the original game, the whole mod switched off), then on:

1. Off, the enemy is at its stock block on the frame before his first update,
   and hits him (Player_InvincibilityPhase $3C, HP down) within the spot's
   frames of that update, where no move he starts once he could react (13
   frames in) escapes it: docs/GAME_INTERNALS.md has each measurement.
2. On, the enemy starts at its new block, and nothing hits him for SAFE
   frames.
3. With Seamless on, an edge is no blind way in: the room beyond is on screen,
   its actors running, before he crosses. Instead, walking to the edge has the
   enemy start at its new block in the room Seamless loads (the move's use
   count). And walking from the Mist's screen 5 toward screen 6 has Seamless
   load screen 6 on an isolated machine, where hooks do not run: its Sir
   Gawaine starts at the new block there too, read from Seamless's own room
   record.
"""
from concurrent.futures import ThreadPoolExecutor
import itertools
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import threading

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
from fax_e2e import Game, features_toml, fixture, parser  # noqa: E402

# RAM
AREA, SCREEN, SCREEN_READY, SCROLL_DIRECTION = 0x24, 0x63, 0x0E, 0x54
X, X_FRACTION, Y, FLAGS, STATUS, IFRAMES = 0x9E, 0x9D, 0xA1, 0xA4, 0xA5, 0xAD
SPRITE_X, SPRITE_Y, SPRITE_ENTITY, SHADOW_OAM = 0xBA, 0xC2, 0x2CC, 0x700
MESSAGE_ID, SELECTED_WEAPON, SELECTED_ARMOR, SELECTED_SHIELD = 0x213, 0x3BD, 0x3BE, 0x3BF
SELECTED_ITEM, PLAYER_CUR_WEAPON = 0x3C1, 0x3C8
SAVED_AREA, SAVED_SCREEN, SAVED_PALETTE, SAVED_POS = 0x3D5, 0x3D6, 0x3D7, 0x3D8
HP, AREA_REGION, PLAYER_IS_DEAD, TEMPLE_SPAWN_POINT = 0x431, 0x435, 0x438, 0x439
LONG_SWORD, LEATHER_ARMOR, SMALL_SHIELD, KEY_K = 1, 0, 0, 0x05     # ITEMINVENTORY_KEY_K
BUILDINGS, NEGATIVE_THOUGHTS, MASCON = 4, 0x24, 3
PACKAGE, FEATURE = "faxanadu.remastered", "remastered"              # the one switch: off is the original game
HOLD = {"right": "RIGHT", "left": "LEFT", "up": "UP", "down": "DOWN", "drop": None}
SAFE = 30                   # frames after his first update with the fix on, and nothing hits him
TRUNK, MIST, BRANCH, DARTMOOR, ZENIS = 1, 2, 5, 6, 7
REGION = {TRUNK: 1, MIST: 2, BRANCH: 3, DARTMOOR: 4, ZENIS: 5}
RAIDEN, ZOMBIE, SUGATA, GIANT_STRIDER, GAWAINE = 0x04, 0x06, 0x18, 0x1E, 0x1F
MASKMAN, WOLFMAN, IKEDA, LAMPREY = 0x20, 0x21, 0x26, 0x28

# Each spot: what it is, the screen (area, screen, palette), the way in, the
# enemy (entity, stock block, new block; YX as bank 11 stores them), and the
# latest frame its stock placement first hits him. A way in is ("temple",),
# ("building", start YX), ("door", source screen, its palette, the door's
# block, the key to select or None), or ("edge", kind, source screen, its
# palette, column, row), kind being left, right, up, down or drop.
SPOTS = [
    ("Mascon's temple", (MIST, 0x06, 0x0A), ("temple",), (GAWAINE, 0x89, 0x9E), 16),
    ("Mist $06, building $22's way out", (MIST, 0x06, 0x0A), ("building", 0x87), (GAWAINE, 0x89, 0x9E), 16),
    ("the Tower of Mist's way out", (MIST, 0x17, 0x0A), ("door", 0x45, 0x0B, 0xA0, None), (RAIDEN, 0xA5, 0xA9), 16),
    ("Mist $1F, building $26's way out", (MIST, 0x1F, 0x0A), ("building", 0x94), (SUGATA, 0x95, 0x97), 16),
    ("the tower on Mist $20's way out", (MIST, 0x20, 0x0A), ("door", 0x50, 0x0B, 0xA2, None), (GAWAINE, 0x96, 0x46),
     16),
    ("Mist $29, building $20's way out", (MIST, 0x29, 0x0A), ("building", 0x7C), (IKEDA, 0x7D, 0x7E), 16),
    ("Trunk $06, building $20's way out", (TRUNK, 0x06, 0x06), ("building", 0x5A), (ZOMBIE, 0x5B, 0x5C), 16),
    ("Branch $0E, the door from $0F", (BRANCH, 0x0E, 0x08), ("door", 0x0F, 0x09, 0x60, None), (LAMPREY, 0x6D, 0x6A),
     16),
    ("Branch $13, the door from $14", (BRANCH, 0x13, 0x09), ("door", 0x14, 0x08, 0x80, KEY_K), (MASKMAN, 0x8C, 0x9A),
     16),
    ("Dartmoor $02, building $22's way out", (DARTMOOR, 0x02, 0x0C), ("building", 0x4A),
     (GIANT_STRIDER, 0x49, 0xA9), 16),
    ("Dartmoor $03, building $24's way out", (DARTMOOR, 0x03, 0x0C), ("building", 0xAB), (LAMPREY, 0xAA, 0xA8), 16),
    ("Dartmoor $08, building $25's way out", (DARTMOOR, 0x08, 0x0C), ("building", 0x41), (GAWAINE, 0x42, 0x46), 16),
    ("the Castle of Fraternal's way out", (DARTMOOR, 0x0C, 0x0C), ("door", 0x10, 0x0D, 0x90, None),
     (WOLFMAN, 0xAB, 0xAD), 16),
    # EDGES
]


def visible(ram):
    """Sprites the update drew this frame, the status bar's sprite 0 included."""
    return sum(1 for k in range(0, 256, 4) if ram[SHADOW_OAM + k] < 0xEF)


def scrolled_in(rams, area, screen):
    """The first update on (area, screen) after a scroll: the frame after the
    one the scroll ends on there."""
    return next((i + 1 for i, r in enumerate(rams)
                 if (r[AREA], r[SCREEN]) == (area, screen) and r[SCROLL_DIRECTION] == 0xFF
                 and r[SCREEN_READY] == 0), None)


def first_hit(rams, first):
    """Frames from `first` to the first hit (invincibility rising to $3C), or None."""
    return next((i - first for i in range(first, len(rams))
                 if rams[i][IFRAMES] == 0x3C and rams[i - 1][IFRAMES] != 0x3C), None)


def slug(name):
    return re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")


class Runs:
    """Headless runs of the copied build from save states, with a button
    schedule and a per-frame RAM log (tests/movement/play.py's headless, with
    the world view's --game-arg options), safe to run several at once."""

    def __init__(self, game, game_args):
        self.game, self.exe, self.rom, self.out = game, game.exe, game.rom, game.out
        self.args = [part for arg in game_args for part in arg.split("=", 1)]
        self.ids = itertools.count()
        self.lock = threading.Lock()

    def mods(self, on):
        """A mod catalog: the remaster on, or off for the original game. One
        per thread: a run publishes its mod state into it."""
        root = self.out / f"mods-{'on' if on else 'off'}-{threading.get_ident()}"
        if not root.exists():
            shutil.copytree(self.out / "game" / "mods" / "packages", root / "packages")
            (root / "state.toml").write_text(features_toml({} if on else {(PACKAGE, FEATURE): False}))
        return root

    def run(self, state, schedule, frames, on, save=None):
        """Run from `state` for `frames` frames, holding buttons on a schedule
        of (frame, buttons) pairs counted from the state's frame ("RIGHT+A",
        "-" for none). Returns each frame's CPU RAM, after the frame. With
        `save`, (frame, path): also save a state after that frame."""
        with self.lock:
            work = self.out / "runs" / str(next(self.ids))
        work.mkdir(parents=True)
        start = struct.unpack_from("<I", Path(state).read_bytes(), 28)[0]      # the frame it continues at
        (work / "input.txt").write_text("".join(f"{start + at} {buttons}\n" for at, buttons in sorted(schedule)))
        log = work / "frames.bin"
        cmd = [str(self.exe), str(self.rom), "--load-state", str(state), "--input", str(work / "input.txt"),
               "--frames", str(start + frames), "--frame-log", str(log),
               "--frame-log-frames", f"{start}:{start + frames - 1}", "--mods-root", str(self.mods(on)),
               "--no-save", *self.args]
        if save:
            cmd += ["--save-state", f"{start + save[0]}:{save[1]}"]
        done = subprocess.run(cmd, capture_output=True, text=True, cwd=work)
        assert done.returncode == 0, done.stdout + done.stderr
        data = log.read_bytes()
        shutil.rmtree(work)
        assert data[:8] == b"CYCFRAME"
        rams, pos = [], 12
        while pos < len(data):
            head = struct.unpack_from("<9I", data, pos)
            pos += 36 + 32 + 256
            rams.append(data[pos:pos + head[3]])
            pos += head[3] + sum(head[4:9])
        assert len(rams) == frames, (len(rams), frames)
        return rams


class Spots:
    """Each spot's way in, from the temple's saved game, and its two checks."""

    def __init__(self, runs, temple, exit_frame):
        self.runs, self.out, self.temple, self.exit_frame = runs, runs.out, temple, exit_frame
        self.walk = [(0, "RIGHT"), (exit_frame, "-")]

    def leading_to(self, tag, area, screen, palette, pos, more=None):
        """The temple's saved game with its door edited to lead to `pos` on
        (area, screen)."""
        state = self.out / f"{tag}.cycstate"
        fixture(self.temple, state, {SAVED_AREA: area, SAVED_SCREEN: screen, SAVED_PALETTE: palette,
                                     SAVED_POS: pos, AREA_REGION: REGION[area], HP: 0x50, **(more or {})})
        return state

    def arrive(self, state, area, screen, schedule, frames, on):
        """Play `schedule`; the frames, and the first update on (area, screen)
        after a building's or door's screen load (the frame its sprites are
        first drawn)."""
        rams = self.runs.run(state, schedule, frames, on)
        came = next((i for i, r in enumerate(rams) if (r[AREA], r[SCREEN]) == (area, screen)), None)
        assert came is not None, f"never reached {area}:{screen:#04x}: at {rams[-1][AREA]}:{rams[-1][SCREEN]:#04x}"
        blank = next(i for i in range(max(came - 1, 0), len(rams)) if visible(rams[i]) <= 1)
        return rams, next(i for i in range(blank, len(rams)) if visible(rams[i]) > 1)

    def door(self, state, src, door, key, on):
        """The walk out of the temple to `door` on `src`, Up on the first
        update there (and the key's message paged after it), and the frame the
        door's screen starts loading."""
        _, first = self.arrive(state, src[0], src[1], self.walk, self.exit_frame + 120, on)
        sched = self.walk + [(first, "UP"), (first + 4, "-")]
        if key is not None:
            for k in range(20):                          # "I've used key.": A, 6 frames, every 16
                sched += [(first + 10 + 16 * k, "A"), (first + 16 + 16 * k, "-")]
        rams = self.runs.run(state, sched, first + 700, on)
        left = next(i for i in range(first, len(rams)) if (rams[i][AREA], rams[i][SCREEN]) != src)
        assert rams[left][IFRAMES] == 0, f"hit on {src} before the door"
        return [step for step in sched if step[0] < left] + [(left, "-")], left

    def source(self, tag, area, way):
        """The temple's saved game, leading onto the edge's source screen."""
        _, kind, src, src_palette, col, row = way
        return self.leading_to(f"{tag}-src", area, src, src_palette, row << 4 | col)

    def edge(self, tag, area, way):
        """The hero at the edge of the source screen, its enemies gone, his
        invincibility spent and HP full (prerequisites, not coverage): from
        the walk out of the temple to there."""
        _, kind, src, src_palette, col, row = way
        there = self.source(tag, area, way)
        raw, ready = self.out / f"{tag}-raw.cycstate", self.out / f"{tag}-edge.cycstate"
        clear = {**{SPRITE_ENTITY + k: 0xFF for k in range(8)}, IFRAMES: 0, HP: 0x50}
        if kind == "drop":
            # He falls on his first update there: saved as the screen
            # finishes loading, before it.
            rams = self.runs.run(there, self.walk, self.exit_frame + 60, False)
            f = next(i for i in range(self.exit_frame, len(rams)) if (rams[i][AREA], rams[i][SCREEN]) == (area, src)
                     and rams[i][SCREEN_READY] == 0 and rams[i][SCROLL_DIRECTION] == 0xFF)
            self.runs.run(there, self.walk, f + 1, False, save=(f, raw))
            fixture(raw, ready, clear)
        else:
            self.runs.run(there, self.walk, self.exit_frame + 60, False, save=(self.exit_frame + 59, raw))
            fixture(raw, ready, {**clear, STATUS: 0, X: col * 16, X_FRACTION: 0, Y: row * 16,
                                 FLAGS: 0x00 if kind == "left" else 0x40})
        return ready

    def check(self, spot):
        """Play the spot with the remaster off, then on. Returns its report line."""
        name, (area, screen, palette), way, (entity, stock, moved), latest = spot
        tag = slug(name)
        if way[0] == "edge":
            state = self.edge(tag, area, way)
            key = HOLD[way[1]]
            plans = {on: (([(0, key)] if key else []), 420) for on in (False, True)}
        elif way[0] == "temple":
            state = self.temple
            plans = {on: (self.walk, self.exit_frame + 120) for on in (False, True)}
        elif way[0] == "building":
            state = self.leading_to(tag, area, screen, palette, way[1])
            plans = {on: (self.walk, self.exit_frame + 120) for on in (False, True)}
        else:
            _, src, src_palette, door, key = way
            state = self.leading_to(tag, area, src, src_palette, door, {SELECTED_ITEM: key} if key else None)
            plans = {}
            for on in (False, True):
                sched, left = self.door(state, (area, src), door, key, on)
                plans[on] = (sched, left + 120)
        seen = {}
        for on in (False, True):
            sched, frames = plans[on]
            if way[0] == "edge":
                rams = self.runs.run(state, sched, frames, on)
                first = scrolled_in(rams, area, screen)
                assert first is not None, f"{name}: never reached {area}:{screen:#04x}"
            else:
                rams, first = self.arrive(state, area, screen, sched, frames + SAFE + 40, on)
            before = rams[first - 1]
            slots = [k for k in range(8) if before[SPRITE_ENTITY + k] == entity and
                     before[SPRITE_Y + k] | before[SPRITE_X + k] >> 4 in (stock, moved)]
            assert len(slots) == 1, f"{name}: the enemy's slot ({entity:#04x}) " + str(
                [(before[SPRITE_ENTITY + k], before[SPRITE_X + k], before[SPRITE_Y + k]) for k in range(8)])
            k = slots[0]
            at = before[SPRITE_Y + k] | before[SPRITE_X + k] >> 4
            assert at == (moved if on else stock), f"{name}: the enemy starts at {at:#04x}, remaster on: {on}"
            assert before[IFRAMES] == 0, f"{name}: still invincible on arrival"
            hit = first_hit(rams, first)
            seen[on] = hit
            if on:
                assert hit is None or hit >= SAFE, f"{name}: hit {hit} frames after arriving, with the fix"
            else:
                hp = (rams[first - 1][HP], rams[-1][HP])
                assert hit is not None and hit <= latest and hp[1] < hp[0], \
                    f"{name}: stock hit {hit} frames after arriving (HP {hp}), expected by {latest}"
        hero = (rams[first][X], rams[first][Y])
        return (f"ok: {name} (hero at {hero}): in the original, {entity:#04x} at {stock:#04x} hits him "
                f"{seen[False]} frames after his first update; moved to {moved:#04x}, "
                + ("nothing hits him" if seen[True] is None else f"the first hit is {seen[True]} frames in")
                + f" ({SAFE} frames watched)")


def walk_out(game, there, key, test):
    """Load `there` in the window, walk out of the temple, then hold `key`
    (nothing, for a drop) until `test()`, counting frames."""
    game.tcp("load_state", path=str(there))
    for _ in range(200):
        if game.ram(AREA) != BUILDINGS:
            break
        game.tcp("action", name="right", frames=2)
        game.wait(2)
    for _ in range(60):
        if test():
            return True
        if key:
            game.tcp("action", name=key.lower(), frames=8)
        game.wait(8)
    return test()


def main():
    ap = parser(__doc__)
    ap.add_argument("--only", action="append", default=[], help="only the spots whose name contains this")
    ap.add_argument("--jobs", type=int, default=3, help="spots played at once (headless runs)")
    args = ap.parse_args()
    seamless = any("seamless" in a for a in args.game_arg)
    game = Game.from_args(args)
    out = game.out
    runs = Runs(game, args.game_arg)
    try:
        game.launch()
        assert game.tcp("entrances_state")["enabled"], "Fair entrances is on in the remaster"
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
        # from every fixture of this state, and in the original.
        rams = runs.run(temple, [(0, "RIGHT")], 120, False)
        exit_frame = next(i for i, r in enumerate(rams) if r[AREA] != BUILDINGS)
        spots = Spots(runs, temple, exit_frame)
        chosen = [s for s in SPOTS if not args.only or any(o in s[0] for o in args.only)]
        # With Seamless, an edge's room is on screen before he crosses: those
        # are checked in the window, below.
        played = [s for s in chosen if not (seamless and s[2][0] == "edge")]
        with ThreadPoolExecutor(max(1, args.jobs)) as pool:
            for line in pool.map(spots.check, played):
                print(line, flush=True)

        if seamless:
            # The enemy starts at its new block whichever machine loads the
            # room: Seamless's isolated one, or the game's own scroll where a
            # seam is not joined. The move's use count says it was made.
            table = [tuple(m) for m in game.tcp("entrances_state")["table"]]
            for name, (area, screen, _), way, (entity, stock, moved), _ in chosen:
                if way[0] != "edge":
                    continue
                there = spots.source(slug(name), area, way)
                i = table.index((area, screen, entity, stock, moved))
                used = game.tcp("entrances_state")["uses"][i]
                made = walk_out(game, there, HOLD[way[1]], lambda: game.tcp("entrances_state")["uses"][i] > used)
                assert made, f"{name}: the move was not made ({used} -> {game.tcp('entrances_state')['uses'][i]})"
                print(f"ok: {name}: with Seamless, the room beyond is loaded with {entity:#04x} at {moved:#04x}",
                      flush=True)

            # Seamless's neighbouring room, loaded on an isolated machine.
            mist5 = spots.leading_to("mist5", MIST, 0x05, 0x0A, 0x9C)
            before = game.tcp("entrances_state")
            room = {}

            def gawaine_live():
                room.update(game.tcp("fax_world_room", room=6))
                return room["live"] and GAWAINE in room["entities"]

            walk_out(game, mist5, None, lambda: (game.ram(AREA), game.ram(SCREEN)) == (MIST, 0x05))
            game.until("Seamless's room for the Mist's screen 6", gawaine_live, 400)
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
        print(f"all fair entrances checks passed ({next(runs.ids)} headless runs)")
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    main()
