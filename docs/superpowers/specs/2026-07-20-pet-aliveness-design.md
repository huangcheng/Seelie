# Pet Aliveness — Idle Sayings + Idle Animation Rotation

**Date:** 2026-07-20 · **Status:** draft (pending user review)
**Absorbs openspec proposals:** `openspec/changes/random-sayings`, `openspec/changes/random-idle-animations` (both pre-persona-era; archive on ship)

## Why

Seelie only speaks when `TipsEngine` fires or an event arrives, and its idle animation rotation uses a fixed 3s timer with naive weighted random. Between events the pet feels inert. This change adds (a) random idle sayings — canned pools by default with occasional LLM-generated quips — and (b) livelier idle animation rotation across all three engines (sprite, Lottie, Live2D).

## Decisions locked during brainstorm

- Sayings source: **canned pools + occasional LLM** (~15% of saying opportunities, opt-in).
- Animation coverage: **all three engines now**.
- `llmIdleQuipsEnabled` defaults to **off** (explicit cost opt-in).
- One combined coordinator (`IdleBehaviorEngine`) instead of the two engines the old proposals assumed.

## Goals / Non-goals

**Goals**
- Pet produces ambient speech and varied idle motion when truly idle.
- Sayings never preempt or delay event/tip/persona bubbles.
- Zero behavior change when no LLM profile is configured (fully offline path).
- Full i18n (en + zh_CN) for all user-visible strings.

**Non-goals**
- No IPC protocol changes; no new canonical events (the LLM quip path uses an internal pseudo-event, not routed over UDP).
- No new pack manifest fields — the existing engine-agnostic `idlePool` key (`CharacterPack.cpp:734-755`, `IdleEntry{animationName, weight}`) is reused.
- No changes to `PersonaPool` (LLM-generated pool lines stay event-scoped).
- Surprise non-idle animation picks are sprite-engine-only (see below); Lottie/Live2D get timing + anti-repeat only.

## Architecture

```
IPC events ──► EventRouter ──eventProcessed──► IdleBehaviorEngine (resets idle clock)
                                                  │
                        PetStateMachine ──stateChanged──► (gate: activeState()==Idle)
                                                  │
                    ┌─────────────────────────────┼──────────────────────────────┐
                    ▼                             ▼                              ▼
              SayingPool (canned,          PersonaEngine                    Animation engines
              i18n via tips bundle)        resolve("idle.quip")             (sprite/Lottie/Live2D:
                    │                      (OnDemand, async)                variable timing,
                    └────────► TipWidget.showBubble() ◄────────┘            anti-repeat, surprise)
```

`IdleBehaviorEngine` is owned by `MainWindow` (same as `TipsEngine`). It does not subclass or modify `EventRouter`; it observes `eventProcessed` to keep its idle clock.

## Components

### 1. `IdleBehaviorEngine` (new: `src/IdleBehaviorEngine.h/.cpp`)

Responsibilities: saying scheduling only. (Animation rotation upgrades live inside each animation engine — they already own idle timers and pools.)

- **Idle clock**: records `lastEventAt`, bumped on every `EventRouter::eventProcessed`. A saying slot fires when `now() - lastEventAt >= currentInterval` and the re-armed timer expires. Interval is re-randomized after every saying.
- **Frequency mapping** (config `sayingFrequency`, enum persisted as int, default `Sometimes`):
  | Setting | Interval (uniform random) |
  |---|---|
  | Never | engine disabled |
  | Rarely | 12–20 min |
  | Sometimes | 6–10 min |
  | Often | 2.5–4 min |
- **Gates** (all must hold at fire time, else skip this slot silently):
  - `PetStateMachine::activeState() == State::Idle` (no saying while grabbed/tossed/petted/working).
  - Main window visible (covers gaming-mode auto-hide).
  - `!m_tipWidget->isVisible()` — never stack on an event/tip/persona bubble.
  - No persona LLM upgrade in flight (track `requestId != 0` state via existing `tipUpgraded`/`tipUpgradeFailed` signals).
  (The shortest saying interval is 2.5 min, so the `lastEventAt` clock already suppresses sayings after `session.start`; no separate startup window is needed.)
- **LLM quip roll**: on each saying slot, if `llmIdleQuipsEnabled && personaEnabled && LLMProvider::isConfigured()`, 15% chance → request LLM quip instead of canned. Injected `RngFn` seam makes this deterministic in tests.
- **Test seams** (mirroring `SystemContextEngine.h:41-52`): `setNowFn(std::function<qint64()>)`, `setRngFn(std::function<double()>)`, plus a `tick()` driver so tests never wait on real timers.

