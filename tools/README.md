# Tools

- `retarget.py` — MP KFS/KF2 loader + MP→SA retarget (`Retargeter`); uses `../../../mod creation tools/sa_anim_baseline/sa_rig.py`.
- `kf2_to_ifp.py` — export clips to ANP3 (`--upper`, `--arm`, `--root-motion vertical|full|none`, `--ground-clamp`, `--block`).
- `build_ifps.sh` — regenerate all four IFPs in `../assets/`.
- `validate_ifp.py` — per-frame game-space checks (gun forward, upright, no flips).
- `plot_pose.py` — MP vs SA stick figures with gun axis (PNG).
- `dump_dff.cpp` — legacy librw DFF dumper (superseded by `sa_rig.Rig`).

Do not commit extracted Max Payne assets here. Only source and generated test clips.
