"""Play zone respawn end to end in a hidden window.

    python3 tests/respawn/zone_respawn.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/respawn
    python3 tests/respawn/zone_respawn.py ... --game-arg=--world-view=seamless

The game runs uncapped and the script waits on the game's own state, never on
the clock, on a reserved TCP port (tests/e2e/fax_e2e.py), so any number can
run in parallel.

Copies the build to OUT and starts a new game over TCP. Save-state RAM edits
set up what the scenarios need (external prerequisites, not coverage): the
hero's gold and experience, where a building's door leads (Area_SavedArea and
the rest, as tests/zones/zone_names.py does), the key or ring a door takes,
and, in the tower, the screen's enemies cleared before a walk. Every entrance
is then used the game's own way, through its door, and every death is the
game's (PlayerIsDead set):

1. Dying outside Eolis's gate, before any entrance, wakes at Eolis's temple,
   as before.
2. Through the gate into Eolis, then a screen further left (a scroll, or with
   Seamless on a room handoff): dying there comes back at the gate's side of
   Eolis, after the Guru's words, with the gold and experience kept. Save
   files (on by default, file 1 in play) writes the file at that respawn too,
   with the gold and experience, keeping its spawn point.
3. Through each tower's and castle's door (the Towers of Trunk, Fortress,
   Suffer and Mist, the Castle of Fraternal, the Fortress of Zenis) and each
   region's (Trunk, Mist, Branch, Dartmoor): dying inside comes back exactly
   where, and as, the door brought him in (screen, position, palette, music),
   with full HP and MP. Then he walks back out through the door he came in
   by, where there is one: no entrance traps him.
4. In the Tower of Trunk, dying a screen further in still comes back at its
   door (the default view: the walk needs the screen's enemies cleared).
5. A Guru travel journey (from Conflate's temple to Apolune's) forgets the
   checkpoint: walking out of Apolune's temple records the town, and dying
   there comes back at that door.
6. Sugata's flash (#50): on the Mist's screen $1F, where the Branch's door
   back arrives and Sugata lives, its flash takes his last 10 HP, and he
   comes back at that door with no band or greyscale left. Ointment, a timed
   item, still ends with a death.
7. With the remaster off in the mod selection, the same death in the tower
   wakes at the temple, as the original does.

Where it comes back is what is checked, and that is decided after the
Guru's words, so most deaths start as the Guru speaks: from a saved game of
the same death after its dissolve (a fixture, whose recipe plays the same
way in and dies there). The deaths in 2 (with its pictures), after the
journey in 5, and to Sugata's flash in 6 are played whole.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
import fax_e2e  # noqa: E402
from fax_e2e import (Game, act, chosen, dissolve, fixture, hold, page_through, page_to, parser, ram,  # noqa: E402
                     step_toward, tcp, type_out, until, wait)

AREA, SCREEN, PLAYER_X, PLAYER_Y, PLAYER_FLAGS = 0x24, 0x63, 0x9E, 0xA1, 0xA4
SCREEN_READY_STATE, SCROLL_DIRECTION, INTERRUPTS_ON = 0x0E, 0x54, 0x13
EXPERIENCE, GOLD, HP, MP = 0x390, 0x392, 0x431, 0x39A
SCREEN_PALETTE, AREAS_DEFAULT_MUSIC = 0x3D0, 0x3D1
AREA_SAVED_AREA, AREA_SAVED_SCREEN, SCREEN_SAVED_PALETTE, PLAYER_SAVED_POS = 0x3D5, 0x3D6, 0x3D7, 0x3D8
SELECTED_ITEM, SPECIAL_ITEMS, AREA_REGION = 0x3C1, 0x42C, 0x435
PLAYER_IS_DEAD, TEMPLE_SPAWN_POINT = 0x438, 0x439
TEXTBOX_CLOSED, MESSAGE_ID, TEXTBOX_PAUSED = 0x20C, 0x213, 0x215
SPRITE_ENTITIES = 0x2CC                                             # CurrentSprites_Entities, 8 slots
NEGATIVE_THOUGHTS = 0x24                                            # the death message, the Guru's
BUILDINGS, EOLIS_TEMPLE = 4, 0x02
KEY_A, KEY_K, KEY_Q, KEY_J = 0x04, 0x05, 0x06, 0x07                 # ITEMINVENTORY_KEY_*
RING_OF_DWORF, DEMONS_RING = 0x20, 0x10                             # SpecialItems bits
HERO = dict(experience=4660, gold=12345)
APOLUNE, CONFLATE = 1, 5                                            # temples
TEMPLE_SCREEN = {APOLUNE: 0x0B, CONFLATE: 0x2B}                     # DEST_SCREEN_FOR_TEMPLE_SPAWN
TOWNS = 3
MEDITATE_WITH_YOU = 0x22                                            # the Guru's greeting
OFFER, DEPART, TO_GURU = 1, 3, 4                                    # fax_travel.c's lines (TO_GURU + temple)
PPU_MASK, DURATION_OINTMENT, SUGATA = 0x0B, 0x427, 0x18             # greyscale is bit 0; $FF: no Ointment
PACKAGE, FEATURE = "faxanadu.remastered", "remastered"          # the one switch: off is the original game

# Each entrance: the zone, where its door is (area, screen, palette, block YX,
# region: what the temple's door is edited to lead to), what opens it, where
# the door brings the hero (area, screen, start YX), and the door back out
# (block YX, and the screen it leads to), if there is one. From the door
# tables in bank 3 (docs/GAME_INTERNALS.md, "Entrances and zone respawn").
ENTRANCES = [
    ("Tower of Trunk", (1, 0x0B, 0x06, 0xA9, 1), {SELECTED_ITEM: KEY_J}, (1, 0x0D, 0x91), (0x90, 0x0B)),
    ("Tower of Fortress", (1, 0x28, 0x06, 0x3D, 1), {SELECTED_ITEM: KEY_J}, (1, 0x29, 0x93), (0x92, 0x28)),
    ("Tower of Suffer", (2, 0x00, 0x0A, 0x9A, 2), {SELECTED_ITEM: KEY_Q}, (2, 0x3E, 0x91), (0x90, 0x00)),
    ("Tower of Mist", (2, 0x17, 0x0A, 0xA6, 2), {SELECTED_ITEM: KEY_K}, (2, 0x45, 0xA1), (0xA0, 0x17)),
    ("Castle of Fraternal", (6, 0x0C, 0x0C, 0x48, 4), {}, (6, 0x10, 0x91), (0x90, 0x0C)),
    ("Fortress of Zenis", (6, 0x0E, 0x0C, 0xAE, 4), {SPECIAL_ITEMS: DEMONS_RING}, (7, 0x08, 0xA2), (0xA1, 0x0E)),
    ("Trunk", (0, 0x08, 0x00, 0x9C, 0), {SELECTED_ITEM: KEY_J}, (1, 0x00, 0xA1), None),   # no door back to Eolis
    ("Mist", (1, 0x28, 0x06, 0x10, 1), {SELECTED_ITEM: KEY_Q}, (2, 0x11, 0x9E), (0x9F, 0x28)),
    ("Branch", (2, 0x1F, 0x0A, 0x9C, 2), {SELECTED_ITEM: KEY_A}, (5, 0x00, 0xA6), (0xA5, 0x1F)),
    ("Dartmoor", (5, 0x27, 0x08, 0xA8, 3), {SPECIAL_ITEMS: RING_OF_DWORF}, (6, 0x00, 0x9E), (0xAF, 0x27)),
]
# The parts tools/test.sh runs at once, each its own game: Eolis (1, 2), the
# entrances (3) a few at a time, with the Tower of Trunk's checks that use its
# state (4, 7), Guru travel (5) and Sugata's flash (6).
GROUPS = {"Tower of Trunk": ("trunk-tower",), "Tower of Fortress": ("fortress-suffer",),
          "Tower of Suffer": ("fortress-suffer",), "Tower of Mist": ("mist-fraternal",),
          "Castle of Fraternal": ("mist-fraternal",), "Fortress of Zenis": ("zenis-trunk",), "Trunk": ("zenis-trunk",),
          "Mist": ("mist-branch",), "Branch": ("mist-branch",), "Dartmoor": ("dartmoor",)}
PARTS = ("eolis", "trunk-tower", "fortress-suffer", "mist-fraternal", "zenis-trunk", "mist-branch", "dartmoor",
         "travel", "sugata")
HERO_KIT = {EXPERIENCE: HERO["experience"] & 0xFF, EXPERIENCE + 1: HERO["experience"] >> 8,
            GOLD: HERO["gold"] & 0xFF, GOLD + 1: HERO["gold"] >> 8, GOLD + 2: 0, HP: 0x50}


def number(addr, n):
    return int.from_bytes(ram(addr, n), "little")


def box_open():
    return ram(TEXTBOX_CLOSED) == 0


def settled():
    """A screen has loaded and is not scrolling, and no box is open."""
    return (ram(SCREEN_READY_STATE) == 0 and ram(INTERRUPTS_ON) and ram(SCROLL_DIRECTION) == 0xFF
            and not box_open() and not ram(PLAYER_IS_DEAD))


def at(area, screen):
    return (ram(AREA), ram(SCREEN)) == (area, screen) and settled()


def respawn():
    return tcp("respawn_state")


def page_until(what, test):
    """Page through any text with A until `test` holds (A held while it types)."""
    page_through(what, test)


def walk_to(x_min, x_max):
    """Walk until the hero's X is in [x_min, x_max], paging through any text."""
    for _ in range(600):
        if box_open():
            page_through("the box to close", lambda: not box_open())
            continue
        x = ram(PLAYER_X)
        if x_min <= x <= x_max:
            return
        step_toward(x_min, x_max, box_open)
    raise AssertionError(f"could not reach x {x_min}-{x_max}, at {ram(PLAYER_X)}")


