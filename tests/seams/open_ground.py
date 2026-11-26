"""Play Seams are open ground (#166's slice 1) end to end in a hidden window.

    python3 tests/seams/open_ground.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/open-ground

The game runs uncapped and the script waits on the game's own frames, never
on the clock, on a reserved TCP port.

The parts start from fixtures (external prerequisites, not coverage,
tests/e2e/fax_e2e.py): eolis-zozuras (Eolis screen 4 as its two Zozuras crawl
in, the hero at its right; screen 5 left of it with two Zozuras of its own,
screen 3 right of it, the first of the town), eolis-town (the town's screen
1, room 2 left of it with a walking woman) and seams-town-words (dead there,
as the Guru speaks). Each edits its save: the live game's RAM, and rooms in
Seamless's record (faxanadu.world: a 32-byte header, then each room's
fax_world.c Room, its 2 KB of RAM first and Seams' block its last 384 bytes:
a wake bit per slot, then a mark per slot for a slot let through a side, 1
left and 2 right), filled as Sprites_PopulateNextAvailableSprite fills a
slot, from the ROM's tables. Ointment (DurationOintment below $80) keeps
touches off the hero where monsters come for him.

1. dormant: Zozuras in two rooms out of the view, room 5's at its x 80 and
   176 and two put in room 3 at its x 200 and 230, the hero on room 4 at x
   160 between them with its own Zozuras gone: for 300 frames none moves or
   wakes, and Seams counts them frozen. Walking left, room 5's nearer one
   stands still on every read until the first on which it is awake, and
   then its box is within 16 pixels of the view, which it was not on the
   read before. Walking right until it is wholly out of the view again, it
   keeps coming: awake, its X up by 20 or more in the next 160 frames.
2. follow: under Ointment, walking left from room 4 into room 5 and back
   right into room 3, the town's left end (a town room: fax_world_room's
   "town"; rooms 4 and 5 are not). The Zozuras, all awake, follow him: one
   of room 4's into room 5 behind him, and room 5's back into room 4 and on
   to the town's edge, room 4's right side, where he waits at room 3's x 60.
   There they turn back: a Zozura at room 4's x 236 or more is later 8
   pixels or more back from its furthest, and Seams counts the town's edge
   a wall. On no read, in 2,400 frames at most, is a Zozura in room 3 or a
   move into it in the log, and none is ever past room 4's x 240. Each
   Zozura is followed through Seams' log of moves (from room and slot, to
   room and slot).
3. walker: in Eolis's town, the hero on room 1 at x 12. Room 1's walking man
   is put in room 2 at its x 200, and room 2's walking woman (entity $45),
   whose script walks her 30 pixels, back 40, and so on, at its x 236. She
   walks into his room (a move 2 to 1); the man walks in (2 to 1) and,
   facing the hero after he passes him, out again (1 to 2).
4. shot: an enemy's fireball (entity $0A, as Sprite_CastMagic makes it)
   flying left from room 4's x 60, the hero at x 200: it goes through the
   seam into room 5 (a move 4 to 5) and flies on, its X falling, until it
   is wholly out of the view by 16 pixels, when Seams removes it, never
   before.
5. full: the same fireball with room 5 holding 4 actors (its Zozuras and two
   more, out of the view): it crosses. With 5: it doesn't (Seams counts it
   kept out), and the game removes it at room 4's edge, as the original's
   wall does.
6. straddle: a Zozura in room 5 at its x 250, awake and through its right
   side, straddling the seam: the picture differs from the same frame
   without it on both sides of the seam, in its rows, and room 5 holds
   cells drawn past its side.
7. load: dead in the town's room 1 with room 2's woman taken out of
   Seamless's record: the zone respawn's load (Screen_LoadSpriteInfo in
   play) brings room 2 back fresh, its woman where its list puts her.
8. person: a person who talks when touched (the intro's trigger, entity
   $4F, category 4, with its script, from the gate fixture) at room 5's x
   240, by the hero projected to its x 255: for 120 frames room 5 ticks on,
   with no failure, and the trigger stays (slice 0's hook).
9. town-people: in Eolis's town, the hero on room 1 at x 248, by its right
   side, and its walking man put at x 200 facing right. Room 8, right of it,
   is no town (its Monodron): the seam is the town's edge. For 900 frames
   at most the man comes to it (x 236 or more) and turns back, and is never
   in room 8, past x 240, or in a move out of room 1, the town's edge
   counted a wall.
10. save-load: a Zozura straddling room 5's right side, saved by the game
   itself 20 frames after it was put there, mid-crossing (its mark in room
   5's block): two fresh games, headless, run 60 frames from that save to
   the same machine and the same record, the Zozura through into room 4.

Screenshots: each part's moment (the Zozuras frozen, the wake, the Zozuras
turned back at the town's edge, the woman in his room, the fireball across
the seam, the straddling Zozura with and without, the walking man kept in).
"""
from pathlib import Path
import shutil
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
import fax_e2e  # noqa: E402
from fax_e2e import (Game, batch, chunks, dissolve, fixture, frame, page_through, parse_args, ram,  # noqa: E402
                     tcp, until, wait)
