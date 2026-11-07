"""Parametric FreeCAD model of the plug lockout: a bottom housing for a wall plug.

The plug lockout replaces the key tray of ``bottom-housing.step`` with a
cavity that takes the plug of a mains appliance. Its cord runs out through a
narrow slot in the end wall. The slot is wider than the cord and narrower than
any plug body, so while the top housing is locked on, the plug cannot come
out and the appliance cannot be plugged in. The top of the part is the
original bottom housing, unchanged except for the slot, so the stock top
housing slides on, latches and detects it exactly as before.

Run headless with FreeCAD 1.0:

    freecadcmd build_freecad.py

The Onshape exports in ``hardware/cad/`` are Git LFS files. If they are not
checked out, point ``LKBX_CAD_DIR`` at a folder that holds real copies of
``bottom-housing.step``, ``top-housing.step`` and ``lockbox-assembly.step``.

Creates ``plug-lockout.FCStd`` next to this script and writes, into
``hardware/cad/`` (gitignored, regenerate them from the FCStd):

    plug-lockout.step        the part in the housing frame (mates with top-housing.step)
    plug-lockout-print.stl   the part in print orientation, base on Z = 0

Document contents:

    Params      spreadsheet of the tunable dimensions; features read them by expression
    BottomHousingImport / InterfaceCut / Interface
                the Onshape bottom housing, and the part of it above CUT_Z
                (rails, latch slot, magnets) that this part keeps
    PlugLockout PartDesign body on that interface: outer wall pad, fills for
                the two features the old part opened through its underside, the
                top-entry back magnet pocket, the opening, the 45 degree
                transition loft, the cavity and the cord slot
    References  plug envelopes in the cavity and the cord in its slot

Coordinates are the top-housing STEP frame: Z is depth from the screen face
(it grows toward the print bed), Y runs along the housing length and X across
its width, in millimetres. The part prints upright on its base, the same way
up as the original bottom housing.
"""

import math
import os
import sys
from pathlib import Path

import FreeCAD as App
import Import
import Mesh  # noqa: F401  (registers the STL writer)
import MeshPart
import Part
import Sketcher

try:
    HERE = Path(__file__).resolve().parent
except NameError:  # freecadcmd may run the file without __file__
    HERE = Path([a for a in sys.argv if a.endswith(".py")][0]).resolve().parent
OUT_DIR = HERE.parent
CAD_DIR = Path(os.environ.get("LKBX_CAD_DIR", str(OUT_DIR)))
BOTTOM_SOURCE = CAD_DIR / "bottom-housing.step"
TOP_SOURCE = CAD_DIR / "top-housing.step"
ASSEMBLY = CAD_DIR / "lockbox-assembly.step"
DOC_OUT = HERE / "plug-lockout.FCStd"
PART_OUT = OUT_DIR / "plug-lockout.step"
PART_STL = OUT_DIR / "plug-lockout-print.stl"
STL_LINEAR_DEFLECTION = 0.01
STL_ANGULAR_DEFLECTION = 0.1

V = App.Vector

