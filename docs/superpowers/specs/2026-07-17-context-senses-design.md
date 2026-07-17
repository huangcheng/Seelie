# ContextSenses (Spec 2) — Design Spec

**Status:** approved design, pending implementation plan
**Date:** 2026-07-17
**Program:** memory → **senses** → touch → AI commentary (Spec 2 of 4)

## Goal

Give the pet awareness of system/session context by emitting synthetic `context.*`
events into the existing IPC/event pipeline, so it can react to late nights, long
sessions, user idleness/return, gaming, low battery, and time of day — with rate
limits so reactions stay charming rather than noisy. Also finish the Linux
`FullscreenWatcher` stub (X11) so Gaming Mode works there.

## Architecture

One new engine, shaped like the codebase's existing single-purpose engines
(TipsEngine, FullscreenWatcher, MemoryManager):

```
┌─────────────┐   events    ┌──────────────────────┐
│  IPCServer  │────────────▶│     EventRouter      │──▶ EventAction (anim/effect/tip)
└─────────────┘             │                      │──▶ TipsEngine
┌─────────────┐   context.* │  (validation via     │──▶ PetStateMachine
│ SystemContext│────────────▶│   s_validEvents)     │──▶ MemoryManager stats
│   Engine     │             └──────────────────────┘
└─────────────┘  (observes via eventProcessed for idle-reset / time-of-day)
```

- `SystemContextEngine` (QObject) is constructed in `main.cpp`, receives
  `EventRouter*` and `ConfigManager*`, and is given the existing
  `FullscreenWatcher*` for gaming signals (ownership stays with MainWindow).
- Emission: `eventRouter->routeEvent()` with
  `{type:"event", source:"system", event:"context.<name>", ...payload}` — identical
  shape to gateway messages, so the whole downstream pipeline (animation, effect,
  tip bubble, memory stats, future persona) works unchanged.
- Observation: connects to `EventRouter::eventProcessed` to (a) reset the
  no-activity clock, (b) track session active/inactive via `session.start`/`session.end`,
  (c) emit the time-of-day follow-up on `session.start`.
- Detectors are internal timers/state inside the engine — deliberately **not**
  per-detector classes (rejected as over-decomposition, ~250 lines total).

## 1. Event Set

Seven new canonical names added to `src/CanonicalEvents.h` and `s_validEvents`
(`src/EventRouter.cpp:15-24`):

| Event | Fires when | Payload | Cooldown / re-arm |
|---|---|---|---|
| `context.latenight` | Wall clock ≥ 23:00 while a session is active | `{hour}` | once per night (20h) |
| `context.longsession` | Continuous session ≥ 3h | `{hours}` | 2h |
| `context.idle` | No IPC events for ≥ 10 min (app-level inactivity) | `{minutes}` | 30 min; re-arms on activity |
| `context.away` | OS input idle ≥ 5 min, **fires on input return** (welcome-back) | `{awayMinutes}` | once per away episode |
| `context.gaming` | Fullscreen app **stops** (welcome-back; start is silent auto-hide) | `{}` | 30 min |
| `context.lowbattery` | Battery ≤ 20% and discharging | `{percent}` | re-arm on AC attach or > 30% |
| `context.timeofday` | After every `session.start` (queued follow-up) | `{bucket}` — morning 05–11, afternoon 11–17, evening 17–22, night 22–05 | once per session |

Time-of-day is its own synthetic event because the engine cannot mutate the
gateway's `session.start` payload; a queued follow-up keeps gateway messages
untouched.

`context.idle` (no tool activity) and `context.away` (OS input idle) are distinct
events with distinct detectors — decided 2026-07-17.

## 2. Detectors

All timers and cooldown constants live at the top of `SystemContextEngine.cpp`,
mirroring `TipsEngine`'s `m_cooldownMinMs` / `m_lastTriggered` pattern
(`src/TipsEngine.h:68-70`). Cooldown bookkeeping:
`QMap<QString, qint64> m_lastFired` keyed by event name, checked in one
`emitIfAllowed(name, payload)` helper.

- **Clock timer (60s)** — latenight + longsession. Session-active flag and
  session-start timestamp maintained from `eventProcessed`.
- **Shared 30s tick** — one QTimer drives the activity-idle check and the
  OS-input-idle probe; battery is evaluated every second tick (≈60s). Total
  timer load: one 60s clock timer + one 30s shared tick.
- **Activity idle** — `QElapsedTimer` reset on every `eventProcessed`; checked on
  the shared tick.
  `CGEventSourceSecondsSinceLastEventType`, Windows `GetLastInputInfo`, Linux
  `XScreenSaverQueryInfo` (X11 only). Tracks away-episode state; fires on return.
- **Battery** — evaluated every second shared tick (≈60s). macOS IOPS
  power-source info, Windows `GetSystemPowerStatus`, Linux
  `/sys/class/power_supply`. No battery (desktop) → detector disabled.
- **Gaming** — hooks existing `FullscreenWatcher::fullscreenAppStarted/Stopped`.
  Start: silent (auto-hide already handled by MainWindow; **no event** — a visible
  reaction while hiding is pointless and delaying hide is risky mid-game).
  Stop: `context.gaming` welcome-back. Decided 2026-07-17.
- **Time-of-day** — on `eventProcessed(session.start)`, queued emission of
  `context.timeofday` with the current hour bucket; once per session.

## 3. Presentation & i18n

