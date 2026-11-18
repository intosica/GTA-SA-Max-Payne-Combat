#!/usr/bin/env python3
"""Record Xbox-pad inputs with a screenshot + fuzzy hash of the screen at each press, so agents
can replay "on screen X press Y" (see tools/autostart.py for the replay side).

  python3 tools/record_pad.py [--name boot] [--launch]

--launch starts ../../launch.sh (game under gamescope). Stops when the game exits (or Ctrl-C).
Output: tools/recordings/<name>.jsonl, one event per line:
  {"t": 12.3, "input": "A", "down": true, "hash": "...", "shot": "/tmp/mp_rec/<name>/007.png",
   "in_world": false}
Sticks/triggers are logged only when they cross half travel (LX-/LX+ ...), not every wiggle.
hash = md5 of a 32x18 grey thumbnail quantised to 8 levels: same screen -> same hash (mostly);
compare thumbnails (mean abs diff) when hashes differ by a cursor blink.
Screenshots are taken just before the press is handled, i.e. the screen the input acted on.
"""
import argparse
import glob
import hashlib
import json
import os
import queue
import select
import struct
import subprocess
import threading
import time

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
LOG = os.path.join(GAME, "maxpayne_combat.log")
ENV = dict(os.environ, DISPLAY=":1", LD_LIBRARY_PATH=os.path.expanduser("~/.local/lib/x86_64-linux-gnu"))
# Linux xpad joystick numbering (js API)
BUTTONS = ["A", "B", "X", "Y", "LB", "RB", "BACK", "START", "GUIDE", "LS", "RS"]
AXES = ["LX", "LY", "LT", "RX", "RY", "RT", "DPADX", "DPADY"]
TRIGGERS = {2, 5}  # rest at -32767


def running():
    pat = "[g]ta" + "-sa\\.exe"  # never spell the exe name literally: launch.sh pkills it
    return subprocess.run(["pgrep", "-f", pat], capture_output=True).returncode == 0


def screenshot(path):
    subprocess.run([os.path.expanduser("~/.local/bin/gamescopectl"), "screenshot", path],
                   env=ENV, capture_output=True)
    last = -1
    for _ in range(40):  # gamescope writes asynchronously; wait for the size to settle
        time.sleep(0.1)
        if os.path.exists(path):
            s = os.path.getsize(path)
            if s and s == last:
                try:
                    return Image.open(path).convert("L")
                except OSError:
                    pass
            last = s
    return None


def fuzzy_md5(img):
    a = np.asarray(img.resize((32, 18)), np.uint8) >> 5
    return hashlib.md5(a.tobytes()).hexdigest()


def in_world(start):
    try:
        with open(LOG, errors="replace") as f:
            f.seek(start)
            return "in world" in f.read()
    except FileNotFoundError:
        return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", default=time.strftime("rec_%Y%m%d_%H%M%S"))
    ap.add_argument("--launch", action="store_true")
    ap.add_argument("--device", default=([j for j in glob.glob("/dev/input/by-id/*-joystick") if "-event-" not in j] or ["/dev/input/js0"])[0])
    a = ap.parse_args()
    shots = f"/tmp/mp_rec/{a.name}"
    os.makedirs(shots, exist_ok=True)
    os.makedirs(os.path.join(HERE, "recordings"), exist_ok=True)
    out_path = os.path.join(HERE, "recordings", a.name + ".jsonl")
    out = open(out_path, "w")
    log_start = os.path.getsize(LOG) if os.path.exists(LOG) else 0
    if a.launch and not running():
        subprocess.Popen(["setsid", os.path.join(GAME, "launch.sh")], cwd=GAME,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    t0 = time.time()
    print(f"recording {a.device} -> {out_path}", flush=True)

    # Screenshots in a worker so the reader never drops events; one shot per press.
    q = queue.Queue()

    def worker():
        n = 0
        while True:
            ev = q.get()
            if ev is None:
                return
            if ev["down"]:
                path = f"{shots}/{n:03d}_{ev['input']}.png"
                n += 1
                img = screenshot(path)
                ev["shot"], ev["hash"] = (path, fuzzy_md5(img)) if img else (None, None)
            ev["in_world"] = in_world(log_start)
            out.write(json.dumps(ev) + "\n")
            out.flush()
            print(json.dumps(ev), flush=True)

    th = threading.Thread(target=worker, daemon=True)
    th.start()

    dev = os.open(a.device, os.O_RDONLY | os.O_NONBLOCK)
    zone = {}  # axis -> -1/0/1 (past half travel)
    seen_game, last_check = False, 0.0
    try:
        while True:
            if time.time() - last_check > 2:  # has the game gone?
                last_check = time.time()
                r = running()
                seen_game |= r
                if seen_game and not r:
                    print("game exited", flush=True)
                    break
            if not select.select([dev], [], [], 0.5)[0]:
                continue
            data = os.read(dev, 8 * 64)
            for i in range(0, len(data) - 7, 8):
                _, value, typ, num = struct.unpack("IhBB", data[i:i + 8])
                if typ & 0x80:  # initial-state burst
                    continue
                t = round(time.time() - t0, 3)
                if typ == 1:
                    name = BUTTONS[num] if num < len(BUTTONS) else f"B{num}"
                    q.put({"t": t, "input": name, "down": bool(value)})
                elif typ == 2:
                    name = AXES[num] if num < len(AXES) else f"A{num}"
                    if num in TRIGGERS:
                        z = int(value > 0)
                    else:
                        z = (value > 16000) - (value < -16000)
                    if z != zone.get(num, 0):
                        if z:
                            q.put({"t": t, "input": name + ("+" if z > 0 else ""
                                   if num in TRIGGERS else "-"), "down": True})
                        else:
                            q.put({"t": t, "input": name, "down": False})
                        zone[num] = z
    except KeyboardInterrupt:
        pass
    q.put(None)
    th.join(timeout=30)
    print(f"saved {out_path}", flush=True)


if __name__ == "__main__":
    main()
