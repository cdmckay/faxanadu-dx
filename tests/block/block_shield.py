"""Play what Block looks like end to end in a hidden window.

    python3 tests/block/block_shield.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/block-shield

Copies the build to OUT and starts a new game over TCP, to Eolis's gate. The
hero's shield-slot item is set in a copy of that save state (save-state RAM,
an external prerequisite), and walking through the gate into Eolis loads its
tiles; once the zone's banner has gone, the townsfolk are sent away (slots
cleared), so the pictures show the hero alone. Block is held the way a player does: TCP `key` feeds the window a real
V key. The hero's sprites are read from the shadow OAM ($0700), which the
game fills each frame and the picture (or Seamless world's) is drawn from.

1. With the Small, Large and Magic Shields, facing right and left: holding
   Block, the shield's two sprites are drawn where the game draws them, and
   its box (Player_ShieldPositionX/Y), which decides what it stops, is where
   the game puts it; one glint is counted. Let go, nothing has moved.
2. With no shield, nothing changes; with the Battle Helmet, no shield is
   drawn, and a glint is counted.
3. At normal speed, with each shield and the Battle Helmet, facing right and
   left: Block going up makes the hero's body glint in the shield's palette
   (1) for 4 frames, his own (0) for 4, the shield's for 4, then his own
   while Block is still held. The shadow OAM is read every frame, on one
   connection; the shield's sprites never change. The pictures show it too:
   around the hero, the glint changes only his box, and the picture between
   the beats and after them is the one before Block went up. A lagging
   script can miss a beat: up to 5 tries each, counting game frames.
4. At normal speed: A pressed in the air just before landing a jump is kept
   by forgiving jumps; Block going up before the landing drops it, so he
   does not jump on landing.

Screenshots of each, in OUT.
"""
from pathlib import Path
import struct
import sys
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parent))
from harness import (Game, fixture, parse_args, POS_X, POS_Y, PLAYER_FLAGS, FACING_RIGHT, SELECTED_SHIELD,  # noqa: E402
                     ENTITIES, SMALL, LARGE, MAGIC, HELMET, NO_SHIELD)
import fax_e2e  # noqa: E402  (on the path through harness)

SHADOW_OAM, SHIELD_BOX = 0x700, 0x39B         # Player_ShieldPositionX/Y
INTERRUPT_COUNTER = 0x1A                       # counts every NMI: one a frame
JUMP_CURVE, JUMPING = 0xA6, 0x01               # Player_JumpCurveIndex; Player_Flags' jumping bit
HUD, SHIELD_TILES = 32, range(0x30, 0x36)      # Sprite_Draw adds the HUD; Sprites_PPUOffset $30 for the shield
GLINT_BEAT, GLINT = 4, 12                      # frames a beat; frames in all: on, off, on
ROOM_W, ROOM_H = 256, 208                      # a room in Seamless world's picture, under the HUD
NAMES = {SMALL: "small", LARGE: "large", MAGIC: "magic", HELMET: "helmet"}


def glinting(shown):
    """The glint's frame `shown` (0 = its first) is in the shield's palette."""
    return 0 <= shown < GLINT and shown // GLINT_BEAT % 2 == 0


def png(path):
    """A screenshot's pixels: (bytes a pixel, rows of bytes). The game writes
    8-bit RGB or RGBA with no filter (cyc_png.c)."""
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
    raw, stride = zlib.decompress(idat), width * bpp + 1
    assert all(raw[y * stride] == 0 for y in range(height)), f"{path.name}: a row with a filter"
    return bpp, [raw[y * stride + 1:(y + 1) * stride] for y in range(height)]


