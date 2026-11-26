"""Play the one Faxanadu DX switch end to end in a hidden window.

    python3 tests/dx/one_switch.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/one-switch

The game runs uncapped and the script waits on the game's own state, never on
the clock, on a reserved TCP port (tests/e2e/fax_e2e.py), so any number of
runs can go in parallel. It uses Block's test harness (tests/block/harness.py).

Copies the build to OUT and starts it with no mod selection, as a fresh
install does:

1. The build installs one package, faxanadu.dx, named Faxanadu DX in the
   launcher, as is its one feature, `dx`; no faxanadu.remastered, its name
   before #116 (CMake deletes it from build folders). The mod runtime writes
   the selection (mods/state.toml) with that one switch on, and every feature
   of the remaster (each in mods/allocations.toml), Seamless's view among
   them, reports itself on over TCP. The title shows NEW GAME and CONTINUE,
   and NEW GAME in file 1 starts a new game.
2. In that selection, the launcher's own file, Faxanadu DX is switched
   off, as the launcher's Mods screen does, and two packages of earlier
   versions are switched on, as an older install left them: the separate
   Seamless (before #110) and Faxanadu Remastered, as the remaster was named
   before #116 (faxanadu.remastered, feature `remastered`). The game starts
   again, and those stale lines change nothing: every feature reports itself
   off, Seamless too, and the game is the original: the title's Start and
   Continue, Continue's mantra screen, where a new game's mantra (MANTRA,
   what the game's encoder gives at Eolis's gate) is typed with the d-pad and
   A, its END into the game, and Eolis's tool shop, whose seller greets the
   hero and asks Buy or Sell with the shop's own lines. The runtime writes the
   selection back with the remaster still off, and keeps the stale lines,
   which it ignores while no package of their names is installed.
3. A selection from before #116 that switched Faxanadu Remastered off, and
   names no Faxanadu DX: the game starts with every feature on, as DX is on
   by default and the stale line is ignored, and the runtime writes DX's
   switch on beside the stale line, which it keeps.

Screenshots of the remaster's title, the original title, the mantra screen and
the shop's two lines go in OUT.
"""
from pathlib import Path
import re
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests" / "block"))
from harness import AREA, BUILDINGS, MESSAGE_ID, POS_X, Game, fixture, parse_args  # noqa: E402
from fax_e2e import choose_file, title_menu  # noqa: E402

PACKAGE, FEATURE = "faxanadu.dx", "dx"
OLD_SEAMLESS = ("faxanadu.enhancement.seamless-world", "seamless-world")   # its own package before #110
OLD_REMASTERED = ("faxanadu.remastered", "remastered")                        # the one switch's ids before #116
# Each feature's TCP state, and the field that says it is on.
STATES = {
    "save-files": ("files_state", "enabled"), "guru-travel": ("travel_state", "enabled"),
    "death-keeps-gold": ("death_state", "enabled"), "zone-respawn": ("respawn_state", "enabled"),
    "block": ("block_state", "enabled"), "magic-button": ("magic_state", "enabled"),
    "pendant-fix": ("pendant_state", "enabled"), "taller-dagger": ("dagger_state", "enabled"),
    "sugata-flash": ("sugata_state", "enabled"), "quick-shops": ("shops_state", "enabled"),
    "auto-keys": ("keys_state", "auto"), "reusable-tools": ("keys_state", "reuse"),
    "recharging-items": ("recharge_state", "enabled"), "charges": ("charges_state", "enabled"),
    "item-box": ("item_box_state", "enabled"),
    "mana-refill": ("mana_state", "enabled"),
    "magician-lesson": ("lesson_state", "enabled"),
    "double-gold": ("gold_state", "enabled"),
    "forgiving-jumps": ("forgiving_jumps_state", "enabled"), "falling-pose": ("falling_pose_state", "enabled"),
    "ladder-fixes": ("ladders_state", "enabled"), "ladder-hold": ("ladder_hold_state", "enabled"),
    "text-speed": ("text_speed_state", "enabled"), "zone-names": ("zone_state", "enabled"),
    "speaker-names": ("speaker_state", "enabled"), "slain-by": ("slain_state", "enabled"),
    "title-remastered": ("title_state", "enabled"),
    "indoor-equip": ("indoor_equip_state", "enabled"),
    "damage-numbers": ("damage_state", "enabled"),
    "inventory-fix": ("inventory_fix_state", "enabled"), "small-fixes": ("small_fixes_state", "enabled"),
    "kept-finds": ("kept_finds_state", "enabled"),
    "eolis-shaft": ("eolis_shaft_state", "enabled"),
    "map": ("map_state", "enabled"),
    "graphics-fix": ("graphics_fix_state", "enabled"),
    "no-flicker": ("no_flicker_state", "enabled"),
    "quest-log": ("quest_state", "enabled"),
    "fair-entrances": ("entrances_state", "enabled"),
    "spell-cost": ("spell_cost_state", "enabled"),
    "found-items": ("found_state", "enabled"),
    "select-menu": ("select_menu_state", "enabled"),
    "controls": ("controls_state", "enabled"), "use-item": ("use_item_state", "enabled"),
    "seamless-world": ("fax_world", "enabled"), "seams": ("seams_state", "enabled"),
    "punches": ("punches_state", "enabled"),
}
START_CHOICE = 0x687                                                # DAT_0687: 0 Start, 1 Continue
PASSWORD_COL, PASSWORD_ROW, PASSWORD_MAX, PASSWORD_LENGTH = 0x662, 0x663, 0x665, 0x666
PASSWORD_CHARS = 0x600                                              # Password_EnteredChars: 6-bit values
END_ROW, END_COL, COLUMNS = 5, 3, 13                                # END on the mantra screen; 13 to a row
MANTRA = "rFAAAAQAAA"                                               # a new game's, at Eolis's gate
CHARACTERS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789,?"
PORTRAIT, NO_PORTRAIT = 0x3C7, 0xFF
AREA_SAVED_AREA, AREA_SAVED_SCREEN, PLAYER_SAVED_POS = 0x3D5, 0x3D6, 0x3D8
SCREEN = 0x63
EOLIS, TOOL_SHOP_SCREEN, TOOL_SHOP_DOOR = 0, 3, 0x9A                 # Eolis's tool shop: screen 3, block $9A
GREETING, BUY_OR_SELL = 0x0F, 0x10                                  # the seller's greeting; the Buy/Sell menu's line


