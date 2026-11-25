"""Play the Select menu's QUESTS and MAP rows end to end in a hidden window.

    python3 tests/menu/select_menu.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/menu
    python3 tests/menu/select_menu.py ... --part screens

The game runs uncapped and the script waits on the game's own state, never on
the clock, on a reserved TCP port (tests/e2e/fax_e2e.py). The rows the menu
draws are read back from the PPU buffer's ring ($0500), where the game queued
them, with their PPU addresses: nothing else writes it while the menu waits.
Four parts, each its own game, all but the last in Seamless:

rows      From Eolis town (the fixture: a new game, through the gate), a Hand
          Dagger in his weapons list: Select opens the menu 18 tiles tall
          with eight rows, QUESTS, MAP, WEAPON, ARMOR, SHIELD, MAGIC, ITEM,
          PLAYER, a row every two tiles, the cursor on QUESTS. Up from QUESTS
          wraps to PLAYER and Down back. A on WEAPON opens the weapons list
          (PlayerMenu_SelectedInventory 0, the dagger its one entry), B closes
          it; A on PLAYER opens the PLAYER page, B closes it.
screens   From Eolis town, The Ring of Elf given (in the quest log's record):
          A on QUESTS closes the menu and opens the quest log, paused, the
          quest under way; B closes it and the hero walks. A on MAP opens the
          map on the Eolis page, paused; B closes it and he walks again.
          Neither counts as a press of its button.
refused   In Eolis's meat shop with a map that has seen nothing (its record
          emptied, and the shop's way out moved to the gate's screen, which has
          no door to it, so nothing is seen while he stands there): A on MAP
          plays the invalid-choice sound ($0D) and the menu stays up, the
          cursor on MAP, as the Map button would show nothing. A on QUESTS
          still opens the quest log there.
original  With the remaster off (the stock view): the original's menu, 14 tiles
          tall with its six rows from WEAPON, no QUESTS or MAP; A on its top
          row opens the weapons list.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
from fax_e2e import Game, act, hold, parse_args, ram, tcp, until, wait  # noqa: E402

AREA, SCREEN, PLAYER_X, PAUSED = 0x24, 0x63, 0x9E, 0x120
PPU_READ, PPU_WRITE, PPU_BUFFER = 0x1F, 0x20, 0x500                 # PPUBuffer_ReadOffset, _WriteOffset, the ring
TEXTBOX_X, TEXTBOX_CLOSED, SELECTED_INVENTORY = 0x208, 0x20C, 0x20E   # X, Y, Width, Height; PlayerMenu_SelectedInventory
MENU_CURSOR, STRINGS_COUNT, DATA_ARRAY = 0x21E, 0x21F, 0x220
WEAPONS, NUMBER_OF_WEAPONS, HAND_DAGGER = 0x39D, 0x3C2, 0x00          # WeaponInventory
AREA_SAVED_SCREEN, SOUND_TO_PLAY = 0x3D6, 0x436                     # Area_SavedScreen, Temp_SoundIDToPlay
EOLIS, BUILDINGS = 0, 4
# The quest log's record (fax_quests.c): a version, then the quests given, The Ring of Elf bit 0.
QUESTS_RECORD, GIVEN, ELF = "faxanadu.quests", 1, 0x01
MENU_WIDTH, LIST_WIDTH, PLAYER_WIDTH = 0x0A, 0x14, 0x1A             # PlayerMenu_Show's, _ShowSubmenu's, _ShowStatusMenu's
REMASTER_ROWS = ("QUESTS", "MAP", "WEAPON", "ARMOR", "SHIELD", "MAGIC", "ITEM", "PLAYER")
ORIGINAL_ROWS = REMASTER_ROWS[2:]
SOUND_INVALID = 0x0D
PACKAGE = ("faxanadu.dx", "dx")
# The map's record (fax_map.c): a version, then what the hero has seen (312 bytes) and the last place (3).
MAP_RECORD, MAP_SEEN = "faxanadu.map", range(1, 1 + 312 + 3)
PARTS = ("rows", "screens", "refused", "original")


def menu():
    return tcp("select_menu_state")


def tiles(text):
    """Strings_ASCIIToIndex: a space is tile 0, a letter its ASCII less $31."""
    return bytes(0 if c == " " else ord(c) - 0x31 if c >= "A" else ord(c) for c in text)


def drawn(text):
    """The PPU addresses UI_DrawString queued `text` at, in the PPU buffer's ring."""
    ring = bytes(ram(PPU_BUFFER, 256))
    ring2, want, found = ring + ring, tiles(text), []
    for i in range(256):
        if ring2[i] == len(want) and ring2[i + 3:i + 3 + len(want)] == want:
            found.append(ring2[i + 1] << 8 | ring2[i + 2])
    return found


