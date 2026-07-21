# Authoring Model3D Packs

Model3D is Seelie's rig-and-skeletal-animation character type. A Model3D pack ships a single glTF 2.0 binary (`.glb`) containing the mesh, skin, animation clips, and embedded textures — everything the runtime needs to render and animate the character.

This doc covers the pack layout, the manifest fields specific to Model3D, how to convert assets from common sources (Blender, Mixamo, Unity/Unreal), and the constraints to keep in mind for v1 of the engine.

## Pack layout

A Model3D pack is a directory (or `.spk` zip of that directory) containing exactly the same files every other Seelie pack uses:

```
yourpack/
├── manifest.json     # pack metadata + Model3D character config
├── model.glb         # mesh + skin + animations + textures (single file)
└── preview.png       # shown in the pack browser
```

Install like any other pack: drop the `.spk` on the pet window, or copy the directory / archive to `~/.config/Seelie/packs/`. The loader picks the engine from `character.type` — set it to `"model3d"` and the Model3D engine takes over.

## Manifest template

```json
{
  "formatVersion": "1.0.0",
  "id": "you.yourpack",
  "name": "Your Pack",
  "author": "you",
  "version": "1.0.0",
  "character": {
    "type": "model3d",
    "model": "model.glb",
    "frameWidth": 124,
    "frameHeight": 200,
    "displayScale": 1.0,
    "cameraDistance": 0.0,
    "cameraHeight": 0.0,
    "upAxis": "y",
    "unitScale": 1.0
  },
  "idlePool": [ { "name": "Idle", "weight": 3 } ],
  "eventMap": { "session.start": "Wave", "user.click": "Wave" },
  "stateMap": { "Petted": "Happy", "Tossed": "Spin" }
}
```

The generic top-level keys (`formatVersion`, `id`, `name`, `author`, `version`, `idlePool`, `eventMap`, `stateMap`) behave the same as in Lottie / Live2D / sprite packs — see `schemas/character-pack-v1.schema.json` for the full schema. The Model3D-specific bits all live under `character`.

### Model3D character fields

| Field | Required | Default | Description |
|---|---|---|---|
| `type` | yes | — | Must be `"model3d"` to select this engine. |
| `model` | yes | — | Path to the `.glb` inside the pack (usually `"model.glb"`). |
| `frameWidth` | yes | — | Window width in pixels. Should match the global pet window (124) unless you have a reason to diverge. |
| `frameHeight` | yes | — | Window height in pixels (200 for the default pet). |
| `displayScale` | no | `1.0` | Multiplier applied after auto-fit / camera framing. Use for fine-tuning without re-exporting. |
| `cameraDistance` | no | `0.0` | Distance from the camera to the model's origin. `0` means **auto-fit** to the bind-pose bounding box. |
| `cameraHeight` | no | `0.0` | Vertical offset of the camera target. `0` means **auto-fit**. |
| `upAxis` | no | `"y"` | World up axis. Use `"y"` for Blender / glTF defaults, `"z"` for Z-up exports (some Maya / 3ds Max pipelines). |
| `unitScale` | no | `1.0` | Multiplier on world units. Use `0.01` when the export is in centimetres, `100` for metres-to-centimetres, etc. |

> **`frameWidth` / `frameHeight` are required.** Every other engine has hard-coded window dimensions; Model3D is the first one that lets the pack author pick, so the manifest has to say.

### idlePool note

The idle-pool entries use the JSON key **`"name"`** (not `"animationName"`). This matches the parser convention shared across all Seelie engines:

```json
"idlePool": [ { "name": "Idle", "weight": 3 }, { "name": "IdleBored", "weight": 1 } ]
```

## Clip naming

`eventMap`, `idlePool`, and `stateMap` values are **glTF animation clip names** — the strings stored in the `animations[].name` field of the GLB. The Model3D engine enumerates the clips in the file at load time and matches by exact string. Rules:

- **Missing clips are skipped with a log warning** — the engine never crashes on a typo'd or unmatched clip name. The pet just falls back to idle.
- **Clip names are case-sensitive and whitespace-sensitive.** Exporters sometimes append ` |` or `Armature.001` prefixes; clean those up in Blender before exporting.
- **Recommended canonical clip names** (so packs are interchangeable): `Idle`, `Wave`, `Happy`, `Spin`, `Sad`. Map events / states onto these in the manifest.

