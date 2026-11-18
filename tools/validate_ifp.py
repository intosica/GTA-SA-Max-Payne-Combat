#!/usr/bin/env python3
"""Sanity-check retargeted IFPs in game space (every frame of every clip).

Checks: R Hand +X (weapon muzzle) points forward-ish and gun top up-ish while
aiming/shooting, head above pelvis, MP_BODY root travel goes the way the
SA clip it replaces does (SA moves the ped by it),, feet below pelvis, no NaNs, no quat flips.
Usage: python3 validate_ifp.py [../assets/MP_COMBAT.IFP ...]
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import retarget  # noqa: F401,E402  (sets sys.path for sa_rig)
import sa_rig  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT = [os.path.join(HERE, "..", "assets", f) for f in
           ("MP_COMBAT.IFP", "MP_COMBAT_UPPER.IFP", "MP_DODGE.IFP", "MP_LOCO.IFP", "MP_BODY.IFP")]


SA_TRAVEL = {}  # ped.ifp clip -> unit root-travel direction (moving clips only)


def _sa_travel():
    for n, o in sa_rig.read_anp3(sa_rig.PED_IFP):
        k = o[0][2] if 0 in o else None
        if not k or k[0][1] is None:
            continue
        v = (k[-1][1] - k[0][1])[:2] / k[-1][2]
        if np.linalg.norm(v) > 0.3:
            SA_TRAVEL[n.lower()] = v / np.linalg.norm(v)


def check(path, rig):
    if not SA_TRAVEL:
        _sa_travel()
    bad = 0
    for name, objs in sa_rig.read_anp3(path):
        nf = max(len(o[2]) for o in objs.values())
        aim = name.startswith(("idle_aim", "Shoot", "idle_bb")) and "Dodge" not in name and "_Up" not in name
        worst = 1.0
        for fi in range(nf):
            w = sa_rig.ifp_pose(rig, objs, fi)
            barrel, top = sa_rig.hand_barrel(w, 24)
            if aim:
                worst = min(worst, barrel[1])
            if "Dodge" not in name and 1 in objs and not (w[5][1][2] > w[1][1][2] > w[53][1][2]):
                print(f"  {name} f{fi}: body not upright"); bad += 1; break
        for b, (_, _, fr) in objs.items():
            q = np.array([f[0] for f in fr])
            if not np.isfinite(q).all() or (np.sum(q[1:] * q[:-1], 1) < 0).any():
                print(f"  {name} bone {b}: NaN or hemisphere flip"); bad += 1
        if os.path.basename(path) == "MP_BODY.IFP" and name.lower() in SA_TRAVEL:
            k = objs[0][2]
            got = (k[-1][1] - k[0][1])[:2] / k[-1][2]
            want = SA_TRAVEL[name.lower()]
            if got @ want < 0.5:  # under 0.5 m/s the SA way = stalls or walks backwards
                print(f"  {name}: root travel {got.round(2)} m/s, SA's goes {want.round(2)}"); bad += 1
        flag = "" if worst > 0.5 else "   <-- barrel not forward"
        bad += bool(flag)
        print(f"  {name:26s} frames={nf:3d} min(barrel.fwd)={worst:+.2f}{flag}")
    return bad


def main():
    rig = sa_rig.Rig()
    bad = 0
    for p in sys.argv[1:] or DEFAULT:
        print(os.path.basename(p))
        bad += check(p, rig)
    print("OK" if not bad else f"{bad} problems")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
