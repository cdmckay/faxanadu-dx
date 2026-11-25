"""Play the Battle Suit and Dragon Slayer fix end to end in a hidden window.

    python3 tests/inventory/inventory_fix.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/inventory

The game runs uncapped and the script waits on the game's own state, never on
the clock, on a reserved TCP port (tests/e2e/fax_e2e.py), so any number can
run in parallel.

Copies the build to OUT and starts from the fixture `eolis-town`
(tests/e2e/fax_e2e.py): the hero in Eolis's town, where the Select menu opens
(it does not on the gate's screen). Each case edits that save's RAM (an
external prerequisite, not coverage): the weapons and armor, worn and listed,
the bytes after each list's count (what Player_RemoveItem leaves: a Quick
shops sale, or in the original equipping with nothing worn), and the item, the Battle Suit (entity $58) or the Dragon Slayer ($5A), put in
a sprite slot under the hero as Sprites_PopulateNextAvailableSprite would,
with the screen's other sprites cleared. The game's own code picks it up: the
touch (Player_HandleTouchItem), "I've got ...", which A closes, then the
routine adds it. The case checks both lists, and a screenshot shows the
item's list in the Select menu. Two parts (--list-parts), each its own game:

1. fixed: with the remaster on (the default), each pickup adds at its own list's
   count: the Battle Suit after the armor, the Dragon Slayer after the
   weapons, whatever the other list holds, and the other list is untouched:
   - every other armor listed and no weapons, every other weapon listed and
     no armor: the list ends full, with the item last;
   - armor listed and fewer weapons, weapons listed and fewer armor: kept;
   - an empty list with two sold items' bytes after it, and the other list
     longer: the item alone, nothing sold comes back;
   - a full list (four, a repeat the original's bug can leave): its last
     entry is replaced, as the Battle Helmet's pickup and the shops do.
2. original: with the remaster off, the original's bug in each case: the
   item is stored at the other list's count, and its own list's count set
   from it, so armor or weapons are lost, or the bytes after the list are
   listed. The other list is untouched either way: each routine only reads
   the wrong count.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
from fax_e2e import Game, chunks, fixture, menu_row, parse_args, ram, tcp, type_out, until, wait  # noqa: E402

AREA, SCREEN, HERO_X, HERO_Y = 0x24, 0x63, 0x9E, 0xA1
TEXTBOX_X, TEXTBOX_CLOSED, MESSAGE_ID, MENU_CURSOR = 0x208, 0x20C, 0x213, 0x21E
# CurrentSprites_*, 8 slots each
ENTITIES, SPRITE_X, SPRITE_Y, PHASES, FLAGS = 0x2CC, 0xBA, 0xC2, 0x2E4, 0x2DC
HIT_BY_MAGIC, HITBOX, SPRITE_HP, BEHAVIOR_L, BEHAVIOR_U, BEHAVIORS = 0x334, 0x304, 0x344, 0x354, 0x35C, 0x2D4
WEAPONS, ARMOR, NUMBER_OF_WEAPONS, NUMBER_OF_ARMORS = 0x39D, 0x3A1, 0x3C2, 0x3C3
SELECTED_WEAPON, SELECTED_ARMOR, PLAYER_CUR_WEAPON = 0x3BD, 0x3BE, 0x3C8
# Bank 14, by entity: what Sprites_PopulateNextAvailableSprite copies
HITBOX_TYPES, ENTITY_HP, BSCRIPTS = 0xB4DF, 0xB5A9, 0xAD2D
EOLIS, TOWN = 0, 1
BATTLE_SUIT_ITEM, DRAGON_SLAYER_ITEM = 0x58, 0x5A                   # sprite entities
GOT_BATTLE_SUIT, GOT_DRAGON_SLAYER = 0xB5, 0xB7                     # "I've got the ..."
MENU_WIDTH, LIST_WIDTH = 0x0A, 0x14                                 # PlayerMenu_Show's box, ShowSubmenu's
SLOT, NONE, MAX = 7, 0xFF, 4
# Weapons: Hand Dagger, Long Sword, Giant Blade, Dragon Slayer. Armor: Leather
# Armor, Studded Mail, Full Plate, Battle Suit.
DAGGER, LONG_SWORD, GIANT_BLADE, DRAGON_SLAYER = 0, 1, 2, 3
LEATHER, STUDDED, FULL_PLATE, BATTLE_SUIT = 0, 1, 2, 3
PACKAGE = ("faxanadu.dx", "dx")                                  # the one switch: off is the original game

# name, the item, (worn, listed, after the count) for the weapons, then the
# armor; the item's own list with the fix, and with the original's bug. The
# lists are what the game holds after the pickup, up to its count.
CASES = [
    ("suit-every-armor", BATTLE_SUIT_ITEM, (DAGGER, [], []), (NONE, [LEATHER, STUDDED, FULL_PLATE], []),
     [LEATHER, STUDDED, FULL_PLATE, BATTLE_SUIT], [BATTLE_SUIT]),
    ("suit-fewer-weapons", BATTLE_SUIT_ITEM, (GIANT_BLADE, [DAGGER], []), (FULL_PLATE, [LEATHER, STUDDED], []),
     [LEATHER, STUDDED, BATTLE_SUIT], [LEATHER, BATTLE_SUIT]),
    ("suit-sold-armor", BATTLE_SUIT_ITEM, (GIANT_BLADE, [DAGGER, LONG_SWORD], []),
     (FULL_PLATE, [], [STUDDED, STUDDED]), [BATTLE_SUIT], [STUDDED, STUDDED, BATTLE_SUIT]),
    ("suit-full", BATTLE_SUIT_ITEM, (DAGGER, [], []), (NONE, [LEATHER, STUDDED, FULL_PLATE, STUDDED], []),
     [LEATHER, STUDDED, FULL_PLATE, BATTLE_SUIT], [BATTLE_SUIT]),
    ("slayer-every-weapon", DRAGON_SLAYER_ITEM, (NONE, [DAGGER, LONG_SWORD, GIANT_BLADE], []), (LEATHER, [], []),
     [DAGGER, LONG_SWORD, GIANT_BLADE, DRAGON_SLAYER], [DRAGON_SLAYER]),
    ("slayer-fewer-armor", DRAGON_SLAYER_ITEM, (GIANT_BLADE, [DAGGER, LONG_SWORD], []), (BATTLE_SUIT, [LEATHER], []),
     [DAGGER, LONG_SWORD, DRAGON_SLAYER], [DAGGER, DRAGON_SLAYER]),
    ("slayer-sold-weapons", DRAGON_SLAYER_ITEM, (GIANT_BLADE, [], [LONG_SWORD, LONG_SWORD]),
     (BATTLE_SUIT, [LEATHER, STUDDED], []), [DRAGON_SLAYER], [LONG_SWORD, LONG_SWORD, DRAGON_SLAYER]),
    ("slayer-full", DRAGON_SLAYER_ITEM, (NONE, [DAGGER, LONG_SWORD, GIANT_BLADE, LONG_SWORD], []), (LEATHER, [], []),
     [DAGGER, LONG_SWORD, GIANT_BLADE, DRAGON_SLAYER], [DRAGON_SLAYER]),
]


PARTS = ("fixed", "original")                                     # each its own game


def bank14(image, addr, n=1):
    start = 16 + 14 * 0x4000 + addr - 0x8000
    return image[start:start + n]


def state_ram(path, addr):
    """A byte of a save state's CPU RAM (its HW chunk, at offset 23)."""
    data = Path(path).read_bytes()
    pos = next(p for tag, p, _ in chunks(data) if tag == b"HW  ")
    return data[pos + 23 + addr]


