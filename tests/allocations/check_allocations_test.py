"""Tests for tools/check_allocations.py: ROM claims (unused bytes, `rom`, and
overridden ones, `overrides`), the read-override budget, and shared hook sites.

    python3 tests/allocations/check_allocations_test.py [--rom "Faxanadu (USA).nes"]

Without a ROM the claims are checked against a model PRG image: $FF except
the weapon range table in bank 14, and the sites against a model game.toml.
With --rom, the checker also runs as tools/test.sh runs it, on
mods/allocations.toml and on copies that break it: a wrong stock value for
the dagger's reach, a [[shared_site]] out of game.toml's order or missing, a
feature left out, and a [[shared_rom]] that no longer names Block.
"""
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True   # no __pycache__ in tools/
sys.path.insert(0, str(ROOT / "tools"))
import check_allocations as checker  # noqa: E402

checks = 0


def check(test, what):
    global checks
    checks += 1
    if not test:
        sys.exit(f"FAIL: {what}")


def model_prg():
    """16 banks of $FF, with MAYBE_WEAPON_RANGES at bank 14 $B897 as stock."""
    prg = bytearray(b"\xff" * 16 * 0x4000)
    base = 14 * 0x4000 - 0x8000
    prg[base + 0xB897:base + 0xB89F] = bytes([0x08, 0x04, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08])
    return bytes(prg)


def errors(features, prg=None):
    return checker.rom_errors(checker.rom_claims(features), prg)


def dagger(stock=(0x04,), start=0xB898, feature="dagger"):
    return {"id": feature, "rom": [],
            "overrides": [{"bank": 14, "start": start, "stock": list(stock), "use": "the dagger's Y reach"}]}


def unused(start, end, bank=14, feature="other"):
    return {"id": feature, "rom": [{"bank": bank, "start": start, "end": end, "use": "new data"}]}


def model():
    prg = model_prg()
    check(errors([dagger()], prg) == [], "an override of the stock byte is accepted")
    check(errors([dagger(stock=(0x08, 0x04), start=0xB897)], prg) == [], "a two-byte override is accepted")
    wrong = errors([dagger(stock=(0x08,))], prg)
    check(len(wrong) == 1 and "$B898 is $04 in the stock ROM, not the $08" in wrong[0],
          f"a wrong stock value is rejected: {wrong}")
    check(len(errors([dagger(stock=(0x08, 0x08), start=0xB897)], prg)) == 1, "each byte of a long override is checked")
    check(errors([dagger(stock=(0x08,))]) == [], "stock values are only checked against a ROM")
    empty = errors([dagger(stock=())])
    check(len(empty) == 1 and "needs one stock byte" in empty[0], f"an override of no bytes is rejected: {empty}")
    check(len(errors([dagger(stock=(0x104,))])) == 1, "a stock value that is not a byte is rejected")

    check(errors([unused(0xBF00, 0xBF10)], prg) == [], "unused claims are still checked for $FF")
    check(len(errors([unused(0xB890, 0xB8A0)], prg)) == 1, "an unused claim on used ROM is still rejected")
    clash = errors([unused(0xBF00, 0xBF10), unused(0xBF0F, 0xBF20, feature="another")])
    check(len(clash) == 1 and "overlaps" in clash[0], f"two unused claims that overlap are still reported: {clash}")

    clash = errors([dagger(), unused(0xB890, 0xB8A0)])
    check(len(clash) == 1 and "overlaps" in clash[0], f"an override overlapping an unused claim is reported: {clash}")
    clash = errors([dagger(), dagger(feature="another")])
    check(len(clash) == 1 and "overlaps" in clash[0], "two overrides of one byte are reported")
    check(len(errors([dagger(stock=(0x08, 0x04), start=0xB897), unused(0xB898, 0xB899)])) == 1,
          "a long override's whole range counts for overlaps")
    check(errors([dagger(), unused(0xB898, 0xB899, bank=13)]) == [], "the same address in another bank is no overlap")

    slot = [{"bank": 13, "start": 0xB45B, "end": 0xB48B, "features": ["travel", "files"], "why": "one line slot"}]
    pair = [(f, dict(r, stock=None)) for f, r in (("travel", {"bank": 13, "start": 0xB45B, "end": 0xB48B, "use": "a"}),
                                                  ("files", {"bank": 13, "start": 0xB45B, "end": 0xB48B, "use": "b"}))]
    check(checker.rom_errors(pair, None, slot) == [], "a [[shared_rom]] lets the two features it names share it")
    check(len(checker.rom_errors(pair, None)) == 1, "without it they overlap")
    third = [(f if f == "travel" else "other", r) for f, r in pair]
    got = checker.rom_errors(third, None, slot)
    check(len(got) == 2 and "overlaps" in got[0] and "names files, which claims none of it" in got[1],
          f"a feature it does not name still overlaps, and one it names that claims none of it is stale: {got}")
    wider = [(f, dict(r, end=0xB490)) for f, r in pair]
    check(len(checker.rom_errors(wider, None, slot)) == 1, "a claim past it still overlaps")

    # The read-override budget: one entry per byte served, however many serve it.
    claims = checker.rom_claims([unused(0xBF00, 0xBF40), unused(0xBF00, 0xBF40, feature="another")])
    check(checker.budget_errors(claims, 64) == [], "64 bytes that two features serve take 64 entries")
    claims = checker.rom_claims([unused(0xBF00, 0xBF40), dagger()])
    over = checker.budget_errors(claims, 64)
    check(len(over) == 1 and "claim 65 ROM bytes" in over[0], f"65 bytes do not fit 64 entries: {over}")
    check(checker.budget_errors(claims[:1], 64) == [], "64 do")

    # Shared sites: every two features are together but those the model keeps apart.
    sites = [{"id": "a.one", "addr": 0x8000}, {"id": "b.one", "addr": 0x8000}, {"id": "a.two", "addr": 0x9000},
             {"id": "a.three", "addr": 0x9000}, {"id": "c.one", "addr": 0xA000}, {"id": "a.four", "addr": 0xA000}]
    owner = {s["id"]: s["id"].split(".")[0] for s in sites}
    apart = {frozenset("ac")}
    together = lambda x, y: x != y and frozenset((x, y)) not in apart   # noqa: E731
    declared = [{"addr": 0x8000, "hooks": ["a.one", "b.one"], "why": "safe"}]
    check(checker.site_errors(sites, owner, together, declared) == [],
          "two features at one address, declared in game.toml's order, pass; one feature's two sites need nothing")
    got = checker.site_errors(sites, owner, together, [])
    check(len(got) == 1 and "$8000 is hooked by a.one, b.one" in got[0], f"undeclared, they fail: {got}")
    got = checker.site_errors(sites, owner, together, [dict(declared[0], hooks=["b.one", "a.one"])])
    check(len(got) == 1 and "in the order they run" in got[0], f"declared out of game.toml's order, they fail: {got}")
    got = checker.site_errors(sites, owner, together, [dict(declared[0], hooks=["a.one"])])
    check(len(got) == 1 and "lists a.one" in got[0], f"declared with a hook missing, they fail: {got}")
    stale = declared + [{"addr": 0xA000, "hooks": ["c.one", "a.four"], "why": "never together"}]
    got = checker.site_errors(sites, owner, together, stale)
    check(len(got) == 1 and "$A000 covers no address" in got[0], f"a declaration nothing needs fails: {got}")
    got = checker.site_errors(sites, owner, lambda x, y: x != y, declared)
    check(len(got) == 1 and "$A000 is hooked by c.one, a.four" in got[0],
          f"features that can be on together need one: {got}")


