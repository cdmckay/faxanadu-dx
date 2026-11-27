"""Play a room's arrival in Seamless end to end, headless, every frame (#204):
from the first frame the game's picture shows a room it has loaded, the room
sits where Seamless keeps it and never moves while the hero stands, and the
room he leaves never moves either while the game still shows it. Whatever
moves, moves while the screen is black.

    python3 tests/world/arrivals.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/arrivals

Seamless used to take a new room only on a frame that ended in play, with
neither its script mode (a door's "I've used key.") nor its death mode (a
zone respawn) on, and those went off only at the game's next main loop tick,
8 to 15 frames after the room's picture came back: until then it showed the
game's own picture centred in the 424x240 view, and the room jumped to its
place as the loop began, out of Eolis's exit door 84 pixels left as the Trunk
came in. And on the first frame of a town's edge the game's RAM is already in
the new area while its picture still shows the old room, which Seamless
centred for that frame.

Each part plays a load from a saved game headless, frame exact: once to find
the frame the game's RAM is first in the new room, once to save a state LEAD
frames before it, and from that state, the buttons as they were down to that
frame and let go after it, with every frame's presented picture
(--present-out), the game's own picture (--screenshot), RAM (--frame-log) and
Seamless's trace (NESRECOMP_WORLD_TRACE). In a frame whose own picture shows
something below the status bar, the room's place in the presented picture is
found: where the game's own playfield lies in it (its 8-pixel runs equal,
more than three quarters of those in the view: the hero, the monsters and the
zone's name are drawn over it). Before the first frame whose own picture
shows the new room, the room he leaves stays where it was on every frame the
picture still shows it. From that frame on, for as long as the hero stands
where he arrived, the new room is at one place on every frame, the place
where the trace says its top-left is (origin_x, origin_y); and those frames
reach at least PLAY that Seamless composes in play.

1. door: up at Eolis's exit door with the J key in hand, and A through "I've
   used key.", into the Trunk's $00: the room's left edge at the view's.
2. town: left out of Apolune by the town's edge (Game_SetupNewArea) onto the
   Trunk's $07; the game's picture still shows Apolune on the first frame its
   RAM is in the Trunk.
3. tower: out of Eolis's meat shop, its door edited to lead to Trunk $0B's
   Tower of Trunk door (the RAM tests/block/harness.py's warp() writes, an
   external prerequisite, not coverage), with the J key in hand: up through the door,
   and A through its line, into $0D, a door within an area.
4. respawn: dead on the tower's $0E (zone_respawn.py's respawn-died-tower-0e,
   as the Guru speaks), A through his words, and back at the tower's door on
   $0D by zone respawn, a death's respawn outdoors.
5. way-out: out of Eolis's meat shop onto $01, a building's way out, which
   Seamless took in play by the frame it showed: it stays so.

Screenshots: each part's first frame that shows the room, and its PLAY-th
frame of Seamless's play.
"""
import csv
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "movement"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "keys"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "respawn"))
from fax_e2e import fixture, parse_args  # noqa: E402
import play  # noqa: E402  (mod catalogs for headless runs)
import auto_and_reusable_keys  # noqa: E402,F401  (its keys-eolis-exit fixture)
import zone_respawn  # noqa: E402,F401  (its respawn-died-tower-0e fixture)

AREA, SCREEN, POS_X, POS_Y = 0x24, 0x63, 0x9E, 0xA1
HP, OINTMENT = 0x431, 0x427                                         # DurationOintment: below $80, no touch gets through
SELECTED_ITEM, NUMBER_OF_ITEMS, KEY_J = 0x3C1, 0x3C6, 0x07
AREA_SAVED_AREA, AREA_SAVED_SCREEN, SCREEN_SAVED_PALETTE, PLAYER_SAVED_POS = 0x3D5, 0x3D6, 0x3D7, 0x3D8
AREA_REGION = 0x435                     # where a building's door leads: tests/block/harness.py's warp() writes the same
EOLIS, TRUNK = 0, 1
TOWER_DOOR, TOWER = 0x0B, 0x0D                                      # Trunk: the Tower of Trunk's door (J), and inside
HUD, NATIVE_W, VIEW_W, VIEW_H = 32, 256, 424, 240
SHARE = 0.75                    # of the game's playfield's 8-pixel runs in the view, equal where the room is placed
PLAY = 5                        # frames of Seamless's own composing, in play, the check must reach
LEAD = 4                        # frames before the load's first (its RAM in the new room) the pictures start
PARTS = ("door", "town", "tower", "respawn", "way-out")


