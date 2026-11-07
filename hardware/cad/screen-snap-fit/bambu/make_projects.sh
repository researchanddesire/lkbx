#!/bin/bash
# Build the Bambu Studio projects for the screen snap-fit test print.
#
# usage: bambu/make_projects.sh [printer-key ...]     keys: p1s x1c a1 a1mini (default: all)
#
# Needs the two STL exports written by build_freecad.py in hardware/cad/ and a Bambu
# Studio 2.8 install in $BAMBU_STUDIO_DIR (the directory holding bin/bambu-studio and
# resources/profiles; an extracted Linux AppImage works). Without a display the command
# line still exports projects, it only skips the plate thumbnails; xvfb-run is used when
# it is installed. For each printer key this writes, next to this script,
#
#   lkbx-top-housing-snap-fit-<key>-0.4mm.3mf   housing, PLA, front face down, no supports
#   lkbx-screen-keeper-<key>-0.4mm.3mf          keeper, PETG, smooth face down, 4 walls, solid
#
# with the printer, filament and process presets embedded, then slices a copy of each to
# print the time and filament estimates and packages the slices as printer-ready files in
# sliced/ (not tracked), which Bambu Handy and the printer accept as they are:
#
#   sliced/lkbx-top-housing-snap-fit-<key>-0.4mm.gcode.3mf
#   sliced/lkbx-screen-keeper-<key>-0.4mm.gcode.3mf
#
# The plate previews inside them come from render_thumbnails.py (matplotlib); without it
# the files are packaged without pictures. The process presets are lkbx-*-process.json
# flattened over the stock 0.20 mm Standard profile for that printer (see flatten_preset.py).
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
CAD_DIR=$(cd "$HERE/../.." && pwd)
: "${BAMBU_STUDIO_DIR:?set BAMBU_STUDIO_DIR to the Bambu Studio install directory (bin/bambu-studio, resources/profiles)}"
PROFILES="$BAMBU_STUDIO_DIR/resources/profiles/BBL"
HOUSING_STL="$CAD_DIR/top-housing-snap-fit.stl"
KEEPER_STL="$CAD_DIR/screen-keeper-print.stl"
for f in "$BAMBU_STUDIO_DIR/bin/bambu-studio" "$HOUSING_STL" "$KEEPER_STL"; do
  [ -f "$f" ] || { echo "missing $f" >&2; exit 1; }
done

KEYS=("$@"); [ ${#KEYS[@]} -gt 0 ] || KEYS=(p1s x1c a1 a1mini)

presets_for() {  # presets_for <key>: sets MACHINE, PROC_BASE, PLA, PETG
  case $1 in
    p1s)    MACHINE="Bambu Lab P1S 0.4 nozzle";       PROC_BASE="0.20mm Standard @BBL X1C"; PLA="Bambu PLA Basic @BBL P1S 0.4 nozzle"; PETG="Bambu PETG HF @BBL P1S 0.4 nozzle";;
    x1c)    MACHINE="Bambu Lab X1 Carbon 0.4 nozzle"; PROC_BASE="0.20mm Standard @BBL X1C"; PLA="Bambu PLA Basic @BBL X1C";            PETG="Bambu PETG HF @BBL X1C";;
    a1)     MACHINE="Bambu Lab A1 0.4 nozzle";        PROC_BASE="0.20mm Standard @BBL A1";  PLA="Bambu PLA Basic @BBL A1";             PETG="Bambu PETG HF @BBL A1";;
    a1mini) MACHINE="Bambu Lab A1 mini 0.4 nozzle";   PROC_BASE="0.20mm Standard @BBL A1M"; PLA="Bambu PLA Basic @BBL A1M";            PETG="Bambu PETG HF @BBL A1M";;
    *) echo "unknown printer key $1 (p1s, x1c, a1, a1mini)" >&2; exit 1;;
  esac
}

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
SLICED="$HERE/sliced"; mkdir -p "$SLICED"
mkdir -p "$WORK/thumbs"
for part in housing:"$HOUSING_STL" keeper:"$KEEPER_STL"; do
  python3 "$HERE/render_thumbnails.py" "${part#*:}" "$WORK/thumbs/${part%%:*}" \
    || echo "  previews for ${part%%:*} skipped (render_thumbnails.py needs numpy and matplotlib)"
done
RUNNER=()
if [ -z "${DISPLAY:-}" ] && command -v xvfb-run >/dev/null; then RUNNER=(xvfb-run -a -s "-screen 0 1280x1024x24"); fi
export LD_LIBRARY_PATH="$BAMBU_STUDIO_DIR/bin${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" LC_ALL=C

