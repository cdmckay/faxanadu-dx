"""Play the map end to end in a hidden window.

    python3 tests/map/explored_map.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/map
    python3 tests/map/explored_map.py ... --game-arg=--world-view=seamless
    python3 tests/map/explored_map.py ... --part file

The game runs uncapped and the script waits on the game's own state, never on
the clock, on a reserved TCP port (tests/e2e/fax_e2e.py). Three parts, each its
own game:

explore  From Eolis town (the fixture: a new game in file 1, through the gate):
         left across screens 2 and 3, into the tool shop on 3 and out again. M
         opens the map: the game pauses (Game_PausedState, and the hero stays
         put with the d-pad held), on the Eolis page, with a box for each
         screen he has stood on (the gate, 1, 2, 3) and none for the rest, the
         gate's door and the shop marked where their doors are. B closes it.
         Then out of the shop onto the Trunk's first screen (its door edited to
         lead there, an external prerequisite), its enemies cleared, and right
         across two screens: the map opens on the Trunk page; Right and Left
         turn to Eolis and back; Start closes it.
file     The map's record set to screens of Eolis and the Trunk, a ladder and
         doors (what explore plays to, set directly): death writes the file
         (Save files saves at every respawn), the map's fields matching it.
         The death starts as the Guru speaks, from a saved game of it after
         its dissolve.
         Quit, continue file 1: the hero wakes in Eolis's temple, and the map
         shows what it did, the temple's door now too. Continuing from the
         title is the check, so this part boots.
original With the remaster off (the default view only): M does nothing, no
         pause.
"""
from pathlib import Path
import struct
import sys
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
import fax_e2e  # noqa: E402
from fax_e2e import (Game, act, dissolve, fixture, hold, page_through, parse_args, ram, step_toward,  # noqa: E402
                     tcp, until, wait)

AREA, SCREEN, PLAYER_X, PLAYER_Y = 0x24, 0x63, 0x9E, 0xA1
SCREEN_READY, SCROLL, PAUSED = 0x0E, 0x54, 0x120
TEXTBOX_CLOSED, MESSAGE_ID = 0x20C, 0x213
HP, PLAYER_IS_DEAD = 0x431, 0x438
SPRITE_ENTITIES = 0x2CC
SAVED_AREA, SAVED_SCREEN, SAVED_PALETTE, SAVED_POS, REGION = 0x3D5, 0x3D6, 0x3D7, 0x3D8, 0x435
EOLIS, TRUNK, BUILDINGS = 0, 1, 4
EOLIS_TEMPLE, TOOL_SHOP = 0x02, 0x04
PALETTE_OUTSIDE, REGION_TRUNK = 0x06, 1
NEGATIVE_THOUGHTS = 0x24                                            # the Guru's words after a death
FILE = "faxanadu_file1.txt"
PACKAGE = ("faxanadu.remastered", "remastered")
# Eolis's doors, in EOLIS_DOOR_LOCATIONS' order: 0 the gate (screen 0, block $9C), 3 the temple (screen 2),
# 5 the tool shop (screen 3, block $9A).
GATE_DOOR, TEMPLE_DOOR, SHOP_DOOR = 0, 3, 5
GATE_BLOCK, SHOP_BLOCK = 0x9C, 0x9A
# The map's save-state record (fax_map.c): a version, then seen, up and down (11 bytes an area each), then
# doors (6 bytes an area).
RECORD, SEEN, UP, DOWN, DOORS = "faxanadu.map", 1, 1 + 88, 1 + 176, 1 + 264
PARTS = ("explore", "file", "original")


