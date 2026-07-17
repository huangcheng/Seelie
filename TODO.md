# Seelie — TODO / Next Steps

**Updated:** 2026-07-17 · **Branch:** `pet-memory-2` merged into `main` (ff `31ba078 → ee2f379`), pushed to `origin/main`

---

## 0. Status: ✅ merged to main

- [x] **Merged to main** 2026-07-17 — fast-forward, all 24 commits, pushed to `origin/main`.
  Final whole-implementation review verdict was **READY TO MERGE**; all 10 plan tasks
  passed spec + quality review loops.
- [x] Mac handoff complete — building from `main` per CLAUDE.md
  (`cmake .. -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)"`). The Windows PATH quirks noted
  below do not apply to macOS.

---

## 1. Pet Memory 2.0 — status: ✅ COMPLETE

Spec: `docs/superpowers/specs/2026-07-17-pet-memory-2-design.md` (status: implemented)
Plan: `docs/superpowers/plans/2026-07-17-pet-memory-2.md` (with execution errata)

Shipped: relationship state (bond XP L0–L5, decaying affection, days-met), episodic memory
(2,000-cap, FIFO + embedding-dedup rollup), exact-cosine semantic recall, `memoryDigest`,
`EmbeddingService` (worker thread, injectable embed fn), `LLMProvider::embedTextSync`,
wiring (sessions/daily-login/pokes/milestones/level-up bubbles), zh_CN i18n.

**Verification:** `test_memory2` 31 slots green; full ctest 15/17 (2 pre-existing flakes, §3).

### Follow-ups from final review (not blocking merge)

- [x] **(Medium) Decouple memory wiring from persona** — DONE 2026-07-17 (`0a0f872`).
  The connect now lives in idempotent `MainWindow::wireMemoryEventConnect()`,
  called from `setEventRouter()`/`setMemoryManager()`; persona is no longer involved.
- [ ] **(Low) Smoke-test real embeddings once** — `embedTextSync` is untested network glue.
  With an OpenAI profile + `shareMemoryWithAi` on: record an episode, confirm its
  `embedding` BLOB fills and `memoryDigest()` switches to similarity mode.
- [x] **(Low) `~MemoryManager` "connection still in use" warning** — DONE 2026-07-17
  (`0a0f872`): `m_db` released before `removeDatabase`; warning confirmed gone
  from `test_memory2` output.

---

## 2. "Senses & Touch" program — remaining specs (in dependency order)

Agreed program: memory → senses → touch → AI commentary. Memory is done.

- [~] **Spec 2 — ContextSenses** — spec ✅ + 10-task plan ✅ committed 2026-07-17
  (`docs/superpowers/specs/2026-07-17-context-senses-design.md`,
  `docs/superpowers/plans/2026-07-17-context-senses.md`). **Implementation not started.**
  Scope decided: `SystemContextEngine` emitting `context.latenight/longsession/idle/away/
  gaming/lowbattery/timeofday` with cooldowns; gaming = quiet hide + welcome-back on
  fullscreen stop (auto-hide wiring already exists in MainWindow — found during recon);
  Linux `FullscreenWatcher` X11 impl is plan Task 9. Execute via subagent-driven-development.
- [ ] **Spec 3 — TouchReactions**: `user.hover`, `user.pet` (press+drag), `user.grab`/
  `user.toss` (window drag with velocity); affection/XP effects via MemoryManager
  (API already shipped); animations + persona lines per touch event.
- [ ] **Spec 4 — AI-native commentary**: PersonaEngine prompt builder gains the memory
  digest (`MemoryManager::memoryDigest()`, behind `shareMemoryWithAi`) + context injection;
  new persona-pool event names for context/touch events; LLM-generated `session.end`
  summary lines ("3h, 42 edits, one heroic save"); canned random-sayings become the
  offline fallback. Also wire production `requestDigestEmbedding()` calls (currently
  nothing calls it — this spec owns that).
- [ ] **(Parked, future)** `RemoteMemoryBackend` (Mem0/Zep-class managed memory adapter) —
  interface seam reserved as `MemoryRecallBackend`; local SQLite stays source of truth.
  LLM-summarized episode rollups. Habit learning (active-hours patterns). Per-pack bonds.
  "Remember when…" recall UI (backend `recallByVector` already shipped).

---

## 3. Pre-existing issues (independent of memory work)

- [x] `test_ipc_animations` — FIXED 2026-07-17 (`e8e590d`). Root cause was NOT a UDP
  flake: `assets/map.png` was removed (c8e7b99) so asset discovery always failed and
  the null-unsafe `cleanupTestCase` segfaulted. Now resolves from `assets/packs/*/sprites`,
  null-safe cleanup, harness falls back past legacy MS-Agent name drift. 10/10 green.
- [x] `test_pet_state_machine` — FIXED 2026-07-17 (`fd37a4d`). Root cause was NOT a
  QTimer flake: grace (1500ms) deterministically expired under every 2000ms one-shot
  overlay → Idle. FSM now suppresses grace under overlays + re-arms on finish (user
  decision); tests hardened with QTRY_COMPARE. 21/21 green, 5 consecutive runs.
- [x] Duplicate `"Character"` source string in `Seelie_zh_CN.ts` — FIXED 2026-07-17
  (`2172475`): distinct `tr()` disambiguation comments (display-mode option vs
  settings section title); survives lupdate.
- [ ] Linux `FullscreenWatcher::isFullscreenAppActive()` stub returns false
  (owned by Spec 2 plan, Task 9 — X11 impl; Wayland stays stubbed)

**Suite status:** 17/17 green on macOS as of 2026-07-17 (was 15/17).

---

## 4. Dev-environment notes

- **Windows shell builds:** prepend `C:\Qt\Tools\mingw1310_64\bin` and
  `C:\Qt\6.11.1\mingw_64\bin` to PATH, otherwise `cc1plus.exe` fails *silently* (exit 1,
  no diagnostics). `ctest` works without this (CMake captures the toolchain env).
- **QtTest per-slot output:** use `test_memory2.exe -o out.txt,txt` — console redirection
  doesn't capture `-v2` output on Windows.
- **Suite time:** ~20s, dominated by the 2,000-row rollup tests. Normal.
- **thirdparty/CubismNative* submodule pointer noise** in `git status` — pre-existing,
  leave alone.
