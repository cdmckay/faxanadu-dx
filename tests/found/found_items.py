"""Play Found items end to end in a hidden window.

    python3 tests/found/found_items.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/found
    python3 tests/found/found_items.py ... --part glass

The game runs uncapped and the script waits on the game's own state, never on
the clock, on a reserved TCP port (tests/e2e/fax_e2e.py). It uses Block's test
harness (tests/block/harness.py) for its walks.

Every part starts in Eolis's meat shop (the fixture "eolis-meat-shop"), with
save-state edits for what it needs (external prerequisites, not coverage):
where the shop's door leads (Area_Saved*, so walking out puts the hero where
the check is), what the hero holds, Quests, a screen's enemies cleared, the
random-item visit counter, and the Elixir spent (Recharging items' record).

shops      No tool shop lists the Wing Boots, the Hour Glass or the Elixir:
           Eolis's (Hand Dagger, Deluge), Forepaw's (Long Sword, Studded
           Mail, Small Shield), the one after Mascon on Mist $0D (Death) and
           Conflate's (Giant Blade, Magic Shield), each its own list less
           those three and its Red Potion.
boots      Trunk $2D, where a pair lay one counted visit in four, has none on
           8 visits, at every count, and the count stays put; the map has no
           dot for it. Zorugeriru's pair still drops: with him gone, they lie
           on the ledge at the top right of Trunk $3B, and the hero walks
           onto them ("I'm holding Wing Boots.", Quests $08). The Elixir
           lies on the same ledge a room to the right, Trunk $3C, a room left
           of the Spring of Trunk: he walks on and takes it, so an Elixir is
           found before the spring.
glass      Mist $30's Hour Glass lies on its ledge at every visit, at every
           count; the map shows its dot while he is elsewhere. He takes it
           ("I'm holding Hour Glass."), and dies: the respawn loads Mist $30
           again (zone respawn), with no Hour Glass, and writes
           found.hourglass = taken to file 1. Relaunched, the file continued
           and the Hour Glass poked out of his Items, Mist $30 has none: the
           file keeps it taken.
spring     The Spring of Trunk's old man (Trunk $3D) with the Elixir spent:
           his question, then "Come back with the medicine.", the quest not
           done and the Elixir still spent; the quest log gives the quest,
           and no "?" goes over him. Charged: a "?" over him, his question,
           "2 more springs", the quest done, and the Elixir kept, spent: the
           status bar's mark grey.
original   With the remaster off: Eolis's tool shop lists its Elixir (and its
           Red Potion), Trunk $2D's pair shows on its fourth count, Mist
           $30's Hour Glass not on another, and the spring takes the Elixir.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "block"))
import harness  # noqa: E402
from harness import (AREA, AREA_REGION, AREA_SAVED_AREA, AREA_SAVED_SCREEN, BUILDINGS, ENTITIES, HP,  # noqa: E402
                     MESSAGE_ID, PLAYER_IS_DEAD, PLAYER_SAVED_POS, POS_X, POS_Y, SCREEN_SAVED_PALETTE, TOWNS,
                     fixture, parse_args)
import fax_e2e  # noqa: E402
from fax_e2e import act, choose_file, page_through, tcp, title_menu, type_out, until, walk  # noqa: E402

PARTS = ("shops", "boots", "glass", "spring", "original")
PACKAGE = ("faxanadu.dx", "dx")
RECHARGE, FOUND, ZONE_NAMES = "faxanadu.recharge", "faxanadu.found-items", "faxanadu.zone-names"
ELIXIR_SPENT, TAKEN = 11, 1                                         # their records' bytes
ITEMS, SELECTED_ITEM, NUMBER_OF_ITEMS, PORTRAIT = 0x3AD, 0x3C1, 0x3C6, 0x3C7
SPECIAL_ITEMS, QUESTS, VISIT_COUNT = 0x42C, 0x42D, 0x43A           # Screen_TrackedVisitCount
DATA_ARRAY, ARG_STRINGS_COUNT = 0x220, 0x21F                        # a shop's list, as Shop_Populate copied it
DURATION_OINTMENT, NO_PORTRAIT = 0x427, 0xFF
BIT_ELIXIR, QUEST_SPRING_OF_TRUNK, QUEST_WING_BOOTS = 0x08, 0x01, 0x08
HOUR_GLASS, WING_BOOTS, RED_POTION = 0x0D, 0x0F, 0x10               # Items IDs
ITEM = 0x80                                                         # the Items category, in a full ID
HAND_DAGGER, LONG_SWORD, GIANT_BLADE, STUDDED_MAIL = 0x00, 0x01, 0x02, 0x21
SMALL_SHIELD, MAGIC_SHIELD, DELUGE, DEATH, ELIXIR = 0x40, 0x42, 0x60, 0x63, 0x92
UNSOLD = (ITEM | WING_BOOTS, ITEM | HOUR_GLASS, ELIXIR)
BOOTS_FIND, GLASS_FIND, ZORUGERIRU, SIR_GAWAINE, HORNET = 0x55, 0x56, 0x31, 0x1F, 0x07
GOT_ELIXIR, GOT_WING_BOOTS, GOT_HOUR_GLASS = 0xB0, 0xB3, 0xB4      # "I'm holding ..."
SPRING_ASKS, MORE_SPRINGS, COME_BACK = 0x59, 0x5A, 0x5B             # the Spring of Trunk's old man
GREETING, BUY_OR_SELL, WHAT_WOULD_YOU_LIKE = 0x0F, 0x10, 0x14
NEGATIVE_THOUGHTS = 0x24
EOLIS, TRUNK, MIST = 0, 1, 2
PALETTE_EOLIS, PALETTE_TOWER, PALETTE_MIST, PALETTE_SUFFER, PALETTE_TOWN = 0x00, 0x07, 0x0A, 0x0B, 0x1B
REGION_EOLIS, REGION_TRUNK, REGION_MIST, REGION_BRANCH = 0, 1, 2, 3
FILE = "faxanadu_file1.txt"                                         # the fixtures' new game is in file 1
CHARGED, SPENT = 1, 2                                               # charges_state's Elixir


class Game(harness.Game):
    pass


def main():
    args = parse_args(__doc__, parts=PARTS)
    g = Game.from_args(args)
    out = g.out

    def items():
        return list(g.ram(ITEMS, 8)[:g.ram(NUMBER_OF_ITEMS)]), g.ram(SELECTED_ITEM)

    def found():
        return tcp("found_state")

    def recharge():
        return tcp("recharge_state")

    def slots():
        return list(g.ram(ENTITIES, 8))

    def launch(on=True):
        g.set_features({PACKAGE: on})
        g.launch()
        assert found()["enabled"] == on, found()

    def warp(source, name, area, screen, pos, palette, region, writes=None, records=None):
        """From the save state `source`, inside a building, out onto block `pos`
        (YX) of `screen` in `area`, with `writes` and `records` edited in. The
        zone's name is not shown again (its record), and Ointment runs, so
        nothing knocks him about while the check is set up."""
        target = out / f"{name}.cycstate"
        fixture(source, target, {AREA_SAVED_AREA: area, AREA_SAVED_SCREEN: screen, SCREEN_SAVED_PALETTE: palette,
                                 PLAYER_SAVED_POS: pos, AREA_REGION: region, HP: 0x50, DURATION_OINTMENT: 0x7F,
                                 **(writes or {})},
                records={ZONE_NAMES: {5: area, 6: screen}, **(records or {})})
        g.load(target)
        g.walk_out()
        assert g.where() == (area, screen), (g.where(), area, screen)
        g.poke({POS_X: (pos & 0x0F) * 16, POS_Y: (pos >> 4) * 16})   # a step taken while the screen loaded
        g.wait(4)

    def clear(*entities):
        """Take these sprites off the screen (with none given, every enemy: not the
        people, $34-$45, nor the items and the fountains, from $48)."""
        now = slots()
        g.poke({ENTITIES + i: 0xFF for i in range(8)
                if now[i] != 0xFF and (now[i] in entities if entities else now[i] < 0x34)})

    def map_item(area, screen, entity):
        """The map's entry for the item `entity` on `area`'s `screen`: (shown, picked)."""
        for i, a, s, e, *_, shown, picked in tcp("map_state")["items"]:
            if (a, s, e) == (area, screen, entity):
                return shown, picked
        raise AssertionError(f"no map item {entity:#x} on {area}/${screen:02X}")

    def touch_find(message, step, frames=600):
        """Walk (`step`: "left" or "right") until message `message` ("I'm
        holding ..."), then page through it."""
        end = g.frame() + frames
        while g.ram(MESSAGE_ID) != message:
            assert g.frame() < end, f"no message {message:#x}: at {g.where()} x {g.ram(POS_X)} y {g.ram(POS_Y)}"
            act(step, 2, 2)                                     # (TextBox_Closed may read 0 with no box up)
        type_out("the line", 900, pauses=False)
        return message

    def no_banner():
        """Until the zone's name is gone, for a picture of the room."""
        until("the zone's name to go", lambda: tcp("zone_state")["showing"] is None, 300)

    def done_talking():
        return g.ram(PORTRAIT) == NO_PORTRAIT and not g.box_open()

    def enter_shop(source, name, area, screen, door, building, palette, region):
        """From `source`, out of the meat shop onto the tool shop's door, in,
        and to the counter."""
        warp(source, name, area, screen, door, palette, region)
        clear()
        column = (door & 15) * 16
        for _ in range(60):                                         # at the door already (the warp put him there):
            if column - 6 <= g.ram(POS_X) <= column + 4:            # no paging, since walking out onto Forepaw's
                break                                               # screen 2 leaves TextBox_Closed at 0
            fax_e2e.step_toward(column - 6, column + 4)
        for _ in range(3):                                          # Up counts only on the frame it goes down
            g.act("up", 4, 10)
            try:
                g.until("the building", lambda: g.ram(AREA) == BUILDINGS, 120)
                break
            except AssertionError:
                pass
        g.until("the building", lambda: g.ram(AREA) == BUILDINGS and g.settled(), 600)
        assert g.ram(0x63) == building, (g.ram(0x63), building)
        g.wait(40)
        walk("left")                                                # to the counter

    def buy_list(name):
        """Up on the Tool Seller: Quick shops' buy list, waiting for a button."""
        g.act("up", 4, 6)
        until("the buy list", lambda: (lambda s: s["shown"] == "buy" and s["list_up"] and s["hint"] is not None)(
            tcp("shops_state")), 900)
        waits = tcp("shops_state")["waits"]
        until("the list to wait for a button", lambda: tcp("shops_state")["waits"] > waits, 300)
        g.wait(20)                                                  # the list and the portrait drawn
        g.shot(name)
        s = tcp("shops_state")
        g.act("b", 4, 6)
        until("the conversation's end", done_talking, 300)
        return s

    def talk_to_spring(first):
        """Up beside the old man (block $2A on Trunk $3D) until his `first` message."""
        end = g.frame() + 1200
        while g.ram(MESSAGE_ID) != first:
            assert g.frame() < end, f"the old man does not answer: x {g.ram(POS_X)} y {g.ram(POS_Y)}"
            x = g.ram(POS_X)
            if not 140 <= x <= 156:
                act("right" if x < 140 else "left", 2, 6)
                continue
            g.act("up", 4, 20)                                      # Up counts only on the frame it goes down

    def marks_over_spring():
        """The quest log's marks over the people on screen: the old man's (script $26)."""
        return [m["mark"] for m in tcp("quest_state")["marks"] if m["script"] == 0x26]

    try:
        shop = g.state("eolis-meat-shop")
        for part in PARTS:
            if not args.wants(part):
                continue
            launch(part != "original")

            if part == "shops":
                lists = (
                    ("eolis", EOLIS, 3, 0x9A, 0x04, PALETTE_EOLIS, REGION_EOLIS,
                     [HAND_DAGGER, DELUGE], [400, 400]),
                    ("forepaw", TOWNS, 0x02, 0x9D, 0x0F, PALETTE_TOWN, REGION_TRUNK,
                     [LONG_SWORD, STUDDED_MAIL, SMALL_SHIELD], [1600, 2500, 1100]),
                    ("after-mascon", MIST, 0x0D, 0x7C, 0x20, PALETTE_MIST, REGION_MIST, [DEATH], [9800]),
                    ("conflate", TOWNS, 0x09, 0x99, 0x2D, PALETTE_TOWN, REGION_BRANCH,
                     [GIANT_BLADE, MAGIC_SHIELD], [8500, 9800]),
                )
                for name, area, screen, door, building, palette, region, want, prices in lists:
                    enter_shop(shop, name, area, screen, door, building, palette, region)
                    s = buy_list(f"{name}-tools")
                    assert s["list"] == want and s["prices"] == prices, (name, s)
                    assert not set(s["list"]) & set(UNSOLD), (name, s)
                    print(f"ok: {name}'s tool shop lists {[hex(i) for i in want]}: no Wing Boots, Hour Glass or "
                          "Elixir")
                print("ok: no tool shop sells the Wing Boots, the Hour Glass or the Elixir")

            if part == "boots":
                before = found()["gone"]
                for n in range(8):                                  # a counted visit in four would show them
                    warp(shop, f"trunk-2d-{n}", TRUNK, 0x2D, 0x31, PALETTE_TOWER, REGION_TRUNK, {VISIT_COUNT: n % 4})
                    g.wait(30)
                    assert BOOTS_FIND not in slots() and g.ram(VISIT_COUNT) == n % 4, (n, slots(), g.ram(VISIT_COUNT))
                    assert found()["gone"] == before + n + 1, found()
                    assert map_item(TRUNK, 0x2D, BOOTS_FIND)[0] == 0
                clear()
                g.shot("trunk-2d-no-boots")
                print("ok: Trunk $2D, where Wing Boots lay one visit in four, has none on 8 visits, the visit count "
                      "unmoved, and no dot on the map")
                # Zorugeriru's pair: on the ledge at the top right of $3B, once he is gone.
                warp(shop, "zorugeriru", TRUNK, 0x3B, 0x2D, PALETTE_TOWER, REGION_TRUNK,
                     {QUESTS: 0, DURATION_OINTMENT: 0xFF})                # a boss's screen would end it, with a line
                assert ZORUGERIRU in slots() and 0x5C in slots(), slots()
                clear()                                             # beaten, as far as his drop knows
                touch_find(GOT_WING_BOOTS, "right")                 # he drops onto the ledge, onto them
                g.shot("zorugeriru-boots")
                page_through("the line", lambda: not g.box_open(), 1200)
                g.wait(4)
                assert g.ram(QUESTS) & QUEST_WING_BOOTS and WING_BOOTS in items()[0], (g.ram(QUESTS), items())
                print("ok: Zorugeriru's Wing Boots still drop: \"I'm holding Wing Boots.\", Quests $08")
                # On along the same ledge, into $3C: the Elixir that lies there at every visit.
                assert not g.ram(SPECIAL_ITEMS) & BIT_ELIXIR
                touch_find(GOT_ELIXIR, "right", 900)
                assert g.where() == (TRUNK, 0x3C), g.where()
                g.shot("elixir-by-the-spring")
                page_through("the line", lambda: not g.box_open(), 1200)
                g.wait(4)
                assert g.ram(SPECIAL_ITEMS) & BIT_ELIXIR, g.ram(SPECIAL_ITEMS)
                print("ok: walking on from Zorugeriru's pair along their ledge into Trunk $3C, the hero finds the "
                      "Elixir that lies there at every visit, a room left of the Spring of Trunk")

            if part == "glass":
                for n in range(4):                                  # every count, the original's and the others
                    warp(shop, f"mist-30-{n}", MIST, 0x30, 0x64, PALETTE_SUFFER, REGION_MIST, {VISIT_COUNT: n})
                    g.wait(10)
                    s = found()
                    assert GLASS_FIND in slots() and g.ram(VISIT_COUNT) == n, (n, slots(), g.ram(VISIT_COUNT))
                    assert s["shown"] == n + 1 and s["there"] and not s["taken"], s
                    assert map_item(MIST, 0x30, GLASS_FIND)[0] == 1
                clear(SIR_GAWAINE)                                  # it shows once its room's enemies are gone
                no_banner()
                g.shot("hour-glass-lying")
                print("ok: Mist $30's Hour Glass lies on its ledge at every visit, at every count, the count unmoved")
                g.poke({POS_X: 232, POS_Y: 160})                    # on the floor, and right into $31
                fax_e2e.hold("right", lambda: g.where() == (MIST, 0x31), 300)
                until("Mist $31", lambda: g.where() == (MIST, 0x31) and g.settled(), 300)
                assert map_item(MIST, 0x30, GLASS_FIND)[0] == 1, "the map's dot while he is elsewhere"
                print("ok: the map shows its dot while the hero is elsewhere (Mist $31)")
                # Taken: he lands on it.
                warp(shop, "take-glass", MIST, 0x30, 0x22, PALETTE_SUFFER, REGION_MIST, {VISIT_COUNT: 0})
                clear(SIR_GAWAINE)
                until("I'm holding Hour Glass.", lambda: g.ram(MESSAGE_ID) == GOT_HOUR_GLASS, 300)
                type_out("the line", 900, pauses=False)
                g.shot("hour-glass-taken")
                page_through("the line", lambda: not g.box_open(), 1200)
                g.wait(4)
                s = found()
                assert s["taken"] and not s["there"] and s["takes"] == 1 and HOUR_GLASS in items()[0], (s, items())
                assert map_item(MIST, 0x30, GLASS_FIND)[0] == 0
                print("ok: he takes it, and the map's dot goes")
                # Dead: the respawn comes back to Mist $30 (zone respawn's way in) and loads it again.
                away = s["away"]
                g.poke({PLAYER_IS_DEAD: 1})
                fax_e2e.dissolve()
                page_through("the Guru's words", lambda: g.ram(PLAYER_IS_DEAD) == 0 and not g.box_open(), 3000)
                until("the respawn", lambda: g.where() == (MIST, 0x30) and g.settled(), 1800)
                g.wait(30)
                s = found()
                assert GLASS_FIND not in slots() and s["away"] == away + 1, (slots(), s)
                saved = (out / "game" / FILE).read_text()
                assert "found.hourglass = taken" in saved, saved
                print("ok: the respawn loads Mist $30 again, with no Hour Glass, and writes found.hourglass = taken")
                # The file continued, the Hour Glass poked out of his Items: still none.
                g.close()
                g.launch()
                title_menu()
                choose_file(False, 1)
                until("the temple", lambda: g.ram(AREA) == BUILDINGS and g.settled(), 1800)
                g.wait(30)
                assert found()["taken"], found()
                temple = g.save("continued")
                warp(temple, "after-continue", MIST, 0x30, 0x64, PALETTE_SUFFER, REGION_MIST,
                     {VISIT_COUNT: 3, NUMBER_OF_ITEMS: 0, SELECTED_ITEM: 0xFF})
                g.wait(10)
                s = found()
                assert GLASS_FIND not in slots() and s["taken"] and not s["there"], (slots(), s)
                clear(SIR_GAWAINE)
                no_banner()
                g.shot("hour-glass-gone")
                print("ok: continuing the file, the Hour Glass is still taken: none at Mist $30, even with none in "
                      "his Items")

            if part == "spring":
                warp(shop, "spring-spent", TRUNK, 0x3D, 0x18, PALETTE_TOWER, REGION_TRUNK,
                     {SPECIAL_ITEMS: 0, QUESTS: 0}, {RECHARGE: {ELIXIR_SPENT: 1}})
                clear()
                assert recharge()["elixir_spent"] and tcp("charges_state")["elixir"]["charge"] == SPENT
                talk_to_spring(SPRING_ASKS)
                page_through("his question, A to it", lambda: g.ram(MESSAGE_ID) == COME_BACK, 3000)
                type_out("his answer", 900, pauses=False)
                g.shot("spring-spent-refuses")
                page_through("the rest", done_talking, 3000)
                s = recharge()
                assert not g.ram(QUESTS) & QUEST_SPRING_OF_TRUNK and s["elixir_spent"] and s["springs"] == 0, s
                q = tcp("quest_state")
                assert "trunk" in q["active"] and "?" not in marks_over_spring(), (q["active"], q["marks"])
                print("ok: with the Elixir spent, the spring's old man asks, then says to come back with the medicine:"
                      " the quest given, not done, the Elixir still spent, and no \"?\" over him")
                # Charged (a Guru's, or a find's): a "?" over him, and he spends it.
                g.poke({SPECIAL_ITEMS: BIT_ELIXIR}, {RECHARGE: {ELIXIR_SPENT: 0}})
                g.wait(10)
                assert tcp("charges_state")["elixir"]["charge"] == CHARGED
                until("a \"?\" over him", lambda: "?" in marks_over_spring(), 120)
                g.shot("spring-charged")
                talk_to_spring(SPRING_ASKS)
                page_through("his question, A to it", lambda: g.ram(MESSAGE_ID) == MORE_SPRINGS, 3000)
                type_out("his line", 900)
                page_through("his line", lambda: recharge()["springs"] == 1, 3000)
                until("the Elixir spent", lambda: recharge()["springs"] == 1, 300)
                g.wait(30)
                c = tcp("charges_state")["elixir"]
                s = recharge()
                assert g.ram(QUESTS) & QUEST_SPRING_OF_TRUNK and not g.ram(SPECIAL_ITEMS) & BIT_ELIXIR, g.ram(QUESTS)
                assert s["elixir_spent"] and not s["elixir_charged"] and c["charge"] == SPENT, (s, c)
                g.shot("spring-spends-elixir")
                page_through("the rest", done_talking, 3000)
                g.wait(4 * 60)                                      # the mark's blink over: grey
                c = tcp("charges_state")["elixir"]
                assert c["charge"] == SPENT and c["blink"] == 0 and c["shown"], c
                g.shot("spring-elixir-grey")
                assert "trunk" in tcp("quest_state")["done"]
                print("ok: with a charged Elixir, a \"?\" over him; he asks, says 2 more springs, and the quest is "
                      "done, the Elixir kept, spent: the status bar's mark grey")

            if part == "original":
                # Eolis's tool shop, through the original's menu.
                enter_shop(shop, "original-eolis", EOLIS, 3, 0x9A, 0x04, PALETTE_EOLIS, REGION_EOLIS)
                g.act("up", 4, 10)
                until("the greeting", lambda: g.ram(MESSAGE_ID) == GREETING, 600)
                type_out("the greeting")
                act("a", 2, 0)
                until("the Buy/Sell menu's line", lambda: g.ram(MESSAGE_ID) == BUY_OR_SELL, 600)
                type_out("the menu")
                act("a", 2, 0)                                      # Buy
                until("\"What would you like?\"", lambda: g.ram(MESSAGE_ID) == WHAT_WOULD_YOU_LIKE, 600)
                type_out("the question")
                act("a", 2, 0)
                until("the list", lambda: g.ram(ARG_STRINGS_COUNT) == 4 and g.ram(MESSAGE_ID) == 0, 600)
                g.wait(10)
                listed = list(g.ram(DATA_ARRAY, 4))
                assert listed == [HAND_DAGGER, ITEM | RED_POTION, ELIXIR, DELUGE], listed
                g.shot("original-eolis-tools")
                act("b", 2, 0)
                until("the conversation's end", done_talking, 300)
                print("ok: with the remaster off, Eolis's tool shop sells the Elixir, as the original")
                warp(shop, "original-2d", TRUNK, 0x2D, 0x31, PALETTE_TOWER, REGION_TRUNK, {VISIT_COUNT: 3})
                g.wait(10)
                assert BOOTS_FIND in slots() and g.ram(VISIT_COUNT) == 0, (slots(), g.ram(VISIT_COUNT))
                warp(shop, "original-30", MIST, 0x30, 0x64, PALETTE_SUFFER, REGION_MIST, {VISIT_COUNT: 0})
                g.wait(10)
                assert GLASS_FIND not in slots() and g.ram(VISIT_COUNT) == 1, (slots(), g.ram(VISIT_COUNT))
                print("ok: Trunk $2D's Wing Boots show on their fourth count, Mist $30's Hour Glass not on another")
                warp(shop, "original-spring", TRUNK, 0x3D, 0x18, PALETTE_TOWER, REGION_TRUNK,
                     {SPECIAL_ITEMS: BIT_ELIXIR, QUESTS: 0})
                clear()
                talk_to_spring(SPRING_ASKS)
                page_through("his question, A to it", lambda: g.ram(MESSAGE_ID) == MORE_SPRINGS, 3000)
                page_through("the rest", done_talking, 3000)
                assert g.ram(QUESTS) & QUEST_SPRING_OF_TRUNK and not g.ram(SPECIAL_ITEMS) & BIT_ELIXIR
                assert not recharge()["elixir_spent"], recharge()
                print("ok: and the Spring of Trunk takes the Elixir for good, as the original")
            g.close()
        print("screenshots:", ", ".join(g.shots))
    except BaseException:
        if g.running():
            g.shot("failure")
        raise
    finally:
        g.close()


if __name__ == "__main__":
    main()
