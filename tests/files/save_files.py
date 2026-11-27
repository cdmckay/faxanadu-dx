"""Play Save files end to end in a hidden window and check what they keep.

    python3 tests/files/save_files.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/files

The game runs uncapped (about 5x real time; --realtime to watch it at normal
speed) and the script waits on the game's own state, never on the clock, so
each run takes a free TCP port and any number can go in parallel.

Copies the build to OUT/game (the save files are written beside it), then:
  1. on a fresh install's title, NEW GAME and CONTINUE, CONTINUE grey with
     nothing to continue and the cursor kept off it; NEW GAME's pick shows
     three empty files, and file 2 starts a new game at once;
  2. edits save-state RAM to give the hero progress worth keeping (an external
     prerequisite, not coverage) and dies, to come back in a temple (from a
     saved game of that death as the Guru speaks: the dissolve is not what
     is checked): Eolis's Guru, with nowhere to send the hero, asks "Shall I
     remember your journey?"; Conflate's, after granting a title, offers the
     menu, and saving there makes Conflate the spawn point and fills HP and
     MP (edited down to 20 and 10 before he speaks) to 80 before his line
     (tests/files/guru_refill.py plays the fill frame by frame), and Guru
     travel still offers after it;
  3. quits and relaunches: the title opens on CONTINUE, whose pick greys the
     empty files and skips them; continuing file 2 restores the game, which
     is compared with the file and with what was saved;
  4. talks to a Guru without saving and checks the spawn point stays;
  5. NEW GAME over a used file: its pick starts on the empty one, and file 2
     asks "Erase file 2? A yes B no"; B keeps it, A erases it and starts a
     new game in it. With the remaster on nothing shows a mantra or the mantra
     screen: Continue skips it, and a Guru offers the choice instead.
--shots DIR also copies the screenshots worth showing there.
"""
from pathlib import Path
import shutil
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
import fax_e2e  # noqa: E402
from fax_e2e import (FILES_CONFIRM, FILES_CONTINUE, FILES_NEW, FILES_TOP, Game, act, choose_file,  # noqa: E402
                     dissolve, fixture, frame, guru_send, page_through, page_to, parser, ram, tcp, title_menu,
                     travel_menu, type_out, wait)

JOY_CHANGED, AREA, SCREEN, START_SCREEN_CHOICE = 0x19, 0x24, 0x63, 0x687
MESSAGE_ID, TEXTBOX_PAUSED, HP, MP = 0x213, 0x215, 0x431, 0x39A
EXPERIENCE, GOLD, SPECIAL_ITEMS, QUESTS, TITLE, PLAYER_IS_DEAD, TEMPLE_SPAWN_POINT, NEXT_TITLE = (
    0x390, 0x392, 0x42C, 0x42D, 0x437, 0x438, 0x439, 0x4ED)
LISTS, SELECTED, COUNTS = (0x39D, 0x3A1, 0x3A5, 0x3A9, 0x3AD), 0x3BD, 0x3C2
PASSWORD_SCRATCH = (0x4C6, 0x27)                                    # Password_TempY .. the encoded state
BUILDINGS = 4
TEMPLE_SCREEN = [0x02, 0x0B, 0x10, 0x1E, 0x23, 0x2B, 0x33, 0x3C]   # DEST_SCREEN_FOR_TEMPLE_SPAWN
EOLIS, MASCON, CONFLATE = 0, 3, 5
NEW_TITLE, GREETING, EOLIS_RING, NEGATIVE_THOUGHTS = 0x23, 0x22, 0x2D, 0x24
GURU_MENU, GURU_QUESTION, GURU_SAVED = 1, 2, 3                      # fax_files.c's GuruPhase
INTRO, OFFER = 0, 1                                                 # fax_travel.c's lines
MISSING, OK = 1, 0                                                  # FaxSaveStatus

# The progress the hero is given: Fighter (3) with the next title (Adept) earned
# but not granted, exact gold and experience, the Ring of Elf and the Pendant,
# two quests, and an inventory in every list (the Red Potion, $10, included).
PROGRESS = {
    "title": 3, "next_title": 4, "experience": 4900, "gold": 123456, "special_items": 0x82, "quests": 0x03,
    "lists": [[1, 2], [1], [], [0, 2], [4, 9, 16]], "selected": [0, 2, 1, 1, 7],
}


