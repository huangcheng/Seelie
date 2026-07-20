#!/usr/bin/env python3
"""Convert an FBX (or .blend/.obj) character to a Seelie Model3D .glb.

Requires Blender >= 3.6 on PATH. Runs headless:
  python3 scripts/fbx_to_glb.py input.fbx --out pack/model.glb

Validates the export has an armature and at least one animation, then
re-exports as .glb with embedded textures.
"""
import argparse, subprocess, sys, tempfile, textwrap, pathlib, shutil

BLENDER_SCRIPT = textwrap.dedent("""
    import bpy, sys, json
    argv = sys.argv[sys.argv.index("--")+1:]
    src, out = argv[0], argv[1]
    bpy.ops.wm.read_factory_settings(use_empty=True)
    ext = src.lower().rsplit(".", 1)[-1]
    if ext == "fbx":
        bpy.ops.import_scene.fbx(filepath=src, automatic_bone_orientation=True)
    elif ext == "obj":
        bpy.ops.wm.obj_import(filepath=src)
    else:
        bpy.ops.wm.open_mainfile(filepath=src)
    armatures = [o for o in bpy.data.objects if o.type == "ARMATURE"]
    if not armatures:
        print("ERROR: no armature found", file=sys.stderr); sys.exit(2)
    if not bpy.data.actions:
        print("ERROR: no animations found", file=sys.stderr); sys.exit(3)
    bpy.ops.export_scene.gltf(
        filepath=out, export_format="GLB",
        export_animations=True, export_skins=True,
        export_image_format="AUTO", export_yup=True)
    joints = len(armatures[0].data.bones)
    print(json.dumps({"ok": True, "joints": joints,
                      "actions": len(bpy.data.actions)}))
    if joints > 64:
        print(f"WARNING: rig has {joints} joints (>64) — will be clamped at "
              "runtime on some GL stacks; consider removing leaf/finger bones.",
              file=sys.stderr)
""")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("--out", required=True)
    ap.add_argument("--blender", default=shutil.which("blender") or "blender")
    a = ap.parse_args()
    with tempfile.NamedTemporaryFile("w", suffix=".py", delete=False) as f:
        f.write(BLENDER_SCRIPT)
        script = f.name
    pathlib.Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    r = subprocess.run([a.blender, "--background", "--python", script,
                        "--", a.input, a.out])
    sys.exit(r.returncode)

if __name__ == "__main__":
    main()
