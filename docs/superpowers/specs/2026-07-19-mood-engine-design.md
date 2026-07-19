# MoodEngine & Proactive Companionship — Design Spec

**Status:** approved design, pending implementation plan
**Date:** 2026-07-19
**Follows:** memory → senses → touch → AI commentary (complete); recall UI (shipped)

## Goal

Give the pet an inner life that persists between tool events. A continuous mood
state (valence + energy) is shaped by coding events, touch interactions, time of
day, and absence; it surfaces through idle-animation bias, pool-line tone, and
four proactive behaviors (morning greeting, long-session nudge, missed-you,
bond stage-up). The relationship axis is **not new** — it reuses
`MemoryManager`'s existing affection / bondLevel / milestone machinery.
Visibility is ambient + peek: no meters, but the tray menu and hover tooltip
answer "why is my pet acting like this?"

## Architecture

```
 EventRouter::eventProcessed ──────────────┐  (same downstream hook
                                           │   PetStateMachine uses)
 user.pet / user.toss (touch)              │
                                           ▼
                              ┌────────────────────────┐   30 s QTimer tick
                              │       MoodEngine       │◄── decay toward
                              │  valence ∈ [-1,1]      │    time-of-day baseline
                              │  energy  ∈ [-1,1]      │
                              │  tier quantization     │   (hysteresis +0.1)
                              └───────────┬────────────┘
                    moodTierChanged(Tier) │ reads
                                          ▼
              ┌───────────────┐   MemoryManager (existing)
              │  MainWindow    │  affection 0..100, bondLevel L0..L5,
              │  idle-variant  │  bondLevelChanged → stage-up
              │  bias, peek UI │
              └───────────────┘
                                          │
        proactive timers (SystemContextEngine pattern, per-type cooldowns,
        global 1/hour cap) emit synthetic mood.* events back through
        EventRouter → TipsEngine / stats / PersonaEngine unchanged:

   mood.greeting · mood.long_session · mood.missed_you · mood.stage_up
```

- `MoodEngine` owns only the **ephemeral** mood vector. Relationship state
  stays in `MemoryManager` (affection, bondXP/bondLevel, milestones) — no
  duplicate affinity score, no parallel persistence.
- Surfacing follows the established persona split: **pool-tier lines** for
  routine proactive bubbles, **on-demand LLM** only for rare milestones
  (stage-up, missed-you after > 72 h).

## 1. MoodEngine (new unit, `src/MoodEngine.h/.cpp`)

Pure logic, widget-free, unit-testable. QObject for signals/timers.

- **State:** `valence`, `energy` (both clamped [-1, 1]).
- **Delta intake** via `onEventProcessed(eventName, payload)` connected to
  `EventRouter::eventProcessed`. Delta table (initial tuning, constants in one
  place for easy adjustment):

  | Input | valence | energy |
  |---|---|---|
  | `tool.failed` (≥3 in 60 s burst) | −0.15 | +0.10 |
  | `session.error` | −0.10 | +0.05 |
  | `user.pet` (per pulse) | +0.08 | +0.02 |
  | `user.toss` | −0.20 | +0.10 |
  | `session.start` after > 12 h absence | +0.25 | +0.25 |
  | `todo.updated` (completion) | +0.05 | — |
  | continuous session > 2 h (evaluated on tick) | — | −0.05 / tick |

