"""Parametric FreeCAD model of the glue-free screen retention (sprung walls).

Run headless with FreeCAD 1.0 (0.21 should also work):

    freecadcmd build_freecad.py

Creates ``screen-snap-fit.FCStd`` next to this script and exports, into
``hardware/cad/`` (gitignored; regenerate them from the FCStd):

    top-housing-snap-fit.step   the housing with the screen retention
    top-housing-snap-fit.stl    the same as a mesh, front face on Z = 0 (print face down)

``bambu/make_projects.sh`` turns the STL into Bambu Studio projects and
printer-ready files.

Document contents:

    Params        spreadsheet of the tunable dimensions; the features read them
                  by expression, so a cell change and a recompute rebuilds the part
    TopHousing    PartDesign body: the Onshape export as base feature, then
                  CutTopRib / CutRightRib (the old locating ribs go), BottomBlock
                  and BottomCatch (the rigid 45 degree catch under the screen's
                  bottom edge), LeftRibs with a lead-in chamfer (the X datum),
                  SideWall + SidePosts + SideBump + lead-in (the sprung side wall
                  that presses the screen against the left ribs), TopWall and
                  HookLeft / HookRight (the sprung top wall whose two hooks click
                  over the screen's top edge)
    References    the screen at its datum and the battery envelope

Coordinates are the top-housing STEP frame: Z is depth from the front face,
Y runs along the housing length and X across its width, in millimetres.
The housing prints front face down, so every feature grows away from Z = 0;
the two sprung walls float WALL_GAP above the skin and are bridged between
their anchors, the catch and hook undersides are 45 and 35 degree overhangs.
"""

import math
import os
import sys
from pathlib import Path

import FreeCAD as App
import Mesh  # noqa: F401  (registers the STL writer)
import MeshPart
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
HOUSING_STL = CAD_DIR / "top-housing-snap-fit.stl"
# Mesh tolerances for the STL export: 0.01 mm chord error, 0.1 rad angular.
STL_LINEAR_DEFLECTION = 0.01
STL_ANGULAR_DEFLECTION = 0.1

V = App.Vector

