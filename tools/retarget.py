#!/usr/bin/env python3
"""Max Payne 1 (KFS/KF2) -> GTA SA ped skeleton retarget.

MP specifics only; the SA side (rig, FK, IFP I/O, generic delta retarget) lives in
`mod creation tools/sa_anim_baseline/sa_rig.py`.

MP space: Y up, +Z forward, ped RIGHT at +X, ground y=0.  SA bind space: Y up,
+Z forward, ped LEFT at +X.  So MP -> SA bind is the mirror S = diag(-1,1,1):
positions p @ S, rotations S @ R @ S (rows-as-axes, row-vector matrices).

Usage (frame check):  python3 retarget.py --clip idle_aim --frame 0
"""
import argparse
import os
import sys

import numpy as np

GAMES = "/media/montel/Extreme SSD/Games"
GTA = os.path.join(GAMES, "Grand Theft Auto San Andreas")
sys.path.insert(0, os.path.join(GTA, "mod creation tools", "sa_anim_baseline"))
sys.path.insert(0, os.path.expanduser(
    "~/.var/app/org.blender.Blender/config/blender/5.2/scripts/addons/io_scene_maxpayne"))
import sa_rig  # noqa: E402

MP_ROOT = os.path.join(GAMES, "Max Payne PC", "extract")  # unpacked x_data.ras
MP_SKEL_DIR = os.path.join(MP_ROOT, "data/database/skeletons/default_skeleton")
MP_SKEL = os.path.join(MP_SKEL_DIR, "maxpayne-collision.kfs")
MP_ANIM = os.path.join(MP_SKEL_DIR, "anim")

S = np.diag([-1.0, 1.0, 1.0])

# SA bone id -> MP bone whose world-space motion it copies.
FOLLOW = {
    1: "Pelvis", 3: "Torso", 21: "Torso", 31: "Torso", 4: "Neck", 5: "Head",
    22: "UpArm-R", 23: "LoArm-R", 32: "UpArm-L", 33: "LoArm-L", 34: "Hand-L",
    51: "UpLeg-R", 52: "LoLeg-R", 53: "Foot-R", 54: "Toe-R",
    41: "UpLeg-L", 42: "LoLeg-L", 43: "Foot-L", 44: "Toe-L",
}
# SA joint id -> MP joint at the same place (for aligning bone directions).
JOINT = {22: "UpArm-R", 23: "LoArm-R", 24: "Hand-R", 32: "UpArm-L", 33: "LoArm-L",
         34: "Hand-L", 51: "UpLeg-R", 52: "LoLeg-R", 53: "Foot-R", 54: "Toe-R",
         41: "UpLeg-L", 42: "LoLeg-L", 43: "Foot-L", 44: "Toe-L"}
CHAINS = [(22, 23), (23, 24), (32, 33), (33, 34),
          (51, 52), (52, 53), (53, 54), (41, 42), (42, 43), (43, 44)]

# SA weapon frame = R Hand frame; weapon muzzle +X, top +Z.  MP Gun bone: barrel
# +Z, top +Y.  Hand world rows (x,y,z) = Gun world rows (z,x,y).
P_GUN_TO_HAND = np.array([[0.0, 0, 1], [1, 0, 0], [0, 1, 0]])
# Dual pistols: SA draws the left gun with L Hand rotated 180 deg about X.
RX180 = np.diag([1.0, -1.0, -1.0])
# Standing root offset used by ped.ifp idles: game (0,0,0.023) == bind (0,0.023,0).
STAND = np.array([0.0, 0.023, 0.0])


def _reader():
    from max_payne_sdk.max_kf2 import MaxKF2Reader
    return MaxKF2Reader()


def load_mp_rest(skel=MP_SKEL):
    mp = {}
    for mesh in _reader().parse(skel).meshes:
        m = np.array(mesh.node.object_to_parent_transform, float)
        mp[mesh.node.name] = (m[:3, :3], m[3, :3], mesh.node.parent_name or None)
    return mp


def load_clip(name):
    """-> (tracks {bone: [(time_s, R, t)]}, duration_s)"""
    path = name if name.endswith(".kf2") else os.path.join(MP_ANIM, name + ".kf2")
    tracks, dur = {}, 0.0
    for ka in _reader().parse(path).keyframe_animations:
        fps = float(ka.animation.fps or 30)
        keys = sorted(((k.frame_id / fps, np.array(k.transform, float)) for k in ka.keyframes),
                      key=lambda k: k[0])
        tracks[ka.animation.object_name] = [(t, m[:3, :3], m[3, :3]) for t, m in keys]
        dur = max(dur, ka.num_total_keyframes / fps)
    return tracks, dur


