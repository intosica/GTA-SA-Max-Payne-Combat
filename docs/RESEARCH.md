# Research: Max Payne 1 assets -> GTA SA animation

Last updated: 2026-11-12. Status: extraction complete; retarget math under validation.

## 1. Getting the PC assets

The PC ISO is an Inno Setup 5.5.0 repack. `innoextract` rejects it, but Wine's silent installer works:

```
WINEPREFIX="$HOME/.wine-maxpayne" WINEDEBUG=-all \
  wine Setup.exe /VERYSILENT /SUPPRESSMSGBOXES \
  /DIR='Z:\tmp\mp_pc\game' /NOICONS
```

Installed game: `/media/montel/Extreme SSD/Games/Max Payne PC/game` (794 MB). `MaxPayne.exe` is 5.9 MB. Archives:

```
x_data.ras       140 MB
x_level1.ras     118 MB
x_level2.ras      68 MB
x_level3.ras      98 MB
x_alocalized1.ras 35 MB
x_alocalized2.ras 177 MB
x_music.ras      138 MB
```

Unpack with libras:

```
/media/montel/Extreme SSD/Games/Grand Theft Auto San Andreas/mod creation tools/libras/build/test/test-file --decompress -O /media/montel/Extreme SSD/Games/Max Payne PC/extract x_data.ras
```

Result: 3095 files, including 834 `.kf2`, 183 `.kfs`, 45 `.skd`.

Useful locations:

```
/media/montel/Extreme SSD/Games/Max Payne PC/extract/data/database/skeletons/default_skeleton/anim/      362 kf2
/media/montel/Extreme SSD/Games/Max Payne PC/extract/data/database/skeletons/default_skeleton/maxpayne-collision.kfs
/media/montel/Extreme SSD/Games/Max Payne PC/extract/data/database/skins/max_payne/PAYNE_MAX_L0.SKD
/media/montel/Extreme SSD/Games/Max Payne PC/extract/data/database/weapons/*.txt
/media/montel/Extreme SSD/Games/Max Payne PC/extract/data/database/characteranimid.h
/media/montel/Extreme SSD/Games/Max Payne PC/extract/data/database/skeletons/default_skeleton.txt
```

The Xbox recomp source has no assets, and the Xbox ISOs are not needed.

## 2. MAX-FX chunk format (KF2/KFS/SKD)

Little-endian chunk envelope:

```
u8  magic = 0x0c
u32 id
u32 version
u32 totalSize      // includes these 13 bytes
```

Chunk IDs used here:

```
0x00010000 Node
0x00010005 Mesh
0x00010006 Geometry
0x00010007 Polygons
0x00010012 KeyframeAnimation
0x00010013 Animation
0x00010014 Skin
```

`KeyframeAnimation`:

```
Animation child       objectName string, fps i32, looping bool
parentName            string
loopInterpolation     bool
totalFrameCount       u32
keyframeCount         u32
keyframes             [frameId u32, Matrix4x3f]
visibility frames
loopToFrame           u32
interpolationMethod   u32
maintainMatrixScaling bool
```

For file version < 5, the game adds 1 to `totalFrameCount`. Matrices are local-to-parent; interpolation is matrix-coefficient, not quaternion.

KFS node chunks contain `name`, `parentName`, local `object_to_parent_transform` (4x3), and `hasParent`. SKD contains skin weights plus `skinObjectNames` and `skeletonObjectNames`; it does not contain the rest hierarchy.

Parser used for inspection: `~/.var/app/org.blender.Blender/config/blender/5.2/scripts/addons/io_scene_maxpayne/max_payne_sdk/max_kf2.py`, class `MaxKF2Reader`. It returns `.meshes`, `.skins`, `.keyframe_animations`.

## 3. Max Payne skeleton (28 bones)

Rest hierarchy from `maxpayne-collision.kfs`:

```
Pelvis (root, pos 0, 0.938649, -0.006350)
  Torso
    Neck
      Head
    shoulders
      JfUR -> JfLR
      JfUL -> JFLL
      JBack -> JBLL, JBRL, Belt
    UpArm-R -> LoArm-R -> Hand-R -> Gun
    UpArm-L -> LoArm-L -> Hand-L
  UpLeg-R -> LoLeg-R -> Foot-R -> Toe-R
  UpLeg-L -> LoLeg-L -> Foot-L -> Toe-L
```

Rest pose is Y-up in pelvis space. `Pelvis` rest rotation is approximately:

```
[-1, 0, 0]
[0, 0, -1]
[0, -1, 0]
```

`Torso`, `Neck`, and `Head` rest rotations are identity; their local translations are mostly along -Z (spine) and +Y (pelvis).

Animation examples:

- `ShootBerettaDual.kf2`: 28 tracks, `Pelvis` fps=100 total=61 keys=24; `Torso` 13 keys; `Head` 27; shoulders 2.
- `idle_aim.kf2`: 28 tracks, fps=40, looping; `Pelvis` total=90 keys=1.
- `ShootMP5Burstshot.kf2`: fps=30, total=31, `Pelvis` 5 keys.
- `ShootColt_Up.kf2`: fps=30, total=31, `Pelvis` 4 keys.