def changed(before, after, box):
    """Where two pictures differ within box (left, top, right, bottom), as
    a box; None if nowhere."""
    (bpp, a), (_, b) = png(before), png(after)
    assert len(a) == len(b) and len(a[0]) == len(b[0]), (before.name, after.name)
    left, top, right, bottom = box
    xs, ys = [], []
    for y in range(max(top, 0), min(bottom, len(a))):
        ra, rb = a[y][left * bpp:right * bpp], b[y][left * bpp:right * bpp]
        if ra != rb:
            ys.append(y)
            xs += [left + x // bpp for x in range(len(ra)) if ra[x] != rb[x]]
    return (min(xs), min(ys), max(xs) + 1, max(ys) + 1) if ys else None


def _into_eolis(shield):
    def how(g):
        """From the gate, with this shield in the shield slot, into Eolis (its
        tiles load on the way), facing right, once the zone's banner has
        gone; the townsfolk sent away, for clear pictures."""
        g.poke({SELECTED_SHIELD: shield})
        g.act("up", 4, 10)
        g.until("Eolis", lambda: g.where() == (0, 1), 300)
        g.until("the screen to settle", g.settled, 300)
        g.wait(30)
        g.act("right", 20, 10)
        assert g.ram(SELECTED_SHIELD) == shield and g.ram(PLAYER_FLAGS) & FACING_RIGHT
        g.until("Eolis's banner to go", lambda: g.tcp("zone_state")["showing"] is None, 1200)
        g.poke({ENTITIES + i: 0xFF for i in range(8)})
        g.wait(4)
    return how


for _shield, _name in NAMES.items():
    fax_e2e.recipe(f"glint-eolis-{_name}", parent="gate-door")(_into_eolis(_shield))
PARTS = ("drawn", *(f"glint-{name}" for name in NAMES.values()), "jump")   # each its own game


def main():
    args = parse_args(__doc__, parts=PARTS)
    g = Game.from_args(args)

    def block():
        return g.tcp("block_state")

    def parse(oam, at):
        """The hero's sprites in a shadow OAM: the shield's and the body's,
        each as (x, y, palette) relative to the hero at `at` (OAM Y includes
        the HUD)."""
        x, y = at
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

    def sprites():
        """The hero's sprites this frame."""
        return parse(g.ram(SHADOW_OAM, 256), (g.ram(POS_X), g.ram(POS_Y)))

    def around_hero(margin):
        """The hero's 16x32 box in the picture, `margin` wider each way. His
        sprites are a line below their OAM Y, which includes the HUD. Seamless
        world composes rooms of 256x208 under the HUD, the active one at its
        place less the camera (fax_world.c); elsewhere in its picture other
        rooms' actors move, so pictures are compared only here."""
        x, y = g.ram(POS_X), g.ram(POS_Y) + HUD + 1
        world = g.tcp("fax_world")
        if world["enabled"]:
            room = g.tcp("fax_world_room")["xy"]
            x += room[0] * ROOM_W - world["camera"][0]
            y += room[1] * ROOM_H - world["camera"][1]
        return x - margin, y - margin, x + 16 + margin, y + 32 + margin

    def hold(frames=90):
        g.tcp("key", name="V", frames=frames)
        g.until("Block to be up", lambda: block()["raised"], 30)
        g.wait(4)

    def let_go():
        g.until("Block to be let go", lambda: not block()["held"], 200)
        g.wait(4)

    def watch_glint(name):
        """At normal speed: read the hero's sprites as often as the game
        answers, on one connection, with a screenshot in each read until
        there is one of each frame parity (the game swaps its sprite slots
        each frame, so where the shield and the body overlap, which is in
        front alternates); then press V and read on until the glint is well
        over, with Block's state, and a screenshot in each read after the
        first that shows it. A read is one read_ram from InterruptCounter to
        the shadow OAM, so its frame and its sprites are one moment, and the
        commands sent with it are served at the same one. Returns
        {frame: (shield, body)}, {frame: Block's glint counter} after V,
        {frame: screenshot} before V and after the glint shows, and the first
        frame read after V was sent. Gives up 60 frames after V if no read
        has shown the glint."""
        at = g.ram(POS_X), g.ram(POS_Y)
        seen, counts, befores, shots, first, pressed = {}, {}, {}, {}, None, None
        frame = last = None
        for _ in range(240):
            shoot = pressed is None or first is not None
            cmds = [("read_ram", dict(addr=INTERRUPT_COUNTER, len=SHADOW_OAM + 256 - INTERRUPT_COUNTER))]
            if pressed is not None:
                cmds.append(("block_state", {}))
            if shoot:
                path = g.out / f"{len(g.shots):02d}-{name}-{'glint' if pressed else 'before'}.png"
                cmds.append(("screenshot", dict(layer="picture", path=str(path))))
            replies = g.batch(*cmds)
            ram = bytes.fromhex(replies[0]["hex"])
            frame = 0 if frame is None else frame + (ram[0] - last & 0xFF)
            last = ram[0]
            seen[frame] = parse(ram[SHADOW_OAM - INTERRUPT_COUNTER:], at)
            if shoot:
                (shots if pressed else befores)[frame] = path
                g.shots.append(path.name)
            if pressed is None:
                if len({f % 2 for f in befores}) == 2:
                    g.batch(("key", dict(name="V", frames=120)))
                    pressed = frame + 1
                continue
            counts[frame] = replies[1]["glint"]
            body = seen[frame][1]
            if first is None and body and all(p == 1 for _, _, p in body):
                first = frame
            if first is not None and frame - first > 2 * GLINT or first is None and frame - pressed > 60:
                break
        assert pressed is not None, f"{name}: every picture before V had the same frame parity"
        return seen, counts, befores, shots, pressed

    try:
        states = {shield: g.state(f"glint-eolis-{name}") for shield, name in NAMES.items()}
        glinted = {}

        if args.wants("drawn"):
            g.launch()
            # 1. The shield is drawn where the game draws it.
            for shield in (SMALL, LARGE, MAGIC):
                g.load(states[shield])
                g.wait(4)
                for facing in ("right", "left"):
                    if facing == "left":
                        g.act("left", 2, 10)
                    right = facing == "right"
                    assert bool(g.ram(PLAYER_FLAGS) & FACING_RIGHT) == right
                    where = g.ram(POS_X), g.ram(POS_Y)
                    down, body = sprites()
                    box = g.ram(SHIELD_BOX, 2)
                    front = 8 if right else 0
                    assert down == [(front, 8, 1), (front, 16, 1)], down
                    assert box == bytes([where[0] + front, where[1] + 8]), (box, where)
                    g.shot(f"{NAMES[shield]}-{facing}-down")
                    before = block()["glints"]
                    hold()
                    up, held_body = sprites()
                    assert up == down, (up, down)
                    assert [(dx, dy) for dx, dy, _ in held_body] == [(dx, dy) for dx, dy, _ in body], (held_body, body)
                    assert (g.ram(POS_X), g.ram(POS_Y)) == where and g.ram(SHIELD_BOX, 2) == box
                    assert block()["glints"] == before + 1, block()
                    g.shot(f"{NAMES[shield]}-{facing}-held")
                    let_go()
                    again, after = sprites()
                    assert again == down and after == body, (again, down, after, body)
                print(f"ok: the {NAMES[shield]} shield, facing right and left: drawn where the game draws it while "
                      f"Block is held, its box where the game puts it, and a glint as Block goes up")

            # 2. No shield, and the Battle Helmet: no shield sprites.
            target = g.out / "eolis-none.cycstate"
            fixture(states[SMALL], target, {SELECTED_SHIELD: NO_SHIELD})
            g.load(target)
            g.wait(4)
            before = block()["glints"]
            g.tcp("key", name="V", frames=30)
            g.until("V to be held", lambda: block()["held"], 30)
            g.wait(4)
            shield, body = sprites()
            assert not block()["raised"] and shield == [] and all(p == 0 for _, _, p in body)
            assert block()["glints"] == before
            let_go()
            g.load(states[HELMET])
            g.wait(4)
            before = block()["glints"]
            hold(30)
            assert block()["raised"] and sprites()[0] == [] and block()["glints"] == before + 1
            let_go()
            print("ok: without a shield Block does nothing; the Battle Helmet has no shield to draw, and glints")
            g.close()

        # 3. The glint, at normal speed: 4 frames on, 4 off, 4 on. A picture
        # shows the shadow OAM the PPU took at the frame's start, the frame
        # before's; Seamless world's shows the frame's own, as it captures
        # the room when the main loop's pass ends.
        shields = [shield for shield, name in NAMES.items() if args.wants(f"glint-{name}")]
        if shields or args.wants("jump"):
            g.realtime = True
            g.launch()
            lag = 0 if g.tcp("fax_world")["enabled"] else 1
        for shield in shields:
            for facing in ("right", "left"):
                name = f"{NAMES[shield]}-{facing}"
                # A read takes a frame, and on a loaded machine the script
                # can lag past a whole beat: an attempt whose reads or
                # pictures miss one lets go and holds Block again from the
                # same state. What the reads do show must be right every time.
                for attempt in range(5):
                    g.load(states[shield])
                    g.wait(10)
                    if facing == "left":
                        g.act("left", 2, 10)
                    assert bool(g.ram(PLAYER_FLAGS) & FACING_RIGHT) == (facing == "right")
                    down, body = sprites()
                    assert body and all(p == 0 for _, _, p in body), body
                    assert (down != []) == (shield != HELMET), down
                    glints = block()["glints"]
                    hero = around_hero(8)
                    seen, counts, befores, shots, pressed = watch_glint(name)
                    timeline = [(f, "".join(str(p) for _, _, p in b)) for f, (_, b) in sorted(seen.items())]
                    for frame, (shield_now, body_now) in seen.items():
                        assert shield_now == down, (name, frame, shield_now, down)
                        assert len(body_now) == len(body), (name, frame, body_now)
                    lit = [f for f, (_, b) in seen.items() if all(p == 1 for _, _, p in b)]
                    if not lit:                                # no read landed in a beat
                        let_go()
                        continue
                    # The glint's first frame: after the last read without it,
                    # and no later than the first with it; one that every read
                    # agrees with. Block's counter (draws left after this
                    # frame's) names it.
                    first = min(lit)
                    assert first >= pressed, (name, pressed, timeline)
                    starts = [a for a in range(max(f for f in seen if f < first) + 1, first + 1)
                              if all(all(p == glinting(f - a) for _, _, p in b) for f, (_, b) in seen.items())]
                    named = {f - (GLINT - 1 - left) for f, left in counts.items() if left}
                    assert len(named) == 1 and named <= set(starts), (name, starts, named, timeline)
                    start = named.pop()
                    beats = [any(lo <= f - start < hi for f in seen) for lo, hi in ((0, 4), (4, 8), (8, 12), (12, 24))]
                    # Each picture is compared with one from before V of the
                    # same parity, around the hero: the glint changes his box
                    # alone.
                    still = {f % 2: path for f, path in befores.items()}
                    pictured = {}
                    for frame, path in sorted(shots.items()):
                        shown = frame - lag - start
                        kind = "on" if glinting(shown) else "between" if shown < GLINT else "over"
                        spot = changed(still[frame % 2], path, hero)
                        if kind == "on":
                            assert spot and spot[0] >= hero[0] + 8 and spot[1] >= hero[1] + 8, (path.name, hero, spot)
                            assert spot[2] <= hero[2] - 8 and spot[3] <= hero[3] - 8, (path.name, hero, spot)
                        else:
                            assert spot is None, (path.name, kind, spot)
                        pictured.setdefault(kind, (path, still[frame % 2]))
                    if not all(beats) or set(pictured) != {"on", "between", "over"}:
                        let_go()
                        continue
                    glinted[name] = *pictured["on"], hero
                    state = block()
                    assert state["raised"] and state["glints"] == glints + 1 and state["glint"] == 0, state
                    let_go()
                    print(f"ok: {name}: Block going up, the body glints in the shield's palette for frames 1-4 and "
                          f"9-12 ({len(seen)} reads, every {(max(seen) - min(seen)) / (len(seen) - 1):.1f} frames), "
                          f"the shield as the game draws it; the pictures agree (try {attempt + 1})")
                    break
                else:
                    raise AssertionError(f"{name}: the reads or pictures missed a beat of the glint in 5 tries")

        if args.wants("jump"):
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
                g.wait(1)                                      # A goes in first: on its frame, Block would take it
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
        for name, (glint, before, (left, top, right, bottom)) in glinted.items():
            print(f"the glint pictured: {name}: {glint.name}, before it {before.name}, the hero at "
                  f"{left + 8},{top + 8} to {right - 8},{bottom - 8}")
    except BaseException:
        if g.running():
            g.shot("failure")
        raise
    finally:
        g.close()


if __name__ == "__main__":
    sys.exit(main())