def cli(rom):
    run = lambda path: subprocess.run([sys.executable, str(ROOT / "tools/check_allocations.py"), "--rom", str(rom),   # noqa: E731
                                       "--allocations", str(path)], capture_output=True, text=True)
    real = ROOT / "mods/allocations.toml"
    ok = run(real)
    check(ok.returncode == 0, f"mods/allocations.toml passes against the ROM: {ok.stderr}")
    text = real.read_text()

    def broken(old, new, what):
        check(old in text, f"mods/allocations.toml has {old!r}")
        with tempfile.TemporaryDirectory() as tmp:
            bad = Path(tmp) / "allocations.toml"
            bad.write_text(text.replace(old, new, 1))
            failed = run(bad)
        check(failed.returncode == 1 and what in failed.stderr, f"{what!r} fails the checker: {failed.stderr}")
        return failed.stderr

    broken("start = 0xB898, stock = [0x04]", "start = 0xB898, stock = [0x08]", "$B898 is $04 in the stock ROM, not the $08")
    sugata_first = 'hooks = ["faxanadu.sugata.hit", "faxanadu.block.flash"]'
    broken(sugata_first, 'hooks = ["faxanadu.block.flash", "faxanadu.sugata.hit"]', "in the order they run")
    broken(sugata_first, 'hooks = []', "lists , but the sites")
    shared_rom = 'features = ["guru-travel", "save-files", "block", "quest-log", "recharging-items", "magician-lesson"]'
    broken(shared_rom, 'features = ["guru-travel", "save-files", "quest-log", "recharging-items", "magician-lesson"]',
           "(block: message $C3, the line slot")
    feature = re.search(r'\[\[feature\]\]\nid = "pendant-fix".*?(?=\n\[\[)', text, re.S).group(0)
    err = broken(feature, "", "faxanadu.remastered's plugin faxanadu.pendant-fix belongs to no feature")
    check("hook site faxanadu.pendant-fix.check-glove belongs to 0 features" in err, err)
    broken('plugin = "faxanadu.block"', 'plugin = "faxanadu.shield"', "has no plugin faxanadu.shield")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rom", type=Path, help="also run the checker on the real allocations against this ROM")
    args = ap.parse_args()
    model()
    if args.rom:
        cli(args.rom)
    print(f"check_allocations_test: {checks} checks passed")


if __name__ == "__main__":
    main()
