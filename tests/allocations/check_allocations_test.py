"""Tests for tools/check_allocations.py's ROM claims: unused bytes (`rom`) and
overridden ones (`overrides`).

    python3 tests/allocations/check_allocations_test.py [--rom "Faxanadu (USA).nes"]

Without a ROM the claims are checked against a model PRG image: $FF except
the weapon range table in bank 14. With --rom, the checker also runs as
tools/test.sh runs it, on mods/allocations.toml and on a copy whose stock
value for the dagger's reach is wrong.
"""
import argparse
from pathlib import Path
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


def dagger(stock=(0x04,), start=0xB898, package="dagger"):
    return {"package": package, "rom": [],
            "overrides": [{"bank": 14, "start": start, "stock": list(stock), "use": "the dagger's Y reach"}]}


def unused(start, end, bank=14, package="other"):
    return {"package": package, "rom": [{"bank": bank, "start": start, "end": end, "use": "new data"}]}


def model():
    prg = model_prg()
    check(errors([dagger()], prg) == [], "an override of the stock byte is accepted")
    check(errors([dagger(stock=(0x08, 0x04), start=0xB897)], prg) == [], "a two-byte override is accepted")
    wrong = errors([dagger(stock=(0x08,))], prg)
    check(len(wrong) == 1 and "$B898 is $04 in the stock ROM, not the $08" in wrong[0],
          f"a wrong stock value is rejected: {wrong}")
    check(len(errors([dagger(stock=(0x08, 0x08), start=0xB897)], prg)) == 1, "each byte of a long override is checked")
    check(errors([dagger(stock=(0x08,))]) == [], "stock values are only checked against a ROM")

    check(errors([unused(0xBF00, 0xBF10)], prg) == [], "unused claims are still checked for $FF")
    check(len(errors([unused(0xB890, 0xB8A0)], prg)) == 1, "an unused claim on used ROM is still rejected")

    clash = errors([dagger(), unused(0xB890, 0xB8A0)])
    check(len(clash) == 1 and "overlaps" in clash[0], f"an override overlapping an unused claim is reported: {clash}")
    clash = errors([dagger(), dagger(package="another")])
    check(len(clash) == 1 and "overlaps" in clash[0], "two overrides of one byte are reported")
    check(len(errors([dagger(stock=(0x08, 0x04), start=0xB897), unused(0xB898, 0xB899)])) == 1,
          "a long override's whole range counts for overlaps")
    check(errors([dagger(), unused(0xB898, 0xB899, bank=13)]) == [], "the same address in another bank is no overlap")


def cli(rom):
    run = lambda path: subprocess.run([sys.executable, str(ROOT / "tools/check_allocations.py"), "--rom", str(rom),
                                       "--allocations", str(path)], capture_output=True, text=True)
    real = ROOT / "mods/allocations.toml"
    ok = run(real)
    check(ok.returncode == 0, f"mods/allocations.toml passes against the ROM: {ok.stderr}")
    text = real.read_text()
    claim = "start = 0xB898, stock = [0x04]"
    check(claim in text, "mods/allocations.toml claims the dagger's reach")
    with tempfile.TemporaryDirectory() as tmp:
        bad = Path(tmp) / "allocations.toml"
        bad.write_text(text.replace(claim, "start = 0xB898, stock = [0x08]"))
        failed = run(bad)
    check(failed.returncode == 1 and "$B898 is $04 in the stock ROM, not the $08" in failed.stderr,
          f"a wrong stock value fails the checker: {failed.stderr}")


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
