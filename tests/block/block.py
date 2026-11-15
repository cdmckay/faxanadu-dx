"""Play Block end to end in a hidden window.

    python3 tests/block/block.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/block

Copies the build to OUT and starts a new game over TCP, walks through Eolis's
gate into a shop, and from there walks out where the test needs to be: the
shop's door is edited to lead elsewhere (save-state RAM, an external
prerequisite, as tests/zones/zone_names.py does), as are the hero's shield and
gold. Block is held the way a player does: TCP `key` and `pad` feed the window
real SDL key and controller events.

1. Outside the gate with the Small Shield: holding Block (V), the hero stands
   still and does not jump; let go, he walks.
2. In the fortress of Zenis (area 7, screen 16), the Execution Hood casts at
   the hero's height (its shot's Y edited) from the side he faces. The Small
   Shield alone lets half its 8 HP through; held Block, nothing, with the
   shield's sound. A shot at his head misses the shield: 8 HP, or nothing with
   Block. From behind, or without a shield: 8 HP. The Battle Helmet alone
   stops nothing (8 HP, as in the original); held Block, nothing.
3. In Apolune's tool shop with 10,000 gold: buying the Small Shield when the
   hero already has the Large one says nothing more; buying it as his first
   shield adds one line naming the key, after the shopkeeper's thanks, and
   faxanadu_block.txt remembers it. Again with no shield, or after a restart:
   nothing.
   The Battle Helmet, in Zoradohna's room (Branch, area 5, screen 7, the boss
   removed from the save state), picked up as the first shield-slot item: the
   line follows "I've got the Battle Helmet." Picked up again once taught:
   nothing.
   With the Gurus' features off (Guru travel and Save files), whose SAY step
   and message slot the line borrows: the line still shows.
4. config.ini rebinds it (BlockKey = S, BlockButton = leftstick), and the
   window writes the bindings back; the line names the device last used.
5. With Shields (Block and Sugata's flash) off in the mod selection, V does
   nothing.
"""
import argparse
from pathlib import Path
import re
import sys
import time
import tomllib

sys.path.insert(0, str(Path(__file__).resolve().parent))
from harness import (Game, fixture, AREA, BUILDINGS, TOWNS, POS_X, POS_Y, PLAYER_FLAGS, FACING_RIGHT, MESSAGE_ID,  # noqa: E402
                     ENTITIES, SPRITE_X, SPRITE_Y, GOLD, SHIELD_INVENTORY, SELECTED_SHIELD, NUMBER_OF_SHIELDS,
                     SMALL, LARGE, HELMET, NO_SHIELD)

