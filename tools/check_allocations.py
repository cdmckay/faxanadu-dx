"""Check that features do not collide over hook sites or ROM (mods/allocations.toml).

    python3 tools/check_allocations.py [--rom "Faxanadu (USA).nes"] [--allocations FILE]

Every feature of the remaster is a plugin of one launcher switch, so any two
features are enabled together unless their manifest features share an
exclusive group.
"""
import argparse
from itertools import combinations
from pathlib import Path
import re
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[1]
OVERRIDES_SOURCE = ROOT / "nesrecomp/runner/cyc/cyc_mod.c"   # CYC_MOD_READ_OVERRIDES_MAX
HOOKS_SOURCE = ROOT / "nesrecomp/runner/src/mod_function_hooks.c"   # NES_MOD_MAX_FUNCTION_HOOKS


def rom_claims(features):
    """Every ROM claim as (feature, claim). `rom` claims unused ($FF) bytes;
    `overrides` claims existing bytes a feature serves with other values, from
    `start`, one per byte of its `stock` list. Both get an `end`."""
    claims = [(f["id"], dict(r, stock=None)) for f in features for r in f.get("rom", [])]
    for f in features:
        for r in f.get("overrides", []):
            claims.append((f["id"], dict(r, end=r["start"] + len(r["stock"]))))
    return claims


def shared(shared_rom, fa, a, fb, b):
    """A [[shared_rom]] entry covers both claims and names both features."""
    return any(sr["bank"] == a["bank"] == b["bank"] and {fa, fb} <= set(sr["features"]) and
               sr["start"] <= min(a["start"], b["start"]) and max(a["end"], b["end"]) <= sr["end"]
               for sr in shared_rom)


def rom_errors(claims, prg=None, shared_rom=()):
    """Overrides that claim no byte or list a value that is not one, claims
    that overlap, whatever their kind, unless a [[shared_rom]] says why two
    features may share them, [[shared_rom]] entries that name a feature with
    no claim in them, and with the stock PRG image, unused claims that are not
    $FF and overrides whose bytes are not their stock values."""
    errors = []
    for feature, r in claims:
        if r["stock"] is not None and (not r["stock"] or any(not 0 <= v <= 0xFF for v in r["stock"])):
            errors.append(f"{feature}: the override at bank {r['bank']} ${r['start']:04X} needs one stock byte "
                          f"(0-255) per byte it claims, not {r['stock']}")
    for (fa, a), (fb, b) in combinations(claims, 2):
        if a["bank"] == b["bank"] and a["start"] < b["end"] and b["start"] < a["end"] and \
                not shared(shared_rom, fa, a, fb, b):
            errors.append(f"bank {a['bank']} ${a['start']:04X}-${a['end'] - 1:04X} ({fa}: {a['use']}) overlaps "
                          f"${b['start']:04X}-${b['end'] - 1:04X} ({fb}: {b['use']})")
    for sr in shared_rom:
        for feature in sr["features"]:
            if not any(f == feature and r["bank"] == sr["bank"] and r["start"] < sr["end"] and sr["start"] < r["end"]
                       for f, r in claims):
                errors.append(f"the [[shared_rom]] at bank {sr['bank']} ${sr['start']:04X} names {feature}, "
                              "which claims none of it")
    if prg is None:
        return errors
    for feature, r in claims:
        base = r["bank"] * 0x4000 - 0x8000
        if r["stock"] is None:
            used = [a for a in range(r["start"], r["end"]) if prg[base + a] != 0xFF]
            if used:
                errors.append(f"{feature}: bank {r['bank']} ${used[0]:04X} is not unused in the stock ROM")
            continue
        for a, want in zip(range(r["start"], r["end"]), r["stock"]):
            if prg[base + a] != want:
                errors.append(f"{feature}: bank {r['bank']} ${a:04X} is ${prg[base + a]:02X} in the stock ROM, "
                              f"not the ${want:02X} its override expects")
    return errors


def override_budget():
    """The read-override table's size, CYC_MOD_READ_OVERRIDES_MAX in nesrecomp's cyc_mod.c."""
    found = re.search(r"#define\s+CYC_MOD_READ_OVERRIDES_MAX\s+(\d+)", OVERRIDES_SOURCE.read_text())
    if not found:
        raise SystemExit(f"allocation: no CYC_MOD_READ_OVERRIDES_MAX in {OVERRIDES_SOURCE}")
    return int(found.group(1))


def hook_budget():
    """The function-hook registry's size, NES_MOD_MAX_FUNCTION_HOOKS in nesrecomp's mod_function_hooks.c: a
    feature's constructor aborts the game when its hook does not fit."""
    found = re.search(r"#define\s+NES_MOD_MAX_FUNCTION_HOOKS\s+(\d+)", HOOKS_SOURCE.read_text())
    if not found:
        raise SystemExit(f"allocation: no NES_MOD_MAX_FUNCTION_HOOKS in {HOOKS_SOURCE}")
    return int(found.group(1))


def served(claims):
    """The PRG bytes the claims serve, each once however many features serve it."""
    return {(r["bank"], a) for _, r in claims for a in range(r["start"], r["end"])}


