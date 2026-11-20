"""Play damage numbers end to end in a hidden window.

    python3 tests/damage/damage_numbers.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/damage
    python3 tests/damage/damage_numbers.py ... --game-arg=--world-view=seamless

The game runs uncapped and the script counts the game's frames, never the
clock, on a reserved port (tests/e2e/fax_e2e.py), so any number can run in
parallel.

Starts a new game once. To skip the walk-in (an external prerequisite, not
coverage), a save-state RAM fixture drops the intro's trigger and stands the
hero at Eolis's gate. He goes in and walks left out of town to two Zozura
(9 HP, 7 of damage) crawling along the ground on Eolis screen 4, and the game
is saved there. Every case starts from that save, its RAM edited: the Long
Sword in hand (8 a hit with the Pendant fix and no Pendant), Leather Armor,
full HP, no knockback or iframes, and where he and the Zozura stand, crawling
toward him: with Seamless on, the room's actors move before he arrives, so the
walk-in leaves them anywhere.

1. Swinging at both Zozura, 4 pixels apart: the nearer one, left 1 HP, dies,
   and its number is still the hit's 8; the other keeps 1 of its 9 and shows
   8 too. Both show at once without touching (the older moved out of the
   newer's way). Each starts centered over the box the weapon checks, 8 above
   its top, where RAM says the Zozura stood (the survivor's hit counter still
   runs, so it has not moved). White, the light color.
2. A number rises a pixel every 3 frames in the same column, holds while the
   game is paused, and is gone after 60 frames.
3. The Zozura reaches the hero: his HP drops by its 7 and a pink 7 rises over
   him, from where he stood when it touched him.
4. Deluge through the Magic button (C): the Zozura loses 6 (MAGIC_DAMAGE), and
   a 6 shows over it.
5. Under Ointment the Zozura crawls into him: no HP, no number.
6. With the remaster off: the same kill, and no number.

The pictures are taken with the state, at one frame boundary (batch): every
number is drawn at its room's place in the picture plus its own (the screen's
in the original view; with Seamless, its room's, from Seamless's camera,
fax_world and fax_world_room), and its pixels there are the HUD's digit tiles
from the ROM (bank 10, tiles $30-$39, color 3) in one ink, outlined in black.
"""
from pathlib import Path
import struct
import sys
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
from fax_e2e import Game, act, batch, chunks, fixture, frame, parse_args, ram, tcp, until, wait  # noqa: E402

AREA, SCREEN, POS_X, POS_Y, PLAYER_FLAGS, STATUS, IFRAMES, ATTACKING = 0x24, 0x63, 0x9E, 0xA1, 0xA4, 0xA5, 0xAD, 0x80
SPRITE_X, SPRITE_Y, ENTITIES, SPRITE_FLAGS, SPRITE_HP, HIT_COUNTER = 0xBA, 0xC2, 0x2CC, 0x2DC, 0x344, 0x34C   # 8 slots
PAUSED, MP, SELECTED_WEAPON, SELECTED_ARMOR, SELECTED_MAGIC = 0x120, 0x39A, 0x3BD, 0x3BE, 0x3C0
PLAYER_CUR_WEAPON, DURATION_OINTMENT, DURATION_GLOVE, SPECIAL_ITEMS, HP = 0x3C8, 0x427, 0x428, 0x42C, 0x431
EOLIS, TOWN, ZOZURA_SCREEN, GATE_DOOR_X = 0, 1, 4, 195
INTRO_TRIGGER, ZOZURA = 0x4F, 0x47
NEAR, FAR = 7, 6                                    # the Zozura's slots: the nearer, the one behind it
HERO_AT, NEAR_AT, FAR_AT, GROUND = 240, 150, 70, 144   # where each case puts them (the Zozura 16 lower)
FACING_RIGHT = 0x01                                 # CurrentSprites_Flags
LONG_SWORD, DELUGE = 1, 0
REACH = 34                                          # swing when the nearer one is this close
STEP, LIFE, ABOVE, GLYPH = 3, 60, 8, 7              # fax_damage.h and .c
HUD, FULL = 32, 0x50
PACKAGE, FEATURE = "faxanadu.remastered", "remastered"


