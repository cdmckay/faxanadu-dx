"""Play the Magic button end to end in a hidden window.

    python3 tests/magic/magic_button.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/magic

The game runs uncapped and the script waits on the game's own state, never on
the clock, and takes a free TCP port, so any number can run in parallel. The
Magic button is pressed the way a player does: TCP `key` and `pad` feed the
window real SDL key and controller events.

Copies the build to OUT, starts a new game over TCP, pages through the opening
and walks the hero to the gate of Eolis, a door. Then it edits save-state RAM
(an external prerequisite, not coverage) to give him Deluge and 80 MP:

1. Up at the gate goes through it, and so does Up+B, the stock way to cast.
2. Magic (C) at the gate casts Deluge for its 2 MP without going through the
   gate or swinging the sword. A second press while it flies casts nothing.
3. Magic on a controller (Y) casts too.
4. With 1 MP, Magic casts nothing.
5. Away from the gate, Up+B still casts.
6. Next to Conflate's Guru (reached by dying with Conflate as the furthest
   temple, from a saved game of that death as the Guru speaks), Magic casts
   without starting a conversation; Up talks.
7. config.ini rebinds it (MagicKey = V, MagicButton = b), and the window
   writes the bindings back.
8. With the remaster off in the mod selection, as the original, C does nothing.
"""
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
import fax_e2e  # noqa: E402
from fax_e2e import (Game, act, batch, box_open, dissolve, fixture, frame, page_through, parse_args, ram, tcp,  # noqa: E402
                     type_out, until, wait)

AREA, SCREEN, POS_X = 0x24, 0x63, 0x9E
PLAYER_FLAGS, ATTACKING = 0xA4, 0x80
CAST_MAGIC_TYPE, NO_SPELL, DELUGE = 0x2B3, 0xFF, 0
MP, SELECTED_MAGIC, MAGIC_INVENTORY, NUMBER_OF_MAGIC_SPELLS = 0x39A, 0x3C0, 0x3A9, 0x3C5
TEMPLE_SPAWN_POINT, PLAYER_IS_DEAD, MESSAGE_ID, TEXTBOX_PAUSED = 0x439, 0x438, 0x213, 0x215
BUILDINGS, CONFLATE, CONFLATE_TEMPLE = 4, 5, 0x2B                 # temple 5 and its interior screen
OUTSIDE_EOLIS, INSIDE_EOLIS = (0, 0), (0, 1)                        # (area, screen) either side of the gate
GATE = range(188, 200)                                              # Up there goes through the gate
MEDITATE_WITH_YOU, NEGATIVE_THOUGHTS = 0x22, 0x24                  # the Guru's greeting; the death message
DELUGE_COST = 2
PACKAGE, FEATURE = "faxanadu.dx", "dx"                          # the one switch: off is the original game


def never(what, test, frames):
    """`test` stays false for `frames` frames."""
    end = frame() + frames
    while frame() < end:
        assert not test(), what


def up_b():
    """Up+B the stock way: both go down in the same frame, Up held a little longer."""
    batch(("action", dict(name="up", frames=10)), ("action", dict(name="b", frames=4)))


def where():
    return ram(AREA), ram(SCREEN)


SPELLS = {SELECTED_MAGIC: DELUGE, MAGIC_INVENTORY: DELUGE, NUMBER_OF_MAGIC_SPELLS: 1}   # Deluge, his only spell


@fax_e2e.recipe("magic-conflate-words", parent="gate")
def _conflate_words(g):
    """With Deluge, dead outside Eolis's gate with Conflate the furthest
    temple: as the Guru speaks, the dissolve over (the way to Conflate's
    Guru, not what is checked)."""
    g.poke({**SPELLS, TEMPLE_SPAWN_POINT: CONFLATE, PLAYER_IS_DEAD: 1})
    dissolve()


def spell():
    return ram(CAST_MAGIC_TYPE)


