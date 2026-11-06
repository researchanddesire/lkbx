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
  (`screen-snap-fit.FCStd`): a `TopHousing` body that adds rigid lips to the
  imported `top-housing.step`, a native `ScreenKeeper` body for the PETG
  keeper frame that snaps under those lips and spring-loads the screen, and a
  `Params` spreadsheet that drives both. The STEP exports
  (`top-housing-snap-fit.step`, `screen-keeper.step`,
  `screen-keeper-print.step`) and the print STLs
  (`top-housing-snap-fit.stl`, `screen-keeper-print.stl`) are not tracked;
  `build_freecad.py` regenerates them. Ready-to-slice Bambu Studio projects
  for the test print live in `screen-snap-fit/bambu/`; see
  `screen-snap-fit/README.md`. Not yet carried into Onshape.
