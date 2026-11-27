#!/usr/bin/env bash
# Run every check this repo has. They run locally, not in CI: the build
# recompiles your ROM, which cannot be distributed.
#
#   tools/test.sh                 # ROM: "Faxanadu (USA).nes" at the repo root
#   tools/test.sh --rom PATH      # another copy of the same ROM
#   tools/test.sh --quick         # skip the end-to-end game runs
#   tools/test.sh --sanitize      # play the game built with AddressSanitizer
#                                 # and UBSan (build-asan) instead
#
# Run inside the Nix devShell (direnv, or nix develop). The game build in
# build-cycle is configured on first use, and a warning in the repo's own C
# fails it; the unit tests build with AddressSanitizer and UBSan, clang-tidy
# lints src/, and the C must be formatted (tools/format.sh; docs/C_TOOLING.md).
# The end-to-end runs fill every hardware thread, so only one suite on the
# machine runs them at a time: another checkout's suite waits for the lock,
# then runs. E2E_JOBS=N plays at most N of its runs at once (default: one a
# thread). Without --quick, the allocation checks, lint, the format check,
# unit tests and the game's smoke run go on beside the end-to-end runs, and
# print when those end.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
ROM="$REPO/Faxanadu (USA).nes"
QUICK=0
SANITIZED=0
while [ $# -gt 0 ]; do
  case "$1" in
    --rom) ROM="$(realpath "$2")"; shift 2;;
    --quick) QUICK=1; shift;;
    --sanitize) SANITIZED=1; shift;;
    -h|--help) sed -n '2,20p' "$0"; exit 0;;
    *) echo "unknown arg: $1" >&2; exit 2;;
  esac
done
[ -f "$ROM" ] || { echo "No ROM at $ROM; see the README" >&2; exit 2; }
cd "$REPO"
JOBS="$(nproc 2>/dev/null || echo 4)"
OUT="$REPO/build-tests"

# The sanitizer builds, every unit test and with --sanitize the game: under
# AddressSanitizer and UndefinedBehaviorSanitizer, a read or write out of
# bounds, a use after free or any undefined behaviour stops a unit test or
# fails the suite, with a report (docs/C_TOOLING.md). They build with the
# devShell's sanitizer compiler, clang 22, and SDL3's folder as an rpath
# (flake.nix says why). Leaks go unchecked: LeakSanitizer finds none on
# macOS, and the gate passes or fails the same everywhere.
SANITIZE_CC="${FAX_SANITIZE_CC:?run in the Nix devShell (direnv, or nix develop): it names the sanitizer compiler}"
SANITIZE_CXX="${FAX_SANITIZE_CXX:?run in the Nix devShell}"
SANITIZE_RPATH="${FAX_SANITIZE_RPATH:?run in the Nix devShell}"
[ -d "$SANITIZE_RPATH" ] || { echo "No SDL3 folder at $SANITIZE_RPATH: reload the devShell" >&2; exit 2; }
SANITIZE="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g"
export ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}"
export UBSAN_OPTIONS="print_stacktrace=1${UBSAN_OPTIONS:+:$UBSAN_OPTIONS}"
fresh() {  # DIR COMPILER: start DIR's build again if another compiler configured it
  grep -qsx "CMAKE_C_COMPILER:[A-Z]*=$2" "$1/CMakeCache.txt" || rm -rf "$1/CMakeCache.txt" "$1/CMakeFiles"
}
# The game, built first: the end-to-end runs play it. A warning in the repo's
# own C fails its build (FAX_WERROR). With --sanitize they play build-asan's,
# the whole game under AddressSanitizer and UBSan (docs/C_TOOLING.md).
game() {  # DIR [CMAKE ARGS...]: configure DIR on first use, with FAX_WERROR on, and build it
  local dir="$1"; shift
  [ -f "$dir/CMakeCache.txt" ] || cmake -S . -B "$dir" -DNESRECOMP_ROM="$ROM" -DFAX_WERROR=ON "$@" >/dev/null
  grep -qsx "FAX_WERROR:BOOL=ON" "$dir/CMakeCache.txt" || cmake -S . -B "$dir" -DFAX_WERROR=ON >/dev/null
  cmake --build "$dir" -j"$JOBS" >/dev/null
}
echo "== game build"
game build-cycle -DCMAKE_BUILD_TYPE=Release
GAME=build-cycle
if [ "$SANITIZED" = 1 ]; then
  echo "== game build, sanitized (build-asan)"
  fresh build-asan "$SANITIZE_CC"
  game build-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DFAX_SANITIZE=ON \
    -DCMAKE_C_COMPILER="$SANITIZE_CC" -DCMAKE_CXX_COMPILER="$SANITIZE_CXX"
  GAME=build-asan
