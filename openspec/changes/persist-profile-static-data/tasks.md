## 1. Add statistics.json Persistence Helper

- [x] 1.1 Create a helper class/function for atomic JSON read/write (or add to existing utility)
- [x] 1.2 Define `statistics.json` schema/structure

## 2. TTSEngine — Persist Stats

- [x] 2.1 Add `loadStats(const QString &configDir)` method to read TTS stats from `statistics.json`
- [x] 2.2 Add `saveStats(const QString &configDir)` method to write TTS stats to `statistics.json`
- [x] 2.3 Call `loadStats()` in constructor or `start()`
- [x] 2.4 Ensure `sessionRequests` and `sessionHits` resume from persisted values

## 3. PersonaEngine — Persist Stats

- [x] 3.1 Add `loadStats(const QString &configDir)` method
- [x] 3.2 Add `saveStats(const QString &configDir)` method
- [x] 3.3 Call `loadStats()` in constructor
- [x] 3.4 Persist all fields: refillsOk/Fail, ondemandOk/Fail, tokensIn/Out, lastError

## 4. EventRouter — Persist Stats

- [x] 4.1 Add `loadStats(const QString &configDir)` method
- [x] 4.2 Add `saveStats(const QString &configDir)` method
- [x] 4.3 Call `loadStats()` in constructor
- [x] 4.4 Handle `perEvent` QHash serialization to/from JSON object

## 5. IPCServer — Persist Stats

- [x] 5.1 Add `loadStats(const QString &configDir)` method
- [x] 5.2 Add `saveStats(const QString &configDir)` method
- [x] 5.3 Call `loadStats()` in constructor or `start()`

## 6. Main — Wire Startup Load & Periodic Save

- [x] 6.1 In `main.cpp`, call `loadStats()` for all components after construction
- [x] 6.2 Set up a `QTimer` that calls `saveStats()` for all components every 60 seconds
- [x] 6.3 Connect `QCoreApplication::aboutToQuit` to a final `saveStats()` call before exit

## 7. Verification

- [x] 7.1 Build compiles successfully
- [ ] 7.2 Run app, generate some TTS/events/IPC activity
- [ ] 7.3 Wait 60 seconds, verify `~/.config/Seelie/statistics.json` is updated by periodic auto-save
- [ ] 7.4 Open Statistics panel, note the values
- [ ] 7.5 Force-kill app (SIGKILL), restart, verify stats resumed from last periodic save (not zero)
- [ ] 7.6 Gracefully close app, verify final `statistics.json` is written
- [ ] 7.7 Click "Reset stats" and verify `statistics.json` is cleared or values reset
