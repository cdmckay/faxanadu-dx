"""Play the music under the map and the quest log end to end (#162).

    python3 tests/map/music_plays_on.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/map-music
    python3 tests/map/music_plays_on.py ... --part quests

Both screens open through the game's own pause, which the sound engine mutes
(Music_HandleOnInterrupt); with the remaster the music plays on under them,
and nothing else moves. Each part starts in Eolis's town (the fixture: a new
game, through the gate), its people walking about, Ointment running (an
external prerequisite: a timer that would drain). Three parts, each its own
game:

map       In a hidden window, M opens the map: over TCP, 60 frames on, the
          game is still paused (Game_PausedState 1, never the sound engine's
          muted $81), the hero where he was, and the music's scripts
          (Music_CurrentScriptAddrs) have moved on.
          Then headless from the fixture, frame exact, the map opened through
          the Select menu's MAP row (Start, Down, A: the same pause check),
          and closed with B at frame 120, and again at frame 376, 256 frames
          later, so the two runs' InterruptCounter and the pause loop's turn
          of the sprite ranges agree. In every frame of both the music ticks
          once (each channel's Music_ChannelTicksRemaining down by one, or its
          script on to its next note): through the Select menu, the map's
          opening, the map, the close and play after, with no restart or
          skip. While the map is open, nothing else changes frame for frame:
          CPU RAM but the music's own ($F2-$FA, $012C-$0181) and what the
          original's Start pause changes too (the NMI's InterruptCounter, the
          shadow OAM that Sprites_FlipRanges turns each frame, the stack), the
          nametables, the palette, CHR RAM and the game's picture; the sound
          effects' RAM ($FB, $0121-$012B) holds, nothing queued or playing.
          Over 48 of its frames (a save state each, for HwApu's reg[16],
          $4000-$400F as last written) the squares' and the triangle's
          registers keep taking new values, each channel's, and the audio
          (--wav-out) is at least half as loud as in play after it closes.
          After each close, for 120 frames, both runs play on the same, every
          byte but the music's and the stack's: the time under the map,
          music and all, changed nothing, and no sound plays late.
quests    The same with L and the Select menu's QUESTS row (Start, A): the
          quest log.
original  With the remaster off (the stock view), Start pauses headless as
          ever: Game_PausedState $81, the music's RAM held, nothing else but
          what the pause always changes, the channels' registers as
          Audio_Reset left them for 48 frames, and the audio silent.

The audio of the runs goes in OUT as WAVs (map-late.wav, quests-late.wav,
original.wav).
"""
from concurrent.futures import ThreadPoolExecutor
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import wave

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
import fax_e2e  # noqa: E402
from fax_e2e import Game, chunks, fixture, parse_args, ram, tcp, until, wait  # noqa: E402

# RAM
INTERRUPT_COUNTER, PLAYER_X, PLAYER_Y = 0x1A, 0x9E, 0xA1
SCRIPTS = range(0xF2, 0xFA)            # Music_CurrentScriptAddrs: each channel's place in its music, 2 bytes
MUSIC_CURRENT = 0xFA                   # bit 7 once Music_Load has started it
PAUSED = 0x120                         # Game_PausedState: 1 paused; the sound engine adds bit 7 as it mutes
SOUND_TICKS = range(0x123, 0x127)      # SoundEffect_TicksRemaining, a channel each: 0 when none plays
TICKS = 0x130                          # Music_ChannelTicksRemaining, a channel each
NOISE_REMAINING = 0x169                # Music_Noise_Remaining: the noise channel's hits left before its script
MUSIC = set(SCRIPTS) | {MUSIC_CURRENT} | set(range(0x12C, 0x182))   # MScript_NoteDurations to
#                                        Music_SQ2_TimerLowBias: only the sound engine's music writes them. The
#                                        sound effects' (SoundEffect_Current $FB, $0121-$012B) are not here
STACK = set(range(0x1A0, 0x200))       # above Screen_ScrollVert_ZeroData ($0192-$0199), the last of page 1
SHADOW_OAM = set(range(0x700, 0x800))  # Sprites_FlipRanges, in the pause loop, turns its ranges each frame
TEXTBOX_CLOSED = 0x20C
DURATION_OINTMENT = 0x427              # seconds left, $FF when not running
# What the original's Start pause changes each frame too.
PAUSE_CHANGES = {INTERRUPT_COUNTER} | SHADOW_OAM | STACK
# The APU's save-state chunk is nesrecomp's HwApu (runner/cyc/hw_apu.c): its reg[16], $4000-$400F as last
# written, starts at byte 31. SQ1 $4000-$4003, SQ2 $4004-$4007, the triangle $4008-$400B.
APU_REGS = 31
CHANNELS = {"SQ1": range(0, 4), "SQ2": range(4, 8), "TRI": range(8, 12)}
AUDIO_RESET = bytes([0x10, 0, 0, 0, 0x10, 0, 0, 0, 0, 0, 0, 0, 0x10, 0, 0, 0])   # what Audio_Reset writes
PACKAGE = ("faxanadu.dx", "dx")
# Whether each screen is open, in its save-state record (fax_map.c, fax_quests.c state_get): the map's after
# its version, what the hero has seen (312 bytes), the last place (3) and the Gurus counted; the quest log's
# after its version, five counts of 4 bytes and 7 bytes of its banner and lines.
OPEN_FLAGS = {"map": ("faxanadu.map", 1 + 312 + 3 + 1), "quests": ("faxanadu.quests", 1 + 20 + 7)}

