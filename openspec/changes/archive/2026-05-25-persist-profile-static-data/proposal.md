## Why

The Statistics panel (accessible via system tray → "Statistics") displays session counters for TTS, AI Persona, Events, and IPC activity. Currently, these statistics are purely in-memory and reset to zero on every app restart. The comments in the code explicitly note this is intentional due to thread-safety concerns with SQLite. However, users want their statistics to persist across sessions. This change introduces a dedicated `statistics.json` file in the config directory (`~/.config/Seelie/` on macOS) to persist all Statistics panel data, bypassing the SQLite thread-safety issue by writing from the main thread on app shutdown.

## What Changes

- Add a new `statistics.json` file in the config directory for persisting statistics data
- Modify `TTSEngine`, `PersonaEngine`, `EventRouter`, and `IPCServer` to save their stats to `statistics.json` on shutdown / periodically
- Load persisted stats from `statistics.json` on startup and initialize counters from saved values
- Keep all existing APIs unchanged — `stats()` methods continue to return the struct; persistence is transparent
- Add a `StatisticsManager` or use `MemoryManager` to coordinate reading/writing `statistics.json`

## Capabilities

### New Capabilities
<!-- No new user-facing capabilities. Statistics panel behavior remains identical. -->

### Modified Capabilities
<!-- No spec-level requirement changes. -->

## Impact

- `src/TTSEngine.h` / `.cpp` — load/save stats to `statistics.json`
- `src/PersonaEngine.h` / `.cpp` — load/save stats to `statistics.json`
- `src/EventRouter.h` / `.cpp` — load/save stats to `statistics.json`
- `src/IPCServer.h` / `.cpp` — load/save stats to `statistics.json`
- `src/MemoryManager.h` / `.cpp` — OR a new helper for JSON read/write
- `src/main.cpp` — wire shutdown save / startup load
- No breaking changes to public APIs or user-facing behavior
