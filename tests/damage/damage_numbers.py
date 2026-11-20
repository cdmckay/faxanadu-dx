"""Play damage numbers end to end in a hidden window.

    python3 tests/damage/damage_numbers.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/damage
    python3 tests/damage/damage_numbers.py ... --game-arg=--world-view=seamless

The game runs uncapped and the script waits on the game's own state, never on
the clock, and takes a free TCP port, so any number can run in parallel.

Copies the build to OUT and starts a new game over TCP. To skip the walk-in
(an external prerequisite, not coverage), a save-state RAM fixture drops the
intro's trigger and stands the hero at Eolis's gate. He goes in and walks left
out of town to two Zozura (9 HP, 7 HP of damage) crawling along the ground on
Eolis screen 4, and the game is saved there. Each case edits that save's RAM:
the Long Sword in hand (8 a hit with the Pendant fix and no Pendant), full HP,
where he stands, the Zozura's HP and places.

1. One swing at both Zozura, 4 pixels apart: it kills the nearer one, left 1
   HP, and the number is still the hit's 8; the other keeps 1 HP of its 9 and
   shows 8 too. Both show at once without touching (the older moved out of
   the newer's way). Each starts centered over the box the weapon checks, 8
   above its top, where RAM says the Zozura stood (the survivor's hit counter
   still runs, so it has not moved). White, the light color.
2. A number rises a pixel every 3 frames and stays put across; it holds while
   the game is paused, and goes after 60 frames.
3. The Zozura reaches the hero: his HP drops by its 7 (Leather Armor) and a
   pink 7 rises over him, from where he stood when it touched him.
4. Deluge through the Magic button (C): the Zozura loses 6 (MAGIC_DAMAGE), and
   6 shows over it.
5. Under Ointment the Zozura crawls through him: no HP, no number.
6. With the remaster off: the same kill, and no numbers.

Each check also takes a picture, with the state, at one frame boundary: every
number is drawn at its room's place in the picture plus its own (the screen's
place in the original view; with Seamless, its room's, from Seamless's own
camera, fax_world and fax_world_room), and its pixels there are the HUD's digit
tiles from the ROM (bank 10, tiles $30-$39, color 3), in one ink, outlined in
black.
"""
import argparse
import json
from pathlib import Path
import socket
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "block"))
from harness import Game, fixture, POS_X, POS_Y, ENTITIES, SPRITE_X, SPRITE_Y, SPRITE_HP, HP, PLAYER_FLAGS  # noqa: E402
from block_shield import png  # noqa: E402

SCREEN, PAUSED, HIT_COUNTER, ATTACKING = 0x63, 0x120, 0x34C, 0x80
SELECTED_WEAPON, PLAYER_CUR_WEAPON, SELECTED_MAGIC, MP = 0x3BD, 0x3C8, 0x3C0, 0x39A
SELECTED_ARMOR, SPECIAL_ITEMS, DURATION_GLOVE, DURATION_OINTMENT = 0x3BE, 0x42C, 0x428, 0x427
EOLIS, TOWN, ZOZURA_SCREEN, GATE_DOOR_X = 0, 1, 4, 195
INTRO_TRIGGER, ZOZURA = 0x4F, 0x47
NEAR, FAR = 7, 6                                    # the Zozura's slots: the nearer, the one behind it
LONG_SWORD, DELUGE = 1, 0
REACH = 34                                          # swing when the nearer one is this close
STEP, LIFE, ABOVE, GLYPH = 3, 60, 8, 7              # fax_damage.h / .c
HUD = 32
PACKAGE, FEATURE = "faxanadu.remastered", "remastered"


