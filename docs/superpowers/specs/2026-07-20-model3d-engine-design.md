# Model3D Engine — Skeletal 3D Character Packs

**Date:** 2026-07-20 · **Status:** draft (pending user review)
**Branch strategy:** feature branch merged back to `main` as a fourth engine (user decision — no product fork)

## Why

Seelie's three engines are capped by their asset formats: Live2D models ship fixed motion groups, sprite/Lottie packs ship fixed frames/clips. Rigged 3D models (Blender, 3ds Max, Unity/Unreal marketplace exports) offer skeletal animation — arbitrary poses, reusable clips, a huge asset ecosystem. This milestone adds a fourth engine that renders glTF/GLB skinned models inside the existing transparent pet window, with full interaction parity.

## Decisions locked during brainstorm

- **Goal:** user-imported 3D packs (a new `engineType` behind the existing pack system), not a one-off official character.
- **Interaction scope:** full parity v1 — event/idle animations plus touch/grab/toss state clips.
- **Runtime format:** **glTF/GLB only**. FBX (3ds Max, most Unity/Unreal assets) reaches the app via a Blender-based conversion script in the pack-authoring workflow — native runtime FBX is a non-goal (Assimp has years of unresolved skeletal-animation regressions; `ufbx` can slot behind the loader seam later if demanded).
- **Renderer:** custom OpenGL + vendored `cgltf` (MIT, single header, ~50KB). Qt3D is deprecated (gone in Qt 6.8+); Qt Quick 3D is GPL-3/commercial and incompatible with Seelie's MIT license; filament/bgfx/ozz are overkill.
- **Structure:** mirrors `Live2DAnimationEngine` — offscreen `QOpenGLContext` → skinned render to `QOpenGLFramebufferObject` → `toImage()` → `QPainter` onto the translucent window.
- **Plan ordering:** walking-skeleton first (one static GLB rendering in the pet window) before any interface work.

## Goals / Non-goals

