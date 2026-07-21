import bpy, sys, json
bpy.ops.wm.open_mainfile(filepath="/Users/huangcheng/Downloads/ellie.blend")

# --- Flat-color overrides (Sprite Fright scout palette) ---------------------
# The .blend's procedural materials don't export to glTF (uniform brown).
# Name-substring -> sRGB color.
PALETTE = [
    ("head_lips",     (0.80, 0.45, 0.40)),
    ("head_hair",     (0.08, 0.12, 0.14)),   # dark teal-black hair
    ("head",          (0.91, 0.73, 0.55)),   # face — light tan
    ("skin",          (0.91, 0.73, 0.55)),   # body — light tan
    ("mouth_inner",   (0.43, 0.18, 0.18)),
    ("teeth",         (0.96, 0.94, 0.90)),
    ("gums",          (0.71, 0.40, 0.40)),
    ("tongue",        (0.76, 0.42, 0.42)),
    ("hair",          (0.08, 0.12, 0.14)),
    ("eyebrow",       (0.10, 0.10, 0.10)),
    ("eyelash",       (0.10, 0.10, 0.10)),
    ("eyes_pupils",   (0.16, 0.11, 0.07)),
    ("eyes",          (0.96, 0.96, 0.96)),
    ("highlights",    (1.0, 1.0, 1.0)),
    ("earrings_metal",(0.85, 0.20, 0.50)),   # magenta hoops
    ("earrings",      (0.85, 0.20, 0.50)),
    ("denim_inside",  (0.25, 0.38, 0.50)),
    ("denim",         (0.38, 0.53, 0.68)),   # denim jacket
    ("shorts",        (0.25, 0.20, 0.30)),
    ("buttons",       (0.83, 0.69, 0.22)),
    ("pins_blue",     (0.23, 0.42, 0.83)),
    ("pins_gold",     (0.83, 0.69, 0.22)),
    ("pins_light",    (0.91, 0.88, 0.82)),
    ("pins_magenta",  (0.77, 0.23, 0.51)),
    ("pins_green",    (0.23, 0.55, 0.29)),
    ("pins_underside",(0.40, 0.40, 0.40)),
    ("fannypack_magenta", (0.85, 0.20, 0.50)),
    ("fannypack_green",   (0.30, 0.75, 0.30)),
    ("fannypack_purple",  (0.48, 0.23, 0.77)),
    ("fanny_pack_black",  (0.13, 0.13, 0.13)),
    ("fanny_pack_buckle", (0.60, 0.60, 0.60)),
    ("fannypack_zipper",  (0.53, 0.53, 0.53)),
    ("handkerchief",  (0.85, 0.25, 0.15)),   # red-orange neckerchief
    ("scrunchie",     (0.15, 0.55, 0.55)),   # teal scrunchy
    ("watch_strap",   (0.90, 0.75, 0.15)),   # yellow bangle
    ("watch_metal",   (0.60, 0.60, 0.60)),
    ("watch_rubber",  (0.13, 0.13, 0.13)),
    ("watch_gold",    (0.83, 0.69, 0.22)),
    ("watch_glass",   (0.67, 0.80, 0.87)),
    ("watch_red",     (0.85, 0.20, 0.20)),   # red bangle
    ("shoes_leather", (0.10, 0.12, 0.15)),   # black boots
    ("shoes_metal",   (0.60, 0.60, 0.60)),
    ("shoes_loop",    (0.10, 0.12, 0.15)),
    ("shoes_laces",   (0.85, 0.85, 0.85)),
    ("shoes_soles",   (0.15, 0.15, 0.16)),
    ("shoes_seam",    (0.10, 0.12, 0.15)),
    ("shoes_inner",   (0.20, 0.22, 0.25)),
    ("socks",         (0.92, 0.92, 0.92)),
]

def srgb_to_linear(c):
    return tuple(pow(v, 2.2) for v in c) + (1.0,)

assigned = {}
for mat in bpy.data.materials:
    name = mat.name.lower()
    for key, rgb in PALETTE:
        if key in name:
            mat.use_nodes = True
            nt = mat.node_tree
            nt.nodes.clear()
            out = nt.nodes.new('ShaderNodeOutputMaterial')
            bsdf = nt.nodes.new('ShaderNodeBsdfPrincipled')
            bsdf.inputs['Base Color'].default_value = srgb_to_linear(rgb)
            bsdf.inputs['Roughness'].default_value = 0.7
            bsdf.inputs['Metallic'].default_value = 0.0
            nt.links.new(bsdf.outputs['BSDF'], out.inputs['Surface'])
            assigned[mat.name] = key
            break
print(f"palette assigned to {len(assigned)}/{len(bpy.data.materials)} materials")

# --- Select visible meshes + visible armature --------------------------------
n = 0
for o in bpy.data.objects:
    sel = o.visible_get() and o.type in ('MESH', 'ARMATURE')
    o.select_set(sel)
    n += 1 if sel else 0
print(f"selected={n}")

KEEP = {"pose_wave","face_default","face_content","face_excited","face_angry",
"face_annoyed","face_scared","face_scared2","face_awe","face_suspicious",
"face_squint","mouth_smileopen","RIG.Ellie_Eyelid_Upper_Close-Open"}
for a in list(bpy.data.actions):
    if a.name not in KEEP:
        bpy.data.actions.remove(a)
print(f"actions kept: {len(bpy.data.actions)}")

bpy.ops.export_scene.gltf(
    filepath="/tmp/ellie/model4.glb", export_format="GLB",
    export_animations=True, export_skins=True,
    export_def_bones=True,
    export_animation_mode='ACTIONS',
    export_force_sampling=True,
    export_optimize_animation_size=True,
    export_image_format="AUTO", export_yup=True,
    use_selection=True)
print("EXPORT_DONE")
