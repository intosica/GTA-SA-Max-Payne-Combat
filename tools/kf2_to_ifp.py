#!/usr/bin/env python3
"""Export retargeted Max Payne KF2 clips to a GTA SA ANP3 IFP.

Usage:
  python3 kf2_to_ifp.py --clips idle_aim ShootBerettaDual --out ../assets/MP_COMBAT.IFP
  python3 kf2_to_ifp.py --upper --block MPU --clips ... --out ../assets/MP_COMBAT_UPPER.IFP
  ./build_ifps.sh   # regenerates all four IFPs

Full-body: 26 SA ped bones, root type 4 (rot+trans), rest type 3.
--upper: 16 spine/head/arm bones, --arm: 9 spine/R-arm bones; all type 3, no root.
Clips are resampled to 30 fps. Clips with "Dual" in the name get a left-hand gun.
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import retarget as R  # noqa: E402
from sa_rig import IFP_BONES, ifp_frame, write_anp3  # noqa: E402

FPS = 30
_UP = {2, 3, 4, 5, 31, 32, 33, 34, 35, 36, 21, 22, 23, 24, 25, 26}
_ARM = {2, 3, 4, 21, 22, 23, 24, 25, 26}
UPPER_BONES = [b for b in IFP_BONES if b[1] in _UP]
ARM_BONES = [b for b in IFP_BONES if b[1] in _ARM]
# No pelvis: SA extracts the ped's walk velocity from the pelvis track, so overriding it with
# a partial (rotation-only) clip zeroes CJ's anim-driven movement -> he glides in place.
LEG_BONES = [b for b in IFP_BONES if b[1] in {41, 42, 43, 44, 51, 52, 53, 54}]
L_ARM = {31, 32, 33, 34, 35, 36}
# MP (default_skeleton.txt) splits Max into upper body (torso, head, gun arm(s)) and
# lower body (pelvis, legs, and the free left arm for one-handed weapons).
# Two-handed = "2_" clips plus these weapon families.
TWO_HANDED = ("2_", "Reload", "Dual", "Shotgun", "MP5", "Colt", "Sniper", "Jackhammer", "M79")


def two_handed(clip):
    clip = clip.split("=")[-1]
    return clip.startswith("2_") or any(k in clip for k in TWO_HANDED[1:])


def clip_bones(bones, clip, upper, legs):
    """Upper clips drop the left arm for one-handed weapons; leg clips carry it."""
    if upper and not two_handed(clip):
        return [b for b in bones if b[1] not in L_ARM]
    if legs and not two_handed(clip):
        return bones + [b for b in IFP_BONES if b[1] in L_ARM]
    return bones


def bake_stride(rig, raw):
    """MP loco clips are in place; SA moves peds by the root's translation. Each frame the
    root advances by the horizontal slide of the planted (= lower) foot, reversed, so the
    planted foot stays put in the world (summed per cycle, then spread evenly). (Guessing the travel direction from the
    mean foot motion was unreliable for in-place cycles: it flipped walk backwards and zeroed
    run/gunmove.)"""
    feet = []
    for local, root_t in raw:
        w = rig.fk({b: (local[b], root_t if b == 0 else rig.local[b][1]) for b in rig.order})
        feet.append([w[43][1], w[53][1]])  # L/R foot, SA bind space (Y up)
    feet = np.array(feet)
    d = (feet[1:] - feet[:-1]) * [1, 0, 1]                         # [frame, foot, xyz]
    # 1) travel direction: reversed slide of the lower foot, summed over the cycle (robust)
    low = np.argmin(feet[:-1, :, 1] + feet[1:, :, 1], axis=1)
    travel = -d[np.arange(len(d)), low].sum(0)
    travel /= np.linalg.norm(travel) or 1
    # 2) per frame, the planted foot is the one sliding most against the travel direction
    off = np.zeros(3)
    for i in range(len(d)):
        k = int(np.argmin(d[i] @ travel))
        off = off - d[i, k] if d[i, k] @ travel < 0 else off
    # 3) spread that cycle travel evenly over the frames. Following the planted foot frame by
    # frame made the speed swing 0..0.14 m/frame (dead stop at every foot switch); SA moves CJ
    # and the camera by it, so walking looked like stop motion. SA's own clips are ~constant.
    n = len(raw) - 1
    return [(local, root_t + off * i / n) for i, (local, root_t) in enumerate(raw)]


# Face/chest helper bones SA's ped clips key but MP has no data for. A clip that fully
# replaces an SA clip must carry them, else they fall to a broken default (stretched face).
FILL_BONES = [("Jaw", 8), ("L Brow", 6), ("R Brow", 7), ("L breast", 302), ("R breast", 301), ("Belly", 201)]
_PED = None


def fill_from_ped(name, frames):
    """Copy the SA clip's (same name, any case) frame-0 face/chest keys into every frame."""
    global _PED
    if _PED is None:
        import sa_rig
        _PED = {n.lower(): o for n, o in sa_rig.read_anp3(sa_rig.PED_IFP)}
    src = _PED.get(name.lower()) or _PED["idle_stance"]
    for _, b in FILL_BONES:
        if b in src:
            for f in frames:
                f[b] = (src[b][2][0][0], None)
    return frames


