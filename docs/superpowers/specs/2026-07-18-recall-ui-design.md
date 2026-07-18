# Recall UI ("What do you remember?") — Design Spec

**Status:** design approved by standing user delegation (2026-07-17/18, "fully make
your own design, and coding"), pending implementation plan
**Date:** 2026-07-18
**Follows:** the memory→senses→touch→commentary program (complete). This is the
first post-program feature: making the pet's memory visible to the user.

## Goal

Give the user a window into the pet's memory: a "What do you remember?" dialog
that browses past episodes (newest first, kind-tagged, relative timestamps) and
searches them — semantically (embedding recall) when the AI path is configured,
by substring otherwise. Plus a relationship header (days known, bond, affection)
so the memory feels like a relationship, not a log file.

## Architecture

```
Pet context menu / tray menu ──▶ MainWindow (QPointer<RecallDialog>, raise-or-create)
                                        │
                              RecallDialog : PersonaDialog
                              ├─ header: days/bond/affection/count (MemoryManager getters)
                              ├─ QLineEdit (400ms debounce)
                              │     ├─ EmbeddingService present → requestQueryEmbedding
                              │     │     → queryEmbeddingReady → recallByVector(top 20)
                              │     └─ otherwise → filterEpisodesContains (substring)
                              └─ QListWidget: "[kind] text — 2h ago"
                                        │
EmbeddingService += generic query path: enqueueQuery(key, text)
                    → setQueryEmbedding(key, vec) + queryEmbeddingReady(key, vec)
```

- `PersonaDialog` base (frameless, shadow, drag-to-move, centered) +
  `StyleUtils::personaDialogQss()` + `personaFont` — identical chrome to
  StatisticsDialog. Singleton-ish via `QPointer` + `WA_DeleteOnClose`, raised on
  re-open (mirrors `m_statsDialog`).
- All heavy logic lives in testable units: `filterEpisodesContains` (pure),
  `relativeTime` (pure), the EmbeddingService query path (fake-fn seam), and the
  already-tested `recallByVector` backend.

## 1. RecallDialog

`src/RecallDialog.h/.cpp`, `class RecallDialog : public PersonaDialog`.

- **Title:** "What do you remember?" (zh: 你还记得吗？)
- **Header line** (one QLabel, monospace value style per personaDialogQss):
  `Known {name} for {days} days · Bond L{level} · Affection {n}/100 · {count} memories`
  (`effectiveName()`, `daysMet()`, `bondLevel()`, `affection()`, `episodeCount()`;
  name omitted when empty).
- **Search box:** QLineEdit, placeholder "Search memories…" (zh: 搜索回忆…),
  400 ms debounce via QTimer::singleShot restart.
- **List:** QListWidget of episode rows: `[{kind}] {text} — {relativeTime}`.
  Default (empty query): latest **100** episodes, newest first.
  Results view: top **20** matches (semantic) or substring matches (same cap).
- **Empty states:** zero episodes → "No memories yet — chat with me more!"
  (zh: 还没有回忆，多陪我聊聊吧！); zero results → "Nothing like that yet."
  (zh: 还没有相关的回忆。)
- **Footer:** Close button (personaButtonQss); Esc closes.
- Size: 480×560, matching the StatisticsDialog's proportions.

## 2. Search Paths

**Substring (always available):** `filterEpisodesContains(episodes, query)` —
case-insensitive `QString::contains` over the latest 500 episodes (recall cap
keeps the DB at 2000; 500 is a generous local working set). Pure function in
`src/RecallFilter.h` (header-only, so the test links nothing heavy).

**Semantic (when AI configured):** requires `EmbeddingService` (exists iff
personaEnabled && shareMemoryWithAi — main.cpp:479).
- New `EmbeddingService::enqueueQuery(const QString &key, const QString &text)`
  — reuses the worker pipeline; the job's result is stored via
  `MemoryManager::setQueryEmbedding(key, vec)` and emitted as
  `queryEmbeddingReady(key, vec)` (new signal). The existing digest path
  (`requestDigestEmbedding`, id=-1 → `kDigestQueryKey`) becomes the special case
  `enqueueQuery(kDigestQueryKey, text)` — no behavior change.
- On `queryEmbeddingReady`: `recallByVector(vec, 20)` → render.
- Debounce per keystroke (400 ms); stale results dropped by a query-counter
  (each new query bumps `m_querySeq`; the slot renders only the latest key).
  If `queryEmbeddingReady` hasn't arrived 400 ms after the debounce fired,
  the substring path runs as the visible fallback (belt-and-suspenders:
  semantic when it answers in time, substring otherwise).

`recallByVector` has no threshold parameter — render its top-20 as-is (scores
descend; the backend already sorts). No client-side thresholding in v1.

## 3. Entry Points

- **Pet context menu** (`MainWindow::showContextMenu`): new action
  "What do you remember?" between Settings and About.
- **Tray menu** (`SystemTray`): new action after "Statistics…", emitting a new
  `recallTriggered()` signal wired in main.cpp like `statisticsTriggered`.
- Both open the same singleton-ish dialog via `MainWindow::showRecallDialog()`.
- All new strings `tr()` + zh entries in `Seelie_zh_CN.ts` (menu items, dialog
  title, header template, placeholder, empty states). "Close" already has a
  `.ts` entry (line ~775) — reuse it verbatim.

## 4. Error Handling

- `m_memory` null/invalid → dialog shows the zero-episode empty state (no crash).
- `EmbeddingService` null (persona off / shareMemory off) → substring search
  only; no UI hint needed (search box just works).
- Embedding request fails or is slow (worker error / no answer within the
  fallback window) → substring results show instead; a late
  `queryEmbeddingReady` still upgrades the list to semantic ranking.
- Relative time for future/clock-skewed timestamps → clamp to "just now".

## 5. Testing

`tests/test_recall_dialog.cpp`:
- `relativeTime`: just-now (<60s), minutes, hours, days, ≥7d absolute-date
  fallback, future clamp.
- `filterEpisodesContains`: case-insensitive hit, miss → empty, empty query →
  input unchanged, non-ASCII (zh) match.
- Widget smoke (`:memory:` DB): dialog constructs, header contains the days/
  bond fragments, list count equals seeded episodes (≤100), substring search
  filters the list, empty state shows when DB empty.
- EmbeddingService query path (fake embed fn, mirrors test_memory2's seam):
  `enqueueQuery("k", "text")` → `queryEmbeddingReady("k", vec)` emitted and
  `m_memory` has the key stored.

## Files Changed (planned)

- **New:** `src/RecallDialog.h`, `src/RecallDialog.cpp`, `src/RecallFilter.h`,
  `tests/test_recall_dialog.cpp`
- **Modified:** `src/EmbeddingService.h/.cpp` (enqueueQuery + signal),
  `src/MemoryManager.h` (expose `kDigestQueryKey` comment only — no change
  expected), `src/MainWindow.h/.cpp` (menu action + showRecallDialog +
  QPointer), `src/SystemTray.h/.cpp` (tray action + recallTriggered),
  `src/main.cpp` (tray wiring), `Seelie_zh_CN.ts`, `CMakeLists.txt`,
  `tests/CMakeLists.txt`, `TODO.md`

## Constraints

- Read-only feature: no editing, deleting, or exporting memories in v1.
- Zero network in the default path (substring search always works; semantic
  only when the AI path is already enabled by the user).
- <10 MB RAM budget: list capped at 100 default / 20 results; no per-row
  widgets beyond QListWidgetItem text.
- Repo conventions: QStringLiteral, UPPERCASE acronyms, reason-comments,
  conventional commits, TDD per task, i18n via tr() + hand-synced .ts.

## Decision Record

- 2026-07-18 — Standing autonomy delegation ("fully make your own design, and
  coding") applies; interactive gates waived, decisions recorded here.
- 2026-07-18 — Dialog, not bubble: recall needs scroll/search; PersonaDialog
  chrome (StatisticsDialog precedent) over extending TipWidget.
- 2026-07-18 — Two search paths (semantic + substring) with silent fallback;
  no UI distinction between them in v1 (the user shouldn't think about it).
- 2026-07-18 — Generic `enqueueQuery(key, text)` on EmbeddingService rather
  than a second service or direct `embedTextSync` calls (reuses the worker
  thread, queue backpressure, and fake-fn test seam).
- 2026-07-18 — Latest-100 default / top-20 results; no pagination, no
  threshold UI, no editing (YAGNI).
- 2026-07-18 — Both menus (pet + tray) get the entry; title is the question
  form ("What do you remember?") because that's the feature's name.

## Out of Scope

- Editing/deleting/exporting episodes; per-kind filters; date pickers;
  pagination; score badges/threshold sliders.
- "Remember when…" as a *bubble* feature (the pet volunteering memories —
  proactive commentary; separate future spec with its own taste bar).
- Habit learning, RemoteMemoryBackend (still parked).
