# Screen retention without glue

The 2.0 in TFT is currently hot-glued into the `top-housing`. This design
holds it with two parts and no adhesive:

- The **housing** gains five rigid lips over the screen pocket. Nothing in
  the housing flexes and nothing is bridged.
- A **keeper frame**, printed flat in PETG, drops in over the screen, locks
  under those lips, and spring-loads the screen onto the window with four
  leaf fingers. Two edge pushers hold it against the alignment ribs, and its
  window locates the battery.

![Section views](snap-fit-sections.png)

## The FreeCAD document

`screen-snap-fit.FCStd` (FreeCAD 1.0) is the CAD for this change. It holds:

| Object | What it is |
| --- | --- |
| `Params` | Spreadsheet of the tunable dimensions. Every feature reads them by expression, so changing a cell and recomputing rebuilds both bodies. |
| `TopHousing` | PartDesign body. Its base feature is the Onshape export `../top-housing.step`; on top of it `RaiseTopRib` (pad), `TopLips` (one pad from `TopLipsSketch`), `TopLipLeadIn` (chamfer) and the two profile pads `BottomBlockLeft` and `BottomBlockRight`. |
| `ScreenKeeper` | PartDesign body built from sketches: `Plate` (pad), `WindowCutout`, `TopSlots` and `BottomSlots` (through pockets), `ThinStrips` and `FingerLeaves` (pockets from below that thin the lip strips and the leaves), `FingerPillars`, `Feet` and `PusherTabs` (pads downward), `HookLeadIn` (chamfer). |
| `References` | The screen at its datum (from `../lockbox-assembly.step`) and a 36 x 20.5 x 8 mm battery envelope, for checking fit inside FreeCAD. |

Every sketch is fully constrained. Layout dimensions (slot positions, finger
lengths, lip positions) live in the sketches; thicknesses, heights, preload
and interference live in `Params`. The keeper body's coordinates are the
housing's, so it sits seated in the document.

`build_freecad.py` built the document (`freecadcmd build_freecad.py`) and
also checks each body against a plain union of boxes. It is the record of
how the geometry was derived and a regenerator; the document is the master.
If you edit the document by hand, export from it rather than rerunning the
script.

Exports, all untracked: `../top-housing-snap-fit.step` (housing for test
printing), `../screen-keeper.step` (keeper seated, housing coordinates) and
`../screen-keeper-print.step` (keeper flat on Z = 0, top face down, ready to
slice). Export them from FreeCAD or rerun the script.

The housing itself is still the Onshape export underneath. Remodelling it
natively in FreeCAD is a separate job; until then the lips are parametric
features on the imported solid, and the dimensions below carry into any
other tool.

## Why this shape

Zero play needs a spring that stays pushed against hard stops. A clearance
fit always rattles by its clearance, and print tolerance alone is ±0.1 to
0.2 mm. The spring has to live somewhere, and the face-down housing is the
wrong place for it: a beam that floats above the skin needs a bridge, a beam
anchored at one end cannot even be bridged, a hook that stands up from the
skin bends across layer lines, and PLA held under load creeps, quickly when
warm.

So the housing only gets rigid geometry, and every spring moves into one
flat-printed PETG part where:

- leaf fingers bend out of plane, which loads the extrusion lines along their
  length, the strongest direction an FDM part has;
- snap hooks and edge pushers bend in plane, where the beam width is set by
  the extrusion width and the beam height by the part thickness;
- every leaf runs at under 0.5 % strain at nominal preload, well below where
  PETG creeps at room temperature;
- rigid feet sit 0.15 mm above the screen, so if every finger relaxed the
  screen could still move no more than 0.15 mm and the lips would still hold
  the keeper.

The keeper can be reprinted and re-tuned without touching the housing.

## How it goes together

1. Lay the screen face down in the pocket, pushed against the top rib and
   the right rib, ribbon toward the left wall.
2. Hold the keeper tilted about 10 degrees, bottom edge up, and slide its
   top edge under the three top lips until it stops against the rib.
3. Press the bottom edge down until both snap hooks click under the bottom
   lips. The fingers compress 0.35 mm as it seats; push-in force is about
   5 N.
