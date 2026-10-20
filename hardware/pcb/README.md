# Lockbox PCB

This directory contains the Lockbox PCB source imported from
`researchanddesire/Lockbox-Hardware` at commit `d19d84b`.

## Contents

- `LockBox.kicad_pro`, `LockBox.kicad_sch`, and `LockBox.kicad_pcb` - KiCad project, schematic, and board layout.
- `battery_and_power.kicad_sch`, `mag_sensors.kicad_sch`, `mcu_and_ui.kicad_sch`, `motor.kicad_sch`, and `screen.kicad_sch` - schematic sheets.
- `Lockbox.pretty/` and `Lockbox.kicad_sym` - local footprints and symbols used by the project.
- `3dmodels/` and `LockBox_v0.1.step` - component and board STEP model exports.
- `datasheets/` - component reference PDFs included with the hardware source.
- `production/` - fabrication packages, BOMs, and position files for released PCB revisions.
- `FAB_OUTPUT.md` - notes for generating JLC-style fabrication outputs from KiCad.

Mechanical enclosure CAD should live under `hardware/cad/` when published.

## Design Notes

Lockbox uses an ESP32-S3 board to control the lock motor and GUI. The board has
magnetic sensors for detecting whether the box is open or closed, plus wiper
position sensing. The GUI uses an ST7789W LCD screen, a scroll wheel, and two
buttons. The device has an internal battery charged through USB-C.

## Revision History

- `v0.1` - 2024-07-31 first prototype.
- `v1.0` - 2024-08-20 production run: swapped the encoder, adjusted positions, removed the spare button and unneeded motor voltage measurement.
- `v1.1` - swapped Enter and MTR1 pins to allow deep sleep, rotated and moved the battery connector, added a diode-OR option to let 3.3V power the motor in low-battery conditions.
- `v1.2` - added pulldown to the motor driver mode pin, added a second 3.3V regulator for accessories on IO48 enable, added ST6519 reset manager tied to 3.3V ESP32 power enable, moved MTR1 and MTR2 away from IO45/IO46.
- `v1.3` - reverted motor pins and added a 10k pullup from 3.3 enable to Battery+.
- `v1.4` - fixed discharge FET orientation and marked the discharge resistor DNP.

## License

Hardware designs in this directory are licensed under CERN-OHL-S v2. See
`LICENSES/CERN-OHL-S-2.0.txt` from the repository root.
