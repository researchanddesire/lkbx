"""Add screen snap-fit latches to the exported top housing.

Reads ``hardware/cad/top-housing.step`` (the Onshape export) and writes
``hardware/cad/top-housing-snap-fit.step``. The screen drops face down into
its pocket and is held by two flexing latch beams, one on each long edge, so
it no longer needs hot glue. See README.md in this directory for the design
rationale, print notes and the dimensions to carry back into Onshape.

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
SCREEN_Y_MAX = 42.45  # long edge nearest the top of the housing
SCREEN_BACK_Z = 3.85  # rear face of the glass/backlight stack
SKIN_Z = 1.80  # inner face of the front skin the screen rests on

# Latch beams.
EDGE_CLEARANCE = 0.20  # screen edge to beam face
BEAM_THICKNESS = 0.80  # two 0.4 mm perimeters; flexes in the print plane
FLOOR_GAP = 0.60  # bridged slot between beam and skin so the beam can flex
BEAM_TOP_Z = 6.00
CATCH_OVERLAP = 0.50  # how far the catch reaches over the screen's back
CATCH_Z_CLEARANCE = 0.10  # catch underside above the screen's back face
CATCH_LAND = 0.20  # vertical land at the catch tip
LEAD_IN_RUN = 1.20  # Z run of the 30 degree lead-in ramp

BEAM_BOTTOM_Z = SKIN_Z + FLOOR_GAP
CATCH_Z = SCREEN_BACK_Z + CATCH_Z_CLEARANCE

# Top edge: the existing locating rib becomes two anchors and a beam.
TOP_RIB_X = (-20.0, 20.0)
TOP_RIB_Y = (42.65, 45.65)
TOP_RIB_TOP_Z = 4.30
TOP_ANCHOR_WIDTH = 5.0
TOP_CATCH_X = (-4.0, 4.0)

# Bottom edge: a new beam spans between the two cover-screw bosses.
BOSS_CENTERS_X = (-17.0, 13.0)
CROSS_WALL_Y = 5.50  # face of the wall between the screen and encoder
BOTTOM_ANCHOR_WIDTH = 3.5
BOTTOM_CATCH_X = (-5.0, 1.0)


def box(x, y, z):
    """Axis-aligned box from (min, max) pairs."""
    return (
        cq.Workplane("XY")
        .box(x[1] - x[0], y[1] - y[0], z[1] - z[0], centered=False)
        .translate((x[0], y[0], z[0]))
    )


def catch(x, face_y, toward):
    """Latch catch on a beam face; ``toward`` is +1 or -1 in Y (into the screen)."""
    tip_y = face_y + toward * (EDGE_CLEARANCE + CATCH_OVERLAP)
    profile = [
        (face_y, CATCH_Z),
        (tip_y, CATCH_Z),
        (tip_y, CATCH_Z + CATCH_LAND),
        (face_y, CATCH_Z + CATCH_LAND + LEAD_IN_RUN),
    ]
    return (
        cq.Workplane("YZ", origin=(x[0], 0, 0))
        .polyline(profile)
        .close()
        .extrude(x[1] - x[0])
    )


def build():
    housing = cq.importers.importStep(str(SOURCE))

    # Top edge.
    top_face_y = SCREEN_Y_MAX + EDGE_CLEARANCE
    free_x = (TOP_RIB_X[0] + TOP_ANCHOR_WIDTH, TOP_RIB_X[1] - TOP_ANCHOR_WIDTH)
    housing = housing.cut(box(free_x, TOP_RIB_Y, (SKIN_Z, TOP_RIB_TOP_Z + 0.01)))
    for anchor_x in ((TOP_RIB_X[0], free_x[0]), (free_x[1], TOP_RIB_X[1])):
        housing = housing.union(box(anchor_x, (top_face_y, TOP_RIB_Y[1]), (SKIN_Z, BEAM_TOP_Z)))
    housing = housing.union(
        box(free_x, (top_face_y, top_face_y + BEAM_THICKNESS), (BEAM_BOTTOM_Z, BEAM_TOP_Z))
    )
    housing = housing.union(catch(TOP_CATCH_X, top_face_y, -1))

    # Bottom edge.
    bottom_face_y = SCREEN_Y_MIN - EDGE_CLEARANCE
    left, right = BOSS_CENTERS_X
    free_x = (left + BOTTOM_ANCHOR_WIDTH, right - BOTTOM_ANCHOR_WIDTH)
    for anchor_x in ((left, free_x[0]), (free_x[1], right)):
        housing = housing.union(box(anchor_x, (CROSS_WALL_Y, bottom_face_y), (SKIN_Z, BEAM_TOP_Z)))
    housing = housing.union(
        box(free_x, (bottom_face_y - BEAM_THICKNESS, bottom_face_y), (BEAM_BOTTOM_Z, BEAM_TOP_Z))
    )
    housing = housing.union(catch(BOTTOM_CATCH_X, bottom_face_y, +1))

    return housing


if __name__ == "__main__":
    result = build()
    cq.exporters.export(result, str(OUTPUT))
    print(f"wrote {OUTPUT.relative_to(CAD_DIR.parent.parent)}")
