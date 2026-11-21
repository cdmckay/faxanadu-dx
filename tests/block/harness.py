"""The game in a hidden window over TCP, for Block's and Sugata's flash's end-to-end tests.

The game runs uncapped and the tests wait on the game's own state, never on
the clock, on a reserved TCP port (tests/e2e/fax_e2e.py), so any number can
run in parallel. Save-state RAM is edited (an external prerequisite, not
coverage) to set up fixtures: a shield, gold, or where a building's door leads
(Area_Saved*, as tests/zones/zone_names.py does) to walk out anywhere.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
import fax_e2e  # noqa: E402
from fax_e2e import fixture, parse_args  # noqa: E402,F401  (block.py and sugata_flash.py import them here)

AREA, SCREEN, POS_X, POS_Y = 0x24, 0x63, 0x9E, 0xA1
PLAYER_FLAGS, FACING_RIGHT = 0xA4, 0x40
ENTITIES, SPRITE_X, SPRITE_Y, SPRITE_HP = 0x2CC, 0xBA, 0xC2, 0x344
TEXTBOX_CLOSED, MESSAGE_ID, TEXTBOX_PAUSED = 0x20C, 0x213, 0x215
GOLD = 0x392
SHIELD_INVENTORY, SELECTED_SHIELD, NUMBER_OF_SHIELDS = 0x3A5, 0x3BF, 0x3C4
AREA_SAVED_AREA, AREA_SAVED_SCREEN, SCREEN_SAVED_PALETTE, PLAYER_SAVED_POS = 0x3D5, 0x3D6, 0x3D7, 0x3D8
HP, AREA_REGION, PLAYER_IS_DEAD, TEMPLE_SPAWN_POINT = 0x431, 0x435, 0x438, 0x439
INVINCIBILITY = 0xAD
BUILDINGS, TOWNS = 4, 3
SMALL, LARGE, MAGIC, HELMET, NO_SHIELD = 0, 1, 2, 3, 0xFF


@fax_e2e.recipe("gate-door", parent="gate")
def _gate_door(g):
    """From outside Eolis's gate, right through the hero's thoughts at the
    opening, to the gate (a door)."""
    def act(name, frames=6, after=8):
        g.tcp("action", name=name, frames=frames)
        g.wait(frames + after)
    act("right", 60, 10)
    for _ in range(60):
        if g.ram(POS_X) > 54:
            break
        if fax_e2e.typed():
            act("a", 4, 8)
        else:
            fax_e2e.hurry(10)
        act("right", 6, 2)
    for _ in range(200):
        if g.ram(POS_X) >= 192:
            break
        act("right", 6, 2)
    assert (g.ram(AREA), g.ram(SCREEN)) == (0, 0) and 188 <= g.ram(POS_X) < 200, (g.ram(AREA), g.ram(POS_X))


class Game(fax_e2e.Game):
    """fax_e2e.Game, with what Block's and Sugata's flash's tests add."""

    def __init__(self, build, rom, out, game_args=(), realtime=False, port=0):
        super().__init__(build, rom, out, game_args, realtime, port)
        self.config = self.out / "game" / "config.ini"

    def save(self, name):
        path = self.out / f"{name}.cycstate"
        self.tcp("save_state", path=str(path))
        return path

    def load(self, path):
        self.tcp("load_state", path=str(path))

    # ---- the game ----
    def where(self):
        return self.ram(AREA), self.ram(SCREEN)

    def hp(self):
        return self.ram(HP)

    def typed(self):
        """The text box is waiting for a button: the message has typed out
        (MessageID clears) or reached a pause (TextBox_MessagePaused)."""
        message, _, paused = self.ram(MESSAGE_ID, 3)
        return message == 0 or paused != 0

    def box_open(self):
        return self.ram(TEXTBOX_CLOSED) == 0

    def settled(self):
        """A screen has loaded and is not scrolling (docs/GAME_INTERNALS.md)."""
        return self.ram(0x0E) == 0 and self.ram(0x54) == 0xFF and self.ram(0x13) != 0

    def new_game_at_gate(self):
        """A new game, through the opening, to the gate of Eolis (a door):
        the gate-door fixture."""
        self.load(self.state("gate-door"))

    def walk_to(self, x_min, x_max):
        """Walk until the hero's X is in [x_min, x_max], paging through any text."""
        for _ in range(600):
            if self.box_open():
                if self.typed():
                    self.act("a", 6, 14)
                else:
                    fax_e2e.hurry(4)
                continue
            x = self.ram(POS_X)
            if x_min <= x <= x_max:
                return
            fax_e2e.step_toward(x_min, x_max, self.box_open, POS_X)
        raise AssertionError(f"could not reach x {x_min}-{x_max}")

    def walk_out(self):
        """Walk right out of a building, back to where its door leads."""
        for _ in range(200):
            if self.ram(AREA) != BUILDINGS:
                self.until("the screen to load", self.settled, 600)
                return
            fax_e2e.hold("right", lambda: self.ram(AREA) != BUILDINGS)
        raise AssertionError("could not walk out")

    def warp(self, inside, name, area, screen, palette, pos, region, writes=None):
        """From a save state inside a building, walk out at (area, screen, pos YX)."""
        target = self.out / f"{name}.cycstate"
        fixture(inside, target, {AREA_SAVED_AREA: area, AREA_SAVED_SCREEN: screen, SCREEN_SAVED_PALETTE: palette,
                                 PLAYER_SAVED_POS: pos, AREA_REGION: region, HP: 0x50, **(writes or {})})
        self.load(target)
        self.walk_out()
        assert self.where() == (area, screen), self.where()
        return target