def _lfwd(feet_fwd):
    """normalized time where the left foot is furthest ahead of the right."""
    return int(np.argmax(feet_fwd)) / len(feet_fwd)


def phase_to_sa(rig, name, raw):
    """SA cross-blends its loco clips (walk/run, GUNMOVE_*) at the same normalized time, so
    their foot cycles were authored in step. Roll the MP cycle (raw = n frames + duplicate
    end frame) so its left-foot-forward moment lands where the SA clip it replaces has it;
    otherwise blends mix opposite feet and the legs twist."""
    import sa_rig
    global _PED
    if _PED is None:
        _PED = {n.lower(): o for n, o in sa_rig.read_anp3(sa_rig.PED_IFP)}
    sa = _PED.get(name.lower())
    if sa is None:
        return raw
    nsa = max(len(o[2]) for o in sa.values())
    sa_d = [(lambda w: w[43][1][1] - w[53][1][1])(sa_rig.ifp_pose(rig, sa, i)) for i in range(nsa - 1)]
    C = sa_rig.C_BIND_TO_GAME
    n = len(raw) - 1
    mp_d = []
    for local, root_t in raw[:n]:
        w = rig.fk({b: (local[b], root_t if b == 0 else rig.local[b][1]) for b in rig.order})
        mp_d.append(((w[43][1] - w[53][1]) @ C)[1])
    s = int(round((_lfwd(mp_d) - _lfwd(sa_d)) * n)) % n
    print(f"  phase {name}: MP Lfwd {_lfwd(mp_d):.2f} -> SA {_lfwd(sa_d):.2f} (roll {s}/{n})")
    body = raw[s:n] + raw[:s]
    return body + [body[0]]


def still_torso(rt, raw, dual, ground_clamp, ref="Stand"):
    """Spine and everything above it take Stand frame 0, with the spine re-expressed so its
    rotation relative to the root (not the swinging pelvis) matches Stand's."""
    rig = rt.rig
    up = {2}
    for b in rig.order:
        if rig.parent[b] in up:
            up.add(b)
    tracks, _ = R.load_clip(ref)
    ls, _ = rt.frame(tracks, 0.0, dual, "vertical", ground_clamp)
    ps = ls[1] @ ls[0]
    out = []
    for local, root_t in raw:
        local = dict(local)
        for b in up:
            local[b] = ls[b]
        local[2] = ls[2] @ ps @ (local[1] @ local[0]).T
        out.append((local, root_t))
    return out


