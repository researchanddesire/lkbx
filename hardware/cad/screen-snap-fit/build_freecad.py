"""Parametric FreeCAD model of the glue-free screen retention.

Run headless with FreeCAD 1.0 (0.21 should also work):

    freecadcmd build_freecad.py

Creates ``screen-snap-fit.FCStd`` next to this script and exports three STEP
files into ``hardware/cad/`` (all three STEPs are gitignored; regenerate them
from the FCStd):

    top-housing-snap-fit.step   housing with the rigid lips
    screen-keeper.step          keeper frame seated, housing coordinates
    screen-keeper-print.step    keeper flat on Z = 0, top face down

Document contents:

    Params        spreadsheet of the tunable dimensions; they drive the features
    TopHousing    PartDesign body: the Onshape export as base feature, the raised
                  locating rib, the three top lips (one pad plus a lead-in
                  chamfer) and the two bottom lipped blocks (profile pads)
    ScreenKeeper  PartDesign body: plate, through pockets for the window and the
                  slots, pockets that thin the lip strips and the finger leaves,
                  pads for the finger pillars, feet and pusher tabs, and lead-in
                  chamfers on the hook tabs
    References    the screen at its datum and the battery envelope

Coordinates are the top-housing STEP frame: Z is depth from the front face,
Y runs along the housing length and X across its width, in millimetres.
"""

import math
import os
import sys
from pathlib import Path

import FreeCAD as App
import Part
import Sketcher

try:
    HERE = Path(__file__).resolve().parent
except NameError:  # freecadcmd may run the file without __file__
    HERE = Path([a for a in sys.argv if a.endswith(".py")][0]).resolve().parent
CAD_DIR = HERE.parent
SOURCE = CAD_DIR / "top-housing.step"
ASSEMBLY = CAD_DIR / "lockbox-assembly.step"
DOC_OUT = HERE / "screen-snap-fit.FCStd"
HOUSING_OUT = CAD_DIR / "top-housing-snap-fit.step"
KEEPER_OUT = CAD_DIR / "screen-keeper.step"
KEEPER_PRINT_OUT = CAD_DIR / "screen-keeper-print.step"

V = App.Vector

# --------------------------------------------------------------------------
# Parameters. Every row becomes a spreadsheet cell with an alias of the same
# name; features reference them by expression so they can be tuned in FreeCAD.
# --------------------------------------------------------------------------
PARAMS = [
    # (alias, value in mm or '=formula', note)
    ("SCREEN_BACK_Z", 3.90, "Rear face of the screen's metal frame when it sits on the skin"),
    ("SKIN_Z", 1.80, "Inner face of the front skin"),
    ("SCREEN_X0", -25.35, "Screen left edge, seated against the right rib"),
    ("SCREEN_Y0", 8.05, "Screen bottom edge, seated against the top rib"),
    ("RIB_Y", 42.65, "Screen-side face of the top locating rib"),
    ("FRAME_TOP_Z", 7.30, "Lip undersides; the keeper floats up to here"),
    ("FRAME_T", 3.00, "Keeper plate thickness"),
    ("TAB_T", 1.00, "Strips that go under the lips, thinned from below"),
    ("FINGER_T", 1.40, "Leaf finger thickness, thinned from below"),
    ("PRELOAD", 0.35, "Finger pillar interference against the screen"),
    ("BACKSTOP", 0.15, "Gap between the rigid feet and the screen"),
    ("PUSHER_TIP_Z", 2.60, "How far the pusher tabs reach down beside the screen edge"),
    ("PUSH_X", 0.20, "X pusher interference against the seated screen"),
    ("PUSH_Y", 0.20, "Y pusher interference against the seated screen"),
    ("LIP_OVERLAP", 1.00, "Top lip reach over the pocket"),
    ("LIP_CHAMFER", 0.80, "45 degree lead-in on the lips"),
    ("FEATURE_TOP_Z", 8.85, "Top of the rib, lips and bottom blocks"),
    ("HOOK_CHAMFER", 0.50, "Lead-in chamfer on the keeper's hook tabs"),
    ("FRAME_BOT_Z", "=FRAME_TOP_Z - FRAME_T", "Keeper plate underside"),
    ("FINGER_BOT_Z", "=FRAME_TOP_Z - FINGER_T", "Finger leaf underside"),
    ("BUMP_TIP_Z", "=SCREEN_BACK_Z - PRELOAD", "Relaxed finger pillar tip"),
    ("FOOT_Z", "=SCREEN_BACK_Z + BACKSTOP", "Foot underside"),
]
P = {}
for _name, _value, _note in PARAMS:
    if isinstance(_value, str):
        _expr = _value[1:]
        for _k, _v in P.items():
            _expr = _expr.replace(_k, repr(_v))
        P[_name] = eval(_expr)  # noqa: S307 - formulas above are our own
    else:
        P[_name] = _value

