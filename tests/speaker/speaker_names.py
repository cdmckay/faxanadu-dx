"""Play speaker names end to end in a hidden window and screenshot the tabs.

    python3 tests/speaker/speaker_names.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/speaker

The game runs uncapped (about 5x real time; --realtime to watch it at normal
speed) and the script waits on the game's own state, never on the clock, so
each run takes a free TCP port, and any number can go in parallel.

Copies the build to OUT (so the faxanadu_gurus.txt sidecar stays there), starts
a new game over TCP, walks through Eolis's gate into the meat shop and saves a
state there. It relaunches with the Gurus of Eolis and Apolune visited (as
tests/travel/guru_travel.py does), then talks to the Meat Seller. For each other
Eolis building it edits where the shop's door leads (save-state RAM, an
external prerequisite, as tests/zones/zone_names.py does), walks out onto that
building's door and goes in: the Guru (twice: the Ring of Elf, then Guru
travel's offer), the King, the King's guard outside (no portrait, no name), the
Martial Artist, and the Tool Seller through his shop's list. Last, the shop's
door leads to the Castle of Fraternal's temple, for the longest name, "Guru of
Fraternal". Each conversation is screenshot in OUT, and the tab is checked in
the picture itself: its corner is the game's own frame tile, and it holds a
line of text.
"""
from pathlib import Path
import struct
import sys
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
from fax_e2e import Game, act, fixture, frame, parse_args, ram, tcp, typed, until, wait  # noqa: E402

AREA, SCREEN, PLAYER_X = 0x24, 0x63, 0x9E
TEXTBOX_CLOSED, MESSAGE_ID, TEXTBOX_PAUSED = 0x20C, 0x213, 0x215
GOLD, SPECIAL_ITEMS, HP = 0x392, 0x42C, 0x431
AREA_SAVED_AREA, AREA_SAVED_SCREEN, SCREEN_SAVED_PALETTE, PLAYER_SAVED_POS = 0x3D5, 0x3D6, 0x3D7, 0x3D8
AREA_REGION, TEMPLE_SPAWN_POINT = 0x435, 0x439
DARTMOOR, PALETTE_FRATERNAL, REGION_DARTMOOR = 6, 0x0D, 4          # as tests/zones/zone_names.py warps there
RING_OF_ELF = 0x80
ZONE_NAMES = "faxanadu.zone-names"                                 # its save-state record
BUILDINGS = 4
NO_PORTRAIT = 0xFF
OFFER = 1                                                           # fax_travel.c's "Shall I send..."
PORTRAIT_BOX, TAB_X = (16, 112), 80                                 # tiles 2,14 and column 10, in pixels
PORTRAIT_BOX_RIGHT = (72, 112)                                      # its top-right corner, tile 9,14


def speaker():
    return tcp("speaker_state")


def box_open():
    return ram(TEXTBOX_CLOSED) == 0


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


def check_tab(path, row, length, inside):
    """The tab in the presented picture: its top corners are the game's own
    frame tiles (the portrait box's, pixel for pixel), `inside` cells apart,
    and its line holds the name: `length` characters of the text box's text
    color on black."""
    width, height, px = png(path)
    ox = (width - 256) // 2 if width >= 256 else 0                 # where Seamless puts the native picture
    block = lambda x, y: [px[y + r][ox + x + c] for r in range(8) for c in range(8)]
    assert block(TAB_X, row * 8) == block(*PORTRAIT_BOX), f"{path.name}: no frame corner at the tab"
    assert block(TAB_X, row * 8 - 8) != block(*PORTRAIT_BOX), f"{path.name}: the tab is not where expected"
    right = TAB_X + (inside + 1) * 8
    assert block(right, row * 8) == block(*PORTRAIT_BOX_RIGHT), f"{path.name}: the tab's right corner is not at {right}"
    line = [px[row * 8 + 8 + r][ox + TAB_X + 8 + c] for r in range(8) for c in range(8 * inside)]
    lit = [p for p in line if p != (0, 0, 0)]
    assert len(set(lit)) == 1 and len(lit) >= 8 * length, f"{path.name}: no line of text in the tab"


