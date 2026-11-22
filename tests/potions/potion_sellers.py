"""Play Potion Sellers end to end in a hidden window.

    python3 tests/potions/potion_sellers.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/potions
    python3 tests/potions/potion_sellers.py ... --part original

The game runs uncapped and the script waits on the game's own state, never on
the clock, on a reserved TCP port (tests/e2e/fax_e2e.py). It uses Block's test
harness (tests/block/harness.py) for its walks.

Every part starts in Eolis's meat shop (the fixture "eolis-meat-shop"), with
save-state edits for what it needs (external prerequisites, not coverage):
the gold, HP, the Items list, and where the shop's door leads. The hero walks
out and in again, which draws that gold on the HUD, then left to the counter.

eolis      With 500 Golds and HP at 32: Up on the Potion Seller, named on his
           tab, asks "A Red Potion to restore you for 160 Golds." as a
           question. A: the game takes 160, the game's item box shows the Red
           Potion until A, then "Thank you for shopping." and the end: a Red
           Potion in the Items list, HP as it was (no dried meat). Asked
           again, B on his question closes the box, nothing taken. Then
           Eolis's tool shop (Hand Dagger 400, Elixir 320, Deluge 400): no Red
           Potion in its list, and it buys the one he carries for 80, half its
           old price.
refusals   With 159 Golds, A on his question gets the game's own "This is not
           enough Golds." and the end: nothing taken, no potion. With 8 items
           (a full Items list) and 1000 Golds, the game's own "You can't carry
           any more." and the end: nothing taken, the list as it was.
daybreak   A later town: Daybreak's meat shop (the towns' screen $0A, its
           door at block $99) asks 1200 Golds, which Daybreak's tool shop
           asked, and A takes 1200 for a potion. Next door, its tool shop
           (Tilte 15000, Giant Blade 11500) lists no Red Potion and buys one
           for 600. Last, a save state from while Eolis's question typed,
           loaded after Daybreak's took message $C3's slot: the record puts
           the conversation back and serves Eolis's line again, the same
           picture as before.
original   With Faxanadu DX off: "Dried meat to bring energy for 50 Golds.";
           A takes 50 and gives HP, then "Thank you for shopping.", as the
           original does; and Eolis's tool shop lists its Red Potion for 160.

Screenshots of his question with his tab, the potion's box, the refusals,
the tool shops' lists and the original's meat go in OUT, each question or
box with its blinking "?" or arrow lit.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "block"))
import harness  # noqa: E402
from harness import (AREA, AREA_REGION, AREA_SAVED_AREA, AREA_SAVED_SCREEN, BUILDINGS, ENTITIES, GOLD, HP,  # noqa: E402
                     MESSAGE_ID, PLAYER_SAVED_POS, POS_X, SCREEN_SAVED_PALETTE, TOWNS, fixture, parse_args)
from fax_e2e import act, page_through, tcp, type_out, until, walk  # noqa: E402

PARTS = ("eolis", "refusals", "daybreak", "original")
PACKAGE = ("faxanadu.dx", "dx")
RECORD = "faxanadu.potion-sellers"                                  # its save-state record
ZONE_NAMES = "faxanadu.zone-names"                                  # its record: the area and screen, at 5 and 6
ITEMS, SELECTED_ITEM, NUMBER_OF_ITEMS, PORTRAIT = 0x3AD, 0x3C1, 0x3C6, 0x3C7
DATA_ARRAY, ARG_STRINGS_COUNT = 0x220, 0x21F                        # a shop's list, as Shop_Populate copied it
INTERRUPT_COUNTER, LIT = 0x1A, 0x14                                 # a box's "?" or arrow shows while it has $10
NO_PORTRAIT = 0xFF
RED_POTION, POISON, ITEM = 0x10, 0x11, 0x80                         # Items IDs; ITEM: the category, in a full ID
HAND_DAGGER, GIANT_BLADE, ELIXIR, DELUGE, TILTE = 0x00, 0x02, 0x92, 0x60, 0x64
SLOT_ID, THANKS, NOT_ENOUGH, CANT_CARRY, DRIED_MEAT_50 = 0xC3, 0x02, 0x03, 0x06, 0x07   # message IDs
GREETING, BUY_OR_SELL, WHAT_WOULD_YOU_LIKE = 0x0F, 0x10, 0x14
EOLIS, MEAT_SHOP_SCREEN, MEAT_SHOP_DOOR = 0, 1, 0x93                 # EOLIS_DOOR_LOCATIONS: building $00
TOOL_SHOP_SCREEN, TOOL_SHOP_DOOR, EOLIS_TOOL_SHOP = 3, 0x9A, 0x04    # building $04
DAYBREAK, DAYBREAK_MEAT_DOOR, DAYBREAK_TOOLS_DOOR = 0x0A, 0x99, 0x96  # the towns' screen $0A: buildings $31, $30
DAYBREAK_MEAT_SHOP, DAYBREAK_TOOL_SHOP = 0x31, 0x30
PALETTE_TOWN, REGION_BRANCH = 0x1B, 3
EOLIS_PRICE, DAYBREAK_PRICE, STOCK_MEAT = 160, 1200, 50
ASKING = 1                                                          # fax_potions.c's phase: his question up


def main():
    args = parse_args(__doc__, parts=PARTS)
    g = harness.Game.from_args(args)
    out = g.out

    def gold():
        return int.from_bytes(g.ram(GOLD, 3), "little")

    def items():
        return list(g.ram(ITEMS, 8)[:g.ram(NUMBER_OF_ITEMS)])

    def potions():
        return tcp("potions_state")

    def shops():
        return tcp("shops_state")

    def shot_lit(name):
        """A screenshot with the box's blinking mark lit: the game draws its
        "?" or arrow only while InterruptCounter has bit 4 ($10) set."""
        g.wait(((LIT - g.ram(INTERRUPT_COUNTER) - 1) & 0x1F) + 1)
        return g.shot(name)

    def done_talking():
        return g.ram(PORTRAIT) == NO_PORTRAIT and not g.box_open()

    def carrying(amount, carried=(), hp=0x20):
        """RAM writes for a hero with this gold and these Items, and `hp`."""
        w = {GOLD: amount & 0xFF, GOLD + 1: amount >> 8 & 0xFF, GOLD + 2: amount >> 16, HP: hp,
             NUMBER_OF_ITEMS: len(carried), SELECTED_ITEM: 0xFF}
        w.update({ITEMS + i: v for i, v in enumerate(carried)})
        return w

    def go_in(source, name, writes, area=EOLIS, screen=MEAT_SHOP_SCREEN, door=MEAT_SHOP_DOOR, building=0x00,
              palette=None, region=None):
        """From the save state `source`, inside a building, with `writes`: out
        of its door, edited to lead to `door` (block YX) on `screen`, and in
        there, to the counter. Going in draws the gold on the HUD."""
        target = out / f"{name}.cycstate"
        edits = {AREA_SAVED_AREA: area, AREA_SAVED_SCREEN: screen, PLAYER_SAVED_POS: door, **writes}
        if palette is not None:
            edits.update({SCREEN_SAVED_PALETTE: palette, AREA_REGION: region})
        fixture(source, target, edits, records={ZONE_NAMES: {5: area, 6: screen}})
        g.load(target)
        g.walk_out()
        assert g.where() == (area, screen), g.where()
        g.poke({ENTITIES + i: 0xFF for i in range(8)})             # no one in the way, or talked to at the door
        column = (door & 15) * 16
        g.walk_to(column - 6, column + 4)
        for _ in range(3):                                          # Up counts only on the frame it goes down
            g.act("up", 4, 10)
            try:
                g.until("the building", lambda: g.ram(AREA) == BUILDINGS, 120)
                break
            except AssertionError:
                pass
        g.until("the building", lambda: g.ram(AREA) == BUILDINGS and g.settled(), 600)
        assert g.ram(0x63) == building, g.ram(0x63)
        g.wait(40)                                                  # the way in, over: he can walk
        walk("left")                                                # to the counter: held until he stops

    def talk(first):
        """Up at the counter (again, if a press lands as a script winds
        down), until message `first` types."""
        for _ in range(5):                                          # Up counts only on the frame it goes down
            g.act("up", 4, 10)
            try:
                g.until("the shopkeeper", lambda: g.ram(PORTRAIT) != NO_PORTRAIT, 120)
                break
            except AssertionError:
                pass
        else:
            raise AssertionError("the shopkeeper does not answer")
        g.until(f"message {first:#x}", lambda: g.ram(MESSAGE_ID) == first, 300)

    def his_question(price, town, name=None):
        """His question up and typed out, through message $C3, his tab over
        it, with a screenshot if `name`."""
        talk(SLOT_ID)
        type_out("his question", 900, pauses=False)
        s, speaker = potions(), tcp("speaker_state")
        assert (s["phase"], s["price"], s["town"]) == (ASKING, price, town), s
        assert speaker["shown"] and speaker["name"] == "Potion Seller", speaker
        return shot_lit(name) if name else None

    def buy(before, price, carried, name=None):
        """A on his question: `price` taken, the potion's box (a screenshot if
        `name`), then his thanks and the end."""
        sold = potions()["sold"]
        act("a", 2, 0)
        until("the potion", lambda: potions()["sold"] == sold + 1, 2400)
        assert gold() == before - price, (gold(), before, price)
        g.wait(30)                                                  # the box is drawn, and waits for A
        assert items() == carried + [RED_POTION], items()
        assert g.ram(MESSAGE_ID) != THANKS and g.box_open(), "the potion's box is up"
        if name:
            shot_lit(name)
        act("a", 2, 0)
        until("\"Thank you for shopping.\"", lambda: g.ram(MESSAGE_ID) == THANKS, 300)
        page_through("his thanks", done_talking, 1200)
        assert gold() == before - price and items() == carried + [RED_POTION], (gold(), items())

    def refused(message, name):
        """A on his question: the game's own `message`, then the end."""
        act("a", 2, 0)
        until(f"message {message:#x}", lambda: g.ram(MESSAGE_ID) == message, 600)
        type_out("the game's refusal")
        shot_lit(name)
        page_through("the refusal", done_talking, 1200)

    def list_up(mode, what):
        """Until the `mode` list is on screen with its hint, and waits for a
        button (Quick shops counts the frames it reads the pad)."""
        until(what, lambda: (lambda s: s["shown"] == mode and s["list_up"] and s["hint"] is not None)(shops()), 900)
        waits = shops()["waits"]
        until("the list to wait for a button", lambda: shops()["waits"] > waits, 300)
        return shops()

    def tool_list(expect_items, expect_prices, name):
        """Up on a Tool Seller: his buy list at once (Quick shops), checked
        and screenshot."""
        g.act("up", 4, 6)
        s = list_up("buy", "the buy list")
        assert s["list"] == expect_items and s["prices"] == expect_prices, s
        g.wait(20)                                                  # the PPU buffer has drawn the list and portrait
        g.shot(name)
        return s

    def sells_potion(price):
        """Select to the sell list, a Red Potion at `price` on top; A sells
        it, and B leaves."""
        money = gold()
        g.act("select", 4, 6)
        s = list_up("sell", "the sell list")
        assert s["list"][0] == ITEM | RED_POTION and s["prices"][0] == price and s["cursor"] == 0, s
        act("a", 4, 6)
        until("the sale", lambda: shops()["sold"] == s["sold"] + 1, 2400)
        list_up("sell", "the sell list, after the sale")
        assert gold() == money + price and RED_POTION not in items(), (gold(), items())
        g.act("b", 4, 6)
        until("the conversation's end", done_talking, 300)

    try:
        shop = g.state("eolis-meat-shop")
        for part in PARTS:
            if not args.wants(part):
                continue
            dx = part != "original"
            g.set_features({PACKAGE: dx})
            g.launch()

            if part == "eolis":
                go_in(shop, "eolis", carrying(500, [POISON]))
                s = potions()
                assert s["enabled"] and s["selling"] and s["phase"] == 0, s
                his_question(EOLIS_PRICE, "Eolis", "eolis-question")
                buy(500, EOLIS_PRICE, [POISON], "eolis-potion")
                s = potions()
                assert g.ram(HP) == 0x20 and (s["offers"], s["sold"], s["phase"]) == (1, 1, 0), (g.ram(HP), s)
                print(f"ok: Eolis's Potion Seller, named on his tab, asks {EOLIS_PRICE} Golds for a Red Potion; A "
                      "takes it, the game's box shows the potion, he thanks you, and it is in the Items list; no HP")
                his_question(EOLIS_PRICE, "Eolis")
                act("b", 2, 0)
                until("the box to close", done_talking, 300)
                s = potions()
                assert gold() == 500 - EOLIS_PRICE and s["declined"] == 1 and s["sold"] == 1, (gold(), s)
                print("ok: B on his question closes the box, and nothing is taken")
                # Eolis's tool shop, with the potion he bought.
                go_in(g.save("eolis-bought"), "eolis-tools", {}, screen=TOOL_SHOP_SCREEN, door=TOOL_SHOP_DOOR,
                      building=EOLIS_TOOL_SHOP)
                s = tool_list([HAND_DAGGER, ELIXIR, DELUGE], [400, 320, 400], "eolis-tools")
                assert s["dropped"] == 1, s
                sells_potion(EOLIS_PRICE // 2)
                print("ok: Eolis's tool shop lists the Hand Dagger, the Elixir and Deluge, no Red Potion, and buys "
                      f"his for {EOLIS_PRICE // 2}")

            if part == "refusals":
                go_in(shop, "short", carrying(EOLIS_PRICE - 1))
                his_question(EOLIS_PRICE, "Eolis")
                refused(NOT_ENOUGH, "not-enough")
                s = potions()
                assert gold() == EOLIS_PRICE - 1 and items() == [] and (s["short"], s["sold"]) == (1, 0), (gold(), s)
                print(f"ok: with {EOLIS_PRICE - 1} Golds, the game's \"This is not enough Golds.\" and the end; "
                      "nothing taken, no potion")
                full = [RED_POTION, POISON, RED_POTION, RED_POTION, POISON, RED_POTION, POISON, RED_POTION]
                go_in(shop, "full", carrying(1000, full))
                his_question(EOLIS_PRICE, "Eolis")
                refused(CANT_CARRY, "cant-carry")
                s = potions()
                assert gold() == 1000 and items() == full and (s["full"], s["sold"]) == (1, 0), (gold(), items(), s)
                print("ok: with a full Items list, the game's \"You can't carry any more.\" and the end; nothing "
                      "taken, the list as it was")

            if part == "daybreak":
                # Eolis's question, saved as it types, for the record's check at the end.
                go_in(shop, "eolis", carrying(0))
                talk(SLOT_ID)
                typing = g.save("eolis-typing")
                type_out("his question", 900, pauses=False)
                eolis = shot_lit("eolis-question")
                act("b", 2, 0)
                until("the box to close", done_talking, 300)
                # Daybreak's meat shop, then its tool shop next door.
                go_in(shop, "daybreak", carrying(1500, [POISON]), area=TOWNS, screen=DAYBREAK, door=DAYBREAK_MEAT_DOOR,
                      building=DAYBREAK_MEAT_SHOP, palette=PALETTE_TOWN, region=REGION_BRANCH)
                his_question(DAYBREAK_PRICE, "Daybreak", "daybreak-question")
                buy(1500, DAYBREAK_PRICE, [POISON])
                print(f"ok: Daybreak's Potion Seller asks {DAYBREAK_PRICE} Golds, its tool shop's old price, and "
                      "takes it for a potion")
                go_in(g.save("daybreak-bought"), "daybreak-tools", {}, area=TOWNS, screen=DAYBREAK,
                      door=DAYBREAK_TOOLS_DOOR, building=DAYBREAK_TOOL_SHOP, palette=PALETTE_TOWN,
                      region=REGION_BRANCH)
                tool_list([TILTE, GIANT_BLADE], [15000, 11500], "daybreak-tools")
                sells_potion(DAYBREAK_PRICE // 2)
                print(f"ok: Daybreak's tool shop lists the Tilte and the Giant Blade, no Red Potion, and buys his for "
                      f"{DAYBREAK_PRICE // 2}")
                # The state from Eolis's question, loaded with Daybreak's line in the slot.
                g.load(typing)
                type_out("his question, again", 900, pauses=False)
                s = potions()
                assert (s["phase"], s["town"], s["price"]) == (ASKING, "Eolis", EOLIS_PRICE), s
                again = shot_lit("eolis-question-reloaded")
                assert again.read_bytes() == eolis.read_bytes(), "the reloaded question is not the same picture"
                act("b", 2, 0)
                until("the box to close", done_talking, 300)
                print("ok: a save state from while Eolis's question typed, loaded after Daybreak's, shows Eolis's "
                      "question again, picture for picture")

            if part == "original":
                go_in(shop, "original", carrying(100))
                assert not potions()["enabled"] and not potions()["selling"]
                talk(DRIED_MEAT_50)
                type_out("his dried meat")
                shot_lit("original-meat")
                act("a", 2, 0)
                until("\"Thank you for shopping.\"", lambda: g.ram(MESSAGE_ID) == THANKS, 1200)
                page_through("his thanks", done_talking, 1200)
                assert gold() == 100 - STOCK_MEAT and g.ram(HP) > 0x20 and items() == [], (gold(), g.ram(HP), items())
                print(f"ok: with the remaster off, he sells dried meat for {STOCK_MEAT} Golds, and it gives HP")
                go_in(g.save("original-meat"), "original-tools", {}, screen=TOOL_SHOP_SCREEN, door=TOOL_SHOP_DOOR,
                      building=EOLIS_TOOL_SHOP)
                talk(GREETING)
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
                g.shot("original-tools")
                act("b", 2, 0)
                until("the conversation's end", done_talking, 300)
                print("ok: and Eolis's tool shop lists its Red Potion, as the original")
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
