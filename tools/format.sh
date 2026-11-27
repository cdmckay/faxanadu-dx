#!/usr/bin/env bash
# clang-format on the repo's own C, every .c and .h in src/ and tests/, to
# .clang-format (docs/C_TOOLING.md). Not the submodules, nor the recompiled
# code. It runs the devShell's clang-format, $FAX_CLANG_FORMAT, so every
# machine formats alike.
#
#   tools/format.sh             # format them all, in place
#   tools/format.sh FILE...     # those files
#   tools/format.sh --check     # change nothing: name any not formatted, and
#                               # fail (tools/test.sh runs this)
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"
FORMAT="${FAX_CLANG_FORMAT:?run in the Nix devShell (direnv, or nix develop): it names the clang-format to use}"
CHECK=0
if [ "${1:-}" = --check ]; then CHECK=1; shift; fi
if [ $# -gt 0 ]; then
  files=("$@")
else
  mapfile -t files < <(git ls-files --cached --others --exclude-standard 'src/*.c' 'src/*.h' 'tests/*.c' 'tests/*.h')
fi
[ "${#files[@]}" -gt 0 ] || { echo "No C files to format" >&2; exit 2; }
if [ "$CHECK" = 0 ]; then
  # Twice: clang-format aligns a comment after a closing brace differently
  # on its second pass, then stays put.
  "$FORMAT" -i "${files[@]}"
  "$FORMAT" -i "${files[@]}"
  exit 0
fi
out=$("$FORMAT" --dry-run -Werror "${files[@]}" 2>&1) && { echo "format ok: ${#files[@]} files"; exit 0; }
mapfile -t bad < <(sed -n 's/^\(.*\):[0-9]*:[0-9]*: error: code should be clang-formatted.*/\1/p' <<< "$out" |
                   sed "s|^$REPO/||" | sort -u)
if [ "${#bad[@]}" = 0 ]; then echo "$out" >&2; echo "== format: clang-format failed (above)" >&2; exit 1; fi
{
  echo "== format: ${#bad[@]} file(s) not formatted to .clang-format:"
  printf '  %s\n' "${bad[@]}"
  echo "Format them with: tools/format.sh ${bad[*]}"
} >&2
exit 1
