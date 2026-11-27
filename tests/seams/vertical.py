"""Play One above the other (#166's slice 5) end to end, in a hidden window and
headless.

    python3 tests/seams/vertical.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/vertical

The game runs uncapped and the script waits on the game's own frames, never
on the clock, on a reserved TCP port; the frame-exact parts play headless
from a save state, buttons on a schedule from its frame, with each frame's
CPU RAM, Seamless's per-frame trace (NESRECOMP_WORLD_TRACE: the view actors
wake in) and states saved after chosen frames (the rooms' records).

The parts start from fixtures made out of Eolis's meat shop, its door edited
to lead onto a screen (tests/block/harness.py's warp(), an external
prerequisite, not coverage), under Ointment (DurationOintment below $80: no
touch gets through): seams-trunk-29 (the Trunk's $29, the hero on its ladder
at x 32, y 0, under $2D, whose ladder comes down a shaft in its floor, a
block wide), seams-mist-10 (the Mist's $10, standing at x 96 on its row 4,
y 32, under $07's open sky, where Hornets fly) and seams-trunk-37 (out onto
the Trunk's $3C, he falls through its wide opening into $37). Each edits its
save: the live game's RAM, and rooms in Seamless's record (faxanadu.world,
tests/seams/open_ground.py's Record: each room's RAM, and Seams' block, its
first byte a wake bit per slot, then a mark per slot, 4 through the top and
8 through the bottom).

1. follow: a Hornet of $2D in its shaft at x 32, y 178, moving down, awake,
   $2D's other Hornets and $29's monsters taken out; he climbs down $29's
   ladder. The Hornet follows him down: through $2D's bottom (marked 8) and,
   once its origin is past it, into $29 (Seams' log: a move $2D to $29), at
   its Y less 208, and buzzes on in $29, the live game's.
2. straddle: the Hornet at $2D's y 200, through its bottom, straddling the
   seam over the hero: the picture differs from the same frame without it
   both above and below the seam, and $2D holds its cells past its bottom.
3. sword: on the Mist's $10 he jumps, the Long Sword in hand, and swings at
   the top of the jump (y 0) at a Hornet of $07 above at its y 203 (his
   room's -5), its body reaching 11 lines into $10: the frame it dies its
   death is in $07's record, he is in $10 and its 25 experience is his. A
   Hornet at $07's y 180 (his room's -28) is out of the swing's reach: no
   hit.
4. dormant: a Hornet of $07 at its y 100, dormant: as he stands and jumps,
   the view rises (his body stays in it), and the Hornet stands still,
   asleep, on every frame until the first whose view comes within 16 pixels
   of its box (from the trace), and wakes on that very frame.
5. up: a Hornet of $10 near its top (y 4), awake, flying up: its Y wraps
   past the top, marked 4, and between frames it moves into $07 at its Y
   plus 208, straddling $07's bottom (marked 8).
6. fall: in the Trunk, the hero on $37, a Zozura of $3C above, awake, over
   its opening: it falls through the seam (the original removes a faller at
   y 192, "off the screen": Seams counts the falls it carried on) into $37
   and lands on $37's floor.
7. save-load: the straddling Hornet of part 2, saved by the game itself
   mid-crossing: two fresh games, headless, run 60 frames from that save to
   the same machine and records, the Hornet through into $29.

Screenshots: the Hornet straddling the seam over him on the ladder, and
without it; the sword's hit across the seam above (headless, --present-out);
the Zozura fallen into $37.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "block"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "movement"))
import fax_e2e  # noqa: E402
from fax_e2e import batch, fixture, frame, parse_args, tcp, until  # noqa: E402
import harness  # noqa: E402  (warp(): out of a building onto any screen)
import play  # noqa: E402  (headless runs and mod catalogs)
from seams_lose_nothing import hw_ram, spawn  # noqa: E402
from open_ground import Record, png_rows  # noqa: E402

TRUNK, MIST, REGION_TRUNK, REGION_MIST, PALETTE_TRUNK, PALETTE_MIST = 1, 2, 1, 2, 0x06, 0x0A
SCREEN, HERO_X, HERO_Y, PLAYER_FLAGS = 0x63, 0x9E, 0xA1, 0xA4    # Area_CurrentScreen, Player_PosX_Block, _PosY, _Flags
SPRITE_X, SPRITE_Y = 0xBA, 0xC2                                  # CurrentSprites_XPos_Full, _YPos
ENTITIES, FLAGS, BEHAVIOR_DATA2 = 0x2CC, 0x2DC, 0x2EC            # CurrentSprites_*
SLOT_ARRAYS = [SPRITE_X, SPRITE_Y] + list(range(0x2BC, 0x374, 8))
EXPERIENCE, SELECTED_WEAPON, PLAYER_CUR_WEAPON = 0x390, 0x3BD, 0x3C8
OINTMENT, HP = 0x427, 0x431
HORNET, ZOZURA, DEATH, EMPTY = 0x07, 0x47, 0x13, 0xFF
LONG_SWORD, HORNET_XP = 1, 25                                    # ENEMY_EXPERIENCE's for the Hornet
FACING_RIGHT, READY, FALLING = 0x01, 0x40, 0x80                  # CurrentSprites_Flags
STILL = 32                       # BehaviorData2: SpriteBehavior_BuzzAround's Y step 0 for 8 ticks ($20 >> 3 & 7 = 4)
THROUGH_UP, THROUGH_DOWN = 4, 8
ROOM_H, VIEW_W, VIEW_H, PAD, HUD = 208, 424, 208, 16, 32
PARTS = ("follow", "straddle", "sword", "dormant", "up", "fall", "save-load")


class Game(harness.Game, play.Game):
    """harness.Game's warp(), and play.Game's headless runs (play(), with
    Seamless's trace) and mod catalogs."""


@fax_e2e.recipe("seams-trunk-29", parent="eolis-meat-shop")
def _trunk_29(g):
    """Out of Eolis's meat shop onto the Trunk's $29, its door edited to lead
    there, at block $02: on its ladder at x 32, y 0, under $2D's shaft."""
    g.warp(g.save("shop"), "trunk-29", TRUNK, 0x29, PALETTE_TRUNK, 0x02, REGION_TRUNK, {OINTMENT: 0x7F})
    g.poke({HERO_X: 32, HERO_Y: 0})
    g.wait(20)


@fax_e2e.recipe("seams-mist-10", parent="eolis-meat-shop")
def _mist_10(g):
    """The same onto the Mist's $10 at block $26: standing on its row 4 at x
    96, y 32, under $07."""
    g.warp(g.save("shop"), "mist-10", MIST, 0x10, PALETTE_MIST, 0x26, REGION_MIST, {OINTMENT: 0x7F})
    g.wait(30)


@fax_e2e.recipe("seams-trunk-37", parent="eolis-meat-shop")
def _trunk_37(g):
    """The same onto the Trunk's $3C at block $97, over its opening: he falls
    through into $37, and stands on its floor."""
    g.warp(g.save("shop"), "trunk-3c", TRUNK, 0x3C, PALETTE_TRUNK, 0x97, REGION_TRUNK, {OINTMENT: 0x7F})
    g.until("fallen into $37", lambda: g.ram(SCREEN) == 0x37 and g.ram(0x54) == 0xFF, 300)
    g.wait(30)


def seams():
    return tcp("seams_state")


def room(n):
    return tcp("fax_world_room", room=n)


def experience(ram):
    return ram[EXPERIENCE] | ram[EXPERIENCE + 1] << 8


def moved(log, src, dst, entity):
    return [m for m in log if m[0] == src and m[2] == dst and m[4] == entity]


def main():
    args = parse_args(__doc__, parts=PARTS)
    game = Game.from_args(args)
    image, out = game.image, game.out

    def edited(case, source, writes, edits):
        path = out / f"{case}.cycstate"
        fixture(source, path, writes, {"faxanadu.world": edits})
        return path

    def start(case, at, writes, edits):
        """Launch (or keep) the game, and load fixture `at` with these edits."""
        path = edited(case, game.state(at), writes, edits)
        if not game.running():
            game.launch(at=at)
        tcp("load_state", path=str(path))
        return path

    try:
        def wanted(*names):
            return any(args.wants(n) for n in names)
        trunk29 = game.state("seams-trunk-29") if wanted("follow", "straddle", "save-load") else None
        mist10 = game.state("seams-mist-10") if wanted("sword", "dormant", "up") else None
        trunk37 = game.state("seams-trunk-37") if wanted("fall") else None
        mods = game.mods("mods-on")

        def shaft(y, through=0, step=0):
            """seams-trunk-29's edits: $2D's Hornets out but one, in its shaft
            at x 32 and y, moving down (BuzzAround's Y step from BehaviorData2
            `step`), awake (and marked `through`); $29's monsters out. Returns
            (edits, its slot)."""
            data = trunk29.read_bytes()
            rec = Record(data)
            r2d, base = rec.ram(0x2D), rec.base(0x2D)
            hornets = [s for s in range(8) if r2d[ENTITIES + s] == HORNET and r2d[FLAGS + s] & READY]
            assert hornets, list(r2d[ENTITIES:ENTITIES + 8])
            s = hornets[-1]
            edits = {base + ENTITIES + k: EMPTY for k in range(8) if k != s}
            edits.update({base + SPRITE_X + s: 32, base + SPRITE_Y + s: y, base + BEHAVIOR_DATA2 + s: step,
                          base + FLAGS + s: (r2d[FLAGS + s] & FACING_RIGHT) | READY | FALLING})
            edits[rec.block(0x2D)] = 1 << s
            edits[rec.block(0x2D) + 2 + s] = through
            edits.update({rec.base(0x29) + ENTITIES + k: EMPTY for k in range(8)})
            return edits, s

        def sky(hornet_y, awake=True, x=120, still=True):
            """seams-mist-10's edits: $10's monsters out, and those of $11 and
            $08 (beside it and $07: their Hornets would fly in), the hero
            facing right with the Long Sword; one of $07's Hornets at (x,
            hornet_y), behaviour under way (BuzzAround's Y still for 8 ticks),
            awake or not, the rest of $07 out. Returns (writes, edits, its
            slot)."""
            data = mist10.read_bytes()
            live, rec = hw_ram(data), Record(data)
            assert live[SCREEN] == 0x10 and (live[HERO_X], live[HERO_Y]) == (96, 32), (live[SCREEN], live[HERO_X])
            r07, base = rec.ram(0x07), rec.base(0x07)
            s = [k for k in range(8) if r07[ENTITIES + k] == HORNET][0]
            edits = {base + ENTITIES + k: EMPTY for k in range(8) if k != s}
            edits.update({base + SPRITE_X + s: x, base + SPRITE_Y + s: hornet_y,
                          base + FLAGS + s: (r07[FLAGS + s] & FACING_RIGHT) | READY,
                          base + BEHAVIOR_DATA2 + s: STILL if still else 0})
            edits[rec.block(0x07)] = (1 << s) if awake else 0
            for n in (0x11, 0x08):
                edits.update({rec.base(n) + ENTITIES + k: EMPTY for k in range(8)})
            writes = {ENTITIES + k: EMPTY for k in range(8)}
            writes.update({PLAYER_CUR_WEAPON: LONG_SWORD, SELECTED_WEAPON: LONG_SWORD, PLAYER_FLAGS: 0x40, HP: 0x50})
            return writes, edits, s

        if args.wants("follow"):
            edits, s = shaft(178)
            start("follow", "seams-trunk-29", {}, edits)
            tcp("action", name="down", frames=240)
            last = None                                                 # the last read of it straddling $2D's bottom

            def through():
                nonlocal last
                r2d, log = batch(("fax_world_room", dict(room=0x2D)), ("seams_state", {}))
                if r2d["entities"][s] == HORNET and r2d["through"][s]:
                    last = (r2d["y"][s], r2d["through"][s], r2d["cells"])
                return moved(log["log"], 0x2D, 0x29, HORNET)
            until("the Hornet into $29", through, 240)
            m = through()[-1]
            r29, s_ = room(0x29), seams()
            assert s_["moves_down"] >= 1 and ram_screen() == 0x29, ("he is in $29, it came down", s_["moves_down"])
            assert last and last[0] >= 194 and last[1] == THROUGH_DOWN, ("seen through $2D's bottom first", last)
            k = m[3]
            assert r29["entities"][k] == HORNET and r29["x"][k] == 32 and r29["y"][k] < 32, r29
            print(f"ok: a Hornet in $2D's shaft follows him down $29's ladder: through $2D's bottom (at its y "
                  f"{last[0]}, marked {last[1]}, {last[2]} cells past it), then moved into $29's slot {k} "
                  f"(move {m}), at y {r29['y'][k]} there, its Y less 208")
            until("the Hornet out of the shaft in $29", lambda: room(0x29)["y"][k] > 60, 240)
            r29 = room(0x29)
            assert r29["entities"][k] == HORNET and ram_screen() == 0x29, r29
            print(f"ok: it buzzes on in $29, the live game's, at ({r29['x'][k]}, {r29['y'][k]})")

        if args.wants("straddle"):
            shots = []
            with_h, s = shaft(200, THROUGH_DOWN, STILL)                  # its Y still for 8 ticks, then slow
            without = dict(with_h)
            without[Record(trunk29.read_bytes()).base(0x2D) + ENTITIES + s] = EMPTY
            for case, edits in (("straddle", with_h), ("straddle-without", without)):
                path = edited(case, trunk29, {}, edits)
                if not game.running():
                    game.launch(at="seams-trunk-29")
                png = out / f"{len(game.shots):02d}-{case}.png"
                tcp("load_state", path=str(path))
                _, r2d, world = batch(("screenshot", dict(layer="picture", path=str(png))),
                                      ("fax_world_room", dict(room=0x2D)), ("fax_world", {}))
                game.shots.append(png.name)
                shots.append((png, r2d, world))
            (pa, ra, wa), (pb, _, wb) = shots
            assert ra["entities"][s] == HORNET and ra["y"][s] >= 194 and ra["through"][s] == THROUGH_DOWN, ra
            assert ra["cells"] > 0 and wa["camera"] == wb["camera"], (ra, wa["camera"], wb["camera"])
            seam = HUD + 0 - wa["camera"][1]                         # $29's top in the picture ($29 is at row 0)
            left = 32 - wa["camera"][0]
            (bpp, a), (_, b) = png_rows(pa), png_rows(pb)
            above = below = 0
            for y in range(max(seam - 16, HUD), min(seam + 16, len(a))):
                for x in range(max(left - 4, 0), min(left + 20, VIEW_W)):
                    if a[y][x * bpp:x * bpp + 3] != b[y][x * bpp:x * bpp + 3]:
                        above, below = above + (y < seam), below + (y >= seam)
            assert above > 0 and below > 0, ("the Hornet's pixels on both sides of the seam", above, below, seam)
            print(f"ok: a Hornet straddling the seam at $2D's y {ra['y'][s]} is drawn whole: {above} of its pixels "
                  f"above the seam, {below} below it, with {ra['cells']} cells past $2D's bottom")

        if args.wants("sword"):
            def swing(case, hornet_y):
                writes, edits, s = sky(hornet_y)
                path = edited(case, mist10, writes, edits)
                rams, _ = game.play(path, [(0, "A"), (2, "-"), (3, "B"), (5, "-")], 30, mods)
                return path, rams, s
            path, rams, s = swing("sword", 203)
            xp0 = experience(rams[0])
            hit = [f for f in range(len(rams)) if experience(rams[f]) != xp0]
            assert hit, "no hit across the seam above"
            f = hit[0]
            assert experience(rams[f]) == xp0 + HORNET_XP, (xp0, experience(rams[f]))
            assert all(r[SCREEN] == 0x10 for r in rams) and rams[f][HERO_Y] <= 8, ("he is in $10, near its top",
                                                                                   rams[f][HERO_Y])
            assert all(HORNET not in r[ENTITIES:ENTITIES + 8] and DEATH not in r[ENTITIES:ENTITIES + 8] for r in rams)
            before, at = out / "sword-before.cycstate", out / "sword-hit.cycstate"
            shot = out / f"{len(game.shots):02d}-sword-across.png"
            game.play(path, [(0, "A"), (2, "-"), (3, "B"), (5, "-")], f + 1, mods, saves=[(f - 1, before), (f, at)],
                      picture=shot)
            game.shots.append(shot.name)
            rb, ra = Record(before.read_bytes()), Record(at.read_bytes())
            yb = rb.ram(0x07)[SPRITE_Y + s]
            assert rb.ram(0x07)[ENTITIES + s] == HORNET and yb >= 196, ("its origin in $07, by his room's top", yb)
            assert ra.ram(0x07)[ENTITIES + s] == DEATH, ("its death in $07", ra.ram(0x07)[ENTITIES + s])
            assert hw_ram(at.read_bytes())[HERO_Y] == rams[f][HERO_Y], "the save and the frame log agree"
            print(f"ok: he jumps on $10 and swings at the top of the jump (y {rams[f][HERO_Y]}): frame {f}, the Hornet "
                  f"of $07 at its y {yb} (his room's {yb - ROOM_H}) dies in $07, its {HORNET_XP} experience his, "
                  f"he in $10 throughout")
            _, rams, _ = swing("sword-out-of-reach", 180)
            assert all(experience(r) == experience(rams[0]) for r in rams), "out of reach: no hit"
            print("ok: the same swing at a Hornet at $07's y 180 (his room's -28), out of its reach: no hit")

        if args.wants("dormant"):
            writes, edits, s = sky(100, awake=False)
            path = edited("dormant", mist10, writes, edits)
            schedule = [(0, "-"), (20, "A"), (22, "-")]                  # stands 20 frames, then jumps
            rams, rows = game.play(path, schedule, 50, mods)
            assert all(r["y"] == rams[i][HERO_Y] for i, r in enumerate(rows)), "the trace and the frame log agree"
            box = (-1 * ROOM_H + 100, 120)                               # its top in world pixels ($07 is row -1), its X

            def meets(row):
                vx, vy = row["view_x"], row["view_y"]
                x, y = box[1] + 0, box[0]                                # $07 is at column 0, as $10
                return x + 16 + PAD > vx and x - PAD < vx + VIEW_W and y + 16 + PAD > vy and y - PAD < vy + VIEW_H
            seen = [i for i, r in enumerate(rows) if meets(r)]
            assert seen and seen[0] > 20, ("it comes into the view as he jumps", seen[:3], [r["view_y"] for r in rows[:3]])
            first = seen[0]
            saves = [(i, out / f"dormant-{i:02d}.cycstate") for i in (0, 19, first - 1, first, first + 3)]
            game.play(path, schedule, first + 4, mods, saves=saves)
            for i, p in saves:
                r07 = Record(p.read_bytes())
                awake = bool(r07.data[r07.start + r07.block(0x07)] >> s & 1)
                x, y = r07.ram(0x07)[SPRITE_X + s], r07.ram(0x07)[SPRITE_Y + s]
                assert hw_ram(p.read_bytes())[HERO_Y] == rams[i][HERO_Y], "the save and the frame log agree"
                if i < first:
                    assert not awake and (x, y) == (120, 100), ("asleep and still", i, awake, x, y)
                elif i == first:
                    assert awake, ("woken on the frame the view comes within 16 of it", i)
                else:
                    assert awake and x != 120, ("awake, it moves", i, x)
            print(f"ok: a dormant Hornet of $07 at its y 100 stands still while he stands and as he jumps, until "
                  f"frame {first}, the first whose view (its top at {rows[first]['view_y']}, before "
                  f"{rows[first - 1]['view_y']}) comes within 16 pixels of it, and wakes on that frame")

        if args.wants("up"):
            data = mist10.read_bytes()
            writes, edits, _ = sky(100)
            rec = Record(data)
            edits.update({rec.base(0x07) + ENTITIES + k: EMPTY for k in range(8)})
            r07 = rec.ram(0x07)
            src = [k for k in range(8) if r07[ENTITIES + k] == HORNET][0]
            for a in SLOT_ARRAYS:                                        # a Hornet of $07's, in $10's slot 7
                writes[a + 7] = r07[a + src]
            writes.update({ENTITIES + 7: HORNET, SPRITE_X + 7: 150, SPRITE_Y + 7: 4, BEHAVIOR_DATA2 + 7: 0,
                           FLAGS + 7: (r07[FLAGS + src] & FACING_RIGHT) | READY})
            live = hw_ram(data)
            edits[rec.block(0x10)] = 0x80                                # awake: slot 7 of the live room
            path = edited("up", mist10, writes, edits)
            saves = [(i, out / f"up-{i:02d}.cycstate") for i in range(12)]
            rams, _ = game.play(path, [], 12, mods, saves=saves)
            gone = [i for i in range(12) if rams[i][ENTITIES + 7] != HORNET]
            assert gone and gone[0] > 0, "the Hornet never left $10"
            f = gone[0]
            # A move is whole between frames: the frame its Y wraps past $10's
            # top (marked through it), Seams' step moves it.
            prev, now = Record(saves[f - 1][1].read_bytes()), Record(saves[f][1].read_bytes())
            y0 = rams[f - 1][SPRITE_Y + 7]
            assert prev.data[prev.start + prev.block(0x10) + 2 + 7] == 0 and y0 < 4, ("in $10 at its top", y0)
            k = [j for j in range(8) if now.ram(0x07)[ENTITIES + j] == HORNET]
            assert len(k) == 1, list(now.ram(0x07)[ENTITIES:ENTITIES + 8])
            y1, mark1 = now.ram(0x07)[SPRITE_Y + k[0]], now.data[now.start + now.block(0x07) + 2 + k[0]]
            assert y0 + ROOM_H - 4 <= y1 < y0 + ROOM_H, ("its Y plus 208, less its step up", y0, y1)
            assert mark1 == THROUGH_DOWN and y1 + 15 >= ROOM_H, ("straddling $07's bottom", y1, mark1)
            assert live[SCREEN] == 0x10 and all(r[SCREEN] == 0x10 for r in rams)
            print(f"ok: a Hornet of $10 flying up from its y {y0} goes through its top and moves into $07 at its y "
                  f"{y1} (less its step, plus 208), straddling $07's bottom (marked {mark1}), frame {f}")

        if args.wants("fall"):
            data = trunk37.read_bytes()
            live, rec = hw_ram(data), Record(data)
            assert live[SCREEN] == 0x37, hex(live[SCREEN])
            edits = {rec.base(0x3C) + ENTITIES + k: EMPTY for k in range(8)}
            spawn(image, edits, rec.base(0x3C), 7, ZOZURA, 96, 160)
            edits[rec.block(0x3C)] = 0x80                               # awake: slot 7
            start("fall", "seams-trunk-37", {ENTITIES + k: EMPTY for k in range(8)}, edits)
            fell0 = seams()["fell_on"]
            until("the Zozura on $37's floor",
                  lambda: any(e == ZOZURA and y == 160 for e, y in zip(room(0x37)["entities"], room(0x37)["y"])), 240)
            s_, r37, r3c = seams(), room(0x37), room(0x3C)
            m = moved(s_["log"], 0x3C, 0x37, ZOZURA)
            assert m and s_["fell_on"] > fell0 and ZOZURA not in r3c["entities"], (s_["log"], s_["fell_on"])
            k = r37["entities"].index(ZOZURA)
            game.shot("fall")
            print(f"ok: a Zozura of $3C over its opening falls through the seam into $37 (move {m[-1]}; "
                  f"{s_['fell_on'] - fell0} falls carried past the original's y 192) and lands on $37's floor at "
                  f"({r37['x'][k]}, {r37['y'][k]})")

        if args.wants("save-load"):
            edits, s = shaft(200, THROUGH_DOWN, STILL)
            if game.running():
                game.close()
            game.launch(at="seams-trunk-29")
            tcp("load_state", path=str(edited("save-load", trunk29, {}, edits)))
            mid = game.save("mid-crossing")
            mrec = Record(mid.read_bytes())
            assert mrec.ram(0x2D)[ENTITIES + s] == HORNET and mrec.data[mrec.start + mrec.block(0x2D) + 2 + s] == \
                THROUGH_DOWN, "saved mid-crossing, through $2D's bottom"
            after = []
            for i in range(2):
                p = out / f"after-{i}.cycstate"
                game.play(mid, [], 61, mods, saves=[(60, p)])
                after.append(p.read_bytes())
            a, b = after
            ra, rb = Record(a), Record(b)
            assert hw_ram(a) == hw_ram(b), "the machine"
            for n in (0x2D, 0x29):
                assert ra.ram(n) == rb.ram(n), f"room {n:#x}'s record"
                assert a[ra.start + ra.block(n):ra.start + ra.block(n) + 512] == \
                    b[rb.start + rb.block(n):rb.start + rb.block(n) + 512], f"room {n:#x}'s block"
            live = hw_ram(a)
            assert live[SCREEN] == 0x29 and HORNET in live[ENTITIES:ENTITIES + 8], "through into $29, the live game's"
            assert HORNET not in ra.ram(0x2D)[ENTITIES:ENTITIES + 8], "gone from $2D"
            print("ok: saved mid-crossing, two fresh games run 60 frames from it to the same machine and records, "
                  "the Hornet through into $29")
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


def ram_screen():
    return fax_e2e.ram(SCREEN)


if __name__ == "__main__":
    sys.exit(main())