# --------------------------------------------------------------------------
# Parameters. Every row becomes a spreadsheet cell with an alias of the same
# name; features reference them by expression so they can be tuned in FreeCAD.
# A string value is a spreadsheet formula over earlier aliases.
# --------------------------------------------------------------------------
PARAMS = [
    # (alias, value in mm / deg or '=formula', note)
    ("SKIN_Z", 1.80, "Inner face of the front skin"),
    ("SCREEN_BACK_Z", 3.90, "Back face of the screen glass when it sits on the skin"),
    ("SCREEN_X0", -25.35, "Screen left edge, against the left ribs"),
    ("SCREEN_X1", 22.45, "Screen right edge"),
    ("SCREEN_Y0", 8.05, "Screen bottom edge, its back corner in the bottom catch"),
    ("SCREEN_Y1", 42.65, "Screen top edge, under the hooks of the top wall"),
    ("WALL_T", 1.26, "Sprung wall thickness, three 0.42 mm perimeters"),
    ("WALL_GAP", 1.60, "Air under the sprung walls so they can flex; bridged when printing"),
    ("TOP_WALL_TOP_Z", 11.40, "Top of the sprung top wall"),
    ("SIDE_WALL_TOP_Z", 9.00, "Top of the sprung side wall and its posts"),
    ("HOOK_REACH", 0.60, "How far each hook reaches over the glass once seated"),
    ("HOOK_PRELOAD", 0.30, "Top wall deflection with the screen seated"),
    ("HOOK_RELIEF", 0.15, "Gap between the seated glass edge and the top wall face"),
    ("HOOK_ANGLE", 35.0, "Hook retention face, degrees from horizontal (overhang when printing)"),
    ("RAMP_ANGLE", 35.0, "Hook insertion ramp, degrees from vertical"),
    ("HOOK_TIP", 0.30, "Flat on the hook tip between the retention face and the ramp"),
    ("CATCH_REACH", 1.00, "Bottom catch reach over the glass"),
    ("CATCH_RELIEF", 0.30, "Gap between the glass bottom edge and the catch block face"),
    ("CATCH_TOP_Z", 6.00, "Top of the bottom block and its catch"),
    ("SIDE_CLEAR", 0.20, "Gap between the glass right edge and the side wall face"),
    ("SIDE_PRELOAD", 0.25, "Side bump interference with the glass right edge"),
    ("SIDE_BUMP_CHAMFER", 0.40, "Lead-in on the side bump"),
    ("LEFT_RIB_T", 1.50, "Left datum rib thickness"),
    ("LEFT_RIB_TOP_Z", 7.00, "Top of the left datum ribs"),
    ("LEFT_RIB_CHAMFER", 0.60, "Lead-in on the left datum ribs"),
    ("RIB_CUT_Z", 4.35, "Height to which the old locating ribs are removed"),
    ("WALL_BOT_Z", "=SKIN_Z + WALL_GAP", "Underside of both sprung walls"),
    ("TOP_WALL_Y0", "=SCREEN_Y1 + HOOK_RELIEF - HOOK_PRELOAD", "Free-state face of the top wall"),
    ("HOOK_TIP_Y", "=SCREEN_Y1 - HOOK_PRELOAD - HOOK_REACH", "Free-state hook tip"),
    ("CATCH_FACE_Y", "=SCREEN_Y0 - CATCH_RELIEF", "Catch block face"),
    ("CATCH_TIP_Y", "=SCREEN_Y0 + CATCH_REACH", "Catch tip"),
    ("CATCH_TIP_Z", "=SCREEN_BACK_Z + CATCH_REACH", "Catch tip underside (45 degree slope)"),
    ("SIDE_WALL_X0", "=SCREEN_X1 + SIDE_CLEAR", "Side wall face"),
    ("SIDE_BUMP_X", "=SCREEN_X1 - SIDE_PRELOAD", "Side bump face"),
    ("LEFT_RIB_X0", "=SCREEN_X0 - LEFT_RIB_T", "Outer face of the left ribs"),
]
ANGLES = {"HOOK_ANGLE", "RAMP_ANGLE"}
P = {}
for _name, _value, _note in PARAMS:
    if isinstance(_value, str):
        _expr = _value[1:]
        for _k in sorted(P, key=len, reverse=True):
            _expr = _expr.replace(_k, repr(P[_k]))
        P[_name] = eval(_expr)  # noqa: S307 - formulas above are our own
    else:
        P[_name] = _value

# Layout (housing frame).
OUTER_X = (-27.3, 27.3)  # reaches 0.3 mm into the outer walls (inner faces at +-27.0)
HOOK_X = ((-15.0, -9.0), (9.0, 15.0))
SIDE_POST_Y = ((6.9, 9.3), (40.6, 42.1))
SIDE_WALL_Y = (SIDE_POST_Y[0][1] - 0.3, SIDE_POST_Y[1][0] + 0.3)  # overlaps the posts
SIDE_POST_X1 = 25.0
SIDE_BUMP_Y = (21.4, 29.4)
LEFT_RIB_Y = ((7.7, 20.5), (30.2, 42.0))
BOTTOM_BLOCK_Y0 = 6.9  # overlaps the existing cross rib (face at Y 7.0)
CATCH_X = (-26.85, 22.67)  # merges into the left ribs and the right posts
TOP_RIB_CUT = ((-20.5, 20.5), (42.6, 45.7))
RIGHT_RIB_CUT = ((22.4, 24.9), (10.1, 40.2))
EPS = 0.02  # profile overlap into the wall they attach to


def hook_profile():
    """Hook cross-section (housing Y, Z) in the free state, attached to the top wall face."""
    tan_hook = math.tan(math.radians(P["HOOK_ANGLE"]))
    tan_ramp = math.tan(math.radians(P["RAMP_ANGLE"]))
    root_y = P["TOP_WALL_Y0"] + EPS
    tip_y = P["HOOK_TIP_Y"]
    root_z = P["SCREEN_BACK_Z"] - (P["HOOK_RELIEF"] + EPS) * tan_hook
    tip_z = P["SCREEN_BACK_Z"] + P["HOOK_REACH"] * tan_hook
    flat_z = tip_z + P["HOOK_TIP"]
    ramp_z = flat_z + (P["HOOK_REACH"] + P["HOOK_PRELOAD"] + EPS) / tan_ramp
    return [(root_y, root_z), (tip_y, tip_z), (tip_y, flat_z), (root_y, ramp_z)]


