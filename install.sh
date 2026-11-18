#!/bin/bash
# Install the ASI + IFPs. Refuses while GTA SA is running.
set -euo pipefail
HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
GAME="${GAME:-$HERE/../..}"

if pgrep -x 'gta-sa.exe' >/dev/null 2>&1 || pgrep -x 'gta_sa.exe' >/dev/null 2>&1; then
  echo "GTA SA is running; close it before installing." >&2
  exit 1
fi

# A gamescope left over after the game exited blocks the next launch. Clear it ONLY when no game
# process exists and it is older than 2 minutes (a fresh launch has gamescope up before wine
# starts the exe). Never use close.sh from an agent: it kills a live session.
for pid in $(pgrep -x 'gamescope(-wl)?'; pgrep -x gamescopereaper); do
  if [ "$(ps -o etimes= -p "$pid" | tr -d ' ')" -gt 120 ] 2>/dev/null; then
    kill -9 "$pid" 2>/dev/null && echo "cleared stale gamescope $pid"
  fi
done

mkdir -p "$GAME/maxpayne_ui"
cp -f "$HERE"/assets/ui/* "$GAME/maxpayne_ui/"
for f in asi/maxpayne_combat.asi assets/MP_COMBAT.IFP assets/MP_COMBAT_UPPER.IFP assets/MP_DODGE.IFP assets/MP_LOCO.IFP assets/MP_BODY.IFP; do
  test -f "$HERE/$f" || { echo "missing $f; run ./build.sh" >&2; exit 1; }
  cp -f "$HERE/$f" "$GAME/"
  echo "installed $f"
done
