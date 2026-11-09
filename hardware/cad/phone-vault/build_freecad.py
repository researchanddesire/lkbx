"""Parametric FreeCAD model of the phone vault: a tall bottom housing.

The phone vault replaces the key tray of ``bottom-housing.step`` with a
honeycomb-windowed tower deep enough for a large phone (standing on end) or a
king-size cigarette pack. The top of the part is the original bottom housing,
unchanged apart from the emergency key slot, so the stock top housing slides
on, latches and detects it exactly as before.

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
                            the top of the vault only (lid interface, latch
                            slot, key slot and marking), to test the escape
                            with the real lid, latch and motor before the vault

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
                a key slot through the latch-end wall under a punch-through
                skin, with its marking sunk into the end face
    EscapeTestPiece
                the vault down to TRANSITION_Z, for a test print
    References  phone and cigarette pack envelopes in the cavity

The emergency bypass replaces the escape of the original bottom housing,
whose plug in the floor under the latch the vault covers. A 2.5 mm hex key
goes in through a slot in the latch-end wall, just under the lid's end wall,
at the latch's free side. Pushed along the slot, it bears sideways on the
latch's lower corner, far below the pivot, and turns it out of the slot;
the lid then slides off, its latch riding over the far slot wall on its
chamfered edge. The build simulates that push with friction (in the same
simulation a push straight up from under the latch, as a rod up through the
vault would give, jams) and checks the slide-off in 3D.
The slot is closed by a one-line skin that the key punches through, so
using the bypass leaves a mark.

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
import numpy as np
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
    ("KEY_X0", -23.00, "Key slot end under the latch's free side, level with the latch slot wall (the right end seen from outside)"),
    ("KEY_X1", -1.50, "Far end of the key slot; it stops the key past its last contact with the latch"),
    ("KEY_TOP_Z", 28.50, "Key slot top, level with the bottom of the lid's end wall"),
    ("KEY_BOT_Z", 31.50, "Key slot floor along its run: a 2.5 mm hex key plus 0.5 mm"),
    ("KEY_ENTRY_X", -19.60, "End of the entry pocket, where a 45 degree ramp lifts the key onto the slot floor"),
    ("KEY_ENTRY_Z", 32.40, "Entry pocket floor: a key resting on it passes under the latch's corner"),
    ("KEY_SKIN", 0.45, "Outer skin over the key slot (one extrusion line); the key punches through it"),
    ("LATCH_SLOT_Y0", -54.50, "End-wall face of the latch slot, from bottom-housing.step"),
    ("KEY_REACH", 0.75, "The key slot runs this far into the latch slot, squaring off the slot's 0.7 mm corner round at the key"),
    ("MARK_DEPTH", 0.60, "Depth of the emergency bypass marking in the end face"),
    ("INNER_X", "=OUTER_X - WALL", "Half width of the cavity"),
    ("INNER_Y", "=OUTER_Y - WALL", "Half length of the cavity"),
    ("INNER_R", "=OUTER_R - WALL", "Cavity corner radius, concentric with the outline"),
    ("TRANSITION_Z", "=CUT_Z + INNER_Y + OPEN_Y0 + TRANSITION_EXTRA", "Bottom of the opening-to-cavity transition (about 41 degrees at the latch end)"),
    ("FLOOR_Z", "=TOP_FACE_Z + CAVITY_DEPTH", "Cavity floor"),
    ("BASE_Z", "=FLOOR_Z + FLOOR_T", "Underside of the base, on the print bed"),
    ("FOOT_TOP_Z", "=BASE_Z - FOOT_EDGE - FOOT_W", "Where the 45 degree foot chamfer leaves the wall"),
    ("ROOF_RISE", "=HEX_W / 2 * tan(ROOF_ANGLE)", "Height of a cell's pointed roof"),
    ("KEY_RAMP_X", "=KEY_ENTRY_X + KEY_ENTRY_Z - KEY_BOT_Z", "Top of the entry ramp (45 degrees)"),
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

# The latch turns about the N20 shaft, an axis along Y. The build fits the
# axis to the shaft bore in this section of the latch, where its profile is
# uniform; positive angles turn its tip toward +X, out of the slot.
LATCH_SECTION_Y = -51.0
# Key checks. A 2.5 mm hex key is modelled as a round bar: 1.25 mm radius
# with a flat facing up, 1.45 mm with a corner up. PLA on PLA slides at a
# friction coefficient of about 0.3 to 0.5; a push only turns the latch while
# the coefficient stays under the critical value the check reports.
KEY_RADII = (1.25, 1.45)
KEY_CLEAR = 0.10  # key to the slot end at the start of the push
FRICTION_MARGIN = 0.60  # above the top of that range
# Sliding the lid off, the latch's 1 mm chamfered edge may ride over the far
# slot wall; anything deeper than this would meet its vertical face instead.
RIDE_DEPTH_HELD, RIDE_DEPTH_RESTING = 0.40, 0.70
# Marking on the end face, read from outside the latch end with the box
# standing on its base. Each line: text, cap height, baseline Z.
MARK_FONT = Path("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf")
MARK_LINES = (("EMERGENCY", 4.0, 43.0), ("BYPASS", 4.0, 48.6), ("2.5 mm HEX KEY", 3.2, 54.0))
# The entry pointer under the key slot's entry and the push arrow beside it:
# Z of the pointer apex and base, Z of the arrow's centre line, and its ends.
MARK_POINTER_Z = (33.4, 36.2)
MARK_ARROW_Z = 34.8
MARK_ARROW_X = (-18.6, -2.6)

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


def polygon(sk, pts, exprs=None, prefix=""):
    """Closed polygon of lines, fully constrained by its vertex coordinates.

    ``exprs`` may give, per vertex, a pair of expressions for its sketch
    coordinates (either may be None); those constraints are named
    ``<prefix>P<i>U`` and ``<prefix>P<i>V``.
    """
    n = len(pts)
    exprs = exprs or [(None, None)] * n
    geo = [sk.addGeometry(Part.LineSegment(V(pts[i][0], pts[i][1], 0),
                                           V(pts[(i + 1) % n][0], pts[(i + 1) % n][1], 0)), False)
           for i in range(n)]
    for i in range(n):
        sk.addConstraint(Sketcher.Constraint("Coincident", geo[i], 2, geo[(i + 1) % n], 1))
    for i in range(n):
        x, y = pts[i]
        ex, ey = exprs[i]
        if abs(x) < 1e-9:
            sk.addConstraint(Sketcher.Constraint("PointOnObject", geo[i], 1, -2))
        else:
            distance_constraint(sk, "DistanceX", geo[i], 1, x, "%sP%dU" % (prefix, i) if ex else None, ex)
        if abs(y) < 1e-9:
            sk.addConstraint(Sketcher.Constraint("PointOnObject", geo[i], 1, -1))
        else:
            distance_constraint(sk, "DistanceY", geo[i], 1, y, "%sP%dV" % (prefix, i) if ey else None, ey)
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


def sketch(body, name, plane_role, offset=0.0, offset_expr=None, turn=0.0):
    """Sketch on a body origin plane, offset along the plane normal and
    optionally turned about it."""
    sk = body.newObject("Sketcher::SketchObject", name)
    sk.AttachmentSupport = [(origin_feature(body, plane_role), "")]
    sk.MapMode = "FlatFace"
    sk.AttachmentOffset = App.Placement(V(0, 0, offset), App.Rotation(V(0, 0, 1), turn))
    if offset_expr:
        sk.setExpression(".AttachmentOffset.Base.z", offset_expr)
    return sk


def end_face_sketch(body, name, depth, depth_expr):
    """Sketch facing out of the latch end, ``depth`` from the end face.

    FreeCAD's XZ plane maps sketch (u, v) to global (X, Z) with its normal
    along -Y; turned half a turn it maps (u, v) to (-X, -Z). That is how the
    end face looks from outside with the box standing on its base, so text
    reads correctly, and a pocket cuts into the wall (+Y).
    """
    sk = sketch(body, name, "XZ_Plane", P["OUTER_Y"] - depth,
                "Params.OUTER_Y - %s" % depth_expr if depth_expr else "Params.OUTER_Y", turn=180.0)
    return sk


def end_uv(x, z):
    """Global (X, Z) to the end-face sketch's (u, v)."""
    return (-x, -z)


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
    build_key_slot(doc, body)
    return body


