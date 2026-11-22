#!/usr/bin/env bash
# Run every check this repo has. They run locally, not in CI: the build
# recompiles your ROM, which cannot be distributed.
#
#   tools/test.sh                 # ROM: "Faxanadu (USA).nes" at the repo root
#   tools/test.sh --rom PATH      # another copy of the same ROM
#   tools/test.sh --quick         # skip the end-to-end game runs
#
# Run inside the Nix devShell (direnv, or nix develop). The game build in
# build-cycle is configured on first use. The end-to-end runs fill every
# hardware thread, so only one suite on the machine runs them at a time:
# another checkout's suite waits for the lock, then runs. E2E_JOBS=N plays at
# most N of its runs at once (default: one a thread). Without --quick, the
# allocation checks, unit tests and the game's smoke run go on beside the
# end-to-end runs, and print when those end.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
ROM="$REPO/Faxanadu (USA).nes"
QUICK=0
while [ $# -gt 0 ]; do
  case "$1" in
    --rom) ROM="$(realpath "$2")"; shift 2;;
    --quick) QUICK=1; shift;;
    -h|--help) sed -n '2,13p' "$0"; exit 0;;
    *) echo "unknown arg: $1" >&2; exit 2;;
  esac
done
[ -f "$ROM" ] || { echo "No ROM at $ROM; see the README" >&2; exit 2; }
cd "$REPO"
JOBS="$(nproc 2>/dev/null || echo 4)"
OUT="$REPO/build-tests"

# The game, built first: the end-to-end runs play it.
echo "== game build"
[ -f build-cycle/CMakeCache.txt ] || cmake -S . -B build-cycle -DCMAKE_BUILD_TYPE=Release -DNESRECOMP_ROM="$ROM" >/dev/null
cmake --build build-cycle -j"$JOBS" >/dev/null

checks() {  # everything but the end-to-end runs
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
cmake -S tests/ladders -B "$OUT/ladders" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/ladders" -j"$JOBS" >/dev/null
"$OUT/ladders/ladders_test"
"$OUT/ladders/ladder_hold_test"
cmake -S tests/files -B "$OUT/files" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/files" -j"$JOBS" >/dev/null
FAX_FILES_TEST_DIR="$OUT/files/scratch" "$OUT/files/files_test"
cmake -S tests/title -B "$OUT/title" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/title" -j"$JOBS" >/dev/null
"$OUT/title/title_test"
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
cmake -S tests/charges -B "$OUT/charges" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/charges" -j"$JOBS" >/dev/null
"$OUT/charges/charges_test"
cmake -S tests/mana -B "$OUT/mana" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/mana" -j"$JOBS" >/dev/null
"$OUT/mana/mana_test"
cmake -S tests/lesson -B "$OUT/lesson" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/lesson" -j"$JOBS" >/dev/null
"$OUT/lesson/lesson_test"
cmake -S tests/potions -B "$OUT/potions" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/potions" -j"$JOBS" >/dev/null
"$OUT/potions/potions_test"
cmake -S tests/gold -B "$OUT/gold" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/gold" -j"$JOBS" >/dev/null
"$OUT/gold/gold_test"
cmake -S tests/slain -B "$OUT/slain" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/slain" -j"$JOBS" >/dev/null
"$OUT/slain/slain_test"
cmake -S tests/indoor -B "$OUT/indoor" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/indoor" -j"$JOBS" >/dev/null
"$OUT/indoor/indoor_test"
cmake -S tests/damage -B "$OUT/damage" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/damage" -j"$JOBS" >/dev/null
"$OUT/damage/damage_test"
cmake -S tests/inventory -B "$OUT/inventory" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/inventory" -j"$JOBS" >/dev/null
"$OUT/inventory/inventory_test"
cmake -S tests/fixes -B "$OUT/fixes" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/fixes" -j"$JOBS" >/dev/null
"$OUT/fixes/fixes_test"
cmake -S tests/finds -B "$OUT/finds" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/finds" -j"$JOBS" >/dev/null
"$OUT/finds/finds_test"
cmake -S tests/shaft -B "$OUT/shaft" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/shaft" -j"$JOBS" >/dev/null
"$OUT/shaft/shaft_test"
cmake -S tests/map -B "$OUT/map" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/map" -j"$JOBS" >/dev/null
"$OUT/map/map_test" "$ROM"
cmake -S tests/quests -B "$OUT/quests" >/dev/null
cmake --build "$OUT/quests" -j"$JOBS" >/dev/null
"$OUT/quests/quests_test" "$ROM"
cmake -S tests/graphics -B "$OUT/graphics" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/graphics" -j"$JOBS" >/dev/null
"$OUT/graphics/graphics_test"
cmake -S tests/entrances -B "$OUT/entrances" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/entrances" -j"$JOBS" >/dev/null
"$OUT/entrances/entrances_test"
cmake -S tests/spell_cost -B "$OUT/spell_cost" -DNESRECOMP_ROOT="$REPO/nesrecomp" >/dev/null
cmake --build "$OUT/spell_cost" -j"$JOBS" >/dev/null
"$OUT/spell_cost/spell_cost_test"

echo "== the game, 600 frames headless"
build-cycle/FaxanaduRecomp "$ROM" --frames 600 --no-save | grep "mode="
}