def until(what, test, frames=1500):
    fax_e2e.until(what, test, frames)


def files():
    return tcp("files_state")


def travel():
    return tcp("travel_state")


def in_temple(temple):
    return (ram(AREA), ram(SCREEN)) == (BUILDINGS, TEMPLE_SCREEN[temple])


def progress_writes(p):
    """RAM writes that give the hero progress `p`."""
    w = {TITLE: p["title"], NEXT_TITLE: p["next_title"], EXPERIENCE: p["experience"] & 255,
         EXPERIENCE + 1: p["experience"] >> 8, GOLD: p["gold"] & 255, GOLD + 1: p["gold"] >> 8 & 255,
         GOLD + 2: p["gold"] >> 16, SPECIAL_ITEMS: p["special_items"], QUESTS: p["quests"]}
    for c, (entries, sel) in enumerate(zip(p["lists"], p["selected"])):
        w[SELECTED + c] = sel
        w[COUNTS + c] = len(entries)
        for i, e in enumerate(entries):
            w[LISTS[c] + i] = e
    return w


def progress_in_ram():
    """The progress in RAM, in PROGRESS's shape, and the spawn point."""
    exp, gold = ram(EXPERIENCE, 2), ram(GOLD, 3)
    counts, sel = ram(COUNTS, 5), ram(SELECTED, 5)
    return {"title": ram(TITLE), "next_title": ram(NEXT_TITLE), "experience": exp[0] | exp[1] << 8,
            "gold": gold[0] | gold[1] << 8 | gold[2] << 16, "special_items": ram(SPECIAL_ITEMS),
            "quests": ram(QUESTS), "lists": [list(ram(LISTS[c], 8)[:counts[c]]) for c in range(5)],
            "selected": list(sel)}, ram(TEMPLE_SPAWN_POINT)


def read_file(path):
    """A save file's fields."""
    fields = {}
    for line in path.read_text().splitlines():
        if line.strip() and not line.startswith("#"):
            key, value = (part.strip() for part in line.split("=", 1))
            fields[key] = value
    return fields


def file_progress(f):
    """A save file's fields in PROGRESS's shape."""
    lists = [[int(x) for x in f[k].split()] for k in ("weapons", "armor", "shields", "spells", "items")]
    selected = [255 if f[k] == "none" else int(f[k]) for k in
                ("selected_weapon", "selected_armor", "selected_shield", "selected_spell", "selected_item")]
    return {"title": int(f["title"]), "next_title": int(f["next_title"]), "experience": int(f["experience"]),
            "gold": int(f["gold"]), "special_items": int(f["special_items"]), "quests": int(f["quests"]),
            "lists": lists, "selected": selected}


def write_file(path, p, spawn):
    """A save file with progress `p`, saved with the Guru of temple `spawn`."""
    lists = ("weapons", "armor", "shields", "spells", "items")
    sel = ("selected_weapon", "selected_armor", "selected_shield", "selected_spell", "selected_item")
    fields = {"version": 1, "title": p["title"], "next_title": p["next_title"], "experience": p["experience"],
              "gold": p["gold"], "spawn": spawn, "furthest": spawn, "special_items": p["special_items"],
              "quests": p["quests"], **{k: " ".join(map(str, v)) for k, v in zip(lists, p["lists"])},
              **{k: "none" if v == 255 else v for k, v in zip(sel, p["selected"])}}
    path.write_text("".join(f"{k} = {v}\n" for k, v in fields.items()))


def die_and_respawn(temple):
    """Page through the death message until the hero is back in `temple`."""
    until("the death message", lambda: ram(MESSAGE_ID) == NEGATIVE_THOUGHTS, 600)
    page_through(f"temple {temple}", lambda: in_temple(temple))
    wait(60)

def talk_until(phase, tries=12):
    """Walk left to the Guru, talk, and page through his lines until the
    Guru's choice is at `phase`."""
    act("left", 170, 10)
    act("up", 4, 10)
    page_to(lambda s: s["guru"] == phase, f"the Guru's choice {phase}", "files_state", tries)   # no A on it
    wait(20)

def close_box(button="a"):
    type_out("the line to type out")
    act(button, 6, 60)
    until("the box to close", lambda: ram(0x20C) != 0, 300)