4. Stick the battery to the back of the screen inside the keeper's window as
   before. The window locates it.
5. Continue with the encoder, PCB and cover as before.

As the keeper seats, the two pusher tabs sweep the screen against the top and
right ribs, so the technician no longer needs to hold it aligned. To remove
the keeper, slip a flat spudger into the gap at the free end of either hook
beam, lever the beam toward the screen, lift that corner and slide the
keeper out from under the top lips.

## Dimensions

Housing STEP frame: Z is depth from the front face, Y along the housing, X
across it. Screen seated against the ribs: X -25.35 to 22.45, Y 8.05 to
42.65, rear face at Z 3.90, on the skin at Z 1.80.

### Housing (rigid)

| Feature | Value |
| --- | --- |
| Top rib | Existing rib (X ±20, Y 42.65 to 45.65) raised to Z 8.85 (`RaiseTopRib`) |
| Top lips | Three, X -17 to -11, -3 to 3, 11 to 17; underside Z 7.30; reach 1.0 mm over the pocket (to Y 41.65); 0.8 mm 45 degree chamfer on the inner top edge (`TopLips`, `TopLipLeadIn`) |
| Bottom blocks | Two, X -12 to -8 and 4 to 8; Y 5.5 (cross wall) to 7.0; Z 1.8 to 8.85 (`BottomBlockLeft`, `BottomBlockRight`) |
| Bottom lips | On each block; underside Z 7.30; reach 0.8 mm (to Y 7.8); 45 degree lead-in on top for the hooks |

No bridges: each lip is a 0.8 to 1.0 mm overhang off solid material.

### Keeper (PETG, printed flat)

| Feature | Value |
| --- | --- |
| Outline | X -26.8 to 22.3, Y 7.2 to 42.45, 3.0 mm thick (seated Z 4.3 to 7.3) |
| Battery window | X -21 to 18, Y 14.5 to 35.5 (39 x 21 mm) |
| Ribbon cutout | Left rail open from Y 19.5 to 30.5 |
| Top strip | Outer 1.0 mm (Y 41.45 to 42.45) thinned from below to 1.0 mm; goes under the top lips |
| Fingers | Four leaves 3.0 wide x 1.4 thick, 13 mm root to tip, rooted at the middle of the top and bottom rails, free ends outward; 2 x 2 mm pillars under the tips reach Z 3.55 relaxed (0.35 mm preload) at X -20.5 to -18.5 and 13 to 15 |
| Snap hooks | Two in-plane beams 1.0 wide x 3.0 tall on the bottom edge, 11 to 12 mm long; 1.0 mm thick tabs at X -12 to -8 and 4 to 8 reach Y 7.2 under the bottom lips; 0.5 mm lead-in chamfer |
| Y pusher | In-plane leaf 0.8 wide, 8 mm long, on the bottom edge; tab at X 0 to 2 reaches Z 2.6 beside the screen edge with 0.20 mm interference |
| X pusher | In-plane leaf 0.8 wide, 8 mm long, on the left edge; tab at Y 16 to 19 with 0.20 mm interference |
| Feet | Four 2 x 1.5 mm pads at the corners, underside Z 4.05 |

### Numbers

PETG, E = 2.1 GPa; PLA runs about 1.7x stiffer.

| Element | Deflection | Force | Peak strain |
| --- | --- | --- | --- |
| Each finger, nominal | 0.35 mm | 0.7 N (2.9 N total) | 0.47 % |
| Each finger, tolerance extremes | 0.15 to 0.55 mm | 0.3 to 1.2 N | 0.2 to 0.74 % |
| Snap hook, passing the lip | 0.7 mm | 0.8 N click | 0.87 % (momentary) |
| Snap hook, Z sag under its share of preload | 0.07 mm | 0.7 N | small |
| Edge pushers, nominal / worst frame offset | 0.20 / 0.55 mm | 0.3 / 0.9 N | 0.38 / 0.75 % |

