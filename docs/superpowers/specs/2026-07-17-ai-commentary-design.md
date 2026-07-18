# AI-Native Commentary (Spec 4) — Design Spec

**Status:** design approved by user delegation (2026-07-17, user asleep: "fully make
your own design, and coding"), pending implementation plan
**Date:** 2026-07-17
**Program:** memory → senses → touch → **AI commentary** (Spec 4 of 4, final)

## Goal

Close the loop: the pet remembers (memory 2.0), senses (context events), and feels
(touch) — now it should *talk about it*. Persona prompts gain the memory digest and
event context; context/touch events get LLM-backed reaction lines through the
existing pool machinery; sessions end with a one-line AI summary when an LLM is
configured; everything degrades gracefully to the existing canned behavior offline.

## Architecture

```
EventRouter ─ eventProcessed ─▶ PersonaEngine::resolve (existing (a) connect)
                                   │ Pool tier: + context.*, user.pet, user.toss
                                   │ OnDemand tier: prompt += memoryDigest()
                                   │   (behind shareMemoryWithAi)
Touch (MainWindow) ─ resolve("user.pet"/"user.toss") ─▶ async bubble upgrade
                                   │ offline: canned touchLine (Spec 3, kept)
session.end (MainWindow) ─▶ PersonaEngine::requestSessionSummary(statsLine)
                                   │ offline: deterministic template bubble
                                   └▶ EmbeddingService::requestDigestEmbedding
                                       (1/session → kDigestQueryKey →
                                        memoryDigest() switches to similarity
                                        recall for FUTURE prompts)
```

- Everything reuses existing seams: `PersonaEngine::resolve`, the
  `m_activeBubbleRequestId` upgrade machinery in MainWindow, `PersonaPool`
  auto-seeding, `EmbeddingService`'s digest-query job (`enqueueEpisode(-1, …)`).
- No new config keys. Gates: `personaEnabled` + `shareMemoryWithAi` (existing).

## 1. Memory Digest Injection (OnDemand prompts)

In `PersonaEngine::resolveOnDemand`'s prompt builder
(`src/PersonaEngine.cpp:162-199`), inside the existing `shareMemory` block:
append `m_memory->memoryDigest()` (default 600 chars) as a "Memory:" section of
the user prompt, after the recent-events list. No digest when the gate is off
(byte-identical prompt to today). `memoryDigest()` itself stays ungated — it is
only *called* here and in tests.

## 2. Pool-Tier Expansion (context + touch events)

Add to `PersonaEngine::poolTierEvents()`: all 7 `context.*` names plus
`user.pet` and `user.toss`. Pool membership means: synchronous canned pick;
auto-seed via `generateBatch` when a pool runs low (existing machinery, no new
code paths). Fallback when the pool is empty AND no provider: `fallbackTip`.

`fallbackTip` becomes touch-aware: for `user.pet`/`user.toss` it returns a
random line from the Spec-3 `"touch"` JSON pools (gesture key = event name
suffix). `context.*` already resolves via `TipsCatalog::eventTip` (Spec 2
entries). `user.hover` stays silent (never resolves to a bubble).

## 3. Touch → Persona Upgrade Path

`MainWindow::onPetStroke` / `onTossDetected` keep their Spec-3 behavior
(FSM + memory + canned bubble), and additionally call
`m_personaEngine->resolve("user.pet" / "user.toss")` when persona is enabled:

- **Bubble text rule:** if `resolve()` returns non-empty text synchronously
  (pool-tier hit), that text *is* the bubble body; otherwise the Spec-3
  `touchLine` is shown. Offline these are the same content stream
  (`fallbackTip("user.pet")` returns a touch-pool line — §2), so offline
  behavior is unchanged.
- `requestId == 0` (pool hit / offline / not configured): nothing further —
  the shown bubble is final.
- `requestId != 0`: register `m_activeBubbleRequestId` /
  `m_activeBubbleFallbackBody` (same fields as the (a) connect) so
  `onTipUpgraded` replaces the bubble text and TTS policy matches event bubbles.
- Spam guard: touch upgrades only attempted when a bubble was actually shown
  this stroke/toss (pet's 1-in-3 rule and milestone guard respected — if no
  canned bubble was shown, no resolve call either).

## 4. Session-End Summary

Trigger: `session.end` in `MainWindow::onEventForMemory`, only when the session
qualified for an episode (≥ 30 min — same threshold, already computed there).

New `PersonaEngine::requestSessionSummary(const QString &statsLine) -> quint64`:
- Builds the summary user prompt: "Session: {statsLine}\nRecent events:
  {eventWindow}\nMemory: {digest if shareMemory}\nSummarize this work session
  in-character in ONE short sentence ({lang})."
- Returns a requestId through the same async lifecycle as resolveOnDemand
  (stale-pack/hash detection, failure → tipUpgradeFailed, success →
  tipUpgraded with 200-char cap).
- Offline / not configured → returns 0.

`statsLine` assembly (MainWindow): `"{h}h {m}m, {n} events"` from
`m_sessionStartMs` + `m_sessionEventCount`. (Episode text already records the
session; the summary is the *spoken* counterpart.)

Offline fallback bubble (deterministic, shown immediately, upgraded async if
LLM responds): template from TipsCatalog `"messages"` section —
`session.summary` entry: title "Session ended", body "Session: {duration} ·
{events} events" (en + zh_CN, `{duration}`/`{events}` substituted at call site).
LLM upgrade replaces it like any other tip upgrade. No summary bubble for
sessions < 30 min (silence — short sessions aren't worth commentary).

## 5. Digest Embedding Wiring (requestDigestEmbedding)

On the same `session.end` trigger (≥ 30 min sessions only), MainWindow calls
`m_embeddingService->requestDigestEmbedding(contextText)` where `contextText =
statsLine + "\n" + memoryDigest()` — but only when `m_embeddingService` exists
(it is constructed iff personaEnabled && shareMemoryWithAi, main.cpp:479) and
`m_memory->isValid()`. Effect: worker embeds the text → `kDigestQueryKey` set →
**future** `memoryDigest()` calls rank episodes by similarity to the last
session instead of recency. Bounded quota: one embedding per qualifying
session. No user-facing text; failures are silent (EmbeddingService already
swallows worker errors with a qWarning).

## 6. Error Handling

- LLM down/cooldown: existing provider behavior (callback error →
  tipUpgradeFailed → fallback spoken/shown). Summary failure → the template
  bubble stands.
- `shareMemoryWithAi` off: prompt shape byte-identical to today for OnDemand;
  no digest injection; no embedding calls (service absent).
- Persona off: resolve() early-returns fallbackTip (existing); summary
  requestSessionSummary returns 0 → template bubble shows (still
  deterministic, no AI) — acceptable and desirable (stats summary isn't AI).
- Touch resolve with no FSM/persona: stubs already null-guard.

## 7. Testing

Extend `tests/test_persona_engine.cpp` (QHttpServer mock pattern with request
capture) + `tests/test_persona_pool.cpp` where noted:

- Digest injection: shareMemory on → resolve OnDemand → captured prompt
  contains "Memory:" + episode text; off → prompt identical to today's shape.
- Pool tier: `tierFor()` returns Pool for all 7 context names + user.pet/toss.
- fallbackTip: `user.pet` → non-empty line from the touch pool; `user.hover`
  → empty.
- Session summary: requestSessionSummary returns requestId, QHttpServer
  captures prompt containing the statsLine; offline (unconfigured provider) →
  returns 0 and template path taken.
- Digest embedding: EmbeddingService with fake embed fn (existing seam) →
  requestDigestEmbedding stores kDigestQueryKey vector; memoryDigest()
  switches to similarity mode (already covered by test_memory2 — assert the
  wiring call path only).
- Template substitution: `{duration}`/`{events}` replaced in the fallback line.

## Files Changed (planned)

- **Modified:** `src/PersonaEngine.h/.cpp` (digest injection, pool set,
  requestSessionSummary), `src/mainwindow.h/.cpp` (touch resolve, session
  summary + embedding trigger, stats assembly), `assets/i18n/tips.en.json` +
  `tips.zh_CN.json` (`session.summary` message), `tests/test_persona_engine.cpp`,
  possibly `tests/CMakeLists.txt` (no new files expected)
- **No changes:** ConfigManager, EventRouter, EmbeddingService (wiring only),
  PetStateMachine, TipsCatalog (touchLine reused)

## Constraints

- Zero new config keys; zero behavior change when persona is disabled or
  `shareMemoryWithAi` is off (except the deterministic template summary, which
  is the designed offline path).
- Quota discipline: ≤1 summary generation + ≤1 embedding per qualifying
  session; pool refills unchanged (existing MIN_POOL_SIZE machinery).
- Canned behavior from Specs 2-3 remains the offline fallback everywhere —
  never removed, only upgraded.
- Repo conventions: QStringLiteral, UPPERCASE acronyms, reason-comments,
  conventional commits, TDD per task.

## Decision Record

- 2026-07-17 — Full autonomy granted by user (asleep): "fully make your own
  design, and coding." Interactive brainstorm gates waived; all decisions
  recorded here.
- 2026-07-17 — Scope = the five TODO Spec-4 items, trimmed to: digest
  injection, pool expansion, touch upgrade path, session summary, digest
  embedding wiring. Nothing else (YAGNI).
- 2026-07-17 — Session summary threshold = the existing ≥30 min episode rule;
  offline fallback = deterministic template (kept even when persona is off —
  a stats line is not AI); LLM upgrade through the existing requestId
  machinery.
- 2026-07-17 — Touch events get pool-tier lines + OnDemand-style bubble
  upgrade via resolve(); the Spec-3 canned touchLine is the fallback, not a
  replacement target.
- 2026-07-17 — Digest embedding fires once per qualifying session on
  session.end; purpose is FUTURE similarity-ranked digests, not the current
  summary (which already has its own context).
- 2026-07-17 — user.hover stays silent (no pool, no fallback) — hover must
  never bubble.

## Out of Scope

- Per-pack personalities / persona-pool authoring UI.
- "Remember when…" recall UI (backend exists; separate future spec).
- Habit learning, RemoteMemoryBackend, LLM episode rollups (parked in TODO).
- Streaming LLM responses, multi-sentence summaries.
- Real-endpoint smoke (needs user's profile + quota; parked for user).
