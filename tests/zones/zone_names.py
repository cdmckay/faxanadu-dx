"""Play zone names end to end in a hidden window and screenshot the banners.

    python3 tests/zones/zone_names.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/zones

The game runs uncapped (about 5x real time; --realtime to watch it at normal
speed) and the script waits on the game's own state, never on the clock, so
each run takes a free TCP port, and any number can go in parallel.

Copies the build to OUT (so the faxanadu_gurus.txt sidecar stays there) and
starts a new game over TCP. It walks through Eolis's gate, into a shop and out,
and through the door out of Eolis into the Trunk, with the enemies on Eolis's
screen 8 cleared before the walk to that door (save-state RAM, an external
prerequisite). Then it relaunches with the Gurus of Eolis, Apolune and
Conflate visited (as tests/travel/guru_travel.py does), dies outside Eolis's
gate with Conflate's temple his spawn point (before any entrance, so zone
respawn wakes him at that temple, as the original does), travels to
Apolune's Guru, and walks out of each temple. Last, it edits where a temple's
door leads (save-state RAM too) to walk out into the Tower of Fortress and
the Castle of Fraternal. Then out into Forepaw's screen 2 the same way, where
the screen the game draws leaves TextBox_Closed at 0 with no box up
(docs/GAME_INTERNALS.md, "The text box"): Forepaw's name still fades and
goes in its 160 frames, a frame at a time. Again from its first frames, the
Select menu holds it (hidden behind the menu), and once B closes the menu
it fades out the rest of its time. A screenshot of each banner is saved in
OUT.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
from fax_e2e import (Game, act, batch, fixture, hold, hurry, page_to, parse_args, ram, step_toward, tcp,  # noqa: E402
                     typed, until, wait)

AREA, SCREEN, PLAYER_X = 0x24, 0x63, 0x9E
TEXTBOX_WIDTH, TEXTBOX_CLOSED, MESSAGE_ID, TEXTBOX_PAUSED = 0x20A, 0x20C, 0x213, 0x215
AREA_SAVED_AREA, AREA_SAVED_SCREEN, SCREEN_SAVED_PALETTE, PLAYER_SAVED_POS = 0x3D5, 0x3D6, 0x3D7, 0x3D8
HP, AREA_REGION, PLAYER_IS_DEAD, TEMPLE_SPAWN_POINT = 0x431, 0x435, 0x438, 0x439
SELECTED_ITEM, KEY_J = 0x3C1, 0x07                                 # Game_OpenDoorWithJKey's check
SPRITE_ENTITIES = 0x2CC                                             # CurrentSprites_Entities, 8 slots
BUILDINGS, TOWNS = 4, 3
TEMPLE_SCREEN = {1: 0x0B, 5: 0x2B}                                  # DEST_SCREEN_FOR_TEMPLE_SPAWN: Apolune, Conflate
APOLUNE, CONFLATE = 1, 5
MEDITATE_WITH_YOU, NEGATIVE_THOUGHTS = 0x22, 0x24                  # the Guru's greeting; the death message
OFFER, DEPART, TO_GURU = 1, 3, 4                                   # fax_travel.c's lines (TO_GURU + temple)
PALETTE_TOWER, PALETTE_FRATERNAL, PALETTE_TOWN = 0x07, 0x0D, 0x1B
FOREPAW, REGION_TRUNK = 2, 1                                       # its first screen in the towns' area
MENU_WIDTH = 10                                                    # PlayerMenu_Show's box: 2, 6, 10 x 14
SHOWN = 8 + 120 + 32                                               # fax_banner.c: fade in, hold, fade out
ZONE_NAMES = "faxanadu.zone-names"                                 # its save-state record


def zone():
    return tcp("zone_state")


def box_open():
    return ram(TEXTBOX_CLOSED) == 0


PARTS = ("eolis", "trunk", "conflate-apolune", "tower-castle", "forepaw")   # each its own game


def main():
    args = parse_args(__doc__, parts=PARTS)
    game = Game.from_args(args)
    out = game.out

    def banner(name, count):
        """Wait for the banner `name` to be the `count`th shown, fully in, and screenshot it."""
        until(f"the {name} banner", lambda: zone()["banners"] >= count, 600)
        state = zone()
        assert state["banners"] == count and state["last"] == name, state
        until(f"the {name} banner to fade in", lambda: zone()["alpha"] == 255, 60)
        game.shot(name.lower().replace(" ", "-"))

    def zone_at():
        """zone_state and the frame it was read at, from one frame boundary."""
        state, ping = batch(("zone_state", {}), ("ping", {}))
        return state, ping["frame"]

    def fades_on_time(name):
        """From now, the banner `name` ages a frame each frame and goes at SHOWN,
        read at one frame boundary after another until it has."""
        start, at = zone_at()
        assert start["showing"] == name and not start["box"], start
        while True:
            state, frame = zone_at()
            age = min(start["age"] + frame - at, SHOWN)
            assert state["age"] == age and (state["showing"] == name) == (age < SHOWN), (start, at, state, frame)
            if age == SHOWN:
                return frame - at

    def settled_in(area, screen):
        until(f"area {area} screen {screen:#x}", lambda: zone()["area"] == area and zone()["screen"] == screen)
        wait(30)

    def walk_to(x_min, x_max):
        """Walk until the hero's X is in [x_min, x_max], paging through any text."""
        for _ in range(600):
            if box_open():
                if typed():
                    act("a", 6, 14)
                else:
                    hurry(4)
                continue
            x = ram(PLAYER_X)
            if x_min <= x <= x_max:
                return
            step_toward(x_min, x_max, box_open)
        raise AssertionError(f"could not reach x {x_min}-{x_max}")

    def walk_out():
        """Walk right out of a building, back to where its door leads, and stop."""
        for _ in range(200):
            if ram(AREA) != BUILDINGS:
                return
            hold("right", lambda: ram(AREA) != BUILDINGS)
        raise AssertionError("could not walk out")

    def travel(temple):
        """Talk to the Guru and travel to `temple`, which must be the second one offered."""
        act("left", 170, 10)
        act("up", 4, 10)
        until("the Guru's greeting", lambda: ram(MESSAGE_ID) == MEDITATE_WITH_YOU)
        for want, button in ((OFFER, "a"), (TO_GURU + 0, "b"), (TO_GURU + temple, "a"), (DEPART, "a")):
            page_to(lambda s: s["text"] == want, f"travel line {want}")
            act(button, 6, 30)
        until(f"temple {temple}", lambda: (ram(AREA), ram(SCREEN)) == (BUILDINGS, TEMPLE_SCREEN[temple]), 2400)

    def warp(source, name, area, screen, palette, pos, region):
        """From inside a building, walk out to another place: edit where its door leads."""
        target = out / f"{name}.cycstate"
        fixture(source, target, {AREA_SAVED_AREA: area, AREA_SAVED_SCREEN: screen, SCREEN_SAVED_PALETTE: palette,
                                 PLAYER_SAVED_POS: pos, AREA_REGION: region, HP: 0x50})
        tcp("load_state", path=str(target))
        walk_out()

    try:
        outside = game.state("gate")                                 # before any entrance
        shop = game.state("eolis-meat-shop")                         # through Eolis's gate, in the meat shop

        def game_up():
            """The game: the one running, if it is."""
            if not game.running():
                game.launch()

        if args.wants("eolis"):
            game.launch(at="gate")
            assert zone()["banners"] == 0 and zone()["here"] is None, zone()
            print("ok: no banner outside Eolis's gate")

            # 1. Through the gate (door at block column 12): Eolis.
            walk_to(186, 196)
            act("up", 4, 10)
            settled_in(0, 1)
            banner("Eolis", 1)
            print("ok: walking through the gate names Eolis")

            # 2. Into the meat shop (column 3) and out: nothing more.
            walk_to(41, 56)
            act("up", 4, 10)
            until("the shop", lambda: ram(AREA) == BUILDINGS)
            wait(30)
            assert zone()["showing"] is None, zone()
            shop = out / "eolis-shop.cycstate"
            tcp("save_state", path=str(shop))
            walk_out()
            settled_in(0, 1)
            wait(60)
            assert zone()["banners"] == 1, zone()
            print("ok: stepping out of a shop shows nothing")

        if args.wants("trunk"):
            game_up()
            game.load(shop)
            # 3. Out of Eolis: the shop's door edited to lead to the door on screen 8
            #    (record bytes 5 and 6 are where zone names last saw the hero stand),
            #    with the "J" Key it takes in hand.
            before = zone()["banners"]
            warp_to = {AREA_SAVED_AREA: 0, AREA_SAVED_SCREEN: 8, PLAYER_SAVED_POS: 0x9C, HP: 0x50, SELECTED_ITEM: KEY_J}
            fixture(shop, out / "eolis-8.cycstate", warp_to, records={ZONE_NAMES: {5: 0, 6: 8}})
            tcp("load_state", path=str(out / "eolis-8.cycstate"))
            walk_out()
            settled_in(0, 8)
            assert zone()["banners"] == before, zone()
            # The screen's enemies cleared and HP full (a prerequisite, not
            # coverage): one hits the hero for 20 HP every few hundred frames, and a
            # run slowed by a busy machine can lose that many before it reaches the
            # door. Player_CheckHandleEnterDoor reads Up only on the frame it goes
            # down, and not while the screen scrolls, so press until the key's line
            # shows.
            game.poke({**{SPRITE_ENTITIES + i: 0xFF for i in range(8)}, HP: 0x50})
            for _ in range(5):
                walk_to(186, 196)                              # the door, column 12
                act("up", 4, 10)
                try:
                    until("the key's line", lambda: box_open() or ram(AREA) == 1, 120)
                    break
                except AssertionError:
                    pass
            for _ in range(120):                                # "You used the key"
                if ram(AREA) == 1:
                    break
                if box_open() and typed():
                    act("a", 6, 14)
                else:
                    hurry(10)
            settled_in(1, 0)
            banner("Trunk", before + 1)
            print("ok: the door out of Eolis names the Trunk, once")
            game.close()

        if args.wants("conflate-apolune"):
            # 4. Respawn in Conflate, then travel to Apolune's Guru. He dies outside
            #    Eolis's gate, before any entrance, so zone respawn wakes him at his
            #    temple; dying in the Trunk would bring him back at its door.
            (out / "game" / "faxanadu_gurus.txt").write_text("Eolis\nApolune\nConflate\n")
            game.launch()
            dead = out / "conflate-death.cycstate"
            fixture(outside, dead, {TEMPLE_SPAWN_POINT: CONFLATE, PLAYER_IS_DEAD: 1})
            tcp("load_state", path=str(dead))
            until("the death message", lambda: ram(MESSAGE_ID) == NEGATIVE_THOUGHTS, 600)
            for _ in range(120):
                if (ram(AREA), ram(SCREEN)) == (BUILDINGS, TEMPLE_SCREEN[CONFLATE]):
                    break
                if typed():
                    act("a")
                else:
                    hurry(10)
            until("Conflate's temple", lambda: (ram(AREA), ram(SCREEN)) == (BUILDINGS, TEMPLE_SCREEN[CONFLATE]))
            wait(60)
            before = zone()["banners"]
            assert zone()["showing"] is None, zone()
            walk_out()
            settled_in(TOWNS, 8)
            banner("Conflate", before + 1)
            print("ok: walking out after a respawn names Conflate")
            walk_to(137, 152)                          # the temple's door, column 9
            act("up", 4, 10)
            until("Conflate's temple", lambda: ram(AREA) == BUILDINGS)
            wait(30)
            travel(APOLUNE)
            wait(60)
            assert zone()["showing"] is None and zone()["banners"] == before + 1, zone()
            temple = out / "apolune-temple.cycstate"
            tcp("save_state", path=str(temple))
            walk_out()
            settled_in(TOWNS, 1)
            banner("Apolune", before + 2)
            print("ok: walking out after a Guru travel journey names Apolune")

        if args.wants("tower-castle"):
            game_up()
            game.load(shop)
            # 5. A tower and a castle, out of a building whose door is edited to
            #    lead there (the meat shop's; the banner is for where he arrives).
            before = zone()["banners"]
            warp(shop, "fortress", 1, 0x29, PALETTE_TOWER, 0x93, 1)     # where the door on screen $28 arrives
            settled_in(1, 0x29)
            banner("Tower of Fortress", before + 1)
            warp(shop, "fraternal", 6, 0x10, PALETTE_FRATERNAL, 0x91, 4)   # the door on Dartmoor's $0C
            settled_in(6, 0x10)
            banner("Castle of Fraternal", before + 2)
            print("ok: a tower and a castle are named")

        if args.wants("forepaw"):
            game_up()
            game.load(shop)
            # 6. Forepaw's screen 2, out of a building whose door is edited to lead
            #    there: the last column of tiles the game loads as it draws the
            #    screen leaves TextBox_Closed 0, with no box up.
            before = zone()["banners"]
            warp(shop, "forepaw", TOWNS, FOREPAW, PALETTE_TOWN, 0x99, REGION_TRUNK)
            until("Forepaw's name", lambda: zone()["showing"] == "Forepaw", 600)
            named = game.save("forepaw-named")
            state = zone()
            assert state["banners"] == before + 1 and state["age"] < 40, state
            assert ram(TEXTBOX_CLOSED) == 0 and not state["box"], (ram(TEXTBOX_CLOSED), state)
            game.shot("forepaw")
            frames = fades_on_time("Forepaw")
            assert ram(TEXTBOX_CLOSED) == 0, "TextBox_Closed reads 0 all along"
            print(f"ok: out into Forepaw's screen 2, TextBox_Closed reads 0 with no box up, and Forepaw's name "
                  f"still fades and goes in its {SHOWN} frames (the last {frames} watched)")
            # The menu there holds it, hidden; B closes it, and the name goes on.
            game.load(named)
            act("select", 4, 6)
            until("the menu", lambda: zone()["box"], 120)
            assert ram(TEXTBOX_WIDTH) == MENU_WIDTH and ram(TEXTBOX_CLOSED) == 0
            held = zone()
            wait(60)
            state = zone()
            assert state["box"] and state["showing"] == "Forepaw" and state["age"] == held["age"], (held, state)
            game.shot("forepaw-menu")
            act("b", 4, 6)
            until("the menu to close", lambda: ram(TEXTBOX_CLOSED) != 0 and not zone()["box"], 120)
            frames = fades_on_time("Forepaw")
            print(f"ok: the Select menu holds Forepaw's name at age {held['age']} behind it; closed, the name "
                  f"fades and goes (the last {frames} frames)")
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
