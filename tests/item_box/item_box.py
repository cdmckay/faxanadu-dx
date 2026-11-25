"""Play the item box's brackets gold while its item runs, end to end, in Seamless.

    python3 tests/item_box/item_box.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/item-box

While the item the status bar's item box shows is running (the Wing Boots
flying, Player_StatusFlag's $80; the Hour Glass freezing, DurationHourGlass
below $80), the box's [ and ] turn gold, and go back to white the frame the
run ends. The checks are on the screenshots' pixels, against the band's left
edge as Seamless laid it out: lit, the brackets' pixels (those bank 10's
tiles $2C-$2F draw) are the gold the feature reports, the rest of their
tiles the backdrop's black, and nothing else in the band is gold; unlit, the
brackets are white and nothing in the band is gold. And on the feature's TCP
state. Each run's end is played headless from a save
state, frame exact, every frame's RAM logged and its presented picture
written (--present-out): the box is lit in every frame the run is on, and in
none from the frame it ends. The setup reuses Recharging
items' end-to-end test (tests/recharge/recharging_items.py): walking out of
a shop whose door was edited to lead where each check is, with RAM edits for
what the hero holds and his MP. Items are used with Down+B and chosen in the
Select menu's ITEM list, as a player would.

1. boots: in Eolis, the Wing Boots selected and the Hour Glass his: unlit.
   Down+B: gold as the flight begins, with no box (#151).
   The Hour Glass chosen in the menu while they fly: the box shows it,
   unlit, and T: still counts the flight. The boots chosen again: lit.
   Down+B standing ends the flight (headless): unlit from the frame it ends.
2. glass: in Eolis, the Hour Glass selected, on 6 MP: lit as the freeze
   begins. The Wing Boots chosen while it freezes: unlit; the Hour Glass
   again: lit. At 0 MP the freeze ends (headless, from 1 MP): unlit from the
   frame it ends.
3. grieve: in King Grieve's room (Dartmoor $15), whose status bar palette is
   the one that differs (sub-palette 0, its green $1A where everywhere else
   has $19): unlit, then lit while the boots fly, the same gold.
4. fade: flying, Up at the door of Eolis's temple (headless): as the screen
   fades out, the status bar's white goes light grey, grey, then black, and
   the brackets' gold goes down with it, in the same frames ($17, $07, then
   black), never brighter than the bar; gold again in the temple.
5. original: the remaster off, the Wing Boots flying (and put back in the box
   by a RAM edit, as the remaster keeps them): the brackets white.
"""
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "recharge"))
from fax_e2e import (Game, act, batch, box_open, menu_row, page_through, parse_args, ram, tcp, until,  # noqa: E402
                     wait)
import recharging_items as recharge  # noqa: E402  (its recipes, its walk out of a shop, and use())

PARTS = ("boots", "glass", "grieve", "fade", "original")
PACKAGE = ("faxanadu.dx", "dx")
BRACKETS = (216, 240)                                      # the [ and ] tiles' left edges, 8 wide, lines 16-31
TILES = ((0x2C, 0x2D), (0x2E, 0x2F))                       # UI_STATUS_TILES' [ and ], top over bottom
HUD_TILES = 16 + 10 * 0x4000 + 0x140                       # HUD_TILES_START, bank 10, in the ROM image
PIXELS = 44                                                # fax_item_box.h: the brackets' white pixels
NATIVE_W, HUD, HUD_X = 256, 32, 92                          # the band's column 0 in Seamless's 424-wide view
GOLD = (232, 173, 0)                                       # $27, as the host draws it
# Through a fade-out (Screen_SetFadeOutPalette), the bar's white as the host draws it, and the brackets' gold:
# $30 or $20 (both white) take $27, $10 $17, $00 $07; black takes nothing.
FADE = {(255, 255, 255): GOLD, (174, 174, 174): (151, 93, 0), (102, 102, 102): (84, 40, 0)}
M_PIXEL = (8, 16)                                          # the "M"'s top left pixel: the bar's white
BLACK, WHITE = (0, 0, 0), (255, 255, 255)
SELECTED_ITEM, ITEM_INVENTORY, NUMBER_OF_ITEMS = 0x3C1, 0x3AD, 0x3C6
MP, PLAYER_STATUS, PLAYER_FLAGS, MESSAGE_ID, UI_PALETTE = 0x39A, 0xA5, 0xA4, 0x213, 0x38D
DURATION_WING_BOOTS, DURATION_HOUR_GLASS = 0x429, 0x42A
SPRITE_ENTITIES, AREA, BUILDINGS = 0x2CC, 0x24, 4
HOUR_GLASS, WING_BOOTS = 0x0D, 0x0F
USED_WING_BOOTS = 0xAD                                     # "I've used Wing Boots.": the original's box
EOLIS, DARTMOOR = 0, 6
TEMPLE_SCREEN, TEMPLE_DOOR = 2, 0x95                       # Eolis's temple door (recharging_items' recipe)
PALETTE_EOLIS, PALETTE_GRIEVE, REGION_DARTMOOR = 0x00, 0x0E, 4
GRIEVE_ROOM, GRIEVE_DOOR = 0x15, 0x91                      # King Grieve's room, where its door brings the hero
GREEN, GREEN_GRIEVE = (23, 158, 0), (0, 166, 0)            # the MP bar's $19, and King Grieve's room's $1A


