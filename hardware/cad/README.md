# Lockbox CAD

This directory contains STEP exports from the Lockbox Onshape document. Each file is a single exported component named with the assembly BOM slug style.

## Assembly STEP files

- `lockbox-assembly.step`

## Component STEP files

- `back-button.step`
- `battery.step`
- `bottom-housing.step`
- `encoder-wheel.step`
- `encoder.step`
- `enter-button.step`
- `lockbox-stand.step`
- `locking-latch.step`
- `motor-clamp.step`
- `motor.step`
- `part-13.step`
- `pcb-cover.step`
- `pcb-spacer-washer.step`
- `pcb.step`
- `screen.step`
- `top-housing.step`

## Variants

- `screen-snap-fit/` - glue-free screen retention, modelled in FreeCAD
  (`screen-snap-fit.FCStd`): a `TopHousing` body that reworks the imported
  `top-housing.step` so the housing itself holds the screen, with a rigid
  catch along the bottom edge, a sprung top wall carrying two hooks, a sprung
  side wall and two datum ribs, all driven by a `Params` spreadsheet. No
  separate keeper part. The STEP export (`top-housing-snap-fit.step`) and the
  print STL (`top-housing-snap-fit.stl`) are not tracked; `build_freecad.py`
  regenerates them and `check_fit.py` verifies the fit against the assembly.
  Ready-to-slice Bambu Studio projects for the test print live in
  `screen-snap-fit/bambu/`; see `screen-snap-fit/README.md`. Not yet carried
  into Onshape.
