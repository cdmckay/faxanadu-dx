"""Play Kept finds end to end in a hidden window (#38).

    python3 tests/finds/kept_finds.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/finds

The game runs uncapped and the script waits on the game's own state, never on
the clock, on a reserved TCP port (tests/e2e/fax_e2e.py). Each part starts
from Eolis's town (the eolis-town fixture) with eight Red Potions listed and
one selected, a full Items list, and a find dropped under the hero as
Sprites_PopulateNextAvailableSprite would set its slot up (an external
prerequisite, not coverage: see GAME_INTERNALS, "An item under the hero, for
tests"). The game touches it on its first frames and runs its own pickup.

1. original (the remaster off): Ripasheiku's Mattock ($5B). Its line says it
   was got and Quests $10 is set, but Player_PickUpItem adds nothing to the
   full list, and the sprite is gone: lost until a death clears the bit.
2. remastered: the same Mattock stays where it lies: no box opens, Quests
   stays 0, the list is unchanged, and kept_finds_state counts the refused
   touches and one sound for the whole touch. With a slot made free (the
   count set to 7, as drinking one and selecting another would leave it), the
   game's own pickup runs: its line, the Mattock listed, the bit set, the
   sprite gone. Then Zorugeriru's Wing Boots ($5C) with a pair already
   selected: Recharging items takes them (readies the pair), so Kept finds
   lets the game's pickup run: the line, the bit, the sprite gone, nothing
   added, no sound.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
from fax_e2e import Game, chunks, fixture, parse_args, ram, tcp, type_out, until, wait  # noqa: E402

PARTS = ("original", "remastered")
PACKAGE = ("faxanadu.remastered", "remastered")               # the one switch: off is the original game
HERO_X, HERO_Y = 0x9E, 0xA1
TEXTBOX_CLOSED, MESSAGE_ID = 0x20C, 0x213
QUESTS, ITEM_INVENTORY, SELECTED_ITEM, NUMBER_OF_ITEMS = 0x42D, 0x3AD, 0x3C1, 0x3C6
# CurrentSprites_*, 8 slots each
ENTITIES, SPRITE_X, SPRITE_Y, PHASES, FLAGS = 0x2CC, 0xBA, 0xC2, 0x2E4, 0x2DC
HIT_BY_MAGIC, HITBOX, SPRITE_HP, BEHAVIOR_L, BEHAVIOR_U, BEHAVIORS = 0x334, 0x304, 0x344, 0x354, 0x35C, 0x2D4
# Bank 14, by entity: what Sprites_PopulateNextAvailableSprite copies
HITBOX_TYPES, ENTITY_HP, BSCRIPTS = 0xB4DF, 0xB5A9, 0xAD2D
MATTOCK_QUEST_FIND, WING_BOOTS_QUEST_FIND = 0x5B, 0x5C            # Ripasheiku's Mattock, Zorugeriru's Wing Boots
MATTOCK, WING_BOOTS, RED_POTION, NONE = 0x09, 0x0F, 0x10, 0xFF
QUEST_WING_BOOTS, QUEST_MATTOCK = 0x08, 0x10
SLOT = 7


def bank14(image, addr, n=1):
    start = 16 + 14 * 0x4000 + addr - 0x8000
    return image[start:start + n]


def items():
    """The Items list, up to its count, and the selected item."""
    count = ram(NUMBER_OF_ITEMS)
    return list(ram(ITEM_INVENTORY, 8)[:count]), ram(SELECTED_ITEM)


def press_until(button, what, test, frames=60, tries=5):
    """Press `button` until `test` holds; a press that lands before the box
    reads the pad is lost on a loaded machine, so one that changed nothing in
    `frames` is pressed again."""
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

    def case(name, entity, selected=RED_POTION):
        """Eolis's town, eight Red Potions listed, `selected` selected, Quests
        0, and `entity` under the hero: its save state's Path."""
        town = game.state("eolis-town")
        script = bank14(image, BSCRIPTS + 2 * entity, 2)
        hero = (state_byte(town, HERO_X), state_byte(town, HERO_Y))
        writes = {ENTITIES + i: NONE for i in range(8)}
        writes.update({ENTITIES + SLOT: entity, SPRITE_X + SLOT: hero[0], SPRITE_Y + SLOT: hero[1],
                       PHASES + SLOT: 0, FLAGS + SLOT: 0, HIT_BY_MAGIC + SLOT: NONE,
                       HITBOX + SLOT: bank14(image, HITBOX_TYPES + entity)[0],
                       SPRITE_HP + SLOT: bank14(image, ENTITY_HP + entity)[0],
                       BEHAVIOR_L + SLOT: script[0], BEHAVIOR_U + SLOT: script[1], BEHAVIORS + SLOT: NONE})
        writes.update({ITEM_INVENTORY + i: RED_POTION for i in range(8)})
        writes.update({NUMBER_OF_ITEMS: 8, SELECTED_ITEM: selected, QUESTS: 0})
        path = out / f"{name}.cycstate"
        fixture(town, path, writes)
        return path

    def state_byte(path, addr):
        """A byte of a save state's CPU RAM (its HW chunk, at offset 23)."""
        data = Path(path).read_bytes()
        pos = next(p for tag, p, _ in chunks(data) if tag == b"HW  ")
        return data[pos + 23 + addr]

    def picked_up(what):
        """The find's own pickup: its line, closed with A; then its sprite gone."""
        until(f"{what}: its line", lambda: ram(TEXTBOX_CLOSED) == 0 and ram(MESSAGE_ID) != 0, 300)
        type_out(f"{what}: the line to type out", 600)
        press_until("a", f"{what}: the box to close", lambda: ram(TEXTBOX_CLOSED) != 0)
        until(f"{what}: the sprite removed", lambda: ram(ENTITIES + SLOT) == NONE, 60)

    try:
        if args.wants("original"):
            game.set_features({PACKAGE: False})
            mattock = case("mattock-full", MATTOCK_QUEST_FIND)
            game.launch()
            game.load(mattock)
            picked_up("Ripasheiku's Mattock")
            game.shot("original-mattock-gone")
            assert ram(QUESTS) & QUEST_MATTOCK, "the pickup set Quests $10"
            assert items() == ([RED_POTION] * 8, RED_POTION), items()
            print("ok: the original says it got Ripasheiku's Mattock and sets Quests $10, but the full list takes "
                  "nothing and the sprite is gone")
            game.close()

        if args.wants("remastered"):
            game.set_features({PACKAGE: True})
            mattock = case("mattock-full", MATTOCK_QUEST_FIND)
            game.launch()
            game.load(mattock)
            wait(60)
            state = tcp("kept_finds_state")
            game.shot("remastered-mattock-kept")
            assert state["enabled"] and state["refused"] >= 30 and state["beeps"] == 1, state
            assert state["last_entity"] == MATTOCK_QUEST_FIND, state
            assert ram(TEXTBOX_CLOSED) != 0, "no line"
            assert ram(ENTITIES + SLOT) == MATTOCK_QUEST_FIND, "the Mattock still lies there"
            assert ram(QUESTS) == 0, "no Quests bit"
            assert items() == ([RED_POTION] * 8, RED_POTION), items()
            game.poke({NUMBER_OF_ITEMS: 7})
            picked_up("Ripasheiku's Mattock, with room")
            game.shot("remastered-mattock-picked-up")
            assert ram(QUESTS) & QUEST_MATTOCK, "the pickup set Quests $10"
            assert items() == ([RED_POTION] * 7 + [MATTOCK], RED_POTION), items()
            assert tcp("kept_finds_state")["beeps"] == 1
            print("ok: with a full list Ripasheiku's Mattock stays where it lies, unsaid, its bit unset, with one "
                  "sound for the touch; with a slot free the game picks it up")

            boots = case("boots-held", WING_BOOTS_QUEST_FIND, selected=WING_BOOTS)
            before = tcp("kept_finds_state")
            game.load(boots)
            picked_up("Zorugeriru's Wing Boots, a pair held")
            after = tcp("kept_finds_state")
            assert ram(QUESTS) & QUEST_WING_BOOTS, "the pickup set Quests $08"
            assert items() == ([RED_POTION] * 8, WING_BOOTS), items()
            assert (after["refused"], after["beeps"]) == (before["refused"], before["beeps"]), (before, after)
            print("ok: Wing Boots found with a pair held and the list full go to Recharging items, as before: "
                  "the line, the bit, nothing added, no sound")
            game.close()
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
