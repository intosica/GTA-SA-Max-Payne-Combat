# Testing

Quick path (no manual menus): `./build.sh && python3 tools/quickload.py --install`, then
`python3 tools/drive.py w:2 a:1 s:1`, then `python3 tools/mvstats.py`. See AGENT-HANDOFF.md §0n.

## Preconditions

- GTA SA not running (`pgrep -f '[g]ta-sa.exe'` empty) and nobody playing.
- `./build.sh && tools/build_ifps.sh && python3 tools/validate_ifp.py && ./install.sh`

## Launch

```
cd "/media/montel/Extreme SSD/Games/Grand Theft Auto San Andreas" && ./launch.sh
```

Gamescope is mandatory. Main menu → **Load Game → second "Big Smoke" save**
(CJ already has guns). Loading takes ~40 s; wait for `tick` in `maxpayne_combat.log`.
Headless: focus the gamescope window with `xdotool windowfocus --sync`, send keys with
`xdotool key --clearmodifiers`, screenshot with `scrot -o`. Close with `./close.sh`.

## Smoke test

1. Start the game.
2. Check `maxpayne_combat.log` says `load MP_COMBAT.IFP block=MPC` and `version=...`.
3. Press `Select+B`; the Max Payne menu should draw and `B`/Escape should close it.
4. Equip a pistol and aim: full-body `idle_aim` should play.
5. Fire: `ShootBeretta`; reload: native `B` state triggers `ReloadBeretta`.
6. Equip a shotgun: `SHELLS 8/8` HUD; fire down to 0; press `R` to reload.
7. Hold `Select`: native camera/replay must not change.
8. Enemy peds: current build only overrides stationary hostiles; do not expect MP AI.

## Menu toggles (Select+B / M)

- MAX PAYNE BODY ANIMS — Max's idle/walk/run/aim-walk clips replace SA's on CJ.
- MAX PAYNE CONTROLS — armed on foot, pad only (mouse/keyboard play stays native): CJ always faces the crosshair, stick strafes (MP Xbox).
- NATIVE BACKWARD WALK — walking toward the camera uses SA's walk instead.
- OLD SHOOT OVERLAYS — earlier layered aim/fire poses (off).
- MAX IDLE LOOK-AROUND — Max's full idle (looks right/left/right after stopping; 1:1 MP). Off = still idle.
- STILL TORSO SIDESTEP / STILL TORSO BACKPEDAL — upper body held still while strafing / backpedalling (as Max). Off = clip's torso swing.
- SA HEAD TRACKING — SA's head look-at IK (off: head follows Max's animation only).
- ENEMY ANIMS.

## Manual keys

- `Ctrl+5` full-body forward shootdodge
- `F6` full-body `idle_aim`
- `F7` full-body `ShootBerettaDual`
- `F8` full-body `ReloadBerettaDual`
- `F9` toggle automatic player animation layer
- `F10` toggle enemy animation layer

Manual keys bypass the state machine and are for checking the IFP itself.

## Expected failures / limits

- Full-body manual clips override movement; do not use them while walking.
- Enemy behavior is still SA combat AI; the mod only overrides animations.
- If `MPC` fails to load, the ASI is inert; check the log and that `MP_COMBAT.IFP` is next to `gta-sa.exe`.
- `tools/validate_ifp.py` checks every frame: barrel forward (aim/shoot), upright body, no quat flips.

## Automated boot / quit (2026-11-15 16:35)
- `tools/record_pad.py [--name N] [--launch]`: records the Xbox pad (/dev/input/by-id/*-joystick,
  js API) to tools/recordings/N.jsonl; each press gets a screenshot (/tmp/mp_rec/N/, temporary)
  and a fuzzy md5 (32x18 grey, 3-bit) plus in_world from maxpayne_combat.log. Stops when the game exits.
  Pad map: DPADY+ = down, DPADX+ = right. Screenshots lag ~0.1-0.5 s: in fast press bursts a shot
  can show the result of the press, not the screen it acted on.
- recordings/boot_to_world.jsonl (user run): A (skip NVIDIA), START (skip R*), A, main: A,
  Game: Down, A (LOAD GAME), Down (slot 2 BIG SMOKE), A -> in world ~19 s later;
  quit: START, Down x3 (RESUME->MAP->BRIEF->QUIT GAME), A, Down (NO->YES), A.
  Identical to autostart.py's ACTIONS, which already boots hands-free.
- `tools/autostart.py` = load save; `tools/autostart.py --quit` = FORCE kill (user: no menu quit).
