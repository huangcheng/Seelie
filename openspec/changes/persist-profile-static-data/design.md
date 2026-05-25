## Context

The Statistics panel displays four categories of session data:

1. **TTS Cache** (from `TTSEngine`):
   - `sessionRequests` — total TTS synthesis requests
   - `sessionHits` — cache hits
   - `lastMissMs` — timestamp of last cache miss
   - `lastMissText` — text of last cache miss

2. **AI Persona** (from `PersonaEngine`):
   - `refillsOk` / `refillsFail` — pool refill success/failure
   - `ondemandOk` / `ondemandFail` — on-demand LLM call success/failure
   - `tokensIn` / `tokensOut` — LLM token counts
   - `lastError` — last error message

3. **Events** (from `EventRouter`):
   - `total` — total events received
   - `perEvent` — per-event-type counts (QHash)
   - `lastEventMs` / `lastEventName` — last event info

4. **IPC** (from `IPCServer`):
   - `packets` — UDP packets received
   - `decodeErrors` — JSON parse failures
   - `startedAtMs` — server start timestamp

Currently, all of these are in-memory counters that reset on app restart. The code comments explicitly state this is intentional because:
- `TTSEngine` runs on a worker thread; SQLite writes from worker threads violate the single-connection thread invariant
- `IPCServer`'s `UDPWorker` lives on a worker thread with the same constraint

## Goals / Non-Goals

**Goals:**
- Persist all Statistics panel data to a dedicated `statistics.json` file in the config directory
- Load saved statistics on startup so counters resume from previous values
- Write statistics on app shutdown (main thread, avoiding worker-thread SQLite issues)
- Keep all existing `stats()` APIs unchanged

**Non-Goals:**
- No real-time persistence (e.g., after every event); shutdown-only is sufficient
- No changes to the StatisticsDialog UI
- No changes to how other `MemoryManager` data is stored

## Decisions

### 1. Dedicated statistics.json file
**Decision**: Statistics are persisted to `<configDir>/statistics.json` as a JSON object.

**Rationale**: A separate file avoids mixing operational memory data (milestones, greetings) with statistics. JSON is human-readable and Qt has built-in support. Writing happens from the main thread on shutdown, avoiding the worker-thread SQLite constraint.

**Format**:
```json
{
  "tts": {
    "sessionRequests": 42,
    "sessionHits": 38,
    "lastMissMs": 1234567890,
    "lastMissText": "hello world"
  },
  "persona": {
    "refillsOk": 10,
    "refillsFail": 2,
    "ondemandOk": 5,
    "ondemandFail": 1,
    "tokensIn": 1500,
    "tokensOut": 3200,
    "lastError": ""
  },
  "events": {
    "total": 150,
    "perEvent": {
      "tool.before": 50,
      "file.edited": 30
    },
    "lastEventMs": 1234567890,
    "lastEventName": "tool.before"
  },
  "ipc": {
    "packets": 200,
    "decodeErrors": 0,
    "startedAtMs": 1234567000
  }
}
```

### 2. Write on shutdown AND periodic auto-save
**Decision**: 
- **Startup**: Each engine/router loads from `statistics.json` in its constructor or init.
- **Periodic auto-save**: A `QTimer` fires every 60 seconds on the main thread, calling `saveStats()` for all components. This limits data loss on abnormal termination to at most 60 seconds.
- **Shutdown**: `main.cpp` explicitly calls `saveStats()` before `QApplication` exits.

**Rationale**: Shutdown-only is too fragile — crashes, force-quit, or SIGKILL would lose the entire session. Periodic saves from the main thread avoid worker-thread SQLite issues while providing reasonable durability. 60 seconds is frequent enough to not lose much data, but infrequent enough to avoid constant disk I/O.

### 3. Each component owns its own serialization
**Decision**: `TTSEngine`, `PersonaEngine`, `EventRouter`, and `IPCServer` each get `loadStats()` and `saveStats()` methods that know their own struct fields.

**Rationale**: Encapsulation. Each component knows its own stats format. A central manager would need to know about every field of every struct.

### 4. Atomic write
**Decision**: Use `QSaveFile` or write-to-temp-then-rename to avoid corrupting `statistics.json` if the app crashes mid-write.

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| App crash before shutdown loses session stats | Periodic auto-save every 60s limits loss to ≤60 seconds of stats |
| Multiple instances overwrite each other | Document as unsupported; Seelie is a single-instance app |
| JSON file grows large (per-event hash) | `EventRouter::perEvent` is bounded by the ~17 canonical events |
| Old format `statistics.json` after schema changes | Ignore unknown keys on read; write full current schema |

## Migration Plan

1. Deploy code change
2. On next app start, all stats load as 0 (no `statistics.json` yet)
3. During the session, stats accumulate normally and auto-save every 60s
4. On app exit, `statistics.json` is written with final accumulated values
5. On subsequent starts, counters resume from last saved values

## Open Questions

- Should we also save stats when the Statistics dialog is closed? (Out of scope — shutdown-only is simpler)
