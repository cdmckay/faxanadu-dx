"""Play Punches end to end, headless.

    python3 tests/punches/punches.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/punches

Starts from the fixture eolis-zozuras (an external prerequisite, not
coverage, tests/e2e/fax_e2e.py): a new game's hero, unarmed, on Eolis's
screen 4 as its two Zozuras crawl in. Each case is that save with RAM edits:
the hero at x 240 on the ground facing left, full HP, 10 MP and no
experience, the Glove and the Pendant as the case has them, the other Zozura
sent away, and the nearer 40 pixels off, crawling in.

The hero fights it until it dies, headless and frame exact: a punch's first
hit phase lasts 3 frames, which polls over TCP can step over. Each frame's
RAM is read after it and decides the next frame's buttons, as one poll a
frame would: B when he is not swinging and the enemy is 0-23 pixels in front
of him (his x minus its x), so it is in the fist's reach when the first hit
phase comes; Left while it is further off, since a hit turns a Zozura round
and it crawls away; nothing while he swings. The fight plays 32 frames a run,
each run from a state the run before saved, and a change of buttons plays on
from the last state before it; then the whole fight plays again in one run
from the case, which must play it the same. Every case at once.

1. Bare-handed (the remaster on, the default): each punch that lands takes 2
   HP, so a Zozura (9 HP, SPRITE_ENTITIES_HP) dies to the fifth. On that
   frame its 30 experience comes in, and with it 3 MP (MP from kills). In
   the first hit phase the fist is where the Hand Dagger's pose puts it (8
   pixels left of him, 8 down), and its reach is the taller dagger's (8, 8):
   a first hit phase lands exactly when the enemy is 0-23 pixels off.
2. With the Glove running a punch takes 3, and the Zozura dies to the
   fourth. With the Pendant a punch still takes 2 (a quarter of 2 is 0), and
   with both, 3.
3. A Monodron: the Zozura's slot made one (its entity, so its 10 HP, box,
   experience and MP; it keeps the Zozura's crawl). Bare-handed it dies to
   the sixth punch (10 to 0 leaves it alive, as any hit does), for 20
   experience and 1 MP; with the Glove, to the fourth.
4. The original (the remaster off): the same punch, the Zozura in its reach
   in the first hit phase, takes nothing. No hit counter, no experience, the
   fist's box never set, until the Zozura reaches the hero and hurts him.

A picture of the first punch landing, the Zozura under its damage number, and
one of the original's punch with it in reach, go in OUT.
"""
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import shutil
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "movement"))
from fax_e2e import chunks, fixture, parse_args  # noqa: E402
import play  # noqa: E402  (fax_e2e.Game with headless runs)

HERO_X, HERO_Y, PLAYER_FLAGS, STATUS, IFRAMES, ATTACK_PHASE = 0x9E, 0xA1, 0xA4, 0xA5, 0xAD, 0xAE
ATTACKING = 0x80
WEAPON_X, WEAPON_Y, REACH_X, REACH_Y = 0xCE, 0xD0, 0xD2, 0xD3     # Player_DrawWeapon's box ($CF: X's high byte)
SPRITE_X, SPRITE_Y, ENTITIES, SPRITE_FLAGS, SPRITE_HP, HIT_COUNTER = 0xBA, 0xC2, 0x2CC, 0x2DC, 0x344, 0x34C
EXPERIENCE, MP, SELECTED_WEAPON, PLAYER_CUR_WEAPON = 0x390, 0x39A, 0x3BD, 0x3C8
DURATION_OINTMENT, DURATION_GLOVE, SPECIAL_ITEMS, HP = 0x427, 0x428, 0x42C, 0x431
NO_WEAPON, BIT_PENDANT, GLOVE_RUNNING, NOT_RUNNING = 0xFF, 0x02, 0x40, 0xFF
ZOZURA, MONODRON = 0x47, 0x2A
NEAR, FAR = 7, 6                                  # the Zozuras' slots: the nearer, the one behind it
HERO_AT, GROUND, START_OFF = 240, 144, 40         # where he stands; the enemy starts this far off
AIM = 23                                          # punch when it is this close: the fist reaches it from 23
DAGGER_POSE_5 = (-8, 8)                           # PTR_ARRAY_PRG14__b89f/b90f: the dagger's x and y in pose 5, facing left
TALL = (8, 8)                                     # the taller dagger's reach
FULL = 80
SPRITE_ENTITIES_HP, ENEMY_EXPERIENCE = 0xB5A9, 0xB60E   # bank 14, by entity
STRENGTH, GLOVE = 2, 1                            # the fist's, fax_punch.h
PRESS, CHUNK, SAVE_EVERY, FRAMES = 2, 32, 4, 1200  # B held; frames a run plays, and saves a state every; a fight at most


