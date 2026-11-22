"""Play Double gold end to end in a hidden window.

    python3 tests/gold/double_gold.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/gold
    python3 tests/gold/double_gold.py ... --part original

The game runs uncapped and the script waits on the game's own state, never on
the clock, on a reserved TCP port (tests/e2e/fax_e2e.py). It uses Block's test
harness (tests/block/harness.py), and the movement tests' headless runs
(tests/movement/play.py) for the King's count, frame by frame. The parts
start in Eolis's meat shop (the fixture) and walk out onto the screen they
need: the shop's door is edited to lead there (save-state RAM, an external
prerequisite, as tests/slain/slain_by.py does). So are the Ring of Elf for the
King's door, and for the coin the Long Sword in hand, a Monodron with 1 HP in
reach and the Hour Glass running, so the hopping Monodron holds still for the
swing. The kill, the drop and the pickup are the game's own. Five parts, each
its own game:

king      The King, to a hero with no gold: his plea types "I shall give
          you3000 Golds." and he pays 3000, 20 a frame: headless from the
          plea's end, with A on its second frame, gold climbs 20 on each of
          150 frames in a row, then stays. Spent to 0 (through a save state,
          so his record keeps it) and asked again, he has "nothing more" and
          pays nothing: once.
file      Dead with the gift given (both records), from a saved game of it as
          the Guru speaks: the respawn's save writes gold.king_given = yes to
          file 1. Quit, continue file 1, and walk to the King with no gold:
          "nothing more". Continuing from the title is the check, so this part
          boots.
old-file  The same file with gold.king_given taken out, as one saved before
          this change: The King's Plea in its quests.given counts as given,
          and the King has "nothing more".
coin      The Trunk's first screen: the Monodron dies to one swing and drops
          its coin, worth 59 (SPRITE_DROP_VALUES); walking onto it pays 118.
          The same coin 50 short of the cap ($FFFFFF) fills gold to the cap,
          not past it.
original  With Faxanadu Remastered off: the plea says 1500 and he pays 1500, 10
          a frame over 150 frames, and again each time the gold is spent to
          0; the coin pays its 59, and 50 short of the cap the original's add
          wraps around past it.

Screenshots of the plea's page with the amount, the gold after it, the King's
"nothing more" to a continued file, and the coin before and after it is
picked up go in OUT.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "block"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "movement"))
import harness  # noqa: E402
from harness import (AREA, BUILDINGS, ENTITIES, GOLD, HP, PLAYER_IS_DEAD, POS_X, POS_Y, SPRITE_HP, SPRITE_X,  # noqa: E402
                     SPRITE_Y, fixture, parse_args)
import play  # noqa: E402
import fax_e2e  # noqa: E402  (on the path through harness)
from fax_e2e import batch, choose_file, dissolve, page_through, page_to, title_menu  # noqa: E402

PARTS = ("king", "file", "old-file", "coin", "original")
PACKAGE = ("faxanadu.remastered", "remastered")
ZONE_NAMES, GOLD_RECORD, QUESTS = "faxanadu.zone-names", "faxanadu.double-gold", "faxanadu.quests"   # records
GIVEN = 1                          # both records: Double gold's given (0 or 1); the quest log's given set, from here
KINGS_PLEA = 1 << 1                # The King's Plea's bit in the quest log's set (fax_quest_table.h's order)
FILE, FIELD = "faxanadu_file1.txt", "gold.king_given"
AREA_SAVED_AREA, AREA_SAVED_SCREEN, PLAYER_SAVED_POS = 0x3D5, 0x3D6, 0x3D8
PLAYER_FLAGS, FACING_RIGHT = 0xA4, 0x40
SPRITE_VALUES = 0x36C                                               # CurrentSprites_Values: a coin's Golds
SELECTED_WEAPON, PLAYER_CUR_WEAPON, SPECIAL_ITEMS = 0x3BD, 0x3C8, 0x42C
DURATION_HOUR_GLASS, PORTRAIT, MESSAGE_ID = 0x42A, 0x3C7, 0x213
RING_OF_ELF, LONG_SWORD, NO_PORTRAIT = 0x80, 1, 0xFF
EOLIS, KINGS_SCREEN, KINGS_DOOR = 0, 7, 0x98                        # EOLIS_DOOR_LOCATIONS: building $07
EOLIS_TEMPLE = 0x02                                                 # the building a continued Novice starts in
KINGS_STAIRS = 48                                                   # x atop his stairs (Left and A to climb)
PLEA, NOTHING_MORE, NEGATIVE_THOUGHTS = 0x35, 0x36, 0x24            # his two lines; the Guru's after a death
COUNT_FRAMES = 200                                                  # the headless count: A on frame 2, 150 to pay
TRUNK = (1, 0x00, 0x06, 0xA1, 1)                                    # area, screen, palette, door YX, region
MONODRON, COIN = 0x2A, 0x02                                         # entities
MONODRON_COIN = 59                                                  # SPRITE_DROP_VALUES[9]
IN_REACH = 24                                                       # the Long Sword reaches 31; touching hurts from 15
CAP = 0xFFFFFF                                                      # Player_AddGold's
SHORT = 50                                                          # gold short of the cap for the cap's check


class Game(harness.Game, play.Game):
    """Block's harness (walking, warps) with the movement tests' headless runs."""


def gold_bytes(value):
    return {GOLD: value & 0xFF, GOLD + 1: value >> 8 & 0xFF, GOLD + 2: value >> 16 & 0xFF}


def read_file(path):
    return dict(line.split(" = ", 1) for line in path.read_text().splitlines() if " = " in line and not line.startswith("#"))


@fax_e2e.recipe("gold-given-words", parent="eolis-town")
def _given_words(g):
    """In Eolis's town with the King's gift given (Double gold's record, and
    The King's Plea in the quest log's), dead: as the Guru speaks, the
    dissolve over (the respawn's save is what is checked)."""
    g.poke({PLAYER_IS_DEAD: 1}, {GOLD_RECORD: {GIVEN: 1}, QUESTS: {GIVEN: KINGS_PLEA}})
    dissolve(1800)


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

    def to_the_king(source, name):
        """From inside a building, into the King's room with the ring and no gold."""
        go_in(source, name, KINGS_SCREEN, KINGS_DOOR, {SPECIAL_ITEMS: RING_OF_ELF, **gold_bytes(0)})

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

    def nothing_more(what, shot=None):
        """Asked with no gold, after his gift: his "nothing more", and no
        gold. With `shot`, a screenshot of its first page."""
        before = state()
        talk_to_king(NOTHING_MORE)
        if shot:
            page_to(lambda s: True, "his word's first page", "gold_state")
            g.shot(shot)
        page_through("his word", done_talking, 1200)
        s = state()
        assert gold() == 0 and s["given"], (gold(), s)
        assert s["kings"] == before["kings"] and s["nothing_more"] == before["nothing_more"] + 1, (before, s)
        print(f"ok: {what}, the King has nothing more for a hero with no gold")

    def counted(plea_end, remastered, amount):
        """Headless from the plea's end, A on its second frame: each frame's
        gold climbs by the same pay, on frames in a row, to the amount."""
        mods = g.mods("headless-mods", original=not remastered)
        rams = g.headless(plea_end, [(0, "-"), (2, "A"), (6, "-")], COUNT_FRAMES, mods)
        golds = [int.from_bytes(r[GOLD:GOLD + 3], "little") for r in rams]
        steps = [(i, golds[i] - golds[i - 1]) for i in range(1, len(golds)) if golds[i] != golds[i - 1]]
        pay = 20 if remastered else 10
        assert golds[0] == 0 and golds[-1] == amount, (golds[0], golds[-1])
        assert steps and all(paid == pay for _, paid in steps), steps
        first = steps[0][0]
        assert [i for i, _ in steps] == list(range(first, first + 150)), steps
        print(f"ok: the King's {amount} counts up {pay} a frame, in 150 frames in a row (frames {first}-{first + 149} "
              "of the headless run)")

    def the_king(remastered):
        """His plea and his gift, to a hero with no gold; spent to 0 and asked
        again, nothing with the remaster, the gift again without."""
        shop = g.state("eolis-meat-shop")
        g.launch(at="eolis-meat-shop")
        s = state()
        assert s["enabled"] == remastered and not s["given"], s
        to_the_king(shop, "kings-room")
        talk_to_king(PLEA)
        s = page_to(lambda s: s["plea_digits"] == 4, "the plea's amount", "gold_state")
        g.shot("plea-amount")
        amount = 3000 if remastered else 1500
        assert s["line"] == (2 if remastered else 0), s             # "15" served as "30"; "00" is the same
        page_to(lambda s: g.ram(MESSAGE_ID) == 0, "the plea's end", "gold_state")
        assert gold() == 0, "nothing is paid until the plea is let go"
        counted(g.save("plea-end"), remastered, amount)
        page_through("the gift", done_talking, 3000)
        assert gold() == amount, f"the King gives {amount} Golds, not {gold()}"
        g.shot("gift")
        s = state()
        assert (s["kings"], s["king_pays"], s["given"]) == ((1, 150, True) if remastered else (0, 0, False)), s
        print(f"ok: the King's plea says {amount} Golds and he gives {amount}"
              + (" (1500 doubled)" if remastered else " (the original)"))
        g.poke(gold_bytes(0))                                       # spent to 0, through a save state
        if remastered:
            nothing_more("spent to 0 and asked again (his record kept through a save state)")
        else:
            talk_to_king(PLEA)
            page_through("the gift again", done_talking, 3000)
            assert gold() == amount, f"the original King gives again at 0 gold, not {gold()}"
            print(f"ok: spent to 0 and asked again, the original King gives his {amount} again")
        g.close()

    def the_file(old):
        """The gift given, a respawn's save, and file 1 continued; `old`:
        without gold.king_given, as a file saved before it."""
        g.launch(at="gold-given-words")                             # dead, as the Guru speaks
        assert state()["given"], state()
        saves = g.tcp("files_state")["saves"]
        g.until("the Guru's words", lambda: g.ram(MESSAGE_ID) == NEGATIVE_THOUGHTS, 1800)
        page_through("the box to close", lambda: not g.box_open())
        g.until("the respawn's save", lambda: g.tcp("files_state")["saves"] > saves, 1800)
        path = g.out / "game" / FILE
        saved = read_file(path)
        assert saved.get(FIELD) == "yes" and saved.get("quests.given") == "20000", saved
        print(f"ok: dying wrote {FIELD} = yes to {FILE}, beside quests.given = {saved['quests.given']}")
        g.close()
        if old:
            path.write_text("".join(line for line in path.read_text().splitlines(keepends=True)
                                    if not line.startswith(FIELD + " ")))
            assert FIELD not in read_file(path) and read_file(path)["quests.given"] == "20000"
        g.launch()
        title_menu()
        choose_file(False, 1)                                       # CONTINUE, file 1
        g.until("Eolis's temple", lambda: g.where() == (BUILDINGS, EOLIS_TEMPLE) and g.settled(), 1800)
        g.wait(10)
        assert state()["given"], state()
        to_the_king(g.save("continued"), "kings-room-continued")
        if old:
            nothing_more(f"continuing a file without {FIELD}, with The King's Plea in quests.given", "nothing-more")
        else:
            nothing_more(f"continuing file 1 with {FIELD} = yes", "nothing-more")
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
            remastered = part != "original"
            g.set_features({PACKAGE: remastered})
            if part in ("king", "original"):
                the_king(remastered)
            if part in ("file", "old-file"):
                the_file(part == "old-file")
            if part in ("coin", "original"):
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