def walk_out():
    """Walk right out of a building, to where its door leads."""
    for _ in range(200):
        if ram(AREA) != BUILDINGS:
            return
        hold("right", lambda: ram(AREA) != BUILDINGS)
    raise AssertionError("could not walk out")


def save(g, name):
    path = g.out / f"{name}.cycstate"
    g.tcp("save_state", path=str(path))
    return path


def clear_screen(g, name):
    """The screen's enemies gone and HP full, for a walk (a prerequisite, not coverage)."""
    raw = save(g, f"{name}-raw")
    clear = g.out / f"{name}-clear.cycstate"
    fixture(raw, clear, {**{SPRITE_ENTITIES + i: 0xFF for i in range(8)}, HP: 0x50})
    g.tcp("load_state", path=str(clear))


def through_door(block, what, test):
    """Stand at the door at `block` (YX), as Area_SetStateFromDoorDestination
    finds it at X + 7, and press Up until `test` holds (a knock or a landing
    can take the press). Its line, if it has one, is paged with A; until
    the door answers, no A, which would jump him off his block."""
    col = block & 0x0F
    for _ in range(6):
        walk_to(max(0, col * 16 - 7), col * 16 + 8)
        until("the hero to stand", lambda: not ram(PLAYER_FLAGS) & 0x05, 120)
        area_before = ram(AREA), ram(SCREEN)
        act("up", 4, 10)
        try:
            until(f"the door to {what}", lambda: test() or box_open() or (ram(AREA), ram(SCREEN)) != area_before, 120)
            return page_until(what, test)
        except AssertionError:
            pass
    raise AssertionError(f"could not go through the door to {what}")