from seams_lose_nothing import hw_ram, spawn  # noqa: E402

AREA, SCREEN, HERO_X = 0x24, 0x63, 0x9E                              # Area_CurrentArea, _CurrentScreen, Player_PosX_Block
SPRITE_X, SPRITE_Y = 0xBA, 0xC2                                      # CurrentSprites_XPos_Full, _YPos
ENTITIES, FLAGS, PPU_OFFSETS, VALUES = 0x2CC, 0x2DC, 0x32C, 0x36C    # CurrentSprites_*
SLOT_ARRAYS = [SPRITE_X, SPRITE_Y] + list(range(0x2BC, 0x374, 8))     # every per-slot byte
OINTMENT, HP, PLAYER_IS_DEAD = 0x427, 0x431, 0x438                   # DurationOintment, Player_HP, PlayerIsDead
ZOZURA, FIREBALL, WOMAN, WALKING_MAN, TRIGGER, EMPTY = 0x47, 0x0A, 0x45, 0x34, 0x4F, 0xFF
ROOMS, HEADER, BLOCK = 83, 32, 384                                   # fax_world.c's MAX_ROOMS, STATE_HEADER; Seams' block
THROUGH_RIGHT = 2
VIEW_W, PAD, ROOM_W, HUD = 424, 16, 256, 32                       # the view, its margin, a room, the status bar
PARTS = ("dormant", "follow", "walker", "shot", "full", "straddle", "load", "person", "town-people", "save-load")
EDGE = 240                                                           # the most a 16-wide hitbox's X reaches in a room


@fax_e2e.recipe("seams-town-words", parent="eolis-town")
def _town_words(g):
    """Dead on Eolis town's screen 1: as the Guru speaks, the dissolve over."""
    g.poke({PLAYER_IS_DEAD: 1})
    dissolve()


def rooms_record(data):
    """Seamless's record in a save state: where its payload starts, and each
    Room's size."""
    for tag, p, n in chunks(data):
        if tag == b"MOD " and bytes(data[p:p + 64]).rstrip(b"\0") == b"faxanadu.world":
            size, rest = divmod(n - 64 - HEADER, ROOMS)
            assert not rest and size > 2048 + BLOCK, n
            assert data[p + 64 + 8] == 5, "Seamless's record is version 5"
            return p + 64, size
    raise AssertionError("no Seamless record in the save state")


class Record:
    """A save state's rooms, to edit: room n's RAM at ram(n), its block at block(n)."""

    def __init__(self, data):
        self.data = data
        self.start, self.size = rooms_record(data)

    def base(self, room):
        return HEADER + room * self.size

    def ram(self, room, addr=0, n=2048):
        at = self.start + self.base(room) + addr
        return self.data[at:at + n]

    def block(self, room):
        return self.base(room) + self.size - BLOCK          # an offset in the record, for fixture() edits


def slot_of(reply, entity):
    return [k for k in range(8) if reply["entities"][k] == entity]


def room(n):
    return tcp("fax_world_room", room=n)


def seams():
    return tcp("seams_state")