def png(path):
    """A screenshot's pixels: (bytes a pixel, rows of bytes). The game writes
    8-bit RGB or RGBA with no filter (cyc_png.c)."""
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
    raw, stride = zlib.decompress(idat), width * bpp + 1
    assert all(raw[y * stride] == 0 for y in range(height)), f"{path.name}: a row with a filter"
    return bpp, [raw[y * stride + 1:(y + 1) * stride] for y in range(height)]


def main():
    args = parse_args(__doc__)
    seamless = any("seamless" in a for a in args.game_arg)
    game = Game.from_args(args)
    out, rom = game.out, game.image[16:]

    def bank(n, addr, length=1):
        start = n * 0x4000 + addr - 0x8000
        return rom[start:start + length]

    zozura_hp = bank(14, 0xB5A9 + ZOZURA)[0]                   # SPRITE_ENTITIES_HP
    zozura_damage = bank(14, 0xB6D7 + ZOZURA)[0]               # SPRITE_ENTITY_DAMAGES
    extents = list(bank(14, 0xB407 + ZOZURA * 2, 2))           # MAYBE_SPRITE_EXTENTS
    sword = bank(14, 0xB7A5 + LONG_SWORD)[0]                   # WEAPON_STRENGTHS: the base, without the Pendant
    deluge = bank(14, 0xB7A0 + DELUGE)[0]                      # MAGIC_DAMAGE
    digits = [bank(10, 0x8140 + (0x30 + d) * 16, 16) for d in range(10)]   # HUD_TILES_START, tiles $30-$39
    assert (zozura_hp, zozura_damage, extents, sword, deluge) == (9, 7, [16, 16], 8, 6), \
        (zozura_hp, zozura_damage, extents, sword, deluge)

    def state():
        return tcp("damage_state")

    def watch():
        """RAM and the state, at one frame boundary."""
        memory, s = batch(("read_ram", dict(addr=0, len=0x440)), ("damage_state", {}))
        return bytes.fromhex(memory["hex"]), s

    def mask(amount):
        """The number's pixels as fax_damage.c draws them, from its first
        glyph's top-left less one: 2 ink, 1 outline."""
        text = str(amount)
        m = [[0] * (8 * len(text) + 2) for _ in range(10)]
        for d, ch in enumerate(text):
            t = digits[int(ch)]
            for r in range(8):
                for b in range(8):
                    if t[r] >> (7 - b) & t[r + 8] >> (7 - b) & 1:
                        m[1 + r][1 + 8 * d + b] = 2
        for r in range(10):
            for b in range(len(m[0])):
                if m[r][b] == 2:
                    for rr in range(max(r - 1, 0), min(r + 2, 10)):
                        for bb in range(max(b - 1, 0), min(b + 2, len(m[0]))):
                            m[rr][bb] = m[rr][bb] or 1
        return m

    def look(name):
        """A picture and the state at one frame boundary. Every number showing
        must be drawn at its room's place plus its own, in the HUD's digits.
        Returns the state."""
        path = out / f"{len(game.shots):02d}-{name}.png"
        cmds = [("screenshot", dict(layer="picture", path=str(path))), ("damage_state", {})]
        if seamless:
            cmds += [("fax_world", {}), ("fax_world_room", dict(room=ZOZURA_SCREEN))]
        replies = batch(*cmds)
        game.shots.append(path.name)
        s = replies[1]
        bpp, rows = png(path)
        width = len(rows[0]) // bpp
        if seamless:
            camera, xy = replies[2]["camera"], replies[3]["xy"]
            origin = (xy[0] * 256 - camera[0], HUD + xy[1] * 208 - camera[1])
        else:
            origin = ((width - 256) // 2, HUD)
        for n in s["numbers"]:
            assert n["room"] == ZOZURA_SCREEN, n
            assert n["drawn"] == [origin[0] + n["x"], origin[1] + n["y"]], (n, origin)
            if n["alpha"] < 255:
                continue
            ink, edge, (x0, y0) = set(), set(), n["drawn"]
            for r, row in enumerate(mask(n["amount"])):
                for b, v in enumerate(row):
                    x, y = x0 - 1 + b, y0 - 1 + r
                    if v and 0 <= x < width and HUD <= y < len(rows):
                        (ink if v == 2 else edge).add(bytes(rows[y][x * bpp:x * bpp + 3]))
            assert len(ink) == 1 and len(edge) == 1, f"{path.name}: {n['amount']} at {n['drawn']}: {ink} {edge}"
            (red, green, blue), = ink
            assert max(edge.pop()) < 40, "outlined in black"
            if n["kind"] == "dealt":
                assert min(red, green, blue) > 180, f"damage dealt is light: {(red, green, blue)}"
            else:
                assert red > 180 and green < red - 60, f"damage taken is pink: {(red, green, blue)}"
        return s

    def apart(a, b):
        """Two numbers' outlined boxes neither touch nor overlap."""
        ax, at, aw = a["x"] - 1, a["y"] - 1, a["w"] + 2
        bx, bt, bw = b["x"] - 1, b["y"] - 1, b["w"] + 2
        return ax >= bx + bw or bx >= ax + aw or at >= bt + GLYPH + 2 or bt >= at + GLYPH + 2

    def saved(path, addr):
        data = path.read_bytes()
        return data[next(p for t, p, n in chunks(data) if t == b"HW  ") + 23 + addr]

    def case(name, writes):
        path = out / f"{name}.cycstate"
        zozura_at = {}
        for slot, x in ((NEAR, NEAR_AT), (FAR, FAR_AT)):
            zozura_at.update({SPRITE_X + slot: x, SPRITE_Y + slot: GROUND + 16, HIT_COUNTER + slot: 0,
                              SPRITE_FLAGS + slot: saved(zozura, SPRITE_FLAGS + slot) | FACING_RIGHT})
        fixture(zozura, path, {POS_X: HERO_AT, POS_Y: GROUND, PLAYER_FLAGS: 0, STATUS: 0, IFRAMES: 0,
                               PLAYER_CUR_WEAPON: LONG_SWORD, SELECTED_WEAPON: LONG_SWORD, HP: FULL,
                               SELECTED_ARMOR: 0, SPECIAL_ITEMS: 0, DURATION_GLOVE: 0xFF,
                               DURATION_OINTMENT: 0xFF, **zozura_at, **writes})
        tcp("load_state", path=str(path))
        assert ram(SCREEN) == ZOZURA_SCREEN

    def swing_at_both():
        """Swing as the Zozura come, until both have been hit. RAM and the
        state, read together right after the second hit."""
        before = state()["weapon"]
        for _ in range(900):
            memory, s = watch()
            if memory[ENTITIES + NEAR] != ZOZURA and memory[SPRITE_HP + FAR] != zozura_hp:
                assert s["weapon"] - before == 2, s
                return memory, s
            if memory[POS_X] - memory[SPRITE_X + NEAR] <= REACH and not memory[PLAYER_FLAGS] & ATTACKING:
                tcp("action", name="b", frames=2)
        raise AssertionError("the swings never hit both Zozura")

    try:
        game.launch()
        assert state()["enabled"], "on by default"
        until("the title", lambda: frame() > 700)
        tcp("pad", buttons="start", frames=10)
        until("the Eolis gate", lambda: frame() > 1900)
        assert (ram(AREA), ram(SCREEN), ram(ENTITIES + 7)) == (EOLIS, 0, INTRO_TRIGGER)
        gate, door = out / "gate.cycstate", out / "door.cycstate"
        tcp("save_state", path=str(gate))
        fixture(gate, door, {ENTITIES + 7: 0xFF, POS_X: GATE_DOOR_X})
        tcp("load_state", path=str(door))
        wait(10)
        act("up", 6, 10)
        until("the town", lambda: ram(SCREEN) == TOWN, 600)
        wait(30)
        for _ in range(60):
            if ram(SCREEN) == ZOZURA_SCREEN:
                break
            act("left", 30, 1)
        until("the Zozura", lambda: ram(SCREEN) == ZOZURA_SCREEN and
              (ram(ENTITIES + FAR), ram(ENTITIES + NEAR)) == (ZOZURA, ZOZURA), 300)
        until("the screen to settle", lambda: ram(0x0E) == 0 and ram(0x54) == 0xFF and ram(0x13) != 0, 300)
        zozura = out / "arrived.cycstate"
        tcp("save_state", path=str(zozura))
        # The cases' base: everyone in place, and no number left from the walk-in.
        case("placed", {})
        until("no numbers", lambda: state()["live"] == 0, LIFE + 10)
        assert ram(SPRITE_X + NEAR) < HERO_AT - REACH, "the Zozura are still far off"
        zozura = out / "zozura.cycstate"
        tcp("save_state", path=str(zozura))
        print(f"ok: on Eolis screen 4 with two Zozura (the hero at x {ram(POS_X)}, them at "
              f"{ram(SPRITE_X + NEAR)} and {ram(SPRITE_X + FAR)})")

        # 1. Both Zozura: a kill (1 HP left) and a hit, 8 each, shown at once and apart.
        case("both", {SPRITE_X + FAR: NEAR_AT - 4, SPRITE_HP + NEAR: 1})
        memory, s = swing_at_both()
        assert memory[ENTITIES + FAR] == ZOZURA and memory[SPRITE_HP + FAR] == zozura_hp - sword, memory[SPRITE_HP + FAR]
        assert memory[HIT_COUNTER + FAR] > 0, "the survivor is still frozen by its hit: it has not moved since"
        dealt = [n for n in s["numbers"] if n["kind"] == "dealt"]
        assert [n["amount"] for n in dealt] == [sword, sword], s
        place = [memory[SPRITE_X + FAR], memory[SPRITE_Y + FAR]]
        survivor = next(n for n in dealt if n["target"][:2] == place)
        killed = next(n for n in dealt if n is not survivor)
        assert survivor["target"] == place + extents, (survivor, place)
        for n in dealt:
            tx, ty, tw, th = n["target"]
            assert n["x"] == tx + tw // 2 - n["w"] // 2, n                       # centered over the box
            assert n["y"] == ty - ABOVE - n["lift"] - n["age"] // STEP, n       # risen from 8 above its top
        assert killed["target"][1] == place[1] and abs(killed["target"][0] - place[0]) <= 8, (killed, place)
        assert apart(*dealt) and s["crowded"] >= 1 and dealt[0]["lift"] != 0, s   # the older one moved
        s = look("kill-and-hit")
        assert len(s["numbers"]) == 2 and all(n["drawn"] for n in s["numbers"]), s
        print(f"ok: a kill of a Zozura left 1 HP shows the hit's whole {sword}, the other's hit {sword} too; each "
              "centered over its box, both shown at once and apart, in the HUD's digits")

        # 2. The float: a pixel every 3 frames, in the same column; held while paused; gone after 60.
        first = s["numbers"][-1]
        wait(12)
        later = look("rising")
        moved = next(n for n in later["numbers"] if n["target"] == first["target"])
        assert moved["x"] == first["x"] and moved["age"] > first["age"], (first, moved)
        assert first["y"] - moved["y"] == moved["age"] // STEP - first["age"] // STEP, (first, moved)
        act("start", 6, 4)
        until("the pause", lambda: ram(PAUSED) != 0, 60)   # 1, then $81 once the sound is muted
        held = [n["age"] for n in state()["numbers"]]
        wait(30)
        assert [n["age"] for n in state()["numbers"]] == held and ram(PAUSED) != 0, "held while paused"
        act("start", 6, 4)
        until("the game to go on", lambda: ram(PAUSED) == 0, 60)
        until("the numbers to go", lambda: state()["live"] == 0, LIFE + 10)
        print(f"ok: a number rises 1 pixel every {STEP} frames ({first['y']} to {moved['y']} from age "
              f"{first['age']} to {moved['age']}), holds while paused, and is gone after {LIFE} frames")

        # 3. The Zozura reaches the hero: pink, his HP's loss, over him.
        case("hurt", {POS_X: 192})
        before, hero = state()["hero"], None
        for _ in range(600):
            memory, s = watch()
            if memory[HP] != FULL:
                break
            hero = [memory[POS_X], memory[POS_Y]]                # he stands still until it touches him
        else:
            raise AssertionError("the Zozura never reached the hero")
        taken = [n for n in s["numbers"] if n["kind"] == "taken"]
        assert s["hero"] - before == 1 and len(taken) == 1, s
        assert taken[0]["amount"] == FULL - memory[HP] == zozura_damage, (taken, memory[HP])
        assert taken[0]["target"] == hero + [16, 32] and taken[0]["x"] == hero[0] + 8 - 3, (taken, hero)
        look("hero-hit")
        print(f"ok: the Zozura's touch takes {zozura_damage} HP, and a pink {zozura_damage} rises over the hero "
              f"from where he stood ({hero[0]}, {hero[1]})")

        # 4. Deluge, through the Magic button.
        case("deluge", {SELECTED_MAGIC: DELUGE, MP: 80, SPRITE_HP + NEAR: 60})
        before = state()["magic"]
        until("the Zozura to come", lambda: ram(POS_X) - ram(SPRITE_X + NEAR) <= 72, 600)
        tcp("key", name="C", frames=3)
        until("the spell's hit", lambda: ram(SPRITE_HP + NEAR) != 60, 300)
        memory, s = watch()
        dealt = [n for n in s["numbers"] if n["kind"] == "dealt"]
        assert s["magic"] - before == 1 and [n["amount"] for n in dealt] == [deluge], s
        assert 60 - memory[SPRITE_HP + NEAR] == deluge, memory[SPRITE_HP + NEAR]
        look("deluge")
        print(f"ok: Deluge from the Magic button takes {deluge} and shows {deluge}")

        # 5. Ointment: no HP, no number.
        case("ointment", {POS_X: 192, DURATION_OINTMENT: 0x40})
        before = state()["hero"]
        until("the Zozura to reach him", lambda: abs(ram(SPRITE_X + NEAR) - ram(POS_X)) < 15, 600)
        wait(60)
        s = state()
        assert ram(HP) == FULL and s["hero"] == before and s["live"] == 0, s
        print("ok: under Ointment the Zozura's touch takes nothing, and nothing shows")
        game.close()

        # 6. The remaster off: the same kill, no number.
        game.set_features({(PACKAGE, FEATURE): False})
        game.launch()
        assert not state()["enabled"]
        case("off", {SPRITE_HP + NEAR: 1})
        for _ in range(600):
            if ram(ENTITIES + NEAR) != ZOZURA:
                break
            if ram(POS_X) - ram(SPRITE_X + NEAR) <= REACH and not ram(PLAYER_FLAGS) & ATTACKING:
                tcp("action", name="b", frames=2)
        else:
            raise AssertionError("the hero never killed the Zozura")
        s = look("off")
        assert s["live"] == 0 and s["weapon"] == 0, s
        print("ok: with the remaster off, the kill shows no number")
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
