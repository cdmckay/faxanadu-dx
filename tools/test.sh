#!/usr/bin/env bash
# Run every check this repo has. They run locally, not in CI: the build
# recompiles your ROM, which cannot be distributed.
#
#   tools/test.sh                 # ROM: "Faxanadu (USA).nes" at the repo root
#   tools/test.sh --rom PATH      # another copy of the same ROM
#   tools/test.sh --quick         # skip the end-to-end game runs
#
# Run inside the Nix devShell (direnv, or nix develop). The game build in
# build-cycle is configured on first use.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
ROM="$REPO/Faxanadu (USA).nes"
QUICK=0
while [ $# -gt 0 ]; do
  case "$1" in
    --rom) ROM="$(realpath "$2")"; shift 2;;
    --quick) QUICK=1; shift;;
    -h|--help) sed -n '2,10p' "$0"; exit 0;;
    *) echo "unknown arg: $1" >&2; exit 2;;
  esac
done
[ -f "$ROM" ] || { echo "No ROM at $ROM; see README, Building from Source" >&2; exit 2; }
cd "$REPO"
JOBS="$(nproc 2>/dev/null || echo 4)"
OUT="$REPO/build-tests"

echo "== allocations"
python3 tools/check_allocations.py --rom "$ROM"
python3 tests/allocations/check_allocations_test.py --rom "$ROM"

echo "== unit tests"
cmake -S tests/travel -B "$OUT/travel" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/travel" -j"$JOBS" >/dev/null
"$OUT/travel/travel_test"
cmake -S tests/text_speed -B "$OUT/text_speed" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/text_speed" -j"$JOBS" >/dev/null
"$OUT/text_speed/text_speed_test"
cmake -S tests/world -B "$OUT/world" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/world" -j"$JOBS" >/dev/null && echo "fax_world.c compiles without SDL"
cmake -S tests/death -B "$OUT/death" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/death" -j"$JOBS" >/dev/null
"$OUT/death/death_test"
cmake -S tests/pendant -B "$OUT/pendant" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/pendant" -j"$JOBS" >/dev/null
"$OUT/pendant/pendant_test"
cmake -S tests/magic -B "$OUT/magic" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/magic" -j"$JOBS" >/dev/null
"$OUT/magic/magic_test"
cmake -S tests/zones -B "$OUT/zones" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/zones" -j"$JOBS" >/dev/null
"$OUT/zones/zones_test"
cmake -S tests/keys -B "$OUT/keys" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/keys" -j"$JOBS" >/dev/null
"$OUT/keys/keys_test"
cmake -S tests/movement -B "$OUT/movement" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/movement" -j"$JOBS" >/dev/null
"$OUT/movement/pose_test"
"$OUT/movement/jumps_test"
cmake -S tests/speaker -B "$OUT/speaker" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/speaker" -j"$JOBS" >/dev/null
"$OUT/speaker/speaker_test"
cmake -S tests/dagger -B "$OUT/dagger" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/dagger" -j"$JOBS" >/dev/null
"$OUT/dagger/dagger_test"

echo "== game build"
[ -f build-cycle/CMakeCache.txt ] || cmake -S . -B build-cycle -DCMAKE_BUILD_TYPE=Release -DNESRECOMP_ROM="$ROM" >/dev/null
cmake --build build-cycle -j"$JOBS" >/dev/null
build-cycle/FaxanaduRecomp "$ROM" --frames 600 --no-save | grep "mode="

if [ "$QUICK" = 1 ]; then echo "(end-to-end runs skipped: --quick)"; exit 0; fi
echo "== end to end: the shared helper's port reservation and relaunch"
python3 tests/e2e/fax_e2e_test.py --build build-cycle --rom "$ROM" --out "$OUT/e2e-helper"
echo "== end to end: Guru travel, by default and with Seamless on, in parallel"
e2e() {  # NAME [ARGS...]: one run, in its own folder, on a free port
  local name="$1"; shift
  python3 tests/travel/guru_travel.py --build build-cycle --rom "$ROM" --out "$OUT/$name" "$@" \
    > "$OUT/$name.log" 2>&1
}
e2e travel-e2e & default=$!
e2e travel-e2e-seamless --game-arg=--world-view=seamless & seamless=$!
echo "== end to end: death keeps gold, alongside them"
python3 tests/death/death_keeps_gold.py --build build-cycle --rom "$ROM" --out "$OUT/death-e2e" \
  > "$OUT/death-e2e.log" 2>&1 & death=$!
