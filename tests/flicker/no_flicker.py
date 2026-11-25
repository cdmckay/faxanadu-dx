"""Play No sprite flicker end to end in Eolis's town, headless frame by frame.

    python3 tests/flicker/no_flicker.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/flicker

The game puts the n-th sprite it draws in one half of the shadow OAM on one
frame and in the other half on the next (Sprites_StartSlotRange, $1C), and
the NES draws the lower OAM index in front, 8 a line at most: overlapping
sprites swap every frame (#161: the hero blinked behind the villagers). With
the remaster on, Seamless paints every sprite of a room, back to front in the
order the game drew them: the hero first, so in front.

Each part starts from the fixture eolis-town (the hero standing in Eolis's
town; an external prerequisite, not coverage), saved in a hidden window with
where Seamless's view puts the town (fax_world's camera, which holds while he
stands), then edited, and played on headless from there to two consecutive
frames, one with $1C $00 and one with $80. Each picture is what the window
shows after its frame (--present-out): Seamless's with the remaster on, the
stock view's, the machine's own picture, with it off. A frame's sprites are
its state's: the shadow OAM Seamless composes from, or the PPU's OAM.

1. villagers: the town's villager copied into two more slots, the three over
   the hero as he stands. With the remaster on, every pixel of his shows his
   own color on both frames, the same on both. With it off (the original),
   villagers hide some of his pixels, and which changes from frame to frame.
2. crowd: eight villagers and the hero side by side, 18 sprites on each of
   their lines. With the remaster on, every sprite's pixels show; with it off,
   the NES draws 8 a line, and some go missing.
3. fire: Fire cast in the town, with the remaster on. The game draws it 2
   frames in 4 (CastMagic_FinishHandler_Fire, by design), and the pictures
   show it on those frames and not on the others: its flicker stays.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "movement"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "graphics"))
from fax_e2e import batch, fixture, parse_args  # noqa: E402
from graphics_fixes import State, png  # noqa: E402  (a picture's pixels; a save state's RAM, CHR and OAM)
from play import Game  # noqa: E402  (fax_e2e's, with headless runs)

PARTS = ("villagers", "crowd", "fire")
HUD, ROOM_W, ROOM_H = 32, 256, 208
# RAM
START_SLOT_RANGE, SHADOW_OAM, HERO_X = 0x1C, 0x700, 0x9E           # Sprites_StartSlotRange, Player_PosX_Block
INTERRUPTS_HANDLED_LATCH, NEED_OAM_RESET = 0x10, 0x1B               # Game_InterruptsHandledLatch, Game_NeedOAMReset
ENTITIES, SPRITE_X, SPRITE_Y = 0x2CC, 0xBA, 0xC2                    # CurrentSprites_Entities, _XPos_Full, _YPos
SLOT_ARRAYS = [SPRITE_X, SPRITE_Y] + list(range(0x2BC, 0x36D, 8))   # every per-slot sprite array, 8 slots each
MAGIC, NUMBER_OF_SPELLS, SELECTED_MAGIC, CAST_MAGIC_TYPE, MP = 0x3A9, 0x3C5, 0x3C0, 0x2B3, 0x39A
VILLAGER, FIRE, NONE = 7, 2, 0xFF                                   # the town's villager's slot
BEHIND = 0x20                                                       # OAM attribute: behind the background
HERO_TILES = range(0x40)                                            # the hero's CHR slots ($0000-$03FF)
FIRE_TILES = (0x7C, 0x7D, 0x7E, 0x66)                               # its two frames' cells, palette 0


class Frame:
    """A frame's picture and state: the sprites the picture shows and where."""

    def __init__(self, picture, state, origin, seamless):
        self.picture, self.rows, self.state = picture, png(picture), State(state)
        self.ram, self.origin, self.seamless = self.state.ram, origin, seamless

    def sprites(self):
        """(index, y, tile, attr, x) of each sprite drawn: the shadow OAM
        Seamless composes from, or the PPU's for the stock view."""
        oam = self.ram[SHADOW_OAM:SHADOW_OAM + 256] if self.seamless else self.state.oam
        return [(i, *oam[i * 4:i * 4 + 4]) for i in range(1, 64) if oam[i * 4] < 0xEF]

    def pixels(self, sprite):
        """{(x, y): color index} of a sprite's opaque pixels, on the picture.
        A sprite's pixels start a line below its OAM Y, which counts the HUD."""
        _, y, tile, attr, x = sprite
        pattern = self.state.pattern(0x0000, tile)
        out = {}
        for dy in range(8):
            for dx in range(8):
                v = pattern[7 - dy if attr & 0x80 else dy][7 - dx if attr & 0x40 else dx]
                if v:
                    out[(self.origin[0] + x + dx, self.origin[1] + y + 1 - HUD + dy)] = v
        return out

    def at(self, point):
        return self.rows[point[1]][point[0]]


