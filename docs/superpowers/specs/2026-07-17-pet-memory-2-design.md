# Pet Memory 2.0 — Design Spec

**Date:** 2026-07-17
**Status:** draft
**Scope:** Relationship state, episodic memory, memory digest — evolves the shipped Pet Memory MVP (2026-05-23). First of four specs in the "Senses & Touch" program (memory → senses → touch → AI commentary).

## Goal

Make Seelie's memory feel real: she remembers how long she's known you, how much you interact with her, and notable moments you've shared — and can recite that back to the persona layer. Builds on the existing `MemoryManager` + `memory.db`; no new storage system.

## Architecture

Extend `MemoryManager` with two new capabilities alongside the existing KV facts:

1. **Relationship state** — numeric state stored as `rel.*` / `stats.*` keys in the existing `memory` table (fits the shipped pattern, no schema churn).
2. **Episodes** — a new time-ordered `episodes` table with a hard cap and rollup-on-overflow ("forgetting" that preserves aggregates).

Plus a `memoryDigest()` that renders a compact text summary for LLM prompts (consumed by the AI-commentary spec, gated behind the existing `shareMemoryWithAi` config flag).

---

## 1. Data Model

**File:** `~/.config/Seelie/memory.db` (existing SQLite DB, same connection).

### 1a. Relationship keys (existing `memory` table)

| Key | Type | Meaning |
|-----|------|---------|
| `rel.first_met_ts` | int (epoch ms) | Set once, first time MemoryManager v2 opens the DB |
| `rel.bond_xp` | int | Lifetime bond experience; level is derived, never stored |
| `rel.affection` | int 0–100 | Fast-moving meter; decays over time (lazy, computed on read) |
| `rel.affection_ts` | int (epoch ms) | Last affection write; basis for decay calculation |
| `rel.last_seen_day` | string (`yyyy-MM-dd`) | Last day a daily-login reward was granted |
| `stats.sessions` | int | Completed sessions (via existing `increment()`) |
| `stats.pets` | int | Pet interactions (written by TouchReactions spec; key reserved now) |
| `stats.pokes` | int | Click interactions (wired in this spec — `user.click` already exists) |
| `episodes.rolled.<kind>` | int | Per-kind rollup counters for forgotten episodes |

### 1b. Episodes table (new)

```sql
CREATE TABLE IF NOT EXISTS episodes (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    ts        INTEGER NOT NULL,       -- epoch ms
    kind      TEXT NOT NULL,          -- "session", "late_night", "error_streak", "interaction", "milestone"
    text      TEXT NOT NULL,          -- one line, pre-rendered in current UI language
    embedding BLOB                    -- nullable float32 vector; filled async when AI embeddings enabled
);
CREATE INDEX IF NOT EXISTS idx_episodes_ts ON episodes(ts);
```

**Cap + rollup:** hard cap of **2,000 rows**. On insert when full:
- **Embedding mode** (`hasEmbeddings()`): find the nearest pair by cosine similarity; if similarity ≥ 0.95 (near-duplicates), `increment("episodes.rolled." + kind)` for the older one and delete it; otherwise fall back to oldest-by-ts.
- **Plain mode** (no embeddings): delete oldest-by-ts with the same rollup counter.

DB stays well under 10MB even fully embedded (2,000 × ~6KB vectors + text); aggregates survive forgetting.

**Pre-rendered text:** episodes store final display text in the UI language active at write time (same precedent as `greeting.last_text`). The digest passes text through verbatim; the LLM handles either language.

### 1c. Migration

Constructor bumps `PRAGMA user_version` 0 → 1: creates `episodes` table + index, seeds `rel.first_met_ts` if absent. Existing installs upgrade in place; fresh installs get both tables.

---

## 2. MemoryManager API Additions

**File:** `src/MemoryManager.h` / `src/MemoryManager.cpp` (extend, do not replace)