# Keeper layout (plate footprints, housing frame). The plate is FRAME_T thick
# between FRAME_BOT_Z and FRAME_TOP_Z; every gap between these rectangles is a
# slot cut through the plate.
FRAME_X = (-26.8, 22.3)
FRAME_Y = (8.2, 42.45)
WINDOW_X = (-21.0, 18.0)
WINDOW_Y = (14.5, 35.5)
FPC_CUTOUT_Y = (19.5, 30.5)
x0, x1 = FRAME_X
y0, y1 = FRAME_Y
wx0, wx1 = WINDOW_X
wy0, wy1 = WINDOW_Y

FULL = [  # full plate thickness
    ((x0, x1), (39.95, 41.45)),  # top rail
    ((-7.5, 1.5), (wy1, 39.95)),  # top centre block, roots of fingers A and B
    ((15.5, x1), (wy1, 39.95)),  # top right block
    ((-21.5, -8.0), (y0, 9.2)),  # left hook beam, rooted in the corner block
    ((-7.5, -7.0), (7.6, 9.7)),  # Y pusher root
    ((-7.2, 2.0), (7.6, 8.4)),  # Y pusher leaf
    ((2.5, 3.5), (y0, 9.7)),  # stop post between the pusher and the right beam
    ((4.0, 18.5), (y0, 9.2)),  # right hook beam, rooted in the right rail
    ((wx1, x1), (y0, 9.7)),  # bottom right corner
    ((wx0, x1), (9.7, 11.2)),  # spine
    ((-7.5, 1.5), (11.2, wy0)),  # bottom centre block, roots of fingers C and D
    ((15.5, x1), (11.2, wy0)),  # bottom right block
    ((x0, wx0), (y0, 9.5)),  # bottom left corner block
    ((-26.4, -25.6), (9.0, 19.4)),  # X pusher leaf
    ((-25.1, wx0), (9.0, FPC_CUTOUT_Y[0])),  # left rail below the ribbon cutout
    ((x0, wx0), (FPC_CUTOUT_Y[1], 39.95)),  # left rail above the ribbon cutout
    ((wx1, x1), (y0, y1)),  # right rail
]
THIN = [  # TAB_T thick, kept at the top face
    ((x0, x1), (41.45, y1)),  # strip under the top lips
    ((-12.0, -8.0), (7.2, y0)),  # left hook tab
    ((4.0, 8.0), (7.2, y0)),  # right hook tab
]
LEAVES = [  # FINGER_T thick, kept at the top face
    ((-20.5, -7.0), (36.45, 39.45)),  # finger A, free end at X -20.5
    ((1.0, 15.0), (36.45, 39.45)),  # finger B, free end at X 15
    ((-20.5, -7.0), (11.7, wy0)),  # finger C
    ((1.0, 15.0), (11.7, wy0)),  # finger D
]
PILLARS = [((-20.5, -18.5), (36.5, 38.5)), ((13.0, 15.0), (36.5, 38.5)),
           ((-20.5, -18.5), (12.0, 14.0)), ((13.0, 15.0), (12.0, 14.0))]
