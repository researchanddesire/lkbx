"""Glue-free screen retention: rigid lips on the top housing plus a keeper frame.

Reads ``hardware/cad/top-housing.step`` (the Onshape export) and writes:

- ``hardware/cad/top-housing-snap-fit.step``: the housing with rigid lips over
  the screen pocket. Nothing in the housing flexes and nothing is bridged.
- ``hardware/cad/screen-keeper.step``: the keeper frame in housing coordinates,
  seated, for the Onshape assembly.
- ``hardware/cad/screen-keeper-print.step``: the same frame flat on Z = 0 with
  its top face down, ready to slice.

The screen hooks under three lips on its top edge and lies on the front skin.
The keeper frame drops in over it: its top edge hooks under the same lips,
two in-plane snap hooks on its bottom edge click under two more lips, and four
leaf fingers press the screen onto the skin while two edge pushers hold it
against the alignment ribs. See README.md in this directory.

Coordinates are the top-housing STEP frame: Z is depth from the front face
(the face printed on the bed), Y runs along the housing length and X across
its width. Dimensions are millimetres.

Run with CadQuery 2.x:  python build_top_housing_snap_fit.py
"""

from pathlib import Path

import cadquery as cq

CAD_DIR = Path(__file__).resolve().parent.parent
SOURCE = CAD_DIR / "top-housing.step"
HOUSING_OUT = CAD_DIR / "top-housing-snap-fit.step"
KEEPER_OUT = CAD_DIR / "screen-keeper.step"
KEEPER_PRINT_OUT = CAD_DIR / "screen-keeper-print.step"

# Screen module (2.0 in 320x240 TFT) as seated against the top and right ribs.
SCREEN_X = (-25.35, 22.45)
SCREEN_Y = (8.05, 42.65)
SCREEN_BACK_Z = 3.90  # rear face of the metal backlight frame (2.1 mm module)
SKIN_Z = 1.80  # inner face of the front skin the screen rests on

# Keeper frame, seated and floating up against the lips.
FRAME_TOP_Z = 7.30  # lip undersides; the fingers push the frame up to here
FRAME_T = 3.00
FRAME_BOT_Z = FRAME_TOP_Z - FRAME_T
TAB_T = 1.00  # strips that go under the lips, thinned from below
FINGER_T = 1.40  # leaf fingers, thinned from below
FINGER_BOT_Z = FRAME_TOP_Z - FINGER_T
BUMP_TIP_Z = SCREEN_BACK_Z - 0.35  # relaxed finger tip: 0.35 mm preload
FOOT_Z = SCREEN_BACK_Z + 0.15  # rigid backstop if every finger relaxed
PUSHER_TIP_Z = 2.60  # edge pusher tabs reach down beside the screen edge
PUSH_X = 0.20  # X pusher interference against the seated screen
PUSH_Y = 0.20  # Y pusher interference against the seated screen

FRAME_X = (-26.8, 22.3)
FRAME_Y = (8.2, 42.45)
WINDOW_X = (-21.0, 18.0)  # battery pocket
WINDOW_Y = (14.5, 35.5)
FPC_CUTOUT_Y = (19.5, 30.5)  # ribbon exits the screen's left edge here

# Housing lips. Top lips sit on the raised locating rib; bottom lips sit on
# two new blocks that tie into the floor and the cross wall.
TOP_RIB_X = (-20.0, 20.0)
TOP_RIB_Y = (42.65, 45.65)
TOP_LIP_X = ((-17.0, -11.0), (-3.0, 3.0), (11.0, 17.0))
LIP_OVERLAP = 1.00
LIP_CHAMFER = 0.80
FEATURE_TOP_Z = FRAME_TOP_Z + 1.55
CROSS_WALL_Y = 5.50
BOTTOM_BLOCK_X = ((-12.0, -8.0), (4.0, 8.0))
BOTTOM_BLOCK_Y = (CROSS_WALL_Y - 0.01, 7.0)
BOTTOM_LIP_TIP_Y = 7.8


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


def build_housing():
    housing = cq.importers.importStep(str(SOURCE))

    # Top edge: raise the rib and hang three lips off it.
    housing = housing.union(box(TOP_RIB_X, TOP_RIB_Y, (SKIN_Z, FEATURE_TOP_Z)))
    inner_y = TOP_RIB_Y[0] - LIP_OVERLAP
    for x in TOP_LIP_X:
        housing = housing.union(
            yz_prism(
                x,
                [
                    (TOP_RIB_Y[0], FRAME_TOP_Z),
                    (inner_y, FRAME_TOP_Z),
                    (inner_y, FEATURE_TOP_Z - LIP_CHAMFER),
                    (inner_y + LIP_CHAMFER, FEATURE_TOP_Z),
                    (TOP_RIB_Y[0], FEATURE_TOP_Z),
                ],
            )
        )

    # Bottom edge: two blocks with lips for the keeper's snap hooks.
    y0, y1 = BOTTOM_BLOCK_Y
    for x in BOTTOM_BLOCK_X:
        housing = housing.union(
            yz_prism(
                x,
                [
                    (y0, SKIN_Z),
                    (y1, SKIN_Z),
                    (y1, FRAME_TOP_Z),
                    (BOTTOM_LIP_TIP_Y, FRAME_TOP_Z),
                    (BOTTOM_LIP_TIP_Y, FEATURE_TOP_Z - LIP_CHAMFER),
                    (y1, FEATURE_TOP_Z),
                    (y0, FEATURE_TOP_Z),
                ],
            )
        )
    return housing


