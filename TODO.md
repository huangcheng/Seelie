# Seelie — TODO / Next Steps

**Updated:** 2026-08-22 · **Branch:** `main`

---

## 0.2 Seelie Sprite Desktop Life — status: ✅ COMPLETE

Spec: `docs/superpowers/specs/2026-08-21-seelie-sprite-desktop-life-design.md` ·
Plan: `docs/superpowers/plans/2026-08-21-seelie-sprite-desktop-life.md`

Shipped: sakura 21-clip / 115-frame Seelie pack + `seelie-sprite-gen` pipeline,
`nameMap` / `desktopMotion` manifest parsing, sprite `playAnimationChain`,
`DesktopMotionController` (wander / perch / fall), `DesktopGeometry` probes,
`desktopWandering` setting, `SEELIE_LOTTIE_ENABLED` default OFF (rlottie effects kept).

**Follow-ups:**
- [ ] (Low) Manual smoke on macOS: Dock shelf landing + window perch.
- [ ] (Low) Visual QA pass on individual clip frames; regen flagged cells via `generate_clips.py`.

---

## 0.1 Pet Aliveness — status: ✅ COMPLETE (branch pet-aliveness, pending merge)

Spec: `docs/superpowers/specs/2026-07-20-pet-aliveness-design.md` · Plan: `docs/superpowers/plans/2026-07-20-pet-aliveness.md`

Shipped: `IdleBehaviorEngine` (idle sayings scheduler, canned pools + opt-in LLM idle quips
via `idle.quip`), `SayingPool` (disk-first override in `<configDir>/sayings.<locale>.json`,
qrc defaults), frequency + LLM-quip config keys, settings UI, idle rotation upgrades in all
three engines (variable timing, anti-repeat, 12 new sprite names + EmptyTrash), zh_CN bundles.

**Follow-ups:**
- [ ] (Low) Manual smoke: frequency=Often → saying within ~4 min idle; LLM quip with a real profile.
- [ ] (Low) Persona pool text can overwrite a saying bubble via `updateMessage` when an event
  with an empty catalog tip arrives while a saying shows (found in Task 9 review;
  mainwindow.cpp persona handler could skip when `bubbleType() == StatusBubble`).

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
  (Note: Spec 4 wired requestDigestEmbedding into session.end — the production smoke
  now covers both episode and digest-embedding paths when run.)
- [x] **(Low) `~MemoryManager` "connection still in use" warning** — DONE 2026-07-17
  (`0a0f872`): `m_db` released before `removeDatabase`; warning confirmed gone
  from `test_memory2` output.

---

## 2. "Senses & Touch" program — remaining specs (in dependency order)

Agreed program: memory → senses → touch → AI commentary. Memory is done.

- [x] **Spec 2 — ContextSenses** — SHIPPED 2026-07-17 on branch `context-senses`.
  `SystemContextEngine` emitting `context.latenight/longsession/idle/away/gaming/
  lowbattery/timeofday` with per-event cooldowns; X11 `FullscreenWatcher` for Linux
  Gaming Mode (Wayland stays a documented no-op); macOS IOKit battery + CoreGraphics
  idle probes; production wiring in `main.cpp` (start/stop follows the
  `contextSensesEnabled` toggle live). 30 engine tests green; full ctest 18/18.
  Spec: `docs/superpowers/specs/2026-07-17-context-senses-design.md`,
  plan: `docs/superpowers/plans/2026-07-17-context-senses.md` (10-task).
  **Pending:** merge to `main` + Linux compile/runtime verification of the X11 branch
  (never compiled on this Mac).
- [x] **Spec 3 — TouchReactions** — SHIPPED 2026-07-17 on branch `touch-reactions`
  (pending merge). Stroke-detected petting (≥2 reversals, <15px budget — window
  stays put), grab with sustained FSM overlay, toss via 1500px/s release-velocity
  EMA (reaction-only, no momentum), silent hover. New FSM states Petted/Grabbed/
  Tossed reusing existing pack animations (candidate-fallback chains; no new assets).
  Positive-only affection (pet +2/2s, hover +1/60s), stats + first_pet/first_toss
  milestones, canned touch bubbles (tips JSON "touch" pools, en+zh_CN), master
  toggle `touchReactionsEnabled` (default on) + Settings → Interaction checkbox.
  12 detector tests + 7 FSM touch tests; suite green. Parked: momentum glide physics.
- [x] **Spec 4 — AI-native commentary** — SHIPPED 2026-07-17 on branch
  `ai-commentary` (pending merge). Persona prompts gain `memoryDigest()` +
  user name/bio behind `shareMemoryWithAi` (privacy tooltip re-scoped to match);
  pool-tier canned lines for 6 context events + user.pet/toss (auto-seeded;
  context.timeofday excluded — never bubbles); touch bubble bodies resolve via
  the persona pool with Spec-3 canned fallback; session.end fires an LLM summary
  ("2h, 42 edits, one heroic save") for ≥30min sessions with a deterministic
  template fallback; one digest embedding per qualifying session seeds future
  similarity recall (requestDigestEmbedding wired). Fixed en route: EventRouter
  now shows the catalog tip BEFORE emitting eventProcessed (downstream
  consumers win the bubble). Program memory→senses→touch→commentary COMPLETE.
  Parked: recall UI, habit learning, RemoteMemoryBackend, momentum glide,
  real-endpoint smoke.
- [x] **Recall UI ("What do you remember?")** — SHIPPED 2026-07-18 on branch
  `recall-ui` (pending merge). PersonaDialog-based recall window: relationship
  header (days/bond/affection/count), latest-100 episode list with kind tags +
  locale-aware relative times, dual search (substring always; embedding recall
  via new generic `EmbeddingService::enqueueQuery` when persona+shareMemory on,
  with timed substring fallback + "Searching…" placeholder). Entries in pet
  context menu + tray menu. 17 new tests (filter/time/dialog/search); suite
  20/20 green. Read-only v1 (no editing/deleting).
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
- [ ] Linux X11 `FullscreenWatcher` + Xss idle probe: implemented (Spec 2 T9) but
  never compiled/run — verify on a Linux machine (X11 session); Wayland stays a
  documented no-op.
- [ ] **TipsEngine-triggered bubbles are overwritten by the catalog event tip** —
  pre-existing: `TipsEngine::processEvent` runs before the catalog `showBubble`
  in `EventRouter::routeEvent`, so pattern-matched tip bubbles (incl. milestones
  fired from TipsEngine) get clobbered by the event's catalog tip. Surfaced by the
  Spec 4 emit-order review (noted in `EventRouter.cpp`). Needs a priority
  decision (catalog tip vs TipsEngine tip — which voice wins?), then a fix.

**Suite status:** 19/19 green on macOS as of 2026-07-17 (was 15/17 before ContextSenses).

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