echo "== end to end: the Pendant fix, alongside them"
python3 tests/pendant/pendant_fix.py --build build-cycle --rom "$ROM" --out "$OUT/pendant-e2e" \
  > "$OUT/pendant-e2e.log" 2>&1 & pendant=$!
echo "== end to end: the Magic button, alongside them"
python3 tests/magic/magic_button.py --build build-cycle --rom "$ROM" --out "$OUT/magic-e2e" \
  > "$OUT/magic-e2e.log" 2>&1 & magic=$!
echo "== end to end: zone names, by default and with Seamless on, alongside them"
zones_e2e() {  # NAME [ARGS...]
  local name="$1"; shift
  python3 tests/zones/zone_names.py --build build-cycle --rom "$ROM" --out "$OUT/$name" "$@" > "$OUT/$name.log" 2>&1
}
zones_e2e zones-e2e & zones=$!
zones_e2e zones-e2e-seamless --game-arg=--world-view=seamless & zones_seamless=$!
echo "== end to end: auto keys and reusable tools, alongside them"
python3 tests/keys/auto_and_reusable_keys.py --build build-cycle --rom "$ROM" --out "$OUT/keys-e2e" \
  > "$OUT/keys-e2e.log" 2>&1 & keys=$!
echo "== end to end: the falling pose and forgiving jumps, alongside them"
python3 tests/movement/falling_pose.py --build build-cycle --rom "$ROM" --out "$OUT/pose-e2e" \
  > "$OUT/pose-e2e.log" 2>&1 & pose=$!
python3 tests/movement/forgiving_jumps.py --build build-cycle --rom "$ROM" --out "$OUT/jumps-e2e" \
  > "$OUT/jumps-e2e.log" 2>&1 & jumps=$!
echo "== end to end: speaker names, by default and with Seamless on, alongside them"
speaker_e2e() {  # NAME [ARGS...]
  local name="$1"; shift
  python3 tests/speaker/speaker_names.py --build build-cycle --rom "$ROM" --out "$OUT/$name" "$@" > "$OUT/$name.log" 2>&1
}
speaker_e2e speaker-e2e & speaker=$!
speaker_e2e speaker-e2e-seamless --game-arg=--world-view=seamless & speaker_seamless=$!
echo "== end to end: Hold A to speed up text, alongside them"
python3 tests/text_speed/text_speed.py --build build-cycle --rom "$ROM" --out "$OUT/text-speed-e2e" \
  > "$OUT/text-speed-e2e.log" 2>&1 & text_speed=$!
echo "== end to end: the taller dagger, alongside them"
python3 tests/dagger/taller_dagger.py --build build-cycle --rom "$ROM" --out "$OUT/dagger-e2e" \
  > "$OUT/dagger-e2e.log" 2>&1 & dagger=$!
failed=0
wait "$default" || failed=1
sed 's/^/[default]  /' "$OUT/travel-e2e.log"
wait "$seamless" || failed=1
sed 's/^/[seamless] /' "$OUT/travel-e2e-seamless.log"
wait "$death" || failed=1
sed 's/^/[death]    /' "$OUT/death-e2e.log"
wait "$pendant" || failed=1
sed 's/^/[pendant]  /' "$OUT/pendant-e2e.log"
wait "$magic" || failed=1
sed 's/^/[magic]    /' "$OUT/magic-e2e.log"
wait "$zones" || failed=1
sed 's/^/[zones]    /' "$OUT/zones-e2e.log"
wait "$zones_seamless" || failed=1
sed 's/^/[zones+sw] /' "$OUT/zones-e2e-seamless.log"
wait "$keys" || failed=1
sed 's/^/[keys]     /' "$OUT/keys-e2e.log"
wait "$pose" || failed=1
sed 's/^/[pose]     /' "$OUT/pose-e2e.log"
wait "$jumps" || failed=1
sed 's/^/[jumps]    /' "$OUT/jumps-e2e.log"
wait "$speaker" || failed=1
sed 's/^/[speaker]  /' "$OUT/speaker-e2e.log"
wait "$speaker_seamless" || failed=1
sed 's/^/[spkr+sw]  /' "$OUT/speaker-e2e-seamless.log"
wait "$text_speed" || failed=1
sed 's/^/[text]     /' "$OUT/text-speed-e2e.log"
wait "$dagger" || failed=1
sed 's/^/[dagger]   /' "$OUT/dagger-e2e.log"
[ "$failed" = 0 ] || { echo "end-to-end tests failed; screenshots in $OUT/*-e2e*/" >&2; exit 1; }
echo "all checks passed"
