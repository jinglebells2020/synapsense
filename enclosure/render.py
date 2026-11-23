"""Renders CadQuery shapes / STEP files to PNG previews (matplotlib, no GPU).

  python render.py part.step out.png
  (or import render_views from another script)
"""
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

VIEWS = [("top iso", 35, -60), ("bottom iso", -35, -60), ("front", 0, -90), ("side", 0, 0), ("top", 90, -90)]


def _mesh(shape, tol=0.05):
    verts, tris = shape.tessellate(tol, 0.2)
    v = np.array([(p.x, p.y, p.z) for p in verts])
    return v, np.array(tris)


def render_views(parts, out, title="", views=VIEWS):
    """parts: list of (shape, color) tuples."""
    meshes = [(_mesh(s), c) for s, c in parts]
    allv = np.vstack([m[0][0] for m in meshes])
    lo, hi = allv.min(0), allv.max(0)
    ctr, span = (lo + hi) / 2, (hi - lo).max() / 2 * 1.05
    fig = plt.figure(figsize=(4 * len(views), 4.4))
    light = np.array([0.4, -0.5, 0.75])
    light /= np.linalg.norm(light)
    for i, (name, elev, azim) in enumerate(views):
        ax = fig.add_subplot(1, len(views), i + 1, projection="3d")
        # One collection for all parts, so triangles are depth-sorted across parts.
        tris, fcs = [], []
        for (v, t), color in meshes:
            tri = v[t]
            n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
            n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
            shade = 0.35 + 0.65 * np.abs(n @ light)
            base = np.array(matplotlib.colors.to_rgb(color))
            tris.append(tri)
            fcs.append(np.clip(shade[:, None] * base[None, :], 0, 1))
        pc = Poly3DCollection(np.vstack(tris), facecolors=np.vstack(fcs), edgecolors="none", linewidths=0)
        ax.add_collection3d(pc)
        for k, setlim in enumerate((ax.set_xlim, ax.set_ylim, ax.set_zlim)):
            setlim(ctr[k] - span, ctr[k] + span)
        ax.set_box_aspect((1, 1, 1))
        ax.view_init(elev=elev, azim=azim)
        ax.set_title(name, fontsize=10)
        ax.set_axis_off()
    if title:
        fig.suptitle(title, fontsize=12)
    fig.tight_layout()
    fig.savefig(out, dpi=110)
    plt.close(fig)


if __name__ == "__main__":
    import cadquery as cq

    shp = cq.importers.importStep(sys.argv[1]).val()
    render_views([(shp, "#8fb3d9")], sys.argv[2], title=sys.argv[1])