FEET = [((-25.0, -23.5), (10.0, 12.0)), ((19.0, 21.0), (10.0, 12.0)),
        ((-25.0, -23.5), (36.0, 38.0)), ((19.0, 21.0), (36.0, 38.0))]
Y_TAB = ((0.0, 2.0), (7.6, P["SCREEN_Y0"] + P["PUSH_Y"]))
X_TAB = ((-26.4, P["SCREEN_X0"] + P["PUSH_X"]), (16.0, 19.0))
HOOK_TAB_X = ((-12.0, -8.0), (4.0, 8.0))
PLATE_X = FRAME_X
PLATE_Y = (7.2, y1)
BANDS = [  # name, y range: the through pockets are grouped by band
    ("WindowCutout", (wy0, wy1)),
    ("TopSlots", (wy1, PLATE_Y[1])),
    ("BottomSlots", (PLATE_Y[0], wy0)),
]

# Housing additions.
TOP_RIB_X = (-20.0, 20.0)
TOP_RIB_Y = (42.65, 45.65)
TOP_LIP_X = ((-17.0, -11.0), (-3.0, 3.0), (11.0, 17.0))
CROSS_WALL_Y = 5.50
BOTTOM_BLOCK_X = ((-12.0, -8.0), (4.0, 8.0))
BLOCK_Y = (CROSS_WALL_Y - 0.01, 7.0)
BOTTOM_LIP_TIP_Y = 7.8

# --------------------------------------------------------------------------
# Helpers
# --------------------------------------------------------------------------


def rect_face(x, y):
    pts = [V(x[0], y[0], 0), V(x[1], y[0], 0), V(x[1], y[1], 0), V(x[0], y[1], 0), V(x[0], y[0], 0)]
    return Part.Face(Part.makePolygon(pts))


def union_faces(rects):
    faces = [rect_face(*r) for r in rects]
    shape = faces[0] if len(faces) == 1 else faces[0].fuse(faces[1:])
    return shape.removeSplitter()


def simplify(pts):
    """Drop repeated and collinear vertices of a closed polygon."""
    pts = [(round(x, 6), round(y, 6)) for x, y in pts]
    changed = True
    while changed and len(pts) > 3:
        changed = False
        for i in range(len(pts)):
            a, b, c = pts[i - 1], pts[i], pts[(i + 1) % len(pts)]
            cross = (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0])
            if b == a or abs(cross) < 1e-9:
                del pts[i]
                changed = True
                break
    return pts


def polygons(shape):
    """Closed polygons (vertex lists) of every face of a planar shape."""
    out = []
    for f in shape.Faces:
        if len(f.Wires) != 1:
            raise RuntimeError("unexpected hole in a footprint region")
        out.append(simplify([(v.X, v.Y) for v in f.OuterWire.OrderedVertexes]))
    return out


def regions(minuend, subtrahend):
    """Polygons of (union of minuend rects) minus (union of subtrahend rects)."""
    shape = union_faces(minuend).cut(union_faces(subtrahend)).removeSplitter()
    return polygons(shape)


def rect_pts(x, y):
    return [(x[0], y[0]), (x[1], y[0]), (x[1], y[1]), (x[0], y[1])]


def edge_kind(a, b):
    if abs(a[1] - b[1]) < 1e-9:
        return "H"
    if abs(a[0] - b[0]) < 1e-9:
        return "V"
    return "D"


def distance_constraint(sk, kind, geo, pos, value, name=None, expr=None):
    """DistanceX/DistanceY of a sketch point from the origin, kept positive."""
    if value >= 0:
        ci = sk.addConstraint(Sketcher.Constraint(kind, -1, 1, geo, pos, value))
    else:
        ci = sk.addConstraint(Sketcher.Constraint(kind, geo, pos, -1, 1, -value))
    if name:
        sk.renameConstraint(ci, name)
        if expr:
            sk.setExpression("Constraints." + name, expr if value >= 0 else "-(%s)" % expr)
    return ci


