"""Play Seams lose nothing end to end in a hidden window.

    python3 tests/seams/seams_lose_nothing.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/seams

The game runs uncapped and the script waits on the game's own frames, never
on the clock, on a reserved TCP port.

Every part starts from the fixture eolis-zozuras (an external prerequisite,
not coverage, tests/e2e/fax_e2e.py): Eolis screen 4 as its two Zozuras crawl
in, the hero at its right facing left. Screen 5 is the room left of it, with
two Zozuras of its own, which Seamless runs on its copy of the machine
against a hero projected to its right edge, x 240 wherever he stands on 4.
Each case edits that save: the live game's RAM, and room 5's slots in
Seamless's record (faxanadu.world: a 32-byte header, then each room's
fax_world.c Room, its 2 KB of RAM first), filled as
Sprites_PopulateNextAvailableSprite fills a slot from a screen's list, from
the ROM's own tables.

1. coin: the hero on screen 4 at x 20, a coin worth 20 Golds on screen 5's
   floor at its x 236, by the projected hero. Before the fix the copy took it
   within two frames, and the gold was lost with the copy. Now, while room 5
   runs 120 frames, the coin stays and his gold does not move. Walking left
   into room 5, he takes it: his gold goes up by twice its value (Double
   gold), in the live game.
2. item: the Dragon Slayer on screen 5 at its x 240. Before the fix every
   tick of room 5 failed (Player_HandleTouchItem's script can't run in an
   isolated call), and its Zozuras stood still. Now over 120 frames its
   ticks go on, Seamless counts no failure, its Zozuras move, and the Dragon
   Slayer lies where it was.
3. ghost: the Long Sword swung from screen 4's x 4 facing left, a Zozura at
   its x 236, 232 pixels behind him; screen 5's slots emptied. The weapon is
   off the screen there, so its Y and reach are the last frame's on it, as
   a walk to x 4 leaves them: y+8 and the Long Sword's (16, 8). Through the
   whole swing the Zozura keeps its 9 HP and its hit counter stays 0, and
   no experience comes; seams_state counts the X tests it refused for
   matching only by wrapping round the screen.
4. original: the same swing with the remaster off, in the original's own
   view: its ghost hit kills the Zozura (one Long Sword hit is 10 without the
   Pendant), for its experience.

Screenshots: the coin waiting at screen 5's edge, the hero picking it up,
the Dragon Slayer and room 5's Zozuras, and the end of each swing.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
from fax_e2e import Game, act, batch, chunks, fixture, frame, hold, parse_args, ram, tcp, until, wait  # noqa: E402

SCREEN, HERO_X, HERO_Y, PLAYER_FLAGS = 0x63, 0x9E, 0xA1, 0xA4       # Area_CurrentScreen, Player_PosX_Block, ...
ATTACKING = 0x80                                                     # Player_Flags: a swing is on
WEAPON_Y, RANGE_X, RANGE_Y = 0xD0, 0xD2, 0xD3                       # Maybe_Something_WeaponPosY, Maybe_WeaponRange_X/Y
SPRITE_X, SPRITE_Y = 0xBA, 0xC2                                      # CurrentSprites_XPos_Full, _YPos: 8 slots each
ENTITIES, BEHAVIORS, FLAGS, PHASES = 0x2CC, 0x2D4, 0x2DC, 0x2E4      # CurrentSprites_*
HITBOX, HIT_BY_MAGIC, HP_, HIT_COUNTER = 0x304, 0x334, 0x344, 0x34C
BEHAVIOR_L, BEHAVIOR_U, VALUES = 0x354, 0x35C, 0x36C
EXPERIENCE, GOLD = 0x390, 0x392                                      # 2 bytes; 3 bytes
SELECTED_WEAPON, PLAYER_CUR_WEAPON, HP = 0x3BD, 0x3C8, 0x431
SPRITE_BSCRIPTS, SPRITE_ENTITIES_HITBOX_TYPES, SPRITE_ENTITIES_HP = 0xAD2D, 0xB4DF, 0xB5A9   # bank 14
COIN, ZOZURA, DRAGON_SLAYER, EMPTY = 0x02, 0x47, 0x5A, 0xFF
LONG_SWORD, LONG_SWORD_REACH = 1, (0x10, 0x08)                       # MAYBE_WEAPON_RANGES
ZOZURA_SCREEN, NEXT_SCREEN = 4, 5                                    # Eolis $04, and $05 left of it
ROOMS, HEADER = 83, 32                                               # fax_world.c's MAX_ROOMS, STATE_HEADER
COIN_VALUE = 20
WATCH = 120                                                          # frames room 5 runs with the coin or the item there
PACKAGE, FEATURE = "faxanadu.dx", "dx"                               # the one switch: off is the original game
PARTS = ("coin", "item", "ghost", "original")


def bank14(image, addr, n=1):
    start = 16 + 14 * 0x4000 + addr - 0x8000
    return image[start:start + n]


def hw_ram(data):
    """The save state's CPU RAM (its HW chunk, from offset 23)."""
    pos, size = next((p, n) for tag, p, n in chunks(data) if tag == b"HW  ")
    assert size == 2072, "Unqualified hardware ABI"
    return data[pos + 23:pos + 23 + 2048]


