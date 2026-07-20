## Context

The current `TipsEngine` drives all Qlippy speech through pattern matching on IPC events. There is no mechanism for spontaneous, idle-time speech. `MainWindow` owns an `m_idleTimer` (via `SpriteAnimationEngine`) that fires after a period of inactivity, but it only triggers animations. The `TipBubbleWidget` already provides the UI for showing speech bubbles with title, message, and auto-dismiss.

## Goals / Non-Goals

**Goals:**
- Introduce spontaneous idle-time sayings that make Qlippy feel alive
- Keep saying frequency user-controllable
- Reuse existing `TipBubbleWidget` for presentation

**Non-Goals:**
- Text-to-speech or audio
- Context-aware sayings based on active application or window title
- User-editable saying pools (future enhancement)

## Decisions

### Decision 1: `RandomSayingsEngine` as a standalone class
**Rationale:** Separating random sayings from `TipsEngine` keeps the event-driven tip logic clean. `RandomSayingsEngine` only cares about idle state and time, not IPC events.

Responsibilities:
- Store categorized saying pools (`humor`, `encouragement`, `observation`, `coding_wisdom`)
- Pick a random saying from a weighted category
- Enforce cooldown between sayings
- Expose `maybeSaySomething()` that returns an optional `(title, body, animation)` tuple

### Decision 2: Trigger via `SpriteAnimationEngine::startIdleAnimation()`
**Rationale:** The idle timer already fires when the pet is "bored." Showing a saying at the same moment as an idle animation creates a cohesive "pet does something when idle" experience. `MainWindow` will call `RandomSayingsEngine::maybeSaySomething()` whenever `startIdleAnimation()` fires.

### Decision 3: Hard-coded saying pool in C++
**Rationale:** For the first version, a compile-time `QVector<Saying>` is simpler than external JSON/YAML and avoids file-I/O complexity. Sayings are translatable via `tr()`.

Saying structure:
```cpp
struct Saying {
    QString category;      // "humor", "encouragement", etc.
    QString title;         // e.g., tr("Hey!")
    QString body;          // e.g., tr("It looks like you're coding. Need a bug?")
    QString animation;     // optional animation name, e.g., "wave"
    int weight = 1;        // selection weight within category
};
```

### Decision 4: Frequency enum mapped to probability
**Rationale:** Users think in qualitative terms, not percentages.

- `Never` → 0%
- `Rarely` → 10%
- `Sometimes` → 25% (default)
- `Often` → 50%

The probability is checked each time the idle timer fires.

### Decision 5: 60-second saying cooldown
**Rationale:** Prevents spam even at "Often" frequency. Independent of the idle animation timer.

## Risks / Trade-offs

- **[Risk]** Random sayings might interrupt user focus during deep work.  
  → **Mitigation:** Default frequency is "Sometimes" (25%). User can set to "Never". Cooldown prevents rapid repeats.

- **[Risk]** Hard-coded sayings become stale or feel repetitive.  
  → **Mitigation:** Start with ~20 sayings across 4 categories. Future change can externalize to JSON.

## Migration Plan

No migration needed. New config keys default to sensible values (`sometimes` frequency).