def full(x, y):
    return box(x, y, (FRAME_BOT_Z, FRAME_TOP_Z))


def thin(x, y):
    return box(x, y, (FRAME_TOP_Z - TAB_T, FRAME_TOP_Z))


def finger(x, y, bump_x, bump_y):
    """Leaf finger thinned from below, with a pillar under its free end."""
    leaf = box(x, y, (FINGER_BOT_Z, FRAME_TOP_Z))
    return leaf.union(box(bump_x, bump_y, (BUMP_TIP_Z, FINGER_BOT_Z + 0.01)))


def build_keeper():
    """Union of rails, leaves and tabs; every gap is a slot left between parts."""
    x0, x1 = FRAME_X
    y0, y1 = FRAME_Y
    wx0, wx1 = WINDOW_X
    wy0, wy1 = WINDOW_Y
    parts = []

    # Top rail: thinned strip under the lips, spine, finger roots, fingers A and B.
    parts.append(thin((x0, x1), (41.45, y1)))
    parts.append(full((x0, x1), (39.95, 41.45)))
    parts.append(full((-7.5, 1.5), (wy1, 39.95)))
    parts.append(full((15.5, x1), (wy1, 39.95)))
    parts.append(finger((-20.5, -7.0), (36.45, 39.45), (-20.5, -18.5), (36.5, 38.5)))
    parts.append(finger((1.0, 15.0), (36.45, 39.45), (13.0, 15.0), (36.5, 38.5)))

    # Bottom rail: hook beams and Y pusher on the outer strip, spine, fingers C and D.
    parts.append(full((-21.5, -8.0), (y0, 9.2)))  # left hook beam, rooted in the corner block
    parts.append(thin((-12.0, -8.0), (7.2, y0)))  # left hook tab
    parts.append(full((-7.5, -7.0), (7.6, 9.7)))  # Y pusher root
    parts.append(full((-7.2, 2.0), (7.6, 8.4)))  # Y pusher leaf
    parts.append(box((0.0, 2.0), (7.6, SCREEN_Y[0] + PUSH_Y), (PUSHER_TIP_Z, FRAME_BOT_Z + 0.01)))
    parts.append(full((2.5, 3.5), (y0, 9.7)))
    parts.append(full((4.0, 18.5), (y0, 9.2)))  # right hook beam, rooted in the right rail
    parts.append(thin((4.0, 8.0), (7.2, y0)))  # right hook tab
    parts.append(full((wx1, x1), (y0, 9.7)))
    parts.append(full((wx0, x1), (9.7, 11.2)))  # spine
    parts.append(full((-7.5, 1.5), (11.2, wy0)))
    parts.append(full((15.5, x1), (11.2, wy0)))
    parts.append(finger((-20.5, -7.0), (11.7, wy0), (-20.5, -18.5), (12.0, 14.0)))
    parts.append(finger((1.0, 15.0), (11.7, wy0), (13.0, 15.0), (12.0, 14.0)))

    # Left rail: corner block, X pusher on the outer strip, solid rail with the ribbon cutout.
    parts.append(full((x0, wx0), (y0, 9.5)))
    parts.append(full((-26.4, -25.6), (9.0, 19.4)))  # X pusher leaf
    parts.append(box((-26.4, SCREEN_X[0] + PUSH_X), (16.0, 19.0), (PUSHER_TIP_Z, FRAME_BOT_Z + 0.01)))
    parts.append(full((-25.1, wx0), (9.0, FPC_CUTOUT_Y[0])))
    parts.append(full((x0, wx0), (FPC_CUTOUT_Y[1], 39.95)))

    # Right rail.
    parts.append(full((wx1, x1), (y0, y1)))

    # Feet: rigid backstop 0.15 mm above the screen.
    for fx, fy in (((-25.0, -23.5), (10.0, 12.0)), ((19.0, 21.0), (10.0, 12.0)),
                   ((-25.0, -23.5), (36.0, 38.0)), ((19.0, 21.0), (36.0, 38.0))):
        parts.append(box(fx, fy, (FOOT_Z, FRAME_BOT_Z + 0.01)))

    keeper = parts[0]
    for p in parts[1:]:
        keeper = keeper.union(p)

    # Lead-in chamfers on the hook tabs' outer bottom edges.
    for x in BOTTOM_BLOCK_X:
        keeper = keeper.cut(
            yz_prism(x, [(7.19, FRAME_TOP_Z - TAB_T - 0.01), (7.71, FRAME_TOP_Z - TAB_T - 0.01), (7.19, FRAME_TOP_Z - TAB_T + 0.5)])
        )
    return keeper


def print_orientation(keeper):
    """Flip so the flat top face lies on Z = 0 and every feature grows upward."""
    return keeper.rotate((0, 0, 0), (1, 0, 0), 180).translate((0, 0, FRAME_TOP_Z))


if __name__ == "__main__":
    housing = build_housing()
    cq.exporters.export(housing, str(HOUSING_OUT))
    keeper = build_keeper()
    cq.exporters.export(keeper, str(KEEPER_OUT))
    cq.exporters.export(print_orientation(keeper), str(KEEPER_PRINT_OUT))
    for out in (HOUSING_OUT, KEEPER_OUT, KEEPER_PRINT_OUT):
        print(f"wrote {out.relative_to(CAD_DIR.parent.parent)}")
