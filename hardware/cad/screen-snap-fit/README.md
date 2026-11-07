# Screen retention without glue

The 2.0 in TFT is currently hot-glued into the `top-housing`. This design
makes the housing itself hold it, with no adhesive and no extra part:

- A **rigid catch** runs the full width of the pocket along the bottom edge
  of the glass. Its underside is a 45 degree slope that the bottom back
  corner of the glass tucks under.
- A **sprung top wall** spans the housing above the top edge. It floats
  1.6 mm above the skin so it can flex, and carries two hooks whose
  undersides are 35 degree slopes. Seated, the wall is bent back 0.30 mm and
  the hooks wedge the glass down onto the skin and into the catch.
- A **sprung side wall** with a small bump presses the glass sideways against
  two **datum ribs** on the left, 0.25 mm of preload.

Hard stops in every direction (skin, catch, datum ribs), a live spring on each
of them, and the only flexing beams are horizontal walls bridged across the
housing, which bend along their layer lines, never across them.

![Section views](snap-fit-sections.png)

## The FreeCAD document

`screen-snap-fit.FCStd` (FreeCAD 1.0) is the CAD for this change. It holds:

| Object | What it is |
| --- | --- |
| `Params` | Spreadsheet of the dimensions below. Every feature reads them by expression, so changing a cell and recomputing rebuilds the housing. |
| `TopHousing` | PartDesign body. Its base feature is the Onshape export `../top-housing.step`; on top of it, in order: `CutTopRib` and `CutRightRib` (pockets that remove the old locating ribs), `BottomBlock` and `BottomCatch` (the catch), `LeftRibs` and `LeftRibLeadIn`, `SidePosts`, `SideWall`, `SideBump` and `SideBumpLeadIn`, `TopWall`, `HookLeft` and `HookRight`. |
| `References` | The screen at its datum (from `../lockbox-assembly.step`) and a 36 x 20.5 x 8 mm battery envelope, for checking fit inside FreeCAD. |

Every sketch is fully constrained. Layout (hook positions, rib and post
spans) lives in the sketches; thicknesses, heights, preloads and angles live
in `Params`.

`build_freecad.py` built the document (`freecadcmd build_freecad.py` from
this directory) and checks the result against a plain union of boxes and
prisms. It is the record of how the geometry was derived and a regenerator;
the document is the master. If you edit the document by hand, export from it
rather than rerunning the script. `check_fit.py` (CadQuery, run from the
repository root) then puts the screen, the other assembly parts and the
battery against the export and reports every intersection, including the
insertion path.

Exports, untracked: `../top-housing-snap-fit.step` and
`../top-housing-snap-fit.stl` (print file, front face down). Export them from
FreeCAD or rerun the script.

The housing is still the Onshape export underneath. Remodelling it natively
in FreeCAD is a separate job; until then the retention features are
parametric features on the imported solid, and the dimensions below carry
into any other tool.

## Why this shape

Zero play needs a spring that stays pushed against hard stops. A clearance
fit always rattles by its clearance, and print tolerance alone is ±0.1 to
0.2 mm. The question is where the spring can live in a part printed front
face down:

- A tab that stands up from the skin bends across its layer lines at a root
  2 mm from the glass. It cannot reach 0.5 mm of travel without cracking.
- A cantilever floating above the skin cannot be printed at all without
  supports inside the pocket.
- A **wall anchored at both ends** in the outer walls of the housing can be
  bridged across the 1.6 mm gap, and bends in the plane of its layers. A
  54 mm wall of three perimeters moves 0.9 mm at its hooks with under 1 %
  strain in the PLA, and gives a firm, audible click.

So the retention is a fixed-fixed beam on two edges and rigid stops on the
other two. The wedges matter as much as the springs: because both the hook
undersides and the catch underside are slopes, the spring's push along the
glass is turned into a clamp onto the skin as well, about 9 N for 3.7 N of
spring. Push on the glass from the front and the hooks have to be cammed
back 0.9 mm against that wall; it takes roughly 30 N on the top edge of the
glass, more than 60 N at its centre, to pop the screen. From behind, a finger
on the top wall releases it on purpose.

### What changed after the second test print

The first revisions held the screen with a separate PETG keeper frame
snapping under rigid lips in the housing. The second print showed two
problems: the glass could not be fitted (the bottom lips reached 0.55 mm
over the glass with no slack at the top rib), and the keeper's thin snap
tabs felt wispy and indefinite. The round screw bosses beside the bottom
edge looked like the obstruction but clear the glass by 1.05 mm; they are
unchanged, and now merge into the catch block behind the glass.

This revision drops the keeper. The housing holds the glass directly with
the sprung walls above, the old top and right locating ribs are removed, and
the glass goes in by tilting, as a phone screen does, so the pocket no longer
has to be larger than the glass.

## How it goes together

1. Hold the screen face down, tilted 15 to 25 degrees with its bottom edge
   low and the ribbon toward the left wall. Lower it into the pocket; the
   right edge rides down the side bump's lead-in and pushes the side wall
   out 0.25 mm.
