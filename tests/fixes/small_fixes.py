"""Play Small fixes end to end: the original's typing sound, and Ointment (#39)
and iframes (#82) against magic on the shield.

    python3 tests/fixes/small_fixes.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/fixes

Each check runs the same moment twice, headless from one save state, frame
exact: with the remaster off (the original game, where the bug shows) and on
(where it is gone). The save states come from fixtures (tests/e2e/fax_e2e.py)
with RAM edited where noted (an external prerequisite, not coverage). Three
parts (--list-parts), each its own game:

sound     Before Conflate's Guru (died into his temple from Eolis's gate), Up:
          his greeting types. A save state every frame, and from each the
          APU's noise registers as last written: each character's typing
          sound is four ticks, and its NOISE_LO is $10, $10, then $A9, $A9 in
          the original (read past SOUNDEFFECT_MESSAGE_NOISE_LO's two bytes,
          from the next routine's LDA #$14), and $10, $10, $01, $01 with the
          fix: the table's own second byte.
ointment  The Execution Hood's shot in the Fortress of Zenis (Block's fixture,
          its other actors sent away) at the Small Shield's height, the hero
          not blocking. Without Ointment the shield lets half its 8 HP
          through, with knockback, in both. With Ointment running, the
          original still lets 4 through and knocks him back; with the fix,
          nothing, and the shield still takes the shot. A shot at his head
          under Ointment passes through him, doing nothing, in both:
          Player_HandleTouchEnemyMagic checks Ointment, the shield's
          Player_HandleHitByMagic does not.
iframes   The same shot and a copy of it 16 pixels behind, both at the Small
          Shield's height, the hero moved right along the floor (x 120) so
          that knockback keeps him on it, no Ointment. The first takes 4 and
          starts the 60 iframes; the second lands 20 frames later, inside
          them. In the original it takes 4 more, starts the iframes over and
          knocks him back again (Player_HandleHitByMagic never checks them).
          With the fix it takes nothing, the iframes go on counting down and
          he stays put; the shield still takes it (the shot is gone, its sound
          $0C queued that frame). Then in a window, from just after the first
          hit, with Block, whose hook runs first at the same site: raised
          then, Block takes the second shot and Small fixes counts nothing;
          not raised, Small fixes takes it, and with Ointment running too
          counts it as iframes, which the touch handler checks first.

Pictures of the shots on the shield go in OUT.
"""
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

TESTS = Path(__file__).resolve().parents[1]
for folder in ("e2e", "block", "travel"):
    sys.path.insert(0, str(TESTS / folder))
import fax_e2e  # noqa: E402
from fax_e2e import chunks, fixture, parse_args  # noqa: E402
import block  # noqa: E402,F401  (registers the fixture "block-hood-cast")
import harness  # noqa: E402
import guru_travel  # noqa: E402,F401  (registers the fixture "travel-conflate-temple")

PACKAGE, FEATURE = "faxanadu.dx", "dx"                          # the one switch: off is the original game
PARTS = ("sound", "ointment", "iframes")
# RAM
IFRAMES, SOUND_NOISE_TICKS, SOUND_COUNTER = 0xAD, 0x126, 0x127  # Player_InvincibilityPhase;
#                                         SoundEffect_TicksRemaining_3_ (the noise channel's), _State_Counter
MESSAGE_ID = 0x213
ENTITIES, SPRITE_X, SPRITE_Y = 0x2CC, 0xBA, 0xC2
# Every per-slot sprite array, CurrentSprites_XPos_Full and _YPos, then
# _XPos_Frac to _Values: copying a slot's bytes copies the sprite.
SPRITE_ARRAYS = (SPRITE_X, SPRITE_Y) + tuple(range(0x2BC, 0x374, 8))
POS_X, SOUND_CURRENT = 0x9E, 0xFB                               # Player_PosX_Block; SoundEffect_Current (bit 7 once
#                                                                 started)
SELECTED_SHIELD, DURATION_OINTMENT, HP = 0x3BF, 0x427, 0x431
# The APU's save-state chunk is nesrecomp's HwApu (runner/cyc/hw_apu.c): its
# reg[16], "$4000-$400F as last written", starts at byte 31.
APU_REGS = 31
NOISE_VOL, NOISE_LO = APU_REGS + 0x0C, APU_REGS + 0x0E
MEDITATE_WITH_YOU = 0x22                                        # the Guru's greeting
HOOD_MAGIC, SMALL = 0x51, 0
SHIELD_HEIGHT, HEAD_HEIGHT = 150, 136                           # the shot's Y, the hero on the ledge at 144
SHIELD_SOUND, IFRAMES_ON_HIT, KNOCKBACK = 0x0C, 60, 24          # Player_HandleShieldHitByMagic's sound; a hit's iframes,
#                                                                 and how far it knocks him back
ON_THE_FLOOR, TRAILING = 120, 16                                # the hero's X on screen 16's floor (knocked back twice:
#                                                                 72; to 48, he falls through its gap); the second
#                                                                 shot's distance behind the first
AFTER_FIRST_HIT = 5                                             # frames after the first hit: its knockback over, well
#                                                                 before the second


