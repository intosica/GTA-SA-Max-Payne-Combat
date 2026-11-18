# Run inside Blender (Blender MCP / Text editor). Builds CJ (torso+legs+feet+head+hands) and plays
# /tmp/<tag>_<clip>.json from tools/dump_pose.py on it, one CJ per tag, side by side.
import bpy, json
from mathutils import Matrix

ASSETS = "/media/montel/Extreme SSD/Games/Grand Theft Auto San Andreas/mod creation tools/blender_assets/"
PARTS = ["torso", "legs", "feet", "head", "hands"]


def build_cj(name):
    arm = None
    for part in PARTS:
        before = set(bpy.data.objects)
        bpy.ops.import_scene.dff(filepath=ASSETS + part + ".dff")
        new = set(bpy.data.objects) - before
        a = next(o for o in new if o.type == "ARMATURE")
        if arm is None:
            arm = a; arm.name = name
            continue
        for o in new:
            if o.type == "MESH":
                o.parent = arm
                for m in o.modifiers:
                    if m.type == "ARMATURE":
                        m.object = arm
        for o in new:
            if o.type != "MESH":
                bpy.data.objects.remove(o)
    return arm


def apply(arm, path, offset_x=0.0):
    d = json.load(open(path))
    byid = {b["bone_id"]: b for b in arm.data.bones if "bone_id" in b}
    bind = {int(k): Matrix(v) for k, v in d["bind"].items()}
    # DFF armature space vs game space: fit from the pelvis (identity if they agree)
    # DFF armature space = T @ game space: rigid fit of bind joint positions (Kabsch).
    import numpy as np
    ids = [i for i in byid if i in bind]
    A = np.array([list(bind[i].translation) for i in ids]); B = np.array([list(byid[i].head_local) for i in ids])
    ca, cb = A.mean(0), B.mean(0)
    U, S, Vt = np.linalg.svd((A - ca).T @ (B - cb))
    Rm = (U @ np.diag([1, 1, np.sign(np.linalg.det(U @ Vt))]) @ Vt).T
    T = Matrix.Translation(cb - Rm @ ca) @ Matrix(Rm.tolist()).to_4x4()
    print(arm.name, "fit err", float(np.abs((A @ Rm.T + cb - Rm @ ca) - B).max()))
    corr = {i: (T @ bind[i]).inverted() @ byid[i].matrix_local for i in ids}
    arm.animation_data_create()
    arm.animation_data.action = bpy.data.actions.new(arm.name + "_act")
    arm.location.x = offset_x
    for f, fr in enumerate(d["frames"]):
        W = {int(k): Matrix(v) for k, v in fr.items()}
        posed = {}
        for b in arm.data.bones:  # parents come first
            i = b.get("bone_id")
            if i in W and i in corr:
                pa = T @ W[i] @ corr[i]
            elif b.parent:
                pa = posed[b.parent.name] @ b.parent.matrix_local.inverted() @ b.matrix_local
            else:
                pa = b.matrix_local.copy()
            posed[b.name] = pa
            par = (posed[b.parent.name] @ b.parent.matrix_local.inverted() @ b.matrix_local) if b.parent else b.matrix_local
            pb = arm.pose.bones[b.name]
            pb.matrix_basis = par.inverted() @ pa
            pb.keyframe_insert("location", frame=f + 1); pb.keyframe_insert("rotation_quaternion", frame=f + 1)
    return len(d["frames"])


def render_strip(arm, frames, out_dir, view=(4, 0, 0.9), tag=None):
    """Workbench-render arm alone from `view` (relative to its pelvis) at each frame -> PNGs."""
    import os
    scn = bpy.context.scene
    cam = bpy.data.objects.get("ViewCam") or bpy.data.objects.new("ViewCam", bpy.data.cameras.new("ViewCam"))
    if cam.name not in scn.collection.objects:
        scn.collection.objects.link(cam)
    scn.camera = cam
    cam.data.type = "ORTHO"; cam.data.ortho_scale = 2.6
    scn.render.engine = "BLENDER_WORKBENCH"
    scn.render.resolution_x, scn.render.resolution_y = 400, 560
    others = [o for o in bpy.data.objects if o.type == "ARMATURE" and o != arm]
    hidden = []
    for a in others:
        for o in [a] + list(a.children):
            hidden.append((o, o.hide_render)); o.hide_render = True
    os.makedirs(out_dir, exist_ok=True)
    paths = []
    for f in frames:
        scn.frame_set(f)
        pel = arm.matrix_world @ arm.pose.bones[1].head
        cam.location = (pel.x + view[0], pel.y + view[1], 0.0 + view[2] * 0 + 0.0)
        cam.location.z = arm.matrix_world.translation.z + 0.0
        import mathutils
        cam.location = mathutils.Vector((pel.x + view[0], pel.y + view[1], pel.z + view[2] - 0.9))
        d = (pel - cam.location)
        cam.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
        p = os.path.join(out_dir, f"{tag or arm.name}_{f:03d}.png")
        scn.render.filepath = p
        bpy.ops.render.render(write_still=True)
        paths.append(p)
    for o, h in hidden:
        o.hide_render = h
    return paths
