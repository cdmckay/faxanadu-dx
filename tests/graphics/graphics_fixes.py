"""Play the graphics fixes end to end in a hidden window and check the pixels.

    python3 tests/graphics/graphics_fixes.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/graphics

The game runs uncapped and the script waits on the game's own state, never on
the clock, on a reserved TCP port (tests/e2e/fax_e2e.py), so any number can
run in parallel. The remaster plays in Seamless, the original in the stock view.

Each look is a screenshot and a save state taken at the same frame boundary:
the state gives the sprites the picture shows (the PPU's OAM, or with Seamless
the OAM its picture is composed from), the CHR RAM they are drawn from, the
nametable and the blocks. Each check then reads the picture: a cell's
pixels, where its tile's pattern says, are its palette's colors, learned from
the cells around it that the game draws right. Each part runs with the
remaster on (the fix), then off (the original's bug, shown the same way), and
starts from a fixture (an external prerequisite, not coverage):

1. studded: the hero in Studded Mail without a shield (equipped through the
   Select menu, so the game's own loader fills the CHR: slots 0-51, and slot
   $34 keeps the Small Shield's tile from before), on Trunk 6's ladder. His
   climbing pose's chest cell (row 1) is slot $30, which holds Studded Mail's
   tile $7D, in palette 1 like the cells beside it; the original draws slot
   $34, the stale shield tile.
2. fire: Fire cast on Trunk 6. Its second frame is mirrored top to bottom
   like its first: the bottom right cell is the top right's tile ($7E)
   flipped, pixel for pixel; the original draws its first frame's $66 there.
3. myconid: Trunk $18's Myconid, out of a building's door. Its second frame's
   bottom right cell (a foot) is palette 3, as the other seven are; in the
   original, palette 2's other colors.
4. fortress: the Tower of Fortress's $2D (palette 7), out of a building's
   door. The floor block under the left pillar (row 11, column 3) is block
   $08, drawn as the floor's $08 blocks beside it; the original has the
   Mattock wall's brown block $63. With the remaster, also from $2E next
   door, where $2D is a room Seamless decodes itself (the original, in the
   stock view, does not show it from there).
5. title: the title's F: the tile with its tip (column 6, row 5) is in the
   logo's palette like the F's other tiles; the original draws it in the
   stars' grey.

graphics_fix_state counts what each fix changed (none with the remaster off).
"""
from pathlib import Path
import struct
import sys
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "movement"))
import fax_e2e  # noqa: E402
import play  # noqa: E402,F401  (its fixtures: trunk-ladder, the hero on Trunk 6 by its ladder)
from fax_e2e import batch, chunks, parse_args, ram, tcp, until, wait  # noqa: E402
from play import Game  # noqa: E402  (fax_e2e's, with what play's fixtures use)

PARTS = ("studded", "fire", "myconid", "fortress", "title")
PACKAGE = ("faxanadu.remastered", "remastered")                 # the one switch: off is the original game
HUD, ROOM_W, ROOM_H = 32, 256, 208
# RAM
AREA, SCREEN, POS_X, POS_Y, FLAGS = 0x24, 0x63, 0x9E, 0xA1, 0xA4
SHADOW_OAM, SCREEN_BUFFER, ENTITIES, HP, MP = 0x700, 0x600, 0x2CC, 0x431, 0x39A
TEXTBOX_X, TEXTBOX_CLOSED, MENU_CURSOR = 0x208, 0x20C, 0x21E
ARMOR, NUMBER_OF_ARMORS, SELECTED_ARMOR, SELECTED_SHIELD = 0x3A1, 0x3C3, 0x3BE, 0x3BF
MAGIC, NUMBER_OF_SPELLS, SELECTED_MAGIC, CAST_MAGIC_TYPE = 0x3A9, 0x3C5, 0x3C0, 0x2B3
SAVED_AREA, SAVED_SCREEN, SAVED_PALETTE, SAVED_POS, AREA_REGION = 0x3D5, 0x3D6, 0x3D7, 0x3D8, 0x435
CAN_CLIMB, CLIMBING = 0x08, 0x10
BUILDINGS, TRUNK = 4, 1
LEATHER, STUDDED, FIRE, NONE = 0, 1, 2, 0xFF
MENU_WIDTH, LIST_WIDTH, MENU_ARMOR = 0x0A, 0x14, 1
MYCONID, MYCONID_SCREEN = 0x1B, 0x18
# ROM: bank 8's armor tiles (TILES_ARMOR_START, $81A4, 16 bytes each) and shield tiles
# (TILES_SHIELDS_START, $9104); Player_LoadShieldTilesToPPU loads the Small Shield's
# (TILES_SHIELDS_SMALL: 00 01 06 07 0C 0D) at slots $30-$35, so slot $34 gets its $0C
ARMOR_TILES, SHIELD_TILES, SMALL_SHIELD_AT_34 = 16 + 8 * 0x4000 + 0x81A4 - 0x8000, 16 + 8 * 0x4000 + 0x9104 - 0x8000, 0x0C
STUDDED_TILES = [0x00, 0x01, 0x06, 0x07, 0x08, 0x09, 0x0E, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B,
                 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x4B, 0x4C, 0x51, 0x52, 0x53,
                 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5E, 0x5F, 0x60, 0x61, 0x62, 0x63, 0x75, 0x76, 0x7B, 0x7C,
                 0x7D, 0x7E, 0x7F, 0x80]       # TILES_PLAYER_ARMOR_STUDDED_MAIL_START ($8056): slots 0-51