MP animation fps varies (30/40/50/100), so IFP export must resample to a fixed rate (planned 30 or 60 fps).

## 4. GTA SA IFP (ANP3)

`anim/ped.ifp` is 1,433,248 bytes, package `ped`, 294 animations.

Header:

```
char magic[4] = "ANP3"
u32  fileSize          // bytes after magic+size
char packageName[24]
u32  numAnimations
```

Each animation:

```
char name[24]
u32  numObjects
u32  frameBytes        // sum of all object frame sizes
u32  flags             // bit 0 = compressed
```

Each object:

```
char name[24]
u32  frameType         // 3 or 4
u32  numFrames
u32  boneId
```

Frame types:

- `3`: `int16 rotation[4] + uint16 time` (10 bytes)
- `4`: `int16 rotation[4] + uint16 time + int16 translation[3]` (16 bytes)

Decoding:

```
rotation.xyzw = int16 / 4096.0
time          = uint16 / 60.0 seconds
translation   = int16 / 1024.0
```

Only types 3 and 4 occur in SA. Type 4 is used for the root (translation); type 3 for other bones.

Reference loader: `mod creation tools/gta-reversed-master/source/game_sa/Animation/AnimManager.cpp`, `CAnimManager::LoadAnimFile_ANP23`.

SA bone IDs (standard 26-bone rig):

```
0 Root, 1 Pelvis, 2 Spine, 3 Spine1, 4 Neck, 5 Head
31 L Clavicle, 32 L UpperArm, 33 L ForeArm, 34 L Hand, 35 L Finger, 36 L Finger01
21 R Clavicle, 22 R UpperArm, 23 R ForeArm, 24 R Hand, 25 R Finger, 26 R Finger01
41 L Thigh, 42 L Calf, 43 L Foot, 44 L Toe0
51 R Thigh, 52 R Calf, 53 R Foot, 54 R Toe0
```

Additional DFF HAnim nodes: 8 Jaw, 6/7 brows, 201 Belly, 301/302 breasts, 303-307 face helpers. These exist in the 37-node DFF skeleton but are not needed for the animation set; leave them at bind.

`models/player.img` contains body-part DFFs (`torso.dff`, `legs.dff`, `feet.dff`, `hands.dff`, `head.dff`), not one `player.dff`. All share the same 37-node HAnim skeleton. Extracted copies: `mod creation tools/blender_assets/*.dff`.

DFF matrices are **row-vector** (`v' = v * M`; world = local * parent). Read with DragonFF `gtaLib` via `mod creation tools/sa_anim_baseline/sa_rig.py` (`Rig`); the old librw dumper (`tools/dump_dff.cpp`) is no longer needed.

SA rest positions (bind):

```
Head      (-0.001041,  0.590647, 0.023586)
Foot R/L  (±0.169959, -0.934197, -0.002286)
Toe R/L   (±0.169959, -1.036198, 0.151822)
Pelvis    (0, 0, 0)
```

`IDLE_armed` root first key is quaternion `(0.0076, 0, 0.7078, 0.7063)` (~90 degrees about Z), translation `(-0.00098, 0.00098, 0.02344)`. This is an animation pose, not the DFF bind root (which is a 120-degree cyclic rotation about (1,1,1)).

SA translation semantics, from `RpAnimBlend.cpp`:

```
fd->KeyFrame->q = nextQ;
fd->KeyFrame->t = nextT - deltaV + fd->BonePos;
```

`BonePos` is the bind bone position from the skin-to-bone matrices. Root `BonePos = (0,0,0)`, so root translation is absolute; non-root translations are relative to bind.

## 5. Retarget status

Pure rotation copying was tested and rejected: world-left, world-right, local-left and local-right all produced an incorrect pose because MP and SA bind bone axes/child-offset frames differ. Even when world delta angles matched exactly, the SA hand sat low or the character tilted sideways.

**Root cause found:** the SA DFF HAnim parent hierarchy from the actual frame tree is not the bone-ID convention. The clavicles (31/21) are children of **Neck (4)**, not Spine1 (3). Earlier tests parented them to Spine1, which put the shoulders at y=0.16 instead of y=0.48 and made the arms hang. The torso.dff frame tree confirms: Clavicle L frame `0x...33a50` parent `0x...33890` (Neck); Clavicle R frame `0x...33a50` sibling.

Working method (implemented in `tools/retarget.py`):

1. Keep the SA root bind orientation and apply only MP's pelvis world delta.
2. For each SA bone, aim its child offset at the mapped MP child direction (top-down).
3. Pick the twist from the MP bone's local X axis, projected perpendicular to the aim direction.
4. Use the SA bind local translations; FK the result.

The root translation is MP pelvis displacement. Both model spaces are Y-up, so no global scale/rotation is applied yet.

Validation on frame 0 (`tools/retarget.py`):

```
idle_aim          head (-0.035, 0.609, -0.002)  handR (-0.057, 0.677, 0.291)  footR (-0.091, -0.900, 0.003)
ShootBerettaDual  head (-0.020, 0.598, -0.056)  handR (-0.062, 0.694, 0.242)  handL (0.002, 0.566, 0.613)
ShootMP5Burstshot head ( 0.075, 0.562, -0.004)  handR (-0.112, 0.546, 0.237)  handL (0.394, 0.480, 0.426)
```