def learn(frame, pixels):
    """Color index -> color, from pixels nothing else covers; every pixel of
    an index must agree."""
    colors = {}
    for point, v in pixels.items():
        assert colors.setdefault(v, frame.at(point)) == frame.at(point), f"index {v} at {point}"
    return colors


def town(game):
    """The fixture eolis-town, saved in a hidden window at the same frame
    boundary as Seamless's camera is read, and where that puts the town's
    top-left (under the HUD) in Seamless's picture."""
    game.launch(at="eolis-town")
    saved = game.out / "town.cycstate"
    _, world, room, flicker = batch(("save_state", dict(path=str(saved))), ("fax_world", {}),
                                    ("fax_world_room", {}), ("no_flicker_state", {}))
    game.close()
    assert world["enabled"] and world["playing"] and room["xy"] is not None, (world, room)
    assert flicker["enabled"], flicker
    return saved, (room["xy"][0] * ROOM_W - world["camera"][0], HUD + room["xy"][1] * ROOM_H - world["camera"][1])


def villager_copies(ram, xs):
    """RAM writes that copy the town's villager into slots 0.. at the X's in
    `xs` (CurrentSprites_XPos_Full), the first in its own slot."""
    writes = {}
    for slot, x in zip([VILLAGER] + [s for s in range(8) if s != VILLAGER], xs):
        for base in SLOT_ARRAYS:
            writes[base + slot] = ram[base + VILLAGER]
        writes[SPRITE_X + slot] = x
    return writes


def frames(game, state, origin, name, counts, original=False, schedule=()):
    """Headless from `state` to each of `counts` frames, all at once: a Frame
    each (its picture and a state saved after it)."""
    mods = game.mods("mods-off" if original else "mods-on", original=original)
    runs = []
    for n in counts:
        stem = game.out / f"{name}-{'original' if original else 'remaster'}-{n}"
        runs.append((state, list(schedule), n, mods, None, (n - 1, stem.with_suffix(".cycstate")),
                     stem.with_suffix(".png")))
    out = []
    for run, ram in zip(runs, game.headless_all(runs)):
        frame = Frame(run[6], run[5][1], origin if not original else (0, HUD), not original)
        assert frame.ram[:0x800] == ram[-1][:0x800], "the state is the frame's"
        out.append(frame)
    return out


def two_frames(game, state, origin, name, original=False, schedule=(), first=3):
    """Frames `first` and `first` + 1 from `state`: one with $1C $00, one $80."""
    pair = frames(game, state, origin, name, (first, first + 1), original, schedule)
    assert {f.ram[START_SLOT_RANGE] for f in pair} == {0x00, 0x80}, "a frame of each half"
    return pair


def lagged(frame):
    """The game was still drawing as the frame ended: OnInterrupt keeps the
    PPU's sprites (Game_InterruptsHandledLatch set, Game_NeedOAMReset clear)."""
    return frame.ram[INTERRUPTS_HANDLED_LATCH] and not frame.ram[NEED_OAM_RESET]