OPEN_BY = 100                          # the frame by which the menu's row has opened its screen
CLOSE, LATER, AFTER = 120, 256, 120    # B at CLOSE (and CLOSE + LATER); frames compared after each close
SETTLE = 8                             # frames after it opens to the menu's A let go and its box's last
#                                        tiles written (the PPU buffer, $1F-$22): from then on, all held
APU_FRAMES = 48                        # held frames with a save state each, for the APU
ROUTES = {"map": [(2, "START"), (4, "-"), (40, "DOWN"), (42, "-"), (60, "A"), (62, "-")],
          "quests": [(2, "START"), (4, "-"), (60, "A"), (62, "-")]}
KEYS = {"map": ("M", "map_state"), "quests": ("L", "quest_state")}
PARTS = ("map", "quests", "original")


class Frame:
    """One frame's record from --frame-log, after the frame: CPU RAM and the PPU's memories."""

    def __init__(self, head, palette, oam, blobs):
        self.number = head[0]
        self.palette, self.oam = palette, oam
        self.ram, self.ciram, self.cart, self.chr, self.picture = blobs[:5]

    def ppu(self):
        return self.ciram, self.palette, self.chr, self.picture


def read_frames(path):
    data = path.read_bytes()
    assert data[:8] == b"CYCFRAME" and struct.unpack_from("<I", data, 8)[0] == 2
    frames, pos = [], 12
    while pos < len(data):
        head = struct.unpack_from("<9I", data, pos)
        pos += 36
        palette, oam = data[pos:pos + 32], data[pos + 32:pos + 288]
        pos += 288
        blobs = []
        for size in head[3:9]:
            blobs.append(data[pos:pos + size])
            pos += size
        frames.append(Frame(head, palette, oam, blobs))
    return frames


def apu_regs(path):
    """A save state's APU registers, and which of the two screens it has open."""
    data = Path(path).read_bytes()
    pos = next(p for tag, p, _ in chunks(data) if tag == b"APU ")
    records = {bytes(data[p:p + 64]).rstrip(b"\0"): p + 64 for tag, p, _ in chunks(data) if tag == b"MOD "}
    shown = {part for part, (name, at) in OPEN_FLAGS.items()
             if name.encode() in records and data[records[name.encode()] + at]}
    return data[pos + APU_REGS:pos + APU_REGS + 16], shown


def loudness(wav, frames, first, last):
    """The audio's RMS (of full scale) over game frames first..last of a run's WAV."""
    with wave.open(str(wav)) as w:
        assert w.getnchannels() == 1 and w.getsampwidth() == 2, (w.getnchannels(), w.getsampwidth())
        rate, raw = w.getframerate(), w.readframes(w.getnframes())
    samples = struct.unpack(f"<{len(raw) // 2}h", raw)
    per = len(samples) / frames                                     # samples a game frame
    part = samples[int(first * per):int((last + 1) * per)]
    assert part, (first, last, len(samples))
    return math.sqrt(sum(s * s for s in part) / len(part)) / 32768, rate


