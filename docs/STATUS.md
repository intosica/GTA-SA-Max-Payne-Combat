# Status — 2026-11-13 (retarget fixed)

## Verdict

**Working test build.** Arm/weapon retarget fixed (root causes in
`RESEARCH.md §9`); user confirmed in-game "looks a lot better". Remaining polish
is gameplay integration, not the rig.

## What works

- MP PC assets persistent at `/media/montel/Extreme SSD/Games/Max Payne PC/{game,extract}`.
- `tools/build_ifps.sh` regenerates all four IFPs in ~6 s; `tools/validate_ifp.py`
  passes (barrel forward on every aim/shoot frame, upright, no quat flips).
- MPC (20 clips), MPU (20), MPA (9), MPD (4) all load in-game (`maxpayne_combat.log`).
- Guns point forward with the top up for pistols, dual Berettas, MP5, shotgun, rifle, sniper.
- Menu (`Select+B`), manual shotgun magazine, F6/F7/F8 test keys.

## Approach since 2026-11-15: replace, don't layer

Layering MP partial clips over SA's running anims made the two idles/walks fight
(gliding, gyrating legs). Now `assets/MP_BODY.IFP` (block MPS) holds full-body MP clips
named after the SA player clips they replace (IDLE_stance, WALK_player, run_player,
run_1armed, IDLE_armed, WALK_armed, run_armed, GUNMOVE_FWD/L/R/BWD). The ASI re-points
SA's static associations at them (`ApplyBody`), so SA's own state machine plays Max.
Root travel is baked per frame from the planted foot (`kf2_to_ifp.py --root-motion
stride`), so SA's root-motion movement matches the feet. `TraceClip` logs every
dominant clip change (`clip ped#N` = ped.ifp index) to find any SA clip still showing.
Old overlays: menu "OLD SHOOT OVERLAYS" (default off).

## 2026-11-15 18:05 — lost to a test-harness bug, no gameplay change

Nothing in the locomotion was changed this session: the only edit was logging (a `Dominant()` helper
plus `raw=`/`fps=`/`tn=` fields on the `mv` trace line). It was built and installed but never seen
in-world, because hands-free loading silently broke: `gamescopectl screenshot` returned rc=0 before
the file existed and the file then took 8-10 s, so `tools/autostart.py` judged pre-key frames,
re-pressed keys and wandered the front end. I misread the resulting blank log (no `tick` lines) as an
infinite load and spent the session on a bogus A/B of the build. `Tick` only runs in-world
(`Events::gameProcessEvent`), so an empty log is not evidence of a hang — the game was at the main
menu. Keyboard input was never the problem: `xdotool key Down` does move the menu highlight.
Installed vs source mismatch: the source is instrumented, the game root has the pre-instrumentation
`/tmp/asiA` build — rebuild+install before any test that expects the new fields. Full write-up and
the recovery plan are in `../../AGENT-HANDOFF.md` §0m (2026-11-15 18:05).

## Just changed, needs in-game confirmation

- Blend-in delta 0.25 → 8.0 (`kBlendIn`) and crossfaded fade-out with auto-remove:
  the slow blend left the native pose visible for seconds (user saw arms lift on
  rifle/shotgun switch, and walking while aiming/shooting).
- Removed the 350 ms weapon-switch gap (native rifle-hold pose showed during it).
- In-place clips keep only vertical root motion (`--root-motion vertical`).

## Unfinished

- Moving while shooting: full-body clips replace the legs; needs MPU/MPA overlays
  wired into the state machine (they now export correctly but are only used by name).
- Dodges: only forward is triggered by the ASI; others via menu.
- Enemy “AI”: stationary animation override only.

## Test

See `TESTING.md`. Save: **Load Game → second "Big Smoke" entry** (already armed).

## In-game verification (2026-11-13, second Big Smoke save)

- Aim + fire (pistol): CJ stays in place; old builds drifted backward because in-place
  clips carried MP's horizontal pelvis offset as root translation (now vertical only).
- Weapon switch pistol→shotgun→AK→rifle: MP pose within 0.25 s, no native arm-lift.
  Cause was blendDelta 0.25 (~4 s fade) plus a 350 ms gap after switching.
- Strafing while aiming/firing: MPU upper-body clip plays as partial (flag 0x10) over SA
  strafe legs. IsMoving threshold fixed (m_vecMoveSpeed is per 1/50 s; old 0.04 never triggered).

