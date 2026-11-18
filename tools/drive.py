#!/usr/bin/env python3
"""Hold keys in the running game (gamescope, DISPLAY=:1). Steps: key[+key]:seconds, "-:s" = pause.
  python3 tools/drive.py w:2.5 -:0.5 a:2 d:2 s:2 a+w:2
Then read the trace: python3 tools/mvstats.py"""
import subprocess, sys, time
ENV = {"DISPLAY": ":1", "PATH": "/usr/bin:/bin"}
for step in sys.argv[1:]:
    keys, secs = step.rsplit(":", 1)
    keys = [] if keys == "-" else keys.split("+")
    for k in keys: subprocess.run(["xdotool", "keydown", k], env=ENV)
    time.sleep(float(secs))
    for k in keys: subprocess.run(["xdotool", "keyup", k], env=ENV)