def move_to(g, row, col):
    """The mantra screen's cursor to (row, col): rows 0-4 hold the 64
    characters in order, 13 to a row (PASSWORD_VALUES_FOR_CURSOR_POS); row 5
    is the commands, END the fourth."""
    for name, addr, want in (("down" if row >= g.ram(PASSWORD_ROW) else "up", PASSWORD_ROW, row),
                             ("right" if col >= g.ram(PASSWORD_COL) else "left", PASSWORD_COL, col)):
        for _ in range(COLUMNS):
            if g.ram(addr) == want:
                break
            g.act(name)
    assert (g.ram(PASSWORD_ROW), g.ram(PASSWORD_COL)) == (row, col), (g.ram(PASSWORD_ROW), g.ram(PASSWORD_COL))


def stale(old, version, enabled):
    """mods/state.toml lines that select a package of earlier versions."""
    package, feature = old
    return (f'\n[[package]]\nid = "{package}"\nversion = "{version}"\n'
            f'\n[[feature]]\npackage_id = "{package}"\nid = "{feature}"\nenabled = {"true" if enabled else "false"}\n')


def selection(text):
    """{(package, feature): enabled} in a mods/state.toml."""
    return {(f["package_id"], f["id"]): f["enabled"] for f in tomllib.loads(text).get("feature", [])}


