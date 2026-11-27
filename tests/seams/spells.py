"""Play The spell flies on (#166's slice 3) end to end, in a hidden window and
headless.

    python3 tests/seams/spells.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/spells

The game runs uncapped and the script waits on the game's own frames, never
on the clock, on a reserved TCP port; what depends on the exact frame runs
headless from a save state, with every frame's RAM (tests/movement/play.py).

Every part starts from the fixture eolis-zozuras (an external prerequisite,
not coverage, tests/e2e/fax_e2e.py): Eolis screen 4, room 5 left of it and
room 3, the town's left end, right of it. Each edits that save: the hero's
place and facing, Deluge (or Fire) with MP, Ointment, the live room's
Zozuras taken out, and room 5's slots in Seamless's record
(tests/seams/open_ground.py's Record; Seams' block there, its first byte a
wake bit per slot). The spell's X in 16 bits from room 4's left edge is
worked out here from its byte, which moves 3 pixels a frame (Fire 4) from his
X at the cast; the view's left edge comes from seams_state's view and room
4's place.

1. flies-on: Deluge cast left from room 4's x 60 flies through the seam into
   room 5, 3 pixels every frame, and ends on the first frame it is left of
   the view's left edge (room 5's x 112), never before. Loaded mid-flight in
   the window, it is drawn in room 5's space: the picture differs from the
   same frame without it there, and Seams counts its draws abroad.
2. wall: a solid block put in room 5's column 12, on its rows: the Deluge
   ends on the first frame its X is in that column (the wall's sound), far
   from the view's edge.
3. kill: a room 5 Zozura at its x 200, awake, with 1 HP: the Deluge kills it
   in room 5 while he stands in room 4 (its death in room 5's record, the
   visit in seams_state's visit_log), for its 30 experience and 1 MP back.
4. dormant: Cameron's check. A room 5 Zozura at its x 80, never seen, its
   box 26 pixels past the view's left edge (more than the 16 it wakes at):
   the Deluge cast toward it ends on the first frame past the view's edge
   (headless, every frame; and Seams' own note of the end, its X and the
   edge); the Zozura keeps its 9 HP, stays dormant, never visits, and no
   experience comes; Seams never needed its second guard (no dormant hit
   refused).
5. own-room: the same in his own room, the hole before this slice: the hero
   at room 4's x 238, a dormant Zozura of room 4 at its x 0, past the view's
   left edge (x 34). The Deluge ends there instead of flying on to x 0.
6. behind: cast from room 4's x 16, the Deluge passes room 5's tower
   (columns 4 and 5, property 4, a foreground): there the picture shows
   almost none of it (CastMagic_CalculateVisibility read in room 5), while
   in open air it shows.
7. fire: Fire cast left from room 4's x 60, wholly in room 5: over 8 frames
   it is drawn on 4, two at a time (CastMagic_FinishHandler_Fire's flicker,
   by InterruptCounter), each in room 5's space where its X says.
8. crossing: Deluge cast right from room 4's x 240, then Right held: the
   spell passes the seam into room 3 before he does, and when he crosses
   (the screen 4 to 3) it flies on, its byte 3 pixels further every frame,
   until its right edge reaches the view's right edge, worked out from his X
   in room 3.
9. save-load: the kill's cast, saved by the game itself with the Deluge in
   room 5 (its side -1 in Seams' record): two fresh games, headless, run 60
   frames from that save to the same machine, rooms and Seams record, the
   Zozura dead in both.

Screenshots: the Deluge in room 5 with the hero in room 4, the kill across
the seam, the Deluge's last frame at the view's edge, and the Fire across.
"""
from pathlib import Path
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "movement"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "graphics"))
import fax_e2e  # noqa: E402
from fax_e2e import batch, fixture, frame, parse_args, ram, tcp, until, wait  # noqa: E402
from graphics_fixes import png  # noqa: E402  (a picture's pixels)
from open_ground import Record  # noqa: E402
from play import Game  # noqa: E402  (fax_e2e's, with headless runs)
from seams_lose_nothing import hw_ram, spawn  # noqa: E402