@fax_e2e.recipe("fixes-at-guru", parent="travel-conflate-temple")
def _at_guru(g):
    """Back in Conflate's temple after a death: walked left to the Guru."""
    g.tcp("action", name="left", frames=170)
    g.wait(180)


class Game(harness.Game):
    """Block's test harness's Game (its fixture's recipe walks with it), with
    headless runs."""

    def mods(self, original):
        """A mod catalog for headless runs: the remaster on, or off for the original."""
        root = self.out / ("mods-original" if original else "mods-dx")
        if not root.exists():
            shutil.copytree(self.out / "game" / "mods" / "packages", root / "packages")
            (root / "state.toml").write_text(fax_e2e.features_toml({(PACKAGE, FEATURE): False} if original else {}))
        return root

    def headless(self, state, schedule, frames, original, picture=None, states=False, save=None):
        """Play `frames` frames from `state` with no window, frame exact, the
        remaster off (`original`) or on, buttons held on a schedule of
        (frame, buttons) from the state's frame ("UP", "-" for none). Returns
        each frame's CPU RAM, and with `states` each frame's APU chunk too.
        With `picture`, the last frame's picture goes there; with `save`,
        (frame, path), the state at the start of that frame goes to path."""
        start = struct.unpack_from("<I", Path(state).read_bytes(), 28)[0]
        run = Path(tempfile.mkdtemp(prefix="run-", dir=self.out))
        (run / "input.txt").write_text("".join(f"{start + at} {buttons}\n" for at, buttons in sorted(schedule)))
        log = run / "frames.bin"
        args = [str(self.exe), str(self.rom), "--load-state", str(state), "--input", str(run / "input.txt"),
                "--frames", str(start + frames), "--frame-log", str(log),
                "--frame-log-frames", f"{start}:{start + frames - 1}",
                "--mods-root", str(self.mods(original)), "--no-save"]
        if picture:
            args += ["--screenshot", str(picture)]
        if states:
            for k in range(frames):
                args += ["--save-state", f"{start + k}:{run / f'{k:03d}.cycstate'}"]
        if save:
            args += ["--save-state", f"{start + save[0]}:{save[1]}"]
        done = subprocess.run(args, capture_output=True, text=True)
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
        apus = [apu_chunk(run / f"{k:03d}.cycstate") for k in range(frames)] if states else None
        shutil.rmtree(run)
        return (rams, apus) if states else rams


def apu_chunk(path):
    data = Path(path).read_bytes()
    pos, size = next((p, n) for tag, p, n in chunks(data) if tag == b"APU ")
    return data[pos:pos + size]


def rom_byte(image, bank, addr):
    return image[16 + bank * 0x4000 + addr - 0x8000]


