"""Play Seams' loose ends (#166's slice 7) end to end, headless.

    python3 tests/seams/loose_ends.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/loose-ends

Every part plays headless from a save state, frame exact however loaded the
machine is: each frame's CPU RAM, states saved after chosen frames (the
neighbours' records) and Seamless's per-frame trace (NESRECOMP_WORLD_TRACE),
as tests/seams/vertical.py does, and the window's picture (--present-out).

One-in-four items. Out of Eolis's meat shop onto a Trunk screen, its door
edited (Area_SavedScreen and the rest, as tests/block/harness.py's warp() has
it; an external prerequisite, not coverage), under Ointment: each run starts
inside the shop, so the walk out is an area load and Seamless starts every
room afresh, with Screen_TrackedVisitCount ($043A) set in the same edit.

1. count: the original's rule first, the remaster off, in the stock view:
   out onto $0B with the count at 2, it does not move until he walks into
   $0C (2 to 3: no Red Potion, $5D, there); back into $0B and into $0C
   again, it moves again (3 to 0: the potion), a visit each time the screen
   is entered. Then the remaster's: onto $20, the count at 0, 1, 2 and 3.
   $21 beside it holds the same one-in-four Red Potion; met as a neighbour,
   its first frame counts, and the count goes up by one, once, and the potion
   lies in $21 only when the count was 3 (3 of 4 counts remove it). On
   master the count never moved.
2. in-turn: onto $1F, the count at 2: $1D, a room up and left, holds an
   Ointment that shows one visit in four ($60); it is met as he arrives
   (2 to 3: gone), and walking right into $20 meets $21 (3 to 0: the potion
   there). Walking back into $1F and into $20 again moves the count no more:
   once a room met, not a tick or a crossing.

The fountain's ladder. From fixtures made out of Apolune's temple, its door
edited to lead to Trunk (tests/ladders/ladders.py's way out):

3. fountain: the first opening first, loose-trunk-28, on the fountain in
   $28 with the three springs and the Ring of Ruby, the path not yet open:
   $28 has its stone and no ladder. Holding Left against the stone, the
   original's event plays: the stone moves aside, 30 frames later it clears
   and Quests gets $20, the ladder drops a block every 4 frames, and he
   stands still meanwhile: every frame's blocks, quests and place as the
   original's (the remaster off) from the same save and input. Then a later
   visit, loose-trunk-27, on $27 beside the fountain's screen with the path
   open (Quests $20, set before the area loaded): $28's record has its
   ladder (column 2, rows 2-8), the stone gone and its event cleared, as the
   path's screen event leaves the screen in the original. Walking left into
   $28, nothing runs as he crosses: the event stays cleared, the ladder is
   in the live room from the first frame there, and he never stands still
   (the original's event stops the game 28 frames while the ladder drops).
   On master $28 had its stone and its event.

A death at a seam. From eolis-town, on Eolis's room 1, room 8 right of it:

4. death: the hero dies at x 248, his right half across the seam in room 8.
   On the picture, at frames before the dissolve's loop draws him, as he
   stands whole, as he dissolves and as the screen fades, his pixels are
   those of the same death at x 120, in his room, wholly the NES's: across
   the seam too, the dissolve's rubbed lines included. Facing either way.
   On master he was cut at the seam.

5. save-load: saved by a headless game as the count is about to move (the
   in-turn walk), as he is about to cross into the fountain's screen, and as
   he dissolves at the seam: from each, two fresh games run on to the same
   machine and records (and the death's to the same picture).

Screenshots: the fountain's screen just past the seam with its ladder there,
and the death whole across the seam (Seamless's 424 view, --present-out).
"""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "movement"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ladders"))
import fax_e2e  # noqa: E402
from fax_e2e import fixture, parse_args  # noqa: E402
import play  # noqa: E402  (headless runs and mod catalogs)
from seams_lose_nothing import hw_ram  # noqa: E402
from open_ground import Record, png_rows  # noqa: E402
from ladders import _out_to  # noqa: E402  (out of Apolune's temple onto a Trunk screen)

