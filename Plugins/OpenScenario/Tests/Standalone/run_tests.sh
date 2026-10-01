#!/usr/bin/env bash
# Compiles the plugin's engine-independent runtime code against a tiny mock of the Unreal core types
# and runs the example scenarios headless. No Unreal Engine installation required.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../../Source/OpenScenario"
EX="$HERE/../../Examples"
OUT="${TMPDIR:-/tmp}/osc_standalone_test"

${CXX:-g++} -std=c++20 -Wall -Wextra -Wno-unused-parameter -Wno-misleading-indentation \
  -I "$HERE/MockUE" -I "$SRC/Public" -I "$SRC/Private" "$HERE/main.cpp" \
  "$SRC/Private/OpenScenarioModule.cpp" "$SRC/Private/OpenDrive/OpenDriveMap.cpp" \
  "$SRC/Private/OpenDrive/OpenDriveAsset.cpp" "$SRC/Private/Scenario/OpenScenarioParser.cpp" \
  "$SRC/Private/Scenario/OpenScenarioAsset.cpp" "$SRC/Private/Scenario/OpenScenarioWriter.cpp" \
  "$SRC/Private/Scenario/OpenScenarioModelEdit.cpp" "$SRC/Private/Simulation/OpenScenarioRunner.cpp" -o "$OUT"

${CXX:-g++} -std=c++20 -Wall -Wextra -Wno-unused-parameter -Wno-misleading-indentation \
  -I "$HERE/MockUE" -I "$SRC/Public" -I "$SRC/Private" "$HERE/edit_test.cpp" \
  "$SRC/Private/OpenScenarioModule.cpp" "$SRC/Private/OpenDrive/OpenDriveMap.cpp" \
  "$SRC/Private/OpenDrive/OpenDriveAsset.cpp" "$SRC/Private/Scenario/OpenScenarioParser.cpp" \
  "$SRC/Private/Scenario/OpenScenarioAsset.cpp" "$SRC/Private/Scenario/OpenScenarioWriter.cpp" \
  "$SRC/Private/Scenario/OpenScenarioModelEdit.cpp" "$SRC/Private/Simulation/OpenScenarioRunner.cpp" -o "$OUT-edit"

echo "== JunctionRouting: routed left turn, ends on ReachPosition at ~28 s =="
"$OUT" "$EX/JunctionRouting.xosc" 27 30
echo "== TrajectoryAndEvents: ends on StopTrigger at 12 s =="
"$OUT" "$EX/TrajectoryAndEvents.xosc" 12 12.1
echo "== Round trip and storyboard edit operations =="
"$OUT-edit" "$EX"
${CXX:-g++} -std=c++20 -Wall -Wextra -Wno-unused-parameter -Wno-misleading-indentation \
  -I "$HERE/MockUE" -I "$SRC/Public" -I "$SRC/Private" "$HERE/dynamics_test.cpp" \
  "$SRC/Private/OpenScenarioModule.cpp" "$SRC/Private/OpenDrive/OpenDriveMap.cpp" \
  "$SRC/Private/OpenDrive/OpenDriveAsset.cpp" "$SRC/Private/Scenario/OpenScenarioParser.cpp" \
  "$SRC/Private/Scenario/OpenScenarioAsset.cpp" "$SRC/Private/Scenario/OpenScenarioWriter.cpp" \
  "$SRC/Private/Scenario/OpenScenarioModelEdit.cpp" "$SRC/Private/Simulation/OpenScenarioRunner.cpp" -o "$OUT-dynamics"
echo "== Simple vehicle dynamics =="
"$OUT-dynamics" "$EX"
${CXX:-g++} -std=c++20 -Wall -Wextra -Wno-unused-parameter -Wno-misleading-indentation \
  -I "$HERE/MockUE" -I "$SRC/Public" -I "$SRC/Private" "$HERE/traffic_test.cpp" \
  "$SRC/Private/OpenScenarioModule.cpp" "$SRC/Private/OpenDrive/OpenDriveMap.cpp" \
  "$SRC/Private/OpenDrive/OpenDriveAsset.cpp" "$SRC/Private/Scenario/OpenScenarioParser.cpp" \
  "$SRC/Private/Scenario/OpenScenarioAsset.cpp" "$SRC/Private/Scenario/OpenScenarioWriter.cpp" \
  "$SRC/Private/Scenario/OpenScenarioModelEdit.cpp" "$SRC/Private/Simulation/OpenScenarioRunner.cpp" -o "$OUT-traffic"
echo "== Traffic generators =="
"$OUT-traffic" "$EX"
echo "All standalone tests passed."