def main():
    args = parse_args(__doc__)
    remaster = sorted(f["id"] for f in tomllib.loads((ROOT / "mods/allocations.toml").read_text())["feature"]
                      if f["package"] == PACKAGE)
    assert remaster == sorted(STATES), f"every feature of the remaster has a state here: {remaster}"
    g = Game.from_args(args)
    state_toml = g.out / "game" / "mods" / "state.toml"
    state_toml.unlink(missing_ok=True)                              # a fresh install: no selection yet

    def features():
        """Each feature of the remaster: whether it says it is on."""
        replies = {}
        return {f: (replies.setdefault(cmd, g.tcp(cmd)))[field] for f, (cmd, field) in STATES.items()}

    try:
        # 1. On by default.
        packages = g.out / "game" / "mods" / "packages"
        manifest = tomllib.loads(next((packages / PACKAGE).glob("*/manifest.toml")).read_text())
        names = (manifest["name"], [f["name"] for f in manifest["feature"] if f["id"] == FEATURE])
        assert names == ("Faxanadu DX", ["Faxanadu DX"]), names
        assert not (packages / OLD_REMASTERED[0]).exists(), f"{OLD_REMASTERED[0]} is installed"
        g.launch()
        chosen = selection(state_toml.read_text())
        assert chosen == {(PACKAGE, FEATURE): True}, f"one switch, the remaster on: {chosen}"
        on = features()
        assert all(on.values()), f"every feature is on: {on}"
        print(f"ok: a fresh install has one package, {PACKAGE} (Faxanadu DX), selects its one switch, {FEATURE}, "
              f"on, and all {len(on)} features of the remaster are on, Seamless among them")
        menu = title_menu()
        assert (menu["mode"], menu["cursor"], menu["can_continue"]) == (0, 0, False), menu
        g.wait(30)
        g.shot("remaster-title")
        choose_file(True, 1)
        assert not g.tcp("files_state")["title"]
        print("ok: the title shows NEW GAME and CONTINUE, and NEW GAME in file 1 starts a new game")
        g.close()

        # 2. Off, in the launcher's own file.
        text = state_toml.read_text()
        switched = re.sub(r'(package_id = "faxanadu\.dx"\nid = "dx"\nenabled = )true', r"\1false", text)
        assert switched != text, text
        state_toml.write_text(switched + stale(OLD_SEAMLESS, "0.1.0", True) + stale(OLD_REMASTERED, "1.0.0", True))
        g.launch()
        off = features()
        assert not any(off.values()), f"every feature is off: {off}"
        print(f"ok: switched off in the launcher's file, all {len(off)} features of the remaster are off, Seamless "
              "too, though the file still switches on the separate Seamless and Faxanadu Remastered of earlier "
              "versions")
        g.until("the title", lambda: g.frame() > 700)
        assert not g.tcp("files_state")["title"], "no Save files menu"
        g.shot("original-title")
        for _ in range(4):
            if g.ram(START_CHOICE) == 1:
                break
            g.act("select")
        assert g.ram(START_CHOICE) == 1, "the title's Continue"
        g.act("start")
        g.until("the mantra screen", lambda: g.ram(PASSWORD_MAX) != 0, 600)
        g.wait(30)
        assert g.ram(PASSWORD_LENGTH) == 0, "nothing filled in: the remaster's prefill is gone"
        for ch in MANTRA:
            move_to(g, *divmod(CHARACTERS.index(ch), COLUMNS))
            g.act("a")
        typed = [CHARACTERS[v] for v in g.ram(PASSWORD_CHARS, g.ram(PASSWORD_LENGTH))]
        assert "".join(typed) == MANTRA, typed
        g.act("up")                                                 # from row 0 up wraps to the commands
        move_to(g, END_ROW, END_COL)
        g.shot("mantra-screen")
        g.act("a")
        g.until("the hero in a temple", lambda: g.ram(AREA) == BUILDINGS and g.settled(), 1200)
        g.wait(60)
        print(f"ok: the title's Continue opens the mantra screen, {MANTRA} typed there, and its END continues the game")

        temple = g.save("temple")
        shop = g.out / "to-tool-shop.cycstate"
        fixture(temple, shop, {AREA_SAVED_AREA: EOLIS, AREA_SAVED_SCREEN: TOOL_SHOP_SCREEN,
                               PLAYER_SAVED_POS: TOOL_SHOP_DOOR})
        g.load(shop)
        g.walk_out()
        assert g.where() == (EOLIS, TOOL_SHOP_SCREEN), g.where()
        g.wait(30)
        column = (TOOL_SHOP_DOOR & 15) * 16
        g.walk_to(column - 6, column + 4)
        g.act("up", 4, 10)
        g.until("the tool shop", lambda: g.ram(AREA) == BUILDINGS)
        g.wait(40)
        for _ in range(60):                                         # left, to the counter
            before = g.ram(POS_X)
            g.act("left", 8, 2)
            if g.ram(POS_X) == before:
                break
        g.act("up", 4, 6)
        g.until("the seller's greeting", lambda: g.ram(MESSAGE_ID) == GREETING, 600)
        assert not g.tcp("shops_state")["active"], "no quick list"
        g.until("the greeting to type out", g.typed, 600)
        g.shot("shop-greeting")
        g.act("a", 4, 6)
        g.until("the Buy/Sell menu's own line", lambda: g.ram(MESSAGE_ID) == BUY_OR_SELL, 600)
        g.wait(60)                                                  # the menu's two lines type out
        g.shot("shop-buy-or-sell")
        print("ok: Eolis's Tool Seller greets the hero and asks Buy or Sell, the original's lines")
        g.act("b", 4, 6)
        g.close()
        chosen = selection(state_toml.read_text())
        assert chosen == {(PACKAGE, FEATURE): False, OLD_SEAMLESS: True, OLD_REMASTERED: True}, f"still off: {chosen}"
        print("ok: the runtime keeps the selection with the remaster off, and the stale lines as they were")

        # 3. A selection from before #116, Faxanadu Remastered switched off.
        state_toml.write_text("format_version = 1\n" + stale(OLD_REMASTERED, "1.0.0", False))
        g.launch()
        on = features()
        assert all(on.values()), f"every feature is on: {on}"
        g.close()
        chosen = selection(state_toml.read_text())
        assert chosen == {(PACKAGE, FEATURE): True, OLD_REMASTERED: False}, f"DX on, the stale line kept: {chosen}"
        print(f"ok: a selection from before #116 that switched Faxanadu Remastered off leaves all {len(on)} features "
              "of Faxanadu DX on, its default; the runtime adds its switch, on, and keeps the stale line")
        print("screenshots:", ", ".join(g.shots))
    except BaseException:
        if g.running():
            g.shot("failure")
        raise
    finally:
        g.close()


if __name__ == "__main__":
    sys.exit(main())