def polygon(sk, pts, named=None):
    """Closed polygon of lines, fully constrained.

    Horizontal and vertical edges get a horizontal/vertical constraint and a
    distance from the origin for their start point; a diagonal edge gets the
    coordinates its neighbours leave free. ``named`` maps an edge index to
    (constraint name, expression) for the position constraint of that edge.
    """
    n = len(pts)
    geo = [sk.addGeometry(Part.LineSegment(V(pts[i][0], pts[i][1], 0),
                                           V(pts[(i + 1) % n][0], pts[(i + 1) % n][1], 0)), False)
           for i in range(n)]
    for i in range(n):
        sk.addConstraint(Sketcher.Constraint("Coincident", geo[i], 2, geo[(i + 1) % n], 1))
    kinds = [edge_kind(pts[i], pts[(i + 1) % n]) for i in range(n)]
    for i in range(n):
        a = pts[i]
        name, expr = (named or {}).get(i, (None, None))
        if kinds[i] == "H":
            sk.addConstraint(Sketcher.Constraint("Horizontal", geo[i]))
            distance_constraint(sk, "DistanceY", geo[i], 1, a[1], name, expr)
        elif kinds[i] == "V":
            sk.addConstraint(Sketcher.Constraint("Vertical", geo[i]))
            distance_constraint(sk, "DistanceX", geo[i], 1, a[0], name, expr)
        else:
            prev_kind, next_kind = kinds[i - 1], kinds[(i + 1) % n]
            b = pts[(i + 1) % n]
            if prev_kind != "V":
                distance_constraint(sk, "DistanceX", geo[i], 1, a[0])
            if prev_kind != "H":
                distance_constraint(sk, "DistanceY", geo[i], 1, a[1])
            if next_kind != "V":
                distance_constraint(sk, "DistanceX", geo[i], 2, b[0])
            if next_kind != "H":
                distance_constraint(sk, "DistanceY", geo[i], 2, b[1])
    return geo


def origin_feature(body, role):
    return [f for f in body.Origin.OriginFeatures if f.Role == role][0]


def sketch(body, name, plane_role, offset=0.0, offset_expr=None):
    """Sketch on a body origin plane, offset along the plane normal."""
    sk = body.newObject("Sketcher::SketchObject", name)
    plane = origin_feature(body, plane_role)
    if hasattr(sk, "AttachmentSupport"):
        sk.AttachmentSupport = [(plane, "")]
    else:
        sk.Support = [(plane, "")]
    sk.MapMode = "FlatFace"
    sk.AttachmentOffset = App.Placement(V(0, 0, offset), App.Rotation())
    if offset_expr:
        sk.setExpression(".AttachmentOffset.Base.z", offset_expr)
    return sk


def finish_sketch(sk):
    sk.solve()
    if not sk.FullyConstrained:
        raise RuntimeError("sketch %s is not fully constrained" % sk.Name)


def pad(body, sk, name, length=None, length_expr=None, reversed_=False):
    f = body.newObject("PartDesign::Pad", name)
    f.Refine = True
    f.Profile = sk
    f.Type = "Length"
    if length is not None:
        f.Length = length
    if length_expr:
        f.setExpression("Length", length_expr)
    f.Reversed = reversed_
    return f


def pocket(body, sk, name, length=None, length_expr=None, reversed_=False, through=False):
    f = body.newObject("PartDesign::Pocket", name)
    f.Refine = True
    f.Profile = sk
    if through:
        f.Type = "ThroughAll"
    else:
        f.Type = "Length"
        if length is not None:
            f.Length = length
        if length_expr:
            f.setExpression("Length", length_expr)
    f.Reversed = reversed_
    return f