class Runs(Game):
    """The build in a hidden window (fax_e2e.Game), with headless runs."""

    def mods(self, original):
        root = self.out / ("mods-original" if original else "mods-dx")
        if not root.exists():
            shutil.copytree(self.out / "game" / "mods" / "packages", root / "packages")
            (root / "state.toml").write_text(fax_e2e.features_toml({PACKAGE: False} if original else {}))
        return root

    def headless(self, state, schedule, frames, original=False, apu=None, wav=None):
        """Play `frames` frames from `state` with no window, frame exact, the
        remaster on (or off: `original`), buttons held on a schedule of
        (frame, buttons) counted from the state's frame ("START", "-" for
        none). Returns each frame's record, and with `apu`, a range of those
        frames, the APU's registers at each; with `wav`, the audio goes there."""
        start = struct.unpack_from("<I", Path(state).read_bytes(), 28)[0]
        run = Path(tempfile.mkdtemp(prefix="run-", dir=self.out))
        mods = shutil.copytree(self.mods(original), run / "mods")   # its own: a run writes its selection back
        (run / "input.txt").write_text("".join(f"{start + at} {buttons}\n" for at, buttons in sorted(schedule)))
        log = run / "frames.bin"
        args = [str(self.exe), str(self.rom), "--load-state", str(state), "--input", str(run / "input.txt"),
                "--frames", str(start + frames), "--frame-log", str(log),
                "--frame-log-frames", f"{start}:{start + frames - 1}", "--mods-root", str(mods), "--no-save"]
        for k in apu or ():
            args += ["--save-state", f"{start + k}:{run / f'{k:04d}.cycstate'}"]
        if wav:
            args += ["--wav-out", str(wav)]
        done = subprocess.run(args, capture_output=True, text=True)
        assert done.returncode == 0, done.stdout + done.stderr
        got = read_frames(log)
        assert len(got) == frames and got[0].number == start, (len(got), frames)
        regs = {k: apu_regs(run / f"{k:04d}.cycstate") for k in apu or ()}
        shutil.rmtree(run)
        return (got, regs) if apu else got

    def headless_all(self, runs):
        """headless(*args, **options) for each (args, options) at once, each its own process; their results."""
        for _, options in runs:
            self.mods(options.get("original", False))                # made before the runs share it
        with ThreadPoolExecutor(max_workers=min(len(runs), os.cpu_count() or 4)) as pool:
            return list(pool.map(lambda run: self.headless(*run[0], **run[1]), runs))


def changed(frames, first, last, ignore):
    """CPU RAM addresses outside `ignore` that change anywhere in frames first..last."""
    base = frames[first].ram
    return sorted({a for f in frames[first + 1:last + 1] for a in range(0x800)
                   if a not in ignore and f.ram[a] != base[a]})


def hexes(addrs):
    return " ".join(f"${a:04X}" for a in addrs)


def check_ticks(frames, what):
    """The music ticks once a frame, every frame (Music_PlayNext): each
    channel's ticks down by one, its script held; or on its note's last tick,
    on to the next (MScripts_InvokeNext: its script moved on, or the noise
    channel's next hit, Music_Noise_Remaining). Returns how many notes started
    in each frame."""
    notes = [0] * len(frames)
    for k in range(1, len(frames)):
        was, now = frames[k - 1].ram, frames[k].ram
        assert now[MUSIC_CURRENT] == was[MUSIC_CURRENT] and now[MUSIC_CURRENT] & 0x80, \
            (what, k, hex(now[MUSIC_CURRENT]))                       # the same music, never loaded again
        for c in range(4):
            at = SCRIPTS[2 * c]
            moved = now[at:at + 2] != was[at:at + 2]
            t0, t1 = was[TICKS + c], now[TICKS + c]
            if t0 == 1:
                assert moved or (c == 3 and now[NOISE_REMAINING] != was[NOISE_REMAINING]), \
                    (what, f"frame {k}, channel {c}: its note over, nothing next")
                notes[k] += 1
            else:
                assert t1 == (t0 - 1) & 0xFF and not moved, \
                    (what, f"frame {k}, channel {c}: ticks {t0} then {t1}, script moved {moved}")
    return notes


