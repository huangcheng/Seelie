# TouchReactions (Spec 3) — Design Spec

**Status:** approved design, pending implementation plan
**Date:** 2026-07-17
**Program:** memory → senses → **touch** → AI commentary (Spec 3 of 4)

## Goal

Let the user interact with the pet directly through the mouse: hover over it, pet
it with strokes, grab and carry the window, and toss it — with the pet reacting
visibly (new FSM overlay states reusing existing pack animations), emotionally
(affection gains via MemoryManager), and verbally (canned tip-bubble lines).
Touch must never fight window dragging: stroke and drag are disambiguated by a
dedicated detector, and dragging behaves exactly as today when it's a drag.

## Architecture

```
 mouse events on MainWindow
        │
        ▼
 ┌─────────────────┐   undecided → stroke | drag
 │ StrokeDetector  │──user.pet (per stroke)──┐
 │ VelocityTracker │──user.toss (fast release)┤  (pure C++ units,
 └─────────────────┘──user.grab / grabEnd ────┤   widget-free, testable)
        │                                     ▼
        │                        PetStateMachine::onSyntheticEvent
        │                            │ new overlay states
        │                            ▼
        │              Petted / Tossed (one-shot 2s)
        │              Grabbed (sustained, exits on grabEnd)
        │                            │ chain candidates (existing pack anims)
        ▼                            ▼
 MemoryManager (throttled)      MainWindow tip bubble
 addAffection / increment /     (canned lines from new
 checkMilestone                  "touch" pool in tips JSON)
```

- Detection lives in MainWindow's existing mouse handlers (they own drag state);
  the decision logic itself lives in two pure units — `StrokeDetector` and
  `VelocityTracker` — so it is unit-testable without widgets.
- `PetStateMachine` gains three overlay states and reacts to new `user.*`
  synthetic events; it stays the single state→animation authority.
- Memory writes stay in MainWindow, mirroring `tryRecordPoke`'s throttle pattern.

## 1. Gesture Detection

### StrokeDetector (new pure unit, `src/StrokeDetector.h/.cpp`)

Fed `(globalPos, timestampMs)` on press/move and button transitions; emits a
state: `Undecided → Stroking | Dragging`, plus per-stroke pulses.

- Press inside `petRect()` starts **undecided**: window does NOT move yet.
- Direction reversal: cursor dx sign change ≥ 8 px from the last extremum
  (jitter rejection).
- **Stroking** when ≥ 2 reversals AND max displacement from press point < 15 px.
  Window stays put for the whole stroke session.
- **Dragging** when displacement from press point ≥ 15 px before 2 reversals —
  the detector reports the drag and MainWindow applies movement retroactively
  from the press point (no visible jump; existing `m_dragWindowPos + delta`
  math resumes).
- **Stroke pulses:** entering Stroking at reversal #2 fires pulse #1; every
  further reversal fires another pulse (each reversal = one stroke endpoint).
  MainWindow fires one `user.pet` per pulse.
- Reset on release, `leaveEvent`, or double-click. Press outside `petRect()` =
  plain drag (detector never engages — current behavior preserved).

### VelocityTracker (new pure unit, same header pair)

- Feeds the same move stream; keeps an exponential moving average of speed
  (window ≈ 100 ms) of the global cursor during drag mode.
- On release: speed > **1500 px/s** ⇒ `user.toss`; otherwise today's release
  behavior (`lookdown` + `rest`, `positionChanged`).
- Toss is **reaction-only** — the window stays where released. No momentum
  physics (parked, see Out of Scope).

### Hover

- `enterEvent`/`leaveEvent` already fire `user.hoverEnter`/`user.hoverLeave`
  (today: FSM ignores them). This spec keeps them **visually silent** —
  Live2D packs already track the pointer (look-at); sprite/Lottie packs get no
  overlay. Hover's only effect is memory (§3) and a stats counter.

### Grab

- Entering drag mode (real window movement begins) fires `user.grab` once.
- Release fires `user.grabEnd`, then the normal release path (or toss).

