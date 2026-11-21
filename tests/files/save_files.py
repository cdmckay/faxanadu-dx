"""Play Save files end to end in a hidden window and check what they keep.

    python3 tests/files/save_files.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/files

The game runs uncapped (about 5x real time; --realtime to watch it at normal
speed) and the script waits on the game's own state, never on the clock, so
each run takes a free TCP port and any number can go in parallel.

Copies the build to OUT/game (the save files are written beside it), then:
  1. starts a new game in file 2 from the file select;
  2. edits save-state RAM to give the hero progress worth keeping (an external
     prerequisite, not coverage) and dies, to come back in a temple (from a
     saved game of that death as the Guru speaks: the dissolve is not what
     is checked): Eolis's Guru, with nowhere to send the hero, asks "Shall I
     remember your journey?"; Conflate's, after granting a title, offers the
     menu, and saving there makes Conflate the spawn point;
  3. quits, relaunches and continues file 2, and compares the game with the
     file and with what was saved;
  4. talks to a Guru without saving and checks the spawn point stays;
  5. imports a mantra from faxanadu.srm (the first run) and from --password,
     continues the imported file, and erases a file from the file select.
--shots DIR also copies the screenshots worth showing there.
"""
from pathlib import Path
import shutil
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
import fax_e2e  # noqa: E402
from fax_e2e import Game, act, dissolve, fixture, frame, page_through, page_to, parser, ram, tcp, type_out, wait  # noqa: E402

JOY_CHANGED, AREA, SCREEN = 0x19, 0x24, 0x63
MESSAGE_ID, TEXTBOX_PAUSED = 0x213, 0x215
EXPERIENCE, GOLD, SPECIAL_ITEMS, QUESTS, TITLE, PLAYER_IS_DEAD, TEMPLE_SPAWN_POINT, NEXT_TITLE = (
    0x390, 0x392, 0x42C, 0x42D, 0x437, 0x438, 0x439, 0x4ED)
LISTS, SELECTED, COUNTS = (0x39D, 0x3A1, 0x3A5, 0x3A9, 0x3AD), 0x3BD, 0x3C2
PASSWORD_SCRATCH = (0x4C6, 0x27)                                    # Password_TempY .. the encoded state
BUILDINGS = 4
TEMPLE_SCREEN = [0x02, 0x0B, 0x10, 0x1E, 0x23, 0x2B, 0x33, 0x3C]   # DEST_SCREEN_FOR_TEMPLE_SPAWN
EOLIS, MASCON, CONFLATE = 0, 3, 5
NEW_TITLE, GREETING, EOLIS_RING, NEGATIVE_THOUGHTS = 0x23, 0x22, 0x2D, 0x24
GURU_MENU, GURU_QUESTION, GURU_SAVED = 1, 2, 3                      # fax_files.c's GuruPhase
INTRO, OFFER, FAREWELL, TO_GURU = 0, 1, 2, 4                        # fax_travel.c's lines
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


def choose(file):
    """Move the file select's cursor to `file` (1-3) and take it."""
    while files()["cursor"] != file - 1:
        act("down")
    tcp("pad", buttons="start", frames=10)
    until(f"file {file} in play", lambda: files()["file"] == file, 300)

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

def answer_travel(button, expect):
    type_out("the question to type out")
    act(button)
    until(f"travel line {expect}", lambda: travel()["text"] == expect, 600)


@fax_e2e.recipe("files-gate-2")
def _gate_in_file_2(g):
    """A new game in file 2, from the file select, outside Eolis's gate once
    the opening has played."""
    g.timeout = 60
    until("the file select", lambda: files()["shown"], 2000)
    wait(10)
    choose(2)
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


@fax_e2e.recipe("files-continued", parent="files-conflate-words")
def _continued(g):
    """Back in Conflate's temple after that death; saved at its Guru's
    menu; the window quit, and file 2 continued in Conflate's temple."""
    g.timeout = 60
    die_and_respawn(CONFLATE)
    talk_until(GURU_MENU)
    act("a")
    until("the saved line", lambda: files()["guru"] == GURU_SAVED, 300)
    type_out("the saved line to type out")
    act("a")
    until("Guru travel's intro", lambda: travel()["text"] == INTRO and travel()["phase"] in (1, 2), 600)
    type_out("the intro to type out")
    act("a")
    until("Guru travel's offer", lambda: travel()["text"] == OFFER, 600)
    close_box("b")
    g.close()
    g.launch()
    until("the file select", lambda: files()["shown"], 2000)
    wait(10)
    choose(2)
    until("Conflate's temple", lambda: in_temple(CONFLATE), 1200)
    wait(60)


