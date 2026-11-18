#!/usr/bin/env python3
"""Stick-figure check: Max Payne source pose (left) vs retargeted SA IFP pose (right).

Usage: python3 plot_pose.py idle_aim ShootBerettaDual --frame 5 --out /tmp/poses.png
       python3 plot_pose.py Walk:WALK_player --frames 0 9 18 27 --azim 0   (MP clip:IFP clip, side view)
The red line on each right hand is the weapon muzzle axis (SA hand +X / MP Gun +Z).
"""
import argparse
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import retarget as R  # noqa: E402
import sa_rig  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
IFPS = [os.path.join(HERE, "..", "assets", f) for f in
        ("MP_COMBAT.IFP", "MP_DODGE.IFP", "MP_COMBAT_UPPER.IFP", "MP_LOCO.IFP", "MP_BODY.IFP")]
SA_SKIP = {0}


def draw(ax, segs, guns, title, azim=-60):
    for a, b in segs:
        ax.plot(*zip(a, b), color="k")
    for p, d in guns:
        ax.plot(*zip(p, p + 0.3 * d), color="r", lw=3)
    ax.set_title(title, fontsize=8)
    ax.set_xlim(-1, 1); ax.set_ylim(-1, 1); ax.set_zlim(-1.1, 0.9)
    ax.view_init(elev=10, azim=azim)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("clips", nargs="+")
    ap.add_argument("--frame", type=int, default=0)
    ap.add_argument("--frames", type=int, nargs="+")
    ap.add_argument("--azim", type=float, default=-60)
    ap.add_argument("--out", default="/tmp/mp_poses.png")
    a = ap.parse_args()
    frames = a.frames or [a.frame]
    rig = sa_rig.Rig()
    mp = R.load_mp_rest()
    clips = {}
    for p in IFPS:
        for n, o in sa_rig.read_anp3(p):
            clips.setdefault(n, o)
    # MP bind space (mirrored, Y-up) -> game (Z-up): same C as the SA root.
    C = sa_rig.C_BIND_TO_GAME
    rows = [(f, c) for c in a.clips for f in frames]
    fig = plt.figure(figsize=(6, 3 * len(rows)))
    for i, (fr, spec) in enumerate(rows):
        name, _, ifp_name = spec.partition(":")
        ifp_name = ifp_name or name
        tr, _ = R.load_clip(name)
        w = R.mp_pose(mp, tr, fr / 30.0)
        off = w["Pelvis"][1]
        P = {n: (v[1] - off) @ C for n, v in w.items()}
        segs = [(P[n], P[p]) for n, (_, _, p) in mp.items() if p and not n.startswith(("J", "Belt", "should"))]
        gun = (P["Gun"], (w["Gun"][0] @ C)[2])
        draw(fig.add_subplot(len(rows), 2, 2 * i + 1, projection="3d"), segs, [gun], f"MP {name} f{fr}", a.azim)
        sw = sa_rig.ifp_pose(rig, clips[ifp_name], fr)
        o = sw[1][1].copy()
        segs = [(sw[b][1] - o, sw[rig.parent[b]][1] - o) for b in rig.order
                if rig.parent[b] is not None and rig.parent[b] not in SA_SKIP and b < 100]
        guns = [(sw[24][1] - o, sw[24][0][0])]
        if "Dual" in name:
            guns.append((sw[34][1] - o, sw[34][0][0]))
        draw(fig.add_subplot(len(rows), 2, 2 * i + 2, projection="3d"), segs, guns, f"SA {ifp_name} f{fr}", a.azim)
    fig.tight_layout()
    fig.savefig(a.out, dpi=80)
    print("wrote", a.out)


if __name__ == "__main__":
    main()
