"""Check that features do not collide over hook sites or ROM (mods/allocations.toml).

    python3 tools/check_allocations.py [--rom "Faxanadu (USA).nes"] [--allocations FILE]
"""
import argparse
from itertools import combinations
from pathlib import Path
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[1]


def rom_claims(features):
    """Every ROM claim as (package, claim). `rom` claims unused ($FF) bytes;
    `overrides` claims existing bytes a feature serves with other values, from
    `start`, one per byte of its `stock` list. Both get an `end`."""
    claims = [(f["package"], dict(r, stock=None)) for f in features for r in f.get("rom", [])]
    for f in features:
        for r in f.get("overrides", []):
            claims.append((f["package"], dict(r, end=r["start"] + len(r["stock"]))))
    return claims


def shared(shared_rom, pa, a, pb, b):
    """A [[shared_rom]] entry covers both claims and names both features."""
    return any(sr["bank"] == a["bank"] == b["bank"] and {pa, pb} <= set(sr["packages"]) and
               sr["start"] <= min(a["start"], b["start"]) and max(a["end"], b["end"]) <= sr["end"]
               for sr in shared_rom)


def rom_errors(claims, prg=None, shared_rom=()):
    """Overrides that claim no byte or list a value that is not one, claims
    that overlap, whatever their kind, unless a [[shared_rom]] says why two
    features may share them, and with the stock PRG image, unused claims that
    are not $FF and overrides whose bytes are not their stock values."""
    errors = []
    for package, r in claims:
        if r["stock"] is not None and (not r["stock"] or any(not 0 <= v <= 0xFF for v in r["stock"])):
            errors.append(f"{package}: the override at bank {r['bank']} ${r['start']:04X} needs one stock byte "
                          f"(0-255) per byte it claims, not {r['stock']}")
    for (pa, a), (pb, b) in combinations(claims, 2):
        if a["bank"] == b["bank"] and a["start"] < b["end"] and b["start"] < a["end"] and \
                not shared(shared_rom, pa, a, pb, b):
            errors.append(f"bank {a['bank']} ${a['start']:04X}-${a['end'] - 1:04X} ({pa}: {a['use']}) overlaps "
                          f"${b['start']:04X}-${b['end'] - 1:04X} ({pb}: {b['use']})")
    if prg is None:
        return errors
    for package, r in claims:
        base = r["bank"] * 0x4000 - 0x8000
        if r["stock"] is None:
            used = [a for a in range(r["start"], r["end"]) if prg[base + a] != 0xFF]
            if used:
                errors.append(f"{package}: bank {r['bank']} ${used[0]:04X} is not unused in the stock ROM")
            continue
        for a, want in zip(range(r["start"], r["end"]), r["stock"]):
            if prg[base + a] != want:
                errors.append(f"{package}: bank {r['bank']} ${a:04X} is ${prg[base + a]:02X} in the stock ROM, "
                              f"not the ${want:02X} its override expects")
    return errors


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rom", type=Path, help="also check claimed ROM against the stock image")
    ap.add_argument("--allocations", type=Path, default=ROOT / "mods/allocations.toml",
                    help="the allocations to check (default: mods/allocations.toml)")
    args = ap.parse_args()
    allocations = tomllib.loads(args.allocations.read_text())
    sites = tomllib.loads((ROOT / "game.toml").read_text())["mod_function_hook"]
    features = allocations["feature"]
    groups = {}
    for manifest in (ROOT / "mods/preloaded/packages").glob("*/*/manifest.toml"):
        data = tomllib.loads(manifest.read_text())
        found = set()
        for feature in data.get("feature", []):
            found.update(feature.get("exclusive_groups", []))
            if "exclusive_group" in feature:
                found.add(feature["exclusive_group"])
        groups[data["id"]] = found
    errors = []

    owner = {}
    for site in sites:
        mine = [f["package"] for f in features if site["id"].startswith(f["hooks"])]
        if len(mine) != 1:
            errors.append(f"hook site {site['id']} belongs to {len(mine)} features; give it one prefix owner")
        else:
            owner[site["id"]] = mine[0]
    for f in features:
        if f["package"] not in groups:
            errors.append(f"{f['package']}: no manifest under mods/preloaded/packages")
        if not (ROOT / f["source"]).exists():
            errors.append(f"{f['package']}: source {f['source']} does not exist")

    def together(a, b):
        return a != b and not (groups.get(a, set()) & groups.get(b, set()))

    declared = {(s["addr"], tuple(sorted(s["hooks"]))) for s in allocations.get("shared_site", [])}
    by_addr = {}
    for site in sites:
        by_addr.setdefault(site["addr"], []).append(site["id"])
    for addr, ids in by_addr.items():
        clash = any(together(owner.get(a), owner.get(b)) for a, b in combinations(ids, 2))
        if clash and (addr, tuple(sorted(ids))) not in declared:
            errors.append(f"${addr:04X} is hooked by {', '.join(ids)}, which can be enabled together; "
                          "add a [[shared_site]] saying why that is safe")

    prg = None
    if args.rom:
        image = args.rom.read_bytes()
        prg = image[16 + (512 if image[6] & 4 else 0):]
    claims = rom_claims(features)
    errors += rom_errors(claims, prg, allocations.get("shared_rom", []))

    for e in errors:
        print("allocation:", e, file=sys.stderr)
    if errors:
        return 1
    overrides = sum(r["stock"] is not None for _, r in claims)
    print(f"allocations ok: {len(features)} features, {len(sites)} hook sites, "
          f"{len(claims) - overrides} ROM claims, {overrides} overridden")
    return 0


if __name__ == "__main__":
    sys.exit(main())
