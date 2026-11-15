"""Play Sugata's flash end to end in a hidden window, with Block.

    python3 tests/sugata/sugata_flash.py --build build-cycle --rom "Faxanadu (USA).nes" --out /tmp/sugata

Copies the build to OUT and starts a new game over TCP, walks through Eolis's
gate into a shop, and walks out of it onto the Mist's screen 30 (area 2), where
Sugata lives: the shop's door is edited to lead there (save-state RAM, an
external prerequisite, as tests/zones/zone_names.py does). Sugata also lives on
the Mist's screens 31, 78 and 79. Then, from a save state at the first frame of
Sugata's warning, edited for the hero's place and shield:

1. Sugata blinks for 30 frames, the band within its reach dimmed, before
   every flash.
2. The flash greys only that band. In it, the hero loses 10 HP; out of it,
   nothing.
3. Holding Block (V) with the Small Shield, in the band: nothing, and the
   shield's sound. Then Sugata walks into him: contact damage is the game's 4.
   With the Battle Helmet: nothing either.
4. Holding Block with the Magic Shield: Sugata loses 12 HP; a second
   reflection (Sugata at the 10 HP the first leaves) kills it, and its death
   ends the band.
5. Holding Block without a shield: 10 HP. Under Ointment, in the band: no HP,
   but the knockback.
6. With Sugata's flash off, the original: no warning, 10 HP, Ointment or not.

Pictures of the band are taken by the game itself (TCP sugata_capture) on the
frame it shows, since a flash lasts a few frames.
"""
import argparse
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "block"))
from harness import (Game, fixture, AREA, BUILDINGS, POS_X, ENTITIES, SPRITE_X, SPRITE_HP, SELECTED_SHIELD,  # noqa: E402
                     SMALL, MAGIC, HELMET, NO_SHIELD, HP, INVINCIBILITY)