def screen_route(game, part, start):
    """The map or the quest log, opened from the Select menu's row and closed
    with B at CLOSE and at CLOSE + LATER, headless: both runs' frames, the
    late one's APU registers over APU_FRAMES held frames, and its audio."""
    route = ROUTES[part]
    late_apu = range(OPEN_BY + 8, OPEN_BY + 8 + APU_FRAMES)
    early, (late, regs) = game.headless_all([
        ((start, route + [(CLOSE, "B"), (CLOSE + 4, "-")], CLOSE + AFTER + 8), {}),
        ((start, route + [(CLOSE + LATER, "B"), (CLOSE + LATER + 4, "-")], CLOSE + LATER + AFTER + 8),
         dict(apu=late_apu, wav=game.out / f"{part}-late.wav")),
    ])
    spans = []
    for name, frames in (("early", early), ("late", late)):
        paused = [k for k, f in enumerate(frames) if f.ram[PAUSED]]
        assert paused, (part, name, "never paused")
        opened, closed = paused[0], paused[-1] + 1
        assert paused == list(range(opened, closed)), (part, name, "paused in two spells")
        assert 60 < opened < OPEN_BY and CLOSE <= closed - (LATER if name == "late" else 0) <= CLOSE + 4, \
            (part, name, opened, closed)
        assert {frames[k].ram[PAUSED] for k in paused} == {1}, (part, name, "muted: $81")
        menu = [k for k in range(opened) if frames[k].ram[TEXTBOX_CLOSED] == 0]
        assert menu, (part, name, "no Select menu before the pause")
        notes = check_ticks(frames, f"{part} {name}")
        spans.append((opened, closed, menu, sum(notes[opened:closed])))
    (o_e, c_e, menu, _), (o_l, c_l, _, notes) = spans
    assert o_e == o_l and c_l - c_e == LATER, spans

    # Held: nothing but the music (and what the pause always changes) moves, frame for frame.
    held = o_l + SETTLE
    moved = changed(late, held, c_l - 1, MUSIC | PAUSE_CHANGES)
    assert not moved, (part, "changed under the screen:", hexes(moved))
    ppu = late[held].ppu()
    assert all(late[k].ppu() == ppu for k in range(held, c_l)), (part, "the PPU's memories or picture changed")
    sounds = {late[k].ram[a] for k in range(o_l, c_l) for a in SOUND_TICKS}
    assert sounds == {0}, (part, "a sound effect played under the screen", sounds)
    assert o_l < late_apu[0] and late_apu[-1] < c_l, (part, o_l, c_l)
    assert all(regs[k][1] == {part} for k in late_apu), (part, "not the screen open", regs[late_apu[0]][1])
    in_screen = [regs[k][0] for k in late_apu]
    changes = {name: sum(any(a[i] != b[i] for i in r) for a, b in zip(in_screen, in_screen[1:]))
               for name, r in CHANNELS.items()}
    frames_new = sum(a[:12] != b[:12] for a, b in zip(in_screen, in_screen[1:]))
    assert frames_new >= APU_FRAMES // 6 and all(changes.values()), (part, frames_new, changes)
    loud, _ = loudness(game.out / f"{part}-late.wav", len(late), held, c_l - 1)
    play, _ = loudness(game.out / f"{part}-late.wav", len(late), c_l + SETTLE, len(late) - 1)
    assert loud > 0.5 * play and loud > 0.01, (part, loud, play)
    print(f"ok: {part}: headless, opened from the Select menu at frame {o_l}, open for {c_l - o_l} frames: "
          f"Game_PausedState 1 throughout, the music ticked every frame ({notes} notes started), the APU's "
          f"square and triangle registers new on {frames_new} of {APU_FRAMES - 1} frames ({changes}), audio "
          f"RMS {loud:.3f} against {play:.3f} in play after; nothing else changed, the sound effects silent")
    print(f"ok: {part}: the Select menu, open frames {menu[0]}-{menu[-1]}, played the music every frame too "
          f"(Game_PausedState 0)")

    # After the close: the same play, whenever it closed.
    for j in range(AFTER):
        e, lt = early[c_e + j], late[c_l + j]
        diff = [a for a in range(0x800) if a not in MUSIC | STACK and e.ram[a] != lt.ram[a]]
        assert not diff, (part, f"{j} frames after the close", hexes(diff))
        assert e.ppu() == lt.ppu() and e.oam == lt.oam, (part, f"{j} frames after the close: the picture")
    assert early[c_e + AFTER - 1].ram[DURATION_OINTMENT] < early[c_e].ram[DURATION_OINTMENT] or \
        early[c_e].ram[DURATION_OINTMENT] == 0xFF, "the Ointment never drained after the close"
    print(f"ok: {part}: closed with B at {c_e} and at {c_l}: the {AFTER} frames after are the same in both but "
          f"the music, which went {LATER} frames further, so no sound played late; Ointment held at "
          f"{late[o_l].ram[DURATION_OINTMENT]} under the screen")


