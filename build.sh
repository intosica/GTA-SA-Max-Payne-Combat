#!/bin/bash
# Build maxpayne_combat.asi against the local plugin-sdk.
set -euo pipefail
HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
SDK="${SDK:-$HERE/../../mod creation tools/plugin-sdk-sa-master}"
OUT="${OUT:-$HERE/asi/maxpayne_combat.asi}"

mapfile -t DIRS < <(find "$SDK/shared" "$SDK/plugin_sa" -type d)
ARGS=()
for d in "${DIRS[@]}"; do ARGS+=(-I"$d"); done

i686-w64-mingw32-g++ -std=c++23 -DRW -DGTASA -O2 -s \
  -shared -static -static-libgcc -static-libstdc++ \
  "${ARGS[@]}" \
  "$HERE/asi/maxpayne_combat.cpp" \
  "$SDK/shared/DynAddress.cpp" \
  "$SDK/shared/GameVersion.cpp" \
  "$SDK/shared/Patch.cpp" \
  "$SDK/shared/game/CRGBA.cpp" \
  "$SDK/plugin_sa/game_sa/CRect.cpp" \
  "$SDK/plugin_sa/game_sa/CSprite2d.cpp" \
  "$SDK/plugin_sa/game_sa/CPad.cpp" \
  "$SDK/plugin_sa/game_sa/RenderWare.cpp" \
  -o "$OUT" -lwinmm

echo "built $OUT"