HOOK_EXPR = {
    0: {"y1": ("HookRootZ", "Params.SCREEN_BACK_Z - (Params.HOOK_RELIEF + %g mm) * tan(Params.HOOK_ANGLE)" % EPS),
        "y2": ("HookTipZ", "Params.SCREEN_BACK_Z + Params.HOOK_REACH * tan(Params.HOOK_ANGLE)")},
    1: ("HookTipY", "Params.HOOK_TIP_Y"),
    2: {"y1": ("HookFlatZ", "Params.SCREEN_BACK_Z + Params.HOOK_REACH * tan(Params.HOOK_ANGLE) + Params.HOOK_TIP"),
        "y2": ("HookRampZ", "Params.SCREEN_BACK_Z + Params.HOOK_REACH * tan(Params.HOOK_ANGLE) + Params.HOOK_TIP"
                            " + (Params.HOOK_REACH + Params.HOOK_PRELOAD + %g mm) / tan(Params.RAMP_ANGLE)" % EPS)},
    3: ("HookRootY", "Params.TOP_WALL_Y0 + %g mm" % EPS),
}


def catch_profile():
    """Bottom catch cross-section (housing Y, Z): 45 degree underside through the glass back corner."""
    face_y = P["CATCH_FACE_Y"] - 0.05
    face_z = P["SCREEN_BACK_Z"] - P["CATCH_RELIEF"] - 0.05
    return [(face_y, face_z), (P["CATCH_TIP_Y"], P["CATCH_TIP_Z"]), (P["CATCH_TIP_Y"], P["CATCH_TOP_Z"]),
            (face_y, P["CATCH_TOP_Z"])]


CATCH_EXPR = {
    0: {"y1": ("CatchFaceZ", "Params.SCREEN_BACK_Z - Params.CATCH_RELIEF - 0.05 mm"),
        "y2": ("CatchTipZ", "Params.CATCH_TIP_Z")},
    1: ("CatchTipY", "Params.CATCH_TIP_Y"),
    2: ("CatchTop", "Params.CATCH_TOP_Z"),
    3: ("CatchFaceY", "Params.CATCH_FACE_Y - 0.05 mm"),
}

# --------------------------------------------------------------------------
# Helpers
# --------------------------------------------------------------------------


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
    (constraint name, expression) for the position constraint of a horizontal
    or vertical edge, or to a dict with keys among x1, y1, x2, y2 for the
    endpoint constraints of a diagonal edge.
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
        spec = (named or {}).get(i)
        if kinds[i] == "H":
            name, expr = spec or (None, None)
            sk.addConstraint(Sketcher.Constraint("Horizontal", geo[i]))
            distance_constraint(sk, "DistanceY", geo[i], 1, a[1], name, expr)
        elif kinds[i] == "V":
            name, expr = spec or (None, None)
            sk.addConstraint(Sketcher.Constraint("Vertical", geo[i]))
            distance_constraint(sk, "DistanceX", geo[i], 1, a[0], name, expr)
        else:
            spec = spec or {}
            prev_kind, next_kind = kinds[i - 1], kinds[(i + 1) % n]
            b = pts[(i + 1) % n]
            if prev_kind != "V":
                distance_constraint(sk, "DistanceX", geo[i], 1, a[0], *spec.get("x1", (None, None)))
            if prev_kind != "H":
                distance_constraint(sk, "DistanceY", geo[i], 1, a[1], *spec.get("y1", (None, None)))
            if next_kind != "V":
                distance_constraint(sk, "DistanceX", geo[i], 2, b[0], *spec.get("x2", (None, None)))
            if next_kind != "H":
                distance_constraint(sk, "DistanceY", geo[i], 2, b[1], *spec.get("y2", (None, None)))
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


def pocket(body, sk, name, length=None, length_expr=None, reversed_=False):
    f = body.newObject("PartDesign::Pocket", name)
    f.Refine = True
    f.Profile = sk
    f.Type = "Length"
    if length is not None:
        f.Length = length
    if length_expr:
        f.setExpression("Length", length_expr)
    f.Reversed = reversed_
    return f