### 2026-11-15 05:05 — foot phase alignment + Blender viewer
- Verified in Blender (tools/blender_view.py + tools/dump_pose.py, build/poses/*.json) and
  tools/plot_pose.py (`Walk:WALK_player --frames ... --azim 0`) that MP_BODY clips match MP's
  source frame-for-frame on CJ. Blender is Flatpak: its /tmp is private, keep files on the SSD.
- Gyrating feet cause: SA cross-blends loco clips at equal normalized time; MP cycles were out
  of step with SA's (GUNMOVE_BWD ~0.5 cycle off L/R). kf2_to_ifp.py `--phase-to-sa` rolls each
  stride cycle so left-foot-forward matches the SA clip it replaces (now within 0.03).
- Face fix (--fill-from-ped) installed with this build.

### 2026-11-15 05:35 — stride travel, stop/start loop, MP controls
- Short-step bug: bake_stride guessed travel direction from mean foot motion; in-place MP
  cycles came out walking BACKWARD (WALK_player -1.43 m) or zero (run, GUNMOVE_FWD/R/BWD).
  SA's walk/run logic then saw ~no forward speed and looped WALK_start <-> walk every step
  (log: ped#274 id=5 alternating with MPS#1). Rewritten: travel = reversed slide of the planted
  foot, per frame. Now walk 1.09 m/s, run 2.5, GUNMOVE L/R 2.5, BWD 1.75; planted-foot slip <1 mm/f.
  validate_ifp.py now checks MP_BODY root travel against the SA clip's direction.
- MP controls (menu "MAX PAYNE CONTROLS", default ON): armed on foot, the ASI holds SA's target
  input right after CPad::UpdatePads (call 0x53BEE6 redirected) so CJ always faces the crosshair
  and the stick strafes/backpedals with Max's GUNMOVE clips (as Max Payne Xbox does). Off when
  sprinting (Cross), entering a car (Triangle, 2 s), sniper, menu open, toward-viewer if
  NATIVE BACKWARD WALK. GUNMOVE_FWD now uses Max's Run (MP moves at run speed while aiming).
- Fire-walk diagnostics: "fire:" log lines (stick, target, aim, weapon state, speed, move state).

### 2026-11-15 06:15 — task 1 of the user's list: head tracking off (SA HEAD TRACKING menu toggle)
- User report: indoors (upstairs at CJ's house) the head snapped in many directions and the legs
  swayed/gyrated with it. User confirmed MP CONTROLS (always face crosshair) feels like Max Payne.
- SA moves CJ's head outside the animations in two ways:
  1. CPlayerPed::ProcessControl calls CPed::SetLookFlag(camera heading) (call at 0x60F459)
     whenever the camera is >30 deg off his facing. Indoors the camera collides with walls and
     jumps, so the head chases it. Hook: SetLookFlagHook -> ClearLookFlag (0x5E1950) for the player.
  2. IKChainManager look-at tasks (g_ikChainMan 0xC15448: "InterestingEntities" random peds/objects,
     TaskUseGun, ProcPlyrWeapon). Blocked with CPed::bDontAcceptIKLookAts + AbortLookAt (0x618280)
     when IsLooking (0x6181A0), every tick.
- Menu: "SA HEAD TRACKING" (default OFF = Max's animation drives the head).
- Untested in game. Also noted: CPedIK::PitchForSlope (0x5FE0E0) rotates thighs/calves/feet on
  slopes/stairs; if leg sway remains ONLY on stairs/slopes, that is the next suspect.

### 2026-11-15 13:45 — head right/left/right after stopping: it was Max's own idle
- User test of SA HEAD TRACKING off: less erratic, but after one forward step the head goes
  right, left, arcs right, back left, and the feet drift out of place.
- Cause: MP's Stand.kf2 (used for IDLE_stance, the pistol/unarmed idle) is a 14.7 s idle where
  Max looks around (head yaw -9 -> +41 @1.5 s -> -47 @2 s -> +39 @4 s ...) and shuffles his whole
  body ~25 cm sideways with ~22 deg pelvis yaw. SA restarts the idle at 0 on every stop.
- Fix (kf2_to_ifp.py, MP_BODY/stride mode only): Stand clips hold pelvis, neck, head, legs at
  frame 0; spine/arms keep the breathing. Feet now 0 cm drift, head yaw -10..-1 deg.

### 2026-11-15 14:05 — freeze at end of load bar (environment, not the mod — unconfirmed)
- After the agent's close.sh killed a session mid-load, two launches froze at the end of the load
  bar (music playing, Tick never ran: no "tick"/"load" lines after "maxpayne_combat loaded").
  Nothing in the mod changed between the working 13:35 session and these (same ASI md5; the only
  new file, MP_BODY.IFP, is loaded by Tick, which never ran). Saves/gta_sa.set untouched.
- Found 16 orphaned winedevice.exe (~/.wine-gtasa, no wineserver, up to 11 h old) each holding
  the gamepad's hidraw/input-event nodes. Killed them; launch.sh now kills winedevice.exe of its
  own WINEPREFIX on start. If the freeze persists: rename maxpayne_combat.asi to .off and retest.

### 2026-11-15 14:45 — idle look-around toggle, still torso toggles, mouse aim lock
- User (after test): head fix good; Max's right/left/arc look-around after stopping (any
  direction) IS 1:1 Max Payne -> keep suppressed by default, add toggle. Max barely moves his
  upper torso when strafing or backpedalling -> still torso, with separate toggles. Mouse locked
  CJ into aim (forced target input). Camera "spring" when the right stick is released is liked:
  keep it as an option when crosshair movement replaces the camera (source not yet identified;
  the mod does not touch the camera).
- MP_BODY now has variants (kf2_to_ifp.py clip modes `%still`, `%torso`):
  IDLE_stance/IDLE_armed = still idle; IDLE_*Look = Max's full idle. GUNMOVE_L/R/BWD = torso held
  at Stand frame 0 facing the root (chest yaw range 0); GUNMOVE_*Sway = Max's original torso
  (StrafeLeft chest swings 55 deg). ApplyBody re-points SA's slots at whichever variant the menu
  picks (WantClip), switching live.
- Menu: MAX IDLE LOOK-AROUND (off), STILL TORSO SIDESTEP (on), STILL TORSO BACKPEDAL (on).
- Mouse: MP controls only force SA's target input while the Xbox pad was used last (XInput
  state vs mouse buttons/motion and WASD/Space/Shift). Log lines "input: pad" / "input: mouse/keyboard".
- NATIVE BACKWARD WALK off + MP CONTROLS on: stick toward the camera = forced aim -> Max's
  backpedal (GUNMOVE_BWD). Unarmed backpedal still SA (not done).

### 2026-11-15 15:30 — Max Payne free walk (no pivot), NATIVE BACKWARD WALK removed
- User (m01096): in MP, walking left/back keeps Max facing away from the camera; SA pivots CJ to
  face the move direction. Wanted MP sidestep/backpedal, no full-body pivot; remove the broken
  NATIVE BACKWARD WALK option (removed from code and menu).
- MP_BODY.IFP now has MOVE<k>/MOVE2_<k> (+Sway) clips, k=1..7 clockwise from fwd-right
  (ForwardRight, StrafeRight, BackwardRight, WalkBackward, BackwardLeft, StrafeLeft, ForwardLeft;
  2_ = two-handed), still torso unless the STILL TORSO toggles are off. Root travel goes the
  clip's way (sideways/back), which SA applies in ped space.
- ASI (MAX PAYNE CONTROLS on, on foot, not aiming/sprinting/ducking): pad hook reads the stick
  relative to the camera (same maths as PlayerControlZelda), picks k with 10 deg hysteresis,
  zeroes m_fHeadingChangeRate so SA stops turning CJ, re-points the walk/run/WALK_start slots
  to MOVE<k> (running anims re-created at the same normalized time), and FaceCamera turns CJ to
  the camera heading at 10 rad/s. Log: "freewalk on/off dir=k".
- Untested in game. Angle mapping unit-checked (fwd 0, right 2, back 4, left 6).

### 2026-11-15 16:10 — why the pivot never went away, and the real fix
- User: "character still pivots no matter what". Log had ZERO "freewalk" lines: free walk never
  switched on. Every "fire:" line shows stick=0,0 even while moving — GInputSA feeds the Xbox pad
  through CPad's getters, CPad::NewState.LeftStick stays 0. Lesson: read the pad via
  GetPedWalkLeftRight 0x53FC90 / GetPedWalkUpDown 0x53FD30 / GetTarget 0x540670, never NewState.
  (ForceAim also writes NewState.RightShoulder1 — may not reach GInput's GetTarget; check.)
- Pivot mechanism: PlayerControlZelda sets m_fHeadingGoal = stick world angle; the only turner is
  CPed::CalculateNewVelocity 0x5E4C50 (reads m_fHeadingChangeRate 0x560), sole call 0x5E92ED in
  CPed::ProcessControl. New hook there: while free-walking, goal = camera heading. Removed the
  rate-zeroing and the per-tick SetHeading (FaceCamera).
- Menu labels now state values: MOVEMENT: MAX PAYNE/SA, STRAFE TORSO: STILL/MAX SWAY,
  BACKPEDAL TORSO: STILL/MAX SWAY, IDLE HEAD: MAX LOOK-AROUND/STILL.

### 2026-11-15 16:40 — stop-motion strafe, curving sidesteps, backpedal feel
- User: anim looks stop-motion (worst when strafing), strafe camera/position broken, backpedal
  not Max-like. Log confirmed free walk now switches on (freewalk on dir=6/4/2...).
- Cause 1 (stop motion): bake_stride moved the root by the planted foot each frame -> speed
  0..0.14 m/frame with a dead stop at each foot switch (SA run_player: steady 0.18-0.20).
  Fix: per-cycle travel unchanged, spread evenly (MOVE2 now 0.082-0.083 m/frame). Feet may slide
  slightly within a step.
- Cause 2 (curving): heading goal was the live camera heading; SA's follow cam trails CJ, so a
  sidestep swings it and CJ circled. Now g_walkHeading is locked at free-walk start and only
  follows camera turns while the right stick (XInput sThumbRX > 7000) or mouse X moves.
- Cause 3 (pops): RefreshRunning cut the old direction clip (delta -1000); now fades it out
  (delta -6, ~0.15 s). Untested in game.

### 2026-11-15 16:50 — automated test (agent-driven boot + xdotool WASD), real bug found
- Testing loop: `tools/autostart.py` (boot to save), xdotool keydown a/d/s/w 3 s each,
  `tools/autostart.py --quit` (force kill, user 2026-11-15: no menu quit). ASI now logs
  "mv <dir> x y h= goal= cam= dt=" every free-walk frame; analysis script idea: per-frame
  displacement split into CJ forward/right axes.
- Found: sidesteps moved ZERO sideways (right component 0.000, 0.003 m/frame forward drift):
  SA only extracts a clip's X root travel when the running anim has
  ANIMATION_CAN_EXTRACT_X_VELOCITY (0x80; gta-reversed RpAnimBlend.cpp). The walk/run slots
  lack it (GUNMOVE_L/R have it). Fix: ExtractSideways() sets 0x80 on every running MP_BODY anim
  each tick; RefreshRunning no longer copies the old anim's 0x80. Repoint's player-group test
  now also matches a walk slot that currently holds any MP_BODY clip (InBody).
- After fix (log): left 0.088 m/frame, right 0.082, back 0.058, fwd 0.085 — constant every frame,
  heading range 0.0 deg, camera range 0.0 deg (no curving). Stops in the trace were the save's
  kitchen walls (screenshot). Game runs ~30 fps (dt 0.033).

## 2026-11-15 19:15 — strafing/backpedal wrong-clip bug fixed
- run_1armed slot (no anim id unarmed) was re-pointed unfiltered and stole WALK_start -> MP forward Run
  played for a whole cycle on strafe/backpedal starts. Skipped now.
- Swap moved before the ped update (UpdatePadsHook) so SA's WALK_start no longer fades over Max's clip.
- Verified by log: correct MOVE clip from the first frame in all directions. Needs an eye test.

## 2026-11-15 20:10 — user's F9 session analysed; 5 changes (needs eye test)
Findings from the user's F9 session log: (a) empty "as" line 1-5 frames after clip changes = bind pose = horizontal flash;
(b) leftover MOVE clips at 0.4-0.7 blend next to the new one after sector flips (stutter, mixed arm swing);
(c) frozen runs: position + tn stuck for 24-82 frames while a direction is held;
(d) partial MP_COMBAT_UPPER "Stand" (p362) overrode spine/neck/head/R arm with a frozen pose while L arm followed the MOVE clip = mismatched arms.
Changes (asi/maxpayne_combat.cpp): EnsureFullBlend (tops weights up to 1), KillStrays (fades non-current MP clips + SA WALK_start while free-walking,
runs in UpdatePadsHook too), Stand overlay stopped while free-walking, ExtractSideways also sets ANIMATION_STARTED (0x1), lunge clamp (>0.12 m/frame ->0.09, logs "lunge clamp").
NOTE: "jump" in the pose log is distance since the last free-walk frame, not per frame -> the 0.36 "teleport" there is mostly an artefact; real lunges are the "lunge clamp" lines.
NOTE: F9 toggles autoPlayer in the mod as well. Stick magnitude: keyboard is always full deflection; pad is analog (GetPedWalkLeftRight/UpDown), threshold 30 in FreeWalk.

## 2026-11-15 20:50 — frozen arms = the "still" flag
g_stillSide/g_stillBack (asi/maxpayne_combat.cpp:37-38) picked the non-Sway MOVE clips, whose arm/Spine1/Head tracks have 0 deg range (frozen arms/torso).
Set both false -> MOVE*Sway (arms 30-50 deg swing). Lunge clamps in a test run dropped 276 -> 3. User wants: 1:1 Max logic first, subtract strafe/back logic later;
right-stick camera currently feels good because camera follows head tracking (happy accident) - keep; looking up with right stick is broken (TODO).

## 2026-11-15 21:10 — crossfade fix + Max Payne camera facts
- RefreshRunning (asi/maxpayne_combat.cpp): new MOVE assoc now starts at blend 0 / delta +6 (was a copy of the old 1.0, which SA's list-order weighting made a 1-frame pop on every sector change). tools/jolt.py <seed> = random vigorous keys + per-frame foot/head height jump metric: spikes >0.35 per run 19->11 / 16->7 (seeds 3/4). Metric is crude (normal run swing ~0.2).
- Tried and reverted: ramping in SA's own blend-1.0 walk/start assoc in KillStrays (no gain, more "blend fix").
- Native MP right stick (from payne-recomp-main/docs/mouse-look.md, lifted Xbox code): right stick = camera yaw/pitch RATE, shaped rate=(1.5*s+0.5)*(0.8*x)^3 (half deflection = 1/8 speed), max ~0.96*pi rad/s ~173 deg/s; per-frame turn = Look*pi*dt written at input object +0x25 (yaw) / +0x29 (pitch). Auto-aim overrides look when a target is near and stick < ~85%. Where yaw is finally stored: not found. No data on the step-vs-glide turn behaviour.

## 2026-11-15 21:30 — flash fix + native MP trace attempt
- FreeWalk (asi/maxpayne_combat.cpp ~590): 5-frame grace when the stick dips through centre while free-walking; removed the on/off/on bounce that preceded every "--- pose" flat-skeleton frame (user's F9 trace showed 3 of them, all right after 'freewalk off' -> 'freewalk on'). Test run: 0 flat frames, 0 bounces; spikes >0.35 per jolt.py run ~10.
- Native MP trace: added payne-recomp-main/tools/mp_run_turn.sh (copy of mp_run.sh, scripted rstick turns standing + walking; output tools/mp_trace/turn1/player.bin). It runs headless-scripted (no user needed, ~100 s) BUT the traced 0x600-byte window around the "input object" (sub_0002E6D0 this) has only ~25 changing dwords and none look like yaw/position (same as the older probe8). The window is not the player's locomotion struct, or the script never reaches gameplay. Next: find the player's heading/position field (diff a memory dump idle vs after rstick_right), or screenshot to verify the run reaches the tutorial.

## 2026-11-15 21:45 — native MP right-stick trace (play1) 
payne-recomp-main/tools/mp_play.sh <name> = user plays native MP; records pad (pad.rec) + player.bin. play1 pad timeline (ms since launch): rstick_right 60649+3100, rstick_left 65183+4016 (both sustained spins),
taps rstick_left 150/120/133 @70549.., rstick_right 134/120/120 @72649.., then walking+turning @88333-95600.
USER OBSERVATION (m00496): starting a mission and SPINNING (sustained right stick) triggers the stepping-feet turn animation; TAPS do not (glide). => trigger depends on hold duration / accumulated turn, not just stick presence.
player.bin window is NOT a usable locomotion struct (garbage floats/NaN: looks like stack, `this` of sub_0002E6D0 isn't the player; activity bursts do line up with stick-active periods). Decoding the real trigger would need a proper player-struct find (memory diff) - judged expensive.

## 2026-11-15 22:10 — stand-still turning (native MP behaviour)
Max has real TurnLeft/TurnRight(+2_) KF2 clips (data/database/skeletons/default_skeleton/anim). Added to MP_BODY as TURN_L/TURN_R (2_) and TURN1_L/TURN1_R (unarmed) in tools/build_ifps.sh (appended before $MOVES, so MOVE clip indices shift +4; code uses names).
asi: StandTurn() (called after FreeWalk in UpdatePadsHook): standing, not moving, right stick/mouse X -> heading follows camera yaw delta (g_turnGoal via CalcVelocityHook); held > kTurnStep=0.35 s -> plays TURN_L/R, else glide (taps). Tunable: kTurnStep. Untested by eye: with a pad.

## 2026-11-15 22:45 — "hiking up" fix + OPEN ISSUE: frame limiter
HIKING UP: log of the user's F9 run showed full-body weights summing to ~2 during sector flips (M45 0.92 + M45 0.08 + M2 0.91 + M1 0.06 = 1.97). SA adds the pelvis translation of each assoc, so the model rose (pel height up to 0.19 vs normal ~0.07).
Fix: EnsureFullBlend (asi/maxpayne_combat.cpp) now also takes the excess back from fading assocs so non-partial weights total 1. Test (500 fps): max weight sum 1.01, 0 frames >1.05 of 12021; pelvis max 0.10. Not seen by eye.
Also: FreeWalk "grace" for stick dips is now wall-clock (170 ms) instead of 5 frames (was 10 ms at 500 fps).

OPEN ISSUE (bookmark, not fixed): FRAME LIMITER. User: after one of the movement-improvement sessions (suspects: the per-frame blend/swap/lunge-clamp logic, StandTurn, KillStrays; started around the 'EnsureFullBlend/KillStrays/crossfade' work of 2026-11-15 evening),
San Andreas runs badly with COSMIC's Frame Limiter ON (game locked ~30 fps in our logs, dt 0.033) and smooth with it OFF (we then see 200-500 fps, dt 0.002-0.005). User can turn it off on a 60 Hz TV; a 120 Hz monitor / other users may not.
Known per-frame (not per-second) things in asi/maxpayne_combat.cpp that behave differently by fps: lunge clamp (>0.12 m per frame -> 0.09; never fires at 500 fps), ExtractSideways/ApplyBody every frame, blend deltas (per-second, OK), clips are 30 fps keys (stop-motion at 30 fps is inherent: 24 clip-ticks per cycle).
Hypotheses to test next time: (1) compare jolt.py at 30 fps vs uncapped, (2) 30 fps cap + 120 Hz display = 4:1 repeat judder (not ours), (3) anything keyed on frame count. Needs a limiter toggle in quickload (SA setting is in gta_sa.set / launch args; user toggles it in COSMIC).

## 2026-11-15 23:00 — crash hunt (unresolved)
User: game crashed twice (limiter on and off) after F9 sessions. Reproduced once at 30 fps (jolt seed 11, then seed 15 ended with game gone).
Both crash logs end the same way: ... 'freewalk on dir=0|4', 'blend fix: sum=0.00 -> top up p274' (only SA's WALK_start left, blend 0), 1 more frame, then log stops.
Suspects: EnsureFullBlend topping up p274 right after KillStrays gave it delta -100 + AUTO_REMOVE (SA frees it next update); ApplyBody swap freeing assocs held in Slots. Not confirmed; no crash dump (WINEDEBUG=+seh makes game too slow to reach in-world).
Also: tools/jolt.py log-offset bug fixed (read bytes). Random keys can open SA's save/pause menus: jolt.py then measures nothing; user also saw "--- pose" on first forward walk after load (FLAG pose jump 0.26-0.36 at 'freewalk on' with MPS#1 id=0 after 'clip ped#274').

## 2026-11-15 23:15 — crash suspects tested (both fixes already in asi, built 23:02)
Fix 1 (EnsureFullBlend skips assocs KillStrays just killed, delta < -50) + fix 2 (KillStrays leaves SA WALK_start alone until a Max clip is playing, `mpPlaying`) .
Test: close.sh, quickload slot 2 (26 s to in-world), walk forward w:1.2 s + w:1.5 s (no exit reached), then tools/jolt.py 21 (random keys, 548 frames, 30 fps): game alive after, 0 "top up p274" lines (crash signature), 0 unimplemented/SEH lines in /tmp/gta-sa.log.
Initial walk analysis (log): first forward step = SA WALK_start p274 at weight 1.0 for 8 frames (tn 0 -> 0.75) while Max MPS#1 is created; swap at 'clip MPS#1' 8 frames later, then 0.04-0.05 s crossfade M1->M2 (6 frames). Z steady 0.055 m/frame, no lunge clamp, no blend fix, pelvis -0.03..0.06. Floppy never reached. Screenshots via scrot are black under gamescope (can't verify it visually).
Caveat: one clean run is not proof; blend-fix top ups still happen (M4xx clips, 3k lines total in log).

## 23:45 jolt fix
Direction-change hard cut: SA snapped new MOVE assoc to 1.0 in one frame. Fix: HoldFadeIn ramps it +6/s (4-frame crossfade, verified in log). jolt.py seed 21: >0.35 spikes 21 -> 12, >0.5: 3, no crash. Remaining: foot swaps, lunge clamps. Next: right-stick pitch rate + radial deadzone.

## 2026-11-15 foot-swap spikes
Cause: SA starts/duplicates/re-times MOVE assocs itself (new clip at t=0 while partner at 0.49, M25 0.95->0.25, dup assocs at random time), and MOVE clips are NOT foot-aligned at equal tn (strike phase differs 0.04-0.5 between clips, esp. back/strafe) -> crossfade averages opposite feet.
Fix (asi/maxpayne_combat.cpp): `kPhase[]` (tools/phase_table.py = left-foot strike time per MP_BODY clip), `PhaseOf`, `PhaseLock` (one shared phase advanced by dt*speed; every loco clip forced to phase+its strike each Tick); RefreshRunning carries phase too; HoldFadeIn never clamps below 1-(fading blends). jolt.py >0.35 spikes: seed21 21->6, seed4 2, seed3 5; max pop 1.1->0.5; no crash. Not compared against the payne-recomp (Max has no such issue by design: single skeleton state machine). Remaining spikes: stick-direction flips (legit pose change). Rebuild kPhase if MP_BODY.IFP order changes.

## 2026-11-16 00:50 — RPG baseline: Max's legs + SA rocket carry
- Cause of "everything walks forward" with the RPG: ped group 57 (IDLE_ROCKET #193, run_rocket #238, WALK_rocket #272, WALK_start_rocket #277) wasn't in kSwap, so free-walk directions never reached it.
- Fix: MP_BODY gets ROCKET1..7 (MOVEk legs + IDLE_ROCKET frame-0 torso/arms, `%rocket` mode in tools/kf2_to_ifp.py), ROCKET_IDLE/WALK/RUN; kSwap + WantClip map the four rocket slots; kPhase regenerated.
- Test (slot 2 save with RPG): all 8 dirs play ROCKET clips (MPS#23/28/38/48/53 for dirs 1/2/4/6/7), idle = MPS#20, no crash/HANG. jolt seed 21: 13 spikes >0.35 over 4552 frames (max 0.66) — a bit above the non-RPG 6; not yet investigated. Aim/fire untouched (not retested). Visual check by user pending.

## 2026-11-16 01:05 — pose inertialization ("Max smoothness")
User (m00334): jolts/instant pose changes still everywhere; wants Max Payne's smoothness for the player in SA.
Root idea: every pop is a pose discontinuity (SA swaps/restarts walk assocs in one frame, our ApplyBody/PhaseLock re-times clips, partial layers start at full weight). Instead of chasing each source, `Inertialize` (asi/maxpayne_combat.cpp) runs right after SA blends CJ's pose (`patch::RedirectCall(0x535F94, UpdateAnimationsHook)` = the RpAnimBlendClumpUpdateAnimations call in CEntity::UpdateAnim). When the anim state jumps (`AnimJumped`: weighted assoc added/removed, blend step > 10/s, time jump > 0.08 normalized) or a bone turns > 0.6 rad at > 35 rad/s in one frame, each bone keeps offset = shown * inverse(new) (rot + translation), decaying with half-life `kInertiaHalfLife` 0.07 s. Steady animation is untouched (offset is identity).
Menu: SMOOTH TRANSITIONS (g_inertia, default ON) to A/B.
Results (RPG save, slot 2): jolt seeds 21/3/4/11 -> 0 spikes >0.35 (before: 13 on seed 21), max per-frame 0.07-0.14 (before 0.66). No crash/HANG. Needs eye test: watch for "swimmy" lag on fast direction flips (raise/lower kInertiaHalfLife).

## 2026-11-16 01:15 — CPU cost check (user: is the smoothing eating CPU -> limiter fps drops / hospital freeze?)
- Measured (QueryPerformanceCounter around Tick + UpdatePadsHook + Inertialize; `tick ... perf mod=` line once a second): ~150 us/frame incl. logging, ~62 ms per second at ~420 fps. At 30 fps limiter = ~0.15 ms of a 33 ms frame (<0.5%). Not the cause of limiter drops; inertialization (fixed 32 bones) didn't exist yet when those were seen.
- Freeze candidate found + fixed: WrapPi was two while-loops; a huge/garbage angle (|a| > ~1e8 or inf/NaN, e.g. camera during respawn) never terminates (a - 2pi == a) = main thread 100% CPU, matches the hospital hang signature. Now `remainderf` + isfinite guard. Unconfirmed it was THE cause (no HANG dump from that freeze).

## 2026-11-16 ~01:55 — RPG/Bazooka sprint + jump (user: "working pretty good"; session ended early, no log written by that agent)
Reconstructed from the code (asi/maxpayne_combat.cpp, built 01:53) and the previous entry:
- Cause: rocket launchers (weapon ids 35 RPG / 36 heat-seeker) are HEAVY in weapon.dat (flags 48214, 3rd digit 2). SA then skips the whole
  sprint branch (TaskSimplePlayerOnFoot::PlayerControlZelda) and blocks jumping, so Cross did nothing with the RPG.
- Fix: Tick clears `CWeaponInfo::m_nFlags.bHeavy` for weapons 35/36 every tick (`GetWeaponInfo` 0x743C60, skill STD). Sprint then uses SA's
  group-57 sprint slot, which still points at `run_rocket` -> our swap maps it to `ROCKET_RUN` (Max's Run legs + IDLE_ROCKET torso, `%rocket` mode in tools/kf2_to_ifp.py).
  Our swap had moved slot 1 only; slot 2 is the sprint. Jumping with the RPG works too (Max jumps with anything).
- ForceAim (MAX PAYNE CONTROLS) already yields to SA while Cross (sprint) is held; FreeWalk is not eligible when `m_nMoveState == PEDMOVE_SPRINT`.
- The `mv` trace line now also logs while sprinting (`g_moveActive || PEDMOVE_SPRINT`), so sprint can be inspected with tools/mvstats.py.
- tools/build_ifps.sh + tools/kf2_to_ifp.py + tools/phase_table.py touched 00:40 (ROCKET clips + kPhase regeneration); MP_BODY.IFP rebuilt 01:53.
- Status: user-confirmed by eye ("pretty good"). Not verified: heat-seeker (36), fire/aim while sprinting, other heavy weapons (flamethrower/minigun etc. keep the SA flag).
- Known leftovers: RPG jolt seed 21 earlier showed 13 spikes (now 0 with inertialization); frame-limiter issue and crash hunt remain open (see above).

## 2026-11-16 ~02:30 — Mouse shooting walks CJ forward: gone with the mod DISABLED (cause NOT proven)

- Symptom (pad connected, playing with mouse+keyboard): LMB/RMB shooting made CJ walk forward / toward camera. User suspected GInput, said "100% not the Max Payne mod".
- Facts: installed `GInputSA.asi` is the ORIGINAL 2016 build (md5 d2d20ee0b4e998ce086ebafb743f2f98), not a build of `mod creation tools/GinputNext-main/` (source only, never compiled/installed). `GInputSA.ini` untouched (ControlsSet=2, FreeAim=1).
- Test: killed game, renamed `maxpayne_combat.asi` -> `maxpayne_combat.asi.off` (game root), relaunched. User then reported: "shooting in game works now with mouse". No other file changed in the game root since 01:50 (only the rename).
- Conclusion so far: the only variable was the MP mod being off, so the bug most likely comes from the mod (not GInput). Candidates: ForceAim writes `NewState.RightShoulder1` (asi/maxpayne_combat.cpp ~586); leftover experiment force-writing MOUSE3RD=0 on right-stick input; FreeWalk driving movement. A stalled/stale session (game had hung since 01:57) is also possible — NOT excluded.
- TODO: restore `maxpayne_combat.asi.off` -> `maxpayne_combat.asi`, re-test mouse shooting; if it walks forward again, bisect those three writes.
- Tooling note: with the mod off, `tools/quickload.py` cannot work (autoload + "in world" come from the mod's log); `tools/autostart.py` got stuck at a black CLEO screen pressing Return. Test vanilla by hand.

### 2026-11-16 ~02:50 — retest WITH the mod on (pistol save): walk-forward NOT reproduced
- Restored `maxpayne_combat.asi`; quickload slot 2 (user re-saved with pistol default); pad js0 is connected. xdotool LMB hold, RMB+LMB, with mouse X motion: `fire:` lines show speed=0.000,0.000 movestate=1 (stopped), no `mv` lines, no `freewalk` lines -> CJ did not walk. (The `mv d0 fwd=+0.05` rows mvstats shows are stale rows from an older session, not this test.)
- So "works with mod off" was likely NOT the mod: the old failing state is not reproducible now. Suspects: (a) previous save's weapon (RPG/bazooka work that session: bHeavy cleared for ids 35/36), (b) the hung 01:57 session, (c) real pad/mouse timing that xdotool can't mimic.
- Installed asi (01:53) still contains a debug `lmbprobe` log line (per-frame walk/LS/RS/R1/L1/CI/SQ/CR/mode) that the source (edited 01:56, probe removed) no longer has: rebuild with ./build.sh && tools/quickload.py --install to sync (log volume: ~1500 lmbprobe lines per 40 s).
- ForceAim (asi/maxpayne_combat.cpp ~584) only writes RightShoulder1 when PadInUse(); LMB/RMB/mouse motion flip PadInUse false so mouse play is not touched. MOUSE3RD force-write no longer exists in source.
- NEXT: user to reproduce by hand and tell weapon + exact buttons; then run with log on.

### 2026-11-16 ~03:00 — mouse-shooting walk-forward: treated as RESOLVED (user: most likely fixed by the previous agent before the cut-off session)
- Not reproduced with the mod on (see entry above). The previous agent was debugging exactly this (the `lmbprobe` log line in the installed asi) and its last source edit (01:56, probe removed) likely contained the fix; the exact change is unknown (no git). Reopen if it returns: note weapon + buttons, check `fire:`/`freewalk` log lines.
- Installed asi (01:53) is older than the source (01:56): rebuild/install to sync.

## 2026-11-17 — CAMERA REQUIREMENT (user, restated; was not recorded before)
- Current camera: right stick cannot look up/down (pitch broken), camera feels "locked to head/body" -> immersive happy accident.
- Goal THIS session: fix pitch, and add a menu choice of 3 camera modes: GTA SA (stock), MAX PAYNE (Xbox/PC traced rates), CURRENT (the happy-accident locked feel).
- Menu: pauses (m_CodePause) via CHud::Draw hook 0x53E4FF, opaque, plays max_theme.wav while open. Fixed double MenuInput per frame (open/close flicker).

## 2026-11-17 camera direction (user m00852)
- Orbit-cam experiment REVERTED: too close/generic, didn't fix strafe jitter. No more patching SA's cameras (re-centre, yaw-lock, pitch offset are band-aids; CURRENT mode still carries them).
- New path: port Max Payne's own camera (PC mouse + console stick) from the traced binaries, then offer it on pad ("PC Max Payne camera on controller"). Next: find consumer of controller +0x19/+0x1d deltas (PC 0x4cecd0) = player/camera update in MaxPayne.exe.
- Test tooling kept: tools/vpad.py (virtual pad; --serve before launching game, --send), tools/padtest.py, mp_cammode.txt / mp_trace.txt hooks (remove when done).

## 2026-11-17 04:30 — Max Payne camera v1 built, reverted on user order; the revert is WORSE (user m01239)
- Built `MPCam()` (mode 1 = MAX PAYNE): own accumulating yaw/pitch, Xbox `1.024*pi rad/s` stick rate, mouse
  `0.0025 rad/count`, rigid orbit about `ped + 0.62`, LOS pull-in (`0x56BA00`), 0.25 s resume blend.
  Fixed the two symptoms that matter (strafe/back-diag jitter gone, pitch free), but broke the aim->camera
  transition (adopted SA's aim cam front, `fz=-0.992`, camera landed above CJ's head) and the framing read
  "too close, generic" (MP offsets were invented: MP's real offset is runtime `obj+0x30`, not a constant).
- User: "revert what ever you just implemented ... no more edits. This is a reversion." Done faithfully:
  `asi/maxpayne_combat.cpp` camera block is byte-for-byte the pre-session text (placeholder mode 1, mode 2,
  yaw-lock / lateral re-centre / `g_pitchOfs` band-aids, `g_camT/g_camPed` + the `camt` log line), the
  `CColPoint.h` include is gone. Built 04:29:05, installed 04:29:12 (md5 `458c53ee5a35`, matches source).
- **User verdict on the state that preceded the revert (m01239)**: "brought the strafe left-right character jitter back
  and also disabled looking up and down again." Recorded here as the regression signature of that camera change: an
  earlier edit reintroduced the two old symptoms (strafe/back-diagonal jitter + dead pitch), which is exactly what
  the reverted state looks like. Do not re-ship that state.
- **User decision (m01270, paraphrase)**: keep the code as it is now — it is the base that moves the work forward —
  and record the regression above so it is not repeated. So the pre-session camera code stays installed; no further
  camera edits until the user asks.
- Details, the MP consumer chain, SA camera internals and the port's failure modes are in
  `RESEARCH.md` § "Max Payne camera: consumer chain + port attempt v1".
- **State fingerprint** (this project has no git, so file drift is invisible and old behaviour can silently come
  back): `maxpayne_combat.asi` md5 `458c53ee5a35`, built 04:29:05, installed 04:29:12, source
  `asi/maxpayne_combat.cpp` camera block = pre-session text. Record md5 + install time for every state that gets
  play-tested; a mismatch between the source you edited and the binary in the game dir explains most "the old bug
  is back" reports.

### Next (ordered, nothing implemented yet; user has not asked for it)
1. Pick the camera back up ONLY on user request. The two symptoms to fix are unchanged: strafe/back-diagonal
   jitter, and right-stick pitch being dead.
2. If the orbit experiment is retried, retry its core (rigid orbit on our own yaw/pitch — that is what removed the
   jitter and restored pitch) and fix these three already-diagnosed bugs:
   a. shape the sticks inside the camera code (`ShapeStick`) — the pad state is raw there;
   b. on resume from aim/vehicle/cutscene do NOT adopt the rendered front (SA's aim front is near-vertical,
      `fz=-0.992`); ease rendered angles back to ours over ~0.25 s instead;
   c. clamp pitch to ~1.0 rad (57 deg), not MP's 1.5553 — at 89 deg SA's up-vector degenerates (`0x509CE0`).
3. Kill the "fights back" feel in `ShapeStick()`, not the camera: MP's `(0.8x)^3` on top of the pad deadzone
   leaves a dead band below ~45% travel. Replace with a curve whose derivative at the deadzone edge is non-zero
   (e.g. `x*(0.3+0.7x^2)`), and expose `kLookGain` in the menu.
4. Only then chase MP's real framing: log `obj+0x30` (camera offset vector) in MaxPayne.exe at runtime instead of
   guessing 2.4 / 0.30 / 0.62. Ghidra headless task `bc4edc572` is still up against `/MaxPayne.exe-e8dd07`.

## 2026-11-17 05:10 — camera tilt: view-only + pitch cap; collision code removed
- User reports: stairs fixed, but outside "moving right-stick too far down still causes crazy camera bug,
  flipping character's view upside down and making what's visible flicker"; later "very broken still ...
  happens inside now too".
- Evidence from the `cf` trace (previous build): 26,326 of 92,031 frames had `|fz| > 0.85` (view within 12 deg
  of vertical). `CCam::GetVectorsReadyForRW` (0x509CE0) rebuilds `m_vecUp = cross(normalize(cross(front,worldZ)), front)`,
  so a near-vertical front makes the up vector normalised noise -> view flips / flickers. Cause confirmed, not the collision.
- Evidence against the collision pull-in: one plain stick hold with no movement changed `dist` 2.42 -> 3.31 and
  `pull` 0.66 -> 0.93, i.e. the camera breathed in and out while tilting; at rest it sat at `pull=0.664`.
- Changes in `asi/maxpayne_combat.cpp` `FollowPedImpl`: the pitch tilt now rotates ONLY `m_vecFront` (view),
  never `m_vecSource`; the applied tilt is capped so the final pitch stays within +-1.36 rad (~78 deg)
  (`tilt = clamp(p0 + g_pitchOfs, +-1.36) - p0`, `p0 = asin(front.z)`); the `CColPoint`/`0x56BA00` collision
  block, `g_camPullK` and the `pull=` trace field are gone (the eye no longer leaves the space SA placed it in,
  so there is nothing to re-test). `m_vecUp` is left alone (0x509CE0 overwrites it).
- Verified with simulated pad input (tools/vpad.py, virtual pad present before launch): full down and full up
  deflection for 2.5 s each -> `fz` stayed within [-0.149, 0.788], `dist` constant at 3.55-3.64 (only SA's own
  convergence). No flip, no breathing.
- Mouse finding (same session): with the pad in use the mouse does not move this camera at all (`fz` unchanged
  through repeated `xdotool mousemove_relative`). SA's mouse third-person cam `0x50F970` is skipped because
  GInput sets the pad-in-use byte `0x8CCF00`, and `Process_FollowPed_SA` ignores the mouse. So in pad mode the
  follow cam is pad-only by design, not broken.
- Verified again with `po=`/`p0=` added to the `cf` trace (our accumulator vs the pitch SA's cam produced):
  holding the stick fully down saturates `po=0.900`, `p0=-0.15`, `fz=0.68`, `dist` 3.63-3.69 throughout; full
  up unwinds `po` 0.900 -> 0.697 -> 0.485 with `fz` following. So the tilt is bounded by `0.9 rad` around SA's own
  pitch and never approaches vertical, and the camera distance no longer moves with the tilt.
- State: `asi/maxpayne_combat.asi` md5 `d76e79d036fd9e5ce3c9693b9e54d4d4`, built 05:16, installed 05:17 (in world).
- The "upside down + flicker" is diagnosed as the eye leaving SA's collision-safe position and the pull-in then
  dragging the camera inside CJ's model (near-plane clipping), not as the pitch maths; removing the eye rotation
  removes that path entirely.

## 2026-11-17 05:56 — CRITICAL / UNSOLVED: Max Payne camera in the pad path

**Status: INCOMPLETE, UNSOLVED, CRITICAL.** Six implementations were tried this session; the last one
(measured Max Payne geometry, `MPCam` as PAD CAMERA -> MAX PAYNE) was rejected by the user as "still failed"
(m01963), and the one before it as "the weirdest behavior yet, like the camera has no vertical rotation"
(m01925). The camera is the mod's remaining blocker: the pad path must not ship with mode 1 enabled.
`g_camMode` default is 0 (CURRENT = stock SA follow cam, no pitch override); modes 1/2 exist but neither is
accepted.

### What the user wants (verbatim intent, carried across the session)
- Max Payne's own camera on the controller ("Max Payne's thumbstick movement for console and for PC (mouse)"),
  exposed as a PAD CAMERA option next to GTA SA / CURRENT.
- Specifically: the look must go up **and** down (m00494/m01290), must not flicker, must not lock to the feet
  (m01626), must not fight the stick ("fights back", m01096), and strafe / back-diagonal must not jitter.
- User rejected inventing a camera ("camera is too close, generic and fixes nothing … No gerryrigging", m00852).

### Every attempt, and why it failed
1. **Tilt SA's finished vectors (mode 0 band-aid).** Rotate `m_vecFront`/`m_vecSource`/`m_vecUp` after
   `Process_FollowPed_SA` (0x522D40). Fails: SA's own follow cam reads `m_vecFront`/`m_vecSource` back as its
   state next frame, so the override feeds itself. Log: `po` (our accumulator) sat still while SA's own pitch
   flipped `+0.785 <-> -0.785` and `dist` flipped `1.42 <-> 3.49` every few frames = the flicker.
2. **Apply the pitch in `CCam::GetVectorsReadyForRW` (0x509CE0)**, after SA finishes. Two bugs: (a)
   `patch::RedirectJump` has no trampoline (verified in plugin-sdk `shared/Patch.cpp:48`), so calling 0x509CE0
   from the hook recursed into itself — this also broke launching (`autoload requested slot=2` in a loop, never
   `IN WORLD`); (b) same feedback problem as (1). Fixed (a) by re-implementing the function; (b) is structural.
3. **Sphere orbit about a guessed pivot** (head +0.75, radius taken from SA's own placement). Log froze at
   `fz=0.985 dist=0.89` with the stick neutral — SA adopted our vectors, pinned at the clamp.
4. **Feed the stick into SA's own follow cam** (`pad->NewState.RightStick*` swapped around the 0x522D40 call) so
   SA does the pitching. Measured: SA's follow cam answers ~4 deg/s at full deflection (`fz -0.149 -> -0.030`
   over 3 s) and is ground/pitch-spring limited. Cannot reach Max Payne's 184 deg/s.
5. **Independent `MPCam` (mode 1), guessed constants** (dist 2.4, pivot 0.62, side 0.30). User: camera glued to
   the feet, generic; flicker at full down (eye left SA's collision-safe position, then the pull-in dragged the
   camera inside CJ's model -> near-plane clipping, logged `dist` 0.89-1.49).
6. **`MPCam` with measured Max Payne geometry**: sphere radius **2.72**, pivot **0.95** above the feet, down
   clamp **75 deg**, up clamp **~14 deg** (`asin((0.95-0.30)/2.72)`), rate `1.024*pi rad/s`, Xbox curve.
   First version also clamped `eye.z` to the floor, which crushed the radius to 0.77 when looking up (camera at
   his feet = "no vertical rotation"). Second version keeps the radius and limits up-pitch instead. User:
   "Still failed." (m01963) — the concrete failure mode was **not** described this time.

### Measured facts (do not re-derive)
- Max Payne (recomp, logged from the real view matrix): eye orbits a pivot **0.95** above the feet at radius
  **2.72**; check `pivot.z = eye.z + 2.72*sin(pitch)` = 8.24 at pitch -72.8 and 8.30 at +2.0. Down reaches
  -74..-75 deg, up only +2 deg in the captured sweep. That asymmetry is why `kMpUpMax` exists.
- Max Payne (PC, Ghidra): pitch clamp +-1.5553 rad in `0x725b30`; camera position = `node[+0x24..+0x2c]` +
  `player[+0xbb..+0xc3]`, and that offset is runtime state (writer not found). Mouse: 0.0025 rad per count,
  linear, no accel, no deadzone; stick: `rate = (1.5*sens+0.5)*(0.8x)^3`, max 1.024*pi rad/s both axes.
- SA follow cam (0x522D40): springs back to pitch ~-0.15, ~4 deg/s from the stick, ignores the mouse whenever
  GInput sets the pad-in-use byte `0x8CCF00`.
- The right stick is already consumed by the time the camera runs (GInput free aim), so camera code must use
  `g_camStickX/Y`, cached at the end of `UpdatePadsHook` after `ForceAim` shaped `NewState`.
- SA's up vector degenerates near vertical (`CCam::GetVectorsReadyForRW` forces `front.x = front.y = 0.0001`).

### Current code state (md5 `d56d5c8f3cc7321a2806a0377ea95cdc`, built 05:55)
`asi/maxpayne_combat.cpp`: mode 0 = stock `0x522D40` plus the three old band-aids and `g_pitchOfs` pinned to 0;
mode 1 = `MPCam` (self-contained, measured geometry); mode 2 = PC cam with negated stick Y; `VectorsHook` is a
pure `0x509CE0` replacement; `cf` trace logs `po=<g_mpPitch>` and `cs=<g_camStickX,Y>`.

### Open questions (the next agent should start here)
1. **Which camera actually owned the view during the failing tests?** Free aim routes `CCamera::Process` to
   `AimWeapon` (0x521500), not to our hook. If the user was aiming/holding the trigger, mode 1 was never
   rendered and "no vertical rotation" is the aim cam. Unexplained and cheap to check: log a counter in mode 1.
2. Does MP move the pivot with pitch? Fitted centres drifted (8.2 .. 10.3) across sweeps, which a fixed pivot
   cannot explain; the sweep data was not clean enough (yaw was changing during the "pitch" sweep).
3. Is the +2 deg up-look real, or is the up axis simply not wired to pitch in the recomp?
4. Is the residual complaint the *rate*, the *ground clamp*, or the fact that CJ is not centred the way Max is?
   The last report gave no description, so the next step must capture one controlled sweep with ped position,
   camera position and angles logged together (see RESEARCH.md "view-matrix trace").

### Tooling built this session (reusable)
- `tools/fitcam.py` — least-squares pivot fit over recomp view-matrix lines; radius spread separates a pure
  orbit from mixed cameras.
- Recomp camera trace: `dev_SetTransform` in `xboxrecomp/src/d3d/d3d8_device.c` logs `D3DTS_VIEW` eye+forward
  to `mpcam.log` when `MP_CAMLOG=1` (`./launch.sh mp`). Build: `cd build/Release && cmake --build . --target payne_recomp`.
- Mod trace hooks still present and temporary: `mp_cammode.txt` (sets `g_camMode`), `mp_trace.txt` (`cf` lines),
  `mp_pitch.txt` (pitch experiment gate), `mp_camtest.txt` (scripted stick). Remove before shipping.
- `tools/vpad.py` (virtual pad) and `tools/padtest.py` still exist; **never leave `vpad.py --serve` running while
  the user plays** (it hides the real pad and looks like "controller input is broken").

## 2026-11-17 06:20 — Pad camera mode 1: ROOT CAUSE FOUND, fixed (not visually verified)
- Root cause: ForceAim forces R1 when armed, so SA runs AIM cam (0x521500, called at 0x527A95), not FollowPed (0x522D40). Mode 1 only hooked FollowPed, so MPCam never ran while aiming.
- Fix (asi/maxpayne_combat.cpp): AimCamHook redirect at 0x527A95 -> MPCam when g_camMode==1; MPCam reseeds after >700ms idle, yaw behind ped, pitch -0.10.
- Numeric trace: stick up raises pitch to +0.2416 clamp (~0.9s), down to -1.309 clamp; strafe/back diagonals leave yaw/dist constant (no jitter).
- NOT verified: gamescopectl screenshots are black; no visual check. Frame dt unevenness (0.0017/0.017) is stock. No camera collision yet. Temp hook files removed; mp_cammode.txt (=1) left in game dir — delete it if unwanted.

## 2026-11-17 06:50 — Pad camera mode 1 (MAX PAYNE): rendering FIXED, visually verified
Three bugs, all in "MPCam replaces SA's cam function but forgot what that function also did":
1. **Never ran while armed** — ForceAim forces R1, so SA runs AIM cam 0x521500 (call at 0x527A95), not FollowPed.
   `AimCamHook` now redirects that call too.
2. **Pitch black on load-in** — SA's cam functions write `m_fFOV` every frame; a fresh CCam after a load has FOV 0.
   Toggling the mode off/on "fixed" it only because SA's cam ran once. MPCam now sets `m_fFOV = 70`.
3. **"No vertical rotation" (the real cause of attempts 5/6 failing)** — SA's cam functions end with
   `GetVectorsReadyForRW` (0x509CE0), which rebuilds `m_vecUp`. MPCam skipped it, so up stayed (0,0,1) while front
   pitched: the render matrix was skewed and the view stayed roughly level whatever the pitch (log
   `F=0,0.26,-0.97 U=0,0,1`; screenshot at full down showed the ceiling). MPCam now calls `VectorsHook(c)`.
Also:
- **Pivot height**: SA ped origin is ~1.0 above the feet (`kSaOriginZ`), MP's 0.95 is above the FEET. Pivot =
  pos + (0.95 - 1.0). Before, the pivot sat at head-top and the eye went through the ceiling indoors.
- **Camera collision**: ProcessLineOfSight 0x56BA00 (buildings+objects, camera-ignore) pivot->eye, eye 0.30 short
  of the hit, min 0.25; pull in instantly, ease out 2.5 m/s. As distance collapses below 1.6 the pivot lifts
  up to +0.75 (head height) so a wall behind CJ gives an over-the-head view instead of a camera inside his hips.
Verified by screenshots (gamescopectl works; `read` tool needs the image inside the project dir, and sees new
files only after a delay): rest = behind CJ; full down = top-down over his head; up = low behind looking up;
yaw + body turn follow; backed against a table = over-shoulder. Trace while strafing / back-diagonals / walking
with rx: CJ's lateral offset from the view axis stays within +-0.01 m (no jitter). Load-in with mode 1 = normal view.
Not done: shooting accuracy vs crosshair under MPCam not checked (crosshair sits over CJ's back like in MP);
stick deadzones 0.45/0.60 (MP measured) mean half deflection does nothing — intended per MP curve.
Debug left in code, file-gated (no files present): mp_trace.txt (cf line, now also logs src/ped/F/U/matrix),
mp_cammode.txt, mp_camtest.txt. Default g_camMode is still 0; select PAD CAMERA -> MAX PAYNE in the menu.

## 2026-11-17 15:50 — Pad camera mode 1: geometry now TRACED from Max Payne (replaces the sphere)
User (m00368): works but doesn't feel like MP; "moving the camera down should not be viewing the character's
feet, the effect should be quite the opposite". Done by tracing the recomp, not guessing:
- **The old 2.72 / 0.95 / -75 / +14 numbers were wrong**: the recomp logger (`d3d8_device.c` dev_SetTransform)
  computed the eye with R^T. Fixed; and the first clean-looking sweep was taken with a wall behind Max
  (horizontal distance pinned at 2.5). Re-traced in open ground after walking forward (run "mp_h").
- **MP's camera**: view direction = the pitch itself (NOT aimed at Max); the eye slides on a pitch curve
  relative to Max's root object (1.56 above ground): rest -4.6 deg = 4.13 behind / 0.67 up; looking down the
  eye RISES and moves FORWARD over his head (-35: 2.1 behind 1.9 up; -71: directly above; -88: 0.15 ahead,
  0.6 up, looking straight down at the ground ahead; Max drops off the bottom of the screen). Looking up it
  drops behind his back to hip height (+30: 2.4 behind, -0.65; +80: 0.95 behind, -0.81). Pitch range -88..+80.
  Eye 0.155 right of his centre. Follow is rigid (no lag). Vertical FOV 55.4 = SA's 70 at 4:3.
- `asi/maxpayne_combat.cpp`: `kMpCurve` table (46 traced points, linear interp, `MpCurve()`), MPCam uses it;
  front = pitch direction; collision pulls the eye toward the root (LOS 0x56BA00), eases out 2.5 m/s.
  Raw trace kept: tools/recordings/mp_pitchsweep_mpcam.log / _mpworld.log (`v ex ey ez fx fy fz ux uy uz n`).
- SA verified with screenshots at -11/-36/-62/-88/+43/+80 deg: same framing as the MP shots (down = floor
  ahead, CJ's head/shoulders at the bottom edge; up = low behind, ceiling/sky). Strafe/diagonal lateral
  offset steady (+-0.02).
- Not matched: wall behaviour (MP seemed to keep the eye height and clamp horizontally; we pull along the
  ray), pitch rate (MP ~170 deg/s at full stick, ours 184).
- tools/vpad.py: `--send` returns immediately (server queues steps); combine axes with commas
  (`ly=-1,rx=0.8:1.5`) — a space makes a new step and a malformed step used to kill the server (now caught).

## 2026-11-17 16:10 — Menu: GUNFIRE / NATIVE AIM / CROSSHAIR; SA launches on the left monitor
- `GUNFIRE: MAX PAYNE|SA` (g_mpFire, default MAX PAYNE = previous behaviour: armed = forced aim stance via ForceAim,
  fire any time, no aim button). `SA` = no forcing, hold R1 to aim.
- `NATIVE AIM: ON|OFF` (g_nativeAim) — only shown while GUNFIRE=MAX PAYNE (new `showIf` field in MpMenuItem,
  hidden rows are skipped by nav/scroll). ON = ForceAim stops forcing R1, so physical R1 is SA's own aim again.
- `CROSSHAIR: MAX PAYNE|SA` (g_mpCross, separate): CrossHook replaces the CHud::Draw call at 0x58FBBF to
  CHud::DrawCrossHairs (0x58E020). MP = black-outlined white dot whenever armed on foot (not in vehicle/menu);
  sniper keeps SA's scope reticle; SA = original. In PAD CAMERA=MAX PAYNE + GUNFIRE=MP the SA crosshair
  multipliers (0xB6EC14/0xB6EC10) are set to 0.5 so shots go through the dot at screen centre.
- Verified: dot at screen centre when armed at rest, menu shows the 3 rows. NOT verified: SA lock-on still
  snaps to nearby peds with forced aim (not suppressed); NATIVE AIM / SA modes only code-reviewed; shot
  impact vs dot not measured.
- launch.sh: `--display-index "${GTA_DISPLAY:-1}"` -> window at x=0 (left monitor HDMI-1-1); GTA_DISPLAY=0 = old right one.

## 2026-11-17 17:10 — NATIVE AIM fixed; shots go through the crosshair
- Cause of "on/off do the same": SA aim = target button (R1 = lock-on, nearest ped) OR GInput's aim button LT (free aim,
  cam mode 53, shot ray through the CHair crosshair). Old ForceAim forced R1 (lock-on only: ray ignored crosshair) for both.
- Now GUNFIRE=MAX PAYNE + NATIVE AIM=OFF: ForceAim forces **LT=255, R1=0** = permanent SA free aim (no lock-on).
  NATIVE AIM=ON: nothing forced; physical LT / R1 act as plain SA (verified: idle not aiming, LT -> free=1 cm=53).
- Shots: CCamera::Find3rdPersonCamTargetVector (0x514970, gta-reversed Camera.cpp:1570) = active cam front +
  up/right * CHair offsets, source projected onto the muzzle. CHair=0.5,0.5 in MODE 1 + MP gunfire => bullets fly along
  the MPCam view axis = the dot. Verified top-down: impact sparks next to the dot. Not verified: long range / on peds.
- Test gear: slot 2 save has a bat; tested with pistol given via CPed::GiveWeapon 0x5E6080 (+RequestModel 346) and
  SetCurrentWeapon 0x5E61F0 re-applied each frame (something resets the slot to the bat). Hooks removed.

## 2026-11-17 17:40 — Native-aim camera jump fixed; RIGHT ARM toggle (default MAX PAYNE)
- Camera displaced at native aim start: SA starts a 350 ms cam transition (CCamera::m_bTransitionState) whenever the
  cam mode changes (FollowPed <-> Aim on LT), blending from the old pose. MPCam now clears `m_bTransitionState` each frame.
- New menu row `RIGHT ARM: MAX PAYNE|SA` (g_mpArm, default MAX PAYNE; shown only while GUNFIRE=MAX PAYNE).
  MP: ForceAim holds SA free aim (LT) only for kArmHoldMs=1800 after a shot (Circle/R2) -> gun hangs down at rest,
  rises to the crosshair while firing, SA lowers it afterwards. SA: arm always up (previous behaviour).
- kArmHoldMs=1800 is a GUESS: MP's real drop time was not measured in the recomp (do that if it feels off).
- Verified by screenshots with a pistol: rest = arm down, shot = arm up toward dot, 3 s later = down, walking fine.
  Not verified: view while the arm drops mid-turn, fast fire sequences.

## 2026-11-17 18:05 — "look up, fire -> camera snaps forward / CJ fires forward": NOT reproduced (open)
Tested GUNFIRE=MAX PAYNE, NATIVE AIM=OFF, RIGHT ARM=MAX PAYNE, pistol, mode 1, vpad:
- Camera: cf log pitch (po=) and yaw identical before/during/after a shot (e.g. po=1.396 stays; nm 4 -> 53 -> 4 only
  switches SA's cam mode, MPCam runs in both). Also while walking (ly=-1) and firing: pitch kept, nm 53.
- Bullets: with the view pitched ~+0.34 rad at a wall, impact sparks appear AT the dot (not forward); arm points up/right.
- MP reference (recomp, mpcam.log view matrix constant 0.055,0.984,-0.167 through the shot at 25500): MP camera does not
  move on fire either; Max's arm only becomes visible ~2 s after the shot while looking up.
Unknown: whatever the user sees on a real pad (needs exact repro: pitch size, moving or standing, R2 vs Circle, how long
held). Suspects not yet tested: real analog R2 half-press (>40 starts aim), fire while stick still held up.

## 2026-11-17 18:47 — Focus zoom restored (NATIVE AIM + SA GUNFIRE); tracing hooks removed
SA's native aim zoom is a DOLLY, not FOV (measured mode 0, vpad LT): camera source distance to the ped
3.515 idle -> 2.702 aiming (0.769x), c->m_fFOV stays 70.0 both states.
- MPCam now eases `g_zoom` 0..1 over ~0.25 s (dt*6) and scales the whole eye offset toward the MP root:
  `eye = root + (eye-root) * (1 - 0.231*g_zoom)` (asi/maxpayne_combat.cpp, before the collision block).
- `g_zoomWant` is set by the two cam hooks: true in AimCamHook (SA aim cam = nm 53) UNLESS MP gunfire is holding
  LT permanently (`g_mpFire && !g_nativeAim`) — MP gunfire has no aim button, so no zoom there; false in FollowPedHook.
- Verified (cf log eyedist, mode 1): rest 4.312 -> NATIVE AIM lt 3.361 -> back 4.283; SA GUNFIRE (g_mpFire=0) lt 3.361;
  MP GUNFIRE lt: nm stays 4, eyedist 4.312 (correct, LT is ours). Screenshots match (closer framing while aiming).
- Temporary per-frame recorder hook (mp_rec.txt) and all mp_*.txt files removed; ASI rebuilt and installed.
  The old F9 = autoPlayer toggle still exists (old aim/shoot overlays), unrelated to tracing.

## 2026-11-17 20:15 — Hipfire (no aim stance, bullet at the dot) + motorcycle-enter snap fixed
Requirements: user m01457 "with native aim on, the hipfire should still behave like MP (the same as with native aim off)";
m01546 "hipfire does not involve aiming, which is the definition of hipfire"; m01681 "native aim hipfire lacks accuracy for
distant pedestrians/enemies ... | Motorcycle 'entering' animation has a visual bug that mimics the strafe left/right
teleport problem".
- ForceAim (asi/maxpayne_combat.cpp, ForceAim) no longer force-holds LT or R1: with MP gunfire it swallows both aim buttons
  (SA stays in its hip state, no aim stance, no zoom), and `g_aimHeld` records the player's own button first so NATIVE AIM
  only takes over while an aim button is actually down. The old RIGHT ARM latch (`g_mpArm`, kArmHoldMs=1800) is gone.
- Bullet direction: new `GunFireHook` on the CALL SITE 0x61ECCD (CTaskSimpleUseGun::FireGun -> CWeapon::Fire 0x742300).
  While hip-firing it replaces targetPosn with the point where the camera ray lands: `CWorld::ProcessLineOfSight` 0x56BA00
  (CColPoint 0x2C bytes) from camera source+front*0.5 to source+front*300, retried up to 3x past the player - CJ's head is
  in the way when looking down. CWeapon::Fire then aims the muzzle at that point. The earlier fixed "200 m ahead" point was
  not enough (muzzle offset made close/mid-range shots miss).
- Motorcycle entering: `EnsureFullBlend` topped up SA's OWN clip (`blend fix: sum=0.00 -> top up p483` immediately before
  `clip ped#483 id=194 grp=2`) - SA's bike-enter animation was forced from blend 0 to 1 in one frame, so the body snapped
  into the pose = the "teleport" the user saw. It now only adopts OUR clips (`InBody`) as top-up candidates, the >1.0 excess
  trim also only touches ours, and with no MP clip in play it returns without touching anything (SA blends itself).
- Verification status: builds clean; the aim-stance behaviour is confirmed (fire raises the arm, no aim state, dot unchanged).
  Shooting accuracy + bike enter need the user's real-pad pass. Temp test hook STILL IN: `mp_gun.txt` gives CJ a pistol
  (`p->GiveWeapon` 0x5E6080 + slot 0x5E61F0) - remove with the other temp hooks at handoff.
- Dead end (do not repeat): spawning a bike from the ASI hangs the game. `CCarCtrl::CreateCarForScript` 0x431F80 hangs
  whether the model is requested only or also loaded with `CStreaming::LoadAllRequestedModels` 0x40EA10 (blocking loader from
  the main thread); two hang dumps in maxpayne_combat.log ("HANG eip=7bd642fc", Wine/user32 stack).

## 2026-11-17 20:35 — Hipfire target proven; body heading locked to the camera
User m01880: "hipfire still lacks accuracy with max payne logic with native aim on. There's also more undefined movement bugs
that need to be solved, like an issue where character can get locked in a diagonal facing direction in certain directions."
- Movement (diagonal facing lock): FreeWalk now writes `g_walkHeading = CameraHeading()` on every walking frame
  (asi/maxpayne_combat.cpp, FreeWalk) - in mode 1 there is no follow-cam drift to track, so the old
  `PlayerLooking()` gate (>7000 raw right stick, i.e. 21 % deflection) could leave the body facing an old diagonal while the
  camera crept. StandTurn now treats ANY camera yaw change (`fabsf(dc) > 1e-4`) as looking, so the idle body always turns
  with the camera (the PlayerLooking helper is gone). Verified with the `mp_aimlog.txt` diagnostic: `ang` (ped forward vs
  camera forward, 2D) is 0.0 through 0.5/1.0/2.0 s camera turns and while walking all 8 stick directions.
- Hipfire: the substitution on call site 0x61ECCD now also hands SA the raycast hit ENTITY (`targetEnt`) besides the point,
  and the point is the camera-ray hit found with `CWorld::ProcessLineOfSight` (see RESEARCH.md, "Firing exactly where the
  dot is"). Proof that SA honours it: `CWeapon::FireInstantHit` (0x73FB10) disassembled (see RESEARCH.md) - at 0x73FDD7 it
  branches to 0x74034A (fire along the ped heading) only when BOTH targetEnt == 0 and targetPosn == 0; with a point it goes
  0x73FE6D and builds `dir = target - muzzle`, normalised. So no aim stance is needed for accuracy.
- Residual risk: SA still computes a per-shot aim error from `CWeaponInfo::m_fAccuracy` ([ebx+0x38], read at 0x73FC04 together
  with a ped skill byte at +0x71A) - that may be the remaining "spread" the user sees at long range with a pistol. Not yet
  proven or touched; if the user still reports misses, look there first (and NOT at m_fSpread, which FireInstantHit never reads).
- Verification status: aim substitution and body heading confirmed by log; impact placement could NOT be verified from
  screenshots (muzzle flash changes the whole frame's lighting, so pixel diffs are useless; SA decals are invisible at
  960x540 on these surfaces). Needs the user's real-pad pass.
- Temp hooks: `mp_gun.txt` (pistol + slot 2, 0x5E6080 / 0x5E61F0) is kept deliberately as a dev helper next to
  `mp_trace.txt` / `mp_cammode.txt` so gunfire can be tested from a cold save; `mp_native.txt` (NATIVE AIM toggle) is removed -
  the menu option is authoritative. `mp_aimlog.txt` (per-shot aim line) is file-gated and still in for hipfire diagnosis.

## 2026-11-17 20:50 — PINPOINT ACCURACY toggles (hipfire + aiming)
User m02081: "Almost seems like the native game has reduced accuracy on distance. or general spread yeah. That's why I said make
a toggle to bring in pinpoint accuracy on hipfire and one for aiming, both compatible with each other."
- Where SA's spread actually comes from: `CWeapon::FireInstantHit` (0x73FB10) computes an angular error at 0x73FC04 as
  `(skillMax - <ped skill byte at +0x71A>) / CWeaponInfo::m_fAccuracy` (`[ebx+0x38]`, `fdiv`), multiplied by a constant for
  the player (`0x4000000` ped flag). `m_fSpread` (+0x68) is never read on this path. So "pinpoint" = make that quotient ~0.
- Implementation, `asi/maxpayne_combat.cpp` GunFireHook: when the shooter is the player and the matching toggle is on, every
  `CWeaponInfo` (base 0xC8AAB8, stride 0x70, field +0x38, 80 entries) gets `m_fAccuracy = 1e7` for the duration of the
  `CWeapon::Fire` call and is restored right after (`0xB7CD64` - written by the shotgun branch at 0x73FC72 - is saved and
  restored too). No permanent table change, so NPC/hostile weapon behaviour is untouched.
- Two new menu rows in `asi/maxpayne_menu.h`: `HIPFIRE ACCURACY` (g_mpHipAcc, PINPOINT / SA SPREAD) and `AIM ACCURACY`
  (g_mpAimAcc, PINPOINT / SA SPREAD), both default PINPOINT, both independent, both shown with either GUNFIRE setting.
  The hook picks the hipfire one when no aim button is held and the aiming one while LT/R1 is held (`aiming` now reads the
  pad directly, so it also works with GUNFIRE = SA).
- Verified from the log (`mp_aimlog.txt`, temp diagnostic): `pin=1 wt=22 acc0=1.00 acc=10000000 rng=35.0` - pistol accuracy is
  really 1.0 at +0x38 (and 35.0 = m_fWeaponRange at +0x8), i.e. the struct/stride is right and the override lands.
  Actual grouping on a distant target still needs the user's pass.
- Menu order: the two accuracy rows sit at the END of the list (after ENEMY ANIMS) on purpose - inserting them next to
  NATIVE AIM pushed `STICK LOOK CURVE` (the camera-movement row) out of the 10-row page, and the user read that as a
  removed option. Page 1 is unchanged: BODY ANIMS, MOVEMENT, STRAFE TORSO, BACKPEDAL TORSO, GUNFIRE, NATIVE AIM,
  CROSSHAIR, PAD CAMERA, STICK LOOK CURVE, SMOOTH TRANSITIONS.

## 2026-11-17 21:10 — two-column menu, published repo + release

- In-game menu is now 2 columns (10 rows each, `DrawMenu` in `asi/maxpayne_menu.h`): every option is on screen at once,
  so adding rows can no longer push `STICK LOOK CURVE` (or anything else) onto a hidden page. Navigation stays linear
  (DPAD up/down walks the list, left column first) - no new input to learn. The HIPFIRE ACCURACY / AIM ACCURACY rows sit
  after NATIVE AIM again, in the first column.
- The same B press that opens the menu with SELECT+B can no longer close it: `SetMenu` stamps `g_menuOpenedAt` and the
  B-close path needs 400 ms to have passed.
- `doc/`-free test aid: a temporary `mp_menu_open.txt` hook (auto-open, used for the layout screenshots because the
  virtual pad's non-axis buttons stopped registering in this Wine session; axes still do) was removed again after use.
- Published: https://github.com/intosica/GTA-SA-Max-Payne-Combat (source, docs, tools, assets) and release `v1.0`
  with `GTA-SA-Max-Payne-Combat-v1.0.zip` = asi + 5 IFPs + `maxpayne_ui/` (install: drop them in the GTA SA folder,
  needs an ASI loader and GTA SA 1.0 US).

## 2026-11-17 21:25 — turn-in-place: no more turn clip (jolt + odd hands), idles stay possible

- User (m02347): "rotating in place with camera only causes CJ to jolt up and down, and his hands become positioned in an
  odd manner, which is unlike the Max Payne's native rotation animation ... fix this strange arm behavior now ... let me
  know if we do it this way, if it will lock us out of ever having those idle animations working."
- Cause: `StandTurn` played `TURN_L`/`TURN_R` from `MP_BODY.IFP` as SOON as a fast camera yaw was detected
  (`g_turnT > kTurnStep`). Those are full-body takes recorded on Max's rig while he holds a gun: they carry their own
  root bob (the vertical jolt) and their own arm pose (the odd hands), and they cut in over the standing idle.
- Fix (`asi/maxpayne_combat.cpp` `StandTurn`): the turn clip is gone; turning is the heading goal alone
  (`g_turnGoal` -> `CalcVelocityHook` -> `CPed::CalculateNewVelocity`), so CJ rotates under whatever body clip is
  already playing. Verified: `standturn step=1` frames, log shows no `play` line, `cf` `ped=` z constant (1014.7 for
  the whole 3 s turn), screenshots mid-turn show the normal idle arm pose.
- Does this lock out Max's random idle animations? No. The idles were never driven by this path: they play from the
  standing-idle slot, and the turn now only rotates the ped, so an idle keeps running while CJ turns. What is lost is
  only MP's *turn* animation itself (the step). To get it back without the jolt/hands, export a LEGS-ONLY turn clip
  (or mask the existing one to the leg bones) and play it as a partial overlay - that is an additive change, nothing
  here has to be undone.