PARTS = ("new-game", "eolis-guru", "conflate", "imports")           # each its own game


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
        """Start the copy of the game in `folder`, and wait for the file select."""
        game.exe = folder / "FaxanaduRecomp"
        game.launch(*extra)
        until("the file select", lambda: files()["shown"], 2000)
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
        continued = None
        expect = dict(PROGRESS, title=4)                            # what Conflate's Guru saves

        if args.wants("new-game"):
            # 1. A new game in file 2.
            launch(gamedir)
            state = files()
            assert [s["status"] for s in state["slots"]] == [MISSING] * 3, state
            assert state["rows"][0].endswith("1 New game") and state["rows"][7].endswith("Erase a file"), state
            shot("select-empty", "select-empty")
            choose(2)
            until("the Eolis gate", lambda: frame() > 1900, 3000)
            assert files()["file"] == 2 and travel()["visited"] == 1
            assert not (gamedir / "faxanadu_file2.txt").exists(), "a new game writes nothing until it saves"
            print("ok: a new game in file 2")

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
            scratch = ram(*PASSWORD_SCRATCH)
            talk_until(GURU_MENU)
            assert ram(TITLE) == 4, "the Guru granted the title first"
            shot("conflate-menu", "guru-menu")
            act("a")
            until("the saved line", lambda: files()["guru"] == GURU_SAVED, 300)
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
            print("ok: Conflate's Guru grants a title, offers the menu, saves, then offers travel")
            print("ok: file 2 holds exact gold, experience, title, items, quests, spawn point and Gurus")

            # 3. Quit, relaunch, continue file 2.
            game.close()
            launch(gamedir)
            state = files()
            assert [s["status"] for s in state["slots"]] == [MISSING, OK, MISSING], state
            assert "Adept" in state["rows"][2] and "Conflate" in state["rows"][2], state["rows"]
            assert "Gold 123456" in state["rows"][3] and "Exp 4900" in state["rows"][3], state["rows"]
            shot("select-saved", "select-saved")
            choose(2)
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
            act("down")
            act("a")
            until("Guru travel's intro", lambda: travel()["phase"] == 6, 600)
            type_out("the intro to type out")
            act("a")
            until("the towns", lambda: travel()["text"] == TO_GURU + EOLIS, 600)
            type_out("the town to type out")
            shot("travel-towns")
            answer_travel("b", FAREWELL)
            close_box()
            talk_until(GURU_MENU)
            close_box("b")
            assert ram(TEMPLE_SPAWN_POINT) == MASCON, "talking to Conflate's Guru left the spawn point at Mascon"
            assert files()["saves"] == saves and (gamedir / "faxanadu_file2.txt").read_bytes() == before
            assert files()["furthest"] == CONFLATE
            tcp("save_state", path=str(out / "talked.cycstate"))
            fixture(out / "talked.cycstate", out / "talked-death.cycstate", {PLAYER_IS_DEAD: 1})
            tcp("load_state", path=str(out / "talked-death.cycstate"))
            die_and_respawn(MASCON)
            print("ok: the menu's second choice asks about the towns; B leaves; neither moves the spawn point")
            print("ok: death returns to the Guru last saved with")

        if args.wants("imports"):
            if continued is None:                                   # as the conflate part continues file 2
                continued = game.state("files-continued")
                at("files-continued")
            # The mantras the original would have shown, for the import.
            tcp("load_state", path=str(continued))
            mantra = tcp("mantra_now")["mantra"]
            fixture(continued, out / "other.cycstate", progress_writes(dict(
                PROGRESS, title=1, next_title=1, lists=[[0], [], [], [], [16]], selected=[0, 255, 255, 255, 255])))
            tcp("load_state", path=str(out / "other.cycstate"))
            other = tcp("mantra_now")["mantra"]
            assert mantra != other
            game.close()

            # 5. Importing mantras.
            imports = out / "imports"
            install(imports)
            (imports / "faxanadu.srm").write_text(mantra + "\n")
            (imports / "faxanadu_gurus.txt").write_text("Eolis\nApolune\nConflate\n")
            launch(imports)
            state = files()
            assert state["imports"] == 1 and [s["status"] for s in state["slots"]] == [OK, MISSING, MISSING], state
            f1 = read_file(imports / "faxanadu_file1.txt")
            assert f1["gurus"] == "Eolis Apolune Conflate" and f1["spawn"] == str(CONFLATE), f1
            # A mantra holds the title but not gold or experience: those are the
            # title's, as continuing from it gave. Items above $0F survive it.
            assert file_progress(f1) == dict(expect, next_title=4, experience=4800, gold=1600), file_progress(f1)
            assert mantra in (imports / "faxanadu_imported.txt").read_text()
            print("ok: the first run imports faxanadu.srm's mantra and faxanadu_gurus.txt into file 1")
            game.close()
            launch(imports, "--password", other)
            state = files()
            assert state["imports"] == 1 and [s["status"] for s in state["slots"]] == [OK, OK, MISSING], state
            assert read_file(imports / "faxanadu_file2.txt")["items"] == "16"
            game.close()
            launch(imports, "--password", other)
            assert files()["imports"] == 0, "a mantra is imported once"
            print("ok: --password imports into the first empty file, once")
            choose(1)
            until("Conflate's temple", lambda: in_temple(CONFLATE), 1200)
            wait(60)
            progress, spawn = progress_in_ram()
            assert progress == file_progress(f1) and spawn == CONFLATE, progress
            assert travel()["visited"] == 0b100011
            print("ok: continuing the imported file gives the mantra's progress and the visited Gurus")
            game.close()

            # 6. Erasing a file.
            launch(imports)
            while files()["cursor"] != 3:
                act("down")
            act("a")
            assert files()["mode"] == 1 and files()["cursor"] == 0
            act("down")
            act("a")
            assert files()["mode"] == 2 and files()["target"] == 1
            assert files()["rows"][7].endswith("Erase file 2? A yes B no"), files()["rows"]
            shot("erase-confirm")
            act("a")
            state = files()
            assert not (imports / "faxanadu_file2.txt").exists() and state["rows"][2].endswith("2 New game"), state
            assert (imports / "faxanadu_file1.txt").exists()
            print("ok: erasing file 2 from the file select")
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
