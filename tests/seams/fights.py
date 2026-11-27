"""Play Fights across a seam (#166's slice 2) end to end in a hidden window.

    python3 tests/seams/fights.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/fights

The game runs uncapped and the script waits on the game's own frames, never
on the clock, on a reserved TCP port.

The parts start from fixtures (external prerequisites, not coverage,
tests/e2e/fax_e2e.py): eolis-zozuras (Eolis screen 4 as its Zozuras crawl in,
screen 5 left of it) and seams-mist-warning (the Mist's screen 30, $1E, at the
first frame of its Sugata's warning; $1F joined at its right). Each edits its
save: the live game's RAM, and rooms in Seamless's record (faxanadu.world,
tests/seams/open_ground.py's Record: each room's RAM and Seams' block, its
first byte a wake bit per slot, then a mark per slot for one let through a
side), filled as Sprites_PopulateNextAvailableSprite fills a slot. In every
part the hero stays in his room while the actor he fights is in the next
one's: the live game meets it as a visitor in its sprite loop (seams_state's
visit_log: room, slot, live slot, entity, what it was after).

1. kill: the Long Sword swung from room 4's x 4 facing left, a room 5 Zozura
   at its x 236 with 7 HP (one blow: 8, with the Pendant fixed). It dies in room 5 (the death effect, $13, in room 5's
   record) while he stands in room 4: its 30 experience and its 3 MP (MP
   from kills) are his on the swing, and room 5's tick plays its death and
   leaves its drop there: bread, a Zozura's (SPRITE_ENTITY_DROP_VALUES_INDEX
   $32, Sprite_ReplaceWithBreadDrop, which plays no sound).
2. coin-drop: the same at a Monodron ($2A, at 7 HP), which drops a
   coin: the coin lies in room 5, and its sound ($10, Sprites_ReplaceWithCoinDrop
   in room 5's tick) is played in the live game (seams_state's sound_log).
3. coin: a coin worth 20 Golds at room 5's x 250, straddling the seam; he
   walks left from room 4's x 12 and takes it while still in room 4: his gold
   up by 40 (Double gold), the coin gone from room 5's record.
4. touch: a Zozura of room 5 at its x 248, through its right side, awake,
   the hero at room 4's x 2 with no Ointment: it hurts him (HP down, his
   iframes on) while it is still in room 5.
5. punch: no weapon, the same swing as kill: the fist's 2 damage, the
   Zozura at 7 HP and knocked back in room 5.
6. flash: the Mist's Sugata, at its warning's first frame, moved into $1F's
   record at its x 4 (its middle the live room's 268), the hero on $1E at x
   228 (in its reach of 40) facing it: it warns in $1F's tick, flashes there
   (its band in $1F), and the live game takes the damage (seams_state's
   flash_runs): 10 HP, Sugata never in $1E. With the hero at x 150, out of
   reach, the flash ends in $1F's tick and takes nothing.
7. save-load: the kill, saved by the game itself mid-swing: two fresh games,
   headless, run 60 frames from that save to the same machine and the same
   records.
8. lag: the fortress of Zenis's screen 16 (block-hood-cast, the Execution
   Hood's shot at the hero's head: tests/block/block.py), whose six actors and
   a Maskman across the seam make the main loop overrun some frames, so a
   frame ends inside the visitors' pass. That frame is found headless, frame
   exact: a state saved after each frame, and the first whose Seams record
   has the pass open (polls over TCP could step over every such frame on a
   loaded machine, #189). Read then (that save loaded, read in the same
   batch), the live room's slots are its own (no actor hidden), as
   fax_world_room shows them; saved then, two fresh games run 60 frames from
   it to the same machine and records.

Screenshots: the Zozura's death in room 5 with the hero in room 4, the coin
taken across, the touch, and Sugata's band across the seam (taken by the game
on the frame it shows, TCP sugata_capture).
"""
from pathlib import Path
import shutil
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "block"))
from harness import Game  # noqa: E402  (fax_e2e's Game, with warp() and the rest)
import fax_e2e  # noqa: E402
from fax_e2e import batch, fixture, frame, parse_args, ram, tcp, until, wait  # noqa: E402
from seams_lose_nothing import hw_ram, spawn  # noqa: E402
from open_ground import Record  # noqa: E402
import block  # noqa: E402,F401  (its fixture, block-hood-cast)

