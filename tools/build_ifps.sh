#!/bin/bash
# Regenerate all four IFPs in assets/ from the Max Payne KF2 clips.
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"
A=../assets
# Clip names follow MP's own Max setup (data/database/skeletons/default_skeleton.txt):
# Stand/2_Stand = armed stand, Walk/Run/... = locomotion ("w_" = wounded limp, "idle_aim*" =
# fidgets, so neither is used), Shoot*/Reload* = upper-body actions. 2_ = two-handed.
SHOOT="ShootBeretta ShootBerettaDual ShootDesertEagle ShootIngram ShootColt ShootSniper \
  ShootPumpShotgun ShootSawedShotgun ShootJackhammer"
RELOAD="ReloadBeretta ReloadBerettaDual ReloadDesertEagle ReloadIngram ReloadMP5 \
  ReloadPumpShotgun ReloadSawedShotgun ReloadJackhammer"
python3 kf2_to_ifp.py --block MPC --out $A/MP_COMBAT.IFP --clips Stand 2_Stand $SHOOT $RELOAD
# Aim* = first frame of the weapon's shoot clip held (MP's aim pose; Max is always aiming).
AIM=$(for c in $SHOOT; do echo "Aim${c#Shoot}=$c@0"; done)
python3 kf2_to_ifp.py --upper --block MPU --out $A/MP_COMBAT_UPPER.IFP --clips Stand 2_Stand $AIM $SHOOT $RELOAD
python3 kf2_to_ifp.py --ground-clamp --root-motion full --block MPD --out $A/MP_DODGE.IFP --clips \
  ShootDodgeForward ShootDodgeBackward ShootDodgeLeft ShootDodgeRight
python3 kf2_to_ifp.py --legs --loop --block MPL --out $A/MP_LOCO.IFP --clips \
  Stand 2_Stand Walk Run WalkBackward StrafeLeft StrafeRight ForwardLeft ForwardRight BackwardLeft BackwardRight \
  2_Walk 2_Run 2_WalkBackward 2_StrafeLeft 2_StrafeRight 2_ForwardLeft 2_ForwardRight \
  2_BackwardLeft 2_BackwardRight
# MP_BODY: full-body MP clips named after the SA player clips they replace (the ASI re-points
# SA's own associations at them). Uncompressed, end frame kept, root travel baked from the
# planted foot so SA's root-motion movement matches Max's feet.
# MOVE<k>/MOVE2_<k>: free walking in 8 directions while CJ keeps facing the camera, like Max
# (k = 1 fwd-right .. 7 fwd-left, clockwise; 2_ = two-handed). The ASI swaps SA's walk/run
# slots to the stick's direction. Plain = still torso, *Sway = Max's own torso.
MOVES=""
DIRS=(x ForwardRight StrafeRight BackwardRight WalkBackward BackwardLeft StrafeLeft ForwardLeft)
for k in 1 2 3 4 5 6 7; do
  c=${DIRS[$k]}
  MOVES="$MOVES ROCKET$k=$c%rocket MOVE$k=$c%torso MOVE${k}Sway=$c MOVE2_$k=2_$c%torso MOVE2_${k}Sway=2_$c"
done
python3 kf2_to_ifp.py --root-motion stride --phase-to-sa --fill-from-ped --block MPS --out $A/MP_BODY.IFP --clips \
  IDLE_stance=Stand%still WALK_player=Walk run_player=Run run_1armed=Run \
  IDLE_armed=2_Stand%still WALK_armed=2_Walk run_armed=2_Run \
  GUNMOVE_FWD=Run GUNMOVE_L=StrafeLeft%torso GUNMOVE_R=StrafeRight%torso GUNMOVE_BWD=WalkBackward%torso \
  IDLE_stanceLook=Stand IDLE_armedLook=2_Stand \
  GUNMOVE_LSway=StrafeLeft GUNMOVE_RSway=StrafeRight GUNMOVE_BWDSway=WalkBackward \
  TURN_L=2_TurnLeft TURN_R=2_TurnRight TURN1_L=TurnLeft TURN1_R=TurnRight \
  ROCKET_IDLE=Stand%rocket ROCKET_WALK=Walk%rocket ROCKET_RUN=Run%rocket $MOVES
# Variants (*Look = Max's full idle with look-around, *Sway = original torso) are picked by the
# ASI menu toggles; the plain names are the defaults.