# --------------------------------------------------------------------------
# Parameters. Every row becomes a spreadsheet cell with an alias of the same
# name; features reference them by expression so they can be tuned in FreeCAD.
# --------------------------------------------------------------------------
PARAMS = [
    ("TOP_FACE_Z", 28.70, "Floor of the slide channel, where the old key tray started"),
    ("CUT_Z", 41.00, "The original part is kept above this plane (latch slot floor)"),
    ("OUTER_X", 30.00, "Half width of the housing outline"),
    ("OUTER_Y", 60.50, "Half length of the housing outline"),
    ("OUTER_R", 5.00, "Corner radius of the housing outline"),
    ("WALL", 4.00, "Wall thickness below the transition"),
    ("OPEN_X", 24.00, "Half width of the opening through the interface"),
    ("OPEN_Y0", -42.50, "Latch end of the opening (5 mm clear of the latch slot)"),
    ("OPEN_Y1", 47.00, "Magnet end of the opening (2.7 mm clear of the back magnet)"),
    ("OPEN_R", 3.00, "Opening corner radius"),
    ("CAVITY_DEPTH", 80.00, "Usable depth from TOP_FACE_Z to the cavity floor"),
    ("FLOOR_T", 3.00, "Base thickness under the cavity"),
    ("TRANSITION_EXTRA", 2.00, "Extra height that keeps the transition corners under 45 degrees"),
    ("SLOT_W", 8.50, "Cord slot width: passes 18 and 16 AWG cord (up to about 8.4 mm), stops any plug body 9 mm or thicker"),
    ("SLOT_X", 0.00, "Cord slot centre across the end wall"),
    ("SLOT_Y0", 44.00, "Inner end of the cord slot cut (inside the opening)"),
    ("SLOT_Y1", 64.00, "Outer end of the cord slot cut (outside the end wall)"),
    ("MAG_X", -20.70, "Back magnet centre, from bottom-housing.step"),
    ("MAG_Y", 51.85, "Back magnet centre, from bottom-housing.step"),
    ("MAG_R", 2.175, "Back magnet pocket radius, the same as the front magnet pocket"),
    ("MAG_FLOOR_Z", 34.75, "Back magnet pocket floor, the same depth as the front magnet pocket"),
    ("INNER_X", "=OUTER_X - WALL", "Half width of the cavity"),
    ("INNER_Y", "=OUTER_Y - WALL", "Half length of the cavity"),
    ("INNER_R", "=OUTER_R - WALL", "Cavity corner radius, concentric with the outline"),
    ("TRANSITION_Z", "=CUT_Z + INNER_Y + OPEN_Y0 + TRANSITION_EXTRA", "Bottom of the opening-to-cavity transition (about 41 degrees at the latch end)"),
    ("FLOOR_Z", "=TOP_FACE_Z + CAVITY_DEPTH", "Cavity floor"),
    ("BASE_Z", "=FLOOR_Z + FLOOR_T", "Underside of the base, on the print bed"),
    ("SLOT_R", "=SLOT_W / 2", "Radius of the slot's rounded end"),
    ("SLOT_END_Z", "=FLOOR_Z - SLOT_R", "Centre of the slot's rounded end; the cord rests level with the cavity floor"),
    ("SLOT_TOP_Z", "=TOP_FACE_Z - 1 mm", "Top of the cord slot sketch, above the slide channel floor"),
]
P = {}
for _name, _value, _note in PARAMS:
    if isinstance(_value, str):
        _expr = _value[1:].replace(" mm", "")
        for _k in sorted(P, key=len, reverse=True):
            _expr = _expr.replace(_k, repr(P[_k]))
        P[_name] = float(eval(_expr))  # noqa: S307 - formulas above are our own
    else:
        P[_name] = _value

# Features of the original part that opened through its underside; below
# CUT_Z the part closes them, so they are filled instead of left as voids.
OLD_MAG_HOLE = dict(r=2.40, z0=30.70)  # back magnet bore, pressed from below
OLD_STAND_SLOT = dict(x=(-4.4, 4.4), y=(-47.2, -44.1), z0=36.40)  # stand clip slot

# Plug envelopes (thickness along X, width along Y, length along Z, blades
# included) standing on the cavity floor, and the thinnest plug body the slot
# must stop. US NEMA 1-15 and 5-15 plugs: blades 15.9 mm long, 12.7 mm apart.
PLUGS = (
    ("LargePlug", (32.0, 45.0, 62.0)),  # heater or vacuum plug: 45 x 32 x 46 body plus blades
    ("RightAnglePlug", (24.0, 40.0, 46.0)),  # flat right-angle plug, cord leaving the side
)
THINNEST_PLUG_BODY = 9.5  # flat low-profile plug bodies; molded plugs are 15 to 30 mm thick
CORD_D = 8.4  # 16/3 SJT; 18/3 SJT is about 7.4 mm, flat SPT-2 lamp cord about 4 x 8.5 mm

# --------------------------------------------------------------------------
# Sketch helpers (fully constrained sketches)
# --------------------------------------------------------------------------


def named(sk, ci, name, expr):
    if name:
        sk.renameConstraint(ci, name)
        if expr:
            sk.setExpression("Constraints." + name, expr)


def distance_constraint(sk, kind, geo, pos, value, name=None, expr=None):
    """DistanceX/DistanceY of a sketch point from the origin, kept positive."""
    if value >= 0:
        ci = sk.addConstraint(Sketcher.Constraint(kind, -1, 1, geo, pos, value))
        named(sk, ci, name, expr)
    else:
        ci = sk.addConstraint(Sketcher.Constraint(kind, geo, pos, -1, 1, -value))
        named(sk, ci, name, "-(%s)" % expr if expr else None)
    return ci