def edges_along_x(shape, y, z, x_ranges, min_length=1.0):
    """Names of the straight edges of ``shape`` that run along X on the line (y, z)."""
    names = []
    for i, e in enumerate(shape.Edges):
        a, b = e.Vertexes[0].Point, e.Vertexes[-1].Point
        on_line = all(abs(p.y - y) < 1e-6 and abs(p.z - z) < 1e-6 for p in (a, b))
        inside = any(xr[0] - 1e-6 <= a.x <= xr[1] + 1e-6 and xr[0] - 1e-6 <= b.x <= xr[1] + 1e-6 for xr in x_ranges)
        if on_line and inside and abs(a.x - b.x) >= min_length:
            names.append("Edge%d" % (i + 1))
    if len(names) != len(x_ranges):
        raise RuntimeError("expected %d edges on y %.2f z %.2f, found %s" % (len(x_ranges), y, z, names))
    return names


def chamfer(body, feature, edge_names, name, size_expr):
    f = body.newObject("PartDesign::Chamfer", name)
    f.Refine = True
    f.Base = (feature, edge_names)
    f.setExpression("Size", size_expr)
    return f


def recompute(doc, *objs):
    doc.recompute()
    for o in objs:
        if "Invalid" in o.State or "Touched" in o.State:
            raise RuntimeError("%s did not recompute cleanly: %s" % (o.Name, o.State))


def report(label, shape):
    bb = shape.BoundBox
    print("  %-22s solids %d valid %s vol %9.3f  z %.2f..%.2f" % (
        label, len(shape.Solids), shape.isValid(), shape.Volume, bb.ZMin, bb.ZMax))


# --------------------------------------------------------------------------
# Spreadsheet
# --------------------------------------------------------------------------


def build_params(doc):
    sheet = doc.addObject("Spreadsheet::Sheet", "Params")
    sheet.set("A1", "Parameter")
    sheet.set("B1", "Value")
    sheet.set("C1", "Note")
    for row, (name, value, note) in enumerate(PARAMS, start=2):
        sheet.set("A%d" % row, name)
        sheet.set("B%d" % row, value if isinstance(value, str) else "=%g mm" % value)
        sheet.setAlias("B%d" % row, name)
        sheet.set("C%d" % row, note)
    doc.recompute()
    for name, _value, _note in PARAMS:
        got = float(sheet.get(name))
        if abs(got - P[name]) > 1e-6:
            raise RuntimeError("spreadsheet %s = %s, expected %s" % (name, got, P[name]))
    return sheet


# --------------------------------------------------------------------------
# Housing body
# --------------------------------------------------------------------------