slicer() {  # slicer <outdir> <process.json> <filament.json> <stl> <project.3mf> [extra args]
  local outdir=$1 proc=$2 fil=$3 stl=$4 name=$5; shift 5
  mkdir -p "$outdir"
  "${RUNNER[@]}" "$BAMBU_STUDIO_DIR/bin/bambu-studio" --debug 1 \
    --load-settings "$W/machine.json;$proc" --load-filaments "$fil" \
    --orient 0 --arrange 1 "$@" --export-3mf "$name" --outputdir "$outdir" "$stl" > "$outdir.log" 2>&1 || true
  python3 - "$outdir/result.json" <<'PY'
import json, sys
try:
    result = json.load(open(sys.argv[1]))
except OSError:
    raise SystemExit("slicer wrote no result; see %s.log" % sys.argv[1].rsplit("/", 1)[0])
if result["return_code"] != 0:
    raise SystemExit("slicer failed: %s (%s)" % (result["error_string"], result["return_code"]))
for plate in result.get("sliced_plates", []):
    seconds = plate["total_predication"]
    grams = sum(f["total_used_g"] for f in plate["filaments"])
    print("    estimate: %dh%02dm, %.1f g%s" % (seconds // 3600, seconds % 3600 // 60, grams,
          ("; warning: " + plate["warning_message"]) if plate["warning_message"] else ""))
PY
}

for KEY in "${KEYS[@]}"; do
  presets_for "$KEY"
  echo "== $KEY: $MACHINE"
  W="$WORK/$KEY"; mkdir -p "$W"
  python3 "$HERE/flatten_preset.py" "$PROFILES/machine/$MACHINE.json" "$PROFILES" "$W/machine.json"
  python3 "$HERE/flatten_preset.py" "$PROFILES/filament/$PLA.json"    "$PROFILES" "$W/filament-pla.json"
  python3 "$HERE/flatten_preset.py" "$PROFILES/filament/$PETG.json"   "$PROFILES" "$W/filament-petg.json"
  for part in housing keeper; do
    sed -E "s|\"inherits\": \"[^\"]*\"|\"inherits\": \"$PROC_BASE\"|" "$HERE/lkbx-$part-process.json" > "$W/$part-process.src.json"
    python3 "$HERE/flatten_preset.py" "$W/$part-process.src.json" "$PROFILES" "$W/$part-process.json" "$MACHINE"
  done
  echo "  housing project"
  slicer "$W/housing" "$W/housing-process.json" "$W/filament-pla.json"  "$HOUSING_STL" housing.3mf
  cp "$W/housing/housing.3mf" "$HERE/lkbx-top-housing-snap-fit-$KEY-0.4mm.3mf"
  echo "  keeper project"
  slicer "$W/keeper"  "$W/keeper-process.json"  "$W/filament-petg.json" "$KEEPER_STL"  keeper.3mf
  cp "$W/keeper/keeper.3mf" "$HERE/lkbx-screen-keeper-$KEY-0.4mm.3mf"
  echo "  housing slice check"
  slicer "$W/housing-sliced" "$W/housing-process.json" "$W/filament-pla.json"  "$HOUSING_STL" housing-sliced.3mf --slice 0
  echo "  keeper slice check"
  slicer "$W/keeper-sliced"  "$W/keeper-process.json"  "$W/filament-petg.json" "$KEEPER_STL"  keeper-sliced.3mf --slice 0
  echo "  printer-ready files"
  MODEL_ID=$(python3 - "$W/machine.json" "$PROFILES" <<'PY'
import json, sys
machine = json.load(open(sys.argv[1]))
print(json.load(open("%s/machine/%s.json" % (sys.argv[2], machine["printer_model"])))["model_id"])
PY
)
  python3 "$HERE/package_sliced.py" "$W/housing-sliced/housing-sliced.3mf" "$SLICED/lkbx-top-housing-snap-fit-$KEY-0.4mm.gcode.3mf" \
    --thumbnails "$WORK/thumbs/housing" --printer-model-id "$MODEL_ID"
  python3 "$HERE/package_sliced.py" "$W/keeper-sliced/keeper-sliced.3mf" "$SLICED/lkbx-screen-keeper-$KEY-0.4mm.gcode.3mf" \
    --thumbnails "$WORK/thumbs/keeper" --printer-model-id "$MODEL_ID"
done
ls -la "$HERE"/*.3mf "$SLICED"