def polygon(sk, pts):
    """Closed polygon of lines, fully constrained by its vertex coordinates."""
    n = len(pts)
    geo = [sk.addGeometry(Part.LineSegment(V(pts[i][0], pts[i][1], 0),
                                           V(pts[(i + 1) % n][0], pts[(i + 1) % n][1], 0)), False)
           for i in range(n)]
    for i in range(n):
        sk.addConstraint(Sketcher.Constraint("Coincident", geo[i], 2, geo[(i + 1) % n], 1))
    for i in range(n):
        x, y = pts[i]
        if abs(x) < 1e-9:
            sk.addConstraint(Sketcher.Constraint("PointOnObject", geo[i], 1, -2))
        else:
            distance_constraint(sk, "DistanceX", geo[i], 1, x)
        if abs(y) < 1e-9:
            sk.addConstraint(Sketcher.Constraint("PointOnObject", geo[i], 1, -1))
        else:
            distance_constraint(sk, "DistanceY", geo[i], 1, y)
    return geo


def rounded_rect(sk, x, y, r, exprs=None, prefix=""):
    """Rounded rectangle, fully constrained: side positions and one radius.

    ``exprs`` may hold expressions for "x0", "x1", "y0", "y1" and "r".
    """
    exprs = exprs or {}
    (x0, x1), (y0, y1) = x, y
    lines = [
        ((x0 + r, y0), (x1 - r, y0)),  # bottom
        ((x1, y0 + r), (x1, y1 - r)),  # right
        ((x1 - r, y1), (x0 + r, y1)),  # top
        ((x0, y1 - r), (x0, y0 + r)),  # left
    ]
    corners = [((x1 - r, y0 + r), -90), ((x1 - r, y1 - r), 0), ((x0 + r, y1 - r), 90), ((x0 + r, y0 + r), 180)]
    lg = [sk.addGeometry(Part.LineSegment(V(a[0], a[1], 0), V(b[0], b[1], 0)), False) for a, b in lines]
    ag = []
    for (cx, cy), start in corners:
        arc = Part.ArcOfCircle(Part.Circle(V(cx, cy, 0), V(0, 0, 1), r),
                               math.radians(start), math.radians(start + 90))
        ag.append(sk.addGeometry(arc, False))
    for i in range(4):
        sk.addConstraint(Sketcher.Constraint("Tangent", lg[i], 2, ag[i], 1))
        sk.addConstraint(Sketcher.Constraint("Tangent", ag[i], 2, lg[(i + 1) % 4], 1))
    sk.addConstraint(Sketcher.Constraint("Horizontal", lg[0]))
    sk.addConstraint(Sketcher.Constraint("Horizontal", lg[2]))
    sk.addConstraint(Sketcher.Constraint("Vertical", lg[1]))
    sk.addConstraint(Sketcher.Constraint("Vertical", lg[3]))
    for i in range(1, 4):
        sk.addConstraint(Sketcher.Constraint("Equal", ag[0], ag[i]))
    ci = sk.addConstraint(Sketcher.Constraint("Radius", ag[0], r))
    named(sk, ci, prefix + "R" if exprs.get("r") else None, exprs.get("r"))
    distance_constraint(sk, "DistanceY", lg[0], 1, y0, prefix + "Y0" if exprs.get("y0") else None, exprs.get("y0"))
    distance_constraint(sk, "DistanceY", lg[2], 1, y1, prefix + "Y1" if exprs.get("y1") else None, exprs.get("y1"))
    distance_constraint(sk, "DistanceX", lg[3], 1, x0, prefix + "X0" if exprs.get("x0") else None, exprs.get("x0"))
    distance_constraint(sk, "DistanceX", lg[1], 1, x1, prefix + "X1" if exprs.get("x1") else None, exprs.get("x1"))
    return lg + ag


def circle(sk, cx, cy, r, exprs=None, prefix=""):
    exprs = exprs or {}
    g = sk.addGeometry(Part.Circle(V(cx, cy, 0), V(0, 0, 1), r), False)
    distance_constraint(sk, "DistanceX", g, 3, cx, prefix + "CX" if exprs.get("x") else None, exprs.get("x"))
    distance_constraint(sk, "DistanceY", g, 3, cy, prefix + "CY" if exprs.get("y") else None, exprs.get("y"))
    ci = sk.addConstraint(Sketcher.Constraint("Radius", g, r))
    named(sk, ci, prefix + "R" if exprs.get("r") else None, exprs.get("r"))
    return g


def origin_feature(body, role):
    return [f for f in body.Origin.OriginFeatures if f.Role == role][0]


def sketch(body, name, plane_role, offset=0.0, offset_expr=None):
    """Sketch on a body origin plane, offset along the plane normal."""
    sk = body.newObject("Sketcher::SketchObject", name)
    sk.AttachmentSupport = [(origin_feature(body, plane_role), "")]
    sk.MapMode = "FlatFace"
    sk.AttachmentOffset = App.Placement(V(0, 0, offset), App.Rotation())
    if offset_expr:
        sk.setExpression(".AttachmentOffset.Base.z", offset_expr)
    return sk


