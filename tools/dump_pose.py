#!/usr/bin/env python3
"""IFP clip -> JSON of per-frame bone world matrices (game space) + bind, for tools/blender_view.py.
usage: dump_pose.py <ifp|ped> <clip> [out.json]"""
import json, sys
import numpy as np
import retarget  # noqa: F401  (puts sa_anim_baseline on sys.path)
import sa_rig

def m4(R, t):
    M = np.eye(4); M[:3, :3] = R.T; M[:3, 3] = t   # rig uses row vectors: p_world = p @ R + t
    return M.tolist()

src, clip = sys.argv[1], sys.argv[2]
out = sys.argv[3] if len(sys.argv) > 3 else f"/tmp/pose_{clip}.json"
rig = sa_rig.Rig()
objs = dict(sa_rig.read_anp3(sa_rig.PED_IFP if src == "ped" else src))
objs = {k.lower(): v for k, v in objs.items()}[clip.lower()]
nf = max(len(o[2]) for o in objs.values())
bind = sa_rig.ifp_pose(rig, {})
frames = [{b: m4(*w) for b, w in sa_rig.ifp_pose(rig, objs, i).items()} for i in range(nf)]
json.dump({"bind": {b: m4(*w) for b, w in bind.items()}, "frames": frames,
           "dt": objs[0][2][-1][2] / max(1, nf - 1) if 0 in objs else 1 / 30}, open(out, "w"))
print("wrote", out, nf, "frames")
