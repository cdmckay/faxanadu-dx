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
dialogue box again before the end, so for 9 frames no box and no portrait
showed, then the portrait came back for 18 and went (its flicker). Both break
the rules above: on master's frames, Up then B, the check finds both.

talk-buy       Two purchases in a row (the Hand Dagger, then Down and a Red
               Potion), each back in the list with the cursor where it was;
               B: "Thank you for shopping.", then the end.
talk-sell      The same for selling: Down on the menu, "Which one would like
               to sell?", two Red Potions sold in a row, B, the thanks.
talk-message   "This is not enough Golds." goes back to the list, without
               "What would you like?" again; B there, nothing bought: the end,
               with no thanks. Then, in another conversation, B in the buy
               list at once ends it quietly too.
talk-owned     OWNED and the hint, frame by frame (#203), with the Giant Blade
               in hand: A buys the Hand Dagger, and from that frame (it is
               his while the gold counts down, then the list is drawn again)
               no frame has a hint until Down moves the cursor; Up, back on
               it: "Already owned". B, the thanks. A second visit, owning it:
               "Already owned" as the list opens; B at once. A third sells
               it, B, the thanks. No frame of the three shows its 400 once
               bought, nor the J key's 0 in the sell list (not for sale): not
               as a list opens or is drawn again, nor as it goes. After B
               the list stays up for frames (IScriptAction_EndScript's
               before its TextBox_Close, Maybe_Shop_DrawTextBox's before
               the dialogue box), its prices under it: on master those
               showed.
talk-original  The remaster off, in the stock view: the original's
               conversation, one purchase and its thanks, held to the same
               rules, which it keeps.

The other parts drive the shops over TCP (fax_e2e's shop_list() goes through
the conversation) and check the lists, their hint line and OWNED in the
picture, and gold and inventory after each step:

tool-shop  Eolis's tool shop (Hand Dagger 400, Red Potion 160, Deluge 400: its
           Elixir is found, not bought, fax_found.c), with 1100 Golds, the
           Giant Blade in hand and, unequipped, the Long Sword and the Dragon
           Slayer, and the J and A keys and the Wing Boots. A buys the Hand
           Dagger (the list stays up, the cursor on it); Down and A twice buy
           two Red Potions, never OWNED; Down and A on the Deluge says "This is
           not enough Golds." and A goes back to the list; B thanks him. Then
           the sell list: everything carried, the Items first, at half a shop's
           price (the Hand Dagger 200, this shop's; the Long Sword 800,
           Forepaw's), a Red Potion 80 as at every shop (#176), and with no
           price the Wing Boots and the keys, his for good, and the Dragon
           Slayer. On those the hint says "Not for sale" and A does nothing;
           Down from the bottom wraps to the top, and a Red Potion sells.
key-shop   Next door, the Key Seller talks the same way, and buys a Red Potion
           for 80, as every shop does.
two-pages  A full inventory (8 items and 3 more weapons) is two pages, the keys
           and the Mattock on the first with no price: Down from the bottom of
           the first turns to the second, and a sale there keeps the page.
last-one   Selling the last item (a Red Potion) empties the list ("You have
           nothing to buy.", the game's own line), and the conversation goes
           on to buying, with "What would you like?".
owned-*    Mascon's tool shop (Large Shield 3500, Thunder 1200, Mattock 800,
           Red Potion 600), with 9000 Golds and a Red Potion: the Mattock is
           priced, and buying it marks it OWNED at once, with no hint yet
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
owned-gear A weapon, armor, shield or spell the hero has is OWNED (#165), at
           Eolis's tool shop with 2000 Golds, the Giant Blade in hand and a
           Long Sword carried: the Hand Dagger, weaker, is for sale; bought,
           its row says OWNED, with no hint (#203), and A on it does nothing
           (no gold, and not the game's "You can't carry any more."), the
           hint then "Already owned". The same for Deluge. B, the thanks;
           in the sell list the dagger
           still sells (200), and back in the buy list it is for sale again,
           and buys. Then, the dagger equipped (in no list): OWNED too, and a
           Red Potion buys twice in a row: a consumable is never owned. (With
           the remaster off, the game's own refusal: unit tests.)
a-key      The A key farm is gone: the man in the house on Mist $1F gives the A
           key to a hero with the Black Onyx and none (each time he is asked),
           and from his house straight to Forepaw's tool shop, with all he has,
           the key is not for sale and A keeps it, while a Red Potion sells for
           80.
dartmoor   Dartmoor's tool shop (the towns' screen $0D, door $9D: Giant Blade
           13000, Red Potion 2000), with 4000 Golds and a Red Potion: two Red
           Potions buy in a row, and its sell list offers each at 80, not half
           its own 2000 (#176): a sale pays 80. Then, with the remaster off and
           headless from its counter (as the talk parts), the original's sell
           list there pays 1000 for one, half its price.
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
EOLIS_LIST, EOLIS_PRICES = [HAND_DAGGER, ITEM | RED_POTION, DELUGE], [400, 160, 400]   # its Elixir: found
POTION_ROW, DELUGE_ROW = 1, 2
MASCON_LIST = [LARGE_SHIELD, THUNDER, ITEM | MATTOCK, ITEM | RED_POTION]
MASCON_PRICES = [3500, 1200, 800, 600]
THUNDER_ROW, MATTOCK_ROW = 1, 2
DARTMOOR, DARTMOOR_TOOLS_DOOR, DARTMOOR_TOOL_SHOP = 0x0D, 0x9D, 0x41  # the towns' screen $0D, its door, building $41
DARTMOOR_LIST, DARTMOOR_PRICES = [GIANT_BLADE, ITEM | RED_POTION], [13000, 2000]
REGION_DARTMOOR = 4
ARG_STRINGS_COUNT, SHOP_ITEMS, SHOP_COSTS_L, SHOP_COSTS_U = 0x21F, 0x220, 0x228, 0x230   # a list as drawn
# The tool shop's sell list: two Red Potions, then the kit's Items from the last, then its weapons from the last.
BOOTS_SELL_ROW, KEY_A_ROW, KEY_J_ROW, DAGGER_ROW, LONG_SWORD_ROW, SLAYER_ROW = 2, 3, 4, 5, 6, 7
SPECIAL_ITEMS, BLACK_ONYX = 0x42C, 0x01                           # SpecialItems, its Black Onyx bit
MIST, MIST_SCREEN, MIST_HOUSE, MIST_HOUSE_DOOR = 2, 0x1F, 0x22, 0x94  # the A key's giver: Buildings $22
PALETTE_MIST, REGION_MIST = 0x0A, 2
ZONE_NAMES = "faxanadu.zone-names"                                  # its record: the area and screen, at 5 and 6
LIST_FRAME = (80, 64)                                               # the list's top-left frame tile, tile 10,8
HINT = (96, 208)                                                    # its hint line: tile 12,26
PRICE_X, PRICE_YS = 184, range(88, 88 + 8 * 16, 16)                 # each entry's price: column 23, its second row
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


TOOL_KIT = carrying(1100, [DRAGON_SLAYER, LONG_SWORD], [KEY_J, KEY_A, WING_BOOTS], GIANT_BLADE)
PARTS = ("talk-buy", "talk-sell", "talk-message", "talk-owned", "talk-original", "tool-shop", "key-shop", "two-pages",
         "last-one", "owned-mascon", "owned-found", "owned-key", "owned-gear", "a-key", "dartmoor",
         "original")                                                                   # each its own game


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
    """One frame of a headless conversation: its RAM, OAM and nametable after
    it, and what its presented picture showed of the boxes (the corner cells
    and whether the dialogue box's lines are empty), of each list entry's
    price cells (a checksum each, and the nametable's tiles there), and
    whether the list's hint row has anything in it. `pressed` is the button
    pressed on it, if any."""

    def __init__(self, number, ram_, oam, picture, ciram=b""):
        self.number, self.ram, self.oam, self.path, self.pressed = number, ram_, oam, picture, None
        p = Picture(picture)
        self.width = p.width
        self.cells = {name: (p.cell(*a), p.cell(*b)) for name, (a, b) in
                      (("portrait", PORTRAIT_BOX), ("dialogue", DIALOGUE_BOX), ("list", LIST_BOX))}
        self.blank = p.blank(TEXT_AREA, INSIDE)
        self.prices = [[zlib.crc32(p.cell(x, y)) for x in range(PRICE_X, PRICE_X + 40, 8)] for y in PRICE_YS]
        self.tiles = [bytes(ciram[y // 8 * 32 + PRICE_X // 8:y // 8 * 32 + PRICE_X // 8 + 5]) for y in PRICE_YS] \
            if ciram else []
        self.hint = not p.blank((*HINT, 128, 8), (HINT[0], PAGE_ROW))

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
    """A --frame-log: (frame, RAM, OAM, CIRAM: the nametables) for each frame."""
    data = Path(path).read_bytes()
    assert data[:8] == b"CYCFRAME"
    out, pos = [], 12
    while pos < len(data):
        head = struct.unpack_from("<9I", data, pos)
        oam = data[pos + 36 + 32:pos + 36 + 32 + 256]
        pos += 36 + 32 + 256
        out.append((head[0], data[pos:pos + head[3]], oam, data[pos + head[3]:pos + head[3] + head[4]]))
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
            pressed = None
            if press:
                pressed = len(frames) + 1
                watch.pressed(pressed)
                if k == 0:
                    waited[(p, 0)] = pressed
            button, press, found, finished = press, None, None, None
            for number, ram_, oam, ciram in read_log(seg / "frames.bin"):
                f = Frame(number, ram_, oam, seg / f"p_{number:05d}.png", ciram)
                if len(frames) == pressed:
                    f.pressed = button
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

    # A town's tool shop: the towns' screen, its door (block YX), the building, and the region it hangs off.
    tool_shops = {"forepaw": (FOREPAW, FOREPAW_TOOLS_DOOR, FOREPAW_TOOL_SHOP, TRUNK),
                  "mascon": (MASCON, MASCON_TOOLS_DOOR, MASCON_TOOL_SHOP, REGION_MIST),
                  "dartmoor": (DARTMOOR, DARTMOOR_TOOLS_DOOR, DARTMOOR_TOOL_SHOP, REGION_DARTMOOR)}

    def to_tool_shop(source, name, writes, town="forepaw"):
        """From inside a building, walk out onto a town's tool shop door
        (tool_shops), and go in."""
        target = out / f"{name}.cycstate"
        screen, door, building, region = tool_shops[town]
        fixture(source, target, {AREA_SAVED_AREA: TOWNS, AREA_SAVED_SCREEN: screen, AREA_SAVED_PALETTE: PALETTE_TOWN,
                                 PLAYER_SAVED_POS: door, AREA_REGION: region, **writes})
        tcp("load_state", path=str(target))
        walk_out()
        into_tool_shop(screen, building)

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

    def talk(name, kit, plans, original=False, at=None):
        """The conversations of `plans` (converse()) from Eolis's tool shop's
        counter (or the counter state `at`) with `kit`, checked for flashes:
        their frames, where each step waited, where each ended, and the RAM
        after the last frame."""
        start = out / f"{name}.cycstate"
        fixture(at or counter, start, kit)
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
            names = ["up", "greeting", "menu", "line", "list", "bought-dagger", "on-potion", "bought-potion",
                     "thanks"]
            for k, label in enumerate(names):
                keep(game, run, f"0-{k}", f"buy-{k}-{label}")
            keep(game, run, "0-end", "buy-9-end")
            said = lines(frames)
            assert said == [I_SELL_TOOLS, BUY_OR_SELL, WOULD_LIKE, THANK_YOU], [hex(m) for m in said]
            dagger, potion = frames[waited[(0, 5)]], frames[waited[(0, 7)]]
            assert dagger.ram[MENU_CURSOR] == 0 and potion.ram[MENU_CURSOR] == POTION_ROW, (dagger.ram[MENU_CURSOR],
                                                                                           potion.ram[MENU_CURSOR])
            last = frames[-1].ram
            money = last[GOLD] | last[GOLD + 1] << 8 | last[GOLD + 2] << 16
            assert money == 900 - 400 - 160, money
            assert (last[NUMBER_OF_WEAPONS], last[WEAPONS], last[NUMBER_OF_ITEMS], last[ITEMS]) == \
                (1, HAND_DAGGER, 1, RED_POTION), bytes(last[WEAPONS:WEAPONS + 20]).hex()
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

        if args.wants("talk-owned"):
            # The Giant Blade in hand, a Long Sword and the J key carried: the Hand Dagger is for sale.
            kit = carrying(2000, [LONG_SWORD], [KEY_J], GIANT_BLADE)
            bought = [("greeting", "A"), ("menu", "A"), ("line", "A"), ("list", "A"), ("list", "DOWN"),
                      ("still", "UP"), ("still", "B"), ("thanks", "A")]
            again = [("greeting", "A"), ("menu", "A"), ("line", "A"), ("list", "B")]
            sold = [("greeting", "A"), ("menu", "DOWN"), ("menu", "A"), ("line", "A"), ("list", "DOWN"),
                    ("still", "A"), ("list", "B"), ("thanks", "A")]
            frames, waited, ended, run = talk("owned", kit, [bought, again, sold])
            visits = [frames[:ended[0] + 1], frames[ended[0] + 1:ended[1] + 1], frames[ended[1] + 1:]]
            for f in frames:
                f.up = f.box == LIST_GEOMETRY and not f.ram[TEXTBOX_CLOSED]

            # 1. A buys the dagger: OWNED at once, and no hint until the cursor moves (Down).
            first = visits[0]
            buy_at = next(i for i, f in enumerate(first) if f.ram[NUMBER_OF_WEAPONS] == 2)
            a_at = max(i for i, f in enumerate(first[:buy_at + 1]) if f.pressed == "A")
            assert buy_at - a_at <= 2, (a_at, buy_at)
            moved = next(i for i, f in enumerate(first) if i > buy_at and f.ram[MENU_CURSOR] != 0)
            assert first[moved].pressed == "DOWN" and first[moved].ram[MENU_CURSOR] == POTION_ROW, moved
            window = first[a_at:moved]
            assert all(f.up for f in window), "the list went before the cursor moved"
            hinted = [f.number for f in window if f.hint]
            assert not hinted, f"a hint after the purchase, before the cursor moved, at frames {hinted[:10]}"
            path = keep(game, run, "0-5", "owned-bought")
            check_owned(path, 0)
            check_hint(path, None)
            check_hint(keep(game, run, "0-6", "owned-moved"), None)
            path = keep(game, run, "0-7", "owned-back")                  # Up: back on the dagger
            check_owned(path, 0)
            check_hint(path, "Already owned")
            print(f"ok: headless, frame exact: the Hand Dagger bought, OWNED at once and no hint in any of the "
                  f"{len(window)} frames until Down; back on it, \"Already owned\"")

            # 2. Owned before this visit: "Already owned" as the list opens.
            path = keep(game, run, "1-4", "owned-before")
            check_owned(path, 0)
            check_hint(path, "Already owned")
            print("ok: owned before the visit, the dagger says \"Already owned\" as soon as the list is up")

            # 3. No frame shows a covered price: the dagger's 400 from its purchase on (the list
            # redrawn, B and the thanks, then a visit owning it, its list opening and B at once), and
            # the J key's 0 in the sell list (a sale, B and the thanks). The digits are the game's
            # tiles ($30 + the digit, UI_DrawDigitsNoLeadingZeroes), as drawn uncovered in the
            # first visit's list before the purchase; where a covered price's tiles are in the
            # nametable, its cells must not show them.
            glyphs = {}
            for before, f in zip(first[:a_at], first[1:a_at]):               # settled: the same tiles two frames
                if f.up and f.drawn and f.cursor and f.tiles == before.tiles:
                    for row in range(3):
                        for k in range(5):
                            if 0x30 <= f.tiles[row][k] <= 0x39:
                                glyphs.setdefault(f.tiles[row][k], f.prices[row][k])
            covered, flashes = [(first[buy_at:], 400), (visits[1], 400), (visits[2], 0)], []
            for n, (part, price) in enumerate(covered):
                digits = [None if c == " " else 0x30 + int(c) for c in str(price).rjust(5)]
                assert all(t in glyphs for t in digits if t), (sorted(glyphs), digits)
                b = next(i for i, f in enumerate(part) if f.pressed == "B")
                held = [f for f in part[b:] if f.up and f.tiles[0] == bytes(t or 0 for t in digits)]
                assert held, f"visit {n + 1}: no frame after B with the list up and the price under it"
                shown = [f.number for f in part if any(t and f.prices[0][k] == glyphs[t] for k, t in enumerate(digits))]
                if shown:
                    flashes.append(f"visit {n + 1}: {price} at frames {shown[:10]} (B at {part[b].number})")
            assert not flashes, "a covered price shows: " + "; ".join(flashes)
            print(f"ok: no frame shows the dagger's 400 once bought, nor the J key's 0 (the sell list's), "
                  f"the lists opening, drawn again and going (B, with and without the thanks)")
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
            assert state["list"] == EOLIS_LIST and state["dropped"] == 1, state         # the Elixir: found
            assert state["prices"] == EOLIS_PRICES and state["cursor"] == 0, state
            assert state["hint"] is None and state["page_label"] is None and state["buy_line"], state
            speaker_on_list()
            path = game.shot("buy-list")
            check_hint(path, None)
            check_priced(path, POTION_ROW, 160)
            print("ok: Up, his greeting, Buy, \"What would you like?\": the buy list, the Red Potion back at 160, no "
                  "hint, under the Tool Seller's name")

            state = buy(0)
            assert gold() == 1100 - 400 and weapons() == [DRAGON_SLAYER, LONG_SWORD, HAND_DAGGER], (gold(), weapons())
            assert state["owned"] == [True, False, False] and state["traded"], state    # his now: OWNED (#165)
            assert items() == [KEY_J, KEY_A, WING_BOOTS], items()
            print("ok: A buys the Hand Dagger, the list staying up and the cursor on it, and it is OWNED")

            press("down")
            for n in (1, 2):
                state = buy(POTION_ROW)
                assert gold() == 700 - n * 160 and items() == [KEY_J, KEY_A, WING_BOOTS] + [RED_POTION] * n, \
                    (gold(), items())
                assert state["owned"] == [True, False, False] and state["hint"] is None, state
            check_priced(game.shot("potions-bought"), POTION_ROW, 160)
            print("ok: Down and A twice buy two Red Potions in a row at 160, the list staying up and the cursor on "
                  "them, never OWNED")

            press("down")
            assert shops()["cursor"] == DELUGE_ROW, shops()
            act("a", 4, 6)
            until("This is not enough Golds.", lambda: ram(MESSAGE_ID) == NOT_ENOUGH_GOLDS, 600)
            type_out("the line to type out", 600, pauses=False)
            game.shot("not-enough-golds")
            assert gold() == 380 and not shops()["list_up"], (gold(), shops())
            act("a", 4, 6)
            state = list_up("buy", "the buy list, after the message")
            assert state["cursor"] == DELUGE_ROW and state["messages"] == 1, state
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
                  "shop's price and a Red Potion at 80; the Wing Boots, the keys and the Dragon Slayer with no price")

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
            assert gold() == 380 + 80, gold()
            assert items() == [KEY_J, KEY_A, WING_BOOTS, RED_POTION], items()
            print("ok: a Red Potion sells for 80, as at every shop")
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
            print("ok: the Key Seller talks the same way, and offers 80 for a Red Potion, as every shop does; B "
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
            to_tool_shop(start, "mascon", carrying(9000, [], [RED_POTION]), "mascon")
            state = shop_list("buy")
            assert state["list"] == MASCON_LIST and state["prices"] == MASCON_PRICES, state
            assert state["owned"] == [False] * 4, state
            for _ in range(MATTOCK_ROW):
                press("down")
            state = shops()
            assert state["cursor"] == MATTOCK_ROW and state["hint"] is None, state
            path = game.shot("owned-before")
            check_hint(path, None)
            check_priced(path, MATTOCK_ROW, 800)
            state = buy(MATTOCK_ROW)
            assert gold() == 9000 - 800 and items() == [RED_POTION, MATTOCK], (gold(), items())
            assert state["owned"] == [False, False, True, False] and state["hint"] is None, state  # just bought
            print("ok: Mascon sells the Mattock, and once bought it is owned at once, with no hint on it yet")

            # Leave, and come back: the row says OWNED, A does nothing, and Thunder still buys (a first
            # shield would bring Block's line at the end).
            leave(thanked=True)
            walk_out()
            into_tool_shop(MASCON, MASCON_TOOL_SHOP)
            state = shop_list("buy")
            assert state["list"] == MASCON_LIST and state["prices"] == MASCON_PRICES, state
            assert state["owned"] == [False, False, True, False] and state["cursor"] == 0, state
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
            to_tool_shop(start, "mascon-found", carrying(9000, [], found), "mascon")
            state = shop_list("buy")
            assert state["owned"] == [False, False, True, False], state
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

        if args.wants("owned-gear"):
            game_up()
            # Eolis's tool shop's counter: the Giant Blade in hand, a Long Sword carried.
            kit = out / "owned-gear.cycstate"
            fixture(counter, kit, carrying(2000, [LONG_SWORD], [], GIANT_BLADE))
            game.load(kit)
            wait(10)
            state = shop_list("buy")
            assert state["list"] == EOLIS_LIST and state["owned"] == [False] * 3, state
            path = game.shot("gear-for-sale")
            check_priced(path, 0, 400)
            check_priced(path, DELUGE_ROW, 400)
            state = buy(0)
            assert gold() == 1600 and weapons() == [LONG_SWORD, HAND_DAGGER], (gold(), weapons())
            assert state["owned"] == [True, False, False] and state["hint"] is None, state   # just bought (#203)
            path = game.shot("dagger-owned")
            check_owned(path, 0)
            check_hint(path, None)
            a_does_nothing(0)
            check_hint(game.shot("dagger-a-again"), "Already owned")
            print("ok: the Hand Dagger, weaker than his Giant Blade and Long Sword, is for sale; bought, it says "
                  "OWNED, with no hint until A on it again, which does nothing (no gold, no line): \"Already owned\"")
            press("down")
            press("down")
            state = buy(DELUGE_ROW)
            spells = lambda: list(ram(MAGIC, 4)[:ram(NUMBER_OF_MAGIC)])
            assert gold() == 1200 and spells() == [DELUGE & 0x1F], (gold(), spells())
            assert state["owned"] == [True, False, True] and state["hint"] is None, state
            a_does_nothing(DELUGE_ROW)
            path = game.shot("deluge-owned")
            check_owned(path, 0)
            check_owned(path, DELUGE_ROW)
            print("ok: Deluge, once learned, says OWNED too, and A on it does nothing")
            leave(thanked=True)

            state = shop_list("sell")
            assert state["list"] == [HAND_DAGGER, LONG_SWORD, DELUGE] and state["prices"] == [200, 800, 200], state
            assert state["hint"] is None, state
            sell()
            assert gold() == 1400 and weapons() == [LONG_SWORD], (gold(), weapons())
            leave(thanked=True)
            state = shop_list("buy")
            assert state["owned"] == [False, False, True], state
            path = game.shot("dagger-for-sale-again")
            check_priced(path, 0, 400)
            check_owned(path, DELUGE_ROW)
            buy(0)
            assert gold() == 1000 and weapons() == [LONG_SWORD, HAND_DAGGER], (gold(), weapons())
            assert shops()["owned"] == [True, False, True], shops()
            print("ok: sold, the Hand Dagger is for sale again, and buys")
            leave(thanked=True)

            # The Hand Dagger equipped (in no list), nothing carried.
            equipped = out / "dagger-equipped.cycstate"
            fixture(counter, equipped, carrying(2000, [], [], HAND_DAGGER))
            game.load(equipped)
            wait(10)
            state = shop_list("buy")
            assert state["owned"] == [True, False, False] and state["hint"] == "Already owned", state
            check_owned(game.shot("dagger-equipped-owned"), 0)
            a_does_nothing(0)
            print("ok: the Hand Dagger in hand is OWNED, and A on it does nothing")

            # A consumable: the Red Potion below it buys twice in a row, and is never owned.
            press("down")
            for n in (1, 2):
                state = buy(POTION_ROW)
                assert items() == [RED_POTION] * n and gold() == 2000 - n * 160, (items(), gold())
                assert state["owned"] == [True, False, False] and state["hint"] is None, state
            print("ok: a Red Potion, a consumable, buys twice in a row")
            leave(thanked=True)

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
            assert state["list"] == [ITEM | KEY_A, ITEM | RED_POTION] and state["prices"] == [0, 80], state
            assert state["cursor"] == 0 and state["hint"] == "Not for sale", state
            path = game.shot("a-key-not-for-sale")
            check_hint(path, "Not for sale")
            check_no_price(path, 0)
            check_priced(path, 1, 80)
            before = shops()
            act("a", 4, 30)
            state = shops()
            assert (state["refused"], state["sold"]) == (before["refused"] + 1, before["sold"]), state
            assert state["shown"] == "sell" and state["list_up"], state
            assert gold() == 0 and items() == [RED_POTION, KEY_A], (gold(), items())
            press("down")
            sell()
            assert gold() == 80 and items() == [KEY_A], (gold(), items())
            print("ok: a shop won't buy the A key his giver hands out, so he can't be farmed; a Red Potion sells")
            leave(thanked=True)

        if args.wants("dartmoor"):
            game_up()
            # Dartmoor's tool shop, which asks 2000 for a Red Potion: the original paid 1000 for one.
            to_tool_shop(start, "dartmoor", carrying(4000, [], [RED_POTION]), "dartmoor")
            state = shop_list("buy")
            assert state["list"] == DARTMOOR_LIST and state["prices"] == DARTMOOR_PRICES, state
            press("down")
            for n in (1, 2):
                buy(1)
                assert gold() == 4000 - n * 2000 and items() == [RED_POTION] * (1 + n), (gold(), items())
            check_priced(game.shot("dartmoor-buy-list"), 1, 2000)
            print("ok: Dartmoor's tool shop sells the Red Potion at its own 2000, two in a row")
            leave(thanked=True)
            state = shop_list("sell")
            assert state["list"] == [ITEM | RED_POTION] * 3 and state["prices"] == [80] * 3, state
            path = game.shot("dartmoor-sell-list")
            check_hint(path, None)
            check_priced(path, 0, 80)
            sell()
            assert gold() == 80 and items() == [RED_POTION] * 2, (gold(), items())
            print("ok: its sell list offers a Red Potion at 80, not half its 2000, and a sale pays 80")
            leave(thanked=True)

            # The remaster off: to its counter, then the original's conversation there, headless.
            game.close()
            launch(on=False)
            to_tool_shop(start, "dartmoor-original", {}, "dartmoor")
            wait(10)
            at = game.save("dartmoor-counter-original")
            game.close()
            plan = [("greeting", "A"), ("menu", "DOWN"), ("menu", "A"), ("line", "A"), ("list", "A"), ("thanks", "A")]
            frames, waited, ended, run = talk("dartmoor-original", carrying(0, [], [RED_POTION], GIANT_BLADE), [plan],
                                              original=True, at=at)
            assert frames[0].width == 256, "not the stock view"
            said = lines(frames)
            assert said == [I_SELL_TOOLS, BUY_OR_SELL, WHICH_ONE, THANK_YOU], [hex(m) for m in said]
            listed = frames[waited[(0, 5)]].ram                         # the sell list, waiting for A
            price = listed[SHOP_COSTS_L] | listed[SHOP_COSTS_U] << 8
            assert (listed[ARG_STRINGS_COUNT], listed[SHOP_ITEMS], price) == (1, ITEM | RED_POTION, 1000), \
                bytes(listed[SHOP_ITEMS:SHOP_COSTS_U + 8]).hex()
            check_priced(keep(game, run, "0-5", "dartmoor-original-sell-list"), 0, 1000)
            last = frames[-1].ram
            money = last[GOLD] | last[GOLD + 1] << 8 | last[GOLD + 2] << 16
            assert money == 1000 and last[NUMBER_OF_ITEMS] == 0, (money, last[NUMBER_OF_ITEMS])
            print("ok: with the remaster off, Dartmoor's tool shop pays 1000 for a Red Potion, as the original does")
            shutil.rmtree(run)

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