```cpp
struct Episode {
    qint64  ts;
    QString kind;
    QString text;
};

class MemoryManager : public QObject {
    // ... existing API unchanged ...

    // Relationship
    qint64 firstMetTs() const;          // 0 if invalid
    int    daysMet() const;             // calendar days since firstMetTs
    int    bondXP() const;
    int    bondLevel() const;           // derived: thresholds at 0/50/150/400/1000/2500 XP → L0–L5
    void   addBondXP(int delta);        // no-op if delta <= 0 or invalid
    int    affection();                 // decay-adjusted value, 0–100
    void   addAffection(int delta);     // clamps 0–100, refreshes affection_ts

    // Episodes
    void recordEpisode(const QString &kind, const QString &text);
    QVector<Episode> recentEpisodes(int limit = 10) const;   // newest first

    // Semantic recall (see §2b; degrade to recentEpisodes() when !hasEmbeddings())
    bool hasEmbeddings() const;
    QVector<Episode> recallEpisodes(const QString &query, int limit = 5) const;        // "remember when..."
    QVector<Episode> similarEpisodes(const QString &contextText, int limit = 5) const; // digest relevance

    // Digest for LLM prompts (consumed by AI-commentary spec)
    // e.g. "Known Alex 12 days. Bond L2. Affection 63. Sessions: 18. Recent: ..."
    QString memoryDigest(int maxChars = 600) const;

signals:
    void bondLevelChanged(int newLevel);   // emitted by addBondXP on threshold crossing
};
```

### 2b. Semantic recall layer (local-first + adapter)

**Storage:** float32 embeddings in the `embedding` BLOB column. **No vector-DB dependency** — at ≤2,000 vectors, brute-force cosine is sub-millisecond and *exact* (zvec/Pinecone-class machinery was evaluated and rejected: index structures buy nothing at this scale and violate the lightweight constraint; see decision record below).

**Embedding compute (optional, AI-gated):** a small `EmbeddingService` QObject (worker QThread, same pattern as `TTSEngine`) calls the **embeddings endpoint of the active persona LLM profile** (e.g. OpenAI `/v1/embeddings`, `text-embedding-3-small`). Enabled only when persona AI is configured **and** `shareMemoryWithAi` is true. `recordEpisode()` enqueues the text; the row's `embedding` stays NULL until the job completes. Rows without embeddings are simply invisible to semantic recall but still appear in `recentEpisodes()`.

**Recall API (both in `MemoryManager`):**
- `recallEpisodes(query)` — embed the query (cached per query string for the app run), cosine-rank all embedded rows, return top-k. Powers a future "remember when…" interaction.
- `similarEpisodes(contextText)` — same, with a context string (current event + time bucket). `memoryDigest()` uses this instead of `recentEpisodes()` when `hasEmbeddings()` — the persona hears the *relevant* memories, not just the latest.

**Adapter seam:** recall goes through a `MemoryRecallBackend` interface with exactly one implementation in this spec — `LocalRecallBackend` (in-process cosine). A `RemoteMemoryBackend` (managed service: Mem0/Zep/vector-store, sync + remote recall for opted-in users) is a **future spec**; the interface seam + `shareMemoryWithAi` gate are the only hooks reserved now. Local SQLite always remains the source of truth.

**Lazy affection decay:** no timers. `affection()` computes `stored − elapsedHours × DECAY_RATE` (rate: 5/hour), floored at 0, and does **not** write back — writes only happen in `addAffection()`, which first applies decay, then the delta, then stores.

**Bond level thresholds** are a `static constexpr int[]` in the .cpp — tune without touching call sites. `addBondXP()` compares level before/after and emits `bondLevelChanged()` on crossing.

**Failure semantics:** unchanged from v1 — `!isValid()` → reads return defaults (0 / empty), writes are no-ops, signals suppressed. All queries parameterized.

---

## 3. Wiring (what writes memory, in this spec)

Only triggers that exist today — ContextSenses/TouchReactions specs add theirs later:

| Trigger (location) | Memory effect |
|--------------------|---------------|
| First run / migration | seed `rel.first_met_ts` |
| `session.start` event (`MainWindow` event handler) | If `rel.last_seen_day` ≠ today: set it to today, `addBondXP(5)` ("showing up" reward, once per calendar day) |
| `session.end` event | `increment("stats.sessions")`, `addBondXP(2)` |
| `user.click` synthetic event (existing PetStateMachine path) | `increment("stats.pokes")`, `addAffection(+1)` — throttled to one memory write per 2s by comparing against a `m_lastPokeWriteMs` timestamp in `MainWindow` (no new timer) |
| `session.end` | `recordEpisode("session", tr("%1h %2m, %3 events"))` **only if** session ≥ 30 min (uses `EventRouter::EventStats` already persisted by StatisticsManager) |
| `milestoneReached` (existing signal) | `recordEpisode("milestone", title)` — piggybacks on existing milestones |
| Any `recordEpisode()` | Enqueue async embedding job (only when persona AI configured + `shareMemoryWithAi`; no-op otherwise) |