def key_slot_outline():
    """The key slot in global (X, Z), with expressions for the end-face sketch.

    From the end under the latch's free side: the entry pocket, deep enough
    that a key resting on its floor passes under the latch's rounded corner,
    a 45 degree ramp up to the slot floor, and the run to the far end.
    """
    pts = [(P["KEY_X0"], P["KEY_TOP_Z"]), (P["KEY_X1"], P["KEY_TOP_Z"]), (P["KEY_X1"], P["KEY_BOT_Z"]),
           (P["KEY_RAMP_X"], P["KEY_BOT_Z"]), (P["KEY_ENTRY_X"], P["KEY_ENTRY_Z"]), (P["KEY_X0"], P["KEY_ENTRY_Z"])]
    names = [("KEY_X0", "KEY_TOP_Z"), ("KEY_X1", "KEY_TOP_Z"), ("KEY_X1", "KEY_BOT_Z"),
             ("KEY_RAMP_X", "KEY_BOT_Z"), ("KEY_ENTRY_X", "KEY_ENTRY_Z"), ("KEY_X0", "KEY_ENTRY_Z")]
    return pts, [("-Params.%s" % x, "-Params.%s" % z) for x, z in names]


def key_slot_floor(x):
    """Floor of the key slot (Z) at X."""
    if x <= P["KEY_ENTRY_X"]:
        return P["KEY_ENTRY_Z"]
    if x >= P["KEY_RAMP_X"]:
        return P["KEY_BOT_Z"]
    return P["KEY_ENTRY_Z"] - (x - P["KEY_ENTRY_X"])


