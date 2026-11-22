"""Play the Magician's lesson end to end in a hidden window.

    python3 tests/lesson/magician_lesson.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/lesson
    python3 tests/lesson/magician_lesson.py ... --part original

The game runs uncapped and the script waits on the game's own state, never on
the clock, on a reserved TCP port (tests/e2e/fax_e2e.py). It uses Block's test
harness (tests/block/harness.py) and the movement tests' headless runs
(tests/movement/play.py), which count the paying frame by frame.

Most parts start at Eolis's Magician's door (building $05), in a new game in
file 1 (the fixture "lesson-door": out of Eolis's meat shop, its door edited
to lead to his, with the Ring of Elf, as tests/gold/double_gold.py walks to
the King). Save-state edits then give the hero what a part needs (external
prerequisites, not coverage): his gold, and that he has studied at the magic
hall (the feature's own record), or learned. The hero goes in, which draws
that gold on the HUD, and jumps onto the Magician's step beside him.

unstudied  With 40000 Golds, not having studied: "I can teach you to recover
           magic faster.", then "First study at the magic hall in the Mist.",
           and the end. No gold is taken or checked.
buy        Studied, with 30000 Golds: what he teaches, then "It will cost
           30000 Golds." as a question. A: the game takes the 30000, 150 a
           frame in 200 frames (a headless run from the question counts each
           frame), then its 60; he says "Your magic will now return faster."
           and the hero has learned. Asked again, "I have nothing more to
           teach you.", and nothing is taken. Then, outside on the street,
           headless runs from the same spot with Deluge cast on a frame: MP
           comes back 120 frames after the cast, then 1 every 15; without the
           lesson (its record cleared), 240 and 30 as before. Last, a save
           state from while his price typed, loaded after his other lines
           took message $C3's slot: the feature's record puts the conversation
           back (not learned, his price up) and serves the price again, the
           same picture as before.
refuse     Studied: B on his price closes the box, nothing taken, nothing
           learned. With 29999 Golds, A gets the game's own "This is not
           enough Golds." and the script ends: nothing taken, nothing learned.
hall       The magic hall in the Mist (screen $29, building $1C), from Eolis's
           meat shop with its door edited to lead to the hall's: its Magician
           sells Fire for 3000 as the original does, 10 a frame in 300 frames
           (a headless run counts them), and from his gift the hero has
           studied. Walked out (the hall's door edited to lead to Eolis's
           Magician's) and in to Eolis's Magician, he asks his price.
file       Studied and learned, the hero dies; the respawn writes
           lesson.studied = yes and lesson.learned = yes to file 1. Quitting
           and continuing file 1, he has both: MP refill runs 120 and 15. File
           1 edited as a file from before lesson.studied, with Fire among its
           spells: continued, he has studied. A new game in file 2 has neither.
original   With Faxanadu Remastered off: "I shall give magic for 200 Golds.";
           A takes 200 and gives 40 MP, then "Watch your remaining power
           level.", as the original does.

Screenshots of his lines, the game's "not enough" and the original's sale go
in OUT, each with the box's blinking "?" or arrow lit.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "block"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "movement"))
import harness  # noqa: E402
from harness import (AREA, BUILDINGS, ENTITIES, GOLD, HP, MESSAGE_ID, POS_X, SCREEN_SAVED_PALETTE,  # noqa: E402
                     fixture, parse_args)
import fax_e2e  # noqa: E402  (on the path through harness)
from fax_e2e import (act, choose_file, dissolve, page_through, tcp, title_menu, type_out,  # noqa: E402
                     until, walk)
import play  # noqa: E402

PARTS = ("unstudied", "buy", "refuse", "hall", "file", "original")
PACKAGE = ("faxanadu.remastered", "remastered")
RECORD, MANA = "faxanadu.magician-lesson", "faxanadu.mana-refill"   # save-state records: [1] learned, [4] studied;
LEARNED, STUDIED = 1, 4                                             # mana's [1] seen, [2] MP seen, [3] left
ZONE_NAMES = "faxanadu.zone-names"                                  # its save-state record
AREA_SAVED_AREA, AREA_SAVED_SCREEN, PLAYER_SAVED_POS, AREA_REGION = 0x3D5, 0x3D6, 0x3D8, 0x435
MP, SPECIAL_ITEMS, PORTRAIT, PLAYER_IS_DEAD = 0x39A, 0x42C, 0x3C7, 0x438
INTERRUPT_COUNTER, LIT = 0x1A, 0x14                                 # a box's "?" or arrow shows while it has $10
SELECTED_MAGIC, MAGIC_INVENTORY, NUMBER_OF_MAGIC_SPELLS = 0x3C0, 0x3A9, 0x3C5
RING_OF_ELF, NO_PORTRAIT, DELUGE, FIRE, DELUGE_COST = 0x80, 0xFF, 0, 2, 2
EOLIS, MAGICIANS_SCREEN, MAGICIANS_DOOR = 0, 6, 0x9C                # EOLIS_DOOR_LOCATIONS: building $05
MAGICIANS_STEP = 32                                                 # x on his step, beside him (he is at 16)
MIST, HALL_SCREEN, HALL_DOOR, HALL = 2, 0x29, 0x7C, 0x1C            # MIST_DOOR_LOCATIONS: building $1C
PALETTE_MIST, REGION_MIST = 0x0A, 2
SLOT_ID, NOT_ENOUGH, MAGIC_200G, WATCH_POWER = 0xC3, 0x03, 0x15, 0x16   # message IDs
HALL_ASKS, HALL_THANKS = 0x17, 0x79                                 # the hall's question (Fire, 3000) and last word
PRICE, PAY, STOCK_PRICE, STOCK_MP, FIRE_PRICE, STOCK_PAY = 30000, 150, 200, 40, 3000, 10
PAUSE = 60                                                          # IScripts_ProgressivelySubtractGold's closing wait
ASKING, SAYING, TELLING = 1, 3, 4                                   # fax_lesson.c's phases
PRICE_LINE, TAUGHT, NOTHING_MORE, TEACHES, NOT_READY = 1, 2, 3, 4, 5   # and lines
WAIT, STEP, TAUGHT_WAIT, TAUGHT_STEP = 240, 30, 120, 15             # fax_mana.c's schedules
FILE = "faxanadu_file1.txt"                                         # the gate's new game is file 1
CAST_AT = 10                                                        # the frame of a headless run Up+B goes down on
PRESS_AT = 10                                                       # the frame of a headless run A goes down on


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
    """Through a Magician's door, and left to him: he stands on a step at the
    left wall (x 16), in both magic shops, so a jump left onto it. Going in
    draws the HUD, so it shows gold a fixture set."""
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


def golds(run):
    """Gold after each frame of a headless run."""
    return [int.from_bytes(r[GOLD:GOLD + 3], "little") for r in run]


def counted(run, before, amount, per_frame):
    """The frames a headless run took gold on: `amount` from `before`, in
    consecutive frames of `per_frame` each. Returns (first, last)."""
    gold = golds(run)
    drops = [(i, gold[i - 1] - gold[i]) for i in range(1, len(gold)) if gold[i] != gold[i - 1]]
    assert gold[0] == before and gold[-1] == before - amount, (gold[0], gold[-1])
    assert drops and all(d == per_frame for _, d in drops), sorted({d for _, d in drops})
    frames = [i for i, _ in drops]
    assert frames == list(range(frames[0], frames[0] + amount // per_frame)), (frames[0], frames[-1], len(frames))
    return frames[0], frames[-1]


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

    def his_line(which, name=None):
        """His line `which` up and typed out, through message $C3 (no pause in
        it, and TextBox_MessagePaused can be stale: only MessageID counts),
        with a screenshot if `name`."""
        g.until("his line to start", lambda: g.ram(MESSAGE_ID) == SLOT_ID, 600)
        type_out(f"his line {which}", 900, pauses=False)
        s = lesson()
        phase = {TEACHES: TELLING, PRICE_LINE: ASKING}.get(which, SAYING)
        assert (s["line"], s["phase"]) == (which, phase), (which, s)
        return shot_lit(name) if name else None

    def next_page():
        """A on a typed-out page of his: the next types."""
        act("a", 2, 0)

    def pays_in_frames(state, name, before, amount, per_frame, done):
        """A headless run from `state` (a question typed out), A on it on
        PRESS_AT: the payment counts `amount` from `before`, `per_frame` a
        frame in consecutive frames, and the script's next step (done(ram):
        his last line typing, or the hall's Fire in the spells) comes no
        sooner than the game's 60-frame wait after. Returns (first, last,
        that frame)."""
        frames = PRESS_AT + 2 + amount // per_frame + PAUSE + 60
        [run] = g.headless_all([(state, [(PRESS_AT, "A"), (PRESS_AT + 4, "-")], frames, g.mods("on"))])
        first, last = counted(run, before, amount, per_frame)
        after = next((i for i in range(last, frames) if done(run[i])), None)
        assert after is not None and after - last >= PAUSE, (name, last, after)
        print(f"ok: {name}: A on frame {PRESS_AT}; {amount} taken {per_frame} a frame on frames {first}-{last} "
              f"({last - first + 1} frames), the next step on frame {after}, {after - last} frames after")
        return first, last, after

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

    def spells():
        n, listed = g.ram(NUMBER_OF_MAGIC_SPELLS), g.ram(MAGIC_INVENTORY, 4)
        return g.ram(SELECTED_MAGIC), list(listed[:n])

    try:
        door = g.state("lesson-door")
        for part in PARTS:
            if not args.wants(part):
                continue
            remastered = part != "original"
            g.set_features({PACKAGE: remastered})
            g.launch()

            if part == "unstudied":
                start("unstudied", gold_bytes(40000))
                s = lesson()
                assert s["enabled"] and not s["studied"] and not s["learned"] and s["price"] == PRICE, s
                talk(SLOT_ID)
                his_line(TEACHES, "teaches")
                next_page()
                his_line(NOT_READY, "not-studied")
                page_through("his word", done_talking, 1200)
                s = lesson()
                assert gold() == 40000 and not s["learned"] and not s["studied"], (gold(), s)
                assert (s["told"], s["not_ready"], s["offers"], s["short"]) == (1, 1, 0, 0), s
                print("ok: not having studied at the magic hall, with 40000 Golds: what he teaches, then \"First "
                      "study at the magic hall in the Mist.\", and the end; no gold taken or checked")

            if part == "buy":
                start("buy", gold_bytes(PRICE), {RECORD: {STUDIED: 1}})
                s = lesson()
                assert s["studied"] and not s["learned"], s
                talk(SLOT_ID)
                his_line(TEACHES)
                next_page()
                g.until("his price to start", lambda: g.ram(MESSAGE_ID) == SLOT_ID, 600)
                typing = g.save("price-typing")                     # his price as it types, for the record's check
                price = his_line(PRICE_LINE, "price")
                asked = g.save("price-up")
                pays_in_frames(asked, "his price", PRICE, PRICE, PAY, lambda ram: ram[MESSAGE_ID] == SLOT_ID)
                act("a", 2, 0)                                      # yes: a new press on the question
                until("the lesson to be paid", lambda: lesson()["learned"], 900)
                his_line(TAUGHT, "taught")
                assert gold() == 0, gold()
                page_through("his last line", done_talking, 1200)
                s, mana = lesson(), tcp("mana_state")
                assert gold() == 0 and s["learned"] and (s["offers"], s["taught"], s["pays"]) == (1, 1, 200), s
                assert (mana["wait"], mana["step"]) == (TAUGHT_WAIT, TAUGHT_STEP), mana
                print(f"ok: studied, with {PRICE} Golds he asks \"It will cost {PRICE} Golds.\"; A takes it all, in "
                      f"{s['pays']} turns of {PAY}, and he says it is learned; MP refill now waits 120 frames and "
                      "steps 15")
                talk(SLOT_ID)
                his_line(NOTHING_MORE, "nothing-more")
                page_through("his word", done_talking, 1200)
                s = lesson()
                assert gold() == 0 and s["nothing_more"] == 1 and s["offers"] == 1 and s["told"] == 1, s
                print("ok: asked again, he has nothing more to teach, and takes nothing")
                taught_refill()
                # A save state from while his price typed, loaded after his other lines took the slot: the
                # record puts the conversation back (not learned, asking) and the price in the slot.
                g.load(typing)
                type_out("his price, again", 900, pauses=False)
                s = lesson()
                assert not s["learned"] and s["studied"] and (s["phase"], s["line"]) == (ASKING, PRICE_LINE), s
                again = shot_lit("price-reloaded")
                assert again.read_bytes() == price.read_bytes(), "the reloaded price is not the same picture"
                act("b", 2, 0)
                until("the box to close", done_talking, 300)
                print("ok: a save state from while his price typed, loaded after his other lines, shows the price "
                      "again, picture for picture, not learned")

            if part == "refuse":
                start("refuse", gold_bytes(PRICE), {RECORD: {STUDIED: 1}})
                talk(SLOT_ID)
                his_line(TEACHES)
                next_page()
                his_line(PRICE_LINE)
                act("b", 2, 0)
                until("the box to close", done_talking, 300)
                s = lesson()
                assert gold() == PRICE and not s["learned"] and s["declined"] == 1 and s["phase"] == 0, s
                print("ok: B on his price closes the box; nothing taken, nothing learned")
                start("short", gold_bytes(PRICE - 1), {RECORD: {STUDIED: 1}})
                talk(SLOT_ID)
                his_line(TEACHES)
                next_page()
                his_line(PRICE_LINE)
                act("a", 2, 0)
                until("\"This is not enough Golds.\"", lambda: g.ram(MESSAGE_ID) == NOT_ENOUGH, 300)
                type_out("the game's refusal")
                shot_lit("not-enough")
                page_through("the refusal", done_talking, 1200)
                s = lesson()
                assert gold() == PRICE - 1 and not s["learned"] and s["short"] == 1 and s["taught"] == 0, s
                print(f"ok: with {PRICE - 1} Golds, A gets the game's \"This is not enough Golds.\" and the end; "
                      "nothing taken, nothing learned")

            if part == "hall":
                shop = g.state("eolis-meat-shop")
                g.load(shop)
                g.wait(2)
                eolis_palette, eolis_region = g.ram(SCREEN_SAVED_PALETTE), g.ram(AREA_REGION)
                hall_door = out / "hall-door.cycstate"
                fixture(shop, hall_door, {AREA_SAVED_AREA: MIST, AREA_SAVED_SCREEN: HALL_SCREEN,
                                          SCREEN_SAVED_PALETTE: PALETTE_MIST, PLAYER_SAVED_POS: HALL_DOOR,
                                          AREA_REGION: REGION_MIST, HP: 0x50, **gold_bytes(FIRE_PRICE + 500)},
                        records={ZONE_NAMES: {5: MIST, 6: HALL_SCREEN}})
                g.load(hall_door)
                g.walk_out()
                assert g.where() == (MIST, HALL_SCREEN), g.where()
                g.poke({ENTITIES + i: 0xFF for i in range(8)})
                column = (HALL_DOOR & 15) * 16
                g.walk_to(column - 6, column + 4)
                g.settle()
                go_in(g)
                assert g.where() == (BUILDINGS, HALL), g.where()
                assert not lesson()["studied"] and FIRE not in spells()[1], (lesson(), spells())
                talk(HALL_ASKS)
                type_out("the hall's question")
                shot_lit("hall-question")
                asked = g.save("hall-asks")
                pays_in_frames(asked, "the magic hall's Fire", FIRE_PRICE + 500, FIRE_PRICE, STOCK_PAY,
                               lambda ram: FIRE in ram[MAGIC_INVENTORY:MAGIC_INVENTORY + ram[NUMBER_OF_MAGIC_SPELLS]])
                act("a", 2, 0)
                until("Fire paid for", lambda: gold() == 500, 900)
                page_through("the hall's Magician", done_talking, 2400)
                s = lesson()
                assert s["studied"] and not s["learned"] and s["fire_given"] == 1 and s["pays"] == 0, s
                assert gold() == 500 and FIRE in spells()[1], (gold(), spells())
                print(f"ok: the magic hall's Magician sells Fire for {FIRE_PRICE}, {STOCK_PAY} a frame, as the "
                      "original; from his gift the hero has studied")
                bought = g.save("hall-bought")
                to_eolis = out / "hall-to-eolis.cycstate"
                fixture(bought, to_eolis, {AREA_SAVED_AREA: EOLIS, AREA_SAVED_SCREEN: MAGICIANS_SCREEN,
                                           SCREEN_SAVED_PALETTE: eolis_palette, PLAYER_SAVED_POS: MAGICIANS_DOOR,
                                           AREA_REGION: eolis_region, HP: 0x50, SPECIAL_ITEMS: RING_OF_ELF,
                                           **gold_bytes(PRICE)},
                        records={ZONE_NAMES: {5: EOLIS, 6: MAGICIANS_SCREEN}})
                g.load(to_eolis)
                g.walk_out()
                assert g.where() == (EOLIS, MAGICIANS_SCREEN), g.where()
                g.poke({ENTITIES + i: 0xFF for i in range(8)})
                column = (MAGICIANS_DOOR & 15) * 16
                g.walk_to(column - 6, column + 4)
                g.settle()
                go_in(g)
                talk(SLOT_ID)
                his_line(TEACHES)
                next_page()
                his_line(PRICE_LINE, "hall-then-price")
                act("b", 2, 0)
                until("the box to close", done_talking, 300)
                s = lesson()
                assert s["studied"] and (s["offers"], s["not_ready"], s["declined"]) == (1, 0, 1), s
                print("ok: walked from the magic hall to Eolis's Magician, he asks his price")

            if part == "file":
                start("file", {PLAYER_IS_DEAD: 1}, {RECORD: {LEARNED: 1, STUDIED: 1}}, enter=False)
                assert lesson()["learned"] and lesson()["studied"]
                saves = tcp("files_state")["saves"]
                dissolve()
                page_through("the respawn", lambda: tcp("files_state")["saves"] > saves and not g.box_open(), 3000)
                path = out / "game" / FILE
                saved = read_file(path)
                assert (saved.get("lesson.learned"), saved.get("lesson.studied")) == ("yes", "yes"), saved
                print("ok: studied and learned, a death's respawn writes lesson.studied = yes and lesson.learned = "
                      "yes to file 1")
                g.close()
                g.launch()
                title_menu()
                choose_file(False, 1)                               # CONTINUE, file 1
                s, mana = lesson(), tcp("mana_state")
                assert s["learned"] and s["studied"], s
                assert (mana["wait"], mana["step"]) == (TAUGHT_WAIT, TAUGHT_STEP), mana
                print("ok: quitting and continuing file 1, he has studied and learned: MP refill waits 120, steps 15")
                g.close()
                assert FIRE not in [int(v) for v in saved.get("spells", "").split()], saved
                write_file(path, {"lesson.studied": None, "lesson.learned": None, "spells": f"{DELUGE} {FIRE}"})
                g.launch()
                title_menu()
                choose_file(False, 1)
                s = lesson()
                assert s["studied"] and not s["learned"], s
                print("ok: file 1 as from before lesson.studied, with Fire among its spells: continued, he has "
                      "studied (not learned)")
                g.close()
                g.launch()
                title_menu()
                choose_file(True, 2)                                # NEW GAME, file 2
                s, mana = lesson(), tcp("mana_state")
                assert not s["learned"] and not s["studied"] and (mana["wait"], mana["step"]) == (WAIT, STEP), \
                    (s, mana)
                print("ok: a new game in file 2 has neither")

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


def write_file(path, changes):
    """Edit a save file's fields in place: a value sets one, None removes it."""
    lines, seen = [], set()
    for line in path.read_text().splitlines():
        key = line.split("=", 1)[0].strip() if "=" in line and not line.startswith("#") else None
        if key in changes:
            seen.add(key)
            if changes[key] is not None:
                lines.append(f"{key} = {changes[key]}")
            continue
        lines.append(line)
    lines += [f"{k} = {v}" for k, v in changes.items() if k not in seen and v is not None]
    path.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