def room_ram(data, room):
    """Where room `room`'s RAM starts in Seamless's record, and the RAM. A Room
    is the record's size past its header over 83 rooms (10,296 bytes with this
    build's compiler)."""
    for tag, p, n in chunks(data):
        if tag == b"MOD " and bytes(data[p:p + 64]).rstrip(b"\0") == b"faxanadu.world":
            size, rest = divmod(n - 64 - HEADER, ROOMS)
            assert not rest and size > 2048, n
            base = HEADER + room * size
            return base, data[p + 64 + base:p + 64 + base + 2048]
    raise AssertionError("no Seamless record in the fixture")


def spawn(image, edits, base, slot, entity, x, y, value=EMPTY):
    """Fill a slot as Sprites_PopulateNextAvailableSprite does from a screen's
    list: its phase and flags cleared, no magic hit, the entity's hitbox, HP
    and behaviour script from the ROM, then its value; and no hit counter."""
    script = bank14(image, SPRITE_BSCRIPTS + entity * 2, 2)
    for addr, v in {ENTITIES: entity, SPRITE_X: x, SPRITE_Y: y, PHASES: 0, FLAGS: 0, HIT_BY_MAGIC: EMPTY,
                    HITBOX: bank14(image, SPRITE_ENTITIES_HITBOX_TYPES + entity)[0],
                    HP_: bank14(image, SPRITE_ENTITIES_HP + entity)[0], BEHAVIOR_L: script[0],
                    BEHAVIOR_U: script[1], BEHAVIORS: EMPTY, VALUES: value, HIT_COUNTER: 0}.items():
        edits[base + addr + slot] = v


def gold():
    g = ram(GOLD, 3)
    return g[0] | g[1] << 8 | g[2] << 16


def experience():
    e = ram(EXPERIENCE, 2)
    return e[0] | e[1] << 8