if [ "$QUICK" = 1 ]; then checks; echo "(end-to-end runs skipped: --quick)"; exit 0; fi
# The checks need a second or two of CPU each, and the end-to-end runs a
# minute of every thread: they go on together, the checks' output kept for
# the end. Their unit tests build with a few jobs, not one a thread.
mkdir -p "$OUT"
rm -f "$OUT/checks.code"
( JOBS=4; set +e; ( set -e; checks ) > "$OUT/checks.log" 2>&1; echo $? > "$OUT/checks.code" ) &
# Two suites at once double every game's time and make the timing tests flaky,
# so wait for any other checkout's end-to-end runs to finish. The lock is held
# on fd 9 until this script exits. tools/readme_screenshots.py takes the same
# lock.
LOCK="${XDG_RUNTIME_DIR:-${TMPDIR:-/tmp}}/faxanadu-dx-e2e.lock"
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
# game, every part or those listed (--parts=a,b); the saved games they start
# from are made once, from your ROM, and cached in build-tests/fixtures.
#
# Every run plays the remaster in Seamless, its view, as players get it, so
# each part runs once (#110). "The original" parts switch the remaster off,
# and play in the stock view. Seamless's view is 424x240 whatever the
# window, what a player sees, so its window is at scale 1 (fax_e2e's install()).
E2E=(
  "travel       tests/travel/guru_travel.py --parts"
  "death        tests/death/death_keeps_gold.py"
  "pendant      tests/pendant/pendant_fix.py --parts"
  "magic        tests/magic/magic_button.py"
  "zones        tests/zones/zone_names.py --parts"
  "keys         tests/keys/auto_and_reusable_keys.py --parts"
  "pose         tests/movement/falling_pose.py"
  "jumps        tests/movement/forgiving_jumps.py"
  "ladders      tests/ladders/ladders.py --parts"
  "speaker      tests/speaker/speaker_names.py --parts"
  "text         tests/text_speed/text_speed.py"
  "files        tests/files/save_files.py --parts"
  "dagger       tests/dagger/taller_dagger.py"
  "block        tests/block/block.py --parts"
  "sugata       tests/sugata/sugata_flash.py"
  "glint        tests/block/block_shield.py --parts"
  "shops        tests/shops/quick_shops.py --parts"
  "respawn      tests/respawn/zone_respawn.py --parts"
  "recharge     tests/recharge/recharging_items.py --parts"
  "elixir-lines tests/recharge/guru_lines.py --parts"
  "charges      tests/charges/status_bar.py --parts"
  "mana         tests/mana/mp_from_kills.py --parts"
  "lesson       tests/lesson/magician_lesson.py --parts"
  "potions      tests/potions/potion_sellers.py --parts"
  "gold         tests/gold/double_gold.py --parts"
  "switch       tests/dx/one_switch.py"
  "title        tests/title/title_dx.py"
  "slain        tests/slain/slain_by.py"
  "indoor       tests/indoor/indoor_equip.py --parts"
  "damage       tests/damage/damage_numbers.py"
  "inventory    tests/inventory/inventory_fix.py --parts"
  "map          tests/map/explored_map.py --parts"
  "quests       tests/quests/quest_log.py --parts"
  "fixes        tests/fixes/small_fixes.py --parts"
  "graphics     tests/graphics/graphics_fixes.py --parts"
  "finds        tests/finds/kept_finds.py --parts"
  "softlocks    tests/softlocks/eolis_shaft.py --parts"
  "entrances    tests/entrances/fair_entrances.py --parts"
  "view         tests/world/fixed_view.py"
  "arenas       tests/world/boss_arenas.py --parts"
  "live-hud     tests/world/live_status_bar.py --parts"
  "spell-cost   tests/spell_cost/spell_cost.py --parts"
)
# At most E2E_JOBS runs play at once (default: one a hardware thread; 0: all
# at once): a game is uncapped and takes a whole thread, and more of them
# than threads make each frame dearer, so the suite ends later. The longest
# runs start first, by the CPU each used last time (e2e-times.txt): unlike its
# wall time, that does not depend on how busy the machine was when it ran.
JOBS="${E2E_JOBS:-$JOBS}"
TIMES="$OUT/e2e-times.txt"
ms() { echo $(( $(date +%s%N) / 1000000 )); }
names=(); cmds=()
for entry in "${E2E[@]}"; do
  read -r -a words <<< "$entry"
  name="${words[0]}" script="${words[1]}" args=("${words[@]:2}")
  case "${args[0]:-}" in
    --parts) parts=$(python3 "$script" --list-parts);;
    --parts=*) parts=${args[0]#--parts=}; parts=${parts//,/ };;
    *) names+=("$name"); cmds+=("$script ${args[*]}"); continue;;
  esac
  for part in $parts; do names+=("$name.$part"); cmds+=("$script --part $part ${args[*]:1}"); done