2. Slide the bottom edge under the catch until it stops, keeping the glass
   against the left ribs.
3. Press the top edge down. The glass runs down the hook ramps, pushes the
   top wall back 0.9 mm and clicks under both hooks. Push-in force is about
   14 N; the click is the wall springing forward 0.6 mm.
4. Stick the battery to the back of the screen as before, then continue with
   the encoder, PCB and cover.

To remove the screen, press the top wall back with a fingertip from inside
the housing while pushing the glass out through the window from the front;
the top edge lifts off the hooks, then the bottom edge slides out of the
catch.

## Dimensions

Housing STEP frame: Z is depth from the front face, Y along the housing, X
across it. Screen seated: X -25.35 to 22.45, Y 8.05 to 42.65, glass front at
Z 1.85 on the skin, rear face at Z 3.90, ribbon stiffener to Z 4.40.

| Feature | Value |
| --- | --- |
| Old ribs | Top rib (X ±20, Y 42.65 to 45.65) and right rib (X 22.45 to 24.85) removed to Z 4.35 (`CutTopRib`, `CutRightRib`) |
| Bottom block | Full width (into the outer walls), Y 6.9 to 7.75, Z 1.8 to 6.0 (`BottomBlock`) |
| Bottom catch | On the block, X -26.85 to 22.67; underside a 45 degree slope through the glass corner (Y 8.05, Z 3.90) to the tip at Y 9.05, Z 4.90; top Z 6.0; 1.0 mm reach over the glass; block face 0.30 mm clear of the glass edge (`BottomCatch`) |
| Left datum ribs | X -26.85 to -25.35, Y 7.7 to 20.5 and 30.2 to 42.0 (the ribbon leaves between them), Z 1.8 to 7.0, 0.6 mm lead-in chamfer (`LeftRibs`, `LeftRibLeadIn`) |
| Side posts | X 22.65 to 25.0, Y 6.9 to 9.3 and 40.6 to 42.1, Z 1.8 to 9.0 (`SidePosts`) |
| Side wall | X 22.65 to 23.91 (1.26 mm), Y 9.0 to 40.9 between the posts, Z 3.4 to 9.0, 0.20 mm clear of the glass edge (`SideWall`) |
| Side bump | On the wall, X 22.20 (0.25 mm into the glass), Y 21.4 to 29.4, 0.4 mm lead-in chamfer on top (`SideBump`, `SideBumpLeadIn`) |
| Top wall | X -27.3 to 27.3 (anchored 0.3 mm into the outer walls), 1.26 mm thick, free face at Y 42.50, Z 3.4 to 11.4 (`TopWall`) |
| Hooks | Two, X -15 to -9 and 9 to 15; free tip at Y 41.75 (0.9 mm over the glass edge), underside 35 degrees from horizontal through the glass corner when the wall is bent back 0.30 mm; 0.3 mm flat on the tip; insertion ramp 35 degrees from vertical (`HookLeft`, `HookRight`) |
| Bridge gap | 1.6 mm (Z 1.8 to 3.4) under both sprung walls |

Seated, the glass touches the skin, the catch slope along its bottom back
corner, the two hook slopes along its top back corner, the left ribs and the
side bump. Nothing else. The top wall face stays 0.15 mm clear of the glass
edge, so the hooks' slopes, not the wall, set the preload.

### Numbers

PLA, E = 3 GPa, fixed-fixed beams, hooks loaded 12 mm either side of centre.

| Element | Deflection | Force | Peak strain |
| --- | --- | --- | --- |
| Top wall, nominal preload | 0.30 mm at the hooks | 1.8 N per hook | 0.31 % |
| Top wall, tolerance extremes | 0.10 to 0.50 mm | 0.6 to 3.1 N per hook | 0.10 to 0.52 % |
| Top wall, click or release | 0.90 mm | 5.5 N per hook, 11 N total | 0.94 % (momentary) |
| Side wall, nominal / extremes | 0.25 / 0.05 to 0.55 mm | 4.4 / 0.9 to 9.6 N | 0.39 / 0.08 to 0.85 % |

Derived: push-in about 14 N; the seated glass is pushed toward the catch with
3.7 N and onto the skin with about 9 N; pushing the glass out from the front
needs about 33 N on its top edge or 66 N at its centre before the hooks cam
out (friction 0.3). A print that comes out 0.1 mm thin on every face still
leaves 0.1 mm of preload on the top wall and 0.05 mm on the side wall; that
is why both preloads are set higher than the glass tolerance alone would
need.

`check_fit.py` against `lockbox-assembly.step`: the seated glass overlaps
the free-state walls by exactly the two preloads and touches nothing else;
lifted 0.3 mm it is caught by the catch and both hooks; shifted 0.3 mm it is
stopped by the top wall face and by the left ribs; the air behind both
sprung walls is empty for 1.3 mm of travel and the bridge gap under them is
clear; the glass tilted 0 to 25 degrees with its bottom corner on the catch
slope clears every rigid feature, with only the two sprung walls giving way;
the PCB, cover, encoder, buttons, battery envelope and the two screw holes
beside the catch are as they were.