def arrow_outline():
    """Push arrow along the key slot, pointing toward +X (left, seen from outside)."""
    (x_t, x_h), z = MARK_ARROW_X, MARK_ARROW_Z
    neck = x_h - 3.0
    return [(x_t, z - 0.5), (neck, z - 0.5), (neck, z - 1.5), (x_h, z), (neck, z + 1.5), (neck, z + 0.5), (x_t, z + 0.5)]


def pointer_outline():
    """Triangle under the slot's entry, pointing up at it."""
    xc = P["KEY_X0"] + max(KEY_RADII) + KEY_CLEAR
    apex, base = MARK_POINTER_Z
    return [(xc, apex), (xc + 1.5, base), (xc - 1.5, base)]


def build_key_slot(doc, body):
    """Emergency bypass: the key slot through the latch-end wall, its skin and marking."""
    # The slot runs from behind the skin through the end wall into the
    # latch slot, under the lid's end wall, and squares off the latch
    # slot's rounded corner where the key goes in beside the latch.
    sk = end_face_sketch(body, "KeySlotSketch", P["KEY_SKIN"], "Params.KEY_SKIN")
    pts, exprs = key_slot_outline()
    polygon(sk, [end_uv(x, z) for x, z in pts], exprs, "Key")
    finish_sketch(sk)
    f = pocket(body, sk, "KeySlot", "Params.OUTER_Y - Params.KEY_SKIN + Params.LATCH_SLOT_Y0 + Params.KEY_REACH")
    recompute(doc, sk, f)
    report("Key slot", body.Shape)

    # Marking, sunk into the end face: a pointer at the entry, an arrow
    # along the slot the way the key pushes, and three lines of text.
    sk = end_face_sketch(body, "BypassArrowSketch", 0.0, None)
    polygon(sk, [end_uv(x, z) for x, z in pointer_outline()])
    polygon(sk, [end_uv(x, z) for x, z in arrow_outline()])
    finish_sketch(sk)
    f = pocket(body, sk, "BypassArrow", "Params.MARK_DEPTH")
    recompute(doc, sk, f)
    sk = end_face_sketch(body, "BypassTextSketch", 0.0, None)
    for text, cap, baseline in MARK_LINES:
        wires = text_wires(text, cap, 0.0, -baseline)
        half = Part.Compound(wires).BoundBox.XLength / 2
        if half > P["OUTER_X"] - P["OUTER_R"] - 2.0:
            raise RuntimeError("marking line %r is %.1f mm wide; the flat end face is %.1f" % (
                text, 2 * half, 2 * (P["OUTER_X"] - P["OUTER_R"])))
        add_blocked_wires(sk, wires)
    finish_sketch(sk)
    f = pocket(body, sk, "BypassText", "Params.MARK_DEPTH")
    recompute(doc, sk, f)
    report("Bypass marking", body.Shape)


