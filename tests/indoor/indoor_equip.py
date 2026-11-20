"""Play Equip indoors end to end in a hidden window and screenshot it.

    python3 tests/indoor/indoor_equip.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/indoor

The game runs uncapped (about 5x real time; --realtime to watch it at normal
speed) on a port tests/e2e/fax_e2e.py reserves, and the script waits on the
game's own frames and state, never on the clock. Anything that depends on the
frame a button goes down (a swing) runs headless from a save state instead,
with a button schedule and every frame's RAM.

Copies the build to OUT, starts a new game over TCP, walks through Eolis's gate
into the meat shop and saves a state there. Save-state RAM edits then set up
what each scenario needs (external prerequisites, not coverage): where the
shop's door leads (Area_Saved*, as tests/shops/quick_shops.py does), the gold,
and the weapon in hand.

1. Forepaw's tool shop, with the Hand Dagger in hand and 1700 Golds: he keeps
   the dagger in hand coming in. Quick shops sells him the Long Sword; Select
   opens the menu, A on Weapons lists the sword, and A equips it: he holds the
   Long Sword and the dagger goes back in the list.
2. A swing at the Tool Seller across the counter, headless: the hero swings,
   the sword is drawn every frame it can hit, its box overlaps the seller's by
   the game's own test, and the seller's slot, HP and hit counter never change.
   Then Up still opens his list, and Select's sell list (Quick shops) offers
   the Hand Dagger the sword replaced, in the same visit.
3. Eolis's temple: the same swing at the Guru, then Up runs his script to the
   end (the Ring of Elf, the first time).
4. The room behind the door on the Mist's screen 6 (building $1D), one of the
   few buildings with an enemy in it (Sir Gawaine): the sword now hurts him.
   With the remaster off, and the weapon out of hand as the original leaves
   it, the same swing does nothing.
5. A death in the meat shop respawns at Eolis's Guru with the sword in hand.
6. With the remaster off, the original: carrying the Long Sword into the shop
   with the dagger in hand, he holds nothing, and the menu refuses the sword
   (the menu stays up, the dagger still selected).
"""
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
from fax_e2e import (Game, act, features_toml, fixture, frame, parse_args, ram, tcp, typed,  # noqa: E402
                     until, wait)

AREA, SCREEN, PLAYER_X, PLAYER_Y = 0x24, 0x63, 0x9E, 0xA1
PLAYER_FLAGS, ATTACK_PHASE = 0xA4, 0xAE                             # Player_Flags, Player_AttackPhaseAnimFrame
WEAPON_X, WEAPON_Y, RANGE_X, RANGE_Y = 0xCE, 0xD0, 0xD2, 0xD3       # Player_DrawWeapon's box
ATTACKING, FACING_RIGHT = 0x80, 0x40
TEXTBOX_X, TEXTBOX_CLOSED, MESSAGE_ID, DATA_ARRAY = 0x208, 0x20C, 0x213, 0x220
ENTITIES, SPRITE_X, SPRITE_Y, SPRITE_HP, HIT_COUNTER = 0x2CC, 0xBA, 0xC2, 0x344, 0x34C
GOLD, WEAPONS, NUMBER_OF_WEAPONS = 0x392, 0x39D, 0x3C2
SELECTED_WEAPON, SELECTED_ITEM, PLAYER_CUR_WEAPON, PORTRAIT = 0x3BD, 0x3C1, 0x3C8, 0x3C7
AREA_SAVED_AREA, AREA_SAVED_SCREEN, SCREEN_SAVED_PALETTE, PLAYER_SAVED_POS = 0x3D5, 0x3D6, 0x3D7, 0x3D8
DURATION_HOUR_GLASS, SPECIAL_ITEMS = 0x42A, 0x42C
HP, PLAYER_IS_DEAD, TEMPLE_SPAWN_POINT = 0x431, 0x438, 0x439
SHADOW_OAM = 0x700
EOLIS, MIST, TOWNS, BUILDINGS = 0, 2, 3, 4
PALETTE_EOLIS, PALETTE_MIST, PALETTE_TOWN = 0x00, 0x0A, 0x1B
HAND_DAGGER, LONG_SWORD, NONE = 0x00, 0x01, 0xFF
TOOL_SELLER, PRIEST, SIR_GAWAINE = 0x37, 0x39, 0x1F                 # sprite entities
GURU, NO_PORTRAIT = 1, 0xFF
RING_OF_ELF, NEGATIVE_THOUGHTS = 0x80, 0x24
WEAPON_TILES = range(0x38, 0x40)                  # PLAYER_WEAPON_TILE_PPU_ADDRS: $0380 on, but the Dragon Slayer's
MENU_WIDTH, WEAPONS_WIDTH = 0x0A, 0x14            # PlayerMenu_Show's box, PlayerMenu_ShowSubmenu's
SPRITE_EXTENTS = 0xB407                           # MAYBE_SPRITE_EXTENTS, bank 14: (EX, EY) by entity
PACKAGE = ("faxanadu.remastered", "remastered")                  # the one switch: off is the original game
ROM = b""


