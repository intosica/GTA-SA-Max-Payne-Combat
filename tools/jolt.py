#!/usr/bin/env python3
"""Vigorous random-key test + jolt metric. Usage: python3 tools/jolt.py [seed]
Metric: per-frame |d lf.z|+|d rf.z|+|d hd.z| from the 'pose' log lines (foot/head height); counts frames > 0.35 (a pop)
and prints the worst few with their neighbours' clip line. Run after quickload.py."""
import random, subprocess, sys, re, os
seed = int(sys.argv[1]) if len(sys.argv) > 1 else 3
random.seed(seed)
ks = ['w','a','s','d','a+w','a+s','d+w','d+s']
steps = [f"{random.choice(ks)}:{random.choice([0.06,0.1,0.2,0.4,0.8])}" for _ in range(60)]
log = os.path.join(os.path.dirname(__file__), '../../../maxpayne_combat.log')
n0 = os.path.getsize(log)
subprocess.run(['python3', os.path.join(os.path.dirname(__file__), 'drive.py')] + steps + ['-:0.5'])
d = open(log, 'rb').read()[n0:].decode(errors='replace').split('\n')
P = []
for l in d:
    m = re.match(r'pose pel=(\S+) hd=(\S+) lf=(\S+) rf=(\S+)', l)
    if m: P.append([float(x) for x in m.groups()])
bad = []
for i in range(1, len(P)):
    j = abs(P[i][2]-P[i-1][2]) + abs(P[i][3]-P[i-1][3]) + abs(P[i][1]-P[i-1][1])
    bad.append(j)
bad_sorted = sorted(bad)
print(f"frames={len(P)} median={bad_sorted[len(bad)//2]:.3f} p95={bad_sorted[int(len(bad)*.95)]:.3f} max={bad_sorted[-1]:.3f} >0.35: {sum(b>0.35 for b in bad)} >0.5: {sum(b>0.5 for b in bad)}")
print('lunges', sum('lunge' in l for l in d), 'blendfix', sum('blend fix' in l for l in d))