def key_slot_solid():
    """The key slot as a solid, for the checks."""
    pts, _ = key_slot_outline()
    y0 = -P["OUTER_Y"] + P["KEY_SKIN"]
    wire = Part.makePolygon([V(x, y0, z) for x, z in pts] + [V(pts[0][0], y0, pts[0][1])])
    return Part.Face(wire).extrude(V(0, P["LATCH_SLOT_Y0"] + P["KEY_REACH"] - y0, 0))


def latch_profile(latch):
    """Outline (x, z) of the latch's uniform section, and the pivot fitted to its shaft bore."""
    loops = [np.array([(p.x, p.z) for p in w.discretize(Deflection=0.01)]) for w in latch.slice(V(0, 1, 0), LATCH_SECTION_Y)]

    def area(q):
        return 0.5 * abs(np.dot(q[:, 0], np.roll(q[:, 1], 1)) - np.dot(q[:, 1], np.roll(q[:, 0], 1)))
    loops.sort(key=area)
    outline, bore = loops[-1], loops[0]
    cx, cz = bore.mean(axis=0)
    for _ in range(50):  # least-squares circle, leaving out the shaft's D flat
        keep = np.hypot(bore[:, 0] - cx, bore[:, 1] - cz) > np.median(np.hypot(bore[:, 0] - cx, bore[:, 1] - cz)) - 0.05
        a = np.c_[2 * bore[keep], np.ones(keep.sum())]
        cx, cz = np.linalg.lstsq(a, (bore[keep] ** 2).sum(1), rcond=None)[0][:2]
    return outline, np.array([cx, cz])


def turn_profile(pts, piv, a):
    """Turn (x, z) points about the pivot; positive angles move the tip toward +X."""
    c, s = math.cos(math.radians(a)), math.sin(math.radians(a))
    d = pts - piv
    return np.c_[piv[0] + c * d[:, 0] + s * d[:, 1], piv[1] - s * d[:, 0] + c * d[:, 1]]


def key_gap(poly, c, r):
    """Clearance from a round key (centre c, radius r) to a polygon, and the nearest boundary point."""
    a, b = poly, np.roll(poly, -1, axis=0)
    ab = b - a
    t = np.clip(((c - a) * ab).sum(1) / np.maximum((ab ** 2).sum(1), 1e-12), 0.0, 1.0)
    q = a + t[:, None] * ab
    d = np.hypot(q[:, 0] - c[0], q[:, 1] - c[1])
    i = int(np.argmin(d))
    crosses = (a[:, 1] > c[1]) != (b[:, 1] > c[1])
    xs = a[:, 0] + (c[1] - a[:, 1]) * (b[:, 0] - a[:, 0]) / np.where(crosses, b[:, 1] - a[:, 1], 1.0)
    inside = np.count_nonzero(crosses & (c[0] < xs)) % 2 == 1
    return (-d[i] if inside else d[i]) - r, q[i]


def critical_friction(outline, piv, a, c, d):
    """Friction coefficient above which the key's push at this contact jams.

    The contact force is the normal push plus friction against the latch's
    sliding over the key. It turns the latch out of the slot while its torque
    about the pivot is positive.
    """
    _, q = key_gap(turn_profile(outline, piv, a + 0.02), c, 0.0)
    n = (q - c) / np.linalg.norm(q - c)  # push direction on the latch
    arm = q - piv

    def torque(f):
        return arm[1] * f[0] - arm[0] * f[1]
    t = np.array([-n[1], n[0]])
    v_latch = np.array([arm[1], -arm[0]])  # velocity of the contact per unit turn
    w = (d @ n) / (v_latch @ n)
    slip = (w * v_latch - d) @ t
    t_push, t_fric = torque(n), torque(-np.sign(slip) * t)
    if t_push <= 0:
        return 0.0
    return t_push / -t_fric if t_fric < 0 else math.inf


