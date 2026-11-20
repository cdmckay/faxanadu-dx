"""Play Hold A to speed up text end to end in a hidden window.

    python3 tests/text_speed/text_speed.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/text-speed

The game runs uncapped and the script waits on the game's own state, never on
the clock, on a free TCP port, so any number of runs can go in parallel. It
uses Guru travel's test helpers (tests/travel/guru_travel.py).

Starts a new game, then edits save-state RAM so the hero dies with Conflate as
the furthest temple (an external prerequisite, not coverage). Then:

1. Holds A through the death message: it types to its $FC page break and
   waits there, and at its end, until A is pressed again.
2. Times the Conflate Guru's greeting typed without A, with A held, and with
   A released partway, and checks the held typing sound keeps its rhythm.
   Then, frame exact: from a save state taken as the greeting types, runs
   the same number of frames headless with A held, without A, and held with
   the mods off, and compares how far each got.
3. Holds A through Guru travel's offer and its first destination: neither
   question is answered until A is pressed again, and B still declines.

Screenshots of each held wait, and of the greeting after the same frames
held and not held, go in OUT.
"""
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "travel"))
from fax_e2e import Game, act, chunks, fixture, frame, parse_args, ram, tcp, typed, until, wait  # noqa: E402
import guru_travel as gt  # noqa: E402

MESSAGE_ID, PROCESSED, PAUSED = 0x213, 0x214, 0x215   # MessageID, Message_ProcessedLength, TextBox_MessagePaused
INTERRUPT_COUNTER = 0x1A                               # counts every NMI: one a frame
SOUND_TICK = 0x127                                     # SoundEffect_State_Counter: the typing sound's tick, 1-4
ISCRIPT_OFFSET = 0xDD                                  # moves when the script goes on to its next action
NEGATIVE_THOUGHTS, MEDITATE_WITH_YOU = 0x24, 0x22      # the death message; the Guru's greeting
OFFER_PHASE, ASKING_PHASE = 2, 3                       # fax_travel.c's Phase
HOLD = 3000                                            # frames: longer than any step below
SAME_FRAMES = 36                                       # the headless runs: part of the greeting, held or not


def hold_a():
    tcp("action", name="a", frames=HOLD)


def release_a():
    tcp("action", name="a", frames=1)
    wait(4)


def fast():
    return tcp("text_speed_state")["fast"]


def snapshot():
    """One frame's (InterruptCounter, Message_ProcessedLength, typing, sound tick), from a single read."""
    data = bytes.fromhex(tcp("read_ram", addr=INTERRUPT_COUNTER, len=PAUSED - INTERRUPT_COUNTER + 1)["hex"])
    typing = data[MESSAGE_ID - INTERRUPT_COUNTER] != 0 and data[PAUSED - INTERRUPT_COUNTER] == 0
    return data[0], data[PROCESSED - INTERRUPT_COUNTER], typing, data[SOUND_TICK - INTERRUPT_COUNTER]


def time_page():
    """Type the rest of the page: (frames per message byte, frames, share of
    snapshots in the typing sound's first tick), from the next character to
    the last snapshot taken while the page was still typing. TCP polling is a
    few frames coarse, so frames are counted by InterruptCounter. A box takes
    a few frames to open after Messages_Load, so the clock starts on a
    character, not on MessageID."""
    start = first = snapshot()
    while first[1] == start[1] and first[2]:
        first = snapshot()
    last = first
    frames, ticks = 0, []
    while True:
        now = snapshot()
        if not now[2]:
            break
        frames += (now[0] - last[0]) % 256
        ticks.append(now[3])
        last = now
    return frames / ((last[1] - first[1]) % 256), frames, ticks.count(1) / max(len(ticks), 1)


def state_ram(path, addr):
    """A byte of CPU RAM in a save state."""
    data = path.read_bytes()
    pos = next(p for tag, p, _ in chunks(data) if tag == b"HW  ")
    return data[pos + 23 + addr]


def still(what, test, frames=120):
    """`test` stays true for `frames` frames."""
    end = frame() + frames
    while frame() < end:
        assert test(), what
        time.sleep(0.005)


