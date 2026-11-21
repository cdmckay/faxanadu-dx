"""Play death keeping gold and experience end to end in a hidden window.

    python3 tests/death/death_keeps_gold.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/death

The game runs uncapped and the script waits on the game's own state, never on
the clock, and takes a free TCP port, so any number can run in parallel.

Copies the build to OUT, starts a new game over TCP, then edits save-state RAM
to make a hero (an external prerequisite, not coverage): a Soldier (title 3)
who has earned the next title without seeing a Guru yet, with 5000 experience,
12345 gold, the Mattock bit, Wing Boots running, and Mascon as the furthest
temple. Then he dies:

1. With the feature on, he comes back at Mascon's Guru with the same gold,
   experience and earned title. The rest of death is the game's: full HP and
   MP, the Mattock bit and the Wing Boots gone.
2. His mantra, the remaster's way: --password imports it into the first
   empty save file, with his title's gold and experience, and continuing that
   file from the file select starts him with them, as continuing a mantra
   always has. Continuing goes through Player_SetInitialExpAndGold as the
   mantra screen's END does, and this feature lets that call through.
3. With the remaster off in the mod selection, the same death resets them to
   his title's, as the original does. That death starts as the Guru speaks,
   from a saved game of the first after its dissolve: the reset comes after
   the Guru's words, and the first death plays the dissolve.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
import fax_e2e  # noqa: E402
from fax_e2e import Game, act, dissolve, fixture, page_through, parse_args, ram, tcp, until, wait  # noqa: E402

EXPERIENCE, GOLD, PLAYER_TITLE, NEXT_PLAYER_TITLE = 0x390, 0x392, 0x437, 0x4ED
HP, MP, QUESTS, DURATION_WING_BOOTS = 0x431, 0x39A, 0x42D, 0x429
TEMPLE_SPAWN_POINT, PLAYER_IS_DEAD, MESSAGE_ID, TEXTBOX_PAUSED = 0x439, 0x438, 0x213, 0x215
AREA, SCREEN = 0x24, 0x63
MATTOCK_BARRIER = 0x10
BUILDINGS, MASCON, MASCON_TEMPLE = 4, 3, 0x1E
NEGATIVE_THOUGHTS = 0x24                                            # the death message
PLAYER_TITLE_EXP_NEEDED, PLAYER_TITLE_GOLD = 0xF749, 0xF767         # fixed bank, a word per title
SOLDIER = 3
HERO = dict(experience=5000, gold=12345)
PACKAGE, FEATURE = "faxanadu.remastered", "remastered"          # the one switch: off is the original game


def number(addr, n):
    return int.from_bytes(ram(addr, n), "little")


def in_mascon_temple():
    return (ram(AREA), ram(SCREEN)) == (BUILDINGS, MASCON_TEMPLE)


def dying_writes():
    """The hero of the scenario, dead: a Soldier with the next title earned,
    HERO's experience and gold, the Mattock bit, Wing Boots running, low HP and
    MP, Mascon his temple."""
    return {PLAYER_TITLE: SOLDIER, NEXT_PLAYER_TITLE: SOLDIER + 1,
            EXPERIENCE: HERO["experience"] & 0xFF, EXPERIENCE + 1: HERO["experience"] >> 8,
            GOLD: HERO["gold"] & 0xFF, GOLD + 1: HERO["gold"] >> 8 & 0xFF, GOLD + 2: HERO["gold"] >> 16,
            QUESTS: MATTOCK_BARRIER, DURATION_WING_BOOTS: 0x40, HP: 0x10, MP: 0x08,
            TEMPLE_SPAWN_POINT: MASCON, PLAYER_IS_DEAD: 1}


@fax_e2e.recipe("death-soldier-words", parent="gate")
def _soldier_words(g):
    """The scenario's death, outside Eolis's gate: as the Guru speaks, the
    dissolve over."""
    g.poke(dying_writes())
    dissolve()


def title_start(image, title):
    """Player_SetInitialExpAndGold's experience and gold for a title."""
    if title == 0:
        return 0, 0
    word = lambda table: int.from_bytes(image[16 + 15 * 0x4000 + table - 0xC000 + 2 * (title - 1):][:2], "little")
    return word(PLAYER_TITLE_EXP_NEEDED), word(PLAYER_TITLE_GOLD)


