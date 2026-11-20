"""Play Slain by end to end in a hidden window: what killed the hero, named as he dies.

    python3 tests/slain/slain_by.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/slain
    python3 tests/slain/slain_by.py ... --game-arg=--world-view=seamless

Copies the build to OUT and starts a new game over TCP, walks through Eolis's
gate into a shop, and walks out of it onto each screen below: the shop's door
is edited to lead there (save-state RAM, an external prerequisite, as
tests/zones/zone_names.py does). So are the hero's last HP, an enemy moved
onto him, a shot aimed at his head, and the other enemies cleared away. Every
hit and every death is the game's own:

1. A death with nothing to blame (PlayerIsDead set) names nothing.
2. The Trunk's first screen: a Monodron's touch takes his last HP. The banner
   says "Slain by a Monodron" as he dissolves, and is gone when the Guru
   speaks. Then, as before: death keeps the gold and experience, zone respawn
   brings him back at the Trunk's entrance, and Save files writes the file.
3. The fortress of Zenis, screen 16: a Zozura's touch, then an Execution
   Hood's shot ($51), named after the Hood that cast it.
4. The Mist's screen 30: Sugata's flash, "Slain by a Sugata". Holding Block
   with the Small Shield stops it, and with the Magic Shield sends it back:
   neither kills, and nothing is named.
5. A boss, through the Tower of Trunk's top screen ($15): Ripasheiku.
6. With Faxanadu Remastered off (the original game), the same death names
   nothing.
"""
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "block"))
from harness import (Game, fixture, AREA, SCREEN, POS_X, POS_Y, ENTITIES, SPRITE_X, SPRITE_Y, SELECTED_SHIELD,  # noqa: E402
                     SMALL, MAGIC, HP, PLAYER_IS_DEAD, MESSAGE_ID, GOLD)

HP_FRACTION, EXPERIENCE = 0x432, 0x390
NEGATIVE_THOUGHTS = 0x24                                     # the Guru's words after a death
TRUNK, MIST, ZENIS = 1, 2, 7
MONODRON, ZOZURA, EXECUTION_HOOD, HOOD_SHOT, SUGATA, RIPASHEIKU = 0x2A, 0x47, 0x10, 0x51, 0x18, 0x2D
HERO = dict(experience=4660, gold=12345)
# (area, screen, palette, start YX, region) for each place, from the door tables
# (docs/GAME_INTERNALS.md, "Entrances and zone respawn"): the Trunk as its door
# from Eolis leaves him, Zenis's screen 16 and the Mist's screen 30 as the
# Block and Sugata tests reach them, and the Tower of Trunk's last screen.
PLACES = dict(trunk=(TRUNK, 0x00, 0x06, 0xA1, 1), zenis=(ZENIS, 0x10, 0x0F, 0x91, 5),
              mist=(MIST, 30, 0x0A, 0x92, 2), boss=(TRUNK, 0x15, 0x07, 0x91, 1))
