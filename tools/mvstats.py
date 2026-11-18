#!/usr/bin/env python3
"""Per-frame free-walk trace from maxpayne_combat.log "mv" lines (newest N lines).
  python3 tools/mvstats.py [N=200]
Prints, per frame: dir, forward/right displacement in CJ's own axes (m/frame), heading, clip tn.
Stop-motion shows up as alternating big/zero steps or tn jumping/backwards."""
import math, os, re, sys
LOG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "maxpayne_combat.log")
n = int(sys.argv[1]) if len(sys.argv) > 1 else 200
rows = []
for l in open(LOG, errors="replace"):
    if l.startswith("mv "):
        p = l.split(); kv = dict(x.split("=") for x in p[4:] if "=" in x)
        rows.append((int(p[1]), float(p[2]), float(p[3]), float(kv["h"]), float(kv.get("tn", -1)), float(kv["dt"])))
rows = rows[-n:]
for a, b in zip(rows, rows[1:]):
    dx, dy, h = b[1] - a[1], b[2] - a[2], b[3]
    fwd = -dx * math.sin(h) + dy * math.cos(h)
    right = dx * math.cos(h) + dy * math.sin(h)
    print("d%d fwd=%+.3f right=%+.3f h=%+.2f tn=%.3f dtn=%+.3f" % (b[0], fwd, right, h, b[4], b[4] - a[4]))
