"""Play Quick shops end to end, in a hidden window and headless, and screenshot each step.

    python3 tests/shops/quick_shops.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/shops
    python3 tests/shops/quick_shops.py ... --part talk-buy

The game runs uncapped and the script waits on the game's own state, never on
the clock, on a reserved TCP port (tests/e2e/fax_e2e.py). Every part but
"original" and "talk-original" plays the remaster, in Seamless.

The shops talk as the original's do (#163): Up on the shopkeeper, his
portrait and greeting, the Buy/Sell menu ("Come here to buy. Come here to
sell."), then the list after its own line ("What would you like?" or "Which
one would like to sell?"). The one change: after a purchase or a sale the
hero stays in that list until B, which ends the conversation, after "Thank
you for shopping." if he bought or sold anything.

The talk parts play a whole conversation headless from a save state at Eolis's
tool shop's counter (the fixture "eolis-tool-counter", with RAM edits for the
gold and what the hero carries), frame exact, every frame's RAM and presented
picture (--present-out) kept. It plays in segments: each runs on until the
step its plan waits for is on screen (a line typed out, the menu's or the
list's cursor drawn: Menu_UpdateAndDraw's arrow, a sprite, in OAM), and the
next starts from the save state of its last frame with that step's button
pressed, so the frames are one unbroken run. Each step is screenshotted, and
every frame of the run is held to what the original shows, never a flash:

  - the portrait, once its box is up, is in every frame until the
    conversation ends (IScriptAction_EndScript clears it);
  - an empty dialogue box always gives way to a line typed in it, never to a
    list or to nothing;
  - a list opens only over a dialogue box with a line in it.

Seamless (fax_world.c) shows a script's boxes only while TextBox_Closed is 0.
Before #163, Quick shops opened the list straight over an empty dialogue box
(the chat box's flash), and B closed the list (TextBox_Close) and opened the
dialogue box again before the end, so for 8 frames no box and no portrait
showed, then the portrait came back for 20 and went (its flicker). Both break
the rules above.

talk-buy       Two purchases in a row (the Hand Dagger, then Down and the
               Deluge), each back in the list with the cursor where it was;
               B: "Thank you for shopping.", then the end.
talk-sell      The same for selling: Down on the menu, "Which one would like
               to sell?", two Red Potions sold in a row, B, the thanks.
talk-message   "This is not enough Golds." goes back to the list, without
               "What would you like?" again; B there, nothing bought: the end,
               with no thanks. Then, in another conversation, B in the buy
               list at once ends it quietly too.
talk-original  The remaster off, in the stock view: the original's
               conversation, one purchase and its thanks, held to the same
               rules, which it keeps.

The other parts drive the shops over TCP (fax_e2e's shop_list() goes through
the conversation) and check the lists, their hint line and OWNED in the
picture, and gold and inventory after each step:

tool-shop  Eolis's tool shop (Hand Dagger 400, Deluge 400: its Red Potion is
           the Potion Seller's now, fax_potions.c, and its Elixir is found, not
           bought, fax_found.c), with 700 Golds, the Giant Blade in hand and,
           unequipped, the Long Sword and the Dragon Slayer, and the J and A
           keys, the Wing Boots and two Red Potions. A buys the Hand Dagger
           (the list stays up, the cursor on it); Down and A on the Deluge says
           "This is not enough Golds." and A goes back to the list; B thanks
           him. Then the sell list: everything carried, the Items first, at
           half a shop's price (a Red Potion 80 and the Hand Dagger 200, this
           shop's; the Long Sword 800, Forepaw's), and with no price the Wing
           Boots and the keys, his for good, and the Dragon Slayer. On those
           the hint says "Not for sale" and A does nothing; Down from the
           bottom wraps to the top, and a Red Potion sells.
key-shop   Next door, the Key Seller talks the same way, and buys a Red Potion
           for 80 (half the lowest tool shop's price).
two-pages  A full inventory (8 items and 3 more weapons) is two pages, the keys
           and the Mattock on the first with no price: Down from the bottom of
           the first turns to the second, and a sale there keeps the page.
last-one   Selling the last item (a Red Potion) empties the list ("You have
           nothing to buy.", the game's own line), and the conversation goes
           on to buying, with "What would you like?".
owned-*    Mascon's tool shop (Large Shield 3500, Thunder 1200, Mattock 800,
           its Red Potion gone to the Potion Seller), with 9000 Golds and a Red
           Potion: the Mattock is priced, and buying it marks it OWNED at once
           (it is kept, reusable tools). Coming back, it keeps its row with
           OWNED where the price was, the hint says "Already owned" on it, and
           A there does nothing: no gold, no line, the list as it was. Thunder
           above it still buys. Then a Mattock found in the world (Trunk $0E,
           where one lies on one visit in four: the visit counter set to show
           it, its enemies cleared) is owned the same. (The Wing Boots, the Hour
           Glass and the Elixir, which recharging items keeps, no shop sells any
           more: tests/found/found_items.py.) And at Eolis's key shop, a J key
           held: OWNED, and A does nothing; in the sell list it is not for
           sale, and A keeps it.
a-key      The A key farm is gone: the man in the house on Mist $1F gives the A
           key to a hero with the Black Onyx and none (each time he is asked),
           and from his house straight to Forepaw's tool shop, with all he has,
           the key is not for sale and A keeps it, while a Red Potion sells for
           200.
original   With the remaster off, the original: the same Up shows the shop's
           greeting and then the Buy/Sell menu's own line. (With it on the
           shops run that menu too, with its own line: Save files serves its
           line there only for the Guru's menu.)

Each list is checked in the picture too: the list's frame, and its hint line
inside it, in the text color (and nothing there on an entry that buys or
sells), and an owned item's OWNED in its price's cells.
"""
from collections import Counter
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
import fax_e2e  # noqa: E402
from fax_e2e import (Game, act, fixture, hold, parse_args, ram, shop_leave, shop_list, step_toward, tcp,  # noqa: E402
                     type_out, until, wait, walk)

AREA, SCREEN, PLAYER_X = 0x24, 0x63, 0x9E
TEXTBOX_X, TEXTBOX_CLOSED, MESSAGE_ID, MENU_CURSOR = 0x208, 0x20C, 0x213, 0x21E
GOLD, HP = 0x392, 0x431
WEAPONS, MAGIC, ITEMS = 0x39D, 0x3A9, 0x3AD                         # WeaponInventory, the spells, ItemInventory
SELECTED_WEAPON, SELECTED_MAGIC, SELECTED_ITEM = 0x3BD, 0x3C0, 0x3C1
NUMBER_OF_WEAPONS, NUMBER_OF_MAGIC, NUMBER_OF_ITEMS = 0x3C2, 0x3C5, 0x3C6
PORTRAIT = 0x3C7
AREA_SAVED_AREA, AREA_SAVED_SCREEN, AREA_SAVED_PALETTE, PLAYER_SAVED_POS = 0x3D5, 0x3D6, 0x3D7, 0x3D8
AREA_REGION, VISIT_COUNT = 0x435, 0x43A                             # Screen_TrackedVisitCount
SCREEN_READY, SPRITE_ENTITIES = 0x0E, 0x2CC                         # CurrentSprites_Entities: 8 slots
ISCRIPT_ADDR, ISCRIPT_OFFSET = 0xDB, 0xDD
PPU_BUFFER_READ, PPU_BUFFER_WRITE = 0x1F, 0x20
TRUNK, TOWNS, BUILDINGS, NO_PORTRAIT = 1, 3, 4, 0xFF
FOREPAW, FOREPAW_TOOLS_DOOR, FOREPAW_TOOL_SHOP = 0x02, 0x9D, 0x0F    # the towns' screen 2, its door, building $0F
MASCON, MASCON_TOOLS_DOOR, MASCON_TOOL_SHOP = 0x04, 0x9E, 0x18       # the towns' screen 4, its door, building $18
EOLIS_TOOLS_SCREEN, EOLIS_TOOLS_DOOR = 3, 0x9A                       # Eolis's tool shop: screen 3, block $9A
PALETTE_TOWN, PALETTE_TOWER = 0x1B, 0x07
HAND_DAGGER, LONG_SWORD, GIANT_BLADE, DRAGON_SLAYER = 0x00, 0x01, 0x02, 0x03
STUDDED_MAIL, SMALL_SHIELD, LARGE_SHIELD, THUNDER = 0x21, 0x40, 0x41, 0x61
KEY_A, KEY_K, KEY_Q, KEY_J, MATTOCK, WING_BOOTS, RED_POTION = 0x04, 0x05, 0x06, 0x07, 0x09, 0x0F, 0x10
DELUGE = 0x60
ITEM = 0x80                                                         # the Items category, in a full ID
# Message IDs: the shops' lines
THANK_YOU, NOT_ENOUGH_GOLDS, CANT_CARRY, I_SELL_KEYS, I_SELL_TOOLS = 0x02, 0x03, 0x06, 0x0E, 0x0F
BUY_OR_SELL, NOTHING_TO_SELL, WHICH_ONE, WOULD_LIKE = 0x10, 0x12, 0x13, 0x14
GREETINGS, LIST_LINES, MESSAGES = (I_SELL_KEYS, I_SELL_TOOLS), (WHICH_ONE, WOULD_LIKE), (NOT_ENOUGH_GOLDS, CANT_CARRY,
                                                                                       NOTHING_TO_SELL)
