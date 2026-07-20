#!/usr/bin/env python3
"""Generate tests/data/rig_cube.glb — a minimal 2-joint skinned cube.

Pinned output (no timestamps/UUIDs) so the committed binary is reproducible.
Pure stdlib: struct + json + zlib. No Blender, no pip deps.

Model: 8-vertex cube, y in [0,1], x/z in [-0.5,0.5]. Bottom 4 verts skinned
100% to joint "Root", top 4 to joint "Tip" (child of Root, offset +1y).
Clips: "Idle" (Root rotZ 10 deg over 1s), "Wave" (Tip rotZ 90 deg over 0.5s).
Material: 2x2 red/green PNG, embedded.
"""
import json, struct, zlib, argparse, pathlib

def png_2x2():
    def chunk(typ, data):
        return (struct.pack(">I", len(data)) + typ + data
                + struct.pack(">I", zlib.crc32(typ + data) & 0xffffffff))
    ihdr = struct.pack(">IIBBBBB", 2, 2, 8, 6, 0, 0, 0)
    raw = bytes([0, 255,0,0,255, 255,0,0,255,   # row 0: red, red
                 0, 0,255,0,255, 0,255,0,255])  # row 1: green, green
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))

# --- geometry ----------------------------------------------------------------
POS = [(-0.5,0,-0.5),(0.5,0,-0.5),(-0.5,0,0.5),(0.5,0,0.5),
       (-0.5,1,-0.5),(0.5,1,-0.5),(-0.5,1,0.5),(0.5,1,0.5)]
NRM = [(0,1,0)]*8
UV  = [(0,0),(1,0),(0,1),(1,1)]*2
JNT = [(0,0,0,0)]*4 + [(1,0,0,0)]*4
WGT = [(1,0,0,0)]*8
IDX = [0,2,1, 1,2,3, 4,5,6, 5,7,6,          # bottom, top
       0,1,5, 0,5,4, 2,6,7, 2,7,3,          # sides
       0,4,6, 0,6,2, 1,3,7, 1,7,5]
IBM_ROOT = [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]            # identity (column-major)
IBM_TIP  = [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,-1,0,1]           # inverse of T(0,1,0)
IDLE_T = [0.0, 1.0]; IDLE_R = [(0,0,0,1), (0,0,0.0871557,0.9961947)]  # 10 deg about Z
WAVE_T = [0.0, 0.5]; WAVE_R = [(0,0,0,1), (0,0,0.7071068,0.7071068)]  # 90 deg about Z

bin_blob = bytearray()
views, accessors = [], []
def add_view(data, target):
    while len(bin_blob) % 4: bin_blob.append(0)
    views.append({"buffer":0,"byteOffset":len(bin_blob),"byteLength":len(data),**({"target":target} if target else {})})
    bin_blob.extend(data)
    return len(views)-1
def f32s(vals):
    flat = [c for v in vals for c in (v if isinstance(v,tuple) else (v,))]
    return struct.pack("<%df"%len(flat), *flat)
def add_acc(vals, ctype, atype, target, mn=None, mx=None):
    if ctype == 5126: data = f32s(vals)
    elif ctype == 5123: data = struct.pack("<%dH"%len(vals), *vals)
    elif ctype == 5121: data = struct.pack("<%dB"%len([c for v in vals for c in v]), *[c for v in vals for c in v])
    acc = {"bufferView":add_view(data,target),"componentType":ctype,
           "count":len(vals) if isinstance(vals[0],tuple) else len(vals),"type":atype}
    if mn is not None: acc["min"]=mn; acc["max"]=mx
    accessors.append(acc); return len(accessors)-1

acc_pos = add_acc(POS,5126,"VEC3",34962, mn=[-0.5,0,-0.5], mx=[0.5,1,0.5])
acc_nrm = add_acc(NRM,5126,"VEC3",34962)
acc_uv  = add_acc(UV,5126,"VEC2",34962)
acc_jnt = add_acc(JNT,5121,"VEC4",34962)
acc_wgt = add_acc(WGT,5126,"VEC4",34962)
acc_idx = add_acc(IDX,5123,"SCALAR",34963)
acc_ibm = add_acc([tuple(IBM_ROOT),tuple(IBM_TIP)],5126,"MAT4",None)
acc_idle_t = add_acc(IDLE_T,5126,"SCALAR",None, mn=[0.0], mx=[1.0])
acc_idle_r = add_acc(IDLE_R,5126,"VEC4",None)
acc_wave_t = add_acc(WAVE_T,5126,"SCALAR",None, mn=[0.0], mx=[0.5])
acc_wave_r = add_acc(WAVE_R,5126,"VEC4",None)
png = png_2x2()
view_png = add_view(png, None)

gltf = {
  "asset":{"version":"2.0","generator":"make_test_glb.py (pinned)"},
  "scene":0, "scenes":[{"nodes":[0,2]}],
  "nodes":[
    {"name":"Root","children":[1],"translation":[0,0,0]},
    {"name":"Tip","translation":[0,1,0]},
    {"name":"CubeMesh","mesh":0,"skin":0}],
  "skins":[{"joints":[0,1],"inverseBindMatrices":acc_ibm,"skeleton":0}],
  "meshes":[{"primitives":[{"attributes":{"POSITION":acc_pos,"NORMAL":acc_nrm,
      "TEXCOORD_0":acc_uv,"JOINTS_0":acc_jnt,"WEIGHTS_0":acc_wgt},
      "indices":acc_idx,"material":0}]}],
  "materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],
  "textures":[{"source":0}],
  "images":[{"mimeType":"image/png","bufferView":view_png}],
  "animations":[
    {"name":"Idle","channels":[{"sampler":0,"target":{"node":0,"path":"rotation"}}],
     "samplers":[{"input":acc_idle_t,"output":acc_idle_r,"interpolation":"LINEAR"}]},
    {"name":"Wave","channels":[{"sampler":0,"target":{"node":1,"path":"rotation"}}],
     "samplers":[{"input":acc_wave_t,"output":acc_wave_r,"interpolation":"LINEAR"}]}],
  "accessors":accessors, "bufferViews":views,
  "buffers":[{"byteLength":0}]}

actual_len = len(bin_blob)
gltf["buffers"][0]["byteLength"] = actual_len  # unpadded data extent
json_chunk = json.dumps(gltf, separators=(",",":")).encode()
while len(json_chunk) % 4: json_chunk += b" "
while len(bin_blob) % 4: bin_blob.append(0)   # pad chunk to multiple of 4
bin_chunk = bytes(bin_blob)
total = 12 + 8+len(json_chunk) + 8+len(bin_chunk)
out = (struct.pack("<III", 0x46546C67, 2, total)
       + struct.pack("<II", len(json_chunk), 0x4E4F534A) + json_chunk
       + struct.pack("<II", len(bin_chunk), 0x004E4942) + bin_chunk)

ap = argparse.ArgumentParser()
ap.add_argument("--out", required=True)
a = ap.parse_args()
pathlib.Path(a.out).parent.mkdir(parents=True, exist_ok=True)
pathlib.Path(a.out).write_bytes(out)
print(f"wrote {a.out} ({total} bytes)")