def _sample(keys, t):
    if t <= keys[0][0]:
        return keys[0][1:]
    for (t0, r0, p0), (t1, r1, p1) in zip(keys, keys[1:]):
        if t <= t1:
            u = 0.0 if t1 == t0 else (t - t0) / (t1 - t0)
            q = sa_rig.slerp(sa_rig.mat_to_quat(r0), sa_rig.mat_to_quat(r1), u)
            return sa_rig.quat_to_mat(q), (1 - u) * p0 + u * p1
    return keys[-1][1:]


def mp_pose(mp, tracks=None, t=0.0):
    """MP world {bone: (R, p)} converted to SA bind space (mirrored)."""
    local = {n: (r, p) for n, (r, p, _) in mp.items()}
    for n, keys in (tracks or {}).items():
        if n in local and keys:
            local[n] = _sample(keys, t)
    w = {}

    def walk(n):
        if n not in w:
            r, p = local[n]
            par = mp[n][2]
            if par is None:
                w[n] = (r, p)
            else:
                rp, pp = walk(par)
                w[n] = (r @ rp, p @ rp + pp)
        return w[n]
    for n in mp:
        walk(n)
    return {n: (S @ r @ S, p @ S) for n, (r, p) in w.items()}


class Retargeter:
    def __init__(self, rig=None, mp=None):
        self.rig = rig or sa_rig.Rig()
        self.mp = mp or load_mp_rest()
        self.mp_ref = mp_pose(self.mp)
        src_pos = {sa: self.mp_ref[m][1] for sa, m in JOINT.items()}
        self.sa_ref = sa_rig.align_reference(self.rig, src_pos, CHAINS)
        # Hand-L's local axes are not the mirror of Hand-R's; E maps Hand-L's frame
        # onto the mirror-twin of Hand-R's frame (from the rest pose).
        hr, hl = self.mp_ref["Hand-R"][0], self.mp_ref["Hand-L"][0]
        self.E = (S @ hr @ S) @ hl.T

    def frame(self, tracks, t, dual=False, root_motion=True, ground_clamp=False):
        """-> (local rotations {sa_id: R}, root translation in SA bind space)"""
        cur = mp_pose(self.mp, tracks, t)
        src_ref = {sa: self.mp_ref[m][0] for sa, m in FOLLOW.items()}
        src_anim = {sa: cur[m][0] for sa, m in FOLLOW.items()}
        over = {24: P_GUN_TO_HAND @ cur["Gun"][0]}
        if dual:
            # MP has no left gun bone: left gun = right gun-in-hand offset, mirrored,
            # attached to Hand-L (through E).
            g_in_h = cur["Gun"][0] @ cur["Hand-R"][0].T
            gl = (S @ g_in_h @ S) @ self.E @ cur["Hand-L"][0]
            over[34] = RX180 @ P_GUN_TO_HAND @ gl
        local = sa_rig.retarget_world(self.rig, self.sa_ref, src_ref, src_anim, over)
        root_t = STAND.copy()
        if root_motion:
            d = cur["Pelvis"][1] - self.mp_ref["Pelvis"][1]
            # "vertical": in-place clips keep crouch/bob but no horizontal drift, which
            # SA would show as sliding/stepping; "full" (dodges) keeps travel.
            root_t += d if root_motion == "full" else d * [0, 1, 0]
        if ground_clamp:
            w = self.rig.fk({b: (local[b], (root_t if b == 0 else self.rig.local[b][1]))
                             for b in self.rig.order})
            foot_min = min(w[b][1][1] for b in (43, 44, 53, 54))
            bind_min = min(self.rig.bind_world[b][1][1] for b in (43, 44, 53, 54))
            if foot_min < bind_min:
                root_t = root_t + np.array([0, bind_min - foot_min, 0])
        return local, root_t


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--clip", default="idle_aim")
    ap.add_argument("--frame", type=int, default=0)
    a = ap.parse_args()
    rt = Retargeter()
    tracks, _ = load_clip(a.clip)
    local, root_t = rt.frame(tracks, a.frame / 30.0, dual="Dual" in a.clip)
    q = sa_rig.ifp_frame(rt.rig, local, root_t)
    for _, b in sa_rig.IFP_BONES:
        print(f"{b:3d} {rt.rig.name[b]:18s} q={np.round(q[b][0], 4)}")


if __name__ == "__main__":
    main()
