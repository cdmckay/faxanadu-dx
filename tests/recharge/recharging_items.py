"""Play recharging items end to end in a hidden window.

    python3 tests/recharge/recharging_items.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/recharge

The game runs uncapped and the script waits on the game's own state, never on
the clock, and takes a free TCP port, so any number can run in parallel.

Copies the build to OUT and starts a new game over TCP. The hero walks through
the Eolis gate into the town and into a shop. Save-state RAM edits then set up
what each scenario needs (external prerequisites, not coverage): the item
selected, HP, gold, the title, where the shop's door leads (Area_Saved*, so
walking out puts the hero on the screen wanted), a screen's enemies cleared,
the random-item visit counter, and for the duplicate finds and Guru travel
the feature's own record (a cooldown running, the Elixir spent). Items are
used with Down+B, as a player would.

1. Wing Boots. One use at each title band gives 30, 40, 50 and 60 s of flight
   (the original: 40, 30, 20, 10). Then, as a Novice in Eolis: 30 s of flight,
   T: counting it down. When it runs out, T: blinks a 60 s cooldown; Down+B
   then does nothing. Once it is over, the same pair flies again. Dying makes
   them ready.
2. The Hour Glass on Eolis screen 4: the Zozuras stop, HP halves, 15 s. Then a
   60 s cooldown; refused meanwhile; then it works again.
3. The Hour Glass at Ripasheiku (Trunk $15): the boss keeps moving. Walking
   onto a boss's screen with the Hour Glass running ends it within a second
   (LimitItemDurations), and its cooldown follows.
4. The Elixir on Eolis screen 4 with 1 HP: a Zozura's hit, and it revives the
   hero ("I've used Elixir."). With 1 HP again, the next hit kills him; he
   respawns with it still spent, and the menu's Player page, which showed it,
   does not. Talking to Eolis's Guru charges it, and the page shows it again.
5. Eolis's tool shop with a spent Elixir: buying one charges it, for its 320
   gold. With it charged, the shop refuses a second ("You can't carry any
   more.") and keeps the gold.
6. Finds: a pair of Wing Boots (Trunk $2D) while they recharge, an Hour Glass
   (Trunk $13) while it recharges, and an Elixir (Trunk $3C) while it is spent:
   each is ready at once, and none is a second copy.
7. The Spring of Sky, the spring the Wing Boots are needed for: from the floor
   of Trunk $20 below the shaft, fly up through $23 and $24 to the Spring of
   Sky's man in $25 and wake him, inside the lowest band's 30 s.
8. Guru travel (relaunched having met the Gurus of Eolis and Apolune): a
   journey from Eolis to Apolune with the Wing Boots flying and the Hour Glass
   recharging keeps both counting, and the flight's cooldown follows it.
9. Turned off: the pair is used up after one 40 s (Novice) flight.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import sys
import time
import zlib

PORT = 0                                                            # set in main
AREA, SCREEN, PLAYER_X, PLAYER_Y, PLAYER_FLAGS, PLAYER_STATUS = 0x24, 0x63, 0x9E, 0xA1, 0xA4, 0xA5
SCREEN_READY = 0x0E
TEXTBOX_CLOSED, MESSAGE_ID = 0x20C, 0x213
HP, HP_FRACTION, PLAYER_IS_DEAD, TEMPLE_SPAWN_POINT, PLAYER_TITLE = 0x431, 0x432, 0x438, 0x439, 0x437
ITEM_INVENTORY, SELECTED_ITEM, NUMBER_OF_ITEMS, GOLD = 0x3AD, 0x3C1, 0x3C6, 0x392
DURATION_WING_BOOTS, DURATION_HOUR_GLASS, SPECIAL_ITEMS, QUESTS = 0x429, 0x42A, 0x42C, 0x42D
SPRITE_ENTITIES, SPRITE_X, SPRITE_Y = 0x2CC, 0xBA, 0xC2                # CurrentSprites_*, 8 slots each
SAVED_AREA, SAVED_SCREEN, SAVED_PALETTE, SAVED_POS, REGION = 0x3D5, 0x3D6, 0x3D7, 0x3D8, 0x435
VISIT_COUNT = 0x43A                                                 # Screen_TrackedVisitCount
HOUR_GLASS, WING_BOOTS, NO_ITEM, BIT_ELIXIR, RING_OF_ELF = 0x0D, 0x0F, 0xFF, 0x08, 0x80
USED_HOUR_GLASS, USED_WING_BOOTS, USED_ELIXIR = 0xAC, 0xAD, 0xAF      # message IDs
GOT_WING_BOOTS, GOT_HOUR_GLASS, GOT_ELIXIR = 0xB3, 0xB4, 0xB0        # "I'm holding ..."
WING_BOOTS_GONE, HOUR_GLASS_GONE, NEGATIVE_THOUGHTS, HO_HO_HO = 0xC0, 0xC1, 0x24, 0x57
SHOP_HELLO, WHAT_WOULD_YOU_LIKE, CANT_CARRY_MORE = 0x0F, 0x14, 0x06
EOLIS, TRUNK, BUILDINGS = 0, 1, 4
EOLIS_TEMPLE, APOLUNE_TEMPLE, TOOL_SHOP = 0x02, 0x0B, 0x04          # building numbers
PALETTE_EOLIS, PALETTE_OUTSIDE, PALETTE_TOWER = 0x00, 0x06, 0x07
ZOZURA, RIPASHEIKU = 0x47, 0x2D
QUEST_SPRING_OF_SKY = 0x02
TRAVEL_DEPART, TRAVEL_TO_GURU = 3, 4                                # fax_travel.c's lines (TO_GURU + temple)
FLIGHTS, COOLDOWN, FREEZE = (30, 40, 50, 60), 60, 15              # the game's seconds (64 frames)
FLIGHT = FLIGHTS[0]                                                 # a Novice's: the lowest band
ELIXIR_PRICE, ELIXIR_ROW = 320, 2                                   # EOLIS_TOOL_SHOP_ITEMS: dagger, potion, Elixir
SECOND = 64
PACKAGE = ("faxanadu.enhancement.recharging-items", "recharging-items")
RECORD = b"faxanadu.recharge"                                       # its save-state record: version, boots
#                                                                     running and cooldown, glass running and
#                                                                     cooldown, the Elixir spent


def tcp(cmd, **kw):
    with socket.create_connection(("127.0.0.1", PORT), 5) as s:
        s.settimeout(30)
        f = s.makefile("rwb", buffering=0)
        f.write((json.dumps(dict(id=1, cmd=cmd, **kw)) + "\n").encode())
        reply = json.loads(f.readline())
    if not reply.get("ok"):
        raise RuntimeError(f"{cmd}: {reply}")
    return reply


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def frame():
    return tcp("ping")["frame"]


def wait(frames):
    end = frame() + frames
    while frame() < end:
        time.sleep(0.005)


def ram(addr, n=1):
    data = bytes.fromhex(tcp("read_ram", addr=addr, len=n)["hex"])
    return data[0] if n == 1 else data


def until(what, test, frames=1200):
    end = frame() + frames
    while frame() < end:
        if test():
            return
        time.sleep(0.005)
    raise AssertionError(f"timed out waiting for {what}")


def act(name, frames=6, after=8):
    tcp("action", name=name, frames=frames)
    wait(frames + after)


def state():
    return tcp("recharge_state")


def place():
    return ram(AREA), ram(SCREEN)


def gold():
    return int.from_bytes(ram(GOLD, 3), "little")


def settled():
    return ram(SCREEN_READY) == 0


def typed():
    """The text box waits for a button: the message has typed out (MessageID
    clears) or reached a pause (TextBox_MessagePaused)."""
    message, _, paused = ram(MESSAGE_ID, 3)
    return message == 0 or paused != 0


def box_open():
    return ram(TEXTBOX_CLOSED) == 0


def items():
    """The unselected items, in order, and the selected one."""
    return list(ram(ITEM_INVENTORY, 8)[:ram(NUMBER_OF_ITEMS)]), ram(SELECTED_ITEM)


def chunks(data):
    assert data[:8] == b"CYCSTATE"
    pos = 32
    while pos < len(data):
        tag, size = struct.unpack_from("<4sI", data, pos)
        pos += 8
        yield tag, pos, size
        pos += size
        if tag == b"END ":
            return


def fixture(source, target, writes, record=None):
    """Copy a save state, changing RAM and, if given, bytes of this feature's record."""
    data = bytearray(source.read_bytes())
    pos, size = next((p, n) for t, p, n in chunks(data) if t == b"HW  ")
    assert size == 2072, "Unqualified hardware ABI"
    for address, value in writes.items():
        data[pos + 23 + address] = value
    found = False
    for tag, p, n in chunks(data):
        if tag == b"MOD " and data[p:p + 64].rstrip(b"\0") == RECORD:
            found = True
            for offset, value in (record or {}).items():
                data[p + 64 + offset] = value
    assert found or record is None, "the state has no recharging-items record"
    target.write_bytes(data)