def png(path):
    """A screenshot's pixels: (width, height, rows of (r, g, b) tuples)."""
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos, idat = 8, b""
    while pos < len(data):
        size, kind = struct.unpack_from(">I4s", data, pos)
        body = data[pos + 8:pos + 8 + size]
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", body)
            assert depth == 8 and color in (2, 6) and not interlace, (depth, color, interlace)
        elif kind == b"IDAT":
            idat += body
        pos += 12 + size
    bpp = 3 if color == 2 else 4
    raw, stride, rows, prior = zlib.decompress(idat), width * bpp, [], bytearray(width * bpp)
    for y in range(height):
        kind, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b, c = prior[i], prior[i - bpp] if i >= bpp else 0
            p = a + b - c
            pred = (0, a, b, (a + b) // 2,
                    a if abs(p - a) <= abs(p - b) and abs(p - a) <= abs(p - c) else b if abs(p - b) <= abs(p - c) else c)[kind]
            line[i] = (line[i] + pred) & 0xFF
        rows.append([tuple(line[x * bpp:x * bpp + 3]) for x in range(width)])
        prior = line
    return width, height, rows


def members(text):
    """A map set's hex digits: the first holds items 0-3, item 0 its lowest bit."""
    return {i * 4 + b for i, ch in enumerate(text) for b in range(4) if int(ch, 16) >> b & 1}