fi

unit() {  # DIR: configure and build tests/DIR's unit test in $OUT/DIR
  fresh "$OUT/$1" "$SANITIZE_CC"
  cmake -S "tests/$1" -B "$OUT/$1" -DNESRECOMP_ROOT="$REPO/nesrecomp" --no-warn-unused-cli \
    -DCMAKE_C_COMPILER="$SANITIZE_CC" -DCMAKE_C_FLAGS="$SANITIZE" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" -DCMAKE_BUILD_RPATH="$SANITIZE_RPATH" >/dev/null
  cmake --build "$OUT/$1" -j"$JOBS" >/dev/null
}

checks() {  # everything but the end-to-end runs
echo "== allocations"
python3 tools/check_allocations.py --rom "$ROM"
python3 tests/allocations/check_allocations_test.py --rom "$ROM"

echo "== lint"
JOBS="$JOBS" tools/lint.sh

echo "== format"
tools/format.sh --check

echo "== unit tests"
unit travel
"$OUT/travel/travel_test"
unit text_speed
"$OUT/text_speed/text_speed_test"
unit world
echo "fax_world.c compiles without SDL"
"$OUT/world/present_fit_test" | tail -4
"$OUT/world/camera_test" "$ROM" | tail -3
unit death
"$OUT/death/death_test"
unit pendant
"$OUT/pendant/pendant_test"
unit magic
"$OUT/magic/magic_test"
unit zones
"$OUT/zones/zones_test"
unit keys
"$OUT/keys/keys_test"
unit movement
"$OUT/movement/pose_test"
"$OUT/movement/jumps_test"
unit ladders
"$OUT/ladders/ladders_test"
"$OUT/ladders/ladder_hold_test"
unit files
FAX_FILES_TEST_DIR="$OUT/files/scratch" "$OUT/files/files_test"
unit title
"$OUT/title/title_test"
unit speaker
"$OUT/speaker/speaker_test"
unit dagger
"$OUT/dagger/dagger_test"
unit block
"$OUT/block/block_test"
unit sugata
"$OUT/sugata/sugata_test"
unit shops
"$OUT/shops/shops_test"
unit respawn
"$OUT/respawn/respawn_test"
unit recharge
"$OUT/recharge/recharge_test"
unit charges
"$OUT/charges/charges_test"
unit item_box
"$OUT/item_box/item_box_test"
unit mana
"$OUT/mana/mana_test"
unit lesson
"$OUT/lesson/lesson_test"
unit gold
"$OUT/gold/gold_test"
unit slain
"$OUT/slain/slain_test"
unit indoor
"$OUT/indoor/indoor_test"
unit damage
"$OUT/damage/damage_test"
unit inventory
"$OUT/inventory/inventory_test"
unit fixes
"$OUT/fixes/fixes_test"
unit finds
"$OUT/finds/finds_test"
unit found
"$OUT/found/found_test"
unit shaft
"$OUT/shaft/shaft_test"
unit map
"$OUT/map/map_test" "$ROM"
unit quests
"$OUT/quests/quests_test" "$ROM"
unit graphics
"$OUT/graphics/graphics_test"
unit flicker
"$OUT/flicker/flicker_test"
unit entrances
"$OUT/entrances/entrances_test"
unit spell_cost
"$OUT/spell_cost/spell_cost_test"
unit menu
"$OUT/menu/menu_test"
unit use_item
"$OUT/use_item/use_item_test"
unit run
"$OUT/run/run_test"
unit controls
"$OUT/controls/controls_test" "$OUT/controls"
unit seams
"$OUT/seams/seams_test"
unit punches
"$OUT/punches/punch_test"

echo "== the game, 600 frames headless ($GAME)"
UBSAN_OPTIONS="$UBSAN_OPTIONS:halt_on_error=1" "$GAME/FaxanaduRecomp" "$ROM" --frames 600 --no-save | grep "mode="
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
# With --sanitize the games keep fixtures of their own (making a fixture
# with one build throws away the other's), and their sanitizers write each
# report to a file, read at the end: any report fails the suite. A UBSan
# report lets the game play on, so one suite finds every place.
REPORTS="$OUT/sanitizer-reports"
TIMES="$OUT/e2e-times.txt"
if [ "$SANITIZED" = 1 ]; then
  rm -rf "$REPORTS"
  mkdir -p "$REPORTS"
  export FAX_E2E_FIXTURES="$OUT/fixtures-asan"
  export ASAN_OPTIONS="$ASAN_OPTIONS:log_path=$REPORTS/report" UBSAN_OPTIONS="$UBSAN_OPTIONS:log_path=$REPORTS/report"
  TIMES="$OUT/e2e-times-asan.txt"
fi
echo "== end to end: the shared helper's port reservation and relaunch"
python3 tests/e2e/fax_e2e_test.py --build "$GAME" --rom "$ROM" --out "$OUT/e2e-helper"
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
  "item-box     tests/item_box/item_box.py --parts"
  "mana         tests/mana/mp_from_kills.py --parts"
  "lesson       tests/lesson/magician_lesson.py --parts"
  "gold         tests/gold/double_gold.py --parts"
  "switch       tests/dx/one_switch.py"
  "title        tests/title/title_dx.py"
  "slain        tests/slain/slain_by.py"
  "indoor       tests/indoor/indoor_equip.py --parts"
  "damage       tests/damage/damage_numbers.py"
  "inventory    tests/inventory/inventory_fix.py --parts"
  "map          tests/map/explored_map.py --parts"
  "map-music    tests/map/music_plays_on.py --parts"
  "quests       tests/quests/quest_log.py --parts"
  "fixes        tests/fixes/small_fixes.py --parts"
  "graphics     tests/graphics/graphics_fixes.py --parts"
  "flicker      tests/flicker/no_flicker.py --parts"
  "finds        tests/finds/kept_finds.py --parts"
  "softlocks    tests/softlocks/eolis_shaft.py --parts"
  "entrances    tests/entrances/fair_entrances.py --parts"
  "view         tests/world/fixed_view.py"
  "arenas       tests/world/boss_arenas.py --parts"
  "only-rooms   tests/world/only_rooms.py --parts"
  "live-hud     tests/world/live_status_bar.py --parts"
  "spell-cost   tests/spell_cost/spell_cost.py --parts"
  "found        tests/found/found_items.py --parts"
  "menu         tests/menu/select_menu.py --parts"
  "controls     tests/controls/controller_layout.py --parts"
  "back-out     tests/controls/b_backs_out.py --parts"
  "run          tests/run/run.py --parts"
  "seams        tests/seams/seams_lose_nothing.py --parts"
  "open-ground  tests/seams/open_ground.py --parts"
  "fights       tests/seams/fights.py --parts"
  "spells       tests/seams/spells.py --parts"
  "punches      tests/punches/punches.py"
  "vsync        tests/vsync/vsync_default.py"
)
# At most E2E_JOBS runs play at once (default: one a hardware thread; 0: all
# at once): a game is uncapped and takes a whole thread, and more of them
# than threads make each frame dearer, so the suite ends later. The longest
# runs start first, by the CPU each used last time (e2e-times.txt): unlike its
# wall time, that does not depend on how busy the machine was when it ran.
JOBS="${E2E_JOBS:-$JOBS}"
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
    python3 "$script" --build "$GAME" --rom "$ROM" --out "$out" "$@" > "$out.log" 2>&1
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
[ "$checked" = 0 ] || echo "== FAILED: the checks above (allocations, lint, format, unit tests or the game's smoke run)" >&2
[ "$failed" = 0 ] || echo "end-to-end tests failed; screenshots in $OUT/*-e2e*/" >&2
if [ "$SANITIZED" = 1 ] && [ -n "$(ls -A "$REPORTS")" ]; then
  failed=1
  echo "== FAILED: the sanitizers' reports in $REPORTS, each place with how many games hit it:" >&2
  grep -h -E "^SUMMARY: AddressSanitizer|runtime error:" "$REPORTS"/* | sort | uniq -c | sort -rn >&2
fi
[ "$checked" = 0 ] && [ "$failed" = 0 ] || exit 1
echo "all checks passed"