def main():
    global PORT
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for key in ("build", "rom", "out"):
        ap.add_argument("--" + key, type=Path, required=True)
    ap.add_argument("--game-arg", action="append", default=[],
                    help="extra game option, e.g. --game-arg=--world-view=seamless (repeatable)")
    ap.add_argument("--port", type=int, default=0, help="TCP port (default: a free one)")
    ap.add_argument("--realtime", action="store_true", help="run at normal speed instead of uncapped")
    args = ap.parse_args()
    PORT = args.port or free_port()
    image = args.rom.read_bytes()
    assert zlib.crc32(image[16:]) == 0x57DD23D1, "the original Faxanadu (USA) ROM is required"
    out = args.out.resolve()
    shutil.rmtree(out, ignore_errors=True)
    (out / "game").mkdir(parents=True)
    shutil.copy2(args.build / "FaxanaduRecomp", out / "game")
    for folder in ("assets", "mods"):
        shutil.copytree(args.build / folder, out / "game" / folder)
    env = dict(os.environ, NESRECOMP_NO_LAUNCHER="1", SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    log = open(out / "game.log", "w")
    game = None
    shots = []

    def launch(on=True, gurus=None):
        nonlocal game
        package, feature = PACKAGE
        (out / "game" / "mods" / "state.toml").write_text(
            f'format_version = 1\n\n[[feature]]\npackage_id = "{package}"\nid = "{feature}"\n'
            f"enabled = {'true' if on else 'false'}\n")
        if gurus:
            (out / "game" / "faxanadu_gurus.txt").write_text("".join(g + "\n" for g in gurus))
        extra = [part for arg in args.game_arg for part in arg.split("=", 1)]
        if not args.realtime:
            extra += ["--uncapped", "--exit-after", "100000000"]
        game = subprocess.Popen([str(out / "game" / "FaxanaduRecomp"), str(args.rom.resolve()), "--hidden",
                                 "--tcp", str(PORT), *extra], env=env, stdout=log, stderr=subprocess.STDOUT)
        for _ in range(100):
            try:
                frame()
                break
            except OSError:
                time.sleep(0.1)
        else:
            raise AssertionError("the game did not start")
        assert state()["enabled"] == on, state()

    def close():
        try:
            tcp("quit")
        except Exception:
            game.kill()
        game.wait(10)

    def shot(name):
        path = out / f"{len(shots):02d}-{name}.png"
        tcp("screenshot", layer="picture", path=str(path))
        shots.append(path.name)

    def poke(writes):
        """Edit RAM in the running game: save, edit the state, load it back."""
        path = out / "poke.cycstate"
        tcp("save_state", path=str(path))
        fixture(path, path, writes)
        tcp("load_state", path=str(path))

    def clear(entity=None):
        """Take every sprite of `entity` (or every sprite) off the screen."""
        slots = ram(SPRITE_ENTITIES, 8)
        poke({SPRITE_ENTITIES + i: 0xFF for i in range(8) if entity is None or slots[i] == entity})

    def page_through(frames=900):
        """Press A on each typed-out page until the box closes."""
        for _ in range(20):
            if not box_open():
                return
            until("the page to type out", typed, frames)
            act("a", 6, 14)
        raise AssertionError("the box did not close")

    def dismiss(expect, frames=900):
        """Wait for message `expect`, and page through it with A until the box closes."""
        seen = []

        def shown():
            seen.append(ram(MESSAGE_ID))
            return seen[-1] == expect
        until(f"message ${expect:02X} (saw {sorted(set(seen))})", shown, frames)
        page_through()

    def use():
        """Down+B: the selected item."""
        tcp("action", name="down", frames=24)
        wait(6)
        tcp("action", name="b", frames=4)
        wait(24)

    def walk_out():
        """Walk right out of the shop, to where its door was edited to lead."""
        for _ in range(200):
            if ram(AREA) != BUILDINGS:
                break
            act("right", 2, 0)
        until("the screen to settle", lambda: ram(AREA) != BUILDINGS and settled(), 600)
        wait(30)

    def warp(source, name, area, screen, pos, palette, region, writes=None, record=None):
        """From the shop, walk out onto `screen` of `area` at block YX `pos`."""
        target = out / f"{name}.cycstate"
        fixture(source, target, {SAVED_AREA: area, SAVED_SCREEN: screen, SAVED_PALETTE: palette,
                                 SAVED_POS: pos, REGION: region, HP: 0x50, **(writes or {})}, record)
        tcp("load_state", path=str(target))
        walk_out()
        assert place() == (area, screen), f"warp to {area}/${screen:02X} reached {place()}"
        poke({PLAYER_X: (pos & 0x0F) * 16, PLAYER_Y: (pos >> 4) * 16})   # a step taken while the screen loaded

    def enter_door(building):
        """Up at the door the hero stands at, into `building`."""
        act("up", 4, 10)
        until(f"building ${building:02X}", lambda: place() == (BUILDINGS, building) and settled(), 600)
        wait(30)

    def await_ready(which, frames):
        until(f"the {which} to be ready", lambda: state()[f"{which}_ready"], frames)

    def hud_shot(name, value_range):
        """Screenshot T: in the blink's on phase, showing a cooldown in `value_range`."""
        until("T: showing a cooldown", lambda: (lambda s: s["hud_drawn"] in value_range and
                                                not s["hud_drawn_blank"])(state()), 600)
        wait(3)
        shot(name)

    def flying_shot(name):
        """Hold A+Up for a moment: the hero rises."""
        y = ram(PLAYER_Y)
        tcp("action", name="a", frames=40)
        tcp("action", name="up", frames=40)
        wait(36)
        assert ram(PLAYER_Y) < y, f"flying: y {ram(PLAYER_Y)} not above {y}"
        shot(name)
        wait(10)

    def status_shot(name):
        """The Select menu's PLAYER page, whose last row shows the special items."""
        for _ in range(3):                                          # not while knocked back
            act("select", 4, 30)
            if box_open():
                break
            wait(60)
        for _ in range(5):
            act("down", 4, 8)
        act("a", 4, 40)
        assert box_open(), "the status page"
        shot(name)
        act("b", 4, 30)
        until("the menu to close", lambda: not box_open(), 120)

    def buy(row):
        """Talk to the shopkeeper, choose Buy and the item on `row`. The
        messages shown after choosing it."""
        act("up", 4, 10)
        until("the greeting", lambda: ram(MESSAGE_ID) == SHOP_HELLO, 300)
        until("the greeting to type out", typed, 900)
        act("a", 4, 40)                                             # to Buy / Sell
        act("a", 4, 10)                                             # Buy
        until("What would you like?", lambda: ram(MESSAGE_ID) == WHAT_WOULD_YOU_LIKE, 300)
        until("the question to type out", typed, 900)
        act("a", 4, 40)                                             # the list
        for _ in range(row):
            act("down", 4, 10)
        act("a", 4, 4)
        seen = []
        end = frame() + 30 * SECOND                                 # A to each line until the box closes
        while frame() < end and box_open():
            m = ram(MESSAGE_ID)
            if m and m not in seen:
                seen.append(m)
            if typed():
                act("a", 6, 20)
            else:
                wait(4)
        assert not box_open(), f"the shop did not close (saw {seen})"
        return seen

    def talk_to_guru():
        """Walk left to the Guru (who stands on the left) and talk."""
        act("left", 170, 10)
        act("up", 4, 10)
        until("the Guru to speak", box_open, 300)

    try:
        launch()
        until("the title", lambda: frame() > 700)
        tcp("pad", buttons="start", frames=10)
        until("the Eolis gate", lambda: place() == (EOLIS, 0) and frame() > 1900)
        for _ in range(200):                                        # the intro's lines stop him; A goes on
            if ram(PLAYER_X) >= 184:
                break
            x = ram(PLAYER_X)
            tcp("action", name="right", frames=8)
            wait(8)
            if ram(PLAYER_X) == x:
                act("a", 6, 20)
        act("up", 4, 10)
        until("Eolis town", lambda: place() == (EOLIS, 1), 600)
        wait(60)
        town = out / "town.cycstate"
        tcp("save_state", path=str(town))
        for _ in range(200):                                        # the meat shop's door, column 3
            if 41 <= ram(PLAYER_X) <= 56:
                break
            act("left", 2, 0)
        act("up", 4, 10)
        until("the shop", lambda: ram(AREA) == BUILDINGS)
        wait(30)
        shop = out / "shop.cycstate"
        tcp("save_state", path=str(shop))

        # 1. Wing Boots: the flight for each title band, then a whole cycle as a Novice.
        boots = out / "boots.cycstate"
        no_one = {SPRITE_ENTITIES + i: 0xFF for i in range(8)}      # Up next to the town's walker would talk
        for band, flight in enumerate(FLIGHTS):
            fixture(town, boots, {HP: 0x50, SELECTED_ITEM: WING_BOOTS, NUMBER_OF_ITEMS: 0, PLAYER_TITLE: band * 4 + 3,
                                  **no_one})
            tcp("load_state", path=str(boots))
            wait(10)
            use()
            dismiss(USED_WING_BOOTS)
            assert ram(DURATION_WING_BOOTS) in (flight, flight - 1), f"title {band * 4 + 3}: {ram(DURATION_WING_BOOTS)} s"
        print(f"ok: the Wing Boots fly {', '.join(map(str, FLIGHTS))} s by title band (the original: 40, 30, 20, 10)")
        warp(shop, "boots-novice", EOLIS, 1, 0x99, PALETTE_EOLIS, 0,  # walked out, so the HUD shows the pair
             {SELECTED_ITEM: WING_BOOTS, NUMBER_OF_ITEMS: 0, PLAYER_TITLE: 0})
        clear()
        use()
        dismiss(USED_WING_BOOTS)
        assert ram(DURATION_WING_BOOTS) in (FLIGHT, FLIGHT - 1), f"flight {ram(DURATION_WING_BOOTS)}, not {FLIGHT} s"
        assert items() == ([], WING_BOOTS), items()
        s = state()
        assert s["boots_running"] == 1 and s["hud"] == -1 and s["kept"] >= 1, s
        flying_shot("flying")
        print(f"ok: a Novice's Wing Boots fly {FLIGHT} s and the pair stays selected")
        start = frame()
        dismiss(WING_BOOTS_GONE, (FLIGHT + 2) * SECOND)
        flown = frame() - start
        until("the cooldown", lambda: state()["boots_cooldown"] > 0, 60)
        cooling_since = frame()
        s = state()
        assert s["boots_cooldown"] == COOLDOWN and not s["boots_ready"] and s["hud"] == COOLDOWN, s
        hud_shot("boots-recharging", range(COOLDOWN - 2, COOLDOWN + 1))
        blinks = set()
        for _ in range(40):
            blinks.add(state()["hud_drawn_blank"])
            wait(4)
        assert blinks == {True, False}, f"T: should blink while recharging: {blinks}"
        use()
        wait(30)
        s = state()
        assert s["refused"] == 1 and ram(DURATION_WING_BOOTS) == 0xFF and not box_open(), s
        assert items() == ([], WING_BOOTS), items()
        print(f"ok: the flight ran out after {flown} frames; T: blinks the {COOLDOWN} s cooldown, and Down+B "
              "meanwhile does nothing but the invalid-choice sound")
        await_ready("boots", (COOLDOWN + 2) * SECOND)
        waited = frame() - cooling_since
        assert (COOLDOWN - 1) * SECOND <= waited <= (COOLDOWN + 1) * SECOND, f"the cooldown took {waited} frames"
        wait(4)
        assert state()["hud_drawn"] == 0, state()                  # T: back to the idle 00
        use()
        dismiss(USED_WING_BOOTS)
        assert ram(DURATION_WING_BOOTS) in (FLIGHT, FLIGHT - 1) and items() == ([], WING_BOOTS), items()
        flying_shot("flying-again")
        print(f"ok: after {waited} frames of cooldown the same pair flies again")
        dismiss(WING_BOOTS_GONE, (FLIGHT + 2) * SECOND)
        until("the cooldown", lambda: state()["boots_cooldown"] > 0, 60)
        poke({PLAYER_IS_DEAD: 1})
        dismiss(NEGATIVE_THOUGHTS, 600)
        until("the respawn", lambda: place() == (BUILDINGS, EOLIS_TEMPLE), 1200)
        wait(30)
        s = state()
        assert s["boots_ready"] and s["boots_cooldown"] == 0 and items() == ([], WING_BOOTS), s
        print("ok: dying makes the recharging Wing Boots ready")

        # 2. The Hour Glass on Eolis screen 4.
        warp(shop, "glass", EOLIS, 4, 0x9E, PALETTE_EOLIS, 0, {SELECTED_ITEM: HOUR_GLASS, NUMBER_OF_ITEMS: 0})
        zozuras = [i for i, e in enumerate(ram(SPRITE_ENTITIES, 8)) if e == ZOZURA]
        assert zozuras, "Zozuras on screen 4"
        use()
        dismiss(USED_HOUR_GLASS)
        assert ram(DURATION_HOUR_GLASS) in (FREEZE, FREEZE - 1) and ram(HP) == 0x28, (ram(DURATION_HOUR_GLASS), ram(HP))
        before = [ram(SPRITE_X + i) for i in zozuras]
        wait(90)
        assert [ram(SPRITE_X + i) for i in zozuras] == before, "the Zozuras froze"
        shot("hour-glass")
        print("ok: the Hour Glass freezes the Zozuras for 15 s and halves HP (80 to 40)")
        dismiss(HOUR_GLASS_GONE, (FREEZE + 2) * SECOND)
        clear(ZOZURA)
        until("the cooldown", lambda: state()["glass_cooldown"] > 0, 60)
        s = state()
        assert s["glass_cooldown"] == COOLDOWN and s["hud"] == COOLDOWN and items() == ([], HOUR_GLASS), s
        refused = state()["refused"]
        use()
        wait(30)
        assert state()["refused"] == refused + 1 and ram(DURATION_HOUR_GLASS) == 0xFF and not box_open(), state()
        await_ready("glass", (COOLDOWN + 2) * SECOND)
        use()
        dismiss(USED_HOUR_GLASS)
        assert ram(DURATION_HOUR_GLASS) in (FREEZE, FREEZE - 1) and ram(HP) == 0x14 and items() == ([], HOUR_GLASS)
        print("ok: the Hour Glass recharges for 60 s, refusing meanwhile, then freezes again (HP 40 to 20)")

        # 3. Ripasheiku, at the top of the Tower of Trunk.
        warp(shop, "boss", TRUNK, 0x15, 0x95, PALETTE_TOWER, 1, {SELECTED_ITEM: HOUR_GLASS, NUMBER_OF_ITEMS: 0})
        boss = ram(SPRITE_ENTITIES, 8).index(RIPASHEIKU)
        use()
        dismiss(USED_HOUR_GLASS)
        assert ram(DURATION_HOUR_GLASS) in (FREEZE, FREEZE - 1)
        seen = set()                                                # he stands up to ~4 s between moves
        end = frame() + 10 * SECOND
        while frame() < end and len(seen) < 4:
            seen.add((ram(SPRITE_X + boss), ram(SPRITE_Y + boss)))
            wait(3)
        assert len(seen) > 1 and ram(DURATION_HOUR_GLASS) < 0x80, f"Ripasheiku stood still under the Hour Glass: {seen}"
        shot("boss-not-frozen")
        print(f"ok: bosses do not freeze: Ripasheiku moved through {len(seen)} positions under the Hour Glass")
        target = out / "boss-entry.cycstate"                        # the Hour Glass running in the shop
        fixture(shop, target, {SAVED_AREA: TRUNK, SAVED_SCREEN: 0x15, SAVED_PALETTE: PALETTE_TOWER, SAVED_POS: 0x95,
                               REGION: 1, HP: 0x50, SELECTED_ITEM: HOUR_GLASS, NUMBER_OF_ITEMS: 0,
                               DURATION_HOUR_GLASS: 12}, {3: 1})
        tcp("load_state", path=str(target))
        for _ in range(200):
            if ram(AREA) != BUILDINGS:
                break
            act("right", 2, 0)
        entered, left = frame(), ram(DURATION_HOUR_GLASS)            # at the door, before the screen's first frame
        until("the Hour Glass to end", lambda: ram(DURATION_HOUR_GLASS) == 0xFF, 3 * SECOND)
        ended = frame() - entered
        assert 5 <= left <= 12 and ended <= 2 * SECOND, (left, ended)
        dismiss(HOUR_GLASS_GONE, 300)
        until("the cooldown", lambda: state()["glass_cooldown"] > 0, 60)
        assert state()["glass_cooldown"] == COOLDOWN, state()
        print(f"ok: walking onto the boss's screen with {left} s of the Hour Glass left ends it within "
              f"{ended} frames (LimitItemDurations), then its cooldown starts")

        # 4. The Elixir: a revive, a death with it spent, a Guru.
        warp(shop, "elixir", EOLIS, 4, 0x9C, PALETTE_EOLIS, 0,
             {SPECIAL_ITEMS: BIT_ELIXIR, SELECTED_ITEM: NO_ITEM, NUMBER_OF_ITEMS: 0})
        status_shot("status-elixir-charged")
        poke({HP: 1, HP_FRACTION: 0})
        until("a Zozura's hit", lambda: ram(MESSAGE_ID) == USED_ELIXIR, 2400)
        until("the line to type out", typed, 900)
        shot("elixir-used")
        page_through()
        until("HP and MP to refill", lambda: ram(HP) == 0x50 and not box_open(), 1200)
        s = state()
        assert s["revives"] == 1 and s["elixir_spent"] and not s["elixir_charged"], s
        print("ok: at 0 HP the Elixir revives the hero, and is kept, spent")
        poke({HP: 1, HP_FRACTION: 0})
        dismiss(NEGATIVE_THOUGHTS, 2400)
        until("the respawn", lambda: place() == (BUILDINGS, EOLIS_TEMPLE), 1200)
        wait(30)
        s = state()
        assert s["elixir_spent"] and not s["elixir_charged"] and s["revives"] == 1, s
        temple = out / "temple.cycstate"
        tcp("save_state", path=str(temple))
        status_shot("status-elixir-spent")
        print("ok: spent, the Elixir does not save him; he dies and respawns with it still spent, and the status "
              "page does not show it")
        talk_to_guru()
        until("the Guru to charge it", lambda: state()["guru_recharges"] == 1, 120)
        s = state()
        assert s["elixir_charged"] and not s["elixir_spent"], s
        page_through()
        status_shot("status-elixir-recharged")
        print("ok: talking to Eolis's Guru charges the spent Elixir, and the status page shows it again")

        # 5. Eolis's tool shop: a spent Elixir is charged for its price; a charged one is not sold twice.
        warp(shop, "tool-shop", EOLIS, 3, 0x9A, PALETTE_EOLIS, 0,
             {SPECIAL_ITEMS: 0, SELECTED_ITEM: NO_ITEM, NUMBER_OF_ITEMS: 0, GOLD: 0xE8, GOLD + 1: 0x03, GOLD + 2: 0},
             {5: 1})
        assert state()["elixir_spent"] and gold() == 1000, (state(), gold())
        enter_door(TOOL_SHOP)
        act("left", 120, 10)                                        # to the counter
        seen = buy(ELIXIR_ROW)
        wait(4)
        s = state()
        assert gold() == 1000 - ELIXIR_PRICE and s["elixir_charged"] and not s["elixir_spent"], (gold(), s, seen)
        assert CANT_CARRY_MORE not in seen, seen
        seen = buy(ELIXIR_ROW)
        assert CANT_CARRY_MORE in seen and gold() == 1000 - ELIXIR_PRICE, (seen, gold())
        assert state()["elixir_charged"], state()
        print(f"ok: the tool shop charges a spent Elixir for {ELIXIR_PRICE} gold, and refuses a second while it is "
              "charged (\"You can't carry any more.\")")

        # 6. Finds: each recharges what it duplicates.
        warp(shop, "find-boots", TRUNK, 0x2D, 0x31, PALETTE_TOWER, 1,
             {SELECTED_ITEM: WING_BOOTS, NUMBER_OF_ITEMS: 0, VISIT_COUNT: 3}, {2: COOLDOWN})
        assert state()["boots_cooldown"] > 0, state()
        slots = ram(SPRITE_ENTITIES, 8)                             # it shows once the hornets are gone;
        poke({PLAYER_X: 40, **{SPRITE_ENTITIES + i: 0xFF for i in range(8) if slots[i] == 0x07}})   # 24 px off it
        assert ram(PLAYER_Y) == 48, f"on the ledge: y {ram(PLAYER_Y)}"
        wait(2 * SECOND)                                            # past the zone's name
        assert ram(MESSAGE_ID) != GOT_WING_BOOTS and not state()["boots_ready"], state()
        hud_shot("find-boots-before", range(1, COOLDOWN + 1))       # the pair by the wall, T: blinking
        for _ in range(60):                                         # it lies at x 16
            if ram(MESSAGE_ID) == GOT_WING_BOOTS:
                break
            act("left", 2, 2)
        until("the find", lambda: ram(MESSAGE_ID) == GOT_WING_BOOTS, 600)
        until("the line to type out", typed, 900)
        assert not state()["boots_ready"], state()                  # Player_PickUpItem comes after the line
        page_through()
        wait(4)
        s = state()
        assert s["boots_ready"] and s["pickups"] == 1 and items() == ([], WING_BOOTS), (s, items())
        until("T: to show 00", lambda: state()["hud_drawn"] == 0, 60)
        wait(2)
        shot("find-boots-after")
        warp(shop, "find-glass", TRUNK, 0x13, 0x3D, PALETTE_TOWER, 1,
             {SELECTED_ITEM: HOUR_GLASS, NUMBER_OF_ITEMS: 0, VISIT_COUNT: 3}, {4: COOLDOWN})
        assert state()["glass_cooldown"] > 0, state()
        clear(0x47)
        clear(0x26)
        dismiss(GOT_HOUR_GLASS, 600)
        s = state()
        assert s["glass_ready"] and s["pickups"] == 2 and items() == ([], HOUR_GLASS), (s, items())
        warp(shop, "find-elixir", TRUNK, 0x3C, 0x22, PALETTE_TOWER, 1,
             {SPECIAL_ITEMS: 0, SELECTED_ITEM: NO_ITEM, NUMBER_OF_ITEMS: 0}, {5: 1})
        assert state()["elixir_spent"], state()
        clear(0x07)
        dismiss(GOT_ELIXIR, 600)
        until("the Elixir charged", lambda: not state()["elixir_spent"], 60)
        assert state()["elixir_charged"], state()
        print("ok: a found pair of Wing Boots, an Hour Glass and an Elixir each recharge the one held, "
              "without a second copy")

        # 7. The Spring of Sky: from Trunk $20's floor below the shaft to the man in $25. The enemies of
        # $20, $23 and $24 are cleared as the hero reaches each: the flight is what is measured.
        warp(shop, "spring", TRUNK, 0x20, 0x94, PALETTE_OUTSIDE, 1,
             {SELECTED_ITEM: WING_BOOTS, NUMBER_OF_ITEMS: 0, QUESTS: 0x01, PLAYER_TITLE: 0})
        use()
        dismiss(USED_WING_BOOTS)
        start = frame()
        assert ram(DURATION_WING_BOOTS) in (FLIGHT, FLIGHT - 1)
        hold, held = 40, {}                                         # pad button -> frame its hold was renewed

        def press(want, now):
            """Hold the buttons in `want` (A to fly, Up to rise), renewing each hold before it lapses, and let
            the others go: a lapse would land the hero, and a fresh A would jump."""
            for b in ("a", "dpup", "dpleft", "dpright"):
                if b in want and now - held.get(b, -hold) > hold // 2:
                    tcp("pad", buttons=b, frames=hold)
                    held[b] = now
                elif b not in want and b in held:
                    tcp("pad", buttons=b, frames=1)
                    del held[b]

        def steer(x, target):
            return {"dpright"} if x < target - 3 else {"dpleft"} if x > target + 3 else set()
        cleared = shaft_shot = None
        for _ in range(5000):
            now = frame()
            zp = ram(SCREEN_READY, PLAYER_Y + 1 - SCREEN_READY)
            screen, x, y = zp[SCREEN - SCREEN_READY], zp[PLAYER_X - SCREEN_READY], zp[PLAYER_Y - SCREEN_READY]
            assert ram(DURATION_WING_BOOTS) < 0x80, f"the flight ran out at {place()}, x={x} y={y}"
            if screen != cleared and screen != 0x25 and zp[0] == 0:   # not the man and his fountain
                clear()
                cleared = screen
            if screen == 0x24 and not shaft_shot and y < 160:
                shot("spring-flight")
                shaft_shot = True
            if screen in (0x20, 0x23) or (screen == 0x24 and y > 112):
                want = {"a", "dpup"} | steer(x, 48)                 # up the shaft, columns 2-5
            elif screen == 0x24 or (screen == 0x25 and y > 40):
                want = {"a", "dpup"} | steer(x, 216)                # round the ledge's right end, up the right side
            elif screen == 0x25 and x > 186:
                want = {"a"} | ({"dpup"} if y > 16 else set()) | steer(x, 182)   # along the top, over the man
            elif screen == 0x25:
                press(set(), now)                                   # let go: down onto his ledge
                break
            else:
                raise AssertionError(f"off the route: {place()}, x={x} y={y}")
            press(want, now)
        until("the landing", lambda: ram(PLAYER_FLAGS) & 0x05 == 0 and place() == (TRUNK, 0x25), 300)
        landed = frame() - start
        left = ram(DURATION_WING_BOOTS)
        assert left < 0x80, "the flight ran out before the landing"
        for _ in range(12):                                         # beside him, on the fountain's ledge
            if ram(MESSAGE_ID) == HO_HO_HO:
                break
            x = ram(PLAYER_X)
            if not 172 <= x <= 186:
                act("right" if x < 172 else "left", 2, 8)
                continue
            act("up", 4, 20)
        until("the Spring of Sky's man", lambda: ram(MESSAGE_ID) == HO_HO_HO, 300)
        shot("spring-of-sky")
        page_through()
        assert ram(QUESTS) & QUEST_SPRING_OF_SKY, "the Spring of Sky's man woke"
        print(f"ok: the Spring of Sky: from Trunk $20's floor to the man in $25 in {landed} frames "
              f"({landed / SECOND:.1f} of the game's seconds), with {left} s of the {FLIGHT} s flight left")
        close()

        # 8. Guru travel: a journey keeps the flight and the cooldown counting.
        launch(gurus=("Eolis", "Apolune"))
        travel = out / "temple-travel.cycstate"
        fixture(temple, travel, {SPECIAL_ITEMS: RING_OF_ELF, TEMPLE_SPAWN_POINT: 1, DURATION_WING_BOOTS: 25,
                                 SELECTED_ITEM: WING_BOOTS, NUMBER_OF_ITEMS: 0}, {1: 1, 2: 0, 4: 50})
        tcp("load_state", path=str(travel))
        wait(4)
        s = state()
        assert s["boots_running"] == 1 and 48 <= s["glass_cooldown"] <= 50 and ram(PLAYER_STATUS) & 0x80, s
        talk_to_guru()                                              # with the ring, he offers Apolune
        end = frame() + 40 * SECOND                                  # his line, the intro, the offer: A to each
        while frame() < end:
            line = tcp("travel_state")["text"]
            if line == TRAVEL_TO_GURU + 1 and typed():
                break
            if typed():
                act("a", 6, 14)
            else:
                wait(4)
        assert tcp("travel_state")["text"] == TRAVEL_TO_GURU + 1, tcp("travel_state")
        flying, cooling = ram(DURATION_WING_BOOTS), state()["glass_cooldown"]
        act("a", 6, 14)
        until("the farewell", lambda: tcp("travel_state")["text"] == TRAVEL_DEPART, 300)
        page_through()
        until("Apolune's temple", lambda: place() == (BUILDINGS, APOLUNE_TEMPLE) and settled(), 2400)
        wait(SECOND + 6)                                            # a game second in the main loop
        s, after = state(), ram(DURATION_WING_BOOTS)
        assert s["boots_running"] == 1 and 0 < after < flying and ram(PLAYER_STATUS) & 0x80, (s, after)
        assert 0 < s["glass_cooldown"] < cooling, s
        dismiss(WING_BOOTS_GONE, (FLIGHT + 2) * SECOND)
        until("the cooldown", lambda: state()["boots_cooldown"] > 0, 60)
        assert state()["boots_cooldown"] == COOLDOWN, state()
        print(f"ok: a Guru travel journey from Eolis to Apolune keeps the flight ({flying} s left before, {after} "
              f"after) and the Hour Glass's cooldown ({cooling} s, then {s['glass_cooldown']}), and the flight's "
              "cooldown follows it")
        close()

        # 9. Off: the original's one-use pair and its title-based flight.
        launch(on=False)
        fixture(town, boots, {HP: 0x50, SELECTED_ITEM: WING_BOOTS, NUMBER_OF_ITEMS: 0, PLAYER_TITLE: 0, **no_one})
        tcp("load_state", path=str(boots))
        wait(10)
        use()
        dismiss(USED_WING_BOOTS)
        assert ram(DURATION_WING_BOOTS) in (40, 39) and items() == ([], NO_ITEM), (ram(DURATION_WING_BOOTS), items())
        print("ok: turned off, a Novice's Wing Boots fly 40 s and are used up, as in the original")
        print("screenshots:", ", ".join(shots))
    except BaseException:
        if game and game.poll() is None:
            shot("failure")
            print("at", place(), "x", ram(PLAYER_X), "y", ram(PLAYER_Y), "message", hex(ram(MESSAGE_ID)),
                  "state", state(), file=sys.stderr)
        raise
    finally:
        if game:
            close()


if __name__ == "__main__":
    sys.exit(main())
