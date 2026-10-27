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

- `screen-snap-fit/` - glue-free screen retention. Generates
  `top-housing-snap-fit.step` (the `top-housing` with rigid lips over the
  screen pocket), `screen-keeper.step` (a new PETG keeper frame that snaps
  under those lips and spring-loads the screen, seated in housing
  coordinates) and `screen-keeper-print.step` (the keeper flat for slicing).
  The generated STEPs are not tracked; see `screen-snap-fit/README.md`. Not
  yet carried into Onshape.
