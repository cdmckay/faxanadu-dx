"""Play zone respawn end to end in a hidden window.

    python3 tests/respawn/zone_respawn.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/respawn
    python3 tests/respawn/zone_respawn.py ... --game-arg=--world-view=seamless

The game runs uncapped and the script waits on the game's own state, never on
the clock, and takes a free TCP port, so any number can run in parallel.

Copies the build to OUT and starts a new game over TCP. Save-state RAM edits
set up what the scenarios need (external prerequisites, not coverage): the
hero's gold and experience, where a building's door leads (Area_SavedArea and
the rest, as tests/zones/zone_names.py does), the key or ring a door takes,
and, in the tower, the screen's enemies cleared before a walk. Every entrance
is then used the game's own way, through its door, and every death is the
game's (PlayerIsDead set):

1. Dying outside Eolis's gate, before any entrance, wakes at Eolis's temple,
   as before.
2. Through the gate into Eolis, then a screen further left (a scroll, or with
   Seamless on a room handoff): dying there comes back at the gate's side of
   Eolis, after the Guru's words, with the gold and experience kept.
3. Through each tower's and castle's door (the Towers of Trunk, Fortress,
   Suffer and Mist, the Castle of Fraternal, the Fortress of Zenis) and each
   region's (Trunk, Mist, Branch, Dartmoor): dying inside comes back exactly
   where, and as, the door brought him in (screen, position, palette, music),
   with full HP and MP. Then he walks back out through the door he came in
   by, where there is one: no entrance traps him.
4. In the Tower of Trunk, dying a screen further in still comes back at its
   door (the default view: the walk needs the screen's enemies cleared).
5. A Guru travel journey (from Conflate's temple to Apolune's) forgets the
   checkpoint: walking out of Apolune's temple records the town, and dying
   there comes back at that door.
6. With the feature off in the mod selection, the same death in the tower
   wakes at the temple, as the original does.
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
AREA, SCREEN, PLAYER_X, PLAYER_Y, PLAYER_FLAGS = 0x24, 0x63, 0x9E, 0xA1, 0xA4
SCREEN_READY_STATE, SCROLL_DIRECTION, INTERRUPTS_ON = 0x0E, 0x54, 0x13
EXPERIENCE, GOLD, HP, MP = 0x390, 0x392, 0x431, 0x39A
SCREEN_PALETTE, AREAS_DEFAULT_MUSIC = 0x3D0, 0x3D1
AREA_SAVED_AREA, AREA_SAVED_SCREEN, SCREEN_SAVED_PALETTE, PLAYER_SAVED_POS = 0x3D5, 0x3D6, 0x3D7, 0x3D8
SELECTED_ITEM, SPECIAL_ITEMS, AREA_REGION = 0x3C1, 0x42C, 0x435
PLAYER_IS_DEAD, TEMPLE_SPAWN_POINT = 0x438, 0x439
TEXTBOX_CLOSED, MESSAGE_ID, TEXTBOX_PAUSED = 0x20C, 0x213, 0x215
SPRITE_ENTITIES = 0x2CC                                             # CurrentSprites_Entities, 8 slots
NEGATIVE_THOUGHTS = 0x24                                            # the death message, the Guru's
BUILDINGS, EOLIS_TEMPLE = 4, 0x02
KEY_A, KEY_K, KEY_Q, KEY_J = 0x04, 0x05, 0x06, 0x07                 # ITEMINVENTORY_KEY_*
RING_OF_DWORF, DEMONS_RING = 0x20, 0x10                             # SpecialItems bits
HERO = dict(experience=4660, gold=12345)
APOLUNE, CONFLATE = 1, 5                                            # temples
TEMPLE_SCREEN = {APOLUNE: 0x0B, CONFLATE: 0x2B}                     # DEST_SCREEN_FOR_TEMPLE_SPAWN
TOWNS = 3
MEDITATE_WITH_YOU = 0x22                                            # the Guru's greeting
OFFER, DEPART, TO_GURU = 1, 3, 4                                    # fax_travel.c's lines (TO_GURU + temple)
PACKAGE, FEATURE = "faxanadu.enhancement.zone-respawn", "zone-respawn"

# Each entrance: the zone, where its door is (area, screen, palette, block YX,
# region: what the temple's door is edited to lead to), what opens it, where
# the door brings the hero (area, screen, start YX), and the door back out
# (block YX, and the screen it leads to), if there is one. From the door
# tables in bank 3 (docs/GAME_INTERNALS.md, "Entrances and zone respawn").
ENTRANCES = [
    ("Tower of Trunk", (1, 0x0B, 0x06, 0xA9, 1), {SELECTED_ITEM: KEY_J}, (1, 0x0D, 0x91), (0x90, 0x0B)),
    ("Tower of Fortress", (1, 0x28, 0x06, 0x3D, 1), {SELECTED_ITEM: KEY_J}, (1, 0x29, 0x93), (0x92, 0x28)),
    ("Tower of Suffer", (2, 0x00, 0x0A, 0x9A, 2), {SELECTED_ITEM: KEY_Q}, (2, 0x3E, 0x91), (0x90, 0x00)),
    ("Tower of Mist", (2, 0x17, 0x0A, 0xA6, 2), {SELECTED_ITEM: KEY_K}, (2, 0x45, 0xA1), (0xA0, 0x17)),
    ("Castle of Fraternal", (6, 0x0C, 0x0C, 0x48, 4), {}, (6, 0x10, 0x91), (0x90, 0x0C)),
    ("Fortress of Zenis", (6, 0x0E, 0x0C, 0xAE, 4), {SPECIAL_ITEMS: DEMONS_RING}, (7, 0x08, 0xA2), (0xA1, 0x0E)),
    ("Trunk", (0, 0x08, 0x00, 0x9C, 0), {SELECTED_ITEM: KEY_J}, (1, 0x00, 0xA1), None),   # no door back to Eolis
    ("Mist", (1, 0x28, 0x06, 0x10, 1), {SELECTED_ITEM: KEY_Q}, (2, 0x11, 0x9E), (0x9F, 0x28)),
    ("Branch", (2, 0x1F, 0x0A, 0x9C, 2), {SELECTED_ITEM: KEY_A}, (5, 0x00, 0xA6), (0xA5, 0x1F)),
    ("Dartmoor", (5, 0x27, 0x08, 0xA8, 3), {SPECIAL_ITEMS: RING_OF_DWORF}, (6, 0x00, 0x9E), (0xAF, 0x27)),
]


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


def number(addr, n):
    return int.from_bytes(ram(addr, n), "little")


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


def typed():
    """The text box is waiting for a button: the message has typed out
    (MessageID clears) or reached a pause (TextBox_MessagePaused)."""
    message, _, paused = ram(MESSAGE_ID, 3)
    return message == 0 or paused != 0


def box_open():
    return ram(TEXTBOX_CLOSED) == 0


def settled():
    """A screen has loaded and is not scrolling, and no box is open."""
    return (ram(SCREEN_READY_STATE) == 0 and ram(INTERRUPTS_ON) and ram(SCROLL_DIRECTION) == 0xFF
            and not box_open() and not ram(PLAYER_IS_DEAD))


def at(area, screen):
    return (ram(AREA), ram(SCREEN)) == (area, screen) and settled()


def respawn():
    return tcp("respawn_state")


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


def fixture(source, target, writes):
    data = bytearray(source.read_bytes())
    pos, size = next((p, n) for t, p, n in chunks(data) if t == b"HW  ")
    assert size == 2072, "Unqualified hardware ABI"
    for address, value in writes.items():
        data[pos + 23 + address] = value
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
    ap.add_argument("--only", action="append", default=[], help="run only these entrances (repeatable)")
    args = ap.parse_args()
    PORT = args.port or free_port()
    seamless = any("seamless" in arg for arg in args.game_arg)
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

    def launch():
        nonlocal game
        extra = [part for arg in args.game_arg for part in arg.split("=", 1)]
        if not args.realtime:
            extra += ["--uncapped", "--exit-after", "100000000"]
        game = subprocess.Popen([str(out / "game" / "FaxanaduRecomp"), str(args.rom.resolve()), "--hidden",
                                 "--tcp", str(PORT), *extra], env=env, stdout=log, stderr=subprocess.STDOUT)
        for _ in range(100):
            try:
                return frame()
            except OSError:
                time.sleep(0.1)
        raise AssertionError("the game did not start")

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

    def save(name):
        path = out / f"{name}.cycstate"
        tcp("save_state", path=str(path))
        return path

    def page_until(what, test, tries=80):
        """Page through any text with A until `test` holds."""
        for _ in range(tries):
            if test():
                return
            if box_open() and typed():
                act("a", 6, 14)
            else:
                wait(10)
        raise AssertionError(f"timed out paging to {what}")

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
        raise AssertionError(f"could not reach x {x_min}-{x_max}, at {ram(PLAYER_X)}")

    def walk_out():
        """Walk right out of a building, to where its door leads."""
        for _ in range(200):
            if ram(AREA) != BUILDINGS:
                return
            act("right", 2, 0)
        raise AssertionError("could not walk out")

    def clear_screen(name):
        """The screen's enemies gone and HP full, for a walk (a prerequisite, not coverage)."""
        raw = save(f"{name}-raw")
        clear = out / f"{name}-clear.cycstate"
        fixture(raw, clear, {**{SPRITE_ENTITIES + i: 0xFF for i in range(8)}, HP: 0x50})
        tcp("load_state", path=str(clear))

    def through_door(block, what, test):
        """Stand at the door at `block` (YX), as Area_SetStateFromDoorDestination
        finds it at X + 7, and press Up until `test` holds (a knock or a landing
        can take the press)."""
        col = block & 0x0F
        for _ in range(6):
            walk_to(max(0, col * 16 - 7), col * 16 + 8)
            act("up", 4, 10)
            try:
                return page_until(what, test, 40)
            except AssertionError:
                pass
        raise AssertionError(f"could not go through the door to {what}")

    def arrival():
        """Where and how the hero is, as a door or a respawn left him."""
        return dict(area=ram(AREA), screen=ram(SCREEN), x=ram(PLAYER_X), y=ram(PLAYER_Y),
                    palette=ram(SCREEN_PALETTE), music=ram(AREAS_DEFAULT_MUSIC),
                    facing_right=bool(ram(PLAYER_FLAGS) & 0x40))

    def hero():
        return dict(experience=number(EXPERIENCE, 2), gold=number(GOLD, 3))

    def die(state, name, area, screen, pictures=False):
        """Die from `state` and come back at (area, screen), paging through the
        Guru's words. Returns how the hero is when the screen settles."""
        dying = out / f"dying-{name}.cycstate"
        fixture(state, dying, {PLAYER_IS_DEAD: 1})
        tcp("load_state", path=str(dying))
        if pictures:
            wait(30)
            shot(f"{name}-dying")
        until("the Guru's words", lambda: ram(MESSAGE_ID) == NEGATIVE_THOUGHTS, 600)
        if pictures:
            until("the Guru's words to type", typed, 600)
            shot(f"{name}-guru")
        page_until(f"area {area} screen {screen:#x}", lambda: at(area, screen))
        came = arrival()
        if pictures:
            wait(20)
            shot(f"{name}-respawned")
        return came

    try:
        launch()
        assert respawn()["enabled"], "zone respawn is on by default"
        until("the title", lambda: frame() > 700)
        tcp("pad", buttons="start", frames=10)
        until("the Eolis gate", lambda: ram(HP) and at(0, 0), 3000)
        raw = save("gate-raw")
        gate = out / "gate.cycstate"
        fixture(raw, gate, {EXPERIENCE: HERO["experience"] & 0xFF, EXPERIENCE + 1: HERO["experience"] >> 8,
                            GOLD: HERO["gold"] & 0xFF, GOLD + 1: HERO["gold"] >> 8, GOLD + 2: 0, HP: 0x50})

        # 1. No entrance yet: the temple, as before.
        assert respawn()["zone"] is None
        die(gate, "gate", BUILDINGS, EOLIS_TEMPLE)
        state = respawn()
        assert (state["respawns"], state["fallbacks"]) == (0, 1), state
        wait(30)
        temple = save("temple")
        print("ok: with no entrance yet, death wakes at Eolis's temple, as before")

        # 2. Eolis, through its gate, and a screen further left.
        tcp("load_state", path=str(gate))
        walk_to(186, 196)
        act("up", 4, 10)
        until("Eolis", lambda: at(0, 1), 600)
        entered = arrival()
        state = respawn()
        assert (state["zone"], state["area"], state["screen"], state["start"]) == ("Eolis", 0, 1, 0x99), state
        assert (entered["x"], entered["y"]) == (0x90, 0x90), entered
        for _ in range(300):
            if ram(SCREEN) == 2 and settled() and ram(PLAYER_X) < 200:
                break
            act("left", 4, 0)
        assert ram(SCREEN) == 2, "a screen further into Eolis"
        wait(10)
        further = save("eolis-2")
        came = die(further, "eolis", 0, 1, pictures=True)
        assert came == entered, (came, entered)
        assert (ram(HP), ram(MP)) == (0x50, 0x50), "full HP and MP"
        assert hero() == HERO, hero()
        assert ram(TEMPLE_SPAWN_POINT) == 0, "the mantra's temple is unchanged"
        state = respawn()
        assert (state["respawns"], state["fallbacks"]) == (1, 1), state
        print("ok: dying a screen into Eolis comes back at its gate, after the Guru, with gold and experience")

        # 3. Every tower, castle and region, through its own door.
        inside = {}
        for name, (area, screen, palette, pos, region), opens, (in_area, in_screen, start), back in ENTRANCES:
            if args.only and name not in args.only:
                continue
            door = out / f"door-{name}.cycstate"
            fixture(temple, door, {AREA_SAVED_AREA: area, AREA_SAVED_SCREEN: screen, SCREEN_SAVED_PALETTE: palette,
                                   PLAYER_SAVED_POS: pos, AREA_REGION: region, HP: 0x50, **opens})
            tcp("load_state", path=str(door))
            walk_out()
            until(f"outside {name}", lambda: at(area, screen), 600)
            clear_screen(f"outside-{name}")
            through_door(pos, name, lambda: at(in_area, in_screen))
            entered = arrival()
            # Where the door puts him; Dartmoor's start is a row above the floor,
            # and he has dropped onto it by the time the screen settles.
            assert entered["x"] == (start & 0x0F) << 4 and entered["y"] in (start & 0xF0, (start & 0xF0) + 16), \
                (name, entered)
            state = respawn()
            assert (state["zone"], state["area"], state["screen"], state["start"]) == \
                (name, in_area, in_screen, start), (name, state)
            inside[name] = save(f"in-{name}")
            before = respawn()["respawns"]
            came = die(inside[name], f"died-{name}", in_area, in_screen)
            assert came == entered, (name, came, entered)
            assert (ram(HP), ram(MP)) == (0x50, 0x50), name
            assert hero() == HERO, (name, hero())
            assert respawn()["respawns"] == before + 1
            where = f"{in_screen:#04x}, x={came['x']}, y={came['y']}, palette {came['palette']:#04x}, " \
                    f"music {came['music']:#04x}"
            if back:
                block, outside = back
                clear_screen(f"respawned-{name}")
                through_door(block, f"out of {name}", lambda: at(area, outside))
                print(f"ok: {name}: back where its door brought him ({where}), and out through that door")
            else:
                print(f"ok: {name}: back where its door brought him ({where})")

        # 4. A screen further into the Tower of Trunk. With Seamless on, the
        #    next room's enemies cannot be cleared, so the walk is the default
        #    view's; step 2 crossed a room with Seamless on.
        if "Tower of Trunk" in inside and not seamless:
            clear = out / "tower-clear.cycstate"
            fixture(inside["Tower of Trunk"], clear, {SPRITE_ENTITIES + i: 0xFF for i in range(8)})
            tcp("load_state", path=str(clear))
            for _ in range(400):
                if ram(SCREEN) == 0x0E and settled():
                    break
                act("right", 4, 0)
            assert ram(SCREEN) == 0x0E, "a screen further into the tower"
            walk_to(40, 60)
            further = save("tower-0e")
            came = die(further, "tower", 1, 0x0D, pictures=True)
            assert (came["x"], came["y"]) == (0x10, 0x90), came
            assert hero() == HERO, hero()
            print("ok: dying a screen into the Tower of Trunk comes back at its door")

        # 5. A Guru travel journey forgets the checkpoint: walking out of the
        #    temple it lands in records the next, and a death comes back there.
        close()
        (out / "game" / "faxanadu_gurus.txt").write_text("Eolis\nApolune\nConflate\n")
        launch()
        die_in_conflate = out / "conflate.cycstate"
        fixture(gate, die_in_conflate, {TEMPLE_SPAWN_POINT: CONFLATE})
        die(die_in_conflate, "to-conflate", BUILDINGS, TEMPLE_SCREEN[CONFLATE])
        walk_out()
        until("Conflate", lambda: at(TOWNS, 0x08), 600)
        state = respawn()
        assert (state["zone"], state["screen"], state["start"]) == ("Conflate", 0x08, 0x99), state
        through_door(0x99, "Conflate's temple", lambda: at(BUILDINGS, TEMPLE_SCREEN[CONFLATE]))
        assert respawn()["zone"] == "Conflate", "a building changes nothing"
        act("left", 170, 10)
        act("up", 4, 10)
        until("the Guru's greeting", lambda: ram(MESSAGE_ID) == MEDITATE_WITH_YOU)
        for want, button in ((OFFER, "a"), (TO_GURU + 0, "b"), (TO_GURU + APOLUNE, "a"), (DEPART, "a")):
            for _ in range(12):
                until("a page to type out", typed)
                if tcp("travel_state")["text"] == want:
                    break
                act("a")
            assert tcp("travel_state")["text"] == want, tcp("travel_state")
            act(button, 6, 30)
        until("Apolune's temple", lambda: at(BUILDINGS, TEMPLE_SCREEN[APOLUNE]), 2400)
        assert respawn()["zone"] is None, "the journey forgot Conflate"
        walk_out()
        until("Apolune", lambda: at(TOWNS, 0x01), 600)
        entered = arrival()
        state = respawn()
        assert (state["zone"], state["screen"]) == ("Apolune", 0x01), state
        came = die(save("apolune"), "apolune", TOWNS, 0x01)
        assert came == entered, (came, entered)
        assert ram(TEMPLE_SPAWN_POINT) == CONFLATE, "the furthest temple is unchanged"
        print("ok: a Guru travel journey forgets the checkpoint; walking out of Apolune's temple records the "
              "next, and dying comes back there")

        # 6. Turned off, the same death wakes at the temple.
        if "Tower of Trunk" in inside:
            close()
            (out / "game" / "mods" / "state.toml").write_text(
                f'format_version = 1\n\n[[feature]]\npackage_id = "{PACKAGE}"\nid = "{FEATURE}"\nenabled = false\n')
            launch()
            assert not respawn()["enabled"], "the feature is off"
            die(inside["Tower of Trunk"], "off", BUILDINGS, EOLIS_TEMPLE)
            print("ok: turned off, dying in the Tower of Trunk wakes at Eolis's temple, as the original does")
        print("screenshots:", ", ".join(shots))
    except BaseException:
        if game and game.poll() is None:
            shot("failure")
        raise
    finally:
        if game:
            close()


if __name__ == "__main__":
    sys.exit(main())