def budget_errors(claims, budget):
    """Every claimed byte is served through a read override, one table entry
    per byte however many features serve it, and every feature can be on at
    once: the bytes claimed must fit in the table."""
    used = len(served(claims))
    if used > budget:
        return [f"the features claim {used} ROM bytes, all served through read overrides, "
                f"but the table has {budget} entries"]
    return []


def site_errors(sites, owner, together, shared_sites):
    """Addresses that two features enabled together hook, without a
    [[shared_site]] listing exactly those hooks in the order they run (the
    order game.toml declares them: cyc_hooks_fire walks the sites in that
    order), and [[shared_site]] entries that match no such address."""
    errors = []
    by_addr = {}
    for site in sites:
        by_addr.setdefault(site["addr"], []).append(site["id"])
    declared = {s["addr"]: list(s["hooks"]) for s in shared_sites}
    if len(declared) != len(shared_sites):
        errors.append("two [[shared_site]] entries name the same address; make them one")
    needed = set()
    for addr, ids in by_addr.items():
        if not any(together(owner.get(a), owner.get(b)) for a, b in combinations(ids, 2)):
            continue
        needed.add(addr)
        if addr not in declared:
            errors.append(f"${addr:04X} is hooked by {', '.join(ids)}, which are enabled together; "
                          "add a [[shared_site]] saying why that is safe")
        elif declared[addr] != ids:
            errors.append(f"the [[shared_site]] at ${addr:04X} lists {', '.join(declared[addr])}, but the sites "
                          f"there, in the order they run (game.toml's), are {', '.join(ids)}; "
                          "list them so, and check that its reason still holds")
    for addr in sorted(set(declared) - needed):
        errors.append(f"the [[shared_site]] at ${addr:04X} covers no address that two features hook; remove it")
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
    errors = []

    # The manifests: the manifest feature that switches each plugin, and each
    # manifest feature's exclusive groups.
    plugins = {}   # (package, plugin) -> (package, manifest feature)
    groups = {}    # (package, manifest feature) -> its exclusive groups
    for manifest in sorted((ROOT / "mods/preloaded/packages").glob("*/*/manifest.toml")):
        data = tomllib.loads(manifest.read_text())
        for feature in data.get("feature", []):
            found = set(feature.get("exclusive_groups", []))
            if "exclusive_group" in feature:
                found.add(feature["exclusive_group"])
            groups[(data["id"], feature["id"])] = found
        for plugin in data.get("plugin", []):
            plugins[(data["id"], plugin["id"])] = (data["id"], plugin["feature"])

    ids = [f["id"] for f in features]
    for dup in sorted({i for i in ids if ids.count(i) > 1}):
        errors.append(f"two features are called {dup}")
    switch = {}    # feature -> the manifest feature that switches it
    for f in features:
        key = (f["package"], f["plugin"])
        if key in plugins:
            switch[f["id"]] = plugins[key]
        else:
            errors.append(f"{f['id']}: the manifest of {f['package']} under mods/preloaded/packages has no "
                          f"plugin {f['plugin']}")
        if not (ROOT / f["source"]).exists():
            errors.append(f"{f['id']}: source {f['source']} does not exist")
    for package, plugin in sorted(set(plugins) - {(f["package"], f["plugin"]) for f in features}):
        errors.append(f"{package}'s plugin {plugin} belongs to no feature in mods/allocations.toml; "
                      "declare what it takes there")

    owner = {}
    for site in sites:
        mine = [f["id"] for f in features if site["id"].startswith(f["hooks"])]
        if len(mine) != 1:
            errors.append(f"hook site {site['id']} belongs to {len(mine)} features; give it one prefix owner")
        else:
            owner[site["id"]] = mine[0]

    def together(a, b):
        """Two different features that can be enabled at once: switched by the
        same manifest feature, or by two that share no exclusive group."""
        if a is None or b is None or a == b:
            return False
        sa, sb = switch.get(a), switch.get(b)
        return sa == sb or not (groups.get(sa, set()) & groups.get(sb, set()))

    errors += site_errors(sites, owner, together, allocations.get("shared_site", []))

    prg = None
    if args.rom:
        image = args.rom.read_bytes()
        prg = image[16 + (512 if image[6] & 4 else 0):]
    claims = rom_claims(features)
    errors += rom_errors(claims, prg, allocations.get("shared_rom", []))
    budget = override_budget()
    errors += budget_errors(claims, budget)
    hooks = hook_budget()
    if len(sites) > hooks:
        errors.append(f"game.toml has {len(sites)} hook sites, but the engine registers at most {hooks} "
                      f"(NES_MOD_MAX_FUNCTION_HOOKS): the game would abort at start")

    for e in errors:
        print("allocation:", e, file=sys.stderr)
    if errors:
        return 1
    overrides = sum(r["stock"] is not None for _, r in claims)
    print(f"allocations ok: {len(features)} features, {len(sites)} hook sites, "
          f"{len(claims) - overrides} ROM claims, {overrides} overridden, "
          f"{len(served(claims))} of {budget} read overrides, {len(sites)} of {hooks} hooks")
    return 0


if __name__ == "__main__":
    sys.exit(main())
