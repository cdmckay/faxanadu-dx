"""Play the quest log end to end in a hidden window.

    python3 tests/quests/quest_log.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/quests
    python3 tests/quests/quest_log.py ... --game-arg=--world-view=seamless
    python3 tests/quests/quest_log.py ... --part king

The game runs uncapped and the script waits on the game's own state, never on
the clock, on a reserved TCP port (tests/e2e/fax_e2e.py). Every mark is
checked in the screenshot itself: the bold "!" or "?" in gold, outlined in
black, where quest_state says it was drawn. Six parts, each its own game:

elf       From Eolis town (the fixture: a new game, through the gate): left to
          screen 2, where the woman walking there has a "!". Talking to her
          gives The Ring of Elf: her line ("Go to see the Guru before you see
          the king."), then "New quest / The Ring of Elf" in the same box, and
          her "!" is gone. L opens the log, paused, with the quest and its
          objective; B closes it. In the temple, a "?" over the Guru; he gives
          the ring, then "Quest complete / The Ring of Elf", and the log lists
          it under Completed.
king      The King's room, with the ring: a "!" over the King before the hero
          speaks to him; his plea, then "New quest / The King's Plea", and no
          mark after. The log shows its objective and "0 of 3 springs".
meteorite The house on Mist $1F, with the Black Onyx and The Meteorite given:
          a "?" over the man, the giver waiting on a finished objective. He
          gives the Ace key, then "Quest complete / The Meteorite".
wall      The Wall given (set in the record), the hero walks out into Forepaw:
          once its name below the HUD has faded and gone, "Quest complete /
          The Wall" in the same place: reaching the next town completes it,
          outside any conversation. (Forepaw's screen 2 leaves TextBox_Closed at
          0 with no box up; the name still goes in its time.) The Mattock, done
          too but never given, is not said.
file      The King's Plea given (set in the record): dying writes it to the
          file (Save files saves at every respawn), quests.given; the death
          starts as the Guru speaks, from a saved game of it. Quit,
          continue file 1: the log shows it again. Continuing from the title
          is the check, so this part boots.
original  With the remaster off (the default view only): no quest log, no
          marks, and L does nothing.
"""
from pathlib import Path
import struct
import sys
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
import fax_e2e  # noqa: E402
from fax_e2e import (PAGE_FRAMES, Game, act, batch, dissolve, fixture, frame, hold, hold_a, hurry_page,  # noqa: E402
                     let_go, page_through, parse_args, ram, step_toward, tcp, type_out, typed, until, wait)

AREA, SCREEN, PLAYER_X = 0x24, 0x63, 0x9E
SCREEN_READY, SCROLL, PAUSED = 0x0E, 0x54, 0x120
TEXTBOX_CLOSED, MESSAGE_ID = 0x20C, 0x213
SPRITE_ENTITIES, SPRITE_X, SPRITE_VALUES = 0x2CC, 0xBA, 0x36C
GOLD, SPECIAL_ITEMS, HP, PLAYER_IS_DEAD = 0x392, 0x42C, 0x431, 0x438
SAVED_AREA, SAVED_SCREEN, SAVED_PALETTE, SAVED_POS, REGION = 0x3D5, 0x3D6, 0x3D7, 0x3D8, 0x435
EOLIS, MIST, TOWNS, BUILDINGS = 0, 2, 3, 4
FOREPAW = 2                                                         # its first screen in the towns' area
EOLIS_TEMPLE, KINGS_ROOM, MIST_HOUSE = 0x02, 0x07, 0x22
PALETTE_MIST, REGION_MIST, PALETTE_TOWN, REGION_TRUNK = 0x0A, 2, 0x1B, 1
RING_OF_ELF, BLACK_ONYX = 0x80, 0x01
NEGATIVE_THOUGHTS = 0x24                                            # the Guru's words after a death
WOMAN, GURU, KING, MIST_HOUSE_MAN = 0x03, 0x04, 0x08, 0x46          # their scripts (CurrentSprites_Values)
GO_SEE_THE_GURU, THE_KINGS_PLEA = 0x2A, 0x35                        # their lines
PACKAGE = ("faxanadu.remastered", "remastered")
ZONE_NAMES, QUESTS = "faxanadu.zone-names", "faxanadu.quests"       # save-state records
# fax_quests.c's record: a version, then given (4 bytes, a bit per quest in fax_quest_table.h's order).
GIVEN = 1
QUEST_BIT = {"elf": 0, "king": 1, "mattock": 2, "wall": 3, "meteorite": 10}
GOLD_INK, EDGE = (232, 173, 0), (0, 0, 0)                           # NES $27 and $0F, as the host decodes them
FILE = "faxanadu_file1.txt"
PARTS = ("elf", "king", "meteorite", "wall", "file", "original")


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


