"""Play Guru travel end to end in a hidden window and check what it changes.

    python3 tests/travel/guru_travel.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/travel

The game runs uncapped (about 5x real time; --realtime to watch it at normal
speed) and the script waits on the game's own state, never on the clock, so
each run takes a free TCP port, so any number can go in parallel. Each part
is its own game (--part; tools/test.sh plays them at once).

Copies the build to OUT (so the faxanadu_gurus.txt sidecar stays there) and
starts from a saved game, travel-conflate-temple: the hero has died and woken
in Conflate's temple, the furthest, with a file in play. faxanadu_gurus.txt
says the Gurus of Eolis, Apolune, Mascon and Conflate have been visited, so
Guru travel's menu (#206) offers Eolis, Apolune and Mascon. The parts:

  decline  a new game meets only Eolis's Guru; B on the offer ends the talk
  menu     A on the offer: the menu, a row a town, the cursor on the first;
           Up wraps; B says farewell, and he stays
  back     "Send me to another Guru." opens the menu at once; B goes back to
           the Guru's choice, where B leaves
  journey  Down, A: the second row's Guru, Apolune's, everything kept but the
           place; a death after it comes back at Conflate
  held     A held from the press that opens the menu chooses nothing; a new
           press does
  one      with one Guru to offer, a menu of one row, which Down keeps
  resume   states saved while the menu opens and while it waits load and go on
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
import fax_e2e  # noqa: E402
from fax_e2e import (Game, accept_offer, act, batch, fixture, frame, guru_send, let_go, page_through,  # noqa: E402
                     page_to, parse_args, ram, tcp, travel_menu, travel_to, type_out, until, wait)

TEMPLE_SPAWN_POINT, PLAYER_IS_DEAD, MESSAGE_ID = 0x439, 0x438, 0x213
ISCRIPT_PORTRAIT_ID, PPU_BUFFER = 0x3C7, 0x1F                     # $FF once a conversation ends; Read, WriteOffset
AREA, SCREEN, HP, MP, GOLD, JOY1_HELD = 0x24, 0x63, 0x431, 0x39A, 0x392, 0x16
BUILDINGS = 4
TEMPLE_SCREEN = [0x02, 0x0B, 0x10, 0x1E, 0x23, 0x2B, 0x33, 0x3C]   # DEST_SCREEN_FOR_TEMPLE_SPAWN
EOLIS, APOLUNE, MASCON, CONFLATE = 0, 1, 3, 5
MEDITATE_WITH_YOU, NEGATIVE_THOUGHTS = 0x22, 0x24                  # the Guru's greeting; the death message
OFFER, FAREWELL, DEPART = 1, 2, 3                                  # fax_travel.c's lines
SLOT_ID = 0xC3                                                     # their message, the line slot
IDLE, OPENING, MENU = 0, 7, 8                                      # fax_travel.c's Phase
GURU_IDLE, GURU_MENU = 0, 1                                        # fax_files.c's GuruPhase
ROWS = ["EOLIS", "APOLUNE", "MASCON"]                              # the menu from Conflate, as the screen shows it
BUTTON_A = 0x80
HELD = 600                                                         # frames A is held from the choice
# What the original's mantra holds (Password_EncodeGameState): the spawn point, the title, the special
# items, the quests, the five inventories, their selections and their counts.
PROGRESS = ((0x439, 1), (0x437, 1), (0x42C, 2), (0x39D, 24), (0x3BD, 10))


def progress():
    """The bytes of PROGRESS, in one batch (one frame's)."""
    return [r["hex"] for r in fax_e2e.batch(*(("read_ram", dict(addr=a, len=n)) for a, n in PROGRESS))]


def in_temple(temple):
    return (ram(AREA), ram(SCREEN)) == (BUILDINGS, TEMPLE_SCREEN[temple])


def die_and_respawn(temple):
    """Page through the death message until the hero is back in `temple`."""
    until("the death message", lambda: ram(MESSAGE_ID) == NEGATIVE_THOUGHTS, 600)
    page_through(f"temple {temple}", lambda: in_temple(temple))


@fax_e2e.recipe("travel-conflate-temple", parent="conflate-words")
def _conflate_temple(g):
    """Died outside Eolis's gate with Conflate's temple his spawn point: back
    in Conflate's temple, after the Guru's words."""
    die_and_respawn(CONFLATE)
    wait(60)


PARTS = ("decline", "menu", "back", "journey", "held", "one", "resume")   # each its own game


def main():
    args = parse_args(__doc__, parts=PARTS)
    game = Game.from_args(args)
    out = game.out

    def travel():
        return tcp("travel_state")

    def talk():
        """Walk left to the Guru and talk: his greeting."""
        act("left", 170, 10)
        act("up", 4, 10)
        until("the Guru's greeting", lambda: ram(MESSAGE_ID) == MEDITATE_WITH_YOU)

    def to_offer():
        """Talk, and page to Guru travel's offer, typed out: a file is in
        play, so the Guru asks first; A on his choice remembers the journey,
        then come the intro and the offer, with no A on it (page_to())."""
        talk()
        page_to(lambda s: s["text"] == OFFER, "Guru travel's offer")

    def to_choice():
        """Talk, and page to the Guru's choice (Save files' menu), typed out."""
        talk()
        page_to(lambda s: s["guru"] == GURU_MENU, "the Guru's choice", "files_state")

    def said(line):
        """Until Guru travel's `line` has begun, in the dialogue box opened again
        over the menu (MessageID is message $C3's once it loads, and 0 while
        the box opens), and has typed out (type_out())."""
        until(f"line {line}", lambda: travel()["text"] == line and ram(MESSAGE_ID) == SLOT_ID, 600)
        type_out(f"line {line}")

    def close_box(button="a"):
        """Dismiss the last line once it has typed out, and let the conversation
        end (IScript_PortraitID $FF: TextBox_Closed can read 0 with no box up)."""
        type_out("the line to type out")
        act(button, 6, 10)
        until("the conversation to end", lambda: ram(ISCRIPT_PORTRAIT_ID) == 0xFF, 300)

    def choice_up():
        """The Guru's choice is up and waits: Save files' menu, its message
        shown whole (IScripts_LoadAndShowMessage ends with MessageID 0) and
        sent to the screen. Read, not paged to: A there would choose."""
        files, message, buffer = batch(("files_state", {}), ("read_ram", dict(addr=MESSAGE_ID, len=1)),
                                       ("read_ram", dict(addr=PPU_BUFFER, len=2)))
        sent = bytes.fromhex(buffer["hex"])
        return files["guru"] == GURU_MENU and message["hex"] == "00" and sent[0] == sent[1]

    def check_menu(state, cursor=0, rows=ROWS):
        """The menu on screen: a row a town, in temple order, as the game drew them."""
        assert state["phase"] == MENU and state["shown"] == rows and state["cursor"] == cursor, state
        assert state["dest"] == -1, state

    def with_gurus(gurus="Eolis\nApolune\nMascon\nConflate\n", visited=0b101011):
        """The game with the visited Gurus the part needs."""
        (out / "game" / "faxanadu_gurus.txt").write_text(gurus)
        game.launch()
        state = tcp("travel_state")
        assert state["served"] and state["visited"] == visited, state

    try:
        raw = game.state("travel-conflate-temple")
        temple = out / "conflate-temple.cycstate"
        fixture(raw, temple, {HP: 0x2A, MP: 0x11, GOLD: 0xE8, GOLD + 1: 0x03, GOLD + 2: 0x00})   # 1000 gold

        if args.wants("decline"):
            # A new game meets only Eolis's Guru.
            game.launch(at="gate")
            assert travel()["visited"] == 1, "a new game has met only Eolis's Guru"
            game.close()
            with_gurus()
            # B at the offer: the conversation ends where it is.
            tcp("load_state", path=str(temple))
            to_offer()
            game.shot("offer")
            close_box("b")
            assert travel()["dest"] == -1 and travel()["menus"] == 0 and in_temple(CONFLATE)
            print("ok: declining the offer stays in Conflate")

        if args.wants("menu"):
            with_gurus()
            # A on the offer: the menu of the Gurus visited up to the furthest temple, the cursor on the first.
            tcp("load_state", path=str(temple))
            to_offer()
            accept_offer()
            state = travel_menu()
            check_menu(state)
            assert state["menu"] == [EOLIS, APOLUNE, MASCON] and not state["back"], state
            assert list(ram(0x208, 5)) == [10, 16, 20, 8, 0], "three rows: the dialogue box's place"
            game.shot("menu")
            print("ok: A on the offer opens the menu: EOLIS, APOLUNE, MASCON, the cursor on EOLIS")
            act("up")
            assert travel()["cursor"] == 2, "Up from the first row wraps to the last"
            act("b")
            said(FAREWELL)
            game.shot("farewell")
            close_box()
            wait(60)
            assert travel()["dest"] == -1 and in_temple(CONFLATE)
            print("ok: Up wraps; B in the menu says farewell, and he stays in Conflate")

        if args.wants("back"):
            with_gurus()
            tcp("load_state", path=str(temple))
            saved = sorted((p.name, p.read_bytes()) for p in (out / "game").glob("faxanadu_file*.txt"))
            to_choice()
            guru_send()
            state = travel_menu()
            check_menu(state)
            assert state["back"], state
            game.shot("menu-from-choice")
            print("ok: \"Send me to another Guru.\" opens the menu at once")
            act("b")
            until("the Guru's choice again", lambda: travel()["phase"] == IDLE and choice_up(), 300)
            assert travel()["dest"] == -1 and list(ram(0x208, 5)) == [10, 16, 20, 8, 0]
            game.shot("back")
            close_box("b")
            assert tcp("files_state")["guru"] == GURU_IDLE and travel()["dest"] == -1 and in_temple(CONFLATE)
            assert sorted((p.name, p.read_bytes()) for p in (out / "game").glob("faxanadu_file*.txt")) == saved
            print("ok: B in the menu goes back to the Guru's choice; B there leaves, nothing saved")

        if args.wants("journey"):
            with_gurus()
            # Travel to Apolune, the second row: everything but the place is kept. Saving with the Guru (A on
            # his choice, in to_offer()) fills HP and MP (Save files, #205), so they are set again once he
            # has, to see the journey keep them.
            tcp("load_state", path=str(temple))
            before = progress()
            to_offer()
            assert (ram(HP), ram(MP)) == (0x50, 0x50), "his save fills HP and MP"
            game.poke({HP: 0x2A, MP: 0x11})
            accept_offer()
            travel_to(APOLUNE, leave=False)       # Down, A
            said(DEPART)
            game.shot("depart")
            close_box()
            until("Apolune's temple", lambda: in_temple(APOLUNE))
            wait(90)
            game.shot("arrived")
            assert ram(TEMPLE_SPAWN_POINT) == CONFLATE, "the furthest temple stays the spawn point"
            assert (ram(HP), ram(MP), ram(GOLD, 3)) == (0x2A, 0x11, bytes([0xE8, 0x03, 0x00])), (ram(HP), ram(MP))
            assert progress() == before, "what a mantra would hold is unchanged by travel"
            print("ok: Down, A: travelled to Apolune keeping HP, MP, gold, spawn point, title, items and quests")

            # Dying after the journey respawns at the furthest temple.
            arrived = out / "apolune-arrived.cycstate"
            tcp("save_state", path=str(arrived))
            fixture(arrived, out / "apolune-death.cycstate", {PLAYER_IS_DEAD: 1})
            tcp("load_state", path=str(out / "apolune-death.cycstate"))
            die_and_respawn(CONFLATE)
            print("ok: death after travel respawns at Conflate")

        if args.wants("held"):
            with_gurus()
            # The Guru's choice up, the cursor on "Send me to another Guru.": A goes down on the frame after
            # the load (one batch), opens the menu and is held on through it. The menu takes only a new A.
            tcp("load_state", path=str(temple))
            to_choice()
            guru_send(choose=False)
            choice = out / "choice.cycstate"
            tcp("save_state", path=str(choice))
            start = batch(("load_state", dict(path=str(choice))), ("action", dict(name="a", frames=HELD)),
                          ("ping", {}))[-1]["frame"]
            travel_menu()
            until("the menu up, A held, for a while", lambda: frame() >= start + HELD - 60, HELD)
            state, held = batch(("travel_state", {}), ("read_ram", dict(addr=JOY1_HELD, len=1)))
            assert int(held["hex"], 16) & BUTTON_A, "A is still held"
            check_menu(state)
            assert state["menu_up"], state
            game.shot("held")
            print(f"ok: A held from the choice, {HELD - 60} frames on, opened the menu and chose no town")
            let_go()
            travel_to(EOLIS, leave=False)         # a new press, on the first row
            assert travel()["dest"] == EOLIS
            print("ok: a new press of A then sends him to Eolis")

        if args.wants("one"):
            with_gurus("Eolis\nConflate\n", 0b100001)
            tcp("load_state", path=str(temple))
            to_offer()
            accept_offer()
            state = travel_menu()
            check_menu(state, rows=["EOLIS"])
            assert state["menu"] == [EOLIS], state
            game.shot("one-row")
            act("down")
            assert travel()["cursor"] == 0, "Down on the only row keeps it"
            travel_to(EOLIS)                      # A, and the departure
            until("Eolis's temple", lambda: in_temple(EOLIS))
            print("ok: one Guru to offer: a menu of one row, EOLIS, and A goes there")

        if args.wants("resume"):
            with_gurus()
            tcp("load_state", path=str(temple))
            to_choice()
            guru_send(choose=False)
            choice = out / "choice.cycstate"
            tcp("save_state", path=str(choice))
            # A state saved while the menu opens: its box drawn, the rows not yet, or not yet sent.
            opening = out / "opening.cycstate"
            for _ in range(8):
                batch(("load_state", dict(path=str(choice))), ("action", dict(name="a", frames=4)))
                _, state = batch(("save_state", dict(path=str(opening))), ("travel_state", {}))
                if state["phase"] in (OPENING, MENU) and not state["menu_up"]:
                    break
            else:
                raise AssertionError("never saved a state while the menu opened")
            print(f"   saved while the menu opened (phase {state['phase']})")
            state = travel_menu()
            act("down")
            assert travel()["cursor"] == 1
            waiting = out / "waiting.cycstate"
            tcp("save_state", path=str(waiting))
            act("b")
            until("the Guru's choice again", choice_up, 300)
            close_box("b")
            assert travel()["phase"] == IDLE

            tcp("load_state", path=str(opening))
            check_menu(travel_menu())
            print("ok: a state saved while the menu opened loads, and the menu comes up whole")
            tcp("load_state", path=str(waiting))
            state = travel_menu()
            check_menu(state, cursor=1)
            game.shot("resumed")
            travel_to(APOLUNE)                    # Down, A, and the departure
            until("Apolune's temple", lambda: in_temple(APOLUNE))
            print("ok: a state saved while the menu waited loads, the cursor where it was, and A goes there")

        if args.wants("decline") or args.wants("menu") or args.wants("back"):
            visited = (out / "game" / "faxanadu_gurus.txt").read_text().split()
            assert visited == ["Eolis", "Apolune", "Mascon", "Conflate"], visited
            print("ok: faxanadu_gurus.txt unchanged:", ", ".join(visited))
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
