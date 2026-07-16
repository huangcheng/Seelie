# Seelie — TODO / Next Steps

**Updated:** 2026-07-17 · **Branch:** `pet-memory-2` (20 commits, rebased on `main`, clean)

---

## 0. Right now (handoff to Mac)

- [ ] **Push the branch** so the Mac can pull it (not done — left for your call):
  `git push -u origin pet-memory-2`
- [ ] **Merge to main when ready** — final whole-implementation review verdict was **READY TO MERGE**.
  All 10 plan tasks passed spec + quality review loops. Options: merge commit / squash / PR.
- [ ] On the Mac: `git fetch && git checkout pet-memory-2`, then build per CLAUDE.md
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

- [ ] **(Medium) Decouple memory wiring from persona** — the `EventRouter::eventProcessed`
  → memory connect lives in `MainWindow::setPersonaEngine()`. A future "no-persona" mode
  would silently disable all memory tracking. Move the connect to `setMemoryManager()` or
  `setEventRouter()` (both are called unconditionally in `main.cpp`).
- [ ] **(Low) Smoke-test real embeddings once** — `embedTextSync` is untested network glue.
  With an OpenAI profile + `shareMemoryWithAi` on: record an episode, confirm its
  `embedding` BLOB fills and `memoryDigest()` switches to similarity mode.
- [ ] **(Low) `~MemoryManager` "connection still in use" warning** — cosmetic Qt warning on
  teardown; fix by ensuring all QSqlQuery copies die before `removeDatabase`.

---

## 2. "Senses & Touch" program — remaining specs (in dependency order)

Agreed program: memory → senses → touch → AI commentary. Memory is done.

- [ ] **Spec 2 — ContextSenses** (next up): `SystemContextEngine` emitting synthetic events
  (`context.latenight`, `context.longsession`, `context.idle`, `context.gaming`,
  `context.lowbattery`, time-of-day bucket on `session.start`) into the existing pipeline
  with rate limits + cooldowns. Also finish the Linux `FullscreenWatcher` stub and
  gaming-mode auto-hide wiring. Brainstorm via the brainstorming skill, then spec → plan.
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

- [ ] `test_ipc_animations` — SEGFAULT on Windows (UDP/socket flake; noted in CLAUDE.md era)
- [ ] `test_pet_state_machine` — QTimer precision flake under parallel load
  (`testFailedOneShotReturnsToWorking`, tests/test_pet_state_machine.cpp:144)
- [ ] Duplicate `"Character"` source string in `Seelie_zh_CN.ts` (SettingsPanelWidget
  context, ~lines 461/501) — lrelease warning
- [ ] Linux `FullscreenWatcher::isFullscreenAppActive()` stub returns false
  (covered by Spec 2 above)

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