def png(path):
    """A screenshot's (width, height, rows of RGB bytes). The host writes
    them stored and unfiltered (cyc_png.c), so each row is a slice."""
    data = Path(path).read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos, idat = 8, b""
    while pos < len(data):
        size, kind = struct.unpack_from(">I4s", data, pos)
        body = data[pos + 8:pos + 8 + size]
        if kind == b"IHDR":
            width, height, depth, color = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and color == 2, (depth, color)
        elif kind == b"IDAT":
            idat += body
        pos += 12 + size
    raw, stride = zlib.decompress(idat), width * 3 + 1
    assert all(raw[y * stride] == 0 for y in range(height)), "a filtered row"
    return width, height, [raw[y * stride + 1:(y + 1) * stride] for y in range(height)]


def rgb(value):
    return (value >> 16 & 255, value >> 8 & 255, value & 255)


def bracket_pixels(image):
    """The band's pixels the [ and ] tiles draw (all in color 3, the bar's
    white), from the ROM image: {(x, y)}."""
    out = set()
    for left, pair in zip(BRACKETS, TILES):
        for half, t in enumerate(pair):
            tile = image[HUD_TILES + t * 16:HUD_TILES + t * 16 + 16]
            for r in range(8):
                for c in range(8):
                    if (tile[r] | tile[r + 8]) >> (7 - c) & 1:
                        out.add((left + c, 16 + half * 8 + r))
    return out


BRACKET = set()                                            # bracket_pixels(), once the ROM is read


class Box:
    """A screenshot's item box, against the band's left edge (hud_x, as
    Seamless laid the band out)."""

    def __init__(self, path, hud_x):
        self.path, self.hud_x = path, hud_x
        self.width, self.height, self.rows = png(path)

    def at(self, x, y):
        """The band's pixel (x, y)."""
        x += self.hud_x
        return tuple(self.rows[y][x * 3:x * 3 + 3])

    def brackets(self, color):
        """The brackets' pixels all `color`, and the rest of their two tiles the backdrop's black."""
        return all(self.at(x, y) == (color if (x, y) in BRACKET else BLACK)
                   for left in BRACKETS for x in range(left, left + 8) for y in range(16, 32))

    def gold_elsewhere(self, gold):
        """A pixel of the band outside the brackets' tiles is `gold`."""
        return any(self.at(x, y) == gold for y in range(HUD) for x in range(NATIVE_W)
                   if not (16 <= y < 32 and any(left <= x < left + 8 for left in BRACKETS)))

    def lit(self, gold):
        """The brackets `gold`, their shape as it was, and no gold anywhere else in the band."""
        return self.brackets(gold) and not self.gold_elsewhere(gold)

    def unlit(self, gold):
        """The stock brackets, white, and no gold anywhere in the band."""
        return self.brackets(WHITE) and not self.gold_elsewhere(gold)

    def green(self):
        """The MP bar's fill color, at its first pixel (fax_spell_cost.h: x 24, lines 18-21)."""
        return self.at(24, 19)


def state():
    return tcp("item_box_state")


def running(ram):
    """fax_item_box_running, on a frame's RAM."""
    item = ram[SELECTED_ITEM]
    if item == WING_BOOTS:
        return bool(ram[PLAYER_STATUS] & 0x80)
    return item == HOUR_GLASS and ram[DURATION_HOUR_GLASS] < 0x80


