"""Play the taller dagger end to end, headless and in a hidden window.

    python3 tests/dagger/taller_dagger.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/dagger

Copies the build to OUT and starts from the fixture eolis-zozuras (an
external prerequisite, not coverage, tests/e2e/fax_e2e.py): the first
enemies, two Zozura crawling right along the ground on Eolis screen 4, fresh
as the screen loads, the hero at its right facing them. Each case is that
save, played on headless to the frame before the nearer Zozura comes within
any case's distance, with a weapon in hand, full HP and a Zozura that no hit
here kills; he swings once as it crawls in, when it is a set distance away
(his x minus its x).

The swings play headless, frame exact, every case at once (#155): a swing's
first hit phase lasts 3 frames, which polls over TCP on a loaded machine can
step over, and a press sent on a poll goes down any number of frames late.
Each frame's RAM is read after it, and each press goes down on the frame
after the one whose RAM calls for it, as polls a frame apart would press it;
a run that needs a press it lacks plays again with it. Before that, each
case is loaded in a hidden window over TCP (on a reserved port, uncapped):
the dagger's reach is served again after a save-state load in a running game.

The reach is read from RAM, from Maybe_WeaponRange_X/Y, which Player_DrawWeapon
loads from MAYBE_WEAPON_RANGES every frame: a read through the CPU, so it sees
the read override, which a peek of the ROM would not.

A Zozura on the hero's ground stands 16 pixels below his top, so only a
swing's first hit phase (3 frames, the weapon 8 pixels down) can reach it,
and only with a Y reach of 8. In that phase the dagger reaches it from 23
pixels away and the swords from 31; it hurts the hero from 15.

1. With the feature on (the default), the dagger's reach is (8, 8), and a
   swing from close in hits the Zozura. Swung from the Long Sword's distance
   it misses, where the Long Sword (16, 8) hits: the dagger kept its short
   reach.
2. Nothing under the hero's feet: swung at the top of a jump, right over the
   Zozura, the taller dagger passes over it, as the Long Sword does.
3. With the remaster off in the mod selection, the dagger's reach is the
   original (8, 4). The same close swing passes over the Zozura, and so does
   every swing after it, until it reaches the hero and hurts him. The Long
   Sword's reach and hit are as with the feature on.

Screenshots, each what the window shows after the first frame of a swing's
first hit phase (its low frame): the taller dagger hitting the Zozura, the
Long Sword hitting it from further out, the taller dagger missing from there,
the taller dagger passing over it in a jump, and the original dagger passing
over it from close in.
"""
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "movement"))
from fax_e2e import fixture, parse_args, ram, tcp  # noqa: E402
from play import Game  # noqa: E402  (fax_e2e's, with headless runs)

AREA, SCREEN, HERO_X, HERO_Y = 0x24, 0x63, 0x9E, 0xA1               # Player_PosX_Block, Player_PosY
PLAYER_FLAGS, ATTACKING, FACING_RIGHT = 0xA4, 0x80, 0x40
ATTACK_PHASE = 0xAE                                                 # Player_AttackPhaseAnimFrame: 0, 1, 2
SPRITE_X, SPRITE_Y, SPRITE_ENTITY, SPRITE_HP = 0xBA, 0xC2, 0x2CC, 0x344  # CurrentSprites_*, 8 slots each
REACH_X, REACH_Y = 0xD2, 0xD3                                       # Maybe_WeaponRange_X/Y
SELECTED_WEAPON, PLAYER_CUR_WEAPON = 0x3BD, 0x3C8
HP = 0x431                                                          # Player_HP_U
MACHINE = 0x440                                                     # RAM read in one call: everything above
EOLIS, GATE, TOWN, ZOZURA_SCREEN = 0, 0, 1, 4
GATE_DOOR_X = 195                                                   # EOLIS_DOOR_LOCATIONS: screen 0, $9C
INTRO_TRIGGER, ZOZURA = 0x4F, 0x47                                  # SPRITE_TRIGGER_INTRO, SPRITE_ENEMY_ZOZURA
TARGET, OTHER = 7, 6                                                # the nearer Zozura's slot, and the other's
WEAPON_RANGES = 0xB897                                              # bank 14, MAYBE_WEAPON_RANGES
DAGGER, LONG_SWORD = 0, 1
NAMES = {DAGGER: "Hand Dagger", LONG_SWORD: "Long Sword"}
STOCK, TALL, SWORD = (8, 4), (8, 8), (16, 8)                        # (X, Y) reaches
REACH_IN_PHASE_1 = {DAGGER: 23, LONG_SWORD: 31}                     # the furthest a first hit phase reaches
HURTS = 15                                                          # a Zozura this close hurts the hero
# Swing when the Zozura is this close: it crawls about 3 pixels in the 8
# frames before the first hit phase.
CLOSE, SWORD_RANGE = 23, 31
JUMP_AT = 27                                                        # jump when it is this close, swing in the air
GROUND = 144                                                        # Player_PosY standing on screen 4
TOUGH = 60                                                          # a Zozura's HP that no hit here ends
FULL_HP = 0x50
NEAR = 40                                                           # each case starts a frame before this close
PRESS, JUMP_HOLD = 2, 20                                            # frames B and A are held, as a player taps them
STEP, FRAMES = 64, 256                                              # a case's run: past its last press, at most
CRAWL = 200                                                         # the Zozura's crawl to NEAR, with frames to spare
PACKAGE, FEATURE = "faxanadu.dx", "dx"                          # the one switch: off is the original game