def indoor():
    return tcp("indoor_equip_state")


def weapons():
    return list(ram(WEAPONS, 4)[:ram(NUMBER_OF_WEAPONS)])


def gold():
    lo, mid, hi = ram(GOLD, 3)
    return lo | mid << 8 | hi << 16


def extents(entity):
    base = 16 + 14 * 0x4000 + SPRITE_EXTENTS - 0x8000 + entity * 2
    return ROM[base], ROM[base + 1]


def carrying(gold_amount, weapon, weapon_list=()):
    """RAM writes for a hero with this gold, this weapon in hand and these in his list, at full HP."""
    w = {GOLD: gold_amount & 0xFF, GOLD + 1: gold_amount >> 8 & 0xFF, GOLD + 2: gold_amount >> 16,
         SELECTED_WEAPON: weapon, PLAYER_CUR_WEAPON: weapon, NUMBER_OF_WEAPONS: len(weapon_list), HP: 0x50}
    w.update({WEAPONS + i: v for i, v in enumerate(weapon_list)})
    return w


def slot_of(mem, entity):
    """The sprite slot holding `entity`, in a RAM image."""
    slots = [i for i in range(8) if mem[ENTITIES + i] == entity]
    assert slots, f"no sprite {entity:#04x} on screen: {list(mem[ENTITIES:ENTITIES + 8])}"
    return slots[0]


def weapon_drawn(mem):
    """The weapon's sprites are in this frame's shadow OAM, by the hero."""
    hx, hy = mem[PLAYER_X], mem[PLAYER_Y]
    for i in range(4, 256, 4):                      # sprite 0 is the status bar's
        y, tile, _, x = mem[SHADOW_OAM + i:SHADOW_OAM + i + 4]
        if y < 0xEF and tile in WEAPON_TILES and abs(x - hx) < 40 and 0 <= y - 32 - hy < 32:
            return True
    return False


def in_reach(mem, slot):
    """Sprites_IsSpriteOutOfWeaponRange's test, in 8-bit arithmetic: the weapon's
    box this frame overlaps the sprite in `slot` (see docs, Weapon reach)."""
    ex, ey = extents(mem[ENTITIES + slot])
    wx, wy, rx, ry = mem[WEAPON_X], mem[WEAPON_Y], mem[RANGE_X], mem[RANGE_Y]
    return ((wx + rx - mem[SPRITE_X + slot]) & 0xFF) < ex + rx and ((wy + ry - mem[SPRITE_Y + slot]) & 0xFF) < ey + ry


