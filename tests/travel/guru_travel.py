"""Play Guru travel end to end in a hidden window and check what it changes.

    python3 tests/travel/guru_travel.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/travel

The game runs uncapped (about 5x real time; --realtime to watch it at normal
speed) and the script waits on the game's own state, never on the clock, so
runs on different --port values can go in parallel.

Copies the build to OUT (so the mantra and faxanadu_gurus.txt sidecars stay
there), starts a new game over TCP, then edits save-state RAM to place the hero
(an external prerequisite, not coverage): Conflate is the furthest temple, and
the Gurus of Eolis, Apolune, Mascon and Conflate have been visited. Each
scenario starts from the same state and saves a screenshot per step in OUT.
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

PORT = 4392
TEMPLE_SPAWN_POINT, PLAYER_IS_DEAD, MESSAGE_ID, TEXTBOX_PAUSED = 0x439, 0x438, 0x213, 0x215
AREA, SCREEN, HP, MP, GOLD = 0x24, 0x63, 0x431, 0x39A, 0x392
BUILDINGS = 4
TEMPLE_SCREEN = [0x02, 0x0B, 0x10, 0x1E, 0x23, 0x2B, 0x33, 0x3C]   # DEST_SCREEN_FOR_TEMPLE_SPAWN
CONFLATE, MASCON = 5, 3
MEDITATE_WITH_YOU, NEGATIVE_THOUGHTS = 0x22, 0x24                  # the Guru's greeting; the death message
OFFER, FAREWELL, DEPART, TO_GURU = 1, 2, 3, 4                      # fax_travel.c's lines (TO_GURU + temple)


def tcp(cmd, **kw):
    with socket.create_connection(("127.0.0.1", PORT), 5) as s:
        s.settimeout(30)
        f = s.makefile("rwb", buffering=0)
        f.write((json.dumps(dict(id=1, cmd=cmd, **kw)) + "\n").encode())
        reply = json.loads(f.readline())
    if not reply.get("ok"):
        raise RuntimeError(f"{cmd}: {reply}")
    return reply


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


def typed():
    """The text box is waiting for a button: the message has typed out
    (MessageID clears) or reached a pause (TextBox_MessagePaused)."""
    message, _, paused = ram(MESSAGE_ID, 3)
    return message == 0 or paused != 0


def in_temple(temple):
    return (ram(AREA), ram(SCREEN)) == (BUILDINGS, TEMPLE_SCREEN[temple])


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
    ap.add_argument("--port", type=int, default=PORT, help="TCP port; give parallel runs their own")
    ap.add_argument("--realtime", action="store_true", help="run at normal speed instead of uncapped")
    args = ap.parse_args()
    PORT = args.port
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

    def line():
        return tcp("travel_state")["text"]

    def shot_line(name):
        until("the line to type out", typed)
        shot(name)

    def advance(expect, tries=12):
        """Press A on each typed-out page until the Guru's line `expect` is up."""
        for _ in range(tries):
            if line() == expect:
                return
            until("a page to type out", lambda: line() == expect or typed())
            if line() == expect:
                return
            act("a")
        until(f"line {expect}", lambda: line() == expect, 300)

    def talk_to_guru():
        """Walk left to the Guru, talk, and page through his greeting and the mantra."""
        act("left", 170, 10)
        act("up", 4, 10)
        until("the Guru's greeting", lambda: ram(MESSAGE_ID) == MEDITATE_WITH_YOU)
        advance(OFFER)

    def answer(button, expect):
        """Answer the question on screen once it has typed out."""
        until("the question to type out", typed)
        act(button)
        until(f"line {expect}", lambda: line() == expect)

    def close_box(button="a"):
        """Dismiss the last line once it has typed out, and let the box close."""
        until("the line to type out", typed)
        act(button, 6, 60)

    def die_and_respawn(temple):
        """Page through the death message until the hero is back in `temple`."""
        until("the death message", lambda: ram(MESSAGE_ID) == NEGATIVE_THOUGHTS, 600)
        for _ in range(40):
            if in_temple(temple):
                return
            if typed():
                act("a")
            else:
                wait(10)
        until(f"temple {temple}", lambda: in_temple(temple))

    try:
        # A new game meets only Eolis's Guru: make the starting state, then
        # relaunch with the visited Gurus the scenarios need.
        launch()
        until("the title", lambda: frame() > 700)
        tcp("pad", buttons="start", frames=10)
        until("the Eolis gate", lambda: frame() > 1900)
        assert tcp("travel_state")["visited"] == 1, "a new game has met only Eolis's Guru"
        gate = out / "gate.cycstate"
        tcp("save_state", path=str(gate))
        close()
        (out / "game" / "faxanadu_gurus.txt").write_text("Eolis\nApolune\nMascon\nConflate\n")
        launch()
        state = tcp("travel_state")
        assert state["served"] and state["visited"] == 0b101011, state
        dead = out / "conflate-death.cycstate"
        fixture(gate, dead, {TEMPLE_SPAWN_POINT: CONFLATE, PLAYER_IS_DEAD: 1})
        tcp("load_state", path=str(dead))
        die_and_respawn(CONFLATE)
        wait(60)
        temple = out / "conflate-temple.cycstate"
        raw = out / "conflate-temple-raw.cycstate"
        tcp("save_state", path=str(raw))
        fixture(raw, temple, {HP: 0x2A, MP: 0x11, GOLD: 0xE8, GOLD + 1: 0x03, GOLD + 2: 0x00})   # 1000 gold
        before = None

        # 1. B at the offer: the conversation ends where it is.
        tcp("load_state", path=str(temple))
        talk_to_guru()
        shot_line("offer")
        close_box("b")
        wait(60)
        assert tcp("travel_state")["dest"] == -1 and in_temple(CONFLATE)
        print("ok: declining the offer stays in Conflate")

        # 2. Only visited Gurus up to the furthest temple, in order, then farewell.
        tcp("load_state", path=str(temple))
        before = tcp("mantra_now")["mantra"]
        talk_to_guru()
        advance(TO_GURU + 0)
        shot_line("to-eolis")
        answer("b", TO_GURU + 1)
        answer("b", TO_GURU + MASCON)        # Forepaw was never visited
        shot_line("to-mascon")
        asking = out / "asking-mascon.cycstate"
        tcp("save_state", path=str(asking))
        answer("b", FAREWELL)                # neither was Victim; Conflate is here
        shot_line("farewell")
        close_box()
        wait(60)
        assert tcp("travel_state")["dest"] == -1 and in_temple(CONFLATE)
        print("ok: offers Eolis, Apolune, Mascon only, then the farewell")
        print("ok: a state saved before the conversation still loads after it")

        # 3. Travel to Mascon: everything but the place is kept.
        tcp("load_state", path=str(temple))
        talk_to_guru()
        advance(TO_GURU + 0)
        answer("b", TO_GURU + 1)
        answer("b", TO_GURU + MASCON)
        answer("a", DEPART)
        assert tcp("travel_state")["dest"] == MASCON
        shot_line("depart")
        close_box()
        until("Mascon's temple", lambda: in_temple(MASCON))
        wait(90)
        shot("arrived")
        assert ram(TEMPLE_SPAWN_POINT) == CONFLATE, "the furthest temple stays the spawn point"
        assert (ram(HP), ram(MP), ram(GOLD, 3)) == (0x2A, 0x11, bytes([0xE8, 0x03, 0x00]))
        assert tcp("mantra_now")["mantra"] == before, "the mantra is unchanged by travel"
        print("ok: travelled to Mascon keeping HP, MP, gold, spawn point and mantra")

        # 4. Dying after the journey respawns at the furthest temple.
        arrived = out / "mascon-arrived.cycstate"
        tcp("save_state", path=str(arrived))
        fixture(arrived, out / "mascon-death.cycstate", {PLAYER_IS_DEAD: 1})
        tcp("load_state", path=str(out / "mascon-death.cycstate"))
        die_and_respawn(CONFLATE)
        print("ok: death after travel respawns at Conflate")

        # 5. A state saved mid-conversation resumes it, line and all.
        tcp("load_state", path=str(asking))
        assert line() == TO_GURU + MASCON
        shot_line("resumed")
        answer("a", DEPART)
        close_box()
        until("Mascon's temple", lambda: in_temple(MASCON))
        print("ok: a state saved while asking about Mascon resumes and travels there")

        visited = (out / "game" / "faxanadu_gurus.txt").read_text().split()
        assert visited == ["Eolis", "Apolune", "Mascon", "Conflate"], visited
        print("ok: faxanadu_gurus.txt unchanged:", ", ".join(visited))
        print("screenshots:", ", ".join(shots))
    except BaseException:
        shot("failure")
        raise
    finally:
        close()


if __name__ == "__main__":
    sys.exit(main())