def build_housing(doc):
    print("housing")
    solid = Part.read(str(SOURCE))
    if len(solid.Solids) != 1:
        raise RuntimeError("expected one solid in %s" % SOURCE)
    base = doc.addObject("Part::Feature", "TopHousingImport")
    base.Label = "top-housing.step (Onshape export)"
    base.Shape = solid.Solids[0]
    body = doc.addObject("PartDesign::Body", "TopHousing")
    body.BaseFeature = base
    recompute(doc, body)
    report("base", body.Shape)

    # Raise the locating rib to the lip height.
    sk = sketch(body, "TopRibSketch", "XY_Plane", P["SKIN_Z"], "Params.SKIN_Z")
    polygon(sk, rect_pts(TOP_RIB_X, TOP_RIB_Y))
    finish_sketch(sk)
    rib = pad(body, sk, "RaiseTopRib", length_expr="Params.FEATURE_TOP_Z - Params.SKIN_Z")
    recompute(doc, sk, rib)
    report("RaiseTopRib", body.Shape)

    # Three top lips: one sketch on the lip-underside plane, padded up to the
    # feature top, then a 45 degree lead-in chamfer on their inner top edges.
    inner = P["RIB_Y"] - P["LIP_OVERLAP"]
    sk = sketch(body, "TopLipsSketch", "XY_Plane", P["FRAME_TOP_Z"], "Params.FRAME_TOP_Z")
    for i, xr in enumerate(TOP_LIP_X, start=1):
        polygon(sk, rect_pts(xr, (inner, P["RIB_Y"])), named={
            0: ("LipReach%d" % i, "Params.RIB_Y - Params.LIP_OVERLAP"),
            2: ("RibFace%d" % i, "Params.RIB_Y"),
        })
    finish_sketch(sk)
    lips = pad(body, sk, "TopLips", length_expr="Params.FEATURE_TOP_Z - Params.FRAME_TOP_Z")
    recompute(doc, sk, lips)
    report("TopLips", body.Shape)
    names = edges_along_x(lips.Shape, inner, P["FEATURE_TOP_Z"], TOP_LIP_X)
    ch = chamfer(body, lips, names, "TopLipLeadIn", "Params.LIP_CHAMFER")
    recompute(doc, ch)
    report("TopLipLeadIn", body.Shape)

    # Two bottom blocks with their lips, each a profile in the YZ plane
    # (sketch X = housing Y, sketch Y = housing Z) padded across X.
    profile = [
        (BLOCK_Y[0], P["SKIN_Z"]),
        (BLOCK_Y[1], P["SKIN_Z"]),
        (BLOCK_Y[1], P["FRAME_TOP_Z"]),
        (BOTTOM_LIP_TIP_Y, P["FRAME_TOP_Z"]),
        (BOTTOM_LIP_TIP_Y, P["FEATURE_TOP_Z"] - P["LIP_CHAMFER"]),
        (BLOCK_Y[1], P["FEATURE_TOP_Z"]),
        (BLOCK_Y[0], P["FEATURE_TOP_Z"]),
    ]
    for side, xr in zip(("Left", "Right"), BOTTOM_BLOCK_X):
        sk = sketch(body, "BottomBlock%sProfile" % side, "YZ_Plane", xr[0])
        polygon(sk, profile, named={
            0: ("Skin", "Params.SKIN_Z"),
            2: ("LipUnderside", "Params.FRAME_TOP_Z"),
            5: ("BlockTop", "Params.FEATURE_TOP_Z"),
        })
        finish_sketch(sk)
        block = pad(body, sk, "BottomBlock%s" % side, length=xr[1] - xr[0])
        recompute(doc, sk, block)
        report("BottomBlock%s" % side, body.Shape)
    return body, base


# --------------------------------------------------------------------------
# Keeper body
# --------------------------------------------------------------------------