def awake(reply, slot):
    return bool(reply["awake"] >> slot & 1)


def world_box(reply, slot, view=None):
    """A Zozura's box (16 by 16 from its X, Y) in world pixels from its
    room's place, padded by PAD; and whether it meets the view."""
    x = reply["xy"][0] * ROOM_W + reply["x"][slot]
    box = (x - PAD, x + 16 + PAD)
    if view is None:
        return box
    return box, box[1] > view[0] and box[0] < view[0] + VIEW_W


def png_rows(path):
    """A PNG's rows of RGB(A) bytes (zlib, filters), with its bytes per pixel."""
    import struct
    import zlib
    data = Path(path).read_bytes()
    pos, idat, width = 8, b"", 0
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            width, _, depth, color = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and color in (2, 6), (depth, color)
            bpp = 3 if color == 2 else 4
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    raw, rows, stride, prev, at = zlib.decompress(idat), [], width * bpp, bytearray(width * bpp), 0
    while at < len(raw):
        f, line = raw[at], bytearray(raw[at + 1:at + 1 + stride])
        at += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b, c = prev[i], prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        prev = line
    return bpp, rows


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
        zozuras = game.state("eolis-zozuras")
        data = zozuras.read_bytes()
        live, record = hw_ram(data), Record(data)
        assert live[SCREEN] == 4 and list(live[ENTITIES + 6:ENTITIES + 8]) == [ZOZURA] * 2
        room5 = record.ram(5)
        r5 = [s for s in range(8) if room5[ENTITIES + s] == ZOZURA]
        assert len(r5) == 2, list(room5[ENTITIES:ENTITIES + 8])
        floor_y = room5[SPRITE_Y + r5[0]]
        no_live_zozuras = {ENTITIES + 6: EMPTY, ENTITIES + 7: EMPTY}
        kit = {OINTMENT: 0x7F, HP: 0x50}

        def fireball(edits, x, base=None, slot=3):
            """An enemy's fireball, as Sprite_CastMagic makes one: facing left,
            its tiles at $80."""
            writes = {}
            spawn(image, writes, 0 if base is None else base, slot, FIREBALL, x, floor_y - 8)
            writes[(0 if base is None else base) + PPU_OFFSETS + slot] = 0x80
            edits.update(writes)

        if args.wants("dormant"):
            edits = {}
            spawn(image, edits, record.base(3), 5, ZOZURA, 200, floor_y)
            spawn(image, edits, record.base(3), 6, ZOZURA, 230, floor_y)
            start("dormant", "eolis-zozuras", {HERO_X: 160, **kit, **no_live_zozuras}, edits)
            r3, r5_ = room(3), room(5)
            first = {3: [(s, r3["x"][s]) for s in slot_of(r3, ZOZURA)], 5: [(s, r5_["x"][s]) for s in slot_of(r5_, ZOZURA)]}
            assert len(first[3]) == 2 and len(first[5]) == 2, first
            frozen0, end = seams()["frozen"], frame() + 300
            while frame() < end:
                r3, r5_, s, hx = batch(("fax_world_room", dict(room=3)), ("fax_world_room", dict(room=5)),
                                       ("seams_state", {}), ("read_ram", dict(addr=HERO_X, len=1)))
                for n, reply in ((3, r3), (5, r5_)):
                    for slot, x in first[n]:
                        assert reply["x"][slot] == x and not awake(reply, slot), (n, slot, reply)
                wait(10)
            assert seams()["frozen"] >= frozen0 + 4 * 280, "four Zozuras frozen most of 300 frames"
            game.shot("dormant-frozen")
            print(f"ok: unseen Zozuras in rooms 3 and 5 ({first}) stand still for 300 frames, never woken")
            # The nearer of room 5's wakes on first sight.
            slot, x0 = max(first[5], key=lambda p: p[1])
            before, end = None, frame() + 600
            tcp("action", name="left", frames=600)
            while frame() < end:
                r5_, s = batch(("fax_world_room", dict(room=5)), ("seams_state", {}))
                (box, meets) = world_box(r5_, slot, s["view"])
                if not awake(r5_, slot):
                    assert r5_["x"][slot] == x0 and not meets, ("asleep, and out of the view", r5_, s["view"])
                    before = (box, s["view"])
                    continue
                assert before, "seen asleep first"
                assert abs(r5_["x"][slot] - x0) <= 3 and meets, ("woken as it came within 16 of the view", r5_, s)
                break
            else:
                raise AssertionError("room 5's Zozura never woke")
            tcp("action", name="left", frames=1)
            game.shot("dormant-woken")
            print(f"ok: room 5's Zozura at x {x0} woke on the first read its box met the view ({box} against the "
                  f"view from {s['view'][0]}), and stood still out of it until then (the read before: {before})")
            # It keeps coming once he has left it out of the view.
            tcp("action", name="right", frames=600)
            until("the Zozura out of the view", lambda: not world_box(room(5), slot, seams()["view"])[1], 400)
            tcp("action", name="right", frames=1)
            r5_ = room(5)
            x1 = r5_["x"][slot]
            assert awake(r5_, slot)
            wait(160)
            r5_ = room(5)
            gone = [m for m in seams()["log"] if m[0] == 5 and m[1] == slot]
            moved = r5_["x"][slot] - x1 if r5_["entities"][slot] == ZOZURA else 999
            assert gone or (moved >= 20 and awake(r5_, slot)), ("it keeps coming", x1, r5_)
            print(f"ok: out of the view again it keeps coming: x {x1} -> "
                  f"{r5_['x'][slot] if not gone else 'room 4'} in 160 frames, awake")

        if args.wants("follow"):
            start("follow", "eolis-zozuras", kit, {})
            seen = []                                     # every move, from Seams' log (its last 16, read often)

            def moves(log=None):
                for m in seams()["log"] if log is None else log:
                    if m not in seen:
                        seen.append(m)
                return seen

            def where(start_room, start_slot, fetch=True):
                """Where the Zozura that was in this slot is now, by the moves."""
                at = (start_room, start_slot)
                for m in moves() if fetch else seen:
                    if (m[0], m[1]) == at:
                        at = (m[2], m[3])
                return at

            moves()
            r3, r4, r5_ = room(3), room(4), room(5)
            assert r3["town"] and not r4["town"] and not r5_["town"], "room 3 is the town's left end"
            fives = [(5, s) for s in slot_of(r5_, ZOZURA)]
            fours = [(4, s) for s in slot_of(r4, ZOZURA)]
            assert len(fives) == 2 and len(fours) == 2
            tcp("action", name="left", frames=900)
            until("room 5", lambda: ram(SCREEN) == 5 and ram(HERO_X) < 200, 900)
            tcp("action", name="left", frames=1)
            game.poke(kit)
            until("one of room 4's Zozuras following him into room 5",
                  lambda: any(where(*z)[0] == 5 for z in fours), 900)
            game.shot("follow-room-5")
            tcp("action", name="right", frames=1200)
            until("room 3", lambda: ram(SCREEN) == 3 and ram(HERO_X) > 60, 1200)
            tcp("action", name="right", frames=1)
            game.poke(kit)
            # Every read from here: no Zozura in the town, none past room 4's
            # side. Each slot of room 4 at the edge, until it leaves the slot.
            edge0, furthest, turned, shot = seams()["town_edge"], {}, None, False
            end = frame() + 2400
            while frame() < end:
                r3, r4, s, hero = batch(("fax_world_room", dict(room=3)), ("fax_world_room", dict(room=4)),
                                        ("seams_state", {}), ("read_ram", dict(addr=SCREEN, len=1)))
                before = len(seen)
                moves(s["log"])
                assert ZOZURA not in r3["entities"], ("a Zozura in the town", r3)
                assert not [m for m in seen if m[4] == ZOZURA and m[2] == 3], ("a move into the town", seen)
                for m in seen[before:]:
                    if m[0] == 4:
                        furthest.pop(m[1], None)
                for k in slot_of(r4, ZOZURA):
                    x = r4["x"][k]
                    assert x <= EDGE, ("past room 4's side", k, x, r4)
                    if x >= EDGE - 4 or k in furthest:
                        furthest[k] = max(furthest.get(k, 0), x)
                    if k in furthest and x <= furthest[k] - 8 and turned is None:
                        turned = (k, furthest[k], x)
                if furthest and not shot and len([k for k in slot_of(r4, ZOZURA) if r4["x"][k] >= EDGE - 24]) >= 2:
                    game.shot("follow-town-edge")
                    shot = True
                back = [z for z in fives if where(*z, fetch=False)[0] == 4]
                if turned and shot and back and s["town_edge"] > edge0:
                    break
                assert int(hero["hex"], 16) == 3, "he waits in room 3"
                wait(4)
            else:
                raise AssertionError(f"the Zozuras at the town's edge: furthest {furthest}, turned {turned}, "
                                     f"shot {shot}, room 5's {[where(*z) for z in fives]}")
            trail = [(m[0], m[2]) for m in seen if m[4] == ZOZURA]
            assert (4, 5) in trail and (5, 4) in trail and (4, 3) not in trail, trail
            print(f"ok: room 4's Zozuras follow him into room 5, and room 5's back into room 4 "
                  f"({[where(*z) for z in fives + fours]} now) to the town's edge, and turn back there "
                  f"(slot {turned[0]} from x {turned[1]} to {turned[2]}; the edge a wall "
                  f"{s['town_edge'] - edge0} times); none in room 3: moves {trail}")

        if args.wants("walker"):
            town = game.state("eolis-town")
            tdata = town.read_bytes()
            trec, tlive = Record(tdata), hw_ram(tdata)
            men = [s for s in range(8) if tlive[ENTITIES + s] == WALKING_MAN]
            women = [s for s in range(8) if trec.ram(2)[ENTITIES + s] == WOMAN]
            assert tlive[SCREEN] == 1 and men and women, (tlive[SCREEN], men, women)
            # Room 1's walking man goes to room 2 (slot 6, at its x 200); the
            # woman, whose script walks her 30 pixels, back 40, and so on,
            # starts near the seam at her room's x 236.
            base2 = trec.base(2)
            edits = {base2 + a + 6: tlive[a + men[0]] for a in SLOT_ARRAYS}
            edits[base2 + SPRITE_X + 6] = 200
            edits[base2 + SPRITE_X + women[0]] = 236
            path = edited("walker", town, {HERO_X: 12, **{ENTITIES + s: EMPTY for s in men}}, edits)
            if game.running():
                game.close()
            game.launch(at="eolis-town")
            tcp("load_state", path=str(path))
            seen = []

            def moved(entity, a, b):
                seen.extend(m for m in seams()["log"] if m not in seen)
                return [m for m in seen if m[4] == entity and (m[0], m[2]) == (a, b)]

            until("the woman to walk into his room", lambda: moved(WOMAN, 2, 1), 1200)
            until("the walking man to walk into his room", lambda: moved(WALKING_MAN, 2, 1), 1200)
            game.shot("walker-in")
            until("the walking man to walk out again", lambda: moved(WALKING_MAN, 1, 2), 1500)
            print(f"ok: room 2's walking woman walks into his room 1 ({moved(WOMAN, 2, 1)[0]}), and the walking man "
                  f"in and out again ({moved(WALKING_MAN, 2, 1)[0]}, {moved(WALKING_MAN, 1, 2)[0]})")

        def shoot(case, extra):
            """The fireball from room 4's x 60, the hero at x 200; room 5
            with `extra` more Zozuras (out of the view). Returns the moves
            and counts once it is gone."""
            edits = {}
            for k, x in zip(range(extra), (20, 40, 60)):
                spawn(image, edits, record.base(5), k, ZOZURA, x, floor_y)
            writes = {HERO_X: 200, **kit, **no_live_zozuras}
            fireball(writes, 60)
            start(case, "eolis-zozuras", writes, edits)
            s0, path_xs = seams(), []
            end = frame() + 300
            while frame() < end:
                r4, r5_, s = batch(("fax_world_room", dict(room=4)), ("fax_world_room", dict(room=5)), ("seams_state", {}))
                in4, in5 = slot_of(r4, FIREBALL), slot_of(r5_, FIREBALL)
                if in5:
                    box, meets = world_box(r5_, in5[0], s["view"])
                    if case == "shot" and not any(p[0] == 5 for p in path_xs):
                        game.shot("shot-across")
                    path_xs.append((5, r5_["x"][in5[0]], meets))
                elif in4:
                    path_xs.append((4, r4["x"][in4[0]], True))
                else:
                    return s0, s, path_xs
            raise AssertionError(f"the fireball never went: {path_xs[-5:]}")

        if args.wants("shot"):
            s0, s, path_xs = shoot("shot", 0)
            crossed = [m for m in s["log"][-max(1, s["moves"] - s0["moves"]):] if m[4] == FIREBALL and (m[0], m[2]) == (4, 5)]
            in5 = [p for p in path_xs if p[0] == 5]
            assert crossed and in5, (crossed, path_xs)
            assert all(a[1] > b[1] for a, b in zip(in5, in5[1:]) if a[1] != b[1]), ("flying on left", in5)
            assert s["removed"] == s0["removed"] + 1, (s0["removed"], s["removed"])
            assert in5[-1][1] <= 236, ("gone only out of the view", in5[-1])
            print(f"ok: the fireball goes through the seam into room 5 (move {crossed[0]}), flies on to room 5's "
                  f"x {in5[-1][1]}, and is removed once wholly out of the view")

        if args.wants("full"):
            s0, s, path_xs = shoot("full-4", 2)
            assert any(p[0] == 5 for p in path_xs), ("with 4 actors in room 5 it crosses", path_xs)
            s0, s, path_xs = shoot("full-5", 3)
            assert not any(p[0] == 5 for p in path_xs), ("with 5 it doesn't", path_xs)
            assert s["kept_out"] > s0["kept_out"] and s["removed"] == s0["removed"], (s0, s)
            assert path_xs[-1][1] <= 4, ("the game removes it at room 4's edge", path_xs[-3:])
            print(f"ok: room 5 with 4 actors takes the fireball; with 5 its seam is a wall: kept out "
                  f"{s['kept_out'] - s0['kept_out']} times, gone at room 4's x {path_xs[-1][1]}")

        if args.wants("straddle"):
            base5 = record.base(5)
            empty5 = {base5 + ENTITIES + s: EMPTY for s in range(8)}
            with_z = dict(empty5)
            spawn(image, with_z, base5, 7, ZOZURA, 248, floor_y)
            with_z[base5 + FLAGS + 7] = 1                                    # facing right
            with_z[record.block(5)] = 0x80                                   # awake: slot 7
            with_z[record.block(5) + 2 + 7] = THROUGH_RIGHT                  # through its right side
            writes = {HERO_X: 40, **kit, **no_live_zozuras}
            shots = []
            for case, edits in (("straddle", with_z), ("straddle-without", empty5)):
                path = edited(case, zozuras, writes, edits)
                if not game.running():
                    game.launch(at="eolis-zozuras")
                png = out / f"{len(game.shots):02d}-{case}.png"
                tcp("load_state", path=str(path))
                _, r5_, world = batch(("screenshot", dict(layer="picture", path=str(png))),
                                      ("fax_world_room", dict(room=5)), ("fax_world", {}))
                game.shots.append(png.name)
                shots.append((png, r5_, world))
            (pa, ra, wa), (pb, _, wb) = shots
            # 0.375 pixels a frame: 20 frames from x 248 to its X's wrap into room 4.
            assert ra["entities"][7] == ZOZURA and 248 <= ra["x"][7] <= 255 and ra["cells"] > 0, ra
            assert wa["camera"] == wb["camera"], (wa["camera"], wb["camera"])
            seam = 0 - wa["camera"][0]                                       # room 4's left edge in the picture
            top = HUD + floor_y - wa["camera"][1]
            (bpp, a), (_, b) = png_rows(pa), png_rows(pb)
            left = right = 0
            for y in range(max(top - 8, HUD), min(top + 24, len(a))):
                for x in range(seam - 16, seam + 16):
                    if a[y][x * bpp:x * bpp + 3] != b[y][x * bpp:x * bpp + 3]:
                        left, right = left + (x < seam), right + (x >= seam)
            assert left > 0 and right > 0, ("the Zozura's pixels on both sides of the seam", left, right, seam)
            print(f"ok: a Zozura straddling the seam at room 5's x {ra['x'][7]} is drawn whole: {left} of its pixels "
                  f"left of the seam, {right} right of it, with {ra['cells']} cells past room 5's side")

        if args.wants("load"):
            words = game.state("seams-town-words")
            wrec = Record(words.read_bytes())
            woman = [s for s in range(8) if wrec.ram(2)[ENTITIES + s] == WOMAN]
            assert woman, list(wrec.ram(2)[ENTITIES:ENTITIES + 8])
            listed = {s: wrec.ram(2)[SPRITE_X + s] for s in woman}
            path = edited("load", words, {}, {wrec.base(2) + ENTITIES + s: EMPTY for s in woman})
            if game.running():
                game.close()
            game.launch(at="seams-town-words")
            tcp("load_state", path=str(path))
            assert not any(e == WOMAN for e in room(2)["entities"]), "taken out of room 2's record"
            page_through("the respawn", lambda: tcp("fax_world")["playing"] and ram(AREA) == 0, 3000)
            until("room 2 fresh", lambda: room(2)["live"] and WOMAN in room(2)["entities"], 600)
            r2 = room(2)
            back = slot_of(r2, WOMAN)
            assert back and all(abs(r2["x"][s] - listed.get(s, r2["x"][s])) <= 2 for s in back), (listed, r2)
            game.shot("load-fresh")
            print(f"ok: the respawn's load (in room {ram(SCREEN)}) brings room 2 back fresh: its woman at x "
                  f"{[r2['x'][s] for s in back]}, where its list puts her")

        if args.wants("person"):
            gate = hw_ram(game.state("gate").read_bytes())
            triggers = [s for s in range(8) if gate[ENTITIES + s] == TRIGGER]
            assert triggers and gate[VALUES + triggers[0]] != EMPTY, "the opening's trigger, with its script"
            base5 = record.base(5)
            edits = {base5 + ENTITIES + s: EMPTY for s in range(8)}
            for a in SLOT_ARRAYS:
                edits[base5 + a + 7] = gate[a + triggers[0]]
            edits[base5 + SPRITE_X + 7] = 240
            edits[base5 + SPRITE_Y + 7] = floor_y - 16
            start("person", "eolis-zozuras", {HERO_X: 20, **kit, **no_live_zozuras}, edits)
            r5_, world, at0 = room(5), tcp("fax_world"), frame()
            ticks, failures, skipped = r5_["ticks"], world["failures"], seams()["skipped"]
            wait(120)
            r5_, world = room(5), tcp("fax_world")
            assert world["failures"] == failures, f"Seamless failed {world['failures'] - failures} ticks"
            assert r5_["ticks"] >= ticks + 110 and r5_["entities"][7] == TRIGGER, r5_
            assert seams()["skipped"] > skipped, "his touch skipped in room 5's copy"
            game.shot("person")
            print(f"ok: a person who talks when touched at room 5's x 240 leaves it ticking ({r5_['ticks'] - ticks} "
                  f"ticks in {frame() - at0} frames, no failure)")

        if args.wants("town-people"):
            town = game.state("eolis-town")
            tlive = hw_ram(town.read_bytes())
            men = [s for s in range(8) if tlive[ENTITIES + s] == WALKING_MAN]
            assert tlive[SCREEN] == 1 and men, (tlive[SCREEN], men)
            man = men[0]
            path = edited("town-people", town, {HERO_X: 248, **kit, SPRITE_X + man: 200,
                                                  FLAGS + man: tlive[FLAGS + man] | 1}, {})
            if game.running():
                game.close()
            game.launch(at="eolis-town")
            tcp("load_state", path=str(path))
            r1, r8 = room(1), room(8)
            assert r1["town"] and not r8["town"] and r8["live"] and r1["edges"][1] == 8, (r1, r8)
            edge0, furthest, turned, shot = seams()["town_edge"], 0, None, False
            seen, end = [], frame() + 900
            while frame() < end:
                r1, r8, s = batch(("fax_world_room", dict(room=1)), ("fax_world_room", dict(room=8)),
                                  ("seams_state", {}))
                seen.extend(m for m in s["log"] if m not in seen)
                assert WALKING_MAN not in r8["entities"] and r1["entities"][man] == WALKING_MAN, (r1, r8)
                assert not [m for m in seen if m[4] == WALKING_MAN], ("out of room 1", seen)
                x = r1["x"][man]
                assert x <= EDGE, ("past room 1's side", x)
                if x >= EDGE - 4 or furthest:
                    furthest = max(furthest, x)
                if x >= EDGE - 4 and not shot:
                    game.shot("town-people-edge")
                    shot = True
                if furthest and x <= furthest - 8 and turned is None:
                    turned = (furthest, x)
                if turned and s["town_edge"] > edge0:
                    break
                wait(4)
            else:
                raise AssertionError(f"the walking man at the town's edge: furthest {furthest}, turned {turned}")
            print(f"ok: room 1's walking man comes to the town's edge (x {turned[0]}), turns back (x {turned[1]}) "
                  f"and never walks into room 8 (the edge a wall {s['town_edge'] - edge0} times)")

        if args.wants("save-load"):
            base5 = record.base(5)
            edits = {base5 + ENTITIES + s: EMPTY for s in range(8)}
            spawn(image, edits, base5, 7, ZOZURA, 244, floor_y)
            edits[base5 + FLAGS + 7] = 1
            edits[record.block(5)] = 0x80
            edits[record.block(5) + 2 + 7] = THROUGH_RIGHT
            start("save-load", "eolis-zozuras", {HERO_X: 120, **kit, **no_live_zozuras}, edits)
            wait(20)
            mid = game.save("mid-crossing")
            mrec = Record(mid.read_bytes())
            assert mrec.ram(5)[ENTITIES + 7] == ZOZURA and mrec.ram(5)[SPRITE_X + 7] > 240, "mid-crossing"
            assert mrec.data[mrec.start + mrec.block(5) + 2 + 7] == THROUGH_RIGHT, "through room 5's right side"
            # Two headless runs of 60 frames from that save, each loading it
            # into a fresh game: the same machine and the same record after.
            mods = out / "headless-mods"
            shutil.copytree(out / "game" / "mods" / "packages", mods / "packages")
            (mods / "state.toml").write_text(fax_e2e.features_toml({}))
            at = struct.unpack_from("<I", mid.read_bytes(), 28)[0]            # the frame it continues at
            after = []
            for i in range(2):
                path = out / f"after-{i}.cycstate"
                done = subprocess.run([str(game.exe), str(game.rom), "--load-state", str(mid), "--frames",
                                       str(at + 61), "--save-state", f"{at + 60}:{path}", "--mods-root", str(mods),
                                       "--no-save"], env=game.env, capture_output=True, text=True)
                assert done.returncode == 0 and path.exists(), done.stdout + done.stderr
                after.append(path.read_bytes())
            (a, b) = after
            ra, rb = Record(a), Record(b)
            assert hw_ram(a) == hw_ram(b), "the machine"
            for n in (4, 5):
                assert ra.ram(n) == rb.ram(n), f"room {n}'s record"
                assert (a[ra.start + ra.block(n):ra.start + ra.block(n) + BLOCK] ==
                        b[rb.start + rb.block(n):rb.start + rb.block(n) + BLOCK]), f"room {n}'s block"
            live = hw_ram(a)
            assert live[SCREEN] == 4 and ZOZURA in live[ENTITIES:ENTITIES + 8], "through into room 4, the live game's"
            assert ZOZURA not in ra.ram(5)[ENTITIES:ENTITIES + 8], "gone from room 5"
            print("ok: saved mid-crossing, two fresh games run 60 frames from it to the same machine and record, "
                  f"the Zozura through into room 4 at x {live[SPRITE_X + list(live[ENTITIES:ENTITIES + 8]).index(ZOZURA)]}")
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