def finish_sketch(sk):
    sk.solve()
    if not sk.FullyConstrained:
        raise RuntimeError("sketch %s is not fully constrained" % sk.Name)


def pad(body, sk, name, length_expr, reversed_=False):
    f = body.newObject("PartDesign::Pad", name)
    f.Refine = True
    f.Profile = sk
    f.Type = "Length"
    f.setExpression("Length", length_expr)
    f.Reversed = reversed_
    return f


def pocket(body, sk, name, length_expr=None, reversed_=False, through_symmetric=False):
    f = body.newObject("PartDesign::Pocket", name)
    f.Refine = True
    f.Profile = sk
    if through_symmetric:
        f.Type = "ThroughAll"
        f.Midplane = True
    else:
        f.Type = "Length"
        f.setExpression("Length", length_expr)
    f.Reversed = reversed_
    return f


def loft(body, kind, name, first, second):
    f = body.newObject("PartDesign::%sLoft" % kind, name)
    f.Refine = True
    f.Profile = first
    f.Sections = [second]
    f.Ruled = True
    return f


def recompute(doc, *objs):
    doc.recompute()
    for o in objs:
        if "Invalid" in o.State or "Touched" in o.State:
            raise RuntimeError("%s did not recompute cleanly: %s" % (o.Name, o.State))


def report(label, shape):
    for sh in shape.Shells[1:]:
        sb = sh.BoundBox
        print("    extra shell x %.2f..%.2f y %.2f..%.2f z %.2f..%.2f" % (
            sb.XMin, sb.XMax, sb.YMin, sb.YMax, sb.ZMin, sb.ZMax))
    bb = shape.BoundBox
    print("  %-22s solids %d shells %d valid %s vol %10.1f  z %.2f..%.2f" % (
        label, len(shape.Solids), len(shape.Shells), shape.isValid(), shape.Volume, bb.ZMin, bb.ZMax))


def outline_exprs(prefix, inset_expr=None):
    """Expressions for the housing outline, optionally grown or shrunk."""
    if inset_expr is None:
        return {"x0": "-Params.OUTER_X", "x1": "Params.OUTER_X", "y0": "-Params.OUTER_Y",
                "y1": "Params.OUTER_Y", "r": "Params.OUTER_R"}
    return {"x0": "-(Params.OUTER_X + %s)" % inset_expr, "x1": "Params.OUTER_X + %s" % inset_expr,
            "y0": "-(Params.OUTER_Y + %s)" % inset_expr, "y1": "Params.OUTER_Y + %s" % inset_expr,
            "r": "Params.OUTER_R + %s" % inset_expr}


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
            sheet.set("B%d" % row, "=%g mm" % value)
        sheet.setAlias("B%d" % row, name)
        sheet.set("C%d" % row, note)
    doc.recompute()
    for name, _value, _note in PARAMS:
        got = sheet.get(name)
        got = float(got.Value) if hasattr(got, "Value") else float(got)
        if abs(got - P[name]) > 1e-6:
            raise RuntimeError("spreadsheet %s = %s, expected %s" % (name, got, P[name]))
    return sheet


# --------------------------------------------------------------------------
# Vault body
# --------------------------------------------------------------------------


def build_interface(doc):
    solid = Part.read(str(BOTTOM_SOURCE))
    if len(solid.Solids) != 1:
        raise RuntimeError("expected one solid in %s (is it an LFS pointer?)" % BOTTOM_SOURCE)
    imp = doc.addObject("Part::Feature", "BottomHousingImport")
    imp.Label = "bottom-housing.step (Onshape export)"
    imp.Shape = solid.Solids[0]
    imp.Visibility = False
    keep = doc.addObject("Part::Box", "InterfaceCut")
    keep.Label = "Keep above CUT_Z"
    keep.Length, keep.Width, keep.Height = 100, 160, 40
    keep.Placement.Base = V(-50, -80, 0)
    keep.setExpression("Height", "Params.CUT_Z")
    keep.Visibility = False
    common = doc.addObject("Part::Common", "Interface")
    common.Label = "Bottom housing interface (above CUT_Z)"
    common.Base = imp
    common.Tool = keep
    common.Refine = True
    common.Visibility = False
    recompute(doc, common)
    report("interface", common.Shape)
    return imp, common

