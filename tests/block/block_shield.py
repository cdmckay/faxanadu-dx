"""Play what Block looks like end to end in a hidden window.

    python3 tests/block/block_shield.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/block-shield

Copies the build to OUT and starts a new game over TCP, to Eolis's gate. The
hero's shield-slot item is set in a copy of that save state (save-state RAM,
an external prerequisite), and walking through the gate into Eolis loads its
tiles. Block is held the way a player does: TCP `key` feeds the window a real
V key. The hero's sprites are read from the shadow OAM ($0700), which the
game fills each frame and the picture (or Seamless world's) is drawn from.

1. With the Small, Large and Magic Shields, facing right and left: holding
   Block, the shield's two sprites are drawn 6 pixels higher and 2 further
   the way he faces, before his face; the shield's box
   (Player_ShieldPositionX/Y), which decides what it stops, stays where the
   game puts it. Let go, the sprites are back where the game draws them.
2. With no shield, nothing changes; with the Battle Helmet, no shield is drawn.
3. At normal speed: Block going up with the Battle Helmet makes the hero's
   sprites glint in the shield's palette (1), then go back to his own (0)
   while Block is still held.
4. At normal speed: A pressed in the air just before landing a jump is kept
   by forgiving jumps; Block going up before the landing drops it, so he
   does not jump on landing.

Screenshots of each, in OUT.
"""
import argparse
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from harness import (Game, fixture, POS_X, POS_Y, PLAYER_FLAGS, FACING_RIGHT, SELECTED_SHIELD,  # noqa: E402
                     SMALL, LARGE, MAGIC, HELMET, NO_SHIELD)