AREA, SCREEN, HERO_X, HERO_Y, PLAYER_FLAGS = 0x24, 0x63, 0x9E, 0xA1, 0xA4
INVINCIBILITY = 0xAD
SPRITE_X, SPRITE_Y = 0xBA, 0xC2
ENTITIES, FLAGS, HP_, HIT_COUNTER = 0x2CC, 0x2DC, 0x344, 0x34C
SLOT_ARRAYS = [SPRITE_X, SPRITE_Y] + list(range(0x2BC, 0x374, 8))
EXPERIENCE, GOLD, MP = 0x390, 0x392, 0x39A
SELECTED_WEAPON, PLAYER_CUR_WEAPON, OINTMENT, HP = 0x3BD, 0x3C8, 0x427, 0x431
ZOZURA, MONODRON, SUGATA, COIN, BREAD, DEATH, EMPTY = 0x47, 0x2A, 0x18, 0x02, 0x01, 0x13, 0xFF
LONG_SWORD, NO_WEAPON = 1, 0xFF
ZOZURA_XP, ZOZURA_MP = 30, 3                                     # ENEMY_EXPERIENCE; fax_mana.c's experience² / 256
COIN_SOUND = 0x10
THROUGH_RIGHT = 2
MIST, MIST_30, MIST_31, PALETTE_MIST, REGION_MIST = 2, 0x1E, 0x1F, 0x0A, 2
REACH, FLASH_DAMAGE, WARNING = 40, 10, 30
ONE_BLOW = 7                                                     # HP the Long Sword's 8 (the Pendant fixed) kills
PARTS = ("kill", "coin-drop", "coin", "touch", "punch", "flash", "save-load", "lag")
HOOD_SHOT, HEAD_HEIGHT, ZENIS_16 = 0x51, 136, 16
LAG_FRAMES, LAG_CHUNK = 400, 50                                  # lag: frames searched headless, a run's saves


@fax_e2e.recipe("seams-mist-warning", parent="eolis-meat-shop")
def _mist_warning(g):
    """Out of Eolis's meat shop onto the Mist's screen 30 ($1E), its door
    edited to lead there (as tests/sugata/sugata_flash.py walks out of one),
    to the first frame of its Sugata's warning."""
    g.warp(g.save("shop"), "mist-30", MIST, MIST_30, PALETTE_MIST, 0x92, REGION_MIST)
    g.until("Sugata's warning", lambda: g.tcp("sugata_state")["warnings"] > 0, 900)


def seams():
    return tcp("seams_state")


def room(n):
    return tcp("fax_world_room", room=n)


def experience():
    e = ram(EXPERIENCE, 2)
    return e[0] | e[1] << 8


def gold():
    g = ram(GOLD, 3)
    return g[0] | g[1] << 8 | g[2] << 16


def visited(log, room_, entity, after=None):
    """The visits of `entity` from `room_` in a visit log, by what it was after."""
    return [v for v in log if v[0] == room_ and v[3] == entity and (after is None or v[4] == after)]