def main():
    args = parse_args(__doc__)
    game = Game.from_args(args)
    out = game.out
    config = out / "game" / "config.ini"

    def magic(how, name):
        """Press the Magic button once: a key, or a controller button."""
        if how == "key":
            tcp("key", name=name, frames=3)
        else:
            tcp("pad", buttons=name, frames=3)

    def cast_with(how, name, mp_before):
        """Press Magic, see Deluge cast for its cost, and nothing else happen."""
        before = tcp("magic_state")
        at = where()
        magic(how, name)
        until("Deluge", lambda: ram(MP) != mp_before, 60)
        assert ram(MP) == mp_before - DELUGE_COST, ram(MP)
        assert ram(PLAYER_FLAGS) & ATTACKING == 0, "Magic does not swing the sword"
        assert where() == at, where()
        state = tcp("magic_state")
        assert state["presented"] == before["presented"] + 1 and not state["injected"], state
        assert state["restored"] == state["presented"], state

    def nothing_with(how, name, frames=40):
        mp, at = ram(MP), where()
        magic(how, name)
        never("no spell", lambda: spell() != NO_SPELL, frames)
        assert (ram(MP), where()) == (mp, at), (ram(MP), where())

    def spell_ends():
        until("the spell to end", lambda: spell() == NO_SPELL, 600)

    try:
        words = game.state("magic-conflate-words")
        game.launch(at="gate")
        state = tcp("magic_state")
        assert state["enabled"] and state["binding"] == "C / Y", state
        # The opening: the hero walks toward Eolis and stops to think aloud.
        act("right", 60, 10)
        for _ in range(60):
            if ram(POS_X) > 54:
                break
            if box_open():
                page_through("the hero's thoughts", lambda: not box_open(), 1200)
            act("right", 6, 2)
        assert ram(POS_X) > 54, "the opening is over"
        for _ in range(200):
            if ram(POS_X) >= 192:
                break
            act("right", 6, 2)
        act("left", 2, 4)                                               # face the open field, where a spell flies far
        assert ram(POS_X) in GATE and where() == OUTSIDE_EOLIS, (ram(POS_X), where())
        raw = out / "gate-raw.cycstate"
        tcp("save_state", path=str(raw))
        gate = out / "gate.cycstate"
        fixture(raw, gate, {**SPELLS, MP: 80})
        broke = out / "gate-1mp.cycstate"
        fixture(raw, broke, {**SPELLS, MP: 1})

        # 1. The gate is a door: Up goes through it, and so does Up+B.
        tcp("load_state", path=str(gate))
        game.shot("gate")
        act("up", 4, 4)
        until("Eolis", lambda: where() == INSIDE_EOLIS, 300)
        tcp("load_state", path=str(gate))
        up_b()
        until("Eolis", lambda: where() == INSIDE_EOLIS, 300)
        print(f"ok: Up at the gate goes through it, and so does Up+B (MP {ram(MP)} after)")

        # 2. Magic at the gate casts, and only casts.
        tcp("load_state", path=str(gate))
        cast_with("key", "C", 80)
        assert spell() == DELUGE, spell()
        wait(6)
        game.shot("cast-at-gate")
        presented = tcp("magic_state")["presented"]
        magic("key", "C")                                               # again, while Deluge flies
        until("the press", lambda: tcp("magic_state")["presented"] == presented + 1, 60)
        spell_ends()
        never("a second Deluge", lambda: spell() != NO_SPELL, 20)
        assert where() == OUTSIDE_EOLIS and ram(MP) == 78, (where(), ram(MP))
        print("ok: C at the gate casts Deluge for 2 MP, without going through or swinging the sword")
        print("ok: a second press while Deluge flies casts nothing")

        # 3. On a controller.
        cast_with("pad", "y", 78)
        wait(6)
        game.shot("cast-with-pad")
        spell_ends()
        print("ok: Y on a controller casts too")

        # 4. Not enough MP.
        tcp("load_state", path=str(broke))
        nothing_with("key", "C")
        print("ok: with 1 MP, Magic casts nothing")

        # 5. Up+B still casts, away from the gate.
        tcp("load_state", path=str(gate))
        act("left", 30, 4)
        up_b()
        until("Deluge", lambda: spell() == DELUGE, 60)
        assert ram(MP) == 78 and where() == OUTSIDE_EOLIS
        print("ok: away from the gate, Up+B still casts")
        wait(10)                                                        # Up's hold ends before the next state loads

        # 6. Next to the Guru: Magic casts, Up talks.
        tcp("load_state", path=str(words))                          # dead, as the Guru speaks
        page_through("Conflate's temple", lambda: where() == (BUILDINGS, CONFLATE_TEMPLE))
        wait(60)
        act("left", 170, 10)
        act("right", 2, 4)                                              # turn round, for a spell that flies far
        guru = out / "guru.cycstate"
        tcp("save_state", path=str(guru))
        assert ram(MP) == 80, "death refills MP"
        cast_with("key", "C", 80)
        assert spell() == DELUGE, spell()
        wait(4)
        game.shot("cast-at-guru")
        never("the Guru's greeting", lambda: ram(MESSAGE_ID) == MEDITATE_WITH_YOU, 120)
        tcp("load_state", path=str(guru))
        act("up", 4, 4)
        until("the Guru's greeting", lambda: ram(MESSAGE_ID) == MEDITATE_WITH_YOU, 120)
        type_out("the greeting to type out")
        game.shot("up-at-guru")
        print("ok: next to the Guru, Magic casts without a word from him; Up talks to him")
        game.close()

        # 7. config.ini rebinds it.
        text = config.read_text()
        assert re.search(r"^MagicKey = C$", text, re.M) and re.search(r"^MagicButton = y$", text, re.M), text
        config.write_text(re.sub(r"^MagicButton = y$", "MagicButton = b",
                                 re.sub(r"^MagicKey = C$", "MagicKey = V", text, flags=re.M), flags=re.M))
        game.launch()
        state = tcp("magic_state")
        assert state["binding"] == "V / B", state
        tcp("load_state", path=str(gate))
        nothing_with("key", "C")
        cast_with("key", "V", 80)
        spell_ends()
        cast_with("pad", "b", 78)
        game.close()
        text = config.read_text()
        assert re.search(r"^MagicKey = V$", text, re.M) and re.search(r"^MagicButton = b$", text, re.M), text
        print("ok: MagicKey = V and MagicButton = b in config.ini move it to V and B, and stay there")

        # 8. The remaster off: the original.
        game.set_features({(PACKAGE, FEATURE): False})
        game.launch()
        assert not tcp("magic_state")["enabled"], "the remaster is off"
        tcp("load_state", path=str(gate))
        nothing_with("key", "V")
        nothing_with("pad", "b")
        assert tcp("magic_state")["presses"] == 0
        print("ok: with the remaster off, the Magic button does nothing")
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
