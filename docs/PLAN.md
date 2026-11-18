# Plan

Status as of 2026-11-13: **working test build**. Retarget fixed (RESEARCH.md §9); all four blocks export and load. See `STATUS.md`.

## Phase 0 — Retarget proof (done)

Acceptance: one MP clip (`idle_aim` or `ShootBerettaDual`) converted to an SA IFP that, in-game or in a local skeleton renderer:

- stands upright (head above pelvis, feet below);
- right hand is raised near the head for `idle_aim`, left hand lower;
- both hands raised for `ShootBerettaDual`;
- no 90/180-degree limb flips;
- loops cleanly for `idle_aim`.

Done:

1. Aim-based retarget implemented in `tools/retarget.py`; corrected the clavicle parent (Neck, not Spine1).
2. Frame-0 poses validated by joint positions for `idle_aim`, `ShootBerettaDual`, `ShootMP5Burstshot`.

Done: aim retarget implemented; all core standing clips pass `tools/validate_ifp.py` with zero failures. Remaining: in-game visual check.

## Phase 1 — IFP exporter (done)

`tools/kf2_to_ifp.py`:

- input: list of MP `.kf2` clips;
- output: `assets/MP_COMBAT.IFP`, package `MPC`;
- resample all clips to a fixed 30 fps (or 60 if needed);
- write ANP3 with type 4 root + type 3 children, bone IDs from the SA map;
- convert MP local rotations to SA local rotations using the Phase 0 retarget;
- root translation: MP pelvis world translation minus MP pelvis rest, mapped to SA root; keep the character's feet at ground level;
- metadata: clip name = MP file stem, loop flag from MP animation, duration.

Shipped clip sets:

- `assets/MP_COMBAT.IFP` (block `MPC`, 10 clips): `idle_aim`, `ShootBerettaDual`, `ShootMP5Burstshot`, `ShootColt`, `ShootIngram`, `ShootPumpShotgun`, `ShootSniper`, `ShootDesertEagle`, `ReloadBerettaDual`, `ReloadMP5`.
- `assets/MP_COMBAT_UPPER.IFP` (block `MPU`, 14 clips): upper-body versions for partial blending, including `ShootBeretta`, `ReloadBeretta`, `ReloadPumpShotgun`, `ReloadIngram`, `ReloadDesertEagle`.
- `assets/MP_DODGE.IFP` (block `MPD`, 4 clips): root motion retained and ground-clamped; loaded by the ASI, Ctrl+5 test and jump-while-aiming trigger.

Keep the clip names stable; the ASI will reference them by name.

## Phase 2 — ASI playback (first build done)

`asi/max_payne_combat.cpp`:

Implemented in `asi/maxpayne_combat.cpp`:

- loads `MP_COMBAT.IFP` and `MP_COMBAT_UPPER.IFP` via `CAnimManager::LoadAnimFile`;
- blends upper-body clips with `ANIMATION_PARTIAL` so movement/tasks keep running;
- player state machine: aiming -> `idle_aim`, firing -> weapon-specific shoot clip, reloading -> reload clip;
- enemy layer applies the same clips to hostile cops/gang peds in combat;
- F9/F10 toggles, Ctrl+5 dodge, F6-F8 full-body manual tests, log at `maxpayne_combat.log`;
- graceful no-op if an IFP is missing.

Acceptance: player can aim and fire with the MP clips; no crashes; vanilla animations still work when the mod is disabled. **Pending in-game test.**

## Phase 3 — Enemy AI ("within reason")

Goal: make a subset of enemy peds behave more like Max Payne enemies, not a full FSM port.

Current implementation: hostile cops/gang peds keep SA combat AI but use MP upper-body aim/fire/reload clips. Full behavior replacement (cover/burst/reaction hooks) is deferred; do not add fragile movement hooks until the animation layer is verified.

Minimal behaviors:

- **reaction delay** after spotting the player (0.2-0.6 s);
- **burst fire** with pauses instead of continuous fire;
- **strafe** left/right while shooting, using the MP strafe clips;
- **cover preference**: move to nearby cover when suppressed/low health;
- **flank**: if the player is behind cover, try a side approach;
- **shootdodge**: rare, only for high-tier enemies, with a cooldown.

Implementation options:

1. Hook `CTaskSimpleUseGun` / `CTaskComplexKillPedOnFoot` and inject delays/strafe tasks.
2. Add a custom `CTaskComplexMaxPayneCombat` that runs a small state machine and delegates to vanilla tasks.
3. If hooking is too fragile, use a CLEO/ASI script layer that issues tasks to selected peds.

Prefer option 2 only if option 1 cannot express the behavior; prefer vanilla tasks over custom movement code.

Acceptance: 3-5 enemies in a test area use bursts, strafe, and cover; no infinite loops or stuck peds; performance impact small.

## Phase 4 — Packaging

- `asi/max_payne_combat.asi`
- `assets/MP_COMBAT.IFP`
- `README` install steps and a `docs/TESTING.md` checklist
- optional `max_payne_combat.ini` for toggles (AI on/off, clip speed, debug)

Do not redistribute Max Payne assets. The IFP contains derived animation data; keep it out of public releases unless the user confirms the legal position.

## Risks

- **Retarget**: bind-axis mismatch; may need aim/IK instead of pure rotation copy.
- **IFP blending**: SA's animation groups and bone masks may reject custom clips; test early.
- **AI hooks**: SA task/state machine is fragile; prefer vanilla tasks and small hooks.
- **Project Eagle**: different exe/build; test both, but retail first.
- **Crash safety**: never test in the user's live session; use a separate prefix/desktop and log.

## Immediate next action

User-ordered task list (2026-11-15), one at a time, user tests each in game:
1. [built, awaiting test] Head look-at IK off (menu SA HEAD TRACKING) + still Stand idle — fixes random head and
   the leg sway the user tied to it.
2. [todo] "Tank movement" toggle: MAX PAYNE CONTROLS ON = always face crosshair (MP style, done);
   OFF must still allow dodge and bullet time. The right stick drives the camera either way.
3. [todo] NATIVE BACKWARD WALK OFF must play Max's real backpedal when walking toward the
   viewer while NOT aiming (currently only the aim-mode GUNMOVE_BWD is Max's).
4. [todo] Dodge + bullet time on the Xbox-style scheme; settings in the UI later.
5. [later] Aiming: SA's free aim is poor; user may install AdvancedAimingMod 1.3
   (mod creation tools/advanced-aiming-mod-master, AdvancedAimingMod.1.3) — keep the pad hook
   (0x53BEE6) and camera untouched so it stays compatible. Not installed yet: baseline first.
   imCamAim.asi patches CCam aim-weapon code (0x521500, 0x521632, 0x527A95, data 0x8CC4A0-C0);
   no overlap with our hooks (0x53BEE6 pads, 0x60F459 look flag).
