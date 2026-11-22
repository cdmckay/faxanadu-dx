"""Play the Magician's lesson end to end in a hidden window.

    python3 tests/lesson/magician_lesson.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/lesson
    python3 tests/lesson/magician_lesson.py ... --part original

The game runs uncapped and the script waits on the game's own state, never on
the clock, on a reserved TCP port (tests/e2e/fax_e2e.py). It uses Block's test
harness (tests/block/harness.py) and the movement tests' headless runs
(tests/movement/play.py).

Every part starts at Eolis's Magician's door (building $05), in a new game in
file 1 (the fixture "lesson-door": out of Eolis's meat shop, its door edited
to lead to his, with the Ring of Elf, as tests/gold/double_gold.py walks to
the King). Save-state edits then give the hero what a part needs (external
prerequisites, not coverage): his gold, and for the file part the lesson
learned (the feature's own record). The hero goes in, which draws that gold
on the HUD, and jumps onto the Magician's step beside him.

buy       With 3000 Golds, he offers "Learn to recover magic faster for 2000
          Golds." as a question. A: the game takes 2000, 10 a frame, and he
          says "Your magic will now return faster."; the hero has learned.
          Asked again, "I have nothing more to teach you.", and nothing is
          taken. Then, outside on the street, headless runs from the same
          spot with Deluge cast on a frame: MP comes back 120 frames after
          the cast, then 1 every 15; without the lesson (its record cleared),
          240 and 30 as before.
refuse    B on his offer closes the box: nothing taken, nothing learned. With
          1999 Golds, A gets the game's own "This is not enough Golds." and
          the script ends: nothing taken, nothing learned.
file      Learned, the hero dies; the respawn writes lesson.learned = yes to
          file 1. Quitting and continuing file 1, he still has it: MP refill
          runs 120 and 15. A new game in file 2 has not learned it.
original  With Faxanadu Remastered off: "I shall give magic for 200 Golds.";
          A takes 200 and gives 40 MP, then "Watch your remaining power
          level.", as the original does.

Screenshots of his three lines, the game's "not enough" and the original's
sale go in OUT.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "block"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "movement"))
import harness  # noqa: E402
from harness import AREA, BUILDINGS, ENTITIES, GOLD, HP, MESSAGE_ID, POS_X, fixture, parse_args  # noqa: E402
import fax_e2e  # noqa: E402  (on the path through harness)
from fax_e2e import (act, choose_file, dissolve, page_through, tcp, title_menu, type_out,  # noqa: E402
                     until, walk)
import play  # noqa: E402

PARTS = ("buy", "refuse", "file", "original")
PACKAGE = ("faxanadu.remastered", "remastered")
RECORD, MANA = "faxanadu.magician-lesson", "faxanadu.mana-refill"   # save-state records: [1] learned;
LEARNED = 1                                                         # mana's [1] seen, [2] MP seen, [3] left
ZONE_NAMES = "faxanadu.zone-names"                                  # its save-state record
AREA_SAVED_AREA, AREA_SAVED_SCREEN, PLAYER_SAVED_POS = 0x3D5, 0x3D6, 0x3D8
MP, SPECIAL_ITEMS, PORTRAIT, PLAYER_IS_DEAD = 0x39A, 0x42C, 0x3C7, 0x438
INTERRUPT_COUNTER, LIT = 0x1A, 0x14                                 # a box's "?" or arrow shows while it has $10
SELECTED_MAGIC, MAGIC_INVENTORY, NUMBER_OF_MAGIC_SPELLS = 0x3C0, 0x3A9, 0x3C5
RING_OF_ELF, NO_PORTRAIT, DELUGE, DELUGE_COST = 0x80, 0xFF, 0, 2
EOLIS, MAGICIANS_SCREEN, MAGICIANS_DOOR = 0, 6, 0x9C                # EOLIS_DOOR_LOCATIONS: building $05
MAGICIANS_STEP = 32                                                 # x on his step, beside him (he is at 16)
SLOT_ID, NOT_ENOUGH, MAGIC_200G, WATCH_POWER = 0xC3, 0x03, 0x15, 0x16   # message IDs
PRICE, STOCK_PRICE, STOCK_MP = 2000, 200, 40
ASKING, SAYING = 1, 3                                               # fax_lesson.c's phases
OFFER, TAUGHT, NOTHING_MORE = 1, 2, 3                               # and lines
WAIT, STEP, TAUGHT_WAIT, TAUGHT_STEP = 240, 30, 120, 15             # fax_mana.c's schedules
FILE = "faxanadu_file1.txt"                                         # the gate's new game is file 1
CAST_AT = 10                                                        # the frame of a headless run Up+B goes down on


class Game(harness.Game, play.Game):
    """Block's harness (walks, warps) and the movement tests' headless runs."""


@fax_e2e.recipe("lesson-door", parent="eolis-meat-shop")
def _door(g):
    """From Eolis's meat shop, out of a door edited to be the Magician's
    (Eolis screen 6, block $9C), with the Ring of Elf, onto his door: the
    street cleared of people, who would knock the hero back."""
    path = g.out / "to-magician.cycstate"
    g.tcp("save_state", path=str(path))
    fixture(path, path, {AREA_SAVED_AREA: EOLIS, AREA_SAVED_SCREEN: MAGICIANS_SCREEN, PLAYER_SAVED_POS: MAGICIANS_DOOR,
                         HP: 0x50, SPECIAL_ITEMS: RING_OF_ELF}, records={ZONE_NAMES: {5: EOLIS, 6: MAGICIANS_SCREEN}})
    g.load(path)
    g.walk_out()
    assert g.where() == (EOLIS, MAGICIANS_SCREEN), g.where()
    g.poke({ENTITIES + i: 0xFF for i in range(8)})
    column = (MAGICIANS_DOOR & 15) * 16
    g.walk_to(column - 6, column + 4)
    g.settle()


def go_in(g):
    """Through his door, and left to him: he stands on a step at the left
    wall (x 16), so a jump left onto it. Going in draws the HUD, so it shows
    gold a fixture set."""
    g.act("up", 4, 10)
    g.until("the Magician's room", lambda: g.ram(AREA) == BUILDINGS and g.settled(), 600)
    walk("left")
    for _ in range(10):
        if g.ram(POS_X) <= MAGICIANS_STEP:
            break
        g.tcp("action", name="left", frames=24)
        g.tcp("action", name="a", frames=12)
        g.wait(30)
    assert g.ram(POS_X) <= MAGICIANS_STEP, g.ram(POS_X)
    g.settle()


def gold_bytes(value):
    return {GOLD: value & 0xFF, GOLD + 1: value >> 8 & 0xFF, GOLD + 2: value >> 16 & 0xFF}


def main():
    args = parse_args(__doc__, parts=PARTS)
    g = Game.from_args(args)
    out = g.out

    def gold():
        return int.from_bytes(g.ram(GOLD, 3), "little")

    def lesson():
        return g.tcp("lesson_state")

    def shot_lit(name):
        """A screenshot with the box's blinking mark lit: the game draws its
        "?" or arrow only while InterruptCounter has bit 4 ($10) set, so a few
        frames into such a run."""
        g.wait(((LIT - g.ram(INTERRUPT_COUNTER) - 1) & 0x1F) + 1)
        return g.shot(name)

    def done_talking():
        return g.ram(PORTRAIT) == NO_PORTRAIT and not g.box_open()

    def start(name, writes, records=None, enter=True):
        """Load the fixture at his door with `writes` (and records), and go in to him."""
        target = out / f"{name}.cycstate"
        fixture(door, target, {SPECIAL_ITEMS: RING_OF_ELF, **writes}, records)
        g.load(target)
        g.wait(4)
        assert g.where() == (EOLIS, MAGICIANS_SCREEN) and not g.box_open(), "at the Magician's door"
        if enter:
            go_in(g)

    def talk(first):
        """Up at him (again, if a press lands as a script winds down), until
        message `first` types."""
        for _ in range(5):                                          # Up counts only on the frame it goes down
            g.act("up", 4, 10)
            try:
                g.until("the Magician", lambda: g.ram(PORTRAIT) != NO_PORTRAIT, 120)
                break
            except AssertionError:
                pass
        else:
            raise AssertionError("the Magician does not answer")
        g.until(f"message {first:#x}", lambda: g.ram(MESSAGE_ID) == first, 300)

    def his_line(which, name):
        """His line `which` up and typed out, through message $C3 (no pause in
        it, and TextBox_MessagePaused can be stale: only MessageID counts): a
        screenshot."""
        g.until("his line to start", lambda: g.ram(MESSAGE_ID) == SLOT_ID, 600)
        type_out(f"his line {which}", 900, pauses=False)
        s = lesson()
        assert s["line"] == which and s["phase"] == (ASKING if which == OFFER else SAYING), s
        return shot_lit(name)

    def taught_refill():
        """Outside his door, a few steps along the street: headless runs with
        Deluge cast, the lesson learned and (its record cleared) not."""
        g.walk_out()
        assert g.where() == (EOLIS, MAGICIANS_SCREEN), g.where()
        g.poke({ENTITIES + i: 0xFF for i in range(8)})
        column = (MAGICIANS_DOOR & 15) * 16
        g.walk_to(column + 24, column + 34)                          # off the door: Up+B there would go in
        g.settle()
        street = g.save("street")
        spells = {SELECTED_MAGIC: DELUGE, MAGIC_INVENTORY: DELUGE, NUMBER_OF_MAGIC_SPELLS: 1, MP: 60}
        taught_state, untaught_state = out / "street-taught.cycstate", out / "street-untaught.cycstate"
        fixture(street, taught_state, spells, {MANA: {1: 1, 2: 60, 3: TAUGHT_WAIT}})
        fixture(street, untaught_state, spells, {MANA: {1: 1, 2: 60, 3: WAIT}, RECORD: {LEARNED: 0}})
        on = g.mods("on")
        cast = [(CAST_AT, "UP+B"), (CAST_AT + 4, "UP"), (CAST_AT + 10, "-")]
        frames = CAST_AT + WAIT + 4 * STEP
        taught, untaught = g.headless_all([(taught_state, cast, frames, on), (untaught_state, cast, frames, on)])
        for run, wait_, step, what in ((taught, TAUGHT_WAIT, TAUGHT_STEP, "taught"), (untaught, WAIT, STEP, "untaught")):
            mps = [r[MP] for r in run]
            drops = [i for i in range(1, frames) if mps[i] < mps[i - 1]]
            gains = [i for i in range(1, frames) if mps[i] > mps[i - 1]]
            want = [CAST_AT + wait_ + k * step for k in range(frames) if CAST_AT + wait_ + k * step < frames]
            assert drops == [CAST_AT] and mps[CAST_AT] == 60 - DELUGE_COST, (what, drops, mps)
            assert gains == want, f"{what}: MP came back at {gains}, not {want}"
            assert [mps[i] for i in want] == list(range(59, 59 + len(want))), (what, mps)
            print(f"ok: {what}, Deluge on frame {CAST_AT} takes MP to 58; it holds {wait_} frames, then climbs 1 MP "
                  f"every {step} frames (on {', '.join(map(str, want[:4]))}, ...)")

    try:
        door = g.state("lesson-door")
        for part in PARTS:
            if not args.wants(part):
                continue
            remastered = part != "original"
            g.set_features({PACKAGE: remastered})
            g.launch()

            if part == "buy":
                start("buy", gold_bytes(3000))
                s = lesson()
                assert s["enabled"] and not s["learned"] and s["price"] == PRICE, s
                talk(SLOT_ID)
                typing = g.save("offer-typing")                     # his offer as it types, for the record's check
                offer = his_line(OFFER, "offer")
                act("a", 2, 0)                                      # yes: a new press on the question
                until("the lesson to be paid", lambda: lesson()["learned"], 600)
                his_line(TAUGHT, "taught")
                assert gold() == 3000 - PRICE, gold()
                page_through("his last line", done_talking, 1200)
                s, mana = lesson(), tcp("mana_state")
                assert gold() == 3000 - PRICE and s["learned"] and (s["offers"], s["taught"]) == (1, 1), s
                assert (mana["wait"], mana["step"]) == (TAUGHT_WAIT, TAUGHT_STEP), mana
                print(f"ok: with 3000 Golds he offers the lesson; A takes {PRICE} (3000 to {gold()}) and he says "
                      "it is learned; MP refill now waits 120 frames and steps 15")
                talk(SLOT_ID)
                his_line(NOTHING_MORE, "nothing-more")
                page_through("his word", done_talking, 1200)
                s = lesson()
                assert gold() == 3000 - PRICE and s["nothing_more"] == 1 and s["offers"] == 1, s
                print("ok: asked again, he has nothing more to teach, and takes nothing")
                taught_refill()
                # A save state from while his offer typed, loaded after his other lines took the slot: the
                # record puts the conversation back (not learned, asking) and the offer in the slot.
                g.load(typing)
                type_out("his offer, again", 900, pauses=False)
                s = lesson()
                assert not s["learned"] and (s["phase"], s["line"]) == (ASKING, OFFER), s
                again = shot_lit("offer-reloaded")
                assert again.read_bytes() == offer.read_bytes(), "the reloaded offer is not the same picture"
                act("b", 2, 0)
                until("the box to close", done_talking, 300)
                print("ok: a save state from while his offer typed, loaded after his other lines, shows the offer "
                      "again, picture for picture, not learned")

            if part == "refuse":
                start("refuse", gold_bytes(3000))
                talk(SLOT_ID)
                his_line(OFFER, "refuse-offer")
                act("b", 2, 0)
                until("the box to close", done_talking, 300)
                s = lesson()
                assert gold() == 3000 and not s["learned"] and s["declined"] == 1 and s["phase"] == 0, s
                print("ok: B on his offer closes the box; nothing taken, nothing learned")
                start("short", gold_bytes(PRICE - 1))
                talk(SLOT_ID)
                his_line(OFFER, "short-offer")
                act("a", 2, 0)
                until("\"This is not enough Golds.\"", lambda: g.ram(MESSAGE_ID) == NOT_ENOUGH, 300)
                type_out("the game's refusal")
                shot_lit("not-enough")
                page_through("the refusal", done_talking, 1200)
                s = lesson()
                assert gold() == PRICE - 1 and not s["learned"] and s["short"] == 1 and s["taught"] == 0, s
                print(f"ok: with {PRICE - 1} Golds, A gets the game's \"This is not enough Golds.\" and the end; "
                      "nothing taken, nothing learned")

            if part == "file":
                start("file", {PLAYER_IS_DEAD: 1}, {RECORD: {LEARNED: 1}}, enter=False)
                assert lesson()["learned"]
                saves = tcp("files_state")["saves"]
                dissolve()
                page_through("the respawn", lambda: tcp("files_state")["saves"] > saves and not g.box_open(), 3000)
                saved = read_file(out / "game" / FILE)
                assert saved.get("lesson.learned") == "yes", saved
                print("ok: learned, a death's respawn writes lesson.learned = yes to file 1")
                g.close()
                g.launch()
                title_menu()
                choose_file(False, 1)                               # CONTINUE, file 1
                s, mana = lesson(), tcp("mana_state")
                assert s["learned"] and (mana["wait"], mana["step"]) == (TAUGHT_WAIT, TAUGHT_STEP), (s, mana)
                print("ok: quitting and continuing file 1, the lesson is still learned: MP refill waits 120, steps 15")
                g.close()
                g.launch()
                title_menu()
                choose_file(True, 2)                                # NEW GAME, file 2
                s, mana = lesson(), tcp("mana_state")
                assert not s["learned"] and (mana["wait"], mana["step"]) == (WAIT, STEP), (s, mana)
                print("ok: a new game in file 2 has not learned it")

            if part == "original":
                start("original", {**gold_bytes(3000), MP: 20})
                assert not lesson()["enabled"]
                talk(MAGIC_200G)
                type_out("his sale")
                shot_lit("original-sale")
                act("a", 2, 0)
                until("\"Watch your remaining power level.\"", lambda: g.ram(MESSAGE_ID) == WATCH_POWER, 900)
                type_out("his last word")
                shot_lit("original-watch")
                page_through("his last word", done_talking, 1200)
                assert gold() == 3000 - STOCK_PRICE and g.ram(MP) == 20 + STOCK_MP, (gold(), g.ram(MP))
                print(f"ok: with the remaster off, he sells {STOCK_MP} MP for {STOCK_PRICE} Golds, as the original")
            g.close()
        print("screenshots:", ", ".join(g.shots))
    except BaseException:
        if g.running():
            g.shot("failure")
        raise
    finally:
        g.close()


def read_file(path):
    """A save file's fields."""
    fields = {}
    for line in path.read_text().splitlines():
        if line.strip() and not line.startswith("#"):
            key, value = (part.strip() for part in line.split("=", 1))
            fields[key] = value
    return fields


if __name__ == "__main__":
    main()
