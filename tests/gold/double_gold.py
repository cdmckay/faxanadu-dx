"""Play Double gold end to end in a hidden window.

    python3 tests/gold/double_gold.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/gold
    python3 tests/gold/double_gold.py ... --part original

The game runs uncapped and the script waits on the game's own state, never on
the clock, on a reserved TCP port (tests/e2e/fax_e2e.py). It uses Block's test
harness (tests/block/harness.py). Each part starts in Eolis's meat shop (the
fixture) and walks out onto the screen it needs: the shop's door is edited to
lead there (save-state RAM, an external prerequisite, as
tests/slain/slain_by.py does). So are the Ring of Elf for the King's door, and
for the coin the Long Sword in hand, a Monodron with 1 HP in reach and the
Hour Glass running, so the hopping Monodron holds still for the swing. The
kill, the drop and the pickup are the game's own.

remastered  The King, to a hero with no gold: his plea types "I shall give
            you3000 Golds." and he pays 3000, 10 a frame. Asked again, he has
            "nothing more" and pays nothing: once. Then the Trunk's first
            screen: the Monodron dies to one swing and drops its coin, worth
            59 (SPRITE_DROP_VALUES); walking onto it pays 118. The same coin
            50 short of the cap ($FFFFFF) fills gold to the cap, not past it.
original    With Faxanadu Remastered off: the plea says 1500 and he pays 1500;
            the coin pays its 59, and 50 short of the cap the original's add
            wraps around past it.

Screenshots of the plea's page with the amount, the gold after it, and the
coin before and after it is picked up go in OUT.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "block"))
from harness import (AREA, BUILDINGS, ENTITIES, GOLD, HP, POS_X, POS_Y, SPRITE_HP, SPRITE_X, SPRITE_Y,  # noqa: E402
                     Game, fixture, parse_args)
import fax_e2e  # noqa: E402  (on the path through harness)
from fax_e2e import batch, page_through, page_to  # noqa: E402

PARTS = ("remastered", "original")
PACKAGE = ("faxanadu.remastered", "remastered")
ZONE_NAMES = "faxanadu.zone-names"                                  # its save-state record
AREA_SAVED_AREA, AREA_SAVED_SCREEN, PLAYER_SAVED_POS = 0x3D5, 0x3D6, 0x3D8
PLAYER_FLAGS, FACING_RIGHT = 0xA4, 0x40
SPRITE_VALUES = 0x36C                                               # CurrentSprites_Values: a coin's Golds
SELECTED_WEAPON, PLAYER_CUR_WEAPON, SPECIAL_ITEMS = 0x3BD, 0x3C8, 0x42C
DURATION_HOUR_GLASS, PORTRAIT, MESSAGE_ID = 0x42A, 0x3C7, 0x213
RING_OF_ELF, LONG_SWORD, NO_PORTRAIT = 0x80, 1, 0xFF
EOLIS, KINGS_SCREEN, KINGS_DOOR = 0, 7, 0x98                        # EOLIS_DOOR_LOCATIONS: building $07
KINGS_STAIRS = 48                                                   # x atop his stairs (Left and A to climb)
PLEA, NOTHING_MORE = 0x35, 0x36                                     # his two lines
TRUNK = (1, 0x00, 0x06, 0xA1, 1)                                    # area, screen, palette, door YX, region
MONODRON, COIN = 0x2A, 0x02                                         # entities
MONODRON_COIN = 59                                                  # SPRITE_DROP_VALUES[9]
IN_REACH = 24                                                       # the Long Sword reaches 31; touching hurts from 15
CAP = 0xFFFFFF                                                      # Player_AddGold's
SHORT = 50                                                          # gold short of the cap for the cap's check


def gold_bytes(value):
    return {GOLD: value & 0xFF, GOLD + 1: value >> 8 & 0xFF, GOLD + 2: value >> 16 & 0xFF}


def main():
    args = parse_args(__doc__, parts=PARTS)
    g = Game.from_args(args)

    def gold():
        return int.from_bytes(g.ram(GOLD, 3), "little")

    def state():
        return g.tcp("gold_state")

    def go_in(source, name, screen, door, writes):
        """From inside a building, walk out onto Eolis's door `door` (block YX)
        on `screen`, and go in."""
        target = g.out / f"{name}.cycstate"
        fixture(source, target, {AREA_SAVED_AREA: EOLIS, AREA_SAVED_SCREEN: screen, PLAYER_SAVED_POS: door,
                                 HP: 0x50, **writes}, records={ZONE_NAMES: {5: EOLIS, 6: screen}})
        g.load(target)
        g.walk_out()
        assert g.where() == (EOLIS, screen), g.where()
        g.poke({ENTITIES + i: 0xFF for i in range(8)})              # nothing on the street knocks him back
        column = (door & 15) * 16
        g.walk_to(column - 6, column + 4)
        g.act("up", 4, 10)
        g.until("the building", lambda: g.ram(AREA) == BUILDINGS and g.settled(), 600)
        g.wait(30)

    def talk_to_king(line):
        """Up the King's stairs (Left and A) and Up at him, until `line` types."""
        for _ in range(60):
            if g.ram(POS_X) <= KINGS_STAIRS:
                break
            g.tcp("action", name="left", frames=24)
            g.tcp("action", name="a", frames=12)
            g.wait(30)
        for _ in range(5):                                          # Up counts only on the frame it goes down
            g.act("up", 4, 10)
            try:
                g.until("the King", lambda: g.ram(PORTRAIT) != NO_PORTRAIT, 120)
                break
            except AssertionError:
                pass
        g.until(f"line {line:#x}", lambda: g.ram(MESSAGE_ID) == line, 600)

    def done_talking():
        return g.ram(PORTRAIT) == NO_PORTRAIT and not g.box_open()

    def the_king(remastered):
        """His plea and his gift, to a hero with no gold; asked again, nothing."""
        shop = g.state("eolis-meat-shop")
        g.launch(at="eolis-meat-shop")
        assert state()["enabled"] == remastered, state()
        go_in(shop, "kings-room", KINGS_SCREEN, KINGS_DOOR, {SPECIAL_ITEMS: RING_OF_ELF, **gold_bytes(0)})
        talk_to_king(PLEA)
        s = page_to(lambda s: s["plea_digits"] == 4, "the plea's amount", "gold_state")
        g.shot("plea-amount")
        amount = 3000 if remastered else 1500
        assert s["line"] == (2 if remastered else 0), s             # "15" served as "30"; "00" is the same
        page_through("the gift", done_talking, 3000)
        assert gold() == amount, f"the King gives {amount} Golds, not {gold()}"
        g.shot("gift")
        assert state()["kings"] == (1 if remastered else 0), state()
        print(f"ok: the King's plea says {amount} Golds and he gives {amount}"
              + (" (1500 doubled)" if remastered else " (the original)"))
        if remastered:
            talk_to_king(NOTHING_MORE)
            page_through("his second word", done_talking, 1200)
            assert gold() == amount and state()["kings"] == 1, (gold(), state())
            print("ok: asked again, the King has nothing more: his 3000 is given once")
        g.close()

    def the_coin(remastered):
        """The Monodron on the Trunk's first screen: one swing, its coin, the pickup."""
        shop = g.state("eolis-meat-shop")
        g.launch(at="eolis-meat-shop")
        g.warp(shop, "trunk-door", *TRUNK)
        g.wait(10)
        trunk = g.save("trunk")
        entities = list(g.ram(ENTITIES, 8))
        slot = entities.index(MONODRON)
        hero_x, hero_y = g.ram(POS_X), g.ram(POS_Y)
        writes = {ENTITIES + i: 0xFF for i in range(8) if i != slot}
        writes.update({SPRITE_X + slot: hero_x + IN_REACH, SPRITE_Y + slot: hero_y, SPRITE_HP + slot: 1,
                       DURATION_HOUR_GLASS: 3, SELECTED_WEAPON: LONG_SWORD, PLAYER_CUR_WEAPON: LONG_SWORD,
                       PLAYER_FLAGS: g.ram(PLAYER_FLAGS) | FACING_RIGHT, HP: 0x50})

        def kill_and_pick_up(name, start):
            """From `trunk` with gold `start`: swing on the first frame, the
            coin it drops, then walk onto it. Gold after the pickup."""
            swing = g.out / f"{name}.cycstate"
            fixture(trunk, swing, {**writes, **gold_bytes(start)})
            batch(("load_state", dict(path=str(swing))), ("action", dict(name="b", frames=2)))
            g.until("the Monodron's coin", lambda: COIN in g.ram(ENTITIES, 8), 120)
            coin = list(g.ram(ENTITIES, 8)).index(COIN)
            assert g.ram(SPRITE_VALUES + coin) == MONODRON_COIN, g.ram(SPRITE_VALUES + coin)
            assert gold() == start, "the kill pays nothing; the coin does"
            g.shot(f"{name}-coin")
            assert fax_e2e.hold("right", lambda: gold() != start, 240), "walked onto the coin"
            g.wait(2)
            assert COIN not in g.ram(ENTITIES, 8), "the coin is taken"
            g.shot(f"{name}-taken")
            return gold()

        paid = kill_and_pick_up("coin", 0)
        want = 2 * MONODRON_COIN if remastered else MONODRON_COIN
        assert paid == want, f"the coin pays {want}, not {paid}"
        if remastered:
            s = state()
            assert (s["coins"], s["coin_gold"], s["last_coin"], s["capped"]) == (1, want, MONODRON_COIN, 0), s
        print(f"ok: the Monodron's coin, worth {MONODRON_COIN}, pays {paid}"
              + (" (doubled)" if remastered else " (the original)"))

        start = CAP - SHORT
        capped = kill_and_pick_up("cap", start)
        if remastered:
            assert capped == CAP, f"gold stops at the cap, {CAP:#x}, not {capped:#x}"
            assert state()["capped"] == 1, state()
            print(f"ok: {SHORT} short of the cap, the coin's {2 * MONODRON_COIN} fills gold to {CAP:#x} and no further")
        else:
            assert capped == start + MONODRON_COIN - (CAP + 1), f"the original wraps: {capped:#x}"
            print(f"ok: the original's coin add wraps past the cap ({start:#x} + {MONODRON_COIN} = {capped:#x})")
        g.close()

    try:
        for part in PARTS:
            if not args.wants(part):
                continue
            remastered = part == "remastered"
            g.set_features({PACKAGE: remastered})
            the_king(remastered)
            the_coin(remastered)
        print("screenshots:", ", ".join(g.shots))
    except BaseException:
        if g.running():
            g.shot("failure")
        raise
    finally:
        g.close()


if __name__ == "__main__":
    main()
