"""Add glue-free screen retention to the exported top housing.

Reads ``hardware/cad/top-housing.step`` (the Onshape export) and writes
``hardware/cad/top-housing-snap-fit.step``. The screen hooks under three
fixed lips on its top edge, then its bottom edge presses down over one
flexing latch tongue, so it no longer needs hot glue. See README.md in this
directory for the design rationale, print notes and the dimensions to carry
back into Onshape.

Coordinates are the top-housing STEP frame: Z is depth from the front face
(the face printed on the bed), Y runs along the housing length and X across
its width. Dimensions are millimetres.

Run with CadQuery 2.x:  python build_top_housing_snap_fit.py
"""

from pathlib import Path

import cadquery as cq

CAD_DIR = Path(__file__).resolve().parent.parent
SOURCE = CAD_DIR / "top-housing.step"
OUTPUT = CAD_DIR / "top-housing-snap-fit.step"

# Screen module (2.0 in 320x240 TFT) as seated in the housing.
SCREEN_Y_MIN = 7.85  # long edge nearest the encoder
SCREEN_BACK_Z = 3.85  # rear face of the glass/backlight stack
SKIN_Z = 1.80  # inner face of the front skin the screen rests on

# Shared retention geometry.
RETAIN_Z = SCREEN_BACK_Z + 0.20  # underside of every lip and catch
FEATURE_TOP_Z = 6.00
EDGE_CLEARANCE = 0.25  # screen edge to the latch face

# Top edge: the existing locating rib grows three fixed lips.
TOP_RIB_X = (-20.0, 20.0)
TOP_RIB_Y = (42.65, 45.65)
LIP_OVERLAP = 1.00  # how far each lip reaches over the screen's back
LIP_X_RANGES = ((-17.0, -11.0), (-3.0, 3.0), (11.0, 17.0))
LIP_CHAMFER = 0.80  # 45 degree lead-in on the lip's inner top edge

# Bottom edge: a cantilever tongue between the two cover-screw bosses,
# anchored at the left boss, flexing toward the cross wall behind it.
CROSS_WALL_Y = 5.50  # face of the wall between the screen and encoder
TONGUE_THICKNESS = 1.60  # four 0.4 mm perimeters
TONGUE_X = (-13.0, 9.0)  # anchor end to free end
ANCHOR_X = (-16.0, -13.0)
TONGUE_BOTTOM_Z = 3.00  # 1.2 mm bridged slot above the skin
CATCH_X = (2.0, 8.0)
CATCH_OVERLAP = 0.75
CATCH_LAND = 0.30
WALL_RELIEF_X = (-2.0, 10.0)  # room behind the tongue so it can flex
WALL_RELIEF_DEPTH = 0.60

LATCH_FACE_Y = SCREEN_Y_MIN - EDGE_CLEARANCE
TONGUE_BACK_Y = LATCH_FACE_Y - TONGUE_THICKNESS


def box(x, y, z):
    """Axis-aligned box from (min, max) pairs."""
    return (
        cq.Workplane("XY")
        .box(x[1] - x[0], y[1] - y[0], z[1] - z[0], centered=False)
        .translate((x[0], y[0], z[0]))
    )


def yz_prism(x, profile):
    """Prism along X from a closed (y, z) profile."""
    return (
        cq.Workplane("YZ", origin=(x[0], 0, 0))
        .polyline(profile)
        .close()
        .extrude(x[1] - x[0])
    )


def lip(x):
    """Fixed lip hanging over the screen's top edge, chamfered toward the pocket."""
    inner_y = TOP_RIB_Y[0] - LIP_OVERLAP
    return yz_prism(
        x,
        [
            (TOP_RIB_Y[0], RETAIN_Z),
            (inner_y, RETAIN_Z),
            (inner_y, FEATURE_TOP_Z - LIP_CHAMFER),
            (inner_y + LIP_CHAMFER, FEATURE_TOP_Z),
            (TOP_RIB_Y[0], FEATURE_TOP_Z),
        ],
    )


def catch():
    """Latch catch on the tongue face with a 45 degree lead-in."""
    tip_y = LATCH_FACE_Y + EDGE_CLEARANCE + CATCH_OVERLAP
    ramp_run = tip_y - LATCH_FACE_Y
    return yz_prism(
        CATCH_X,
        [
            (LATCH_FACE_Y, RETAIN_Z),
            (tip_y, RETAIN_Z),
            (tip_y, RETAIN_Z + CATCH_LAND),
            (LATCH_FACE_Y, RETAIN_Z + CATCH_LAND + ramp_run),
        ],
    )


def build():
    housing = cq.importers.importStep(str(SOURCE))

    # Top edge: raise the rib and add the lips.
    housing = housing.union(box(TOP_RIB_X, TOP_RIB_Y, (SKIN_Z, FEATURE_TOP_Z)))
    for x in LIP_X_RANGES:
        housing = housing.union(lip(x))

    # Bottom edge: relieve the wall, then add the anchor, tongue and catch.
    housing = housing.cut(
        box(
            WALL_RELIEF_X,
            (CROSS_WALL_Y - WALL_RELIEF_DEPTH, CROSS_WALL_Y + 0.01),
            (TONGUE_BOTTOM_Z, FEATURE_TOP_Z + 0.5),
        )
    )
    housing = housing.union(
        box(ANCHOR_X, (CROSS_WALL_Y - 0.01, LATCH_FACE_Y), (SKIN_Z, FEATURE_TOP_Z))
    )
    housing = housing.union(
        box(TONGUE_X, (TONGUE_BACK_Y, LATCH_FACE_Y), (TONGUE_BOTTOM_Z, FEATURE_TOP_Z))
    )
    housing = housing.union(catch())

    return housing


if __name__ == "__main__":
    result = build()
    cq.exporters.export(result, str(OUTPUT))
    print(f"wrote {OUTPUT.relative_to(CAD_DIR.parent.parent)}")
