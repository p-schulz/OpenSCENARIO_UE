#!/usr/bin/env bash
# Compiles the plugin's engine-independent runtime code against a tiny mock of the Unreal core types and
# runs the example scenarios and tests headless. No Unreal Engine installation required.
#
# The OpenDRIVE data model (FOpenDriveMap/UOpenDriveAsset) is owned by the separate OpenDRIVE_UE plugin; in a
# real Unreal build OpenScenario depends on it as a plugin. This harness builds against the vendored copy in
# OpenDriveVendor/ instead (see the comment at the top of OpenDriveMap.h there). Re-sync it when the shared
# model changes.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../../Source/OpenScenario"
ODR="$HERE/OpenDriveVendor"
EX="$HERE/../../Examples"
OUT="${TMPDIR:-/tmp}/osc_standalone"
mkdir -p "$OUT"

CXXFLAGS=(-std=c++20 -Wall -Wextra -Wno-unused-parameter -Wno-misleading-indentation
  -I "$HERE/MockUE" -I "$SRC/Public" -I "$SRC/Private" -I "$ODR/Public" -I "$ODR/Private")
LIB=("$SRC/Private/OpenScenarioModule.cpp" "$ODR/Private/OpenDriveModule.cpp"
  "$ODR/Private/OpenDrive/OpenDriveMap.cpp" "$ODR/Private/OpenDrive/OpenDriveAsset.cpp" "$ODR/Private/OpenDriveWriter.cpp"
  "$SRC/Private/Scenario/OpenScenarioParser.cpp" "$SRC/Private/Scenario/OpenScenarioAsset.cpp"
  "$SRC/Private/Scenario/OpenScenarioWriter.cpp" "$SRC/Private/Scenario/OpenScenarioModelEdit.cpp"
  "$SRC/Private/Simulation/OpenScenarioRunner.cpp")

# Compile the library sources once (in parallel), then link every test against the objects.
OBJS=()
pids=()
for f in "${LIB[@]}"; do
  o="$OUT/$(basename "$f" .cpp).o"; OBJS+=("$o")
  if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ -n "$(find "$SRC/Public" "$ODR/Public" "$HERE/MockUE" -newer "$o" -name '*.h' -print -quit)" ]; then
    ${CXX:-g++} "${CXXFLAGS[@]}" -c "$f" -o "$o" & pids+=($!)
  fi
done
for p in "${pids[@]:-}"; do [ -n "$p" ] && wait "$p"; done

TESTS=(main edit_test dynamics_test traffic_test signal_test)
pids=()
for t in "${TESTS[@]}"; do
  [ -f "$HERE/$t.cpp" ] || continue
  ${CXX:-g++} "${CXXFLAGS[@]}" "$HERE/$t.cpp" "${OBJS[@]}" -o "$OUT/$t" & pids+=($!)
done
for p in "${pids[@]}"; do wait "$p"; done

echo "== JunctionRouting: routed left turn, ends on ReachPosition at ~28 s =="
"$OUT/main" "$EX/JunctionRouting.xosc" 27 30
echo "== TrajectoryAndEvents: ends on StopTrigger at 12 s =="
"$OUT/main" "$EX/TrajectoryAndEvents.xosc" 12 12.1
echo "== Round trip and storyboard edit operations =="
"$OUT/edit_test" "$EX"
echo "== Simple vehicle dynamics =="
"$OUT/dynamics_test" "$EX"
echo "== Traffic generators =="
"$OUT/traffic_test" "$EX"
if [ -x "$OUT/signal_test" ]; then
  echo "== Traffic signals and signs =="
  "$OUT/signal_test" "$EX"
fi
echo "All standalone tests passed."