done
declare -A took=()
[ -f "$TIMES" ] && while read -r t n; do took[$n]=$t; done < "$TIMES"
order=$(for i in "${!names[@]}"; do echo "${took[${names[$i]}]:-999999} $i"; done | sort -s -rn -k1,1 | cut -d' ' -f2)
folder() { echo "$OUT/${1//[+.]/-}-e2e"; }
E2E_START=$(ms)
run_e2e() {  # NAME SCRIPT [ARGS...]: one run in its folder; its log, and "code finish took cpu" (ms) in .end
  local name="$1" script="$2"; shift 2
  local out; out=$(folder "$name")
  rm -f "$out.end"
  { local t0 code cpu; t0=$(ms)
    python3 "$script" --build build-cycle --rom "$ROM" --out "$out" "$@" > "$out.log" 2>&1
    code=$?
    cpu=$(sed -n 's/^.*time: .*, games \([0-9.]*\) s CPU, script \([0-9.]*\) s CPU$/\1 \2/p' "$out.log" |
          awk '{ printf "%d", ($1 + $2) * 1000 }')
    echo "$code $(( $(ms) - E2E_START )) $(( $(ms) - t0 )) ${cpu:-0}" > "$out.end"; } &
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
  [ -f "$(folder "$name").end" ] && read -r code finish t cpu < "$(folder "$name").end"
  [ "$code" = 0 ] || { failed=1; echo "== FAILED: $name"; }
  sed "s/^/[$name] /" "$(folder "$name").log"
done
echo "== end to end, slowest last (seconds after they started; how long each took):"
for name in "${names[@]}"; do
  finish=0 t=0 cpu=0
  [ -f "$(folder "$name").end" ] && read -r code finish t cpu < "$(folder "$name").end"
  echo "$finish $t $cpu $name"
done | sort -n | tee >(awk '{ print $3, $4 }' > "$TIMES") | awk '{ printf "  %6.1f  %5.1f  %s\n", $1 / 1000, $2 / 1000, $4 }'
checked=$(cat "$OUT/checks.code" 2>/dev/null || echo 1)
cat "$OUT/checks.log"
[ "$checked" = 0 ] || echo "== FAILED: the checks above (allocations, unit tests or the game's smoke run)" >&2
[ "$failed" = 0 ] || echo "end-to-end tests failed; screenshots in $OUT/*-e2e*/" >&2
[ "$checked" = 0 ] && [ "$failed" = 0 ] || exit 1
echo "all checks passed"