CHEST, STALE = 0x30, 0x34                        # the fixed slot ($7D) and the one the original names
FIRE_TOP, FIRE_FIRST = 0x7E, 0x66                # the second frame's top right tile; its first frame's
FORTRESS, FORTRESS_EAST, FLOOR_BLOCK, WALL_BLOCK, UNDER_PILLAR = 0x2D, 0x2E, 0x08, 0x63, 0xB3


# Pictures and save states

def png(path):
    """A PNG's pixels: rows of (r, g, b)."""
    data = Path(path).read_bytes()
    pos, idat = 8, b""
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, _, ctype = struct.unpack(">IIBB", body[:10])
            bpp = 4 if ctype == 6 else 3
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    raw, stride, rows, prior = zlib.decompress(idat), w * bpp, [], bytearray(w * bpp)
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a, b, c = line[i - bpp] if i >= bpp else 0, prior[i], prior[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append([tuple(line[x * bpp:x * bpp + 3]) for x in range(w)])
        prior = line
    return rows


class State:
    """A save state's CPU RAM, CHR RAM and PPU memories (HwPpu ends with OAM,
    palette and CIRAM)."""

    def __init__(self, path):
        data = Path(path).read_bytes()
        c = {tag: (pos, size) for tag, pos, size in chunks(data)}
        pos, _ = c[b"HW  "]
        self.ram = data[pos + 23:pos + 23 + 2048]
        pos, size = c[b"CHRR"]
        self.chr = data[pos:pos + size]
        pos, size = c[b"PPU "]
        ppu = data[pos:pos + size]
        self.ciram, self.oam = ppu[-0x1000:], ppu[-0x1000 - 32 - 256:-0x1000 - 32]

    def pattern(self, table, tile):
        """A tile's 8x8 color indices, from CHR RAM."""
        t = self.chr[table + tile * 16:table + tile * 16 + 16]
        return [[(t[y] >> (7 - x) & 1) | (t[y + 8] >> (7 - x) & 1) << 1 for x in range(8)] for y in range(8)]


def rom_pattern(image, at, tile):
    t = image[at + tile * 16:at + tile * 16 + 16]
    return [[(t[y] >> (7 - x) & 1) | (t[y + 8] >> (7 - x) & 1) << 1 for x in range(8)] for y in range(8)]


class Look:
    """One frame boundary: the picture, the state, and where the active room
    (or `room`) sits in the picture (its top-left, under the HUD). drop()
    forgets a look that did not show what was waited for."""

    def __init__(self, game, name, room=None):
        shot = game.out / f"{len(game.shots):02d}-{name}.png"
        state = game.out / f"look-{len(game.shots):02d}.cycstate"
        cmds = [("screenshot", dict(layer="picture", path=str(shot))), ("save_state", dict(path=str(state))),
                ("fax_world", {})]
        world = tcp("fax_world")
        if world["enabled"] and world["playing"]:                 # the room: `room`, else the active one
            cmds.append(("fax_world_room", {} if room is None else dict(room=room)))
        replies = batch(*cmds)
        game.shots.append(shot.name)
        self.game, self.shot = game, shot
        self.rows, self.state, world = png(shot), State(state), replies[2]
        self.seamless = world["enabled"] and world["playing"]
        if self.seamless:
            xy = replies[3]["xy"]
            self.origin = (xy[0] * ROOM_W - world["camera"][0], HUD + xy[1] * ROOM_H - world["camera"][1])
        else:
            self.origin = ((len(self.rows[0]) - 256) // 2, HUD)

    def drop(self):
        self.game.shots.remove(self.shot.name)
        self.shot.unlink()

    def sprites(self):
        """The sprites the picture shows, (y, tile, attr, x) as OAM has them:
        the PPU's, or the OAM Seamless composed its picture from."""
        oam = self.state.ram[SHADOW_OAM:SHADOW_OAM + 256] if self.seamless else self.state.oam
        return [tuple(oam[i:i + 4]) for i in range(0, 256, 4) if oam[i] < 0xEF]

    def at(self, x, y):
        """The picture's pixel at room coordinates (x, y), y under the HUD; None off the picture."""
        px, py = self.origin[0] + x, self.origin[1] + y
        if 0 <= py < len(self.rows) and 0 <= px < len(self.rows[0]):
            return self.rows[py][px]
        return None

    def sprite_cell(self, sprite):
        """{(dx, dy): (index, color)} for a sprite's opaque pixels: its tile's
        pattern, flipped as its attribute says, and the picture's color there.
        A sprite's pixels start a line below its OAM Y, which includes the HUD."""
        y, tile, attr, x = sprite
        pattern = self.state.pattern(0x0000, tile)
        cell = {}
        for dy in range(8):
            for dx in range(8):
                v = pattern[7 - dy if attr & 0x80 else dy][7 - dx if attr & 0x40 else dx]
                color = self.at(x + dx, y + 1 - HUD + dy)
                if v and color is not None:
                    cell[(dx, dy)] = (v, color)
        return cell


def palette(cells):
    """Color index -> color, learned from cells the game draws right in one
    palette; every pixel of an index must agree."""
    colors = {}
    for cell in cells:
        for v, color in cell.values():
            assert colors.setdefault(v, color) == color, f"index {v}: {colors[v]} and {color}"
    return colors


def matches(cell, colors):
    """(pixels whose index was learned, how many of them have its color)."""
    known = [(v, c) for v, c in cell.values() if v in colors]
    return len(known), sum(colors[v] == c for v, c in known)


def press_until(button, what, test, frames=60, tries=5):
    """Press `button` until `test` holds: a press the game read too early, on
    a loaded machine, is pressed again; one it took is never repeated."""
    for _ in range(tries):
        tcp("action", name=button, frames=4)
        try:
            until(what, test, frames)
            return
        except AssertionError:
            pass
    raise AssertionError(f"{what}: {tries} presses of {button} did nothing")


# Fixtures

def walk_out(game, screen):
    """Right out of the building, onto Trunk's `screen`, whose enemies are
    cleared at once; until the zone name has gone."""
    for _ in range(60):
        if ram(AREA) != BUILDINGS:
            break
        tcp("action", name="right", frames=10)
        wait(10)
    until("Trunk", lambda: (ram(AREA), ram(SCREEN)) == (TRUNK, screen), 600)
    if screen != MYCONID_SCREEN:
        game.poke({HP: 0x50, **{ENTITIES + i: NONE for i in range(8)}})
    wait(150)


def _door(g, screen, pos, palette_index):
    """In Eolis's meat shop, its door leading onto Trunk's `screen` at block
    `pos` (YX) in palette `palette_index`: Area_Saved*, which Game_ExitBuilding
    sets the outside up from, and Area_Region (Trunk's is 1, as its area). The test walks out, so the screen loads in its
    own game, with the remaster on or off."""
    g.poke({SAVED_AREA: TRUNK, SAVED_SCREEN: screen, SAVED_PALETTE: palette_index, SAVED_POS: pos,
            AREA_REGION: TRUNK})
    g.wait(5)


@fax_e2e.recipe("door-myconid", parent="eolis-meat-shop")
def _door_myconid(g):
    """The door onto Trunk $18, with its Myconid (Trunk's palette, 6)."""
    _door(g, MYCONID_SCREEN, 0x92, 6)


@fax_e2e.recipe("door-fortress", parent="eolis-meat-shop")
def _door_fortress(g):
    """The door onto Trunk $2D, the Tower of Fortress, at the secret Guru's door (the towers' palette, 7)."""
    _door(g, FORTRESS, 0x9A, 7)


@fax_e2e.recipe("door-fortress-east", parent="eolis-meat-shop")
def _door_fortress_east(g):
    """The door onto $2E, next to $2D, at its left edge."""
    _door(g, FORTRESS_EAST, 0x90, 7)


@fax_e2e.recipe("trunk-studded", parent="trunk-ladder")
def _trunk_studded(g):
    """Trunk 6, the hero in Studded Mail and no shield, put on through the
    Select menu: Player_SetArmor loads Studded Mail's tiles (slots 0-51) over
    the Leather Armor's, and slot $34 keeps the Small Shield's."""
    g.poke({ARMOR: STUDDED, ARMOR + 1: LEATHER, NUMBER_OF_ARMORS: 2, SELECTED_SHIELD: NONE,
            **{ENTITIES + i: NONE for i in range(8)}})
    g.wait(10)
    for _ in range(5):
        g.tcp("action", name="select", frames=4)
        try:
            g.until("the menu", lambda: g.ram(TEXTBOX_CLOSED) == 0 and g.ram(TEXTBOX_X + 2) == MENU_WIDTH, 60)
            break
        except AssertionError:
            pass
    g.wait(10)
    for button, what, test in (("down", "the armor row", lambda: g.ram(MENU_CURSOR) == MENU_ARMOR),
                               ("a", "the armor list", lambda: g.ram(TEXTBOX_X + 2) == LIST_WIDTH),
                               ("a", "Studded Mail on", lambda: g.ram(SELECTED_ARMOR) == STUDDED)):
        for _ in range(5):
            g.tcp("action", name=button, frames=4)
            try:
                g.until(what, test, 60)
                break
            except AssertionError:
                pass
        g.wait(10)
    g.until("the menu to close", lambda: g.ram(TEXTBOX_CLOSED) != 0, 120)
    g.wait(30)


# The parts

def studded(game, fixed):
    game.launch(at="trunk-studded")
    assert (ram(SELECTED_ARMOR), ram(SELECTED_SHIELD)) == (STUDDED, NONE)
    game.poke({POS_X: 224, POS_Y: 136, FLAGS: CAN_CLIMB | CLIMBING, HP: 0x50,
               **{ENTITIES + i: NONE for i in range(8)}})
    tcp("action", name="up", frames=12)
    wait(30)
    look = Look(game, f"studded-{'fixed' if fixed else 'original'}")
    r = look.state.ram
    hx, hy = r[POS_X], r[POS_Y]
    hero = [s for s in look.sprites() if 0 <= s[3] - hx < 16 and 0 <= s[0] - HUD - hy < 32]
    assert len(hero) == 8 and r[FLAGS] & CLIMBING, (hero, r[FLAGS])
    chest = [s for s in hero if s[0] - HUD - hy == 8 and s[1] in (CHEST, STALE)]
    assert len(chest) == 1, hero
    chest = chest[0]
    # What the game's loader left in the two slots: Studded Mail's $7D, and the shield's.
    assert look.state.chr[CHEST * 16:CHEST * 16 + 16] == game.image[ARMOR_TILES + 0x7D * 16:][:16]
    assert STALE >= len(STUDDED_TILES), "Studded Mail loads slots 0-51"
    assert look.state.chr[STALE * 16:STALE * 16 + 16] == game.image[SHIELD_TILES + SMALL_SHIELD_AT_34 * 16:][:16], \
        "slot $34 holds the Small Shield's $0C, loaded before Studded Mail"
    colors = palette([look.sprite_cell(s) for s in hero if s[2] & 3 == chest[2] & 3 and s is not chest])
    n, right = matches(look.sprite_cell(chest), colors)
    if fixed:
        assert chest[1] == CHEST and chest[2] & 3 == 1, chest
        assert n >= 20 and right == n, (n, right)
        print(f"ok: Studded Mail's climbing pose: its chest is slot $30, Studded Mail's $7D, in palette 1, "
              f"{right} pixels")
    else:
        assert chest[1] == STALE, chest
        assert n >= 20 and right == n, (n, right)
        print(f"ok: the original: its chest is slot $34, the Small Shield's tile left there, {n} pixels")


def fire(game, fixed):
    game.launch(at="trunk-ladder")
    cast = game.save("cast")
    fax_e2e.fixture(cast, cast, {MAGIC: FIRE, NUMBER_OF_SPELLS: 1, SELECTED_MAGIC: FIRE, MP: 0x50, POS_X: 232,
                                 **{ENTITIES + i: NONE for i in range(8)}})
    # The second frame shows 2 frames in 8 (drawn when InterruptCounter has
    # bit 1 set, that frame when it has bit 2), and the looks come a few frames
    # apart, so each cast first waits a frame more than the last, to look at
    # another phase. The Fire takes 60 frames to cross the screen.
    for attempt in range(16):
        batch(("load_state", dict(path=str(cast))), ("action", dict(name="up", frames=6)),
              ("action", dict(name="b", frames=2)))
        until("the Fire", lambda: ram(CAST_MAGIC_TYPE) == FIRE, 60)
        wait(attempt % 8)
        while ram(CAST_MAGIC_TYPE) == FIRE:
            look = Look(game, f"fire-{'fixed' if fixed else 'original'}")
            flame = [s for s in look.sprites()                   # palette 0, and in front of the background
                     if s[1] in (0x7C, 0x7D, FIRE_TOP, FIRE_FIRST) and not s[2] & 0x23]
            middle = [s for s in flame if s[1] in (0x7C, 0x7D)]
            # The second frame, whole, away from the room's edges (the NES hides
            # sprites in the leftmost 8 pixels, and Seamless draws past them).
            if len(middle) == 4 and len(flame) == 6 and all(16 <= s[3] <= 224 for s in flame):
                break
            look.drop()
        else:
            continue
        break
    else:
        raise AssertionError("no picture of the Fire's second frame in 16 casts")
    top_y, bottom_y = min(s[0] for s in middle) - 8, max(s[0] for s in middle) + 8
    top = [s for s in flame if s[0] == top_y]
    bottom = [s for s in flame if s[0] == bottom_y]
    assert len(top) == 1 and len(bottom) == 1 and top[0][3] == bottom[0][3], flame
    top, bottom = top[0], bottom[0]
    assert top[1] == FIRE_TOP and not top[2] & 0x80 and bottom[2] & 0x80, (top, bottom)
    t, b = look.sprite_cell(top), look.sprite_cell(bottom)
    mirrored = {(dx, 7 - dy): vc for (dx, dy), vc in t.items()}
    # The picture shows these sprites where OAM says: the four middle cells'
    # pixels give one palette, which the top and bottom cells are drawn in too.
    colors = palette([look.sprite_cell(s) for s in middle])
    for cell in (t, b):
        n, right = matches(cell, colors)
        assert n == len(cell) and right == n, (n, right, len(cell))
    if fixed:
        assert bottom[1] == FIRE_TOP, bottom
        assert len(b) == len(t) >= 8 and b == mirrored, "the bottom right cell is the top right's, flipped"
        print(f"ok: the Fire's second frame: its bottom right cell is $7E flipped, the top right's mirror, "
              f"{len(b)} pixels")
    else:
        assert bottom[1] == FIRE_FIRST, bottom
        assert b != mirrored and b, "the original's bottom right cell is not the top right's mirror"
        print(f"ok: the original: its bottom right cell is the first frame's $66, {len(b)} pixels, not the top's mirror")


def myconid(game, fixed):
    game.launch(at="door-myconid")
    walk_out(game, MYCONID_SCREEN)
    for _ in range(100):
        look = Look(game, f"myconid-{'fixed' if fixed else 'original'}", room=MYCONID_SCREEN)
        r = look.state.ram
        slots = [i for i in range(8) if r[ENTITIES + i] == MYCONID]
        assert slots, "the Myconid"
        mx, my = r[0xBA + slots[0]], r[0xC2 + slots[0]]           # CurrentSprites_XPos, _YPos
        body = [s for s in look.sprites() if 0 <= s[3] - mx < 16 and 0 <= s[0] - HUD - my < 32]
        base = min((s[1] for s in body), default=0)
        foot = [s for s in body if s[1] - base == 0x0D - 0x04 and s[0] - HUD - my == 24]
        if len(body) == 8 and foot:
            break                                       # its second frame: slots 08-0B, 04, 05, 0C, 0D
        look.drop()
        wait(1)
    else:
        raise AssertionError("no picture of the Myconid's second frame")
    foot = foot[0]
    rest = [s for s in body if s is not foot]
    assert all(s[2] & 3 == 3 for s in rest), rest
    colors = palette([look.sprite_cell(s) for s in rest])
    n, right = matches(look.sprite_cell(foot), colors)
    if fixed:
        assert foot[2] & 3 == 3 and n >= 10 and right == n, (foot, n, right)
        print(f"ok: the Myconid's second frame: its foot is palette 3, like the rest of it, {right} pixels")
    else:
        assert foot[2] & 3 == 2 and n >= 10 and right < n, (foot, n, right)
        print(f"ok: the original: its foot is palette 2, {n - right} of {n} pixels not the Myconid's colors")


def fortress(game, fixed, east=False):
    game.launch(at="door-fortress-east" if east else "door-fortress")
    walk_out(game, FORTRESS_EAST if east else FORTRESS)
    name = f"fortress{'-east' if east else ''}-{'fixed' if fixed else 'original'}"
    look = Look(game, name, room=FORTRESS)
    if not east:
        assert look.state.ram[SCREEN_BUFFER + UNDER_PILLAR] == (FLOOR_BLOCK if fixed else WALL_BLOCK)
    # Pixels a sprite may cover (the hero, bees) are left out of the comparison.
    covered = {(x + dx, y + 1 - HUD + dy) for y, _, _, x in look.sprites() for dy in range(8) for dx in range(8)}
    if look.seamless and east:
        covered = set()                                          # the sprites are $2E's, at its own place

    def block(index):
        """The block's pixels the picture shows, by (x, y) in it."""
        r, c = divmod(index, 16)
        pixels = {}
        for y in range(16):
            for x in range(16):
                at = (c * 16 + x, r * 16 + y)
                if at not in covered and look.at(*at) is not None:
                    pixels[(x, y)] = look.at(*at)
        return pixels

    under, floor = block(UNDER_PILLAR), block(0xB5)               # row 11: 1B 08 20 [63] 1B 08
    same = [xy for xy in under if xy in floor]
    assert len(same) >= 100, f"{name}: {len(same)} pixels of the block on the picture"
    alike = sum(under[xy] == floor[xy] for xy in same)
    if fixed:
        assert alike == len(same), f"{name}: the block under the left pillar is not drawn as the floor"
        print(f"ok: {name}: the block under $2D's left pillar is drawn as the floor's $08 beside it, "
              f"{alike} pixels")
    else:
        assert alike < len(same) * 3 // 4, f"{name}: {alike} of {len(same)} pixels like the floor"
        print(f"ok: {name}: the original draws the Mattock wall's block $63 there, "
              f"{len(same) - alike} of {len(same)} pixels unlike the floor")


def title(game, fixed):
    game.launch()
    until("the title", lambda: game.frame() > 800, 2000)
    look = Look(game, f"title-{'fixed' if fixed else 'original'}")
    nt = look.state.ciram

    def cell(col, row):
        """The background cell's (index, color) for each pixel; the title's tiles are at $1000."""
        pattern = look.state.pattern(0x1000, nt[row * 32 + col])
        return {(x, y): (pattern[y][x], look.at(col * 8 + x, row * 8 + y - HUD)) for y in range(8) for x in range(8)}

    assert [nt[5 * 32 + c] for c in (4, 5, 6)] == [0x01, 0x02, 0x03], "the top of the F"
    colors = palette([cell(4, 5), cell(5, 5)])                     # the F's other tiles, in the logo's palette
    tip = cell(6, 5)
    n, right = matches(tip, colors)
    lit = sum(1 for v, _ in tip.values() if v)
    if fixed:
        assert n == 64 and right == 64, (n, right)
        print(f"ok: the title: the tile with the F's tip is in the logo's palette, its {lit} pixels like the F's")
    else:
        assert right < n, (n, right)
        print(f"ok: the original title: the F's tip is in the stars' palette, {n - right} pixels not the F's")


def main():
    args = parse_args(__doc__, parts=PARTS)
    game = Game.from_args(args)
    fixtures = {"studded": ["trunk-studded"], "fire": ["trunk-ladder"], "myconid": ["door-myconid"],
                "fortress": ["door-fortress", "door-fortress-east"], "title": []}
    counters = {"studded": "studded_climb", "fire": "fire", "myconid": "myconid", "fortress": "fortress",
                "title": "title"}
    try:
        for part in PARTS:
            if not args.wants(part):
                continue
            for name in fixtures[part]:
                game.state(name)                                    # made before the test's own games start
            for fixed in (True, False):
                game.set_features({} if fixed else {PACKAGE: False})
                run = {"studded": studded, "fire": fire, "myconid": myconid, "fortress": fortress,
                       "title": title}[part]
                run(game, fixed)
                state = tcp("graphics_fix_state")
                assert state["enabled"] == fixed, state
                assert (state[counters[part]] > 0) == fixed, state
                if part == "fortress" and fixed:                    # $2D beside $2E: only Seamless shows it
                    game.close()
                    fortress(game, fixed, east=True)
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