- Tip text added to `assets/i18n/tips.en.json` and `assets/i18n/tips.zh_CN.json`
  (the JSON catalog path used by TipsCatalog — **not** the `.ts` file, which is
  for UI strings). `context.timeofday` gets an intentionally **empty** entry
  (like `session.idle`) — it is an enrichment event for Spec 4 prompts, not a
  bubble. Empty entry ⇒ `EventRouter` skips the bubble (`tip.title.isEmpty()`).
- **Animations/persona lines for context events are NOT in this spec.** Planning
  discovered the "EventAction animation table" from CLAUDE.md no longer exists —
  animations are driven by PetStateMachine states and TipsEngine patterns, and no
  fitting FSM state exists for latenight/longsession/etc. Context events deliver
  tip bubbles + memory stats here; Spec 4 already owns persona-pool lines and can
  decide on animations then.
- One master config toggle `contextSensesEnabled` (default **on**) in
  ConfigManager, with a live `contextSensesEnabledChanged` signal (mirrors the
  gamingMode pattern). Settings-panel UI checkbox: YAGNI (INI-editable; add when
  Spec 4 lands). Gaming keeps its existing `gamingMode` toggle.

## 4. Error Handling

- Every platform probe self-disables on first failure with a single `qWarning`
  (no retries, no crash paths): desktop without battery → battery detector off;
  Wayland session → away-probe and fullscreen detection off (one-time log).
- Engine never blocks the pipeline: all detectors are timer-driven, emission is
  fire-and-forget through `routeEvent`.

## 5. Linux FullscreenWatcher

- Implement `FullscreenWatcher::checkFullscreen()` for Linux **X11 only**: focused
  window has `_NET_WM_STATE_FULLSCREEN` (XCB, matching the file's existing
  platform-impl style at `src/FullscreenWatcher.cpp`).
- Wayland remains a stub returning false, with a one-time log and a TODO.md note.
  X11 covers the Qt6/Linux gaming desktop majority; Wayland fullscreen detection
  is compositor-specific and out of scope.

## 6. Testing

`tests/test_system_context.cpp` following the `test_gaming_mode` pattern
(mock-subclass + `QTEST_MAIN`, registered in `tests/CMakeLists.txt`):

- Probe seams injected (same style as `EmbeddingService`'s injectable embed fn):
  clock, OS-idle probe, battery probe, fullscreen state — so tests never touch
  real OS APIs or wall-clock waits (detector slots invoked directly).
- Cases: cooldown suppression (second fire within window is dropped); latenight
  once per night; idle reset on activity then re-arm; away → welcome-back on
  return (and NOT while still away); battery re-arm on AC; gaming fires on stop
  only; time-of-day once per session with correct bucket; all 7 names accepted by
  `s_validEvents`; tips JSON entries resolve through TipsCatalog for en + zh_CN.
- Full suite must remain green (`ctest`), including the 17 existing tests.

## Files Changed (planned)

- **New:** `src/SystemContextEngine.h`, `src/SystemContextEngine.cpp`,
  `tests/test_system_context.cpp`
- **Modified:** `src/CanonicalEvents.h`, `src/EventRouter.cpp` (valid set),
  `src/FullscreenWatcher.cpp`
  (X11 impl), `src/ConfigManager.h/.cpp` (`contextSensesEnabled` key),
  `src/mainwindow.h` (fullscreenWatcher getter) + `src/main.cpp` (wiring),
  `assets/i18n/tips.en.json`,
  `assets/i18n/tips.zh_CN.json`, `CMakeLists.txt` (engine sources, IOKit/X11
  linkage), `tests/CMakeLists.txt` (engine lib sources, new test, tips
  resources), `TODO.md` (status)

## Constraints

- C++17, Qt6, existing style: `QStringLiteral`, UPPERCASE acronyms in identifiers,
  `tr()` for UI strings, dense reason-focused comments.
- No new third-party dependencies. XCB usage must compile only where available
  (Linux/X11 guard), keeping Windows/macOS builds untouched.
- Engine must not wake the machine meaningfully: exactly two timers — one 60s
  clock timer and one shared 30s tick (idle + away + battery every other tick).
- Master toggle off ⇒ zero timers running, zero emissions.

## Decision Record

- 2026-07-17 — Full TODO scope in one cycle (engine + all events + Linux
  fullscreen + gaming wiring). (user)
- 2026-07-17 — `context.idle` (no tool activity) and `context.away` (OS input
  idle) are separate events/detectors. (user)
- 2026-07-17 — Gaming: quiet hide on fullscreen start (no event), welcome-back
  `context.gaming` on stop. (user)
- 2026-07-17 — Architecture: single `SystemContextEngine` over extending
  TipsEngine or per-detector micro-classes. (user)
- 2026-07-17 — Time-of-day is a synthetic follow-up event (`context.timeofday`),
  not a payload mutation of `session.start`.
- 2026-07-17 — Linux fullscreen: X11-only; Wayland stubbed with documentation.
- 2026-07-17 — Planning discovery: the "EventAction animation table" described
  in CLAUDE.md no longer exists (EventRouter routes tips via TipsCatalog JSON;
  animations via PetStateMachine/TipsEngine). Presentation scope reduced to
  tip bubbles + stats; animations/persona deferred to Spec 4. `context.timeofday`
  carries an empty tip entry by design.

## Out of Scope

- Persona/pool lines and LLM prompt use of context events (Spec 4).
- Touch events (`user.hover`/`user.pet`/…, Spec 3).
- Wayland fullscreen / idle detection.
- Per-event enable toggles, configurable thresholds (constants are fine for now).
- `RemoteMemoryBackend` and parked memory follow-ups.