def word(r, addr):
    return r[addr] | r[addr + 1] << 8


def buttons_at(inputs, frame):
    """The buttons `inputs` (frame, buttons) holds on `frame`: the last change at or before it."""
    held = "-"
    for at, buttons in inputs:
        if at > frame:
            break
        held = buttons
    return held


def follow(rams, inputs, original):
    """The fight's rules on each frame's RAM, as one poll a frame would play
    them, deciding the buttons of the next frame: B for PRESS frames when the
    hero is not swinging and the enemy is 0-AIM pixels in front of him, Left
    while it is further off (a hit turns a Zozura round, so it crawls away),
    nothing while he swings. Returns what happened, and the first change of
    buttons they call for that `inputs` does not have, None once the fight is
    over, or ... if the run ended first. The runs are frame exact, so a run
    with the change plays the same up to it."""
    got = dict(hits=[], swings=0, killed=None, hurt=None, phase1=[], boxes=set(), counters=False)
    entity = rams[0][ENTITIES + NEAR]
    attacking, held_b = False, 0
    for i, m in enumerate(rams):
        before = rams[i - 1] if i else m
        d = m[HERO_X] - m[SPRITE_X + NEAR]
        alive = got["killed"] is None and m[ENTITIES + NEAR] == entity
        was, attacking = attacking, bool(m[PLAYER_FLAGS] & ATTACKING)
        if attacking and not was:
            got["swings"] += 1
        if attacking and m[ATTACK_PHASE] == 1 and got["killed"] is None and before[ENTITIES + NEAR] == entity \
                and not before[HIT_COUNTER + NEAR]:                 # a hit counter skips the weapon's check
            got["phase1"].append((i, d, m[SPRITE_HP + NEAR] != before[SPRITE_HP + NEAR]))
            got["boxes"].add((m[WEAPON_X] - m[HERO_X], m[WEAPON_Y] - m[HERO_Y], m[REACH_X], m[REACH_Y]))
        if m[HIT_COUNTER + NEAR] and alive:
            got["counters"] = True
        if got["killed"] is None and before[ENTITIES + NEAR] == entity and \
                m[SPRITE_HP + NEAR] != before[SPRITE_HP + NEAR]:           # the kill can change the entity at once
            hp0, hp1 = before[SPRITE_HP + NEAR], m[SPRITE_HP + NEAR]
            got["hits"].append((i, hp0, hp1))
            if hp1 > hp0:                                           # a borrow: the kill
                got["killed"] = i
        if got["killed"] is not None and i >= got["killed"] + 4:    # its experience, MP, death effect
            return got, None
        if m[HP] < FULL and got["hurt"] is None:
            got["hurt"] = i
            if original:
                return got, None
        if held_b:
            held_b -= 1
            want = "B"
        elif alive and not attacking and 0 <= d <= AIM:
            held_b, want = PRESS - 1, "B"
        elif alive and not attacking and d > AIM:
            want = "LEFT"
        else:
            want = "-"
        if want != buttons_at(inputs, i + 1):
            return got, (i + 1, want)
    return got, ...


