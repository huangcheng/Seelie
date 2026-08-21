# Seelie Sprite Desktop Life — Vivid Actions + Free Walk + Lottie Off

**Date:** 2026-08-21 · **Status:** draft (pending user review)  
**Approach:** motion-first with placeholders, AI art fills the Seelie atlas in parallel

## Why

Sprite dolls underuse their sheets: the FSM builds fallback chains, but sprites play only `chain.first()`, so many cute clips never fire. Hatch-pet packs (including Seelie) still use a fixed 9-clip GPT/Codex atlas that cannot express true pet/grab/toss, look-arounds, or walk/fall. Meanwhile the pet window only moves when the user drags it — it never walks the desk or sits on windows the way classic desktop pets do.

This change revitalizes **Seelie only** (our own sprite pet): new clip vocabulary + AI-generated atlas, sprite-engine playback fixes, and a desktop motion controller (open walks, edge patrol, window perch, comedy fall). Live2D and Model3D stay as-is. Lottie is compile-time optional and **off by default** (no Lottie resources in the default product).

## Decisions locked during brainstorm

- Scope of art/behavior: **Seelie sprite pack only** (flagship). Other hatch-pets keep the old 9-clip atlas.
- Behavior clusters: **full set** — idle life + touch reactions + work reactions.
- Free walk: **A + C** — occasional short open-desktop walks **and** edge / taskbar-shelf patrol.
- Window perch: occasionally sit on the **active window’s top edge**.
- Fall trigger: **only** when the pet is perched **and the user moves that window**. Landing: Windows taskbar → macOS Dock (if visible) → else bottom screen edge.
- Calm leave: perch **timer** expires → `hop_off` (no fall) to edge or open desk.
- Fall does **not** fire on focus loss or coding events alone.
- Approach: **motion-first** — placeholders ship with the engine; AI art swaps in under the same clip names.
- Art source: **AI image generation** (Voyah / MiniMax / StepFun; keys from user Obsidian / `~/.pi` at runtime, never committed).
- Consistency: character bible + reference-conditioned strips + auto QA + human approve gate.
- Live2D / Model3D: **keep, leave as-is**.
- Lottie: `SEELIE_LOTTIE_ENABLED` CMake option, **default OFF**.

## Goals / Non-goals

**Goals**
- Seelie feels vivid at rest (varied idle) and on work/touch events (dedicated clips).
- Seelie occasionally walks the desk and perches on the active window; dragging the perch causes a fall to the shelf.
- Sprite engine plays FSM fallback chains (parity with Live2D dispatch behavior).
- Default builds exclude Lottie engine, rlottie dependency, and Lottie pack assets.
- Placeholder frames allow motion QA before final art; approved AI frames replace cells without renaming clips.

**Non-goals**
- No Live2D or Model3D motion / framing / SDK changes.
- No redraw of WorkBuddy, meows, qclaw, or other hatch-pet atlases in this change.
- No continuous forever-roaming; no momentum glide after toss (still parked).
- No Linux Dock / panel special-case in v1 (bottom screen edge only).
- No new UDP/IPC canonical events for walking (motion is local UI behavior).
- No shipping API keys or raw gen prompts that embed secrets.

## Architecture

```
UDP / touch / context events
        │
        ▼
  PetStateMachine ──animation chain──► SpriteAnimationEngine (Seelie)
        │                                      ▲
        │                                      │ walk / sit / fall / land clips
        ▼                                      │
 DesktopMotionController ──moves MainWindow────┘
   • IdleWander (A open + C edge)
   • WindowPerch (active window top)
   • FallToShelf (taskbar / Dock / bottom)
```

