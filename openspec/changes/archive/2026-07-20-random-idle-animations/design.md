## Context

`SpriteAnimationEngine` currently maps ~25 of the 43 available sprite animations in `buildNameMap()`. The idle system (`startIdleAnimation()`) uses a fixed 3-second `m_idleTimer` and a weighted random pick from 8 idle animations. Several idle animations and directional looks are present in `animations.json` but unreachable because they lack name-map entries. The idle rotation also feels repetitive because the same animation can play twice in a row and the timing is predictable.

## Goals / Non-Goals

**Goals:**
- Expose all 43 sprite animations through the public API
- Make idle animation rotation feel organic and less repetitive
- Keep the implementation simple and state-local to `SpriteAnimationEngine`

**Non-Goals:**
- Changing the sprite sheet or `animations.json` format
- Adding new animation categories (e.g., emotion tags, context-aware idle)
- Modifying the IPC protocol or event-to-animation mapping logic in `EventRouter`

## Decisions

### Decision 1: Add all missing animations to `buildNameMap()`
**Rationale:** The animations already exist in the asset file. Exposing them is a pure code change with zero asset overhead. We use intuitive lowercase/snake_case aliases that follow the existing naming convention.

New mappings:
- `checking` → `CheckingSomething`
- `empty_trash` → `EmptyTrash`
- `hearing` → `Hearing_1`
- `look_down_left` → `LookDownLeft`
- `look_down_right` → `LookDownRight`
- `look_up_left` → `LookUpLeft`
- `look_up_right` → `LookUpRight`

### Decision 2: Variable idle timeout
**Rationale:** Fixed 3-second intervals feel robotic. A uniform random range between 1000ms and 4000ms creates natural pauses.

Implementation: Replace `m_idleTimer.setInterval(m_idleTimeoutMs)` with `QRandomGenerator::global()->bounded(3000) + 1000` each time the timer is restarted.

### Decision 3: Anti-repeat idle selection
**Rationale:** Seeing the same idle animation twice in a row breaks the illusion of liveliness.

Implementation: Track `m_lastIdleAnim` (QString). In `startIdleAnimation()`, if the random pick equals the last-played animation, perform one re-roll. If the pool has only one item, skip re-rolling.

### Decision 4: Occasional "surprise" non-idle picks
**Rationale:** Real pets don't only idle — they occasionally glance around. Injecting a small chance (10%) to play a directional look (e.g., `look_left`, `look_up_right`) during idle time adds personality without requiring new assets.

Implementation: Maintain a `m_surpriseAnims` list with low-weight directional looks and gestures. In `startIdleAnimation()`, with 10% probability, pick from `m_surpriseAnims` instead of `m_idleAnims`. These surprise picks still respect the anti-repeat logic.

## Risks / Trade-offs

- **[Risk]** More frequent animation changes could distract the user during focused work.  
  → **Mitigation:** The variable timeout range (1–4s) is still relatively sedate. If needed, a future change could add a "focus mode" that pauses idle animations.

- **[Risk]** Surprise picks might feel out of place if they interrupt a long idle.  
  → **Mitigation:** Surprise probability is low (10%) and only uses subtle directional-look animations, not loud gestures like `Wave` or `Alert`.

## Migration Plan

No migration needed. This is a pure code change with no data format or protocol changes.
