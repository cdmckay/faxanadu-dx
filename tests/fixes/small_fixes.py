"""Play Small fixes end to end: the original's typing sound, the mantra's line
ends, and Ointment against magic on the shield (#39).

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

Pictures of each mantra, and of the shot on the shield, go in OUT.
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
PARTS = ("sound", "mantra", "ointment")
# RAM
IFRAMES, SOUND_NOISE_TICKS, SOUND_COUNTER = 0xAD, 0x126, 0x127  # Player_InvincibilityPhase;
#                                         SoundEffect_TicksRemaining_3_ (the noise channel's), _State_Counter
MESSAGE_ID, PROCESSED, CHAR_POS, LINE_NUM, MESSAGE_START = 0x213, 0x214, 0x216, 0x217, 0x218
ENTITIES, SPRITE_Y = 0x2CC, 0xC2
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

    def headless(self, state, schedule, frames, original, picture=None, states=False):
        """Play `frames` frames from `state` with no window, frame exact, the
        remaster off (`original`) or on, buttons held on a schedule of
        (frame, buttons) from the state's frame ("UP", "-" for none). Returns
        each frame's CPU RAM, and with `states` each frame's APU chunk too.
        With `picture`, the last frame's picture goes there."""
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
