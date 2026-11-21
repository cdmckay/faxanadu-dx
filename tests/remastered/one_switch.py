"""Play the one Faxanadu Remastered switch end to end in a hidden window.

    python3 tests/remastered/one_switch.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/one-switch

The game runs uncapped and the script waits on the game's own state, never on
the clock, on a reserved TCP port (tests/e2e/fax_e2e.py), so any number of
runs can go in parallel. It uses Block's test harness (tests/block/harness.py).

Copies the build to OUT and starts it with no mod selection, as a fresh
install does:

1. The mod runtime writes the selection (mods/state.toml) with two switches,
   Faxanadu Remastered on and Seamless off, and every feature of the remaster
   (each in mods/allocations.toml) reports itself on over TCP. The title shows
   the file select. A new game reaches Eolis's gate, and the game's own mantra
   encoder gives the hero's mantra there.
2. In that selection, the launcher's own file, Faxanadu Remastered is switched
   off, as the launcher's Mods screen does, and the game starts again with the
   mantra (--password). Every feature reports itself off, and the game is the
   original: the title's Start and Continue, Continue's mantra screen with the
   mantra filled in, its END into the game, and Eolis's tool shop, whose
   seller greets the hero and asks Buy or Sell with the shop's own lines. The
   runtime writes the selection back with the remaster still off.

Screenshots of the file select, the original title, the mantra screen and the
shop's two lines go in OUT.
"""
from pathlib import Path
import re
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests" / "block"))
from harness import AREA, BUILDINGS, MESSAGE_ID, POS_X, Game, fixture, parse_args  # noqa: E402

PACKAGE, FEATURE = "faxanadu.remastered", "remastered"
SEAMLESS = ("faxanadu.enhancement.seamless-world", "seamless-world")
# Each feature's TCP state, and the field that says it is on.
STATES = {
    "save-files": ("files_state", "enabled"), "guru-travel": ("travel_state", "enabled"),
    "death-keeps-gold": ("death_state", "enabled"), "zone-respawn": ("respawn_state", "enabled"),
    "block": ("block_state", "enabled"), "magic-button": ("magic_state", "enabled"),
    "pendant-fix": ("pendant_state", "enabled"), "taller-dagger": ("dagger_state", "enabled"),
    "sugata-flash": ("sugata_state", "enabled"), "quick-shops": ("shops_state", "enabled"),
    "auto-keys": ("keys_state", "auto"), "reusable-tools": ("keys_state", "reuse"),
    "recharging-items": ("recharge_state", "enabled"),
    "forgiving-jumps": ("forgiving_jumps_state", "enabled"), "falling-pose": ("falling_pose_state", "enabled"),
    "text-speed": ("text_speed_state", "enabled"), "zone-names": ("zone_state", "enabled"),
    "speaker-names": ("speaker_state", "enabled"), "slain-by": ("slain_state", "enabled"),
    "title-remastered": ("title_state", "enabled"),
    "indoor-equip": ("indoor_equip_state", "enabled"),
    "damage-numbers": ("damage_state", "enabled"),
    "inventory-fix": ("inventory_fix_state", "enabled"),
}
START_CHOICE = 0x687                                                # DAT_0687: 0 Start, 1 Continue
PASSWORD_COL, PASSWORD_ROW, PASSWORD_LENGTH = 0x662, 0x663, 0x666
END_ROW, END_COL = 5, 3                                             # END on the mantra screen
PORTRAIT, NO_PORTRAIT = 0x3C7, 0xFF
AREA_SAVED_AREA, AREA_SAVED_SCREEN, PLAYER_SAVED_POS = 0x3D5, 0x3D6, 0x3D8
SCREEN = 0x63
EOLIS, TOOL_SHOP_SCREEN, TOOL_SHOP_DOOR = 0, 3, 0x9A                 # Eolis's tool shop: screen 3, block $9A
GREETING, BUY_OR_SELL = 0x0F, 0x10                                  # the seller's greeting; the Buy/Sell menu's line


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

    door = g.state("gate-door")                                     # made, if need be, before the game starts
    try:
        # 1. On by default.
        g.launch()
        chosen = selection(state_toml.read_text())
        assert chosen == {(PACKAGE, FEATURE): True, SEAMLESS: False}, f"two switches, the remaster on: {chosen}"
        on = features()
        assert all(on.values()), f"every feature is on: {on}"
        assert not g.tcp("fax_world")["enabled"], "Seamless is off"
        print(f"ok: a fresh install selects Faxanadu Remastered (on) and Seamless (off), and all {len(on)} "
              "features of the remaster are on")
        g.until("the file select", lambda: g.tcp("files_state")["title"], 2000)
        g.wait(30)
        g.shot("file-select")
        g.tcp("pad", buttons="start", frames=10)
        g.until("a new game in file 1", lambda: (lambda f: f["file"] == 1 and not f["title"])(g.tcp("files_state")), 300)
        g.load(door)                                                # the opening played: at Eolis's gate
        mantra = g.tcp("mantra_now")["mantra"]
        print(f"ok: the file select starts a new game; at Eolis's gate the hero's mantra is {mantra}")
        g.close()

        # 2. Off, in the launcher's own file.
        text = state_toml.read_text()
        switched = re.sub(r'(package_id = "faxanadu\.remastered"\nid = "remastered"\nenabled = )true', r"\1false",
                          text)
        assert switched != text, text
        state_toml.write_text(switched)
        g.launch("--password", mantra)
        off = features()
        assert not any(off.values()), f"every feature is off: {off}"
        print(f"ok: switched off in the launcher's file, all {len(off)} features of the remaster are off")
        g.until("the title", lambda: g.frame() > 700)
        assert not g.tcp("files_state")["title"], "no file select"
        g.shot("original-title")
        for _ in range(4):
            if g.ram(START_CHOICE) == 1:
                break
            g.act("select")
        assert g.ram(START_CHOICE) == 1, "the title's Continue"
        g.act("start")
        g.until("the mantra screen, with the mantra filled in", lambda: g.ram(PASSWORD_LENGTH) == len(mantra), 600)
        for _ in range(8):
            if g.ram(PASSWORD_ROW) == END_ROW:
                break
            g.act("up")                                             # wraps to the last row
        for _ in range(8):
            if g.ram(PASSWORD_COL) == END_COL:
                break
            g.act("right")
        assert (g.ram(PASSWORD_ROW), g.ram(PASSWORD_COL)) == (END_ROW, END_COL), "the cursor is on END"
        g.shot("mantra-screen")
        g.act("a")
        g.until("the hero in a temple", lambda: g.ram(AREA) == BUILDINGS and g.settled(), 1200)
        g.wait(60)
        print("ok: the title's Continue opens the mantra screen, and its END continues the game")

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
        assert chosen == {(PACKAGE, FEATURE): False, SEAMLESS: False}, f"still off: {chosen}"
        print("ok: the runtime keeps the selection with the remaster off")
        print("screenshots:", ", ".join(g.shots))
    except BaseException:
        if g.running():
            g.shot("failure")
        raise
    finally:
        g.close()


if __name__ == "__main__":
    sys.exit(main())