def main():
    args = parse_args(__doc__, parts=PARTS)
    g = Game.from_args(args)
    out = g.out
    try:
        if args.wants("sound"):
            at_guru = g.state("fixes-at-guru")
            ticks = {}
            for original in (True, False):
                rams, apus = g.headless(at_guru, [(0, "UP"), (4, "-")], 80, original, states=True)
                assert any(r[MESSAGE_ID] == MEDITATE_WITH_YOU for r in rams), "the greeting"
                # The typing sound's four ticks: the noise channel is the
                # effect's (SoundEffect_TicksRemaining_3_ 6 down to 3), the
                # tick SoundEffect_State_Counter, 1 to 4 after it.
                seen = {(r[SOUND_COUNTER], a[NOISE_LO]) for r, a in zip(rams, apus) if 3 <= r[SOUND_NOISE_TICKS] <= 6}
                vols = {(r[SOUND_COUNTER], a[NOISE_VOL]) for r, a in zip(rams, apus) if 3 <= r[SOUND_NOISE_TICKS] <= 6}
                assert vols == {(1, 0x1F), (2, 0x1F), (3, 0x1F), (4, 0x10)}, vols   # the chunk's layout, checked
                ticks[original] = sorted(seen)
            clack, hiss = 0xA9, 0x01
            assert rom_byte(g.image, 5, 0x86D1) == 0x10 and rom_byte(g.image, 5, 0x86D2) == hiss
            assert rom_byte(g.image, 5, 0x86D3) == clack                # SoundEffect_HitEnemy_Setup's LDA
            assert ticks[True] == [(1, 0x10), (2, 0x10), (3, clack), (4, clack)], ticks[True]
            assert ticks[False] == [(1, 0x10), (2, 0x10), (3, hiss), (4, hiss)], ticks[False]
            print("ok: the typing sound's NOISE_LO, ticks 1-4: the original $10 $10 $A9 $A9 (past its table), "
                  "the remaster $10 $10 $01 $01 (the table's own)")

        if args.wants("ointment"):
            cast = g.state("block-hood-cast")
            entities = [state_ram(cast, ENTITIES + i) for i in range(8)]
            slot = entities.index(HOOD_MAGIC)
            alone = {ENTITIES + i: 0xFF for i in range(8) if i != slot}

            def shot(name, y, ointment, original):
                """The shot at height `y`, Ointment's time left ($FF none):
                (HP lost, knocked back, the shot gone)."""
                state = out / f"{name}.cycstate"
                fixture(cast, state, {**alone, SPRITE_Y + slot: y, SELECTED_SHIELD: SMALL, HP: 0x50,
                                      DURATION_OINTMENT: ointment, IFRAMES: 0})
                tag = f"{name}-{'original' if original else 'dx'}"
                rams = g.headless(state, [], 100, original, picture=out / f"shot-{tag}.png")
                g.shots.append(f"shot-{tag}.png")
                gone = rams[-1][ENTITIES + slot] != HOOD_MAGIC
                return 0x50 - min(r[HP] for r in rams), any(r[IFRAMES] for r in rams), gone

            for original in (True, False):
                which = "original" if original else "dx"
                assert shot("shield", SHIELD_HEIGHT, 0xFF, original) == (4, True, True), which
                assert shot("head-ointment", HEAD_HEIGHT, 0x05, original) == (0, False, False), which   # through him
            assert shot("shield-ointment", SHIELD_HEIGHT, 0x05, True) == (4, True, True)
            assert shot("shield-ointment", SHIELD_HEIGHT, 0x05, False) == (0, False, True)
            print("ok: the Small Shield lets 4 of the shot's 8 through in both; under Ointment a shot at his head "
                  "does nothing in both, and one on the shield 4 and knockback in the original, nothing in the "
                  "remaster, the shield still taking it")

        if args.wants("iframes"):
            cast = g.state("block-hood-cast")
            entities = [state_ram(cast, ENTITIES + i) for i in range(8)]
            slot = entities.index(HOOD_MAGIC)
            copy = next(i for i in range(8) if i != slot and entities[i] == 0xFF)
            writes = {ENTITIES + i: 0xFF for i in range(8) if i != slot}
            writes.update({base + copy: state_ram(cast, base + slot) for base in SPRITE_ARRAYS})
            writes.update({SPRITE_X + copy: state_ram(cast, SPRITE_X + slot) + TRAILING, SPRITE_Y + slot: SHIELD_HEIGHT,
                           SPRITE_Y + copy: SHIELD_HEIGHT, POS_X: ON_THE_FLOOR, SELECTED_SHIELD: SMALL, HP: 0x50,
                           DURATION_OINTMENT: 0xFF, IFRAMES: 0})
            two = out / "two-shots.cycstate"
            fixture(cast, two, writes)
            between = out / "between-hits.cycstate"

            def hits(rams):
                """Each shot's end: (its frame, HP lost then, the iframes and
                SoundEffect_Current that frame, the hero's X before it and
                three frames after, when knockback is over)."""
                seen = []
                for s in (slot, copy):
                    k = next(k for k, r in enumerate(rams) if r[ENTITIES + s] != HOOD_MAGIC)
                    after = rams[min(k + 3, len(rams) - 1)]
                    seen.append((k, rams[k - 1][HP] - rams[k][HP], rams[k][IFRAMES], rams[k][SOUND_CURRENT],
                                 rams[k - 1][POS_X], after[POS_X]))
                return seen

            frames = {}
            between_hits = None                                 # where the fixture's shot hits first: the original's run
            for original in (True, False):
                tag = "original" if original else "dx"
                rams = g.headless(two, [], 70, original, picture=out / f"two-shots-{tag}.png",
                                  save=None if original else (between_hits, between))
                g.shots.append(f"two-shots-{tag}.png")
                (k1, lost1, iframes1, sound1, x1, after1), (k2, lost2, iframes2, sound2, x2, after2) = hits(rams)
                frames[tag] = (k1, k2)
                between_hits = between_hits or k1 + AFTER_FIRST_HIT
                assert (lost1, iframes1, sound1) == (4, IFRAMES_ON_HIT, SHIELD_SOUND), (tag, rams[k1][HP], iframes1)
                assert x1 - after1 == KNOCKBACK, (tag, x1, after1)
                assert 0 < k2 - k1 < IFRAMES_ON_HIT and rams[k2 - 1][IFRAMES] > 0, (tag, k1, k2)
                assert sound2 == SHIELD_SOUND, (tag, sound2)
                if original:
                    assert (lost2, iframes2, x2 - after2) == (4, IFRAMES_ON_HIT, KNOCKBACK), (tag, lost2, iframes2)
                else:
                    assert (lost2, iframes2, after2) == (0, rams[k2 - 1][IFRAMES] - 1, x2), (tag, lost2, iframes2)
            assert frames["original"] == frames["dx"], frames
            k1, k2 = frames["original"]
            assert k1 + 3 < between_hits < k2 - 10, (k1, k2)
            print(f"ok: two shots on the Small Shield {k2 - k1} frames apart: the original takes 4 and 4, the second "
                  f"starting the iframes over and knocking him back {KNOCKBACK} pixels again; the remaster takes 4 "
                  "and nothing, the iframes counting on and the hero still, the shield taking both with its sound")

            # Block, whose hook comes first, from just after the first hit.
            g.set_features({})                                  # the remaster on
            g.launch()
            assert g.tcp("block_state")["enabled"] and g.tcp("small_fixes_state")["enabled"]

            def second_shot(state, *keys):
                """From `state`, the second shot played out, with `keys` held
                from its first frame: (HP lost, Block's saves, Small fixes'
                iframe and Ointment saves)."""
                *_, block, fixes = g.batch(("load_state", dict(path=str(state))),
                                           *(("key", dict(name=k, frames=40)) for k in keys),
                                           ("block_state", {}), ("small_fixes_state", {}))
                g.until("the second shot to be gone", lambda: g.ram(ENTITIES + copy) != HOOD_MAGIC, 60)
                g.wait(4)
                lost = state_ram(state, HP) - g.ram(HP)
                block2, fixes2 = g.tcp("block_state"), g.tcp("small_fixes_state")
                if keys:
                    g.until("Block to be let go", lambda: not g.tcp("block_state")["held"], 60)
                return (lost, block2["shield_saved"] - block["shield_saved"],
                        fixes2["iframes_saved"] - fixes["iframes_saved"],
                        fixes2["ointment_saved"] - fixes["ointment_saved"])

            assert state_ram(between, IFRAMES) > 0 and state_ram(between, ENTITIES + copy) == HOOD_MAGIC
            seen = second_shot(between, "V")
            assert seen == (0, 1, 0, 0), seen
            seen = second_shot(between)
            assert seen == (0, 0, 1, 0), seen
            both = out / "between-hits-ointment.cycstate"
            fixture(between, both, {DURATION_OINTMENT: 0x05})
            seen = second_shot(both)
            assert seen == (0, 0, 1, 0), seen
            print("ok: during the iframes, with Block up, Block's hook takes the second shot and Small fixes counts "
                  "nothing; without it, Small fixes takes it, as iframes with Ointment running too")
        print("screenshots:", ", ".join(g.shots))
    except BaseException:
        if g.running():
            g.shot("failure")
        raise
    finally:
        g.close()


def state_ram(path, addr):
    """A byte of CPU RAM in a save state (its HW chunk, as fixture() edits it)."""
    data = Path(path).read_bytes()
    return data[next(p for tag, p, _ in chunks(data) if tag == b"HW  ") + 23 + addr]


if __name__ == "__main__":
    main()
