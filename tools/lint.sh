#!/usr/bin/env bash
# clang-tidy on the repo's own C: every src/ file the game builds, with the
# checks .clang-tidy lists (docs/C_TOOLING.md). Any finding fails it.
# tools/test.sh runs it.
#
#   tools/lint.sh                    # every src/ file in build-cycle's build
#   tools/lint.sh src/fax_map.c ...  # those files
#   LINT_BUILD=build-asan tools/lint.sh
#
# It reads the files' compile commands from the game's build (configured by
# tools/test.sh, or as CLAUDE.md shows), so it lints what the build compiles.
# Run inside the Nix devShell, whose clang-tidy is the same everywhere.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"
BUILD="${LINT_BUILD:-build-cycle}"
DB="$BUILD/compile_commands.json"
[ -f "$DB" ] || { echo "No $DB: configure the game first (CLAUDE.md, Build)" >&2; exit 2; }
if [ $# -gt 0 ]; then
  files=("$@")
else
  mapfile -t files < <(python3 -c '
import json, os, sys
src = os.path.join(os.getcwd(), "src") + os.sep
print("\n".join(sorted(e["file"] for e in json.load(open(sys.argv[1])) if e["file"].startswith(src))))' "$DB")
fi
[ "${#files[@]}" -gt 0 ] || { echo "No src/ files in $DB" >&2; exit 2; }
# A file at a time, a thread each. clang-tidy prints its findings on stdout
# and its counts ("N warnings generated.") on stderr: only the findings show.
printf '%s\0' "${files[@]}" |
  xargs -0 -n 1 -P "${JOBS:-$(nproc 2>/dev/null || echo 4)}" clang-tidy -p "$BUILD" --quiet 2> >(grep -v -E '^[0-9]+ (warning|error)s? (and [0-9]+ (warning|error)s? )?generated\.$' >&2) ||
  { echo "== lint: clang-tidy found the above (.clang-tidy; docs/C_TOOLING.md)" >&2; exit 1; }
echo "lint ok: clang-tidy on ${#files[@]} files"
