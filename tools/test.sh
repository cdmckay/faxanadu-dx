#!/usr/bin/env bash
# Run every check this repo has. They run locally, not in CI: the build
# recompiles your ROM, which cannot be distributed.
#
#   tools/test.sh                 # ROM: "Faxanadu (USA).nes" at the repo root
#   tools/test.sh --rom PATH      # another copy of the same ROM
#   tools/test.sh --quick         # skip the end-to-end game runs
#
# Run inside the Nix devShell (direnv, or nix develop). The game build in
# build-cycle is configured on first use. The end-to-end runs start every game
# at once, so only one suite on the machine runs them at a time: another
# checkout's suite waits for the lock, then runs.
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
[ -f "$ROM" ] || { echo "No ROM at $ROM; see the README" >&2; exit 2; }
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
cmake -S tests/files -B "$OUT/files" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/files" -j"$JOBS" >/dev/null
FAX_FILES_TEST_DIR="$OUT/files/scratch" "$OUT/files/files_test"
cmake -S tests/speaker -B "$OUT/speaker" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/speaker" -j"$JOBS" >/dev/null
"$OUT/speaker/speaker_test"
cmake -S tests/dagger -B "$OUT/dagger" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/dagger" -j"$JOBS" >/dev/null
"$OUT/dagger/dagger_test"
cmake -S tests/block -B "$OUT/block" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/block" -j"$JOBS" >/dev/null
"$OUT/block/block_test"
cmake -S tests/sugata -B "$OUT/sugata" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/sugata" -j"$JOBS" >/dev/null
"$OUT/sugata/sugata_test"
cmake -S tests/shops -B "$OUT/shops" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/shops" -j"$JOBS" >/dev/null
"$OUT/shops/shops_test"
cmake -S tests/respawn -B "$OUT/respawn" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/respawn" -j"$JOBS" >/dev/null
"$OUT/respawn/respawn_test"
cmake -S tests/recharge -B "$OUT/recharge" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/recharge" -j"$JOBS" >/dev/null
"$OUT/recharge/recharge_test"

echo "== game build"
[ -f build-cycle/CMakeCache.txt ] || cmake -S . -B build-cycle -DCMAKE_BUILD_TYPE=Release -DNESRECOMP_ROM="$ROM" >/dev/null
cmake --build build-cycle -j"$JOBS" >/dev/null
build-cycle/FaxanaduRecomp "$ROM" --frames 600 --no-save | grep "mode="

if [ "$QUICK" = 1 ]; then echo "(end-to-end runs skipped: --quick)"; exit 0; fi
# Two suites at once double every game's time and make the timing tests flaky,
# so wait for any other checkout's end-to-end runs to finish. The lock is held
# on fd 9 until this script exits.
LOCK="${XDG_RUNTIME_DIR:-${TMPDIR:-/tmp}}/faxanadu-remastered-e2e.lock"
if command -v flock >/dev/null; then
  exec 9>"$LOCK"
  flock -n 9 || { echo "== waiting for another suite's end-to-end runs to finish ($LOCK)"; flock 9; }
else
  echo "(no flock: end-to-end runs may overlap another suite's)"
