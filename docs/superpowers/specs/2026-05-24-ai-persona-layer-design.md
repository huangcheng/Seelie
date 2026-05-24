# AI Persona Layer — Design Spec

**Date:** 2026-05-24
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

Three new units sit beside existing ones. `EventRouter` is the only caller of `PersonaEngine`. Existing subsystems (`MemoryManager`, `TipsCatalog`, `TTSEngine`, `ConfigManager`) are untouched in their public API.

```
┌────────────┐    ┌──────────────┐
│ EventRouter│───▶│ PersonaEngine│──┐
└────────────┘    └──────────────┘  │
       │                            ▼
       │          ┌───────────────────────────────┐
       │          │ PersonaPool (high-freq tier)  │  ◀── SQLite (existing memory DB)
       │          │ LlmProvider (low-freq tier)   │
       │          └───────────────────────────────┘
       ▼                            │
   TipsCatalog ◀──── fallback ──────┘
       │
       ▼
  TipBubble + TTSEngine
```

- **`PersonaEngine`** — orchestrator. Tier-routes events, owns provider selection, owns the fallback chain. Single instance owned by `MainWindow`.
- **`PersonaPool`** — per-`(pack_id, event_name)` cache of pre-generated text. Backed by a new table in the existing memory SQLite DB. Lazy refill.
- **`LlmProvider`** — abstract interface mirroring the shape of `TtsProvider`. Three concrete protocols (OpenAI Chat, OpenAI Responses, Anthropic Messages) under one class with a `Protocol` enum.

If `PersonaEngine` is yanked out, the pet works exactly like today — the feature is additive.

---

## 1. Tier Policy

The 17 canonical events plus milestones are split into two tiers. The mapping is hardcoded in v1.

| Tier | Events |
|------|--------|
| **Pool (high-frequency)** | `tool.before`, `tool.after`, `tool.failed`, `file.edited`, `file.watched`, `prompt.submitted`, `todo.updated`, `notification.sent`, `permission.response` |
| **On-demand (rare)** | `session.start`, `session.end`, `session.idle`, `session.error`, `permission.requested`, `permission.denied`, `subagent.started`, `subagent.stopped`, all `milestone.*` |

**Why this split:**
- Pool-tier events fire dozens of times per minute during active work. Pre-generated variety is acceptable — the user won't notice that the line came from a batch generated yesterday.
- On-demand events fire ≤10×/session. Worth a fresh LLM call so the line can reference live memory state ("first session since you hit `gaming_mode`").

---

## 2. Data Flow

Per event:

1. `EventRouter::onCanonicalEvent(name, payload)` fires (existing path).
2. `EventRouter` calls `PersonaEngine::resolve(name, memorySnapshot)`.
3. `PersonaEngine` checks AI enabled + profile configured. If not → return `TipsCatalog::eventTip(name)` immediately.
4. Otherwise dispatch by tier:
   - **Pool tier:** `PersonaPool::pick(pack_id, event)` returns a random entry. If pool size < `MIN_POOL_SIZE` (5), schedule a non-blocking background refill via `LlmProvider::generateBatch(...)`.
   - **On-demand tier:** Build prompt with `{persona, last 5 event names, memory snapshot if opted-in}`. Call `LlmProvider::generate(prompt)` with 5s timeout.
5. On success → return text. EventRouter pipes through existing `TipBubble` + `TTSEngine` (which caches audio by text hash, same as today).
6. On any failure → fall back to `TipsCatalog`.

**Invariant:** `PersonaEngine::resolve()` always returns a non-empty `Tip`. The `TipsCatalog` fallback is what makes wiring into the hot path safe.

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
- **Partial results:** if the model returns fewer than requested (e.g. JSON parse partial), accept what came back and mark `(pack, event)` as eligible for retry on next pick.
- **Concurrency:** a `QSet<QString>` of in-flight `(pack, event)` keys prevents duplicate refills.
- **Lazy:** no warm-up at app start. First event of a pool type for a pack falls back to `TipsCatalog`; pool is warm ~2s later.

### Invalidation

- Pack persona hash mismatch → wipe matching rows, schedule refill.
- "Regenerate persona pool" button on AI tab → wipe active pack's rows.
- No time-based expiration in v1.

### Entries are not consumed

`pick()` is random-with-replacement. Pool stays at target size forever unless persona changes or the user clicks Regenerate.

---

## 4. LlmProvider — Three Protocols, User Profiles

### Protocols

`LlmProvider` is a single class with a `Protocol` enum:

| Protocol | Endpoint | Covers |
|---|---|---|
| `OpenAIChat` | `POST {baseUrl}/chat/completions` | OpenAI, OpenRouter, Groq, Together, DeepSeek, Mistral, llama.cpp / LM Studio / Ollama |
| `OpenAIResponses` | `POST {baseUrl}/responses` | OpenAI's Responses API + compatible proxies |
| `AnthropicMessages` | `POST {baseUrl}/messages` (+ `anthropic-version` header) | Anthropic + Anthropic-compatible proxies |