def main():
    args = parse_args(__doc__, parts=PARTS)
    game = Game.from_args(args)
    out = game.out

    def launch(on=True):
        game.close()
        game.set_features({PACKAGE: on})
        game.launch()
        assert state()["enabled"] == on, state()

    def look(name):
        """The picture now and the feature's state, one frame's (the state's
        `lit` is the picture's own present)."""
        path = out / f"{len(game.shots):02d}-{name}.png"
        _, s, world = batch(("screenshot", dict(layer="picture", path=str(path))), ("item_box_state", {}),
                            ("fax_world", {}))
        game.shots.append(path.name)
        box = Box(path, 0)
        box.hud_x = world["hud_x"] if world["enabled"] else max(box.width - NATIVE_W, 0) // 2
        return box, s

    def expect_lit(name, item):
        box, s = look(name)
        gold = rgb(s["gold_rgb"])
        assert s["running"] and s["lit"] and s["selected"] == item, s
        assert s["x"] == box.hud_x + BRACKETS[0] and s["close_x"] == box.hud_x + BRACKETS[1], (s, box.hud_x)
        assert s["pixels"] == PIXELS and s["gold"] == 0x27 and s["white"] == 0x30, s
        assert gold == GOLD and box.lit(gold), f"{name}: the brackets should be {gold}"
        return box, s

    def expect_unlit(name, item):
        box, s = look(name)
        assert not s["running"] and not s["lit"] and s["x"] == -1 and s["pixels"] == 0, s
        assert s["selected"] == item and s["box"], s
        assert box.unlit(GOLD), f"{name}: the brackets should be white, with no gold in the band"
        return box, s

    def warp(name, item, other, mp, area=EOLIS, screen=1, pos=0x99, palette=PALETTE_EOLIS, region=0):
        """Out of the meat shop onto `screen`, `item` selected and `other` his."""
        recharge.warp_out(game, shop, name, area, screen, pos, palette, region,
                          {SELECTED_ITEM: item, ITEM_INVENTORY: other, NUMBER_OF_ITEMS: 1, MP: mp})
        wait(2)

    def use():
        """Down+B, the selected item: it turns on with no box (#151)."""
        recharge.use()
        assert not box_open(), "Down+B shows no box"

    def choose(item):
        """Select, down to ITEM, A, then A on the one item in the list: it goes
        in the box, and the one there into the list. Play is paused while the
        menu is up. Select waits for the hero to stand, and is pressed again
        if the menu did not open, as recharging_items' status_shot() does: with
        no box to page through after Down+B (#151), it comes soon after."""
        before = ram(SELECTED_ITEM)
        until("the hero to stand", lambda: not ram(PLAYER_FLAGS) & 0x05, 120)
        for _ in range(3):
            act("select", 4, 30)
            if box_open():
                break
            wait(30)
        assert box_open(), "the Select menu"
        for _ in range(menu_row("ITEM")):
            act("down", 4, 8)
        act("a", 4, 30)
        act("a", 4, 30)
        until("the menu to close", lambda: not box_open(), 120)
        assert (ram(SELECTED_ITEM), ram(ITEM_INVENTORY)) == (item, before), (ram(SELECTED_ITEM), ram(ITEM_INVENTORY))
        wait(2)

    def replay(name, state, schedule, frames):
        """From save state `state`, `frames` frames headless, holding buttons
        on a schedule of (frame, buttons) from the state's frame: each
        frame's RAM, and a function giving frame i's presented picture."""
        start = struct.unpack_from("<I", Path(state).read_bytes(), 28)[0]   # the frame it continues at
        run = out / f"run-{name}"
        shutil.rmtree(run, ignore_errors=True)
        mods = shutil.copytree(out / "game" / "mods", run / "mods")     # the remaster on, as the window has it
        (run / "input.txt").write_text("".join(f"{start + at} {buttons}\n" for at, buttons in schedule))
        log = run / "frames.bin"
        done = subprocess.run([str(game.exe), str(game.rom), "--load-state", str(state), "--input",
                               str(run / "input.txt"), "--frames", str(start + frames), "--frame-log", str(log),
                               "--frame-log-frames", f"{start}:{start + frames - 1}", "--mods-root", str(mods),
                               "--no-save", "--present-out", str(run / "present.png"), "--present-every", "1"],
                              capture_output=True, text=True)
        assert done.returncode == 0, done.stdout + done.stderr
        data = log.read_bytes()
        assert data[:8] == b"CYCFRAME"
        rams, pos = [], 12
        while pos < len(data):
            head = struct.unpack_from("<9I", data, pos)
            pos += 36 + 32 + 256
            rams.append(data[pos:pos + head[3]])
            pos += head[3] + sum(head[4:9])
        assert len(rams) == frames, (len(rams), frames)

        def picture(i):
            box = Box(run / f"present_{start + i:05d}.png", HUD_X)
            assert box.width == 424, box.width
            return box
        return rams, picture

    def keep(box, name):
        """Keep a replayed frame's picture as a screenshot."""
        path = out / f"{len(game.shots):02d}-{name}.png"
        shutil.copy(box.path, path)
        game.shots.append(path.name)

    def replay_end(name, state, schedule, frames):
        """replay(), in which the run ends: the box is lit in exactly the
        frames its item runs. The frame it ends and the one before are kept as
        screenshots. Returns how many frames it ran lit."""
        rams, picture = replay(name, state, schedule, frames)
        end = None
        for i, ram_i in enumerate(rams):
            box, on = picture(i), running(ram_i)
            assert (box.lit(GOLD) and not box.unlit(GOLD)) if on else (box.unlit(GOLD) and not box.lit(GOLD)), \
                f"{name}: frame {i}: the run {'on' if on else 'over'}, the box {'unlit' if on else 'lit'}"
            if not on and end is None:
                end = i
        assert end and not any(running(r) for r in rams[end:]), f"{name}: the run never ended, or did before it began"
        keep(picture(end - 1), f"{name}-last-lit")
        keep(picture(end), f"{name}-ended")
        shutil.rmtree(out / f"run-{name}")
        return end

    BRACKET.update(bracket_pixels(game.image))
    assert len(BRACKET) == PIXELS, len(BRACKET)
    try:
        shop = game.state("eolis-meat-shop")

        if args.wants("boots"):
            launch()
            warp("boots", WING_BOOTS, HOUR_GLASS, 0x50)
            expect_unlit("eolis-boots-unlit", WING_BOOTS)
            use()
            until("the flight", lambda: ram(PLAYER_STATUS) & 0x80, 30)
            box, s = expect_lit("eolis-boots-lit", WING_BOOTS)
            print(f"ok: the Wing Boots in the box: the stock white [ ] until the flight begins, then gold "
                  f"(${s['gold']:02X}, #{'%02x%02x%02x' % rgb(s['gold_rgb'])}): their {s['pixels']} pixels in the "
                  f"tiles at x {s['x']} and {s['close_x']}, and nothing else")
            choose(HOUR_GLASS)
            box, s = expect_unlit("eolis-glass-shown-boots-flying", HOUR_GLASS)
            assert s["wing_boots"] and not s["hour_glass"] and ram(DURATION_WING_BOOTS) < 0x80, s
            print(f"ok: the Hour Glass chosen while the boots fly: the box shows it, unlit, and T: counts the "
                  f"flight ({ram(DURATION_WING_BOOTS)} s)")
            choose(WING_BOOTS)
            expect_lit("eolis-boots-lit-again", WING_BOOTS)
            until("the hero to stand", lambda: not ram(PLAYER_FLAGS) & 0x05, 120)
            flying = game.save("boots-flying")
            # Down, then B with it: standing, the flight lands at that frame's countdown.
            lit = replay_end("eolis-boots", flying, ((1, "DOWN"), (8, "DOWN+B"), (12, "DOWN"), (16, "-")), 24)
            print(f"ok: Down+B standing ends the flight, and the box is unlit from the frame it ends (headless: "
                  f"lit in each of its first {lit} frames, then unlit in every one)")

        if args.wants("glass"):
            launch()
            warp("glass", HOUR_GLASS, WING_BOOTS, 6)
            expect_unlit("eolis-glass-unlit", HOUR_GLASS)
            use()
            until("the freeze", lambda: ram(DURATION_HOUR_GLASS) < 0x80, 30)
            expect_lit("eolis-glass-lit", HOUR_GLASS)
            choose(WING_BOOTS)
            box, s = expect_unlit("eolis-boots-shown-glass-freezing", WING_BOOTS)
            assert s["hour_glass"] and not s["wing_boots"], s
            choose(HOUR_GLASS)
            expect_lit("eolis-glass-lit-again", HOUR_GLASS)
            print("ok: the Hour Glass in the box lights it while it freezes; the Wing Boots chosen meanwhile, "
                  "unlit; the Hour Glass again, lit")
            until("its last MP", lambda: ram(MP) <= 1, 6 * 24 + 60)
            assert ram(DURATION_HOUR_GLASS) < 0x80, "still freezing on its last MP"
            last = game.save("glass-last-mp")
            lit = replay_end("eolis-glass", last, (), 40)
            print(f"ok: at 0 MP the freeze ends, and the box is unlit from the frame it ends (headless: lit in each "
                  f"of its first {lit} frames, then unlit in every one)")

        if args.wants("grieve"):
            launch()
            warp("grieve", WING_BOOTS, HOUR_GLASS, 0x50, DARTMOOR, GRIEVE_ROOM, GRIEVE_DOOR, PALETTE_GRIEVE,
                 REGION_DARTMOOR)
            game.poke({SPRITE_ENTITIES + i: 0xFF for i in range(8)})   # King Grieve, and his fire
            wait(30)
            assert ram(UI_PALETTE) == 0, ram(UI_PALETTE)
            box, s = expect_unlit("grieve-boots-unlit", WING_BOOTS)
            assert box.green() == GREEN_GRIEVE, box.green()
            use()
            until("the flight", lambda: ram(PLAYER_STATUS) & 0x80, 30)
            box, s = expect_lit("grieve-boots-lit", WING_BOOTS)
            assert box.green() == GREEN_GRIEVE, box.green()
            print(f"ok: in King Grieve's room (status bar sub-palette 0, its green #{'%02x%02x%02x' % GREEN_GRIEVE} "
                  f"where Eolis's is #{'%02x%02x%02x' % GREEN}), the same: white, then gold while the boots fly")

        if args.wants("fade"):
            launch()
            warp("fade", WING_BOOTS, HOUR_GLASS, 0x50, screen=TEMPLE_SCREEN, pos=TEMPLE_DOOR)
            use()
            until("the flight", lambda: ram(PLAYER_STATUS) & 0x80, 30)
            until("the hero to stand", lambda: not ram(PLAYER_FLAGS) & 0x05, 120)
            expect_lit("fade-at-the-door", WING_BOOTS)
            door = game.save("fade-at-the-door")
            rams, picture = replay("fade", door, ((4, "UP"), (24, "-")), 60)
            seen, dark = [], 0
            for i, ram_i in enumerate(rams):
                box = picture(i)
                assert running(ram_i), f"fade: frame {i}: the flight should go on through the door"
                white = box.at(*M_PIXEL)
                if white == BLACK:                             # faded out, or the screen off: nothing lit
                    assert box.brackets(BLACK), f"fade: frame {i}: lit in black"
                    dark += 1
                    continue
                assert white in FADE and box.lit(FADE[white]), (i, white, box.at(220, 20))   # the [ upright
                if not seen or seen[-1] != white:
                    seen.append(white)
                    if white != (255, 255, 255):
                        keep(box, f"fade-{'%02x%02x%02x' % FADE[white]}")
            assert seen == list(FADE) + [(255, 255, 255)] and dark, (seen, dark)
            assert rams[-1][AREA] == BUILDINGS, "in the temple"
            shutil.rmtree(out / "run-fade")
            print(f"ok: through the door's fade the brackets' gold goes down with the status bar's white, in the same "
                  f"frames "
                  f"({', '.join('#%02x%02x%02x' % FADE[w] for w in seen[:3])}), {dark} frames dark, and is lit "
                  "again in the temple")

        if args.wants("original"):
            launch(on=False)
            warp("original", WING_BOOTS, HOUR_GLASS, 0x50)
            recharge.use()
            until("I've used Wing Boots.", lambda: ram(MESSAGE_ID) == USED_WING_BOOTS, 120)
            page_through("the box to close", lambda: not box_open(), 3000)
            until("the flight", lambda: ram(PLAYER_STATUS) & 0x80, 30)
            assert ram(SELECTED_ITEM) == 0xFF, ram(SELECTED_ITEM)     # the original uses the pair up
            game.poke({SELECTED_ITEM: WING_BOOTS})
            wait(2)
            box, s = look("original-boots-flying")
            assert not s["enabled"] and not s["lit"] and s["x"] == -1 and s["wing_boots"], s
            assert box.unlit(GOLD), "the brackets white, no gold"
            print("ok: with the remaster off, the Wing Boots flying (in the box by a RAM edit): the brackets white")
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