- **Decay (30 s tick):** valence → 0 at 0.01/tick; energy → time-of-day
  baseline at 0.01/tick (baseline −0.2 between 23:00–06:00, 0 otherwise,
  reusing the `context.timeofday` sense's daypart boundaries).
- **Tier quantization:**

  | Tier | Condition |
  |---|---|
  | Lonely | absence > 24 h flag set (overrides vector) |
  | Excited | valence > 0.3 AND energy > 0.3 |
  | Tense | valence < −0.3 AND energy > 0.3 |
  | Tired | energy < −0.3 |
  | Content | otherwise |

  Hysteresis: a new tier must beat its threshold by +0.1 to take over;
  `moodTierChanged(Tier)` emitted only on actual transitions.
- **Lonely flag:** set at launch when elapsed absence > 24 h; cleared by the
  first `session.start` (which also fires the excitement spike).

## 2. Relationship axis — MemoryManager reuse

- Stage bands over existing `bondLevel()` (L0..L5): **Stranger** = L0–L1,
  **Companion** = L2–L3, **Partner** = L4–L5. Stage names are a MoodEngine-side
  mapping; MemoryManager is untouched.
- `MemoryManager::bondLevelChanged` → MoodEngine emits `mood.stage_up`
  (guarded by `hasMilestone("mood.stage_up.L<n>")` so each level-up fires once).
- Positive interactions already feed `addBondXP`/`addAffection` from the touch
  spec — no new write paths.

## 3. Proactive behaviors

Timers and cooldowns follow `SystemContextEngine`'s synthetic-event pattern:
each behavior emits one `mood.*` event through EventRouter, so TipsEngine,
stats, and PersonaEngine consume them unchanged.

| Behavior | Trigger | Event | Surfacing |
|---|---|---|---|
| Morning greeting | first `session.start` on a new local date, after 06:00 | `mood.greeting` | pool |
| Long-session nudge | continuous session > 2.5 h AND energy < −0.3 | `mood.long_session` | pool |
| Missed-you | `session.start` after > 24 h absence | `mood.missed_you` | pool; on-demand LLM if > 72 h |
| Stage-up | `bondLevelChanged` (new level) | `mood.stage_up` | on-demand LLM |

Rate limiting: per-type cooldowns (greeting 20 h, nudge 3 h, missed-you 20 h)
plus a global cap of 1 proactive bubble/hour. Cooldown timestamps persist.

## 4. Surfacing

- `mood.*` event names are PersonaPool keys; refill generates per-tier,
  per-stage variants under `mood.<event>@L<n>`.
- On-demand milestones go through the existing `resolveOnDemand` path; prompts
  include current tier + stage so the LLM line matches the moment.
- `TipsCatalog` canned fallback covers persona-off and pool-miss cases
  (existing machinery, new `mood.*` entries in tips JSON, en + zh_CN).

## 5. Animation hook

MainWindow consults `MoodEngine::tier()` when selecting idle variants from
pack metadata. Packs without tier-mapped motions fall back to the default
idle — mood remains audible in bubbles even when invisible in animation.
No new pack-authoring requirement in this spec.

## 6. Peek UI (ambient + peek)

- Tray menu: one non-clickable line, e.g. "Seelie feels tired · Companion",
  refreshed on `moodTierChanged` / `bondLevelChanged`.
- Pet hover tooltip: same text. Localized via `tr()`, en + zh_CN.
- No meters, numbers, or progress bars.

## 7. Persistence

`StatisticsManager::registerComponent("mood", load, save)` — same pattern as
persona. JSON blob: `valence`, `energy`, `lonely` flag, `lastSeenEpoch`,
`lastGreetingDate`, per-type cooldown timestamps.

- Saved on exit and every 60 s (mirrors the existing `startAutoSave` cadence).
- On launch: elapsed = now − `lastSeenEpoch`; apply absence effects
  (valence/energy → 0, affection decay already lives in MemoryManager,
  set Lonely when > 24 h).
- Corrupt/missing JSON → neutral defaults (0, 0, not Lonely), warn in log,
  never block startup.

## 8. Error handling

- Persona disabled or pool miss on a `mood.*` event → TipsCatalog fallback.
- On-demand LLM failure on a milestone → pool line; milestone still marked
  delivered (no retry storms); stats counter bumped.
- `mood.stage_up` with no LLM profile configured → pool variant directly.

## 9. Testing

- `test_mood_engine`: delta application, clamp, decay math, time-of-day
  baseline, tier quantization + hysteresis, Lonely set/clear, absence effects
  from synthetic timestamps.
- `test_mood_events`: synthetic `mood.*` events route through EventRouter;
  per-type cooldowns and the global 1/hour cap honored.
- Pins: corrupt-JSON recovery; stage-band mapping L0..L5 → three stages;
  milestone one-shot guard on repeated `bondLevelChanged`.
- Manual smoke: configure a `qwen-plus` LLM profile (base URL + token from the
  user's API note) and verify `mood.stage_up` / long-absence `mood.missed_you`
  produce real LLM lines.

## Out of scope

- Visible mood/affinity meters or a Tamagotchi-style stats panel.
- Authoring mood-specific pack motions (packs may add them later; spec only
  consumes them when present).
- TTS-specific treatment of mood lines (existing TTS path plays them as-is).
- Negative-bond "angry" stage; bond never decreases.
- Multi-pet interplay.

## Verification (post-implementation)

- `ctest` full suite green (Qt Test, port 52848).
- Manual: pet for a while → tray line updates; `seelie-gateway --source
  claude-code --event session.start` after editing `lastSeenEpoch` →
  missed-you bubble; bond level-up → on-demand LLM line via qwen-plus profile.
