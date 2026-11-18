#!/usr/bin/env python3
"""Scripted pad camera/strafe test on the virtual pad (tools/vpad.py --serve must be running
before the game launched). For each camera mode: strafe, back-diagonals, turning while strafing,
and a stick-thrash; then per-segment metrics from maxpayne_combat.log:
  lat  = CJ's sideways offset in the view, frame-to-frame change (shake), m
  yawj = camera yaw rate change frame-to-frame (rad/s), p95 (jerky turning)
  hdg  = |CJ heading - camera yaw| p95 (body not following camera)
  python3 tools/padtest.py [modes...]   default 0 1 2"""
import math, os, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__)); GAME = os.path.abspath(os.path.join(HERE, "../../.."))
LOG = os.path.join(GAME, "maxpayne_combat.log")
SEGS = [("fwd", "ly=-1:1.5"), ("strafeR", "lx=1:2.5"), ("strafeL", "lx=-1:2.5"), ("backL", "lx=-0.7,ly=0.7:2"), ("backR", "lx=0.7,ly=0.7:2"),
        ("strafe+turn", "lx=1,rx=0.6:2"), ("thrash", " ".join(f"lx={(-1)**i},rx={0.9*(-1)**(i//2)},ry={0.8*(-1)**(i//3)}:0.25" for i in range(12))),
        ("turn", "rx=1:1.5"), ("pitch", "ry=-1:1 ry=1:1")]
send = lambda steps: subprocess.run([sys.executable, os.path.join(HERE, "vpad.py"), "--send", steps])
w = lambda a: (a + math.pi) % (2 * math.pi) - math.pi

def parse(lines):
    rows = []
    for l in lines:
        if l.startswith("cf "):
            kv = dict(x.split("=") for x in l.split()[2:])
            rows.append({k: (tuple(map(int, v.split(","))) if k in ("rs", "ls") else float(v)) for k, v in kv.items()})
    return rows

def metrics(rows):
    lat, yj, hd, dz, dd, ymax = [], [], [], [], [], 0
    prev_yr = None
    for a, b in zip(rows, rows[1:]):
        if b["dt"] < 1e-3: continue
        lat.append(abs(b["lat"] - a["lat"])); dz.append(abs(b["fz"] - a["fz"]) / b["dt"]); dd.append(abs(b["dist"] - a["dist"]) / b["dt"])
        yr = w(b["yaw"] - a["yaw"]) / b["dt"]; ymax = max(ymax, abs(yr))
        if prev_yr is not None: yj.append(abs(yr - prev_yr))
        prev_yr = yr
        hd.append(abs(w(b["h"] + math.pi / 2 - b["yaw"])))
    q = lambda v, p: sorted(v)[int(p * (len(v) - 1))] if v else float("nan")
    return (f"n={len(rows):4d} lat p95 {q(lat,.95):.3f} max {max(lat or [0]):.3f} | yawjerk p95 {q(yj,.95):5.2f} max yawrate {ymax:5.2f}"
            f" | pitchrate p95 {q(dz,.95):5.2f} | zoom p95 {q(dd,.95):5.2f} | body-cam p95 {q(hd,.95):.2f}")

modes = [int(a) for a in sys.argv[1:]] or [0, 1, 2]
open(os.path.join(GAME, "mp_trace.txt"), "w").close()
for mode in modes:
    open(os.path.join(GAME, "mp_cammode.txt"), "w").write(str(mode)); time.sleep(1.2)
    for name, steps in SEGS:
        a = sum(1 for _ in open(LOG, errors="ignore"))
        send(steps + " -:1")
        dur = sum(float(s.rsplit(":", 1)[1]) for s in steps.split()) + 1
        time.sleep(dur + 0.5)
        lines = open(LOG, errors="ignore").read().splitlines()[a:]
        print(f"mode {mode} {name:12s} {metrics(parse(lines))}", flush=True)
