"""A Guru's save is the rest point (#205), end to end: as he remembers your
journey the whole picture flashes once and HP and MP fill to 80 on their
bars, a point a frame with the Red Potion's fill sound.

    python3 tests/files/guru_refill.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/refill
    python3 tests/files/guru_refill.py ... --part original

guru      Eolis's temple at 32 HP and 5 MP, both bars drawn (MP from kills'
          fixture "mana-eolis-temple", come in by a door), with the Ring of
          Elf (a RAM edit, an external prerequisite): talking to the Guru,
          his first action and first line leave HP, MP and both bars as they
          were. At his question, "Shall I remember your journey?" (a save
          state, typed out, waiting), headless runs, every frame's RAM and
          presented picture:
          1. A: the frame he saves on, the whole 424-wide picture is grey, as
             the PPU's greyscale draws it (black lifted to grey), for 8
             frames and never again; HP counts 32 to 80 a point a frame, then
             MP 5 to 80, the bars on the picture a frame behind, with the
             fill sound $13 queued every 4th frame of the fill and on no
             other; then his line "I will remember your journey."
          2. A state saved mid-fill, inside the flash, loads and finishes:
             the flash's last frames, the rest of HP, all of MP, his line.
          3. B: nothing flashes, HP and MP stay 32 and 5, the box closes.
          In the window, A at the question writes the file and fills both
          (files_state's refills), and B saves nothing.
original  The remaster off: Eolis's Guru (his line, then the end) leaves HP
          and MP at 32 and 5.
"""
from pathlib import Path
import shutil
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "world"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "mana"))
from fax_e2e import act, batch, box_open, page_through, page_to, parse_args, ram, tcp, type_out, until, walk  # noqa: E402
import live_status_bar as hud  # noqa: E402  (its status bar read off a picture, and its Game)
import mp_from_kills  # noqa: E402,F401  (its fixture: "mana-eolis-temple")

PARTS = ("guru", "original")
PACKAGE = ("faxanadu.dx", "dx")
SOUND_CURRENT, INTERRUPT_COUNTER = 0xFB, 0x1A                       # SoundEffect_Current (bit 7 once started)
TEXTBOX_CLOSED, MESSAGE_ID, MP, SPECIAL_ITEMS, HP = 0x20C, 0x213, 0x39A, 0x42C, 0x431
TEMPLE_SPAWN_POINT = 0x439
RING_OF_ELF, FILL_SOUND, SLOT = 0x80, 0x13, 0xC3                    # the meat shops' and the Red Potion's; our line
GURU_QUESTION, GURU_SAVED, GURU_FILLING = 2, 3, 4                    # fax_files.c's GuruPhase
HP0, MP0, FULL, FLASH = 0x20, 5, 80, 8
PRESS = 2                                                            # a run's frame a new press goes down on
RUN = 200                                                            # frames: the fill (123), then his line


def schedule(button):
    """A new press of `button` on frame PRESS; A is held on (Hold A types his line 4x, and never turns a
    page), B let go."""
    return [(0, "-"), (PRESS, button)] + ([] if button == "A" else [(PRESS + 5, "-")])


def greyed(path):
    """Every pixel of a picture is a grey (R = G = B)."""
    _, bpp, rows = hud.png(path)
    return all(row[0::bpp] == row[1::bpp] == row[2::bpp] for row in rows)


def pixel(path, x, y):
    _, bpp, rows = hud.png(path)
    return bytes(rows[y][x * bpp:x * bpp + 3])