AREA, SCREEN, HERO_X, HERO_Y, PLAYER_FLAGS = 0x24, 0x63, 0x9E, 0xA1, 0xA4    # Area_CurrentArea ... Player_Flags
AREA_SAVED_AREA, AREA_SAVED_SCREEN, SCREEN_SAVED_PALETTE, PLAYER_SAVED_POS = 0x3D5, 0x3D6, 0x3D7, 0x3D8
ENTITIES = 0x2CC                                                             # CurrentSprites_Entities
OINTMENT, SPECIAL_ITEMS, QUESTS, SPECIAL_EVENT = 0x427, 0x42C, 0x42D, 0x42E  # DurationOintment ... _SpecialEventID
HP, AREA_REGION, PLAYER_IS_DEAD, VISIT_COUNT = 0x431, 0x435, 0x438, 0x43A    # ... Screen_TrackedVisitCount
SCREEN_BUFFER = 0x600
EOLIS, TRUNK, BUILDINGS, TRUNK_PALETTE = 0, 1, 4, 0x06
RED_POTION_SOMETIMES, OINTMENT_SOMETIMES, EMPTY = 0x5D, 0x60, 0xFF          # one visit in four
RING_OF_RUBY, SPRINGS, PATH_TO_MASCON = 0x40, 0x07, 0x20                     # SpecialItems; Quests
FACING_RIGHT = 0x40                                                          # Player_Flags
LADDER, AIR, STONE, NO_EVENT = 0x20, 0x42, 0x88, 0xFF
FOUNTAIN_LADDER = [0x22 + 0x10 * row for row in range(7)]   # Game_DropLadderToMascon: column 2, rows 2-8
FOUNTAIN_STONES = [0x56, 0x66, 0x57, 0x67]                 # the cover, then where the event moves it
PUSHED_STONE = [0x55, 0x65]                                # pushed left (GAME_LADDER_TO_MASCON_BLOCK_OFFSETS $FF)
HUD = 32
PARTS = ("count", "in-turn", "fountain", "death", "save-load")


class Game(play.Game):
    """play.Game's headless runs and mod catalogs."""


@fax_e2e.recipe("loose-trunk-27", parent="apolune-temple")
def _trunk_27(g):
    """With the path to Mascon open (Quests: the three springs and $20), out
    of Apolune's temple through its door edited to lead to Trunk $27, on its
    floor by its left side: Trunk loads with the path open, so Seamless
    decodes the fountain's screen ($28) with it."""
    g.poke({QUESTS: SPRINGS | PATH_TO_MASCON})
    _out_to(g, 0x27, 0x91)


@fax_e2e.recipe("loose-trunk-28", parent="apolune-temple")
def _trunk_28(g):
    """With the three springs and the Ring of Ruby, the path not yet open,
    out onto the fountain in Trunk $28 (block $57), right of its stone."""
    g.poke({QUESTS: SPRINGS, SPECIAL_ITEMS: RING_OF_RUBY})
    _out_to(g, 0x28, 0x57)


def door_to(screen, pos):
    """The meat shop's door edited to lead onto Trunk `screen` at block `pos`."""
    return {AREA_SAVED_AREA: TRUNK, AREA_SAVED_SCREEN: screen, SCREEN_SAVED_PALETTE: TRUNK_PALETTE,
            PLAYER_SAVED_POS: pos, AREA_REGION: TRUNK, HP: 0x50, OINTMENT: 0x7F}


def changes(rams, addr):
    """(frame, value) each time a byte takes a new value, from the first frame's."""
    out, last = [], None
    for f, r in enumerate(rams):
        if r[addr] != last:
            out.append((f, r[addr]))
            last = r[addr]
    return out


def blocks(ram):
    return ram[SCREEN_BUFFER:SCREEN_BUFFER + 0xD0]