def push_latch(outline, piv, path, r, step=0.05):
    """Move a round key along ``path`` and turn the latch just enough to make room.

    Returns the angle the latch ends at, the lowest critical friction over
    the pushing contacts, and whether the key jammed (only a backward turn,
    which the slot wall stops, would make room).
    """
    a, mu = 0.0, math.inf
    for (x0, z0), (x1, z1) in zip(path[:-1], path[1:]):
        length = math.hypot(x1 - x0, z1 - z0)
        if length < 1e-9:
            continue
        d = np.array([x1 - x0, z1 - z0]) / length
        for i in range(int(length / step) + 1):
            c = np.array([x0, z0]) + d * min(i * step, length)
            if key_gap(turn_profile(outline, piv, a), c, r)[0] >= 0:
                continue
            lo, hi = a, a + 0.5
            while key_gap(turn_profile(outline, piv, hi), c, r)[0] < 0:
                lo, hi = hi, hi + 0.5
                if hi > 90:
                    return a, mu, True
            for _ in range(30):
                m = (lo + hi) / 2
                if key_gap(turn_profile(outline, piv, m), c, r)[0] < 0:
                    lo = m
                else:
                    hi = m
            a = hi
            mu = min(mu, critical_friction(outline, piv, a, c, d))
    return a, mu, False


def polyline_points(path, spacing):
    """Points along a polyline, at most ``spacing`` apart, ends included."""
    out = [path[0]]
    for (x0, z0), (x1, z1) in zip(path[:-1], path[1:]):
        n = max(1, int(math.ceil(math.hypot(x1 - x0, z1 - z0) / spacing)))
        out += [(x0 + (x1 - x0) * k / n, z0 + (z1 - z0) * k / n) for k in range(1, n + 1)]
    return out


def key_paths(r, outline, piv):
    """Key centre paths (x, z) for one key size, both from the entry pocket.

    "held up": the key is lifted until it meets the latch's corner (or the
    slot top), pushed along while rising to the slot top over 2 mm, then
    along the top. "resting": the key is left on the slot floor throughout,
    up the ramp and along.
    """
    x_e = P["KEY_X0"] + r + KEY_CLEAR
    x_f = P["KEY_X1"] - r
    z_rest = P["KEY_ENTRY_Z"] - r
    lo, hi = P["KEY_TOP_Z"] + r, z_rest  # highest point the key reaches before it touches the latch
    if key_gap(outline, np.array([x_e, lo]), r)[0] < 0:
        for _ in range(30):
            m = (lo + hi) / 2
            if key_gap(outline, np.array([x_e, m]), r)[0] < 0:
                lo = m
            else:
                hi = m
        z_lift = hi
    else:
        z_lift = lo
    held = [(x_e, z_rest), (x_e, z_lift), (x_e + 2.0, P["KEY_TOP_Z"] + r), (x_f, P["KEY_TOP_Z"] + r)]
    resting = []
    for i in range(int((x_f - x_e) / 0.1) + 1):
        x = min(x_e + 0.1 * i, x_f)
        z = min(key_slot_floor(x + u) - math.sqrt(max(r * r - u * u, 0.0))
                for u in np.linspace(-r, r, 41))
        resting.append((x, z))
    return {"held up": held, "resting": resting}


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
    """The top of the vault, down to the end of the transition: the whole
    lid interface with the latch slot, key slot and marking, for a test with
    the real lid, latch and motor."""
    box = doc.addObject("Part::Box", "EscapeTestBox")
    box.Length, box.Width = 100, 160
    box.setExpression("Height", "Params.TRANSITION_Z")
    box.Placement.Base = V(-50, -80, 0)
    box.Visibility = False
    common = doc.addObject("Part::Common", "EscapeTestPiece")
    common.Label = "Escape test piece (vault top, to TRANSITION_Z)"
    common.Base = vault
    common.Tool = box
    common.Visibility = False
    recompute(doc, common)
    return common