The head stays above the pelvis, feet stay below, `idle_aim` correctly has only the right hand raised, and `ShootBerettaDual` raises both hands. This is the current retarget baseline.

Remaining work: twist/head orientation polish, per-frame resampling, and a visual check. Fallback remains Blender retarget if the aim result looks wrong on the actual mesh.

## 6. References

- IFP loader: `gta-reversed-master/source/game_sa/Animation/AnimManager.cpp`, `LoadAnimFile_ANP23`
- Frame structs: `gta-reversed-master/source/game_sa/Animation/AnimSequenceFrames.h`
- Blend/update: `gta-reversed-master/source/game_sa/Plugins/RpAnimBlendPlugin/RpAnimBlend.cpp`
- Bone tags: `gta-reversed-master/source/game_sa/Enums/eBoneTag.h`
- RenderWare HAnim: `librw` `rphanim.cpp` (github aap/librw; not needed for the pipeline)
- Max Payne SDK parser: `~/.var/app/org.blender.Blender/config/blender/5.2/scripts/addons/io_scene_maxpayne/max_payne_sdk`

## 7. IFP writer and ASI (current)

- `tools/kf2_to_ifp.py` resamples at 30 fps, writes ANP3 with 26 SA bones for full-body clips or 16 upper-body bones with `--upper`.
- ANP3 layout: per animation header, then per sequence a 36-byte header followed immediately by that sequence's frames (not all headers first).
- Full body: root is type 4 (rotation + 16-bit translation), children type 3. Upper body: all sequences type 3, no root, no legs.
- Root Z-up conversion: `C_YUP_TO_ZUP = [[-1,0,0],[0,0,1],[0,1,0]]`; root translation is `(MP pelvis world - MP pelvis rest) @ C`.
- ASI loads blocks with `CAnimManager::LoadAnimFile` (`0x4D47F0`) and plays hierarchies with `CAnimManager::BlendAnimation` (`0x4D4410`). This bypasses the animation association group; custom blocks (`MPC`, `MPU`) are fine.
- Upper-body clips use `ANIMATION_PARTIAL` (0x10) + `ANIMATION_LOOPED` (0x2). SA fades other partial animations but leaves full-body movement running.
- `CAnimBlock` in memory: `szName[16]`, `bLoaded`, `pad`, `usRefs`, `startAnimation`, `animationCount`, `animationStyle` (size 0x20).
- `CPlayerPed::GetWeapon()` = `&m_aWeapons[m_nSelectedWepSlot]`; `CWeapon::m_nState` values: ready 0, firing 1, reloading 2, out-of-ammo 3, melee 4. Use this for the player state machine.
- Hostile ped filter: `PED_TYPE_COP` 6, `PED_TYPE_GANG1..GANG10` 7–16, `PED_TYPE_CRIMINAL` 20. Ped pool global is `*(CPool<CPed,CCopPed>**)0xB74490`.

Experimental dodge export: `--no-root-motion` zeroes root translation; the rotation still tips the skeleton, so `validate_ifp.py` flags it. `assets/MP_DODGE.IFP` is not loaded by the ASI until it is visually checked.

## 8. ~~IFP animation-frame calibration~~ (SUPERSEDED — see §9)

> Historical notes from the failed attempts. The calibration and the
> "hand follows forearm" workaround below were both wrong; §9 is the working method.

The IFP `q` values are **not** in the DFF bind-bone frame. For SA, `IDLE_armed`
root q is ~90° about Z while the DFF HAnim root bind matrix is a 120° cyclic
matrix; applying retargeted bind-frame rotations directly made feet/torso roll
sideways for long-gun clips.

Correct export calibration, using the known-good SA `IDLE_armed` first frame:

```
q_ifp = q_idle[bone] * inverse(B_local[bone]) * l_target[bone]
```

where `B_local` is the DFF bind local rotation and `l_target` is the retargeted
bind-frame local rotation. Root also needs `C_YUP_TO_ZUP` applied before
calibration; upper-only clips do not include Root and use the same per-bone
calibration. Validator must reverse it:

```
l_target = B_local * transpose(q_idle) * q_ifp
```

This is implemented in `tools/kf2_to_ifp.py` (`load_sa_idle_q`, `make_clip`)
and `tools/validate_ifp.py --calibrated`.

### Final correction (in-game validated)

- The game applies IFP `q` **directly** as the bone's local animation rotation
  (`RpHAnimBlendInterpFrame.q` -> `RpHAnimKeyFrameApply`). Do not pre-multiply
  by the DFF bind rotation. Validator FK now uses `q` directly; `--calibrated`
  is legacy.
- Export calibration remains `q_ifp = q_idle * inverse(B_local) * l_target` so
  the rest pose reproduces SA `IDLE_armed`. Root does **not** get an extra
  `C_YUP_TO_ZUP`; the 90 deg Z root q is what makes the Y-up skeleton Z-up.
