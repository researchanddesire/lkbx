#!/usr/bin/env python3
"""Render Bambu-style plate previews of an STL with matplotlib, no GPU needed.

usage: render_thumbnails.py PART.stl OUT_PREFIX

Writes OUT_PREFIX-plate_1.png (512 x 512, shaded), OUT_PREFIX-plate_1_small.png
(128 x 128) and OUT_PREFIX-plate_no_light_1.png (flat colour), all on a transparent
background, showing the part as it sits on the bed (STL Z up) from the front left.
The Bambu Studio command line skips these pictures without a display, and Bambu
Handy and the printer show them as the job preview; see package_sliced.py.
"""
import re
import struct
import sys

import numpy as np

COLOUR = (0.16, 0.50, 0.83)


def load_stl(path):
    """Return an (n, 3, 3) float array of triangles from a binary or ASCII STL."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:5] == b"solid" and b"facet" in data[:400]:
        values = re.findall(rb"vertex\s+(\S+)\s+(\S+)\s+(\S+)", data)
        return np.array(values, dtype=float).reshape(-1, 3, 3)
    (count,) = struct.unpack_from("<I", data, 80)
    record = np.dtype([("n", "<f4", 3), ("v", "<f4", (3, 3)), ("attr", "<u2")])
    return np.frombuffer(data, dtype=record, count=count, offset=84)["v"].astype(float)


def render(triangles, path, pixels, lit):
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from mpl_toolkits.mplot3d.art3d import Poly3DCollection

    normals = np.cross(triangles[:, 1] - triangles[:, 0], triangles[:, 2] - triangles[:, 0])
    lengths = np.linalg.norm(normals, axis=1)
    normals = normals / np.where(lengths == 0, 1, lengths)[:, None]
    if lit:
        light = np.array([-0.4, -0.6, 1.0])
        light /= np.linalg.norm(light)
        shade = 0.45 + 0.55 * np.clip(normals @ light, 0, 1)
    else:
        shade = np.full(len(triangles), 0.85)
    colours = np.clip(np.outer(shade, COLOUR), 0, 1)

    fig = plt.figure(figsize=(pixels / 100, pixels / 100), dpi=100)
    ax = fig.add_axes([0, 0, 1, 1], projection="3d")
    ax.add_collection3d(Poly3DCollection(triangles, facecolors=colours, edgecolors="none"))
    lo, hi = triangles.reshape(-1, 3).min(axis=0), triangles.reshape(-1, 3).max(axis=0)
    centre, span = (lo + hi) / 2, (hi - lo).max() * 0.62
    ax.set_xlim(centre[0] - span, centre[0] + span)
    ax.set_ylim(centre[1] - span, centre[1] + span)
    ax.set_zlim(centre[2] - span, centre[2] + span)
    ax.set_box_aspect((1, 1, 1))
    ax.view_init(elev=32, azim=-55)
    ax.set_axis_off()
    fig.savefig(path, dpi=100, transparent=True)
    plt.close(fig)


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    stl, prefix = sys.argv[1:]
    triangles = load_stl(stl)
    render(triangles, f"{prefix}-plate_1.png", 512, lit=True)
    render(triangles, f"{prefix}-plate_1_small.png", 128, lit=True)
    render(triangles, f"{prefix}-plate_no_light_1.png", 512, lit=False)
    print(f"{prefix}: {len(triangles)} triangles rendered")


if __name__ == "__main__":
    main()
