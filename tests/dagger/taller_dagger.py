"""Play the taller dagger end to end in a hidden window.

    python3 tests/dagger/taller_dagger.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/dagger

The game runs uncapped and the script waits on the game's own state, never on
the clock, and takes a free TCP port, so any number can run in parallel.

Copies the build to OUT and starts a new game over TCP. To skip the walk-in
(an external prerequisite, not coverage), a save-state RAM fixture drops the
intro's trigger and stands the hero at Eolis's gate. He goes in, walks left
out of town to the first enemies, two Zozura crawling right along the ground
on Eolis screen 4, and the game is saved there, the hero facing them. Each
case loads that save with a weapon in hand, full HP and a Zozura that no hit
here kills, and swings once as it crawls in, when it is a set distance away
(his x minus its x). Every case's load also checks that the dagger's reach is
served again after a save-state load.

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

Screenshots, each on the first frame of a swing's first hit phase (its low
frame): the taller dagger hitting the Zozura, the Long Sword hitting it from
further out, the taller dagger missing from there, the taller dagger passing
over it in a jump, and the original dagger passing over it from close in.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
from fax_e2e import Game, fixture, frame, parse_args, ram, tcp, until, wait  # noqa: E402

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
PACKAGE, FEATURE = "faxanadu.remastered", "remastered"          # the one switch: off is the original game


def main():
    args = parse_args(__doc__)
    game = Game.from_args(args)
    ranges = game.image[16 + 14 * 0x4000 + WEAPON_RANGES - 0x8000:][:8]
    assert list(ranges) == [8, 4, 16, 8, 16, 8, 16, 8], f"MAYBE_WEAPON_RANGES: {ranges.hex()}"
    out = game.out

    def swing(zozura, weapon, aim, jump=False, again=False, name=None):
        """Load the Zozura save with this weapon, and swing once the nearer
        Zozura is `aim` pixels away (with `jump`, jump first and swing in the
        air). With `again`, keep swinging until something happens. Returns
        what happened: hit (its HP fell), hurt (it reached the hero), swings,
        the reaches the game loaded, and (distance, hero y) in each sample of
        a first hit phase. `name` takes a screenshot in the first one, the
        swing's low frame, so hits and misses show the same moment."""
        case = out / "case.cycstate"
        fixture(zozura, case, {PLAYER_CUR_WEAPON: weapon, SELECTED_WEAPON: weapon, HP: FULL_HP,
                               SPRITE_HP + TARGET: TOUGH})
        tcp("load_state", path=str(case))
        m = ram(0, MACHINE)
        assert m[SCREEN] == ZOZURA_SCREEN and m[SPRITE_ENTITY + TARGET] == ZOZURA
        assert not m[PLAYER_FLAGS] & FACING_RIGHT, "the hero faces the Zozura"
        got = dict(hit=False, hurt=False, swings=0, reaches=set(), phase1=[])
        jumped = pressed = attacking = False
        for _ in range(900):                                        # one read a sample, a frame or two apart
            m = ram(0, MACHINE)
            d = m[HERO_X] - m[SPRITE_X + TARGET]
            got["reaches"].add((m[REACH_X], m[REACH_Y]))
            was, attacking = attacking, bool(m[PLAYER_FLAGS] & ATTACKING)
            if attacking and not was:
                got["swings"] += 1
            if attacking and m[ATTACK_PHASE] == 1:
                got["phase1"].append((d, m[HERO_Y]))
                if name:
                    game.shot(name)
                    name = None
            if m[SPRITE_HP + TARGET] < TOUGH:
                got["hit"] = True
                return got
            if m[HP] < FULL_HP or m[SCREEN] != ZOZURA_SCREEN:
                got["hurt"] = True
                return got
            if was and not attacking and not again:
                return got                                          # the one swing is over
            if jump and not jumped and d <= JUMP_AT:
                tcp("action", name="a", frames=20)
                jumped = True
            airborne = not jump or (jumped and m[HERO_Y] <= GROUND - 20)
            if not attacking and airborne and (again or not pressed) and d <= aim:
                tcp("action", name="b", frames=2)
                pressed = True
        raise AssertionError(f"{NAMES[weapon]}: nothing happened: {got}")

    def first_phase(got):
        ds = sorted({d for d, _ in got["phase1"]})
        assert ds, f"no sample in the first hit phase: {got}"
        return f"{ds[0]}-{ds[-1]}" if len(ds) > 1 else f"{ds[0]}"

    try:
        game.launch()
        until("the title", lambda: frame() > 700)
        tcp("pad", buttons="start", frames=10)
        until("the Eolis gate", lambda: frame() > 1900)
        state = tcp("dagger_state")
        assert state["enabled"] and state["served"] and not state["failed"], f"on by default: {state}"
        assert (ram(AREA), ram(SCREEN), ram(SPRITE_ENTITY + 7)) == (EOLIS, GATE, INTRO_TRIGGER)
        gate = out / "gate.cycstate"
        tcp("save_state", path=str(gate))
        door = out / "door.cycstate"
        fixture(gate, door, {SPRITE_ENTITY + 7: 0xFF, HERO_X: GATE_DOOR_X})
        tcp("load_state", path=str(door))
        wait(10)
        tcp("action", name="up", frames=6)
        until("the town", lambda: ram(SCREEN) == TOWN, 600)
        wait(30)
        for _ in range(60):
            if ram(SCREEN) == ZOZURA_SCREEN:
                break
            tcp("action", name="left", frames=30)
            wait(31)
        until("the Zozura", lambda: ram(SCREEN) == ZOZURA_SCREEN and
              (ram(SPRITE_ENTITY + OTHER), ram(SPRITE_ENTITY + TARGET)) == (ZOZURA, ZOZURA), 300)
        assert ram(SPRITE_X + TARGET) > ram(SPRITE_X + OTHER), "the target is the nearer Zozura"
        assert (ram(HERO_Y), ram(SPRITE_Y + TARGET)) == (GROUND, GROUND + 16), "both on the ground"
        zozura = out / "zozura.cycstate"
        tcp("save_state", path=str(zozura))

        # 1. On: the dagger hits from close in, and keeps its short reach.
        got = swing(zozura, DAGGER, CLOSE, name="taller-dagger-hits")
        assert got["reaches"] == {TALL}, f"the taller dagger's reach after a load: {got}"
        assert got["hit"] and got["swings"] == 1, f"the taller dagger hits a Zozura: {got}"
        print(f"ok: on, the dagger's reach is {TALL}, and one swing from {CLOSE} pixels hits the Zozura")
        tall_phase = got["phase1"]
        got = swing(zozura, LONG_SWORD, SWORD_RANGE, name="long-sword-hits")
        assert got["reaches"] == {SWORD} and got["hit"], f"the Long Sword: {got}"
        on_sword = got
        got = swing(zozura, DAGGER, SWORD_RANGE, name="taller-dagger-from-afar-misses")
        assert got["reaches"] == {TALL} and not got["hit"] and got["swings"] == 1, f"dagger from afar: {got}"
        far = first_phase(got)
        assert all(d > REACH_IN_PHASE_1[DAGGER] for d, _ in got["phase1"]), got
        print(f"ok: swung from {SWORD_RANGE} pixels, the Long Sword {SWORD} hits and the dagger misses "
              f"(first hit phase at {far}): it keeps its short reach")

        # 2. Nothing under his feet: a swing at the top of a jump, right over it.
        for weapon in (DAGGER, LONG_SWORD):
            got = swing(zozura, weapon, REACH_IN_PHASE_1[weapon], jump=True,
                        name="taller-dagger-jumps-over" if weapon == DAGGER else None)
            assert got["swings"] == 1 and not got["hit"], f"{NAMES[weapon]} in a jump: {got}"
            assert all(y <= GROUND - 20 and d <= REACH_IN_PHASE_1[weapon] for d, y in got["phase1"]), got
            print(f"ok: {NAMES[weapon]}: swung in a jump {GROUND - max(y for _, y in got['phase1'])} pixels up, "
                  f"in reach across (first hit phase at {first_phase(got)}), it passes over the Zozura")
        state = tcp("dagger_state")
        assert state["served"] and not state["failed"], f"still served after every load: {state}"
        game.close()

        # 3. Off: the original dagger passes over it; the Long Sword is the same.
        game.set_features({(PACKAGE, FEATURE): False})
        game.launch()
        state = tcp("dagger_state")
        assert not state["enabled"] and not state["served"], f"the remaster is off: {state}"
        got = swing(zozura, DAGGER, CLOSE, name="stock-dagger-passes-over")
        assert got["reaches"] == {STOCK}, f"the original dagger's reach: {got}"
        assert not got["hit"] and got["swings"] == 1, f"the stock dagger passes over a Zozura: {got}"
        assert all(HURTS < d <= REACH_IN_PHASE_1[DAGGER] for d, _ in got["phase1"]), \
            f"in reach across, as when it hit with the feature on ({tall_phase}): {got}"
        print(f"ok: off, the dagger's reach is {STOCK}, and the same swing (first hit phase at "
              f"{first_phase(got)}, in reach across) passes over the Zozura")
        got = swing(zozura, DAGGER, CLOSE + 12, again=True)
        assert got["hurt"] and not got["hit"] and got["swings"] >= 2, f"swinging until it arrives: {got}"
        print(f"ok: off, {got['swings']} swings pass over it until it reaches the hero and hurts him")
        got = swing(zozura, LONG_SWORD, SWORD_RANGE)
        assert got["reaches"] == {SWORD} and got["hit"], f"the Long Sword, off: {got}"
        print(f"ok: the Long Sword's reach is {SWORD} on and off, and it hits from {SWORD_RANGE} pixels both "
              f"ways (first hit phase at {first_phase(on_sword)} on, {first_phase(got)} off)")
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