def standing():
    """On the ground, neither jumping nor falling (Player_Flags $01 and $04,
    which a fall keeps through its landing frame), at the same Y twenty
    frames apart. Dartmoor's arrival is a row above the floor: he stands
    there up to 15 frames, then drops onto it."""
    y, _, _, flags = ram(PLAYER_Y, 4)                   # Player_PosY ($A1) to Player_Flags ($A4)
    if flags & 0x05:
        return False
    wait(20)
    y2, _, _, flags = ram(PLAYER_Y, 4)
    return not flags & 0x05 and y2 == y


def arrival():
    """Where and how the hero is, as a door or a respawn left him, once he
    stands."""
    until("the hero to stand", standing, 300)
    return dict(area=ram(AREA), screen=ram(SCREEN), x=ram(PLAYER_X), y=ram(PLAYER_Y),
                palette=ram(SCREEN_PALETTE), music=ram(AREAS_DEFAULT_MUSIC),
                facing_right=bool(ram(PLAYER_FLAGS) & 0x40))


def door_writes(area, screen, palette, pos, region, opens):
    """A temple's door edited to lead to the door at `pos` on `screen` of `area`, with what opens it."""
    return {AREA_SAVED_AREA: area, AREA_SAVED_SCREEN: screen, SCREEN_SAVED_PALETTE: palette,
            PLAYER_SAVED_POS: pos, AREA_REGION: region, HP: 0x50, **opens}


