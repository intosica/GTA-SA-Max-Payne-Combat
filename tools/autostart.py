#!/usr/bin/env python3
"""Launch GTA SA and load a save hands-free: skips intros, walks the front end by
recognising each screen against reference shots in tools/menu_refs/<state>.png.

  python3 tools/autostart.py              # launch + load save slot 2 (BIG SMOKE)
  python3 tools/autostart.py --learn NAME # save the current screen as reference NAME
  python3 tools/autostart.py --quit       # force-kill the game (gamescope, wine) — no menu

Safety: a key is only sent for a recognised screen (near-exact match, a fuzzy md5).
Before the main menu has been seen, unknown screens (intro movies) get Return; after it,
unknown screens get Escape (never confirms; backs out of DELETE GAME). Screenshots lag the
game, so after every key we wait for the screen to change before judging it again.
Refs include the save dates: re-learn load_slot1/2 if the saves change.
Done when maxpayne_combat.log prints "in world".
"""
import argparse
import os
import subprocess
import sys
import time

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
REFS = os.path.join(HERE, "menu_refs")
GAME = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
LOG = os.path.join(GAME, "maxpayne_combat.log")
SHOT = "/tmp/mp_autostart.png"
ENV = dict(os.environ, DISPLAY=":1",
           LD_LIBRARY_PATH=os.path.expanduser("~/.local/lib/x86_64-linux-gnu"))
# state -> key. Only these screens ever get a non-Escape key.
ACTIONS = {
    "main": "Return",          # START GAME highlighted
    "game_new": "Down",        # Game menu, NEW GAME highlighted
    "game_load": "Return",     # Game menu, LOAD GAME highlighted
    "load_slot1": "Down",      # save list, slot 1 highlighted
    "load_slot2": "Return",    # save list, slot 2 (BIG SMOKE) highlighted
}
MATCH = 0.3  # mean abs grey diff (0-255), 96x54 thumbnail; menu items differ by ~0.8


def shot():
    try:
        os.remove(SHOT)
    except FileNotFoundError:
        pass
    subprocess.run([os.path.expanduser("~/.local/bin/gamescopectl"), "screenshot", SHOT],
                   env=ENV, capture_output=True)
    last = -1
    for _ in range(60):  # gamescope writes asynchronously; wait until the size settles
        time.sleep(0.25)
        if os.path.exists(SHOT):
            size = os.path.getsize(SHOT)
            if size and size == last:
                try:
                    return thumb(SHOT)
                except OSError:
                    pass
            last = size
    return None


def thumb(path):
    return np.asarray(Image.open(path).convert("L").resize((96, 54)), float)


def classify(t):
    best, name = 1e9, None
    for f in sorted(os.listdir(REFS)) if os.path.isdir(REFS) else []:
        if f.endswith(".png"):
            d = np.abs(thumb(os.path.join(REFS, f)) - t).mean()
            if d < best:
                best, name = d, f[:-4]
    return (name if best < MATCH else None), best


def key(k):
    subprocess.run(["xdotool", "keydown", k], env=ENV)
    time.sleep(0.2)
    subprocess.run(["xdotool", "keyup", k], env=ENV)


def in_world(start):
    try:
        with open(LOG, errors="replace") as f:
            f.seek(start)
            return "in world" in f.read()
    except FileNotFoundError:
        return False


def running():
    pat = "[g]ta" + "-sa\\.exe"  # never spell the exe name literally: launch.sh pkills it
    return subprocess.run(["pgrep", "-f", pat], capture_output=True).returncode == 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--learn")
    ap.add_argument("--timeout", type=float, default=180)
    ap.add_argument("--quit", action="store_true")
    a = ap.parse_args()
    if a.quit:
        return quit_game(a.timeout)
    if a.learn:
        os.makedirs(REFS, exist_ok=True)
        if shot() is None:
            sys.exit("no screenshot")
        Image.open(SHOT).save(os.path.join(REFS, a.learn + ".png"))
        print("saved", a.learn)
        return
    start = os.path.getsize(LOG) if os.path.exists(LOG) else 0
    if not running():
        subprocess.Popen(["setsid", os.path.join(GAME, "launch.sh")], cwd=GAME,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(8)
    t0, seen_main = time.time(), False
    while time.time() - t0 < a.timeout:
        if in_world(start):
            print("in world after %.0fs" % (time.time() - t0))
            return
        t = shot()
        if t is None:
            time.sleep(1)
            continue
        state, d = classify(t)
        seen_main |= state == "main"
        k = ACTIONS.get(state, "Escape" if seen_main else "Return")
        print(f"{time.time() - t0:5.1f}s state={state} d={d:.1f} key={k}", flush=True)
        key(k)
        for _ in range(8):  # wait until the screen reacts (or ~10 s), never double-press
            time.sleep(0.5)
            n = shot()
            if n is not None and np.abs(n - t).mean() > 0.3:
                break
        time.sleep(1.0)
    sys.exit("timeout")


def quit_game(timeout=0):
    """Force quit + clear ghosts (user, 2026-11-15: agents force-quit, not through the menu).
    Gamescope's process name is "gamescope-wl" (pkill -x gamescope misses it); winedevice.exe of this
    prefix survives a kill and makes the next load hang. Kills by PID so this shell never kills itself."""
    me = {os.getpid(), os.getppid()}
    def pids(*args):
        r = subprocess.run(["pgrep", *args], capture_output=True, text=True).stdout.split()
        return [int(p) for p in r if int(p) not in me]
    for sig in ("-TERM", "-KILL"):
        for p in (pids("-f", "[g]ta[_-]sa" + "\\.exe") + pids("-x", "gamescope(-wl)?|gamescopereaper")
                  + pids("-f", "[w]inedevice\\.exe")):
            subprocess.run(["kill", sig, str(p)], capture_output=True)
        time.sleep(1)
    subprocess.run(["wineserver", "-k"], env=dict(os.environ, WINEPREFIX=os.path.expanduser("~/.wine-gtasa")),
                   capture_output=True)
    left = pids("-f", "[g]ta[_-]sa" + "\\.exe") + pids("-x", "gamescope(-wl)?|gamescopereaper")
    print("game killed" if not left else "still running: %s" % left)


if __name__ == "__main__":
    main()