def main():
    args = parse_args(__doc__)
    game = play.Game.from_args(args)
    image = game.image
    out = game.out

    def bank14(addr):
        return image[16 + 14 * 0x4000 + addr - 0x8000]

    try:
        zozuras = game.state("eolis-zozuras")
        data = Path(zozuras).read_bytes()
        hw = next(p for t, p, n in chunks(data) if t == b"HW  ") + 23
        assert (data[hw + PLAYER_CUR_WEAPON], data[hw + SELECTED_WEAPON]) == (NO_WEAPON, NO_WEAPON), \
            "a new game's hero has no weapon"
        assert data[hw + ENTITIES + NEAR] == ZOZURA

        def case(name, entity=ZOZURA, pendant=False, gloved=False):
            path = out / f"{name}.cycstate"
            fixture(zozuras, path, {
                HERO_X: HERO_AT, HERO_Y: GROUND, PLAYER_FLAGS: 0, STATUS: 0, IFRAMES: 0, HP: FULL, MP: 10,
                EXPERIENCE: 0, EXPERIENCE + 1: 0, DURATION_OINTMENT: NOT_RUNNING,
                DURATION_GLOVE: GLOVE_RUNNING if gloved else NOT_RUNNING,
                SPECIAL_ITEMS: BIT_PENDANT if pendant else 0, ENTITIES + FAR: 0xFF, ENTITIES + NEAR: entity,
                SPRITE_HP + NEAR: bank14(SPRITE_ENTITIES_HP + entity), HIT_COUNTER + NEAR: 0,
                SPRITE_X + NEAR: HERO_AT - START_OFF, SPRITE_Y + NEAR: GROUND + 16,
                SPRITE_FLAGS + NEAR: data[hw + SPRITE_FLAGS + NEAR] | 0x01})   # crawling toward him
            return path

        def fight(path, mods, original=False, picture=None):
            """Play the case `path` with `mods` by follow()'s rules, a CHUNK of
            frames a run, each run from a state of the one before: the next
            CHUNK from its end, or, at a change of buttons, from the last state
            saved before it (one every SAVE_EVERY frames). Then the whole fight
            again from the case, in one run, to show the runs joined up as one
            play would have played. Returns what follow() saw, and that run's
            RAM."""
            work = Path(tempfile.mkdtemp(prefix=f"{path.stem}-", dir=out))
            inputs, history = [(0, "-")], []
            states = {-1: path}                                     # the state after each frame; -1 the case
            at = 0                                                  # the frame the next run starts on
            while True:
                assert at < FRAMES, f"{path.stem}: the fight did not end in {FRAMES} frames"
                saves = [(k, work / f"{at + k}.cycstate") for k in range(SAVE_EVERY - 1, CHUNK, SAVE_EVERY)]
                run = [(0, buttons_at(inputs, at))] + [(f - at, b) for f, b in inputs if f > at]
                history[at:] = game.headless(states[at - 1], run, CHUNK, mods, saves=saves)
                states.update({at + k: p for k, p in saves})
                got, change = follow(history, inputs, original)
                if change is None:
                    break
                if change is ...:
                    at += CHUNK
                    continue
                inputs = [e for e in inputs if e[0] < change[0]] + [change]
                at = max(f for f in states if f < change[0]) + 1
                states = {f: p for f, p in states.items() if f < at}   # the rest played the old buttons
                history = history[:at]
            end = len(history)
            rams = game.headless(path, inputs, end, mods)
            again, change = follow(rams, inputs, original)
            assert change is None and again == got, f"{path.stem}: one run plays as the joined runs did"
            shutil.rmtree(work)
            if picture:
                # After the frame a punch lands, the enemy under its damage number (a hit enemy blinks); in the
                # original, after the first frame of a first hit phase with it in reach.
                at = got["hits"][0][0] if got["hits"] else next(i for i, d, _ in got["phase1"] if 0 <= d <= AIM)
                game.headless(path, inputs, at + 1, mods, picture=out / picture)
                game.shots.append(picture)
            return got, rams

        on, off = game.mods("on"), game.mods("off", original=True)
        kits = {"bare": {}, "glove": dict(gloved=True), "pendant": dict(pendant=True),
                "pendant-glove": dict(pendant=True, gloved=True)}
        plays = [(case(f"zozura-{k}", **kit), on, False, "first-punch.png" if k == "bare" else None)
                 for k, kit in kits.items()]
        plays += [(case("monodron-bare", MONODRON), on, False, None),
                  (case("monodron-glove", MONODRON, gloved=True), on, False, None),
                  (case("original", pendant=False), off, True, "original-punch.png")]
        with ThreadPoolExecutor(max_workers=min(len(plays), os.cpu_count() or 4)) as pool:
            results = list(pool.map(lambda p: fight(*p), plays))
        names = list(kits) + ["monodron-bare", "monodron-glove", "original"]
        fights = dict(zip(names, results))

        def check_kill(name, entity, per_hit):
            got, rams = fights[name]
            hp = bank14(SPRITE_ENTITIES_HP + entity)
            xp = bank14(ENEMY_EXPERIENCE + entity)
            mp = max(1, xp * xp // 256)                               # fax_mana.c: experience² / 256, at least 1
            want = []
            while True:                                               # damage at least its HP kills; equal leaves 0
                want.append((hp, (hp - per_hit) & 0xFF))
                if hp < per_hit:
                    break
                hp -= per_hit
            assert [(a, b) for _, a, b in got["hits"]] == want, f"{name}: hits {got['hits']}, expected {want}"
            assert got["killed"] is not None and got["hurt"] is None, f"{name}: {got}"
            kill = got["killed"]
            if per_hit == STRENGTH + GLOVE:
                assert all(r[DURATION_GLOVE] < 0x80 for r in rams[:kill + 1]), f"{name}: the Glove ran out"
            gained = [(i, word(rams[i], EXPERIENCE)) for i in range(1, len(rams))
                      if word(rams[i], EXPERIENCE) != word(rams[i - 1], EXPERIENCE)]
            mps = [(i, rams[i][MP]) for i in range(1, len(rams)) if rams[i][MP] != rams[i - 1][MP]]
            assert gained == [(kill, xp)], f"{name}: experience {gained}, expected {xp} on frame {kill}"
            assert mps == [(kill, 10 + mp)], f"{name}: MP {mps}, expected {10 + mp} on frame {kill}"
            assert rams[-1][ENTITIES + NEAR] != entity, f"{name}: its death effect"
            wrong = [(i, d, hit) for i, d, hit in got["phase1"] if hit != (0 <= d <= AIM)]
            assert not wrong, f"{name}: a first hit phase lands exactly when it is 0-{AIM} pixels off: {wrong}"
            return len(want), got["swings"], xp, mp

        # 1. Bare-handed.
        got, _ = fights["bare"]
        n, swings, xp, mp = check_kill("bare", ZOZURA, STRENGTH)
        assert n == 5, n
        assert got["boxes"] == {(*DAGGER_POSE_5, *TALL)}, f"the fist's box in its first hit phase: {got['boxes']}"
        print(f"ok: bare-handed, each punch takes {STRENGTH} HP and the Zozura dies to the {n}th "
              f"({swings} swings), for its {xp} experience and {mp} MP on the frame it dies; the fist is "
              f"the dagger's {DAGGER_POSE_5} from him with the taller dagger's reach {TALL}, and lands in "
              f"every first hit phase with the Zozura 0-{AIM} pixels off, and none further")

        # 2. The Glove and the Pendant.
        for name, per_hit, punches in (("glove", STRENGTH + GLOVE, 4), ("pendant", STRENGTH, 5),
                                       ("pendant-glove", STRENGTH + GLOVE, 4)):
            n, swings, _, _ = check_kill(name, ZOZURA, per_hit)
            assert n == punches, (name, n)
            print(f"ok: {name.replace('-', ' and ')}: {per_hit} a punch, the Zozura dies to the {n}th ({swings} swings)")

        # 3. A Monodron.
        for name, per_hit, punches in (("monodron-bare", STRENGTH, 6), ("monodron-glove", STRENGTH + GLOVE, 4)):
            n, swings, xp, mp = check_kill(name, MONODRON, per_hit)
            assert n == punches, (name, n)
            print(f"ok: {name}: {per_hit} a punch, the Monodron ({bank14(SPRITE_ENTITIES_HP + MONODRON)} HP) dies "
                  f"to the {n}th ({swings} swings), for {xp} experience and {mp} MP")

        # 4. The original.
        got, rams = fights["original"]
        assert got["hurt"] is not None and not got["hits"] and not got["counters"], f"the original: {got}"
        near = [d for _, d, _ in got["phase1"] if 0 <= d <= AIM]
        assert got["swings"] and near and not any(hit for _, _, hit in got["phase1"]), got
        assert all(r[WEAPON_X:REACH_Y + 1] == rams[0][WEAPON_X:REACH_Y + 1] for r in rams), "no box set"
        assert all(word(r, EXPERIENCE) == 0 for r in rams)
        print(f"ok: the original: {got['swings']} punches, {len(near)} frames of first hit phases with the "
              f"Zozura in the fist's reach ({min(near)}-{max(near)} pixels off), take nothing, until it hurts him")
        print("screenshots:", ", ".join(game.shots))
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