SCREEN, HERO_X, PLAYER_FLAGS, INTERRUPTS = 0x63, 0x9E, 0xA4, 0x1A   # Player_PosX_Block, Player_Flags, InterruptCounter
SPRITE_X, ENTITIES, FLAGS, HP_ = 0xBA, 0x2CC, 0x2DC, 0x344
CAST_TYPE, CAST_X = 0x2B3, 0x2B6                                     # CastMagic_Type, CastMagic_XPos_Full
EXPERIENCE, MP, MAGIC, SELECTED_MAGIC, SPELLS = 0x390, 0x39A, 0x3A9, 0x3C0, 0x3C5
OINTMENT = 0x427
SCREEN_BUFFER, BLOCK_PROPERTIES, IMPASSABLE = 0x600, 0x43C, 0xE8D9   # a room's; BLOCK_PROPERTY_IMPASSABLE_MAP (bank 15)
DELUGE, FIRE, NONE = 0, 2, 0xFF
ZOZURA, SPELL_DEATH, EMPTY = 0x47, 0x14, 0xFF                        # Sprite_SetDeathEntity: $14 for a spell's kill
ZOZURA_XP, ZOZURA_MP, DELUGE_MP = 30, 1, 2                           # ENEMY_EXPERIENCE; fax_mana.c's experience² / 512
DELUGE_STEP, FIRE_STEP, DELUGE_W, FIRE_W = 3, 4, 16, 32               # pixels a frame; widths (CAST_MAGIC_WIDTHS + 1)
VIEW_W, HUD, ROOM_W = 424, 32, 256
SPELL_ROWS = (9, 10)                                                 # his Y 144 + 8: rows 9, and 10 (Y off a block's edge)
LEFT_FACING, RIGHT_FACING = 0, 0x40
CAST = [(0, "UP+B"), (2, "UP"), (6, "-")]                            # Up and B, as the Magic button presses them
NO_CAST = [(0, "UP"), (6, "-")]                                      # the same frames without the spell
PARTS = ("flies-on", "wall", "kill", "dormant", "own-room", "behind", "fire", "crossing", "save-load")


def seams_record(data):
    """Seams' own save-state record (faxanadu.seams), past its name."""
    return next(bytes(data[p + 64:p + n]) for tag, p, n in fax_e2e.chunks(data) if tag == b"MOD " and
                bytes(data[p:p + 64]).rstrip(b"\0") == b"faxanadu.seams")


RECORD_V1 = 5 + 8 * 6 + 2 * 8 * 25 + 8 * 3                          # fax_seams.c: version 2 adds the spell's side next


def track(rams, step):
    """Each frame's spell: (type, its X in 16 bits from room 4's left edge, or
    None while none is on). From his X at the cast, its byte moves `step`
    pixels a frame, so the change of the byte is its move."""
    out, x, last = [], None, None
    for r in rams:
        if r[CAST_TYPE] == NONE:
            out.append((NONE, None))
            x = last = None
            continue
        if x is None:
            x = last = r[HERO_X]
        d = (r[CAST_X] - last + 128) % 256 - 128
        assert abs(d) <= step + 1, ("a jump", last, r[CAST_X])
        x, last = x + d, r[CAST_X]
        out.append((r[CAST_TYPE], x))
    return out


def flight(rams, step):
    """The frames the spell was on (index, X), and the first frame it was off
    after them, with where its byte left it (index, X)."""
    on, x, last = [], None, None
    for f, r in enumerate(rams):
        if r[CAST_TYPE] != NONE:
            if x is None:
                x = last = r[HERO_X]
            d = (r[CAST_X] - last + 128) % 256 - 128
            assert abs(d) <= step + 1, ("a jump", f, last, r[CAST_X])
            x, last = x + d, r[CAST_X]
            on.append((f, x))
        elif on:
            d = (r[CAST_X] - last + 128) % 256 - 128
            return on, (f, x + d)
    raise AssertionError(f"the spell never ended in {len(rams)} frames: {[r[CAST_X] for r in rams[-4:]]}")