def edges_along_y(shape, x, z, y_ranges, min_length=1.0):
    """Names of the straight edges of ``shape`` that run along Y on the line (x, z)."""
    names = []
    for i, e in enumerate(shape.Edges):
        a, b = e.Vertexes[0].Point, e.Vertexes[-1].Point
        on_line = all(abs(p.x - x) < 1e-6 and abs(p.z - z) < 1e-6 for p in (a, b))
        inside = any(yr[0] - 1e-6 <= a.y <= yr[1] + 1e-6 and yr[0] - 1e-6 <= b.y <= yr[1] + 1e-6 for yr in y_ranges)
        if on_line and inside and abs(a.y - b.y) >= min_length:
            names.append("Edge%d" % (i + 1))
    if len(names) != len(y_ranges):
        raise RuntimeError("expected %d edges on x %.2f z %.2f, found %s" % (len(y_ranges), x, z, names))
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
        if getattr(o, "isValid", None) and not o.isValid():
            raise RuntimeError("%s is not valid after recompute: %s" % (o.Name, o.State))


_last_volume = [None]


def report(label, shape, must_change=True):
    bb = shape.BoundBox
    print("  %-18s solids %d valid %s vol %9.3f  z %.2f..%.2f" % (
        label, len(shape.Solids), shape.isValid(), shape.Volume, bb.ZMin, bb.ZMax))
    if len(shape.Solids) != 1 or not shape.isValid():
        raise RuntimeError("%s left the body with %d solids (valid %s)" % (label, len(shape.Solids), shape.isValid()))
    if must_change and _last_volume[0] is not None and abs(shape.Volume - _last_volume[0]) < 1e-6:
        raise RuntimeError("%s did not change the body; a floating pad must overlap existing material" % label)
    _last_volume[0] = shape.Volume


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
        if isinstance(value, str):
            sheet.set("B%d" % row, value)
        else:
            sheet.set("B%d" % row, "=%g %s" % (value, "deg" if name in ANGLES else "mm"))
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