def main():
    args = parse_args(__doc__, parts=PARTS)
    game = Game.from_args(args)

    def open_menu(rows, name=None):
        """Select, until the menu waits for a button: every row queued for the PPU and drawn (the ring empty)."""
        act("select", 4, 6)
        until("the menu", lambda: ram(TEXTBOX_CLOSED) == 0 and ram(TEXTBOX_X + 2) == MENU_WIDTH and
              ram(STRINGS_COUNT) == rows and ram(PPU_READ) == ram(PPU_WRITE) and ram(0x100) == 12, 300)
        wait(4)
        return game.shot(name) if name else None

    def check_rows(rows):
        """Each row drawn once, two tiles below the one before, at the box's left plus two; none of the others."""
        x, y = ram(TEXTBOX_X), ram(TEXTBOX_X + 1)
        at = [drawn(text) for text in rows]
        assert all(len(a) == 1 for a in at), dict(zip(rows, at))
        first = at[0][0]
        assert first & 0x3FF == (y + 2) * 32 + x + 2, (hex(first), x, y)
        assert [a[0] - first for a in at] == [i * 64 for i in range(len(rows))], [hex(a[0]) for a in at]
        absent = [t for t in REMASTER_ROWS if t not in rows and drawn(t)]
        assert not absent, absent

    def walks():
        """Back in play: Left moves the hero."""
        x = ram(PLAYER_X)
        assert hold("left", lambda: ram(PLAYER_X) != x, 120), "the hero does not move"

    try:
        if args.wants("rows"):
            game.launch(at="eolis-town")
            game.poke({WEAPONS: HAND_DAGGER, NUMBER_OF_WEAPONS: 1})
            wait(10)
            assert menu()["enabled"] and menu()["height"] == 18, menu()
            open_menu(8, "menu")
            s = menu()
            assert (s["box_height"], s["rows"], s["cursor"], s["opened"], s["served"]) == (18, 8, 0, 1, 2), s
            assert list(ram(DATA_ARRAY, 8)) == [6, 7, 0, 1, 2, 3, 4, 5], list(ram(DATA_ARRAY, 8))
            check_rows(REMASTER_ROWS)
            print("ok: Select opens the menu 18 tall with QUESTS, MAP, WEAPON, ARMOR, SHIELD, MAGIC, ITEM and "
                  "PLAYER, a row every two tiles, the cursor on QUESTS (in Seamless)")
            act("up", 4, 6)
            assert ram(MENU_CURSOR) == 7, ram(MENU_CURSOR)
            act("down", 4, 6)
            assert ram(MENU_CURSOR) == 0, ram(MENU_CURSOR)
            print("ok: Up from QUESTS wraps to PLAYER, and Down wraps back")

            act("down", 4, 6)
            act("down", 4, 6)
            assert ram(MENU_CURSOR) == 2, ram(MENU_CURSOR)
            act("a", 4, 6)
            until("the weapons list", lambda: ram(TEXTBOX_X + 2) == LIST_WIDTH and ram(TEXTBOX_CLOSED) == 0, 300)
            wait(20)
            assert ram(SELECTED_INVENTORY) == 0 and menu()["originals"] == 1 and menu()["last_row"] == 2, menu()
            assert (ram(STRINGS_COUNT), ram(DATA_ARRAY)) == (1, HAND_DAGGER), (ram(STRINGS_COUNT), ram(DATA_ARRAY))
            game.shot("weapons")
            act("b", 4, 6)
            until("the menu to close", lambda: ram(TEXTBOX_CLOSED) != 0, 300)
            wait(10)
            print("ok: A on WEAPON opens the weapons list, and B closes it")

            open_menu(8)
            act("up", 4, 6)
            assert ram(MENU_CURSOR) == 7, ram(MENU_CURSOR)
            act("a", 4, 6)
            until("the PLAYER page", lambda: ram(TEXTBOX_X + 2) == PLAYER_WIDTH and ram(TEXTBOX_CLOSED) == 0, 300)
            wait(20)
            assert menu()["originals"] == 2 and menu()["last_row"] == 7, menu()
            game.shot("player")
            act("b", 4, 6)
            until("the menu to close", lambda: ram(TEXTBOX_CLOSED) != 0, 300)
            wait(10)
            walks()
            print("ok: A on PLAYER opens the PLAYER page, and B closes it")
            game.close()

        if args.wants("screens"):
            game.launch(at="eolis-town")
            game.poke({}, {QUESTS_RECORD: {GIVEN: ELF}})
            wait(10)
            quests = tcp("quest_state")
            assert quests["active"] == ["elf"], quests["active"]
            open_menu(8)
            assert ram(MENU_CURSOR) == 0
            act("a", 4, 6)
            until("the quest log", lambda: tcp("quest_state")["open"] and ram(PAUSED) != 0, 300)
            log = tcp("quest_state")
            assert ram(TEXTBOX_CLOSED) != 0, "the menu closed"
            assert (log["opens"], log["requests"], log["presses"]) == (quests["opens"] + 1, quests["requests"] + 1,
                                                                       quests["presses"]), log
            assert menu()["quests"] == 1 and menu()["last_row"] == 0, menu()
            wait(10)
            game.shot("quest-log")
            act("b", 4, 6)
            until("the quest log to close", lambda: not tcp("quest_state")["open"] and ram(PAUSED) == 0, 300)
            walks()
            print("ok: A on QUESTS closes the menu and opens the quest log, paused; B closes it, back to play")

            open_menu(8)
            act("down", 4, 6)
            assert ram(MENU_CURSOR) == 1, ram(MENU_CURSOR)
            before = tcp("map_state")
            act("a", 4, 6)
            until("the map", lambda: tcp("map_state")["open"] and ram(PAUSED) != 0, 300)
            m = tcp("map_state")
            assert ram(TEXTBOX_CLOSED) != 0, "the menu closed"
            assert m["page_name"] == "Eolis" and m["here"] == [EOLIS, ram(SCREEN)], m
            assert (m["opens"], m["requests"], m["presses"]) == (before["opens"] + 1, before["requests"] + 1,
                                                                 before["presses"]), m
            assert menu()["map"] == 1 and menu()["last_row"] == 1, menu()
            wait(10)
            game.shot("map")
            act("b", 4, 6)
            until("the map to close", lambda: not tcp("map_state")["open"] and ram(PAUSED) == 0, 300)
            walks()
            print("ok: A on MAP closes the menu and opens the map on the Eolis page, paused; B closes it, back "
                  "to play")
            game.close()

        if args.wants("refused"):
            game.launch(at="eolis-meat-shop")
            game.poke({AREA_SAVED_SCREEN: 0}, {MAP_RECORD: {i: 0 for i in MAP_SEEN}})
            wait(10)
            assert ram(AREA) == BUILDINGS and tcp("map_state")["pages_seen"] == 0, tcp("map_state")
            open_menu(8)
            act("down", 4, 6)
            sound = ram(SOUND_TO_PLAY)
            act("a", 4, 6)
            wait(10)
            s, m = menu(), tcp("map_state")
            assert s["refused"] == 1 and s["map"] == 0 and s["last_row"] == 1, s
            assert ram(SOUND_TO_PLAY) == SOUND_INVALID != sound, (ram(SOUND_TO_PLAY), sound)
            assert not m["open"] and m["requests"] == 0 and ram(PAUSED) == 0, m
            assert ram(TEXTBOX_CLOSED) == 0 and ram(TEXTBOX_X + 2) == MENU_WIDTH and ram(MENU_CURSOR) == 1
            game.shot("refused")
            print("ok: with nothing seen, A on MAP plays the invalid-choice sound and the menu stays up on MAP")
            act("up", 4, 6)
            act("a", 4, 6)
            until("the quest log", lambda: tcp("quest_state")["open"] and ram(PAUSED) != 0, 300)
            act("b", 4, 6)
            until("the quest log to close", lambda: not tcp("quest_state")["open"] and ram(PAUSED) == 0, 300)
            print("ok: A on QUESTS still opens the quest log, in a building")
            game.close()

        if args.wants("original"):
            game.set_features({PACKAGE: False})
            game.launch(at="eolis-town")
            assert not menu()["enabled"] and menu()["height"] == 14, menu()
            open_menu(6, "original-menu")
            assert ram(TEXTBOX_X + 3) == 14 and ram(MENU_CURSOR) == 0 and list(ram(DATA_ARRAY, 6)) == [0, 1, 2, 3, 4, 5]
            check_rows(ORIGINAL_ROWS)
            act("a", 4, 6)
            until("the weapons list", lambda: ram(TEXTBOX_X + 2) == LIST_WIDTH and ram(TEXTBOX_CLOSED) == 0, 300)
            assert ram(SELECTED_INVENTORY) == 0
            act("b", 4, 6)
            until("the menu to close", lambda: ram(TEXTBOX_CLOSED) != 0, 300)
            s = menu()
            assert (s["opened"], s["served"], s["originals"]) == (0, 0, 0), s
            print("ok: with the remaster off, the menu is the original's: six rows from WEAPON, 14 tall, and A "
                  "on WEAPON opens the weapons list")
            game.close()
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    main()