def png(path):
    """A picture's width and rows of RGB bytes. The host writes them stored
    and unfiltered (cyc_png.c), so each row is a slice."""
    data = Path(path).read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos, idat = 8, b""
    while pos < len(data):
        size, kind = struct.unpack_from(">I4s", data, pos)
        body = data[pos + 8:pos + 8 + size]
        if kind == b"IHDR":
            width, height, depth, color = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and color in (2, 6), (depth, color)
        elif kind == b"IDAT":
            idat += body
        pos += 12 + size
    bpp = 3 if color == 2 else 4
    raw, stride = zlib.decompress(idat), width * bpp + 1
    assert all(raw[y * stride] == 0 for y in range(height)), f"{Path(path).name}: a filtered row"
    rows = [raw[y * stride + 1:(y + 1) * stride] for y in range(height)]
    if bpp == 4:
        rows = [bytes(b for i, b in enumerate(row) if i % 4 < 3) for row in rows]
    return width, rows


def shows(own):
    """The game's own picture shows something below the status bar."""
    return any(any(row) for row in own[HUD:])


def placement(own, shown, width):
    """Where the game's own playfield (its lines 32-239) lies in the presented
    picture: (x, y) of its top-left there, and the share of its 8-pixel runs
    in the view that are equal there. Candidates come from where 16-pixel
    runs of the playfield with 3 colours or more turn up in the picture; the
    best of them by that share wins. None when no run turns up."""
    runs = []
    for y in range(HUD + 4, VIEW_H, 6):
        for x in range(0, NATIVE_W - 16, 12):
            run = own[y][x * 3:(x + 16) * 3]
            colours = len({run[i:i + 3] for i in range(0, 48, 3)})
            if colours >= 3:
                runs.append((colours, x, y, run))
    runs = sorted(runs, reverse=True)[:60]
    votes = {}
    for _, x, y, run in runs:
        for sy in range(HUD, VIEW_H):
            row, at = shown[sy], shown[sy].find(run)
            while at >= 0:
                if at % 3 == 0:
                    key = (at // 3 - x, sy - y)
                    votes[key] = votes.get(key, 0) + 1
                at = row.find(run, at + 1)
    best = None
    for dx, dy in sorted(votes, key=votes.get, reverse=True)[:6]:
        equal = total = 0
        for y in range(HUD, VIEW_H, 2):
            sy = y + dy
            if not HUD <= sy < VIEW_H:
                continue
            for x in range(0, NATIVE_W, 8):
                sx = x + dx
                if 0 <= sx and sx + 8 <= width:
                    total += 1
                    equal += own[y][x * 3:(x + 8) * 3] == shown[sy][sx * 3:(sx + 8) * 3]
        if total and (best is None or equal / total > best[2]):
            best = (dx, dy + HUD, equal / total)
    return best


class Game(play.Game):
    """play.Game's mod catalogs and save states' frames, and headless runs with every frame's pictures."""

    def run(self, state, schedule, frames, mods, pictures=False, save=None):
        """Play `frames` frames headless from `state`, holding buttons on a
        schedule of (frame, buttons) from its frame. Returns each frame's RAM
        (after it), Seamless's trace lines (dicts of ints, None where blank),
        and with `pictures`, the folder holding each frame's presented picture
        (shown_FRAME.png) and the game's own (own_FRAME.png), with the state's
        frame. With `save`, (frame, path): a state saved after that frame."""
        start = self.continues_at(state)
        run = Path(tempfile.mkdtemp(prefix="run-", dir=self.out))
        mods = shutil.copytree(mods, run / "mods")
        (run / "input.txt").write_text("".join(f"{start + at} {buttons}\n" for at, buttons in sorted(schedule)))
        log, trace = run / "frames.bin", run / "world.csv"
        args = [str(self.exe), str(self.rom), "--load-state", str(state), "--input", str(run / "input.txt"),
                "--frames", str(start + frames), "--frame-log", str(log),
                "--frame-log-frames", f"{start}:{start + frames - 1}", "--mods-root", str(mods), "--no-save"]
        if pictures:
            args += ["--present-out", str(run / "shown.png"), "--present-every", "1",
                     "--screenshot", str(run / "own.png"), "--shot-every", "1"]
        if save:
            args += ["--save-state", f"{start + save[0]}:{save[1]}"]
        done = subprocess.run(args, capture_output=True, text=True, env=dict(os.environ, NESRECOMP_WORLD_TRACE=str(trace)))
        assert done.returncode == 0, done.stdout + done.stderr
        data = log.read_bytes()
        assert data[:8] == b"CYCFRAME"
        rams, pos = [], 12
        while pos < len(data):
            head = struct.unpack_from("<9I", data, pos)
            pos += 36 + 32 + 256
            rams.append(data[pos:pos + head[3]])
            pos += head[3] + sum(head[4:9])
        with open(trace, newline="") as f:
            rows = [{k: (int(v) if v not in ("", None) and k != "simulation_ms" else None) for k, v in r.items()}
                    for r in csv.DictReader(f)]
        assert len(rams) == frames and len(rows) == frames, (len(rams), len(rows), frames)
        if not pictures:
            shutil.rmtree(run)
            return rams, rows, None, start
        return rams, rows, run, start

    def arrival(self, name, state, schedule, frames, mods, loaded, after=80):
        """Play `schedule` from `state` until the frame whose RAM is first
        `loaded` (within `frames`), then that load again from LEAD frames
        before it, the buttons as the schedule has them down to that frame and
        let go after it, for `after` frames, every frame's pictures kept, and
        check the room stays put from the first frame it shows, and the room
        he leaves until then: what checked() says. Returns that."""
        rams, _, _, _ = self.run(state, schedule, frames, mods)
        load = next((i for i, ram in enumerate(rams) if loaded(ram)), None)
        assert load is not None and load > LEAD, (name, "the load never came", load)
        before = self.out / f"{name}-load.cycstate"
        self.run(state, schedule, load - LEAD, mods, save=(load - LEAD - 1, before))

        def held(frame):
            return ([buttons for at, buttons in sorted(schedule) if at <= frame][-1:] or ["-"])[0]

        replay = [(i, held(load - LEAD + i)) for i in range(LEAD + 1)] + [(LEAD + 1, "-")]
        rams, rows, run, start = self.run(before, replay, after, mods, pictures=True)
        try:
            return self.checked(name, rams, rows, run, start, loaded)
        finally:
            shutil.rmtree(run)

    def checked(self, name, rams, rows, run, start, loaded):
        """From the first frame whose own picture shows the loaded room, while
        the hero stands where he arrived: the room's place in each presented
        picture, all one, and the trace's; at least PLAY of the frames
        Seamless's own, in play. Returns (first frame, place, frames
        checked, frames before Seamless's own composing). The first frame's
        picture and the one PLAY frames into play are kept as screenshots."""
        def own(i):
            return png(run / f"own_{start + i:05d}.png")[1]

        first = next((i for i in range(len(rams)) if loaded(rams[i]) and shows(own(i))), None)
        assert first is not None, (name, "the room never showed")
        # Before it: the room he leaves, wherever the game's picture still shows it, where it was.
        left = []
        for i in range(first):
            if shows(own(i)):
                width, shown = png(run / f"shown_{start + i:05d}.png")
                found = placement(own(i), shown, width)
                assert found and found[2] > SHARE, (name, i, "the room he leaves is nowhere in the picture", found)
                left.append((i, found[:2]))
        moves = [(left[k - 1], left[k]) for k in range(1, len(left)) if left[k][1] != left[k - 1][1]]
        assert not moves, (name, "the room he leaves moved as the load began: (frame, (x, y)) before and after",
                           moves)
        hero = lambda i: (rams[i][AREA], rams[i][SCREEN], rams[i][POS_X], rams[i][POS_Y])  # noqa: E731
        places, play_frames = [], []
        for i in range(first, len(rams)):
            if hero(i) != hero(first):
                break
            width, shown = png(run / f"shown_{start + i:05d}.png")
            assert width == VIEW_W, (name, i, width)
            found = placement(own(i), shown, width)
            assert found and found[2] > SHARE, (name, i, "the room is nowhere in the presented picture", found)
            places.append((i, found[:2]))
            row = rows[i]
            if row["camera_x"] is not None and not row.get("arriving"):
                play_frames.append(i)
        moves = [(places[k - 1], places[k]) for k in range(1, len(places)) if places[k][1] != places[k - 1][1]]
        assert not moves, (name, f"the room moved after it showed on frame {first}: (frame, (x, y)) before and after",
                           moves)
        place = places[0][1]
        assert len(play_frames) >= PLAY, (name, f"only {len(play_frames)} frames of Seamless's play before he moved",
                                          len(places))
        traced = [(i, (rows[i].get("origin_x"), rows[i].get("origin_y"))) for i, _ in places]
        wrong = [(i, o) for i, o in traced if o != place]
        assert not wrong, (name, f"the trace's place for the room is not {place}, where it shows", wrong[:5])
        for i, what in ((first, "first"), (play_frames[PLAY - 1], "in-play")):
            path = self.out / f"{len(self.shots):02d}-{name}-{what}.png"
            shutil.copy(run / f"shown_{start + i:05d}.png", path)
            self.shots.append(path.name)
        return first, place, len(places), play_frames[0] - first, left


def report(what, result):
    first, place, frames, before, left = result
    leaving = (f"; the room he left stayed at {left[0][1]} on the {len(left)} frames the game still showed it"
               if left else "")
    print(f"ok: {what}: from the first frame its picture shows (frame {first} of the run, {before} frames before "
          f"Seamless composes it in play), the room sits at {place}, Seamless's place for it, on all {frames} frames "
          f"while he stands{leaving}")


def main():
    args = parse_args(__doc__, parts=PARTS)
    game = Game.from_args(args)
    out = game.out
    try:
        door = game.state("keys-eolis-exit") if args.wants("door") else None
        apolune = game.state("apolune") if args.wants("town") else None
        shop = game.state("eolis-meat-shop") if args.wants("tower") or args.wants("way-out") else None
        died = game.state("respawn-died-tower-0e") if args.wants("respawn") else None
        mods = game.mods("mods-on")

        def a_presses(begin, end):
            """A down 6 frames in every 24 from `begin`: a page turns on each new press once typed."""
            return [(at, "A" if (at - begin) % 24 == 0 else "-") for at in range(begin, end, 6)]

        if args.wants("door"):
            state = out / "door.cycstate"
            fixture(door, state, {HP: 0x50, NUMBER_OF_ITEMS: 0, SELECTED_ITEM: KEY_J})
            result = game.arrival("door", state, [(5, "UP"), (9, "-")] + a_presses(60, 400), 400, mods,
                                  lambda ram: ram[AREA] == TRUNK)
            assert result[1] == (0, HUD), ("the Trunk's $00 at the view's left edge", result)
            report("Eolis's exit door into the Trunk's $00", result)

        if args.wants("town"):
            result = game.arrival("town", apolune, [(0, "LEFT")], 900, mods,
                                  lambda ram: (ram[AREA], ram[SCREEN]) == (TRUNK, 0x07))
            assert result[4], ("the game still showed Apolune as the load began", result)
            report("left out of Apolune by the town's edge onto the Trunk's $07", result)

        if args.wants("tower"):
            state = out / "tower.cycstate"
            fixture(shop, state, {AREA_SAVED_AREA: TRUNK, AREA_SAVED_SCREEN: TOWER_DOOR, SCREEN_SAVED_PALETTE: 0x06,
                                  PLAYER_SAVED_POS: 0xA9, AREA_REGION: TRUNK, HP: 0x50, OINTMENT: 0x7F,
                                  NUMBER_OF_ITEMS: 0, SELECTED_ITEM: KEY_J})
            rams, _, _, _ = game.run(state, [(0, "RIGHT")], 200, mods)
            out_at = next(i for i, ram in enumerate(rams) if ram[AREA] == TRUNK)
            walk = [(0, "RIGHT"), (out_at, "-"), (out_at + 90, "UP"), (out_at + 94, "-")] + \
                a_presses(out_at + 120, out_at + 600)
            result = game.arrival("tower", state, walk, out_at + 600, mods,
                                  lambda ram: (ram[AREA], ram[SCREEN]) == (TRUNK, TOWER))
            report("through the Tower of Trunk's door on $0B into $0D", result)

        if args.wants("respawn"):
            result = game.arrival("respawn", died, a_presses(10, 1500), 1500, mods,
                                  lambda ram: (ram[AREA], ram[SCREEN]) == (TRUNK, TOWER))
            report("dead on the Tower of Trunk's $0E, back at its door on $0D by zone respawn", result)

        if args.wants("way-out"):
            result = game.arrival("way-out", shop, [(0, "RIGHT")], 200, mods, lambda ram: ram[AREA] == EOLIS)
            report("out of Eolis's meat shop onto $01", result)
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    main()