GOT_MATTOCK, ABOVE_MATTOCK = 0xB2, 0x4D                            # "I'm holding Mattock."; over $0E's, at $5D (YX)
DURATION_OINTMENT = 0x427
EOLIS_TOOLS = 0xA37A                                                # its script: BUY at + 12, SELL at + 6
MASCON_LIST = [LARGE_SHIELD, THUNDER, ITEM | MATTOCK]               # no Red Potion: the Potion Seller's
MASCON_PRICES = [3500, 1200, 800]
THUNDER_ROW, MATTOCK_ROW = 1, 2
# The tool shop's sell list: two Red Potions, then the kit's Items from the last, then its weapons from the last.
BOOTS_SELL_ROW, KEY_A_ROW, KEY_J_ROW, DAGGER_ROW, LONG_SWORD_ROW, SLAYER_ROW = 2, 3, 4, 5, 6, 7
SPECIAL_ITEMS, BLACK_ONYX = 0x42C, 0x01                           # SpecialItems, its Black Onyx bit
MIST, MIST_SCREEN, MIST_HOUSE, MIST_HOUSE_DOOR = 2, 0x1F, 0x22, 0x94  # the A key's giver: Buildings $22
PALETTE_MIST, REGION_MIST = 0x0A, 2
ZONE_NAMES = "faxanadu.zone-names"                                  # its record: the area and screen, at 5 and 6
LIST_FRAME = (80, 64)                                               # the list's top-left frame tile, tile 10,8
HINT = (96, 208)                                                    # its hint line: tile 12,26
PAGE_ROW, LINE_END = 72, 224                                        # the page: row 9, ending at column 27
PACKAGE = ("faxanadu.dx", "dx")                                  # the one switch: off is the original game
# The boxes, in native pixels: TextBox_Open's corner tiles, at the top left and bottom right of each.
PORTRAIT_BOX = ((16, 112), (72, 184))                               # tiles 2,14 and 9,23
DIALOGUE_BOX = ((80, 128), (232, 184))                              # tiles 10,16 and 29,23
LIST_BOX = ((80, 64), (232, 216))                                   # tiles 10,8 and 29,27
TEXT_AREA, INSIDE = (96, 144, 128, 32), (88, 136)                   # the dialogue's four lines; its frame's inside
DIALOGUE_GEOMETRY, LIST_GEOMETRY = (10, 16, 20, 8), (10, 8, 20, 20)  # TextBox_X, _Y, _Width, _Height
CURSOR_TILE, CURSOR_X = 0x72, 88                                    # Menu_UpdateAndDraw's arrow: a sprite, each frame
HOLD = 2                                                            # frames a headless press is held
SEGMENT = 40                                                        # frames a headless segment plays


def shops():
    return tcp("shops_state")


def gold():
    lo, mid, hi = ram(GOLD, 3)
    return lo | mid << 8 | hi << 16


def items():
    return list(ram(ITEMS, 8)[:ram(NUMBER_OF_ITEMS)])


def weapons():
    return list(ram(WEAPONS, 4)[:ram(NUMBER_OF_WEAPONS)])


def carrying(gold_amount, weapon_list, item_list, weapon=HAND_DAGGER, spells=()):
    """RAM writes for a hero with this gold, weapon in hand, and lists."""
    w = {GOLD: gold_amount & 0xFF, GOLD + 1: gold_amount >> 8 & 0xFF, GOLD + 2: gold_amount >> 16,
         SELECTED_WEAPON: weapon, NUMBER_OF_WEAPONS: len(weapon_list), NUMBER_OF_ITEMS: len(item_list),
         SELECTED_ITEM: 0xFF, SELECTED_MAGIC: 0xFF, NUMBER_OF_MAGIC: len(spells), HP: 0x50}
    w.update({WEAPONS + i: v for i, v in enumerate(weapon_list)})
    w.update({ITEMS + i: v for i, v in enumerate(item_list)})
    w.update({MAGIC + i: v & 0x1F for i, v in enumerate(spells)})
    return w


TOOL_KIT = carrying(700, [DRAGON_SLAYER, LONG_SWORD], [KEY_J, KEY_A, WING_BOOTS] + [RED_POTION] * 2, GIANT_BLADE)
PARTS = ("talk-buy", "talk-sell", "talk-message", "talk-original", "tool-shop", "key-shop", "two-pages",
         "last-one", "owned-mascon", "owned-found", "owned-key", "a-key", "original")   # each its own game


def walk_out():
    for _ in range(300):
        if ram(AREA) != BUILDINGS:
            return
        hold("right", lambda: ram(AREA) != BUILDINGS)
    raise AssertionError("could not walk out")


def walk_to(x_min, x_max):
    for _ in range(600):
        if ram(TEXTBOX_CLOSED) == 0:
            fax_e2e.page_through("the box to close", lambda: ram(TEXTBOX_CLOSED) != 0)
            continue
        x = ram(PLAYER_X)
        if x_min <= x <= x_max:
            return
        step_toward(x_min, x_max, lambda: ram(TEXTBOX_CLOSED) == 0)
    raise AssertionError(f"could not reach x {x_min}-{x_max}")


def into_eolis(screen, door):
    """Walk out of the building the hero is in, whose door leads to Eolis's
    door `door` (block YX) on `screen`, and in at that door."""
    walk_out()
    until(f"Eolis screen {screen}", lambda: (ram(AREA), ram(SCREEN)) == (0, screen))
    wait(30)
    column = (door & 15) * 16
    walk_to(column - 6, column + 4)
    for _ in range(3):                                                  # Up counts only on the frame it goes down
        act("up", 4, 10)
        try:
            until("the building", lambda: ram(AREA) == BUILDINGS, 120)
            break
        except AssertionError:
            pass
    wait(40)


def to_counter(stop=0):
    """Walk left to the counter, or to x `stop`: held until he stops there."""
    walk("left", lambda: ram(PLAYER_X) <= stop)