def main():
    args = parse_args(__doc__, parts=PARTS)
    game = Game.from_args(args)
    image, out = game.image, game.out
    zozura_hp = bank14(image, SPRITE_ENTITIES_HP + ZOZURA)[0]
    assert zozura_hp == 9, zozura_hp

    try:
        source = game.state("eolis-zozuras")
        data = source.read_bytes()
        live = hw_ram(data)
        base, room5 = room_ram(data, NEXT_SCREEN)
        assert live[SCREEN] == ZOZURA_SCREEN and list(live[ENTITIES + 6:ENTITIES + 8]) == [ZOZURA] * 2
        room5_zozuras = [s for s in range(8) if room5[ENTITIES + s] == ZOZURA]
        assert len(room5_zozuras) == 2, list(room5[ENTITIES:ENTITIES + 8])
        floor_y = room5[SPRITE_Y + room5_zozuras[0]]                  # a Zozura's y on screen 5's ground
        hero_y = live[HERO_Y]
        no_live_zozuras = {ENTITIES + 6: EMPTY, ENTITIES + 7: EMPTY}
        empty_room5 = {base + ENTITIES + s: EMPTY for s in range(8)}

        def start(case, writes, edits):
            """Launch (or keep) the game, and load the fixture with these edits."""
            path = out / f"{case}.cycstate"
            fixture(source, path, writes, {"faxanadu.world": edits})
            if not game.running():
                game.launch(at="eolis-zozuras")
            tcp("load_state", path=str(path))
            return path

        def room5_state():
            room, world, at = batch(("fax_world_room", dict(room=NEXT_SCREEN)), ("fax_world", {}), ("ping", {}))
            return room, world, at["frame"]

        if args.wants("coin"):
            edits = dict(empty_room5)
            spawn(image, edits, base, 0, COIN, 236, floor_y, COIN_VALUE)
            start("coin", {HERO_X: 20, **no_live_zozuras}, edits)
            assert tcp("seams_state")["enabled"], "Seams lose nothing is on with the remaster"
            room, world, at = room5_state()
            skipped, gold0, failures, ticks = tcp("seams_state")["skipped"], gold(), world["failures"], room["ticks"]
            assert room["entities"][0] == COIN and room["x"][0] == 236, room
            until("room 5 to run", lambda: room5_state()[2] >= at + WATCH, WATCH + 60)
            room, world, at2 = room5_state()
            assert room["ticks"] >= ticks + WATCH - 2, f"room 5 ran {room['ticks'] - ticks} ticks in {at2 - at} frames"
            assert room["entities"][0] == COIN and room["x"][0] == 236, f"the coin was taken in room 5's copy: {room}"
            assert gold() == gold0 and world["failures"] == failures, (gold(), gold0, world)
            assert tcp("seams_state")["skipped"] > skipped, "the copy's touches skipped"
            assert ram(SCREEN) == ZOZURA_SCREEN and ram(HERO_X) == 20
            game.shot("coin-waits")
            print(f"ok: a coin at room 5's x 236 stays {WATCH} frames by the projected hero, and his gold stays {gold0}")
            assert hold("left", lambda: gold() != gold0, 600), "the hero never took the coin"
            wait(2)
            assert ram(SCREEN) == NEXT_SCREEN, "taken in room 5, the live game's"
            assert gold() == gold0 + 2 * COIN_VALUE, (gold(), gold0)
            assert ram(ENTITIES) == EMPTY, "the coin's slot is empty"
            game.shot("coin-taken")
            print(f"ok: walking into room 5 he takes it: gold {gold0} -> {gold()} (twice {COIN_VALUE}, Double gold)")

        if args.wants("item"):
            edits = {}
            spawn(image, edits, base, 0, DRAGON_SLAYER, 240, floor_y)
            start("item", {HERO_X: 20, **no_live_zozuras}, edits)
            room, world, at = room5_state()
            ticks, failures = room["ticks"], world["failures"]
            xs = [room["x"][s] for s in room5_zozuras]
            until("room 5 to run", lambda: room5_state()[2] >= at + WATCH, WATCH + 60)
            room, world, at2 = room5_state()
            assert world["failures"] == failures, f"Seamless failed {world['failures'] - failures} ticks"
            assert room["ticks"] >= ticks + WATCH - 2, f"room 5 ran {room['ticks'] - ticks} ticks in {at2 - at} frames"
            assert [room["x"][s] for s in room5_zozuras] != xs, "room 5's Zozuras moved"
            assert room["entities"][0] == DRAGON_SLAYER and room["x"][0] == 240, room
            assert ram(PLAYER_CUR_WEAPON) != 3 and ram(SCREEN) == ZOZURA_SCREEN
            game.shot("item-waits")
            print(f"ok: with the Dragon Slayer at room 5's x 240, room 5 ran {room['ticks'] - ticks} ticks in "
                  f"{at2 - at} frames, no failure, its Zozuras moved ({xs} -> {[room['x'][s] for s in room5_zozuras]})")

        def swing(name):
            """Swing the Long Sword from x 4 facing left, a Zozura at x 236 in
            slot 7, 232 pixels behind him, facing him (with Seams, slice 1,
            one walking right would go on through the seam into room 3).
            Returns the Zozura's entity, HP and hit counter at each read until
            the swing ended, and the experience before and after."""
            writes = {HERO_X: 4, PLAYER_FLAGS: 0, PLAYER_CUR_WEAPON: LONG_SWORD, SELECTED_WEAPON: LONG_SWORD,
                      HP: 0x50, WEAPON_Y: hero_y + 8, RANGE_X: LONG_SWORD_REACH[0], RANGE_Y: LONG_SWORD_REACH[1],
                      ENTITIES + 6: EMPTY, SPRITE_X + 7: 236, FLAGS + 7: live[FLAGS + 7] & 0xFE}   # facing left
            path = out / f"{name}.cycstate"
            fixture(source, path, writes, {"faxanadu.world": empty_room5})
            exp0 = None
            for _ in range(3):                       # a press in a loaded state's first frames can go unseen
                batch(("load_state", dict(path=str(path))), ("action", dict(name="b", frames=2)))
                if exp0 is None:
                    exp0 = experience()
                seen, swung, end = [], False, frame() + 40   # a swing is 19 frames (GAME_INTERNALS, "Weapon reach")
                while frame() < end:
                    state = batch(*(("read_ram", dict(addr=a, len=1))
                                    for a in (PLAYER_FLAGS, ENTITIES + 7, HP_ + 7, HIT_COUNTER + 7, SPRITE_X + 7)))
                    flags, entity, hp, counter, x = (int(r["hex"], 16) for r in state)
                    seen.append((entity, hp, counter, x))
                    if flags & ATTACKING:
                        swung = True
                    elif swung:
                        break
                if swung:
                    break
            else:
                raise AssertionError("the hero never swung")
            assert ram(SCREEN) == ZOZURA_SCREEN and ram(HERO_X) == 4
            wait(4)
            game.shot(name)
            return seen, exp0, experience()

        if args.wants("ghost"):
            if not game.running():
                game.launch(at="eolis-zozuras")
            before = tcp("seams_state")["refused"]
            seen, exp0, exp1 = swing("ghost-spared")
            state = tcp("seams_state")
            assert all(e == ZOZURA and hp == zozura_hp and c == 0 for e, hp, c, _ in seen), seen
            assert exp1 == exp0, (exp0, exp1)
            assert state["refused"] > before, (state, seen)
            assert state["refused"] > before, state
            assert state["last_weapon_x"] in (4 - 16, 4 - 24) and state["last_entity"] == ZOZURA, state
            assert 220 <= state["last_sprite_x"] <= 240, state
            print(f"ok: a swing at x 4 leaves the Zozura at x {seen[0][3]}-{seen[-1][3]} alone (HP {zozura_hp}, no hit, "
                  f"experience {exp1}); {state['refused'] - before} wrapped matches refused, the last with the "
                  f"weapon at x {state['last_weapon_x']}")

        if args.wants("original"):
            game.close()
            game.set_features({(PACKAGE, FEATURE): False})
            game.launch()
            assert not tcp("seams_state")["enabled"], "the remaster is off"
            seen, exp0, exp1 = swing("original-ghost-hit")
            died = any(e != ZOZURA or hp > zozura_hp for e, hp, _, _ in seen) or ram(ENTITIES + 7) != ZOZURA
            assert died, f"the original's ghost hit kills the Zozura: {seen}"
            assert exp1 > exp0, (exp0, exp1)
            assert tcp("seams_state")["refused"] == 0
            print(f"ok: with the remaster off, the original's swing at x 4 kills the Zozura 232 pixels behind him, "
                  f"for {exp1 - exp0} experience")
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
