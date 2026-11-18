#!/usr/bin/env python3
"""Print kPhase[] for the ASI: normalized time of the left foot's strike (foot farthest ahead along the
pelvis travel direction) per MP_BODY clip, so crossfades between MOVE clips can be foot-aligned
(t_new = t_old - p_old + p_new). Non-travelling clips get 0."""
import retarget, sa_rig, numpy as np
rig = sa_rig.Rig()
objs = list(sa_rig.read_anp3("../assets/MP_BODY.IFP"))
out = []
for name, o in objs:
    nf = max(len(v[2]) for v in o.values())
    P = [sa_rig.ifp_pose(rig, o, i) for i in range(nf)]
    pel = np.array([p[1][1] for p in P]); lf = np.array([p[43][1] for p in P]) - pel
    trav = pel[-1, :2] - pel[0, :2]
    if not name.startswith(("MOVE", "ROCKET", "run", "WALK", "GUNMOVE")) or np.linalg.norm(trav) < 0.3:
        out.append(0.0); continue
    s = lf[:-1, :2] @ (trav / np.linalg.norm(trav))      # drop duplicate end frame
    i = int(s.argmax()); n = len(s)
    a, b, c = s[(i - 1) % n], s[i], s[(i + 1) % n]
    i += 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) else 0
    out.append(round(i / n, 3))
print("static const float kPhase[] = {" + ", ".join(map(str, out)) + "};  // %d clips" % len(out))
for (n, _), p in zip(objs, out):
    if p: print("  ", n, p)