def check_key(vault_shape, parts, fails):
    """The key slot: passage, push on the latch, and the lid sliding off."""
    latch = parts["Locking Latch"]
    outline, piv = latch_profile(latch)
    print("  latch pivot fitted to its shaft bore: X %.2f Z %.2f" % tuple(piv))

    # The key's way in, behind the skin, must be clear of the vault and of
    # every part but the latch at each point of both paths.
    y0 = -P["OUTER_Y"] + P["KEY_SKIN"] + 0.01
    y1 = P["LATCH_SLOT_Y0"] + 6.2  # where the far corner round of the latch slot starts
    others = {k: v for k, v in parts.items() if k not in ("Locking Latch", "Housing` Bottom")}
    region = Part.makeBox(30, y1 - y0 + 2, 12, V(-26, y0 - 1, 26))
    vault_near = vault_shape.common(region)
    others = {k: v.common(region) for k, v in others.items() if v.BoundBox.intersect(region.BoundBox)}
    results = {}
    for r in KEY_RADII:
        for name, path in key_paths(r, outline, piv).items():
            hit = 0.0
            for x, z in polyline_points(path, 0.5):
                # 0.02 mm under size, so resting on the floor or touching the
                # slot top is not counted as a collision
                bar = Part.makeCylinder(r - 0.02, y1 - y0, V(x, y0, z), V(0, 1, 0))
                hit += bar.common(vault_near).Volume + sum(bar.common(o).Volume for o in others.values() if o.Volume)
            a, mu, jammed = push_latch(outline, piv, path, r)
            results[(r, name)] = a
            print("  key r %.2f %-8s: passage collides %.3f mm^3; latch turned %.1f deg, %s, lowest critical friction %.2f" % (
                r, name, hit, a, "JAMS" if jammed else "free", mu))
            if hit > 0.01:
                fails.append("the key's path (r %.2f, %s) is blocked" % (r, name))
            if jammed or mu < FRICTION_MARGIN:
                fails.append("the key push (r %.2f, %s) jams at friction %.2f" % (r, name, mu))

    # Sliding the lid off (+Y), the turned latch must pass over the far slot
    # wall, riding its chamfered edge where it still dips below the wall top.
    near = vault_shape.common(Part.makeBox(30, 20, 15, V(-25, -50, 26)))
    axis = V(piv[0], 0, piv[1])

    def ride_depth(a):
        deepest = 0.0
        for dy in (0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 6.0, 7.0):
            turned = latch.copy()
            turned.rotate(axis, V(0, 1, 0), a)
            turned.translate(V(0, dy, 0))
            c = turned.common(near)
            if c.Volume > 1e-5:
                deepest = max(deepest, c.optimalBoundingBox().ZLength)
        return deepest
    free = next((t / 2.0 for t in range(74, 90) if ride_depth(t / 2.0) == 0.0), None)
    print("  the lid slides off with no contact once the latch is turned %s deg" % free)
    for name, limit in (("held up", RIDE_DEPTH_HELD), ("resting", RIDE_DEPTH_RESTING)):
        a = min(v for (r, n), v in results.items() if n == name)
        depth = ride_depth(a)
        print("  key %-8s: latch at %.1f deg dips %.2f mm below the far wall top (rides over up to %.2f)" % (
            name, a, depth, limit))
        if depth > limit:
            fails.append("with the key %s the latch dips %.2f mm into the far slot wall" % (name, depth))