## 2. FSM States & Animation

`PetStateMachine::State` gains: `Petted`, `Grabbed`, `Tossed`.

- `user.pet` → `enterOneShot(Petted, NOTIFICATION_ONESHOT_MS)` — retriggerable;
  each stroke while stroking refreshes the one-shot.
- `user.grab` → sustained `Grabbed` overlay (entered like a one-shot but without
  the timer); `user.grabEnd` → overlay clears back to the saved sustained state
  (same restore machinery as `onOneShotFinished`).
- `user.toss` → `enterOneShot(Tossed, NOTIFICATION_ONESHOT_MS)`.
- All three respect the grace-neutrality rule from the memory-2.0 FSM fix
  (overlays no longer kill Working grace).
- `user.click`/`user.doubleclick` unchanged (Greeting one-shot).

Chain candidates added in `rebuildChainsFromMaps` (first hit wins per pack;
every shipped pack resolves at least the idle fallback today):

| State | Candidates (in order) |
|---|---|
| Petted | `pat`, `happy`, `celebrate`, `Celebrate`, `Congratulate`, `Greeting` |
| Grabbed | `grab`, `alert`, `Alert`, `GetAttention`, `attention` |
| Tossed | `toss`, `running-right`, `running-left`, `running`, `gesture_up`, `GestureUp` |

Rationale: no pack ships touch frames today; semantic-first candidate lists let
future packs register true `pat`/`grab`/`toss` animations in their nameMap
while every current pack falls back to a sensible existing animation.

## 3. Memory Effects (positive-only)

Mirrors `tryRecordPoke`'s throttle shape, with per-gesture throttle members:

| Gesture | Effect | Throttle |
|---|---|---|
| `user.pet` (per stroke) | `addAffection(+2)`, `increment("stats.pets")` | 2 s (`m_lastPetWriteMs`) |
| hover (on `user.hoverEnter`) | `addAffection(+1)`, `increment("stats.hover")` | 60 s (`m_lastHoverWriteMs`) |
| `user.grab` | `increment("stats.grabs")` only | per drag |
| `user.toss` | `increment("stats.tosses")` only | per toss |

- Milestones via `checkMilestone`: `first_pet`, `first_toss` (one-time bubbles).
- **No bond XP** on routine touch — bond stays session/milestone-driven
  (decided 2026-07-17). **No affection loss** on grab/toss (positive-only,
  decided 2026-07-17).

## 4. Canned Verbal Reactions

New `"touch"` section in `assets/i18n/tips.en.json` and `tips.zh_CN.json`:
per-gesture arrays of short lines (greetings-pool style), e.g.
`"pet": [...]`, `"toss": [...]`. Loaded through the existing TipsCatalog JSON
bundle (add a `touchLines(gesture)` accessor alongside `randomGreeting`).

- **Pet:** bubble shows a random pet line ~1-in-3 strokes (spam control).
- **Toss:** bubble always shows a random toss line.
- **Grab / hover:** silent.
- Persona/LLM lines for touch events are a **Spec 4 seam** (touch events are
  OnDemand-tier compatible); this spec is canned-only.

## 5. Config & Settings

- `ConfigManager::touchReactionsEnabled()` (default **true**), setter +
  `touchReactionsEnabledChanged(bool)` signal, persisted as `touchReactions` —
  exact mirror of the `contextSensesEnabled` pattern (Spec 2 Task 3).
- Checkbox in SettingsPanelWidget's Interaction group ("Touch reactions",
  zh: 触摸互动) with live enable/disable.
- **Off ⇒ mouse handling behaves exactly as today**: no undecided mode (drag
  starts at DRAG_THRESHOLD as now), no stroke/grab/toss events, no hover
  effects. Click/double-click pokes and greetings are unchanged either way.

## 6. Error Handling & Edges

- Stroke session resets on: release, `leaveEvent`, double-click, or press
  outside `petRect()`. `leaveEvent` mid-stroke cancels without release effects.
- Undecided mode never blocks the existing drag: crossing 15 px converts to
  drag retroactively, so a fast drag feels identical to today.