def build_part(doc, interface):
    print("plug lockout")
    body = doc.addObject("PartDesign::Body", "PlugLockout")
    body.BaseFeature = interface
    recompute(doc, body)

    # Outer wall: the housing outline from the cut plane down to the base.
    sk = sketch(body, "OuterSketch", "XY_Plane", P["CUT_Z"], "Params.CUT_Z")
    rounded_rect(sk, (-P["OUTER_X"], P["OUTER_X"]), (-P["OUTER_Y"], P["OUTER_Y"]), P["OUTER_R"],
                 outline_exprs("O"), "Outer")
    finish_sketch(sk)
    outer = pad(body, sk, "OuterWall", "Params.BASE_Z - Params.CUT_Z")
    recompute(doc, sk, outer)
    report("OuterWall", body.Shape)

    # Fill the two features the original opened through its underside: the
    # back magnet bore (pressed in from below) and the stand clip slot. Below
    # the cut they would be sealed voids.
    sk = sketch(body, "FillMagnetBoreSketch", "XY_Plane", OLD_MAG_HOLE["z0"])
    circle(sk, P["MAG_X"], P["MAG_Y"], OLD_MAG_HOLE["r"], {"x": "Params.MAG_X", "y": "Params.MAG_Y"}, "Bore")
    finish_sketch(sk)
    f1 = pad(body, sk, "FillMagnetBore", "Params.CUT_Z - %g mm" % OLD_MAG_HOLE["z0"])
    recompute(doc, sk, f1)
    sx, sy = OLD_STAND_SLOT["x"], OLD_STAND_SLOT["y"]
    sk = sketch(body, "FillStandSlotSketch", "XY_Plane", OLD_STAND_SLOT["z0"])
    polygon(sk, [(sx[0], sy[0]), (sx[1], sy[0]), (sx[1], sy[1]), (sx[0], sy[1])])
    finish_sketch(sk)
    f2 = pad(body, sk, "FillStandSlot", "Params.CUT_Z - %g mm" % OLD_STAND_SLOT["z0"])
    recompute(doc, sk, f2)

    # Back magnet, now pressed in from the top like the front magnet.
    sk = sketch(body, "BackMagnetSketch", "XY_Plane", P["TOP_FACE_Z"] - 1.0, "Params.TOP_FACE_Z - 1 mm")
    circle(sk, P["MAG_X"], P["MAG_Y"], P["MAG_R"],
           {"x": "Params.MAG_X", "y": "Params.MAG_Y", "r": "Params.MAG_R"}, "Magnet")
    finish_sketch(sk)
    mag = pocket(body, sk, "BackMagnetPocket", "Params.MAG_FLOOR_Z - Params.TOP_FACE_Z + 1 mm", reversed_=True)
    recompute(doc, sk, mag)
    report("Fills + magnet", body.Shape)

    # Opening through the interface, where the key tray was.
    open_exprs = {"x0": "-Params.OPEN_X", "x1": "Params.OPEN_X", "y0": "Params.OPEN_Y0",
                  "y1": "Params.OPEN_Y1", "r": "Params.OPEN_R"}
    sk = sketch(body, "OpeningSketch", "XY_Plane", P["TOP_FACE_Z"] - 1.0, "Params.TOP_FACE_Z - 1 mm")
    rounded_rect(sk, (-P["OPEN_X"], P["OPEN_X"]), (P["OPEN_Y0"], P["OPEN_Y1"]), P["OPEN_R"], open_exprs, "Open")
    finish_sketch(sk)
    opening = pocket(body, sk, "Opening", "Params.CUT_Z - Params.TOP_FACE_Z + 1 mm", reversed_=True)
    recompute(doc, sk, opening)

    # Transition from the opening out to the thin walls: no face is flatter
    # than 45 degrees, so the ceiling prints without support.
    sk_a = sketch(body, "TransitionTop", "XY_Plane", P["CUT_Z"], "Params.CUT_Z")
    rounded_rect(sk_a, (-P["OPEN_X"], P["OPEN_X"]), (P["OPEN_Y0"], P["OPEN_Y1"]), P["OPEN_R"], open_exprs, "Open")
    finish_sketch(sk_a)
    inner_exprs = {"x0": "-Params.INNER_X", "x1": "Params.INNER_X", "y0": "-Params.INNER_Y",
                   "y1": "Params.INNER_Y", "r": "Params.INNER_R"}
    sk_b = sketch(body, "TransitionBottom", "XY_Plane", P["TRANSITION_Z"], "Params.TRANSITION_Z")
    rounded_rect(sk_b, (-P["INNER_X"], P["INNER_X"]), (-P["INNER_Y"], P["INNER_Y"]), P["INNER_R"],
                 inner_exprs, "Inner")
    finish_sketch(sk_b)
    trans = loft(body, "Subtractive", "Transition", sk_a, sk_b)
    recompute(doc, sk_a, sk_b, trans)

    sk = sketch(body, "CavitySketch", "XY_Plane", P["TRANSITION_Z"], "Params.TRANSITION_Z")
    rounded_rect(sk, (-P["INNER_X"], P["INNER_X"]), (-P["INNER_Y"], P["INNER_Y"]), P["INNER_R"],
                 inner_exprs, "Inner")
    finish_sketch(sk)
    cavity = pocket(body, sk, "Cavity", "Params.FLOOR_Z - Params.TRANSITION_Z", reversed_=True)
    recompute(doc, sk, cavity)
    report("Cavity", body.Shape)

    # Cord slot: a vertical slot through the magnet-end wall, open at the
    # slide channel floor so the cord drops in while the plug drops into the
    # cavity, with a rounded end level with the cavity floor. The top housing
    # closes its upper end when it is on. It is cut only between SLOT_Y0 and
    # SLOT_Y1, so the latch end wall stays whole.
    sk = sketch(body, "CordSlotSketch", "XZ_Plane")
    rot = sk.Placement.Rotation
    ux = rot.multVec(V(1, 0, 0)).x  # sketch u along global X: +1 or -1
    vz = rot.multVec(V(0, 1, 0)).z  # sketch v along global Z: +1 or -1
    ny = rot.multVec(V(0, 0, 1)).y  # sketch normal along global Y: +1 or -1
    if abs(abs(ux) - 1) > 1e-9 or abs(abs(vz) - 1) > 1e-9 or abs(abs(ny) - 1) > 1e-9:
        raise RuntimeError("unexpected XZ_Plane orientation")
    mid_y = (P["SLOT_Y0"] + P["SLOT_Y1"]) / 2
    sk.AttachmentOffset = App.Placement(V(0, 0, mid_y * ny), App.Rotation())
    sk.setExpression(".AttachmentOffset.Base.z", "%s(Params.SLOT_Y0 + Params.SLOT_Y1) / 2" % ("" if ny > 0 else "-"))
    r, cx = P["SLOT_R"], P["SLOT_X"] * ux
    zt, ze = P["SLOT_TOP_Z"] * vz, P["SLOT_END_Z"] * vz
    xl, xr = cx - r, cx + r
    top = sk.addGeometry(Part.LineSegment(V(xl, zt, 0), V(xr, zt, 0)), False)
    right = sk.addGeometry(Part.LineSegment(V(xr, zt, 0), V(xr, ze, 0)), False)
    left = sk.addGeometry(Part.LineSegment(V(xl, ze, 0), V(xl, zt, 0)), False)
    # The rounded end lies on the far side of ze from zt.
    a0, a1 = (math.pi, 2 * math.pi) if ze < zt else (0.0, math.pi)
    arc = sk.addGeometry(Part.ArcOfCircle(Part.Circle(V(cx, ze, 0), V(0, 0, 1), r), a0, a1), False)
    sk.addConstraint(Sketcher.Constraint("Coincident", top, 2, right, 1))
    sk.addConstraint(Sketcher.Constraint("Coincident", left, 2, top, 1))
    # Arc start is at angle a0, end at a1.
    p_a0 = V(cx + r * math.cos(a0), ze + r * math.sin(a0), 0)
    if (p_a0 - V(xr, ze, 0)).Length < 1e-6:
        sk.addConstraint(Sketcher.Constraint("Tangent", right, 2, arc, 1))
        sk.addConstraint(Sketcher.Constraint("Tangent", arc, 2, left, 1))
    else:
        sk.addConstraint(Sketcher.Constraint("Tangent", left, 1, arc, 1))
        sk.addConstraint(Sketcher.Constraint("Tangent", arc, 2, right, 2))
    sk.addConstraint(Sketcher.Constraint("Horizontal", top))
    sk.addConstraint(Sketcher.Constraint("Vertical", right))
    sk.addConstraint(Sketcher.Constraint("Vertical", left))
    ci = sk.addConstraint(Sketcher.Constraint("Radius", arc, r))
    named(sk, ci, "SlotR", "Params.SLOT_R")
    sgn_x = "" if ux > 0 else "-"
    sgn_z = "" if vz > 0 else "-"
    distance_constraint(sk, "DistanceX", arc, 3, cx, "SlotX", "%sParams.SLOT_X" % sgn_x if cx != 0 else None)
    distance_constraint(sk, "DistanceY", arc, 3, ze, "SlotEndZ", "%sParams.SLOT_END_Z" % sgn_z)
    distance_constraint(sk, "DistanceY", top, 1, zt, "SlotTopZ", "%sParams.SLOT_TOP_Z" % sgn_z)
    finish_sketch(sk)
    slot = body.newObject("PartDesign::Pocket", "CordSlot")
    slot.Refine = True
    slot.Profile = sk
    slot.Type = "Length"
    slot.Midplane = True
    slot.setExpression("Length", "Params.SLOT_Y1 - Params.SLOT_Y0")
    recompute(doc, sk, slot)
    report("CordSlot", body.Shape)
    return body