def main():
    args = parse_args(__doc__, parts=PARTS)
    game = Game.from_args(args)
    image, out = game.image, game.out
    try:
        zozuras = game.state("eolis-zozuras")
        data = zozuras.read_bytes()
        live, record = hw_ram(data), Record(data)
        assert live[SCREEN] == 4 and list(live[ENTITIES + 6:ENTITIES + 8]) == [ZOZURA] * 2
        floor_y = record.ram(5)[0xC2 + 6]
        empty5 = {record.base(5) + ENTITIES + s: EMPTY for s in range(8)}
        mods = game.mods("mods-on")

        def kit(hero_x, facing=LEFT_FACING, spell=DELUGE, mp=40):
            """His place, the spell and its MP, Ointment; the live Zozuras out."""
            return {HERO_X: hero_x, PLAYER_FLAGS: facing, MAGIC: spell, SPELLS: 1, SELECTED_MAGIC: spell, MP: mp,
                    OINTMENT: 0x7F, ENTITIES + 6: EMPTY, ENTITIES + 7: EMPTY}

        def edited(case, writes, edits=None, source=zozuras):
            path = out / f"{case}.cycstate"
            fixture(source, path, writes, {"faxanadu.world": edits if edits is not None else empty5})
            return path

        def zozura5(x, hp=None, awake=False, slot=7, facing_right=True):
            """Room 5 emptied but a Zozura in `slot` at (x, its floor)."""
            edits = dict(empty5)
            spawn(image, edits, record.base(5), slot, ZOZURA, x, floor_y)
            edits[record.base(5) + FLAGS + slot] = 1 if facing_right else 0
            if hp is not None:
                edits[record.base(5) + HP_ + slot] = hp
            edits[record.block(5)] = (1 << slot) if awake else 0
            return edits

        def window(path):
            """Load `path` in the hidden window (launched if need be)."""
            if not game.running():
                game.launch(at="eolis-zozuras")
            tcp("load_state", path=str(path))

        def view_left(path):
            """The view's left edge in room 4's pixels, with him standing as
            `path` has him: seams_state's view less room 4's place."""
            window(path)
            s, r4 = batch(("seams_state", {}), ("fax_world_room", dict(room=4)))
            assert s["view"][2] and r4["xy"][1] == 0, (s["view"], r4["xy"])
            return s["view"][0] - r4["xy"][0] * ROOM_W

        def cast_in_window(path, done, frames=150):
            """Load `path` and cast (Up held, B pressed, with the load, so in its
            first frames), then read every frame boundary until done(reads):
            the reads (seams_state, room 4, room 5, experience, MP, screen)."""
            for _ in range(3):                                     # a press in a loaded state's first frames can go unseen
                if not game.running():
                    game.launch(at="eolis-zozuras")
                batch(("load_state", dict(path=str(path))), ("action", dict(name="up", frames=6)),
                      ("action", dict(name="b", frames=2)))
                reads, end = [], frame() + frames
                while frame() < end:
                    s, r4, r5, xp, mp, sc = batch(("seams_state", {}), ("fax_world_room", dict(room=4)),
                                                  ("fax_world_room", dict(room=5)),
                                                  ("read_ram", dict(addr=EXPERIENCE, len=2)),
                                                  ("read_ram", dict(addr=MP, len=1)),
                                                  ("read_ram", dict(addr=SCREEN, len=1)))
                    xp = bytes.fromhex(xp["hex"])
                    reads.append(dict(s=s, r4=r4, r5=r5, xp=xp[0] | xp[1] << 8, mp=int(mp["hex"], 16),
                                      screen=int(sc["hex"], 16)))
                    if done(reads):
                        return reads
                if any(r["s"]["spell"][0] != NONE for r in reads):
                    raise AssertionError(f"cast, but never done: {reads[-1]['s']['spell']}")
            raise AssertionError("no cast in three tries")

        def picture_diff(a, b, box):
            """Pixels that differ between two pictures in box (x0, y0, x1, y1)."""
            pa, pb = png(a), png(b)
            x0, y0, x1, y1 = box
            return sum(pa[y][x] != pb[y][x] for y in range(max(y0, HUD), min(y1, len(pa)))
                       for x in range(max(x0, 0), min(x1, len(pa[0]))))

        if args.wants("flies-on"):
            state = edited("flies-on", kit(60))
            edge = view_left(state)
            assert edge == 60 + 8 - VIEW_W // 2, edge                      # centred on him: no room's edge near
            mid = out / "flies-on-mid.cycstate"
            rams = game.headless(state, CAST, 90, mods, saves=[(40, mid)])
            on, (end_f, end_x) = flight(rams, DELUGE_STEP)
            xs = [x for _, x in on]
            assert all(b - a == -DELUGE_STEP for a, b in zip(xs, xs[1:])), xs
            assert all(r[SCREEN] == 4 for r in rams), "he stays in room 4"
            abroad = [x for x in xs if x < 0]
            assert abroad and xs[-1] >= edge > end_x, (xs[-4:], edge, end_x)
            print(f"ok: Deluge cast left from room 4's x 60 flies through the seam, {len(abroad)} frames in room 5, and "
                  f"ends on the first frame left of the view's edge (room 4's x {edge}): last at {xs[-1]}, then {end_x}")
            # Drawn on its side: with and without it, the same frame, in the window.
            without = out / "flies-on-without.cycstate"
            fixture(mid, without, {CAST_TYPE: NONE})
            shots = []
            for case, path in (("deluge-across", mid), ("deluge-without", without)):
                shot = out / f"{len(game.shots):02d}-{case}.png"
                tcp("load_state", path=str(path))
                _, s, r4, world = batch(("screenshot", dict(layer="picture", path=str(shot))), ("seams_state", {}),
                                        ("fax_world_room", dict(room=4)), ("fax_world", {}))
                game.shots.append(shot.name)
                shots.append((shot, s, r4, world))
            (sa, s, r4, world), (sb, _, _, world_b) = shots
            x16 = s["spell"][3]
            assert s["spell"][0] == DELUGE and s["spell"][2] == -1 and x16 < -DELUGE_W, s["spell"]
            assert world["camera"] == world_b["camera"] and s["spell_drawn_abroad"] > 0, (world, s)
            origin = r4["xy"][0] * ROOM_W - world["camera"][0]
            near = picture_diff(sa, sb, (origin + x16 - 4, HUD + 140, origin + x16 + DELUGE_W + 4, HUD + 176))
            home = picture_diff(sa, sb, (origin + x16 + ROOM_W - 4, HUD + 140, origin + x16 + ROOM_W + 20, HUD + 176))
            assert near >= 20 and home == 0, (near, home, x16)
            print(f"ok: loaded with the Deluge at room 4's x {x16} (side -1), it is drawn in room 5's space: {near} "
                  f"pixels where it is, none a room over where its byte alone would put it")

        if args.wants("wall"):
            room5 = record.ram(5)
            solid = next(room5[SCREEN_BUFFER + 11 * 16 + c] for c in range(16)
                         if image[16 + 15 * 0x4000 + IMPASSABLE - 0xC000 + (
                             (lambda i: room5[BLOCK_PROPERTIES + (i >> 1)] >> 4 if i & 1 else
                              room5[BLOCK_PROPERTIES + (i >> 1)] & 15)(room5[SCREEN_BUFFER + 11 * 16 + c]))])
            edits = dict(empty5)
            for row in SPELL_ROWS:
                edits[record.base(5) + SCREEN_BUFFER + row * 16 + 12] = solid
            state = edited("wall", kit(60), edits)
            rams = game.headless(state, CAST, 60, mods)
            on, (end_f, end_x) = flight(rams, DELUGE_STEP)
            column = (-ROOM_W + 12 * 16, -ROOM_W + 13 * 16)               # room 5's column 12, from room 4's left
            assert on[-1][1] >= column[1] > end_x >= column[0], (on[-1], end_x, column)
            print(f"ok: a wall in room 5's column 12 ({column[0]} to {column[1] - 1} from room 4's left) ends the Deluge "
                  f"on the first frame it is in it: last at {on[-1][1]}, then {end_x}")

        if args.wants("kill"):
            state = edited("kill", kit(60), zozura5(200, hp=1, awake=True))
            reads = cast_in_window(state, lambda rs: rs[-1]["r5"]["entities"][7] != ZOZURA)
            first = next(r for r in reads if r["s"]["spell"][0] != NONE)
            last = reads[-1]
            r5_, s = last["r5"], last["s"]
            assert r5_["entities"][7] == SPELL_DEATH and all(r["screen"] == 4 for r in reads), (r5_, s["spell"])
            assert ZOZURA not in last["r4"]["entities"] and SPELL_DEATH not in last["r4"]["entities"]
            assert last["xp"] == first["xp"] + ZOZURA_XP and last["mp"] == first["mp"] + ZOZURA_MP, (first, last)
            kills = [v for v in s["visit_log"] if v[0] == 5 and v[1] == 7 and v[3] == ZOZURA and v[4] == SPELL_DEATH]
            assert kills, s["visit_log"]
            game.shot("kill-across")
            print(f"ok: the Deluge kills room 5's Zozura at its x {r5_['x'][7]} as a visitor (visit {kills[-1]}), its "
                  f"death in room 5 while he stands in room 4: experience {first['xp']} -> {last['xp']}, MP "
                  f"{first['mp']} -> {last['mp']}")

        def dormant_case(case, hero_x, room, slot, x):
            """A Zozura never seen at room `room`'s x, `slot`, past the view's
            left edge by more than it wakes at; the Deluge cast toward it from
            room 4's hero_x. Headless, every frame: it ends on the first frame
            past the edge. In the window: Seams' note of the end, the Zozura's
            HP, wake and visits, the experience."""
            if room == 5:
                edits = zozura5(x, slot=slot, facing_right=False)
                writes = kit(hero_x)
                zx = x - ROOM_W
            else:
                edits = dict(empty5)
                spawn(image, edits, record.base(4), slot, ZOZURA, x, floor_y)
                edits[record.block(4)] = 0                                 # room 4's sleepers: none seen
                writes = {**kit(hero_x), **{a + slot: edits.pop(record.base(4) + a + slot)
                                            for a in [0xBA, 0xC2] + list(range(0x2BC, 0x374, 8))
                                            if record.base(4) + a + slot in edits}}
                zx = x
            state = edited(case, writes, edits)
            edge = view_left(state)
            assert zx + 16 + 16 <= edge, ("dormant: more than 16 past the view", zx, edge)
            rams = game.headless(state, CAST, 100, mods)
            on, (end_f, end_x) = flight(rams, DELUGE_STEP)
            assert on[-1][1] >= edge > end_x, (on[-1], edge, end_x)
            assert all(r[EXPERIENCE] == rams[0][EXPERIENCE] for r in rams)
            if room == 4:
                assert all(r[ENTITIES + slot] == ZOZURA and r[HP_ + slot] == 9 for r in rams), "his own Zozura, whole"
            reads = cast_in_window(state, lambda rs: rs[-1]["s"]["spell_view_ends"] > 0 and
                                   rs[-1]["s"]["spell"][0] == NONE, 120)
            wait(10)
            s, rz, xp = batch(("seams_state", {}), ("fax_world_room", dict(room=room)),
                              ("read_ram", dict(addr=EXPERIENCE, len=2)))
            first = next(r for r in reads if r["s"]["spell"][0] != NONE)
            x16, at, why = s["spell_end"][:3]
            assert why == 1 and at == edge and x16 < edge <= x16 + DELUGE_STEP, (s["spell_end"], edge)
            assert rz["entities"][slot] == ZOZURA and rz["hp"][slot] == 9 and rz["x"][slot] == x, rz
            assert not rz["awake"] & (1 << slot), ("still dormant", rz["awake"])
            assert s["magic_dormant"] == 0 and not [v for v in s["visit_log"] if v[0] == room and v[1] == slot]
            xp = bytes.fromhex(xp["hex"])
            assert (xp[0] | xp[1] << 8) == first["xp"], "no experience"
            return on, edge, end_x, s

        if args.wants("dormant"):
            on, edge, end_x, s = dormant_case("dormant", 70, 5, 6, 80)
            # Its last frame, at the view's edge, in a picture (headless, frame exact).
            state = out / "dormant.cycstate"
            pic = out / f"{len(game.shots):02d}-view-edge.png"
            game.headless(state, CAST, on[-1][0] + 1, mods, picture=pic)
            game.shots.append(pic.name)
            print(f"ok: Deluge cast left from room 4's x 70 toward room 5's dormant Zozura at its x 80 (26 pixels past "
                  f"the view) ends at the view's edge (room 4's x {edge}): last at {on[-1][1]}, then {end_x} "
                  f"(Seams' note {s['spell_end'][:3]}); the Zozura keeps 9 HP, stays dormant and never visits, no "
                  f"experience, no dormant hit refused")

        if args.wants("own-room"):
            on, edge, end_x, s = dormant_case("own-room", 238, 4, 7, 0)
            print(f"ok: in his own room, from x 238, the Deluge ends at the view's edge (x {edge}: last at {on[-1][1]}, "
                  f"then {end_x}), never reaching the dormant Zozura at x 0, which keeps 9 HP and stays dormant")

        if args.wants("behind"):
            state = edited("behind", kit(16))
            edge = view_left(state)
            origin = -edge
            runs = []
            for n in (41, 64):
                runs += [(state, CAST, n, mods, None, None, out / f"behind-{n}.png"),
                         (state, NO_CAST, n, mods, None, None, out / f"behind-{n}-without.png")]
            results = game.headless_all(runs)
            looks = {}
            for n, rams in zip((41, 64), results[::2]):
                kind, x16 = track(rams, DELUGE_STEP)[-1]
                assert kind == DELUGE, (n, kind)
                box = (origin + x16 - 4, HUD + 140, origin + x16 + DELUGE_W + 4, HUD + 176)
                looks[n] = (x16, picture_diff(out / f"behind-{n}.png", out / f"behind-{n}-without.png", box))
            (open_x, shown), (tower_x, hidden) = looks[41], looks[64]
            tower = (-ROOM_W + 4 * 16, -ROOM_W + 6 * 16)                  # room 5's columns 4 and 5
            assert tower[0] <= tower_x + 4 and tower_x + 12 < tower[1] and not tower[0] <= open_x + 12 < tower[1]
            assert shown >= 20 and hidden <= shown // 8, looks
            print(f"ok: the Deluge in open air at room 4's x {open_x} shows ({shown} pixels), behind room 5's tower at "
                  f"{tower_x} it doesn't ({hidden})")

        if args.wants("fire"):
            state = edited("fire", kit(60, spell=FIRE))
            edge = view_left(state)
            origin = -edge
            frames = range(30, 38)
            runs = []
            for n in frames:
                runs += [(state, CAST, n, mods, None, None, out / f"fire-{n}.png"),
                         (state, NO_CAST, n, mods, None, None, out / f"fire-{n}-without.png")]
            results = game.headless_all(runs)
            drawn = []
            for n, rams in zip(frames, results[::2]):
                kind, x16 = track(rams, FIRE_STEP)[-1]
                assert kind == FIRE and x16 + FIRE_W <= 0, (n, kind, x16)    # wholly in room 5
                box = (origin + x16 - 4, HUD + 120, origin + x16 + FIRE_W + 4, HUD + 180)
                here = picture_diff(out / f"fire-{n}.png", out / f"fire-{n}-without.png", box)
                away = picture_diff(out / f"fire-{n}.png", out / f"fire-{n}-without.png",
                                    (origin + x16 + ROOM_W - 4, HUD + 120, origin + x16 + ROOM_W + FIRE_W + 4, HUD + 180))
                drawn.append(here > 20)
                assert away == 0 and (here > 20 or here == 0), (n, x16, here, away)
                assert drawn[-1] == bool(rams[-1][INTERRUPTS] & 2), ("drawn as InterruptCounter's bit 1 says", n)
                print(f"frame {n}: the Fire at room 4's x {x16}, InterruptCounter {rams[-1][INTERRUPTS]}: "
                      f"{here} pixels in room 5's space")
            assert sum(drawn) == 4 and all(drawn[i] != drawn[i + 2] for i in range(6)), drawn
            shot = out / f"{len(game.shots):02d}-fire-across.png"
            shutil.copy(out / f"fire-{frames[drawn.index(True)]}.png", shot)
            game.shots.append(shot.name)
            print(f"ok: Fire wholly in room 5 is drawn there on 4 of 8 frames, two at a time: {drawn}")

        if args.wants("crossing"):
            state = edited("crossing", kit(240, facing=RIGHT_FACING))
            rams = game.headless(state, [(0, "UP+B"), (2, "UP"), (4, "RIGHT")], 200, mods)
            cast_at = next(f for f, r in enumerate(rams) if r[CAST_TYPE] != NONE)
            end = next(f for f in range(cast_at, len(rams)) if rams[f][CAST_TYPE] == NONE)
            crossed = next(f for f, r in enumerate(rams) if r[SCREEN] == 3)
            assert cast_at < crossed < end and all(r[SCREEN] == (3 if f >= crossed else 4) for f, r in enumerate(rams))
            for f in range(cast_at + 1, end + 1):                          # 3 a frame, through the crossing
                assert (rams[f][CAST_X] - rams[f - 1][CAST_X]) % 256 == DELUGE_STEP, (f, rams[f - 1][CAST_X], rams[f][CAST_X])
            wrapped = next(f for f in range(cast_at + 1, end) if rams[f][CAST_X] < rams[f - 1][CAST_X])
            assert wrapped < crossed, ("through the seam before him", wrapped, crossed)
            xs = [x - ROOM_W for _, x in track(rams[:end], DELUGE_STEP)]    # from room 3's left edge
            last, gone = xs[-1], xs[-1] + DELUGE_STEP
            right_edge = [r[HERO_X] + 8 + VIEW_W // 2 for r in rams]      # in room 3, centred on him
            assert last + DELUGE_W < right_edge[end - 1] and gone + DELUGE_W >= right_edge[end], (
                last, right_edge[end - 1], gone, right_edge[end])
            print(f"ok: Deluge cast right from room 4's x 240 is through the seam on frame {wrapped}, before him; he "
                  f"crosses into room 3 on frame {crossed} and it flies on, 3 pixels a frame (on through room 3's right "
                  f"side too), until its right edge reaches the view's (room 3's x {right_edge[end]}) on frame {end}, "
                  f"at room 3's x {gone}")

        if args.wants("save-load"):
            state = edited("save-load", kit(60), zozura5(200, hp=1, awake=True))
            mid = out / "save-load-mid.cycstate"
            game.headless(state, CAST, 21, mods, save=(20, mid))
            rec = seams_record(mid.read_bytes())
            assert rec[0] == 2 and rec[RECORD_V1] == 0xFF and hw_ram(mid.read_bytes())[CAST_TYPE] == DELUGE, (
                rec[0], rec[RECORD_V1])
            at = game.continues_at(mid)
            after = []
            for i in range(2):
                p = out / f"save-load-after-{i}.cycstate"
                done = subprocess.run([str(game.exe), str(game.rom), "--load-state", str(mid), "--frames",
                                       str(at + 61), "--save-state", f"{at + 60}:{p}", "--mods-root", str(mods),
                                       "--no-save"], env=game.env, capture_output=True, text=True)
                assert done.returncode == 0 and p.exists(), done.stdout + done.stderr
                after.append(p.read_bytes())
            a, b = after
            assert hw_ram(a) == hw_ram(b), "the machine"
            for n in (3, 4, 5):
                assert Record(a).ram(n) == Record(b).ram(n), f"room {n}'s record"
            assert seams_record(a) == seams_record(b), "Seams' record"
            assert Record(a).ram(5)[ENTITIES + 7] != ZOZURA, "the Zozura dead in room 5"
            print("ok: saved with the Deluge in room 5 (its side -1 in Seams' record), two fresh games run 60 frames "
                  "from it to the same machine, rooms and record, the Zozura dead in both")
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