- SA's named right chain is at negative X (`Bip01 R Clavicle` 21, `R UpperArm`
  22, `R Thigh` 51). Max Payne's right side is positive X. Map MP right to the
  SA **named R** chain (22/23/24, 51/52/53/54), not the +X chain.
- SA boot/load can take ~40 s in this setup; do not declare an infinite load
  before checking `maxpayne_combat.log` for `tick`.

### Hand/weapon orientation

GTA renders the weapon by copying the **R Hand bone matrix** onto the weapon
clump (`CVisibilityPlugins::RenderWeaponPedsForPC`). Max Payne's hand axes do
not match SA's, so forcing the MP hand world matrix makes the gun point
backward over the shoulder. The exporter now lets SA Hand inherit the retargeted
forearm (`a[Hand] = a[ForeArm]`); the weapon follows the forearm instead.

## 9. Working retarget (2026-11-13)

SA conventions are now documented once, for all future imports, in
`../../mod creation tools/sa_anim_baseline/SA_ANIMATION_RIG_BASELINE.md` (code: `sa_rig.py`).

Root causes of the broken arms/guns:

1. **Mirror applied to positions only.** MP is Y-up, +Z forward, right side at **+X**;
   SA bind is Y-up, +Z forward, right side at **−X**. MP→SA bind needs
   `S = diag(-1,1,1)` on positions **and** rotations (`S @ R @ S`).
2. **IDLE_armed "calibration"** (`q = q_idle · B⁻¹ · l`) added the IDLE_armed arm pose
   on top of every clip. SA ANP3 q is simply the quaternion of the DFF-local rotation.
3. **Hand orientation** was never mapped: SA draws the gun along R Hand **+X** (top +Z);
   MP's `Gun` bone has barrel **+Z**, top **+Y**. Hand world = `P @ Gun_world`,
   `P = [[0,0,1],[1,0,0],[0,1,0]]`. Dual left gun = mirrored right gun-in-hand offset on
   Hand-L, then `Rx180` (SA flips the left weapon).
4. `ShootColt_Up` is MP's *shoot upward* variant; rifles now use `ShootColt`.
5. `idle_aim_beretta` / `idle_bb_aim_deserteagle` were requested from MPC but not in it.

Method (`tools/retarget.py`): world-space delta copy per bone
`SA[b] = SA_ref[b] @ MP_ref[b].T @ MP_anim[b]` with `SA_ref` = SA bind swung to MP's
rest bone directions (A-pose differences removed). Mapping: Pelvis→1, Torso→3 and
clavicles 21/31, Neck→4, Head→5, arms 22/23/32/33(/34), legs 41–44/51–54; R Hand from
the Gun bone. Root: vertical pelvis delta only for in-place clips (`--root-motion vertical`),
full travel for dodges.

`tools/validate_ifp.py` checks every frame in game space (barrel forward, upright,
no flips); `tools/plot_pose.py` draws MP vs SA stick figures with the gun axis.

## Session prep 2026-11-17 (right-stick/mouse look trace)
- Ghidra headless MCP for PC MaxPayne.exe (project ~/mp_ghidra/maxpayne):
  `GHIDRA_INSTALL_DIR=~/Tools/ghidra_12.1.4_PUBLIC uvx pyghidra-mcp --transport streamable-http --project-path ~/mp_ghidra/maxpayne --project-name maxpayne "<Max Payne PC>/game/MaxPayne.exe"` -> http://127.0.0.1:8000/mcp (pi `mcp` ghidra server). 11.3.2 is too old for pyghidra-mcp 0.3.0 (needs >=12).
  Wait for analysis_complete via ghidra_list_project_binaries.