def assembly_parts():
    """Top housing, latch and screen from the assembly, in the housing frame."""
    doc = App.newDocument("asm")
    Import.insert(str(ASSEMBLY), doc.Name)
    place = App.Placement(V(20.35, 49.12, -1.56), App.Rotation(V(0, 1, 0), 180))
    parts = {}
    for o in doc.Objects:
        if o.TypeId == "Part::Feature" and o.Shape.Solids:
            parts[o.Label] = o.Shape.transformGeometry(place.toMatrix())
    App.closeDocument(doc.Name)
    return parts

def build_references(doc):
    """Plug envelopes standing on the cavity floor and the cord in its slot."""
    grp = doc.addObject("App::DocumentObjectGroup", "References")
    floor = P["FLOOR_Z"]
    refs = []
    for name, (thick, width, length) in PLUGS:
        o = doc.addObject("Part::Feature", name)
        o.Shape = Part.makeBox(thick, width, length, V(-thick / 2, -width / 2, floor - length))
        o.Label = "%s %gx%gx%g mm" % (name, thick, width, length)
        grp.addObject(o)
        refs.append(o)
    cord = doc.addObject("Part::Feature", "Cord")
    cord.Shape = Part.makeCylinder(CORD_D / 2, 40, V(P["SLOT_X"], P["OUTER_Y"] - 20, P["SLOT_END_Z"] - 0.05), V(0, 1, 0))
    cord.Label = "Cord %g mm" % CORD_D
    grp.addObject(cord)
    refs.append(cord)
    return refs