## Where to get models

| Source | Workflow |
|---|---|
| **Blender** (recommended) | Author or import the rigged model, then `File → Export → glTF 2.0`. In the export dialog pick **format: glb**, enable **Animation → Animation + Skins**, and keep **+Y Up** (default). |
| **Mixamo** | Download the character (`T-Pose.fbx`) and each animation as FBX, then convert with `scripts/fbx_to_glb.py` (see below). Mixamo rigs ship with a flat bone list and one action per file — combine them in Blender first if you want multiple clips in a single GLB. |
| **Unity / Unreal marketplace assets** | Export the skinned mesh + animations to FBX from the editor, then convert with `scripts/fbx_to_glb.py`. |

For all three, end with: a single `.glb`, Y-up, ≤64 joints, one skin, textures embedded.

## Conversion

`scripts/fbx_to_glb.py` runs Blender headlessly to import an FBX (or `.blend` / `.obj`), validate the rig, and re-export a clean GLB:

```bash
python3 scripts/fbx_to_glb.py input.fbx --out pack/model.glb
```

Requirements:

- **Blender ≥ 3.6 on `PATH`** (or pass `--blender /path/to/blender`).
- The script refuses to export if the file has no armature (exit 2) or no animations (exit 3), and prints a warning if the rig has more than 64 joints.

The exported GLB uses **GLB format, animations on, skins on, Y-up, embedded textures** — the exact settings the Model3D runtime expects.

## Constraints (v1)

The first version of the Model3D engine is deliberately minimal. Plan around these limits:

- **>64 joints works via CPU skinning.** Small rigs (≤ the driver's uniform palette, usually 64) skin on the GPU. Bigger rigs — Mixamo full-body, film rigs like Blender Studio's Ellie (329 joints) — automatically switch to a CPU-skinning path that poses vertices on the CPU and streams them to the GPU. No joint-count limit in practice; the cost is a per-frame vertex upload (fine for a single character). The runtime logs which path it took at load.
- **Pose-library packs (static clips).** Some rigs (film characters) ship 1-frame *pose* clips rather than looping animations. One-shot clips shorter than 1.5s hold their final frame so poses read, and the engine adds a gentle procedural idle sway so the pet never looks frozen. Note: Blender Studio film rigs (CloudRig) may export *flat* clips — their control-bone drivers don't evaluate in headless conversion, so every action samples as rest pose. Check clip motion before shipping a pack.
- **Single skin only.** A GLB can technically contain multiple skins; v1 uses the **first skin** in the file. Models with multiple skins (e.g. separate body + outfit rigs) will animate partially — merge them into one skin before exporting.
- **No morph targets.** Blend shapes / morph target animations are ignored. They parse, they don't move. Slated for v2.
- **No clip crossfading.** Clip transitions are hard cuts (with the engine's idle preemption logic). Smooth crossfades are slated for v2.
- **Textures must be embedded in the GLB.** External `.bin` / `.png` sidecars are not loaded. Use **baseColor PNG or JPEG only** in v1 — no normal / metallic / roughness / emissive maps (they're ignored; the renderer is unlit cartoon-friendly).
- **Y-up, metres preferred.** Z-up exports work via `"upAxis": "z"`, but you'll save yourself a lot of framing pain by exporting Y-up. If the model is tiny or huge on screen, set `"unitScale"` (e.g. `0.01` for cm-scaled FBX) instead of fighting `displayScale`.

## Camera & framing

By default (`cameraDistance` and `cameraHeight` both `0`), the Model3D engine **auto-fits the bind pose's bounding box** into the frame — the whole character should be visible and roughly centred without you touching anything.

Override when the auto-fit isn't flattering:

- `cameraDistance` > 0 — pulls the camera back; use when you want headroom or to frame only the upper body.
- `cameraHeight` > 0 — raises the camera target; pair with `cameraDistance` for a bust-shot framing.
- `displayScale` — final multiplier on the composed frame; use for fine-tuning without re-exporting.

Values are in the model's units after `unitScale` is applied, so a 1.8 m character with `unitScale: 1.0` and `cameraDistance: 3.0` frames from ~3 metres away.
