"""Play Guru travel end to end in a hidden window and check what it changes.

    python3 tests/travel/guru_travel.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/travel

The game runs uncapped (about 5x real time; --realtime to watch it at normal
speed) and the script waits on the game's own state, never on the clock, so
each run takes a free TCP port, so any number can go in parallel.

Copies the build to OUT (so the mantra and faxanadu_gurus.txt sidecars stay
there), starts a new game over TCP, then edits save-state RAM to place the hero
(an external prerequisite, not coverage): Conflate is the furthest temple, and
the Gurus of Eolis, Apolune, Mascon and Conflate have been visited. Each
scenario starts from the same state and saves a screenshot per step in OUT.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
import fax_e2e  # noqa: E402
from fax_e2e import Game, act, fixture, page_through, page_to, parse_args, ram, tcp, type_out, until, wait  # noqa: E402

TEMPLE_SPAWN_POINT, PLAYER_IS_DEAD, MESSAGE_ID, TEXTBOX_PAUSED = 0x439, 0x438, 0x213, 0x215
AREA, SCREEN, HP, MP, GOLD = 0x24, 0x63, 0x431, 0x39A, 0x392
BUILDINGS = 4
TEMPLE_SCREEN = [0x02, 0x0B, 0x10, 0x1E, 0x23, 0x2B, 0x33, 0x3C]   # DEST_SCREEN_FOR_TEMPLE_SPAWN
CONFLATE, MASCON = 5, 3
MEDITATE_WITH_YOU, NEGATIVE_THOUGHTS = 0x22, 0x24                  # the Guru's greeting; the death message
OFFER, FAREWELL, DEPART, TO_GURU = 1, 2, 3, 4                      # fax_travel.c's lines (TO_GURU + temple)


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


PARTS = ("decline", "offers", "journey")                            # each its own game


def main():
    args = parse_args(__doc__, parts=PARTS)
    game = Game.from_args(args)
    out = game.out

    def line():
        return tcp("travel_state")["text"]

    def shot_line(name):
        type_out("the line to type out")
        game.shot(name)

    def advance(expect, tries=12):
        """Press A on each typed-out page until the Guru's line `expect` is up
        and has typed out (page_to(): the line and the page read at one frame,
        so no A lands on `expect`)."""
        page_to(lambda s: s["text"] == expect, f"line {expect}", tries=tries)

    def talk_to_guru():
        """Walk left to the Guru, talk, and page through his greeting to the
        offer. A file is in play (Save files is on by default), so he asks
        first; A on his menu remembers the journey, then Guru travel offers."""
        act("left", 170, 10)
        act("up", 4, 10)
        until("the Guru's greeting", lambda: ram(MESSAGE_ID) == MEDITATE_WITH_YOU)
        advance(OFFER)

    def answer(button, expect):
        """Answer the question on screen once it has typed out."""
        type_out("the question to type out")
        act(button)
        until(f"line {expect}", lambda: line() == expect)

    def close_box(button="a"):
        """Dismiss the last line once it has typed out, and let the box close."""
        type_out("the line to type out")
        act(button, 6, 60)

    try:
        # The Gurus of Eolis, Apolune, Mascon and Conflate visited, Conflate
        # the furthest temple: the hero respawned there, with 1000 gold.
        raw = game.state("travel-conflate-temple")
        temple = out / "conflate-temple.cycstate"
        fixture(raw, temple, {HP: 0x2A, MP: 0x11, GOLD: 0xE8, GOLD + 1: 0x03, GOLD + 2: 0x00})   # 1000 gold
        before = None

        def with_gurus():
            """The game with the visited Gurus the scenarios need."""
            (out / "game" / "faxanadu_gurus.txt").write_text("Eolis\nApolune\nMascon\nConflate\n")
            game.launch()
            state = tcp("travel_state")
            assert state["served"] and state["visited"] == 0b101011, state

        if args.wants("decline"):
            # A new game meets only Eolis's Guru.
            game.launch(at="gate")
            assert tcp("travel_state")["visited"] == 1, "a new game has met only Eolis's Guru"
            game.close()
            with_gurus()
            # 1. B at the offer: the conversation ends where it is.
            tcp("load_state", path=str(temple))
            talk_to_guru()
            shot_line("offer")
            close_box("b")
            wait(60)
            assert tcp("travel_state")["dest"] == -1 and in_temple(CONFLATE)
            print("ok: declining the offer stays in Conflate")

        if args.wants("offers"):
            if not game.running():
                with_gurus()
            # 2. Only visited Gurus up to the furthest temple, in order, then farewell.
            tcp("load_state", path=str(temple))
            before = tcp("mantra_now")["mantra"]
            talk_to_guru()
            advance(TO_GURU + 0)
            shot_line("to-eolis")
            answer("b", TO_GURU + 1)
            answer("b", TO_GURU + MASCON)        # Forepaw was never visited
            shot_line("to-mascon")
            asking = out / "asking-mascon.cycstate"
            tcp("save_state", path=str(asking))
            answer("b", FAREWELL)                # neither was Victim; Conflate is here
            shot_line("farewell")
            close_box()
            wait(60)
            assert tcp("travel_state")["dest"] == -1 and in_temple(CONFLATE)
            print("ok: offers Eolis, Apolune, Mascon only, then the farewell")
            print("ok: a state saved before the conversation still loads after it")

            # 5. A state saved mid-conversation resumes it, line and all.
            tcp("load_state", path=str(asking))
            assert line() == TO_GURU + MASCON
            shot_line("resumed")
            answer("a", DEPART)
            close_box()
            until("Mascon's temple", lambda: in_temple(MASCON))
            print("ok: a state saved while asking about Mascon resumes and travels there")

        if args.wants("journey"):
            if not game.running():
                with_gurus()
            # 3. Travel to Mascon: everything but the place is kept.
            tcp("load_state", path=str(temple))
            before = tcp("mantra_now")["mantra"]                    # as the offers part reads it
            talk_to_guru()
            advance(TO_GURU + 0)
            answer("b", TO_GURU + 1)
            answer("b", TO_GURU + MASCON)
            answer("a", DEPART)
            assert tcp("travel_state")["dest"] == MASCON
            shot_line("depart")
            close_box()
            until("Mascon's temple", lambda: in_temple(MASCON))
            wait(90)
            game.shot("arrived")
            assert ram(TEMPLE_SPAWN_POINT) == CONFLATE, "the furthest temple stays the spawn point"
            assert (ram(HP), ram(MP), ram(GOLD, 3)) == (0x2A, 0x11, bytes([0xE8, 0x03, 0x00]))
            assert tcp("mantra_now")["mantra"] == before, "the mantra is unchanged by travel"
            print("ok: travelled to Mascon keeping HP, MP, gold, spawn point and mantra")

            # 4. Dying after the journey respawns at the furthest temple.
            arrived = out / "mascon-arrived.cycstate"
            tcp("save_state", path=str(arrived))
            fixture(arrived, out / "mascon-death.cycstate", {PLAYER_IS_DEAD: 1})
            tcp("load_state", path=str(out / "mascon-death.cycstate"))
            die_and_respawn(CONFLATE)
            print("ok: death after travel respawns at Conflate")

        visited = (out / "game" / "faxanadu_gurus.txt").read_text().split()
        assert visited == ["Eolis", "Apolune", "Mascon", "Conflate"], visited
        print("ok: faxanadu_gurus.txt unchanged:", ", ".join(visited))
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