### 2. `SayingPool` (new: `src/SayingPool.h/.cpp`)

- Loads categorized sayings from the **existing i18n tip bundles** (`assets/i18n/tips.en.json`, `assets/i18n/tips.zh_CN.json`, bundled via `assets/tips.qrc`) under a new top-level `"sayings"` key:
  ```json
  "sayings": {
    "humor":         [{"title": "...", "body": "..."}, ...],
    "encouragement": [...],
    "coding_wisdom": [...],
    "observation":   [...]
  }
  ```
- Category weights: humor 3, encouragement 3, coding_wisdom 2, observation 2.
- Anti-repeat: tracks the last saying index per category; never serves the same saying twice in a row (per-category pool > 1).
- Locale resolution mirrors `TipsCatalog` (`TipsCatalog.cpp:127`): `:/i18n/i18n/tips.<locale>.json`, falling back to `en`.
- Missing/empty `sayings` key → pool reports empty; `IdleBehaviorEngine` logs a warning and disables sayings (animations unaffected).

### 3. `PersonaEngine` — `idle.quip` pseudo-event

- New internal event name `idle.quip`, classified by `tierFor()` as `Tier::OnDemand`.
- `IdleBehaviorEngine` calls `resolve("idle.quip", {})`; the async upgrade arrives via the existing `tipUpgraded(requestId, text)` signal and is shown only if the idle gates **still hold** at delivery time (re-check; else drop).
- `resolveOnDemand` already: no-profile → `fallbackTip` with `requestId=0`; `shareMemoryWithAi` → appends `memoryDigest()` (`PersonaEngine.cpp:219-221`). Both behaviors are inherited unchanged.
- On `tipUpgradeFailed` for an `idle.quip` request → `IdleBehaviorEngine` immediately shows a canned saying instead (silent fallback, no error UI).
- Prompt addition: a short prompt template for idle quips (mood: ambient, one-liner, no questions to the user), stored alongside the existing persona prompt templates, i18n'd like other persona strings.

### 4. Animation engine idle upgrades (all three)

All three engines already have `m_idleTimer`, `m_idleAnims`/`m_idleWeights`, and `startIdleAnimation()` (sprite `SpriteAnimationEngine.cpp:502-528`; Lottie `LottieAnimationEngine.cpp:275-298`; Live2D `Live2DAnimationEngine.cpp` motion-group idle). Upgrades applied to each:

1. **Variable timing**: replace fixed `m_idleTimeoutMs = 3000` with a per-cycle uniform random in **1000–4000ms**, re-rolled each time the timer re-arms (in `onAnimationFinished` / equivalent).
2. **Anti-repeat**: new `m_lastIdleAnim` member; when `m_idleAnims.size() > 1`, exclude the last pick from the weighted draw.
3. **Surprise picks (sprite only)**: 10% of idle draws pick from a curated non-idle surprise pool instead of the idle pool.

**Sprite engine specifics** (absorbs `random-idle-animations` proposal):
- Add the 12 unmapped animations to `buildNameMap()` (`SpriteAnimationEngine.cpp:31-75`): `IdleHeadScratch`, `IdleFingerTap`, `IdleEyeBrowRaise`, `IdleRopePile`, `IdleSnooze`, `CheckingSomething`, `EmptyTrash`, `Hearing_1`, `LookDownLeft`, `LookDownRight`, `LookUpLeft`, `LookUpRight` — snake_case public names (e.g. `look_down_left`, `hearing`, `checking`, `empty_trash`, plus `idle_*` variants).
- Merge the five `Idle*` variants into the hardcoded idle pool (`SpriteAnimationEngine.cpp:153-178`).
- Surprise pool: `LookUpLeft`, `LookUpRight`, `LookDownLeft`, `LookDownRight`, `Hearing_1`, `CheckingSomething`, `EmptyTrash` at equal weight. Surprise picks never interrupt an event-triggered animation mid-play (they only fire from `startIdleAnimation()`, which already runs only when idle).

**Why sprite-only surprise picks**: sprite surprise candidates are known-safe short loops. Lottie/Live2D packs don't declare which non-idle clips are idle-safe; adding a manifest field for that is YAGNI today. Revisit if pack authors ask.

### 5. Config & settings UI

**ConfigManager** (pattern per `ConfigManager.cpp:231-249` and `DisplayMode` enum):
- `sayingFrequency` — enum `SayingFrequency { Never, Rarely, Sometimes, Often }`, persisted as int, default `Sometimes`, signal `sayingFrequencyChanged`.
- `llmIdleQuipsEnabled` — bool, default `false`, signal `llmIdleQuipsEnabledChanged`.