def main():
    args = parse_args(__doc__)
    game = Game.from_args(args)
    out = game.out

    def walk_to(x_min, x_max):
        """Walk until the hero's X is in [x_min, x_max], paging through any text."""
        for _ in range(600):
            if box_open():
                if typed():
                    act("a", 6, 14)
                else:
                    wait(4)
                continue
            x = ram(PLAYER_X)
            if x_min <= x <= x_max:
                return
            act("right" if x < x_min else "left", 2, 0)
        raise AssertionError(f"could not reach x {x_min}-{x_max}")

    def walk_out():
        """Walk right out of a building, back to where its door leads."""
        for _ in range(300):
            if ram(AREA) != BUILDINGS:
                return
            act("right", 2, 0)
        raise AssertionError("could not walk out")

    def settled_in(area, screen):
        until(f"area {area} screen {screen:#x}", lambda: (ram(AREA), ram(SCREEN)) == (area, screen))
        wait(30)

    def go_in(source, name, screen, door, writes=None, area=0):
        """From inside a building, walk out onto the door `door` (block YX) on
        `screen` of `area` (Eolis unless the writes also give its palette and
        region), and go in."""
        target = out / f"{name}.cycstate"
        fixture(source, target, {AREA_SAVED_AREA: area, AREA_SAVED_SCREEN: screen, PLAYER_SAVED_POS: door,
                                 HP: 0x50, **(writes or {})}, records={ZONE_NAMES: {5: area, 6: screen}})
        tcp("load_state", path=str(target))
        walk_out()
        settled_in(area, screen)
        column = (door & 15) * 16
        walk_to(column - 6, column + 4)
        act("up", 4, 10)
        until("the building", lambda: ram(AREA) == BUILDINGS)
        wait(40)

    def talk(climb=False, stop=0):
        """Walk left to the person behind the counter, or to x `stop`
        (jumping up steps if `climb`), and press Up."""
        for _ in range(60):
            if ram(PLAYER_X) <= stop or speaker()["portrait"] != NO_PORTRAIT:
                break
            if climb:
                tcp("action", name="left", frames=24)
                tcp("action", name="a", frames=12)
                wait(30)
            else:
                before = ram(PLAYER_X)
                act("left", 8, 2)
                if ram(PLAYER_X) == before:
                    break                                   # at the counter
        act("up", 4, 10)
        until("a portrait", lambda: speaker()["portrait"] != NO_PORTRAIT, 600)
        # Until the first line types, MessageID and TextBox_MessagePaused hold
        # whatever was there before (after a door to another area, $F0 $F0 $F0),
        # which can read as typed out.
        until("the first line to start typing", lambda: not typed(), 600)

    def tab(name, row=13, label=None):
        """Wait for the tab `name` over a typed-out page, check it on screen, screenshot it."""
        until(f"the {name} tab", lambda: speaker()["shown"], 600)
        until("the line to type out", typed)
        state = speaker()
        assert state["name"] == name and state["shown"] and state["box"], state
        assert (state["col"], state["row"]) == (10, row), state
        assert state["cols"] == len(name) + (4 if len(name) <= 16 else 2), state
        path = game.shot(label or name.lower().replace(" ", "-"))
        check_tab(path, row, len(name), state["cols"] - 2)
        return path

    def finish(button="a"):
        """Page through to the end of the conversation; the tab goes with the portrait."""
        end = frame() + 6000
        while frame() < end:
            state = speaker()
            if ram(TEXTBOX_CLOSED) and state["portrait"] == NO_PORTRAIT:
                break
            if state["portrait"] != NO_PORTRAIT and not state["shown"]:
                assert not state["box"], state                  # only while the boxes open or clear
            if box_open() and typed():
                act(button, 6, 14)
            else:
                wait(6)
        else:
            raise AssertionError("the conversation did not end")
        state = speaker()
        assert not state["shown"] and state["name"] is None, state

    try:
        # A new game: through the gate and into Eolis's meat shop.
        game.launch()
        until("the title", lambda: frame() > 700)
        tcp("pad", buttons="start", frames=10)
        until("the Eolis gate", lambda: ram(AREA) == 0 and frame() > 1900)
        assert not speaker()["shown"] and speaker()["name"] is None, speaker()
        walk_to(186, 196)                                   # the gate, column 12
        act("up", 4, 10)
        settled_in(0, 1)
        walk_to(41, 56)                                     # the meat shop, column 3
        act("up", 4, 10)
        until("the meat shop", lambda: ram(AREA) == BUILDINGS)
        wait(30)
        shop = out / "eolis-meat-shop.cycstate"
        tcp("save_state", path=str(shop))
        game.close()

        # Relaunch with Apolune's Guru met, so Eolis's offers Guru travel.
        (out / "game" / "faxanadu_gurus.txt").write_text("Eolis\nApolune\n")
        game.launch()
        tcp("load_state", path=str(shop))
        wait(10)
        assert speaker()["enabled"] and not speaker()["shown"], speaker()

        # 1. The Meat Seller's question; B declines it.
        talk()
        tab("Meat Seller")
        finish("b")
        print("ok: the Meat Seller is named through his question, and the tab goes with him")

        # 2. Eolis's Guru: the Ring of Elf, then Guru travel's offer.
        go_in(shop, "eolis-temple", 2, 0x95, {TEMPLE_SPAWN_POINT: 1})
        talk()
        tab("Guru of Eolis")
        finish()
        assert ram(SPECIAL_ITEMS) & RING_OF_ELF, "the Guru gives the Ring of Elf"
        # Player_CheckHandlePressUpOnNPC talks only on the one frame Up goes
        # down, so a press that lands as the conversation winds down goes
        # unseen. Press again until he answers.
        for _ in range(5):
            act("up", 4, 10)
            try:
                until("the Guru again", lambda: speaker()["portrait"] != NO_PORTRAIT, 120)
                break
            except AssertionError:
                pass
        else:
            raise AssertionError("timed out waiting for the Guru again")
        for _ in range(20):
            if tcp("travel_state")["text"] == OFFER:
                break
            until("a page to type out", typed)
            if tcp("travel_state")["text"] == OFFER:
                break
            act("a", 6, 14)
        assert tcp("travel_state")["text"] == OFFER, tcp("travel_state")
        wait(20)                                            # the box is refilled with the offer
        tab("Guru of Eolis", label="guru-travel-offer")
        finish("b")
        print("ok: the Guru of Eolis is named, through Guru travel's offer")

        # 3. The King, at the top of his stairs, with the ring.
        go_in(shop, "kings-room", 7, 0x98, {SPECIAL_ITEMS: RING_OF_ELF})
        talk(climb=True, stop=48)
        tab("King of Eolis")
        finish()
        assert ram(GOLD, 3) == bytes([0xDC, 0x05, 0x00]), "the King gives 1500 Golds"
        print("ok: the King of Eolis is named, through all his pages")

        # 4. His guard outside, who has no portrait: no name.
        walk_out()
        settled_in(0, 7)
        for _ in range(40):
            if box_open():
                break
            x = ram(PLAYER_X)
            if x >= 146:                                    # past the King's door (column 8)
                act("up", 4, 10)
            if not box_open():
                act("right", 3, 2)
        until("the guard's line", lambda: box_open() and typed())
        state = speaker()
        assert state["portrait"] == NO_PORTRAIT and state["name"] is None and not state["shown"], state
        game.shot("kings-guard")
        finish()
        print("ok: the King's guard, with no portrait, shows no name")

        # 5. The Martial Artist's lesson; B declines it.
        go_in(shop, "martial-arts", 6, 0x9A, {SPECIAL_ITEMS: RING_OF_ELF})
        talk(stop=124)                                      # he stands mid-room, at x 112
        tab("Martial Artist")
        finish("b")
        print("ok: the Martial Artist is named")

        # 6. The Tool Seller: his greeting, the Buy/Sell menu, then his list.
        go_in(shop, "tool-shop", 3, 0x9A)
        talk()
        tab("Tool Seller")
        for _ in range(6):                                  # "I sell tools.", Buy, "What would you like?"
            if speaker()["row"] == 5:
                break
            until("a page or the menu", typed)
            assert speaker()["shown"], speaker()
            act("a", 6, 20)
        until("the shop's list", lambda: speaker()["row"] == 5, 600)
        wait(20)
        tab("Tool Seller", row=5, label="tool-seller-list")
        act("b", 6, 14)                                     # leave the list: the end
        finish("b")
        print("ok: the Tool Seller is named, on his shop's list as well")

        # 7. The longest name: the Guru in the Castle of Fraternal (building
        # $42, DARTMOOR_DOOR_LOCATIONS: screen $1E, block $A7). At 17
        # characters it fills its tab without the blank cells.
        go_in(shop, "fraternal-temple", 0x1E, 0xA7, {SCREEN_SAVED_PALETTE: PALETTE_FRATERNAL,
                                                     AREA_REGION: REGION_DARTMOOR}, area=DARTMOOR)
        assert ram(SCREEN) == 0x42, ram(SCREEN)
        talk()
        tab("Guru of Fraternal")
        finish()
        print("ok: the Guru of Fraternal, the longest name, fits his tab")
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
