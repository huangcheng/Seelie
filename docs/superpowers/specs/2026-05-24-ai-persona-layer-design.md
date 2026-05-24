# AI Persona Layer — Design Spec

**Date:** 2026-05-24 (revised v2 after audit)
**Status:** draft (awaiting user review)
**Scope:** v1 — event-driven LLM commentary, two-tier cache, user-managed providers, new AI settings tab, statistics dialog

## Goal

Replace the static `TipsCatalog` as the primary text source for canonical events with LLM-generated, in-character lines that respect the pet's pack persona and survive the high-frequency event hot path without exploding TTS cost. Static catalog remains as the offline fallback.

## Non-goals (v1)

- Conversational chat (user-initiated dialogue with the pet).
- Session Narrator (ambient LLM "thoughts" on a timer).
- Voice stubs / visual-novel hybrid (cached vocal reactions + fresh text).
- User-defined personas in Settings (pack-provided personas only).
- Per-event tier overrides (tier table is hardcoded for v1).
- Streaming responses (single completion only).
- OS keychain for API keys (plaintext in config.json, labeled).
- Multi-language pool variants per pack (one pool per `(pack, event)`; persona's `language` field instructs the model directly).

---

## Architecture

Three new units sit beside existing ones. Public APIs of existing subsystems (`MemoryManager`, `TipsCatalog`, `TTSEngine`, `ConfigManager`) are not modified; the integration is additive (new signal connections, new `stats.*` keys written via the existing `MemoryManager::increment()`).

**Thread model:** All three new units (`PersonaEngine`, `PersonaPool`, `LLMProvider`) live on the **main thread**. `QNetworkAccessManager` handles async HTTP via the main event loop — no worker thread needed. This deliberately *does not* follow the `TTSEngine` `moveToThread()` pattern (TTS uses a worker because audio decoding is CPU-heavy; LLM is pure I/O). Keeping everything on main thread eliminates cross-thread SQLite access entirely.

```
┌────────────┐    ┌──────────────┐
│ EventRouter│───▶│ PersonaEngine│──┐
└────────────┘    └──────────────┘  │
       │                            ▼
       │          ┌───────────────────────────────┐
       │          │ PersonaPool (high-freq tier)  │  ◀── SQLite (existing memory DB)
       │          │ LLMProvider (low-freq tier)   │
       │          └───────────────────────────────┘
       ▼                            │
   TipsCatalog ◀──── fallback ──────┘
       │
       ▼
  TipBubble + TTSEngine
```

- **`PersonaEngine`** — orchestrator. Tier-routes events, owns provider selection, owns the fallback chain. Single instance owned by `MainWindow`.
- **`PersonaPool`** — per-`(pack_id, event_name)` cache of pre-generated text. Backed by a new table in the existing memory SQLite DB. Lazy refill.
- **`LLMProvider`** — abstract interface mirroring the shape of `TtsProvider`. Three concrete protocols (OpenAI Chat, OpenAI Responses, Anthropic Messages) under one class with a `Protocol` enum.

If `PersonaEngine` is yanked out, the pet works exactly like today — the feature is additive.

---

## 1. Tier Policy

Events are split into two tiers. The mapping is hardcoded in v1.

| Tier | Source | Events | Count |
|------|--------|--------|-------|
| **Pool (high-frequency)** | `EventRouter::routeEvent` (IPC) | `tool.before`, `tool.after`, `tool.failed`, `file.edited`, `file.watched`, `prompt.submitted`, `todo.updated`, `notification.sent`, `permission.response` | 9 |
| **On-demand (rare, IPC)** | `EventRouter::routeEvent` (IPC) | `session.start`, `session.end`, `session.idle`, `session.error`, `permission.requested`, `permission.denied`, `subagent.started`, `subagent.stopped` | 8 |
| **On-demand (milestones)** | `MemoryManager::milestoneReached(title, body)` signal | All milestones (`first_tip`, `gaming_mode`, `pack_install`, …) | open-ended |

**Why this split:**
- Pool-tier events fire dozens of times per minute during active work. Pre-generated variety is acceptable — the user won't notice that the line came from a batch generated yesterday.
- On-demand events fire ≤10×/session. Worth a fresh LLM call so the line can reference live memory state ("first session since you hit `gaming_mode`").

**Milestone routing.** Milestones are *not* IPC events flowing through `EventRouter`. They originate from `MemoryManager::milestoneReached(title, body)`, which is already connected in `main.cpp` (commit `4f7d82c`). PersonaEngine subscribes to the same signal in parallel; the existing connection is left intact. PersonaEngine treats the milestone key as the event name for prompt assembly and pool routing (always on-demand tier).

---

## 2. Data Flow

`PersonaEngine::resolve()` is **synchronous and always returns immediately** — never blocks on network. On-demand events return a `TipsCatalog` fallback first, then upgrade in place when the LLM call completes.

### API

```cpp
class PersonaEngine : public QObject {
    Q_OBJECT
public:
    // Returns immediately with a non-empty Tip. For on-demand events whose LLM
    // call is still in flight, this is the TipsCatalog fallback; the real line
    // arrives later via tipUpgraded(). The returnedTip carries a requestId so
    // the caller can correlate an upgrade with its original bubble.
    struct Resolved {
        QString text;
        quint64 requestId;  // 0 if no upgrade will arrive
    };
    Resolved resolve(const QString &eventName, const QJsonObject &payload);

signals:
    // Emitted on the main thread when an on-demand LLM call completes.
    // Bubble owner swaps text if it still owns the bubble for requestId.
    void tipUpgraded(quint64 requestId, const QString &newText);
};
```

### Per-event flow

1. `EventRouter::routeEvent(QJsonObject)` fires (existing path). It calls `PersonaEngine::resolve(name, payload)`.
2. If AI is disabled or no profile configured → return `{TipsCatalog::eventTip(name), requestId=0}` immediately. No upgrade will follow.
3. Otherwise dispatch by tier:
   - **Pool tier:**
     - `PersonaPool::pick(packId, event, personaHash)` returns a random entry → `{text, requestId=0}`. If pool size < `MIN_POOL_SIZE` (5), schedule a non-blocking background refill. No upgrade signal needed.
     - If pool is empty (cold cache or all entries stale) → return `{TipsCatalog text, requestId=0}` and schedule the same refill.
   - **On-demand tier (IPC or milestone):**
     - Allocate `requestId = ++m_nextRequestId`.
     - Build prompt `{persona, last 5 event names, memory snapshot if opted-in}`.
     - Kick off `LLMProvider::generate(prompt, callback)` (callback captured with `requestId`).
     - Return `{TipsCatalog::eventTip(name), requestId}` immediately.
     - When the callback fires later on the main thread, emit `tipUpgraded(requestId, newText)`.
4. The bubble owner (`MainWindow`) stores `requestId` with the active bubble for that event. On `tipUpgraded`, if the stored id matches and the bubble is still visible, swap the text (and re-trigger TTS for the new text). Otherwise drop silently.

### Async data flow diagram

```
 main thread
 ───────────
 routeEvent(name) ──▶ PersonaEngine::resolve()
                              │
                              ├─▶ pool-tier: PersonaPool::pick() ──▶ text  (sync)
                              │
                              └─▶ on-demand: TipsCatalog text   ◀── return
                                   │                    requestId=42
                                   ▼
                          LLMProvider::generate(cb)
                                   │
                          (QNAM async, main event loop)
                                   │
                                   ▼   ~1-2s later
                          callback fires on main thread
                                   │
                                   ▼
                          PersonaEngine::tipUpgraded(42, newText)
                                   │
                                   ▼
                          MainWindow updates bubble text
                                   │
                                   ▼
                          TTSEngine plays new audio (cache miss on first hear)
```

**Invariant:** `resolve()` always returns a non-empty text and never blocks on I/O. The `TipsCatalog` fallback is what makes wiring into the hot path safe.

---

## 3. PersonaPool

### Storage

New table in the existing memory SQLite DB:

```sql
CREATE TABLE IF NOT EXISTS persona_pool (
    pack_id      TEXT NOT NULL,
    event        TEXT NOT NULL,
    text         TEXT NOT NULL,
    persona_hash TEXT NOT NULL,
    created_at   INTEGER NOT NULL,
    PRIMARY KEY (pack_id, event, text)
);
CREATE INDEX IF NOT EXISTS idx_persona_pool_lookup
    ON persona_pool (pack_id, event, persona_hash);
```

`persona_hash` is a SHA-256 of the pack's effective persona JSON (system + language + style_examples — all three fields, in a canonical order). When the pack updates any persona field, the hash changes and stale rows are wiped on next pool access.

### Sizing & refill

- **Target size:** `TARGET_POOL_SIZE` = 20 entries per `(pack_id, event)`.
- **Refill threshold:** `MIN_POOL_SIZE` = 5. Below this, refill is scheduled when pick is called.
- **Refill request:** one batched LLM call asking for `TARGET_POOL_SIZE - current_size` lines in a JSON array. Prompt instructs strict JSON output.
- **Lazy:** no warm-up at app start. First event of a pool type for a pack falls back to `TipsCatalog`; pool is warm ~2s later.

### Refill robustness

- **Partial results:** if the model returns fewer than requested, accept what came back and mark `(pack, event)` as eligible for retry on next pick.
- **Per-entry validation:** discard entries that are (a) empty/whitespace, (b) longer than `MAX_TIP_CHARS` (200) — truncated and kept with `qWarning`, (c) not strings.
- **JSON parse failure:** if `QJsonDocument::fromJson` returns invalid, log via `qWarning` with the first 200 chars of the response, drop the whole batch, increment a `consecutiveEmptyRefills` counter for this `(pack, event)` key.
- **Spam guard:** if `consecutiveEmptyRefills >= 3` for a key, suppress further refills for that key until the user clicks "Regenerate persona pool" or restarts. The Stats dialog `Last LLM error` line surfaces the cause.
- **In-flight tracking:** a `QHash<QString, qint64>` maps `(pack, event)` key to the start timestamp of the in-flight refill. New refill requests for a key with a live entry are silently no-op'd. **Cleanup:** any key whose timestamp is older than `INFLIGHT_TIMEOUT_MS` (30000) is purged on next access — this catches stuck callbacks (provider crash, never-fires) without leaking.

### Invalidation

- **Cached active-pack hash:** PersonaEngine stores the active pack's persona hash in memory, refreshed on `CharacterPackManager::activePackChanged(pack)`. Pool access uses the cached hash — no per-event SHA-256 recompute. (This signal is verified to exist on `CharacterPackManager`.)
- **Stale-row wipe:** when the cached hash differs from the rows in `persona_pool`, matching rows are deleted before the next refill. Single SQL `DELETE` keyed by `(pack_id, event, persona_hash != ?)`.
- **"Regenerate persona pool" button** on AI tab → wipe active pack's rows for all events.
- No time-based expiration in v1.

### Entries are not consumed

`pick()` is random-with-replacement. Pool stays at target size forever unless persona changes or the user clicks Regenerate.

---

## 4. LLMProvider — Three Protocols, User Profiles

### Protocols

`LLMProvider` is a single class with a `Protocol` enum:

| Protocol | Endpoint | Covers |
|---|---|---|
| `OpenAIChat` | `POST {baseUrl}/chat/completions` | OpenAI, OpenRouter, Groq, Together, DeepSeek, Mistral, llama.cpp / LM Studio / Ollama |
| `OpenAIResponses` | `POST {baseUrl}/responses` | OpenAI's Responses API + compatible proxies |
| `AnthropicMessages` | `POST {baseUrl}/messages` (+ `anthropic-version` header) | Anthropic + Anthropic-compatible proxies |

### LLMProfile

```cpp
struct LLMProfile {
    QString name;       // user-set, e.g. "fast", "smart"
    Protocol protocol;
    QString baseUrl;    // e.g. https://api.openai.com/v1
    QString apiKey;     // password input in UI
    QString model;      // free text, e.g. "gpt-4o-mini"
};
```

`extraHeaders` and `systemPrefix` are deferred to v1.1.

### Per-feature profile selection

For v1 there is one LLM feature (`persona`), so a single string is enough:

```json
"llm": {
  "profiles": [...],
  "personaProfile": "fast"
}
```

When the next LLM feature ships (Narrator, Conversational), this widens to a small map (`featureAssignments`). Migration is trivial — one config rename — and not worth carrying the map shape today.

### API (async only — no synchronous variants)

```cpp
class LLMProvider {
public:
    // On-demand single completion. Callback fires exactly once, on the main thread.
    void generate(const QString &system,
                  const QString &user,
                  std::function<void(LLMResult)> callback);

    // Batched completion for pool refill — requests N lines, JSON array output.
    void generateBatch(const QString &system,
                       const QString &user,
                       int n,
                       std::function<void(QVector<QString>)> callback);
};

struct LLMResult {
    bool ok = false;
    QString text;
    QString error;          // empty when ok; populated for HTTP/JSON/timeout failures
    int tokensIn = 0;       // best-effort, parsed from provider response if present
    int tokensOut = 0;
};
```

`generateBatch` issues one HTTP request asking for a JSON array of N strings. The callback receives the parsed array (possibly empty on parse failure — caller handles).

### Threading

`LLMProvider` lives on the main thread. `QNetworkAccessManager` is created on the main thread; its `finished` signal fires on the main thread; the std::function callback is invoked from that slot. There are no worker threads, no `moveToThread`, and no cross-thread DB writes anywhere in the call chain.

### Timeouts and errors

- 5s timeout default (per request, enforced via `QNetworkReply::abort()` from a `QTimer::singleShot`).
- 3 consecutive failures → suppress further calls for 60s. AI tab displays the last error string. (This is a global per-feature suppressor, separate from the per-key pool spam guard in §3.)
- Provider misconfigured (missing URL or key) → `generate` invokes callback synchronously with `ok=false`, no network call.

---

## 5. Pack Manifest Extension

Pack `manifest.json` gains an **optional** `persona` field:

```json
{
  "id": "al_akagi",
  "name": "Akagi",
  "persona": {
    "system": "You are Akagi from Azur Lane. Prideful, possessive of senpai, drops occasional Japanese. Reply with one short sentence.",
    "language": "zh-CN",
    "style_examples": ["Tch, again?", "Senpai... let me handle it."]
  }
}
```

All three sub-fields are optional. `style_examples` are inserted as few-shot examples in the prompt. Missing `persona` → engine-default kicks in.

### Engine-default persona

Used when a pack ships no `persona`:

> *You are a desktop pet companion to a software developer. Reply with one short sentence in the user's language. Be a little sassy but not mean.*

Also used for unknown event names (forward compat).

---

## 6. Settings — New "AI" Tab

Added to `SettingsPanelWidget` alongside existing tabs. Tab label: **AI**.

**Heads-up on internal naming:** `SettingsPanelWidget` already has internal member `m_aiTabBtn` (which holds the user-visible `tr("TTS")` label — naming drift from when TTS lived under "AI"). To avoid C++ identifier collision with the new LLM tab, rename `m_aiTabBtn` → `m_ttsTabBtn` and `m_aiTab` → `m_ttsTab` (rename internally only; user-facing label stays "TTS"). Then add new `m_llmTabBtn` / `m_llmTab` with user label "AI". This is a mechanical refactor — ~12 references in `SettingsPanelWidget.cpp` plus the header.

```
┌─ Settings ─────────────────────────────────────┐
│ [General] [Profile] [Packs] [TTS] [AI] [About] │
├────────────────────────────────────────────────┤
│ Profiles                                  [ + ]│
│ ┌────────────────────────────────────────┐    │
│ │ ◉ fast    OpenAI Chat   gpt-4o-mini    │    │
│ │ ◯ smart   Anthropic     claude-haiku   │    │
│ │ ◯ local   OpenAI Chat   llama-3-8b     │    │
│ └────────────────────────────────────────┘    │
│   [ Edit ]  [ Delete ]  [ Test connection ]    │
│                                                │
│ ── Persona ────────────────────────────────    │
│   Profile:            [fast    ▾]   [ ☑ on ]   │
│                                                │
│ ── Privacy ────────────────────────────────    │
│   [ ☐ ] Share memory with AI (name, milestones)│
│                                                │
│ ── Tools ──────────────────────────────────    │
│   [ Regenerate persona pool for active pack ]  │
│                                                │
│ ── Status ─────────────────────────────────    │
│   Last error: —                                │
└────────────────────────────────────────────────┘
```

### Edit-profile dialog

Fields:
- Name (required, unique among profiles)
- Protocol (dropdown: OpenAI Chat / OpenAI Responses / Anthropic Messages)
- Base URL (required)
- API key (password field, show-on-hover toggle)
- Model (free text — not a dropdown, since users will type whatever they want)

### Test connection button

Sends a minimal request to the profile's endpoint requesting a single token. Reports `✓ 240ms` or `✗ <status code> <error>`. Catches wrong URL, bad key, typo'd model name without waiting for an event.

### Storage

Profiles + assignments + privacy toggle live in `ConfigManager` (`config.json`). API keys stored as plaintext with a label "stored locally on this machine" next to the field — same posture as `~/.aws/credentials`, `~/.codex/auth.json`. OS keychain integration is deferred (out of scope).

---

## 7. Privacy — What Crosses the Wire

| Sent to LLM provider | Not sent |
|---|---|
| Persona system prompt (pack-provided or default) | Source code, file content, diffs |
| Event name (e.g. `tool.before`) | Tool arguments, file paths |
| Last 5 event names in the rolling window (no payloads) | `notification.sent` body |
| Memory counters (name, displayName, milestones reached, sessions counted) — **only with "Share memory with AI" enabled** | Memory by default |

The "Share memory with AI" toggle is **off by default**. Pet works fine with it off — persona lines just won't reference the user by name.

---

## 8. Integration & Threading

### Object ownership

```
main.cpp
  ├─ ConfigManager           (existing)
  ├─ MemoryManager           (existing, owns SQLite)
  ├─ CharacterPackManager    (existing)
  └─ MainWindow              (existing)
        ├─ EventRouter       (existing)
        ├─ IpcServer         (existing)
        ├─ TTSEngine         (existing, worker thread)
        ├─ TipBubble         (existing)
        ├─ PersonaEngine     ★ new, main thread
        │     ├─ PersonaPool (owned, main thread, uses MemoryManager's DB connection)
        │     └─ LLMProvider (owned, main thread)
        └─ StatisticsDialog  ★ new, lazy-created on tray click
```

`MainWindow` is the natural owner: it already owns `EventRouter`, `TTSEngine`, and `TipBubble`, which is the entire downstream path of a persona line. Constructor injection — `PersonaEngine(MemoryManager*, CharacterPackManager*, ConfigManager*, parent)` — same pattern as existing engines.

### Signal connections (made in `MainWindow`)

```cpp
// IPC events → persona resolution (read-side wired in EventRouter::routeEvent)
connect(eventRouter, &EventRouter::canonicalEventRouted,
        personaEngine, &PersonaEngine::onCanonicalEvent);

// Milestone events come directly from MemoryManager, not EventRouter
connect(memoryManager, &MemoryManager::milestoneReached,
        personaEngine, &PersonaEngine::onMilestone);

// Active pack changes → refresh cached persona hash
connect(packManager, &CharacterPackManager::activePackChanged,
        personaEngine, &PersonaEngine::onActivePackChanged);

// Async upgrades for on-demand events → bubble re-renders
connect(personaEngine, &PersonaEngine::tipUpgraded,
        mainWindow, &MainWindow::onTipUpgraded);
```

`EventRouter` gains a new signal `canonicalEventRouted(QString name, QJsonObject payload)` emitted at the end of `routeEvent()`. PersonaEngine listens; existing logic is unchanged.

### Rolling event window

Owned by `PersonaEngine` as a `QQueue<QString>` of the last 5 event names (size capped). Updated in `onCanonicalEvent` and `onMilestone`. Used to assemble the on-demand prompt. Not persisted — in-memory only, resets on app restart. (Pool tier doesn't use the window — its lines were generated batch-style without per-event context.)

### Thread summary

| Component | Thread | DB access | Network |
|---|---|---|---|
| `PersonaEngine` | main | yes (via owned `PersonaPool`) | no |
| `PersonaPool` | main | yes | no |
| `LLMProvider` | main | no | yes (`QNetworkAccessManager` on main event loop) |
| `MemoryManager` | main | yes (owns connection) | no |
| `TTSEngine` (existing) | worker | no | no (HTTP providers create their own QNAM on worker thread) |

No cross-thread DB writes anywhere. The only cross-thread plumbing is the existing `UdpWorker → EventRouter` `Qt::QueuedConnection` hop, unchanged.

---

## 9. Statistics Dialog

New `StatisticsDialog`, modal, same window-chrome posture as the About box.

### Trigger

New entry in the tray context menu between **Settings** and **About**:

```
  Show / Hide pet
  Settings...
  Statistics...          ← new
  ─────────
  About
  Quit
```

### Layout

```
┌─ Statistics ──────────────────────────────────────┐
│                                                    │
│ TTS Cache                                          │
│   Entries cached:        247                       │
│   Disk usage:            18.4 MB                   │
│   Hit rate (lifetime):   94.2%                     │
│   Hit rate (session):    97.1%                     │
│   Last miss:             2 min ago — "tool.before" │
│                                                    │
│ AI Persona                                         │
│   Active pack:           Akagi (al_akagi)          │
│   Pool warmth:           9 / 9 event types         │
│   Pool entries:          162 lines total           │
│   Refills:               3 ok, 0 failed            │
│   On-demand calls:       17 ok, 0 failed           │
│   Tokens (estimate):     14.2K in / 3.1K out       │
│   Last LLM error:        —                         │
│                                                    │
│ Events                                             │
│   Total received:        18,432                    │
│   Most common:           tool.before  (7,201)      │
│   Sessions seen:         84                        │
│   Last event:            tool.after — 8s ago       │
│                                                    │
│ IPC                                                │
│   Endpoint:              127.0.0.1:52847           │
│   Packets received:      18,539                    │
│   Decode errors:         0                         │
│   Uptime:                03:24:12                  │
│                                                    │
│ Memory DB                                          │
│   Size on disk:          312 KB                    │
│   Milestones reached:    2 / 12                    │
│                                                    │
│ [ Refresh ]   [ Reset stats ]   [ Close ]          │
└────────────────────────────────────────────────────┘
```

### Stat sources

Each subsystem grows a small read-only accessor. The dialog owns no business logic.

| Source | Adds |
|---|---|
| `TTSEngine` | hit/miss counters; cache size walk (`QFileInfo` sum, on demand only) |
| `PersonaEngine` | pool warmth, refill counters, on-demand counters, token estimate, last error string |
| `EventRouter` | per-event-name counter (`QHash<QString,int>`) + total + last-event timestamp |
| `IpcServer` / `UdpWorker` | packets received, decode errors, server start time |
| `MemoryManager` | DB file size, milestone count |

### Persistence

Counters survive restart by piggybacking on `MemoryManager`'s existing key/value store. New keys (no schema change):

```
stats.tts.requests          → 2347
stats.tts.hits              → 2211
stats.persona.refills.ok    → 3
stats.persona.refills.fail  → 0
stats.persona.ondemand.ok   → 17
stats.persona.ondemand.fail → 0
stats.persona.tokens.in     → 14213
stats.persona.tokens.out    → 3104
stats.events.total          → 18432
stats.events.<event_name>   → ...    (one key per event seen)
stats.ipc.packets           → 18539
stats.ipc.decode_errors     → 0
```

Per-session counters (e.g. "hit rate since boot") are kept in memory only. Lifetime counters are persisted via `MemoryManager::increment()` — same call path milestone counters already use.

### Refresh

- **Manual** `Refresh` button.
- **Auto-refresh** every 2s while the dialog is visible (`QTimer` tied to dialog lifetime).
- `Reset stats` button clears only `stats.*` keys after a confirmation dialog. Does NOT touch milestones, name, or other memory data.

### Layout primitives

Plain `QFormLayout` blocks grouped by `QGroupBox` per section. Monospace `QLabel` for numbers so columns align. Fixed dialog size around 480×620.

---

## 10. Cost Sanity Check

| Path | Tokens | Cost (gpt-4o-mini @ $0.15/1M in, $0.6/1M out) |
|------|--------|----------------------------------------------|
| Pool warm-up, one pack | **9** pool-tier events × ~1.2K tokens / batch ≈ **~11K tokens** one-time | < **$0.01** per pack |
| On-demand event | ~200 tokens × ~10 calls/session | ~$0.001 / session |
| Steady state | All pool-tier events cached → zero LLM cost during tool-call bursts | $0 |

Effectively free at any individual-user scale. Multi-pack households (user switches packs often) cost ~$0.01 per pack ever installed. (Previous version of this spec said "17 events" — that was the total canonical event count, not pool-tier; corrected to 9.)

---

## 11. Testing Strategy

| Layer | What | How |
|---|---|---|
| Unit | Tier classification | Table-driven test: each canonical event → expected tier |
| Unit | Fallback chain | Mock `LLMProvider` that fails / times out / returns garbage → assert `TipsCatalog` value returned |
| Unit | Pool refill semantics | Inject mock provider; trigger refill; assert no duplicate in-flight, partial-result acceptance |
| Unit | Profile serialization | Round-trip `LLMProfile` through ConfigManager JSON |
| Unit | `stats()` accessors | Push counter increments through MemoryManager, assert struct values match |
| Unit | Persona hash invalidation | Change pack persona → assert old rows wiped on next pool access |
| Unit | Async upgrade flow | Fire on-demand event with mock provider that delays callback; assert immediate fallback, then `tipUpgraded` signal with new text |
| Unit | In-flight key cleanup | Mark key in-flight, advance clock past 30s, trigger access; assert key cleared and refill proceeds |
| Unit | Spam guard | Mock provider returns empty array 3× for one key; assert subsequent refills suppressed; assert manual regenerate clears the suppression |
| Unit | JSON parse edge cases | Empty array, non-string entries, oversized entries (>200 char → truncated), `null` entries, complete garbage (not JSON) |
| Unit | Per-entry validation | Whitespace-only entries dropped; oversized entries truncated with warning |
| Stress | Hot-path 1000 events | Fire 1000 rapid `tool.before` events through `PersonaEngine`; assert no crashes, no thread contention, no unbounded memory growth, no DB lock errors |
| Stress | Concurrent pool access | Fire events while refill callback is in flight (simulate via mock provider); assert no corruption, no duplicate in-flight, pool state coherent |
| Stress | Network failure cascade | Mock provider fails 10 consecutive times → assert 60s cooldown activates after 3, suppresses calls during cooldown, recovers on next request after cooldown |
| Integration | `PersonaEngine` end-to-end | Mock provider returns canned lines; fire event; assert `TipBubble` shows them |
| Integration | Manifest persona loading | Pack with persona vs without → correct prompt assembly |
| Integration | Stats dialog renders | Seeded DB → assert labels show expected strings |
| Integration | Active-pack switch | Switch packs at runtime; assert cached hash refreshes, stale rows wiped on next pool access |
| Integration | Milestone routing | Trigger `MemoryManager::milestoneReached`; assert PersonaEngine produces an on-demand line for it |
| Manual | Test connection button | Real keys in dev, hit each protocol, confirm latency report |
| Manual | Statistics dialog auto-refresh | Open during a real coding session, watch counters tick |
| Manual | Long-running session | Leave Seelie running overnight on a real provider; check Stats dialog next morning for cooldown/error patterns |

---

## 12. Risk Register

| Risk | Severity | Mitigation |
|------|----------|------------|
| Main thread freeze on 5s LLM call | Critical | Async on-demand path (§2) — `resolve()` never blocks; upgrade arrives via signal |
| Cross-thread SQLite crash | Critical | All new units pinned to main thread (§Architecture, §8) |
| JSON parse failure → empty pool → repeated LLM cost spiral | High | Per-key spam guard after 3 consecutive empty refills (§3) |
| Stale pool rows after pack manifest edit | High | Cached hash refreshed on `activePackChanged`; on-disk hash check on every pool access (§3) |
| In-flight key leak from never-firing callback | Medium | 30s auto-purge on next access (§3) |
| Cold-pool burst: many events show TipsCatalog for first ~2s after pack switch | Medium | Accepted as lazy-refill UX; documented; user can pre-warm via Settings button |
| Duplicate text across refill cycles | Low | `PRIMARY KEY (pack_id, event, text)` rejects exact duplicates; semantic dedup deferred |
| Test connection button consumes user's credits | Low | UI note: "Sends a 1-token request to your provider" |
| LLM provider logs prompts by default (privacy) | Low | "Share memory with AI" off by default; documentation advises users to review provider data-use settings |
| First on-demand event shows fallback then "jumps" to LLM text | Low | Accepted as UX — fallback is in-character static text, upgrade is a graceful improvement; bubble has `Qt::QueuedConnection` so the swap is smooth |

---

## 13. Out of Scope (Final Cut for v1)

- Conversational mode (future feature; will mount on the same AI tab).
- Session Narrator (future).
- Voice stubs / visual-novel hybrid (future).
- OS keychain for API keys (deferred).
- User-defined personas in Settings.
- Per-event tier overrides in Settings.
- Per-pack persona pool breakdown in the Statistics dialog (active pack only in v1).
- Graphs / sparklines / "events per minute" charts in Statistics.
- Export-stats-to-clipboard / JSON dump button.
- Streaming responses.
- Multi-language pool variants per pack.
- Pool warm-up at app start (lazy only).
- Time-based pool expiration.
- `extraHeaders` / `systemPrefix` on `LLMProfile` — deferred until users request niche-proxy or persona-override use cases.
- Multi-feature LLM `featureAssignments` map — v1 uses a single `personaProfile` string; widens to a map when the second LLM feature ships.
- Semantic deduplication of pool entries (exact-text PK only).
