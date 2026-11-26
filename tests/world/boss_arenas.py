"""Boss screens are arenas in Seamless, end to end in a hidden window (#110).

    python3 tests/world/boss_arenas.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/arenas
    python3 tests/world/boss_arenas.py ... --part trunk-15

A part for each boss's screen (SCREEN_EVENT_BOSS in a screen's sprite list,
docs/GAME_INTERNALS.md), and one for the final boss's, all in the remaster,
so in Seamless. Each boss's screen has a neighbour the scroll table joins it
to both ways, which Seamless would lay beside it. Most parts walk out of
Eolis's meat shop onto that neighbour (the shop's door edited to lead there,
save-state RAM, an external prerequisite, as tests/zones/zone_names.py does),
with its enemies sent away and the hero's HP full, next to the edge the two
share:

1. Seamless lays the neighbour out with the rooms around it, but not the
   boss's: that room is on no map (fax_world_room's owned), and the
   neighbour's edge toward it joins nothing.
2. The hero walks across. The original's own scroll takes him in (a portal,
   no Seamless handoff), and Seamless shows the boss's room alone, an arena
   (fax_world's owned 1 and arena). The game's boss handler
   (ScreenEvents_HandleBoss) has run: CurrentScreen_SpecialEventID has its
   bit 7 and the boss music ($0A) plays, and the boss is there.
3. The boss is killed: its slot emptied, as its death leaves it (pokes, not
   a fight). The game's handler clears the event and puts the area's music
   back, and Seamless joins the room to the world again: the neighbour is on
   its map, at the edge he came in by.
4. He walks back out: a Seamless handoff, no scroll.

Throughout, until the boss is dead, nothing comes out of its room or goes in
(Seams, fax_seams.c, #166): no mover's move from or to it, and no visitor from
it in the live game's sprite loop (seams_state's log and visit_log).

dartmoor-15: King Grieve's room is walled on every side: the hero comes in
by a door (the shop's, edited to lead where the castle's own on $14 does),
its neighbour $16 (another King Grieve) is not on the map while he lives,
and joins once he is dead, beyond the wall. Then he goes out by the room's
own door to $14 and back in by $14's, both the original's loads in the same
area: the load brings King Grieve back, though Seamless's copy of the room
had none, and the room is an arena again.

zenis-00: the final boss, Shadow Eura, behind a door on Zenis $01. The hero
walks out of the shop into the room itself, the door's own load: Seamless
shows it alone. With every sprite slot emptied, the game's own handler
(ScreenEvents_HandleFinalBossKilled) begins the ending, and the hero comes
to the King's room in Eolis, the ending's first scene.

Screenshots of each arena alone, joined again, and the way in, go in OUT.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "block"))
from harness import AREA, BUILDINGS, ENTITIES, HP, POS_X, POS_Y, SCREEN, Game, parse_args  # noqa: E402
import fax_e2e  # noqa: E402  (on the path through harness)

EVENT, MUSIC, DEFAULT_MUSIC = 0x42E, 0xFA, 0x3D1           # CurrentScreen_SpecialEventID, Music_Current, Areas_DefaultMusic
# Music_Current's bit 7 says the sound engine has started it.
BOSS_MUSIC = 0x0A
BLOCK_PROPERTIES, SCREEN_BUFFER = 0x43C, 0x600             # BlockProperties (two a byte), ScreenBuffer (16x13 blocks)
LEFT_COLUMN, RIGHT_COLUMN = 0x3E6, 0x3F6                   # LastColumnLeftScreen, FirstColumnInRightScreen
IMPASSABLE = (0, 1, 0, 0, 0, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0)   # BLOCK_PROPERTY_IMPASSABLE_MAP
CATEGORIES = 16 + 14 * 16384 + (0xB544 - 0x8000)           # SPRITE_CATEGORIES_BY_ENTITY, bank 14, in the ROM image
BOSS = 7                                                   # a boss's category
TRUNK, MIST, BRANCH, DARTMOOR, ZENIS = 1, 2, 5, 6, 7
LEFT, RIGHT = 0, 1                                         # Seamless's edges: left, right, up, down
# A part: the boss's (area, screen), its boss, and the neighbour the hero
# comes in from (its screen, the side of it the boss's room is on, its
# palette and region, as the door into its tower, castle or region leaves
# them: docs/GAME_INTERNALS.md, "The way into each tower, castle and region").
# The boss's room's own place for a door (Player_SavedPos), where the hero
# comes in by one instead.
DOORS = {"dartmoor-15": 0x91}                               # where $14's door (DARTMOOR_DOOR_LOCATIONS) brings him
ARENAS = {
    "trunk-15": (TRUNK, 0x15, 0x2D, 0x14, RIGHT, 0x07, 1),       # Ripasheiku, the Tower of Trunk's top
    "trunk-3b": (TRUNK, 0x3B, 0x31, 0x3A, RIGHT, 0x07, 1),       # Zorugeriru, the Tower of Fortress
    "mist-34": (MIST, 0x34, 0x2D, 0x33, RIGHT, 0x0B, 2),         # Ripasheiku, the Tower of Suffer
    "mist-4c": (MIST, 0x4C, 0x2E, 0x4B, RIGHT, 0x0B, 2),         # Zoradohna, the Tower of Mist
    "branch-07": (BRANCH, 0x07, 0x2E, 0x08, LEFT, 0x08, 3),      # Zoradohna
    "branch-1d": (BRANCH, 0x1D, 0x2E, 0x1C, RIGHT, 0x08, 3),     # Zoradohna
    "dartmoor-15": (DARTMOOR, 0x15, 0x32, 0x16, LEFT, 0x0E, 4),  # King Grieve, the Castle of Fraternal (its door's palette)
}
DOOR_OUT, DOOR_X = 0x14, 0xE0                              # where King Grieve's room's door leads; $14's door, block $9E
SHADOW_EURA = 0x33
FINAL = (ZENIS, 0x00, 0x0F, 0x91, 5)                       # Zenis $00 from the door on $01: palette, position, region
PARTS = tuple(ARENAS) + ("zenis-00",)


def main():
    args = parse_args(__doc__, parts=PARTS)
    g = Game.from_args(args)
    shop = g.state("eolis-meat-shop")

    def world():
        return g.tcp("fax_world")

    def room(n):
        return g.tcp("fax_world_room", room=n)

    def bosses():
        """The slots holding a boss (category 7)."""
        return [i for i, e in enumerate(g.ram(ENTITIES, 8)) if e != 0xFF and g.image[CATEGORIES + e] == BOSS]

    def standing_spot(side):
        """A block (column, row) on the edge toward `side` where the hero can
        stand and walk across: two passable blocks over a solid one on this
        side, and the two beside them passable on the other (the edge column
        the game keeps of the next screen)."""
        blocks, props = g.ram(SCREEN_BUFFER, 208), g.ram(BLOCK_PROPERTIES, 128)
        other = g.ram(RIGHT_COLUMN if side == RIGHT else LEFT_COLUMN, 16)

        def solid(block):
            return IMPASSABLE[props[block >> 1] >> (block & 1) * 4 & 15]
        column = 15 if side == RIGHT else 0
        for row in range(11, 0, -1):
            here = [blocks[r * 16 + column] for r in (row - 1, row, row + 1)]
            if not solid(here[0]) and not solid(here[1]) and solid(here[2]) and \
                    not solid(other[row - 1]) and not solid(other[row]):
                return column, row - 1
        raise AssertionError(f"no way across the {'right' if side == RIGHT else 'left'} edge: "
                             f"{[hex(b) for b in blocks]} beside {[hex(b) for b in other]}")

    def walk(direction, there, what, frames=900):
        fax_e2e.hold(direction, there, frames)
        g.until(what, lambda: there() and g.settled(), 300)

    def sealed(screen):
        """No move from or to the boss's room, and no visitor from it, in
        Seams' logs (their last 16 each)."""
        s = g.tcp("seams_state")
        moves = [m for m in s["log"] if screen in (m[0], m[2])]
        visits = [v for v in s["visit_log"] if v[0] == screen]
        assert not moves and not visits, (screen, moves, visits)

    try:
        g.launch(at="eolis-meat-shop")
        for part, (area, boss_screen, boss, screen, side, palette, region) in ARENAS.items():
            if not args.wants(part):
                continue
            door = DOORS.get(part)
            if door is None:
                # 1. Beside the boss's room: Seamless has not joined it on.
                g.warp(shop, f"{part}-beside", area, screen, palette, 0x21 if side == LEFT else 0x2E, region)
                g.poke({**{ENTITIES + i: 0xFF for i in range(8)}, HP: 0x50})
                g.until("the hero to land", g.settled, 300)
                column, row = standing_spot(side)
                g.poke({POS_X: column * 16, POS_Y: row * 16})
                g.until("the hero to land", g.settled, 300)
                here = room(screen)
                assert not room(boss_screen)["owned"] and here["owned"] and here["edges"][side] == 255, (here,)
                assert room(boss_screen)["event"] == 1, room(boss_screen)
                g.wait(30)                                          # its edge, a while: nothing comes out of it
                sealed(boss_screen)
                g.shot(f"{part}-beside")
                before = world()
                print(f"ok: {part}: beside the boss's room ({area}, {boss_screen:#04x}), Seamless joins it to nothing "
                      f"({before['owned']} rooms laid out around {screen:#04x}, its edge toward it empty)")

                # 2. In, by the original's own scroll: the arena, alone.
                walk("right" if side == RIGHT else "left", lambda: g.where() == (area, boss_screen), "the boss's room")
                inside = world()
                assert inside["portals"] == before["portals"] + 1 and inside["handoffs"] == before["handoffs"], \
                    (before, inside)
                how = "by the original's scroll"
            else:
                g.warp(shop, f"{part}-door", area, boss_screen, palette, door, region, writes={HP: 0x50})
                inside = world()
                assert not room(screen)["owned"] and room(boss_screen)["event"] == 1, (room(screen), room(boss_screen))
                how = f"by a door, {screen:#04x} beyond its wall not on the map"
            assert inside["room"] == boss_screen and inside["owned"] == 1 and inside["arena"], inside
            g.until("the boss's handler", lambda: g.ram(EVENT) == 0x81 and g.ram(MUSIC) & 0x7F == BOSS_MUSIC, 60)
            assert boss in g.ram(ENTITIES, 8), [hex(e) for e in g.ram(ENTITIES, 8)]
            sealed(boss_screen)
            g.shot(f"{part}-arena")
            print(f"ok: {part}: in {how}, the room alone in Seamless, the boss handler running "
                  f"(event $81, music $0A) and the boss ({boss:#04x}) there")

            # 3. The boss killed: the room joins again.
            g.poke({ENTITIES + i: 0xFF for i in bosses()})
            g.until("the boss handler to end", lambda: g.ram(EVENT) == 0xFF, 60)
            assert g.ram(MUSIC) & 0x7F == g.ram(DEFAULT_MUSIC) != BOSS_MUSIC, (g.ram(MUSIC), g.ram(DEFAULT_MUSIC))
            g.until("Seamless to join the room", lambda: world()["owned"] > 1, 30)
            joined = world()
            assert not joined["arena"] and room(boss_screen)["edges"][1 - side] == screen, (joined, room(boss_screen))
            g.wait(20)
            g.shot(f"{part}-joined")
            print(f"ok: {part}: with the boss dead, the handler puts the music back and Seamless joins the room to "
                  f"{joined['owned'] - 1} more, {screen:#04x} at its {'left' if side == RIGHT else 'right'} edge")

            # 4. Out again, seamlessly.
            if door is not None:
                # Walled in: out by its door to $14, and back in by $14's.
                g.act("up", 4, 10)
                g.until("$14", lambda: g.where() == (area, DOOR_OUT) and g.settled(), 600)
                g.poke({**{ENTITIES + i: 0xFF for i in range(8)}, HP: 0x50})   # $14's enemies, away from its door
                g.walk_to(DOOR_X - 6, DOOR_X + 4)
                for _ in range(5):
                    if g.where() != (area, DOOR_OUT):
                        break
                    g.act("up", 4, 20)
                g.until("the boss's room again", lambda: g.where() == (area, boss_screen) and g.settled(), 600)
                again = world()
                assert again["owned"] == 1 and again["arena"] and boss in g.ram(ENTITIES, 8), again
                g.wait(30)
                g.shot(f"{part}-again")
                print(f"ok: {part}: out by its door and back in by {DOOR_OUT:#04x}'s, the original's load brings "
                      f"the boss back, and the room is an arena again")
                continue
            # With Seams (#166) the room beside, refreshed by the scroll's
            # load, has its monsters back, and once seen they come at him: one
            # that knocks him back over the seam is a second handoff. Send the
            # neighbours' monsters away, as tests/fixes/small_fixes.py does.
            g.poke({}, fax_e2e.quiet_neighbours(g.save(f"{part}-out")))
            before = world()
            walk("left" if side == RIGHT else "right", lambda: g.where() == (area, screen), "the room beside")
            out = world()
            assert out["handoffs"] == before["handoffs"] + 1 and out["portals"] == before["portals"], (before, out)
            print(f"ok: {part}: back out to {screen:#04x} by a Seamless handoff, no scroll")

        if args.wants("zenis-00"):
            area, screen, palette, pos, region = FINAL
            g.warp(shop, "zenis-00", area, screen, palette, pos, region, writes={HP: 0x50})
            inside = world()
            assert inside["room"] == screen and inside["owned"] == 1 and inside["arena"], inside
            assert room(screen)["event"] == 2 and SHADOW_EURA in g.ram(ENTITIES, 8), room(screen)
            g.shot("zenis-00-arena")
            print("ok: zenis-00: through the door, the final boss's room alone in Seamless, Shadow Eura there")
            g.poke({ENTITIES + i: 0xFF for i in range(8)})
            g.until("the ending's King's room", lambda: g.ram(AREA) == BUILDINGS, 1200)
            assert world()["failures"] == 0, world()
            g.wait(60)
            g.shot("zenis-00-ending")
            print("ok: zenis-00: every slot empty, the game's handler begins the ending, and it reaches the King's room")
        print("screenshots:", ", ".join(g.shots))
    except BaseException:
        if g.running():
            g.shot("failure")
        raise
    finally:
        g.close()


if __name__ == "__main__":
    sys.exit(main())
