"""Check that features do not collide over hook sites or ROM (mods/allocations.toml).

    python3 tools/check_allocations.py [--rom "Faxanadu (USA).nes"]
"""
import argparse
from itertools import combinations
from pathlib import Path
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rom", type=Path, help="also check claimed ROM is unused in the stock image")
    args = ap.parse_args()
    allocations = tomllib.loads((ROOT / "mods/allocations.toml").read_text())
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

    claims = [(f["package"], r) for f in features for r in f.get("rom", [])]
    shared_rom = allocations.get("shared_rom", [])

    def shared(pa, a, pb, b):
        """A [[shared_rom]] covers both claims and names both features."""
        return any(sr["bank"] == a["bank"] and {pa, pb} <= set(sr["packages"]) and
                   sr["start"] <= min(a["start"], b["start"]) and max(a["end"], b["end"]) <= sr["end"]
                   for sr in shared_rom)

    for (pa, a), (pb, b) in combinations(claims, 2):
        if a["bank"] == b["bank"] and a["start"] < b["end"] and b["start"] < a["end"] and not shared(pa, a, pb, b):
            errors.append(f"bank {a['bank']} ${a['start']:04X}-${a['end'] - 1:04X} ({pa}: {a['use']}) overlaps "
                          f"${b['start']:04X}-${b['end'] - 1:04X} ({pb}: {b['use']})")
    if args.rom:
        image = args.rom.read_bytes()
        prg = image[16 + (512 if image[6] & 4 else 0):]
        for package, r in claims:
            base = r["bank"] * 0x4000 - 0x8000
            used = [a for a in range(r["start"], r["end"]) if prg[base + a] != 0xFF]
            if used:
                errors.append(f"{package}: bank {r['bank']} ${used[0]:04X} is not unused in the stock ROM")

    for e in errors:
        print("allocation:", e, file=sys.stderr)
    if errors:
        return 1
    print(f"allocations ok: {len(features)} features, {len(sites)} hook sites, {len(claims)} ROM claims")
    return 0


if __name__ == "__main__":
    sys.exit(main())