def main():
    global ROM
    args = parse_args(__doc__)
    game = Game.from_args(args)
    out, exe = game.out, game.exe
    ROM = game.image
    extra_args = [part for arg in game.game_args for part in arg.split("=", 1)]

    def launch(on=True):
        game.set_features({PACKAGE: on})
        game.launch()
        assert indoor()["enabled"] == on, indoor()

    def mods(on):
        """A mod catalog for headless runs: the build's packages, this feature on or off."""
        root = out / ("mods-on" if on else "mods-off")
        if not root.exists():
            shutil.copytree(out / "game" / "mods" / "packages", root / "packages")
            (root / "state.toml").write_text(features_toml({PACKAGE: on}))
        return root

    def headless(state, schedule, frames, on=True, screenshot=None):
        """Run from `state` for `frames` frames, holding buttons on a schedule of
        (frame, buttons) pairs counted from the state's frame. Returns each
        frame's CPU RAM, after the frame."""
        # With the game's options: a Seamless state's world record loads only with Seamless on.
        probe = subprocess.run([str(exe), str(game.rom), "--load-state", str(state), "--frames", "0",
                                "--mods-root", str(mods(on)), "--no-save", *extra_args], capture_output=True, text=True)
        found = re.search(r"continuing at frame (\d+)", probe.stdout)
        assert found, probe.stdout + probe.stderr
        start = int(found.group(1))
        run = out / "run"
        run.mkdir(exist_ok=True)
        (run / "input.txt").write_text("".join(f"{start + at} {buttons}\n" for at, buttons in sorted(schedule)))
        frame_log = run / "frames.bin"
        cmd = [str(exe), str(game.rom), "--load-state", str(state), "--input", str(run / "input.txt"),
               "--frames", str(start + frames), "--frame-log", str(frame_log),
               "--frame-log-frames", f"{start}:{start + frames - 1}", "--mods-root", str(mods(on)), "--no-save",
               *extra_args]
        if screenshot:
            cmd += ["--screenshot", str(screenshot)]
            game.shots.append(screenshot.name)
        done = subprocess.run(cmd, capture_output=True, text=True)
        assert done.returncode == 0, done.stdout + done.stderr
        data = frame_log.read_bytes()
        frame_log.unlink()
        assert data[:8] == b"CYCFRAME"
        rams, pos = [], 12
        while pos < len(data):
            head = struct.unpack_from("<9I", data, pos)
            pos += 36 + 32 + 256
            rams.append(data[pos:pos + head[3]])
            pos += head[3] + sum(head[4:9])
        assert len(rams) == frames, (len(rams), frames)
        return rams

    def swing(state, name, entity, on=True):
        """B from `state`, headless: every frame of the swing, and a picture in
        its second hit phase. Returns the frames' RAM and the slot of `entity`."""
        rams = headless(state, [(2, "B"), (5, "-")], 40, on)
        slot = slot_of(rams[0], entity)
        hits = [i for i, m in enumerate(rams) if m[PLAYER_FLAGS] & ATTACKING and m[ATTACK_PHASE] == 2]
        assert hits, "no swing"
        headless(state, [(2, "B"), (5, "-")], hits[0] + 3, on, screenshot=out / f"{len(game.shots):02d}-{name}.png")
        return rams, slot

    def walk_to(x_min, x_max):
        """Walk to x_min-x_max, paging through the intro's monologue outside
        Eolis's gate. (TextBox_Closed reads 0 in the towns with no box open.)"""
        for _ in range(600):
            if (ram(AREA), ram(SCREEN)) == (EOLIS, 0) and ram(TEXTBOX_CLOSED) == 0:
                if typed():
                    act("a", 6, 14)
                else:
                    wait(4)
                continue
            x = ram(PLAYER_X)
            if x_min <= x <= x_max:
                return
            act("right" if x < x_min else "left", 2, 0)
        raise AssertionError(f"could not reach x {x_min}-{x_max}")

    def walk_out():
        for _ in range(300):
            if ram(AREA) != BUILDINGS:
                return
            act("right", 2, 0)
        raise AssertionError("could not walk out")

    def go_in(source, name, area, screen, palette, door, writes):
        """From inside a building, walk out onto the door `door` (block YX) on
        `screen` of `area`, save a state there, and go in."""
        target = out / f"{name}.cycstate"
        fixture(source, target, {AREA_SAVED_AREA: area, AREA_SAVED_SCREEN: screen, SCREEN_SAVED_PALETTE: palette,
                                 PLAYER_SAVED_POS: door, **writes})
        tcp("load_state", path=str(target))
        walk_out()
        until(f"area {area} screen {screen}", lambda: (ram(AREA), ram(SCREEN)) == (area, screen))
        wait(30)
        column = (door & 15) * 16
        walk_to(column - 6, column + 4)
        door_state = out / f"{name}-door.cycstate"
        tcp("save_state", path=str(door_state))
        enter()
        return door_state

    def enter():
        act("up", 4, 10)
        until("the building", lambda: ram(AREA) == BUILDINGS)
        wait(40)

    def to_counter(stop=0):
        """Walk left until stopped, or to x `stop`."""
        for _ in range(60):
            before = ram(PLAYER_X)
            if before <= stop:
                return
            act("left", 8, 2)
            if ram(PLAYER_X) == before:
                return
        raise AssertionError("no counter")

    def converse():
        """A and B through a conversation to its end."""
        for _ in range(200):
            if ram(PORTRAIT) == NO_PORTRAIT and ram(TEXTBOX_CLOSED) != 0:
                return
            if typed():
                act("a", 6, 14)
            else:
                wait(4)
        raise AssertionError("the conversation did not end")

    def menu_to_weapons(name="menu-indoors"):
        """Select, then A on Weapons: the weapons list, waiting for a button."""
        act("select", 4, 6)
        until("the menu", lambda: ram(TEXTBOX_CLOSED) == 0 and ram(TEXTBOX_X + 2) == MENU_WIDTH, 300)
        wait(30)
        menu = game.shot(name)
        act("a", 4, 6)
        until("the weapons list", lambda: ram(TEXTBOX_X + 2) == WEAPONS_WIDTH, 300)
        wait(30)
        return menu

    def list_up(mode):
        """Quick shops' `mode` list on screen and waiting for a button: presses
        while it draws are lost, as in the original."""
        shops = lambda: tcp("shops_state")
        until(f"the {mode} list", lambda: (lambda s: s["shown"] == mode and s["list_up"])(shops()), 900)
        waits = shops()["waits"]
        until("the list to wait for a button", lambda: shops()["waits"] > waits, 300)
        wait(2)
        return shops()

    def leave_shop():
        act("b", 4, 6)
        until("the conversation's end", lambda: ram(PORTRAIT) == NO_PORTRAIT and ram(TEXTBOX_CLOSED) != 0, 300)
        wait(20)

    def buy_long_sword():
        """Up at the Tool Seller (Quick shops: straight to his list), A on the
        Long Sword, its top entry, and B."""
        weapons_before = weapons()
        act("up", 4, 6)
        state = list_up("buy")
        assert state["list"][0] == LONG_SWORD, state
        act("a", 4, 6)
        until("the purchase", lambda: tcp("shops_state")["bought"] == state["bought"] + 1, 1200)
        list_up("buy")
        leave_shop()
        assert weapons() == weapons_before + [LONG_SWORD], weapons()

    try:
        launch()
        until("the title", lambda: frame() > 700)
        tcp("pad", buttons="start", frames=10)
        until("the Eolis gate", lambda: ram(AREA) == EOLIS and frame() > 1900)
        walk_to(186, 196)                                   # the gate, column 12
        act("up", 4, 10)
        until("Eolis", lambda: (ram(AREA), ram(SCREEN)) == (EOLIS, 1))
        wait(30)
        walk_to(41, 56)                                     # the meat shop, column 3
        enter()
        start = out / "eolis-meat-shop.cycstate"
        tcp("save_state", path=str(start))

        # 1. Forepaw's tool shop (building $0F: the towns' screen 2, block $9D).
        entered = indoor()["entered"]
        forepaw = go_in(start, "forepaw-tools", TOWNS, 2, PALETTE_TOWN, 0x9D, carrying(1700, HAND_DAGGER))
        state = indoor()
        assert state["entered"] == entered + 1 and state["weapon_in_hand"] == HAND_DAGGER, state
        to_counter()
        assert not ram(PLAYER_FLAGS) & FACING_RIGHT
        print("ok: the hero comes into Forepaw's tool shop with the Hand Dagger in hand")
        buy_long_sword()
        assert gold() == 100, gold()
        menu_to_weapons()
        assert ram(DATA_ARRAY) == LONG_SWORD, ram(DATA_ARRAY, 4)          # the weapons listed
        game.shot("weapons-indoors")
        act("a", 4, 6)
        until("the menu to close", lambda: ram(TEXTBOX_CLOSED) != 0, 300)
        wait(20)
        state = indoor()
        assert ram(SELECTED_WEAPON) == LONG_SWORD and state["weapon_in_hand"] == LONG_SWORD, state
        assert weapons() == [HAND_DAGGER] and state["menu_choices"] == 1 and state["weapons_set"] == 1, state
        frame_ram = ram(0, 0x800)
        assert weapon_drawn(frame_ram), "no weapon by the hero"
        game.shot("long-sword-in-forepaws-shop")
        counter = out / "counter.cycstate"
        tcp("save_state", path=str(counter))
        print("ok: bought the Long Sword, and the menu equips it in the shop: he holds it, the dagger is listed")

        # 2. A swing at the Tool Seller, across the counter.
        rams, slot = swing(counter, "swing-at-the-tool-seller", TOOL_SELLER)
        first = rams[0]
        live = [m for m in rams if m[PLAYER_FLAGS] & ATTACKING and m[ATTACK_PHASE] in (1, 2)]
        assert live and all(weapon_drawn(m) for m in live), "the sword is not drawn through the swing"
        assert any(in_reach(m, slot) for m in live), "the Tool Seller is out of the sword's reach"
        for m in rams:
            assert m[ENTITIES + slot] == TOOL_SELLER and m[SPRITE_HP + slot] == first[SPRITE_HP + slot], "seller hurt"
            assert m[HIT_COUNTER + slot] == 0 and m[HP] == first[HP], "a hit"
        tcp("load_state", path=str(counter))
        wait(5)
        act("b", 4, 30)
        act("up", 4, 6)
        list_up("buy")
        act("select", 4, 6)
        state = list_up("sell")
        assert state["list"] == [HAND_DAGGER] and state["prices"] == [200], state       # half of Eolis's 400
        leave_shop()
        print("ok: the Long Sword swings through the Tool Seller (in its reach) and does nothing; he still "
              "trades, and buys the dagger the sword just replaced")

        # 3. Eolis's temple (building 2: screen 2, block $95), at the Guru.
        go_in(start, "eolis-temple", EOLIS, 2, PALETTE_EOLIS, 0x95, carrying(0, LONG_SWORD))
        assert indoor()["weapon_in_hand"] == LONG_SWORD, indoor()
        to_counter(stop=60)
        guru = out / "guru.cycstate"
        tcp("save_state", path=str(guru))
        rams, slot = swing(guru, "swing-at-the-guru", PRIEST)
        live = [m for m in rams if m[PLAYER_FLAGS] & ATTACKING and m[ATTACK_PHASE] in (1, 2)]
        assert live and all(weapon_drawn(m) for m in live), "the sword is not drawn through the swing"
        assert any(in_reach(m, slot) for m in live), "the Guru is out of the sword's reach"
        for m in rams:
            assert m[ENTITIES + slot] == PRIEST and m[HIT_COUNTER + slot] == 0, "the Guru hit"
        tcp("load_state", path=str(guru))
        wait(5)
        act("b", 4, 30)
        assert not ram(SPECIAL_ITEMS) & RING_OF_ELF
        act("up", 4, 6)
        until("the Guru's portrait", lambda: ram(PORTRAIT) == GURU, 600)
        converse()
        assert ram(SPECIAL_ITEMS) & RING_OF_ELF, "no Ring of Elf"
        print("ok: a swing at Eolis's Guru does nothing, and he talks and gives the Ring of Elf as before")

        # 4. The Mist's screen 6, building $1D: Sir Gawaine.
        go_in(start, "mist-room", MIST, 6, PALETTE_MIST, 0x93, carrying(0, LONG_SWORD))
        assert indoor()["weapon_in_hand"] == LONG_SWORD, indoor()
        room = out / "sir-gawaine.cycstate"
        tcp("save_state", path=str(room))
        mem = ram(0, 0x800)
        slot = slot_of(mem, SIR_GAWAINE)
        hx, hy = mem[PLAYER_X], mem[PLAYER_Y]
        # In reach in front of him (he faces left), not touching, and held by the Hour Glass.
        near = {SPRITE_X + slot: hx - 24, SPRITE_Y + slot: hy, DURATION_HOUR_GLASS: 0x10, HIT_COUNTER + slot: 0}
        fixture(room, out / "sir-gawaine-near.cycstate", near)
        rams, _ = swing(out / "sir-gawaine-near.cycstate", "sword-hits-sir-gawaine", SIR_GAWAINE)
        hp = rams[0][SPRITE_HP + slot]
        assert min(m[SPRITE_HP + slot] for m in rams) < hp, "the sword did not hurt Sir Gawaine"
        assert any(m[HIT_COUNTER + slot] for m in rams), "no hit"
        fixture(room, out / "sir-gawaine-stock.cycstate", {**near, PLAYER_CUR_WEAPON: NONE})
        rams = headless(out / "sir-gawaine-stock.cycstate", [(2, "B"), (5, "-")], 40, on=False)
        assert any(m[PLAYER_FLAGS] & ATTACKING for m in rams), "no swing"
        assert all(m[SPRITE_HP + slot] == hp and not m[HIT_COUNTER + slot] for m in rams), "a hit in the original"
        assert not any(weapon_drawn(m) for m in rams), "a weapon drawn in the original"
        print(f"ok: in the Mist's room $1D the Long Sword hurts Sir Gawaine ({hp} HP); in the original, "
              "with no weapon in hand indoors, the same swing passes through him")

        # 5. A death in the meat shop: back at Eolis's Guru, the sword in hand.
        dying = out / "dying.cycstate"
        fixture(start, dying, {**carrying(0, LONG_SWORD), PLAYER_IS_DEAD: 1, TEMPLE_SPAWN_POINT: 0})
        tcp("load_state", path=str(dying))
        until("the death message", lambda: ram(MESSAGE_ID) == NEGATIVE_THOUGHTS, 600)
        for _ in range(60):
            if ram(AREA) == BUILDINGS and ram(SCREEN) == 2 and not ram(PLAYER_IS_DEAD) and ram(TEXTBOX_CLOSED):
                break
            if typed():
                act("a", 6, 14)
            else:
                wait(10)
        wait(30)
        assert (ram(AREA), ram(SCREEN)) == (BUILDINGS, 2), (ram(AREA), ram(SCREEN))
        assert indoor()["weapon_in_hand"] == LONG_SWORD and weapon_drawn(ram(0, 0x800)), indoor()
        game.shot("respawned-with-the-sword")
        print("ok: a death respawns at Eolis's Guru with the Long Sword in hand")
        game.close()

        # 6. The remaster off: the original. (Quick shops is off with it, so he
        #    brings the Long Sword rather than buying it.)
        launch(on=False)
        carried = out / "forepaw-tools-door-off.cycstate"
        fixture(forepaw, carried, {WEAPONS: LONG_SWORD, NUMBER_OF_WEAPONS: 1})
        tcp("load_state", path=str(carried))
        enter()
        assert ram(PLAYER_CUR_WEAPON) == NONE and ram(SELECTED_WEAPON) == HAND_DAGGER
        assert not weapon_drawn(ram(0, 0x800)), "a weapon drawn in the original"
        menu_to_weapons("off-menu-indoors")
        act("a", 4, 30)
        assert ram(TEXTBOX_CLOSED) == 0 and ram(TEXTBOX_X + 2) == WEAPONS_WIDTH, "the menu closed"
        assert ram(SELECTED_WEAPON) == HAND_DAGGER and ram(PLAYER_CUR_WEAPON) == NONE
        game.shot("off-the-menu-refuses")
        act("b", 4, 6)
        until("the menu to close", lambda: ram(TEXTBOX_CLOSED) != 0, 300)
        state = indoor()
        assert state["entered"] == 0 and state["menu_choices"] == 0 and state["weapons_set"] == 0, state
        print("ok: with the remaster off, the hero comes in empty-handed and the menu refuses the Long Sword")
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
