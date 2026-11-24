#!/bin/bash
# Build the Bambu Studio project for the screen snap-fit test print.
#
# usage: bambu/make_projects.sh [printer-key ...]     keys: p1s x1c a1 a1mini (default: all)
#
# Needs the STL export written by build_freecad.py in hardware/cad/ and a Bambu Studio 2.8
# install in $BAMBU_STUDIO_DIR (the directory holding bin/bambu-studio and
# resources/profiles; an extracted Linux AppImage works). Without a display the command
# line still exports projects, it only skips the plate thumbnails; xvfb-run is used when
# it is installed. For each printer key this writes, next to this script,
#
#   lkbx-top-housing-snap-fit-<key>-0.4mm.3mf   housing, PLA, front face down, no supports
#
# with the printer, filament and process presets embedded, then slices a copy to print
# the time and filament estimate and packages the slice as a printer-ready file in
# sliced/ (not tracked), which Bambu Handy and the printer accept as it is:
#
#   sliced/lkbx-top-housing-snap-fit-<key>-0.4mm.gcode.3mf
#
# The plate preview inside it comes from render_thumbnails.py (matplotlib); without it
# the file is packaged without a picture. The process preset is lkbx-housing-process.json
# flattened over the stock 0.20 mm Standard profile for that printer (see flatten_preset.py).
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
CAD_DIR=$(cd "$HERE/../.." && pwd)
: "${BAMBU_STUDIO_DIR:?set BAMBU_STUDIO_DIR to the Bambu Studio install directory (bin/bambu-studio, resources/profiles)}"
PROFILES="$BAMBU_STUDIO_DIR/resources/profiles/BBL"
HOUSING_STL="$CAD_DIR/top-housing-snap-fit.stl"
for f in "$BAMBU_STUDIO_DIR/bin/bambu-studio" "$HOUSING_STL"; do
  [ -f "$f" ] || { echo "missing $f" >&2; exit 1; }
done

KEYS=("$@"); [ ${#KEYS[@]} -gt 0 ] || KEYS=(p1s x1c a1 a1mini)

presets_for() {  # presets_for <key>: sets MACHINE, PROC_BASE, PLA
  case $1 in
    p1s)    MACHINE="Bambu Lab P1S 0.4 nozzle";       PROC_BASE="0.20mm Standard @BBL X1C"; PLA="Bambu PLA Basic @BBL P1S 0.4 nozzle";;
    x1c)    MACHINE="Bambu Lab X1 Carbon 0.4 nozzle"; PROC_BASE="0.20mm Standard @BBL X1C"; PLA="Bambu PLA Basic @BBL X1C";;
    a1)     MACHINE="Bambu Lab A1 0.4 nozzle";        PROC_BASE="0.20mm Standard @BBL A1";  PLA="Bambu PLA Basic @BBL A1";;
    a1mini) MACHINE="Bambu Lab A1 mini 0.4 nozzle";   PROC_BASE="0.20mm Standard @BBL A1M"; PLA="Bambu PLA Basic @BBL A1M";;
    *) echo "unknown printer key $1 (p1s, x1c, a1, a1mini)" >&2; exit 1;;
  esac
}

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
SLICED="$HERE/sliced"; mkdir -p "$SLICED"
mkdir -p "$WORK/thumbs"
python3 "$HERE/render_thumbnails.py" "$HOUSING_STL" "$WORK/thumbs/housing" \
  || echo "  previews skipped (render_thumbnails.py needs numpy and matplotlib)"
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
  sed -E "s|\"inherits\": \"[^\"]*\"|\"inherits\": \"$PROC_BASE\"|" "$HERE/lkbx-housing-process.json" > "$W/housing-process.src.json"
  python3 "$HERE/flatten_preset.py" "$W/housing-process.src.json" "$PROFILES" "$W/housing-process.json" "$MACHINE"
  echo "  housing project"
  slicer "$W/housing" "$W/housing-process.json" "$W/filament-pla.json"  "$HOUSING_STL" housing.3mf
  cp "$W/housing/housing.3mf" "$HERE/lkbx-top-housing-snap-fit-$KEY-0.4mm.3mf"
  echo "  housing slice check"
  slicer "$W/housing-sliced" "$W/housing-process.json" "$W/filament-pla.json"  "$HOUSING_STL" housing-sliced.3mf --slice 0
  echo "  printer-ready files"
  MODEL_ID=$(python3 - "$W/machine.json" "$PROFILES" <<'PY'
import json, sys
machine = json.load(open(sys.argv[1]))
print(json.load(open("%s/machine/%s.json" % (sys.argv[2], machine["printer_model"])))["model_id"])
PY
)
  python3 "$HERE/package_sliced.py" "$W/housing-sliced/housing-sliced.3mf" "$SLICED/lkbx-top-housing-snap-fit-$KEY-0.4mm.gcode.3mf" \
    --thumbnails "$WORK/thumbs/housing" --printer-model-id "$MODEL_ID"
done
ls -la "$HERE"/*.3mf "$SLICED"