def make_clip(rt, clip, bones, partial, root_motion, ground_clamp, loop=False, phase=False):
    """clip: KF2 name, or "Name=Clip@seconds" for a held single pose (e.g. AimBeretta).
    "Name=Clip%still": idle with pelvis/neck/head/legs held at frame 0 (no look-around/shuffle).
    "Name=Clip%torso": upper body (spine up) held at Stand frame 0, facing the root, so
    sidesteps/backpedal don't swing the torso (Max keeps it still; MP plays an aim layer there)."""
    name, src = clip.split("=") if "=" in clip else (clip, clip)
    src, _, mode = src.partition("%")
    src, hold = src.split("@") if "@" in src else (src, None)
    tracks, dur = R.load_clip(src)
    dual = "Dual" in src
    frames = []
    n = 1 if hold else int(round(dur * FPS))
    have = {b for _, b in bones}
    stride = root_motion == "stride" and "Stand" not in src  # idles stay in place
    if stride:
        root_motion = "vertical"
    raw = []
    for i in range(max(2, n if loop else n + 1)):  # loops: drop the duplicate end frame
        t = float(hold) if hold else i / FPS
        local, root_t = rt.frame(tracks, t, dual, None if partial else root_motion, ground_clamp)
        raw.append((local, root_t))
    if mode == "still" or name == "ROCKET_IDLE":
        # Max's Stand idles look around (head +-45 deg) and shuffle the whole body ~25 cm
        # sideways over 14 s. On CJ that reads as the head snapping right/left/right and
        # the feet drifting out of place after every stop. Hold pelvis, neck, head and legs
        # at frame 0; the spine and arms keep Max's breathing.
        still = (1, 4, 5, 41, 42, 43, 44, 51, 52, 53, 54)
        raw = [({b: (raw[0][0][b] if b in still else m) for b, m in local.items()}, raw[0][1])
               for local, _ in raw]
    if mode == "torso":
        raw = still_torso(rt, raw, dual, ground_clamp, "2_Stand" if src.startswith("2_") else "Stand")
    if stride and phase and not loop:
        # MOVE* (free-walk directions) all swap into the walk/run slots: phase them like run.
        raw = phase_to_sa(rt.rig, "run_player" if name.startswith(("MOVE", "ROCKET")) else name.replace("Sway", ""), raw)
    if stride:
        raw = bake_stride(rt.rig, raw)
    for local, root_t in raw:
        if partial:
            # Partial clips play over SA's own pelvis: express the pelvis children (spine,
            # thighs) relative to the reference pelvis instead of MP's animated one.
            local = dict(local)
            for c in (2, 41, 51):
                if c in have:
                    local[c] = local[c] @ local[1] @ (rt.sa_ref[1] @ rt.sa_ref[0].T).T
        frames.append(ifp_frame(rt.rig, local, root_t, bones))
    if mode == "rocket":
        # Max's legs/pelvis, SA's own rocket-launcher carry from torso up (IDLE_ROCKET frame 0)
        global _PED
        if _PED is None:
            import sa_rig
            _PED = {n.lower(): o for n, o in sa_rig.read_anp3(sa_rig.PED_IFP)}
        src = _PED["idle_rocket"]
        for f in frames:
            for b in (2, 3, 4, 5, 21, 22, 23, 24, 25, 26, 31, 32, 33, 34, 35, 36):
                if b in f and b in src:
                    f[b] = (src[b][2][0][0], None)
    return name, frames


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--clip", action="append", default=[])
    ap.add_argument("--clips", nargs="*", default=[])
    ap.add_argument("--out", required=True)
    ap.add_argument("--upper", action="store_true")
    ap.add_argument("--arm", action="store_true")
    ap.add_argument("--legs", action="store_true")
    ap.add_argument("--loop", action="store_true")
    ap.add_argument("--root-motion", choices=["vertical", "full", "stride", "none"], default="vertical")
    ap.add_argument("--ground-clamp", action="store_true")
    ap.add_argument("--block", default="MPC")
    ap.add_argument("--phase-to-sa", action="store_true", help="roll stride cycles into step with the SA clip they replace")
    ap.add_argument("--fill-from-ped", action="store_true", help="add SA face/chest keys (clip replacements)")
    a = ap.parse_args()

    bones = LEG_BONES if a.legs else ARM_BONES if a.arm else UPPER_BONES if a.upper else IFP_BONES
    partial = a.arm or a.upper or a.legs
    rt = R.Retargeter()
    clips = [make_clip(rt, c, clip_bones(bones, c, a.upper, a.legs), partial, None if a.root_motion == "none" else a.root_motion, a.ground_clamp, a.loop, a.phase_to_sa)
             for c in (a.clips or a.clip or ["idle_aim"])]
    if a.fill_from_ped:
        clips = [(n, fill_from_ped(n, f)) for n, f in clips]
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    write_anp3(a.out, a.block, clips, FPS, IFP_BONES + FILL_BONES, root_type=3 if partial else 4)
    print(f"wrote {a.out}: {len(clips)} clips, {os.path.getsize(a.out)} bytes")


if __name__ == "__main__":
    main()
