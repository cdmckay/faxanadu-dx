"""Play the Pendant fix end to end in a hidden window.

    python3 tests/pendant/pendant_fix.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/pendant

The game runs uncapped and the script waits on the game's own state, never on
the clock, and takes a free TCP port, so any number can run in parallel.

Copies the build to OUT and starts a new game over TCP. To skip the walk-in
(an external prerequisite, not coverage), a save-state RAM fixture drops the
intro's trigger and stands the hero at Eolis's gate. He goes in, walks left
out of town to the first enemies, two Zozura on Eolis screen 4, and the game
is saved there. Each case then edits that save's RAM: the weapon in hand, the
Pendant bit of SpecialItems, the Glove running or not, full HP, and the
nearer Zozura's HP. The hero stands still and swings as it crawls in, and the
case reads the HP the first hit took. Expected damage comes from the ROM's own
tables (WEAPON_STRENGTHS, WEAPON_TO_GLOVE_STRENGTH). The Hand Dagger is left
out: the stock dagger passes over an enemy this low.

1. With the feature on (the default), a weapon without the Pendant does its
   base strength, and with it a quarter more, which is the original's damage
   without it. The Glove adds its bonus on top, either way.
2. The same Zozura with its own 9 HP: with the Pendant one Long Sword hit (10)
   kills it; without, the hit (8) leaves it 1 HP. Screenshots of both.
3. With the remaster off in the mod selection, the original's reversed bonus:
   a quarter more without the Pendant, the base strength with it.
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
AREA, SCREEN, HERO_X = 0x24, 0x63, 0x9E
PLAYER_FLAGS, ATTACKING = 0xA4, 0x80
SPRITE_X, SPRITE_ENTITY, SPRITE_HP = 0xBA, 0x2CC, 0x344             # CurrentSprites_*, 8 slots each
SELECTED_WEAPON, PLAYER_CUR_WEAPON = 0x3BD, 0x3C8
DURATION_GLOVE, SPECIAL_ITEMS, HP = 0x428, 0x42C, 0x431
BIT_PENDANT, GLOVE_RUNNING, NO_GLOVE = 0x02, 0x40, 0xFF
EOLIS, GATE, TOWN, ZOZURA_SCREEN = 0, 0, 1, 4
GATE_DOOR_X = 195                                                   # EOLIS_DOOR_LOCATIONS: screen 0, $9C
INTRO_TRIGGER, ZOZURA = 0x4F, 0x47                                  # SPRITE_TRIGGER_INTRO, SPRITE_ENEMY_ZOZURA
TARGET, OTHER = 7, 6                                                # the nearer Zozura's slot, and the other's
REACH = 34                                                          # swing when it is this close (swords)
WEAPON_STRENGTHS, WEAPON_TO_GLOVE_STRENGTH = 0xB7A5, 0x88C7         # bank 14
SPRITE_ENTITIES_HP = 0xB5A9                                         # bank 14, by entity
LONG_SWORD, GIANT_BLADE, DRAGON_SLAYER = 1, 2, 3
NAMES = {LONG_SWORD: "Long Sword", GIANT_BLADE: "Giant Blade", DRAGON_SLAYER: "Dragon Slayer"}
KNOWN_HP = 60
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


def until(what, test, frames=1200):
    end = frame() + frames
    while frame() < end:
        if test():
            return
        time.sleep(0.005)
    raise AssertionError(f"timed out waiting for {what}")


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


def bank14(image, addr, n):
    start = 16 + 14 * 0x4000 + addr - 0x8000
    return image[start:start + n]


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
    strength = list(bank14(image, WEAPON_STRENGTHS, 4))
    glove = list(bank14(image, WEAPON_TO_GLOVE_STRENGTH, 4))
    assert (strength, glove) == ([4, 8, 12, 16], [2, 4, 8, 10]), (strength, glove)
    zozura_hp = bank14(image, SPRITE_ENTITIES_HP + ZOZURA, 1)[0]
    assert zozura_hp == 9, zozura_hp
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

    def hit(zozura, weapon, pendant, gloved=False, enemy_hp=KNOWN_HP, name=None):
        """Load the Zozura save with this kit, swing as the nearer one crawls in,
        and return the HP the first hit took (None if it died)."""
        case = out / "case.cycstate"
        fixture(zozura, case, {
            PLAYER_CUR_WEAPON: weapon, SELECTED_WEAPON: weapon, HP: 0x50,
            SPECIAL_ITEMS: BIT_PENDANT if pendant else 0,
            DURATION_GLOVE: GLOVE_RUNNING if gloved else NO_GLOVE,
            SPRITE_HP + TARGET: enemy_hp})
        tcp("load_state", path=str(case))
        hero = ram(HERO_X)
        assert ram(SCREEN) == ZOZURA_SCREEN and ram(SPRITE_ENTITY + TARGET) == ZOZURA
        for _ in range(600):
            entity, hp = ram(SPRITE_ENTITY + TARGET), ram(SPRITE_HP + TARGET)
            if entity != ZOZURA or hp != enemy_hp:
                break
            if hero - ram(SPRITE_X + TARGET) <= REACH and not ram(PLAYER_FLAGS) & ATTACKING:
                tcp("action", name="b", frames=2)
        else:
            raise AssertionError("the hero never hit the Zozura")
        assert ram(SCREEN) == ZOZURA_SCREEN, "the Zozura reached the hero first and knocked him off the screen"
        if name:
            wait(4)
            shot(name)
        return None if entity != ZOZURA else enemy_hp - hp

    def expected(weapon, pendant, gloved, fixed):
        bonus = pendant if fixed else not pendant
        return strength[weapon] + (strength[weapon] // 4 if bonus else 0) + (glove[weapon] if gloved else 0)

    try:
        launch()
        until("the title", lambda: frame() > 700)
        tcp("pad", buttons="start", frames=10)
        until("the Eolis gate", lambda: frame() > 1900)
        assert tcp("pendant_state")["enabled"], "the feature is on by default"
        assert (ram(AREA), ram(SCREEN), ram(SPRITE_ENTITY + 7)) == (EOLIS, GATE, INTRO_TRIGGER)
        gate = out / "gate.cycstate"
        tcp("save_state", path=str(gate))
        door = out / "door.cycstate"
        fixture(gate, door, {SPRITE_ENTITY + 7: 0xFF, HERO_X: GATE_DOOR_X})
        tcp("load_state", path=str(door))
        wait(10)
        tcp("action", name="up", frames=6)
        until("the town", lambda: ram(SCREEN) == TOWN, 600)
        wait(30)
        for _ in range(60):
            if ram(SCREEN) == ZOZURA_SCREEN:
                break
            tcp("action", name="left", frames=30)
            wait(31)
        until("the Zozura", lambda: ram(SCREEN) == ZOZURA_SCREEN and
              (ram(SPRITE_ENTITY + OTHER), ram(SPRITE_ENTITY + TARGET)) == (ZOZURA, ZOZURA), 300)
        assert ram(SPRITE_X + TARGET) > ram(SPRITE_X + OTHER), "the target is the nearer Zozura"
        assert ram(SPRITE_HP + TARGET) == zozura_hp, "a Zozura's HP from SPRITE_ENTITIES_HP"
        zozura = out / "zozura.cycstate"
        tcp("save_state", path=str(zozura))

        # 1. The literal fix: base strength, a quarter more only with the Pendant, then the Glove.
        for weapon in (LONG_SWORD, GIANT_BLADE, DRAGON_SLAYER):
            for gloved in (False, True):
                for pendant in (False, True):
                    before = tcp("pendant_state")["hits"]
                    lost = hit(zozura, weapon, pendant, gloved)
                    want = expected(weapon, pendant, gloved, fixed=True)
                    state = tcp("pendant_state")
                    kit = f"{NAMES[weapon]}{', Pendant' if pendant else ''}{', Glove' if gloved else ''}"
                    assert lost == want, f"{kit}: took {lost} HP, expected {want}"
                    assert state["hits"] == before + 1, state
                    assert (state["weapon"], state["pendant"]) == (weapon, pendant), state
                    assert state["damage"] == want - (glove[weapon] if gloved else 0), state
                    print(f"ok: {kit}: a hit takes {lost} HP")
        print(f"ok: with the Pendant each weapon does the original's damage without it "
              f"({', '.join(str(expected(w, False, False, False)) for w in NAMES)}); "
              f"without it, its base strength ({', '.join(str(strength[w]) for w in NAMES)})")

        # 2. The same Zozura with its own HP: the Pendant's Long Sword kills it in one hit.
        assert hit(zozura, LONG_SWORD, True, enemy_hp=zozura_hp, name="pendant-kills") is None, "one hit kills"
        assert hit(zozura, LONG_SWORD, False, enemy_hp=zozura_hp, name="no-pendant-survives") == 8
        print("ok: a Zozura (9 HP) dies to one Long Sword hit with the Pendant, and is left 1 HP without it")
        close()

        # 3. With the remaster off, the original's reversed bonus.
        (out / "game" / "mods" / "state.toml").write_text(
            f'format_version = 1\n\n[[feature]]\npackage_id = "{PACKAGE}"\nid = "{FEATURE}"\nenabled = false\n')
        launch()
        assert not tcp("pendant_state")["enabled"], "the remaster is off"
        for weapon in (LONG_SWORD, DRAGON_SLAYER):
            for pendant in (False, True):
                lost = hit(zozura, weapon, pendant)
                want = expected(weapon, pendant, False, fixed=False)
                assert lost == want, f"off, {NAMES[weapon]}, pendant={pendant}: took {lost}, expected {want}"
        assert tcp("pendant_state")["hits"] == 0, "the hook is off"
        assert hit(zozura, LONG_SWORD, False, enemy_hp=zozura_hp, name="original-no-pendant-kills") is None
        print("ok: with the remaster off, the original's bug: a quarter more without the Pendant, base strength with it")
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