def listed(at, count_at):
    count = ram(count_at)
    assert count <= MAX, f"a count of {count}"
    return list(ram(at, MAX)[:count])


def kit(category, at, count_at, selected_at, worn, entries, after):
    """RAM writes: the item worn, `entries` listed, then `after` (stale bytes)
    and $00 to the list's end."""
    rest = (entries + after + [0] * MAX)[:MAX]
    writes = {at + i: v for i, v in enumerate(rest)}
    writes.update({count_at: len(entries), selected_at: worn})
    if category == "weapons":
        writes[PLAYER_CUR_WEAPON] = worn
    return writes


def press_until(button, what, test, frames=60, tries=5):
    """Press `button` until `test` holds. The game takes a button only on the
    frame it goes down, and only once the message or menu reads the pad, so a
    press can land too early when a loaded machine runs the game slowly: if
    nothing changed in `frames`, it was lost, and is pressed again. Every
    `test` here changes on the frame the press is taken (TextBox_Closed and
    the box's size are stored then), so a taken press is never repeated."""
    for _ in range(tries):
        tcp("action", name=button, frames=4)
        try:
            until(what, test, frames)
            return
        except AssertionError:
            pass
    raise AssertionError(f"{what}: {tries} presses of {button} did nothing")


def main():
    args = parse_args(__doc__, parts=PARTS)
    game = Game.from_args(args)
    out = game.out
    image = game.image

    def item_sprite(entity, hero):
        """The item in SLOT, under the hero (at `hero`, x and y), as
        Sprites_PopulateNextAvailableSprite sets a slot up, and no other sprite
        on the screen."""
        script = bank14(image, BSCRIPTS + 2 * entity, 2)
        writes = {ENTITIES + i: NONE for i in range(8)}
        writes.update({ENTITIES + SLOT: entity, SPRITE_X + SLOT: hero[0], SPRITE_Y + SLOT: hero[1],
                       PHASES + SLOT: 0, FLAGS + SLOT: 0, HIT_BY_MAGIC + SLOT: NONE,
                       HITBOX + SLOT: bank14(image, HITBOX_TYPES + entity)[0],
                       SPRITE_HP + SLOT: bank14(image, ENTITY_HP + entity)[0],
                       BEHAVIOR_L + SLOT: script[0], BEHAVIOR_U + SLOT: script[1], BEHAVIORS + SLOT: NONE})
        return writes

    def pick_up(town, hero, name, entity, weapons, armor):
        """Load the town with this kit and the item under the hero; close its
        line; return the weapons and armor listed afterwards."""
        case = out / "case.cycstate"
        writes = item_sprite(entity, hero)
        writes.update(kit("weapons", WEAPONS, NUMBER_OF_WEAPONS, SELECTED_WEAPON, *weapons))
        writes.update(kit("armor", ARMOR, NUMBER_OF_ARMORS, SELECTED_ARMOR, *armor))
        fixture(town, case, writes)
        tcp("load_state", path=str(case))
        message = GOT_BATTLE_SUIT if entity == BATTLE_SUIT_ITEM else GOT_DRAGON_SLAYER
        until(f"{name}: the pickup's line", lambda: ram(MESSAGE_ID) == message, 300)
        assert ram(ENTITIES + SLOT) == entity, "touched before the line"
        type_out(f"{name}: the line to type out", 600)
        press_until("a", f"{name}: the box to close", lambda: ram(TEXTBOX_CLOSED) != 0)
        wait(4)
        assert ram(ENTITIES + SLOT) == NONE, "Player_HandleTouchItem removes the item"
        return listed(WEAPONS, NUMBER_OF_WEAPONS), listed(ARMOR, NUMBER_OF_ARMORS)

    def menu_list(entry, name):
        """Select, Down to the row `entry` ("WEAPON", "ARMOR"; below QUESTS and
        MAP with the remaster), A: that list on screen, shot, then B closes it all."""
        press_until("select", "the menu", lambda: ram(TEXTBOX_CLOSED) == 0 and ram(TEXTBOX_X + 2) == MENU_WIDTH)
        wait(10)
        for row in range(1, menu_row(entry) + 1):
            press_until("down", f"the cursor on row {row}", lambda: ram(MENU_CURSOR) == row)
        press_until("a", "the list", lambda: ram(TEXTBOX_X + 2) == LIST_WIDTH)
        wait(30)
        game.shot(name)
        press_until("b", "the menu to close", lambda: ram(TEXTBOX_CLOSED) != 0)   # B on a list closes it all

    def play(town, hero, fixed):
        for name, entity, weapons, armor, want_fixed, want_original in CASES:
            got_weapons, got_armor = pick_up(town, hero, name, entity, weapons, armor)
            suit = entity == BATTLE_SUIT_ITEM
            own, other = (got_armor, got_weapons) if suit else (got_weapons, got_armor)
            other_kit = weapons if suit else armor
            want = want_fixed if fixed else want_original
            assert own == want, f"{name}: {'armor' if suit else 'weapons'} {own}, expected {want}"
            assert other == other_kit[1], f"{name}: the other list {other}, expected {other_kit[1]}"
            menu_list("ARMOR" if suit else "WEAPON", name if fixed else f"original-{name}")
            print(f"ok: {'' if fixed else 'original, '}{name}: {'armor' if suit else 'weapons'} {own}")

    try:
        town = game.state("eolis-town")
        assert (state_ram(town, AREA), state_ram(town, SCREEN)) == (EOLIS, TOWN), "the fixture is Eolis's town"
        hero = state_ram(town, HERO_X), state_ram(town, HERO_Y)

        if args.wants("fixed"):
            # 1. The fix: each pickup adds to its own list, at its own count.
            game.launch(at="eolis-town")
            assert tcp("inventory_fix_state")["enabled"], "the fix is on by default"
            play(town, hero, fixed=True)
            state = tcp("inventory_fix_state")
            suits = sum(1 for c in CASES if c[1] == BATTLE_SUIT_ITEM)
            assert (state["battle_suits"], state["dragon_slayers"]) == (suits, len(CASES) - suits), state
            print("ok: with the fix the Battle Suit joins the armor and the Dragon Slayer the weapons, at their own "
                  "counts; full, the last entry is replaced; the other list is untouched")
            game.close()

        if args.wants("original"):
            # 2. With the remaster off, the original's bug.
            game.set_features({PACKAGE: False})
            game.launch(at="eolis-town")
            assert not tcp("inventory_fix_state")["enabled"], "the remaster is off"
            play(town, hero, fixed=False)
            assert tcp("inventory_fix_state")["battle_suits"] == 0, "the hooks are off"
            print("ok: with the remaster off, the original's bug: each pickup adds at the other list's count, "
                  "losing armor or weapons, or listing the bytes after the list")
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