| Piece | Role |
|---|---|
| **Seelie pack** | New atlas + `animations.json` + `idlePool` / `stateMap` / `nameMap` |
| **SpriteAnimationEngine** | Walk full fallback chains; play motion clips; richer idle / surprise |
| **DesktopMotionController** (new) | Owns when/where the window moves; **sprite + Seelie only** |
| **Platform shelf probe** | Win taskbar rect; macOS Dock rect; else screen bottom |
| **Active-window probe** | Top edge of foreground window for perch |
| **CMake `SEELIE_LOTTIE_ENABLED`** | Default OFF — strips Lottie from default builds |
| **AI art pipeline** (`scripts/seelie-sprite-gen/`) | Bible → conditioned strips → QA → human gate → pack |

Motion never fights the user: pet / grab / toss / coding overlays **preempt** wander and calm perch leave. The only comedy path is **perched + window dragged → fall**.

## Components

### 1. Seelie clip contract

Stable names shared by placeholders and AI frames:

| Group | Clips | Used for |
|---|---|---|
| **Idle life** | `idle`, `idle_fidget`, `idle_look_left`, `idle_look_right`, `idle_stretch`, `idle_snooze` | Weighted idle pool + rare surprises |
| **Work** | `greet`, `think`, `work`, `review`, `fail`, `celebrate` | FSM states via `stateMap` / `nameMap` |
| **Touch** | `pet`, `grab`, `toss` | Petted / Grabbed / Tossed (replace GPT stand-ins) |
| **Motion** | `walk_left`, `walk_right`, `sit`, `hop_off`, `fall`, `land` | `DesktopMotionController` |

**Rules**
- Engine plays **fallback chains** (first existing clip wins) so a missing frame never freezes the pet.
- Manifest maps FSM states → contract names; legacy Codex names (`waving`, `running`, …) may remain as aliases during transition.
- Old fixed GPT 8×9 atlas for **Seelie only** is retired once the new sheet ships; other hatch-pets keep the old atlas.

### 2. `SpriteAnimationEngine` upgrades

- **Chain playback**: when MainWindow/FSM dispatches a name list, sprites try each name until one exists (same idea as Live2D `playAnimationChain`), instead of `chain.first()` only.
- **Idle pool**: Seelie pack lists the idle-life clips with weights; anti-repeat via existing `IdlePicker`.
- **Surprise idle** (sprite / Seelie): rare non-work clips from the idle set (stretch, snooze, looks) — implement the pet-aliveness surprise idea for this pack.
- **Parse `nameMap` from directory manifests** (today only Codex archives synthesize it) so Seelie can alias without hardcoding in C++.
- No changes required to Live2D / Model3D engines for this work.

### 3. `DesktopMotionController` (new)

Owned by `MainWindow`. Enabled only when:
- active character type is **sprite**, and
- pack id is Seelie (or a dedicated manifest flag `character.desktopMotion: true` on the Seelie pack), and
- user setting `desktopWanderingEnabled` is true (default **on**).

**Wander**
- Gate: FSM active state is Idle; not grabbed; window visible.
- Mix of short open-desktop paths (A) and edge / taskbar-shelf patrol (C).
- Cooldown between trips; clamp to current screen available geometry.
- Cancel immediately on pet / grab / toss / non-idle FSM state.

**Perch**
- Rare idle surprise: path to active window’s top edge; play `sit`; stick to that window’s top while perched.
- **Timer expires** → play `hop_off` → calm leave to edge or open desk (**no fall**).
- **User moves the perched window** (geometry change while perched) → play `fall` → animate toward shelf → `land` → idle.
- Pet / grab / work while perched → preempt with normal FSM animation; leave perch without comedy fall (unless the leave *is* the drag-fall).

**Shelf landing priority**
1. Windows: taskbar top edge  
2. macOS: Dock top edge if Dock visible  
3. Else: bottom edge of the current screen  

**Test seams:** injectable clock, RNG, screen/window geometry probes (mirror `SystemContextEngine` style).

### 4. Platform probes

- **Windows:** foreground window rect + taskbar rect (Shell / Win32).  
- **macOS:** frontmost window bounds + Dock visibility/rect (AppKit / existing patterns where present).  
- **Linux v1:** frontmost window if available; shelf = bottom of current screen only.

