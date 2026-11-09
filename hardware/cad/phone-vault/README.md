# Phone vault

A tall bottom housing whose lower cavity takes a large phone or a king-size
cigarette pack, behind honeycomb windows that print without supports.
The top of the part is the original `bottom-housing.step`, unchanged, so the
stock top housing slides on, latches and detects it exactly as before. The
original's emergency escape is kept as an emergency bypass: a rod printed in
place up the latch end, pushed in through a marked square in the base.

![Phone vault renders](phone-vault-render.png)

![Sections with the phone and cigarette pack envelopes](phone-vault-sections.png)

![Honeycomb on a long wall](phone-vault-wall.png)

## Dimensions

| | mm |
| --- | --- |
| Overall height (vault alone, as printed) | 178.5 |
| Height with the top housing on | about 202 |
| Wall footprint | 60 x 121, the same as the original |
| Base foot footprint | 84 x 145 |
| Opening through the lock interface | 48 x 89.5 |
| Cavity below the opening | 52 x 113 |
| Usable depth, slide-channel floor to cavity floor | 170 |
| Honeycomb cells | 7 wide, 3 mm vertical sides, 60 degree roofs and floors |
| Webs between cells | 3.5 (vertical and slanted), through the 4 mm wall |
| Emergency bypass rod | 6 x 5 tip, 10 x 8 body, 168 long |
| Rod clearance in its shaft | 1.0 all round |
| Rod travel | 10 to the stop (about 8 lifts the latch clear) |
| Bypass shaft in the cavity | 15.2 x 11.6, against the latch end wall |
| Punch-out in the base | 10 x 8, 0.3 slit, 0.2 skin |

The cavity is sized for a 165 x 78 x 9 mm phone in a case (a 170 x 82 x 14
envelope), standing on end, which covers the largest current phones (about 163
x 78 mm), and for an 88 x 55 x 22 mm king-size pack standing on end. Both pass
the opening and clear the bypass shaft (the phone by 6 mm). Seven-millimetre
cells keep a single cigarette (7.8 mm) from being pulled out. The honeycomb
replaced an earlier set of full-height bars, which were easy to snap by hand:
every web is now short, 3.5 mm thick and tied into the lattice on both ends.

## What changed against the original bottom housing

- Everything above `CUT_Z` (Z 41, the floor of the latch slot) is the
  original part: the slide rails, the latch slot, the front magnet pocket and
  the end stop. The build checks that nothing there changes except the
  opening, the back magnet pocket, two fills and the tip of the bypass rod,
  and that the top housing, locking latch and PCB cover overlap the vault no
  more than the original (they do not overlap either).
- The original's escape plug sat just below `CUT_Z`, under the latch, so the
  vault replaces it with the emergency bypass described below.
- The key tray is replaced by an opening into the vault, then a transition
  out to 4 mm walls. No transition face is flatter than 45 degrees.
- **The back magnet now goes in from the top**, in a pocket the same size and
  depth as the front magnet pocket (4.35 mm diameter, floor at Z 34.75), at the
  same XY as before. The original pressed it in from the underside, which the
  vault closes. It sits as close to the top housing as before or closer.
  Install it south side up, as the assembly guide says.
- The old bore under that magnet and the stand clip slot are filled; below the
  cut they would have been sealed voids. The stand does not fit this part.
- The rubber feet glue onto the flat base, with no recesses. Keep them off the
  bypass marking and the punch-out.

## Emergency bypass

![The emergency bypass: rod, tabs, punch-out and marking](phone-vault-escape.png)

The original bottom housing has an escape that is not in the assembly guide: a
plug in the floor under the latch slot, cut free by 0.3 mm slits and held only
by the first printed layer. Pushing it in from outside lifts the latch on the
side away from its pivot, so the latch swings out of its slot and the top
housing slides off. The vault keeps the same escape, 165 mm further down:

- A sealed shaft runs down the latch end of the cavity, against the end wall.
  A rod is printed in place inside it, standing, with 1 mm of clearance on
  every side. Its 6 x 5 mm tip comes up through the slot floor and stops
  1.2 mm under the latch, where the original plug would have pushed.