def check(part_shape, interface_shape, bottom_shape, refs):
    print("checks")
    fails = []
    if len(part_shape.Solids) != 1 or len(part_shape.Shells) != 1 or not part_shape.isValid():
        fails.append("part is not one valid solid with one shell (sealed void?)")

    # Above the cut the part must be the original, minus the opening, the new
    # magnet pocket and the cord slot, plus the two fills; nothing else may change.
    above = Part.makeBox(100, 160, P["CUT_Z"] - 20, V(-50, -80, 20))
    orig_above = bottom_shape.common(above)
    removed = orig_above.cut(part_shape)
    opening = Part.makeBox(2 * P["OPEN_X"], P["OPEN_Y1"] - P["OPEN_Y0"], 30,
                           V(-P["OPEN_X"], P["OPEN_Y0"], P["CUT_Z"] - 30))
    magnet = Part.makeCylinder(P["MAG_R"] + 0.01, 30, V(P["MAG_X"], P["MAG_Y"], P["MAG_FLOOR_Z"] - 30))
    slot = Part.makeBox(P["SLOT_W"] + 0.02, P["SLOT_Y1"] - P["SLOT_Y0"], 30,
                        V(P["SLOT_X"] - P["SLOT_R"] - 0.01, P["SLOT_Y0"], P["CUT_Z"] - 30))
    slot_removed = removed.common(slot).Volume
    unexplained = removed.cut(opening).cut(magnet).cut(slot).Volume
    added = part_shape.common(above).cut(bottom_shape).Volume
    fills = (math.pi * OLD_MAG_HOLE["r"] ** 2 * (P["CUT_Z"] - OLD_MAG_HOLE["z0"])
             + (OLD_STAND_SLOT["x"][1] - OLD_STAND_SLOT["x"][0]) * (OLD_STAND_SLOT["y"][1] - OLD_STAND_SLOT["y"][0])
             * (P["CUT_Z"] - OLD_STAND_SLOT["z0"]))
    print("  above CUT_Z: slot removed %.1f mm^3, removed elsewhere %.3f mm^3, added %.1f mm^3 (fills at most %.1f)" % (
        slot_removed, unexplained, added, fills))
    if unexplained > 0.05 or added > fills:
        fails.append("the interface above CUT_Z changed")
    if slot_removed < 1.0:
        fails.append("the cord slot does not cut the end wall")

    # The slot must be a through cut at the magnet end only: there is a gap in
    # the wall at the slot and none at the latch end.
    for y, want_open in ((P["OUTER_Y"] - 0.5, True), (-P["OUTER_Y"] + 0.5, False)):
        probe = Part.makeCylinder(1.0, P["FLOOR_Z"] - P["CUT_Z"] - 2, V(P["SLOT_X"], y, P["CUT_Z"] + 1))
        hit = probe.common(part_shape).Volume
        if (hit < 1e-6) != want_open:
            fails.append("end wall at Y %.1f is %s" % (y, "closed" if hit > 1e-6 else "open"))

    parts = assembly_parts()
    for label in ("Housing` Top", "Locking Latch", "PCB Cover"):
        shape = parts[label]
        new = shape.common(part_shape).Volume
        old = shape.common(bottom_shape).Volume
        print("  overlap with %-14s part %.3f  original %.3f" % (label, new, old))
        if new > old + 0.05:
            fails.append("%s overlaps the part more than the original" % label)

    for r in refs:
        hit = r.Shape.common(part_shape).Volume
        print("  %-24s overlap %.3f" % (r.Name, hit))
        if hit > 1e-6:
            fails.append("%s does not fit" % r.Name)
    opening = (2 * P["OPEN_X"], P["OPEN_Y1"] - P["OPEN_Y0"])
    for name, (thick, width, _length) in PLUGS:
        if not (thick < opening[0] and width < opening[1]):
            fails.append("%s does not pass the %gx%g opening" % (name, *opening))
    if not (CORD_D < P["SLOT_W"] < THINNEST_PLUG_BODY):
        fails.append("slot width %.2f is not between the cord %.2f and the thinnest plug %.2f" % (
            P["SLOT_W"], CORD_D, THINNEST_PLUG_BODY))

    bb = part_shape.BoundBox
    print("  height %.2f mm, footprint %.1f x %.1f mm" % (bb.ZLength, bb.XLength, bb.YLength))

    # Overhangs in print orientation (bed = BASE_Z, up = -Z): a facet below
    # the cut whose normal points more than 45 degrees toward the bed.
    mesh = MeshPart.meshFromShape(Shape=part_shape, LinearDeflection=0.05, AngularDeflection=0.2, Relative=False)
    bad = 0.0
    bad_at = []
    for f in mesh.Facets:
        nz_print = -f.Normal.z
        zc = sum(p[2] for p in f.Points) / 3
        if zc > P["CUT_Z"] + 0.01 and zc < P["BASE_Z"] - 0.01 and nz_print < -math.cos(math.radians(45)) - 1e-3:
            bad += f.Area
            bad_at.append((round(f.Points[0][0]), round(f.Points[0][1]), round(zc, 1), round(nz_print, 2)))
    print("  overhang area below the cut steeper than 45 deg: %.2f mm^2" % bad)
    for spot in sorted(set(bad_at))[:20]:
        print("    overhang facet at", spot)
    if bad > 0.5:
        fails.append("overhangs below the cut")
    if fails:
        raise RuntimeError("; ".join(fails))