## Tuning in FreeCAD

Open `screen-snap-fit.FCStd`, edit the `Params` cell, press recompute, and
export. The cells you are most likely to touch:

| Cell | Effect |
| --- | --- |
| `HOOK_PRELOAD` | How far the top wall is bent back with the screen seated (0.30). More if the top edge can be rocked; it also moves the free wall face and hook tips toward the glass. |
| `SIDE_PRELOAD` | Side bump interference (0.25). More if the glass can be slid sideways. Keep it under `SIDE_BUMP_CHAMFER`. |
| `HOOK_REACH` | How far the hooks reach over the glass once seated (0.60). More holds harder and needs more push to fit. |
| `WALL_T` | Thickness of both sprung walls (1.26 = three 0.42 mm perimeters). 1.68 roughly doubles every spring force. |
| `TOP_WALL_TOP_Z`, `SIDE_WALL_TOP_Z` | Wall heights (11.4, 9.0). Stiffness scales with height, strain does not. |
| `HOOK_ANGLE` | Hook underside, degrees from horizontal (35). Shallower holds harder but prints as a steeper overhang. |
| `WALL_GAP` | Air under the walls (1.6). Keep it a multiple of 0.2 mm. |

Layout changes (moving a hook, lengthening a rib) are edits to the named
sketches; each is fully constrained with distances from the body origin.

## Printing

Ready-to-slice Bambu Studio projects for a 0.4 mm nozzle are in `bambu/`,
one per printer (`p1s`, `x1c`, `a1`, `a1mini`):

| Project | Part | Filament | Plate | Settings | Estimate (P1S) |
| --- | --- | --- | --- | --- | --- |
| `lkbx-top-housing-snap-fit-<printer>-0.4mm.3mf` | housing, front face down | Bambu PLA Basic | smooth PEI (High Temp) | 0.20 mm Standard, no supports, auto brim | 1 h 37 min, 51 g |

Open the project in Bambu Studio, keep the embedded presets when it asks,
confirm the printer and the 0.4 mm nozzle, swap the filament preset if the
spool is a different brand (any PLA), slice and print. The A1 variants put
the part on the 180 mm or 256 mm bed; the H2 series can open the X1C project
and switch the printer preset.

Face down on smooth PEI as today, no supports. What the slicer should show:

- At 3.4 mm from the bed, the first layer of each sprung wall is a bridge:
  54 mm across the housing for the top wall, 32 mm between the posts for the
  side wall. Bambu Studio prints those lines as fully unsupported overhang
  walls, 10 mm/s with the fan up, and warns that the object has a floating
  cantilever. That warning is these two walls; print anyway, without
  supports. Keep the part cooling fan at its PLA default.
- The catch underside is a 45 degree overhang and the hook undersides are
  55 degrees from vertical, both short. No support.
- The wall heights, bridge gap and hook flats are multiples of 0.2 mm, so
  the layers land on the design faces.

The projects embed the full printer, filament and process profiles and are
regenerated from the STL export of `build_freecad.py` by
`bambu/make_projects.sh`; `bambu/lkbx-housing-process.json` holds the only
settings that differ from Bambu's stock 0.20 mm Standard profile. Bambu
Studio's command line does not resolve preset inheritance, so the script
flattens each preset first (`bambu/flatten_preset.py`).

Printing from a phone: Bambu Handy cannot slice, so `make_projects.sh` also
writes an already sliced
`bambu/sliced/lkbx-top-housing-snap-fit-<printer>-0.4mm.gcode.3mf` per
printer (`bambu/package_sliced.py` adds the plate preview from
`bambu/render_thumbnails.py` and the printer model id the command line
leaves out). Those are not tracked; share one to the phone, open it with
Handy, pick the printer and the AMS slot holding the PLA, and print. Each is
specific to its printer and the 0.4 mm nozzle.

Tuning, in order:

- Top edge of the glass can be rocked: raise `HOOK_PRELOAD` by 0.1.
- Glass slides sideways: raise `SIDE_PRELOAD` by 0.1 (and `SIDE_BUMP_CHAMFER`
  with it).
- Screen hard to click in: first check that the bridge under the top wall
  printed clean, then lower `HOOK_REACH` to 0.5.
- Top wall bridge drooped or broke: slow the bridge speed in the process
  preset, or raise `WALL_GAP` to 2.0 so a droop still clears the skin.

## Test print checklist

- Both sprung walls move by hand and spring back; nothing under them touches
  the skin.
- Screen tilts in, the bottom edge seats under the catch, the top edge clicks
  under both hooks.
- Screen does not move or tick when the housing is shaken or tapped, and
  cannot be rocked by pressing its top edge through the window.
- Screen sits against the left ribs after seating; the ribbon leaves freely
  between them.
- Battery fits behind the screen inside the walls.
- PCB seats as before.
- Screen can be released from behind, removed and refitted three times with
  no crack at either end of the top wall.