def villagers(game, saved, origin):
    hero_x = State(saved).ram[HERO_X]
    # Over him: CurrentSprites_XPos_Full is 4 right of his, at the same left.
    state = game.out / "villagers.cycstate"
    fixture(saved, state, villager_copies(State(saved).ram, [hero_x - 4, hero_x + 4, hero_x + 12]))
    for original in (False, True):
        frames = two_frames(game, state, origin, "villagers", original)
        shown, hidden = [], []
        for frame in frames:
            sprites = frame.sprites()
            hero = [s for s in sprites if s[2] in HERO_TILES and (s[3] & 3) == 0]
            others = [s for s in sprites if s not in hero]
            assert len(hero) >= 6 and len(others) >= 18, (hero, others)
            assert not any(s[3] & BEHIND for s in hero), "he stands clear of the foreground"
            mine = {p: v for s in hero for p, v in frame.pixels(s).items()}
            theirs = {p for s in others for p in frame.pixels(s)}
            over = [p for p in mine if p in theirs]
            assert len(over) >= 64, f"the villagers cover {len(over)} of his pixels"
            colors = learn(frame, {p: v for p, v in mine.items() if p not in theirs})
            hidden.append([p for p, v in mine.items() if frame.at(p) != colors.get(v)])
            shown.append({p: frame.at(p) for p in mine})
            print(f"{frame.picture.name}: $1C ${frame.ram[START_SLOT_RANGE]:02X}, {len(mine)} pixels of the hero, "
                  f"{len(over)} under villagers, {len(hidden[-1])} not his color")
        assert shown[0].keys() == shown[1].keys(), "he stands still"
        changed = sum(shown[0][p] != shown[1][p] for p in shown[0])
        if not original:
            assert not hidden[0] and not hidden[1], "every pixel of the hero shows him, in front"
            assert not changed, "his pixels are the same on both frames"
            print(f"ok: in front of three villagers, all {len(shown[0])} pixels of the hero the same on both frames")
        else:
            # Which of the villagers' cells cover which of his decides how
            # much each frame hides; that it changes is the flicker.
            assert hidden[0] or hidden[1], "the original: villagers hide some of him"
            assert changed, "the original: his pixels change from frame to frame"
            print(f"ok: the original: villagers hide {len(hidden[0])} and {len(hidden[1])} of his pixels, "
                  f"{changed} change from frame to frame")


def crowd(game, saved, origin):
    hero_x = State(saved).ram[HERO_X]
    # Side by side, 8 apart, none over another: a villager is 16 wide, with
    # its left 9 left of CurrentSprites_XPos_Full (the hero's is 5). Seven of
    # 7 cells and his 8 fit the game's 62 slots; from five the game lags.
    lefts = [hero_x - 5 + 24 * k for k in (-4, -3, -2, -1, 1, 2, 3)]
    assert all(8 <= x <= 232 for x in lefts), lefts
    state = game.out / "crowd.cycstate"
    fixture(saved, state, villager_copies(State(saved).ram, [x + 9 for x in lefts]))
    seamless = None
    for original in (False, True):
        # From frame 3: the PPU's OAM is the state's until the NMI first copies
        # the edited one, after the first frame the game finishes.
        looks = frames(game, state, origin, "crowd", (3, 4, 5, 6), original)
        assert any(lagged(f) for f in looks) and not all(lagged(f) for f in looks), [lagged(f) for f in looks]
        last = None
        for frame in looks:
            if not lagged(frame):
                last = frame
            if last is None:
                continue
            # What the picture shows: the stock view the PPU's OAM, which a
            # lagging frame leaves as it was; Seamless the last frame the NMI
            # copied, kept through a lagging one.
            shown = last if frame.seamless else frame
            sprites = [s for s in shown.sprites() if not s[3] & BEHIND]
            lines = {}
            for s in sprites:
                for y in range(s[1] + 1, s[1] + 9):
                    lines[y] = lines.get(y, 0) + 1
            assert max(lines.values()) >= 15, f"{max(lines.values())} sprites on the fullest line"
            cells = [frame.pixels(s) for s in sprites]
            taken = {}
            for cell in cells:
                for p in cell:
                    taken[p] = taken.get(p, 0) + 1
            assert max(taken.values()) == 1, "no sprite over another"
            if seamless is None:                                # the remaster on: learnt from every sprite
                seamless = {pal: learn(frame, {p: v for s, cell in zip(sprites, cells) if s[3] & 3 == pal
                                               for p, v in cell.items()})
                            for pal in {s[3] & 3 for s in sprites}}
            missing = [s for s, cell in zip(sprites, cells)
                       if any(frame.at(p) != seamless[s[3] & 3].get(v) for p, v in cell.items())]
            print(f"{frame.picture.name}: {'lagging, ' if lagged(frame) else ''}{len(sprites)} sprites, "
                  f"{max(lines.values())} on the fullest line, {len(missing)} not drawn whole")
            if not original:
                assert not missing, "every sprite on the line shows"
            else:
                assert missing, "the original draws 8 a line"
        if not original:
            print("ok: 16 sprites a line, every one drawn, on a lagging frame too")
        else:
            print("ok: the original: sprites past 8 a line go missing")


