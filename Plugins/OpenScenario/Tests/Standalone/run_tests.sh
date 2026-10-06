#!/usr/bin/env bash
# Compiles the plugin's engine-independent runtime code against a tiny mock of the Unreal core types
# and runs the example scenarios and tests headless. No Unreal Engine installation required.
#
# The OpenDRIVE data model lives in the separate OpenDRIVE_UE plugin (https://github.com/p-schulz/OpenDRIVE_UE).
# Point OPENDRIVE_PLUGIN at its Plugins/OpenDrive folder (default: a sibling checkout).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../../Source/OpenScenario"
EX="$HERE/../../Examples"
OUT="${TMPDIR:-/tmp}/osc_standalone_test"

OD="${OPENDRIVE_PLUGIN:-}"
if [ -z "$OD" ]; then
  for candidate in "$HERE/../../../../../OpenDRIVE_UE/Plugins/OpenDrive" "$HERE/../../../../../opendrive_ue/Plugins/OpenDrive" \
                   "$HERE/../../../../OpenDRIVE_UE/Plugins/OpenDrive" /home/user/p-schulz/opendrive_ue/Plugins/OpenDrive; do
    if [ -d "$candidate/Source/OpenDrive" ]; then OD="$candidate"; break; fi
  done
fi
if [ ! -d "$OD/Source/OpenDrive" ]; then
  echo "OpenDRIVE plugin not found. Clone https://github.com/p-schulz/OpenDRIVE_UE and set OPENDRIVE_PLUGIN=<clone>/Plugins/OpenDrive" >&2
  exit 1
fi
ODS="$OD/Source/OpenDrive"

CXXFLAGS=(-std=c++20 -Wall -Wextra -Wno-unused-parameter -Wno-misleading-indentation
  -I "$HERE/MockUE" -I "$SRC/Public" -I "$SRC/Private" -I "$ODS/Public" -I "$ODS/Private")
LIB=("$SRC/Private/OpenScenarioModule.cpp"
  "$ODS/Private/OpenDrive/OpenDriveMap.cpp" "$ODS/Private/OpenDrive/OpenDriveAsset.cpp" "$ODS/Private/OpenDriveWriter.cpp"
  "$SRC/Private/Scenario/OpenScenarioParser.cpp" "$SRC/Private/Scenario/OpenScenarioAsset.cpp"
  "$SRC/Private/Scenario/OpenScenarioWriter.cpp" "$SRC/Private/Scenario/OpenScenarioModelEdit.cpp"
  "$SRC/Private/Simulation/OpenScenarioRunner.cpp")

build() { ${CXX:-g++} "${CXXFLAGS[@]}" "$HERE/$1.cpp" "${LIB[@]}" -o "$OUT-$2"; }
build main scenarios; build edit_test edit; build dynamics_test dynamics; build traffic_test traffic

echo "== JunctionRouting: routed left turn, ends on ReachPosition at ~28 s =="
"$OUT-scenarios" "$EX/JunctionRouting.xosc" 27 30
echo "== TrajectoryAndEvents: ends on StopTrigger at 12 s =="
"$OUT-scenarios" "$EX/TrajectoryAndEvents.xosc" 12 12.1
echo "== Round trip and storyboard edit operations =="
"$OUT-edit" "$EX"
echo "== Simple vehicle dynamics =="
"$OUT-dynamics" "$EX"
echo "== Traffic generators =="
"$OUT-traffic" "$EX"
echo "All standalone tests passed."
