#!/usr/bin/env python3
"""ONE command from anything (dead, menu, in-game, ghosts) to CJ standing in the BIG SMOKE save.
  python3 tools/quickload.py [slot=2] [--install] [--timeout 240]   (--install: ./install.sh first)
How: kills everything (autostart.quit_game), writes <game>/mp_autoload.txt, launches launch.sh.
maxpayne_combat.asi reads that file, skips the movies and loads the slot itself (no keys, no
screenshots). Retries up to 3x (a load can freeze at the end of the load bar). Exit 0 + "IN WORLD" when the log says "in world". Read the log's "gamestate N"
lines when it fails: 7 = front end (autoload did not fire), 8 = loading, 9 = in game."""
import os, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from autostart import GAME, LOG, quit_game

def attempt(slot, timeout):
    quit_game()
    start = os.path.getsize(LOG) if os.path.exists(LOG) else 0
    with open(os.path.join(GAME, "mp_autoload.txt"), "w") as f:
        f.write("%d\n" % slot)
    subprocess.Popen(["setsid", os.path.join(GAME, "launch.sh")], cwd=GAME,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    t0, seen, loading = time.time(), 0, None
    while time.time() - t0 < timeout:
        time.sleep(1)
        try:
            with open(LOG, errors="replace") as f:
                f.seek(start); new = f.read()
        except FileNotFoundError:
            continue
        for line in new.splitlines()[seen:]:
            if line.startswith(("gamestate", "autoload", "maxpayne_combat loaded", "in world")):
                print("%5.0fs %s" % (time.time() - t0, line), flush=True)
        seen = len(new.splitlines())
        if "in world" in new:
            return True
        if "gamestate 8" in new and loading is None:
            loading = time.time()
        if loading and time.time() - loading > 60:  # a good load takes ~5 s; this is the known end-of-load-bar freeze
            print("stuck loading >60 s (known intermittent freeze) - killing"); return False
    print("timeout"); return False

def main():
    args = sys.argv[1:]
    if "--quit" in args: quit_game(); return 0
    timeout = 240.0
    if "--timeout" in args:
        i = args.index("--timeout"); timeout = float(args[i + 1]); del args[i:i + 2]
    install = "--install" in args
    if install: args.remove("--install")
    slot = int(args[0]) if args else 2
    if install:  # build output -> game root; needs the game dead, so kill first
        quit_game()
        r = subprocess.run([os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "install.sh")])
        if r.returncode: print("install failed"); return 1
    for n in range(3):
        if attempt(slot, timeout):
            print("IN WORLD"); return 0
        print("attempt %d failed, retrying from a clean kill" % (n + 1))
    quit_game(); print("FAILED 3 times - stop and tell the user"); return 1

if __name__ == "__main__":
    sys.exit(main())