def main():
    args = parse_args(__doc__, parts=PARTS)
    g = hud.Game.from_args(args)
    out = g.out
    rom = g.image[16:]
    tiles = [rom[10 * 0x4000 + 0x140 + (0x30 + d) * 16:][:16] for d in range(10)]
    glyphs = [tuple(tuple(bool((t[r] | t[r + 8]) >> (7 - c) & 1) for c in range(8)) for r in range(8)) for t in tiles]

    def replay(name, state, inputs, frames, saves=()):
        """From `state`, `frames` frames headless with `inputs` (run frame, buttons): each frame's RAM, and a
        function giving run frame i's presented picture's path. `saves`: (run frame, path) save states."""
        start = g.continues_at(state)
        run = out / f"run-{name}"
        shutil.rmtree(run, ignore_errors=True)
        mods = shutil.copytree(g.mods("headless-mods"), run / "mods")
        (run / "input.txt").write_text("".join(f"{start + at} {buttons}\n" for at, buttons in inputs))
        log = run / "frames.bin"
        cmd = [str(g.exe), str(g.rom), "--load-state", str(state), "--input", str(run / "input.txt"),
               "--frames", str(start + frames), "--frame-log", str(log),
               "--frame-log-frames", f"{start}:{start + frames - 1}", "--mods-root", str(mods), "--no-save",
               "--present-out", str(run / "present.png"), "--present-every", "1"]
        for at, path in saves:
            cmd += ["--save-state", f"{start + at}:{path}"]
        done = subprocess.run(cmd, capture_output=True, text=True)
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
        return rams, lambda i: run / f"present_{start + i:05d}.png"

    def keep(path, name):
        target = out / f"{len(g.shots):02d}-{name}.png"
        shutil.copy(path, target)
        g.shots.append(target.name)

    def rising(rams, addr, first, frm, to):
        """The frames a value changes on: by one each, on frames in a row, `frm` to `to`, from frame `first`."""
        steps = [i for i in range(1, len(rams)) if rams[i][addr] != rams[i - 1][addr]]
        assert steps == list(range(first, first + to - frm)), (hex(addr), steps)
        assert all(rams[i][addr] == rams[i - 1][addr] + 1 for i in steps), hex(addr)
        assert rams[steps[0] - 1][addr] == frm and rams[steps[-1]][addr] == to, hex(addr)
        return steps

    def talk(remaster):
        """In Eolis's temple at 32 HP and 5 MP, with the Ring: walk to the Guru and talk, until his first line.
        HP, MP and both bars as they were."""
        g.set_features({PACKAGE: remaster})
        g.launch(at="mana-eolis-temple")
        g.poke({SPECIAL_ITEMS: RING_OF_ELF})
        walk("left")                                                # the Guru stands on the left
        assert not box_open()
        shown = ram(MESSAGE_ID)
        act("up", 4, 0)
        until("the Guru's first line", lambda: box_open() and ram(MESSAGE_ID) != shown, 300)
        type_out("his first line")
        path = out / f"{len(g.shots):02d}-{'guru' if remaster else 'original'}-first-line.png"
        _, memory = batch(("screenshot", dict(path=str(path))), ("read_ram", dict(addr=0, len=0x440)))
        g.shots.append(path.name)
        r = bytes.fromhex(memory["hex"])
        band = hud.Band(path, hud.HUD_X if remaster else 0, glyphs)
        assert (r[HP], r[MP]) == (HP0, MP0), (r[HP], r[MP])
        assert (band.bar(hud.HP_LINE), band.bar(hud.MP_LINE)) == (HP0, MP0), (band.bar(hud.HP_LINE),
                                                                              band.bar(hud.MP_LINE))

    try:
        if args.wants("guru"):
            talk(True)
            assert tcp("files_state")["refills"] == 0
            print(f"ok: Eolis's Guru's first action and first line leave HP at {HP0} and MP at {MP0}, and the "
                  "bars showing them")
            page_to(lambda s: s["guru"] == GURU_QUESTION, "his question", "files_state")
            type_out("his question")
            g.wait(2)
            question = g.save("question")
            mid = out / "mid-fill.cycstate"

            # 1. A: the flash, the fill, the sound, his line.
            rams, picture = replay("save", question, schedule("A"), RUN, [(PRESS + 4, mid)])
            grey = [i for i in range(RUN) if greyed(picture(i))]
            assert grey == list(range(grey[0], grey[0] + FLASH)) and grey[0] in (PRESS, PRESS + 1), grey
            for i in grey:                                          # the whole view: its black margins lifted
                assert pixel(picture(i), 0, 120) != b"\0\0\0" and pixel(picture(i), 423, 120) != b"\0\0\0", i
            assert pixel(picture(grey[-1] + 1), 0, 120) == b"\0\0\0"
            hp_steps = rising(rams, HP, grey[0] + 1, HP0, FULL)
            mp_steps = rising(rams, MP, hp_steps[-1] + 1, MP0, FULL)
            fill = range(hp_steps[0], mp_steps[-1] + 1)
            sounds = [i for i in range(RUN) if rams[i][SOUND_CURRENT] == FILL_SOUND]
            assert sounds and all(i in fill and rams[i][INTERRUPT_COUNTER] & 3 == 0 for i in sounds), sounds
            assert all(b - a == 4 for a, b in zip(sounds, sounds[1:])) and len(sounds) >= len(fill) // 4, sounds
            bars = {}
            for i in range(grey[-1] + 1, RUN):
                band = hud.Band(picture(i), hud.HUD_X, glyphs)
                shown = (band.bar(hud.HP_LINE), band.bar(hud.MP_LINE))
                assert shown == (rams[i - 1][HP], rams[i - 1][MP]), (i, shown, rams[i - 1][HP], rams[i - 1][MP])
                bars.setdefault(shown, i)
            assert (FULL, FULL) in bars and len({hp for hp, _ in bars}) > 30 and len({mp for _, mp in bars}) > 60
            line = [i for i in range(RUN) if rams[i][MESSAGE_ID] == SLOT]
            assert line and line[0] >= mp_steps[-1], (line[:1], mp_steps[-1])     # its first letter as MP ends
            keep(picture(grey[0]), "save-flash")
            keep(picture(hp_steps[24]), "hp-rising")
            keep(picture(mp_steps[0] + 1), "hp-full-mp-rising")
            keep(picture(mp_steps[40]), "mp-rising")
            assert rams[-1][MESSAGE_ID] == 0 and not rams[-1][TEXTBOX_CLOSED], "his line typed out, waiting"
            keep(picture(RUN - 1), "both-full-saved-line")
            print(f"ok: A saves: frame {grey[0]} (A on {PRESS}) and the next {FLASH - 1} are greyed whole, the "
                  f"424-wide view's black margins too, and no other; HP {HP0} to {FULL} a point a frame on frames "
                  f"{hp_steps[0]}-{hp_steps[-1]}, then MP {MP0} to {FULL} on {mp_steps[0]}-{mp_steps[-1]}, each bar "
                  f"a frame behind; the fill sound ${FILL_SOUND:02X} on {len(sounds)} frames, every 4th of the "
                  f"fill; his line from frame {line[0]}")
            shutil.rmtree(out / "run-save")

            # 2. A state saved mid-fill, inside the flash, loads and finishes.
            rams, picture = replay("mid", mid, [(0, "-")], RUN - PRESS - 4)
            left = [i for i in range(len(rams)) if greyed(picture(i))]
            assert left == list(range(grey[-1] - (PRESS + 4))), left      # the frames after the one saved
            assert HP0 < rams[0][HP] < FULL and rams[0][MP] == MP0, (rams[0][HP], rams[0][MP])
            assert rams[-1][HP] == FULL and rams[-1][MP] == FULL and any(r[MESSAGE_ID] == SLOT for r in rams)
            print(f"ok: a state saved mid-fill (HP {rams[0][HP]}, inside the flash) loads and finishes: the "
                  f"flash's last {len(left)} frames, HP and MP to {FULL}, his line")
            shutil.rmtree(out / "run-mid")

            # 3. B: nothing.
            rams, picture = replay("decline", question, schedule("B"), 60)
            assert not any(greyed(picture(i)) for i in range(len(rams)))
            assert all((r[HP], r[MP]) == (HP0, MP0) for r in rams) and rams[-1][TEXTBOX_CLOSED]
            assert not any(r[SOUND_CURRENT] == FILL_SOUND for r in rams)
            print(f"ok: B: no flash, no fill sound, HP {HP0} and MP {MP0} as they were, the box closed")
            shutil.rmtree(out / "run-decline")

            # In the window: A writes the file and fills; B saves nothing.
            gamedir = out / "game"
            s0 = tcp("files_state")
            g.load(question)
            act("a")
            until("the fill", lambda: tcp("files_state")["guru"] == GURU_SAVED, 400)
            s = tcp("files_state")
            assert s["saves"] == s0["saves"] + 1 and s["refills"] == 1 and s["fill"] == [FULL - HP0, FULL - MP0], s
            assert (ram(HP), ram(MP), ram(TEMPLE_SPAWN_POINT)) == (FULL, FULL, 0)
            assert (gamedir / "faxanadu_file1.txt").exists()
            page_through("the end", lambda: not box_open())
            assert (ram(HP), ram(MP)) == (FULL, FULL)
            g.load(question)
            act("b")
            until("the box to close", lambda: not box_open(), 300)
            assert tcp("files_state")["saves"] == s["saves"] and (ram(HP), ram(MP)) == (HP0, MP0)
            print("ok: in the window A writes file 1 and fills HP and MP to 80; B saves nothing and fills nothing")
            g.close()

        if args.wants("original"):
            talk(False)
            page_through("the end", lambda: not box_open())
            assert (ram(HP), ram(MP)) == (HP0, MP0), (ram(HP), ram(MP))
            print(f"ok: with the remaster off, Eolis's Guru leaves HP at {HP0} and MP at {MP0}")
            g.close()
        print("screenshots:", ", ".join(g.shots))
    except BaseException:
        if g.running():
            g.shot("failure")
        raise
    finally:
        g.close()


if __name__ == "__main__":
    main()
