> **ARCHIVED 2026-07-20 — absorbed by the Pet Aliveness change.**
> Shipped design: `docs/superpowers/specs/2026-07-20-pet-aliveness-design.md`
> (amended: persona-era LLM idle quips, disk-first sayings override, all three animation engines).
> Implementation plan: `docs/superpowers/plans/2026-07-20-pet-aliveness.md`.

## Why

The current animation system only exposes ~25 of the 43 available sprite animations through `buildNameMap()`, leaving 12 animations inaccessible (including idle variants like `IdleHeadScratch`, directional looks like `LookUpRight`, and personality animations like `Hearing_1`). Additionally, the idle rotation uses a fixed 3-second timer with a simple weighted random pick, which feels repetitive and predictable. Exposing all available animations and improving the idle rotation will make Qlippy feel more alive and expressive.

## What Changes

- Expose all 12 currently unmapped animations by adding them to `buildNameMap()`
- Add new user-facing names for the newly exposed animations (e.g., `look_down_left`, `hearing`, `checking`)
- Improve idle animation rotation with:
  - Shorter, variable idle timeouts (1–4 seconds instead of fixed 3s)
  - Weighted random selection that avoids repeating the same animation consecutively
  - Occasional "surprise" idle picks from non-idle animations (looks, gestures) at low probability
- Update `SpriteAnimationEngine` to track the last-played idle animation and apply the anti-repeat logic

## Capabilities

### New Capabilities
- `idle-animation-rotation`: Enhanced idle state behavior with variable timing, anti-repeat logic, and surprise non-idle picks
- `animation-name-expansion`: Expose all 43 sprite animations through the public name map

### Modified Capabilities
- `animation-playback`: Extend the animation engine's idle timer and selection logic (behavioral change to idle state machine)

## Impact

- `src/SpriteAnimationEngine.cpp` — `buildNameMap()`, `startIdleAnimation()`, idle timer configuration
- `src/SpriteAnimationEngine.h` — new state tracking for last idle animation
- `src/EventRouter.cpp` — may need to expose new event → animation mappings
- No API or IPC protocol changes