class Line:
    """One TCP connection kept open, so commands sent together are served at
    one frame boundary (tests/block/block_shield.py)."""

    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), 5)
        self.sock.settimeout(30)
        self.replies = self.sock.makefile("rb")

    def batch(self, *cmds):
        self.sock.sendall(b"".join((json.dumps(dict(id=i, cmd=cmd, **kw)) + "\n").encode()
                                   for i, (cmd, kw) in enumerate(cmds)))
        replies = [json.loads(self.replies.readline()) for _ in cmds]
        for (cmd, _), reply in zip(cmds, replies):
            if not reply.get("ok"):
                raise RuntimeError(f"{cmd}: {reply}")
        return replies

    def close(self):
        self.sock.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for key in ("build", "rom", "out"):
        ap.add_argument("--" + key, type=Path, required=True)
    ap.add_argument("--game-arg", action="append", default=[],
                    help="extra game option, e.g. --game-arg=--world-view=seamless (repeatable)")
    ap.add_argument("--port", type=int, default=0, help="TCP port (default: a free one)")
    ap.add_argument("--realtime", action="store_true", help="run at normal speed instead of uncapped")
    args = ap.parse_args()
    seamless = any("seamless" in a for a in args.game_arg)
    rom = args.rom.read_bytes()[16:]

    def bank(n, addr, length=1):
        start = n * 0x4000 + addr - 0x8000
        return rom[start:start + length]

    zozura_hp = bank(14, 0xB5A9 + ZOZURA)[0]                   # SPRITE_ENTITIES_HP
    zozura_damage = bank(14, 0xB6D7 + ZOZURA)[0]               # SPRITE_ENTITY_DAMAGES
    extents = tuple(bank(14, 0xB407 + ZOZURA * 2, 2))          # MAYBE_SPRITE_EXTENTS
    sword = bank(14, 0xB7A5 + LONG_SWORD)[0]                   # WEAPON_STRENGTHS: the base, without the Pendant
    deluge = bank(14, 0xB7A0 + DELUGE)[0]                      # MAGIC_DAMAGE
    digits = [bank(10, 0x8140 + (0x30 + d) * 16, 16) for d in range(10)]   # the HUD's tiles
    assert (zozura_hp, zozura_damage, extents, sword, deluge) == (9, 7, (16, 16), 8, 6), \
        (zozura_hp, zozura_damage, extents, sword, deluge)
    g = Game(args.build, args.rom, args.out, args.game_arg, args.realtime, args.port)

    def state():
        return g.tcp("damage_state")

    def mask(amount):
        """The number's pixels as fax_damage.c draws them, from the first
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
                    for dr in (-1, 0, 1):
                        for db in (-1, 0, 1):
                            rr, bb = r + dr, b + db
                            if 0 <= rr < 10 and 0 <= bb < len(m[0]) and not m[rr][bb]:
                                m[rr][bb] = 1
        return m

    def look(name):
        """A picture and the state at one frame boundary. Checks every number
        showing: drawn at its room's place plus its own, in the HUD's digits.
        Returns the state."""
        path = g.out / f"{len(g.shots):02d}-{name}.png"
        cmds = [("screenshot", dict(layer="picture", path=str(path))), ("damage_state", {}),
                ("read_ram", dict(addr=SCREEN, len=1))]
        if seamless:
            cmds += [("fax_world", {}), ("fax_world_room", dict(room=ZOZURA_SCREEN))]
        line = Line(g.port)
        try:
            replies = line.batch(*cmds)
        finally:
            line.close()
        g.shots.append(path)
        s = replies[1]
        bpp, rows = png(path)
        width = len(rows[0]) // bpp
        if seamless:
            camera, xy = replies[3]["camera"], replies[4]["xy"]
            origin = (xy[0] * 256 - camera[0], HUD + xy[1] * 208 - camera[1])
        else:
            origin = ((width - 256) // 2, HUD)
        for n in s["numbers"]:
            assert n["room"] == ZOZURA_SCREEN, n
            assert n["drawn"] == [origin[0] + n["x"], origin[1] + n["y"]], (n, origin)
            if n["alpha"] < 255:
                continue
            m, (x0, y0) = mask(n["amount"]), n["drawn"]
            ink, edge = set(), set()
            for r, row in enumerate(m):
                for b, v in enumerate(row):
                    x, y = x0 - 1 + b, y0 - 1 + r
                    if v and 0 <= x < width and HUD <= y < len(rows):
                        (ink if v == 2 else edge).add(bytes(rows[y][x * bpp:x * bpp + 3]))
            assert len(ink) == 1 and len(edge) == 1, f"{path.name}: {n['amount']} at {n['drawn']}: {ink} {edge}"
            (r, gr, b), = ink
            assert max(edge.pop()) < 40, "outlined in black"
            if n["kind"] == "dealt":
                assert min(r, gr, b) > 180, f"damage dealt is light: {(r, gr, b)}"
            else:
                assert r > 180 and gr < r - 60, f"damage taken is pink: {(r, gr, b)}"
        return s

    def apart(a, b):
        """Two numbers' outlined boxes neither touch nor overlap."""
        ax, at, aw = a["x"] - 1, a["y"] - 1, a["w"] + 2
        bx, bt, bw = b["x"] - 1, b["y"] - 1, b["w"] + 2
        return ax >= bx + bw or bx >= ax + aw or at >= bt + GLYPH + 2 or bt >= at + GLYPH + 2

    def case(name, writes):
        path = g.out / f"{name}.cycstate"
        fixture(zozura, path, {PLAYER_CUR_WEAPON: LONG_SWORD, SELECTED_WEAPON: LONG_SWORD, HP: 0x50,
                               SELECTED_ARMOR: 0, SPECIAL_ITEMS: 0, DURATION_GLOVE: 0xFF,
                               DURATION_OINTMENT: 0xFF, **writes})
        g.load(path)
        assert g.ram(SCREEN) == ZOZURA_SCREEN
        return path

    def watch():
        """RAM and the state, read at one frame boundary."""
        line = Line(g.port)
        try:
            ram, s = line.batch(("read_ram", dict(addr=0, len=0x440)), ("damage_state", {}))
        finally:
            line.close()
        return bytes.fromhex(ram["hex"]), s

    def swing_at_both():
        """Swing as the Zozura come, until both have been hit. Returns RAM and
        the state, read together right after the second hit."""
        before = state()["weapon"]
        for _ in range(900):
            ram, s = watch()
            if ram[ENTITIES + NEAR] != ZOZURA and ram[SPRITE_HP + FAR] != zozura_hp:
                assert s["weapon"] - before == 2, s
                return ram, s
            if ram[POS_X] - ram[SPRITE_X + NEAR] <= REACH and not ram[PLAYER_FLAGS] & ATTACKING:
                g.tcp("action", name="b", frames=2)
        raise AssertionError("the swings never hit both Zozura")

    try:
        g.launch()
        assert state()["enabled"], "on by default"
        g.until("the title", lambda: g.frame() > 700)
        g.tcp("pad", buttons="start", frames=10)
        g.until("the Eolis gate", lambda: g.frame() > 1900)
        assert (g.ram(0x24), g.ram(SCREEN), g.ram(ENTITIES + 7)) == (EOLIS, 0, INTRO_TRIGGER)
        gate = g.save("gate")
        door = g.out / "door.cycstate"
        fixture(gate, door, {ENTITIES + 7: 0xFF, POS_X: GATE_DOOR_X})
        g.load(door)
        g.wait(10)
        g.act("up", 6, 10)
        g.until("the town", lambda: g.ram(SCREEN) == TOWN, 600)
        g.wait(30)
        for _ in range(60):
            if g.ram(SCREEN) == ZOZURA_SCREEN:
                break
            g.act("left", 30, 1)
        g.until("the Zozura", lambda: g.ram(SCREEN) == ZOZURA_SCREEN and
                (g.ram(ENTITIES + FAR), g.ram(ENTITIES + NEAR)) == (ZOZURA, ZOZURA), 300)
        g.until("the screen to settle", g.settled, 300)
        zozura = g.save("zozura")
        near_x = g.ram(SPRITE_X + NEAR)
        assert near_x > g.ram(SPRITE_X + FAR), "the nearer Zozura is in slot 7"
        print(f"ok: on Eolis screen 4 with two Zozura (hero at x {g.ram(POS_X)}, the nearer at {near_x})")

        # 1. One swing, both Zozura: a kill (1 HP left) and a hit, 8 each, apart.
        case("both", {SPRITE_X + FAR: near_x - 4, SPRITE_HP + NEAR: 1})
        ram, s = swing_at_both()
        assert ram[ENTITIES + NEAR] != ZOZURA and ram[ENTITIES + FAR] == ZOZURA
        assert ram[SPRITE_HP + FAR] == zozura_hp - sword, ram[SPRITE_HP + FAR]
        assert ram[HIT_COUNTER + FAR] > 0, "the survivor is still frozen by its hit: it has not moved since"
        dealt = [n for n in s["numbers"] if n["kind"] == "dealt"]
        assert [n["amount"] for n in dealt] == [sword, sword], s
        survivor = next(n for n in dealt if n["target"][:2] == [ram[SPRITE_X + FAR], ram[SPRITE_Y + FAR]])
        killed = next(n for n in dealt if n is not survivor)
        assert survivor["target"] == [ram[SPRITE_X + FAR], ram[SPRITE_Y + FAR], *extents], survivor
        for n in dealt:
            tx, ty, tw, th = n["target"]
            assert n["x"] == tx + tw // 2 - n["w"] // 2, n                       # centered over the box
            assert n["y"] == ty - ABOVE - n["lift"] - n["age"] // STEP, n       # risen from 8 above its top
        assert killed["target"][1] == ram[SPRITE_Y + FAR] and abs(killed["target"][0] - survivor["target"][0]) <= 8, \
            (killed, survivor)
        assert apart(*dealt) and s["crowded"] >= 1 and dealt[0]["lift"] != 0, s   # the older one moved
        s = look("kill-and-hit")
        assert len(s["numbers"]) == 2 and all(n["drawn"] for n in s["numbers"]), s
        print(f"ok: one swing kills a Zozura left 1 HP and hits the other: {sword} over each (the kill's whole "
              f"hit, not the 1 left), centered over its box, both shown at once and apart")

        # 2. The float: a pixel every 3 frames, the same column; held while paused; gone after 60.
        first = s["numbers"][-1]
        g.wait(12)
        later = look("rising")
        moved = next(n for n in later["numbers"] if n["target"] == first["target"])
        assert moved["x"] == first["x"] and moved["age"] > first["age"], (first, moved)
        assert first["y"] - moved["y"] == moved["age"] // STEP - first["age"] // STEP, (first, moved)
        g.act("start", 6, 4)
        g.until("the pause", lambda: g.ram(PAUSED) == 1, 60)
        held = state()["numbers"]
        g.wait(30)
        assert state()["numbers"] == held and g.ram(PAUSED) == 1, "held while paused"
        g.act("start", 6, 4)
        g.until("the game to go on", lambda: g.ram(PAUSED) == 0, 60)
        g.until("the numbers to go", lambda: state()["live"] == 0, LIFE + 10)
        print(f"ok: a number rises 1 pixel every {STEP} frames ({first['y']} to {moved['y']} from age "
              f"{first['age']} to {moved['age']}), holds while paused, and is gone after {LIFE} frames")

        # 3. The Zozura reaches the hero: pink, his HP's loss, over him.
        case("hurt", {POS_X: 192})
        before, hero = state()["hero"], None
        for _ in range(600):
            ram, s = watch()
            if ram[HP] != 0x50:
                break
            hero = (ram[POS_X], ram[POS_Y])                     # he stands still until it touches him
        else:
            raise AssertionError("the Zozura never reached the hero")
        taken = [n for n in s["numbers"] if n["kind"] == "taken"]
        assert s["hero"] - before == 1 and len(taken) == 1 and taken[0]["amount"] == 0x50 - ram[HP] == zozura_damage, s
        assert taken[0]["target"] == [*hero, 16, 32], (taken, hero)
        assert taken[0]["x"] == hero[0] + 8 - 3, taken
        look("hero-hit")
        print(f"ok: the Zozura's touch takes {zozura_damage} HP, and a pink {zozura_damage} rises over the hero "
              f"from where he stood ({hero})")

        # 4. Deluge, through the Magic button.
        case("deluge", {SELECTED_MAGIC: DELUGE, MP: 80, SPRITE_HP + NEAR: 60})
        before = state()["magic"]
        g.until("the Zozura to come", lambda: g.ram(POS_X) - g.ram(SPRITE_X + NEAR) <= 72, 600)
        g.tcp("key", name="C", frames=3)
        g.until("the spell's hit", lambda: g.ram(SPRITE_HP + NEAR) != 60, 300)
        s = state()
        dealt = [n for n in s["numbers"] if n["kind"] == "dealt"]
        assert s["magic"] - before == 1 and [n["amount"] for n in dealt] == [deluge] == [60 - g.ram(SPRITE_HP + NEAR)], s
        look("deluge")
        print(f"ok: Deluge from the Magic button takes {deluge} and shows {deluge}")

        # 5. Ointment: no HP, no number.
        case("ointment", {POS_X: 192, DURATION_OINTMENT: 0x40})
        before = state()["hero"]
        touched = False
        for _ in range(120):
            touched |= abs(g.ram(SPRITE_X + NEAR) - g.ram(POS_X)) < 8
            if touched:
                break
            g.wait(4)
        assert touched, "the Zozura reached him"
        g.wait(60)
        s = state()
        assert g.ram(HP) == 0x50 and s["hero"] == before and s["live"] == 0, s
        print("ok: under Ointment the Zozura's touch takes nothing, and nothing shows")
        g.close()

        # 6. The remaster off: the same kill, no numbers.
        (g.out / "game" / "mods" / "state.toml").write_text(
            f'format_version = 1\n\n[[feature]]\npackage_id = "{PACKAGE}"\nid = "{FEATURE}"\nenabled = false\n')
        g.launch()
        s = state()
        assert not s["enabled"], s
        case("off", {SPRITE_HP + NEAR: 1})
        g.until("the swing", lambda: g.ram(POS_X) - g.ram(SPRITE_X + NEAR) <= REACH, 600)
        for _ in range(300):
            if g.ram(ENTITIES + NEAR) != ZOZURA:
                break
            if not g.ram(PLAYER_FLAGS) & ATTACKING:
                g.tcp("action", name="b", frames=2)
            g.wait(2)
        assert g.ram(ENTITIES + NEAR) != ZOZURA, "killed"
        s = look("off")
        assert s["live"] == 0 and s["weapon"] == 0, s
        print("ok: with the remaster off, the kill shows no number")
        print("screenshots:", ", ".join(p.name for p in g.shots))
    except BaseException:
        if g.running():
            g.shot("failure")
        raise
    finally:
        g.close()


if __name__ == "__main__":
    sys.exit(main())