ZENIS, SCREEN_16, PALETTE_FORTRESS, REGION_FORTRESS = 7, 16, 0x0F, 5
BRANCH, SCREEN_7, PALETTE_BRANCH, REGION_BRANCH = 5, 7, 0x08, 3     # Zoradohna's room
ZORADOHNA, BATTLE_HELMET_OBJ = 0x2E, 0x59
APOLUNE, PALETTE_TOWN, REGION_TRUNK, TOOL_SHOP = 0, 0x1B, 1, 8   # Apolune is towns' screen 0; its tool shop building 8
HOOD_MAGIC, MAGIC_DAMAGE = 0x51, 8                                # the Execution Hood's shot, SPRITE_ENTITY_DAMAGES
SHIELD_HEIGHT, HEAD_HEIGHT = 150, 136                             # the shot's Y, the hero on the ledge at 144
THANK_YOU, OUR_LINE = 0x02, 0xC3                                # "Thank you for shopping."; Guru travel's slot
LINE = "Hold V to raise\nyour shield\nagainst magic."
PACKAGE = "faxanadu.enhancement.shields"    # Block and Sugata's flash, one mod


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for key in ("build", "rom", "out"):
        ap.add_argument("--" + key, type=Path, required=True)
    ap.add_argument("--game-arg", action="append", default=[],
                    help="extra game option, e.g. --game-arg=--world-view=seamless (repeatable)")
    ap.add_argument("--port", type=int, default=0, help="TCP port (default: a free one)")
    ap.add_argument("--realtime", action="store_true", help="run at normal speed instead of uncapped")
    args = ap.parse_args()
    g = Game(args.build, args.rom, args.out, args.game_arg, args.realtime, args.port)

    def block():
        return g.tcp("block_state")

    def switch_off(which):
        """The mod selection: every feature of the packages `which` picks off,
        the rest as they come. Returns the packages."""
        picked, text = [], "format_version = 1\n"
        for manifest in sorted((g.out / "game" / "mods" / "packages").glob("*/*/manifest.toml")):
            data = tomllib.loads(manifest.read_text())
            if which(data["id"]):
                picked.append(data["id"])
                text += "".join(f'\n[[feature]]\npackage_id = "{data["id"]}"\nid = "{f["id"]}"\nenabled = false\n'
                                for f in data.get("feature", []))
        (g.out / "game" / "mods" / "state.toml").write_text(text)
        return picked

    def shot_at(cast, slot, name, y, writes, hold=None, picture=None):
        """From `cast`, the Hood's shot in `slot` moved to height `y`: play it
        out. Returns (HP lost, Block's counters' change)."""
        target = g.out / f"{name}.cycstate"
        fixture(cast, target, {SPRITE_Y + slot: y, **writes})
        g.load(target)
        before, hp = block(), g.hp()
        if hold:
            g.tcp("key", name=hold, frames=200)
        g.until("Block to be up" if hold else "the shot", lambda: block()["raised"] == bool(hold), 10)
        if picture:
            g.until("the shot to come near", lambda: g.ram(SPRITE_X + slot) < 52, 150)
            g.shot(picture)
        g.until("the shot to be gone", lambda: g.ram(ENTITIES + slot) != HOOD_MAGIC, 200)
        g.wait(10)
        after = block()
        if hold:
            g.tcp("key", name=hold, frames=1)
            g.wait(3)
        return hp - g.hp(), {k: after[k] - before[k] for k in ("magic_blocked", "shield_saved")}

    def talk_and_buy():
        """Walk to the shopkeeper, buy the first item (the Small Shield), and
        page through to the end. Returns the messages (MessageID) shown after
        the purchase, and the picture of our line if there was one."""
        g.act("left", 200, 10)
        g.act("up", 4, 10)
        shields, said, picture = g.ram(NUMBER_OF_SHIELDS), [], None
        for _ in range(16):
            end = g.frame() + 600
            while not g.typed():
                message = g.ram(MESSAGE_ID)
                if g.ram(NUMBER_OF_SHIELDS) > shields and message and (not said or said[-1] != message):
                    said.append(message)
                assert g.frame() < end, "a page to type out"
            g.wait(6)
            if not g.box_open():
                break
            if block()["saying"] and not picture:
                picture = g.shot("tutorial-line")
            g.act("a", 4, 20)
        assert not g.box_open() and g.ram(NUMBER_OF_SHIELDS) == shields + 1, (g.box_open(), shields)
        g.wait(10)
        return said, picture

    def pick_up_helmet():
        """Walk left onto the Battle Helmet and page through what is said.
        Returns whether our line was said, and its picture."""
        saying, picture = False, None
        for _ in range(300):
            if g.box_open():
                if g.typed():
                    if block()["saying"]:
                        saying = True
                        picture = picture or g.shot("battle-helmet-line")
                    g.act("a", 6, 14)
                else:
                    g.wait(4)
                continue
            if g.ram(ENTITIES + 7) != BATTLE_HELMET_OBJ:
                g.wait(10)
                return saying, picture
            g.act("left", 2, 0)
        raise AssertionError("could not pick up the Battle Helmet")

    try:
        g.launch()
        state = block()
        assert state["enabled"] and state["binding"] == "V / B" and not state["taught"], state
        g.new_game_at_gate()
        gate = g.save("gate")
        g.act("up", 4, 10)
        g.until("Eolis", lambda: g.where() == (0, 1), 300)
        g.wait(30)
        g.walk_to(41, 56)
        g.act("up", 4, 10)
        g.until("the shop", lambda: g.ram(AREA) == BUILDINGS)
        g.wait(30)
        shop = g.save("eolis-shop")

        # 1. Standing still.
        shielded = g.out / "gate-shield.cycstate"
        fixture(gate, shielded, {SELECTED_SHIELD: SMALL})
        g.load(shielded)
        g.act("left", 30, 4)
        x, y = g.ram(POS_X), g.ram(POS_Y)
        g.tcp("key", name="V", frames=120)
        g.wait(4)
        g.tcp("action", name="left", frames=40)
        g.wait(44)
        g.tcp("action", name="a", frames=10)
        g.wait(14)
        assert (g.ram(POS_X), g.ram(POS_Y)) == (x, y) and block()["raised"], (g.ram(POS_X), g.ram(POS_Y), x, y)
        assert block()["stood"] > 0
        g.shot("blocking-stands-still")
        g.until("Block to be let go", lambda: not block()["held"], 120)
        g.act("left", 20, 4)
        assert g.ram(POS_X) < x and not block()["raised"], (g.ram(POS_X), x)
        print(f"ok: holding Block with a shield, Left and A move the hero nowhere (x {x}); let go, he walks")

        # 2. The Execution Hood's magic.
        g.warp(shop, "zenis-16", ZENIS, SCREEN_16, PALETTE_FORTRESS, 0x91, REGION_FORTRESS)
        g.until("the Hood's shot", lambda: HOOD_MAGIC in list(g.ram(ENTITIES, 8)), 300)
        slot = list(g.ram(ENTITIES, 8)).index(HOOD_MAGIC)
        cast = g.save("hood-cast")
        assert g.ram(PLAYER_FLAGS) & FACING_RIGHT and g.ram(SPRITE_X + slot) > g.ram(POS_X)
        small = {SELECTED_SHIELD: SMALL}
        lost, d = shot_at(cast, slot, "shield-alone", SHIELD_HEIGHT, small)
        assert lost == MAGIC_DAMAGE // 2 and d == {"magic_blocked": 0, "shield_saved": 0}, (lost, d)
        lost, d = shot_at(cast, slot, "shield-block", SHIELD_HEIGHT, small, "V", picture="blocking-magic")
        assert lost == 0 and d == {"magic_blocked": 0, "shield_saved": 1}, (lost, d)
        print(f"ok: the Small Shield alone lets {MAGIC_DAMAGE // 2} of the shot's {MAGIC_DAMAGE} HP through; "
              "holding Block, none")
        lost, d = shot_at(cast, slot, "head", HEAD_HEIGHT, small)
        assert lost == MAGIC_DAMAGE, lost
        lost, d = shot_at(cast, slot, "head-block", HEAD_HEIGHT, small, "V")
        assert lost == 0 and d == {"magic_blocked": 1, "shield_saved": 0}, (lost, d)
        print(f"ok: a shot at his head misses the shield and takes {MAGIC_DAMAGE}; holding Block, it is stopped")
        behind = {SELECTED_SHIELD: SMALL, PLAYER_FLAGS: g.ram(PLAYER_FLAGS) & ~FACING_RIGHT}
        lost, d = shot_at(cast, slot, "behind-block", SHIELD_HEIGHT, behind, "V")
        assert lost == MAGIC_DAMAGE and d == {"magic_blocked": 0, "shield_saved": 0}, (lost, d)
        print("ok: from behind, Block stops nothing")
        target = g.out / "no-shield.cycstate"
        fixture(cast, target, {SPRITE_Y + slot: SHIELD_HEIGHT, SELECTED_SHIELD: NO_SHIELD})
        g.load(target)
        g.tcp("key", name="V", frames=200)
        g.wait(6)
        assert not block()["raised"], block()
        hp = g.hp()
        g.until("the shot to be gone", lambda: g.ram(ENTITIES + slot) != HOOD_MAGIC, 200)
        g.wait(10)
        assert hp - g.hp() == MAGIC_DAMAGE, (hp, g.hp())
        g.tcp("key", name="V", frames=1)
        g.wait(3)
        print("ok: without a shield, Block does nothing and the shot takes 8")
        helmet = {SELECTED_SHIELD: HELMET}
        lost, d = shot_at(cast, slot, "battle-helmet", SHIELD_HEIGHT, helmet)
        assert lost == MAGIC_DAMAGE and d == {"magic_blocked": 0, "shield_saved": 0}, (lost, d)
        lost, d = shot_at(cast, slot, "battle-helmet-block", SHIELD_HEIGHT, helmet, "V")
        assert lost == 0 and d == {"magic_blocked": 1, "shield_saved": 0}, (lost, d)
        print(f"ok: the Battle Helmet alone stops nothing ({MAGIC_DAMAGE} HP, as in the original); holding Block, "
              "it stops the shot")

        # 3. The tutorial, in Apolune's tool shop.
        g.warp(shop, "apolune", TOWNS, APOLUNE, PALETTE_TOWN, 0x94, REGION_TRUNK, {GOLD: 0x10, GOLD + 1: 0x27})
        g.act("up", 4, 10)
        g.until("the tool shop", lambda: g.where() == (BUILDINGS, TOOL_SHOP), 300)
        g.until("the shop to settle", g.settled, 300)
        g.wait(20)
        tool_shop = g.save("tool-shop")
        owns_large = g.out / "tool-shop-large.cycstate"
        fixture(tool_shop, owns_large, {SHIELD_INVENTORY: LARGE, NUMBER_OF_SHIELDS: 1})
        g.load(owns_large)
        g.wait(4)
        said, _ = talk_and_buy()
        assert said == [THANK_YOU] and block()["tutorials"] == 0 and not block()["taught"], (said, block())
        print("ok: a second shield (the Small, owning the Large) gets only the shopkeeper's thanks")
        g.load(tool_shop)
        g.wait(4)
        said, picture = talk_and_buy()
        state = block()
        assert said == [THANK_YOU, OUR_LINE] and picture, said
        assert state["tutorials"] == 1 and state["taught"] and state["line"] == LINE, state
        sidecar = g.out / "game" / "faxanadu_block.txt"
        assert sidecar.read_text() == "block.taught\n", sidecar.read_text()
        print("ok: the first shield gets one more line after the thanks: " + LINE.replace("\n", " / "))
        g.load(tool_shop)
        g.wait(4)
        said, _ = talk_and_buy()
        assert said == [THANK_YOU] and block()["tutorials"] == 1, said
        g.close()
        g.launch()
        assert block()["taught"]
        g.load(tool_shop)
        g.wait(4)
        said, _ = talk_and_buy()
        assert said == [THANK_YOU] and block()["tutorials"] == 0, said
        print("ok: it is said once: not again, nor after a restart (faxanadu_block.txt)")
        g.close()

        # 3b. The Battle Helmet, picked up as the first shield-slot item.
        sidecar.unlink()
        g.launch()
        assert not block()["taught"]
        g.warp(shop, "branch-7", BRANCH, SCREEN_7, PALETTE_BRANCH, 0x9C, REGION_BRANCH)
        assert list(g.ram(ENTITIES + 6, 2)) == [ZORADOHNA, BATTLE_HELMET_OBJ], list(g.ram(ENTITIES, 8))
        boss_gone = g.out / "zoradohna-gone.cycstate"
        g.save("branch-7-in")
        fixture(g.out / "branch-7-in.cycstate", boss_gone, {ENTITIES + 6: 0xFF})
        g.load(boss_gone)
        saying, picture = pick_up_helmet()
        state = block()
        assert saying and picture and state["tutorials"] == 1 and state["taught"], (saying, state)
        assert g.ram(SHIELD_INVENTORY) == HELMET and g.ram(NUMBER_OF_SHIELDS) == 1
        assert sidecar.read_text() == "block.taught\n"
        print("ok: the Battle Helmet as the first shield-slot item: the line follows \"I've got the Battle Helmet.\"")
        g.load(boss_gone)
        saying, _ = pick_up_helmet()
        assert not saying and block()["tutorials"] == 1 and g.ram(NUMBER_OF_SHIELDS) == 1
        print("ok: picked up again once taught, only the game's line")
        g.close()

        # 3c. With the Gurus off (Guru travel and Save files): the line borrows
        # Guru travel's step and slot, but supplies them itself.
        sidecar.unlink()
        gurus = switch_off(lambda package: "guru" in package or "save-files" in package)
        g.launch()
        assert gurus and not g.tcp("travel_state")["enabled"] and not block()["taught"], gurus
        g.load(tool_shop)
        g.wait(4)
        said, _ = talk_and_buy()
        state = block()
        assert said == [THANK_YOU, OUR_LINE] and state["tutorials"] == 1 and state["line"] == LINE, (said, state)
        print(f"ok: with {', '.join(gurus)} off, the first shield still gets the line")
        g.close()
        switch_off(lambda package: False)

        # 4. config.ini rebinds it.
        text = g.config.read_text()
        assert re.search(r"^BlockKey = V$", text, re.M) and re.search(r"^BlockButton = b$", text, re.M), text
        g.config.write_text(re.sub(r"^BlockButton = b$", "BlockButton = leftstick",
                                   re.sub(r"^BlockKey = V$", "BlockKey = S", text, flags=re.M), flags=re.M))
        g.launch()
        state = block()
        assert state["binding"] == "S / L3" and state["line"] == LINE.replace("V", "S"), state
        g.load(shielded)
        g.tcp("key", name="V", frames=20)
        g.wait(6)
        assert not block()["raised"]
        g.tcp("key", name="S", frames=20)
        g.wait(6)
        assert block()["raised"]
        g.wait(20)
        g.tcp("pad", buttons="leftstick", frames=20)
        g.wait(6)
        state = block()
        assert state["raised"] and state["line"] == "Hold L3 to raise\nyour shield\nagainst magic.", state
        g.close()
        text = g.config.read_text()
        assert re.search(r"^BlockKey = S$", text, re.M) and re.search(r"^BlockButton = leftstick$", text, re.M), text
        print("ok: BlockKey = S and BlockButton = leftstick move it to S and L3, stay in config.ini, "
              "and the line names the one last used")

        # 5. Off.
        assert switch_off(lambda package: package == PACKAGE) == [PACKAGE]
        g.launch()
        assert not block()["enabled"]
        g.load(cast)
        target = g.out / "off.cycstate"
        fixture(cast, target, {SPRITE_Y + slot: HEAD_HEIGHT, SELECTED_SHIELD: SMALL})
        g.load(target)
        g.tcp("key", name="S", frames=200)
        hp = g.hp()
        g.until("the shot to be gone", lambda: g.ram(ENTITIES + slot) != HOOD_MAGIC, 200)
        g.wait(10)
        assert hp - g.hp() == MAGIC_DAMAGE and block()["magic_blocked"] == 0
        print("ok: turned off, holding the key stops nothing")
        print("screenshots:", ", ".join(p.name for p in g.shots))
    except BaseException:
        if g.running():
            g.shot("failure")
        raise
    finally:
        g.close()


if __name__ == "__main__":
    sys.exit(main())
