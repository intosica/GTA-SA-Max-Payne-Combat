#!/usr/bin/env python3
"""Virtual Xbox 360 pad (uinput) for scripted tests. Steps: axis=value[,axis=value]:seconds
  python3 tools/vpad.py lx=1:3 lx=-1:3 lx=-0.7,ly=0.7:2 -:1 rx=0.5:2 a:0.2
Axes lx ly rx ry in -1..1 (ly/ry +1 = down, raw evdev convention); buttons a b x y lb rb start back
lt rt (full press). Each step sets only what it names, everything else back to rest. '-' = rest.
--wait N: create the pad, then wait N s before the steps (Wine needs to see the hotplug).
The game only picks up pads present at launch: keep one alive with `vpad.py --serve` (start it
before the game), then `vpad.py --send <steps>` plays steps on it (via /tmp/vpad.fifo)."""
import sys, time
from evdev import UInput, ecodes as e, AbsInfo

AX = {"lx": e.ABS_X, "ly": e.ABS_Y, "rx": e.ABS_RX, "ry": e.ABS_RY}
BT = {"a": e.BTN_A, "b": e.BTN_B, "x": e.BTN_X, "y": e.BTN_Y, "lb": e.BTN_TL, "rb": e.BTN_TR,
      "start": e.BTN_START, "back": e.BTN_SELECT}
TR = {"lt": e.ABS_Z, "rt": e.ABS_RZ}
stick = AbsInfo(0, -32768, 32767, 16, 128, 0); trig = AbsInfo(0, 0, 255, 0, 0, 0)
caps = {e.EV_KEY: list(BT.values()) + [e.BTN_MODE, e.BTN_THUMBL, e.BTN_THUMBR],
        e.EV_ABS: [(a, stick) for a in AX.values()] + [(a, trig) for a in TR.values()] +
                  [(e.ABS_HAT0X, AbsInfo(0, -1, 1, 0, 0, 0)), (e.ABS_HAT0Y, AbsInfo(0, -1, 1, 0, 0, 0))]}
FIFO = "/tmp/vpad.fifo"
if sys.argv[1:2] == ["--send"]:
    open(FIFO, "w").write(" ".join(sys.argv[2:]) + "\n"); sys.exit()
ui = UInput(caps, name="Microsoft X-Box 360 pad", vendor=0x045E, product=0x028E, version=0x110, bustype=e.BUS_USB)
args = sys.argv[1:]
if args[:1] == ["--wait"]: time.sleep(float(args[1])); args = args[2:]

def apply(state):
    for k, c in AX.items(): ui.write(e.EV_ABS, c, int(max(-1, min(1, state.get(k, 0))) * 32767))
    for k, c in TR.items(): ui.write(e.EV_ABS, c, 255 if state.get(k) else 0)
    for k, c in BT.items(): ui.write(e.EV_KEY, c, 1 if state.get(k) else 0)
    ui.syn()

def play(args):
  for step in args:
    spec, secs = step.rsplit(":", 1)
    st = {}
    if spec != "-":
        for part in spec.split(","):
            k, _, v = part.partition("=")
            st[k] = float(v) if v else 1
    apply(st)
    t_end = time.time() + float(secs)
    while time.time() < t_end: apply(st); time.sleep(0.01)  # re-send: keeps SDL from idling the axis
  apply({})

if args[:1] == ["--serve"]:
    import os
    if not os.path.exists(FIFO): os.mkfifo(FIFO)
    while True:
        for line in open(FIFO):
            try: play(line.split())
            except Exception as x: print("bad steps:", line.strip(), x, flush=True); apply({})  # keep the pad alive
play(args); time.sleep(0.2); ui.close()