Checked against `lockbox-assembly.step` with the screen at its ribs: the
seated keeper touches the screen only at the four pillars and the two pusher
tabs; the housing never touches the keeper; lifted 0.1 mm, the keeper is
caught by all five lips; a 36 x 20.5 x 8 mm battery centred in the window
clears the keeper; the PCB, cover and encoder clear everything; tilted 10
degrees with its top edge under the lips, the keeper clears the housing and
the screen.

## Tuning in FreeCAD

Open `screen-snap-fit.FCStd`, edit the `Params` cell, press recompute, and
export. The cells you are most likely to touch:

| Cell | Effect |
| --- | --- |
| `PRELOAD` | Finger interference against the screen (0.35). More if the screen still moves. |
| `FINGER_T` | Leaf thickness (1.4). Less if the keeper is hard to seat. |
| `PUSH_X`, `PUSH_Y` | Pusher interference (0.20). |
| `BACKSTOP` | Gap under the rigid feet (0.15). |
| `TAB_T` | Strip thickness under the lips (1.0); `FRAME_TOP_Z` minus the screen rear face is the lip gap. |
| `LIP_OVERLAP`, `LIP_CHAMFER`, `FEATURE_TOP_Z` | Lip reach, lead-in and height on the housing. |

Layout changes (moving a slot, lengthening a finger) are edits to the named
sketches; each is fully constrained with distances from the body origin.

## Printing

Ready-to-slice Bambu Studio projects for a 0.4 mm nozzle are in `bambu/`,
one pair per printer (`p1s`, `x1c`, `a1`, `a1mini`):

| Project | Part | Filament | Plate | Settings | Estimate (P1S) |
| --- | --- | --- | --- | --- | --- |
| `lkbx-top-housing-snap-fit-<printer>-0.4mm.3mf` | housing, front face down | Bambu PLA Basic | smooth PEI (High Temp) | 0.20 mm Standard, no supports, auto brim | 1 h 35 min, 50 g |
| `lkbx-screen-keeper-<printer>-0.4mm.3mf` | keeper, smooth face down | Bambu PETG HF | smooth PEI (High Temp) | 0.20 mm, 4 walls, 100 % infill, gap closing 0, no brim | 12 min, 2.3 g |

Open the project in Bambu Studio, keep the embedded presets when it asks,
confirm the printer and the 0.4 mm nozzle, swap the filament preset if the
spool is a different brand (any PLA for the housing, any PETG for the keeper),
slice and print. The A1 variants put the same parts on the 180 mm or 256 mm
bed; the H2 series can open the X1C project and switch the printer preset.

Housing: as today, face down on smooth PEI, no supports. Check the slicer
preview at 7.3 mm from the bed: the five lip undersides should print as
short overhangs, not need support.

Keeper: smooth (top) face on the bed, 0.2 mm layers, PETG, 4 walls or 100 %
infill. Every feature grows upward from that face, so no supports and no
bridges. Keep the 0.5 mm slots open: the project sets the gap closing radius
to zero. Weigh it: about 2.3 g.

The projects embed the full printer, filament and process profiles and are
regenerated from the STL exports of `build_freecad.py` by
`bambu/make_projects.sh`; the two `lkbx-*-process.json` files there hold the
only settings that differ from Bambu's stock 0.20 mm Standard profile. Bambu
Studio's command line does not resolve preset inheritance, so the script
flattens each preset first (`bambu/flatten_preset.py`).

Tuning, in order:

- Screen still moves: raise `PRELOAD` by 0.1.
- Keeper hard to seat or the hooks do not click: check that the slots
  printed open, then reduce `FINGER_T` to 1.2 to soften the fingers.
- Keeper sits proud of the lips: lips too low or the strip too thick; measure
  the strip (1.0 mm) and the lip gap (7.30 - 3.90 = 3.40 mm from the screen).

## Test print checklist

- Keeper slots are open; each finger, hook and pusher moves by hand.
- Keeper slides under the top lips and both hooks click.
- Screen does not move or tick when the housing is shaken or tapped.
- Screen sits against the top and right ribs after seating.
- Battery fits in the window and its cable clears the rail.
- PCB seats without touching the keeper or lips.
- Keeper can be removed and refitted three times without a cracked beam.