def window(game, part, start):
    """The screen's own button in the window: paused, the hero still, the music on."""
    key, cmd = KEYS[part]
    game.launch(at="eolis-town")
    game.load(start)
    wait(10)
    x, y = ram(PLAYER_X), ram(PLAYER_Y)
    opens = tcp(cmd)["opens"]
    tcp("key", name=key, frames=2)
    until(f"the {part} to open", lambda: tcp(cmd)["open"] and tcp(cmd)["opens"] == opens + 1, 300)
    until("the pause", lambda: ram(PAUSED) != 0, 120)
    before = ram(SCRIPTS[0], len(SCRIPTS))
    wait(60)
    after = ram(SCRIPTS[0], len(SCRIPTS))
    assert tcp(cmd)["open"] and ram(PAUSED) == 1 and (ram(PLAYER_X), ram(PLAYER_Y)) == (x, y), \
        (part, hex(ram(PAUSED)), x, y)
    assert after != before, (part, "the music's scripts held", before.hex(), after.hex())
    print(f"ok: {part}: {key} opens it in the window, the game paused (Game_PausedState 1) and the music on: "
          f"its scripts {before.hex(' ')} then {after.hex(' ')} 60 frames later")
    game.close()


def main():
    args = parse_args(__doc__, parts=PARTS)
    game = Runs.from_args(args)
    out = game.out
    try:
        for part in ("map", "quests"):
            if not args.wants(part):
                continue
            game.set_features({PACKAGE: True})
            start = out / "start.cycstate"
            fixture(game.state("eolis-town"), start, {DURATION_OINTMENT: 0x30})
            window(game, part, start)
            screen_route(game, part, start)

        if args.wants("original"):
            game.set_features({PACKAGE: False})
            start = out / "start-original.cycstate"
            fixture(game.state("eolis-town"), start, {DURATION_OINTMENT: 0x30})
            apu = range(20, 20 + APU_FRAMES)
            frames, regs = game.headless(start, [(2, "START"), (4, "-")], 300, original=True, apu=apu,
                                         wav=out / "original.wav")
            paused = [k for k, f in enumerate(frames) if f.ram[PAUSED]]
            assert paused and paused[-1] == len(frames) - 1 and paused[0] < 10, paused[:3]
            assert frames[paused[0]].ram[PAUSED] == 1 and {frames[k].ram[PAUSED] for k in paused[1:]} == {0x81}, \
                ("not muted", [hex(frames[k].ram[PAUSED]) for k in paused[:3]])  # muted at the next frame's NMI
            first = paused[0] + SETTLE                                  # Start let go
            moved = changed(frames, first, len(frames) - 1, PAUSE_CHANGES)
            assert not moved, ("changed in Start's pause:", hexes(moved))
            assert all(regs[k] == (AUDIO_RESET, set()) for k in apu), [regs[k][0].hex(" ") for k in apu][:3]
            quiet, _ = loudness(out / "original.wav", len(frames), first, len(frames) - 1)
            assert quiet < 0.001, quiet
            print(f"ok: original: Start pauses at frame {paused[0]} and mutes: Game_PausedState $81, the music's "
                  f"RAM and everything else held, the APU's registers as Audio_Reset left them for {APU_FRAMES} "
                  f"frames, audio RMS {quiet:.5f}")
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    main()
