"""Parametric FreeCAD model of the wrist restraint hub.

The hub is a whole, unmodified lockbox (top housing, PCB, screen, battery,
motor, latch, buttons) on a new bottom part. The new bottom is two pieces:

* **Cuff dock**: the original bottom housing above ``CUT_Z`` (slide rails,
  latch slot, magnets), with the key tray opened through and a short skirt
  below, so the stock top housing slides on, latches and detects it exactly
  as before.
* **Cuff plate**: a block that fills the dock from below. Each cuff's strap
  comes up through an entry slot, lies flat over two steel pins, and its
  free end goes back down through an exit slot. With the top housing on, the
  PCB cover sits 1 mm above the pin tips, so a strap cannot lift off its
  pins and neither cuff can be loosened or opened.

The plate is held in the dock by four T7 screws through the long side walls.
Removing them drops the plate, both straps and the cuffs out of the bottom
without the battery, the motor or the firmware: this is the emergency
release, and it works with a flat battery.

Run headless with FreeCAD 1.0:

    freecadcmd build_freecad.py

The Onshape exports in ``hardware/cad/`` are Git LFS files. If they are not
checked out, point ``LKBX_CAD_DIR`` at a folder that holds real copies of
``bottom-housing.step``, ``top-housing.step`` and ``lockbox-assembly.step``.

Creates ``wrist-restraints.FCStd`` next to this script and writes, into
``hardware/cad/`` (gitignored, regenerate them from the FCStd):

    cuff-dock.step / cuff-plate.step   the parts in the housing frame
    cuff-dock-print.stl                skirt down on the bed
    cuff-plate-print.stl               underside down on the bed

Document contents:

    Params      spreadsheet of the tunable dimensions; features read them by expression
    BottomHousingImport / InterfaceCut / Interface
                the Onshape bottom housing, and the part of it above CUT_Z
                that the dock keeps
    CuffDock    PartDesign body on that interface: skirt, chamfer, fills,
                top-entry back magnet pocket, opening, countersunk screw holes
    CuffPlate   PartDesign body: block, strap entry and exit slots with
                flared edges, pin holes, screw pilot holes
    References  steel pins, strap layers, screws and two wrist envelopes

Coordinates are the top-housing STEP frame: Z is depth from the screen face
(it grows toward the wrists), Y runs along the housing length and X across
its width, in millimetres.
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
DOC_OUT = HERE / "wrist-restraints.FCStd"
DOCK_OUT = OUT_DIR / "cuff-dock.step"
PLATE_OUT = OUT_DIR / "cuff-plate.step"
DOCK_STL = OUT_DIR / "cuff-dock-print.stl"
PLATE_STL = OUT_DIR / "cuff-plate-print.stl"
STL_LINEAR_DEFLECTION = 0.01
STL_ANGULAR_DEFLECTION = 0.1

V = App.Vector

# --------------------------------------------------------------------------
# Parameters. Every row becomes a spreadsheet cell with an alias of the same
# name; features reference them by expression so they can be tuned in FreeCAD.
# Strap positions along Y are measured from OPEN_YC, the middle of the
# opening, so the two cuffs mirror about it.
# --------------------------------------------------------------------------
PARAMS = [
    ("TOP_FACE_Z", 28.70, "Floor of the slide channel, where the old key tray started"),
    ("CUT_Z", 41.00, "The original part is kept above this plane (latch slot floor)"),
    ("OUTER_X", 30.00, "Half width of the housing outline"),
    ("OUTER_Y", 60.50, "Half length of the housing outline"),
    ("OUTER_R", 5.00, "Corner radius of the housing outline"),
    ("OPEN_X", 24.00, "Half width of the opening through the dock"),
    ("OPEN_Y0", -42.50, "Latch end of the opening (5 mm clear of the latch slot)"),
    ("OPEN_Y1", 47.00, "Magnet end of the opening (2.7 mm clear of the back magnet)"),
    ("OPEN_R", 3.00, "Opening corner radius"),
    ("BASE_Z", 51.00, "Underside of the dock and the plate, against the cuffs"),
    ("BASE_CHAMFER", 1.50, "Inset of the chamfer on the dock's lower outer edge"),
    ("STEEP", "=1.2", "Rise per unit inset (no unit) of the chamfer and flares (about 50 degrees, so they print without support)"),
    ("FIT", 0.30, "Clearance between the plate and the dock opening, per side"),
    ("PLATE_TOP_Z", 37.50, "Top of the plate, where the straps lie"),
    ("PIN_D", 5.00, "Steel dowel pin diameter (5 x 20 mm)"),
    ("PIN_HOLE_D", 4.90, "Printed pin hole, a press fit on the pin"),
    ("PIN_LEN", 20.00, "Steel dowel pin length"),
    ("PIN_TIP_Z", 29.30, "Pin tips, 1 mm under the PCB cover"),
    ("PIN_X", 9.50, "Pin offset either side of the strap centre line"),
    ("PIN_DY", 25.00, "Pin rows, from OPEN_YC"),
    ("STRAP_W", 38.00, "Strap width (1.5 in biothane or leather)"),
    ("STRAP_T", 3.00, "Thickest strap the design allows; thinner is fine down to 1.2 mm"),
    ("SLOT_L", 41.00, "Length of the strap slots across the plate"),
    ("SLOT_R", 1.00, "Slot corner radius"),
    ("ENTRY_W", 7.00, "Entry slot width: both strap layers"),
    ("ENTRY_DY", 35.00, "Entry slots, from OPEN_YC"),
    ("EXIT_W", 4.00, "Exit slot width: the free end only"),
    ("EXIT_DY", 16.00, "Exit slots, from OPEN_YC"),
    ("FLARE", 1.50, "Inset of the flare on the slot edges the straps bend around"),
    ("SCREW_Z", 46.00, "Screw axis depth, mid skirt"),
    ("SCREW_DY", 26.00, "Screw positions along each long wall, from OPEN_YC"),
    ("SCREW_CLEAR_R", 1.60, "Clearance hole for a #4 screw"),
    ("SCREW_HEAD_R", 2.85, "Countersink radius at the wall face (#4 flat head)"),
    ("SCREW_PILOT_R", 1.10, "Pilot hole in the plate for a #4 Plastite thread"),
    ("SCREW_LEN", 12.70, "#4-20 x 1/2 in, the screw already in the lockbox kit"),
    ("MAG_X", -20.70, "Back magnet centre, from bottom-housing.step"),
    ("MAG_Y", 51.85, "Back magnet centre, from bottom-housing.step"),
    ("MAG_R", 2.175, "Back magnet pocket radius, the same as the front magnet pocket"),
    ("MAG_FLOOR_Z", 34.75, "Back magnet pocket floor, the same depth as the front magnet pocket"),
    ("OPEN_YC", "=(OPEN_Y0 + OPEN_Y1) / 2", "Middle of the opening; the cuffs mirror about it"),
    ("PLATE_X", "=OPEN_X - FIT", "Half width of the plate"),
    ("PLATE_Y0", "=OPEN_Y0 + FIT", "Latch end of the plate"),
    ("PLATE_Y1", "=OPEN_Y1 - FIT", "Magnet end of the plate"),
    ("PLATE_R", "=OPEN_R - FIT", "Plate corner radius"),
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

# Features of the original part that opened through its underside; the
# skirt closes them, so they are filled instead of left as voids.
OLD_MAG_HOLE = dict(r=2.40, z0=30.70)  # back magnet bore, pressed from below
OLD_STAND_SLOT = dict(x=(-4.4, 4.4), y=(-47.2, -44.1), z0=36.40)  # stand clip slot

# The two cuffs, by side: -1 is the latch end, +1 the magnet end.
SIDES = (-1, 1)
STRAP_MIN_T = 1.2  # thinnest strap that still cannot lift over the pin tips
CEILING_GAP = (0.5, 1.15)  # allowed gap from the pin tips to the parts above
WRIST = (62.0, 42.0)  # wrist envelope for the renders, width x depth (about 165 mm round)
PAD_T = 6.0  # cuff padding under the hub

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
# Interface: the original bottom housing above CUT_Z
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



def yc_expr(side, name):
    """Expression for OPEN_YC plus or minus a Params offset."""
    return "Params.OPEN_YC %s Params.%s" % ("+" if side > 0 else "-", name)


def yc(side, name):
    return P["OPEN_YC"] + side * P[name]


def xz_axis_sketch(body, name, y, y_expr, z, z_expr):
    """Sketch on the XZ plane whose H axis is the line (Y = y, Z = z) along X.

    Returns the sketch and the signs that map global X and Z to sketch u, v.
    """
    sk = sketch(body, name, "XZ_Plane")
    rot = sk.Placement.Rotation
    ux = rot.multVec(V(1, 0, 0)).x
    vz = rot.multVec(V(0, 1, 0)).z
    ny = rot.multVec(V(0, 0, 1)).y
    if abs(abs(ux) - 1) > 1e-9 or abs(abs(vz) - 1) > 1e-9 or abs(abs(ny) - 1) > 1e-9:
        raise RuntimeError("unexpected XZ_Plane orientation")
    sk.AttachmentOffset = App.Placement(V(0, z * vz, y * ny), App.Rotation())
    sk.setExpression(".AttachmentOffset.Base.y", ("" if vz > 0 else "-") + "(%s)" % z_expr)
    sk.setExpression(".AttachmentOffset.Base.z", ("" if ny > 0 else "-") + "(%s)" % y_expr)
    return sk, ux


def groove(body, sk, name):
    f = body.newObject("PartDesign::Groove", name)
    f.Refine = True
    f.Profile = sk
    f.ReferenceAxis = (sk, ["H_Axis"])
    f.Angle = 360.0
    return f


def screw_profiles(sk, ux, profile):
    """Add a revolve profile (global X, radius) on both long walls."""
    for mirror in (1, -1):
        polygon(sk, [(mirror * x * ux, r) for x, r in profile])


def slot_exprs(side, dy, w, grow=None):
    """Rounded-rectangle expressions for a strap slot, optionally grown."""
    g = " + %s" % grow if grow else ""
    return {"x0": "-(Params.SLOT_L / 2%s)" % g, "x1": "Params.SLOT_L / 2%s" % g,
            "y0": "%s - (Params.%s / 2%s)" % (yc_expr(side, dy), w, g),
            "y1": "%s + Params.%s / 2%s" % (yc_expr(side, dy), w, g),
            "r": "Params.SLOT_R%s" % g}


def slot_rect(sk, side, dy, w, grow=0.0, grow_expr=None, prefix=""):
    y = yc(side, dy)
    half_l, half_w = P["SLOT_L"] / 2 + grow, P[w] / 2 + grow
    rounded_rect(sk, (-half_l, half_l), (y - half_w, y + half_w), P["SLOT_R"] + grow,
                 slot_exprs(side, dy, w, grow_expr), prefix)


# --------------------------------------------------------------------------
# Cuff dock
# --------------------------------------------------------------------------


def build_dock(doc, interface):
    print("cuff dock")
    body = doc.addObject("PartDesign::Body", "CuffDock")
    body.BaseFeature = interface
    recompute(doc, body)

    # Skirt: the housing outline from the cut plane down toward the cuffs,
    # with a chamfer on the lower edge, a little steeper than 45 degrees.
    sk = sketch(body, "SkirtSketch", "XY_Plane", P["CUT_Z"], "Params.CUT_Z")
    rounded_rect(sk, (-P["OUTER_X"], P["OUTER_X"]), (-P["OUTER_Y"], P["OUTER_Y"]), P["OUTER_R"],
                 outline_exprs("O"), "Outer")
    finish_sketch(sk)
    skirt = pad(body, sk, "Skirt", "Params.BASE_Z - Params.BASE_CHAMFER * Params.STEEP - Params.CUT_Z")
    recompute(doc, sk, skirt)
    c = P["BASE_CHAMFER"]
    sk_a = sketch(body, "ChamferTop", "XY_Plane", P["BASE_Z"] - c * P["STEEP"],
                  "Params.BASE_Z - Params.BASE_CHAMFER * Params.STEEP")
    rounded_rect(sk_a, (-P["OUTER_X"], P["OUTER_X"]), (-P["OUTER_Y"], P["OUTER_Y"]), P["OUTER_R"],
                 outline_exprs("O"), "Outer")
    finish_sketch(sk_a)
    sk_b = sketch(body, "ChamferBottom", "XY_Plane", P["BASE_Z"], "Params.BASE_Z")
    rounded_rect(sk_b, (-P["OUTER_X"] + c, P["OUTER_X"] - c), (-P["OUTER_Y"] + c, P["OUTER_Y"] - c),
                 P["OUTER_R"] - c, outline_exprs("I", "-Params.BASE_CHAMFER"), "Inset")
    finish_sketch(sk_b)
    cham = loft(body, "Additive", "BaseChamfer", sk_a, sk_b)
    recompute(doc, sk_a, sk_b, cham)
    report("Skirt", body.Shape)

    # Fill the two features the original opened through its underside: the
    # back magnet bore (pressed in from below) and the stand clip slot.
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

    # Back magnet, pressed in from the top like the front magnet.
    sk = sketch(body, "BackMagnetSketch", "XY_Plane", P["TOP_FACE_Z"] - 1.0, "Params.TOP_FACE_Z - 1 mm")
    circle(sk, P["MAG_X"], P["MAG_Y"], P["MAG_R"],
           {"x": "Params.MAG_X", "y": "Params.MAG_Y", "r": "Params.MAG_R"}, "Magnet")
    finish_sketch(sk)
    mag = pocket(body, sk, "BackMagnetPocket", "Params.MAG_FLOOR_Z - Params.TOP_FACE_Z + 1 mm", reversed_=True)
    recompute(doc, sk, mag)

    # Opening straight through, where the key tray was. The plate fills it
    # from below, so the dock has no ceiling to overhang.
    open_exprs = {"x0": "-Params.OPEN_X", "x1": "Params.OPEN_X", "y0": "Params.OPEN_Y0",
                  "y1": "Params.OPEN_Y1", "r": "Params.OPEN_R"}
    sk = sketch(body, "OpeningSketch", "XY_Plane", P["TOP_FACE_Z"] - 1.0, "Params.TOP_FACE_Z - 1 mm")
    rounded_rect(sk, (-P["OPEN_X"], P["OPEN_X"]), (P["OPEN_Y0"], P["OPEN_Y1"]), P["OPEN_R"], open_exprs, "Open")
    finish_sketch(sk)
    opening = pocket(body, sk, "Opening", "Params.BASE_Z - Params.TOP_FACE_Z + 2 mm", reversed_=True)
    recompute(doc, sk, opening)
    report("Opening", body.Shape)

    # Countersunk screw holes through both long walls (45 degree countersink,
    # close to the 82 degree #4 flat head; the head sits flush or just under).
    ox, hr, cr = P["OUTER_X"], P["SCREW_HEAD_R"], P["SCREW_CLEAR_R"]
    profile = [(ox + 0.5, 0.0), (ox + 0.5, hr + 0.5), (ox - (hr - cr), cr),
               (P["OPEN_X"] - 0.5, cr), (P["OPEN_X"] - 0.5, 0.0)]
    for side in SIDES:
        name = "ScrewHoles%s" % ("Latch" if side < 0 else "Magnet")
        sk, ux = xz_axis_sketch(body, name + "Sketch", yc(side, "SCREW_DY"), yc_expr(side, "SCREW_DY"),
                                P["SCREW_Z"], "Params.SCREW_Z")
        screw_profiles(sk, ux, profile)
        finish_sketch(sk)
        g = groove(body, sk, name)
        recompute(doc, sk, g)
    report("CuffDock", body.Shape)
    return body


# --------------------------------------------------------------------------
# Cuff plate
# --------------------------------------------------------------------------


def build_plate(doc):
    print("cuff plate")
    body = doc.addObject("PartDesign::Body", "CuffPlate")
    plate_exprs = {"x0": "-Params.PLATE_X", "x1": "Params.PLATE_X", "y0": "Params.PLATE_Y0",
                   "y1": "Params.PLATE_Y1", "r": "Params.PLATE_R"}
    sk = sketch(body, "PlateSketch", "XY_Plane", P["PLATE_TOP_Z"], "Params.PLATE_TOP_Z")
    rounded_rect(sk, (-P["PLATE_X"], P["PLATE_X"]), (P["PLATE_Y0"], P["PLATE_Y1"]), P["PLATE_R"],
                 plate_exprs, "Plate")
    finish_sketch(sk)
    block = pad(body, sk, "Block", "Params.BASE_Z - Params.PLATE_TOP_Z")
    recompute(doc, sk, block)

    f, k = P["FLARE"], P["STEEP"]
    for side in SIDES:
        tag = "Latch" if side < 0 else "Magnet"
        for kind, dy, w in (("Entry", "ENTRY_DY", "ENTRY_W"), ("Exit", "EXIT_DY", "EXIT_W")):
            name = "%s%sSlot" % (tag, kind)
            sk = sketch(body, name + "Sketch", "XY_Plane", P["PLATE_TOP_Z"], "Params.PLATE_TOP_Z")
            slot_rect(sk, side, dy, w, prefix="Slot")
            finish_sketch(sk)
            s = pocket(body, sk, name, through_symmetric=True)
            recompute(doc, sk, s)
            # Flares on the top and bottom edges, where the strap turns.
            for where, z_a, za_expr, g_a, ga_expr, z_b, zb_expr, g_b, gb_expr in (
                ("Top", P["PLATE_TOP_Z"] - 0.5 * k, "Params.PLATE_TOP_Z - 0.5 mm * Params.STEEP",
                 f + 0.5, "Params.FLARE + 0.5 mm",
                 P["PLATE_TOP_Z"] + f * k, "Params.PLATE_TOP_Z + Params.FLARE * Params.STEEP", 0.0, None),
                ("Bottom", P["BASE_Z"] - f * k, "Params.BASE_Z - Params.FLARE * Params.STEEP", 0.0, None,
                 P["BASE_Z"] + 0.5 * k, "Params.BASE_Z + 0.5 mm * Params.STEEP", f + 0.5, "Params.FLARE + 0.5 mm"),
            ):
                sk_a = sketch(body, "%s%sFlareA" % (name, where), "XY_Plane", z_a, za_expr)
                slot_rect(sk_a, side, dy, w, g_a, ga_expr, "A")
                finish_sketch(sk_a)
                sk_b = sketch(body, "%s%sFlareB" % (name, where), "XY_Plane", z_b, zb_expr)
                slot_rect(sk_b, side, dy, w, g_b, gb_expr, "B")
                finish_sketch(sk_b)
                fl = loft(body, "Subtractive", "%s%sFlare" % (name, where), sk_a, sk_b)
                recompute(doc, sk_a, sk_b, fl)

    # Pin holes, through so a pin can be pressed in and driven back out.
    sk = sketch(body, "PinHoleSketch", "XY_Plane", P["PLATE_TOP_Z"], "Params.PLATE_TOP_Z")
    for side in SIDES:
        for sx in (-1, 1):
            tag = "%s%s" % ("L" if side < 0 else "M", "n" if sx < 0 else "p")
            circle(sk, sx * P["PIN_X"], yc(side, "PIN_DY"), P["PIN_HOLE_D"] / 2,
                   {"x": ("-" if sx < 0 else "") + "Params.PIN_X", "y": yc_expr(side, "PIN_DY"),
                    "r": "Params.PIN_HOLE_D / 2"}, "Pin" + tag)
    finish_sketch(sk)
    holes = pocket(body, sk, "PinHoles", through_symmetric=True)
    recompute(doc, sk, holes)

    # Pilot holes for the four screws.
    tip = P["OUTER_X"] - P["SCREW_LEN"] - 0.5
    profile = [(P["PLATE_X"] + 0.5, 0.0), (P["PLATE_X"] + 0.5, P["SCREW_PILOT_R"]),
               (tip, P["SCREW_PILOT_R"]), (tip, 0.0)]
    for side in SIDES:
        name = "Pilots%s" % ("Latch" if side < 0 else "Magnet")
        sk, ux = xz_axis_sketch(body, name + "Sketch", yc(side, "SCREW_DY"), yc_expr(side, "SCREW_DY"),
                                P["SCREW_Z"], "Params.SCREW_Z")
        screw_profiles(sk, ux, profile)
        finish_sketch(sk)
        g = groove(body, sk, name)
        recompute(doc, sk, g)
    report("CuffPlate", body.Shape)
    return body


# --------------------------------------------------------------------------
# References
# --------------------------------------------------------------------------


def strap_shapes(side):
    """The two layers of one cuff's strap inside the hub, with pin holes.

    The free end goes in first and lies on the plate: up the entry slot, over
    the pins and down the exit slot. The fixed end goes up the same slot on
    top of it and ends 12 mm past the pins.
    """
    t, w = P["STRAP_T"], P["STRAP_W"]
    top, base = P["PLATE_TOP_Z"], P["BASE_Z"]
    y_entry, y_exit, y_pin = yc(side, "ENTRY_DY"), yc(side, "EXIT_DY"), yc(side, "PIN_DY")
    ew = P["ENTRY_W"]
    # Layer faces in the entry slot: the free end on the inner (pin) side.
    inner = y_entry - side * (ew / 2 - 0.5)  # 0.5 mm off the inner slot wall

    def box(y0, y1, z0, z1):
        y0, y1 = min(y0, y1), max(y0, y1)
        return Part.makeBox(w, y1 - y0, z1 - z0, V(-w / 2, y0, z0))

    free = box(inner, y_exit - side * (P["EXIT_W"] / 2 - 0.5), top - t, top)  # flat run
    free = free.fuse(box(inner, inner + side * t, top, base + 5))  # up the entry slot
    x_exit = y_exit - side * (P["EXIT_W"] / 2 - 0.5)
    free = free.fuse(box(x_exit, x_exit + side * t, top - t, base + 5))  # down the exit slot
    fixed = box(inner + side * t, y_pin - side * 12.0, top - 2 * t, top - t)
    fixed = fixed.fuse(box(inner + side * t, inner + side * 2 * t, top - t, base + 5))
    for sx in (-1, 1):
        hole = Part.makeCylinder(P["PIN_D"] / 2 + 0.25, 40, V(sx * P["PIN_X"], y_pin, top - 30))
        free, fixed = free.cut(hole), fixed.cut(hole)
    return free.removeSplitter(), fixed.removeSplitter()


def build_references(doc):
    grp = doc.addObject("App::DocumentObjectGroup", "References")
    refs = {"pins": [], "straps": [], "screws": [], "wrists": []}

    def add(kind, name, shape, label):
        o = doc.addObject("Part::Feature", name)
        o.Shape = shape
        o.Label = label
        grp.addObject(o)
        refs[kind].append(o)

    for side in SIDES:
        tag = "Latch" if side < 0 else "Magnet"
        for sx in (-1, 1):
            add("pins", "Pin%s%s" % (tag, "Left" if sx < 0 else "Right"),
                Part.makeCylinder(P["PIN_D"] / 2, P["PIN_LEN"], V(sx * P["PIN_X"], yc(side, "PIN_DY"), P["PIN_TIP_Z"])),
                "Steel dowel pin %g x %g mm" % (P["PIN_D"], P["PIN_LEN"]))
        free, fixed = strap_shapes(side)
        add("straps", "Strap%sFree" % tag, free, "%s cuff strap, free end" % tag)
        add("straps", "Strap%sFixed" % tag, fixed, "%s cuff strap, fixed end" % tag)
        for sx in (-1, 1):
            x0 = sx * P["OUTER_X"]
            add("screws", "Screw%s%s" % (tag, "Left" if sx < 0 else "Right"),
                Part.makeCylinder(1.4, P["SCREW_LEN"], V(x0, yc(side, "SCREW_DY"), P["SCREW_Z"]), V(-sx, 0, 0)),
                "#4 x 1/2 in screw (shank)")
        # Wrist envelope under the cuff, forearm along X, behind the padding.
        ww, wd = WRIST
        e = Part.Ellipse(V(0, ww / 2, 0), V(0, 0, wd / 2), V(0, 0, 0))
        wrist = Part.Face(Part.Wire(e.toShape())).extrude(V(80, 0, 0))
        wrist.translate(V(-40, yc(side, "ENTRY_DY"), P["BASE_Z"] + PAD_T + wd / 2))
        add("wrists", "Wrist%s" % tag, wrist, "Wrist envelope %g x %g mm" % WRIST)
    return refs


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



def export_stl(shape, path):
    mesh = MeshPart.meshFromShape(Shape=shape, LinearDeflection=STL_LINEAR_DEFLECTION,
                                  AngularDeflection=STL_ANGULAR_DEFLECTION, Relative=False)
    mesh.write(str(path))



def overhang_area(shape, z_from, bed_z, skip):
    """Facet area steeper than 45 degrees in print orientation (bed at bed_z, up is -Z).

    ``skip`` holds boxes (screw holes) whose small overhangs FDM bridges anyway.
    """
    mesh = MeshPart.meshFromShape(Shape=shape, LinearDeflection=0.01, AngularDeflection=0.05, Relative=False)
    bad, spots = 0.0, []
    for f in mesh.Facets:
        c = V(*[sum(p[i] for p in f.Points) / 3 for i in range(3)])
        if not (z_from + 0.01 < c.z < bed_z - 0.01):
            continue
        if any(b.isInside(c) for b in skip):
            continue
        if -f.Normal.z < -math.cos(math.radians(45)) - 1e-3:
            bad += f.Area
            spots.append((round(c.x), round(c.y), round(c.z, 1)))
    return bad, sorted(set(spots))[:20]


def check(dock, plate, interface_shape, bottom_shape, refs):
    print("checks")
    fails = []
    for label, shape in (("dock", dock), ("plate", plate)):
        if len(shape.Solids) != 1 or len(shape.Shells) != 1 or not shape.isValid():
            fails.append("%s is not one valid solid with one shell (sealed void?)" % label)

    # Above the cut the dock must be the original, minus the opening and the
    # new magnet pocket, plus the two fills; nothing else may change.
    above = Part.makeBox(100, 160, P["CUT_Z"] - 20, V(-50, -80, 20))
    removed = bottom_shape.common(above).cut(dock)
    opening = Part.makeBox(2 * P["OPEN_X"], P["OPEN_Y1"] - P["OPEN_Y0"], 30,
                           V(-P["OPEN_X"], P["OPEN_Y0"], P["CUT_Z"] - 30))
    magnet = Part.makeCylinder(P["MAG_R"] + 0.01, 30, V(P["MAG_X"], P["MAG_Y"], P["MAG_FLOOR_Z"] - 30))
    unexplained = removed.cut(opening).cut(magnet).Volume
    added = dock.common(above).cut(bottom_shape).Volume
    fills = (math.pi * OLD_MAG_HOLE["r"] ** 2 * (P["CUT_Z"] - OLD_MAG_HOLE["z0"])
             + (OLD_STAND_SLOT["x"][1] - OLD_STAND_SLOT["x"][0]) * (OLD_STAND_SLOT["y"][1] - OLD_STAND_SLOT["y"][0])
             * (P["CUT_Z"] - OLD_STAND_SLOT["z0"]))
    print("  above CUT_Z: removed outside the opening %.3f mm^3, added %.1f mm^3 (fills at most %.1f)" % (
        unexplained, added, fills))
    if unexplained > 0.05 or added > fills:
        fails.append("the interface above CUT_Z changed")

    parts = assembly_parts()
    above_parts = [s for k, s in parts.items() if k not in ("Housing` Bottom",)]
    for label in ("Housing` Top", "Locking Latch", "PCB Cover"):
        shape = parts[label]
        new = shape.common(dock).Volume + shape.common(plate).Volume
        old = shape.common(bottom_shape).Volume
        print("  overlap with %-14s parts %.3f  original %.3f" % (label, new, old))
        if new > old + 0.05:
            fails.append("%s overlaps the new parts more than the original" % label)

    gap = dock.distToShape(plate)[0]
    hit = dock.common(plate).Volume
    print("  plate in dock: overlap %.4f mm^3, gap %.2f mm" % (hit, gap))
    if hit > 1e-6 or gap < P["FIT"] - 0.01:
        fails.append("the plate does not fit the dock opening")

    # Pins: seated in the plate, clear of everything above, and close enough
    # under it that no strap thicker than STRAP_MIN_T can rise over a tip.
    for o in refs["pins"]:
        pin = o.Shape
        seat = pin.common(plate).Volume
        clear = min(pin.distToShape(s)[0] for s in above_parts)
        hits = sum(pin.common(s).Volume for s in above_parts)
        print("  %-18s press interference %.1f mm^3, gap to the parts above %.2f mm" % (o.Name, seat, clear))
        if hits > 1e-6 or not (CEILING_GAP[0] <= clear <= CEILING_GAP[1]):
            fails.append("%s ceiling gap %.2f outside %s" % (o.Name, clear, CEILING_GAP))
        if clear >= STRAP_MIN_T:
            fails.append("a %.1f mm strap could lift off %s" % (STRAP_MIN_T, o.Name))
        embed = P["PIN_TIP_Z"] + P["PIN_LEN"] - P["PLATE_TOP_Z"]
        if embed < 0.55 * P["PIN_LEN"] or P["PIN_TIP_Z"] + P["PIN_LEN"] > P["BASE_Z"] - 0.5:
            fails.append("pin embedment %.1f mm is too short or the pin pokes out" % embed)

    # Straps: in the slots and on the plate, clear of the dock, the plate,
    # the pins and everything above.
    pins = refs["pins"][0].Shape.fuse([o.Shape for o in refs["pins"][1:]])
    for o in refs["straps"]:
        s = o.Shape
        vals = {"dock": s.common(dock).Volume, "plate": s.common(plate).Volume, "pins": s.common(pins).Volume,
                "above": sum(s.common(p).Volume for p in above_parts)}
        print("  %-18s overlaps %s" % (o.Name, ", ".join("%s %.3f" % kv for kv in vals.items())))
        if any(v > 1e-3 for v in vals.values()):
            fails.append("%s does not fit" % o.Name)
    stack = 2 * P["STRAP_T"]
    if P["PLATE_TOP_Z"] - stack < P["PIN_TIP_Z"] + 1.0:
        fails.append("the pins stand less than 1 mm above a %.1f mm strap stack" % stack)

    # The top housing slides off toward +Y: its parts (the latch is raised by
    # then) must pass over the pins and the straps.
    movers = [parts[k] for k in parts if k not in ("Housing` Bottom", "Locking Latch")]
    loose = pins.fuse([o.Shape for o in refs["straps"]])
    hit_at = None
    for dy in range(0, 130, 3):
        for m in movers:
            mm = m.copy()
            mm.translate(V(0, dy, 0))
            if mm.BoundBox.intersect(loose.BoundBox) and mm.common(loose).Volume > 1e-6:
                hit_at = dy
                break
        if hit_at is not None:
            break
    print("  top housing slide-off over pins and straps: %s" % (
        "clear for 129 mm" if hit_at is None else "hits at %d mm" % hit_at))
    if hit_at is not None:
        fails.append("the top housing hits a pin or strap %d mm into its slide" % hit_at)

    # Screws: the shank runs through the wall into the plate pilot.
    for o in refs["screws"]:
        in_plate = o.Shape.common(plate).Volume
        print("  %-18s in plate %.1f mm^3 (pilot is undersize, so this is thread engagement)" % (o.Name, in_plate))
        if in_plate < 10:
            fails.append("%s does not reach the plate" % o.Name)

    for o in refs["wrists"]:
        if o.Shape.common(dock).Volume + o.Shape.common(plate).Volume > 1e-6:
            fails.append("%s overlaps the hub" % o.Name)

    bb = dock.BoundBox
    print("  dock height below the top housing %.2f mm, footprint %.1f x %.1f mm, hub height %.1f mm" % (
        bb.ZMax - P["CUT_Z"], bb.XLength, bb.YLength, P["BASE_Z"]))

    holes = []
    for side in SIDES:
        y = yc(side, "SCREW_DY")
        holes.append(App.BoundBox(-P["OUTER_X"] - 1, y - P["SCREW_HEAD_R"] - 0.6, P["SCREW_Z"] - P["SCREW_HEAD_R"] - 0.6,
                                  P["OUTER_X"] + 1, y + P["SCREW_HEAD_R"] + 0.6, P["SCREW_Z"] + P["SCREW_HEAD_R"] + 0.6))
    for label, shape, z_from in (("dock", dock, P["CUT_Z"]), ("plate", plate, P["PLATE_TOP_Z"] - 1)):
        bad, spots = overhang_area(shape, z_from, P["BASE_Z"], holes)
        print("  %s overhang steeper than 45 deg (screw holes excepted): %.2f mm^2" % (label, bad))
        for spot in spots:
            print("    overhang facet at", spot)
        if bad > 0.5:
            fails.append("%s has overhangs" % label)
    if fails:
        raise RuntimeError("; ".join(fails))


def print_copy(shape):
    """Bed on BASE_Z: rotate 180 degrees about X (keeps handedness), drop to Z = 0."""
    s = shape.copy()
    s.rotate(V(0, 0, 0), V(1, 0, 0), 180)
    s.translate(V(0, 0, -s.BoundBox.ZMin))
    return s


def main():
    for src in (BOTTOM_SOURCE, TOP_SOURCE, ASSEMBLY):
        if src.stat().st_size < 1000:
            raise RuntimeError("%s is an LFS pointer; set LKBX_CAD_DIR to real STEP files" % src)
    doc = App.newDocument("WristRestraints")
    build_params(doc)
    imp, interface = build_interface(doc)
    dock = build_dock(doc, interface)
    plate = build_plate(doc)
    refs = build_references(doc)
    doc.recompute()
    check(dock.Shape, plate.Shape, interface.Shape, imp.Shape, refs)
    doc.saveAs(str(DOC_OUT))
    dock.Shape.exportStep(str(DOCK_OUT))
    plate.Shape.exportStep(str(PLATE_OUT))
    export_stl(print_copy(dock.Shape), DOCK_STL)
    export_stl(print_copy(plate.Shape), PLATE_STL)
    for out in (DOC_OUT, DOCK_OUT, PLATE_OUT, DOCK_STL, PLATE_STL):
        print("wrote", out, out.stat().st_size)


main()