def build_keeper(doc):
    print("keeper")
    body = doc.addObject("PartDesign::Body", "ScreenKeeper")
    plate_parts = FULL + THIN + LEAVES

    sk = sketch(body, "PlateOutline", "XY_Plane", P["FRAME_BOT_Z"], "Params.FRAME_BOT_Z")
    polygon(sk, rect_pts(PLATE_X, PLATE_Y))
    finish_sketch(sk)
    plate = pad(body, sk, "Plate", length_expr="Params.FRAME_T")
    recompute(doc, sk, plate)
    report("Plate", body.Shape)

    # Through pockets: everything inside the plate outline that is not a part.
    for name, band_y in BANDS:
        polys = regions([(PLATE_X, band_y)], plate_parts)
        sk = sketch(body, name + "Sketch", "XY_Plane", P["FRAME_TOP_Z"], "Params.FRAME_TOP_Z")
        for poly in polys:
            polygon(sk, poly)
        finish_sketch(sk)
        f = pocket(body, sk, name, through=True)
        recompute(doc, sk, f)
        report(name, body.Shape)

    # Thin the lip strips and hook tabs from below.
    sk = sketch(body, "ThinStripsSketch", "XY_Plane", P["FRAME_BOT_Z"], "Params.FRAME_BOT_Z")
    for poly in regions(THIN, FULL):
        polygon(sk, poly)
    finish_sketch(sk)
    f = pocket(body, sk, "ThinStrips", length_expr="Params.FRAME_T - Params.TAB_T", reversed_=True)
    recompute(doc, sk, f)
    report("ThinStrips", body.Shape)

    # Thin the four finger leaves from below.
    sk = sketch(body, "FingerLeavesSketch", "XY_Plane", P["FRAME_BOT_Z"], "Params.FRAME_BOT_Z")
    for poly in regions(LEAVES, FULL):
        polygon(sk, poly)
    finish_sketch(sk)
    f = pocket(body, sk, "FingerLeaves", length_expr="Params.FRAME_T - Params.FINGER_T", reversed_=True)
    recompute(doc, sk, f)
    report("FingerLeaves", body.Shape)

    # Pillars under the finger tips, down to the preloaded tip height.
    sk = sketch(body, "FingerPillarsSketch", "XY_Plane", P["FINGER_BOT_Z"], "Params.FINGER_BOT_Z")
    for r in PILLARS:
        polygon(sk, rect_pts(*r))
    finish_sketch(sk)
    f = pad(body, sk, "FingerPillars", length_expr="Params.FINGER_BOT_Z - Params.BUMP_TIP_Z", reversed_=True)
    recompute(doc, sk, f)
    report("FingerPillars", body.Shape)

    # Rigid feet, the backstop above the screen.
    sk = sketch(body, "FeetSketch", "XY_Plane", P["FRAME_BOT_Z"], "Params.FRAME_BOT_Z")
    for r in FEET:
        polygon(sk, rect_pts(*r))
    finish_sketch(sk)
    f = pad(body, sk, "Feet", length_expr="Params.FRAME_BOT_Z - Params.FOOT_Z", reversed_=True)
    recompute(doc, sk, f)
    report("Feet", body.Shape)

    # Pusher tabs reaching down beside the screen edges.
    sk = sketch(body, "PusherTabsSketch", "XY_Plane", P["FRAME_BOT_Z"], "Params.FRAME_BOT_Z")
    polygon(sk, rect_pts(*Y_TAB), named={2: ("PushYFace", "Params.SCREEN_Y0 + Params.PUSH_Y")})
    polygon(sk, rect_pts(*X_TAB), named={1: ("PushXFace", "Params.SCREEN_X0 + Params.PUSH_X")})
    finish_sketch(sk)
    tabs = pad(body, sk, "PusherTabs", length_expr="Params.FRAME_BOT_Z - Params.PUSHER_TIP_Z", reversed_=True)
    recompute(doc, sk, tabs)
    report("PusherTabs", body.Shape)

    # Lead-in chamfers on the outer bottom edges of the hook tabs.
    names = edges_along_x(tabs.Shape, PLATE_Y[0], P["FRAME_TOP_Z"] - P["TAB_T"], HOOK_TAB_X)
    ch = chamfer(body, tabs, names, "HookLeadIn", "Params.HOOK_CHAMFER")
    recompute(doc, ch)
    report("HookLeadIn", body.Shape)
    return body


# --------------------------------------------------------------------------
# References and verification
# --------------------------------------------------------------------------


def build_references(doc):
    group = doc.addObject("App::DocumentObjectGroup", "References")
    asm = Part.read(str(ASSEMBLY))
    m = App.Matrix()
    m.rotateY(math.pi)
    m.move(V(20.35 + 0.15, 49.12 + 0.2, -1.56))  # assembly to housing frame, screen at its datum
    screen = doc.addObject("Part::Feature", "ScreenReference")
    screen.Label = "Screen at datum (from lockbox-assembly.step)"
    screen.Shape = asm.Solids[0].transformGeometry(m)
    battery = doc.addObject("Part::Box", "BatteryEnvelope")
    battery.Length, battery.Width, battery.Height = 36.0, 20.5, 8.0
    battery.Placement = App.Placement(V(-19.5, 14.75, 3.86), App.Rotation())
    group.addObject(screen)
    group.addObject(battery)
    doc.recompute()
    return screen, battery


def box(x, y, z):
    return Part.makeBox(x[1] - x[0], y[1] - y[0], z[1] - z[0], V(x[0], y[0], z[0]))


def yz_prism(x, profile):
    face = Part.Face(Part.makePolygon([V(x[0], y, z) for y, z in profile] + [V(x[0], profile[0][0], profile[0][1])]))
    return face.extrude(V(x[1] - x[0], 0, 0))


