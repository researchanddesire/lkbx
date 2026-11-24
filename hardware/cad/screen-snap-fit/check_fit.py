"""Fit checks for the sprung-wall screen retention (CadQuery / OCP, not FreeCAD).

    python3 hardware/cad/screen-snap-fit/check_fit.py        from the repository root

Reads ../top-housing-snap-fit.step (exported by build_freecad.py), the
original ../top-housing.step and ../lockbox-assembly.step, puts the screen at
its datum and reports every solid intersection that matters:

- the seated screen against the housing (only the two spring preloads may touch it),
- the screen lifted off the skin (the catch and both hooks must stop it),
- the insertion path (bottom edge sliding in under the catch, top edge rotating down),
- the air behind and under the two sprung walls that their travel needs,
- the other assembly parts and the battery envelope against the new housing,
- the two screw bosses next to the bottom catch.

Every number is in the housing STEP frame (Z depth from the front face).
The sprung walls are drawn in their free state, so the seated screen is
expected to overlap them by exactly the preloads.
"""
import math
import sys
from pathlib import Path

import cadquery as cq
from OCP.BRepBuilderAPI import BRepBuilderAPI_Transform
from OCP.gp import gp_Ax1, gp_Dir, gp_Pnt, gp_Trsf, gp_Vec

HERE = Path(__file__).resolve().parent
CAD = HERE.parent
sys.path.insert(0, str(HERE))

# Geometry the checks need (kept in step with build_freecad.py's Params).
SCREEN_X = (-25.35, 22.45)
SCREEN_Y = (8.05, 42.65)
SKIN_Z, SCREEN_FRONT_Z, SCREEN_BACK_Z = 1.80, 1.85, 3.90
GLASS_T = SCREEN_BACK_Z - SCREEN_FRONT_Z
WALL_T, WALL_GAP = 1.26, 1.60
HOOK_REACH, HOOK_PRELOAD, HOOK_RELIEF = 0.60, 0.30, 0.15
TOP_WALL_Y0 = SCREEN_Y[1] + HOOK_RELIEF - HOOK_PRELOAD
TOP_WALL_TOP_Z, SIDE_WALL_TOP_Z = 11.40, 9.00
SIDE_WALL_X0 = SCREEN_X[1] + 0.20
SIDE_PRELOAD = 0.25
SIDE_BUMP_X = SCREEN_X[1] - SIDE_PRELOAD
CATCH_FACE_Y, CATCH_TIP_Y, CATCH_TIP_Z = SCREEN_Y[0] - 0.30, SCREEN_Y[0] + 1.00, SCREEN_BACK_Z + 1.00
TRAVEL = HOOK_REACH + HOOK_PRELOAD
OUTER_X = (-27.3, 27.3)
SIDE_WALL_Y = (9.3, 40.6)
HOOK_X = ((-15.0, -9.0), (9.0, 15.0))


def load(path):
    solids = cq.importers.importStep(str(path)).solids().vals()
    shape = solids[0]
    for s in solids[1:]:
        shape = shape.fuse(s)
    return shape


def assembly_parts():
    parts = cq.importers.importStep(str(CAD / "lockbox-assembly.step")).solids().vals()
    t = gp_Trsf()
    t.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0)), math.pi)
    t2 = gp_Trsf()
    t2.SetTranslation(gp_Vec(20.35, 49.12, -1.56))
    t2.Multiply(t)
    return [cq.Shape.cast(BRepBuilderAPI_Transform(p.wrapped, t2, True).Shape()) for p in parts]


def box(x, y, z):
    return cq.Solid.makeBox(x[1] - x[0], y[1] - y[0], z[1] - z[0], cq.Vector(x[0], y[0], z[0]))


def pieces(shape, label, limit=12):
    sol = [s for s in shape.Solids() if s.Volume() > 1e-6]
    print("  %s: %d piece(s), %.3f mm3" % (label, len(sol), sum(s.Volume() for s in sol)))
    for s in sol[:limit]:
        bb = s.BoundingBox()
        print("     %8.3f mm3  x %7.2f..%7.2f  y %6.2f..%6.2f  z %5.2f..%5.2f" % (
            s.Volume(), bb.xmin, bb.xmax, bb.ymin, bb.ymax, bb.zmin, bb.zmax))
    return sol


