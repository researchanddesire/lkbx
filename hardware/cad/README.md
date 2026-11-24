# Lockbox CAD

Open `lockbox-assembly.FCStd` in FreeCAD 1.1.4. The assembly retains the 12 component placements from the June 12, 2026 Onshape `LockBox V1` export and adds the SW3 slider and snap-in keeper. The bottom housing is grounded. The upper housing, slider and keeper are editable Part Design bodies.

The hidden **Component Sources** group contains one embedded source for each link, so the assembly opens on its own. The battery, stand, PCB spacer washer and Part 13 have separate exports. The original STEP records the exported geometry and placements.

When changing a part, rebuild it from dimensioned base sketches and native FreeCAD features. Preserve the part's local coordinate frame or adjust its assembly link placement to fit. Point the existing assembly link at the new source and verify the installed geometry. Then remove the old imported source and its standalone STEP file, and update any BOM or documentation links that referenced the file. Keep `lockbox-assembly.step` as the reference for the original assembly.

## Recessed SW3 upper housing

The keeper has a hooked toe at its rigid end. The toe slides into an undercut groove behind a lip in the upper housing. The flexible end snaps behind a square shoulder on a gusseted sidewall ledge, holding the toe in the groove. The keeper prints flat without support. The housing and slider can print together with removable support under the slider, or as separate parts.

Center the slider. Lower the keeper through the open housing about 6 mm inboard and 1.2 mm toward the key end from its final position. Push it toward the switch wall, leaving about 0.5 mm before it seats. Hold the flexible hook toward the slider with a pick while you slide the keeper toward the USB end to engage the toe in the groove. Push the keeper the rest of the way toward the wall and release the hook behind the ledge. Match the slider to the switch detent before lowering the PCB so the switch tab enters the socket.

To remove the keeper, remove the PCB, press the flexible hook toward the slider with a pick, and pull the keeper inward slightly. Slide it toward the key end to disengage the toe from the groove, then withdraw it.

The screen-end sidewall has a rounded 9.2 × 4.5 mm depression at the floor, 0.84 mm deep. The grooved grip sits 0.10 mm below the outside wall in its nominal position. A crescent engraved 0.35 mm deep marks the key side of the slider.

Print the upper housing and captive slider together from the combined STL, with the front face on the bed. Keep both solids positioned as exported. Both grip grooves run edge to edge across the thumb face. The 10 mm flange overlaps both housing shoulders throughout the guide travel.

Clear the removable print support through the open housing before fitting the board. Work the thumb slider through its travel. Match its position to the SW3 detent before lowering the board so the switch tab enters the socket. The switch location follows the [v1.5 KiCad board](https://github.com/researchanddesire/Lockbox-Hardware/blob/7203115d25404c78131a4b004471412b9711af1f/PCB/LockBox.kicad_pcb) and the [C&K JS202011AQN drawing](https://www.littelfuse.com/assetdocs/littelfuse-ck-slide-js-series-datasheet?assetguid=aba42b08-0d2c-423b-813d-a2faa5a3bb14).

The test slice used a 0.4 mm nozzle, 0.20 mm layers, 0.24 mm support contact distance and 0.45 mm support XY spacing. FreeCAD checked keeper insertion, independent toe retention, 15 slider positions and PCB switch-tab insertion at both detents. The tightest nominal clearance during sideways seating is 0.12 mm. Verify printed fit, slider release, retention and both detents on the physical print. SW3 selects an open circuit or a 10 kΩ pull-down on GPIO8. The current firmware has no SW3 deep-sleep handling.

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