def reference_keeper():
    """The same keeper as a plain union of boxes, independent of PartDesign."""
    zb, zt = P["FRAME_BOT_Z"], P["FRAME_TOP_Z"]
    parts = [box(x, y, (zb, zt)) for x, y in FULL]
    parts += [box(x, y, (zt - P["TAB_T"], zt)) for x, y in THIN]
    parts += [box(x, y, (P["FINGER_BOT_Z"], zt)) for x, y in LEAVES]
    parts += [box(x, y, (P["BUMP_TIP_Z"], P["FINGER_BOT_Z"])) for x, y in PILLARS]
    parts += [box(x, y, (P["FOOT_Z"], zb)) for x, y in FEET]
    parts += [box(x, y, (P["PUSHER_TIP_Z"], zb)) for x, y in (Y_TAB, X_TAB)]
    shape = parts[0].fuse(parts[1:]).removeSplitter()
    c = P["HOOK_CHAMFER"]
    tz = zt - P["TAB_T"]
    for xr in HOOK_TAB_X:
        shape = shape.cut(yz_prism((xr[0] - 0.01, xr[1] + 0.01), [(PLATE_Y[0], tz), (PLATE_Y[0] + c, tz), (PLATE_Y[0], tz + c)]))
    return shape


def reference_housing(base_shape):
    zt, ft = P["FRAME_TOP_Z"], P["FEATURE_TOP_Z"]
    inner = P["RIB_Y"] - P["LIP_OVERLAP"]
    parts = [box(TOP_RIB_X, TOP_RIB_Y, (P["SKIN_Z"], ft))]
    for xr in TOP_LIP_X:
        parts.append(yz_prism(xr, [(P["RIB_Y"], zt), (inner, zt), (inner, ft - P["LIP_CHAMFER"]), (inner + P["LIP_CHAMFER"], ft), (P["RIB_Y"], ft)]))
    for xr in BOTTOM_BLOCK_X:
        parts.append(yz_prism(xr, [(BLOCK_Y[0], P["SKIN_Z"]), (BLOCK_Y[1], P["SKIN_Z"]), (BLOCK_Y[1], zt), (BOTTOM_LIP_TIP_Y, zt), (BOTTOM_LIP_TIP_Y, ft - P["LIP_CHAMFER"]), (BLOCK_Y[1], ft), (BLOCK_Y[0], ft)]))
    return base_shape.fuse(parts)


def compare(label, shape, ref):
    extra = shape.cut(ref).Volume
    missing = ref.cut(shape).Volume
    print("  %s vs reference: extra %.4f mm3, missing %.4f mm3" % (label, extra, missing))
    if extra > 0.001 or missing > 0.001:
        raise RuntimeError("%s differs from its reference" % label)


# --------------------------------------------------------------------------


def main():
    doc = App.newDocument("screen_snap_fit")
    doc.Label = "screen-snap-fit"
    build_params(doc)
    housing, base = build_housing(doc)
    keeper = build_keeper(doc)
    build_references(doc)
    doc.recompute()

    for obj in doc.Objects:
        if "Invalid" in obj.State:
            raise RuntimeError("%s is invalid" % obj.Name)

    if os.environ.get("SNAPFIT_SKIP_VERIFY") != "1":
        print("verify")
        compare("keeper", keeper.Shape, reference_keeper())
        compare("housing", housing.Shape, reference_housing(base.Shape))

    doc.saveAs(str(DOC_OUT))
    housing.Shape.exportStep(str(HOUSING_OUT))
    keeper.Shape.exportStep(str(KEEPER_OUT))
    m = App.Matrix()
    m.rotateX(math.pi)
    m.move(V(0, 0, P["FRAME_TOP_Z"]))
    keeper.Shape.transformGeometry(m).exportStep(str(KEEPER_PRINT_OUT))
    for out in (DOC_OUT, HOUSING_OUT, KEEPER_OUT, KEEPER_PRINT_OUT):
        print("wrote %s (%.1f kB)" % (out.relative_to(CAD_DIR.parent.parent), out.stat().st_size / 1024))


main()