@fax_e2e.recipe("files-gate-2")
def _gate_in_file_2(g):
    """A new game in file 2 (NEW GAME, then file 2), outside Eolis's gate once
    the opening has played."""
    g.timeout = 60
    title_menu()
    wait(10)
    choose_file(True, 2)
    until("the Eolis gate", lambda: frame() > 1900, 3000)


@fax_e2e.recipe("files-eolis-words", parent="files-gate-2")
def _eolis_words(g):
    """With PROGRESS but the next title not yet earned, dead with Eolis his
    temple: as the Guru speaks, the dissolve over."""
    g.poke({**progress_writes(dict(PROGRESS, next_title=3)), TEMPLE_SPAWN_POINT: EOLIS, PLAYER_IS_DEAD: 1})
    dissolve()


@fax_e2e.recipe("files-conflate-words", parent="files-gate-2")
def _conflate_words(g):
    """With PROGRESS, dead with Conflate his temple: as the Guru speaks, the
    dissolve over."""
    g.poke({**progress_writes(PROGRESS), TEMPLE_SPAWN_POINT: CONFLATE, PLAYER_IS_DEAD: 1})
    dissolve()


PARTS = ("new-game", "eolis-guru", "conflate", "overwrite")         # each its own game


def main():
    ap = parser(__doc__, PARTS)
    ap.add_argument("--shots", type=Path, help="copy the screenshots worth showing here")
    args = fax_e2e.chosen(ap.parse_args(), PARTS)
    game = Game.from_args(args)                                     # the build in OUT/game
    game.timeout = 60
    out = game.out
    worth = {}

    def install(folder):
        fax_e2e.install(args.build, folder)

    def launch(folder, *extra):
        """Start the copy of the game in `folder`, and wait for the title's menu."""
        game.exe = folder / "FaxanaduRecomp"
        game.launch(*extra)
        title_menu()
        wait(10)

    def shot(name, keep=None):
        path = game.shot(name)
        if keep:
            worth[keep] = path

    try:
        gamedir = out / "game"

        def at(fixture_name):
            """This copy of the game, started at a fixture."""
            game.close()
            game.exe = gamedir / "FaxanaduRecomp"
            game.launch(at=fixture_name)

        game.state("files-gate-2")                                  # a new game in file 2, at Eolis's gate
        words = {temple: game.state(f"files-{temple}-words")        # dead there, as the Guru speaks
                 for temple, part in (("eolis", "eolis-guru"), ("conflate", "conflate")) if args.wants(part)}
        expect = dict(PROGRESS, title=4)                            # what Conflate's Guru saves

        if args.wants("new-game"):
            # 1. A fresh install: NEW GAME, and CONTINUE grey with nothing to continue.
            launch(gamedir)
            state = files()
            assert [s["status"] for s in state["slots"]] == [MISSING] * 3, state
            assert (state["mode"], state["cursor"], state["can_continue"]) == (FILES_TOP, 0, False), state
            assert ram(START_SCREEN_CHOICE) == 0, "the game's arrow on NEW GAME"
            shot("title-new-game", "title-new-game")
            for button in ("select", "right", "down"):
                act(button)
                assert files()["cursor"] == 0, f"{button} leaves the cursor off the grey CONTINUE"
            act("b")
            assert files()["mode"] == FILES_TOP
            act("start")
            state = files()
            assert (state["mode"], state["cursor"]) == (FILES_NEW, 0), state
            assert state["rows"][0] == "} 1 Empty" and state["rows"][2].endswith("2 Empty"), state["rows"]
            assert state["rows"][7].endswith("New game in which file?") and not any(state["dim"]), state
            shot("new-game-pick", "new-game-pick")
            act("b")
            assert (files()["mode"], files()["cursor"]) == (FILES_TOP, 0), "B goes back"
            choose_file(True, 2)
            assert files()["continuing"] == 0 and ram(START_SCREEN_CHOICE) == 0, "an empty file: no question"
            until("the Eolis gate", lambda: frame() > 1900, 3000)
            assert files()["file"] == 2 and travel()["visited"] == 1
            assert not (gamedir / "faxanadu_file2.txt").exists(), "a new game writes nothing until it saves"
            print("ok: a fresh install's title: NEW GAME, CONTINUE grey and skipped; B goes back from the pick")
            print("ok: NEW GAME in empty file 2 starts at once")

        if args.wants("eolis-guru"):
            at("files-gate-2")
            # 2a. Eolis's Guru, with the Ring and nowhere to send the hero, asks.
            tcp("load_state", path=str(words["eolis"]))
            die_and_respawn(EOLIS)
            saved = read_file(gamedir / "faxanadu_file2.txt")
            assert saved["spawn"] == "0" and int(saved["gold"]) == PROGRESS["gold"], saved
            print("ok: dying saves the file, keeping its spawn point")
            scratch = ram(*PASSWORD_SCRATCH)
            talk_until(GURU_QUESTION)
            shot("eolis-question", "guru-question")
            saves = files()["saves"]
            act("a")
            until("the saved line", lambda: files()["guru"] == GURU_SAVED, 300)
            type_out("the saved line to type out")
            shot("eolis-saved")
            close_box()
            assert files()["saves"] == saves + 1 and ram(TEMPLE_SPAWN_POINT) == EOLIS
            assert ram(*PASSWORD_SCRATCH) == scratch, "the mantra was never encoded"
            print("ok: Eolis's Guru asks, saves, and shows no mantra")

        if args.wants("conflate"):
            at("files-gate-2")
            # 2b. Conflate's Guru grants a title, then offers the menu; saving there
            # makes Conflate the spawn point, and Guru travel still offers.
            tcp("load_state", path=str(words["conflate"]))
            die_and_respawn(CONFLATE)
            temple = out / "conflate-temple.cycstate"
            tcp("save_state", path=str(temple))
            game.poke({HP: 20, MP: 10})
            scratch = ram(*PASSWORD_SCRATCH)
            talk_until(GURU_MENU)
            assert ram(TITLE) == 4, "the Guru granted the title first"
            assert (ram(HP), ram(MP)) == (20, 10), "talking fills nothing"
            shot("conflate-menu", "guru-menu")
            act("a")
            until("the saved line", lambda: files()["guru"] == GURU_SAVED, 300)
            assert (ram(HP), ram(MP)) == (80, 80) and files()["refills"] == 1 and files()["fill"] == [60, 70]
            type_out("the saved line to type out")
            shot("conflate-saved", "guru-saved")
            type_out("the line to type out")
            act("a")
            until("Guru travel's intro", lambda: travel()["text"] == INTRO and travel()["phase"] in (1, 2), 600)
            type_out("the intro to type out")
            act("a")
            until("Guru travel's offer", lambda: travel()["text"] == OFFER, 600)
            type_out("the offer to type out")
            shot("travel-offer")
            close_box("b")
            saved = read_file(gamedir / "faxanadu_file2.txt")
            assert saved["spawn"] == str(CONFLATE) and saved["furthest"] == str(CONFLATE), saved
            assert saved["gurus"] == "Eolis Conflate", saved
            expect = dict(PROGRESS, title=4)
            assert file_progress(saved) == expect, (file_progress(saved), expect)
            assert ram(*PASSWORD_SCRATCH) == scratch, "the mantra was never encoded"
            print("ok: Conflate's Guru grants a title, offers the menu, saves, fills HP and MP, then offers travel")
            print("ok: file 2 holds exact gold, experience, title, items, quests, spawn point and Gurus")

            # 3. Quit, relaunch, continue file 2.
            game.close()
            launch(gamedir)
            state = files()
            assert [s["status"] for s in state["slots"]] == [MISSING, OK, MISSING], state
            assert (state["mode"], state["cursor"], state["can_continue"]) == (FILES_TOP, 1, True), state
            assert ram(START_SCREEN_CHOICE) == 1, "the game's arrow on CONTINUE"
            shot("title-continue")
            act("start")
            state = files()
            assert (state["mode"], state["cursor"]) == (FILES_CONTINUE, 1), state
            assert "Adept" in state["rows"][2] and "Conflate" in state["rows"][2], state["rows"]
            assert "Gold 123456" in state["rows"][3] and "Exp 4900" in state["rows"][3], state["rows"]
            assert state["rows"][7].endswith("Continue which file?"), state["rows"]
            assert state["dim"] == [1, 1, 0, 0, 1, 1, 0, 0], "the empty files are grey"
            for button in ("down", "up"):
                act(button)
                assert files()["cursor"] == 1, f"{button} skips the empty files"
            shot("continue-pick", "continue-pick")
            act("a")
            until("file 2 in play", lambda: files()["file"] == 2, 300)
            until("Conflate's temple", lambda: in_temple(CONFLATE), 1200)
            wait(60)
            shot("continued", "continued")
            progress, spawn = progress_in_ram()
            assert progress == expect and spawn == CONFLATE, (progress, spawn)
            assert travel()["visited"] == 0b100001 and files()["furthest"] == CONFLATE
            print("ok: continuing file 2 restores exactly what was saved, in Conflate's temple")

            # 4. Talking to a Guru without saving keeps the spawn point (#43): the
            # file says it was saved in Mascon, and dying returns there.
            continued = out / "continued.cycstate"
            tcp("save_state", path=str(continued))
            fixture(continued, out / "mascon-spawn.cycstate", {TEMPLE_SPAWN_POINT: MASCON})
            tcp("load_state", path=str(out / "mascon-spawn.cycstate"))
            before = (gamedir / "faxanadu_file2.txt").read_bytes()
            saves = files()["saves"]
            talk_until(GURU_MENU)
            guru_send()                                       # "Send me to another Guru.": Guru travel's menu
            state = travel_menu()
            assert state["menu"] == [EOLIS] and state["shown"] == ["EOLIS"] and state["back"], state
            shot("travel-menu")
            act("b")                                          # back to his choice
            until("the Guru's choice again", lambda: files()["guru"] == GURU_MENU and ram(MESSAGE_ID) == 0, 300)
            close_box("b")
            assert ram(TEMPLE_SPAWN_POINT) == MASCON, "talking to Conflate's Guru left the spawn point at Mascon"
            assert files()["saves"] == saves and (gamedir / "faxanadu_file2.txt").read_bytes() == before
            assert files()["furthest"] == CONFLATE
            tcp("save_state", path=str(out / "talked.cycstate"))
            fixture(out / "talked.cycstate", out / "talked-death.cycstate", {PLAYER_IS_DEAD: 1})
            tcp("load_state", path=str(out / "talked-death.cycstate"))
            die_and_respawn(MASCON)
            print("ok: the menu's second choice opens Guru travel's menu, whose B comes back to it; B there leaves; "
                  "neither moves the spawn point")
            print("ok: death returns to the Guru last saved with")

        if args.wants("overwrite"):
            # 5. NEW GAME over a used file: asked first; B keeps it, A erases it.
            game.close()
            write_file(gamedir / "faxanadu_file2.txt", PROGRESS, CONFLATE)
            launch(gamedir)
            act("select")
            assert (files()["mode"], files()["cursor"]) == (FILES_TOP, 0), files()
            act("a")
            state = files()
            assert (state["mode"], state["cursor"]) == (FILES_NEW, 0), "on the first empty file"
            assert "Fighter" in state["rows"][2] and not any(state["dim"]), state["rows"]
            act("down")
            act("a")
            state = files()
            assert (state["mode"], state["target"]) == (FILES_CONFIRM, 1), state
            assert state["rows"][7].endswith("Erase file 2? A yes B no") and state["rows"][2].startswith("}"), state
            assert state["file"] == 0 and state["title"], "nothing chosen yet"
            shot("erase-confirm", "erase-confirm")
            act("b")
            state = files()
            assert (state["mode"], state["cursor"]) == (FILES_NEW, 1) and state["title"], state
            assert (gamedir / "faxanadu_file2.txt").exists(), "B keeps the file"
            act("a")
            assert files()["mode"] == FILES_CONFIRM
            act("a")
            until("file 2 in play", lambda: files()["file"] == 2, 300)
            state = files()
            assert state["continuing"] == 0 and not state["title"] and ram(START_SCREEN_CHOICE) == 0, state
            assert not (gamedir / "faxanadu_file2.txt").exists(), "A erased it"
            assert state["slots"][1]["status"] == MISSING
            until("the title to go", lambda: not tcp("title_state")["shown"], 120)
            print("ok: NEW GAME over file 2 asks first; B keeps the file, A erases it and starts a new game in it")
        print("screenshots:", ", ".join(game.shots))
        if args.shots:
            args.shots.mkdir(parents=True, exist_ok=True)
            for name, path in worth.items():
                shutil.copy2(path, args.shots / f"{name}.png")
    except BaseException:
        try:
            shot("failure")
        except Exception:
            pass
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