@fax_e2e.recipe("eolis-tool-counter", parent="eolis-meat-shop")
def _eolis_tool_counter(g):
    """At the counter of Eolis's tool shop (building 4: screen 3, block $9A),
    the Tool Seller beyond it: walked out of the meat shop, its door edited to
    lead to the tool shop's, and in."""
    g.poke({AREA_SAVED_AREA: 0, AREA_SAVED_SCREEN: EOLIS_TOOLS_SCREEN, PLAYER_SAVED_POS: EOLIS_TOOLS_DOOR})
    into_eolis(EOLIS_TOOLS_SCREEN, EOLIS_TOOLS_DOOR)
    to_counter()
    until("the counter", lambda: ram(PLAYER_X) <= 80, 120)            # behind it he stops at x 80
    wait(10)


# ---- pictures ----

def png(path):
    """A screenshot's pixels: (width, height, rows of (r, g, b) tuples)."""
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos, idat = 8, b""
    while pos < len(data):
        size, kind = struct.unpack_from(">I4s", data, pos)
        body = data[pos + 8:pos + 8 + size]
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", body)
            assert depth == 8 and color in (2, 6) and not interlace, (depth, color, interlace)
        elif kind == b"IDAT":
            idat += body
        pos += 12 + size
    bpp = 3 if color == 2 else 4
    raw, stride, rows, prior = zlib.decompress(idat), width * bpp, [], bytearray(width * bpp)
    for y in range(height):
        kind, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b, c = prior[i], prior[i - bpp] if i >= bpp else 0
            p = a + b - c
            pred = (0, a, b, (a + b) // 2,
                    a if abs(p - a) <= abs(p - b) and abs(p - a) <= abs(p - c) else b if abs(p - b) <= abs(p - c) else c)[kind]
            line[i] = (line[i] + pred) & 0xFF
        rows.append([tuple(line[x * bpp:x * bpp + 3]) for x in range(width)])
        prior = line
    return width, height, rows


class Picture:
    """A presented picture, read fast: the game writes its PNGs unfiltered
    (cyc_png.c), so each row is a slice of the inflated data. Cells are in
    native pixels, where Seamless puts the native picture (centred)."""

    def __init__(self, path):
        data = Path(path).read_bytes()
        pos, idat = 8, b""
        while pos < len(data):
            size, kind = struct.unpack_from(">I4s", data, pos)
            if kind == b"IHDR":
                self.width, self.height, _, color = struct.unpack_from(">IIBB", data, pos + 8)
            elif kind == b"IDAT":
                idat += data[pos + 8:pos + 8 + size]
            pos += 12 + size
        self.bpp = 3 if color == 2 else 4
        raw, stride = zlib.decompress(idat), self.width * self.bpp + 1
        if any(raw[y * stride] for y in range(self.height)):              # filtered after all: the slow way
            _, _, rows = png(Path(path))
            raw, self.bpp = b"".join(b"\0" + bytes(c for p in row for c in p) for row in rows), 3
            stride = self.width * 3 + 1
        self.raw, self.stride = raw, stride
        self.ox = (self.width - 256) // 2 if self.width >= 256 else 0

    def cell(self, x, y, w=8, h=8):
        x0 = (self.ox + x) * self.bpp + 1
        return b"".join(self.raw[(y + r) * self.stride + x0:(y + r) * self.stride + x0 + w * self.bpp] for r in range(h))

    def blank(self, area, at):
        """Every pixel of `area` (x, y, w, h) is the color at `at`."""
        x, y, w, h = area
        return self.cell(x, y, w, h) == self.cell(*at, 1, 1) * (w * h)


def check_hint(path, text, page=None):
    """The list in the picture: its frame's corner where Shop_Draw puts it,
    its hint line holding `text`'s letters in one color on black (None: no
    hint, every cell dark), and the page, if any, at the right of the row
    over its first entry."""
    width, height, px = png(path)
    ox = (width - 256) // 2 if width >= 256 else 0                 # where Seamless puts the native picture
    corner = [px[LIST_FRAME[1] + r][ox + LIST_FRAME[0] + c] for r in range(8) for c in range(8)]
    assert len(set(corner)) > 1, f"{path.name}: no frame at the list's corner"
    lines = [(HINT[0], HINT[1], text or " " * 16)] + ([(LINE_END - 8 * len(page), PAGE_ROW, page)] if page else [])
    for x, y, line in lines:
        for i, ch in enumerate(line):
            cell = [px[y + r][ox + x + i * 8 + c] for r in range(8) for c in range(8)]
            lit = {p for p in cell if p != (0, 0, 0)}
            assert (ch == " ") == (not lit), f"{path.name}: {ch!r} in {line!r} is {'lit' if lit else 'dark'}"
            assert len(lit) <= 1, f"{path.name}: {line!r} is not one color"


def check_no_price(path, entry):
    """Entry `entry` of the list shows no price: its price cells (Shop_Draw:
    column 23, 5 digits, on the entry's second row) are the list's inside, as
    the blank row over the first entry is."""
    width, height, px = png(path)
    ox = (width - 256) // 2 if width >= 256 else 0
    inside = px[PAGE_ROW][ox + 96]
    y = 88 + entry * 16
    cells = {px[y + r][ox + 184 + c] for r in range(8) for c in range(40)}
    assert cells == {inside}, f"{path.name}: entry {entry} shows a price"


def price_cells(path, entry):
    """Entry `entry`'s five price cells (Shop_Draw: column 23, on the
    entry's second row): for each, the set of colors in it other than the
    list's inside."""
    width, height, px = png(path)
    ox = (width - 256) // 2 if width >= 256 else 0
    inside = px[PAGE_ROW][ox + 96]
    y = 88 + entry * 16
    return [{px[y + r][ox + 184 + 8 * k + c] for r in range(8) for c in range(8)} - {inside} for k in range(5)]


def check_owned(path, entry):
    """Entry `entry` shows OWNED where its price was: five letters, one in
    each of the price's cells, in one color. No price in these lists has five
    digits, so a price never fills them all."""
    cells = price_cells(path, entry)
    assert all(cells), f"{path.name}: entry {entry}'s price cells are not five letters: {[len(c) for c in cells]}"
    assert len(set().union(*cells)) == 1, f"{path.name}: entry {entry}'s OWNED is not one color"


def check_priced(path, entry, price):
    """Entry `entry` shows a price of `price`'s digits, right-aligned: its
    leading cells are empty."""
    cells = price_cells(path, entry)
    blank = 5 - len(str(price))
    assert not any(cells[:blank]) and all(cells[blank:]), f"{path.name}: entry {entry} is not priced {price}"


# ---- a conversation, headless and frame exact ----

class Frame:
    """One frame of a headless conversation: its RAM and OAM after it, and
    what its presented picture showed of the boxes (the corner cells and
    whether the dialogue box's lines are empty)."""

    def __init__(self, number, ram_, oam, picture):
        self.number, self.ram, self.oam, self.path = number, ram_, oam, picture
        p = Picture(picture)
        self.width = p.width
        self.cells = {name: (p.cell(*a), p.cell(*b)) for name, (a, b) in
                      (("portrait", PORTRAIT_BOX), ("dialogue", DIALOGUE_BOX), ("list", LIST_BOX))}
        self.blank = p.blank(TEXT_AREA, INSIDE)

    @property
    def message(self):
        return self.ram[MESSAGE_ID]

    @property
    def box(self):
        return tuple(self.ram[TEXTBOX_X:TEXTBOX_X + 4])

    @property
    def cursor(self):
        """A menu waits for a button this frame: its arrow (UI_DrawPromptInputSymbol) is in OAM."""
        return any(self.oam[i] < 0xEF and self.oam[i + 1] == CURSOR_TILE and self.oam[i + 3] == CURSOR_X
                   for i in range(0, 256, 4))

    @property
    def drawn(self):
        """The PPU buffer is empty: what the game queued (a list's rows, a
        line's last letters) is on screen (PPUBuffer_ReadOffset, _WriteOffset)."""
        return self.ram[PPU_BUFFER_READ] == self.ram[PPU_BUFFER_WRITE]

    @property
    def script_at(self):
        return (self.ram[ISCRIPT_ADDR] | self.ram[ISCRIPT_ADDR + 1] << 8) + self.ram[ISCRIPT_OFFSET]