def main():
    args = parse_args(__doc__, parts=PARTS)
    game = Game.from_args(args)
    image, out = game.image, game.out

    def edited(case, source, writes, edits):
        path = out / f"{case}.cycstate"
        fixture(source, path, writes, {"faxanadu.world": edits})
        return path

    def start(case, at, writes, edits):
        path = edited(case, game.state(at), writes, edits)
        if not game.running():
            game.launch(at=at)
        tcp("load_state", path=str(path))
        return path

    try:
        zozuras = game.state("eolis-zozuras")
        data = zozuras.read_bytes()
        live, record = hw_ram(data), Record(data)
        assert live[SCREEN] == 4 and list(live[ENTITIES + 6:ENTITIES + 8]) == [ZOZURA] * 2
        room5 = record.ram(5)
        r5 = [s for s in range(8) if room5[ENTITIES + s] == ZOZURA]
        floor_y = room5[SPRITE_Y + r5[0]]
        no_live_zozuras = {ENTITIES + 6: EMPTY, ENTITIES + 7: EMPTY}
        empty5 = {record.base(5) + ENTITIES + s: EMPTY for s in range(8)}

        def one(entity, x, y=None, hp=None, through=0, facing_right=True):
            """Room 5 emptied but `entity` in slot 7 at (x, y), awake."""
            edits = dict(empty5)
            spawn(image, edits, record.base(5), 7, entity, x, floor_y if y is None else y)
            edits[record.base(5) + FLAGS + 7] = 1 if facing_right else 0
            if hp is not None:
                edits[record.base(5) + HP_ + 7] = hp
            edits[record.block(5)] = 0x80                           # awake: slot 7
            edits[record.block(5) + 2 + 7] = through
            return edits

        def swing(case, edits, weapon=LONG_SWORD, frames=60, done=lambda r5_: r5_["entities"][7] != ZOZURA):
            """From room 4's x 4 facing left, `weapon` in hand, B pressed with
            the load (one batch, so it lands on the state's first frames).
            Reads room 5, the experience, MP and the screen each frame until
            done(room 5) or `frames` pass. Returns the reads."""
            writes = {HERO_X: 4, PLAYER_FLAGS: 0, PLAYER_CUR_WEAPON: weapon, SELECTED_WEAPON: weapon, HP: 0x50,
                      MP: 10, OINTMENT: 0x7F, **no_live_zozuras}
            path = edited(case, zozuras, writes, edits)
            if not game.running():
                game.launch(at="eolis-zozuras")
            for _ in range(3):                                     # a press in a loaded state's first frames can go unseen
                batch(("load_state", dict(path=str(path))), ("action", dict(name="b", frames=2)))
                reads, end = [], frame() + frames
                while frame() < end:
                    r5_, xp, mp, sc, s = batch(("fax_world_room", dict(room=5)), ("read_ram", dict(addr=EXPERIENCE, len=2)),
                                               ("read_ram", dict(addr=MP, len=1)), ("read_ram", dict(addr=SCREEN, len=1)),
                                               ("seams_state", {}))
                    xp = bytes.fromhex(xp["hex"])
                    reads.append((r5_, xp[0] | xp[1] << 8, int(mp["hex"], 16), int(sc["hex"], 16), s))
                    if done(r5_):
                        return reads
                if any(r[4]["visits"] for r in reads):
                    return reads
            raise AssertionError(f"{case}: no hit across the seam: {[r[0]['entities'][7] for r in reads]}")

        if args.wants("kill"):
            reads = swing("kill", one(ZOZURA, 236, hp=ONE_BLOW))
            r5_, xp, mp, sc, s = reads[-1]
            xp0, mp0 = reads[0][1], reads[0][2]
            assert r5_["entities"][7] == DEATH, ("dies in room 5", r5_)
            assert all(r[3] == 4 for r in reads), "he stays in room 4"
            assert xp == xp0 + ZOZURA_XP and mp == mp0 + ZOZURA_MP, (xp0, xp, mp0, mp)
            kills = visited(s["visit_log"], 5, ZOZURA, DEATH)
            assert kills and kills[-1][1] == 7, s["visit_log"]
            assert ZOZURA not in room(4)["entities"] and DEATH not in room(4)["entities"]
            game.shot("kill-across")
            print(f"ok: from room 4's x 4 the Long Sword kills room 5's Zozura at its x 236 as a visitor "
                  f"(visit {kills[-1]}): its death in room 5, experience {xp0} -> {xp}, MP {mp0} -> {mp}")
            until("its drop in room 5", lambda: BREAD in room(5)["entities"], 120)
            r5_ = room(5)
            k = r5_["entities"].index(BREAD)
            assert abs(r5_["x"][k] - 236) <= 4 and BREAD not in room(4)["entities"] and ram(SCREEN) == 4, r5_
            print(f"ok: room 5's tick plays its death and leaves its bread at its x {r5_['x'][k]}, in room 5")

        if args.wants("coin-drop"):
            reads = swing("coin-drop", one(MONODRON, 236, floor_y - 16, hp=ONE_BLOW),
                          done=lambda r5_: r5_["entities"][7] != MONODRON)
            assert reads[-1][0]["entities"][7] == DEATH and all(r[3] == 4 for r in reads), reads[-1][0]
            played0 = seams()["played"]
            until("its coin in room 5", lambda: COIN in room(5)["entities"], 120)
            until("the coin's sound", lambda: seams()["played"] > played0, 30)
            s, r5_ = seams(), room(5)
            sound = [e for e in s["sound_log"] if e[0] == 5 and e[2] == COIN_SOUND]
            assert sound, s["sound_log"]
            assert COIN not in room(4)["entities"] and ram(SCREEN) == 4
            print(f"ok: a Monodron killed across the seam drops its coin in room 5 (x "
                  f"{r5_['x'][r5_['entities'].index(COIN)]}), and the live game plays the coin's sound: {sound[-1]}")

        if args.wants("coin"):
            edits = dict(empty5)
            spawn(image, edits, record.base(5), 3, COIN, 250, floor_y + 8, 20)
            start("coin", "eolis-zozuras", {HERO_X: 12, PLAYER_FLAGS: 0, OINTMENT: 0x7F, **no_live_zozuras}, edits)
            g0 = gold()
            tcp("action", name="left", frames=120)
            until("the coin", lambda: gold() != g0, 120)
            tcp("action", name="left", frames=1)
            sc, x, s, r5_ = ram(SCREEN), ram(HERO_X), seams(), room(5)
            assert sc == 4 and x < 16, ("taken from room 4", sc, x)
            assert gold() == g0 + 40 and r5_["entities"][3] == EMPTY, (g0, gold(), r5_)
            taken = visited(s["visit_log"], 5, COIN, EMPTY)
            assert taken, s["visit_log"]
            game.shot("coin-across")
            print(f"ok: walking left in room 4 he takes the coin at room 5's x 250 at his x {x}: gold {g0} -> {gold()} "
                  f"(visit {taken[-1]})")

        if args.wants("touch"):
            start("touch", "eolis-zozuras", {HERO_X: 2, PLAYER_FLAGS: 0, HP: 0x50, OINTMENT: 0xFF, INVINCIBILITY: 0,
                                             **no_live_zozuras}, one(ZOZURA, 248, through=THROUGH_RIGHT))
            hp0, end, hit = 0x50, frame() + 20, None
            while frame() < end:
                hp, sc, r5_, r4, inv = batch(("read_ram", dict(addr=HP, len=1)), ("read_ram", dict(addr=SCREEN, len=1)),
                                            ("fax_world_room", dict(room=5)), ("fax_world_room", dict(room=4)),
                                            ("read_ram", dict(addr=INVINCIBILITY, len=1)))
                if int(hp["hex"], 16) != hp0:
                    hit = (int(hp["hex"], 16), int(sc["hex"], 16), r5_, r4, int(inv["hex"], 16))
                    break
            assert hit, "the straddling Zozura never hurt him"
            hp, sc, r5_, r4, inv = hit
            assert sc == 4 and r5_["entities"][7] == ZOZURA and r5_["x"][7] >= 248 and ZOZURA not in r4["entities"], hit
            assert hp < hp0 and inv > 0, hit
            assert visited(seams()["visit_log"], 5, ZOZURA), seams()["visit_log"]
            game.shot("touch-across")
            print(f"ok: a Zozura straddling the seam at room 5's x {r5_['x'][7]} hurts him at room 4's x 2: HP "
                  f"{hp0} -> {hp}, iframes {inv}, while it is still in room 5")

        if args.wants("punch"):
            reads = swing("punch", one(ZOZURA, 236, facing_right=False), NO_WEAPON,
                          done=lambda r5_: r5_["hp"][7] != 9)
            r5_ = reads[-1][0]
            assert r5_["entities"][7] == ZOZURA and r5_["hp"][7] == 7 and r5_["hit_counter"][7] > 0, r5_
            assert all(r[3] == 4 for r in reads) and visited(reads[-1][4]["visit_log"], 5, ZOZURA, ZOZURA)
            print(f"ok: a punch from room 4's x 4 hits room 5's Zozura at its x {r5_['x'][7]} as a visitor: 9 HP -> 7, "
                  f"its hit counter {r5_['hit_counter'][7]} (its knockback, in room 5's tick)")

        if args.wants("flash"):
            warning = game.state("seams-mist-warning")
            wdata = warning.read_bytes()
            wlive, wrec = hw_ram(wdata), Record(wdata)
            assert (wlive[AREA], wlive[SCREEN]) == (MIST, MIST_30)
            slot = list(wlive[ENTITIES:ENTITIES + 8]).index(SUGATA)
            mine = [s for s in range(8) if wrec.ram(MIST_31)[ENTITIES + s] == SUGATA]
            base = wrec.base(MIST_31)

            def across(case, hero_x):
                """The live Sugata, at its warning's first frame, moved into
                $1F's slot 6 at its x 4 (any of $1F's own taken out), the hero
                on $1E at hero_x facing right."""
                edits = {base + ENTITIES + s: EMPTY for s in mine}
                for a in SLOT_ARRAYS:
                    edits[base + a + 6] = wlive[a + slot]
                edits[base + SPRITE_X + 6] = 4
                edits[wrec.block(MIST_31)] = 0x40                    # awake: slot 6
                edits[wrec.block(MIST_31) + 2 + 6] = 0
                writes = {ENTITIES + slot: EMPTY, HERO_X: hero_x, PLAYER_FLAGS: 0x40, HP: 0x50, OINTMENT: 0xFF}
                path = edited(case, warning, writes, edits)
                if game.running():
                    game.close()
                game.launch(at="seams-mist-warning")
                tcp("load_state", path=str(path))

            across("flash", 228)
            r31 = room(MIST_31)
            assert r31["entities"][6] == SUGATA and r31["x"][6] == 4 and room(MIST_30)["edges"][1] == MIST_31, r31
            shot = out / f"{len(game.shots):02d}-flash-across.png"
            tcp("sugata_capture", path=str(shot), band="flash", at=1)
            game.shots.append(shot.name)
            s0, w0, hp0 = tcp("sugata_state"), seams(), ram(HP)
            end, seen = frame() + WARNING + 60, None
            while frame() < end and not seen:                     # the frame the live game takes the flash
                s1, w1, r30, r31, hp, sc = batch(("sugata_state", {}), ("seams_state", {}),
                                                 ("fax_world_room", dict(room=MIST_30)),
                                                 ("fax_world_room", dict(room=MIST_31)),
                                                 ("read_ram", dict(addr=HP, len=1)), ("read_ram", dict(addr=SCREEN, len=1)))
                if s1["in_reach"] > s0["in_reach"]:
                    seen = (s1, w1, r30, r31, int(hp["hex"], 16), int(sc["hex"], 16))
            assert seen, "no flash in reach"
            s1, w1, r30, r31, hp, sc = seen
            # (each case is a fresh game: its counts start with it)
            assert s1["near_warnings"] >= 1 and s1["near_flashes"] >= 1 and s1["warnings"] == s1["flashes"] == 0, s1
            assert s1["waits"] > s0["waits"] and w1["flash_runs"] == w0["flash_runs"] + 1, (s1, w1)
            assert s1["flash_room"] == MIST_31 and s1["flash_x"] == [12 - REACH, 12 + REACH], s1
            assert hp0 - hp == FLASH_DAMAGE, (hp0, hp)
            assert sc == MIST_30 and SUGATA not in r30["entities"] and r31["entities"][6] == SUGATA, (r30, r31)
            until(f"the picture {shot.name}", lambda: tcp("sugata_state")["captured"] > s0["captured"], 60)
            print(f"ok: Sugata in $1F at its x 4 warns and flashes there (its band [{s1['flash_x'][0]}, "
                  f"{s1['flash_x'][1]}) of $1F), and the live game takes the damage: HP {hp0} -> {hp}, "
                  f"the hero on $1E at x 228")
            across("flash-away", 150)
            s0, hp0 = tcp("sugata_state"), ram(HP)
            until("the flash", lambda: tcp("sugata_state")["near_out_of_reach"] > s0["near_out_of_reach"], WARNING + 60)
            wait(4)
            s1 = tcp("sugata_state")
            assert ram(HP) == hp0 and s1["in_reach"] == s0["in_reach"] and s1["waits"] == s0["waits"], (s1, ram(HP))
            print(f"ok: with the hero at x 150, out of its reach, the flash ends in $1F's tick and takes nothing")

        def headless(state, frames, saves):
            """A fresh game, headless, the remaster on: `state` loaded, run
            `frames` frames, a state saved after each (frame, path) of
            `saves`, frames counted from the state's. Frame exact however
            loaded the machine is."""
            mods = out / "headless-mods"
            if not mods.exists():
                shutil.copytree(out / "game" / "mods" / "packages", mods / "packages")
                (mods / "state.toml").write_text(fax_e2e.features_toml({}))
            at = struct.unpack_from("<I", state.read_bytes(), 28)[0]          # the frame it continues at
            run = [str(game.exe), str(game.rom), "--load-state", str(state), "--frames", str(at + frames),
                   "--mods-root", str(mods), "--no-save"]
            for f, p in saves:
                run += ["--save-state", f"{at + f}:{p}"]
            done = subprocess.run(run, env=game.env, capture_output=True, text=True)
            assert done.returncode == 0 and all(p.exists() for _, p in saves), done.stdout + done.stderr

        def replays(mid, rooms):
            """Two fresh games, headless, each loading `mid` and running 60
            frames: the same machine, the same records of `rooms`, and the
            same Seams record. Returns the first's state."""
            after = []
            for i in range(2):
                p = out / f"{mid.stem}-after-{i}.cycstate"
                headless(mid, 61, [(60, p)])
                after.append(p.read_bytes())
            a, b = after
            ra, rb = Record(a), Record(b)
            assert hw_ram(a) == hw_ram(b), "the machine"
            for n in rooms:
                assert ra.ram(n) == rb.ram(n), f"room {n}'s record"
            assert seams_record(a) and seams_record(a) == seams_record(b), "Seams' record"
            return a

        def seams_record(data):
            return [bytes(data[p:p + n]) for tag, p, n in fax_e2e.chunks(data) if tag == b"MOD " and
                    bytes(data[p:p + 64]).rstrip(b"\0") == b"faxanadu.seams"]

        def inside_pass(state):
            """The first of LAG_FRAMES frames played headless from `state`
            that ends inside the visitors' pass: its number, from 0, and the
            state saved after it (OUT/lag-mid-pass.cycstate). A frame's end
            leaves no trace of the pass in the machine (fax_seams_frame_end()
            puts the live slots back), so a state is saved after every frame
            and its Seams record read; LAG_CHUNK frames a run, each run from
            `state`, since a save is over a megabyte."""
            for first in range(0, LAG_FRAMES, LAG_CHUNK):
                saves = [(f, out / f"lag-{f:03d}.cycstate") for f in range(first, first + LAG_CHUNK)]
                headless(state, first + LAG_CHUNK, saves)
                found = None
                for f, p in saves:
                    if found is None and seams_record(p.read_bytes())[0][64 + 1]:   # past its name: open
                        found = f, p.replace(out / "lag-mid-pass.cycstate")
                    else:
                        p.unlink()
                if found:
                    return found
            raise AssertionError(f"no frame ended inside the visitors' pass in {LAG_FRAMES} frames")

        if args.wants("save-load"):
            path = edited("save-load", zozuras, {HERO_X: 4, PLAYER_FLAGS: 0, PLAYER_CUR_WEAPON: LONG_SWORD,
                                                 SELECTED_WEAPON: LONG_SWORD, HP: 0x50, MP: 10, OINTMENT: 0x7F,
                                                 **no_live_zozuras}, one(ZOZURA, 236, hp=ONE_BLOW))
            if game.running():
                game.close()
            game.launch(at="eolis-zozuras")
            batch(("load_state", dict(path=str(path))), ("action", dict(name="b", frames=2)))
            wait(6)
            a = replays(game.save("mid-swing"), (4, 5))
            assert Record(a).ram(5)[ENTITIES + 7] != ZOZURA, "the Zozura dead in room 5 by then"
            print("ok: saved mid-swing, two fresh games run 60 frames from it to the same machine and records")

        if args.wants("lag"):
            cast = game.state("block-hood-cast")
            clive = hw_ram(cast.read_bytes())
            shot = list(clive[ENTITIES:ENTITIES + 8]).index(HOOD_SHOT)
            path = out / "lag.cycstate"
            fixture(cast, path, {SPRITE_Y + shot: HEAD_HEIGHT, 0x3BF: 0})                # the Small Shield
            n, mid = inside_pass(path)
            rec = seams_record(mid.read_bytes())[0][64:]                         # past the record's name
            assert rec[1] == 1 and rec[4] == 1, ("saved inside the pass, the live slots out", list(rec[:5]))
            if game.running():
                game.close()
            game.launch(at="block-hood-cast")
            # Read at that frame's end: its save loaded, and read before a frame runs (one batch).
            loaded, s, live_, r16, sc, at = batch(("load_state", dict(path=str(mid))), ("seams_state", {}),
                                                  ("read_ram", dict(addr=ENTITIES, len=8)),
                                                  ("fax_world_room", dict(room=ZENIS_16)),
                                                  ("read_ram", dict(addr=SCREEN, len=1)), ("ping", {}))
            entities = list(bytes.fromhex(live_["hex"]))
            assert s["visiting"] and at["frame"] == loaded["frame"], ("inside the pass", loaded, at, s["visiting"])
            assert int(sc["hex"], 16) == ZENIS_16 and entities == r16["entities"], (entities, r16)
            assert entities.count(EMPTY) <= 3 and HOOD_SHOT in entities, ("the room's own, none hidden", entities)
            replays(mid, (ZENIS_16,))
            print(f"ok: frame {n} of Zenis 16 from the shot's save ends inside the visitors' pass ({rec[2]} "
                  f"visiting); read then, the live slots are the room's own ({[hex(e) for e in entities]}); saved "
                  f"then, two fresh games run 60 frames from it to the same machine and records")
        print("screenshots:", ", ".join(game.shots))
    except BaseException:
        if game.running():
            game.shot("failure")
        raise
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