MIST, SCREEN_30, PALETTE_MIST, REGION_MIST = 2, 30, 0x0A, 2
SUGATA, DEATH = 0x18, 0x14
REACH, WARNING, REFLECTED, FLASH_DAMAGE = 40, 30, 12, 10
DURATION_OINTMENT = 0x427
PACKAGE, FEATURE = "faxanadu.enhancement.sugata-flash", "sugata-flash"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for key in ("build", "rom", "out"):
        ap.add_argument("--" + key, type=Path, required=True)
    ap.add_argument("--game-arg", action="append", default=[],
                    help="extra game option, e.g. --game-arg=--world-view=seamless (repeatable)")
    ap.add_argument("--port", type=int, default=0, help="TCP port (default: a free one)")
    ap.add_argument("--realtime", action="store_true", help="run at normal speed instead of uncapped")
    args = ap.parse_args()
    rom = args.rom.read_bytes()[16:]
    contact = rom[14 * 0x4000 + 0xB6D7 - 0x8000 + SUGATA]          # SPRITE_ENTITY_DAMAGES[Sugata]
    g = Game(args.build, args.rom, args.out, args.game_arg, args.realtime, args.port)

    def state():
        return g.tcp("sugata_state")

    def sugata():
        """Sugata's slot."""
        entities = list(g.ram(ENTITIES, 8))
        return entities.index(SUGATA) if SUGATA in entities else None

    def capture(name, band, at=0):
        path = g.out / f"{len(g.shots):02d}-{name}.png"
        g.tcp("sugata_capture", path=str(path), band=band, at=at)
        g.shots.append(path)
        return path

    def captured(path, before):
        g.until(f"the picture {path.name}", lambda: state()["captured"] > before, 120)
        assert path.exists(), path

    def flash(hold=None):
        """From a state at the warning's first frame: hold `hold` (a key) and
        play to the end of the flash. Returns (HP lost, the state's change)."""
        before, hp = state(), g.hp()
        if hold:
            g.tcp("key", name=hold, frames=WARNING + 20)
        g.until("the flash", lambda: state()["in_reach"] + state()["out_of_reach"] >
                before["in_reach"] + before["out_of_reach"], WARNING + 30)
        g.wait(2)
        after = state()
        return hp - g.hp(), {k: after[k] - before[k] for k in after if isinstance(after[k], int) and k in before}

    try:
        g.launch()
        assert state()["enabled"] and g.tcp("block_state")["enabled"]
        g.new_game_at_gate()
        g.act("up", 4, 10)
        g.until("Eolis", lambda: g.where() == (0, 1), 300)
        g.wait(30)
        g.walk_to(41, 56)
        g.act("up", 4, 10)
        g.until("the shop", lambda: g.ram(AREA) == BUILDINGS)
        g.wait(30)
        shop = g.save("eolis-shop")
        g.warp(shop, "mist-30", MIST, SCREEN_30, PALETTE_MIST, 0x92, REGION_MIST)
        arrived = g.save("mist-30-arrived")
        slot = sugata()
        assert slot is not None, list(g.ram(ENTITIES, 8))
        print(f"ok: out of a shop onto the Mist's screen 30, with Sugata in slot {slot}")

        # 1. The warning: Sugata blinks where it stands, the band dimmed.
        hidden = capture("sugata-warning", "warning", at=28)      # its last frames: Sugata shown
        g.until("Sugata's warning", lambda: state()["warning"] > 0, 600)
        s = state()
        assert s["warnings"] == 1 and s["flashes"] == 0 and s["band"] == "warning", s
        warning = g.save("warning")
        center = g.ram(SPRITE_X + slot) + 8
        assert s["band_x"] == [center - REACH, center + REACH], (s, center)
        captured(hidden, 0)
        g.until("the warning's 30 frames", lambda: state()["flashes"] == 1, 60)
        assert state()["flash_x"] == s["band_x"], "Sugata stands still while it warns"
        print(f"ok: Sugata blinks for {WARNING} frames before it flashes, its reach [{center - REACH}, "
              f"{center + REACH}) dimmed")

        # 2. In the band, 10 HP; out of it, nothing.
        g.load(warning)
        hero = g.ram(POS_X)
        assert abs(hero + 8 - center) < REACH + 8, (hero, center)
        shot = capture("flash-hits", "flash")
        lost, d = flash()
        captured(shot, 1)
        assert lost == FLASH_DAMAGE and d["in_reach"] == 1 and d["flashes"] == 1, (lost, d)
        print(f"ok: in the band (hero at {hero}, Sugata's middle at {center}), the flash takes {lost} HP")
        away = g.out / "warning-away.cycstate"
        fixture(warning, away, {POS_X: center + REACH + 24})
        g.load(away)
        shot = capture("flash-misses", "flash")
        lost, d = flash()
        captured(shot, 2)
        assert lost == 0 and d["out_of_reach"] == 1 and d["flashes"] == 1, (lost, d)
        assert not state()["last_reached"]
        print(f"ok: out of the band (hero at {center + REACH + 24}), the flash takes nothing")

        # 3. Block with the Small Shield, in the band; then Sugata's touch.
        small = g.out / "warning-small.cycstate"
        fixture(warning, small, {SELECTED_SHIELD: SMALL})
        g.load(small)
        blocked = g.tcp("block_state")["flashes_blocked"]
        shot = capture("flash-blocked", "flash")
        lost, d = flash("V")
        captured(shot, 3)
        assert lost == 0 and d["in_reach"] == 1 and d["reflected"] == 0, (lost, d)
        assert g.tcp("block_state")["flashes_blocked"] == blocked + 1
        print("ok: holding Block with the Small Shield in the band, the flash takes nothing")
        g.tcp("key", name="V", frames=400)
        hp, x = g.hp(), g.ram(POS_X)
        g.until("Sugata's touch", lambda: g.hp() != hp, 300)
        assert g.tcp("block_state")["raised"], "still blocking"
        assert hp - g.hp() == contact and g.ram(POS_X) != x, (hp, g.hp(), contact)
        print(f"ok: blocking, Sugata's touch still takes the game's {contact} HP and knocks him back")
        g.tcp("key", name="V", frames=1)
        g.wait(4)
        helmet = g.out / "warning-helmet.cycstate"
        fixture(warning, helmet, {SELECTED_SHIELD: HELMET})
        g.load(helmet)
        lost, d = flash("V")
        assert lost == 0 and d["in_reach"] == 1 and d["reflected"] == 0, (lost, d)
        assert g.tcp("block_state")["flashes_blocked"] == blocked + 2
        g.tcp("key", name="V", frames=1)
        g.wait(4)
        print("ok: holding Block with the Battle Helmet in the band, the flash takes nothing")

        # 4. The Magic Shield sends it back.
        magic = g.out / "warning-magic.cycstate"
        fixture(warning, magic, {SELECTED_SHIELD: MAGIC})
        g.load(magic)
        shot = capture("magic-shield-reflects", "flash")
        lost, d = flash("V")
        captured(shot, 4)
        assert lost == 0 and d["reflected"] == 1 and g.ram(SPRITE_HP + slot) == 22 - REFLECTED, (lost, d)
        assert g.ram(ENTITIES + slot) == SUGATA and state()["pushed"] == 1, state()   # away from him, to the right
        print(f"ok: holding Block with the Magic Shield, the flash takes {REFLECTED} of Sugata's 22 HP instead, "
              "and knocks it back away from him")
        last = g.out / "warning-magic-10hp.cycstate"
        fixture(warning, last, {SELECTED_SHIELD: MAGIC, SPRITE_HP + slot: 22 - REFLECTED})
        g.load(last)
        lost, d = flash("V")
        assert lost == 0 and d["reflected"] == 1 and g.ram(ENTITIES + slot) == DEATH, (lost, d)
        g.wait(2)
        assert state()["band"] == "none" and g.ram(0x0B) & 1 == 0, state()
        g.shot("reflection-kills-sugata")
        print("ok: a second reflection kills Sugata through the game's death code, and the band ends with it")
        s = state()
        assert s["warned"] == s["flashes"] == 7, s
        print(f"ok: each of the {s['flashes']} flashes came after the full {WARNING}-frame warning")

        # 5. No shield, Block held: the band still hurts. Ointment keeps the HP, not the knockback.
        g.load(warning)
        lost, d = flash("V")
        assert lost == FLASH_DAMAGE, lost
        print("ok: holding Block without a shield, the flash still takes 10 HP")
        g.tcp("key", name="V", frames=1)
        g.wait(4)
        anointed = g.out / "warning-ointment.cycstate"
        fixture(warning, anointed, {DURATION_OINTMENT: 0x40})
        g.load(anointed)
        x = g.ram(POS_X)
        lost, d = flash()
        g.wait(8)
        assert lost == 0 and d["anointed"] == 1 and d["in_reach"] == 1, (lost, d)
        assert g.ram(INVINCIBILITY) > 0 and g.ram(POS_X) != x, (g.ram(INVINCIBILITY), x, g.ram(POS_X))
        print(f"ok: under Ointment, in the band, the flash takes no HP but still knocks him back "
              f"(x {x} to {g.ram(POS_X)})")
        g.close()

        # 6. Off: the original flash, without a warning, through Ointment.
        (g.out / "game" / "mods" / "state.toml").write_text(
            f'format_version = 1\n\n[[feature]]\npackage_id = "{PACKAGE}"\nid = "{FEATURE}"\nenabled = false\n')
        g.launch()
        s = state()
        assert not s["enabled"], s
        g.load(arrived)
        hp = g.hp()
        g.until("the flash", lambda: hp - g.hp() == FLASH_DAMAGE, 400)
        assert state()["warnings"] == state()["flashes"] == 0
        print("ok: turned off, Sugata flashes without warning and takes 10 HP, as the original")
        target = g.out / "off-ointment.cycstate"
        fixture(arrived, target, {DURATION_OINTMENT: 0x40})
        g.load(target)
        hp = g.hp()
        g.until("the flash", lambda: hp - g.hp() == FLASH_DAMAGE, 400)
        assert g.ram(DURATION_OINTMENT) < 0x80, "the Ointment still lasted"
        print("ok: turned off, the flash takes 10 HP through Ointment, as the original")
        print("screenshots:", ", ".join(p.name for p in g.shots))
    except BaseException:
        if g.running():
            g.shot("failure")
        raise
    finally:
        g.close()


if __name__ == "__main__":
    sys.exit(main())
