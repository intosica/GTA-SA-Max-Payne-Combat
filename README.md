# max-payne-combat

GTA San Andreas `.asi` project: bring Max Payne 1's gun-wielding animation set and a reasonable approximation of Max Payne enemy AI into SA.

Status: **working build (2026-11-17)**. Max's full-body locomotion replaces SA's (idle/walk/run/strafe/backpedal, free-walk without pivot, stand-still turns, pose inertialization), RPG/Bazooka carry + sprint + jump work. Mouse-shooting walk-forward bug no longer reproduces (STATUS 2026-11-16 03:00). Open: enemy AI, dodge directions, frame-limiter behaviour, rare crash. Gameplay integration (moving while shooting, AI) still open — see `docs/STATUS.md`. SA rig conventions for future imports: `../../mod creation tools/sa_anim_baseline/SA_ANIMATION_RIG_BASELINE.md`.

Recent (2026-11-17): Max Payne pad camera (opening menu -> PAD CAMERA -> MAX PAYNE) with the measured MP pitch curve
(up/down look, no feet when looking down), collision pull-in, MP gunfire with no aim stance (bullets leave along the
crosshair, optional PINPOINT accuracy for hipfire and for aiming), motorcycle-enter body snap fixed, and a two-column
in-game menu so every option is on one screen.

See `docs/STATUS.md` for the exact current build, what works, and the remaining work.

## Goal

1. Use Max Payne 1's original armed animations (idle-aim, fire, reload, shootdodge, strafes, pain/death) on SA peds.
2. Export them as SA `IFP` animations and play them from a plugin-sdk `.asi`.
3. Add lightweight enemy AI inspired by Max Payne: strafing, cover use, burst fire, reaction delay, flanking. "Within reason" — no full port of MP's FSM.

## Sources

- PC Max Payne 1 repack: `/media/montel/Extreme SSD/Games/Max_Payne_1_New_Edition_by_Cheshire28.iso`
  - Installed to `/media/montel/Extreme SSD/Games/Max Payne PC/game` with Wine (Inno Setup silent install).
  - Unpacked archives: `/media/montel/Extreme SSD/Games/Max Payne PC/extract` (3095 files).
  - Animation source: `/media/montel/Extreme SSD/Games/Max Payne PC/extract/data/database/skeletons/default_skeleton/anim/`
  - Skeleton: `/media/montel/Extreme SSD/Games/Max Payne PC/extract/data/database/skeletons/default_skeleton/maxpayne-collision.kfs`
  - Skin: `/media/montel/Extreme SSD/Games/Max Payne PC/extract/data/database/skins/max_payne/PAYNE_MAX_L0.SKD`
- Xbox recomp (`/media/montel/Extreme SSD/Games/payne-recomp-main`) is **not** an asset source; it is a code recompilation with no game assets.
- Xbox ISOs (`/media/montel/Extreme SSD/Games/Max Payne/`) are not needed for this mod; PC assets are the same data and easier to parse.

## Layout

```
max-payne-combat/
  README.md          this file
  docs/
    RESEARCH.md      formats, skeleton maps, extraction commands
    PLAN.md          implementation plan / open problems
  asi/               C++ plugin-sdk source and build output
  tools/             KF2 -> IFP converter, retarget, validation, DFF dump helpers
  assets/            generated IFPs and data (not committed game assets)
  docs/TESTING.md    smoke test and key bindings
```

Tools/SDKs live in `../mod creation tools/`, per the repo convention. Relevant ones:

- `plugin-sdk-sa-master/` — SA plugin SDK and `plugin.h`
- `gta-reversed-master/` — SA reverse-engineered headers and animation loader reference
- `libras/` — not in this repo; used to unpack `x_*.ras`, source at `/media/montel/Extreme SSD/Games/Grand Theft Auto San Andreas/mod creation tools/libras`

## Build / install

```
./build.sh
./install.sh
```

`build.sh` compiles `asi/maxpayne_combat.asi` against `../mod creation tools/plugin-sdk-sa-master`. `install.sh` copies the ASI plus all four IFPs next to `gta-sa.exe`; it refuses while GTA SA is running. The existing ASI loader is `dinput8.dll`. Retail and Project Eagle builds both need testing.

## Controls / behavior

- `Select+B` opens/closes the Max Payne menu; `B`/Escape close it; D-pad/arrow keys navigate; `A`/Enter play.
- While armed, the ASI plays full-body `MPC` clips for idle/hold, fire, and reload. This is the current working baseline; the pose quality is not final.
- Shotgun: 8-shell Max Payne magazine, `R` performs the manual reload, HUD shows `SHELLS n/8`.
- Rocket launchers (RPG/heat-seeker): Max's legs under SA's rocket carry (`ROCKET*` clips); sprint (Cross) and jump work (heavy flag cleared at runtime).
- `Ctrl+5` plays the full-body forward shootdodge; jumping while aiming triggers it automatically.
- `F9` toggles the player automatic layer; `F10` toggles the enemy layer.
- `F6`/`F7`/`F8` play full-body `idle_aim` / `ShootBerettaDual` / `ReloadBerettaDual` for testing.
- Enemy peds currently get the same full-body/upper clips only when nearly stationary; a real MP combat FSM is not implemented.
- Holding `Select` no longer changes the native camera/replay: the ASI zeroes `CPad::NewState.Select` after reading the menu combo.

## Hard constraints

From `../AGENT-HANDOFF.md`:

- Retail `gta-sa.exe` md5 `170b3a9108687b26da2d8901c6948a18`; Project Eagle `gta_sa.exe` md5 `31c795929648d9476441cfe473a4c275`.
- Never run the game while the user is playing.
- Never `wineserver -k`, `pkill gamescope`, or kill `gta-sa.exe` without telling the user; use the bracket trick (`pkill -f '[g]ta-sa.exe'`).
- Test in a separate Wine prefix / desktop, not the user's live session.
- One folder per mod project; keep generated game assets out of git.

## Known issues

- Standing: full-body MPC clips. Moving: MPU upper-body clips as partial anims over SA strafe legs (works; legs are SA's, not MP's).
- Enemy peds only get stationary animation overrides.
- Save loading can take ~40 s; do not declare an infinite load before `tick` appears in the log.
- Launch with `./launch.sh` only (gamescope is mandatory; `GTA_GAMESCOPE` is ignored).

## Next milestone

1. Dodge directions, enemy behaviour, frame-limiter fix, crash hunt, packaging.

## Regenerating the animations

```
tools/build_ifps.sh && python3 tools/validate_ifp.py && ./install.sh
python3 tools/plot_pose.py idle_aim ShootBerettaDual --frame 5 --out /tmp/poses.png
```
