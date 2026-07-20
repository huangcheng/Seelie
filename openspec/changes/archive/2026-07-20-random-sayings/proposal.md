> **ARCHIVED 2026-07-20 — absorbed by the Pet Aliveness change.**
> Shipped design: `docs/superpowers/specs/2026-07-20-pet-aliveness-design.md`
> (amended: persona-era LLM idle quips, disk-first sayings override, all three animation engines).
> Implementation plan: `docs/superpowers/plans/2026-07-20-pet-aliveness.md`.

## Why

Qlippy currently only speaks when the `TipsEngine` detects a specific coding pattern (repeated errors, rapid edits, etc.). This makes the pet feel reactive rather than alive. Adding random idle sayings — humorous, encouraging, or contextual quips that appear when Qlippy is idle — will make the desktop pet feel more like a companion and less like a notification system.

## What Changes

- Add a `RandomSayingsEngine` class that maintains a pool of categorized sayings (humor, encouragement, coding wisdom, observations)
- Integrate with `TipsEngine` or `MainWindow` to trigger random sayings during idle state
- Sayings appear in the existing `TipBubbleWidget` (Win98-style speech bubble)
- Configurable frequency via `ConfigManager` (e.g., "never", "rarely", "sometimes", "often")
- Cooldown between random sayings to avoid spam
- No changes to the IPC protocol or event system

## Capabilities

### New Capabilities
- `random-sayings`: Random idle-time speech bubbles with categorized content
- `saying-pool-management`: Categorized saying pools with weights and cooldowns

### Modified Capabilities
- `idle-animation-rotation` (from `random-idle-animations`): Idle state machine will also trigger sayings alongside animations

## Impact

- New `src/RandomSayingsEngine.cpp/h` files
- `src/TipsEngine.cpp` — integration point for idle saying triggers
- `src/ConfigManager.cpp/h` — new config keys for saying frequency
- `src/mainwindow.cpp` — wire up `RandomSayingsEngine` to idle timer
- No asset changes
