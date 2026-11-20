"""Play death keeping gold and experience end to end in a hidden window.

    python3 tests/death/death_keeps_gold.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/death

The game runs uncapped and the script waits on the game's own state, never on
the clock, and takes a free TCP port, so any number can run in parallel.

Copies the build to OUT, starts a new game over TCP, then edits save-state RAM
to make a hero (an external prerequisite, not coverage): a Soldier (title 3)
who has earned the next title without seeing a Guru yet, with 5000 experience,
12345 gold, the Mattock bit, Wing Boots running, and Mascon as the furthest
temple. Then he dies:

1. With the feature on, he comes back at Mascon's Guru with the same gold,
   experience and earned title. The rest of death is the game's: full HP and
   MP, the Mattock bit and the Wing Boots gone.
2. His mantra, the remaster's way: --password imports it into the first
   empty save file, with his title's gold and experience, and continuing that
   file from the file select starts him with them, as continuing a mantra
   always has. Continuing goes through Player_SetInitialExpAndGold as the
   mantra screen's END does, and this feature lets that call through.
3. With the remaster off in the mod selection, the same death resets them to
   his title's, as the original does.
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
EXPERIENCE, GOLD, PLAYER_TITLE, NEXT_PLAYER_TITLE = 0x390, 0x392, 0x437, 0x4ED
HP, MP, QUESTS, DURATION_WING_BOOTS = 0x431, 0x39A, 0x42D, 0x429
TEMPLE_SPAWN_POINT, PLAYER_IS_DEAD, MESSAGE_ID, TEXTBOX_PAUSED = 0x439, 0x438, 0x213, 0x215
AREA, SCREEN = 0x24, 0x63
MATTOCK_BARRIER = 0x10
BUILDINGS, MASCON, MASCON_TEMPLE = 4, 3, 0x1E
NEGATIVE_THOUGHTS = 0x24                                            # the death message
PLAYER_TITLE_EXP_NEEDED, PLAYER_TITLE_GOLD = 0xF749, 0xF767         # fixed bank, a word per title
SOLDIER = 3
HERO = dict(experience=5000, gold=12345)
PACKAGE, FEATURE = "faxanadu.remastered", "remastered"          # the one switch: off is the original game


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


def in_mascon_temple():
    return (ram(AREA), ram(SCREEN)) == (BUILDINGS, MASCON_TEMPLE)


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


def title_start(image, title):
    """Player_SetInitialExpAndGold's experience and gold for a title."""
    if title == 0:
        return 0, 0
    word = lambda table: int.from_bytes(image[16 + 15 * 0x4000 + table - 0xC000 + 2 * (title - 1):][:2], "little")
    return word(PLAYER_TITLE_EXP_NEEDED), word(PLAYER_TITLE_GOLD)


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
    stock_exp, stock_gold = title_start(image, SOLDIER)
    assert (stock_exp, stock_gold) == (3500, 1200), (stock_exp, stock_gold)
    assert (stock_exp, stock_gold) != (HERO["experience"], HERO["gold"])
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

    def launch(*extra):
        nonlocal game
        extra = [part for arg in args.game_arg for part in arg.split("=", 1)] + list(extra)
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

    def die_and_respawn():
        """Page through the death message until the hero is back in Mascon's temple."""
        until("the death message", lambda: ram(MESSAGE_ID) == NEGATIVE_THOUGHTS, 600)
        for _ in range(40):
            if in_mascon_temple():
                break
            if typed():
                act("a")
            else:
                wait(10)
        until("Mascon's temple", in_mascon_temple)
        wait(60)

    def hero():
        return dict(experience=number(EXPERIENCE, 2), gold=number(GOLD, 3), title=ram(PLAYER_TITLE),
                    next_title=ram(NEXT_PLAYER_TITLE))

    try:
        launch()
        until("the title", lambda: frame() > 700)
        tcp("pad", buttons="start", frames=10)
        until("the Eolis gate", lambda: frame() > 1900)
        assert tcp("death_state")["enabled"], "the feature is on by default"
        gate = out / "gate.cycstate"
        tcp("save_state", path=str(gate))
        dying = out / "dying.cycstate"
        fixture(gate, dying, {
            PLAYER_TITLE: SOLDIER, NEXT_PLAYER_TITLE: SOLDIER + 1,
            EXPERIENCE: HERO["experience"] & 0xFF, EXPERIENCE + 1: HERO["experience"] >> 8,
            GOLD: HERO["gold"] & 0xFF, GOLD + 1: HERO["gold"] >> 8 & 0xFF, GOLD + 2: HERO["gold"] >> 16,
            QUESTS: MATTOCK_BARRIER, DURATION_WING_BOOTS: 0x40, HP: 0x10, MP: 0x08,
            TEMPLE_SPAWN_POINT: MASCON, PLAYER_IS_DEAD: 1})

        # 1. Death keeps gold, experience and the earned title; the rest is the game's.
        tcp("load_state", path=str(dying))
        die_and_respawn()
        shot("respawned")
        assert hero() == dict(HERO, title=SOLDIER, next_title=SOLDIER + 1), hero()
        assert (ram(HP), ram(MP)) == (0x50, 0x50), "full HP and MP"
        assert ram(QUESTS) & MATTOCK_BARRIER == 0, "the Mattock bit is cleared"
        assert ram(DURATION_WING_BOOTS) == 0xFF, "the timed items are cleared"
        assert ram(TEMPLE_SPAWN_POINT) == MASCON
        state = tcp("death_state")
        assert (state["kept"], state["passed"]) == (1, 0), state
        print(f"ok: died with {HERO['gold']} gold and {HERO['experience']} experience, "
              "came back at Mascon's Guru with both and the earned title")
        print("ok: full HP and MP, the Mattock bit and Wing Boots cleared, as before")
        mantra = tcp("mantra_now")["mantra"]
        close()

        # 2. His mantra, imported into a save file and continued, still starts
        # him with his title's gold and experience.
        launch("--password", mantra)
        until("the file select", lambda: tcp("files_state")["title"], 2000)
        files = tcp("files_state")
        imported = [i + 1 for i, s in enumerate(files["slots"]) if s["status"] == 0 and
                    (s["title"], s["gold"], s["experience"]) == (SOLDIER, stock_gold, stock_exp)]
        assert files["imports"] == 1 and len(imported) == 1, f"one file holds the mantra's hero: {files}"
        while tcp("files_state")["cursor"] != imported[0] - 1:
            act("down")
        shot("imported-file")
        tcp("pad", buttons="start", frames=10)
        until(f"file {imported[0]} in play", lambda: tcp("files_state")["file"] == imported[0], 300)
        until("Mascon's temple", in_mascon_temple)
        wait(60)
        shot("continued")
        assert hero() == dict(experience=stock_exp, gold=stock_gold, title=SOLDIER, next_title=SOLDIER), hero()
        state = tcp("death_state")
        assert state["enabled"] and (state["kept"], state["passed"]) == (0, 1), state
        print(f"ok: his mantra, imported into file {imported[0]} and continued, still starts a Soldier with "
              f"{stock_gold} gold and {stock_exp} experience")
        close()

        # 3. With the remaster off, the same death resets them, as the original does.
        (out / "game" / "mods" / "state.toml").write_text(
            f'format_version = 1\n\n[[feature]]\npackage_id = "{PACKAGE}"\nid = "{FEATURE}"\nenabled = false\n')
        launch()
        assert not tcp("death_state")["enabled"], "the remaster is off"
        tcp("load_state", path=str(dying))
        die_and_respawn()
        shot("respawned-off")
        assert hero() == dict(experience=stock_exp, gold=stock_gold, title=SOLDIER, next_title=SOLDIER), hero()
        print(f"ok: with the remaster off, the same death leaves {stock_gold} gold and {stock_exp} experience, "
              "the title's, as the original does")
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