fi
echo "== end to end: the shared helper's port reservation and relaunch"
python3 tests/e2e/fax_e2e_test.py --build build-cycle --rom "$ROM" --out "$OUT/e2e-helper"
# Every end-to-end test at once, each in its own folder, on a free port. A
# test that splits into parts (--parts here, first) runs each part as its own
# game;
# the saved games they start from are made once, from your ROM, and cached in
# build-tests/fixtures.
SW=--game-arg=--world-view=seamless
E2E=(
  "travel       tests/travel/guru_travel.py --parts"
  "travel+sw    tests/travel/guru_travel.py --parts $SW"
  "death        tests/death/death_keeps_gold.py"
  "pendant      tests/pendant/pendant_fix.py --parts"
  "magic        tests/magic/magic_button.py"
  "zones        tests/zones/zone_names.py --parts"
  "zones+sw     tests/zones/zone_names.py --parts $SW"
  "keys         tests/keys/auto_and_reusable_keys.py --parts"
  "pose         tests/movement/falling_pose.py"
  "jumps        tests/movement/forgiving_jumps.py"
  "speaker      tests/speaker/speaker_names.py --parts"
  "speaker+sw   tests/speaker/speaker_names.py --parts $SW"
  "text         tests/text_speed/text_speed.py"
  "files        tests/files/save_files.py --parts"
  "dagger       tests/dagger/taller_dagger.py"
  "block        tests/block/block.py --parts"
  "block+sw     tests/block/block.py --parts $SW"
  "sugata       tests/sugata/sugata_flash.py"
  "sugata+sw    tests/sugata/sugata_flash.py $SW"
  "glint        tests/block/block_shield.py --parts"
  "glint+sw     tests/block/block_shield.py --parts $SW"
  "shops        tests/shops/quick_shops.py --parts"
  "shops+sw     tests/shops/quick_shops.py --parts $SW"
  "respawn      tests/respawn/zone_respawn.py --parts"
  "respawn+sw   tests/respawn/zone_respawn.py --parts $SW"
  "recharge     tests/recharge/recharging_items.py --parts"
  "switch       tests/remastered/one_switch.py"
)
# At most E2E_JOBS runs play at once (default: one a hardware thread; 0: all
# at once): a game is uncapped and takes a whole thread, and more of them
# than threads make each frame dearer, so the suite ends later. The longest
# runs start first, by how long each took last time (e2e-times.txt).
JOBS="${E2E_JOBS:-$JOBS}"
TIMES="$OUT/e2e-times.txt"
ms() { echo $(( $(date +%s%N) / 1000000 )); }
names=(); cmds=()
for entry in "${E2E[@]}"; do
  read -r -a words <<< "$entry"
  name="${words[0]}" script="${words[1]}" args=("${words[@]:2}")
  if [ "${args[0]:-}" != --parts ]; then names+=("$name"); cmds+=("$script ${args[*]}"); continue; fi
  for part in $(python3 "$script" --list-parts); do names+=("$name.$part"); cmds+=("$script --part $part ${args[*]:1}"); done
done
declare -A took=()
[ -f "$TIMES" ] && while read -r t n; do took[$n]=$t; done < "$TIMES"
order=$(for i in "${!names[@]}"; do echo "${took[${names[$i]}]:-999999} $i"; done | sort -s -rn -k1,1 | cut -d' ' -f2)
folder() { echo "$OUT/${1//[+.]/-}-e2e"; }
E2E_START=$(ms)
run_e2e() {  # NAME SCRIPT [ARGS...]: one run in its folder; its log, and "code finish took" (ms) in .end
  local name="$1" script="$2"; shift 2
  local out; out=$(folder "$name")
  rm -f "$out.end"
  { local t0; t0=$(ms)
    python3 "$script" --build build-cycle --rom "$ROM" --out "$out" "$@" > "$out.log" 2>&1
    echo "$? $(( $(ms) - E2E_START )) $(( $(ms) - t0 ))" > "$out.end"; } &
}
echo "== end to end: ${#names[@]} runs, ${JOBS/#0/all} at a time"
for i in $order; do
  if [ "$JOBS" -gt 0 ]; then while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do wait -n || true; done; fi
  # shellcheck disable=SC2086  # the command's words
  run_e2e "${names[$i]}" ${cmds[$i]}
done
wait
failed=0
for name in "${names[@]}"; do
  code=none finish=0 t=0
  [ -f "$(folder "$name").end" ] && read -r code finish t < "$(folder "$name").end"
  [ "$code" = 0 ] || { failed=1; echo "== FAILED: $name"; }
  sed "s/^/[$name] /" "$(folder "$name").log"
done
echo "== end to end, slowest last (seconds after they started; how long each took):"
for name in "${names[@]}"; do
  finish=0 t=0
  [ -f "$(folder "$name").end" ] && read -r code finish t < "$(folder "$name").end"
  echo "$finish $t $name"
done | sort -n | tee >(awk '{ print $2, $3 }' > "$TIMES") | awk '{ printf "  %6.1f  %5.1f  %s\n", $1 / 1000, $2 / 1000, $3 }'
[ "$failed" = 0 ] || { echo "end-to-end tests failed; screenshots in $OUT/*-e2e*/" >&2; exit 1; }
echo "all checks passed"