**Goals**
- `Model3DEngine` implements `AnimationEngine` (`AnimationEngine.h:50-56`): `loadFromCharacterPack`, `playAnimation(name, priority)`, `stop`, `paint`, `isPlaying`, `hasAnimations`, `lastPaintSuccessful`.
- New `CharacterPack::EngineType::Model3D` (`CharacterPack.h:32-36`) with manifest-driven clip mapping via the **existing** `eventMap`, `idlePool`, `stateMap` keys (values = glTF animation clip names).
- Interaction parity through the interface: FSM states (Petted/Grabbed/Tossed), context senses, persona bubbles, idle sayings all work unchanged.
- Pack-authoring path from FBX: conversion script + docs.
- Cross-platform (macOS/Windows/Linux), no proprietary SDK dependency (unlike Live2D's Cubism).

**Non-goals (v1)**
- No morph targets, no clip crossfading (hard cuts), no PBR beyond baseColor texture + hemisphere lighting, no physics/ragdoll, no runtime FBX, no user camera controls, no multi-character scenes.

## Architecture

```
pack/model.glb ──► GltfLoader (cgltf) ──► SkinnedMesh + Skeleton + AnimationClips
                                                │
FSM/EventRouter ──► Model3DEngine ──► AnimationEvaluator (per-frame joint matrices, CPU)
                                                │
                                  GLSkinningRenderer (vertex-skinning shader)
                                                │
                                    FBO ──► QImage ──► QPainter ──► pet window
```

New sources under `src/model3d/`. Everything above the engine (EventRouter, PetStateMachine, touch, bubbles, persona, memory, IdleBehaviorEngine) is untouched — parity flows through the `AnimationEngine` interface and the manifest mapping, exactly as with Live2D.

## Components

### 1. `GltfLoader` (`src/model3d/GltfLoader.h/.cpp`)

Wraps vendored `thirdparty/cgltf/cgltf.h` (vendored like `thirdparty/miniz`). Extracts:
- Mesh primitives: positions, normals, UVs, joint indices, weights (interleaved VBO-ready)
- Skeleton: joint node hierarchy + inverse bind matrices
- Animation clips: name → samplers/channels (translation/rotation/scale tracks)
- Textures: embedded images decoded via stb_image (cgltf bundles it)

Sits behind a narrow `IModelLoader`-style seam (load → plain structs) so `ufbx` can be added later without touching the engine. Pure parsing — unit-testable without GL.

### 2. `AnimationEvaluator` (`src/model3d/AnimationEvaluator.h/.cpp`)

Given a clip + local time, evaluates translation/rotation(slerp)/scale tracks per joint (step + linear interpolation; cubic-spline only if trivial), walks the hierarchy to model-space joint matrices, multiplies by inverse bind matrices → final skinning palette (≤64 joints). Pure math, unit-testable without GL.

### 3. `GLSkinningRenderer` (`src/model3d/GLSkinningRenderer.h/.cpp`)

Owns GL resources: VBO/VAO per mesh, baseColor texture, skinning vertex shader (joint palette uniform, 4 influences), fragment shader (baseColor texture × hemisphere lighting — simple ambient + directional wrap, tuned for stylized models). Perspective camera **auto-fit to the model's bounding box** (any imported model frames sensibly in the 124×200 window); manifest may override with `cameraDistance`/`cameraHeight`. Transparent clear color; renders into the FBO like `Live2DAnimationEngine`.

### 4. `Model3DEngine` (`src/model3d/Model3DEngine.h/.cpp`)

The `AnimationEngine` implementation, patterned on `Live2DAnimationEngine`:
- `loadFromCharacterPack`: GL init, FBO (clamped 1–4096 like Live2D), load GLB via `GltfLoader`, filter the pack's `idlePool`/`eventMap`/`stateMap` clip names against clips present in the model (missing → logged + skipped, same as Live2D motion groups), fallback idle = first clip.
- `playAnimation(name, priority)`: queue semantics matching the other engines (HighPriority preempts, NormalPriority queues); clips are one-shot except idle-pool clips which loop.
- Idle: jittered timer + anti-repeat via `IdlePicker` (same pattern as Tasks 6–8 of pet-aliveness).
- Per-frame: evaluate active clip → skinning palette → draw to FBO → `toImage` cache for `paint()`.
- 16ms `QTimer` tick like the other engines.

### 5. Pack format (additive, backward compatible)

Manifest (`CharacterPack.cpp:474-560` parsing, engineType parsed at ~579-590):
```jsonc
{
  "character": {
    "engineType": "model3d",
    "modelFile": "model.glb",        // GLB with embedded textures + clips
    "displayScale": 1.0,              // optional uniform scale
    "cameraDistance": 0.0,            // optional; 0 = auto-fit bounding box
    "cameraHeight": 0.0               // optional; 0 = auto (bbox center)
  },
  "eventMap": { "session.start": "Wave", ... },   // existing keys, clip names
  "idlePool": [ {"animationName": "Idle", "weight": 3}, ... ],
  "stateMap": { "Petted": "Happy", "Tossed": "Spin", ... }
}
```
`CharacterPack::EngineType` gains `Model3D`; engine switching in `mainwindow.cpp:1334-1365` gains a Model3D branch. Unknown clip names in any mapping are filtered at load (never crash on a user-imported pack).

### 6. Conversion tooling

`scripts/fbx_to_glb.py` — headless Blender wrapper: import FBX → validate (has armature, ≥1 animation) → export `.glb` (embedded textures). Plus a short pack-authoring doc (`docs/model3d-packs.md`): where to get models (Blender export, Unity/Unreal asset export, Mixamo), clip naming advice, manifest template.

## Behavior notes

- **Engine switching**: same live-switch path as today (pack change → engine swap → `stop()` old, `loadFromCharacterPack` new).
- **First frame**: model appears in bind pose or first idle clip within one tick of load; never a blank window longer than one frame.
- **Touch parity**: Petted/Grabbed/Tossed resolve via `stateMap`; if a state has no mapped clip, the engine plays the current idle (graceful, matches Live2D fallback behavior).
- **Performance**: single character, ≤64 joints, one draw call per mesh primitive — trivially within the <10MB RAM ethos; cgltf adds ~50KB binary.

## Error handling

| Failure | Behavior |
|---|---|
| GLB missing/corrupt | Log warning; `loadFromCharacterPack` returns false → pack loader falls back per existing rules |
| GLB has no skin (static mesh) | Loads; clips ignored; renders static model (still a valid pack) |
| GLB has no animations | Loads; idle pool empty → engine idles on bind pose (Live2D-style fallback) |
| Manifest clip names missing in model | Skipped at load with log; fallbacks apply |
| >64 joints | Clamp palette to first 64 joints with a load-time warning (documented limit) |
| GL context init failure | Same handling as Live2D (`lastPaintSuccessful`, engine fallback) |

## Testing (Qt Test)

- **GltfLoader**: parse a tiny committed test GLB (2-joint rigged cube, 2 clips, few KB, generated once by `scripts/make_test_glb.py` and committed) — joint count, clip names, sampler data, texture decode.
- **AnimationEvaluator**: interpolation math at t=0/mid/end against hand-computed values; palette correctness on the 2-joint rig.
- **CharacterPack**: manifest with `engineType: "model3d"` parses; clip filtering drops missing names.
- **Model3DEngine**: idle anti-repeat (pool >1), priority/queue semantics via a stub renderer (no GL needed for logic paths); offscreen render smoke test (FBO non-empty) guarded like Live2D's GL tests (skip when no GL).
- Full `ctest` stays green; engine tests follow the existing `SEELIEPET_LIB_SOURCES` registration pattern.

## File impact

| File | Change |
|---|---|
| `thirdparty/cgltf/cgltf.h` | **New** (vendored, MIT) |
| `src/model3d/{GltfLoader,AnimationEvaluator,GLSkinningRenderer,Model3DEngine}.{h,cpp}` | **New** |
| `src/CharacterPack.h/.cpp` | `EngineType::Model3D`, `modelFile`/`cameraDistance`/`cameraHeight` manifest fields |
| `src/mainwindow.cpp` | Model3D branch in engine switching |
| `scripts/fbx_to_glb.py`, `scripts/make_test_glb.py`, `docs/model3d-packs.md` | **New** |
| `tests/test_model3d.cpp` | **New** |
| `CMakeLists.txt`, `tests/CMakeLists.txt` | Sources, OpenGL link where absent |
| `assets/packs/` (sample 3D pack) | **New** small sample pack for manual QA |

## Rollout

- Land behind normal merge (no feature flag needed — new engine is inert until a model3d pack is selected).
- Sample pack ships small (target < 2MB GLB) for user testing.
- Follow-ups (explicitly post-v1): crossfade, morph targets, runtime FBX via ufbx, pack-store distribution of 3D packs.
