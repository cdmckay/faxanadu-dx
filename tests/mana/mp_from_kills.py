"""Play MP from kills end to end.

    python3 tests/mana/mp_from_kills.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/mana
    python3 tests/mana/mp_from_kills.py ... --part guru

kills     From Eolis's screen 4 as its two Zozuras crawl in (the fixture
          "eolis-zozuras"), with save-state RAM and record edits (external
          prerequisites, not coverage): the Long Sword in hand, MP, where the
          hero and the nearer Zozura stand, its HP 1, the other sent away.
          Headless runs from there, each frame's RAM logged, check it to the
          frame:
          1. Waiting: no enemy on the screen, Deluge cast at 40 MP, then 600
             frames (#98's refill gave its first MP 240 frames after a cast):
             MP stays at 38.
          2. A kill by the sword, one swing at 10 MP: on the frame the game
             adds the Zozura's 30 experience, MP is 11 (30² / 512 = 1), and
             nothing else moves it; a state saved after shows the MP bar the
             game draws for 11, where it showed another value before.
          3. A kill by magic, Deluge at the Zozura 64 pixels off at 10 MP: 8
             once cast, 9 on the kill.
          4. A boss: the Zozura's slot made a Rokusutahn (category 7, 120
             experience, a box 32 wide, so 16 pixels further off), HP 1, and
             one swing: MP 10 to 30, the bar drawn.
          5. The Magician's lesson learned (its record): the sword's kill
             gives 2, 10 to 12.
          6. At 79 MP, with the lesson, the kill's 2 stops at 80.
          7. The original (the remaster off): the kill gives its 30 experience
             and no MP, and waiting after a cast gives none either.
guru      In Eolis's temple, come in from the meat shop through a door edited
          to lead to it (the fixture "mana-eolis-temple") with 32 HP and 5 MP,
          which the screen's load drew on the status bar: talking to the Guru,
          HP and MP are 80 by his first line, and while it types (his box
          fills the PPU buffer as it opens) both bars are drawn full, as the
          game draws them.
original  The same, with the remaster off: the Guru leaves HP and MP at 32
          and 5, as the original does.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "movement"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "recharge"))
import fax_e2e  # noqa: E402
from fax_e2e import act, box_open, chunks, fixture, parse_args, ram, tcp, until, walk  # noqa: E402
import play  # noqa: E402  (fax_e2e.Game with headless runs)
import recharging_items as recharge  # noqa: E402  (its walk out of a shop, and in at a door)

PARTS = ("kills", "guru", "original")
PACKAGE = ("faxanadu.dx", "dx")
POS_X, POS_Y, PLAYER_FLAGS, STATUS, IFRAMES = 0x9E, 0xA1, 0xA4, 0xA5, 0xAD
SPRITE_X, SPRITE_Y, ENTITIES, SPRITE_FLAGS, SPRITE_HP, HIT_COUNTER = 0xBA, 0xC2, 0x2CC, 0x2DC, 0x344, 0x34C
EXPERIENCE, MP, SELECTED_WEAPON, SELECTED_ARMOR, SELECTED_MAGIC = 0x390, 0x39A, 0x3BD, 0x3BE, 0x3C0
MAGIC_INVENTORY, NUMBER_OF_MAGIC_SPELLS, PLAYER_CUR_WEAPON = 0x3A9, 0x3C5, 0x3C8
DURATION_OINTMENT, DURATION_GLOVE, SPECIAL_ITEMS, HP, CAST_MAGIC_TYPE = 0x427, 0x428, 0x42C, 0x431, 0x2B3
MESSAGE_ID = 0x213
EOLIS, EOLIS_TEMPLE, PALETTE_EOLIS = 0, 0x02, 0x00
ROKUSUTAHN, DELUGE, LONG_SWORD, DELUGE_COST = 0x11, 0, 1, 2
NEAR, FAR = 7, 6                                  # the Zozuras' slots: the nearer, the one behind it
HERO_AT, GROUND = 240, 144                        # where the hero stands (a Zozura crawls 16 lower)
IN_REACH, SPELL_OFF = 26, 64                      # the Zozura this far left of him: a swing's hit, a Deluge's
FACING_RIGHT = 0x01                               # CurrentSprites_Flags: the Zozura crawls toward him
FULL = 80
ZOZURA_XP, ZOZURA_MP, BOSS_XP, BOSS_MP = 30, 1, 120, 20   # ENEMY_EXPERIENCE; fax_mana.c's experience² / 512, BOSS_MP
LESSON = "faxanadu.magician-lesson"               # its save-state record: [1] learned
# The status bar: the Power bar at nametable $2063 and the Magic bar at $2043,
# UI_MPAndHPBarWidth + 1 tiles each: a full tile an 8 (the Power bar's $08,
# the Magic bar's $09), then a partial $0C or $0D unless full, empty $07 to the
# width, and the end $0B.
BAR_WIDTH, EMPTY_TILE, END_TILE = 10, 0x07, 0x0B
BARS = {"hp": (0x63, 0x08, 0x0C), "mp": (0x43, 0x09, 0x0D)}
SWING_AT, CAST_AT = 1, 10                         # the frames of a headless run B, or Up+B, goes down on


def swing_at(at):
    return [(at, "B"), (at + 2, "-")]


def cast_at(at):
    """Up+B the stock way: Up and B down together, Up held a little longer."""
    return [(at, "UP+B"), (at + 4, "UP"), (at + 10, "-")]


def word(r, addr):
    return r[addr] | r[addr + 1] << 8


def changes(rams, addr, size=1):
    """The frames on which a value changes, with what it became."""
    get = (lambda r: r[addr]) if size == 1 else (lambda r: word(r, addr))
    return [(i, get(rams[i])) for i in range(1, len(rams)) if get(rams[i]) != get(rams[i - 1])]


def tiles(value, full, partial):
    """A bar's tiles as UI_SetHUDBarValue draws `value`."""
    n = min(value // 8, BAR_WIDTH)
    rest = [partial] + [EMPTY_TILE] * (BAR_WIDTH - n - 1) if n < BAR_WIDTH else []
    return bytes([full] * n + rest + [END_TILE])


def nametable(path):
    """A save state's nametables (its PPU chunk ends with CIRAM)."""
    data = Path(path).read_bytes()
    pos, size = next((p, n) for t, p, n in chunks(data) if t == b"PPU ")
    return data[pos:pos + size][-0x1000:]


def bar(path, which):
    at, _, _ = BARS[which]
    return bytes(nametable(path)[at:at + BAR_WIDTH + 1])


def bar_shows(path, which, value):
    _, full, partial = BARS[which]
    return bar(path, which) == tiles(value, full, partial)


@fax_e2e.recipe("mana-eolis-temple", parent="eolis-meat-shop")
def _eolis_temple(g):
    """Out of Eolis's meat shop with 32 HP and 5 MP, its door edited to lead
    to the temple's on Eolis's screen 2, so the screen's load draws both bars;
    and in at the temple's door."""
    recharge.warp_out(g, g.save("shop"), "to-temple", EOLIS, 2, 0x95, PALETTE_EOLIS, 0, {HP: 0x20, MP: 5})
    recharge.enter(g, EOLIS_TEMPLE)


def main():
    args = parse_args(__doc__, parts=PARTS)
    game = play.Game.from_args(args)
    out = game.out
    try:
        if args.wants("kills"):
            zozuras = game.state("eolis-zozuras")

            def saved(addr):
                data = Path(zozuras).read_bytes()
                return data[next(p for t, p, n in chunks(data) if t == b"HW  ") + 23 + addr]

            def case(name, writes, records=None):
                """The Zozuras' state with the hero at HERO_AT facing them, the Long Sword in hand, and
                `writes`: the nearer Zozura crawling toward him, the other sent away."""
                path = out / f"{name}.cycstate"
                fixture(zozuras, path, {POS_X: HERO_AT, POS_Y: GROUND, PLAYER_FLAGS: 0, STATUS: 0, IFRAMES: 0,
                                        PLAYER_CUR_WEAPON: LONG_SWORD, SELECTED_WEAPON: LONG_SWORD, HP: FULL,
                                        SELECTED_ARMOR: 0, SPECIAL_ITEMS: 0, DURATION_GLOVE: 0xFF,
                                        DURATION_OINTMENT: 0xFF, SELECTED_MAGIC: DELUGE, MAGIC_INVENTORY: DELUGE,
                                        NUMBER_OF_MAGIC_SPELLS: 1, CAST_MAGIC_TYPE: 0xFF, ENTITIES + FAR: 0xFF,
                                        SPRITE_X + NEAR: HERO_AT - IN_REACH, SPRITE_Y + NEAR: GROUND + 16,
                                        HIT_COUNTER + NEAR: 0,
                                        SPRITE_FLAGS + NEAR: saved(SPRITE_FLAGS + NEAR) | FACING_RIGHT,
                                        SPRITE_HP + NEAR: 1, **writes}, records)
                return path

            on, off = game.mods("on"), game.mods("off", original=True)
            wait_state = case("waiting", {ENTITIES + NEAR: 0xFF, MP: 40})
            kill_state = case("sword", {MP: 10})
            spell_state = case("deluge", {MP: 10, SPRITE_X + NEAR: HERO_AT - SPELL_OFF})
            boss_state = case("boss", {MP: 10, ENTITIES + NEAR: ROKUSUTAHN,      # 32 wide: its right edge where
                                       SPRITE_X + NEAR: HERO_AT - IN_REACH - 16})   # the Zozura's is
            taught_state = case("taught", {MP: 10}, {LESSON: {1: 1}})
            cap_state = case("cap", {MP: 79}, {LESSON: {1: 1}})
            waiting, frames = CAST_AT + 600, 60
            after_sword, after_boss = out / "sword-after.cycstate", out / "boss-after.cycstate"
            runs = game.headless_all([
                (wait_state, cast_at(CAST_AT), waiting, on),
                (kill_state, swing_at(SWING_AT), frames, on, out / "sword-kill.png", (40, after_sword)),
                (spell_state, cast_at(CAST_AT), frames + 60, on),
                (boss_state, swing_at(SWING_AT), frames, on, out / "boss-kill.png", (40, after_boss)),
                (taught_state, swing_at(SWING_AT), frames, on),
                (cap_state, swing_at(SWING_AT), frames, on),
                (kill_state, swing_at(SWING_AT), frames, off),
                (wait_state, cast_at(CAST_AT), waiting, off)])
            game.shots += ["sword-kill.png", "boss-kill.png"]
            wait_on, sword, spell, boss, taught, cap, sword_off, wait_off = runs

            # 1. Waiting gives nothing.
            assert changes(wait_on, MP) == [(CAST_AT, 40 - DELUGE_COST)], changes(wait_on, MP)
            print(f"ok: Deluge at 40 MP takes it to 38, and {waiting - CAST_AT} frames on it is still 38: "
                  "nothing comes back over time")

            def one_kill(rams, what, mp0, mp1, xp):
                """A run with one kill: experience up by `xp` on one frame, and MP from `mp0` to `mp1` on the
                same frame and no other. Returns that frame."""
                gained = changes(rams, EXPERIENCE, 2)
                assert len(gained) == 1 and gained[0][1] - word(rams[0], EXPERIENCE) == xp, (what, gained)
                at = gained[0][0]
                assert rams[0][MP] == mp0 and changes(rams, MP) == [(at, mp1)], (what, changes(rams, MP))
                return at

            # 2. The sword.
            at = one_kill(sword, "sword", 10, 10 + ZOZURA_MP, ZOZURA_XP)
            assert bar_shows(after_sword, "mp", 10 + ZOZURA_MP), bar(after_sword, "mp")
            assert not bar_shows(kill_state, "mp", 10 + ZOZURA_MP), f"the bar showed {10 + ZOZURA_MP} before"
            print(f"ok: a swing kills the Zozura on frame {at}: its {ZOZURA_XP} experience and, on the same frame, "
                  f"{ZOZURA_MP} MP (10 to {10 + ZOZURA_MP}); the bar shows {10 + ZOZURA_MP} as the game draws it")

            # 3. Magic.
            mps = changes(spell, MP)
            assert mps[0] == (CAST_AT, 10 - DELUGE_COST), mps
            gained = changes(spell, EXPERIENCE, 2)
            assert len(gained) == 1 and mps[1:] == [(gained[0][0], 10 - DELUGE_COST + ZOZURA_MP)], (mps, gained)
            print(f"ok: Deluge (frame {CAST_AT}, 10 MP to 8) kills the Zozura on frame {gained[0][0]}: "
                  f"{ZOZURA_MP} MP, 8 to {8 + ZOZURA_MP}")

            # 4. A boss.
            at = one_kill(boss, "boss", 10, 10 + BOSS_MP, BOSS_XP)
            assert bar_shows(after_boss, "mp", 10 + BOSS_MP), bar(after_boss, "mp")
            print(f"ok: a boss (a Rokusutahn, poked into the slot, HP 1) killed on frame {at}: {BOSS_XP} experience "
                  f"and {BOSS_MP} MP, 10 to {10 + BOSS_MP}, the bar drawn")

            # 5. The lesson: twice. 6. The cap.
            one_kill(taught, "taught", 10, 10 + 2 * ZOZURA_MP, ZOZURA_XP)
            one_kill(cap, "cap", 79, FULL, ZOZURA_XP)
            print(f"ok: with the Magician's lesson the kill gives {2 * ZOZURA_MP} (10 to {10 + 2 * ZOZURA_MP}); at 79 "
                  "its 2 stop at 80")

            # 7. The original.
            gained = changes(sword_off, EXPERIENCE, 2)
            assert len(gained) == 1 and gained[0][1] - word(sword_off[0], EXPERIENCE) == ZOZURA_XP, gained
            assert changes(sword_off, MP) == [], changes(sword_off, MP)
            assert changes(wait_off, MP) == [(CAST_AT, 40 - DELUGE_COST)], changes(wait_off, MP)
            print(f"ok: with the remaster off the kill gives its {ZOZURA_XP} experience and no MP, and after a cast "
                  "MP stays where it was")

        for part in ("guru", "original"):
            if not args.wants(part):
                continue
            temple = game.state("mana-eolis-temple")
            dx = part == "guru"
            game.set_features({PACKAGE: dx})
            game.launch()
            tcp("load_state", path=str(temple))
            fax_e2e.wait(4)
            before = out / f"{part}-before.cycstate"
            tcp("save_state", path=str(before))
            assert (ram(HP), ram(MP)) == (0x20, 5), (ram(HP), ram(MP))
            assert bar_shows(before, "hp", 0x20) and bar_shows(before, "mp", 5), (bar(before, "hp"), bar(before, "mp"))
            s = tcp("mana_state")
            assert s["enabled"] == dx, s
            walk("left")                                            # to the Guru, who stands on the left
            assert not box_open()
            shown = ram(MESSAGE_ID)
            act("up", 4, 0)
            until("the Guru's first line", lambda: box_open() and ram(MESSAGE_ID) != shown, 300)
            first = ram(MESSAGE_ID)
            if dx:
                s = tcp("mana_state")
                assert (ram(HP), ram(MP)) == (FULL, FULL) and s["restores"] == 1, s
                # The bars: as soon as the PPU buffer has room, which his box takes as it opens.
                until("the bars", lambda: tcp("mana_state")["bars"] == 0, 60)
                fax_e2e.wait(4)                                     # the NMIs send what was queued before them
                assert ram(MESSAGE_ID) == first and box_open(), "still his first line"
            opened = out / f"{part}-opened.cycstate"
            tcp("save_state", path=str(opened))
            game.shot(f"{part}-guru")
            if dx:
                s = tcp("mana_state")
                assert bar_shows(opened, "hp", FULL) and bar_shows(opened, "mp", FULL), \
                    (bar(opened, "hp"), bar(opened, "mp"))
                print(f"ok: Eolis's Guru, at 32 HP and 5 MP: at his first action both are 80, before his first line, "
                      f"and while it types both bars are drawn full, as the game draws them ({s['drawn']} drawn, "
                      f"after {s['deferred']} frames' wait for room)")
            else:
                assert (ram(HP), ram(MP)) == (0x20, 5), (ram(HP), ram(MP))
                assert bar_shows(opened, "hp", 0x20) and bar_shows(opened, "mp", 5)
                print("ok: with the remaster off, the Guru leaves HP at 32 and MP at 5")
            game.close()
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
