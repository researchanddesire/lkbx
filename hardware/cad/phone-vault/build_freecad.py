"""Parametric FreeCAD model of the phone vault: a tall bottom housing.

The phone vault replaces the key tray of ``bottom-housing.step`` with a
honeycomb-windowed tower deep enough for a large phone (standing on end) or a
king-size cigarette pack. The top of the part is the original bottom housing, unchanged, so the
stock top housing slides on, latches and detects it exactly as before.

Run headless with FreeCAD 1.0:

    freecadcmd build_freecad.py

The Onshape exports in ``hardware/cad/`` are Git LFS files. If they are not
checked out, point ``LKBX_CAD_DIR`` at a folder that holds real copies of
``bottom-housing.step``, ``top-housing.step`` and ``lockbox-assembly.step``.

Creates ``phone-vault.FCStd`` next to this script and writes, into
``hardware/cad/`` (gitignored, regenerate them from the FCStd):

    phone-vault.step        the vault in the housing frame (mates with top-housing.step)
    phone-vault-print.stl   the vault in print orientation, base on Z = 0
    phone-vault-escape-test-print.stl
                            the latch end only (shaft, rod and punch-out), to
                            test the print-in-place escape rod before the vault

Document contents:

    Params      spreadsheet of the tunable dimensions; features read them by expression
    BottomHousingImport / InterfaceCut / Interface
                the Onshape bottom housing, and the part of it above CUT_Z
                (rails, latch slot, magnets) that the vault keeps
    PhoneVault  PartDesign body on that interface: outer wall pad, base foot
                (chamfer loft plus pad), fills for the two features the old
                part opened through its underside, the top-entry back magnet
                pocket, the opening, the 45 degree transition loft, the cavity,
                the side and end honeycomb windows, and the emergency bypass:
                a shaft down the latch end with a rod printed in place inside
                it, breakaway tabs, the base punch-out and its marking
    EscapeTestPiece
                the latch end of the vault, cut out for a test print
    References  phone and cigarette pack envelopes in the cavity

The emergency bypass keeps the escape of the original bottom housing. The
original has a plug in the floor under the latch, cut free by 0.3 mm slits and
held only by the first printed layer: pushing it in lifts the latch on the
side away from its pivot, so it swings out of the slot. Here the plug is in
the base, and a rod printed in place in a sealed shaft carries the push 165 mm
up to the latch. The rod clears its shaft by ESC_CLEAR on every side and is
held centred by one-layer breakaway tabs, so it prints standing and does not
rattle; the push snaps the tabs and the skin together.

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
DOC_OUT = HERE / "phone-vault.FCStd"
VAULT_OUT = OUT_DIR / "phone-vault.step"
VAULT_STL = OUT_DIR / "phone-vault-print.stl"
TEST_STL = OUT_DIR / "phone-vault-escape-test-print.stl"
STL_LINEAR_DEFLECTION = 0.01
STL_ANGULAR_DEFLECTION = 0.1

V = App.Vector

# --------------------------------------------------------------------------
# Parameters. Every row becomes a spreadsheet cell with an alias of the same
# name; features reference them by expression so they can be tuned in FreeCAD.
# The window layout (slot count) is generated from the same values by this
# script; rerun it after changing HEX_W, HEX_SIDE, WEB or the window limits.
# --------------------------------------------------------------------------
PARAMS = [
    ("TOP_FACE_Z", 28.70, "Floor of the slide channel, where the old key tray started"),
    ("CUT_Z", 41.00, "The original part is kept above this plane (latch slot floor)"),
    ("OUTER_X", 30.00, "Half width of the housing outline"),
    ("OUTER_Y", 60.50, "Half length of the housing outline"),
    ("OUTER_R", 5.00, "Corner radius of the housing outline"),
    ("WALL", 4.00, "Vault wall thickness below the transition"),
    ("OPEN_X", 24.00, "Half width of the opening through the interface"),
    ("OPEN_Y0", -42.50, "Latch end of the opening (5 mm clear of the latch slot)"),
    ("OPEN_Y1", 47.00, "Magnet end of the opening (2.7 mm clear of the back magnet)"),
    ("OPEN_R", 3.00, "Opening corner radius"),
    ("CAVITY_DEPTH", 170.00, "Usable depth from TOP_FACE_Z to the cavity floor"),
    ("FLOOR_T", 3.00, "Base thickness under the cavity"),
    ("FOOT_W", 12.00, "How far the base foot reaches out past the walls"),
    ("FOOT_EDGE", 3.00, "Vertical edge of the base foot"),
    ("TRANSITION_EXTRA", 2.00, "Extra height that keeps the transition corners under 45 degrees"),
    ("WIN_TOP_Z", 58.00, "No honeycomb cell reaches above this (below the transition)"),
    ("WIN_BOT_Z", 182.00, "No honeycomb cell reaches below this (above the foot chamfer)"),
    ("HEX_W", 7.00, "Honeycomb cell width across its vertical sides (a cigarette is 7.8 mm)"),
    ("HEX_SIDE", 3.00, "Length of a cell's vertical sides"),
    ("WEB", 3.50, "Web thickness between cells, vertical and slanted alike"),
    ("ROOF_ANGLE", "=60", "Cell roof and floor angle from horizontal, degrees (45 is the limit)"),
    ("MAG_X", -20.70, "Back magnet centre, from bottom-housing.step"),
    ("MAG_Y", 51.85, "Back magnet centre, from bottom-housing.step"),
    ("MAG_R", 2.175, "Back magnet pocket radius, the same as the front magnet pocket"),
    ("MAG_FLOOR_Z", 34.75, "Back magnet pocket floor, the same depth as the front magnet pocket"),
    ("ESC_X", -4.50, "Escape rod centre across the width: under the latch, right of its pivot (X -15.25)"),
    ("ESC_TIP_W", 6.00, "Rod tip width (X) where it passes the slot floor and pushes the latch"),
    ("ESC_TIP_T", 5.00, "Rod tip thickness (Y); the latch slot is 7 mm"),
    ("ESC_TIP_Y", -51.00, "Rod tip centre along the length, the middle of the latch slot"),
    ("ESC_TIP_TOP_Z", 33.50, "Rod top at rest, 1.2 mm under the latch (its tip is at Z 32.3)"),
    ("ESC_BODY_W", 10.00, "Rod body width (X) below the slot floor, stiff enough to print standing"),
    ("ESC_BODY_T", 8.00, "Rod body thickness (Y)"),
    ("ESC_BODY_TOP_Z", 54.00, "Top of the rod body; a 45 degree shoulder narrows it to the tip and stops it on the shaft ceiling"),
    ("ESC_HOLE_Z", 43.00, "Bottom of the tip hole under the slot floor; a 45 degree ceiling widens it to the shaft"),
    ("ESC_CLEAR", 1.00, "Gap between the rod and its shaft on every side"),
    ("ESC_WALL", 1.60, "Shaft wall thickness inside the cavity"),
    ("ESC_SLIT", 0.30, "Slit around the punch-out in the base, as on the original plug"),
    ("ESC_SKIN", 0.20, "First-layer skin that holds the punch-out, as on the original plug"),
    ("ESC_TIE_W", 0.50, "Breakaway tab width; the tabs hold the rod centred so it prints straight and does not rattle"),
    ("ESC_TIE_H", 0.40, "Breakaway tab height (two 0.2 mm layers)"),
    ("ESC_TRAVEL", 9.00, "Rod travel the build checks is free and has turned the latch out of its slot; the shoulder stops the rod within 2 mm more"),
    ("MARK_DEPTH", 0.60, "Depth of the emergency bypass marking in the base"),
    ("MARK_GAP", 2.00, "Gap between the punch-out slit and the marking frame"),
    ("MARK_LINE", 0.80, "Width of the marking frame groove"),
    ("INNER_X", "=OUTER_X - WALL", "Half width of the cavity"),
    ("INNER_Y", "=OUTER_Y - WALL", "Half length of the cavity"),
    ("INNER_R", "=OUTER_R - WALL", "Cavity corner radius, concentric with the outline"),
    ("TRANSITION_Z", "=CUT_Z + INNER_Y + OPEN_Y0 + TRANSITION_EXTRA", "Bottom of the opening-to-cavity transition (about 41 degrees at the latch end)"),
    ("FLOOR_Z", "=TOP_FACE_Z + CAVITY_DEPTH", "Cavity floor"),
    ("BASE_Z", "=FLOOR_Z + FLOOR_T", "Underside of the base, on the print bed"),
    ("FOOT_TOP_Z", "=BASE_Z - FOOT_EDGE - FOOT_W", "Where the 45 degree foot chamfer leaves the wall"),
    ("ROOF_RISE", "=HEX_W / 2 * tan(ROOF_ANGLE)", "Height of a cell's pointed roof"),
    ("ESC_BODY_Y", "=-INNER_Y + ESC_CLEAR + ESC_BODY_T / 2", "Rod body centre: its back face is ESC_CLEAR off the end wall"),
    ("ESC_SHOULDER", "=(ESC_BODY_W - ESC_TIP_W) / 2", "Height of the 45 degree rod shoulder and shaft ceiling"),
]
SIDE_U_LIMIT = 50.0  # cells stay within +-this along Y on the long walls (flat wall ends at 55.5)
END_U_LIMIT = 21.0  # cells stay within +-this along X on the end walls (flat wall ends at 25)
P = {}
for _name, _value, _note in PARAMS:
    if _name == "ROOF_RISE":
        continue  # needs tan() in degrees, computed below
    if isinstance(_value, str):
        _expr = _value[1:]
        for _k in sorted(P, key=len, reverse=True):
            _expr = _expr.replace(_k, repr(P[_k]))
        P[_name] = float(eval(_expr))  # noqa: S307 - formulas above are our own
    else:
        P[_name] = _value
P["ROOF_RISE"] = P["HEX_W"] / 2 * math.tan(math.radians(P["ROOF_ANGLE"]))

# Features of the original part that opened through its underside; below
# CUT_Z the vault closes them, so they are filled instead of left as voids.
OLD_MAG_HOLE = dict(r=2.40, z0=30.70)  # back magnet bore, pressed from below
OLD_STAND_SLOT = dict(x=(-4.4, 4.4), y=(-47.2, -44.1), z0=36.40)  # stand clip slot

# Breakaway tab levels (Z). The first holds the tip in the slot floor, the
# rest hold the body at about 35 mm spacing while it prints.
TIE_LEVELS = (42.0, 75.0, 110.0, 145.0, 180.0)
# The latch turns about the N20 shaft (an axis along Y through this X, Z). It
# swings toward +X to unlock (checked to 90 degrees) and stops on the slot
# wall 8.5 degrees toward -X. It is free once wholly above the slot top.
LATCH_PIVOT = (-15.25, 8.8)
LATCH_SWING = (-8.5, 90.0)
SLOT_TOP_Z = 28.7
# Marking on the base, read from below. Each line: text, cap height, baseline Y.
MARK_FONT = Path("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf")
MARK_LINES = (("EMERGENCY BYPASS", 4.2, -30.6), ("PUSH IN", 4.2, -37.2))
# Box that cuts the latch end out of the vault for the escape test print.
TEST_BOX = dict(x=(-17.0, 9.0), y=(-73.0, -38.0), z=(33.0, None))

# Contents the cavity must take (length along Z, width along Y, thickness along X).
PHONE = (170.0, 82.0, 14.0)  # 165 x 78 x 9 phone in a case
CIG_PACK = (88.0, 55.0, 22.0)  # king-size pack standing on end

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


def rect(sk, x, y, exprs=None, prefix=""):
    """Rectangle, fully constrained by its side positions (expressions optional)."""
    exprs = exprs or {}
    (x0, x1), (y0, y1) = x, y
    pts = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    g = [sk.addGeometry(Part.LineSegment(V(pts[i][0], pts[i][1], 0), V(pts[(i + 1) % 4][0], pts[(i + 1) % 4][1], 0)),
                        False) for i in range(4)]
    for i in range(4):
        sk.addConstraint(Sketcher.Constraint("Coincident", g[i], 2, g[(i + 1) % 4], 1))
    sk.addConstraint(Sketcher.Constraint("Horizontal", g[0]))
    sk.addConstraint(Sketcher.Constraint("Horizontal", g[2]))
    sk.addConstraint(Sketcher.Constraint("Vertical", g[1]))
    sk.addConstraint(Sketcher.Constraint("Vertical", g[3]))
    for key, kind, geo, pos, value in (("x0", "DistanceX", g[0], 1, x0), ("x1", "DistanceX", g[0], 2, x1),
                                       ("y0", "DistanceY", g[0], 1, y0), ("y1", "DistanceY", g[2], 1, y1)):
        distance_constraint(sk, kind, geo, pos, value, prefix + key.upper() if exprs.get(key) else None,
                            exprs.get(key))
    return g


def text_wires(text, cap, cx, baseline):
    """Outline wires of one line of text in the XY plane, centred on cx.

    The first glyph must be a capital: its height sets the scale.
    """
    font = (str(MARK_FONT.parent) + "/", MARK_FONT.name)
    probe = Part.makeWireString(text, *font, 10.0, 0.0)
    size = 10.0 * cap / Part.Compound(probe[0]).BoundBox.YLength
    chars = Part.makeWireString(text, *font, size, 0.0)
    wires = [w for ch in chars for w in ch]
    bb = Part.Compound(wires).BoundBox
    m = App.Matrix()
    m.move(V(cx - (bb.XMin + bb.XLength / 2), baseline - Part.Compound(chars[0]).BoundBox.YMin, 0))
    return [w.transformGeometry(m) for w in wires]


def add_blocked_wires(sk, wires):
    """Copy wires into a sketch as blocked geometry (fully constrained)."""
    for w in wires:
        for e in w.Edges:
            if isinstance(e.Curve, Part.Line):
                geo = Part.LineSegment(e.Vertexes[0].Point, e.Vertexes[-1].Point)
            else:
                geo = e.Curve.toBSpline(*e.ParameterRange)
            gi = sk.addGeometry(geo, False)
            sk.addConstraint(Sketcher.Constraint("Block", gi))


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


def honeycomb_cells(u_limit):
    """Cell outlines (u, z) for one wall: rows alternate by half a pitch."""
    a = P["HEX_W"] / 2
    half_side = P["HEX_SIDE"] / 2
    rise = P["ROOF_RISE"]
    web = P["WEB"]
    pitch = P["HEX_W"] + web
    # Row spacing that makes the slanted webs exactly WEB thick.
    slant = math.hypot(a, rise)
    row = (web * slant - (pitch / 2 - a) * rise) / a + 2 * half_side + rise
    half_h = half_side + rise
    cells = []
    z = P["WIN_TOP_Z"] + half_h
    k = 0
    while z + half_h <= P["WIN_BOT_Z"] + 1e-9:
        offset = 0.0 if k % 2 == 0 else pitch / 2
        j = -int(u_limit // pitch) - 1
        while True:
            u = j * pitch + offset
            if u - a > u_limit:
                break
            if abs(u) + a <= u_limit + 1e-9:
                cells.append([(u - a, z - half_side), (u, z - half_h), (u + a, z - half_side),
                              (u + a, z + half_side), (u, z + half_h), (u - a, z + half_side)])
            j += 1
        z += row
        k += 1
    return cells


def build_vault(doc, interface):
    print("vault")
    body = doc.addObject("PartDesign::Body", "PhoneVault")
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

    # Base foot: a 45 degree chamfer loft from the wall out to FOOT_W, then a
    # vertical edge down to the bed. Upward facing only, so it prints clean.
    sk_a = sketch(body, "FootChamferTop", "XY_Plane", P["FOOT_TOP_Z"], "Params.FOOT_TOP_Z")
    rounded_rect(sk_a, (-P["OUTER_X"], P["OUTER_X"]), (-P["OUTER_Y"], P["OUTER_Y"]), P["OUTER_R"],
                 outline_exprs("A"), "Wall")
    finish_sketch(sk_a)
    fw = P["FOOT_W"]
    sk_b = sketch(body, "FootChamferBottom", "XY_Plane", P["BASE_Z"] - P["FOOT_EDGE"],
                  "Params.BASE_Z - Params.FOOT_EDGE")
    rounded_rect(sk_b, (-P["OUTER_X"] - fw, P["OUTER_X"] + fw), (-P["OUTER_Y"] - fw, P["OUTER_Y"] + fw),
                 P["OUTER_R"] + fw, outline_exprs("B", "Params.FOOT_W"), "Foot")
    finish_sketch(sk_b)
    chamfer = loft(body, "Additive", "FootChamfer", sk_a, sk_b)
    recompute(doc, sk_a, sk_b, chamfer)
    sk_c = sketch(body, "FootEdgeSketch", "XY_Plane", P["BASE_Z"] - P["FOOT_EDGE"],
                  "Params.BASE_Z - Params.FOOT_EDGE")
    rounded_rect(sk_c, (-P["OUTER_X"] - fw, P["OUTER_X"] + fw), (-P["OUTER_Y"] - fw, P["OUTER_Y"] + fw),
                 P["OUTER_R"] + fw, outline_exprs("C", "Params.FOOT_W"), "Foot")
    finish_sketch(sk_c)
    foot = pad(body, sk_c, "FootEdge", "Params.FOOT_EDGE")
    recompute(doc, sk_c, foot)
    report("Foot", body.Shape)

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

    # Honeycomb windows: hexagonal cells with vertical sides and pointed
    # roofs at ROOF_ANGLE from horizontal, so nothing overhangs in print.
    # Every web, vertical or slanted, is WEB thick, so the wall is a rigid
    # lattice instead of long bars that snap by hand.
    feats = []
    for name, plane, limit in (("SideHoneycomb", "YZ_Plane", SIDE_U_LIMIT),
                               ("EndHoneycomb", "XZ_Plane", END_U_LIMIT)):
        sk = sketch(body, name + "Sketch", plane)
        for pts in honeycomb_cells(limit):
            # Sketch v is global Z on both planes; the layout is symmetric
            # in u, so the sign of the plane's u axis does not matter.
            polygon(sk, pts)
        finish_sketch(sk)
        f = pocket(body, sk, name, through_symmetric=True)
        recompute(doc, sk, f)
        feats.append(f)
    report("Windows", body.Shape)
    build_escape(doc, body)
    return body


def esc_rects():
    """Numeric outlines of the escape features, with their expressions."""
    def r(xc, yc, w, t, grow, exprs):
        return ((xc - w / 2 - grow, xc + w / 2 + grow), (yc - t / 2 - grow, yc + t / 2 + grow)), exprs

    def ex(xc, yc, w, t, grow):
        g = (" + " + grow) if grow else ""
        m = (" - " + grow) if grow else ""
        return {"x0": "%s - %s / 2%s" % (xc, w, m), "x1": "%s + %s / 2%s" % (xc, w, g),
                "y0": "%s - %s / 2%s" % (yc, t, m), "y1": "%s + %s / 2%s" % (yc, t, g)}
    X, TY, BY = "Params.ESC_X", "Params.ESC_TIP_Y", "Params.ESC_BODY_Y"
    TW, TT, BW, BT = "Params.ESC_TIP_W", "Params.ESC_TIP_T", "Params.ESC_BODY_W", "Params.ESC_BODY_T"
    c, w, sl = P["ESC_CLEAR"], P["ESC_WALL"], P["ESC_SLIT"]
    tip = (P["ESC_X"], P["ESC_TIP_Y"], P["ESC_TIP_W"], P["ESC_TIP_T"])
    body = (P["ESC_X"], P["ESC_BODY_Y"], P["ESC_BODY_W"], P["ESC_BODY_T"])
    return {
        "tip": r(*tip, 0, ex(X, TY, TW, TT, "")),
        "tip_hole": r(*tip, c, ex(X, TY, TW, TT, "Params.ESC_CLEAR")),
        "body": r(*body, 0, ex(X, BY, BW, BT, "")),
        "shaft": r(*body, c, ex(X, BY, BW, BT, "Params.ESC_CLEAR")),
        "slit": r(*body, sl, ex(X, BY, BW, BT, "Params.ESC_SLIT")),
        "frame_in": r(*body, sl + P["MARK_GAP"], ex(X, BY, BW, BT, "(Params.ESC_SLIT + Params.MARK_GAP)")),
        "frame_out": r(*body, sl + P["MARK_GAP"] + P["MARK_LINE"],
                       ex(X, BY, BW, BT, "(Params.ESC_SLIT + Params.MARK_GAP + Params.MARK_LINE)")),
        "tube": r(*body, c + w, ex(X, BY, BW, BT, "(Params.ESC_CLEAR + Params.ESC_WALL)")),
    }


def tie_boxes():
    """Breakaway tabs: (x range, y range, z range) for each, left and right of the rod."""
    out = []
    c, tw, th = P["ESC_CLEAR"], P["ESC_TIE_W"], P["ESC_TIE_H"]
    for zt in TIE_LEVELS:
        if zt < P["ESC_HOLE_Z"]:
            w, yc = P["ESC_TIP_W"], P["ESC_TIP_Y"]
        else:
            w, yc = P["ESC_BODY_W"], P["ESC_BODY_Y"]
        for side in (-1, 1):
            edge = P["ESC_X"] + side * w / 2
            xs = sorted((edge - side * 0.2, edge + side * (c + 0.2)))
            out.append((tuple(xs), (yc - tw / 2, yc + tw / 2), (zt - th, zt)))
    return out


def build_escape(doc, body):
    """Emergency bypass: shaft, print-in-place rod, tabs, punch-out, marking."""
    R = esc_rects()

    def rect_sketch(name, z, z_expr, keys, extra=None):
        sk = sketch(body, name, "XY_Plane", z, z_expr)
        for k in keys:
            (xr, yr), exprs = R[k]
            rect(sk, xr, yr, exprs, k.capitalize() + "_")
        if extra:
            extra(sk)
        finish_sketch(sk)
        return sk

    # Shaft walls: a block from just under the slot floor to the cavity floor,
    # against the end wall. Inside the ramp it merges with solid material.
    (txr, tyr), texprs = R["tube"]
    sk = sketch(body, "EscapeTubeSketch", "XY_Plane", P["CUT_Z"] + 0.5, "Params.CUT_Z + 0.5 mm")
    texprs = dict(texprs, y0="-Params.INNER_Y")
    rect(sk, txr, (-P["INNER_Y"], tyr[1]), texprs, "Tube_")
    finish_sketch(sk)
    tube = pad(body, sk, "EscapeTube", "Params.FLOOR_Z - Params.CUT_Z - 0.5 mm")
    recompute(doc, sk, tube)

    # Tip hole through the slot floor, a 45 degree ceiling, then the shaft.
    sk = rect_sketch("EscapeTipHoleSketch", P["CUT_Z"] - 1.0, "Params.CUT_Z - 1 mm", ["tip_hole"])
    f = pocket(body, sk, "EscapeTipHole", "Params.ESC_HOLE_Z - Params.CUT_Z + 1 mm", reversed_=True)
    recompute(doc, sk, f)
    sk_a = rect_sketch("EscapeCeilingTop", P["ESC_HOLE_Z"], "Params.ESC_HOLE_Z", ["tip_hole"])
    sk_b = rect_sketch("EscapeCeilingBottom", P["ESC_HOLE_Z"] + P["ESC_SHOULDER"],
                       "Params.ESC_HOLE_Z + Params.ESC_SHOULDER", ["shaft"])
    f = loft(body, "Subtractive", "EscapeCeiling", sk_a, sk_b)
    recompute(doc, sk_a, sk_b, f)
    sk = rect_sketch("EscapeShaftSketch", P["ESC_HOLE_Z"] + P["ESC_SHOULDER"],
                     "Params.ESC_HOLE_Z + Params.ESC_SHOULDER", ["shaft"])
    f = pocket(body, sk, "EscapeShaft", "Params.FLOOR_Z - Params.ESC_HOLE_Z - Params.ESC_SHOULDER", reversed_=True)
    recompute(doc, sk, f)
    report("Escape shaft", body.Shape)

    # The rod, printed in place: body standing on the floor, 45 degree
    # shoulder, then the tip up through the slot floor to just under the latch.
    sk = rect_sketch("EscapeRodBodySketch", P["FLOOR_Z"], "Params.FLOOR_Z", ["body"])
    f = pad(body, sk, "EscapeRodBody", "Params.FLOOR_Z - Params.ESC_BODY_TOP_Z", reversed_=True)
    recompute(doc, sk, f)
    sk_a = rect_sketch("EscapeShoulderBottom", P["ESC_BODY_TOP_Z"], "Params.ESC_BODY_TOP_Z", ["body"])
    sk_b = rect_sketch("EscapeShoulderTop", P["ESC_BODY_TOP_Z"] - P["ESC_SHOULDER"],
                       "Params.ESC_BODY_TOP_Z - Params.ESC_SHOULDER", ["tip"])
    f = loft(body, "Additive", "EscapeRodShoulder", sk_a, sk_b)
    recompute(doc, sk_a, sk_b, f)
    sk = rect_sketch("EscapeRodTipSketch", P["ESC_BODY_TOP_Z"] - P["ESC_SHOULDER"],
                     "Params.ESC_BODY_TOP_Z - Params.ESC_SHOULDER", ["tip"])
    f = pad(body, sk, "EscapeRodTip", "Params.ESC_BODY_TOP_Z - Params.ESC_SHOULDER - Params.ESC_TIP_TOP_Z",
            reversed_=True)
    recompute(doc, sk, f)

    # Punch-out: the rod's foot is cut free in the base by a slit all round
    # and held only by the first layer, like the original plug.
    sk = rect_sketch("EscapePunchOutSketch", P["FLOOR_Z"], "Params.FLOOR_Z", ["slit", "body"])
    f = pocket(body, sk, "EscapePunchOut", "Params.FLOOR_T - Params.ESC_SKIN", reversed_=True)
    recompute(doc, sk, f)

    # Breakaway tabs, two per level, bridging the gap to the shaft walls.
    for i, zt in enumerate(TIE_LEVELS):
        sk = sketch(body, "EscapeTabs%dSketch" % (i + 1), "XY_Plane", zt)
        for xs, ys, _zs in [b for b in tie_boxes() if abs(b[2][1] - zt) < 1e-9]:
            rect(sk, xs, ys)
        finish_sketch(sk)
        f = pad(body, sk, "EscapeTabs%d" % (i + 1), "Params.ESC_TIE_H", reversed_=True)
        recompute(doc, sk, f)
    report("Escape rod", body.Shape)

    # Marking on the base: a frame round the punch-out, an arrow and two
    # lines of text, sunk MARK_DEPTH into the bed face.
    (fx, fy), _ = R["frame_out"]
    apex, base = fy[1] + 1.0, fy[1] + 5.0

    def arrow(sk):
        polygon(sk, [(P["ESC_X"], apex), (P["ESC_X"] + 3.0, base), (P["ESC_X"] - 3.0, base)])
    sk = rect_sketch("BypassMarkSketch", P["BASE_Z"], "Params.BASE_Z", ["frame_out", "frame_in"], arrow)
    f = pocket(body, sk, "BypassMark", "Params.MARK_DEPTH")
    recompute(doc, sk, f)
    sk = sketch(body, "BypassTextSketch", "XY_Plane", P["BASE_Z"], "Params.BASE_Z")
    for text, cap, baseline in MARK_LINES:
        add_blocked_wires(sk, text_wires(text, cap, P["ESC_X"], baseline))
    finish_sketch(sk)
    f = pocket(body, sk, "BypassText", "Params.MARK_DEPTH")
    recompute(doc, sk, f)
    report("Bypass marking", body.Shape)


def rod_shape():
    """The moving part of the bypass (rod plus punch-out), for the checks."""
    R = esc_rects()
    (bx, by), _ = R["body"]
    (tx, ty), _ = R["tip"]
    foot = P["BASE_Z"] - P["ESC_SKIN"]
    body_box = Part.makeBox(bx[1] - bx[0], by[1] - by[0], foot - P["ESC_BODY_TOP_Z"], V(bx[0], by[0], P["ESC_BODY_TOP_Z"]))
    z_s = P["ESC_BODY_TOP_Z"] - P["ESC_SHOULDER"]
    w_a = Part.makePolygon([V(bx[0], by[0], P["ESC_BODY_TOP_Z"]), V(bx[1], by[0], P["ESC_BODY_TOP_Z"]),
                            V(bx[1], by[1], P["ESC_BODY_TOP_Z"]), V(bx[0], by[1], P["ESC_BODY_TOP_Z"]),
                            V(bx[0], by[0], P["ESC_BODY_TOP_Z"])])
    w_b = Part.makePolygon([V(tx[0], ty[0], z_s), V(tx[1], ty[0], z_s), V(tx[1], ty[1], z_s), V(tx[0], ty[1], z_s),
                            V(tx[0], ty[0], z_s)])
    shoulder = Part.makeLoft([w_a, w_b], True, True)
    tip_box = Part.makeBox(tx[1] - tx[0], ty[1] - ty[0], z_s - P["ESC_TIP_TOP_Z"], V(tx[0], ty[0], P["ESC_TIP_TOP_Z"]))
    return body_box.fuse([shoulder, tip_box]).removeSplitter()


def tie_solids():
    return [Part.makeBox(xs[1] - xs[0], ys[1] - ys[0], zs[1] - zs[0], V(xs[0], ys[0], zs[0]))
            for xs, ys, zs in tie_boxes()]


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
    """Phone and cigarette pack envelopes standing in the cavity."""
    grp = doc.addObject("App::DocumentObjectGroup", "References")
    floor = P["FLOOR_Z"]
    refs = []
    for name, (length, width, thick) in (("PhoneEnvelope", PHONE), ("CigarettePackEnvelope", CIG_PACK)):
        o = doc.addObject("Part::Feature", name)
        o.Shape = Part.makeBox(thick, width, length, V(-thick / 2, -width / 2 + 2.25, floor - length))
        o.Label = "%s %gx%gx%g mm" % (name, length, width, thick)
        grp.addObject(o)
        refs.append(o)
    return refs


def build_test_piece(doc, vault):
    """The latch end of the vault: shaft, rod, punch-out and end wall."""
    box = doc.addObject("Part::Box", "EscapeTestBox")
    (x0, x1), (y0, y1), (z0, _z1) = TEST_BOX["x"], TEST_BOX["y"], TEST_BOX["z"]
    box.Length, box.Width = x1 - x0, y1 - y0
    box.setExpression("Height", "Params.BASE_Z - %g mm" % z0)
    box.Placement.Base = V(x0, y0, z0)
    box.Visibility = False
    common = doc.addObject("Part::Common", "EscapeTestPiece")
    common.Label = "Escape test piece (latch end)"
    common.Base = vault
    common.Tool = box
    common.Visibility = False
    recompute(doc, common)
    return common


def check(vault_shape, interface_shape, bottom_shape, refs):
    print("checks")
    fails = []
    if len(vault_shape.Solids) != 1 or len(vault_shape.Shells) != 1 or not vault_shape.isValid():
        fails.append("vault is not one valid solid with one shell (sealed void?)")

    # Above the cut the vault must be the original part, minus the opening
    # and the new magnet pocket, plus the two fills; nothing else may change.
    above = Part.makeBox(100, 160, P["CUT_Z"] - 20, V(-50, -80, 20))
    orig_above = bottom_shape.common(above)
    removed = orig_above.cut(vault_shape)
    opening = Part.makeBox(2 * P["OPEN_X"], P["OPEN_Y1"] - P["OPEN_Y0"], 30,
                           V(-P["OPEN_X"], P["OPEN_Y0"], P["CUT_Z"] - 30))
    magnet = Part.makeCylinder(P["MAG_R"] + 0.01, 30, V(P["MAG_X"], P["MAG_Y"], P["MAG_FLOOR_Z"] - 30))
    unexplained = removed.cut(opening).cut(magnet).Volume
    added = vault_shape.common(above).cut(bottom_shape).Volume
    fills = (math.pi * OLD_MAG_HOLE["r"] ** 2 * (P["CUT_Z"] - OLD_MAG_HOLE["z0"])
             + (OLD_STAND_SLOT["x"][1] - OLD_STAND_SLOT["x"][0]) * (OLD_STAND_SLOT["y"][1] - OLD_STAND_SLOT["y"][0])
             * (P["CUT_Z"] - OLD_STAND_SLOT["z0"]))
    rod_tip = P["ESC_TIP_W"] * P["ESC_TIP_T"] * (P["CUT_Z"] - P["ESC_TIP_TOP_Z"])
    print("  above CUT_Z: removed outside opening/magnet %.3f mm^3, added %.1f mm^3 (fills %.1f + rod tip %.1f)" % (
        unexplained, added, fills, rod_tip))
    if unexplained > 0.05 or added > fills + rod_tip + 0.5:
        fails.append("the interface above CUT_Z changed")

    parts = assembly_parts()
    for label in ("Housing` Top", "Locking Latch", "PCB Cover"):
        shape = parts[label]
        new = shape.common(vault_shape).Volume
        old = shape.common(bottom_shape).Volume
        print("  overlap with %-14s vault %.3f  original %.3f" % (label, new, old))
        if new > old + 0.05:
            fails.append("%s overlaps the vault more than the original" % label)

    rod = rod_shape()
    gap = rod.distToShape(parts["Locking Latch"])[0]
    print("  escape rod to latch at rest %.2f mm" % gap)
    if not 0.8 < gap < 2.0:
        fails.append("escape rod is not just under the latch (%.2f mm)" % gap)
    # The rod tip must stay clear of the latch over its whole normal swing.
    pivot = V(LATCH_PIVOT[0], 0, LATCH_PIVOT[1])
    swing = []
    lo, hi = (int(round(a * 10)) for a in LATCH_SWING)
    for tenth in list(range(lo, hi, 25)) + [hi]:
        turned = parts["Locking Latch"].copy()
        turned.rotate(pivot, V(0, 1, 0), tenth / 10.0)
        swing.append((turned.distToShape(rod)[0], tenth / 10.0))
    closest, at = min(swing)
    print("  latch swing %g..%g deg: closest to the rod %.2f mm at %.1f deg" % (LATCH_SWING + (closest, at)))
    if closest < 0.3:
        fails.append("the latch swing comes within %.2f mm of the escape rod" % closest)
    # Pushed ESC_TRAVEL, the rod must have turned the latch out of its slot:
    # one degree short of the turn that lifts it clear, the latch would still
    # be inside the pushed rod.
    frees = None
    for a in range(0, int(LATCH_SWING[1]) + 1):
        turned = parts["Locking Latch"].copy()
        turned.rotate(pivot, V(0, 1, 0), a)
        if turned.BoundBox.ZMax <= SLOT_TOP_Z:
            frees = a
            break
    pushed = rod.copy()
    pushed.translate(V(0, 0, -P["ESC_TRAVEL"]))
    short = parts["Locking Latch"].copy()
    short.rotate(pivot, V(0, 1, 0), (frees or 1) - 1)
    lifted = frees is not None and short.common(pushed).Volume > 0.01
    print("  latch clears its slot at %s deg; rod pushed %.1f mm %s it" % (
        frees, P["ESC_TRAVEL"], "turns" if lifted else "does NOT turn"))
    if not lifted:
        fails.append("the escape rod does not lift the latch out of its slot")
    ties = tie_solids()
    static = vault_shape.cut(rod).cut(ties)
    upper = rod.common(Part.makeBox(100, 160, P["FLOOR_Z"] - 31, V(-50, -80, 30)))  # above the floor slit
    clear = upper.distToShape(static)[0]
    print("  escape rod clearance to its shaft (tabs aside) %.2f mm" % clear)
    if clear < P["ESC_CLEAR"] - 0.02:
        fails.append("escape rod clearance %.2f mm" % clear)
    moved = rod.copy()
    moved.translate(V(0, 0, -P["ESC_TRAVEL"]))
    hit = moved.common(static).Volume
    print("  escape rod pushed %.1f mm: collides %.3f mm^3" % (P["ESC_TRAVEL"], hit))
    if hit > 0.01:
        fails.append("escape rod cannot travel %.1f mm" % P["ESC_TRAVEL"])
    # ...and the shoulder stops it on the shaft ceiling soon after, so it
    # cannot be driven up out of the slot.
    stop = None
    near = static.common(Part.makeBox(40, 30, 40, V(-24, -66, 30)))  # the shaft top only, for speed
    for tenth in range(int(P["ESC_TRAVEL"] * 10), int(P["ESC_TRAVEL"] * 10) + 21):
        moved = rod.copy()
        moved.translate(V(0, 0, -tenth / 10.0))
        if moved.common(near).Volume > 0.01:
            stop = tenth / 10.0
            break
    print("  escape rod stops after %s mm" % ("more than %.1f" % (P["ESC_TRAVEL"] + 2) if stop is None else "%.1f" % stop))
    if stop is None:
        fails.append("nothing stops the escape rod within %.1f mm" % (P["ESC_TRAVEL"] + 2))
    tie_vol = sum(t.common(vault_shape).Volume for t in ties)
    print("  breakaway tabs: %d, %.2f mm^3" % (len(ties), tie_vol))

    for r in refs:
        hit = r.Shape.common(vault_shape).Volume
        print("  %-24s overlap %.3f" % (r.Name, hit))
        if hit > 1e-6:
            fails.append("%s does not fit" % r.Name)
    opening = (2 * P["OPEN_X"], P["OPEN_Y1"] - P["OPEN_Y0"])
    for name, (length, width, thick) in (("phone", PHONE), ("pack", CIG_PACK)):
        if not (thick < opening[0] and width < opening[1]):
            fails.append("%s does not pass the %gx%g opening" % (name, *opening))

    bb = vault_shape.BoundBox
    print("  height %.2f mm, footprint %.1f x %.1f mm" % (bb.ZLength, bb.XLength, bb.YLength))

    # Overhangs in print orientation (bed = BASE_Z, up = -Z): a facet below
    # the cut whose normal points more than 45 degrees toward the bed.
    # Two kinds of short bridge are expected and reported apart: the
    # breakaway tabs (1 mm spans) and the marking sunk into the bed face.
    mesh = MeshPart.meshFromShape(Shape=vault_shape, LinearDeflection=0.05, AngularDeflection=0.2, Relative=False)
    bad = tabs = mark = 0.0
    bad_at = []
    boxes = tie_boxes()
    for f in mesh.Facets:
        nz_print = -f.Normal.z
        xc, yc, zc = (sum(p[i] for p in f.Points) / 3 for i in range(3))
        if zc > P["CUT_Z"] + 0.01 and zc < P["BASE_Z"] - 0.01 and nz_print < -math.cos(math.radians(45)) - 1e-3:
            if zc > P["BASE_Z"] - P["MARK_DEPTH"] - 0.02:
                mark += f.Area
            elif any(xs[0] - 0.05 <= xc <= xs[1] + 0.05 and ys[0] - 0.05 <= yc <= ys[1] + 0.05
                     and zs[0] - 0.05 <= zc <= zs[1] + 0.05 for xs, ys, zs in boxes):
                tabs += f.Area
            else:
                bad += f.Area
                bad_at.append((round(f.Points[0][0]), round(f.Points[0][1]), round(zc, 1), round(nz_print, 2)))
    print("  bridges: breakaway tabs %.2f mm^2, bed-face marking %.2f mm^2" % (tabs, mark))
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
    doc = App.newDocument("PhoneVault")
    build_params(doc)
    imp, interface = build_interface(doc)
    vault = build_vault(doc, interface)
    refs = build_references(doc)
    test = build_test_piece(doc, vault)
    doc.recompute()
    check(vault.Shape, interface.Shape, imp.Shape, refs)
    if len(test.Shape.Solids) != 1:
        raise RuntimeError("escape test piece is not one solid")
    report("EscapeTestPiece", test.Shape)
    doc.saveAs(str(DOC_OUT))
    vault.Shape.exportStep(str(VAULT_OUT))
    # Print orientation: base on the bed. Rotate 180 degrees about X so the
    # vault keeps its handedness, then drop the base onto Z = 0.
    for shape, path in ((vault.Shape, VAULT_STL), (test.Shape, TEST_STL)):
        printed = shape.copy()
        printed.rotate(V(0, 0, 0), V(1, 0, 0), 180)
        printed.translate(V(0, 0, -printed.BoundBox.ZMin))
        export_stl(printed, path)
    for out in (DOC_OUT, VAULT_OUT, VAULT_STL, TEST_STL):
        print("wrote", out, out.stat().st_size)


main()