- The rod's foot is the punch-out: it fills a 10 x 8 mm hole in the base,
  cut free by a 0.3 mm slit and held by the 0.2 mm first layer, like the
  original plug.
- Ten breakaway tabs (0.5 x 0.4 mm, two at each of five heights) tie the rod
  to its shaft, so it prints straight and does not rattle. A push snaps them
  together with the skin.
- The latch is clear of its slot after about 8 mm of travel (50 degrees of
  turn). At 10 mm a 45 degree shoulder on the rod meets a 45 degree ceiling
  in the shaft, so the rod cannot be driven on up into the top housing, and
  the latch stops at about 60 degrees, short of the top housing.
- The end-wall cells open into the shaft, so the rod can be inspected from
  outside.
- The base is marked EMERGENCY BYPASS / PUSH IN, with a frame round the
  punch-out and an arrow to it, sunk 0.6 mm into the bed face.

To use it, push the marked square in with a screwdriver, a pencil or anything
up to 7 mm across, and keep pushing until the rod stops (10 mm), then slide
the top housing off. Once the punch-out is through, the hole stays open and the rod is loose,
so the vault no longer locks reliably: print a new one.

The build checks that the rod sits 0.8 to 2 mm under the latch, that the
latch keeps clear of the rod tip over its whole swing (8.5 degrees one way to
the slot wall, 90 degrees the other; the tightest gap is 0.45 mm, at the slot
wall), that the rod clears its shaft by 1 mm apart from the tabs, that it
travels 9 mm freely and has turned the latch out of its slot by then, and
that the shoulder stops it within 2 mm more.

### Test piece

`phone-vault-escape-test-print.stl` is the latch end of the vault on its own
(26 x 35 x 169 mm): the shaft, the rod, the punch-out and the end wall. Print
it before the vault, with a brim, and check that:

1. the rod is free all round (look through the end-wall cells) and does not
   rattle,
2. a firm push with a screwdriver breaks the punch-out and the tabs, without
   needing a hammer, and
3. the rod then slides about 10 mm and stops on its shoulder.

If the rod has fused to the shaft wall, or the push is too hard, change
`ESC_CLEAR`, `ESC_TIE_W` or `ESC_TIE_H` and rebuild before printing the vault.

## Printing

Print upright on the base, opening up: the same way up as the original bottom
housing, so the rail hooks print as they always have. Below the interface
every downward-facing surface is at most 45 degrees from vertical (the build
measures 0 mm² steeper); each honeycomb cell has vertical sides and pointed
60 degree roofs, and the foot is a 45 degree chamfer. No supports are needed. It fits the P1S, X1C and
A1 (256 mm Z) and, at 178.5 mm tall, the A1 mini (180 mm Z).

The only bridges are small: the breakaway tabs span the 1 mm gap round the
rod, and the layer above the sunk marking spans its letter strokes. Print
with a 0.2 mm first layer (the Bambu default) so the punch-out skin is one
layer, as designed.

## Regenerating

`build_freecad.py` builds `phone-vault.FCStd` (a `Params` spreadsheet drives
the features by expression, every sketch is fully constrained) and writes
`hardware/cad/phone-vault.step` (housing frame),
`hardware/cad/phone-vault-print.stl` and
`hardware/cad/phone-vault-escape-test-print.stl` (base on Z = 0). The
exports are gitignored.

```
LKBX_CAD_DIR=/path/to/real/step/files freecadcmd build_freecad.py
```

`LKBX_CAD_DIR` must hold real copies of `bottom-housing.step`,
`top-housing.step` and `lockbox-assembly.step` when the Git LFS copies are not
checked out. The honeycomb layout is generated by the script; rerun it after
changing `HEX_W`, `HEX_SIDE`, `WEB`, `ROOF_ANGLE` or the window limits. The
bypass is driven by the `ESC_*` and `MARK_*` parameters; the marking text,
font and tab heights are constants near the top of the script.