def go_in(g, name, area, screen, palette, pos, region, opens, inside):
    """From Eolis's temple (its door edited), walk out onto the entrance
    `name`'s door and through it, to `inside` (area, screen): where he
    stands there."""
    g.poke(door_writes(area, screen, palette, pos, region, opens))
    walk_out()
    until(f"outside {name}", lambda: at(area, screen), 600)
    clear_screen(g, f"outside-{name}")
    through_door(pos, name, lambda: at(*inside))
    return arrival()


def slug(name):
    return name.lower().replace(" ", "-")


@fax_e2e.recipe("respawn-gate-words", parent="gate")
def _gate_words(g):
    """The hero, with his gold and experience, dead outside Eolis's gate
    before any entrance: as the Guru speaks, the dissolve over."""
    g.poke({**HERO_KIT, PLAYER_IS_DEAD: 1})
    dissolve()


@fax_e2e.recipe("respawn-temple", parent="respawn-gate-words")
def _respawn_temple(g):
    """Back in Eolis's temple after that death, after the Guru's words."""
    page_until("Eolis's temple", lambda: at(BUILDINGS, EOLIS_TEMPLE))
    wait(30)


def _entrance_recipes(name, area, screen, palette, pos, region, opens, inside):
    @fax_e2e.recipe(f"respawn-in-{slug(name)}", parent="respawn-temple")
    def _inside(g):
        """Through the entrance's door from Eolis's temple (its door edited to
        lead there), standing where it brought him."""
        go_in(g, name, area, screen, palette, pos, region, opens, inside)

    @fax_e2e.recipe(f"respawn-died-{slug(name)}", parent=f"respawn-in-{slug(name)}")
    def _died(g):
        """Dead where the entrance's door brought him: as the Guru speaks, the dissolve over."""
        g.poke({PLAYER_IS_DEAD: 1})
        dissolve()


for _name, (_area, _screen, _palette, _pos, _region), _opens, (_in_area, _in_screen, _), _ in ENTRANCES:
    _entrance_recipes(_name, _area, _screen, _palette, _pos, _region, _opens, (_in_area, _in_screen))


def further_in_tower(g):
    """From where the Tower of Trunk's door brought him, a screen further in
    ($0E), at x 40-60: each screen's enemies cleared for the walk."""
    g.poke({SPRITE_ENTITIES + i: 0xFF for i in range(8)})
    for _ in range(400):
        if ram(SCREEN) == 0x0E and settled():
            break
        act("right", 4, 0)
    assert ram(SCREEN) == 0x0E, "a screen further into the tower"
    clear_screen(g, "tower-0e")                       # this screen's enemies came with it
    walk_to(40, 60)


@fax_e2e.recipe("respawn-died-tower-0e", parent="respawn-in-tower-of-trunk")
def _died_tower_0e(g):
    """A screen further into the Tower of Trunk, dead: as the Guru speaks,
    the dissolve over."""
    further_in_tower(g)
    g.poke({PLAYER_IS_DEAD: 1})
    dissolve()


@fax_e2e.recipe("respawn-conflate-words", parent="gate")
def _conflate_words(g):
    """The hero, with his gold and experience, dead outside Eolis's gate with
    Conflate's temple his spawn point: as the Guru speaks, the dissolve over."""
    g.poke({**HERO_KIT, TEMPLE_SPAWN_POINT: CONFLATE, PLAYER_IS_DEAD: 1})
    dissolve()


def into_mist_1f(g):
    """From Eolis's temple, its door edited to lead to the Branch's door back
    into the Mist: out, and through it onto the Mist's screen $1F, where
    Sugata lives. Where he stands there."""
    return go_in(g, "the Branch", 5, 0x00, 0x08, 0xA5, 3, {}, (2, 0x1F))