**Why counters live in memory.db, not statistics.json:** StatisticsManager is component-diagnostics oriented (60s autosave, whole-file rewrite). Relationship state is user-facing, low-frequency, and must survive partial writes — SQLite per-key updates fit better. No StatisticsManager changes in this spec.

---

## 4. Files Changed

| File | Change |
|------|--------|
| `src/MemoryManager.h` | Add Episode struct, relationship/episode/recall/digest API, `bondLevelChanged` signal |
| `src/MemoryManager.cpp` | Migration (`user_version`), decay math, episode cap+rollup (both modes), cosine recall, digest renderer |
| `src/MemoryRecallBackend.h` | New — recall interface + `LocalRecallBackend` (in-process cosine) |
| `src/EmbeddingService.h/cpp` | New — QThread worker, embedding-job queue, writes BLOBs back to `episodes` |
| `src/llm/LLMProvider.h/cpp` | Add `embedText()` (OpenAI-compatible `/embeddings`; no-op for profiles without embeddings support) |
| `src/mainwindow.h/cpp` | Wire session/click triggers to memory calls; connect `bondLevelChanged` → tip bubble (reuse milestone bubble path) |
| `Seelie_zh_CN.ts` | Session-episode and level-up strings |
| `tests/` | New `test_memory2` (Qt Test): decay math, level thresholds, episode cap+rollup (both modes), cosine ranking order, digest content, invalid-DB degradation |

No CMake changes (`Qt6::Sql` already linked). No new threads — main-thread only, per v1 constraint.

---

## 5. Constraints

- **Main-thread only**, silent-failure semantics, parameterized queries — all inherited from v1.
- **Bounded growth:** episodes capped at 2,000 (≤ ~10MB fully embedded); relationship state is a fixed key set.
- **No background timers:** decay is lazy, day-boundary detection happens on `session.start`.
- **Embedding failures are silent:** endpoint error/offline/no-key → row keeps NULL embedding, all non-semantic paths unaffected. Query embeddings cached in-memory only (never persisted).
- **No new third-party dependencies:** cosine math is ~30 lines in `MemoryManager`; no vector-DB, no ONNX, no local model.
- **Digest ≤ 600 chars default** — LLM prompt budget is owned by the caller (PersonaEngine), not MemoryManager.
- **i18n:** episode text rendered via `tr()` at write time; digest is machine-facing but may contain localized episode text.

## Decision Record

- **Vector store: local cosine, not zvec/Pinecone-class.** Evaluated zvec (alibaba/zvec, embedded C++ vector DB) on 2026-07-17: rejected — thirdparty tree (RocksDB, Arrow, protobuf, antlr4…) breaks the <10MB/lightweight constraint, and its approximate indexes offer nothing over exact brute-force at ≤2,000 vectors. Revisit only if memory scope grows to 10⁵+ embedded items.
- **Managed memory service: adapter, not primary.** Mem0/Zep-class services offer LLM-driven extraction/summarization at large scale, but as primary store they break offline-first, expose the full intimate episode stream to a third party, and add RTT latency with no recall-quality gain at this scale. `MemoryRecallBackend` seam reserved for a future opt-in adapter; local SQLite is always the source of truth.

## Out of Scope

- Per-pack bond levels (memory is per-installation global; persona pool stays per-pack)
- LLM-generated episode text (AI-commentary spec upgrades `session.end` episodes to generated summaries)
- ContextSenses / TouchReactions triggers (their own specs — they only add new writers to this API)
- Digest injection into prompts + "remember when…" UI (AI-commentary spec consumes `memoryDigest()` / `recallEpisodes()`)
- `RemoteMemoryBackend` implementation (future spec; only the interface seam ships now)
- Cloud sync, multi-profile, encryption at rest
- Settings UI for viewing bond/episodes (candidate for a later "Memory" tab if wanted)