def main():
    args = parse_args(__doc__)
    game = Game.from_args(args)
    out, shots = game.out, game.shots

    def headless(name, state, buttons, mods):
        """Run SAME_FRAMES frames headless from `state` holding `buttons` ("-" for
        none), with the mod catalog `mods`: the bytes of the message read since
        the state, and a screenshot of the last frame."""
        start = struct.unpack_from("<I", state.read_bytes(), 28)[0]   # the frame it was saved after
        folder = out / name
        folder.mkdir()
        (folder / "input.txt").write_text(f"{start} {buttons}\n")
        final = folder / "final.cycstate"
        with open(folder / "run.log", "w") as run_log:
            subprocess.run([str(game.exe), str(game.rom), "--no-save",
                            "--mods-root", str(mods), "--load-state", str(state),
                            "--frames", str(start + SAME_FRAMES), "--input", str(folder / "input.txt"),
                            "--screenshot", str(folder / "picture.png"),
                            "--save-state", f"{start + SAME_FRAMES - 1}:{final}"],
                           cwd=folder, env=game.env, stdout=run_log, stderr=subprocess.STDOUT, timeout=120, check=True)
        assert state_ram(final, MESSAGE_ID) == MEDITATE_WITH_YOU, f"{name}: still the greeting"
        shot = out / f"{len(shots):02d}-{name}.png"
        shutil.copy2(folder / "picture.png", shot)
        shots.append(shot.name)
        return (state_ram(final, PROCESSED) - state_ram(state, PROCESSED)) % 256

    def line():
        return tcp("travel_state")["text"]

    def talk_to_guru():
        act("left", 170, 10)
        act("up", 4, 10)
        until("the Guru's greeting", lambda: ram(MESSAGE_ID) == MEDITATE_WITH_YOU)

    def next_message_typed():
        """Wait for the next message to start, then to type out."""
        until("the next message", lambda: ram(MESSAGE_ID) != 0, 300)
        until("the message to type out", typed, 300)

    def advance(expect, tries=12):
        """Press A on each typed-out page until Guru travel's line `expect` is up."""
        for _ in range(tries):
            until("a page to type out", lambda: line() == expect or typed())
            if line() == expect:
                return
            act("a")
        until(f"line {expect}", lambda: line() == expect, 300)

    try:
        game.launch()
        assert tcp("text_speed_state")["enabled"], "Hold A to speed up text is on by default"
        until("the title", lambda: frame() > 700)
        tcp("pad", buttons="start", frames=10)
        until("the Eolis gate", lambda: frame() > 1900)
        gate = out / "gate.cycstate"
        tcp("save_state", path=str(gate))
        dead = out / "conflate-death.cycstate"
        fixture(gate, dead, {gt.TEMPLE_SPAWN_POINT: gt.CONFLATE, gt.PLAYER_IS_DEAD: 1})

        # 1. The death message: "Don't have/negative/thoughts." $FC "Remember/your mantra."
        tcp("load_state", path=str(dead))
        until("the death message", lambda: ram(MESSAGE_ID) == NEGATIVE_THOUGHTS, 600)
        hold_a()
        until("the page break", lambda: ram(PAUSED) != 0, 300)
        stopped = ram(PROCESSED)
        still("held A waits at the page break",
              lambda: ram(PAUSED) != 0 and ram(MESSAGE_ID) == NEGATIVE_THOUGHTS and ram(PROCESSED) == stopped)
        game.shot("page-break-held")
        release_a()
        assert ram(PAUSED) != 0, "releasing A does not go on"
        hold_a()                                     # press again, and keep holding
        until("the next page", lambda: ram(PAUSED) == 0, 30)
        until("the end of the message", lambda: ram(MESSAGE_ID) == 0, 300)
        offset = ram(ISCRIPT_OFFSET)
        still("held A waits at the end of the message",
              lambda: ram(ISCRIPT_OFFSET) == offset and not gt.in_temple(gt.CONFLATE))
        game.shot("message-end-held")
        release_a()
        act("a")
        until("Conflate's temple", lambda: gt.in_temple(gt.CONFLATE), 600)
        print("ok: held A stops at the death message's page break and its end; a new press goes on")

        wait(60)
        temple = out / "conflate-temple.cycstate"
        tcp("save_state", path=str(temple))

        # 2. The Guru's greeting, "You need peace/of mind./I will meditate/with you.", one box.
        tcp("load_state", path=str(temple))
        talk_to_guru()
        before = fast()
        normal, frames_normal, _ = time_page()
        assert fast() == before, "nothing is sped up without A"

        tcp("load_state", path=str(temple))
        talk_to_guru()
        hold_a()
        held, frames_held, restarts = time_page()
        assert fast() > before, "the hook sped up the held frames"
        until("the greeting to type out", typed)
        offset = ram(ISCRIPT_OFFSET)
        still("held A does not go on to the mantra", lambda: ram(ISCRIPT_OFFSET) == offset)
        game.shot("greeting-typed-out-held")
        release_a()

        tcp("load_state", path=str(temple))
        talk_to_guru()
        hold_a()
        start = ram(PROCESSED)
        until("part of the greeting", lambda: (ram(PROCESSED) - start) % 256 >= 20, 300)
        release_a()
        after, _, _ = time_page()

        print(f"   greeting: {frames_normal} frames at {normal:.2f} a byte without A, {frames_held} at {held:.2f} "
              f"with A held, {after:.2f} after letting go")
        assert normal >= 3.7, f"the game's own pace is a character every 4 frames, not {normal:.2f}"
        assert held <= 1.15, f"held A types a character every frame, not every {held:.2f}"
        assert normal / held >= 3.5, f"held A is {normal / held:.1f}x, not about 4x"
        assert after >= 3.7, f"letting go returns to the game's pace, not {after:.2f}"
        # The typing sound is 4 ticks long. Started every frame it would sit
        # on its first tick; held A starts it every 4th frame.
        assert restarts <= 0.6, f"the typing sound restarts on {restarts:.0%} of held frames"
        print(f"ok: held A types the greeting {normal / held:.1f}x faster; letting go returns to normal speed")
        print(f"ok: held, the typing sound plays out its 4 ticks (first tick on {restarts:.0%} of snapshots)")

        # Frame exact: the same frames from the same state, headless. The
        # state is saved once the greeting has begun to type, so every frame
        # after it is a typing frame.
        tcp("load_state", path=str(temple))
        talk_to_guru()
        loaded = ram(PROCESSED)
        until("the greeting's first character", lambda: ram(PROCESSED) != loaded, 120)
        typing = out / "greeting-typing.cycstate"
        tcp("save_state", path=str(typing))
        mods = out / "headless-mods"
        shutil.copytree(out / "game" / "mods", mods)
        stock = out / "stock-mods"
        stock.mkdir()
        not_held = headless("greeting-not-held", typing, "-", mods)
        held_now = headless("greeting-held", typing, "A", mods)
        stock_held = headless("greeting-held-mods-off", typing, "A", stock)
        print(f"   greeting, {SAME_FRAMES} frames from one state: {not_held} bytes without A, {held_now} with A held, "
              f"{stock_held} held with the mods off")
        assert abs(not_held - SAME_FRAMES / 4) <= 1, f"without A, a byte every 4 frames, not {not_held}"
        assert SAME_FRAMES - 2 <= held_now <= SAME_FRAMES, f"held, a byte a frame after the press, not {held_now}"
        assert stock_held == not_held, "with the mods off, holding A types at the game's pace"
        print(f"ok: in the same {SAME_FRAMES} frames, held A types {held_now} bytes of the greeting to "
              f"{not_held} without; with the mods off, holding A changes nothing")

        # 3. Guru travel's questions, held through.
        tcp("load_state", path=str(temple))
        talk_to_guru()
        advance(gt.OFFER)
        hold_a()
        next_message_typed()
        still("held A does not answer the offer",
              lambda: line() == gt.OFFER and tcp("travel_state")["phase"] == OFFER_PHASE)
        game.shot("offer-held")
        release_a()
        hold_a()                                     # press again, and keep holding
        until("the first destination", lambda: line() == gt.TO_GURU + 0)
        next_message_typed()
        still("held A does not choose Eolis", lambda: (lambda s: s["text"] == gt.TO_GURU + 0 and s["dest"] == -1
                                                       and s["phase"] == ASKING_PHASE)(tcp("travel_state")))
        game.shot("destination-held")
        act("b")                                     # with A still held: B asks about the next, the farewell here
        until("the farewell", lambda: line() == gt.FAREWELL)
        release_a()
        assert tcp("travel_state")["dest"] == -1
        print("ok: held A answers neither Guru travel question; a new press does, and B still declines")
        print("screenshots:", ", ".join(shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