def main():
    args = parse_args(__doc__)
    game = Game.from_args(args)
    stock_exp, stock_gold = title_start(game.image, SOLDIER)
    assert (stock_exp, stock_gold) == (3500, 1200), (stock_exp, stock_gold)
    assert (stock_exp, stock_gold) != (HERO["experience"], HERO["gold"])
    out = game.out

    def die_and_respawn():
        """Page through the death message until the hero is back in Mascon's temple."""
        until("the death message", lambda: ram(MESSAGE_ID) == NEGATIVE_THOUGHTS, 600)
        page_through("Mascon's temple", in_mascon_temple)
        wait(60)

    def hero():
        return dict(experience=number(EXPERIENCE, 2), gold=number(GOLD, 3), title=ram(PLAYER_TITLE),
                    next_title=ram(NEXT_PLAYER_TITLE))

    try:
        words = game.state("death-soldier-words")
        game.launch(at="gate")
        assert tcp("death_state")["enabled"], "the feature is on by default"
        gate = out / "gate.cycstate"
        tcp("save_state", path=str(gate))
        dying = out / "dying.cycstate"
        fixture(gate, dying, dying_writes())

        # 1. Death keeps gold, experience and the earned title; the rest is the game's.
        tcp("load_state", path=str(dying))
        die_and_respawn()
        game.shot("respawned")
        assert hero() == dict(HERO, title=SOLDIER, next_title=SOLDIER + 1), hero()
        assert (ram(HP), ram(MP)) == (0x50, 0x50), "full HP and MP"
        assert ram(QUESTS) & MATTOCK_BARRIER == 0, "the Mattock bit is cleared"
        assert ram(DURATION_WING_BOOTS) == 0xFF, "the timed items are cleared"
        assert ram(TEMPLE_SPAWN_POINT) == MASCON
        state = tcp("death_state")
        assert (state["kept"], state["passed"]) == (1, 0), state
        print(f"ok: died with {HERO['gold']} gold and {HERO['experience']} experience, "
              "came back at Mascon's Guru with both and the earned title")
        print("ok: full HP and MP, the Mattock bit and Wing Boots cleared, as before")
        mantra = tcp("mantra_now")["mantra"]
        game.close()

        # 2. His mantra, imported into a save file and continued, still starts
        # him with his title's gold and experience.
        game.launch("--password", mantra)
        until("the file select", lambda: tcp("files_state")["title"], 2000)
        files = tcp("files_state")
        imported = [i + 1 for i, s in enumerate(files["slots"]) if s["status"] == 0 and
                    (s["title"], s["gold"], s["experience"]) == (SOLDIER, stock_gold, stock_exp)]
        assert files["imports"] == 1 and len(imported) == 1, f"one file holds the mantra's hero: {files}"
        while tcp("files_state")["cursor"] != imported[0] - 1:
            act("down")
        game.shot("imported-file")
        tcp("pad", buttons="start", frames=10)
        until(f"file {imported[0]} in play", lambda: tcp("files_state")["file"] == imported[0], 300)
        until("Mascon's temple", in_mascon_temple)
        wait(60)
        game.shot("continued")
        assert hero() == dict(experience=stock_exp, gold=stock_gold, title=SOLDIER, next_title=SOLDIER), hero()
        state = tcp("death_state")
        assert state["enabled"] and (state["kept"], state["passed"]) == (0, 1), state
        print(f"ok: his mantra, imported into file {imported[0]} and continued, still starts a Soldier with "
              f"{stock_gold} gold and {stock_exp} experience")
        game.close()

        # 3. With the remaster off, the same death resets them, as the original does.
        game.set_features({(PACKAGE, FEATURE): False})
        game.launch()
        assert not tcp("death_state")["enabled"], "the remaster is off"
        tcp("load_state", path=str(words))                          # the same death, as the Guru speaks
        die_and_respawn()
        game.shot("respawned-off")
        assert hero() == dict(experience=stock_exp, gold=stock_gold, title=SOLDIER, next_title=SOLDIER), hero()
        print(f"ok: with the remaster off, the same death leaves {stock_gold} gold and {stock_exp} experience, "
              "the title's, as the original does")
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
