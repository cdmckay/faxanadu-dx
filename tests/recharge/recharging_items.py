"""Play recharging items end to end in a hidden window.

    python3 tests/recharge/recharging_items.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/recharge

The game runs uncapped and the script waits on the game's own state, never on
the clock, on a reserved TCP port (tests/e2e/fax_e2e.py), so any number can
run in parallel.

Copies the build to OUT and starts a new game over TCP. The hero walks through
the Eolis gate into the town and into a shop. Save-state RAM edits then set up
what each scenario needs (external prerequisites, not coverage): the item
selected, HP, MP, gold, the title, where the shop's door leads (Area_Saved*,
so walking out puts the hero on the screen wanted), a screen's enemies
cleared, the random-item visit counter, and for a run going over a Guru
travel journey or a save, and the Elixir spent, the feature's own record.
Items are used with Down+B, as a player would. Both run on MP (#124): a
frame of either is a main-loop pass, a frame the game plays, and the drain
is checked against the frame counter on a quiet screen.

1. Wing Boots by title band: a full bar's flight shows 40, 50, 60 and 80 s on
   T: (the original: 40, 30, 20, 10), and MP falls an MP every 32, 40, 48
   and 64 frames, to the frame.
2. A whole Novice flight from a full bar, in Eolis: T: counts its seconds,
   MP falls on schedule and never comes back while he flies, and at 0 MP,
   2,560 frames on, "The power of the Wing Boots is gone.". Then T: is 00,
   Down+B at 0 MP does nothing but the refusal sound, MP refill waits its
   whole 4 s from the landing, and with MP the same pair flies again at once.
3. Landing early: Down+B in the air (A held, so the game takes Down as a
   climb and allows the item) ends the flight with the game's line, and no
   more MP goes; standing, the same. Then it flies again at once.
4. Dying while flying ends the flight; he comes back with the pair, ready.
5. The Hour Glass on Eolis screen 4: the Zozuras stop, no HP is taken, and
   the freeze takes an MP every 24 frames, T: its seconds while it is
   selected (the original's 00 with another item); after its 15 s, 40 MP
   gone. Again at once, and Down+B ends it early.
6. The Hour Glass on 10 MP: it ends at 0 MP, 240 frames on; then Down+B does
   nothing but the refusal sound.
7. The Hour Glass at Ripasheiku (Trunk $15): the boss keeps moving. Walking
   onto a boss's screen with the Hour Glass running ends it within a second
   (LimitItemDurations), and its MP stops.
8. The Elixir on Eolis screen 4 with 1 HP: a Zozura's hit, and it revives the
   hero ("I've used Elixir."). With 1 HP again, the next hit kills him; he
   respawns with it still spent, and the menu's Player page, which showed it,
   does not. Talking to Eolis's Guru charges it, and says so, and the page
   shows it again.
9. Eolis's tool shop with a spent Elixir: it is still the hero's, so Quick
   shops lists it as owned, "Already owned", and A on it does nothing: the
   gold stays and it stays spent, since buying never charges it (a Guru
   does). Charged, the same.
10. Finds: a pair of Wing Boots (Trunk $2D) and an Hour Glass (Trunk $13)
    the hero holds add nothing, and an Elixir (Trunk $3C) while it is spent
    charges it.
11. The Spring of Sky, the spring the Wing Boots are needed for: from the
    floor of Trunk $20 below the shaft, fly up through $23 and $24 to the
    Spring of Sky's man in $25 and wake him, on a Novice's full bar.
12. Guru travel (relaunched having met the Gurus of Eolis and Apolune): a
    journey from Eolis to Apolune with the Wing Boots flying keeps them
    flying and taking MP.
13. Save files: a Guru's save writes none of #27's cooldown fields; a file
    that has them, its Elixir spent, continues with nothing running, the
    Elixir spent, the pair flying at once, and its next write drops them.
14. A temple Guru: Apolune's, whose conversation Save files and Guru travel
    steer (Eolis's, in 8, takes another path), charges a spent Elixir at his
    first action, and says so before his greeting (the lines themselves are
    tests/recharge/guru_lines.py's).
15. With the remaster off, the original: the pair is used up after one 40 s
    (Novice) flight, which takes no MP, and the Hour Glass halves HP and is
    used up.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
import fax_e2e  # noqa: E402
from fax_e2e import (Game, act, batch, box_open, choose_file, chunks, dissolve, frame, hold, page_to,  # noqa: E402
                     parse_args, ram, tcp, title_menu, type_out, typed, until, wait)

AREA, SCREEN, PLAYER_X, PLAYER_Y, PLAYER_FLAGS, PLAYER_STATUS = 0x24, 0x63, 0x9E, 0xA1, 0xA4, 0xA5
SCREEN_READY = 0x0E
TEXTBOX_CLOSED, MESSAGE_ID = 0x20C, 0x213
HP, HP_FRACTION, PLAYER_IS_DEAD, TEMPLE_SPAWN_POINT, PLAYER_TITLE = 0x431, 0x432, 0x438, 0x439, 0x437
MP = 0x39A
ITEM_INVENTORY, SELECTED_ITEM, NUMBER_OF_ITEMS, GOLD = 0x3AD, 0x3C1, 0x3C6, 0x392
DURATION_WING_BOOTS, DURATION_HOUR_GLASS, SPECIAL_ITEMS, QUESTS = 0x429, 0x42A, 0x42C, 0x42D
SPRITE_ENTITIES, SPRITE_X, SPRITE_Y = 0x2CC, 0xBA, 0xC2                # CurrentSprites_*, 8 slots each
SPRITE_PHASES, SPRITE_DATA2, SPRITE_DATA3, SPRITE_UPDATE_COUNTER = 0x2E4, 0x2EC, 0x2F4, 0x383
SAVED_AREA, SAVED_SCREEN, SAVED_PALETTE, SAVED_POS, REGION = 0x3D5, 0x3D6, 0x3D7, 0x3D8, 0x435
VISIT_COUNT = 0x43A                                                 # Screen_TrackedVisitCount
HOUR_GLASS, WING_BOOTS, RED_POTION, NO_ITEM, BIT_ELIXIR, RING_OF_ELF = 0x0D, 0x0F, 0x10, 0xFF, 0x08, 0x80
USED_HOUR_GLASS, USED_WING_BOOTS, USED_ELIXIR = 0xAC, 0xAD, 0xAF      # message IDs
GOT_WING_BOOTS, GOT_HOUR_GLASS, GOT_ELIXIR = 0xB3, 0xB4, 0xB0        # "I'm holding ..."
WING_BOOTS_GONE, HOUR_GLASS_GONE, NEGATIVE_THOUGHTS, HO_HO_HO = 0xC0, 0xC1, 0x24, 0x57
FILE, GURU_SAVED = "faxanadu_file1.txt", 3                          # Start on the title: a new game in file 1
ELIXIR_LINE = 0xC3                                                  # the slot: a Guru's line about the Elixir
EOLIS, TRUNK, BUILDINGS = 0, 1, 4
EOLIS_TEMPLE, APOLUNE_TEMPLE, TOOL_SHOP = 0x02, 0x0B, 0x04          # building numbers
PALETTE_EOLIS, PALETTE_OUTSIDE, PALETTE_TOWER = 0x00, 0x06, 0x07
ZOZURA, RIPASHEIKU = 0x47, 0x2D
QUEST_SPRING_OF_SKY = 0x02
TRAVEL_DEPART, TRAVEL_TO_GURU = 3, 4                                # fax_travel.c's lines (TO_GURU + temple)
SECOND = 64                                                         # the game's second, what T: counts
FULL = 80                                                           # a full bar of MP
STEPS, FLIGHTS = (32, 40, 48, 64), (40, 50, 60, 80)                 # frames an MP, and a full bar's seconds, by band
GLASS_STEP, FREEZE = 24, 15                                         # the Hour Glass: frames an MP, its seconds
FLIGHT, STEP = FLIGHTS[0], STEPS[0]                                 # a Novice's: the lowest band
REFILL_WAIT = 240                                                   # MP refill's wait after a spend (fax_mana.c)
ELIXIR_ROW = 1                                                      # Eolis's tools: dagger, Elixir, Deluge
INTERRUPT_COUNTER = 0x1A                                            # counts NMIs
PACKAGE = ("faxanadu.dx", "dx")                                  # the one switch: off is the original game
PARTS = ("bands", "boots-flight", "boots-land", "boots-death", "glass-freeze", "glass-mp", "boss", "elixir",
         "tool-shop", "finds", "spring", "travel", "save", "apolune-guru", "original")
GURUS = ("Eolis", "Apolune")                                        # met, for Guru travel's journey
RECORD = b"faxanadu.recharge"                                       # its save-state record, version 3:
FLYING, FLY_NEXT = 1, 2                                             # the flight: running, frames to its next MP;
FREEZING, FROZEN_LO, FROZEN_HI, FREEZE_NEXT, GLASS_SHOWN = 4, 6, 7, 8, 9   # the freeze: running, frames played,
ELIXIR_SPENT = 11                                                   # to its next MP, DurationHourGlass; the Elixir


def state():
    return tcp("recharge_state")


def place():
    return ram(AREA), ram(SCREEN)


def gold():
    return int.from_bytes(ram(GOLD, 3), "little")


def settled():
    return ram(SCREEN_READY) == 0


def items():
    """The unselected items, in order, and the selected one."""
    return list(ram(ITEM_INVENTORY, 8)[:ram(NUMBER_OF_ITEMS)]), ram(SELECTED_ITEM)


def snapshot():
    """This feature's state, MP and the frame, in one batch: one frame's."""
    s, mp, ping = batch(("recharge_state", {}), ("read_ram", dict(addr=MP, len=1)), ("ping", {}))
    return s, int(mp["hex"], 16), ping["frame"]


def run_out(key):
    """Wait out a run ("flight" or "freeze"), reading each frame near its end:
    the frame of the first read that finds it over."""
    while True:
        s, _, f = snapshot()
        if not s["boots_running" if key == "flight" else "glass_running"]:
            return f
        wait(max(1, min(600, s[f"{key}_left"] - 3)))


def drains(first, step, frames):
    """MP a drain takes in `frames` frames: its first `first` frames on, then every `step`."""
    return 0 if frames < first else 1 + (frames - first) // step


def read_file(path):
    """A save file's fields."""
    fields = {}
    for line in path.read_text().splitlines():
        if line.strip() and not line.startswith("#"):
            key, value = (part.strip() for part in line.split("=", 1))
            fields[key] = value
    return fields


