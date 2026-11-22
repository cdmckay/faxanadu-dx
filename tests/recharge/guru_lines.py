"""Play a Guru's Elixir lines end to end in a hidden window.

    python3 tests/recharge/guru_lines.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/guru-lines

The game runs uncapped and the script waits on the game's own state, never on
the clock, on a reserved TCP port (tests/e2e/fax_e2e.py).

Every part starts in Conflate's temple, where a new game in file 1 came back
after dying outside Eolis's gate with Conflate his spawn point (a saved game,
fixtures "guru-lines-temple"). Save-state edits then give the hero what each
part needs (external prerequisites, not coverage): the Elixir charged
(SpecialItems' bit) or spent (Recharging items' record), and for the second
part, nothing told. Conflate's Guru's script checks for the Battle Suit first,
then sets the spawn point, as every temple Guru's does.

1. restored: with the Elixir spent, the Guru opens with "Your Elixir is
   restored. Come back when used." as he charges it, then greets the hero
   ("You need peace of mind. I will meditate with you.") and offers Save
   files' menu, as he would have.
2. can: with a charged Elixir and nothing told, he opens with "If your Elixir
   is used, a Guru will restore it.", then greets the hero and offers the
   menu; saving there writes recharge.elixir_told = yes to file 1. Talking to
   him again, he only greets. Quitting and continuing file 1, he only greets.
3. none: with no Elixir, he only greets, and nothing is told.
4. original: with the remaster off and a charged Elixir, he greets the hero
   and shows the mantra, as the original does: no line.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
import fax_e2e  # noqa: E402
from fax_e2e import (Game, act, box_open, choose_file, page_through, page_to, parse_args, ram, tcp,  # noqa: E402
                     title_menu, type_out, until, wait)

AREA, SCREEN, MESSAGE_ID, SPECIAL_ITEMS = 0x24, 0x63, 0x213, 0x42C
BUILDINGS, CONFLATE_TEMPLE_SCREEN = 4, 0x2B                         # DEST_SCREEN_FOR_TEMPLE_SPAWN[5]
BIT_ELIXIR = 0x08
SLOT_ID, MEDITATE_WITH_YOU, NEGATIVE_THOUGHTS = 0xC3, 0x22, 0x24    # the remaster's line; the greeting; death's
TALK_SAYING, LINE_RESTORED, LINE_CAN = 2, 1, 2                      # fax_recharge.c's Talk phase and lines
GURU_MENU = 1                                                       # fax_files.c's GuruPhase
PASSWORD_SCRATCH = (0x4C6, 0x27)                                    # Password_TempY .. the encoded state: the mantra's
FILE = "faxanadu_file1.txt"                                         # the gate's new game is file 1
PACKAGE = ("faxanadu.dx", "dx")
RECORD = "faxanadu.recharge"                                        # its save-state record: [5] the Elixir spent,
SPENT, TOLD = 5, 9                                                  # [9] a Guru has told the hero
PARTS = ("restored", "can", "none", "original")


def state():
    return tcp("recharge_state")


def in_temple():
    return (ram(AREA), ram(SCREEN)) == (BUILDINGS, CONFLATE_TEMPLE_SCREEN)


def read_file(path):
    fields = {}
    for line in path.read_text().splitlines():
        if line.strip() and not line.startswith("#"):
            key, value = (part.strip() for part in line.split("=", 1))
            fields[key] = value
    return fields


@fax_e2e.recipe("guru-lines-temple", parent="conflate-words")
def _temple(g):
    """Dead outside Eolis's gate with Conflate's temple his spawn point: back
    in Conflate's temple, after the Guru's words, with file 1 in play."""
    page_through("Conflate's temple", in_temple)
    wait(60)


def main():
    args = parse_args(__doc__, parts=PARTS)
    game = Game.from_args(args)
    out = game.out
    shot = game.shot
    launched = {}

    def start(name, elixir, spent=0, told=0, on=True):
        """Launch (the remaster on or off) in Conflate's temple, the Elixir
        charged or not, spent or not, told or not."""
        if game.running() and launched.get("on") != on:
            game.close()
        if not game.running():
            game.set_features({PACKAGE: on})
            game.launch()
            launched["on"] = on
        target = out / f"{name}.cycstate"
        fax_e2e.fixture(temple, target, {SPECIAL_ITEMS: BIT_ELIXIR if elixir else 0},
                        {RECORD: {SPENT: spent, TOLD: told}} if on else None)
        tcp("load_state", path=str(target))
        wait(4)
        assert in_temple() and not box_open(), "Conflate's temple"
        s = state()
        assert s["enabled"] == on and (not on or (s["elixir_spent"], s["elixir_told"]) == (bool(spent), bool(told))), s
        return s

    def counts(before):
        """How many charges, restored lines and "can" lines since `before` (the
        counts run on from earlier parts in the same game)."""
        s = state()
        return tuple(s[k] - before[k] for k in ("guru_recharges", "restored_lines", "can_lines"))

    def talk(walk=True):
        """Walk left to the Guru (from where the hero came back) and press Up;
        the first message the box shows."""
        if walk:
            act("left", 170, 10)
        assert ram(MESSAGE_ID) not in (SLOT_ID, MEDITATE_WITH_YOU), "MessageID reads as a line already"
        act("up", 4, 10)
        seen = []

        def first():
            seen.append(ram(MESSAGE_ID))
            return seen[-1] in (SLOT_ID, MEDITATE_WITH_YOU)
        until(f"his first line (saw {sorted(set(seen))})", first, 300)
        return seen[-1]

    def line_shown(which, name):
        """His Elixir line is up and typed out: Recharging items is showing it
        through message $C3. A screenshot, then A goes on to his greeting."""
        type_out("the line to type out", 900, pauses=False)          # no pause in it; the flag may be stale
        s = state()
        assert s["talk"] == TALK_SAYING and s["line"] == which and s["elixir_charged"] and not s["elixir_spent"], s
        wait(2)
        shot(name)
        act("a", 4, 4)
        until("his greeting", lambda: ram(MESSAGE_ID) == MEDITATE_WITH_YOU, 300)
        return s

    def to_menu(name):
        """Page from his greeting to Save files' menu (no A on it), and screenshot it."""
        page_to(lambda s: s["guru"] == GURU_MENU, "the Guru's menu", "files_state")
        wait(10)
        shot(name)

    def leave():
        """B leaves the Guru's menu, or any of his lines: the box closes."""
        for _ in range(20):
            if not box_open():
                break
            act("b", 6, 20)
        until("the box to close", lambda: not box_open(), 300)
        wait(20)

    try:
        temple = game.state("guru-lines-temple")

        if args.wants("restored"):
            # 1. A spent Elixir: the restored line first, then the greeting and the menu.
            before = start("spent", elixir=False, spent=1)
            assert talk() == SLOT_ID, "the line comes first"
            line_shown(LINE_RESTORED, "restored-line")
            assert counts(before) == (1, 1, 0), (before, state())
            to_menu("restored-then-menu")
            assert state()["elixir_charged"] and ram(SPECIAL_ITEMS) & BIT_ELIXIR
            leave()
            print("ok: with the Elixir spent, Conflate's Guru says \"Your Elixir is restored. Come back when used.\" "
                  "as he charges it, then greets the hero and offers Save files' menu")

        if args.wants("can"):
            # 2. A charged Elixir, nothing told: the line once, kept in the file.
            before = start("charged", elixir=True)
            assert talk() == SLOT_ID, "the line comes first"
            s = line_shown(LINE_CAN, "can-line")
            assert counts(before) == (0, 0, 1) and s["elixir_told"], (before, s)
            saves = tcp("files_state")["saves"]
            to_menu("can-then-menu")
            page_to(lambda f: f["saves"] == saves + 1, "the save", "files_state")   # A: "Remember my journey."
            leave()
            saved = read_file(out / "game" / FILE)
            assert saved.get("recharge.elixir_told") == "yes" and int(saved["spawn"]) == 5, saved
            print("ok: with a charged Elixir, Conflate's Guru says \"If your Elixir is used, a Guru will restore it.\", "
                  "then greets the hero and offers the menu; saving writes recharge.elixir_told = yes to file 1")
            assert talk(walk=False) == MEDITATE_WITH_YOU, "told: no line the second time"
            assert counts(before) == (0, 0, 1), (before, state())
            leave()
            print("ok: talking to him again, he only greets the hero")
            game.close()
            game.launch()                                           # quit, then continue file 1
            title_menu()
            choose_file(False, 1)
            until("Conflate's temple", lambda: in_temple() and ram(0x0E) == 0, 1800)
            wait(30)
            s = state()
            assert s["elixir_told"] and s["elixir_charged"] and not s["elixir_spent"], s
            assert talk() == MEDITATE_WITH_YOU, "continued, told: no line"
            assert state()["can_lines"] == 0, state()
            leave()
            print("ok: quitting and continuing file 1, he only greets the hero")

        if args.wants("none"):
            # 3. No Elixir: no line, nothing told.
            before = start("none", elixir=False)
            assert talk() == MEDITATE_WITH_YOU, "no Elixir: no line"
            to_menu("none-menu")
            s = state()
            assert not s["elixir_told"] and counts(before) == (0, 0, 0) and s["talk"] == 0, (before, s)
            leave()
            print("ok: with no Elixir, he only greets the hero, and tells him nothing")

        if args.wants("original"):
            # 4. The remaster off: the original's greeting and mantra, no line.
            start("original", elixir=True, on=False)
            scratch = ram(*PASSWORD_SCRATCH)
            assert talk() == MEDITATE_WITH_YOU, "the original: no line"
            type_out("the greeting to type out")
            act("a", 4, 4)
            until("the mantra", lambda: ram(*PASSWORD_SCRATCH) != scratch, 300)   # Password_GenerateStateAndReset
            wait(90)
            shot("original-mantra")
            assert ram(SPECIAL_ITEMS) & BIT_ELIXIR and not state()["enabled"]
            print("ok: with the remaster off, the Guru greets the hero and shows the mantra, with no line")
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