def read_log(path):
    """A --frame-log: (frame, RAM, OAM) for each frame."""
    data = Path(path).read_bytes()
    assert data[:8] == b"CYCFRAME"
    out, pos = [], 12
    while pos < len(data):
        head = struct.unpack_from("<9I", data, pos)
        oam = data[pos + 36 + 32:pos + 36 + 32 + 256]
        pos += 36 + 32 + 256
        out.append((head[0], data[pos:pos + head[3]], oam))
        pos += head[3] + sum(head[4:9])
    return out


class Wait:
    """What the conversation is waiting on, read from a frame: the last line
    loaded (MessageID, until it clears as the line types out), whether the
    menu's arrow went away since the last press (`gone`). A step waits once
    the screen has caught up (Frame.drawn), so a press goes in on what shows."""
    STEPS = {
        "greeting": lambda w, f: w.last in GREETINGS and f.message == 0 and not f.cursor,
        "menu": lambda w, f: w.last == BUY_OR_SELL and f.message == 0 and f.cursor,
        "line": lambda w, f: w.last in LIST_LINES and f.message == 0 and not f.cursor and f.box == DIALOGUE_GEOMETRY,
        "list": lambda w, f: f.cursor and f.box == LIST_GEOMETRY and w.gone,
        "still": lambda w, f: f.cursor and f.box == LIST_GEOMETRY,
        "message": lambda w, f: w.last in MESSAGES and f.message == 0 and not f.cursor,
        "thanks": lambda w, f: w.last == THANK_YOU and f.message == 0 and not f.cursor,
    }

    def __init__(self):
        self.last, self.gone, self.free = None, False, 0

    def see(self, i, f):
        if f.message:
            self.last = f.message
        if not f.cursor:
            self.gone = True

    def ready(self, step, i, f):
        return i >= self.free and f.drawn and self.STEPS[step](self, f)

    def pressed(self, i):
        self.gone, self.free = False, i + HOLD + 2


def converse(game, name, state, plans, original=False):
    """Play conversations from save state `state`, headless and frame exact,
    in segments of SEGMENT frames, each from the save state of the last
    one's last frame, so the frames are one unbroken run. `plans` is a list
    of plans, each a conversation from Up (pressed at once) to its end: a list
    of (step, buttons), the buttons pressed at the start of the segment after
    the one the step's wait (Wait.STEPS) first shows in. Each conversation is
    over once its portrait has been gone and its box closed for 20 frames.
    Returns every frame (Frame, its picture dropped), the frame index each
    step waited in ({(plan, step): index}, step 0 being Up's frame) with that
    frame's picture kept in OUT/talk-NAME/steps, the frame index each
    conversation ended at, and the folder."""
    run = game.out / f"talk-{name}"
    shutil.rmtree(run, ignore_errors=True)
    (run / "steps").mkdir(parents=True)
    mods = shutil.copytree(game.out / "game" / "mods" / "packages", run / "mods" / "packages").parent
    frames, waited, ended, n = [], {}, [], 0
    for p, plan in enumerate(plans):
        steps = [("up", "UP")] + list(plan)
        k, press, watch, over, idle = 0, "UP", Wait(), 0, 0
        while True:
            assert idle < 20, f"{name}: nothing to wait on after {steps[k]} in conversation {p + 1}"
            seg = run / f"{n:03d}"
            seg.mkdir()
            (mods / "state.toml").write_text(fax_e2e.features_toml({PACKAGE: False} if original else {}))
            start = struct.unpack_from("<I", Path(state).read_bytes(), 28)[0]
            schedule = [(1, press), (1 + HOLD, "-")] if press else [(0, "-")]
            (seg / "input.txt").write_text("".join(f"{start + at} {b}\n" for at, b in schedule))
            after = run / f"{n:03d}.cycstate"
            done = subprocess.run([str(game.exe), str(game.rom), "--load-state", str(state), "--input",
                                   str(seg / "input.txt"), "--frames", str(start + SEGMENT), "--frame-log",
                                   str(seg / "frames.bin"), "--frame-log-frames", f"{start}:{start + SEGMENT - 1}",
                                   "--mods-root", str(mods), "--no-save", "--present-out", str(seg / "p.png"),
                                   "--present-every", "1", "--save-state", f"{start + SEGMENT - 1}:{after}"],
                                  capture_output=True, text=True)
            assert done.returncode == 0, done.stdout + done.stderr
            if press:
                watch.pressed(len(frames) + 1)
                if k == 0:
                    waited[(p, 0)] = len(frames) + 1
            press, found, finished = None, None, None
            for number, ram_, oam in read_log(seg / "frames.bin"):
                f = Frame(number, ram_, oam, seg / f"p_{number:05d}.png")
                i = len(frames)
                frames.append(f)
                watch.see(i, f)
                if (p, 0) in waited and waited[(p, 0)] == i:
                    shutil.copy(f.path, run / "steps" / f"{p}-0.png")
                if found is None and k + 1 < len(steps) and watch.ready(steps[k + 1][0], i, f):
                    found = i
                    waited[(p, k + 1)] = i
                    shutil.copy(f.path, run / "steps" / f"{p}-{k + 1}.png")
                if k + 1 == len(steps) and finished is None:
                    over = over + 1 if f.ram[PORTRAIT] == NO_PORTRAIT and f.ram[TEXTBOX_CLOSED] else 0
                    if over >= 20:
                        finished = i
                        shutil.copy(f.path, run / "steps" / f"{p}-end.png")
                f.path = None
            shutil.rmtree(seg)
            if n:
                (run / f"{n - 1:03d}.cycstate").unlink()
            state, n = after, n + 1
            if found is not None:
                k, press, idle = k + 1, steps[k + 1][1], 0
            else:
                idle += 1
            if finished is not None:
                ended.append(len(frames) - 1)
                break
    return frames, waited, ended, run


def check_flashes(name, frames, ended):
    """Every frame is held to what the original shows (the module's
    docstring): the portrait stays from its first frame to the end, an empty
    dialogue box gives way only to a line in it, and a list opens only over a
    dialogue box with a line in it. The corners are compared with the
    dialogue box's own: what its corner cells show most often while it is the
    box open (TextBox_X to _Height, TextBox_Closed 0)."""
    seen = Counter(f.cells["dialogue"] for f in frames if f.ram[PORTRAIT] != NO_PORTRAIT and
                   f.box == DIALOGUE_GEOMETRY and not f.ram[TEXTBOX_CLOSED])
    assert seen, f"{name}: no dialogue box"
    top_left, bottom_right = seen.most_common(1)[0][0]
    up = lambda f, box: f.cells[box] == (top_left, bottom_right)
    list_top = lambda f: f.cells["list"][0] == top_left
    begun = 0
    for end in ended:                                                 # each conversation on its own
        part = frames[begun:end + 1]
        shown = [i for i, f in enumerate(part) if up(f, "portrait")]
        assert shown, f"{name}: no portrait"
        gaps = [part[i].number for i in range(shown[0], shown[-1] + 1) if not up(part[i], "portrait")]
        assert not gaps, f"{name}: the portrait is gone, then back, at frames {gaps[:10]}"
        assert all(f.ram[PORTRAIT] == NO_PORTRAIT for f in part[shown[-1] + 1:]), \
            f"{name}: the portrait went before the conversation ended"
        i = 0
        while i < len(part):
            if up(part[i], "dialogue") and part[i].blank and not list_top(part[i]):
                j = i
                while j < len(part) and up(part[j], "dialogue") and part[j].blank and not list_top(part[j]):
                    j += 1
                assert j < len(part) and up(part[j], "dialogue") and not part[j].blank, \
                    f"{name}: an empty dialogue box (frames {part[i].number}-{part[j - 1].number}) gives way to " \
                    f"{'nothing' if j >= len(part) else 'no line'} at frame {part[min(j, len(part) - 1)].number}"
                i = j
            i += 1
        # The list's corner shows in its colors a few frames after its top row
        # (TextBox_Open writes the attributes last), as its rows cover the box.
        for i in range(1, len(part)):
            if list_top(part[i]) and not list_top(part[i - 1]):
                g = next((j for j in range(i - 1, max(i - 12, 0) - 1, -1) if up(part[j], "dialogue")), None)
                assert g is not None and not part[g].blank, \
                    f"{name}: the list opens at frame {part[i].number} over no line"
        begun = end + 1