def write_file(path, fields):
    path.write_text("".join(f"{k} = {v}\n" for k, v in fields.items()))


def fixture(source, target, writes, record=None):
    """fax_e2e's fixture(), and if given, bytes of this feature's record,
    which the state must have."""
    if record is not None:
        data = Path(source).read_bytes()
        assert any(tag == b"MOD " and data[p:p + 64].rstrip(b"\0") == RECORD for tag, p, _ in chunks(data)), \
            "the state has no recharging items record"
    fax_e2e.fixture(source, target, writes, {RECORD.decode(): record} if record else None)


def walk_out():
    """Walk right out of the shop, to where its door was edited to lead."""
    for _ in range(200):
        if ram(AREA) != BUILDINGS:
            break
        hold("right", lambda: ram(AREA) != BUILDINGS)
    tcp("action", name="right", frames=1)                           # let go
    until("the screen to settle", lambda: ram(AREA) != BUILDINGS and settled(), 600)
    wait(30)


def warp_out(g, source, name, area, screen, pos, palette, region, writes=None, record=None):
    """From the shop's state `source`, walk out onto `screen` of `area` at block YX `pos`."""
    target = g.out / f"{name}.cycstate"
    fixture(source, target, {SAVED_AREA: area, SAVED_SCREEN: screen, SAVED_PALETTE: palette,
                             SAVED_POS: pos, REGION: region, HP: 0x50, **(writes or {})}, record)
    tcp("load_state", path=str(target))
    walk_out()
    assert place() == (area, screen), f"warp to {area}/${screen:02X} reached {place()}"
    g.poke({PLAYER_X: (pos & 0x0F) * 16, PLAYER_Y: (pos >> 4) * 16})   # a step taken while the screen loaded


def enter(g, building):
    """Up at the door the hero stands at, into `building`."""
    act("up", 4, 10)
    until(f"building ${building:02X}", lambda: place() == (BUILDINGS, building) and settled(), 600)
    wait(30)


def use():
    """Down+B: the selected item, once he stands (GameLoop_CheckUseCurrentItem
    refuses while jumping or falling, and a warp's poke can leave him a few
    frames above the floor)."""
    until("the hero to stand", lambda: not ram(PLAYER_FLAGS) & 0x05, 120)
    tcp("action", name="down", frames=60)                           # down however late the script's B comes
    wait(6)
    tcp("action", name="b", frames=4)
    wait(24)
    act("down", 1, 0)                                               # let go


def close_box():
    fax_e2e.page_through("the box to close", lambda: not box_open(), 3000)


@fax_e2e.recipe("recharge-boots-words", parent="eolis-meat-shop")
def _boots_words(g):
    """Out of the meat shop onto Eolis's screen 1, a Novice flying on the Wing
    Boots, dead: as the Guru speaks, the dissolve over (what dying does to the
    flight is checked after it)."""
    warp_out(g, g.save("shop"), "boots-dying", EOLIS, 1, 0x99, PALETTE_EOLIS, 0,
             {SELECTED_ITEM: WING_BOOTS, NUMBER_OF_ITEMS: 0, PLAYER_TITLE: 0, MP: FULL})
    use()
    until("I've used Wing Boots.", box_open, 120)
    close_box()
    until("the flight", lambda: state()["boots_running"] == 1, 60)
    g.poke({PLAYER_IS_DEAD: 1})
    dissolve()