def font(rom, ch):
    """A character of the dialogue font (bank 13, TILE_GLYPHS_START): 8 rows of bits."""
    at = 16 + 13 * 16384 + (ord(ch) - 0x20) * 8
    return rom[at:at + 8]


def mark_pixels(rom, ch):
    """The bold mark fax_quests.c draws: {(dx, dy): ink or edge} from its top-left."""
    g = font(rom, ch)

    def bold(r, c):
        return 0 <= r < 8 and 0 <= c < 9 and ((c < 8 and g[r] >> (7 - c) & 1) or (c > 0 and g[r] >> (8 - c) & 1))
    out = {}
    for r in range(-1, 9):
        for c in range(-1, 10):
            if bold(r, c):
                out[(c, r)] = GOLD_INK
            elif any(bold(r + dr, c + dc) for dr in (-1, 0, 1) for dc in (-1, 0, 1)):
                out[(c, r)] = EDGE
    return out


def state():
    return tcp("quest_state")


def place():
    return ram(AREA), ram(SCREEN)


def settled():
    return ram(SCREEN_READY) == 0 and ram(SCROLL) == 0xFF


def box_open():
    return ram(TEXTBOX_CLOSED) == 0


def marks(script):
    return [m for m in state()["marks"] if m["script"] == script]


def read_file(path):
    return dict(line.split(" = ", 1) for line in path.read_text().splitlines() if " = " in line and not line.startswith("#"))


@fax_e2e.recipe("quests-file-words", parent="eolis-town")
def _file_words(g):
    """In Eolis's town with The King's Plea given (the record), dead: as the
    Guru speaks, the dissolve over (the respawn's save is what is checked)."""
    g.poke({PLAYER_IS_DEAD: 1}, {QUESTS: {GIVEN: 1 << QUEST_BIT["king"]}})
    dissolve(1800)