**SettingsPanelWidget**:
- General tab → Interaction group (`SettingsPanelWidget.cpp:593-640`): "Idle sayings" combo box (Never/Rarely/Sometimes/Often).
- LLM tab: "Occasional AI idle quips" checkbox next to `m_shareMemoryCheck`, enabled only when persona is enabled; tooltip discloses that quips send a short prompt (plus memory digest if `shareMemoryWithAi` is on) to the configured provider.
- All strings `tr()`; `Seelie_zh_CN.ts` updated.

## Behavior & preemption rules

1. **Sayings are lowest priority.** Any event, tip, or persona commentary overwrites a saying bubble — this falls out of the existing last-write-wins chain (`EventRouter.cpp:55-72`) and is desired. The reverse never happens because sayings only fire when no event has occurred within the frequency window and no bubble is visible.
2. The documented TipsEngine/catalog clobber bug (TODO.md §3) is **out of scope** here; sayings don't interact with it because they only fire from the idle gate.
3. Sayings are per-slot: a skipped slot (gate failed) is silent — no catch-up burst.
4. Animation idle upgrades apply whenever the engine enters idle, regardless of `sayingFrequency`; `Never` silences speech only.

## Error handling

| Failure | Behavior |
|---|---|
| LLM quip timeout/failure | Canned saying shown immediately; no user-visible error |
| `sayings` key missing/empty in bundle | Log warning; sayings disabled, animations unaffected |
| zh_CN bundle missing a saying entry | Fall back to en bundle (existing `TipsCatalog` pattern) |
| Pack `idlePool` names missing on disk | Existing load-time filtering already skips them (Live2D `loadFromCharacterPack` pattern); unchanged |
| Config key absent (upgrade path) | Defaults: `Sometimes`, LLM quips off |

## Testing (Qt Test, new `tests/test_idle_behavior.cpp` + engine test additions)

- **SayingPool**: bundle parsing, category weights over 10k draws (chi-square-ish bounds), per-category anti-repeat, en fallback when locale bundle lacks the key.
- **IdleBehaviorEngine** (with `NowFn`/`RngFn` seams + `tick()`): frequency interval ranges; no fire before threshold; fire after; gates (state != Idle, bubble visible, upgrade in flight, post-`session.start` 60s window); skipped slot leaves no catch-up; LLM roll at forced RNG 0.10 vs 0.20.
- **Preemption**: saying shown → event arrives → event bubble wins (integration with `TipWidget`).
- **LLM fallback**: `tipUpgradeFailed` → canned saying shown.
- **PersonaEngine**: `tierFor("idle.quip") == OnDemand`; no-profile → `requestId == 0` fallback.
- **Sprite engine**: `buildNameMap()` exposes all 12 new names (43 total playable); anti-repeat over 100 draws never repeats consecutively; surprise rate within [5%, 15%] over 1k draws; idle timeout values within [1000, 4000]ms.
- **Lottie/Live2D**: anti-repeat honored when pool > 1; single-entry pools still work (anti-repeat no-op).

## File impact

| File | Change |
|---|---|
| `src/IdleBehaviorEngine.h/.cpp` | **New** — saying scheduler |
| `src/SayingPool.h/.cpp` | **New** — categorized canned pools |
| `assets/i18n/tips.en.json`, `tips.zh_CN.json` | Add `"sayings"` section (~10 sayings/category/locale) |
| `src/PersonaEngine.h/.cpp` | `idle.quip` in `tierFor()`, idle prompt template |
| `src/SpriteAnimationEngine.h/.cpp` | +12 name-map entries, variable timeout, anti-repeat, surprise pool |
| `src/LottieAnimationEngine.h/.cpp` | Variable timeout, anti-repeat |
| `src/Live2DAnimationEngine.h/.cpp` | Variable timeout, anti-repeat |
| `src/ConfigManager.h/.cpp` | `sayingFrequency`, `llmIdleQuipsEnabled` keys |
| `src/SettingsPanelWidget.h/.cpp` | Sayings combo (General/Interaction), LLM quips checkbox (LLM tab) |
| `src/mainwindow.cpp` | Construct/wire `IdleBehaviorEngine`; gate on window visibility |
| `Seelie_zh_CN.ts` | New UI strings |
| `tests/test_idle_behavior.cpp`, engine tests | **New**/extended |
| `CMakeLists.txt` | New source + test targets |

## Rollout notes

- On ship: archive `openspec/changes/random-sayings` and `openspec/changes/random-idle-animations` via the openspec archive workflow (both are fully absorbed here, with the persona-era amendments recorded in this spec).
- No migration needed; new config keys have safe defaults.