def main():
    args = parse_args(__doc__)
    game = Game.from_args(args)
    ranges = game.image[16 + 14 * 0x4000 + WEAPON_RANGES - 0x8000:][:8]
    assert list(ranges) == [8, 4, 16, 8, 16, 8, 16, 8], f"MAYBE_WEAPON_RANGES: {ranges.hex()}"
    out = game.out

    def schedule(presses):
        """headless()'s schedule for presses (frame, button, frames): each button
        held from its frame for its frames, with any other held then."""
        edges = sorted({f for at, _, n in presses for f in (at, at + n)})
        return [(0, "-")] + [(f, "+".join(sorted(b for at, b, n in presses if at <= f < at + n)) or "-")
                             for f in edges]

    def follow(rams, presses, aim, jump, again):
        """The swing's rules on each frame's RAM, as one poll a frame would
        play them: what happened, and the next press they call for that
        `presses` does not have (to go down on the next frame), None once the
        case is over, or ... if the run ended first. A press already there is
        the same decision on the same frame: the runs are frame exact."""
        got = dict(hit=False, hurt=False, swings=0, reaches=set(), phase1=[], low=None)
        jumped = pressed = attacking = False
        free = 0                                                    # B can go down again from here
        for i, m in enumerate(rams):
            d = m[HERO_X] - m[SPRITE_X + TARGET]
            got["reaches"].add((m[REACH_X], m[REACH_Y]))
            was, attacking = attacking, bool(m[PLAYER_FLAGS] & ATTACKING)
            if attacking and not was:
                got["swings"] += 1
            if attacking and m[ATTACK_PHASE] == 1:
                got["phase1"].append((d, m[HERO_Y]))
                got["low"] = i if got["low"] is None else got["low"]
            if m[SPRITE_HP + TARGET] < TOUGH:
                got["hit"] = True
                return got, None
            if m[HP] < FULL_HP or m[SCREEN] != ZOZURA_SCREEN:
                got["hurt"] = True
                return got, None
            if was and not attacking and not again:
                return got, None                                    # the one swing is over
            press = None
            if jump and not jumped and d <= JUMP_AT:
                press, jumped = (i + 1, "A", JUMP_HOLD), True
            airborne = not jump or (jumped and m[HERO_Y] <= GROUND - 20)
            if (not press and not attacking and airborne and (again or not pressed) and d <= aim and
                    i + 1 >= free):
                press, pressed, free = (i + 1, "B", PRESS), True, i + 2 + PRESS
            if press and press not in presses:
                return got, press
        return got, ...                                             # the run ended first

    def swing(case, mods, aim, jump=False, again=False, name=None):
        """Play the save `case` headless with `mods`, swinging once the nearer
        Zozura is `aim` pixels away (with `jump`, jumping first and swinging in
        the air; with `again`, swinging again each time a swing ends, until
        something happens). Returns what happened: hit (its HP fell), hurt (it
        reached the hero), swings, the reaches the game loaded, (distance, hero
        y) on each frame of a first hit phase, and the first of them (low).
        `name` takes a picture after that frame, the swing's low frame, so hits
        and misses show the same moment."""
        presses, frames = [], STEP
        while True:
            got, press = follow(game.headless(case, schedule(presses), frames, mods), presses, aim, jump, again)
            if press is None:
                break
            if press is ...:                                        # nothing yet: play further
                assert frames < FRAMES, f"nothing happened in {frames} frames: {got}"
                frames = min(FRAMES, frames * 2)
                continue
            presses.append(press)
            frames = min(FRAMES, press[0] + STEP)
        if name:
            assert got["low"] is not None, f"no first hit phase to show: {got}"
            game.headless(case, schedule(presses), got["low"] + 1, mods, picture=out / f"{name}.png")
        return got

    def near(mods, name):
        """The Zozura save, played on with `mods` to the frame before the nearer
        Zozura is NEAR pixels away: a save state of it."""
        crawl = game.headless(zozura, [(0, "-")], CRAWL, mods)
        k = next(i for i, m in enumerate(crawl) if m[HERO_X] - m[SPRITE_X + TARGET] <= NEAR)
        path = out / f"{name}.cycstate"
        game.headless(zozura, [(0, "-")], k, mods, save=(k - 1, path))
        return path

    def case(near, weapon):
        """The save `near` with this weapon in hand, full HP and a Zozura no
        hit here kills."""
        path = out / f"case-{len(cases)}.cycstate"
        fixture(near, path, {PLAYER_CUR_WEAPON: weapon, SELECTED_WEAPON: weapon, HP: FULL_HP,
                             SPRITE_HP + TARGET: TOUGH})
        cases.append(path)
        return path

    def loaded(path):
        """Load the case in the window: the reach the game serves after a load,
        read in one call with what the case needs."""
        tcp("load_state", path=str(path))
        m = ram(0, MACHINE)
        assert m[SCREEN] == ZOZURA_SCREEN and m[SPRITE_ENTITY + TARGET] == ZOZURA
        assert not m[PLAYER_FLAGS] & FACING_RIGHT, "the hero faces the Zozura"
        return m[REACH_X], m[REACH_Y]

    def first_phase(got):
        ds = sorted({d for d, _ in got["phase1"]})
        assert ds, f"no sample in the first hit phase: {got}"
        return f"{ds[0]}-{ds[-1]}" if len(ds) > 1 else f"{ds[0]}"

    cases = []
    try:
        zozura = game.state("eolis-zozuras")
        on, off = game.mods("mods-on"), game.mods("mods-off", original=True)
        near_on, near_off = near(on, "near-on"), near(off, "near-off")    # headless: before a window waits
        # (case, mods, aim, jump, again, picture): 0-4 on, 5-7 off
        plays = [(case(near_on, DAGGER), on, CLOSE, False, False, "taller-dagger-hits"),
                 (case(near_on, LONG_SWORD), on, SWORD_RANGE, False, False, "long-sword-hits"),
                 (case(near_on, DAGGER), on, SWORD_RANGE, False, False, "taller-dagger-from-afar-misses"),
                 (case(near_on, DAGGER), on, REACH_IN_PHASE_1[DAGGER], True, False, "taller-dagger-jumps-over"),
                 (case(near_on, LONG_SWORD), on, REACH_IN_PHASE_1[LONG_SWORD], True, False, None),
                 (case(near_off, DAGGER), off, CLOSE, False, False, "stock-dagger-passes-over"),
                 (case(near_off, DAGGER), off, CLOSE + 12, False, True, None),
                 (case(near_off, LONG_SWORD), off, SWORD_RANGE, False, False, None)]

        # Each case's load in the window: the reach served after a save-state load.
        game.launch(at="eolis-zozuras")
        state = tcp("dagger_state")
        assert state["enabled"] and state["served"] and not state["failed"], f"on by default: {state}"
        assert ram(SCREEN) == ZOZURA_SCREEN and (ram(SPRITE_ENTITY + OTHER), ram(SPRITE_ENTITY + TARGET)) == (ZOZURA, ZOZURA)
        assert ram(SPRITE_X + TARGET) > ram(SPRITE_X + OTHER), "the target is the nearer Zozura"
        assert (ram(HERO_Y), ram(SPRITE_Y + TARGET)) == (GROUND, GROUND + 16), "both on the ground"
        reach = [loaded(play[0]) for play in plays[:5]]
        state = tcp("dagger_state")
        assert state["served"] and not state["failed"], f"still served after every load: {state}"
        game.close()
        game.set_features({(PACKAGE, FEATURE): False})
        game.launch()
        state = tcp("dagger_state")
        assert not state["enabled"] and not state["served"], f"the remaster is off: {state}"
        reach += [loaded(play[0]) for play in plays[5:]]
        game.close()

        # The swings, headless, every case at once.
        with ThreadPoolExecutor(max_workers=min(len(plays), os.cpu_count() or 4)) as pool:
            got = list(pool.map(lambda play: swing(*play), plays))
        for g, r in zip(got, reach):
            g["reaches"].add(r)

        # 1. On: the dagger hits from close in, and keeps its short reach.
        assert got[0]["reaches"] == {TALL}, f"the taller dagger's reach after a load: {got[0]}"
        assert got[0]["hit"] and got[0]["swings"] == 1, f"the taller dagger hits a Zozura: {got[0]}"
        print(f"ok: on, the dagger's reach is {TALL}, and one swing from {CLOSE} pixels hits the Zozura")
        assert got[1]["reaches"] == {SWORD} and got[1]["hit"], f"the Long Sword: {got[1]}"
        assert got[2]["reaches"] == {TALL} and not got[2]["hit"] and got[2]["swings"] == 1, f"dagger from afar: {got[2]}"
        assert all(d > REACH_IN_PHASE_1[DAGGER] for d, _ in got[2]["phase1"]), got[2]
        print(f"ok: swung from {SWORD_RANGE} pixels, the Long Sword {SWORD} hits and the dagger misses "
              f"(first hit phase at {first_phase(got[2])}): it keeps its short reach")

        # 2. Nothing under his feet: a swing at the top of a jump, right over it.
        for weapon, jumped in ((DAGGER, got[3]), (LONG_SWORD, got[4])):
            assert jumped["swings"] == 1 and not jumped["hit"], f"{NAMES[weapon]} in a jump: {jumped}"
            assert jumped["phase1"] and all(y <= GROUND - 20 and d <= REACH_IN_PHASE_1[weapon]
                                            for d, y in jumped["phase1"]), jumped
            print(f"ok: {NAMES[weapon]}: swung in a jump {GROUND - max(y for _, y in jumped['phase1'])} pixels up, "
                  f"in reach across (first hit phase at {first_phase(jumped)}), it passes over the Zozura")

        # 3. Off: the original dagger passes over it; the Long Sword is the same.
        assert got[5]["reaches"] == {STOCK}, f"the original dagger's reach: {got[5]}"
        assert not got[5]["hit"] and got[5]["swings"] == 1, f"the stock dagger passes over a Zozura: {got[5]}"
        assert got[5]["phase1"] and all(HURTS < d <= REACH_IN_PHASE_1[DAGGER] for d, _ in got[5]["phase1"]), \
            f"in reach across, as when it hit with the feature on ({got[0]['phase1']}): {got[5]}"
        print(f"ok: off, the dagger's reach is {STOCK}, and the same swing (first hit phase at "
              f"{first_phase(got[5])}, in reach across) passes over the Zozura")
        assert got[6]["hurt"] and not got[6]["hit"] and got[6]["swings"] >= 2, f"swinging until it arrives: {got[6]}"
        print(f"ok: off, {got[6]['swings']} swings pass over it until it reaches the hero and hurts him")
        assert got[7]["reaches"] == {SWORD} and got[7]["hit"], f"the Long Sword, off: {got[7]}"
        print(f"ok: the Long Sword's reach is {SWORD} on and off, and it hits from {SWORD_RANGE} pixels both "
              f"ways (first hit phase at {first_phase(got[1])} on, {first_phase(got[7])} off)")
        print("screenshots:", ", ".join(f"{play[5]}.png" for play in plays if play[5]))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