def check(vault_shape, interface_shape, bottom_shape, refs):
    print("checks")
    fails = []
    if len(vault_shape.Solids) != 1 or len(vault_shape.Shells) != 1 or not vault_shape.isValid():
        fails.append("vault is not one valid solid with one shell (sealed void?)")

    # Above the cut the vault must be the original part, minus the opening,
    # the new magnet pocket, the key slot and the marking, plus the two
    # fills; nothing else may change.
    above = Part.makeBox(100, 160, P["CUT_Z"] - 20, V(-50, -80, 20))
    orig_above = bottom_shape.common(above)
    removed = orig_above.cut(vault_shape)
    opening = Part.makeBox(2 * P["OPEN_X"], P["OPEN_Y1"] - P["OPEN_Y0"], 30,
                           V(-P["OPEN_X"], P["OPEN_Y0"], P["CUT_Z"] - 30))
    magnet = Part.makeCylinder(P["MAG_R"] + 0.01, 30, V(P["MAG_X"], P["MAG_Y"], P["MAG_FLOOR_Z"] - 30))
    slot = key_slot_solid()
    marking = Part.makeBox(2 * P["OUTER_X"], P["MARK_DEPTH"] + 0.01, 30, V(-P["OUTER_X"], -P["OUTER_Y"] - 0.01, 32))
    unexplained = removed.cut(opening).cut(magnet).cut(slot).cut(marking).Volume
    added = vault_shape.common(above).cut(bottom_shape).Volume
    fills = (math.pi * OLD_MAG_HOLE["r"] ** 2 * (P["CUT_Z"] - OLD_MAG_HOLE["z0"])
             + (OLD_STAND_SLOT["x"][1] - OLD_STAND_SLOT["x"][0]) * (OLD_STAND_SLOT["y"][1] - OLD_STAND_SLOT["y"][0])
             * (P["CUT_Z"] - OLD_STAND_SLOT["z0"]))
    print("  above CUT_Z: removed outside opening/magnet/key slot/marking %.3f mm^3, added %.1f mm^3 (fills %.1f)" % (
        unexplained, added, fills))
    if unexplained > 0.05 or added > fills + 0.5:
        fails.append("the interface above CUT_Z changed")
    cut = removed.common(slot).Volume
    print("  key slot removes %.1f mm^3 of the end wall" % cut)
    if cut < 50:
        fails.append("the key slot does not cut the end wall")
    pts, _ = key_slot_outline()
    face = Part.Face(Part.makePolygon([V(x, 0, z) for x, z in pts] + [V(pts[0][0], 0, pts[0][1])]))
    skin_box = face.extrude(V(0, P["KEY_SKIN"], 0))
    skin_box.translate(V(0, -P["OUTER_Y"], 0))
    skin = vault_shape.common(skin_box).Volume
    print("  skin over the key slot %.2f mm^3 of %.2f (%.2f mm thick)" % (skin, skin_box.Volume, P["KEY_SKIN"]))
    if skin < 0.99 * skin_box.Volume:
        fails.append("the skin over the key slot is incomplete")
    print("  key slot ceiling bridges %.1f mm across the end wall lip" % (P["KEY_X1"] - P["KEY_X0"]))

    parts = assembly_parts()
    for label in ("Housing` Top", "Locking Latch", "PCB Cover"):
        shape = parts[label]
        new = shape.common(vault_shape).Volume
        old = shape.common(bottom_shape).Volume
        print("  overlap with %-14s vault %.3f  original %.3f" % (label, new, old))
        if new > old + 0.05:
            fails.append("%s overlaps the vault more than the original" % label)
    check_key(vault_shape, parts, fails)

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
    # the cut whose normal points more than 45 degrees toward the bed. The
    # marking's 0.6 mm deep ceilings in the end face are short bridges and
    # are reported apart.
    mesh = MeshPart.meshFromShape(Shape=vault_shape, LinearDeflection=0.05, AngularDeflection=0.2, Relative=False)
    bad = mark = 0.0
    bad_at = []
    for f in mesh.Facets:
        nz_print = -f.Normal.z
        xc, yc, zc = (sum(p[i] for p in f.Points) / 3 for i in range(3))
        if zc > P["CUT_Z"] + 0.01 and zc < P["BASE_Z"] - 0.01 and nz_print < -math.cos(math.radians(45)) - 1e-3:
            if yc < -P["OUTER_Y"] + P["MARK_DEPTH"] + 0.02 and zc < MARK_LINES[-1][2] + 0.5:
                mark += f.Area
            else:
                bad += f.Area
                bad_at.append((round(f.Points[0][0]), round(f.Points[0][1]), round(zc, 1), round(nz_print, 2)))
    print("  bridges: end-face marking %.2f mm^2" % mark)
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