def xy_rect_pad(body, doc, name, rects, z0_expr, z1_expr, named=None):
    """Pad rectangles sketched on the XY plane at Params.<z0> up to Params.<z1>."""
    sk = sketch(body, name + "Sketch", "XY_Plane", P[z0_expr], "Params." + z0_expr)
    for i, r in enumerate(rects):
        polygon(sk, rect_pts(*r), named=(named or {}).get(i))
    finish_sketch(sk)
    f = pad(body, sk, name, length_expr="Params.%s - Params.%s" % (z1_expr, z0_expr))
    recompute(doc, sk, f)
    report(name, body.Shape)
    return f


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
    report("base", body.Shape, must_change=False)

    # The old locating ribs sit where the sprung walls go: remove them.
    for name, (xr, yr) in (("CutTopRib", TOP_RIB_CUT), ("CutRightRib", RIGHT_RIB_CUT)):
        sk = sketch(body, name + "Sketch", "XY_Plane", P["SKIN_Z"], "Params.SKIN_Z")
        polygon(sk, rect_pts(xr, yr))
        finish_sketch(sk)
        f = pocket(body, sk, name, length_expr="Params.RIB_CUT_Z - Params.SKIN_Z", reversed_=True)
        recompute(doc, sk, f)
        report(name, body.Shape)

    # Rigid bottom block (merges with the cross rib and the two bosses) and its
    # 45 degree catch over the glass's bottom edge.
    xy_rect_pad(body, doc, "BottomBlock", [(OUTER_X, (BOTTOM_BLOCK_Y0, P["CATCH_FACE_Y"]))],
                "SKIN_Z", "CATCH_TOP_Z", named={0: {2: ("CatchFace", "Params.CATCH_FACE_Y")}})
    sk = sketch(body, "BottomCatchProfile", "YZ_Plane", CATCH_X[0])
    polygon(sk, catch_profile(), named=CATCH_EXPR)
    finish_sketch(sk)
    f = pad(body, sk, "BottomCatch", length=CATCH_X[1] - CATCH_X[0])
    recompute(doc, sk, f)
    report("BottomCatch", body.Shape)

    # Left datum ribs either side of the ribbon cable, with a lead-in chamfer.
    ribs = xy_rect_pad(body, doc, "LeftRibs", [((P["LEFT_RIB_X0"], P["SCREEN_X0"]), yr) for yr in LEFT_RIB_Y],
                       "SKIN_Z", "LEFT_RIB_TOP_Z",
                       named={i: {1: ("RibFace%d" % (i + 1), "Params.SCREEN_X0"),
                                  3: ("RibBack%d" % (i + 1), "Params.LEFT_RIB_X0")} for i in range(2)})
    names = edges_along_y(ribs.Shape, P["SCREEN_X0"], P["LEFT_RIB_TOP_Z"], LEFT_RIB_Y)
    ch = chamfer(body, ribs, names, "LeftRibLeadIn", "Params.LEFT_RIB_CHAMFER")
    recompute(doc, ch)
    report("LeftRibLeadIn", body.Shape)

    # Sprung side wall: two rigid posts on the skin, then the wall floating
    # WALL_GAP above the skin between them (a floating pad must overlap existing
    # material, so the posts come first), then the central bump that presses
    # the glass against the left ribs.
    xy_rect_pad(body, doc, "SidePosts", [((P["SIDE_WALL_X0"], SIDE_POST_X1), yr) for yr in SIDE_POST_Y],
                "SKIN_Z", "SIDE_WALL_TOP_Z",
                named={i: {3: ("PostFace%d" % (i + 1), "Params.SIDE_WALL_X0")} for i in range(2)})
    xy_rect_pad(body, doc, "SideWall", [((P["SIDE_WALL_X0"], P["SIDE_WALL_X0"] + P["WALL_T"]), SIDE_WALL_Y)],
                "WALL_BOT_Z", "SIDE_WALL_TOP_Z",
                named={0: {1: ("SideWallBack", "Params.SIDE_WALL_X0 + Params.WALL_T"),
                           3: ("SideWallFace", "Params.SIDE_WALL_X0")}})
    bump = xy_rect_pad(body, doc, "SideBump", [((P["SIDE_BUMP_X"], P["SIDE_WALL_X0"] + EPS), SIDE_BUMP_Y)],
                       "WALL_BOT_Z", "SIDE_WALL_TOP_Z",
                       named={0: {1: ("BumpBack", "Params.SIDE_WALL_X0 + %g mm" % EPS),
                                  3: ("BumpFace", "Params.SIDE_BUMP_X")}})
    names = edges_along_y(bump.Shape, P["SIDE_BUMP_X"], P["SIDE_WALL_TOP_Z"], [SIDE_BUMP_Y])
    ch = chamfer(body, bump, names, "SideBumpLeadIn", "Params.SIDE_BUMP_CHAMFER")
    recompute(doc, ch)
    report("SideBumpLeadIn", body.Shape)

    # Sprung top wall: floats WALL_GAP above the skin from outer wall to outer
    # wall; two hooks click over the glass's top edge.
    xy_rect_pad(body, doc, "TopWall", [(OUTER_X, (P["TOP_WALL_Y0"], P["TOP_WALL_Y0"] + P["WALL_T"]))],
                "WALL_BOT_Z", "TOP_WALL_TOP_Z",
                named={0: {0: ("TopWallFace", "Params.TOP_WALL_Y0"),
                           2: ("TopWallBack", "Params.TOP_WALL_Y0 + Params.WALL_T")}})
    for side, xr in zip(("Left", "Right"), HOOK_X):
        sk = sketch(body, "Hook%sProfile" % side, "YZ_Plane", xr[0])
        polygon(sk, hook_profile(), named=HOOK_EXPR)
        finish_sketch(sk)
        f = pad(body, sk, "Hook%s" % side, length=xr[1] - xr[0])
        recompute(doc, sk, f)
        report("Hook%s" % side, body.Shape)
    return body, base


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


def xz_prism(y, profile):
    face = Part.Face(Part.makePolygon([V(x, y[0], z) for x, z in profile] + [V(profile[0][0], y[0], profile[0][1])]))
    return face.extrude(V(0, y[1] - y[0], 0))