def export_stl(shape, path):
    mesh = MeshPart.meshFromShape(Shape=shape, LinearDeflection=STL_LINEAR_DEFLECTION,
                                  AngularDeflection=STL_ANGULAR_DEFLECTION, Relative=False)
    mesh.write(str(path))

def main():
    for src in (BOTTOM_SOURCE, TOP_SOURCE, ASSEMBLY):
        if src.stat().st_size < 1000:
            raise RuntimeError("%s is an LFS pointer; set LKBX_CAD_DIR to real STEP files" % src)
    doc = App.newDocument("PlugLockout")
    build_params(doc)
    imp, interface = build_interface(doc)
    part = build_part(doc, interface)
    refs = build_references(doc)
    doc.recompute()
    check(part.Shape, interface.Shape, imp.Shape, refs)
    doc.saveAs(str(DOC_OUT))
    part.Shape.exportStep(str(PART_OUT))
    # Print orientation: base on the bed. Rotate 180 degrees about X so the
    # part keeps its handedness, then drop the base onto Z = 0.
    printed = part.Shape.copy()
    printed.rotate(V(0, 0, 0), V(1, 0, 0), 180)
    printed.translate(V(0, 0, -printed.BoundBox.ZMin))
    export_stl(printed, PART_STL)
    for out in (DOC_OUT, PART_OUT, PART_STL):
        print("wrote", out, out.stat().st_size)


main()