SHADOW_OAM, SHIELD_BOX = 0x700, 0x39B         # Player_ShieldPositionX/Y
JUMP_CURVE, JUMPING = 0xA6, 0x01               # Player_JumpCurveIndex; Player_Flags' jumping bit
HUD, SHIELD_TILES = 32, range(0x30, 0x36)      # Sprite_Draw adds the HUD; Sprites_PPUOffset $30 for the shield
RAISE_UP, RAISE_FORWARD = 6, 2
NAMES = {SMALL: "small", LARGE: "large", MAGIC: "magic", HELMET: "helmet"}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for key in ("build", "rom", "out"):
        ap.add_argument("--" + key, type=Path, required=True)
    ap.add_argument("--game-arg", action="append", default=[],
                    help="extra game option, e.g. --game-arg=--world-view=seamless (repeatable)")
    ap.add_argument("--port", type=int, default=0, help="TCP port (default: a free one)")
    args = ap.parse_args()
    g = Game(args.build, args.rom, args.out, args.game_arg, False, args.port)

    def block():
        return g.tcp("block_state")

    def sprites(at=None):
        """The hero's sprites this frame, from the shadow OAM: the shield's and
        the body's, each as (x, y, palette) relative to the hero (OAM Y
        includes the HUD). `at`: where he is, if known (one read fewer)."""
        oam = g.ram(SHADOW_OAM, 256)
        x, y = at or (g.ram(POS_X), g.ram(POS_Y))
        shield, body = [], []
        for i in range(4, 256, 4):                 # sprite 0 is the status bar's
            sy, tile, attr, sx = oam[i:i + 4]
            dx, dy, palette = sx - x, sy - HUD - y, attr & 3
            if sy >= 0xEF or palette > 1:          # not drawn; another actor's palette
                continue
            if tile in SHIELD_TILES and -8 <= dx <= 16 and -8 <= dy <= 16:
                shield.append((dx, dy, palette))
            elif dx in (0, 8) and dy in (0, 8, 16, 24):
                body.append((dx, dy, palette))
        return sorted(shield), sorted(body)

    def into_eolis(shield, name):
        """From the gate, with `shield` in the shield slot, into Eolis: its
        tiles load on the way. Returns a save state there, facing right."""
        start = g.out / f"gate-{name}.cycstate"
        fixture(gate, start, {SELECTED_SHIELD: shield})
        g.load(start)
        g.act("up", 4, 10)
        g.until("Eolis", lambda: g.where() == (0, 1), 300)
        g.until("the screen to settle", g.settled, 300)
        g.wait(30)
        g.act("right", 20, 10)
        assert g.ram(SELECTED_SHIELD) == shield and g.ram(PLAYER_FLAGS) & FACING_RIGHT
        return g.save(f"eolis-{name}")

    def hold(frames=90):
        g.tcp("key", name="V", frames=frames)
        g.until("Block to be up", lambda: block()["raised"], 30)
        g.wait(4)

    def let_go():
        g.until("Block to be let go", lambda: not block()["held"], 200)
        g.wait(4)

    try:
        g.launch()
        g.new_game_at_gate()
        gate = g.save("gate")
        states = {}

        # 1. The raised shield.
        for shield in (SMALL, LARGE, MAGIC):
            states[shield] = into_eolis(shield, NAMES[shield])
            for facing in ("right", "left"):
                if facing == "left":
                    g.act("left", 2, 10)
                right = facing == "right"
                assert bool(g.ram(PLAYER_FLAGS) & FACING_RIGHT) == right
                where = g.ram(POS_X), g.ram(POS_Y)
                down, body = sprites()
                box = g.ram(SHIELD_BOX, 2)
                front = 8 if right else 0
                assert [(dx, dy) for dx, dy, _ in down] == [(front, 8), (front, 16)], down
                g.shot(f"{NAMES[shield]}-{facing}-down")
                before = block()["shields_raised"]
                hold()
                up, raised_body = sprites()
                forward = RAISE_FORWARD if right else -RAISE_FORWARD
                assert [(dx, dy) for dx, dy, _ in up] == [(front + forward, 8 - RAISE_UP), (front + forward, 16 - RAISE_UP)], up
                assert raised_body == body and (g.ram(POS_X), g.ram(POS_Y)) == where, (raised_body, body)
                assert g.ram(SHIELD_BOX, 2) == box == bytes([where[0] + front, where[1] + 8]), (box, where)
                assert block()["shields_raised"] > before and block()["glints"] == 0
                g.shot(f"{NAMES[shield]}-{facing}-up")
                let_go()
                again, _ = sprites()
                assert again == down, (again, down)
            print(f"ok: the {NAMES[shield]} shield, facing right and left: held up {RAISE_UP} and out "
                  f"{RAISE_FORWARD} while Block is held, its box where the game puts it; back down when let go")

        # 2. No shield, and the Battle Helmet: no shield sprites.
        target = g.out / "eolis-none.cycstate"
        fixture(states[SMALL], target, {SELECTED_SHIELD: NO_SHIELD})
        g.load(target)
        g.wait(4)
        g.tcp("key", name="V", frames=30)
        g.until("V to be held", lambda: block()["held"], 30)
        g.wait(4)
        assert not block()["raised"] and sprites()[0] == [] and block()["glints"] == 0
        let_go()
        states[HELMET] = into_eolis(HELMET, "helmet")
        hold(30)
        assert block()["raised"] and sprites()[0] == []
        let_go()
        print("ok: without a shield Block does nothing; the Battle Helmet has no shield to draw")
        g.close()

        # 3. The Battle Helmet's glint, at normal speed: it lasts 12 frames.
        g.realtime = True
        g.launch()
        g.load(states[HELMET])
        g.wait(10)
        _, body = sprites()
        assert body and all(p == 0 for _, _, p in body), body
        g.shot("helmet-down")
        before = block()["glints"]
        at = g.ram(POS_X), g.ram(POS_Y)
        g.tcp("key", name="V", frames=120)
        glinted, end = False, time.time() + 2
        while time.time() < end and not glinted:            # one read a frame
            _, now = sprites(at)
            glinted = bool(now) and all(p == 1 for _, _, p in now) and len(now) == len(body)
        assert glinted, "the body's sprites in the shield's palette"
        g.shot("helmet-glint")                              # the picture shows the frame before
        g.wait(20)
        _, after = sprites()
        assert block()["raised"] and block()["glints"] == before + 1 and all(p == 0 for _, _, p in after), after
        g.shot("helmet-up")
        let_go()
        print("ok: Block going up with the Battle Helmet: the hero glints in the shield's palette, then is himself "
              "again while it is held")

        # 4. The jump buffer, at normal speed, with the Small Shield. A goes in
        # within the jump's last 6 frames, then V at once; an attempt counts
        # when forgiving jumps had kept the press and dropped it as Block went
        # up (it was not used first: he had not landed).
        floor = states[SMALL]
        for attempt in range(8):
            g.load(floor)
            g.wait(4)
            ground, before = g.ram(POS_Y), g.tcp("forgiving_jumps_state")
            g.tcp("action", name="a", frames=2)
            g.until("the jump", lambda: g.ram(PLAYER_FLAGS) & JUMPING, 60)
            g.until("the jump's last frames", lambda: g.ram(JUMP_CURVE) >= 26, 120)
            g.tcp("action", name="a", frames=2)
            g.tcp("key", name="V", frames=60)
            g.until("Block to be up", lambda: block()["raised"], 30)
            g.wait(30)
            state = g.tcp("forgiving_jumps_state")
            if state["blocked"] == before["blocked"]:
                let_go()
                continue                                   # he landed first, or Block took the A itself: again
            assert not state["pending"] and state["buffered"] == before["buffered"], (state, before)
            assert g.ram(POS_Y) == ground and not g.ram(PLAYER_FLAGS) & JUMPING, (g.ram(POS_Y), ground)
            g.shot("jump-kept-then-blocked")
            let_go()
            print(f"ok: A kept from the air, then Block up before landing: the press is dropped and he does not "
                  f"jump on landing (try {attempt + 1})")
            break
        else:
            raise AssertionError("could not press A and raise Block within a jump's last frames")
        print("screenshots:", ", ".join(p.name for p in g.shots))
    except BaseException:
        if g.running():
            g.shot("failure")
        raise
    finally:
        g.close()


if __name__ == "__main__":
    sys.exit(main())
