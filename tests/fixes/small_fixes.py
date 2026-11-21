"""Play Small fixes end to end: the original's typing sound, the mantra's line
ends, and Ointment (#39) and iframes (#82) against magic on the shield.

    python3 tests/fixes/small_fixes.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/fixes

Each check runs the same moment twice, headless from one save state, frame
exact: with the remaster off (the original game, where the bug shows) and on
(where it is gone). The save states come from fixtures (tests/e2e/fax_e2e.py)
with RAM edited where noted (an external prerequisite, not coverage). Four
parts (--list-parts), each its own game:

sound     Before Conflate's Guru (died into his temple from Eolis's gate), Up:
          his greeting types. A save state every frame, and from each the
          APU's noise registers as last written: each character's typing
          sound is four ticks, and its NOISE_LO is $10, $10, then $A9, $A9 in
          the original (read past SOUNDEFFECT_MESSAGE_NOISE_LO's two bytes,
          from the next routine's LDA #$14), and $10, $10, $01, $01 with the
          fix: the table's own second byte.
mantra    The same Guru with the remaster off, the hero given enough (weapons,
          armor, spells, items) for a mantra of more than 16 characters:
          talk, and once the greeting has typed out (the game waits for A),
          save. The greeting leaves the message pointer on the next message's
          first byte ($876B, bank 13), and the mantra's line-end check reads
          it with bank 12 mapped: an H there, so the original's mantra comes
          out on two lines. The same state with the pointer on bank 12's
          $8760, a $FF (the JSR's operand in PasswordScreen_ShowNextChar):
          the original starts no second line, and the characters after the
          16th overwrite the first ones. With the remaster on (this state,
          saved with it off, has no file in play, so the Guru shows the
          mantra as without Save files), both come out on two lines, as the
          original's good one does.
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

Pictures of each mantra, and of the shots on the shield, go in OUT.
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

PACKAGE, FEATURE = "faxanadu.remastered", "remastered"          # the one switch: off is the original game
PARTS = ("sound", "mantra", "ointment", "iframes")
# RAM
IFRAMES, SOUND_NOISE_TICKS, SOUND_COUNTER = 0xAD, 0x126, 0x127  # Player_InvincibilityPhase;
#                                         SoundEffect_TicksRemaining_3_ (the noise channel's), _State_Counter
MESSAGE_ID, PROCESSED, CHAR_POS, LINE_NUM, MESSAGE_START = 0x213, 0x214, 0x216, 0x217, 0x218
ENTITIES, SPRITE_X, SPRITE_Y = 0x2CC, 0xBA, 0xC2
# Every per-slot sprite array, CurrentSprites_XPos_Full and _YPos, then
# _XPos_Frac to _Values: copying a slot's bytes copies the sprite.
SPRITE_ARRAYS = (SPRITE_X, SPRITE_Y) + tuple(range(0x2BC, 0x374, 8))
POS_X, SOUND_CURRENT = 0x9E, 0xFB                               # Player_PosX_Block; SoundEffect_Current (bit 7 once
#                                                                 started)
WEAPONS, ARMOR, SHIELDS, SPELLS, ITEMS = 0x39D, 0x3A1, 0x3A5, 0x3A9, 0x3AD
SELECTED_SHIELD, COUNTS = 0x3BF, 0x3C2                          # NumberOfWeapons, ...Armors, Shields, Spells, Items
DURATION_OINTMENT, SPECIAL_ITEMS, QUESTS, HP = 0x427, 0x42C, 0x42D, 0x431
# The APU's save-state chunk is nesrecomp's HwApu (runner/cyc/hw_apu.c): its
# reg[16], "$4000-$400F as last written", starts at byte 31.
APU_REGS = 31
NOISE_VOL, NOISE_LO = APU_REGS + 0x0C, APU_REGS + 0x0E
MEDITATE_WITH_YOU = 0x22                                        # the Guru's greeting
AFTER_GREETING, BANK12_FF = 0x876B, 0x8760                      # the next message's first byte; bank 12's $FF
HOOD_MAGIC, SMALL = 0x51, 0
SHIELD_HEIGHT, HEAD_HEIGHT = 150, 136                           # the shot's Y, the hero on the ledge at 144
SHIELD_SOUND, IFRAMES_ON_HIT, KNOCKBACK = 0x0C, 60, 24          # Player_HandleShieldHitByMagic's sound; a hit's iframes,
#                                                                 and how far it knocks him back
ON_THE_FLOOR, TRAILING = 120, 16                                # the hero's X on screen 16's floor (knocked back twice:
#                                                                 72; to 48, he falls through its gap); the second
#                                                                 shot's distance behind the first
BETWEEN_HITS = 45                                               # frames into the two shots: after the first hit's
#                                                                 knockback, well before the second
MANTRA_BOX = (96, 140, 232, 168)                                # the text box's first two lines of text
# Enough for a long mantra: three weapons, three armors, two shields, three
# spells and six items, the Ring of Elf, the Black Onyx and the Pendant. No
# Dragon Slayer: with it Conflate's Guru says something else.
RICH = {WEAPONS: 0, WEAPONS + 1: 1, WEAPONS + 2: 2, ARMOR: 0, ARMOR + 1: 1, ARMOR + 2: 2, SHIELDS: 0, SHIELDS + 1: 1,
        SPELLS: 0, SPELLS + 1: 1, SPELLS + 2: 2, ITEMS: 0x07, ITEMS + 1: 0x09, ITEMS + 2: 0x0F, ITEMS + 3: 0x10,
        ITEMS + 4: 0x11, ITEMS + 5: 0x12, COUNTS: 3, COUNTS + 1: 3, COUNTS + 2: 2, COUNTS + 3: 3, COUNTS + 4: 6,
        SPECIAL_ITEMS: 0x83, QUESTS: 0x05}


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
        root = self.out / ("mods-original" if original else "mods-remastered")
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


def png_rows(path):
    """A screenshot's rows of (r, g, b), through the README script's reader."""
    sys.path.insert(0, str(TESTS.parent / "tools"))
    from readme_screenshots import read_png
    return read_png(path)[1]


def region(rows, box):
    x0, y0, x1, y1 = box
    return [row[x0:x1] for row in rows[y0:y1]]


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

        if args.wants("mantra"):
            at_guru = g.state("fixes-at-guru")
            g.set_features({(PACKAGE, FEATURE): False})
            g.launch(at="fixes-at-guru")
            g.poke(RICH)
            mantra = g.tcp("mantra_now")["mantra"]
            assert 16 < len(mantra) <= 32, mantra
            g.wait(4)
            g.act("up", 4, 10)
            g.until("the greeting", lambda: g.ram(MESSAGE_ID) == MEDITATE_WITH_YOU, 300)
            g.until("the greeting to type out", lambda: g.ram(MESSAGE_ID) == 0, 900)
            g.wait(4)
            ready = g.save("greeted")
            g.close()
            start = state_ram(ready, MESSAGE_START) | state_ram(ready, MESSAGE_START + 1) << 8
            at = start + state_ram(ready, PROCESSED)
            assert at == AFTER_GREETING, hex(at)
            assert rom_byte(g.image, 13, AFTER_GREETING - 1) == 0xFF, "the greeting ends just before"
            read, meant = rom_byte(g.image, 12, AFTER_GREETING), rom_byte(g.image, 13, AFTER_GREETING)
            assert read == ord("H") and meant == ord("I"), (read, meant)
            print(f"ok: after the greeting the mantra's line-end check reads ${at:04X} in bank 12 ({chr(read)}), "
                  f"not bank 13 ({chr(meant)}); either starts a new line")
            assert rom_byte(g.image, 12, BANK12_FF) == 0xFF
            edited = out / "greeted-ff.cycstate"
            fixture(ready, edited, {PROCESSED: BANK12_FF - start})
            press = [(0, "A"), (4, "-")]
            frames = 8 + len(mantra) + 20
            boxes, lines = {}, {}
            for name, state in (("greeted", ready), ("ff", edited)):
                for original in (True, False):
                    tag = f"{name}-{'original' if original else 'remastered'}"
                    picture = out / f"mantra-{tag}.png"
                    rams = g.headless(state, press, frames, original, picture=picture)
                    g.shots.append(picture.name)
                    lines[tag] = (rams[-1][LINE_NUM], rams[-1][CHAR_POS])
                    boxes[tag] = region(png_rows(picture), MANTRA_BOX)
            two_lines = (1, len(mantra) - 16)
            assert lines["greeted-original"] == two_lines, lines
            assert lines["ff-original"] == (0, len(mantra)), lines   # no new line: the second half overwrote the first
            assert lines["greeted-remastered"] == lines["ff-remastered"] == two_lines, lines
            assert boxes["ff-original"] != boxes["greeted-original"], "the overwritten mantra shows"
            assert boxes["ff-remastered"] == boxes["greeted-remastered"] == boxes["greeted-original"], \
                "the fixed mantra looks like the original's good one"
            print(f"ok: the {len(mantra)}-character mantra {mantra}: on two lines after the greeting; with the "
                  "message on bank 12's $FF, the original overwrites its first line and the remaster does not")

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
                tag = f"{name}-{'original' if original else 'remastered'}"
                rams = g.headless(state, [], 100, original, picture=out / f"shot-{tag}.png")
                g.shots.append(f"shot-{tag}.png")
                gone = rams[-1][ENTITIES + slot] != HOOD_MAGIC
                return 0x50 - min(r[HP] for r in rams), any(r[IFRAMES] for r in rams), gone

            for original in (True, False):
                which = "original" if original else "remastered"
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
            for original in (True, False):
                tag = "original" if original else "remastered"
                rams = g.headless(two, [], 70, original, picture=out / f"two-shots-{tag}.png",
                                  save=None if original else (BETWEEN_HITS, between))
                g.shots.append(f"two-shots-{tag}.png")
                (k1, lost1, iframes1, sound1, x1, after1), (k2, lost2, iframes2, sound2, x2, after2) = hits(rams)
                frames[tag] = (k1, k2)
                assert (lost1, iframes1, sound1) == (4, IFRAMES_ON_HIT, SHIELD_SOUND), (tag, rams[k1][HP], iframes1)
                assert x1 - after1 == KNOCKBACK, (tag, x1, after1)
                assert 0 < k2 - k1 < IFRAMES_ON_HIT and rams[k2 - 1][IFRAMES] > 0, (tag, k1, k2)
                assert sound2 == SHIELD_SOUND, (tag, sound2)
                if original:
                    assert (lost2, iframes2, x2 - after2) == (4, IFRAMES_ON_HIT, KNOCKBACK), (tag, lost2, iframes2)
                else:
                    assert (lost2, iframes2, after2) == (0, rams[k2 - 1][IFRAMES] - 1, x2), (tag, lost2, iframes2)
            assert frames["original"] == frames["remastered"], frames
            k1, k2 = frames["original"]
            assert k1 + 3 < BETWEEN_HITS < k2 - 10, (k1, k2)
            print(f"ok: two shots on the Small Shield {k2 - k1} frames apart: the original takes 4 and 4, the second "
                  f"starting the iframes over and knocking him back {KNOCKBACK} pixels again; the remaster takes 4 "
                  "and nothing, the iframes counting on and the hero still, the shield taking both with its sound")

            # Block, whose hook comes first, from just after the first hit.
            g.set_features({})                                  # the remaster on, after the mantra part's original
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