def reference_housing(base_shape):
    """The same housing from plain Part primitives, independent of PartDesign."""
    skin, wb = P["SKIN_Z"], P["WALL_BOT_Z"]
    shape = base_shape
    for xr, yr in (TOP_RIB_CUT, RIGHT_RIB_CUT):
        shape = shape.cut(box(xr, yr, (skin, P["RIB_CUT_Z"])))
    parts = [
        box(OUTER_X, (BOTTOM_BLOCK_Y0, P["CATCH_FACE_Y"]), (skin, P["CATCH_TOP_Z"])),
        yz_prism(CATCH_X, catch_profile()),
        box((P["SIDE_WALL_X0"], P["SIDE_WALL_X0"] + P["WALL_T"]), SIDE_WALL_Y, (wb, P["SIDE_WALL_TOP_Z"])),
        box((P["SIDE_BUMP_X"], P["SIDE_WALL_X0"] + EPS), SIDE_BUMP_Y, (wb, P["SIDE_WALL_TOP_Z"])),
        box(OUTER_X, (P["TOP_WALL_Y0"], P["TOP_WALL_Y0"] + P["WALL_T"]), (wb, P["TOP_WALL_TOP_Z"])),
    ]
    parts += [box((P["LEFT_RIB_X0"], P["SCREEN_X0"]), yr, (skin, P["LEFT_RIB_TOP_Z"])) for yr in LEFT_RIB_Y]
    parts += [box((P["SIDE_WALL_X0"], SIDE_POST_X1), yr, (skin, P["SIDE_WALL_TOP_Z"])) for yr in SIDE_POST_Y]
    parts += [yz_prism(xr, hook_profile()) for xr in HOOK_X]
    shape = shape.fuse(parts)
    for yr in LEFT_RIB_Y:
        shape = shape.cut(chamfer_prism(yr, P["SCREEN_X0"], P["LEFT_RIB_TOP_Z"], P["LEFT_RIB_CHAMFER"], -1))
    shape = shape.cut(chamfer_prism(SIDE_BUMP_Y, P["SIDE_BUMP_X"], P["SIDE_WALL_TOP_Z"], P["SIDE_BUMP_CHAMFER"], +1))
    return shape.removeSplitter()


def chamfer_prism(y, x, z, c, into):
    """What an equal-distance chamfer of size ``c`` removes along the Y edge at (x, z).

    ``into`` is +1 when the material lies toward +X of the edge, -1 toward -X;
    the material is below the edge in Z. The free corner is pushed out a little
    so the cut does not leave a skin, the hypotenuse is exact.
    """
    e = 0.01 * into
    return xz_prism((y[0] - 0.01, y[1] + 0.01),
                    [(x + into * c, z), (x + into * c, z + 0.01), (x - e, z + 0.01), (x - e, z - c), (x, z - c)])


def compare(label, shape, ref, tolerance=0.001):
    extra, missing = shape.cut(ref), ref.cut(shape)
    print("  %s vs reference: extra %.4f mm3, missing %.4f mm3" % (label, extra.Volume, missing.Volume))
    for what, diff in (("extra", extra), ("missing", missing)):
        for s in diff.Solids:
            if s.Volume > 1e-6:
                bb = s.BoundBox
                print("    %s %.4f mm3 at x %.2f..%.2f y %.2f..%.2f z %.2f..%.2f" % (
                    what, s.Volume, bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))
    if extra.Volume > tolerance or missing.Volume > tolerance:
        raise RuntimeError("%s differs from its reference" % label)


# --------------------------------------------------------------------------


def export_stl(shape, path):
    """Write a binary STL of ``shape`` for slicing."""
    mesh = MeshPart.meshFromShape(
        Shape=shape,
        LinearDeflection=STL_LINEAR_DEFLECTION,
        AngularDeflection=STL_ANGULAR_DEFLECTION,
        Relative=False,
    )
    mesh.write(str(path))
    print("%s: %d facets, volume %.3f mm3" % (path.name, mesh.CountFacets, mesh.Volume))


def main():
    doc = App.newDocument("screen_snap_fit")
    doc.Label = "screen-snap-fit"
    build_params(doc)
    housing, base = build_housing(doc)
    build_references(doc)
    doc.recompute()

    for obj in doc.Objects:
        if "Invalid" in obj.State:
            raise RuntimeError("%s is invalid" % obj.Name)

    if os.environ.get("SNAPFIT_SKIP_VERIFY") != "1":
        print("verify")
        compare("housing", housing.Shape, reference_housing(base.Shape))

    doc.saveAs(str(DOC_OUT))
    housing.Shape.exportStep(str(HOUSING_OUT))
    export_stl(housing.Shape, HOUSING_STL)
    for out in (DOC_OUT, HOUSING_OUT, HOUSING_STL):
        print("wrote %s (%.1f kB)" % (out.relative_to(CAD_DIR.parent.parent), out.stat().st_size / 1024))


main()
