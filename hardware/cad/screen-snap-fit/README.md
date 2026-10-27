# Screen snap-fit

The 2.0 in TFT is currently hot-glued into the `top-housing` against two
locating ribs. This variant holds it with two flexing latch beams instead, so
the screen presses into its pocket and clicks into place with no glue.

- `build_top_housing_snap_fit.py` applies the change to the Onshape export
  `../top-housing.step` and writes `../top-housing-snap-fit.step` for test
  printing. Run it with CadQuery 2.x (`pip install cadquery`); the output is
  not tracked.
- `snap-fit-sections.png` shows a plan section of the pocket and a section
  through each latch.

`top-housing.step` is untouched. Once a test print is approved, carry the
dimensions below into the Onshape part and re-export `top-housing.step`; the
assembly guide keeps the hot-glue step until then.

![Snap-fit section views](snap-fit-sections.png)

## How it works

The screen still drops face down onto the inside of the front skin. Each long
edge of the screen now meets a thin beam fixed at both ends. A catch with a
30 degree lead-in sits in the middle of each beam. Pressing the screen in pushes
both beams outward; once the glass passes the catches they spring back and the
flat underside of each catch sits 0.1 mm above the rear of the screen.

The beams stand on a 0.6 mm slot above the skin, so they bend sideways in the
print plane. With the front face on the bed, that bending runs along the
perimeters rather than across layer lines, which is what makes PLA latches
survive here. Upright cantilever hooks in this pocket would be under 2.5 mm
tall and would snap at a layer line.

The short edges are located but not latched: the existing side rib on one end
and the housing wall at the ribbon end (1.5 mm gap kept clear for the ribbon
fold).

## Dimensions

Housing STEP frame: Z is depth from the front face, Y runs along the housing
and X across it. Screen seated at Y 7.85 to 42.45, rear face at Z 3.85.

| Feature | Value |
| --- | --- |
| Beam thickness (Y) | 0.80 mm |
| Beam height | Z 2.40 to 6.00 mm (0.60 mm bridged slot above the skin at Z 1.80) |
| Screen edge to beam face | 0.20 mm |
| Catch overlap onto the rear of the screen | 0.50 mm |
| Catch underside | Z 3.95 mm (0.10 mm above the screen) |
| Catch tip land | 0.20 mm |
| Lead-in | 30 degrees from the press direction, 1.20 mm tall |
| Top beam | Free span X -15 to 15, catch X -4 to 4. Replaces the middle of the old top rib; the rib's outer 5 mm become anchors raised to Z 6.0 |
| Bottom beam | Free span X -13.5 to 9.5 between the two cover-screw bosses, catch X -5 to 1. Anchor blocks fill from the bosses to the cross wall |

Peak bending strain at full deflection is about 0.7 % on the top beam and
1.1 % on the bottom beam, well inside PLA's elastic range. The flat catch
holds the screen by geometry, not friction.

Checked against `lockbox-assembly.step`: the seated screen has no
interference with the new features, and none of the other assembly parts
touch them. Lifted 0.5 mm or more, the screen meets only the catches.

## Printing

- Print face down on the smooth PEI bed as today. No supports.
- The beams start with a bridge over the 0.6 mm slot (about 30 mm on the top
  beam and 23 mm on the bottom). Check in the slicer preview that the slot is
  open and the first beam layer is printed as a bridge.
- At 0.2 mm layers the catch underside lands on the 4.0 mm layer, leaving
  about 0.15 mm of play. If the screen rattles, lower `CATCH_Z_CLEARANCE`.
- If the beams feel too stiff or the screen will not seat, reduce
  `CATCH_OVERLAP` to 0.4 mm before thinning the beams.

## Assembly

1. Remove the protective film and set the screen face down over the pocket
   with the ribbon toward the housing wall.
2. Press evenly on the rear of the screen near both long edges until both
   latches click.
3. Apply the battery to the rear of the screen as before.

To remove the screen, push the middle of the top beam outward with a plastic
spudger and lift that edge.

## Test print checklist

- Slot under each beam is open and the beam flexes by hand.
- Screen seats flat with both latches engaged and the active area centred in
  the window.
- No rattle when the housing is shaken.
- The battery still fits on the rear of the screen and the PCB seats without
  touching the beams.
- The screen can be removed and reinstalled without cracking a beam.