def fire(game, saved, origin):
    state = game.out / "fire.cycstate"
    fixture(saved, state, {ENTITIES + VILLAGER: NONE, MAGIC: FIRE, NUMBER_OF_SPELLS: 1, SELECTED_MAGIC: FIRE,
                           MP: 0x50})
    cast = [(0, "UP+B"), (2, "UP"), (6, "-")]                        # Up and B, as the Magic button presses them
    mods = game.mods("mods-on")
    rams = game.headless(state, cast, 40, mods)

    def flame(oam):
        return [s for s in ((i, *oam[i * 4:i * 4 + 4]) for i in range(1, 64))
                if s[1] < 0xEF and s[2] in FIRE_TILES and not s[3] & (BEHIND | 3) and 16 <= s[4] <= 224]

    def clear(oam):
        """The Fire 24 pixels or more from the hero (his cells' lefts), who is
        drawn in front of it, in its palette: where it was then shows no Fire."""
        hero = [oam[i * 4 + 3] for i in range(1, 64) if oam[i * 4] < 0xEF and oam[i * 4 + 1] in HERO_TILES]
        return all(abs(s[4] - x) >= 24 for s in flame(oam) for x in hero)
    oams = [ram[SHADOW_OAM:SHADOW_OAM + 256] for ram in rams]
    drawn = [len(flame(oam)) == 6 for oam in oams]
    flying = [f for f, ram in enumerate(rams) if ram[CAST_MAGIC_TYPE] == FIRE]
    assert flying, "the Fire is cast"
    # Four frames in its flight, two with it drawn and two without, from one
    # it is drawn on, clear of the hero.
    start = next(f for f in flying if drawn[f] and clear(oams[f]) and clear(oams[f + 2]) and
                 all(g in flying for g in range(f, f + 4)))
    assert sum(drawn[start:start + 4]) == 2, drawn[start:start + 4]
    frames = []
    for pair in (start + 1, start + 3):                             # frames 1-based: frame f is run f + 1
        frames += two_frames(game, state, origin, "fire", schedule=cast, first=pair)
    seen = None
    for frame in frames:
        sprites = flame(frame.ram[SHADOW_OAM:SHADOW_OAM + 256])
        if sprites:
            seen = {p: v for s in sprites for p, v in frame.pixels(s).items()}
            colors = learn(frame, seen)
            print(f"{frame.picture.name}: the Fire drawn, all {len(seen)} of its pixels shown")
        else:
            assert seen, "a drawn frame first"
            alike = sum(frame.at(p) == colors[v] for p, v in seen.items())
            assert alike <= len(seen) // 4, f"{alike} of {len(seen)} pixels still the Fire's"
            print(f"{frame.picture.name}: no Fire in OAM, and none where it was ({alike} of {len(seen)} alike)")
    assert [bool(flame(f.ram[SHADOW_OAM:SHADOW_OAM + 256])) for f in frames] == drawn[start:start + 4]
    print("ok: the Fire still flickers, 2 frames in 4, as the game draws it")


def main():
    args = parse_args(__doc__, parts=PARTS)
    game = Game.from_args(args)
    try:
        game.state("eolis-town")                                    # made before the test's own game starts
        saved, origin = town(game)
        for part in PARTS:
            if args.wants(part):
                {"villagers": villagers, "crowd": crowd, "fire": fire}[part](game, saved, origin)
        print("pictures:", ", ".join(sorted(p.name for p in game.out.glob("*.png"))))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