@fax_e2e.recipe("respawn-ointment-words", parent="respawn-temple")
def _ointment_words(g):
    """On the Mist's screen $1F with Ointment running, dead: as the Guru
    speaks, the dissolve over."""
    into_mist_1f(g)
    g.poke({DURATION_OINTMENT: 0x20, PLAYER_IS_DEAD: 1})
    dissolve()


def main():
    ap = parser(__doc__, PARTS)
    ap.add_argument("--only", action="append", default=[], help="run only these entrances (repeatable)")
    args = chosen(ap.parse_args(), PARTS)
    seamless = any("seamless" in arg for arg in args.game_arg)
    game = Game.from_args(args)
    out = game.out
    shot = game.shot

    def come_back(name, area, screen, pictures=False):
        """From the Guru's words after a death, page through them and come
        back at (area, screen). Returns how the hero is when the screen settles."""
        if pictures:
            type_out("the Guru's words to type", 600)
            shot(f"{name}-guru")
        page_until(f"area {area} screen {screen:#x}", lambda: at(area, screen))
        came = arrival()
        if pictures:
            wait(20)
            shot(f"{name}-respawned")
        return came

    def die(state, name, area, screen, pictures=False):
        """Die from `state`, the dissolve and all, and come back at (area, screen)."""
        dying = out / f"dying-{name}.cycstate"
        fixture(state, dying, {PLAYER_IS_DEAD: 1})
        tcp("load_state", path=str(dying))
        if pictures:
            wait(30)
            shot(f"{name}-dying")
        until("the Guru's words", lambda: ram(MESSAGE_ID) == NEGATIVE_THOUGHTS, 600)
        return come_back(name, area, screen, pictures)

    def after_words(words, name, area, screen, pictures=False):
        """A death from its saved game as the Guru speaks (`words`, a
        fixture), and back at (area, screen)."""
        tcp("load_state", path=str(words))
        return come_back(name, area, screen, pictures)

    def hero():
        return dict(experience=number(EXPERIENCE, 2), gold=number(GOLD, 3))

    try:
        raw = game.state("gate")
        gate = out / "gate.cycstate"
        fixture(raw, gate, HERO_KIT)
        temple = game.state("respawn-temple")
        entrances = [e for e in ENTRANCES
                     if not (args.only and e[0] not in args.only) and any(args.wants(part) for part in GROUPS[e[0]])]
        # The deaths' saved games, as the Guru speaks, made before the game launches.
        died = {name: game.state(f"respawn-died-{slug(name)}") for name, *_ in entrances}
        gate_words = game.state("respawn-gate-words") if args.wants("eolis") else None
        tower_0e = game.state("respawn-died-tower-0e") if "Tower of Trunk" in died and not seamless else None
        conflate_words = game.state("respawn-conflate-words") if args.wants("travel") else None
        ointment_words = game.state("respawn-ointment-words") if args.wants("sugata") else None

        def game_up():
            """The game, launched if it is not running."""
            if not game.running():
                game.launch()
                assert respawn()["enabled"], "zone respawn is on by default"

        if args.wants("eolis"):
            game_up()
            tcp("load_state", path=str(gate))
            until("the Eolis gate", lambda: ram(HP) and at(0, 0), 3000)
            # 1. No entrance yet: the temple, as before.
            assert respawn()["zone"] is None
            after_words(gate_words, "gate", BUILDINGS, EOLIS_TEMPLE)
            state = respawn()
            assert (state["respawns"], state["fallbacks"]) == (0, 1), state
            wait(30)
            temple = save(game, "temple")                      # as respawn-temple is
            print("ok: with no entrance yet, death wakes at Eolis's temple, as before")

            # 2. Eolis, through its gate, and a screen further left.
            tcp("load_state", path=str(gate))
            walk_to(186, 196)
            act("up", 4, 10)
            until("Eolis", lambda: at(0, 1), 600)
            entered = arrival()
            state = respawn()
            assert (state["zone"], state["area"], state["screen"], state["start"]) == ("Eolis", 0, 1, 0x99), state
            assert (entered["x"], entered["y"]) == (0x90, 0x90), entered
            handoffs = tcp("fax_world")["handoffs"] if seamless else 0
            hold("left", lambda: ram(SCREEN) == 2 and settled() and ram(PLAYER_X) < 200, 1200)
            assert ram(SCREEN) == 2, "a screen further into Eolis"
            if seamless:
                assert tcp("fax_world")["handoffs"] > handoffs, "Seamless handed the hero to the next room"
            assert respawn()["screen"] == 1, "the walk set no checkpoint"
            wait(10)
            further = save(game, "eolis-2")
            files = tcp("files_state")
            assert files["file"] == 1, "Save files has file 1 in play (on by default)"
            came = die(further, "eolis", 0, 1, pictures=True)
            assert came == entered, (came, entered)
            saved = tcp("files_state")
            assert saved["saves"] == files["saves"] + 1 and saved["spawn"] == 0, (files, saved)
            record = dict(line.split(" = ", 1) for line in (out / "game" / "faxanadu_file1.txt").read_text().splitlines()
                          if " = " in line)
            assert (record["gold"], record["experience"], record["spawn"]) == \
                (str(HERO["gold"]), str(HERO["experience"]), "0"), record
            assert (ram(HP), ram(MP)) == (0x50, 0x50), "full HP and MP"
            assert hero() == HERO, hero()
            assert ram(TEMPLE_SPAWN_POINT) == 0, "the mantra's temple is unchanged"
            state = respawn()
            assert (state["respawns"], state["fallbacks"]) == (1, 1), state
            print("ok: dying a screen into Eolis comes back at its gate, after the Guru, with gold and experience; "
                  "Save files wrote the file at the respawn, keeping its spawn point")

        # 3. Every tower, castle and region, through its own door. Each death
        #    starts as the Guru speaks, from its fixture, which came in the
        #    same way.
        for name, (area, screen, palette, pos, region), opens, (in_area, in_screen, start), back in entrances:
            game_up()
            tcp("load_state", path=str(temple))
            entered = go_in(game, name, area, screen, palette, pos, region, opens, (in_area, in_screen))
            # Where the door puts him, standing; Dartmoor's start is a row above
            # the floor, and he drops onto it.
            assert entered["x"] == (start & 0x0F) << 4 and entered["y"] in (start & 0xF0, (start & 0xF0) + 16), \
                (name, entered)
            state = respawn()
            assert (state["zone"], state["area"], state["screen"], state["start"]) == \
                (name, in_area, in_screen, start), (name, state)
            before = respawn()["respawns"]
            came = after_words(died[name], f"died-{name}", in_area, in_screen)
            assert came == entered, (name, came, entered)
            assert (ram(HP), ram(MP)) == (0x50, 0x50), name
            assert hero() == HERO, (name, hero())
            assert respawn()["respawns"] == before + 1
            where = f"{in_screen:#04x}, x={came['x']}, y={came['y']}, palette {came['palette']:#04x}, " \
                    f"music {came['music']:#04x}"
            if back:
                block, outside = back
                clear_screen(game, f"respawned-{name}")
                through_door(block, f"out of {name}", lambda: at(area, outside))
                print(f"ok: {name}: back where its door brought him ({where}), and out through that door")
            else:
                print(f"ok: {name}: back where its door brought him ({where})")

        # 4. A screen further into the Tower of Trunk (further_in_tower(), in
        #    its fixture). With Seamless on, the next room's enemies cannot be
        #    cleared, so the walk is the default view's; step 2 crossed a room
        #    with Seamless on.
        if tower_0e:
            came = after_words(tower_0e, "tower", 1, 0x0D, pictures=True)
            assert (came["x"], came["y"]) == (0x10, 0x90), came
            assert hero() == HERO, hero()
            print("ok: dying a screen into the Tower of Trunk comes back at its door")

        if args.wants("travel"):
            # 5. A Guru travel journey forgets the checkpoint: walking out of the
            #    temple it lands in records the next, and a death comes back there.
            game.close()
            (out / "game" / "faxanadu_gurus.txt").write_text("Eolis\nApolune\nConflate\n")
            game.launch()
            after_words(conflate_words, "to-conflate", BUILDINGS, TEMPLE_SCREEN[CONFLATE])
            walk_out()
            until("Conflate", lambda: at(TOWNS, 0x08), 600)
            state = respawn()
            assert (state["zone"], state["screen"], state["start"]) == ("Conflate", 0x08, 0x99), state
            through_door(0x99, "Conflate's temple", lambda: at(BUILDINGS, TEMPLE_SCREEN[CONFLATE]))
            assert respawn()["zone"] == "Conflate", "a building changes nothing"
            act("left", 170, 10)
            act("up", 4, 10)
            until("the Guru's greeting", lambda: ram(MESSAGE_ID) == MEDITATE_WITH_YOU)
            for want, button in ((OFFER, "a"), (TO_GURU + 0, "b"), (TO_GURU + APOLUNE, "a"), (DEPART, "a")):
                page_to(lambda s: s["text"] == want, f"travel line {want}")
                act(button, 6, 30)
            until("Apolune's temple", lambda: at(BUILDINGS, TEMPLE_SCREEN[APOLUNE]), 2400)
            assert respawn()["zone"] is None, "the journey forgot Conflate"
            walk_out()
            until("Apolune", lambda: at(TOWNS, 0x01), 600)
            entered = arrival()
            state = respawn()
            assert (state["zone"], state["screen"]) == ("Apolune", 0x01), state
            came = die(save(game, "apolune"), "apolune", TOWNS, 0x01)
            assert came == entered, (came, entered)
            assert ram(TEMPLE_SPAWN_POINT) == CONFLATE, "the furthest temple is unchanged"
            print("ok: a Guru travel journey forgets the checkpoint; walking out of Apolune's temple records the "
                  "next, and dying comes back there")

        if args.wants("sugata"):
            game_up()
            # 6. Sugata's flash and Ointment. Into the Mist by the Branch's door back,
            #    onto screen $1F, where Sugata lives: with 10 HP left, its flash
            #    kills him, and he comes back at that door with no band or greyscale
            #    left. Ointment is a timed item, and a death still ends it.
            tcp("load_state", path=str(temple))
            entered = into_mist_1f(game)
            state = respawn()
            assert (state["zone"], state["screen"], state["start"]) == ("Mist", 0x1F, 0x9B), state
            sugata = [i for i in range(8) if ram(SPRITE_ENTITIES + i) == SUGATA]
            assert sugata, "Sugata is on the Mist's screen $1F"
            raw = save(game, "mist-1f")
            weak = out / "mist-1f-weak.cycstate"
            fixture(raw, weak, {HP: 10, **{SPRITE_ENTITIES + i: 0xFF for i in range(8) if i not in sugata}})
            tcp("load_state", path=str(weak))
            before = tcp("sugata_state")
            until("Sugata's flash to kill him", lambda: ram(MESSAGE_ID) == NEGATIVE_THOUGHTS, 6000)
            after = tcp("sugata_state")
            assert after["in_reach"] > before["in_reach"], (before, after)
            page_until("the Mist's door", lambda: at(2, 0x1F))
            came = arrival()
            assert came == entered, (came, entered)
            assert tcp("sugata_state")["band"] != "flash" and not ram(PPU_MASK) & 1, "no flash left over"
            assert (ram(HP), ram(MP)) == (0x50, 0x50)
            after_words(ointment_words, "ointment", 2, 0x1F)       # the same place, with Ointment running
            assert ram(DURATION_OINTMENT) == 0xFF, "a death ends the Ointment, as in the original"
            print("ok: Sugata's flash takes his last HP on the Mist's screen $1F: back at the Branch's door, "
                  "no band or greyscale left; a death still ends the Ointment")

        # 7. The remaster off: the same death wakes at the temple, as the original.
        if "Tower of Trunk" in died:
            game.close()
            game.set_features({(PACKAGE, FEATURE): False})
            game.launch()
            assert not respawn()["enabled"], "the remaster is off"
            after_words(died["Tower of Trunk"], "off", BUILDINGS, EOLIS_TEMPLE)
            print("ok: with the remaster off, dying in the Tower of Trunk wakes at Eolis's temple, as the original does")
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
