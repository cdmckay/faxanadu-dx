"""Play recharging items end to end in a hidden window.

    python3 tests/recharge/recharging_items.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/recharge

The game runs uncapped and the script waits on the game's own state, never on
the clock, on a reserved TCP port (tests/e2e/fax_e2e.py), so any number can
run in parallel.

Copies the build to OUT and starts a new game over TCP. The hero walks through
the Eolis gate into the town and into a shop. Save-state RAM edits then set up
what each scenario needs (external prerequisites, not coverage): the item
selected, HP, gold, the title, where the shop's door leads (Area_Saved*, so
walking out puts the hero on the screen wanted), a screen's enemies cleared,
the random-item visit counter, and for the duplicate finds and Guru travel
the feature's own record (a cooldown running, the Elixir spent). Items are
used with Down+B, as a player would.

1. Wing Boots. One use at each title band gives 30, 40, 50 and 60 s of flight
   (the original: 40, 30, 20, 10). Then, as a Novice in Eolis: 30 s of flight,
   T: counting it down. When it runs out (from its last seconds: the game
   counts a flight down, and the bands check each one's length), T: blinks a
   60 s cooldown; Down+B then does nothing. Once it is over, the same pair
   flies again. Dying makes them ready.
2. The Hour Glass on Eolis screen 4: the Zozuras stop, HP halves, 15 s (its end
   from its last seconds, as the flight's). Then a
   60 s cooldown; refused meanwhile; then, from its last seconds (the Wing
   Boots' plays out all 60), it works again.
3. The Hour Glass at Ripasheiku (Trunk $15): the boss keeps moving. Walking
   onto a boss's screen with the Hour Glass running ends it within a second
   (LimitItemDurations), and its cooldown follows.
4. The Elixir on Eolis screen 4 with 1 HP: a Zozura's hit, and it revives the
   hero ("I've used Elixir."). With 1 HP again, the next hit kills him; he
   respawns with it still spent, and the menu's Player page, which showed it,
   does not. Talking to Eolis's Guru charges it, and the page shows it again.
5. Eolis's tool shop with a spent Elixir: it is still the hero's, so Quick
   shops lists it as owned, "Already owned", and A on it does nothing: the
   gold stays and it stays spent, since buying never charges it (a Guru
   does). Charged, the same.
6. Finds: a pair of Wing Boots (Trunk $2D) while they recharge, an Hour Glass
   (Trunk $13) while it recharges, and an Elixir (Trunk $3C) while it is spent:
   each is ready at once, and none is a second copy.
7. The Spring of Sky, the spring the Wing Boots are needed for: from the floor
   of Trunk $20 below the shaft, fly up through $23 and $24 to the Spring of
   Sky's man in $25 and wake him, inside the lowest band's 30 s.
8. Guru travel (relaunched having met the Gurus of Eolis and Apolune): a
   journey from Eolis to Apolune with the Wing Boots flying and the Hour Glass
   recharging keeps both counting, and the flight's cooldown follows it.
9. Save files: dying with the Elixir spent writes recharge.elixir = spent to
   the file in play (file 1, which Start on the title began). A Guru's save
   writes the cooldowns; quitting and continuing the file puts them back, and
   an Elixir the file says is spent stays spent.
10. A temple Guru: Apolune's, whose conversation Save files and Guru travel
    steer (Eolis's, in 4, takes another path), charges a spent Elixir at his
    first action.
11. With the remaster off, the original: the pair is used up after one 40 s
    (Novice) flight.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
import fax_e2e  # noqa: E402
from fax_e2e import Game, act, batch, chunks, dissolve, frame, hold, page_to, parse_args, ram, tcp, type_out, typed, until, wait  # noqa: E402

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
HOUR_GLASS, WING_BOOTS, NO_ITEM, BIT_ELIXIR, RING_OF_ELF = 0x0D, 0x0F, 0xFF, 0x08, 0x80
USED_HOUR_GLASS, USED_WING_BOOTS, USED_ELIXIR = 0xAC, 0xAD, 0xAF      # message IDs
GOT_WING_BOOTS, GOT_HOUR_GLASS, GOT_ELIXIR = 0xB3, 0xB4, 0xB0        # "I'm holding ..."
WING_BOOTS_GONE, HOUR_GLASS_GONE, NEGATIVE_THOUGHTS, HO_HO_HO = 0xC0, 0xC1, 0x24, 0x57
FILE, GURU_SAVED = "faxanadu_file1.txt", 3                          # Start on the title: a new game in file 1
MEDITATE_WITH_YOU = 0x22                                            # a temple Guru's greeting, then the mantra
EOLIS, TRUNK, BUILDINGS = 0, 1, 4
EOLIS_TEMPLE, APOLUNE_TEMPLE, TOOL_SHOP = 0x02, 0x0B, 0x04          # building numbers
PALETTE_EOLIS, PALETTE_OUTSIDE, PALETTE_TOWER = 0x00, 0x06, 0x07
ZOZURA, RIPASHEIKU = 0x47, 0x2D
QUEST_SPRING_OF_SKY = 0x02
TRAVEL_DEPART, TRAVEL_TO_GURU = 3, 4                                # fax_travel.c's lines (TO_GURU + temple)
FLIGHTS, COOLDOWN, FREEZE = (30, 40, 50, 60), 60, 15              # the game's seconds (64 frames)
LAST_SECONDS = 3                                                    # a countdown's end, without the time before it
FLIGHT = FLIGHTS[0]                                                 # a Novice's: the lowest band
ELIXIR_ROW = 2                                                      # EOLIS_TOOL_SHOP_ITEMS: dagger, potion, Elixir
SECOND = 64
INTERRUPT_COUNTER = 0x1A                                            # counts NMIs; the game's second is & $3F == 0
PACKAGE = ("faxanadu.remastered", "remastered")                  # the one switch: off is the original game
PARTS = ("bands", "boots-flight", "boots-cooldown", "boots-death", "glass-freeze", "glass-cooldown", "boss",
         "elixir", "tool-shop", "finds", "spring", "travel", "save", "apolune-guru", "original")
GURUS = ("Eolis", "Apolune")                                        # met, for Guru travel's journey
RECORD = b"faxanadu.recharge"                                       # its save-state record: version, boots
#                                                                     running and cooldown, glass running and
#                                                                     cooldown, the Elixir spent


def state():
    return tcp("recharge_state")


def place():
    return ram(AREA), ram(SCREEN)


def gold():
    return int.from_bytes(ram(GOLD, 3), "little")


def settled():
    return ram(SCREEN_READY) == 0


def box_open():
    return ram(TEXTBOX_CLOSED) == 0


def items():
    """The unselected items, in order, and the selected one."""
    return list(ram(ITEM_INVENTORY, 8)[:ram(NUMBER_OF_ITEMS)]), ram(SELECTED_ITEM)


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


def cooldown_started(which, frames=60):
    """Wait for the `which` ("boots" or "glass") cooldown to start, reading it
    with InterruptCounter in one batch, so both are one frame's: (its first
    value, the state, whether the game's second may have ticked since it
    started). It counts down on the game's second (InterruptCounter & $3F ==
    0), so it reads one less if that came between its start and the read:
    possible only if it came after the last read that saw no cooldown."""
    key, last = f"{which}_cooldown", None
    end = frame() + frames
    while frame() < end:
        s, counter = batch(("recharge_state", {}), ("read_ram", dict(addr=INTERRUPT_COUNTER, len=1)))
        now = int(counter["hex"], 16)
        if s[key] > 0:
            ticked = last is None or any((last + d) & 0x3F == 0 for d in range(1, (now - last & 0xFF) + 1))
            return s[key], s, ticked
        last = now
    raise AssertionError(f"timed out waiting for the {which} cooldown")


def fresh(value, ticked):
    """A cooldown just started: COOLDOWN, or one less after a game second."""
    return value == COOLDOWN or ticked and value == COOLDOWN - 1


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


@fax_e2e.recipe("recharge-boots-words", parent="eolis-meat-shop")
def _boots_words(g):
    """Out of the meat shop onto Eolis's screen 1, a Novice with the Wing
    Boots recharging (the record's cooldown), dead: as the Guru speaks, the
    dissolve over (what dying does to the cooldown is checked after it)."""
    warp_out(g, g.save("shop"), "boots-dying", EOLIS, 1, 0x99, PALETTE_EOLIS, 0,
             {SELECTED_ITEM: WING_BOOTS, NUMBER_OF_ITEMS: 0, PLAYER_TITLE: 0}, {2: COOLDOWN})
    until("the cooldown", lambda: state()["boots_cooldown"] > 0, 60)
    g.poke({PLAYER_IS_DEAD: 1})
    dissolve()


@fax_e2e.recipe("recharge-temple", parent="eolis-meat-shop")
def _recharge_temple(g):
    """Out of the meat shop, its door edited to lead to the temple's door on
    Eolis's screen 2, with the Elixir spent, and into the temple."""
    warp_out(g, g.save("shop"), "to-temple", EOLIS, 2, 0x95, PALETTE_EOLIS, 0,
             {SPECIAL_ITEMS: 0, SELECTED_ITEM: NO_ITEM, NUMBER_OF_ITEMS: 0}, {5: 1})
    enter(g, EOLIS_TEMPLE)
    assert state()["elixir_spent"], state()


@fax_e2e.recipe("recharge-apolune-words", parent="recharge-temple")
def _apolune_words(g):
    """In Eolis's temple with the Elixir spent, dead with Apolune his temple:
    as the Guru speaks, the dissolve over (the way to Apolune's Guru)."""
    g.poke({TEMPLE_SPAWN_POINT: 1, PLAYER_IS_DEAD: 1, SPECIAL_ITEMS: 0}, {RECORD.decode(): {5: 1}})
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

    def use():
        """Down+B: the selected item, once he stands (GameLoop_CheckUseCurrentItem
        refuses while jumping or falling, and a warp's poke can leave him a few
        frames above the floor)."""
        until("the hero to stand", lambda: not ram(PLAYER_FLAGS) & 0x05, 120)
        tcp("action", name="down", frames=60)                       # down however late the script's B comes
        wait(6)
        tcp("action", name="b", frames=4)
        wait(24)
        act("down", 1, 0)                                           # let go

    def warp(*a, **kw):
        warp_out(game, *a, **kw)

    def enter_door(building):
        enter(game, building)

    def poke_cooldown(which, seconds, writes=None):
        """Set the `which` ("boots" or "glass") cooldown to `seconds` in the
        record, and check it took: `seconds`, or one less if the game's second
        ticked in the frames the load ran (InterruptCounter, read with it).
        The frame it was set at."""
        before = ram(INTERRUPT_COUNTER)
        poke(writes or {}, {RECORD.decode(): {2 if which == "boots" else 4: seconds}})
        s, counter = batch(("recharge_state", {}), ("read_ram", dict(addr=INTERRUPT_COUNTER, len=1)))
        now = int(counter["hex"], 16)
        ticked = any((before + d) & 0x3F == 0 for d in range(1, (now - before & 0xFF) + 1))
        assert s[f"{which}_cooldown"] in ((seconds, seconds - 1) if ticked else (seconds,)), (s, before, now)
        assert not s[f"{which}_ready"], s
        return frame() - (now - before & 0xFF)

    def await_ready(which, frames):
        until(f"the {which} to be ready", lambda: state()[f"{which}_ready"], frames)

    def hud_shot(name, value_range):
        """Screenshot T: in the blink's on phase, showing a cooldown in `value_range`."""
        until("T: showing a cooldown", lambda: (lambda s: s["hud_drawn"] in value_range and
                                                not s["hud_drawn_blank"])(state()), 600)
        wait(3)
        shot(name)

    def flying_shot(name):
        """Hold A+Up for a moment: the hero rises."""
        y = ram(PLAYER_Y)
        tcp("action", name="a", frames=40)
        tcp("action", name="up", frames=40)
        wait(36)
        assert ram(PLAYER_Y) < y, f"flying: y {ram(PLAYER_Y)} not above {y}"
        shot(name)
        wait(10)

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

        # 1. Wing Boots: the flight for each title band, then a whole cycle as a Novice.
        if args.wants("bands"):
            start()
            for band, flight in enumerate(FLIGHTS):
                fixture(town, boots, {HP: 0x50, SELECTED_ITEM: WING_BOOTS, NUMBER_OF_ITEMS: 0,
                                      PLAYER_TITLE: band * 4 + 3, **no_one})
                tcp("load_state", path=str(boots))
                wait(10)
                use()
                dismiss(USED_WING_BOOTS)
                assert ram(DURATION_WING_BOOTS) in (flight, flight - 1), f"title {band * 4 + 3}: {ram(DURATION_WING_BOOTS)} s"
            print(f"ok: the Wing Boots fly {', '.join(map(str, FLIGHTS))} s by title band (the original: 40, 30, 20, 10)")
        if args.wants("boots-flight"):
            start()
            warp(shop, "boots-novice", EOLIS, 1, 0x99, PALETTE_EOLIS, 0,  # walked out, so the HUD shows the pair
                 {SELECTED_ITEM: WING_BOOTS, NUMBER_OF_ITEMS: 0, PLAYER_TITLE: 0})
            clear()
            use()
            dismiss(USED_WING_BOOTS)
            assert ram(DURATION_WING_BOOTS) in (FLIGHT, FLIGHT - 1), f"flight {ram(DURATION_WING_BOOTS)}, not {FLIGHT} s"
            assert items() == ([], WING_BOOTS), items()
            s = state()
            assert s["boots_running"] == 1 and s["hud"] == -1 and s["kept"] >= 1, s
            flying_shot("flying")
            print(f"ok: a Novice's Wing Boots fly {FLIGHT} s and the pair stays selected")
            poke({DURATION_WING_BOOTS: LAST_SECONDS})                  # the game counts the flight down itself
            begun = frame()
            dismiss(WING_BOOTS_GONE, (LAST_SECONDS + 2) * SECOND)
            flown = frame() - begun
            value, s, ticked = cooldown_started("boots")
            assert fresh(value, ticked) and not s["boots_ready"] and s["hud"] == value, s
            hud_shot("boots-recharging", range(COOLDOWN - 2, COOLDOWN + 1))
            blinks = set()
            for _ in range(40):
                blinks.add(state()["hud_drawn_blank"])
                wait(4)
            assert blinks == {True, False}, f"T: should blink while recharging: {blinks}"
            use()
            wait(30)
            s = state()
            assert s["refused"] == 1 and ram(DURATION_WING_BOOTS) == 0xFF and not box_open(), s
            assert items() == ([], WING_BOOTS), items()
            print(f"ok: the flight's last {LAST_SECONDS} s ran out after {flown} frames; T: blinks the {COOLDOWN} s cooldown, and Down+B "
                  "meanwhile does nothing but the invalid-choice sound")
        if args.wants("boots-cooldown"):
            # The cooldown a flight's end leaves (boots-flight checks it starts
            # at 60 s): the record's, set as the screen settles, then its 60 s.
            start()
            warp(shop, "boots-cooling", EOLIS, 1, 0x99, PALETTE_EOLIS, 0,
                 {SELECTED_ITEM: WING_BOOTS, NUMBER_OF_ITEMS: 0, PLAYER_TITLE: 0})
            clear()
            cooling_since = poke_cooldown("boots", COOLDOWN)
            await_ready("boots", (COOLDOWN + 2) * SECOND)
            waited = frame() - cooling_since
            assert (COOLDOWN - 1) * SECOND <= waited <= (COOLDOWN + 1) * SECOND, f"the cooldown took {waited} frames"
            wait(4)
            assert state()["hud_drawn"] == 0, state()              # T: back to the idle 00
            use()
            dismiss(USED_WING_BOOTS)
            assert ram(DURATION_WING_BOOTS) in (FLIGHT, FLIGHT - 1) and items() == ([], WING_BOOTS), items()
            flying_shot("flying-again")
            print(f"ok: after {waited} frames of cooldown the same pair flies again")
        if args.wants("boots-death"):
            # Dead with them recharging, as the Guru speaks: a saved game of the
            # death after its dissolve.
            start(at="recharge-boots-words")
            respawn(600)
            s = state()
            assert s["boots_ready"] and s["boots_cooldown"] == 0 and items() == ([], WING_BOOTS), s
            print("ok: dying makes the recharging Wing Boots ready")

        # 2. The Hour Glass on Eolis screen 4.
        if args.wants("glass-freeze"):
            start()
            warp(shop, "glass", EOLIS, 4, 0x9E, PALETTE_EOLIS, 0, {SELECTED_ITEM: HOUR_GLASS, NUMBER_OF_ITEMS: 0})
            zozuras = [i for i, e in enumerate(ram(SPRITE_ENTITIES, 8)) if e == ZOZURA]
            assert zozuras, "Zozuras on screen 4"
            use()
            dismiss(USED_HOUR_GLASS)
            assert ram(DURATION_HOUR_GLASS) in (FREEZE, FREEZE - 1) and ram(HP) == 0x28, (ram(DURATION_HOUR_GLASS), ram(HP))
            before = [ram(SPRITE_X + i) for i in zozuras]
            wait(90)
            assert [ram(SPRITE_X + i) for i in zozuras] == before, "the Zozuras froze"
            shot("hour-glass")
            print("ok: the Hour Glass freezes the Zozuras for 15 s and halves HP (80 to 40)")
            poke({DURATION_HOUR_GLASS: LAST_SECONDS})                  # the freeze's last seconds, the game's count
            dismiss(HOUR_GLASS_GONE, (LAST_SECONDS + 2) * SECOND)
            value, s, ticked = cooldown_started("glass")
            clear(ZOZURA)
            assert fresh(value, ticked) and s["hud"] == value and items() == ([], HOUR_GLASS), s
            refused = state()["refused"]
            use()
            wait(30)
            assert state()["refused"] == refused + 1 and ram(DURATION_HOUR_GLASS) == 0xFF and not box_open(), state()
            print(f"ok: when the freeze ends the Hour Glass recharges for {COOLDOWN} s, refusing meanwhile")
        if args.wants("glass-cooldown"):
            # The end of the cooldown the freeze's end leaves (glass-freeze
            # checks it starts at 60 s, and boots-cooldown that one counts all
            # 60 down: the same countdown), from its last seconds, with the HP
            # the freeze halved and the Zozuras cleared, as it left them.
            start()
            warp(shop, "glass-cooling", EOLIS, 4, 0x9E, PALETTE_EOLIS, 0, {SELECTED_ITEM: HOUR_GLASS, NUMBER_OF_ITEMS: 0})
            clear(ZOZURA)
            cooling_since = poke_cooldown("glass", LAST_SECONDS, {HP: 0x28})
            await_ready("glass", (LAST_SECONDS + 2) * SECOND)
            waited = frame() - cooling_since
            assert (LAST_SECONDS - 1) * SECOND <= waited <= (LAST_SECONDS + 1) * SECOND, f"the cooldown took {waited} frames"
            use()
            dismiss(USED_HOUR_GLASS)
            assert ram(DURATION_HOUR_GLASS) in (FREEZE, FREEZE - 1) and ram(HP) == 0x14 and items() == ([], HOUR_GLASS)
            print(f"ok: the Hour Glass's cooldown counts its last {LAST_SECONDS} s down, then it freezes again (HP 40 to 20)")

        if args.wants("boss"):
            start()
            # 3. Ripasheiku, at the top of the Tower of Trunk.
            warp(shop, "boss", TRUNK, 0x15, 0x95, PALETTE_TOWER, 1, {SELECTED_ITEM: HOUR_GLASS, NUMBER_OF_ITEMS: 0})
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
            target = out / "boss-entry.cycstate"                        # the Hour Glass running in the shop
            fixture(shop, target, {SAVED_AREA: TRUNK, SAVED_SCREEN: 0x15, SAVED_PALETTE: PALETTE_TOWER, SAVED_POS: 0x95,
                                   REGION: 1, HP: 0x50, SELECTED_ITEM: HOUR_GLASS, NUMBER_OF_ITEMS: 0,
                                   DURATION_HOUR_GLASS: 12}, {3: 1})
            tcp("load_state", path=str(target))
            for _ in range(200):
                if ram(AREA) != BUILDINGS:
                    break
                act("right", 2, 0)
            entered, left = frame(), ram(DURATION_HOUR_GLASS)            # at the door, before the screen's first frame
            until("the Hour Glass to end", lambda: ram(DURATION_HOUR_GLASS) == 0xFF, 3 * SECOND)
            ended = frame() - entered
            assert 5 <= left <= 12 and ended <= 2 * SECOND, (left, ended)
            dismiss(HOUR_GLASS_GONE, 300)
            value, s, ticked = cooldown_started("glass")
            assert fresh(value, ticked), s
            print(f"ok: walking onto the boss's screen with {left} s of the Hour Glass left ends it within "
                  f"{ended} frames (LimitItemDurations), then its cooldown starts")

        if args.wants("elixir"):
            start()
            # 4. The Elixir: a revive, a death with it spent, a Guru.
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
            assert saved["recharge.wingboots"] == "0" and saved["recharge.hourglass"] == "0", saved
            # To Eolis's temple through its door (zone respawn, on by default, brings the hero back at the
            # zone's entrance rather than the temple), with the Elixir spent as it is now.
            warp(shop, "to-temple", EOLIS, 2, 0x95, PALETTE_EOLIS, 0,
                 {SPECIAL_ITEMS: 0, SELECTED_ITEM: NO_ITEM, NUMBER_OF_ITEMS: 0}, {5: 1})
            enter_door(EOLIS_TEMPLE)
            assert state()["elixir_spent"], state()
            temple = out / "temple.cycstate"
            tcp("save_state", path=str(temple))
            status_shot("status-elixir-spent")
            print("ok: spent, the Elixir does not save him; he dies and respawns with it still spent, the respawn's "
                  "save writes recharge.elixir = spent to file 1, and the status page does not show it")
            talk_to_guru()
            until("the Guru to charge it", lambda: state()["guru_recharges"] == 1, 120)
            s = state()
            assert s["elixir_charged"] and not s["elixir_spent"], s
            page_through()
            status_shot("status-elixir-recharged")
            print("ok: talking to Eolis's Guru charges the spent Elixir, and the status page shows it again")

        if args.wants("tool-shop"):
            start()
            # 5. Eolis's tool shop: an Elixir, spent or charged, is the hero's, and buying never charges it.
            gold_1000 = {GOLD: 0xE8, GOLD + 1: 0x03, GOLD + 2: 0}
            warp(shop, "tool-shop", EOLIS, 3, 0x9A, PALETTE_EOLIS, 0,
                 {SPECIAL_ITEMS: 0, SELECTED_ITEM: NO_ITEM, NUMBER_OF_ITEMS: 0, **gold_1000}, {5: 1})
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
            # 6. Finds: each recharges what it duplicates.
            warp(shop, "find-boots", TRUNK, 0x2D, 0x31, PALETTE_TOWER, 1,
                 {SELECTED_ITEM: WING_BOOTS, NUMBER_OF_ITEMS: 0, VISIT_COUNT: 3}, {2: COOLDOWN})
            assert state()["boots_cooldown"] > 0, state()
            clear(0x07)                                                 # it shows once the hornets are gone, under
            for _ in range(60):                                         # his feet (it lies at x 16, by the wall)
                if ram(MESSAGE_ID) == GOT_WING_BOOTS:
                    break
                act("left", 2, 2)
            until("the find", lambda: ram(MESSAGE_ID) == GOT_WING_BOOTS, 600)
            type_out("the line to type out", 900, pauses=False)
            assert not state()["boots_ready"], state()                  # Player_PickUpItem comes after the line
            shot("find-boots-message")
            page_through()
            wait(4)
            s = state()
            assert s["boots_ready"] and s["pickups"] == 1 and items() == ([], WING_BOOTS), (s, items())
            until("T: to show 00", lambda: state()["hud_drawn"] == 0, 60)
            wait(2)
            shot("find-boots-after")
            warp(shop, "find-glass", TRUNK, 0x13, 0x3D, PALETTE_TOWER, 1,
                 {SELECTED_ITEM: HOUR_GLASS, NUMBER_OF_ITEMS: 0, VISIT_COUNT: 3}, {4: COOLDOWN})
            assert state()["glass_cooldown"] > 0, state()
            clear(0x47)
            clear(0x26)
            dismiss(GOT_HOUR_GLASS, 600)
            s = state()
            assert s["glass_ready"] and s["pickups"] == 2 and items() == ([], HOUR_GLASS), (s, items())
            warp(shop, "find-elixir", TRUNK, 0x3C, 0x22, PALETTE_TOWER, 1,
                 {SPECIAL_ITEMS: 0, SELECTED_ITEM: NO_ITEM, NUMBER_OF_ITEMS: 0}, {5: 1})
            assert state()["elixir_spent"], state()
            clear(0x07)
            dismiss(GOT_ELIXIR, 600)
            until("the Elixir charged", lambda: not state()["elixir_spent"], 60)
            assert state()["elixir_charged"], state()
            print("ok: a found pair of Wing Boots, an Hour Glass and an Elixir each recharge the one held, "
                  "without a second copy")

        if args.wants("spring"):
            start()
            # 7. The Spring of Sky: from Trunk $20's floor below the shaft to the man in $25. The enemies of
            # $20, $23 and $24 are cleared as the hero reaches each: the flight is what is measured.
            warp(shop, "spring", TRUNK, 0x20, 0x94, PALETTE_OUTSIDE, 1,
                 {SELECTED_ITEM: WING_BOOTS, NUMBER_OF_ITEMS: 0, QUESTS: 0x01, PLAYER_TITLE: 0})
            use()
            dismiss(USED_WING_BOOTS)
            start = frame()
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
            landed = frame() - start
            left = ram(DURATION_WING_BOOTS)
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
            page_through(hurried=False)                                 # his dots stall under a hurried A on a busy machine
            assert ram(QUESTS) & QUEST_SPRING_OF_SKY, "the Spring of Sky's man woke"
            print(f"ok: the Spring of Sky: from Trunk $20's floor to the man in $25 in {landed} frames "
                  f"({landed / SECOND:.1f} of the game's seconds), with {left} s of the {FLIGHT} s flight left")
            game.close()

        if args.wants("travel"):
            # 8. Guru travel: a journey keeps the flight and the cooldown counting.
            start(gurus=GURUS)
            travel = out / "temple-travel.cycstate"
            fixture(temple, travel, {SPECIAL_ITEMS: RING_OF_ELF, TEMPLE_SPAWN_POINT: 1, DURATION_WING_BOOTS: 25,
                                     SELECTED_ITEM: WING_BOOTS, NUMBER_OF_ITEMS: 0}, {1: 1, 2: 0, 4: 50})
            tcp("load_state", path=str(travel))
            wait(4)
            s = state()
            assert s["boots_running"] == 1 and 48 <= s["glass_cooldown"] <= 50 and ram(PLAYER_STATUS) & 0x80, s
            talk_to_guru()                                              # with the ring, he offers Apolune
            # His line, the intro, the offer: A to each, and none on the offer (page_to()).
            page_to(lambda s: s["text"] == TRAVEL_TO_GURU + 1, "the offer of Apolune", tries=20)
            flying, cooling = ram(DURATION_WING_BOOTS), state()["glass_cooldown"]
            act("a", 6, 14)
            until("the farewell", lambda: tcp("travel_state")["text"] == TRAVEL_DEPART, 300)
            page_through()
            until("Apolune's temple", lambda: place() == (BUILDINGS, APOLUNE_TEMPLE) and settled(), 2400)
            wait(SECOND + 6)                                            # a game second in the main loop
            s, after = state(), ram(DURATION_WING_BOOTS)
            assert s["boots_running"] == 1 and 0 < after < flying and ram(PLAYER_STATUS) & 0x80, (s, after)
            assert 0 < s["glass_cooldown"] < cooling, s
            poke({DURATION_WING_BOOTS: LAST_SECONDS})                  # the flight's last seconds, the game's count
            dismiss(WING_BOOTS_GONE, (LAST_SECONDS + 2) * SECOND)
            value, s2, ticked = cooldown_started("boots")
            assert fresh(value, ticked), s2
            print(f"ok: a Guru travel journey from Eolis to Apolune keeps the flight ({flying} s left before, {after} "
                  f"after) and the Hour Glass's cooldown ({cooling} s, then {s['glass_cooldown']}), and the flight's "
                  "cooldown follows it")

        if args.wants("save"):
            # 9. Save files: a Guru's save keeps the cooldowns in file 1; continuing it after quitting puts them
            # back, and a file whose Elixir is spent continues with it spent.
            start(gurus=GURUS)
            keep = out / "temple-save.cycstate"
            fixture(temple, keep, {SPECIAL_ITEMS: RING_OF_ELF | BIT_ELIXIR, SELECTED_ITEM: WING_BOOTS,
                                   NUMBER_OF_ITEMS: 0}, {1: 0, 2: 45, 3: 0, 4: 30, 5: 0})
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
            boots_left, glass_left = int(saved["recharge.wingboots"]), int(saved["recharge.hourglass"])
            assert 40 <= boots_left <= 45 and 25 <= glass_left <= 30 and "recharge.elixir" not in saved, saved
            game.close()
            saved["recharge.elixir"] = "spent"                         # as a respawn with it spent writes it
            saved["special_items"] = str(int(saved["special_items"]) & ~BIT_ELIXIR)
            write_file(out / "game" / FILE, saved)
            launch()
            until("the file select", lambda: frame() > 700)
            tcp("pad", buttons="start", frames=10)                      # file 1: continue
            until("Eolis's temple", lambda: place() == (BUILDINGS, EOLIS_TEMPLE) and settled(), 1800)
            wait(30)
            s = state()
            assert boots_left - 2 <= s["boots_cooldown"] <= boots_left and glass_left - 2 <= s["glass_cooldown"] <= glass_left, \
                (s, saved)
            assert s["elixir_spent"] and not s["elixir_charged"], s
            hud_shot("continued-recharging", range(1, COOLDOWN + 1))
            print(f"ok: saving at a Guru wrote the cooldowns to file 1 (Wing Boots {boots_left} s, Hour Glass {glass_left} s); "
                  f"continuing it after quitting put them back ({s['boots_cooldown']} s, {s['glass_cooldown']} s), "
                  "and a spent Elixir in the file is still spent")
            game.close()

        if args.wants("apolune-guru"):
            # 10. A temple Guru: Apolune's, whose conversation Save files and Guru travel steer, charges a spent
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
            until("his greeting", lambda: ram(MESSAGE_ID) == MEDITATE_WITH_YOU, 300)
            until("the Guru to charge it", lambda: state()["guru_recharges"] == 1, 120)
            s = state()
            assert s["elixir_charged"] and not s["elixir_spent"], s
            # His greeting, then "Remember my journey." (A on his menu saves).
            page_to(lambda s: s["saves"] == saves + 1, "the save", "files_state", tries=20)
            for _ in range(20):                                         # B closes what follows
                if not box_open():
                    break
                act("b", 6, 20)
            assert not box_open() and state()["elixir_charged"], state()
            print("ok: Apolune's Guru, whose conversation Save files and Guru travel steer, charges the spent Elixir "
                  "as he greets the hero, and remembers the journey")
            game.close()

        if args.wants("original"):
            # 11. The remaster off: the original's one-use pair and its title-based flight.
            start(on=False)
            fixture(town, boots, {HP: 0x50, SELECTED_ITEM: WING_BOOTS, NUMBER_OF_ITEMS: 0, PLAYER_TITLE: 0, **no_one})
            tcp("load_state", path=str(boots))
            wait(10)
            use()
            dismiss(USED_WING_BOOTS)
            assert ram(DURATION_WING_BOOTS) in (40, 39) and items() == ([], NO_ITEM), (ram(DURATION_WING_BOOTS), items())
            print("ok: with the remaster off, a Novice's Wing Boots fly 40 s and are used up, as in the original")
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
