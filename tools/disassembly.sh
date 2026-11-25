#!/usr/bin/env bash
# Fetch chipx86's annotated Faxanadu disassembly, assemble it, check it
# rebuilds your ROM byte for byte, and write every label's address.
#
#   tools/disassembly.sh [ROM]      # ROM: "Faxanadu (USA).nes" at the repo root
#
# Output in build-tests/disassembly/: src/ (the annotated ca65 sources, the
# place to read the game's code) and labels.txt ("al 00DD61 .Game_SpawnInTemple";
# the same address can name RAM or a routine in any bank). Needs git and cc65
# (both in the Nix devShell). Mesen's .mlb in that repo is older than the
# sources; trust labels.txt.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
ROM="${1:-$REPO/Faxanadu (USA).nes}"
OUT="$REPO/build-tests/disassembly"
[ -f "$ROM" ] || { echo "No ROM at $ROM" >&2; exit 2; }
if [ ! -d "$OUT/repo" ]; then
  git clone -q --depth 1 https://github.com/chipx86/faxanadu.git "$OUT/repo"
fi
rm -rf "$OUT/src" && cp -r "$OUT/repo/src" "$OUT/src"
# The sources were written on a case-insensitive file system. On one (macOS's
# by default) HEADER.asm already is header.asm, and a link would replace it.
[ -e "$OUT/src/HEADER.asm" ] || ln -s header.asm "$OUT/src/HEADER.asm"
(cd "$OUT/src" && ca65 -g faxanadu.asm -o ../faxanadu.o && ld65 -C ld65.cfg -o ../faxanadu.nes -Ln ../labels.txt ../faxanadu.o)
python3 - "$OUT/faxanadu.nes" "$ROM" <<'PY'
import sys, zlib
built, rom = (open(p, "rb").read() for p in sys.argv[1:])
skip = 16 + (512 if rom[6] & 4 else 0)
assert built[16:] == rom[skip:], "the disassembly does not rebuild this ROM"
print(f"disassembly rebuilds the ROM byte for byte (CRC32 {zlib.crc32(built[16:]):08X})")
PY
echo "labels: $OUT/labels.txt ($(wc -l < "$OUT/labels.txt") symbols); sources: $OUT/src"