- ECG display mode and gaming-hidden: existing gating untouched (mouse handlers
  already no-op appropriately).
- Multi-monitor/retina: global coordinates everywhere (`globalPosition()`),
  matching current drag math.
- Detector never fires while a context-menu is open (press consumed by menu).
- Velocity tracker resets on press; a stalled drag (cursor parked ≥ 200 ms
  before release) decays to ~0 ⇒ never a false toss.

## 7. Testing

- **`tests/test_stroke_detector.cpp`** (new): reversal counting, 8 px jitter
  rejection, 15 px displacement budget, undecided→drag conversion + retroactive
  movement signal, reset paths (release/leave/double-click), out-of-petRect
  never engages, velocity EMA + 1500 px/s threshold + parked-cursor decay.
  Pure unit tests, no widgets.
- **`tests/test_pet_state_machine.cpp`** (extend): Petted/Tossed one-shot
  behavior (enter, expire, restore), retrigger while stroking, Grabbed
  sustained enter/exit via grab/grabEnd, chain candidates resolve with fallback,
  grace-neutrality preserved (overlay from Working restores Working).
- **Config round-trip** for `touchReactionsEnabled`, placed in
  `test_stroke_detector.cpp` with its own `QSettings` temp-dir redirect in
  `initTestCase` (the established pattern from test_gaming_mode).
- **Widget-level gesture tests: skipped deliberately** (MainWindow construction
  needs the full engine stack; detector purity makes them unnecessary).
- Full suite must stay green (18/18 → more with new files).

## Files Changed (planned)

- **New:** `src/StrokeDetector.h`, `src/StrokeDetector.cpp`,
  `tests/test_stroke_detector.cpp`
- **Modified:** `src/MainWindow.cpp` + `src/mainwindow.h` (mouse handlers,
  throttles, detector wiring), `src/PetStateMachine.h/.cpp` (3 states, chains,
  grab overlay), `src/ConfigManager.h/.cpp` (toggle), `src/TipsCatalog.h/.cpp`
  (`touchLines`), `assets/i18n/tips.en.json`, `assets/i18n/tips.zh_CN.json`,
  `src/SettingsPanelWidget.cpp` (checkbox), `Seelie_zh_CN.ts` (checkbox string),
  `CMakeLists.txt` + `tests/CMakeLists.txt`, `TODO.md` (status)

## Constraints

- C++17, Qt6, existing style: `QStringLiteral`, UPPERCASE acronyms, dense
  reason-comments, conventional commits.
- No new dependencies; no asset authoring (no new animation frames this spec).
- Zero behavioral change to window dragging, click pokes, and double-click
  greetings when the feature is on **or** off, except where this spec explicitly
  adds reactions.
- Pet stays < 10 MB RAM: detectors are O(1) per mouse event (fixed-size state).

## Decision Record

- 2026-07-17 — Pet vs drag: stroke detection (≥2 reversals, <15 px budget),
  window stays put while stroking; drag converts retroactively. (user)
- 2026-07-17 — Animation: new FSM states (Petted/Grabbed/Tossed) with chain
  candidates reusing existing per-pack animations; no new assets. (user)
- 2026-07-17 — Affection: positive-only; pet +2 (2 s), hover +1 (60 s),
  grab/toss 0; no bond XP on routine touch. (user)
- 2026-07-17 — Toss is reaction-only; no momentum physics (parked). (user)
- 2026-07-17 — Architecture: MainWindow + FSM with two pure detector units;
  TouchEngine / FSM-only designs rejected.
- 2026-07-17 — Hover is visually silent (Live2D look-at already exists);
  memory + stats only.

## Out of Scope

- Momentum glide / bounce physics for tossed windows (parked future delight).
- New animation frames / pack asset authoring (candidate fallbacks cover it).
- Persona/LLM-generated touch lines (Spec 4 seam).
- Per-gesture enable toggles, configurable thresholds in UI (constants).
- Widget-level QTest gesture playback (detector purity covers logic).