### LlmProfile

```cpp
struct LlmProfile {
    QString name;                          // user-set, e.g. "fast", "smart"
    Protocol protocol;
    QString baseUrl;                       // e.g. https://api.openai.com/v1
    QString apiKey;                        // password input in UI
    QString model;                         // free text, e.g. "gpt-4o-mini"
    QHash<QString, QString> extraHeaders;  // for niche proxies
    QString systemPrefix;                  // optional, prepended to persona system prompt
};
```

### Feature → profile mapping

A separate map decouples *which features use AI* from *which provider*:

```json
"llm": {
  "profiles": [...],
  "featureAssignments": {
    "persona": "fast"
    // future: "narrator": "smart", "conversational": "smart"
  }
}
```

This lets users route a cheap/fast model to pool gen and a smarter model to interactive features as they ship.

### API

```cpp
class LlmProvider {
public:
    // On-demand single completion. Calls callback exactly once.
    void generate(const QString &system,
                  const QString &user,
                  std::function<void(LlmResult)> callback);

    // Batched completion for pool refill — requests N lines, JSON output.
    void generateBatch(const QString &system,
                       const QString &user,
                       int n,
                       std::function<void(QVector<QString>)> callback);
};

struct LlmResult {
    bool ok = false;
    QString text;
    QString error;          // empty when ok
    int tokensIn = 0;       // best-effort, from provider response if present
    int tokensOut = 0;
};
```

`generateBatch` is implemented as one `generate` call requesting a JSON array, with strict JSON instructions in the user message. No `n` parameter on the wire — just a prompt convention.

### Timeouts and errors

- 5s timeout default.
- 3 consecutive failures → suppress further calls for 60s. AI tab displays the last error string.
- Provider misconfigured (missing URL or key) → `generate` immediately invokes callback with `ok=false`, no network call.

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

```
┌─ Settings ─────────────────────────────────────┐
│ [General] [Profile] [Packs] [AI] [About]       │
├────────────────────────────────────────────────┤
│ Profiles                                  [ + ]│
│ ┌────────────────────────────────────────┐    │
│ │ ◉ fast    OpenAI Chat   gpt-4o-mini    │    │
│ │ ◯ smart   Anthropic     claude-haiku   │    │
│ │ ◯ local   OpenAI Chat   llama-3-8b     │    │
│ └────────────────────────────────────────┘    │
│   [ Edit ]  [ Delete ]  [ Test connection ]    │
│                                                │
│ ── Feature assignments ────────────────────    │
│   Persona commentary  [fast    ▾]   [ ☑ on ]   │
│   (future features show up here)               │
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
- Name (required, unique)
- Protocol (dropdown: OpenAI Chat / OpenAI Responses / Anthropic Messages)
- Base URL (required)
- API key (password field, show-on-hover toggle)
- Model (free text — not a dropdown, since users will type whatever they want)
- Extra headers (expandable key/value list, optional)
- System prefix (optional multiline)

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

## 8. Statistics Dialog

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

## 9. Cost Sanity Check

| Path | Tokens | Cost (gpt-4o-mini @ $0.15/1M in, $0.6/1M out) |
|------|--------|----------------------------------------------|
| Pool warm-up, one pack | ~17 pool-tier events × ~1KB tokens / batch ≈ 17K tokens one-time | < $0.02 per pack |
| On-demand event | ~200 tokens × ~10 calls/session | ~$0.001 / session |
| Steady state | All pool-tier events cached → zero LLM cost during tool-call bursts | $0 |

Effectively free at any individual-user scale. Multi-pack households (user switches packs often) cost ~$0.02 per pack ever installed.

---

## 10. Testing Strategy

| Layer | What | How |
|---|---|---|
| Unit | Tier classification | Table-driven test: each canonical event → expected tier |
| Unit | Fallback chain | Mock `LlmProvider` that fails / times out / returns garbage → assert `TipsCatalog` value returned |
| Unit | Pool refill semantics | Inject mock provider; trigger refill; assert no duplicate in-flight, partial-result acceptance |
| Unit | Profile serialization | Round-trip `LlmProfile` through ConfigManager JSON |
| Unit | `stats()` accessors | Push counter increments through MemoryManager, assert struct values match |
| Unit | Persona hash invalidation | Change pack persona → assert old rows wiped on next pool access |
| Integration | `PersonaEngine` end-to-end | Mock provider returns canned lines; fire event; assert `TipBubble` shows them |
| Integration | Manifest persona loading | Pack with persona vs without → correct prompt assembly |
| Integration | Stats dialog renders | Seeded DB → assert labels show expected strings |
| Manual | Test connection button | Real keys in dev, hit each protocol, confirm latency report |
| Manual | Statistics dialog auto-refresh | Open during a real coding session, watch counters tick |

---

## 11. Out of Scope (Final Cut for v1)

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