def main():
    args = parse_args(__doc__, parts=PARTS)
    game = Game.from_args(args)
    out = game.out

    def wanted(*names):
        return any(args.wants(n) for n in names)

    try:
        shop = game.state("eolis-meat-shop") if wanted("count", "in-turn", "save-load") else None
        trunk27 = game.state("loose-trunk-27") if wanted("fountain", "save-load") else None
        trunk28 = game.state("loose-trunk-28") if wanted("fountain") else None
        town = game.state("eolis-town") if wanted("death", "save-load") else None
        mods = game.mods("mods-on")
        original = game.mods("mods-off", original=True) if wanted("count", "fountain") else None

        def edited(case, source, writes, edits=None):
            path = out / f"{case}.cycstate"
            fixture(source, path, writes, {"faxanadu.world": edits} if edits else None)
            return path

        # ---- one-in-four items ----
        def walk_out(case, screen, count, schedule, frames, saves=(), catalog=None):
            path = edited(case, shop, {**door_to(screen, 0x91), VISIT_COUNT: count})
            if catalog:                                                  # the original: no Seamless, no trace
                return path, game.headless(path, schedule, frames, catalog), None, None
            end = out / f"{case}-end.cycstate"
            rams, rows = game.play(path, schedule, frames, mods, saves=[*saves, (frames - 1, end)])
            return path, rams, rows, Record(end.read_bytes())

        if args.wants("count"):
            # The stock view scrolls: Right into $0C, Left back into $0B, Right into $0C again.
            sched = [(0, "RIGHT"), (340, "LEFT"), (460, "RIGHT"), (640, "-")]
            _, rams, _, _ = walk_out("original", 0x0B, 2, sched, 700, catalog=original)
            screens = [r[SCREEN] if r[AREA] == TRUNK else None for r in rams]
            moved = changes(rams, VISIT_COUNT)
            entered = [f for f in range(1, len(rams)) if screens[f] == 0x0C and screens[f - 1] != 0x0C]
            assert len(entered) == 2, ("into $0C twice", entered, sorted(set(screens), key=str))
            assert [v for _, v in moved] == [2, 3, 0], ("a visit each time he enters $0C", moved)
            assert entered[0] < moved[1][0] < entered[1] < moved[2][0], (entered, moved)
            after = [rams[moved[i][0] + 10] for i in (1, 2)]
            assert [RED_POTION_SOMETIMES in r[ENTITIES:ENTITIES + 8] for r in after] == [False, True], \
                [list(r[ENTITIES:ENTITIES + 8]) for r in after]
            print(f"ok: the original (the remaster off): out onto Trunk $0B with the count at 2, it moves only as "
                  f"he enters $0C (frame {moved[1][0]}: 2 to 3, no Red Potion), and again as he enters it again "
                  f"(frame {moved[2][0]}: 3 to 0, the Red Potion): a visit each time the screen is entered")
            # The remaster's: a room met as a neighbour counts.
            def run(c):
                return walk_out(f"count-{c}", 0x20, c, [(0, "RIGHT"), (30, "-")], 90)
            with ThreadPoolExecutor(max_workers=4) as pool:
                results = list(pool.map(run, range(4)))
            shown = []
            for c, (_, rams, _, rec) in enumerate(results):
                assert rams[-1][AREA] == TRUNK and rams[-1][SCREEN] == 0x20, (c, rams[-1][AREA], rams[-1][SCREEN])
                moved = changes(rams, VISIT_COUNT)
                assert [v for _, v in moved] == [c, (c + 1) % 4], ("the count moves by one, once", c, moved)
                r21 = rec.ram(0x21)
                there = RED_POTION_SOMETIMES in r21[ENTITIES:ENTITIES + 8]
                assert there == (c == 3), ("$21's Red Potion lies there one count in four", c,
                                           list(r21[ENTITIES:ENTITIES + 8]))
                shown.append(there)
                if c == 0:
                    first_change = moved[1][0]
            print(f"ok: out onto Trunk $20 with the count at 0-3, $21 met as a neighbour counts one visit: "
                  f"0 to 1, 1 to 2, 2 to 3, 3 to 0 (on frame {first_change} of the walk out), and its one-in-four "
                  f"Red Potion lies there only from 3: {shown}")

        if wanted("in-turn", "save-load"):
            # Out onto $1F, right across it into $20, back into $1F, and into $20 again.
            turn = [(0, "RIGHT"), (250, "LEFT"), (310, "RIGHT"), (380, "-")]
            turn_path, turn_rams, _, turn_rec = walk_out("in-turn", 0x1F, 2, turn, 420)
            turn_moved = changes(turn_rams, VISIT_COUNT)

        if args.wants("in-turn"):
            rams = turn_rams
            screens = [r[SCREEN] for r in rams]
            assert [v for _, v in turn_moved] == [2, 3, 0], ("met in turn, a visit each", turn_moved)
            arrived = next(f for f, r in enumerate(rams) if r[AREA] == TRUNK and r[SCREEN] == 0x1F)
            into20 = screens.index(0x20)
            assert arrived <= turn_moved[1][0] < into20, ("$1D is met as he arrives on $1F", arrived, turn_moved,
                                                          into20)
            assert into20 <= turn_moved[2][0] <= into20 + 1, ("$21 is met as he crosses into $20", into20, turn_moved)
            back = next(f for f in range(into20, len(rams)) if screens[f] == 0x1F)
            again = next(f for f in range(back, len(rams)) if screens[f] == 0x20)
            assert screens[-1] == 0x20 and again > back > into20, "back into $1F and into $20 again"
            r1d, r21 = turn_rec.ram(0x1D), turn_rec.ram(0x21)
            s1d, s21 = list(r1d[ENTITIES:ENTITIES + 8]), list(r21[ENTITIES:ENTITIES + 8])
            assert OINTMENT_SOMETIMES not in s1d, ("$1D's Ointment: gone at 2", s1d)
            assert RED_POTION_SOMETIMES in s21, ("$21's Red Potion: there at 3", s21)
            print(f"ok: out onto $1F with the count at 2, $1D's one-in-four Ointment is met as he arrives (2 to 3, "
                  f"frame {turn_moved[1][0]}: gone), and walking into $20 (frame {into20}) meets $21's Red Potion "
                  f"(3 to 0, "
                  f"frame {turn_moved[2][0]}: there); back into $1F (frame {back}) and into $20 again (frame {again}), "
                  f"the count stays at 0")

        # ---- the fountain's ladder ----
        def trunk27_path(case):
            data = trunk27.read_bytes()
            live = hw_ram(data)
            assert (live[AREA], live[SCREEN], live[QUESTS] & PATH_TO_MASCON) == (TRUNK, 0x27, PATH_TO_MASCON), \
                (live[AREA], live[SCREEN], live[QUESTS])
            return edited(case, trunk27, {**{ENTITIES + k: EMPTY for k in range(8)}, HERO_X: 8, OINTMENT: 0x7F})

        if args.wants("fountain"):
            data = trunk28.read_bytes()
            live = hw_ram(data)
            assert (live[SCREEN], live[QUESTS], live[SPECIAL_ITEMS] & RING_OF_RUBY) == (0x28, SPRINGS, RING_OF_RUBY)
            b = blocks(live)
            assert [b[s] for s in FOUNTAIN_STONES[:2]] == [STONE, STONE] and b[FOUNTAIN_LADDER[1]] != LADDER, \
                ("the stone, and no ladder, the path not open", [b[s] for s in FOUNTAIN_STONES], b[FOUNTAIN_LADDER[1]])
            path = edited("first-opening", trunk28, {ENTITIES + k: EMPTY for k in range(8)})
            frames = 170
            push = [(0, "LEFT")]
            now, _ = game.play(path, push, frames, mods)
            was = game.headless(path, push, frames, original)

            def timeline(rams):
                return [(r[HERO_X], r[HERO_Y], r[QUESTS], bytes(blocks(r))) for r in rams]
            assert timeline(now) == timeline(was), "every frame as the original's"
            pushed = next(f for f, r in enumerate(now) if blocks(r)[PUSHED_STONE[0]] == STONE)
            opened = next(f for f, r in enumerate(now) if r[QUESTS] & PATH_TO_MASCON)
            drops = [next(f for f, r in enumerate(now) if blocks(r)[b] == LADDER) for b in FOUNTAIN_LADDER[1:]]
            assert all(blocks(now[opened])[s] == AIR for s in PUSHED_STONE + FOUNTAIN_STONES[:2]), "the stone clears"
            assert 28 <= opened - pushed <= 32 and [d2 - d1 for d1, d2 in zip(drops, drops[1:])] == [4] * 5, \
                (pushed, opened, drops)
            assert len({(r[HERO_X], r[HERO_Y]) for r in now[pushed:drops[-1] + 1]}) == 1, "he stands still meanwhile"
            print(f"ok: the first opening, as the original plays it, every frame alike: the stone pushed aside on "
                  f"frame {pushed}, cleared with Quests $20 on frame {opened}, the ladder's blocks on frames {drops} "
                  f"(the first is $28's own), the hero still throughout")
            # A later visit: from afar.
            rec = Record(trunk27.read_bytes())
            r28 = rec.ram(0x28)
            b28 = blocks(r28)
            assert all(b28[b] == LADDER for b in FOUNTAIN_LADDER), ("$28 has its ladder from afar",
                                                                   [b28[b] for b in FOUNTAIN_LADDER])
            assert all(b28[b] == AIR for b in FOUNTAIN_STONES), ("and no stone", [b28[b] for b in FOUNTAIN_STONES])
            assert r28[SPECIAL_EVENT] == NO_EVENT, ("its event cleared", r28[SPECIAL_EVENT])
            path = trunk27_path("fountain")
            frames = 100
            rams, rows = game.play(path, [(0, "LEFT")], frames, mods)
            screens = [r[SCREEN] for r in rams]
            into = screens.index(0x28)
            assert all(s == 0x28 for s in screens[into:]), "into $28, and on it"
            assert all(r[SPECIAL_EVENT] == NO_EVENT for r in rams), "the event never runs"
            assert all(blocks(r)[b] == LADDER for r in rams[into:] for b in FOUNTAIN_LADDER), "the ladder is there"
            assert all(blocks(r)[b] == AIR for r in rams[into:] for b in FOUNTAIN_STONES), "and no stone"
            xs = [r[HERO_X] for r in rams]
            # He walks on as he crosses (a pixel a frame or so, at times none): never still 4 frames running.
            assert all(len(set(xs[f:f + 4])) > 1 for f in range(into + 40)), ("he never stands still", xs[:into + 44])
            assert all(r[QUESTS] == SPRINGS | PATH_TO_MASCON for r in rams)
            for name, f in (("fountain-from-27", 1), ("fountain-ladder", into + 50)):
                png = out / f"{len(game.shots):02d}-{name}.png"           # the stone gone from afar; the ladder there
                game.play(path, [(0, "LEFT")], f, mods, picture=png)
                game.shots.append(png.name)
            print(f"ok: from $27 with the path to Mascon open, $28's record has its ladder, no stone and no event; "
                  f"walking left he crosses into $28 on frame {into} and walks on (x {xs[into - 2:into + 3]} ...), "
                  f"the event cleared and the ladder there on every frame")

        # ---- a death at a seam ----
        def death_path(case, x, flags):
            data = town.read_bytes()
            live = hw_ram(data)
            assert (live[AREA], live[SCREEN]) == (EOLIS, 1), (live[AREA], live[SCREEN])
            writes = {**{ENTITIES + k: EMPTY for k in range(8)}, HERO_X: x, PLAYER_IS_DEAD: 1,
                      PLAYER_FLAGS: (live[PLAYER_FLAGS] & ~FACING_RIGHT) | flags}
            return edited(case, town, writes)

        def picture(path, f):
            png = out / f"{path.stem}-{f:03d}.png"
            _, rows = game.play(path, [], f, mods, picture=png)
            bpp, px = png_rows(png)
            png.unlink()
            return rows[-1], bpp, px

        if args.wants("death"):
            checked = []
            for facing, flags in (("right", FACING_RIGHT), ("left", 0)):
                at_seam = death_path(f"death-248-{facing}", 248, flags)
                inside = death_path(f"death-120-{facing}", 120, flags)
                for f in (5, 20, 50, 70, 90):
                    (ra, bpp, a), (rb, _, b) = picture(at_seam, f), picture(inside, f)
                    oa, ob = (ra["origin_x"], ra["origin_y"]), (rb["origin_x"], rb["origin_y"])

                    def px(img, origin, x, y):
                        """Room 1's pixel (x, y), past its right side too, in a picture."""
                        cx, cy = origin[0] + x, origin[1] + y
                        return img[cy][cx * bpp:cx * bpp + 3]
                    known = right = shown = 0
                    for dy in range(32):
                        for dx in range(16):
                            y = 144 + dy
                            seam_hero, seam_bg = px(a, oa, 248 + dx, y), px(b, ob, 248 + dx, y)
                            in_hero, in_bg = px(b, ob, 120 + dx, y), px(a, oa, 120 + dx, y)
                            shown += in_hero != in_bg
                            if seam_hero == seam_bg and in_hero == in_bg:
                                continue                                 # no hero pixel here, or one like the ground
                            assert seam_hero == in_hero, ("his pixel at the seam is his pixel in the room", facing, f,
                                                          dx, dy, seam_hero, in_hero)
                            known += 1
                            right += dx >= 8
                    # While the death sets up the NES shows no hero (it cleared the sprites): nor past the seam.
                    assert right > 0 if shown else known == 0, ("pixels of his past the seam", facing, f, shown,
                                                                  known, right)
                    checked.append((facing, f, known, right))
            png = out / f"{len(game.shots):02d}-death-at-seam.png"
            game.play(death_path("death-248-shot", 248, FACING_RIGHT), [], 60, mods, picture=png)
            game.shots.append(png.name)
            print("ok: dying at x 248, across the seam into room 8, his pixels are those of the same death at x 120 "
                  "(the NES's whole), on both sides of the seam, before the dissolve's loop draws him, whole, "
                  "dissolving and fading, facing either way (facing, frame, pixels, past the seam): "
                  + ", ".join(f"{c[0]} {c[1]} {c[2]} {c[3]}" for c in checked))

        if args.wants("save-load"):
            def alike(case, start, frames, rooms, schedule=(), pictures=False):
                after = []
                for i in range(2):
                    p = out / f"{case}-after-{i}.cycstate"
                    png = out / f"{case}-after-{i}.png" if pictures else None
                    game.play(start, list(schedule), frames + 1, mods, saves=[(frames, p)], picture=png)
                    after.append((p.read_bytes(), png.read_bytes() if png else None))
                (a, pa), (b, pb) = after
                ra, rb = Record(a), Record(b)
                assert hw_ram(a) == hw_ram(b), (case, "the machine")
                for n in rooms:
                    assert ra.ram(n) == rb.ram(n), (case, f"room {n:#x}'s record")
                    assert a[ra.start + ra.block(n):ra.start + ra.block(n) + 512] == \
                        b[rb.start + rb.block(n):rb.start + rb.block(n) + 512], (case, f"room {n:#x}'s block")
                assert pa == pb, (case, "the picture")
                return hw_ram(a)

            mid = out / "count-mid.cycstate"
            m = turn_moved[1][0] - 5
            game.play(turn_path, turn, m + 1, mods, saves=[(m, mid)])
            rest = [(max(f - m - 1, 0), b) for f, b in turn if f > m] + [(0, "RIGHT")]   # the walk on from there
            live = alike("count", mid, 300, (0x1D, 0x1F, 0x20, 0x21), sorted(rest))
            assert live[VISIT_COUNT] == 0, ("on through $1D and $21: two visits", live[VISIT_COUNT])

            mid = out / "fountain-mid.cycstate"
            path = trunk27_path("fountain-save")
            game.play(path, [(0, "LEFT")], 4, mods, saves=[(3, mid)])
            assert hw_ram(mid.read_bytes())[SCREEN] == 0x27
            live = alike("fountain", mid, 60, (0x27, 0x28), [(0, "LEFT")])
            assert live[SCREEN] == 0x28 and all(blocks(live)[b] == LADDER for b in FOUNTAIN_LADDER)

            mid = out / "death-mid.cycstate"
            path = death_path("death-save", 248, FACING_RIGHT)
            game.play(path, [], 41, mods, saves=[(40, mid)])
            alike("death", mid, 30, (1, 8), pictures=True)
            print("ok: saved as $1D's count is about to move, as he is about to cross into the fountain's screen, "
                  "and as he dissolves at the seam, two fresh games run on from each to the same machine and "
                  "records (and the death's to the same picture)")
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