def bits(base, size, sets):
    """The record's bytes for {area: items}, at `base`, `size` bytes an area."""
    out = {}
    for area, items in sets.items():
        for i in items:
            out[base + area * size + i // 8] = out.get(base + area * size + i // 8, 0) | 1 << (i % 8)
    return out


def state():
    return tcp("map_state")


def place():
    return ram(AREA), ram(SCREEN)


def settled():
    return ram(SCREEN_READY) == 0 and ram(SCROLL) == 0xFF


def box_open():
    return ram(TEXTBOX_CLOSED) == 0


def read_file(path):
    return dict(line.split(" = ", 1) for line in path.read_text().splitlines() if " = " in line and not line.startswith("#"))


FILE_SEEN = {EOLIS: {0, 1, 2, 3}, TRUNK: {0, 1, 2, 3, 4}}          # the map the file part dies with


def file_record():
    """The map's record for FILE_SEEN, a ladder each way on the Trunk, and two of Eolis's doors."""
    return {**bits(SEEN, 11, FILE_SEEN), **bits(UP, 11, {TRUNK: {3}}), **bits(DOWN, 11, {TRUNK: {4}}),
            **bits(DOORS, 6, {EOLIS: {GATE_DOOR, SHOP_DOOR}})}


@fax_e2e.recipe("map-file-words", parent="eolis-town")
def _file_words(g):
    """In Eolis's town with file_record()'s map, dead: as the Guru speaks,
    the dissolve over (the respawn's save is what is checked)."""
    g.poke({PLAYER_IS_DEAD: 1}, {RECORD: file_record()})
    dissolve(1800)


def main():
    args = parse_args(__doc__, parts=PARTS)
    game = Game.from_args(args)
    out = game.out
    seamless = any("seamless" in a for a in args.game_arg)

    def walk(direction, target, frames=1800):
        """Hold `direction` until the hero stands on `target` (area, screen), settled."""
        assert hold(direction, lambda: place() == target and settled(), frames), \
            f"walking {direction} did not reach {target}: at {place()}"
        wait(4)

    def clear():
        game.poke({SPRITE_ENTITIES + i: 0xFF for i in range(8)})

    def open_map():
        opens = state()["opens"]
        tcp("key", name="M", frames=2)
        until("the map to open", lambda: state()["open"] and state()["opens"] == opens + 1, 300)
        until("the pause", lambda: ram(PAUSED) != 0, 120)
        wait(2)
        return state()

    def close_map(button="b"):
        tcp("action", name=button, frames=4)
        until("the map to close", lambda: not state()["open"], 300)
        until("the game to go on", lambda: ram(PAUSED) == 0, 120)
        wait(4)

    def check_boxes(name, s, area_seen):
        """Each box in the screenshot as map_state says it was drawn: seen ones in the one fill inside
        their walls (the hero's blinking), the rest black. Returns the pixels and the fill."""
        shot = game.shot(name)
        _, _, rows = png(shot)
        black = rows[0][0]
        fills, walls = set(), set()
        here = tuple(s["here"])
        for area, screen, x, y, w, h, seen in s["boxes"]:
            want = screen in area_seen.get(area, set())
            assert bool(seen) == want, (name, area, screen, seen, want)
            inside, corner = rows[y + 1][x + 1], rows[y][x]
            if not seen:
                assert inside == black and corner == black, (name, area, screen, inside, corner)
                continue
            assert inside != black and corner != black and inside != corner, (name, area, screen, inside, corner)
            walls.add(corner)
            if (area, screen) != here:
                fills.add(inside)
        assert len(fills) <= 1 and len(walls) == 1, (name, fills, walls)
        return rows, fills.pop() if fills else None

    def mark_at(rows, s, area, screen, block):
        """The pixel at the middle of a door's mark, where fax_map.c puts it on its screen's box."""
        x, y, w, h = next((b[2], b[3], b[4], b[5]) for b in s["boxes"] if b[0] == area and b[1] == screen)
        bx, by = block & 15, block >> 4
        return rows[y + 1 + by * (h - 5) // 12 + 1][x + 1 + bx * (w - 5) // 15]

    try:
        if args.wants("explore"):
            game.set_features({PACKAGE: True})
            game.launch(at="eolis-town")
            assert state()["enabled"] and place() == (EOLIS, 1), (state(), place())
            walk("left", (EOLIS, 2))
            walk("left", (EOLIS, 3))
            door = (SHOP_BLOCK & 15) * 16
            while not door - 7 <= ram(PLAYER_X) <= door + 8:
                step_toward(door - 7, door + 8)
            act("up", 4, 10)
            until("the tool shop", lambda: place() == (BUILDINGS, TOOL_SHOP) and settled(), 600)
            wait(10)
            shop = game.save("shop")
            walk("right", (EOLIS, 3))
            s = open_map()
            assert s["page_name"] == "Eolis" and s["here"] == [EOLIS, 3] and s["pages_seen"] == 1, s
            seen = {EOLIS: {0, 1, 2, 3}}
            assert members(s["seen"][EOLIS]) == seen[EOLIS], s["seen"]
            assert members(s["doors"][EOLIS]) == {GATE_DOOR, SHOP_DOOR}, s["doors"]
            x, y = ram(PLAYER_X), ram(PLAYER_Y)
            tcp("action", name="right", frames=30)                 # paused: he stays put, the page too
            wait(36)
            assert (ram(PLAYER_X), ram(PLAYER_Y)) == (x, y) and ram(PAUSED) and state()["page_name"] == "Eolis"
            rows, fill = check_boxes("eolis-map", state(), seen)
            s = state()
            shop_mark, gate_mark = mark_at(rows, s, EOLIS, 3, SHOP_BLOCK), mark_at(rows, s, EOLIS, 0, GATE_BLOCK)
            assert shop_mark not in (fill, rows[0][0]) and gate_mark not in (fill, rows[0][0], shop_mark), \
                (shop_mark, gate_mark, fill)
            close_map()
            assert ram(PAUSED) == 0 and place() == (EOLIS, 3)
            print(f"ok: the map opens paused on Eolis with boxes for the gate and screens 1-3 only, and marks for "
                  f"the gate's door and the shop's ({'Seamless' if seamless else 'the original view'})")

            trunk = out / "trunk.cycstate"
            fixture(shop, trunk, {SAVED_AREA: TRUNK, SAVED_SCREEN: 0, SAVED_PALETTE: PALETTE_OUTSIDE,
                                  SAVED_POS: 0xA1, REGION: REGION_TRUNK, HP: 0x50})
            game.load(trunk)
            hold("right", lambda: ram(AREA) != BUILDINGS, 600)
            until("the Trunk", lambda: place() == (TRUNK, 0) and settled(), 600)
            clear()
            walk("right", (TRUNK, 1))
            clear()
            walk("right", (TRUNK, 2))
            clear()
            s = open_map()
            assert s["page_name"] == "Trunk" and s["pages_seen"] == 2, s
            seen[TRUNK] = {0, 1, 2}
            assert members(s["seen"][TRUNK]) == seen[TRUNK] and members(s["seen"][EOLIS]) == seen[EOLIS], s["seen"]
            check_boxes("trunk-map", s, seen)
            tcp("action", name="right", frames=4)
            until("the Eolis page", lambda: state()["page_name"] == "Eolis", 120)
            check_boxes("trunk-map-eolis-page", state(), seen)
            wait(8)
            tcp("action", name="left", frames=4)
            until("the Trunk page again", lambda: state()["page_name"] == "Trunk", 120)
            close_map("start")                                      # Start closes it too
            print("ok: on the Trunk the map opens on its page, and Right and Left turn to Eolis and back")
            game.close()

        if args.wants("file"):
            game.set_features({PACKAGE: True})
            # Dead in Eolis's town with this map, as the Guru speaks (a saved
            # game of the death after its dissolve).
            game.launch(at="map-file-words")
            seen = FILE_SEEN
            saves = tcp("files_state")["saves"]
            before = state()
            assert members(before["seen"][TRUNK]) == seen[TRUNK] and members(before["up"][TRUNK]) == {3}, before
            until("the Guru's words", lambda: ram(MESSAGE_ID) == NEGATIVE_THOUGHTS, 1800)
            page_through("the box to close", lambda: not box_open())    # A on each page until the box closes
            until("the respawn's save", lambda: tcp("files_state")["saves"] > saves, 1800)
            saved = read_file(out / "game" / FILE)
            for area, key in ((EOLIS, "map.eolis"), (TRUNK, "map.trunk")):
                fields = saved[key].split(" ")
                assert len(fields) == 4 and members(fields[0]) == seen[area], (key, saved[key])
                assert [members(f) for f in fields[1:3]] == [members(before["up"][area]), members(before["down"][area])]
                assert members(fields[3]) == members(before["doors"][area]), (key, saved[key], before)
            print(f"ok: dying wrote the map to {FILE}: map.eolis = {saved['map.eolis']}, "
                  f"map.trunk = {saved['map.trunk']}")
            game.close()

            game.launch()
            until("the file select", lambda: tcp("files_state")["shown"], 2000)
            tcp("pad", buttons="start", frames=10)                  # file 1: continue
            until("Eolis's temple", lambda: place() == (BUILDINGS, EOLIS_TEMPLE) and settled(), 1800)
            wait(10)
            s = open_map()
            assert s["page_name"] == "Eolis" and s["here"] == [EOLIS, 2] and s["pages_seen"] == 2, s
            for key in ("seen", "up", "down"):
                assert [members(t) for t in s[key]] == [members(t) for t in before[key]], (key, s[key], before[key])
            assert members(s["doors"][EOLIS]) == {GATE_DOOR, SHOP_DOOR, TEMPLE_DOOR}, s["doors"]
            check_boxes("continued-map", s, seen)
            tcp("action", name="left", frames=4)
            until("the Trunk page", lambda: state()["page_name"] == "Trunk", 120)
            check_boxes("continued-map-trunk", state(), seen)
            close_map()
            print("ok: continuing file 1 puts the map back as it was, with the temple he woke in")
            game.close()

        if args.wants("original") and not seamless:
            game.set_features({PACKAGE: False})
            game.launch(at="gate")
            assert not state()["enabled"] and place() == (EOLIS, 0), state()
            for _ in range(3):
                tcp("key", name="M", frames=2)
                wait(30)
                assert ram(PAUSED) == 0 and not state()["open"], state()
            print("ok: with the remaster off, M does nothing")
            game.close()
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    main()