Failures degrade gracefully: skip perch if no usable window; fall target always has a screen-bottom fallback.

### 5. AI art consistency pipeline

```
Existing Seelie frames
        │
        ▼
 1. Character bible (one-time)
    • canonical front + ¾ + side model sheet
    • frozen prompt block: species, palette, proportions, line weight
    • do-nots: no outfit change, no chibi resize, no extra limbs
        │
        ▼
 2. Per-clip generation (always conditioned)
    • every request attaches the bible sheet as reference image(s)
    • same style preset / seed family where the API allows
    • generate short strips (4–8 frames), not orphan singles
        │
        ▼
 3. Auto QA gates
    • palette distance vs bible
    • silhouette / bbox size within ±tolerance of idle
    • optional embedding distance vs reference
        │
        ▼
 4. Human eye gate
    • contact sheet review
    • approve → slice into atlas; reject → regen with last good frame as ref
        │
        ▼
 5. Pack
    • only approved frames enter animations.json / sheet
```

**Hard rules**
- Never generate a clip without the bible reference attached.
- Motion clips start from the last approved idle pose as the first-frame ref.
- Prompt templates live in `scripts/seelie-sprite-gen/`; API keys read at runtime from user-configured paths (Obsidian / `~/.pi`), never committed.
- Placeholders ship first; AI frames replace cell-by-cell without renaming clips.

### 6. Lottie compile-out

- CMake option `SEELIE_LOTTIE_ENABLED` default **OFF**.
- When OFF: omit Lottie engine sources, rlottie FetchContent, Lottie pack discovery/bundle, and settings entries that only apply to Lottie.
- When ON: restore current Lottie behavior for contributors who need it.
- Live2D and Model3D remain unconditionally available (subject to existing SDK presence).

### 7. Settings & i18n

- Settings → Interaction: **“Desktop wandering”** checkbox bound to `desktopWanderingEnabled` (default on). Ignored for Live2D / Model3D.
- All new user-visible strings use `tr()` with zh_CN updates in `Seelie_zh_CN.ts`.

## Data flow (perch → fall)

```
Idle + wander cooldown elapsed
  → optional WindowPerch (rare)
  → move to active window top, play sit, stick geometry
       │
       ├─ timer done → hop_off → calm path to edge/open → idle
       ├─ pet/grab/work → cancel perch, FSM owns animation
       └─ perched window moved by user
            → fall clip + animate Y toward shelf
            → land clip → idle on taskbar/Dock/bottom
```

## Error handling

- Missing clip → try next in chain; if none, keep last frame and log (no crash).
- Active window unreadable → skip perch attempt.
- Shelf probe failure → bottom of current screen.
- AI gen / QA failure → leave placeholder; do not block runtime.
- Wandering disabled or non-Seelie sprite → controller no-ops.

## Testing

- Unit: motion states (wander → perch → hop_off / fall → land); shelf priority; chain fallbacks on sprite engine; CMake Lottie OFF builds without Lottie symbols.
- Pack: Seelie manifest lists every contract clip (placeholders acceptable).
- Manual: walk bursts; perch on a normal app window; drag window → fall to shelf; toggle wandering off; confirm Live2D/Model3D packs unchanged; default build has no Lottie.

## Phasing (Approach 2)

1. Clip contract + Seelie placeholder atlas + manifest maps.  
2. Sprite chain playback + idle/surprise wiring.  
3. `DesktopMotionController` + platform probes + settings.  
4. `SEELIE_LOTTIE_ENABLED=OFF` default.  
5. AI bible + gen scripts; replace placeholders clip-by-clip behind human approve.

## Open points (resolved)

| Topic | Resolution |
|---|---|
| Which packs | Seelie only |
| Walk style | A + C |
| Fall trigger | Perched + window moved only |
| Calm leave | Timer → hop_off |
| Art | AI gen with consistency pipeline |
| Lottie | Compile-time OFF by default |
| Live2D / Model3D | Unchanged |

## Status

Draft pending user review of this file before implementation planning.