PACKAGE, FEATURE = "faxanadu.remastered", "remastered"          # the one switch: off is the original game


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for key in ("build", "rom", "out"):
        ap.add_argument("--" + key, type=Path, required=True)
    ap.add_argument("--game-arg", action="append", default=[],
                    help="extra game option, e.g. --game-arg=--world-view=seamless (repeatable)")
    ap.add_argument("--port", type=int, default=0, help="TCP port (default: a free one)")
    ap.add_argument("--realtime", action="store_true", help="run at normal speed instead of uncapped")
    args = ap.parse_args()
    g = Game(args.build, args.rom, args.out, args.game_arg, args.realtime, args.port)

    def slain():
        return g.tcp("slain_state")

    def entities():
        return list(g.ram(ENTITIES, 8))

    def only(keep):
        """Every other enemy on the screen cleared away."""
        return {ENTITIES + i: 0xFF for i, e in enumerate(entities()) if e != 0xFF and e not in keep}

    def counted(s):
        return s["deaths"] + s["unnamed"]

    def die(state, name, writes, line, how=None, frames=1500, picture=True):
        """Load `state` edited with `writes` (his last HP, and whatever kills
        him), wait for the killing hit, and check the line through the
        dissolve until the Guru speaks. Returns the line's state at the hit."""
        dying = g.out / f"{name}-dying.cycstate"
        fixture(state, dying, writes)
        before = slain()
        g.load(dying)
        g.until(f"the hit that kills him ({name})", lambda: g.ram(PLAYER_IS_DEAD) or counted(slain()) > counted(before),
                frames)
        hit = slain()
        assert hit["showing"] == line and hit["deaths"] == before["deaths"] + 1, (name, hit)
        if how:
            assert hit["how"] in ((how,) if isinstance(how, str) else how), (name, hit)
        g.wait(48)                                           # the line at full strength, the hero dissolving
        s = slain()
        assert s["showing"] == line and s["alpha"] == 255, (name, s)
        if picture:
            g.shot(name)
        g.until("the Guru's words", lambda: g.ram(MESSAGE_ID) == NEGATIVE_THOUGHTS, 600)
        assert slain()["showing"] is None, "the line is gone before the Guru speaks"
        return hit

    def page_until(what, test, tries=80):
        for _ in range(tries):
            if test():
                return
            if g.box_open() and g.typed():
                g.act("a", 6, 14)
            else:
                g.wait(10)
        raise AssertionError(f"timed out paging to {what}")

    try:
        g.launch()
        assert slain()["enabled"], "Slain by is on by default"
        g.new_game_at_gate()
        g.act("up", 4, 10)
        g.until("Eolis", lambda: g.where() == (0, 1), 300)
        g.wait(30)
        g.walk_to(41, 56)
        g.act("up", 4, 10)
        g.until("the shop", lambda: g.ram(AREA) == 4)
        g.wait(30)
        shop = g.save("eolis-shop")

        # 1. Nothing took his HP: nothing to name.
        dying = g.out / "nothing-dying.cycstate"
        fixture(shop, dying, {PLAYER_IS_DEAD: 1})
        g.load(dying)
        for _ in range(12):
            g.wait(20)
            assert slain()["showing"] is None
        g.until("the Guru's words", lambda: g.ram(MESSAGE_ID) == NEGATIVE_THOUGHTS, 600)
        s = slain()
        assert (s["deaths"], s["unnamed"]) == (0, 0), s
        print("ok: a death that nothing caused names nothing")

        # 2. A Monodron's touch, and the death that follows, as before.
        g.warp(shop, "trunk", *PLACES["trunk"], writes={
            EXPERIENCE: HERO["experience"] & 0xFF, EXPERIENCE + 1: HERO["experience"] >> 8,
            GOLD: HERO["gold"] & 0xFF, GOLD + 1: HERO["gold"] >> 8, GOLD + 2: 0})
        g.wait(20)
        trunk = g.save("trunk")
        slot = entities().index(MONODRON)
        hero_x = g.ram(POS_X)
        respawns, saves = g.tcp("respawn_state")["respawns"], g.tcp("files_state")["saves"]
        die(trunk, "monodron", {HP: 1, HP_FRACTION: 0, SPRITE_X + slot: hero_x + 8}, "Slain by a Monodron", "touch")
        page_until("the Trunk's entrance", lambda: g.where() == (TRUNK, 0) and g.settled() and not g.box_open())
        assert g.tcp("respawn_state")["respawns"] == respawns + 1, "zone respawn: back at the Trunk's entrance"
        assert g.tcp("files_state")["saves"] == saves + 1, "Save files wrote the file at the respawn"
        kept = dict(experience=int.from_bytes(g.ram(EXPERIENCE, 2), "little"),
                    gold=int.from_bytes(g.ram(GOLD, 3), "little"))
        assert kept == HERO, kept
        assert slain()["showing"] is None
        print("ok: a Monodron's touch: \"Slain by a Monodron\" as he dissolves, gone when the Guru speaks; then he "
              "keeps his gold and experience, comes back at the Trunk's entrance, and the file is saved")

        # 3. Zenis's screen 16: a Zozura's touch, then the Execution Hood's shot.
        g.warp(shop, "zenis", *PLACES["zenis"])
        g.wait(20)
        zenis = g.save("zenis")
        hero_x, hero_y = g.ram(POS_X), g.ram(POS_Y)
        zozura = entities().index(ZOZURA)
        die(zenis, "zozura", {HP: 1, HP_FRACTION: 0, SPRITE_X + zozura: hero_x + 8, **only({ZOZURA})},
            "Slain by a Zozura", "touch", picture=False)
        print("ok: a Zozura's touch: \"Slain by a Zozura\"")
        quiet = g.out / "zenis-quiet.cycstate"
        fixture(zenis, quiet, only({EXECUTION_HOOD}))
        g.load(quiet)
        g.until("the Execution Hood's shot", lambda: HOOD_SHOT in entities(), 900)
        cast = g.save("zenis-shot")
        shot = entities().index(HOOD_SHOT)
        s = slain()
        assert s["spawners"][shot] == EXECUTION_HOOD, s
        die(cast, "execution-hood", {HP: 1, HP_FRACTION: 0, SPRITE_Y + shot: hero_y - 8},
            "Slain by an Execution Hood", "shot")
        print("ok: the Execution Hood's shot is named after it: \"Slain by an Execution Hood\"")

        # 4. Sugata's flash, then the same flash blocked and sent back.
        g.warp(shop, "mist", *PLACES["mist"])
        g.wait(20)
        mist = g.save("mist")
        weak = g.out / "mist-weak.cycstate"
        fixture(mist, weak, {HP: 9, HP_FRACTION: 0, **only({SUGATA})})
        g.load(weak)
        g.until("Sugata's warning", lambda: g.tcp("sugata_state")["warning"] > 0, 900)
        warning = g.save("mist-warning")
        die(warning, "sugata", {}, "Slain by a Sugata", "flash", 120)
        print("ok: Sugata's flash: \"Slain by a Sugata\"")
        for shield, what, counter in ((SMALL, "blocks", "block"), (MAGIC, "sends back", "reflect")):
            blocked = g.out / f"mist-warning-{shield}.cycstate"
            fixture(warning, blocked, {SELECTED_SHIELD: shield})
            before = slain()
            g.load(blocked)
            sugata, stopped = g.tcp("sugata_state"), g.tcp("block_state")["flashes_blocked"]
            g.tcp("key", name="V", frames=60)
            g.until("the flash", lambda: g.tcp("sugata_state")["flashes"] > sugata["flashes"], 60)
            g.wait(4)
            if counter == "block":
                assert g.tcp("block_state")["flashes_blocked"] == stopped + 1, "Block stopped the flash"
            else:
                assert g.tcp("sugata_state")["reflected"] == sugata["reflected"] + 1, "the flash went back"
            assert g.hp() == 9, g.hp()
            g.wait(20)                                       # Sugata may walk into him now: 4 HP, not his last
            s = slain()
            assert not g.ram(PLAYER_IS_DEAD) and s["showing"] is None and counted(s) == counted(before), s
            print(f"ok: Block with the {'Small' if shield == SMALL else 'Magic'} Shield {what} the flash: "
                  "no death, nothing named")

        # 5. A boss: Ripasheiku, at the top of the Tower of Trunk.
        g.warp(shop, "boss", *PLACES["boss"])
        g.wait(20)
        assert RIPASHEIKU in entities(), [hex(e) for e in entities()]
        boss = g.save("boss")
        hit = die(boss, "ripasheiku", {HP: 1, HP_FRACTION: 0}, "Slain by a Ripasheiku", ("shot", "touch"), 3000)
        print(f"ok: Ripasheiku ({hit['how']}): \"Slain by a Ripasheiku\"")

        # 6. The remaster off: the original game's death, with nothing named.
        g.close()
        (g.out / "game" / "mods" / "state.toml").write_text(
            f'format_version = 1\n\n[[feature]]\npackage_id = "{PACKAGE}"\nid = "{FEATURE}"\nenabled = false\n')
        g.launch()
        assert not slain()["enabled"], "Slain by is off with the remaster"
        g.load(g.out / "monodron-dying.cycstate")
        g.until("the killing hit", lambda: g.ram(PLAYER_IS_DEAD), 300)
        for _ in range(6):
            g.wait(20)
            assert slain()["showing"] is None
        g.until("the Guru's words", lambda: g.ram(MESSAGE_ID) == NEGATIVE_THOUGHTS, 600)
        s = slain()
        assert (s["deaths"], s["unnamed"]) == (0, 0), s
        print("ok: with Faxanadu Remastered off, the Monodron's touch kills him and nothing is named")
        print("screenshots:", ", ".join(p.name for p in g.shots))
    except BaseException:
        if g.running():
            g.shot("failure")
        raise
    finally:
        g.close()


if __name__ == "__main__":
    sys.exit(main())