@fax_e2e.recipe("recharge-temple", parent="eolis-meat-shop")
def _recharge_temple(g):
    """Out of the meat shop, its door edited to lead to the temple's door on
    Eolis's screen 2, with the Elixir spent, and into the temple."""
    warp_out(g, g.save("shop"), "to-temple", EOLIS, 2, 0x95, PALETTE_EOLIS, 0,
             {SPECIAL_ITEMS: 0, SELECTED_ITEM: NO_ITEM, NUMBER_OF_ITEMS: 0}, {ELIXIR_SPENT: 1})
    enter(g, EOLIS_TEMPLE)
    assert state()["elixir_spent"], state()


@fax_e2e.recipe("recharge-apolune-words", parent="recharge-temple")
def _apolune_words(g):
    """In Eolis's temple with the Elixir spent, dead with Apolune his temple:
    as the Guru speaks, the dissolve over (the way to Apolune's Guru)."""
    g.poke({TEMPLE_SPAWN_POINT: 1, PLAYER_IS_DEAD: 1, SPECIAL_ITEMS: 0}, {RECORD.decode(): {ELIXIR_SPENT: 1}})
    dissolve()


def main():
    args = parse_args(__doc__, parts=PARTS)
    game = Game.from_args(args)
    out = game.out
    shot, poke = game.shot, game.poke
    launched = {}

    def wanted(*parts):
        return any(args.wants(part) for part in parts)

    def start(**options):
        """The game, launched with these options: the one running if it was."""
        if game.running() and launched == options:
            return
        game.close()
        launch(**options)
        launched.clear()
        launched.update(options)

    def launch(on=True, gurus=None, at=None):
        """Start the game with the remaster, and so recharging items, on or off."""
        game.set_features({PACKAGE: on})
        if gurus:
            (out / "game" / "faxanadu_gurus.txt").write_text("".join(g + "\n" for g in gurus))
        game.launch(at=at)
        assert state()["enabled"] == on, state()

    def clear(entity=None):
        """Take every sprite of `entity` (or every sprite) off the screen."""
        slots = ram(SPRITE_ENTITIES, 8)
        poke({SPRITE_ENTITIES + i: 0xFF for i in range(8) if entity is None or slots[i] == entity})

    def page_through(frames=900, hurried=True):
        """Press A on each typed-out page until the box closes; with `hurried`,
        holding A while each types (fax_e2e.page_through())."""
        if hurried:
            fax_e2e.page_through("the box to close", lambda: not box_open(), max(3000, frames))
            return
        for _ in range(20):
            if not box_open():
                return
            until("the page to type out", typed, frames)
            act("a", 6, 14)
        raise AssertionError("the box did not close")

    def dismiss(expect, frames=900):
        """Wait for message `expect`, and page through it with A until the box closes."""
        seen = []

        def shown():
            seen.append(ram(MESSAGE_ID))
            return seen[-1] == expect
        until(f"message ${expect:02X} (saw {sorted(set(seen))})", shown, frames)
        page_through()

    def refused_at_zero():
        """Down+B with no MP: the refusal sound and nothing else, no line and no run."""
        before = state()["refused"]
        use()
        wait(20)
        s = state()
        assert s["refused"] == before + 1 and not box_open() and ram(DURATION_WING_BOOTS) == 0xFF, s
        assert ram(DURATION_HOUR_GLASS) == 0xFF and not s["boots_running"] and not s["glass_running"], s

    def warp(*a, **kw):
        warp_out(game, *a, **kw)

    def enter_door(building):
        enter(game, building)

    def rise(frames=40):
        """Hold A and Up: the hero rises. A stays held for the next press."""
        y = ram(PLAYER_Y)
        tcp("pad", buttons="a", frames=600)
        tcp("pad", buttons="dpup", frames=frames)
        wait(frames + 4)
        assert ram(PLAYER_Y) < y, f"flying: y {ram(PLAYER_Y)} not above {y}"

    def let_go():
        tcp("pad", buttons="a", frames=1)
        tcp("pad", buttons="dpup", frames=1)
        wait(2)

    def drain_on_schedule(step, what, frames, key="flight"):
        """Over `frames` frames the run's frames left fall a frame a frame, and
        MP an MP every `step` from its next: (MP before, MP after)."""
        s1, mp1, f1 = snapshot()
        wait(frames)
        s2, mp2, f2 = snapshot()
        played = f2 - f1
        assert s1[f"{key}_left"] - s2[f"{key}_left"] == played, (what, s1, s2, played)
        assert mp1 - mp2 == drains(s1[f"{key}_next"], step, played), (what, mp1, mp2, s1[f"{key}_next"], played)
        return mp1, mp2

    def status_shot(name):
        """The Select menu's PLAYER page, whose last row shows the special items."""
        for _ in range(3):                                          # not while knocked back
            act("select", 4, 30)
            if box_open():
                break
            wait(60)
        for _ in range(5):
            act("down", 4, 8)
        act("a", 4, 40)
        assert box_open(), "the status page"
        shot(name)
        act("b", 4, 30)
        until("the menu to close", lambda: not box_open(), 120)

    def owned_in_shop(row, name):
        """The item on `row` of the shopkeeper's list is the hero's: Quick
        shops' list marks it owned, its hint says so, and A on it does
        nothing, with no gold taken and no line. B leaves."""
        def list_waiting():
            s = tcp("shops_state")
            return s["shown"] == "buy" and s["list_up"]
        act("up", 4, 10)
        until("the list", list_waiting, 900)
        wait(20)
        for _ in range(row):
            act("down", 4, 10)
        before, money = tcp("shops_state"), gold()
        assert before["cursor"] == row and before["owned"][row] and before["hint"] == "Already owned", before
        shot(name)
        act("a", 4, 60)
        after = tcp("shops_state")
        assert after["ignored"] == before["ignored"] + 1, after
        assert (after["bought"], after["messages"]) == (before["bought"], before["messages"]), after
        assert list_waiting() and gold() == money, (after, gold())
        act("b", 4, 10)
        until("the shop to close", lambda: not box_open(), 600)

    def respawn(frames):
        """Page through the death message until the hero is back: at the temple, or with zone respawn (on by
        default) at the entrance to the zone he died in."""
        before = state()["spawns"]
        dismiss(NEGATIVE_THOUGHTS, frames)
        until("the respawn", lambda: state()["spawns"] > before and settled() and not box_open(), 1800)
        wait(30)

    def talk_to_guru():
        """Walk left to the Guru (who stands on the left) and talk."""
        act("left", 170, 10)
        act("up", 4, 10)
        until("the Guru to speak", box_open, 300)

    try:
        town = game.state("eolis-town") if wanted("bands", "original") else None
        shop = game.state("eolis-meat-shop")
        temple = game.state("recharge-temple") if wanted("travel", "save", "apolune-guru") else None
        apolune_words = game.state("recharge-apolune-words") if wanted("apolune-guru") else None
        if wanted("boots-death"):
            game.state("recharge-boots-words")
        boots = out / "boots.cycstate"
        no_one = {SPRITE_ENTITIES + i: 0xFF for i in range(8)}      # Up next to the town's walker would talk

        # 1. Wing Boots: a full bar's flight, and its drain, for each title band.
        if args.wants("bands"):
            start()
            for band, (flight, step) in enumerate(zip(FLIGHTS, STEPS)):
                fixture(town, boots, {HP: 0x50, MP: FULL, SELECTED_ITEM: WING_BOOTS, NUMBER_OF_ITEMS: 0,
                                      PLAYER_TITLE: band * 4 + 3, **no_one})
                tcp("load_state", path=str(boots))
                wait(10)
                use()
                dismiss(USED_WING_BOOTS)
                s = state()
                assert s["boots_running"] and s["frames_per_mp"] == step, s
                assert ram(DURATION_WING_BOOTS) in (flight, flight - 1) and s["hud_drawn"] in (flight, flight - 1), s
                drain_on_schedule(step, f"band {band}", 3 * step + 5)
            print(f"ok: a full bar flies {', '.join(map(str, FLIGHTS))} s by title band (the original: 40, 30, 20, "
                  f"10), T: shows it, and MP falls an MP every {', '.join(map(str, STEPS))} frames, to the frame")

        # 2. A whole Novice flight from a full bar.
        if args.wants("boots-flight"):
            start()
            warp(shop, "boots-novice", EOLIS, 1, 0x99, PALETTE_EOLIS, 0,  # walked out, so the HUD shows the pair
                 {SELECTED_ITEM: WING_BOOTS, NUMBER_OF_ITEMS: 0, PLAYER_TITLE: 0, MP: FULL})
            clear()
            use()
            until("I've used Wing Boots.", lambda: ram(MESSAGE_ID) == USED_WING_BOOTS, 120)
            page_through()
            s0, mp0, f0 = snapshot()
            assert s0["boots_running"] and s0["hud"] == FLIGHT and s0["hud_drawn"] == FLIGHT, s0
            assert items() == ([], WING_BOOTS) and s0["kept"] >= 1, items()
            begun = f0 - (FULL * STEP - s0["flight_left"])             # the flight's first frame
            low, sampled = mp0, 0
            while True:                                                 # the drain, and T:, as it flies
                s, mp, f = snapshot()
                if not s["boots_running"]:
                    ended = f
                    break
                assert f - begun == FULL * STEP - s["flight_left"], (s, f, begun)
                assert mp == -(-s["flight_left"] // STEP) and mp <= low, (s, mp, low)
                assert s["hud_drawn"] == -(-s["flight_left"] // SECOND), s
                low = mp
                if sampled == 0 and mp <= FULL // 2:                    # half way: up in the air, for the picture
                    rise()
                    shot("flight-mp-draining")
                    let_go()
                    sampled = 1
                wait(min(200, max(1, s["flight_left"] - 2)))
            until("The power of the Wing Boots is gone.", lambda: ram(MESSAGE_ID) == WING_BOOTS_GONE, 60)
            s = state()
            assert ram(MP) == 0 and s["boots_landed"] == 0 and not ram(PLAYER_STATUS) & 0x80, s
            assert FULL * STEP <= ended - begun <= FULL * STEP + 2, (ended, begun)
            page_through()
            closed = frame()
            wait(4)
            s = state()
            assert s["hud_drawn"] == 0 and s["boots_ready"] and items() == ([], WING_BOOTS), s
            shot("flight-over")
            print(f"ok: a Novice's full bar flies {ended - begun} frames ({FULL} MP, an MP every {STEP}), MP "
                  f"never rising, T: counting down; at 0 MP the game's own line ends it, and T: is 00")
            refused_at_zero()
            print("ok: Down+B at 0 MP does nothing but the refusal sound")
            wait(REFILL_WAIT - 15 - (frame() - closed))               # the wait counts from the box's close
            assert ram(MP) == 0, "MP came back before refill's wait from the landing"
            until("MP refill", lambda: ram(MP) > 0, 60)
            assert REFILL_WAIT - 5 <= frame() - closed <= REFILL_WAIT + 15, frame() - closed
            print("ok: MP refill waits its whole 4 s from the landing")
            poke({MP: FULL})
            use()
            dismiss(USED_WING_BOOTS)
            assert state()["boots_running"] and state()["flights"] == 2, state()
            print("ok: with MP the same pair flies again at once: no cooldown")

        # 3. Landing early: Down+B again, in the air and standing.
        if args.wants("boots-land"):
            start()
            warp(shop, "boots-land", EOLIS, 1, 0x99, PALETTE_EOLIS, 0,
                 {SELECTED_ITEM: WING_BOOTS, NUMBER_OF_ITEMS: 0, PLAYER_TITLE: 0, MP: FULL})
            clear()
            use()
            dismiss(USED_WING_BOOTS)
            rise(30)
            y = ram(PLAYER_Y)
            tcp("pad", buttons="dpdown", frames=20)                     # A still held: Down is a climb down
            wait(4)
            assert not ram(PLAYER_FLAGS) & 0x05, f"flags ${ram(PLAYER_FLAGS):02X}: jumping or falling"
            tcp("action", name="b", frames=4)
            until("The power of the Wing Boots is gone.", lambda: ram(MESSAGE_ID) == WING_BOOTS_GONE, 30)
            s, mp, _ = snapshot()
            assert s["boots_landed"] == 1 and not s["boots_running"] and ram(PLAYER_Y) < y + 30, s
            shot("boots-landed-in-the-air")
            let_go()
            page_through()
            wait(STEP * 2)
            assert ram(MP) == mp and not ram(PLAYER_STATUS) & 0x80, (ram(MP), mp)
            until("on the floor", lambda: not ram(PLAYER_FLAGS) & 0x05, 120)
            print(f"ok: Down+B in the air, A held, ends the flight with the game's line at {mp} MP, and no more MP goes")
            use()
            dismiss(USED_WING_BOOTS)
            wait(30)
            use()                                                       # standing
            dismiss(WING_BOOTS_GONE, 120)
            s = state()
            assert s["boots_landed"] == 2 and s["flights"] == 2 and s["boots_ready"], s
            print("ok: they fly again at once, and Down+B standing ends that flight too")

        # 4. Dying while flying.
        if args.wants("boots-death"):
            start(at="recharge-boots-words")
            respawn(600)
            s = state()
            assert s["boots_ready"] and not s["boots_running"] and items() == ([], WING_BOOTS), s
            assert ram(MP) == FULL and ram(DURATION_WING_BOOTS) == 0xFF, ram(MP)
            print("ok: dying while flying ends the flight; he comes back with the pair, ready, and a full bar")

        # 5. The Hour Glass on Eolis screen 4.
        if args.wants("glass-freeze"):
            start()
            warp(shop, "glass", EOLIS, 4, 0x9E, PALETTE_EOLIS, 0,
                 {SELECTED_ITEM: HOUR_GLASS, NUMBER_OF_ITEMS: 0, MP: FULL, HP_FRACTION: 0})
            zozuras = [i for i, e in enumerate(ram(SPRITE_ENTITIES, 8)) if e == ZOZURA]
            assert zozuras, "Zozuras on screen 4"
            use()
            until("I've used Hour Glass.", lambda: ram(MESSAGE_ID) == USED_HOUR_GLASS, 120)
            page_through()
            s0, mp0, f0 = snapshot()
            begun = f0 - s0["freeze_played"]
            assert ram(DURATION_HOUR_GLASS) in (FREEZE, FREEZE - 1) and ram(HP) == 0x50, (ram(DURATION_HOUR_GLASS), ram(HP))
            assert s0["glass_running"] and s0["hud_drawn"] in (FREEZE, FREEZE - 1), s0
            before = [ram(SPRITE_X + i) for i in zozuras]
            drain_on_schedule(GLASS_STEP, "the freeze", 3 * GLASS_STEP + 7, "freeze")
            assert [ram(SPRITE_X + i) for i in zozuras] == before, "the Zozuras froze"
            print(f"ok: the Hour Glass freezes the Zozuras, takes no HP, and an MP every {GLASS_STEP} frames; T: "
                  "shows its seconds")
            poke({SELECTED_ITEM: RED_POTION})                         # another item: the original's 00
            until("T: 00", lambda: state()["hud_drawn"] == 0, 30)
            poke({SELECTED_ITEM: HOUR_GLASS})
            until("T: its seconds", lambda: 0 < state()["hud_drawn"] <= FREEZE, 30)
            print("ok: with another item selected T: is 00; with the Hour Glass again, its seconds")
            until("half the freeze", lambda: state()["freeze_left"] <= FREEZE * SECOND // 2, FREEZE * SECOND)
            assert [ram(SPRITE_X + i) for i in zozuras] == before, "the Zozuras still frozen"
            shot("hour-glass-mp-draining")
            ended = run_out("freeze")
            until("The power of the Hour Glass is gone.", lambda: ram(MESSAGE_ID) == HOUR_GLASS_GONE, 60)
            assert FREEZE * SECOND <= ended - begun <= FREEZE * SECOND + 2, (ended, begun)
            assert ram(MP) == FULL - FREEZE * SECOND // GLASS_STEP and ram(HP) == 0x50, (ram(MP), ram(HP))
            page_through()
            print(f"ok: after its 15 s ({ended - begun} frames) the freeze is over, {FREEZE * SECOND // GLASS_STEP} MP "
                  "gone, HP whole")
            clear(ZOZURA)
            use()
            dismiss(USED_HOUR_GLASS)
            assert state()["glass_running"] and state()["freezes"] == 2, state()
            wait(40)
            use()
            dismiss(HOUR_GLASS_GONE, 120)
            s, mp, _ = snapshot()
            wait(GLASS_STEP * 2)
            assert s["glass_landed"] == 1 and not s["glass_running"] and ram(MP) == mp, (s, ram(MP), mp)
            print("ok: no cooldown: it freezes again at once, and Down+B again ends that freeze, its MP with it")

        # 6. The Hour Glass on less MP than a freeze costs.
        if args.wants("glass-mp"):
            start()
            warp(shop, "glass-mp", EOLIS, 4, 0x9E, PALETTE_EOLIS, 0,
                 {SELECTED_ITEM: HOUR_GLASS, NUMBER_OF_ITEMS: 0, MP: 10})
            clear(ZOZURA)
            use()
            until("I've used Hour Glass.", lambda: ram(MESSAGE_ID) == USED_HOUR_GLASS, 120)
            page_through()
            s0, mp0, f0 = snapshot()
            begun = f0 - s0["freeze_played"]
            assert mp0 == 10 and s0["freeze_left"] == 10 * GLASS_STEP - s0["freeze_played"], s0
            assert s0["hud_drawn"] == -(-s0["freeze_left"] // SECOND), s0
            ended = run_out("freeze")
            until("The power of the Hour Glass is gone.", lambda: ram(MESSAGE_ID) == HOUR_GLASS_GONE, 60)
            assert 10 * GLASS_STEP <= ended - begun <= 10 * GLASS_STEP + 2 and ram(MP) == 0, (ended, begun)
            page_through()
            refused_at_zero()
            print(f"ok: on 10 MP the freeze ends at 0 MP, {ended - begun} frames on; then Down+B does nothing but the "
                  "refusal sound")

        if args.wants("boss"):
            start()
            # 7. Ripasheiku, at the top of the Tower of Trunk.
            warp(shop, "boss", TRUNK, 0x15, 0x95, PALETTE_TOWER, 1,
                 {SELECTED_ITEM: HOUR_GLASS, NUMBER_OF_ITEMS: 0, MP: FULL})
            boss = ram(SPRITE_ENTITIES, 8).index(RIPASHEIKU)
            for _ in range(3):                                          # a shot's knockback can take the press
                use()
                try:
                    until("the Hour Glass's line", lambda: ram(MESSAGE_ID) == USED_HOUR_GLASS, 60)
                    break
                except AssertionError:
                    pass
            dismiss(USED_HOUR_GLASS)
            assert ram(DURATION_HOUR_GLASS) in (FREEZE, FREEZE - 1)
            counter = ram(SPRITE_UPDATE_COUNTER)
            seen = set()                                                # his place, phase and its countdowns
            end = frame() + 3 * SECOND
            while frame() < end and len(seen) < 4 and not box_open():
                seen.add((ram(SPRITE_X + boss), ram(SPRITE_Y + boss), ram(SPRITE_PHASES + boss),
                          ram(SPRITE_DATA2 + boss), ram(SPRITE_DATA3 + boss)))
                wait(3)
            assert len(seen) > 1 and ram(DURATION_HOUR_GLASS) < 0x80, f"Ripasheiku's behavior stood still: {seen}"
            assert ram(SPRITE_UPDATE_COUNTER) == counter, "SpriteUpdateCounter stops under the Hour Glass"
            shot("boss-not-frozen")
            print(f"ok: bosses do not freeze: Ripasheiku's behavior ran through {len(seen)} states under the Hour "
                  f"Glass, while SpriteUpdateCounter stood at ${counter:02X}")
            target = out / "boss-entry.cycstate"                        # the Hour Glass running in the shop, 12 s left
            played = (FREEZE - 12) * SECOND
            fixture(shop, target, {SAVED_AREA: TRUNK, SAVED_SCREEN: 0x15, SAVED_PALETTE: PALETTE_TOWER, SAVED_POS: 0x95,
                                   REGION: 1, HP: 0x50, MP: FULL, SELECTED_ITEM: HOUR_GLASS, NUMBER_OF_ITEMS: 0,
                                   DURATION_HOUR_GLASS: 12},
                    {FREEZING: 1, FROZEN_LO: played & 0xFF, FROZEN_HI: played >> 8, FREEZE_NEXT: GLASS_STEP,
                     GLASS_SHOWN: 12})
            tcp("load_state", path=str(target))
            assert state()["glass_running"], state()
            for _ in range(200):
                if ram(AREA) != BUILDINGS:
                    break
                act("right", 2, 0)
            entered, left = frame(), ram(DURATION_HOUR_GLASS)            # at the door, before the screen's first frame
            until("the Hour Glass to end", lambda: ram(DURATION_HOUR_GLASS) == 0xFF, 3 * SECOND)
            ended, cut_at = frame() - entered, ram(MP)
            s = state()
            assert 5 <= left <= 12 and ended <= 2 * SECOND and s["cuts"] == 1, (left, ended, s)
            dismiss(HOUR_GLASS_GONE, 300)
            wait(GLASS_STEP * 2)
            assert ram(MP) == cut_at and not state()["glass_running"], (ram(MP), cut_at)
            print(f"ok: walking onto the boss's screen with {left} s of the Hour Glass left ends it within "
                  f"{ended} frames (LimitItemDurations), and its MP stops")

        if args.wants("elixir"):
            start()
            # 8. The Elixir: a revive, a death with it spent, a Guru.
            warp(shop, "elixir", EOLIS, 4, 0x9C, PALETTE_EOLIS, 0,
                 {SPECIAL_ITEMS: BIT_ELIXIR, SELECTED_ITEM: NO_ITEM, NUMBER_OF_ITEMS: 0})
            status_shot("status-elixir-charged")
            poke({HP: 1, HP_FRACTION: 0})
            until("a Zozura's hit", lambda: ram(MESSAGE_ID) == USED_ELIXIR, 2400)
            type_out("the line to type out", 900, pauses=False)        # the pause flag may be stale
            wait(2)
            shot("elixir-used")
            page_through()
            until("HP and MP to refill", lambda: (ram(HP), ram(MP)) == (0x50, 0x50) and not box_open(), 1200)
            wait(10)                                                   # the refill over: it would undo the next poke
            s = state()
            assert s["revives"] == 1 and s["elixir_spent"] and not s["elixir_charged"], s
            print("ok: at 0 HP the Elixir revives the hero, and is kept, spent")
            poke({HP: 1, HP_FRACTION: 0})
            respawn(2400)
            s = state()
            assert s["elixir_spent"] and not s["elixir_charged"] and s["revives"] == 1, s
            saved = read_file(out / "game" / FILE)                     # the respawn wrote the file in play
            assert saved.get("recharge.elixir") == "spent" and int(saved["special_items"]) & BIT_ELIXIR == 0, saved
            assert "recharge.wingboots" not in saved and "recharge.hourglass" not in saved, saved
            # To Eolis's temple through its door (zone respawn, on by default, brings the hero back at the
            # zone's entrance rather than the temple), with the Elixir spent as it is now.
            warp(shop, "to-temple", EOLIS, 2, 0x95, PALETTE_EOLIS, 0,
                 {SPECIAL_ITEMS: 0, SELECTED_ITEM: NO_ITEM, NUMBER_OF_ITEMS: 0}, {ELIXIR_SPENT: 1})
            enter_door(EOLIS_TEMPLE)
            assert state()["elixir_spent"], state()
            status_shot("status-elixir-spent")
            print("ok: spent, the Elixir does not save him; he dies and respawns with it still spent, the respawn's "
                  "save writes recharge.elixir = spent to file 1, and the status page does not show it")
            talk_to_guru()
            until("the Guru to charge it", lambda: state()["guru_recharges"] == 1, 120)
            s = state()
            assert s["elixir_charged"] and not s["elixir_spent"] and s["restored_lines"] == 1, s
            page_through()
            status_shot("status-elixir-recharged")
            print("ok: talking to Eolis's Guru charges the spent Elixir, and says so, and the status page shows it "
                  "again")

        if args.wants("tool-shop"):
            start()
            # 9. Eolis's tool shop: an Elixir, spent or charged, is the hero's, and buying never charges it.
            gold_1000 = {GOLD: 0xE8, GOLD + 1: 0x03, GOLD + 2: 0}
            warp(shop, "tool-shop", EOLIS, 3, 0x9A, PALETTE_EOLIS, 0,
                 {SPECIAL_ITEMS: 0, SELECTED_ITEM: NO_ITEM, NUMBER_OF_ITEMS: 0, **gold_1000}, {ELIXIR_SPENT: 1})
            assert state()["elixir_spent"] and gold() == 1000, (state(), gold())
            enter_door(TOOL_SHOP)
            act("left", 120, 10)                                        # to the counter
            owned_in_shop(ELIXIR_ROW, "shop-elixir-spent")
            wait(4)
            s = state()
            assert gold() == 1000 and s["elixir_spent"] and not s["elixir_charged"], (gold(), s)
            warp(shop, "tool-shop-charged", EOLIS, 3, 0x9A, PALETTE_EOLIS, 0,
                 {SPECIAL_ITEMS: BIT_ELIXIR, SELECTED_ITEM: NO_ITEM, NUMBER_OF_ITEMS: 0, **gold_1000})
            enter_door(TOOL_SHOP)
            act("left", 120, 10)
            owned_in_shop(ELIXIR_ROW, "shop-elixir-charged")
            s = state()
            assert gold() == 1000 and s["elixir_charged"] and not s["elixir_spent"], (gold(), s)
            print("ok: at the tool shop a spent Elixir is owned, A on it does nothing and it stays spent (a Guru charges "
                  "it, not a purchase); a charged one is owned the same")

        if args.wants("finds"):
            start()
            # 10. Finds: one the hero holds adds nothing; an Elixir charges a spent one.
            warp(shop, "find-boots", TRUNK, 0x2D, 0x31, PALETTE_TOWER, 1,
                 {SELECTED_ITEM: WING_BOOTS, NUMBER_OF_ITEMS: 0, VISIT_COUNT: 3})
            clear(0x07)                                                 # it shows once the hornets are gone, under
            for _ in range(60):                                         # his feet (it lies at x 16, by the wall)
                if ram(MESSAGE_ID) == GOT_WING_BOOTS:
                    break
                act("left", 2, 2)
            until("the find", lambda: ram(MESSAGE_ID) == GOT_WING_BOOTS, 600)
            type_out("the line to type out", 900, pauses=False)
            shot("find-boots-message")
            page_through()
            wait(4)
            s = state()
            assert s["pickups"] == 1 and items() == ([], WING_BOOTS), (s, items())
            warp(shop, "find-glass", TRUNK, 0x13, 0x3D, PALETTE_TOWER, 1,
                 {SELECTED_ITEM: HOUR_GLASS, NUMBER_OF_ITEMS: 0, VISIT_COUNT: 3})
            clear(0x47)
            clear(0x26)
            dismiss(GOT_HOUR_GLASS, 600)
            s = state()
            assert s["pickups"] == 2 and items() == ([], HOUR_GLASS), (s, items())
            warp(shop, "find-elixir", TRUNK, 0x3C, 0x22, PALETTE_TOWER, 1,
                 {SPECIAL_ITEMS: 0, SELECTED_ITEM: NO_ITEM, NUMBER_OF_ITEMS: 0}, {ELIXIR_SPENT: 1})
            assert state()["elixir_spent"], state()
            clear(0x07)
            dismiss(GOT_ELIXIR, 600)
            until("the Elixir charged", lambda: not state()["elixir_spent"], 60)
            assert state()["elixir_charged"], state()
            print("ok: a found pair of Wing Boots and an Hour Glass the hero holds add no second copy, and a found "
                  "Elixir charges the spent one")

        if args.wants("spring"):
            start()
            # 11. The Spring of Sky: from Trunk $20's floor below the shaft to the man in $25. The enemies of
            # $20, $23 and $24 are cleared as the hero reaches each: the flight is what is measured.
            warp(shop, "spring", TRUNK, 0x20, 0x94, PALETTE_OUTSIDE, 1,
                 {SELECTED_ITEM: WING_BOOTS, NUMBER_OF_ITEMS: 0, QUESTS: 0x01, PLAYER_TITLE: 0, MP: FULL})
            use()
            dismiss(USED_WING_BOOTS)
            took_off = frame()
            assert ram(DURATION_WING_BOOTS) in (FLIGHT, FLIGHT - 1)
            hold, held = 40, {}                                         # pad button -> frame its hold was renewed

            def press(want, now):
                """Hold the buttons in `want` (A to fly, Up to rise), renewing each hold before it lapses, and let
                the others go: a lapse would land the hero, and a fresh A would jump."""
                for b in ("a", "dpup", "dpleft", "dpright"):
                    if b in want and now - held.get(b, -hold) > hold // 2:
                        tcp("pad", buttons=b, frames=hold)
                        held[b] = now
                    elif b not in want and b in held:
                        tcp("pad", buttons=b, frames=1)
                        del held[b]

            def steer(x, target):
                return {"dpright"} if x < target - 3 else {"dpleft"} if x > target + 3 else set()
            cleared = shaft_shot = None
            end = frame() + (FLIGHT + 2) * SECOND                     # frames, not reads: several can come in a frame
            while True:
                now = frame()
                assert now < end, f"no landing beside the man in the flight's {FLIGHT} s: {place()}"
                zp = ram(SCREEN_READY, PLAYER_Y + 1 - SCREEN_READY)
                screen, x, y = zp[SCREEN - SCREEN_READY], zp[PLAYER_X - SCREEN_READY], zp[PLAYER_Y - SCREEN_READY]
                assert ram(DURATION_WING_BOOTS) < 0x80, f"the flight ran out at {place()}, x={x} y={y}"
                if screen != cleared and screen != 0x25 and zp[0] == 0:   # not the man and his fountain
                    clear()
                    cleared = screen
                if screen == 0x24 and not shaft_shot and y < 160:
                    shot("spring-flight")
                    shaft_shot = True
                if screen in (0x20, 0x23) or (screen == 0x24 and y > 112):
                    want = {"a", "dpup"} | steer(x, 48)                 # up the shaft, columns 2-5
                elif screen == 0x24 or (screen == 0x25 and y > 40):
                    want = {"a", "dpup"} | steer(x, 216)                # round the ledge's right end, up the right side
                elif screen == 0x25 and x > 186:
                    want = {"a"} | ({"dpup"} if y > 16 else set()) | steer(x, 182)   # along the top, over the man
                elif screen == 0x25 and x < 172:
                    want = {"a"} | ({"dpup"} if y > 16 else set()) | steer(x, 180)   # past his ledge (a late read): back
                elif screen == 0x25:
                    press(set(), now)                                   # let go: down onto his ledge
                    break
                else:
                    raise AssertionError(f"off the route: {place()}, x={x} y={y}")
                press(want, now)
            until("the landing", lambda: ram(PLAYER_FLAGS) & 0x05 == 0 and place() == (TRUNK, 0x25), 300)
            landed = frame() - took_off
            left, mp = ram(DURATION_WING_BOOTS), ram(MP)
            assert left < 0x80, "the flight ran out before the landing"
            for _ in range(30):                                         # beside him, on the fountain's ledge
                if ram(MESSAGE_ID) == HO_HO_HO:
                    break
                x = ram(PLAYER_X)
                if not 172 <= x <= 186:
                    act("right" if x < 172 else "left", 2, 8)
                    continue
                act("up", 4, 20)
            until("the Spring of Sky's man", lambda: ram(MESSAGE_ID) == HO_HO_HO, 300)
            shot("spring-of-sky")
            page_through()
            assert ram(QUESTS) & QUEST_SPRING_OF_SKY, "the Spring of Sky's man woke"
            print(f"ok: the Spring of Sky: from Trunk $20's floor to the man in $25 in {landed} frames "
                  f"({landed / SECOND:.1f} of the game's seconds), with {left} s of a Novice's {FLIGHT} s flight left "
                  f"({mp} MP)")
            game.close()

        if args.wants("travel"):
            # 12. Guru travel: a journey keeps the flight flying and taking MP.
            start(gurus=GURUS)
            travel = out / "temple-travel.cycstate"
            fixture(temple, travel, {SPECIAL_ITEMS: RING_OF_ELF, TEMPLE_SPAWN_POINT: 1, DURATION_WING_BOOTS: 40,
                                     PLAYER_STATUS: 0x80, MP: FULL, SELECTED_ITEM: WING_BOOTS, NUMBER_OF_ITEMS: 0},
                    {FLYING: 1, FLY_NEXT: STEP})
            tcp("load_state", path=str(travel))
            wait(4)
            s = state()
            assert s["boots_running"] == 1 and ram(PLAYER_STATUS) & 0x80, s
            talk_to_guru()                                              # with the ring, he offers Apolune
            # His line, the intro, the offer: A to each, and none on the offer (page_to()).
            page_to(lambda s: s["text"] == TRAVEL_TO_GURU + 1, "the offer of Apolune", tries=20)
            mp_before, left_before = ram(MP), state()["flight_left"]
            act("a", 6, 14)
            until("the farewell", lambda: tcp("travel_state")["text"] == TRAVEL_DEPART, 300)
            page_through()
            until("Apolune's temple", lambda: place() == (BUILDINGS, APOLUNE_TEMPLE) and settled(), 2400)
            wait(30)
            mp_after, f_after = ram(MP), state()["flight_left"]
            drain_on_schedule(STEP, "after the journey", 2 * STEP + 3)
            s = state()
            assert s["boots_running"] == 1 and ram(PLAYER_STATUS) & 0x80 and f_after < left_before, (s, left_before)
            assert mp_after <= mp_before, (mp_before, mp_after)
            poke({MP: 1})
            dismiss(WING_BOOTS_GONE, 2 * STEP)
            print(f"ok: a Guru travel journey from Eolis to Apolune keeps the flight ({mp_before} MP before, "
                  f"{mp_after} after), still taking MP on schedule, until it runs out")

        if args.wants("save"):
            # 13. Save files: a Guru's save keeps nothing of the timed items; a file from #27, with its cooldowns
            # and the Elixir spent, continues with nothing running, and its next write drops them.
            start(gurus=GURUS)
            keep = out / "temple-save.cycstate"
            fixture(temple, keep, {SPECIAL_ITEMS: RING_OF_ELF | BIT_ELIXIR, SELECTED_ITEM: WING_BOOTS,
                                   NUMBER_OF_ITEMS: 0, DURATION_WING_BOOTS: 40, PLAYER_STATUS: 0x80, MP: FULL},
                    {FLYING: 1, FLY_NEXT: STEP, ELIXIR_SPENT: 0})
            tcp("load_state", path=str(keep))
            wait(4)
            saves = tcp("files_state")["saves"]
            talk_to_guru()                                              # "Remember my journey." is the first choice
            page_to(lambda s: s["saves"] == saves + 1, "the save", "files_state", tries=20)   # A on his menu saves
            for _ in range(20):                                         # B closes what follows
                if not box_open():
                    break
                act("b", 6, 20)
            saved = read_file(out / "game" / FILE)
            assert "recharge.wingboots" not in saved and "recharge.hourglass" not in saved, saved
            assert "recharge.elixir" not in saved, saved
            game.close()
            saved["recharge.wingboots"], saved["recharge.hourglass"] = "45", "30"   # as #27 wrote them
            saved["recharge.elixir"] = "spent"                         # as a respawn with it spent writes it
            saved["special_items"] = str(int(saved["special_items"]) & ~BIT_ELIXIR)
            write_file(out / "game" / FILE, saved)
            launch()
            title_menu()
            choose_file(False, 1)                                       # CONTINUE, file 1
            until("Eolis's temple", lambda: place() == (BUILDINGS, EOLIS_TEMPLE) and settled(), 1800)
            wait(30)
            s = state()
            assert s["boots_ready"] and s["glass_ready"] and not s["boots_running"], s
            assert s["elixir_spent"] and not s["elixir_charged"], s
            rewritten = read_file(out / "game" / FILE)                 # the continue's spawn wrote the file
            assert "recharge.wingboots" not in rewritten and "recharge.hourglass" not in rewritten, rewritten
            assert rewritten.get("recharge.elixir") == "spent", rewritten
            poke({MP: FULL})
            use()
            dismiss(USED_WING_BOOTS)
            assert state()["boots_running"], state()
            print("ok: a Guru's save writes nothing of the Wing Boots or the Hour Glass; a file with #27's cooldowns "
                  "continues with nothing running and the pair flying at once, a spent Elixir still spent, and its "
                  "next write drops the cooldowns")
            game.close()

        if args.wants("apolune-guru"):
            # 14. A temple Guru: Apolune's, whose conversation Save files and Guru travel steer, charges a spent
            # Elixir at his first action, before either has moved the script.
            start(gurus=GURUS)
            assert tcp("files_state")["enabled"] and tcp("travel_state")["enabled"]
            tcp("load_state", path=str(apolune_words))                # dead, as the Guru speaks
            dismiss(NEGATIVE_THOUGHTS, 600)
            until("Apolune's temple", lambda: place() == (BUILDINGS, APOLUNE_TEMPLE) and settled(), 1800)
            wait(30)
            assert state()["elixir_spent"], state()
            saves = tcp("files_state")["saves"]
            talk_to_guru()
            until("his line about it", lambda: ram(MESSAGE_ID) == ELIXIR_LINE, 300)
            until("the Guru to charge it", lambda: state()["guru_recharges"] == 1, 120)
            s = state()
            assert s["elixir_charged"] and not s["elixir_spent"] and s["line"] == 1, s
            # His line, his greeting, then "Remember my journey." (A on his menu saves).
            page_to(lambda s: s["saves"] == saves + 1, "the save", "files_state", tries=20)
            for _ in range(20):                                         # B closes what follows
                if not box_open():
                    break
                act("b", 6, 20)
            assert not box_open() and state()["elixir_charged"], state()
            print("ok: Apolune's Guru, whose conversation Save files and Guru travel steer, charges the spent Elixir "
                  "and says so as he greets the hero, and remembers the journey")
            game.close()

        if args.wants("original"):
            # 15. The remaster off: the original's one-use pair, its title-based flight, and the Hour Glass's half HP.
            start(on=False)
            fixture(town, boots, {HP: 0x50, MP: FULL, SELECTED_ITEM: WING_BOOTS, NUMBER_OF_ITEMS: 0, PLAYER_TITLE: 0,
                                  **no_one})
            tcp("load_state", path=str(boots))
            wait(10)
            use()
            dismiss(USED_WING_BOOTS)
            assert ram(DURATION_WING_BOOTS) in (40, 39) and items() == ([], NO_ITEM), (ram(DURATION_WING_BOOTS), items())
            wait(3 * SECOND)
            assert ram(MP) == FULL, ram(MP)
            glass = out / "glass-original.cycstate"
            fixture(town, glass, {HP: 0x50, HP_FRACTION: 0, MP: FULL, SELECTED_ITEM: HOUR_GLASS, NUMBER_OF_ITEMS: 0,
                                  **no_one})
            tcp("load_state", path=str(glass))
            wait(10)
            use()
            dismiss(USED_HOUR_GLASS)
            assert ram(HP) == 0x28 and items() == ([], NO_ITEM) and ram(DURATION_HOUR_GLASS) in (15, 14), ram(HP)
            wait(SECOND)
            assert ram(MP) == FULL, ram(MP)
            print("ok: with the remaster off, a Novice's Wing Boots fly 40 s, take no MP and are used up, and the "
                  "Hour Glass halves HP and is used up, as in the original")
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