def lines(frames):
    """The lines loaded, in order (MessageID as each loads), after the first
    frame's (which an earlier line may have left)."""
    out, last = [], frames[0].message
    for f in frames:
        if f.message and f.message != last:
            out.append(f.message)
        last = f.message
    return out


def keep(game, run, step, label):
    """Keep a step's picture (converse()'s, `step` its "plan-step" name) as
    a screenshot: OUT/NN-label.png."""
    path = game.out / f"{len(game.shots):02d}-{label}.png"
    shutil.copy(run / "steps" / f"{step}.png", path)
    game.shots.append(path.name)
    return path


def main():
    args = parse_args(__doc__, parts=PARTS)
    game = Game.from_args(args)
    out = game.out

    def game_up():
        """The game with the remaster on: the one running, if it is."""
        if not game.running():
            launch()

    def launch(on=True, at=None):
        game.set_features({PACKAGE: on})
        game.launch(at=at)
        assert shops()["enabled"] == on, shops()

    def go_in(source, name, screen, door, writes):
        """From inside a building, walk out onto Eolis's door `door` (block YX)
        on `screen`, and go in."""
        target = out / f"{name}.cycstate"
        fixture(source, target, {AREA_SAVED_AREA: 0, AREA_SAVED_SCREEN: screen, PLAYER_SAVED_POS: door, **writes})
        tcp("load_state", path=str(target))
        into_eolis(screen, door)

    def list_up(mode, what=None):
        """Wait until the `mode` list is on screen and has stopped redrawing."""
        until(what or f"the {mode} list", lambda: (lambda s: s["shown"] == mode and s["list_up"])(shops()), 900)
        waits = shops()["waits"]
        until("the list to wait for a button", lambda: shops()["waits"] > waits, 300)
        wait(2)
        state = shops()
        assert state["shown"] == mode and state["list_up"], state
        return state

    def press(button, mode=None):
        """One press in a list; with `mode`, wait for that list again."""
        act(button, 4, 6)
        return list_up(mode) if mode else None

    def buy(expect_cursor):
        before = shops()["bought"]
        act("a", 4, 6)
        until("the purchase", lambda: shops()["bought"] == before + 1, 1200)
        state = list_up("buy")
        assert state["cursor"] == expect_cursor, state
        return state

    def sell():
        before = shops()["sold"]
        act("a", 4, 6)
        until("the sale", lambda: shops()["sold"] == before + 1, 2400)
        return list_up("sell")

    def into_tool_shop(screen=FOREPAW, building=FOREPAW_TOOL_SHOP):
        """From a town's street, where the tool shop's door put the hero, go in
        and to the counter. The townsfolk are taken off the screen first, so
        none stands at the door: Up on one would talk to him."""
        until("the town", lambda: (ram(AREA), ram(SCREEN)) == (TOWNS, screen) and ram(SCREEN_READY) == 0, 600)
        wait(30)
        game.poke({SPRITE_ENTITIES + i: 0xFF for i in range(8)})
        wait(4)
        act("up", 4, 10)
        until("the tool shop", lambda: (ram(AREA), ram(SCREEN)) == (BUILDINGS, building))
        wait(40)
        to_counter()

    def to_tool_shop(source, name, writes, mascon=False):
        """From inside a building, walk out onto Forepaw's tool shop door (the
        towns' screen 2, block $9D), or Mascon's (screen 4, block $9E), and go in."""
        target = out / f"{name}.cycstate"
        screen, door = (MASCON, MASCON_TOOLS_DOOR) if mascon else (FOREPAW, FOREPAW_TOOLS_DOOR)
        fixture(source, target, {AREA_SAVED_AREA: TOWNS, AREA_SAVED_SCREEN: screen, AREA_SAVED_PALETTE: PALETTE_TOWN,
                                 PLAYER_SAVED_POS: door, AREA_REGION: REGION_MIST if mascon else TRUNK, **writes})
        tcp("load_state", path=str(target))
        walk_out()
        into_tool_shop(*((MASCON, MASCON_TOOL_SHOP) if mascon else ()))

    def leave(thanked):
        """B in the list (shop_leave()): the end, after "Thank you for
        shopping." (screenshot, then A) if anything was bought or sold this
        visit."""
        assert shop_leave() == thanked, shops()
        path = None
        if thanked:
            assert ram(MESSAGE_ID) == 0 and shops()["step"] == "thanks", shops()
            path = game.shot("thanks")
            fax_e2e.page_through("the conversation's end", lambda: ram(PORTRAIT) == NO_PORTRAIT and
                                 ram(TEXTBOX_CLOSED) != 0)
        assert ram(PORTRAIT) == NO_PORTRAIT, ram(PORTRAIT)
        return path

    def a_does_nothing(row):
        """A on the owned item at `row`: nothing bought, no line, the list as it was."""
        before, money, carried = shops(), gold(), items()
        act("a", 4, 60)
        state = shops()
        assert state["ignored"] == before["ignored"] + 1, state
        assert (state["bought"], state["messages"]) == (before["bought"], before["messages"]), state
        assert state["shown"] == "buy" and state["list_up"] and state["cursor"] == row, state
        assert state["hint"] == "Already owned" and state["list"] == before["list"], state
        assert gold() == money and items() == carried, (gold(), items())
        return state

    def speaker_on_list():
        state = tcp("speaker_state")
        assert state["shown"] and (state["col"], state["row"]) == (10, 5), state

    def talk(name, kit, plans, original=False):
        """The conversations of `plans` (converse()) from Eolis's tool shop's
        counter with `kit`, checked for flashes: their frames, where each step
        waited, where each ended, and the RAM after the last frame."""
        start = out / f"{name}.cycstate"
        fixture(counter, start, kit)
        frames, waited, ended, run = converse(game, name, start, plans, original)
        check_flashes(name, frames, ended)
        return frames, waited, ended, run

    try:
        start = game.state("eolis-meat-shop")
        counter = game.state("eolis-tool-counter")

        if args.wants("talk-buy"):
            kit = carrying(900, [], [], GIANT_BLADE)
            plan = [("greeting", "A"), ("menu", "A"), ("line", "A"), ("list", "A"), ("list", "DOWN"), ("still", "A"),
                    ("list", "B"), ("thanks", "A")]
            frames, waited, ended, run = talk("buy", kit, [plan])
            names = ["up", "greeting", "menu", "line", "list", "bought-dagger", "on-deluge", "bought-deluge",
                     "thanks"]
            for k, label in enumerate(names):
                keep(game, run, f"0-{k}", f"buy-{k}-{label}")
            keep(game, run, "0-end", "buy-9-end")
            said = lines(frames)
            assert said == [I_SELL_TOOLS, BUY_OR_SELL, WOULD_LIKE, THANK_YOU], [hex(m) for m in said]
            dagger, deluge = frames[waited[(0, 5)]], frames[waited[(0, 7)]]
            assert dagger.ram[MENU_CURSOR] == 0 and deluge.ram[MENU_CURSOR] == 1, (dagger.ram[MENU_CURSOR],
                                                                                  deluge.ram[MENU_CURSOR])
            last = frames[-1].ram
            money = last[GOLD] | last[GOLD + 1] << 8 | last[GOLD + 2] << 16
            assert money == 900 - 400 - 400, money
            assert (last[NUMBER_OF_WEAPONS], last[WEAPONS], last[NUMBER_OF_MAGIC], last[MAGIC]) == \
                (1, HAND_DAGGER, 1, DELUGE & 0x1F), bytes(last[WEAPONS:WEAPONS + 16]).hex()
            print(f"ok: headless, frame exact ({len(frames)} frames): Up, his greeting, Buy or Sell, \"What would "
                  "you like?\", the list; two purchases in a row, each back in the list with the cursor where it "
                  "was; B, \"Thank you for shopping.\", the end; the portrait in every frame, no empty box but "
                  "before a line, the list only over its line")
            shutil.rmtree(run)

        if args.wants("talk-sell"):
            kit = carrying(0, [], [RED_POTION] * 3, GIANT_BLADE)
            plan = [("greeting", "A"), ("menu", "DOWN"), ("menu", "A"), ("line", "A"), ("list", "A"), ("list", "A"),
                    ("list", "B"), ("thanks", "A")]
            frames, waited, ended, run = talk("sell", kit, [plan])
            names = ["up", "greeting", "menu", "menu-sell", "line", "list", "sold", "sold-again", "thanks"]
            for k, label in enumerate(names):
                keep(game, run, f"0-{k}", f"sell-{k}-{label}")
            keep(game, run, "0-end", "sell-9-end")
            said = lines(frames)
            assert said == [I_SELL_TOOLS, BUY_OR_SELL, WHICH_ONE, THANK_YOU], [hex(m) for m in said]
            last = frames[-1].ram
            money = last[GOLD] | last[GOLD + 1] << 8 | last[GOLD + 2] << 16
            assert money == 2 * 80 and (last[NUMBER_OF_ITEMS], last[ITEMS]) == (1, RED_POTION), \
                (money, last[NUMBER_OF_ITEMS])
            print(f"ok: headless ({len(frames)} frames): Down on the menu, \"Which one would like to sell?\", two Red "
                  "Potions sold in a row, B, the thanks; no flash")
            shutil.rmtree(run)

        if args.wants("talk-message"):
            kit = carrying(300, [], [], GIANT_BLADE)
            refused = [("greeting", "A"), ("menu", "A"), ("line", "A"), ("list", "A"), ("message", "A"),
                       ("list", "B")]
            quiet = [("greeting", "A"), ("menu", "A"), ("line", "A"), ("list", "B")]
            frames, waited, ended, run = talk("message", kit, [refused, quiet])
            for k, label in enumerate(["up", "greeting", "menu", "line", "list", "not-enough", "back"]):
                keep(game, run, f"0-{k}", f"message-{k}-{label}")
            keep(game, run, "0-end", "message-7-end-no-thanks")
            first = lines(frames[:ended[0] + 1])
            assert first == [I_SELL_TOOLS, BUY_OR_SELL, WOULD_LIKE, NOT_ENOUGH_GOLDS], [hex(m) for m in first]
            back = frames[waited[(0, 6)]]
            assert back.ram[MENU_CURSOR] == 0 and back.box == LIST_GEOMETRY, back.ram[MENU_CURSOR]
            second = lines(frames[ended[0] + 1:])
            assert second == [I_SELL_TOOLS, BUY_OR_SELL, WOULD_LIKE], [hex(m) for m in second]
            keep(game, run, "1-4", "quiet-list")
            keep(game, run, "1-end", "quiet-end")
            last = frames[-1].ram
            assert last[GOLD] | last[GOLD + 1] << 8 == 300 and last[NUMBER_OF_WEAPONS] == 0, bytes(last[GOLD:GOLD + 3])
            print("ok: headless: \"This is not enough Golds.\" goes back to the list, its line not said again; B, "
                  "nothing bought, ends with no thanks; and B in the list at once ends quietly; no flash")
            shutil.rmtree(run)

        if args.wants("talk-original"):
            # The original's counter, walked to with the remaster off (the stock view).
            launch(on=False)
            game.load(start)
            game.poke({AREA_SAVED_AREA: 0, AREA_SAVED_SCREEN: EOLIS_TOOLS_SCREEN, PLAYER_SAVED_POS: EOLIS_TOOLS_DOOR})
            into_eolis(EOLIS_TOOLS_SCREEN, EOLIS_TOOLS_DOOR)
            to_counter()
            until("the counter", lambda: ram(PLAYER_X) <= 80, 120)
            wait(10)
            counter = game.save("original-counter")
            game.close()
            kit = carrying(900, [], [], GIANT_BLADE)
            plan = [("greeting", "A"), ("menu", "A"), ("line", "A"), ("list", "A"), ("thanks", "A")]
            frames, waited, ended, run = talk("original", kit, [plan], original=True)
            assert frames[0].width == 256, "not the stock view"
            for k, label in enumerate(["up", "greeting", "menu", "line", "list", "thanks"]):
                keep(game, run, f"0-{k}", f"original-{k}-{label}")
            said = lines(frames)
            assert said == [I_SELL_TOOLS, BUY_OR_SELL, WOULD_LIKE, THANK_YOU], [hex(m) for m in said]
            print("ok: with the remaster off, the original's conversation (one purchase, then his thanks and the "
                  "end) keeps the same rules: no flash")
            shutil.rmtree(run)

        if args.wants("tool-shop"):
            game_up()
            # Eolis's tool shop (building 4: screen 3, block $9A).
            go_in(start, "tool-shop", EOLIS_TOOLS_SCREEN, EOLIS_TOOLS_DOOR, TOOL_KIT)
            to_counter()
            state = shop_list("buy")
            assert state["list"] == [HAND_DAGGER, DELUGE] and state["dropped"] == 2, state   # the Red Potion, the Elixir
            assert state["prices"] == [400, 400] and state["cursor"] == 0, state
            assert state["hint"] is None and state["page_label"] is None and state["buy_line"], state
            speaker_on_list()
            check_hint(game.shot("buy-list"), None)
            print("ok: Up, his greeting, Buy, \"What would you like?\": the buy list, no hint, under the Tool "
                  "Seller's name")

            state = buy(0)
            assert gold() == 700 - 400 and weapons() == [DRAGON_SLAYER, LONG_SWORD, HAND_DAGGER], (gold(), weapons())
            assert state["owned"] == [False, False] and state["traded"], state
            assert items() == [KEY_J, KEY_A, WING_BOOTS] + [RED_POTION] * 2, items()
            print("ok: A buys the Hand Dagger, the list staying up and the cursor on it")

            press("down")
            assert shops()["cursor"] == 1, shops()
            act("a", 4, 6)
            until("This is not enough Golds.", lambda: ram(MESSAGE_ID) == NOT_ENOUGH_GOLDS, 600)
            type_out("the line to type out", 600, pauses=False)
            game.shot("not-enough-golds")
            assert gold() == 300 and not shops()["list_up"], (gold(), shops())
            act("a", 4, 6)
            state = list_up("buy", "the buy list, after the message")
            assert state["cursor"] == 1 and state["messages"] == 1, state
            print("ok: the Deluge is not enough Golds, in the game's words, and A goes back to the list")

            leave(thanked=True)
            print("ok: B, after buying: \"Thank you for shopping.\", then the end")

            state = shop_list("sell")
            assert state["list"] == [ITEM | RED_POTION] * 2 + [ITEM | WING_BOOTS, ITEM | KEY_A, ITEM | KEY_J,
                                                                HAND_DAGGER, LONG_SWORD, DRAGON_SLAYER], state
            assert state["prices"] == [80, 80, 0, 0, 0, 200, 800, 0], state
            assert state["hint"] is None and state["pages"] == 1 and state["page_label"] is None, state
            assert state["sell_line"] and not state["traded"], state
            speaker_on_list()
            path = game.shot("sell-list")
            check_hint(path, None)
            check_priced(path, 0, 80)
            check_priced(path, DAGGER_ROW, 200)
            check_priced(path, LONG_SWORD_ROW, 800)
            for row in (BOOTS_SELL_ROW, KEY_A_ROW, KEY_J_ROW, SLAYER_ROW):
                check_no_price(path, row)
            print("ok: Down and A on the menu, \"Which one would like to sell?\": everything carried, at half a "
                  "shop's price; the Wing Boots, the keys and the Dragon Slayer with no price")

            def not_for_sale(row, name):
                """The cursor on `row`: "Not for sale", and A there sells nothing."""
                while shops()["cursor"] != row:
                    press("down")
                state = shops()
                assert state["hint"] == "Not for sale", state
                path = game.shot(f"{name}-not-for-sale")
                check_hint(path, "Not for sale")
                check_no_price(path, row)
                before, money, carried, kit = state, gold(), items(), weapons()
                act("a", 4, 30)
                state = shops()
                assert state["refused"] == before["refused"] + 1 and state["sold"] == before["sold"], state
                assert state["list_up"] and state["shown"] == "sell" and state["cursor"] == row, state
                assert state["list"] == before["list"] and state["hint"] == "Not for sale", state
                assert (gold(), items(), weapons()) == (money, carried, kit), (gold(), items(), weapons())

            not_for_sale(BOOTS_SELL_ROW, "wing-boots")
            not_for_sale(KEY_J_ROW, "j-key")
            print("ok: the Wing Boots and the J key, his for good, are not for sale, and A on them keeps them")
            not_for_sale(SLAYER_ROW, "dragon-slayer")
            game.shot("dragon-slayer-kept")
            print("ok: the Dragon Slayer is not for sale, and A on it keeps it")

            press("down")                                       # one page: Down from the bottom wraps to the top
            assert shops()["cursor"] == 0, shops()
            sell()
            assert gold() == 300 + 80, gold()
            assert items() == [KEY_J, KEY_A, WING_BOOTS, RED_POTION], items()
            print("ok: a Red Potion sells for 80, half this shop's price")
            leave(thanked=True)
            print("ok: B, after selling: the thanks, then the end")

        if args.wants("key-shop"):
            game_up()
            # The key shop next door (building 3: screen 3, block $9D): the same.
            go_in(start, "key-shop", 3, 0x9D, carrying(0, [], [RED_POTION]))
            to_counter(stop=80)                                 # he stands at block $84, x 64
            state = shop_list("buy")
            assert state["list"] == [ITEM | KEY_J] and state["prices"] == [100], state
            leave(thanked=False)
            state = shop_list("sell")
            assert state["list"] == [ITEM | RED_POTION] and state["prices"] == [80], state
            game.shot("key-shop-sell-list")
            leave(thanked=False)
            print("ok: the Key Seller talks the same way, and offers 80 for a Red Potion, a tool shop's half; B "
                  "without a sale ends at once")

        if args.wants("two-pages"):
            game_up()
            # Two pages: 8 items, and the Long Sword, the Giant Blade and the Dragon Slayer.
            many = [KEY_J, KEY_Q, KEY_K, MATTOCK, RED_POTION, RED_POTION, KEY_A, RED_POTION]
            go_in(start, "two-pages", EOLIS_TOOLS_SCREEN, EOLIS_TOOLS_DOOR,
                  carrying(100, [LONG_SWORD, GIANT_BLADE, DRAGON_SLAYER], many))
            to_counter()
            state = shop_list("sell")
            assert state["pages"] == 2 and state["page"] == 0 and len(state["list"]) == 8, state
            assert state["list"] == [ITEM | v for v in reversed(many)], state
            assert state["prices"] == [80, 0, 80, 80, 0, 0, 0, 0], state       # the keys and the Mattock: his for good
            assert state["hint"] is None and state["page_label"] == "1 of 2", state
            check_hint(game.shot("sell-page-1"), None, "1 of 2")
            for _ in range(7):
                press("down")
            state = press("down", "sell")
            until("the second page", lambda: shops()["page"] == 1, 300)
            state = list_up("sell")
            assert state["list"] == [DRAGON_SLAYER, GIANT_BLADE, LONG_SWORD] and state["cursor"] == 0, state
            assert state["prices"] == [0, 4250, 800] and state["hint"] == "Not for sale", state
            assert state["page_label"] == "2 of 2", state
            path = game.shot("sell-page-2")
            check_hint(path, "Not for sale", "2 of 2")
            check_no_price(path, 0)
            press("down")
            state = sell()
            assert gold() == 100 + 4250 and weapons() == [LONG_SWORD, DRAGON_SLAYER], (gold(), weapons())
            assert state["page"] == 1 and state["list"] == [DRAGON_SLAYER, LONG_SWORD], state
            print("ok: eleven things are two pages, Down from the bottom turns the page, and a sale keeps it")
            leave(thanked=True)

        if args.wants("last-one"):
            game_up()
            # The last thing to sell: then the list is empty, and the shop says so.
            go_in(start, "last-one", EOLIS_TOOLS_SCREEN, EOLIS_TOOLS_DOOR, carrying(0, [], [RED_POTION]))
            to_counter()
            state = shop_list("sell")
            assert state["list"] == [ITEM | RED_POTION], state
            before = state["sold"]
            act("a", 4, 6)
            until("the sale", lambda: shops()["sold"] == before + 1, 1200)
            until("You have nothing to buy.", lambda: ram(MESSAGE_ID) == NOTHING_TO_SELL, 600)
            type_out("the line to type out", 600, pauses=False)
            assert gold() == 80 and items() == [], (gold(), items())
            game.shot("nothing-to-sell")
            act("a", 4, 6)
            until("What would you like?", lambda: ram(MESSAGE_ID) == WOULD_LIKE, 600)
            type_out("the buy list's line", 600, pauses=False)
            game.shot("then-buying")
            act("a", 4, 6)
            list_up("buy", "the buy list, with nothing to sell")
            print("ok: selling the last thing gives the game's own line, then \"What would you like?\" and buying")
            leave(thanked=True)

        if args.wants("owned-mascon"):
            game_up()
            # Owned: Mascon's tool shop. Buy the Mattock.
            to_tool_shop(start, "mascon", carrying(9000, [], [RED_POTION]), mascon=True)
            state = shop_list("buy")
            assert state["list"] == MASCON_LIST and state["prices"] == MASCON_PRICES, state
            assert state["owned"] == [False] * 3, state
            for _ in range(MATTOCK_ROW):
                press("down")
            state = shops()
            assert state["cursor"] == MATTOCK_ROW and state["hint"] is None, state
            path = game.shot("owned-before")
            check_hint(path, None)
            check_priced(path, MATTOCK_ROW, 800)
            state = buy(MATTOCK_ROW)
            assert gold() == 9000 - 800 and items() == [RED_POTION, MATTOCK], (gold(), items())
            assert state["owned"] == [False] * 2 + [True] and state["hint"] == "Already owned", state
            print("ok: Mascon sells the Mattock, and once bought it is owned at once")

            # Leave, and come back: the row says OWNED, A does nothing, and Thunder still buys (a first
            # shield would bring Block's line at the end).
            leave(thanked=True)
            walk_out()
            into_tool_shop(MASCON, MASCON_TOOL_SHOP)
            state = shop_list("buy")
            assert state["list"] == MASCON_LIST and state["prices"] == MASCON_PRICES, state
            assert state["owned"] == [False] * 2 + [True] and state["cursor"] == 0, state
            assert state["hint"] is None, state
            path = game.shot("owned-after")
            check_hint(path, None)
            check_owned(path, MATTOCK_ROW)
            check_priced(path, THUNDER_ROW, 1200)
            for _ in range(MATTOCK_ROW):
                press("down")
            assert shops()["hint"] == "Already owned", shops()
            path = game.shot("owned-hint")
            check_hint(path, "Already owned")
            check_owned(path, MATTOCK_ROW)
            a_does_nothing(MATTOCK_ROW)
            check_owned(game.shot("owned-a-does-nothing"), MATTOCK_ROW)
            for _ in range(MATTOCK_ROW - THUNDER_ROW):
                press("up")
            buy(THUNDER_ROW)
            assert gold() == 9000 - 800 - 1200 and items() == [RED_POTION, MATTOCK], (gold(), items())
            print("ok: back again, the Mattock says OWNED and \"Already owned\", A on it does nothing, "
                  "and Thunder still buys")
            leave(thanked=True)

        if args.wants("owned-found"):
            game_up()
            # A Mattock found in the world, Trunk $0E's (one visit in four), is owned the same.
            target = out / "find-mattock.cycstate"
            fixture(start, target, {AREA_SAVED_AREA: TRUNK, AREA_SAVED_SCREEN: 0x0E, AREA_SAVED_PALETTE: PALETTE_TOWER,
                                    PLAYER_SAVED_POS: ABOVE_MATTOCK, AREA_REGION: TRUNK, VISIT_COUNT: 3,
                                    DURATION_OINTMENT: 0x7F, **carrying(9000, [], [RED_POTION])})
            tcp("load_state", path=str(target))
            walk_out()
            until("Trunk $0E", lambda: (ram(AREA), ram(SCREEN)) == (TRUNK, 0x0E) and ram(SCREEN_READY) == 0, 600)
            wait(30)                                                    # its sprites in their slots
            slots = ram(SPRITE_ENTITIES, 8)
            gone = {SPRITE_ENTITIES + i: 0xFF for i in range(8)                    # it shows once they are gone:
                    if slots[i] < 0x48 and not 0x34 <= slots[i] <= 0x45}            # the enemies, not the people
            game.poke({**gone, PLAYER_X: (ABOVE_MATTOCK & 15) * 16})  # where he walked out: standing on it
            until("the find", lambda: ram(MESSAGE_ID) == GOT_MATTOCK, 600)
            fax_e2e.page_through("the box to close", lambda: ram(MESSAGE_ID) == 0 and ram(TEXTBOX_CLOSED) != 0)
            wait(10)
            found = items()
            assert found == [RED_POTION, MATTOCK], found
            to_tool_shop(start, "mascon-found", carrying(9000, [], found), mascon=True)
            state = shop_list("buy")
            assert state["owned"] == [False] * 2 + [True], state
            for _ in range(MATTOCK_ROW):
                press("down")
            check_owned(game.shot("owned-found"), MATTOCK_ROW)
            a_does_nothing(MATTOCK_ROW)
            print("ok: a Mattock found in the world is owned the same: OWNED, and A does nothing")
            leave(thanked=False)

        if args.wants("owned-key"):
            game_up()
            # A J key held, at Eolis's key shop.
            go_in(start, "key-owned", 3, 0x9D, carrying(1000, [], [KEY_J]))
            to_counter(stop=80)
            state = shop_list("buy")
            assert state["list"] == [ITEM | KEY_J] and state["owned"] == [True], state
            assert state["hint"] == "Already owned", state
            check_owned(game.shot("owned-key"), 0)
            a_does_nothing(0)
            print("ok: a J key held is owned at the key shop, and A does nothing")
            leave(thanked=False)
            state = shop_list("sell")
            assert state["list"] == [ITEM | KEY_J] and state["prices"] == [0], state
            assert state["hint"] == "Not for sale", state
            path = game.shot("key-not-for-sale")
            check_hint(path, "Not for sale")
            check_no_price(path, 0)
            before = shops()
            act("a", 4, 30)
            state = shops()
            assert (state["refused"], state["sold"]) == (before["refused"] + 1, before["sold"]), state
            assert state["shown"] == "sell" and state["list_up"], state
            assert gold() == 1000 and items() == [KEY_J], (gold(), items())
            print("ok: nor does the key shop buy it back: not for sale, and A keeps it")
            leave(thanked=False)

        if args.wants("a-key"):
            game_up()
            # The A key from its giver, the man in the house on Mist $1F, who gives one
            # to a hero with the Black Onyx and none, each time he is asked.
            target = out / "mist-house.cycstate"
            fixture(start, target, {AREA_SAVED_AREA: MIST, AREA_SAVED_SCREEN: MIST_SCREEN,
                                    AREA_SAVED_PALETTE: PALETTE_MIST, PLAYER_SAVED_POS: MIST_HOUSE_DOOR,
                                    AREA_REGION: REGION_MIST, SPECIAL_ITEMS: BLACK_ONYX,
                                    **carrying(0, [], [RED_POTION])},
                    records={ZONE_NAMES: {5: MIST, 6: MIST_SCREEN}})
            tcp("load_state", path=str(target))
            walk_out()
            until("Mist $1F", lambda: (ram(AREA), ram(SCREEN)) == (MIST, MIST_SCREEN) and ram(SCREEN_READY) == 0, 600)
            wait(30)
            game.poke({SPRITE_ENTITIES + i: 0xFF for i in range(8)})     # nothing knocks him off the door
            wait(4)
            column = (MIST_HOUSE_DOOR & 15) * 16
            walk_to(column - 6, column + 4)
            act("up", 4, 10)
            until("the house", lambda: (ram(AREA), ram(SCREEN)) == (BUILDINGS, MIST_HOUSE), 600)
            wait(40)
            to_counter()
            for _ in range(5):
                act("up", 4, 10)
                try:
                    until("his words", lambda: ram(TEXTBOX_CLOSED) == 0, 120)
                    break
                except AssertionError:
                    pass
            else:
                raise AssertionError("the man on Mist $1F did not answer")
            fax_e2e.page_through("the box to close", lambda: ram(TEXTBOX_CLOSED) != 0)
            wait(10)
            assert items() == [RED_POTION, KEY_A], items()
            print("ok: the man on Mist $1F gives the A key to a hero with the Black Onyx")

            # Straight from his house to Forepaw's tool shop, with all he has.
            to_tool_shop(game.save("a-key-given"), "forepaw-a-key", {})
            state = shop_list("sell")
            assert state["list"] == [ITEM | KEY_A, ITEM | RED_POTION] and state["prices"] == [0, 200], state
            assert state["cursor"] == 0 and state["hint"] == "Not for sale", state
            path = game.shot("a-key-not-for-sale")
            check_hint(path, "Not for sale")
            check_no_price(path, 0)
            check_priced(path, 1, 200)
            before = shops()
            act("a", 4, 30)
            state = shops()
            assert (state["refused"], state["sold"]) == (before["refused"] + 1, before["sold"]), state
            assert state["shown"] == "sell" and state["list_up"], state
            assert gold() == 0 and items() == [RED_POTION, KEY_A], (gold(), items())
            press("down")
            sell()
            assert gold() == 200 and items() == [KEY_A], (gold(), items())
            print("ok: a shop won't buy the A key his giver hands out, so he can't be farmed; a Red Potion sells")
            leave(thanked=True)

        if args.wants("original"):
            game.close()
            # The remaster off: the shop is the original's.
            launch(on=False)
            go_in(start, "tool-shop", EOLIS_TOOLS_SCREEN, EOLIS_TOOLS_DOOR, TOOL_KIT)
            to_counter()
            act("up", 4, 6)
            until("the greeting", lambda: ram(MESSAGE_ID) == I_SELL_TOOLS, 600)
            assert not shops()["active"], shops()
            type_out("the greeting", 600, pauses=False)
            act("a", 4, 6)
            until("the Buy/Sell menu's own line", lambda: ram(MESSAGE_ID) == BUY_OR_SELL, 600)
            print("ok: with the remaster off, the Tool Seller greets you and asks Buy or Sell, as the original does")
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