def main():
    args = parse_args(__doc__, parts=PARTS)
    game = Game.from_args(args)
    out = game.out
    seamless = any("seamless" in a for a in args.game_arg)
    view = "Seamless" if seamless else "the original view"

    def shot_marks(name, script):
        """A screenshot and the marks over `script`'s person in that same picture:
        both served at one frame boundary, so a person walking is where he was drawn."""
        path = out / f"{len(game.shots):02d}-{name}.png"
        _, reply = batch(("screenshot", dict(layer="picture", path=str(path))), ("quest_state", {}))
        game.shots.append(path.name)
        return path, [m for m in reply["marks"] if m["script"] == script]

    def check_mark(shot, mark, ch, present=True):
        """The mark `mark` (from quest_state) in the screenshot `shot`: its every
        pixel, or (present=False) not all of them."""
        _, _, rows = png(shot)
        pixels = mark_pixels(game.image, ch)
        if present:
            hits = sum(rows[mark["y"] + dy][mark["x"] + dx] == want for (dx, dy), want in pixels.items())
            assert hits == len(pixels), (shot.name, mark, hits, len(pixels))
        else:   # not one of its gold pixels
            gold = [(dx, dy) for (dx, dy), want in pixels.items() if want == GOLD_INK]
            assert not any(rows[mark["y"] + dy][mark["x"] + dx] == GOLD_INK for dx, dy in gold), (shot.name, mark)

    def walk(direction, target, frames=1800):
        assert hold(direction, lambda: place() == target and settled(), frames), \
            f"walking {direction} did not reach {target}: at {place()}"
        wait(4)

    def walk_to(x_min, x_max):
        for _ in range(600):
            x = ram(PLAYER_X)
            if x_min <= x <= x_max:
                return
            step_toward(x_min, x_max, box_open)
        raise AssertionError(f"could not reach x {x_min}-{x_max}")

    def go_in(door):
        """Up at the door in block column (door & 15) of this screen (again,
        if something knocked him off it)."""
        column = (door & 15) * 16
        for _ in range(4):
            walk_to(column - 6, column + 4)
            act("up", 4, 10)
            try:
                until("the building", lambda: ram(AREA) == BUILDINGS and settled(), 120)
                break
            except AssertionError:
                pass
        until("the building", lambda: ram(AREA) == BUILDINGS and settled(), 600)
        wait(30)

    def warp(source, name, area, screen, door, writes=None, records=None):
        """From inside a building, walk out onto the door `door` (block YX) on
        `screen` of `area` (Eolis unless the writes give its palette and
        region), and go in."""
        target = out / f"{name}.cycstate"
        fixture(source, target, {SAVED_AREA: area, SAVED_SCREEN: screen, SAVED_POS: door, HP: 0x50, **(writes or {})},
                records={ZONE_NAMES: {5: area, 6: screen}, **(records or {})})
        game.load(target)
        hold("right", lambda: ram(AREA) != BUILDINGS, 600)
        until(f"area {area} screen {screen:#x}", lambda: place() == (area, screen) and settled(), 600)
        game.poke({SPRITE_ENTITIES + i: 0xFF for i in range(8)})   # the screen's enemies away: nothing knocks him back
        wait(20)
        go_in(door)

    def talk_left(stop=0, climb=False):
        """Walk left to the person behind the counter, or to x `stop` (jumping
        up steps if `climb`), and press Up until a box opens."""
        for _ in range(60):
            if ram(PLAYER_X) <= stop:
                break
            if climb:
                tcp("action", name="left", frames=24)
                tcp("action", name="a", frames=12)
                wait(30)
            else:
                before = ram(PLAYER_X)
                act("left", 8, 2)
                if ram(PLAYER_X) == before:
                    break
        for _ in range(5):
            act("up", 4, 10)
            try:
                until("a box", box_open, 120)
                return
            except AssertionError:
                pass
        raise AssertionError("nobody answered")

    def converse(label=None):
        """Page through the conversation to its end; the quest log's lines
        ("New quest|..." and "Quest complete|...") in the order said, each
        screenshot (as `label`-N) once it has typed out."""
        said, end = [], frame() + 9000
        held = False                # A held: since the press that turned the last page, or from hurry_page()

        def turn():
            """A new press turns the page, and A stays down while the next types."""
            nonlocal held
            if held:
                let_go()
            hold_a()
            held = True
            wait(PAGE_FRAMES)
        try:
            while frame() < end:
                s = state()
                if s["saying"] and s["lines"] > lines_before + len(said):
                    if held:                                # never a new A on it: it would turn the page
                        until("the quest log's line to type out", typed, 300)
                    else:
                        type_out("the quest log's line to type out", 300)
                    s = state()
                    said.append(s["saying"])
                    if label:
                        game.shot(f"{label}-{len(said)}")
                    turn()
                    continue
                if not box_open() and s["in_play"]:
                    return said
                if box_open() and typed():
                    turn()
                elif not box_open():
                    if held:
                        let_go()
                        held = False
                    act("a", 4, 2)                          # a new press, between boxes
                elif not held:
                    held = hurry_page()
        finally:
            if held:
                let_go()
        raise AssertionError(f"the conversation did not end; said {said}")

    def open_log():
        opens = state()["opens"]
        tcp("key", name="L", frames=2)
        until("the log to open", lambda: state()["open"] and state()["opens"] == opens + 1, 300)
        until("the pause", lambda: ram(PAUSED) != 0, 120)
        wait(2)
        return state()

    def close_log(button="b"):
        tcp("action", name=button, frames=4)
        until("the log to close", lambda: not state()["open"], 300)
        until("the game to go on", lambda: ram(PAUSED) == 0, 120)
        wait(4)

    def rows(s):
        return [text for _, text in s["rows"]]

    lines_before = 0
    try:
        if args.wants("elf"):
            game.set_features({PACKAGE: True})
            game.launch(at="eolis-town")
            s = state()
            assert s["enabled"] and s["given"] == [] and place() == (EOLIS, 1), (s, place())
            if seamless:   # she is in view from screen 1 already, in Seamless's copy of screen 2
                until("her mark next door", lambda: any(m["room"] == 2 for m in marks(WOMAN)), 300)
                assert place() == (EOLIS, 1) and marks(WOMAN)[0]["mark"] == "!", (place(), marks(WOMAN))
                print("ok: Seamless shows her \"!\" in the room next door")
            walk("left", (EOLIS, 2))
            until("her mark", lambda: marks(WOMAN), 120)
            shot, [m] = shot_marks("woman-mark", WOMAN)
            assert m["mark"] == "!" and (m["area"], m["room"]) == (EOLIS, 2), m
            check_mark(shot, m, "!")
            # Catch her: she walks back and forth on the screen.
            for _ in range(300):
                if box_open():
                    break
                slot = next(i for i in range(8) if ram(SPRITE_VALUES + i) == WOMAN)
                her = ram(SPRITE_X + slot)
                if abs(ram(PLAYER_X) - her) <= 10:
                    act("up", 4, 6)
                else:
                    step_toward(her - 6, her + 6, box_open)
            until("her line", lambda: ram(MESSAGE_ID) == GO_SEE_THE_GURU, 300)
            lines_before = state()["lines"]
            said = converse("woman")
            assert said == ["New quest|The Ring of Elf"], said
            s = state()
            assert s["given"] == ["elf"] and s["active"] == ["elf"] and not marks(WOMAN), s
            print(f"ok: the woman on Eolis screen 2 has a \"!\"; she gives The Ring of Elf, said in her box ({view})")

            s = open_log()
            assert ram(PAUSED) and rows(s)[:4] == ["The Ring of Elf", "See the Guru of", "Eolis before you",
                                                   "see the King."], s["rows"]
            game.shot("log-ring-of-elf")
            x = ram(PLAYER_X)
            tcp("action", name="right", frames=20)                  # paused: he stays put
            tcp("key", name="M", frames=2)                          # and the map stays shut over it
            wait(40)
            assert ram(PLAYER_X) == x and state()["open"] and not tcp("map_state")["open"]
            close_log()
            print("ok: L opens the log, paused, with The Ring of Elf and its objective, M does not open the map "
                  "over it; B closes it")

            go_in(0x95)                                             # the temple
            assert place() == (BUILDINGS, EOLIS_TEMPLE), place()
            until("the Guru's mark", lambda: marks(GURU), 120)
            shot, [m] = shot_marks("guru-mark", GURU)
            assert m["mark"] == "?", m
            check_mark(shot, m, "?")
            talk_left(stop=48)
            lines_before = state()["lines"]
            said = converse("guru")
            assert said == ["Quest complete|The Ring of Elf"], said
            assert ram(SPECIAL_ITEMS) & RING_OF_ELF and not marks(GURU)
            s = state()
            assert s["done"] == ["elf"] and s["active"] == [], s
            s = open_log()
            assert rows(s) == ["Completed", "The Ring of Elf"], s["rows"]
            game.shot("log-completed")
            close_log("start")                                      # Start closes it too
            print(f"ok: a \"?\" over Eolis's Guru; his ring completes The Ring of Elf, which the log lists "
                  f"under Completed ({view})")
            game.close()

        if args.wants("king"):
            game.set_features({PACKAGE: True})
            shop = game.state("eolis-meat-shop")
            game.launch(at="eolis-meat-shop")
            warp(shop, "kings-room", EOLIS, 7, 0x98, {SPECIAL_ITEMS: RING_OF_ELF})
            assert place() == (BUILDINGS, KINGS_ROOM), place()
            until("the King's mark", lambda: marks(KING), 120)
            shot, [m] = shot_marks("king-mark", KING)
            assert m["mark"] == "!", m
            check_mark(shot, m, "!")
            talk_left(stop=48, climb=True)
            until("his plea", lambda: ram(MESSAGE_ID) == THE_KINGS_PLEA, 300)
            lines_before = state()["lines"]
            said = converse("king")
            assert said == ["New quest|The King's Plea"], said
            assert ram(GOLD, 3) == bytes([0xDC, 0x05, 0x00]), "the King gives 1500 Golds"
            wait(10)
            assert not marks(KING), state()["marks"]
            check_mark(game.shot("king-no-mark"), m, "!", present=False)
            s = open_log()
            assert rows(s)[:6] == ["The King's Plea", "Bring the water", "back to the Elf", "fountain, our",
                                   "life source.", "0 of 3 springs"], s["rows"]
            game.shot("log-kings-plea")
            close_log()
            print(f"ok: a \"!\" over the King until he gives The King's Plea, none after; the log shows it with "
                  f"its springs ({view})")
            game.close()

        if args.wants("meteorite"):
            game.set_features({PACKAGE: True})
            shop = game.state("eolis-meat-shop")
            game.launch(at="eolis-meat-shop")
            given = 1 << QUEST_BIT["meteorite"]
            warp(shop, "mist-house", MIST, 0x1F, 0x94,
                 {SPECIAL_ITEMS: RING_OF_ELF | BLACK_ONYX, SAVED_PALETTE: PALETTE_MIST, REGION: REGION_MIST},
                 {QUESTS: {GIVEN + i: given >> (8 * i) & 0xFF for i in range(4)}})
            assert place() == (BUILDINGS, MIST_HOUSE), place()
            s = state()
            assert s["active"] == ["meteorite"], s
            until("the man's mark", lambda: marks(MIST_HOUSE_MAN), 120)
            shot, [m] = shot_marks("meteorite-mark", MIST_HOUSE_MAN)
            assert m["mark"] == "?", m
            check_mark(shot, m, "?")
            talk_left()
            lines_before = state()["lines"]
            said = converse("meteorite")
            assert said == ["Quest complete|The Meteorite"], said
            assert "meteorite" in state()["done"] and not marks(MIST_HOUSE_MAN)
            print(f"ok: a \"?\" over the man on Mist $1F, waiting on the meteorite the hero holds; his Ace key "
                  f"completes The Meteorite ({view})")
            game.close()

        if args.wants("wall"):
            game.set_features({PACKAGE: True})
            shop = game.state("eolis-meat-shop")
            game.launch(at="eolis-meat-shop")
            given = 1 << QUEST_BIT["wall"]
            target = out / "forepaw.cycstate"
            fixture(shop, target, {SAVED_AREA: TOWNS, SAVED_SCREEN: FOREPAW, SAVED_PALETTE: PALETTE_TOWN,
                                   SAVED_POS: 0x99, REGION: REGION_TRUNK, HP: 0x50},
                    records={QUESTS: {GIVEN + i: given >> (8 * i) & 0xFF for i in range(4)}})
            game.load(target)
            assert state()["active"] == ["wall"], state()
            hold("right", lambda: ram(AREA) != BUILDINGS, 600)
            until("Forepaw", lambda: place() == (TOWNS, FOREPAW) and settled(), 600)
            until("Forepaw's name", lambda: tcp("zone_state")["showing"] == "Forepaw", 120)
            assert state()["banner"] is None, state()               # it waits for the zone's name
            until("the quest's banner", lambda: state()["banner"] == "wall", 600)
            name = tcp("zone_state")
            assert name["showing"] is None and name["age"] == 160, name    # after the name, not over it
            wait(20)
            s = state()
            assert {"wall", "mattock"} <= set(s["done"]) and s["active"] == [] and s["banners"] == 1, s
            game.shot("wall-complete")
            until("the banner to go", lambda: state()["banner"] is None, 600)
            assert state()["banners"] == 1, state()
            print(f"ok: walking into Forepaw completes The Wall, said below the HUD once Forepaw's name has "
                  f"gone; the Mattock, never given, is done but not said ({view})")
            game.close()

        if args.wants("file"):
            game.set_features({PACKAGE: True})
            game.launch(at="quests-file-words")                 # dead, as the Guru speaks
            saves = tcp("files_state")["saves"]
            assert state()["given"] == ["king"], state()
            until("the Guru's words", lambda: ram(MESSAGE_ID) == NEGATIVE_THOUGHTS, 1800)
            page_through("the box to close", lambda: not box_open())
            until("the respawn's save", lambda: tcp("files_state")["saves"] > saves, 1800)
            saved = read_file(out / "game" / FILE)
            assert saved.get("quests.given") == "20000" and saved.get("quests.done") == "00000", saved
            print(f"ok: dying wrote the quests to {FILE}: quests.given = {saved['quests.given']}")
            game.close()

            game.launch()
            until("the file select", lambda: tcp("files_state")["shown"], 2000)
            tcp("pad", buttons="start", frames=10)                  # file 1: continue
            until("Eolis's temple", lambda: place() == (BUILDINGS, EOLIS_TEMPLE) and settled(), 1800)
            wait(10)
            s = open_log()
            assert s["given"] == ["king"] and rows(s)[0] == "The King's Plea", s
            game.shot("continued-log")
            close_log()
            print("ok: continuing file 1 puts The King's Plea back in the log")
            game.close()

        if args.wants("original") and not seamless:
            game.set_features({PACKAGE: False})
            shop = game.state("eolis-meat-shop")
            game.launch(at="eolis-meat-shop")
            warp(shop, "kings-room-original", EOLIS, 7, 0x98, {SPECIAL_ITEMS: RING_OF_ELF})
            s = state()
            assert not s["enabled"] and s["marks"] == [], s
            for _ in range(3):
                tcp("key", name="L", frames=2)
                wait(30)
                assert ram(PAUSED) == 0 and not state()["open"], state()
            print("ok: with the remaster off, no marks, and L does nothing")
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