def span(shape):
    bb = shape.BoundingBox()
    return "x %.2f..%.2f y %.2f..%.2f z %.2f..%.2f" % (bb.xmin, bb.xmax, bb.ymin, bb.ymax, bb.zmin, bb.zmax)


def tilted_screen(screen, deg):
    """Screen tilted `deg` about its bottom front edge (top edge lifted), then slid so the
    bottom back corner of the glass sits on the catch's 45 degree underside while the bottom
    front corner stays on the skin: the way the glass goes in."""
    th = math.radians(deg)
    s = screen.rotate(cq.Vector(0, SCREEN_Y[0], SCREEN_FRONT_Z), cq.Vector(1, SCREEN_Y[0], SCREEN_FRONT_Z), deg)
    return s.translate(cq.Vector(0, GLASS_T * (math.cos(th) + math.sin(th) - 1), 0))


def main():
    H = load(CAD / "top-housing-snap-fit.step")
    H0 = load(CAD / "top-housing.step")
    parts = assembly_parts()
    screen = parts[0].translate(cq.Vector(0.15, 0.2, 0))
    sb = screen.BoundingBox()
    print("housing valid %s, volume %.1f mm3 (original %.1f)" % (H.isValid(), H.Volume(), H0.Volume()))
    print("screen at datum x %.2f..%.2f y %.2f..%.2f z %.2f..%.2f" % (sb.xmin, sb.xmax, sb.ymin, sb.ymax, sb.zmin, sb.zmax))

    print("\nseated screen vs housing (expect: the free-state top wall and hooks %.2f mm into the glass top edge"
          " at y %.2f, the side bump %.2f mm into the right edge at x %.2f, face contact on the left ribs, nothing else)"
          % (HOOK_PRELOAD, SCREEN_Y[1], SIDE_PRELOAD, SCREEN_X[1]))
    pieces(screen.intersect(H), "seated")

    print("\nscreen lifted 0.30 mm off the skin (catch and both hooks must stop it)")
    pieces(screen.translate(cq.Vector(0, 0, 0.30)).intersect(H), "lifted 0.30")
    print("screen pushed 0.30 mm toward the top wall (its face must stop it)")
    pieces(screen.translate(cq.Vector(0, 0.30, 0)).intersect(H), "shifted +y")
    print("screen pushed 0.30 mm toward the left ribs (they must stop it)")
    pieces(screen.translate(cq.Vector(-0.30, 0, 0)).intersect(H), "shifted -x")

    top_back = TOP_WALL_Y0 + WALL_T
    side_back = SIDE_WALL_X0 + WALL_T
    wall_bot = SKIN_Z + WALL_GAP
    print("\nair behind the sprung walls (must be empty): top wall travel %.2f mm, side wall travel %.2f mm"
          % (TRAVEL, SIDE_PRELOAD + 0.35))
    pieces(H.intersect(box((OUTER_X[0] + 0.4, OUTER_X[1] - 0.4), (top_back + 0.001, top_back + TRAVEL + 0.4),
                           (SKIN_Z + 0.001, TOP_WALL_TOP_Z + 0.3))), "behind top wall")
    pieces(H.intersect(box((side_back + 0.001, side_back + 0.9), (SIDE_WALL_Y[0] + 0.4, SIDE_WALL_Y[1] - 0.4),
                           (SKIN_Z + 0.001, SIDE_WALL_TOP_Z + 0.3))), "behind side wall")
    print("air under the sprung walls (must be empty, the bridge gap)")
    pieces(H.intersect(box((OUTER_X[0] + 0.4, OUTER_X[1] - 0.4), (TOP_WALL_Y0 - 0.001, top_back + 0.001),
                           (SKIN_Z + 0.001, wall_bot - 0.001))), "under top wall")
    pieces(H.intersect(box((SIDE_BUMP_X - 0.001, side_back + 0.001), (SIDE_WALL_Y[0] + 0.4, SIDE_WALL_Y[1] - 0.4),
                           (SKIN_Z + 0.001, wall_bot - 0.001))), "under side wall")

    # Insertion: the two sprung walls flex out of the way, everything else is rigid.
    top_region = box((OUTER_X[0] - 1, OUTER_X[1] + 1), (TOP_WALL_Y0 - 2.0, top_back + 0.5),
                     (wall_bot - 0.01, TOP_WALL_TOP_Z + 0.5))
    side_region = box((SIDE_BUMP_X - 0.3, side_back + 0.01), SIDE_WALL_Y, (wall_bot - 0.01, SIDE_WALL_TOP_Z + 0.5))
    rigid = H.cut(top_region).cut(side_region)
    sprung_top = H.intersect(top_region)
    sprung_side = H.intersect(side_region)
    print("\ninsertion path: glass tilted about its bottom edge, bottom back corner on the catch's 45 degree"
          " underside, bottom front corner on the skin (rigid housing must stay clear; the springs report"
          " how far the glass pushes into their free-state shape)")
    print("  catch block face y %.2f, catch tip y %.2f z %.2f, free top wall face y %.2f, free hook tip y %.2f"
          % (CATCH_FACE_Y, CATCH_TIP_Y, CATCH_TIP_Z, TOP_WALL_Y0, SCREEN_Y[1] - HOOK_PRELOAD - HOOK_REACH))
    for deg in (0, 1, 2, 3, 4, 6, 8, 10, 15, 20, 25, 30):
        s = tilted_screen(screen, deg)
        v = s.intersect(rigid)
        vt = s.intersect(sprung_top)
        vs = s.intersect(sprung_side)
        bb = s.BoundingBox()
        line = "  tilt %2d deg: bottom edge y %.2f, top edge z %5.2f, rigid housing %.3f mm3" % (deg, bb.ymin, bb.zmax, v.Volume())
        if v.Volume() > 1e-6:
            line += " HIT " + span(v)
        if vt.Volume() > 1e-6:
            tb = vt.BoundingBox()
            line += " | top wall/hooks %.3f mm3, glass to y %.2f (wall must give %.2f)" % (
                vt.Volume(), tb.ymax, tb.ymax - TOP_WALL_Y0)
        if vs.Volume() > 1e-6:
            line += " | side bump %.3f mm3 (wall must give %.2f)" % (vs.Volume(), SCREEN_X[1] - SIDE_BUMP_X)
        print(line)
    print("  (above ~32 degrees the bottom front corner would reach the catch block face; the glass is simply held higher)")

    print("\nother parts vs the housing (new / original; anything non-zero in both is pre-existing in the assembly)")
    names = {1: "button", 2: "encoder", 3: "encoder wheel", 4: "button", 8: "pcb", 11: "pcb cover"}
    for i, n in names.items():
        print("  %-14s %.3f / %.3f mm3" % (n, parts[i].intersect(H).Volume(), parts[i].intersect(H0).Volume()))
    battery = box((-19.5, 16.5), (14.75, 35.25), (3.86, 11.86))
    print("  %-14s %.3f / %.3f mm3" % ("battery", battery.intersect(H).Volume(), battery.intersect(H0).Volume()))

    print("\nscrew bosses beside the catch: material within 0.5 mm of each hole, before and after")
    for cx in (-17.0, 13.0):
        ring = cq.Solid.makeCylinder(1.32 + 0.5, 14.0, cq.Vector(cx, 3.0, 4.9)).cut(
            cq.Solid.makeCylinder(1.32, 14.0, cq.Vector(cx, 3.0, 4.9)))
        print("  boss at x %.0f: %.1f mm3 -> %.1f mm3" % (cx, ring.intersect(H0).Volume(), ring.intersect(H).Volume()))

    print("\nhook footprints: x %s, wall from x %.1f to %.1f (anchored in the outer walls)" % (HOOK_X, *OUTER_X))


if __name__ == "__main__":
    main()
