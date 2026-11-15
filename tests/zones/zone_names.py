"""Play zone names end to end in a hidden window and screenshot the banners.

    python3 tests/zones/zone_names.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/zones

The game runs uncapped (about 5x real time; --realtime to watch it at normal
speed) and the script waits on the game's own state, never on the clock, so
each run takes a free TCP port, and any number can go in parallel.

Copies the build to OUT (so the faxanadu_gurus.txt sidecar stays there) and
starts a new game over TCP. It walks through Eolis's gate, into a shop and out,
and through the door out of Eolis into the Trunk. Then it relaunches with the
Gurus of Eolis, Apolune and Conflate visited (as tests/travel/guru_travel.py
does), respawns in Conflate, travels to Apolune's Guru, and walks out of each
temple. Last, it edits where a temple's door leads (save-state RAM, an external
prerequisite) to walk out into the Tower of Fortress and the Castle of
Fraternal. A screenshot of each banner is saved in OUT.
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
AREA, SCREEN, PLAYER_X = 0x24, 0x63, 0x9E
TEXTBOX_CLOSED, MESSAGE_ID, TEXTBOX_PAUSED = 0x20C, 0x213, 0x215
AREA_SAVED_AREA, AREA_SAVED_SCREEN, SCREEN_SAVED_PALETTE, PLAYER_SAVED_POS = 0x3D5, 0x3D6, 0x3D7, 0x3D8
HP, AREA_REGION, PLAYER_IS_DEAD, TEMPLE_SPAWN_POINT = 0x431, 0x435, 0x438, 0x439
SELECTED_ITEM, KEY_J = 0x3C1, 0x07                                 # Game_OpenDoorWithJKey's check
BUILDINGS, TOWNS = 4, 3
TEMPLE_SCREEN = {1: 0x0B, 5: 0x2B}                                  # DEST_SCREEN_FOR_TEMPLE_SPAWN: Apolune, Conflate
APOLUNE, CONFLATE = 1, 5
MEDITATE_WITH_YOU, NEGATIVE_THOUGHTS = 0x22, 0x24                  # the Guru's greeting; the death message
OFFER, DEPART, TO_GURU = 1, 3, 4                                   # fax_travel.c's lines (TO_GURU + temple)
PALETTE_TOWER, PALETTE_FRATERNAL = 0x07, 0x0D


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


def zone():
    return tcp("zone_state")


def typed():
    """The text box is waiting for a button: the message has typed out
    (MessageID clears) or reached a pause (TextBox_MessagePaused)."""
    message, _, paused = ram(MESSAGE_ID, 3)
    return message == 0 or paused != 0


def box_open():
    return ram(TEXTBOX_CLOSED) == 0


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
    """Copy a save state, changing RAM and, if given, bytes of zone names' record."""
    data = bytearray(source.read_bytes())
    pos, size = next((p, n) for t, p, n in chunks(data) if t == b"HW  ")
    assert size == 2072, "Unqualified hardware ABI"
    for address, value in writes.items():
        data[pos + 23 + address] = value
    for tag, p, n in chunks(data):
        if tag == b"MOD " and data[p:p + 64].rstrip(b"\0") == b"faxanadu.zone-names":
            for offset, value in (record or {}).items():
                data[p + 64 + offset] = value
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
        shots.append(path)

    def banner(name, count):
        """Wait for the banner `name` to be the `count`th shown, fully in, and screenshot it."""
        until(f"the {name} banner", lambda: zone()["banners"] >= count, 600)
        state = zone()
        assert state["banners"] == count and state["last"] == name, state
        until(f"the {name} banner to fade in", lambda: zone()["alpha"] == 255, 60)
        shot(name.lower().replace(" ", "-"))

    def settled_in(area, screen):
        until(f"area {area} screen {screen:#x}", lambda: zone()["area"] == area and zone()["screen"] == screen)
        wait(30)

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
        """Walk right out of a building, back to where its door leads, and stop."""
        for _ in range(200):
            if ram(AREA) != BUILDINGS:
                return
            act("right", 2, 0)
        raise AssertionError("could not walk out")

    def travel(temple):
        """Talk to the Guru and travel to `temple`, which must be the second one offered."""
        act("left", 170, 10)
        act("up", 4, 10)
        until("the Guru's greeting", lambda: ram(MESSAGE_ID) == MEDITATE_WITH_YOU)
        for want, button in ((OFFER, "a"), (TO_GURU + 0, "b"), (TO_GURU + temple, "a"), (DEPART, "a")):
            for _ in range(12):
                until("a page to type out", typed)
                if tcp("travel_state")["text"] == want:
                    break
                act("a")
            assert tcp("travel_state")["text"] == want, tcp("travel_state")
            act(button, 6, 30)
        until(f"temple {temple}", lambda: (ram(AREA), ram(SCREEN)) == (BUILDINGS, TEMPLE_SCREEN[temple]), 2400)

    def warp(source, name, area, screen, palette, pos, region):
        """From inside a building, walk out to another place: edit where its door leads."""
        target = out / f"{name}.cycstate"
        fixture(source, target, {AREA_SAVED_AREA: area, AREA_SAVED_SCREEN: screen, SCREEN_SAVED_PALETTE: palette,
                                 PLAYER_SAVED_POS: pos, AREA_REGION: region, HP: 0x50})
        tcp("load_state", path=str(target))
        walk_out()

    try:
        launch()
        until("the title", lambda: frame() > 700)
        tcp("pad", buttons="start", frames=10)
        until("the Eolis gate", lambda: zone()["area"] == 0 and frame() > 1900)
        assert zone()["banners"] == 0 and zone()["here"] is None, zone()
        print("ok: no banner outside Eolis's gate")

        # 1. Through the gate (door at block column 12): Eolis.
        walk_to(186, 196)
        act("up", 4, 10)
        settled_in(0, 1)
        banner("Eolis", 1)
        print("ok: walking through the gate names Eolis")

        # 2. Into the meat shop (column 3) and out: nothing more.
        walk_to(41, 56)
        act("up", 4, 10)
        until("the shop", lambda: ram(AREA) == BUILDINGS)
        wait(30)
        assert zone()["showing"] is None, zone()
        shop = out / "eolis-shop.cycstate"
        tcp("save_state", path=str(shop))
        walk_out()
        settled_in(0, 1)
        wait(60)
        assert zone()["banners"] == 1, zone()
        print("ok: stepping out of a shop shows nothing")

        # 3. Out of Eolis: the shop's door edited to lead to the door on screen 8
        #    (record bytes 5 and 6 are where zone names last saw the hero stand),
        #    with the "J" Key it takes in hand.
        warp_to = {AREA_SAVED_AREA: 0, AREA_SAVED_SCREEN: 8, PLAYER_SAVED_POS: 0x9C, HP: 0x50, SELECTED_ITEM: KEY_J}
        fixture(shop, out / "eolis-8.cycstate", warp_to, record={5: 0, 6: 8})
        tcp("load_state", path=str(out / "eolis-8.cycstate"))
        walk_out()
        settled_in(0, 8)
        assert zone()["banners"] == 1, zone()
        walk_to(186, 196)                                  # the door, column 12
        act("up", 4, 10)
        for _ in range(40):                                # "You used the key"
            if ram(AREA) == 1:
                break
            if box_open() and typed():
                act("a", 6, 14)
            else:
                wait(10)
        settled_in(1, 0)
        banner("Trunk", 2)
        print("ok: the door out of Eolis names the Trunk, once")
        gate = out / "trunk.cycstate"
        tcp("save_state", path=str(gate))
        close()

        # 4. Respawn in Conflate, then travel to Apolune's Guru.
        (out / "game" / "faxanadu_gurus.txt").write_text("Eolis\nApolune\nConflate\n")
        launch()
        dead = out / "conflate-death.cycstate"
        fixture(gate, dead, {TEMPLE_SPAWN_POINT: CONFLATE, PLAYER_IS_DEAD: 1})
        tcp("load_state", path=str(dead))
        until("the death message", lambda: ram(MESSAGE_ID) == NEGATIVE_THOUGHTS, 600)
        for _ in range(40):
            if (ram(AREA), ram(SCREEN)) == (BUILDINGS, TEMPLE_SCREEN[CONFLATE]):
                break
            if typed():
                act("a")
            else:
                wait(10)
        until("Conflate's temple", lambda: (ram(AREA), ram(SCREEN)) == (BUILDINGS, TEMPLE_SCREEN[CONFLATE]))
        wait(60)
        before = zone()["banners"]
        assert zone()["showing"] is None, zone()
        walk_out()
        settled_in(TOWNS, 8)
        banner("Conflate", before + 1)
        print("ok: walking out after a respawn names Conflate")
        walk_to(137, 152)                          # the temple's door, column 9
        act("up", 4, 10)
        until("Conflate's temple", lambda: ram(AREA) == BUILDINGS)
        wait(30)
        travel(APOLUNE)
        wait(60)
        assert zone()["showing"] is None and zone()["banners"] == before + 1, zone()
        temple = out / "apolune-temple.cycstate"
        tcp("save_state", path=str(temple))
        walk_out()
        settled_in(TOWNS, 1)
        banner("Apolune", before + 2)
        print("ok: walking out after a Guru travel journey names Apolune")

        # 5. A tower and a castle, out of Apolune's temple.
        warp(temple, "fortress", 1, 0x29, PALETTE_TOWER, 0x93, 1)   # where the door on screen $28 arrives
        settled_in(1, 0x29)
        banner("Tower of Fortress", before + 3)
        warp(temple, "fraternal", 6, 0x10, PALETTE_FRATERNAL, 0x91, 4)   # the door on Dartmoor's $0C
        settled_in(6, 0x10)
        banner("Castle of Fraternal", before + 4)
        print("ok: a tower and a castle are named")
        print("screenshots:", ", ".join(p.name for p in shots))
    except BaseException:
        shot("failure")
        raise
    finally:
        close()


if __name__ == "__main__":
    sys.exit(main())