- Xbox side: no XBE loader in Ghidra -> trace via payne-recomp-main/docs/mouse-look.md + src/recomp/gen/*.c (sub_002954B0 curve, sub_0002E6D0 look update).
- Current mod code: asi/maxpayne_combat.cpp g_padRX l.559, PlayerLooking l.619, FreeWalk/turn-in-place l.616-680. Next per STATUS: right-stick pitch rate + radial deadzone.
- English swap parked: "Max Payne PC/ENGLISH-SWAP-NOTES.md".

## PC mouse-look pipeline (traced 2026-11-17, MaxPayne.exe, Ghidra+runtime hooks)
Chain: X_InputDeviceMouse::update 0x519070 -> getMouseDelta 0x5195c0 -> axis getter 0x51a160 (case1 X / case2 Y)
 -> 0x4ce160 -> bound-action getters 0x4cdc70 (+kbd override +1/-1) -> 0x4cc4c0/0x4cc520 (gate 0x516d60/0x752f10)
 -> controller vtable 0x7cf970 slots 0x48 (0x4ceab0, yaw) / 0x4c (0x4cead0, pitch); both return 0 if byte [this+0x2d] set.
Consumer: controller update 0x4cecd0 (this, dtArg). dt = FUN_007269f0(arg) -> [ebp-4].
 Plain path (0x4cefae): pitchDelta[+0x19] = slot4c()*180*dt*(pi/180); yawDelta[+0x1d] = slot48()*180*dt*(pi/180)  (radians, = rate*dt)
 Aim/lock-on branch (+0x2d set, 0x4ced06..): re-derives angles from target dot/atan2, turn-rate clamp dt*9.42478 (3pi rad/s).
Per-sample math:  d = raw * (sens*1.4+0.1) * scale (sens 0.5 -> 0.8, scale 1.0), then 2-tap carry smoothing (0x519370: out=acc*0.5)
 rate = d/dt_mouse * 0.0025 ; angle = rate*dt  =>  ~0.0025 rad (0.143 deg) per smoothed count; NO accel curve, NO deadzone, linear.
 Y inverted via byte [mouse+0x51]. Mouse update ~120 Hz.
Other pitch-getter user: 0x765eb0 (UI/other state, 2x call rate, thresholds via 0x766040) - not gameplay camera.
Trace tools: ~/mp_ghidra/hook/{mplook,mpcallers,mpvt}.cpp (.asi in game/scripts, logs mplook.log/mpcallers.log/mpvt.log)

## Xbox right-stick look (runtime-traced 2026-11-17, payne-recomp)
- sub_002954B0 per-axis (NOT radial) deadzone on ctrl+0x85 (X)=0.45, +0x89 (Y)=0.60 (runtime values, sens ctrl+0x1E=1.0, invert +0x1D=0).
  x=(|s|-dz)/(1-dz) for |s|>dz else 0; rate = (1.5*sens+0.5) * (0.8*x)^3 *sign  -> full stick = 1.024 (turn-rate multiplier)
- Input obj sub_0002E6D0: +0x25 = LookX*pi*dt (0.0536/frame @60Hz full stick => 1.024*pi rad/s = 184 deg/s). Scripted keyboard stick is instant full, no ramp.
- Observed rateX is smooth from 0 to 1.024 (cubic). Y not exercised in run 2 (rateY stayed 0: deflection under 0.6 dz or untested).
- Mod trace patch: src/recomp/gen/recomp_0060.c (log line after loc_00295935, backup /tmp/recomp_0060.c.bak) + src/recomp_manual.c mp_stick_wrap (unused: direct calls bypass overrides). Env RECOMP_STICK_LOG=1.
- Y axis confirmed (run 3): same cubic, max 1.024, dzY=0.60; pitch uses +0x29=LookY*pi*dt -> same 184 deg/s cap as yaw.
- Applied in asi/maxpayne_combat.cpp ShapeStick() (called at top of ForceAim): per-axis dz 0.45/0.60 + cubic; kLookGain untuned. Built, NOT yet installed/tested.

## Max Payne camera: consumer chain + port attempt v1 (2026-11-17)

### PC (MaxPayne.exe): where the look deltas end up
The mouse/stick path (section above) stops at controller update `0x4cecd0`. Its output leaves through the
controller vtable `0x7cf970`: slot `0x48` = `0x4ceab0` (yaw delta), slot `0x4c` = `0x4cead0` (pitch delta),
both returning 0 while `[this+0x2d]` (aim/lock-on) is set.
- Only gameplay caller (runtime-hooked, 3 return addresses out of a 20 s session): `FUN_007429b0` —
  calls at `0x7431ed` / `0x743206` / `0x74321f` through the proxy wrappers `FUN_00503280/005032b0/005032e0`
  (vtable `0x7cffa8/ac/b0`, inner object `*(this+0x1b5)`), results pushed into setters
  `FUN_00742960` (`+0x7e`), `FUN_00742920` (`+0x7a`), `FUN_00742990` (vec `+0x86`).
- Yaw consumer: `0x68857f` -> `FUN_00742910()` + turn speed `0x6d7100`, wrapped to 0..2pi (const `0x7d5ce0`),
  -> `0x694430` (node) -> `0x6d7110(yaw)`. Yaw is written into a scene node.
- Pitch consumer: `0x68818d` -> `FUN_00742950()` -> `0x6916d0` -> `0x725b30`: `[ecx+0x20] += arg`, clamped to
  `+-0x3fc6d3f2` = +-1.5553 rad (89.1 deg), then builds a cos/sin matrix at `[ecx+0x14]`.
- **Yaw and pitch are consumed by two different objects.** MP's camera is not one rigid orbit: yaw goes to a
  node, pitch to its own pitch-pivot. That is why MP can sit at 89 deg pitch with no up-vector problem.
- `FUN_00743b10` is a separate decaying camera kick (limit `[+0x92]`, decay `[0x5e53d0]*[+0x96]`, sign flips on
  reversal) — not look, do not port it as look.
- `0x74e8e0` = `lea eax,[ecx+0x30]; ret` -> MP's camera offset vector is per-object memory (`obj+0x30`), NOT a
  constant. Any "port" using invented constants (2.4 / 0.30 / 0.62) is a guess, not a port.
- MP's exact clamp value is right for MP and wrong for an orbit (see below), so it must not be copied blindly.

### GTA SA camera (for contrast)
- `CCam::Process` `0x5279e5` -> `0x50F970` (SA's PC/mouse 3rd-person cam) only if byte `0xB6EC2E`
  m_bUseMouse3rdPerson && !byte `0x8CCF00`; otherwise `0x527A09` -> `0x522D40` Process_FollowPed_SA (the console
  follow cam the pad uses). Aim `0x527a95` -> `0x521500` Process_AimWeapon.
- `CCam::GetVectorsReadyForRW` `0x509CE0`: normalises front, and when `front.x == 0 && front.y == 0` forces both
  to `0.0001` and crosses with `(0,0,1)` -> near-vertical pitch yields a garbage `m_vecUp` = the 90 deg view roll.
- SA's own 3rd-person mouse cam (`Cam.cpp`, gta-reversed): front `(-cos h * cos v, -sin h * cos v, sin v)`;
  mouse `h += -3*mx*fov*m_fMouseAccelHorzntl`, `v += +4*my*fov*m_fMouseAccelVertical`, fov = `m_fFOV/80`;
  `CCam::ClipAlpha` clamps v to -85.5..+60 deg. Ped heading `= atan2(front.x, front.y) - 90 deg`.
- The pad state read inside camera code is **raw**: `ForceAim` shapes `NewState.RightStickX/Y` later in the
  frame, so camera code must apply `ShapeStick()` itself (evidence: stick -83 gave exactly 0.6535*3.216 rad/s).

### Port attempt v1 (reverted the same night, user order)
Implemented `MPCam()`: own yaw/pitch accumulators, Xbox rate `1.024*pi rad/s`, mouse `0.0025 rad/count`,
pitch clamp, `front = (-cos*cos, -sin*cos, sin)`, rigid orbit `pivot = ped + 0.62`, `src = pivot - front*2.4 +
right*0.30`, `ProcessLineOfSight` `0x56BA00` pull-in, then drove `m_vecSource/Front/Up/HorizontalAngle/
VerticalAngle/Distance`. Resume from other cameras (aim/vehicle/cutscene, detected as >40 ms gap between camera
frames) blended by adopting the rendered `m_vecFront` angles over 0.25 s.
- **Fixed, measured**: strafe/back-diagonal jitter gone (rigid to the ped: `lat` stayed 0 on the virtual pad too),
  pitch fully free (0.16 rad/s at 33% stick vs SA cam's ignored pitch), no self-recentring.
- **Broke**: after aiming the camera jumped to ~2.4 m above the head looking straight down. Cause: SA's aim cam
  front is near-vertical (`fz = -0.992`, it points at the target); adopting it produced pitch 1.44 rad, and at
  that pitch the orbit's up-vector degenerates. Attempted fix (keep our angles, ease rendered angles back to them
  over 0.25 s) was built but never tested.
- **Judged**: framing was "too close, generic" — the guessed constants, not MP's real offset (`obj+0x30`).
- **Regression signature (user m01239)**: the state that followed put the two old symptoms back exactly — strafe
  left-right jitter and no pitch at all. Treat "jitter + dead pitch" as the fingerprint of a build that is running
  an SA-follow-cam path (or the placeholder mode 1), not of a Max Payne camera. Detect it from the trace: `cf`
  lines with `lat` alternating 0 / 0.03 / 0.05 m and `fz` pinned near 0 = follow cam; `lat` 0 with `yaw`
  accumulating monotonically on stick input = the orbit.
- Reverted to the previous code (placeholder mode 1 = SA PC cam fed Xbox-rate stick + the three band-aids +
  `g_pitchOfs`). The user's decision (m01270, paraphrase) is to keep that code as the base and record the
  regression, so no camera edits are pending.
- Because the project has no git, a mismatch between the edited source and the `maxpayne_combat.asi` actually in the
  game dir is the most likely explanation for an "old behaviour came back" report: record md5 + install time for
  every tested build (`md5sum asi/maxpayne_combat.asi "<game>/maxpayne_combat.asi"`). Current state: md5
  `458c53ee5a35`, built 04:29:05, installed 04:29:12.

### What "the camera fights back" actually is
MP's curve `rate = (1.5*sens+0.5)*(0.8x)^3` on top of a per-axis deadzone (`0.45` X / `0.60` Y) was traced in
Xbox MP. With the pad's own deadzone (`GInputSA.ini RightStickDeadzone=27` = 0.213) subtracted first, MP's curve
then removes another 60% of travel: below ~60% deflection the output is ~2% of max, below ~45% it is **exactly
zero**. That dead band is the "fighting back", and it is independent of which camera is active — it is shaping.
Fix belongs in `ShapeStick()`, not in the camera.

### Reusable knobs/evidence
- Camera-frame trace columns (temp hook): `cf <tick> m=<g_camMode> dt=<ms_fTimeStep/50> rs=<RX,RY> ls=<LX,LY>
  yaw=<atan2(front.y,front.x)> fz=<m_vecFront.z> h=<m_fHeadingCurrent> lat=<lateral offset> dist=<|ped-cam|>`.
  `fz = -0.99` == camera ~straight down; `lat` != 0 == the strafe shake.
- Test hooks still in the source (remove when done): `mp_cammode.txt` (writes `g_camMode`), `mp_trace.txt`
  (`cf` per camera frame); `tools/vpad.py --serve` must run before launching if using the virtual pad;
  `tools/padtest.py` for scripted sweeps. Install only while the game is closed (`install.sh` refuses otherwise).

## Max Payne camera position: where the offset lives (2026-11-17, PC MaxPayne.exe, Ghidra)

`FUN_00687f20` (character frame update) builds the camera transform:

- `FUN_00742950()` = pitch, `FUN_00725b30(pitch)` accumulates it into the pivot object and rebuilds its
  cos/sin matrix at `[obj+0x14]`, clamped to +-1.5553 rad (`0x3fc6d3f2`).
- `FUN_0074e8e0()` = `lea eax,[ecx+0x30]`: the player node's 3x3 basis. Its nine floats are copied to a
  local matrix, an Euler triple from `FUN_00744e30()` (degrees, multiplied by `0.017453292`) is turned into
  a rotation by `FUN_006b71b0(radX, radY, radZ, 0)`, and `FUN_00642c00(matrix)` applies the result.
- Camera position is then `node[+0x24..+0x2c] + player[+0xbb..+0xc3]`:
  `FUN_0074e8e0()` gives the node, `FUN_006d74c0()` = `lea eax,[ecx+0xbb]` gives the per-player camera
  offset vector, and `FUN_00648d00(pos, FUN_0074fe10()=obj+0x50)` hands position + basis to the camera.
- So Max Payne's camera offset is a **runtime vector at player+0xbb**, not a constant: it is zeroed in the
  player constructor (`FUN_006d6490`) and recomputed each frame elsewhere. Still to find: the routine that
  fills `+0xbb` (the orbit radius/height/pitch response live there).

## Max Payne camera geometry from the recomp view matrix (2026-11-17)

Problem: the PC exe only exposes the offset as runtime state (`player[+0xbb]`, writer not found), and an ASI
patching the two call sites (`0x688194`, `0x6884f4`) crashed the game before `DllMain` logged anything. The
recomp has source, so the cheapest ground truth is the matrix the game actually renders with.

Method: `dev_SetTransform` in `xboxrecomp/src/d3d/d3d8_device.c` writes every 4th `D3DTS_VIEW` matrix to
`mpcam.log` when `MP_CAMLOG` is set, as `v <eye xyz> <forward xyz>` with `eye = -(t . R^T)` from row 4
(`m[12..14]`) and `forward = (m[2], m[6], m[10])` (D3D view space looks down +Z). `./launch.sh mp` with
`MP_CAMLOG=1`. Then fit with `tools/fitcam.py` (least-squares point closest to all rays) or per-window sphere
fits: a genuine orbit gives a small radius spread (<0.03 in the good windows), mixed cameras give hundreds.

Numbers (units are Max Payne world units; the pivot is `0.95` above the feet, so 1 unit ~ 1 m at character scale):
- Eye sits on a sphere of radius **2.72** around a pivot **0.95** above the feet. Verified on a sweep at nearly
  fixed yaw: `pivot.z = eye.z + 2.72*sin(pitch)` returns 8.24 at pitch -72.8 deg and 8.30 at pitch +2.0 deg.
- Pitch range: down **-74 .. -75 deg** (matches the 1.5553 rad clamp found at `0x725b30` in the PC exe: 89 deg
  is the object clamp, the camera stops earlier), up only **+2 deg** observed. Looking up lowers the eye toward
  the ground, so the original is ground-limited, not sphere-limited: with radius 2.72 and pivot 0.95 the eye
  reaches the floor at `asin(0.65/2.72)` = 13.8 deg. Keep the radius and stop there; do not clamp `eye.z`,
  that crushes the distance (0.77 in the mod's first measured build) and reads as "no vertical rotation".
- Dead end worth recording: fitting a *single* pivot over a whole session gives centres that drift by metres
  (8.2 .. 10.3 in z). That is contaminated data — yaw changes during a "pitch" sweep (the stick sweeps were
  diagonal), and cutscene/menu cameras are in the same log. Only per-window fits with a small radius spread
  are trustworthy, and a controlled sweep should log the ped position alongside the camera.

Reproduce: build the recomp (`cd build/Release && cmake --build . --target payne_recomp`), `MP_CAMLOG=1
./launch.sh mp`, walk two steps so the trace is live, then right stick up 3 s / down 3 s at fixed yaw, then
`python3 tools/fitcam.py mpcam.log` (run from the recomp root; the log is written in the game cwd).

### Update: why MPCam never ran (2026-11-17)
Armed + ForceAim(R1) => CCam::Process picks AIM cam 0x521500 (5 args, no bool), not FollowPed 0x522D40. Hook both (0x527A95 and 0x527A09).

### Replacing an SA CCam Process function: what it must also do (2026-11-17)
Any replacement for a CCam::Process_* (FollowPed 0x522D40, AimWeapon 0x521500) must itself: set `m_fFOV`
(fresh cams after a load have 0 -> black frame) and rebuild `m_vecUp` from `m_vecFront` (what 0x509CE0
GetVectorsReadyForRW does at the end of the originals). With up left at (0,0,1) and a pitched front the camera
matrix (`TheCamera.m_mCameraMatrix`, 0xB6F028) is non-orthonormal and the rendered view ignores the pitch.
SA ped origin is ~1.0 m above the feet.

### MP camera geometry, re-traced (2026-11-17 15:50) — supersedes the 2.72 sphere above
Logger bug: dev_SetTransform eye was -R t (columns); correct is eye_j = -(t . row j). MP world is Y-up.
New recomp env: MP_CAMLOG=1 (mpcam.log: eye, forward(m2,m6,m10), up(m1,m5,m9), settransform count;
mpproj.log: projection _11=1.42815 _22=1.90420), MP_WLOG=1 (mpworld.log: world matrices near the eye; Max's
root object = most frequent one, 1.56 above ground; feet bones at ground+0.07, hip +0.8).
Eye vs pitch relative to root, in the heading frame: see kMpCurve in asi/maxpayne_combat.cpp.
Never sweep near a wall: MP's collision pins the horizontal distance and makes the curve look like a sphere.

## Firing exactly where the dot is (MP hipfire) — 2026-11-17
SA's free aim computes a target from `CCamera::Find3rdPersonCamTargetVector` (0x514970: active cam m_vecSource +
m_vecFront, plus up/right scaled by the 0xB6EC14/0xB6EC10 crosshair multipliers) and stores it in
`CTaskSimpleUseGun::m_TargetPos`; `FireGun` then calls `CWeapon::Fire` (0x742300,
`bool __thiscall Fire(CEntity* firedBy, CVector* startPosn, CVector* barrelPosn, CEntity* targetEnt, CVector* targetPosn,
CVector* altPosn)`) with a pointer to it - or with `targetPosn = nullptr` when it hip-fires (m_TargetPos.x/y == 0), which
makes CWeapon::Fire shoot along the ped's heading.
So the cheap way to put MP's hipfire shots under the dot, without an aim stance, is to redirect the call site inside FireGun
(0x61ECCD, both branches converge there) and pass our own targetPosn. Redirect the CALL site, not the function entry:
CWeapon::Fire is called from 7 places (0x61ECCD, 0x628328, 0x62B109, 0x62B12A, 0x68626D, 0x686283, 0x686787) and the others
must keep SA's semantics.
The target must be the ray hit, not a far point: the bullet starts at the muzzle bone, so aiming at a point 200 m away puts
the impact ~(range/200)*muzzle_offset off at closer ranges. `CWorld::ProcessLineOfSight` 0x56BA00 (the overload with
`CColPoint&`, 0x2C bytes, then `CEntity*&` and 8 bools) from camera source+front*0.5 out to *300; when the first hit is the
player (looking down the camera ray crosses CJ's head), restart from hit+front*0.4 and cast again (3 tries).

## Does SA use the target we pass? (CWeapon::FireInstantHit 0x73FB10, disassembled 2026-11-17)
FireInstantHit is not decompiled, so disassemble it. Recipe that works without Ghidra: parse the PE (e_lfanew at 0x3C,
sections at opt+0x14), map `va -> raw`: `off = raw + (va - base - section_va)`; then capstone (`CS_ARCH_X86, CS_MODE_32`)
over `0x73FB10 .. 0x742300` (2976 insns). Base 0x400000, `.text` va 0x1000 / raw 0x400.
- Prologue: `push -1; push 0x848EBB; push fs:[0]; mov fs:[0],esp; sub esp,0x198`, so the 7 args sit at
  [esp+0x1B8] firedBy, +0x1BC startPosn, +0x1C0 barrelPosn, +0x1C4 targetEnt, +0x1C8 targetPosn, +0x1CC altPosn, +0x1D0
  (muzzle bool). Yes, three pointers start with the same letter; check the reads, not the names.
- 0x73FDD7: `test ebp,ebp` (shooter ped) / `test esi,esi` (targetEnt) / `mov eax,[esp+0x1C8]` (targetPosn) ->
  `je 0x74034A` when both are null: that path fires along the ped's heading. With a targetPosn it jumps 0x73FE0A -> 0x73FE6D.
- 0x73FE6D: copies the target xyz, subtracts the muzzle/origin at [esp+0x2C..0x34], squares and adds the 3 components,
  `fsqrt`, clamps, `fdivr [0x858624]` (=1.0), scales by `call 0x73B380` and `[ebx+8]` (`m_fWeaponRange`, ebx = the
  `CWeaponInfo*` from `call 0x743C60`). So the shot direction is built from OUR point; a target entity is optional.
- `m_fSpread` (+0x68) is NEVER read in FireInstantHit; the accuracy term is `m_fAccuracy` (+0x38) with the ped's skill
  byte at +0x71A (0x73FC04) - read that if long-range hipfire still feels loose.

## Free walk: the body heading is ours to set
In mode 1 the camera never drifts (yaw only moves with the look stick), so the body can simply be aimed at the camera
every frame: `g_walkHeading = CameraHeading()` while free-walking, and `m_fHeadingGoal = g_walkHeading` in CalcVelocityHook
(0x5E92ED -> CPed::CalculateNewVelocity 0x5E4C50). The old `PlayerLooking()` (>7000 raw stick) gate was a leftover from
mouse-look days and is what let CJ keep an old diagonal heading.
