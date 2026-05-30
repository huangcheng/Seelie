# Seelie Project Audit

**Date:** 2026-05-24  
**Scope:** Full project — architecture, code quality, tests, security, build/CI, assets, i18n  
**Auditors:** Explorer (build/CI, tests/code, security/assets), Oracle (AI persona design)

---

## Executive Summary

Seelie is a well-structured Qt6/C++ desktop pet with clean subsystem boundaries, good thread safety discipline, and solid input validation. The codebase follows Qt idioms consistently and the architecture is sound for its scope.

**Key strengths:**
- Clean IPC pipeline with proper input validation and event whitelisting
- Thread safety done right — all shared state on main thread, cross-thread signals via `Qt::QueuedConnection`
- Zip-slip protection, path traversal guards, archive size caps
- Zero-dependency Node.js gateway
- Comprehensive bilingual README and CONTRIBUTING guide
- 16 test binaries covering core logic (FSM, persona pool, TTS providers, IPC, ECG)

**Key gaps:**
- **No CI/CD at all** — no GitHub Actions, no automated builds or test runs
- **Plaintext API keys** in QSettings INI file (acknowledged in codebase, not yet fixed)
- **Test coverage holes** — LottieAnimationEngine, Live2DAnimationEngine, UpdateChecker, TipsCatalog have zero tests
- **AI Persona Layer design** has critical threading issues that must be resolved before implementation

**Overall: 6.5/10** — solid foundation, needs CI and security hardening.

---

## 1. Architecture — 7/10

### Strengths
- **Clean pipeline:** IPC → EventRouter → Animation/Effects/Tips → UI. Each stage has a single responsibility.
- **MainWindow is a facade, not a god object** — 921 lines but delegates all real work to subsystems. 17 member pointers, all wired via signals/slots.
- **Signal/slot discipline** — no `Qt::DirectConnection` abuse, all cross-thread connections use queued connections.
- **Singleton usage is minimal and justified** — only `TipsCatalog::instance()`, read-only after init.
- **Additive design** — new features (TTS, AI persona) plug in without modifying existing subsystems' public APIs.

### Concerns
| Issue | Severity | Location |
|-------|----------|----------|
| `MainWindow::onActivePackChanged()` is 134 lines handling resize, engine switching, Live2D cropping, ECG anchoring | Medium | `mainwindow.cpp:717-851` |
| `ConfigManager` directly calls `AutoStartManager::setEnabled()` — tight coupling to OS-specific side effects | Low | `ConfigManager.cpp` |
| `PetStateMachine → MainWindow` coupling is tight — MainWindow must know about all 3 animation engines | Low | `mainwindow.cpp` |
| No `AnimationDispatcher` abstraction — fallback chain (Live2D → Lottie → Sprite) repeated in dispatch methods | Low | `mainwindow.cpp:235-271` |

### Recommendations
1. Extract `PackLoadCoordinator` from `onActivePackChanged()`
2. Extract `AnimationDispatcher` to own the 3-engine fallback chain
3. Invert `ConfigManager → AutoStartManager` dependency (AutoStartManager observes config signal)

---

## 2. Code Quality — 7.5/10

### Memory Management — 8/10
- Qt parent-child ownership used correctly throughout (138 `new` calls, all with parent or explicit `delete`)
- `std::unique_ptr` used sparingly but correctly (7 uses)
- `IpcServer::stop()` has documented intentional leak-on-wedge (better than cross-thread delete UB)
- `mz_free` correctly used for miniz heap allocations

### Error Handling — 8/10
- Errors consistently logged via `qWarning()` + returned as `false` or error struct
- `CharacterPackManager` separates log messages from user-facing `m_lastError` strings
- Graceful degradation on SQLite failure (returns empty/default, doesn't crash)
- No `Q_ASSERT` or `qFatal` in production code (appropriate for desktop app)

### Thread Safety — 9/10
- Zero `QMutex` in the codebase — not needed because all shared state is main-thread-only
- `UdpWorker` on dedicated `QThread`, signals cross via `Qt::QueuedConnection`
- `TTSEngine` has its own `QThread` with documented thread affinity
- `m_shuttingDown` guard prevents signal-to-destroyed-receiver UB during teardown

### Naming Consistency — 9/10
- PascalCase classes, `m_` prefix members, `on`-prefixed slots, `SCREAMING_SNAKE_CASE` constants
- Consistent within each file; minor camelCase/snake_case mixing in locals (acceptable)

### Dead Code — 7/10
| Item | Location | Action |
|------|----------|--------|
| `git_commands` matcher always returns `false` | `TipsEngine.cpp:199-213` | Remove or implement |
| `retranslateUi()` no-op in EventRouter | `EventRouter.h:27` | Keep — documented for signal-slot compat |

### Code Duplication — 7/10
- Font factory pattern (`HarmonyOS Sans SC` + `PreferAntialias` + `PreferNoHinting`) repeated in 5 files — extract `StyleUtils::makeFont()`
- `QHttpServer` setup boilerplate in test files — extract test helper
- Animation dispatch chain already extracted into methods

---

## 3. Test Coverage — 6/10

### What's Tested (16 test files)

| Test File | Quality | Coverage |
|-----------|---------|----------|
| `test_pet_state_machine.cpp` | Excellent | 20 methods — all FSM states, grace timers, one-shots, walking overlay |
| `test_tts_voice_cache.cpp` | Excellent | 12 methods — key determinism, LRU eviction, mime round-trip |
| `test_tts_providers.cpp` | Excellent | 12 methods — all 4 providers, request shape, response parsing, auth failure |
| `test_ecg.cpp` | Excellent | 13 methods — R-peak math, WAV synthesis, ICU monitor UI |
| `test_gaming_mode.cpp` | Excellent | ConfigManager round-trip, FullscreenWatcher signals, no-double-signal |
| `test_persona_engine.cpp` | Good | Pool hit, fallback, tier classification, on-demand upgrade, pool refill |
| `test_persona_pool.cpp` | Good | Insert/pick/wipe/duplicates/inflight/spam guard |
| `test_llm_provider.cpp` | Good | All 3 protocols, HTTP errors, cooldown suppression |
| `test_ipc_animations.cpp` | Good | Ping/pong, event→animation, tip bubble, malformed JSON, priority queue |
| `test_memory_manager.cpp` | Good | Set/get, increment, milestone, DB-failure graceful path |
| `test_tts_engine.cpp` | Good | Cancel-on-supersession, retry-on-error (thin — FakeProvider only) |
| `test_llm_profile.cpp` | Good | ConfigManager round-trip |
| `test_character_pack_persona.cpp` | Good | Manifest parse, persona absent fallback, SHA-256 hash |
| `test_character_pack_manager_errors.cpp` | Good | installPack/uninstallPack error messages |
| `test_autostart_manager.cpp` | Good | macOS plist / Linux .desktop round-trip |
| `test_platform_window.cpp` | Adequate | Null-safety smoke tests |

### What's NOT Tested

| Area | Risk | Notes |
|------|------|-------|
| **LottieAnimationEngine** | High | Zero tests. Complex rlottie integration, frame timing, load failures |
| **Live2DAnimationEngine** | High | Zero tests. OpenGL context, Cubism model loading, memory management |
| **LottieEffectOverlay** | High | Zero tests. Effect rendering, offset positioning |
| **UpdateChecker** | High | Zero tests. Binary UDP protocol, CRC, DNS, semver comparison |
| **TipsCatalog** | Medium | Zero tests. JSON loading, locale fallback, `{version}` substitution |
| **CharacterPackLoader** | Medium | Zero tests. ZIP extraction, manifest parsing, path traversal defense |
| **SpriteAnimationEngine** | Medium | Only tested indirectly via `test_ipc_animations` |
| **SettingsPanelWidget** | Medium | Zero tests. Complex 3-tab settings UI |
| **SystemTray** | Medium | Zero tests. Tray icon, menu, pack switching |
| **PackDropHandler** | Medium | Zero tests. Drag-and-drop MIME handling |
| **GlobalShortcutManager** | Medium | Zero tests. QHotkey registration |

### Test Quality Issues
- `test_ipc_animations.cpp:293` — `QVERIFY(true)` with comment "Did not crash" for malformed JSON. Should verify server is still responsive afterward.
- `test_tts_engine.cpp:117` — retry test only asserts FakeProvider classifies the error, doesn't exercise actual retry logic.

---

## 4. Security — 6/10

### Findings by Severity

#### HIGH
| # | Finding | Location |
|---|---------|----------|
| 1 | **API keys stored as plaintext in QSettings INI file** — any user-level process can read `~/AppData/Roaming/Seelie/Seelie.ini` | `ConfigManager.cpp:444` |
| 2 | **TTS tokens stored as plaintext in QSettings INI file** — same file, same exposure | `ConfigManager.cpp:185` |

The codebase is self-aware: `ProviderConfig.h:14-18` carries a `SECURITY NOTE` flagging this and recommending QKeychain integration.

#### MEDIUM
| # | Finding | Location |
|---|---------|----------|
| 3 | **No authentication on UDP IPC channel** — any local process can send events/tips to the pet | `UdpWorker.cpp` |
| 4 | **Update check over unencrypted UDP** — CRC-16 only, vulnerable to on-path spoofing | `UpdateChecker.cpp:42-46` |
| 5 | **Update server endpoint user-configurable** — compromised config could redirect update checks | `ConfigManager.cpp:330-337` |

#### LOW
| # | Finding | Location |
|---|---------|----------|
| 6 | Lottie files loaded without size validation before `rlottie::Animation::loadFromFile()` | `LottieAnimationEngine.cpp:40` |
| 7 | `filePath` IPC field not path-validated (currently display-only, not used for I/O) | `IpcServer.cpp` |
| 8 | Live2D Cubism SDK linked as pre-built binary without checksum verification | `CMakeLists.txt` |
| 9 | SQLite `memory.db` unencrypted at rest | `MemoryManager.cpp` |
| 10 | Tip messages not HTML-sanitized (currently safe — `QLabel` renders plain text) | `main.cpp:416-423` |

#### Positive Findings
- No hardcoded secrets in source code
- IPC bound to `127.0.0.1` only — `setIpcPort()` hardcodes the prefix
- Zip-slip protection in `.spk` installer (`safeZipDestination()`)
- Asset path traversal protection (`assetPath()` with `QDir::cleanPath` + prefix check)
- Archive entry size caps (10 MB) prevent decompression bombs
- SQL injection prevented — all queries use prepared statements
- Event whitelist (17 canonical names) prevents arbitrary event injection
- TTS credential fields masked in UI (`QLineEdit::Password`)
- Gateway has zero npm dependencies (minimal supply chain)
- Drag-and-drop filename sanitization (base name only)

### Security Recommendations
1. **[HIGH]** Integrate OS keychain (QKeychain) for API key and TTS token storage
2. **[MEDIUM]** Add HMAC or shared secret to UDP IPC protocol
3. **[MEDIUM]** Migrate update check to HTTPS (even a static JSON on CDN)
4. **[LOW]** Add file-size validation before Lottie file loading
5. **[LOW]** Add HTML sanitization to tip display (defensive, for future rich-text migration)

---

## 5. Build System & Dependencies — 8/10

### CMake
- `cmake_minimum_required(VERSION 3.19)` — correct for `string(JSON)` usage
- C++17 with `REQUIRED ON`
- `qt_standard_project_setup()` for AUTOMOC/AUTOUIC/AUTORCC
- **Missing:** No explicit `-Wall -Wextra -Wpedantic` compiler warnings

### FetchContent Dependencies
| Dependency | Version | Pinning | Patches |
|------------|---------|---------|---------|
| rlottie | v0.2 | Tarball URL | 3 patches (NEON, GCC 13, examples) |
| QHotkey | 1.5.0 | Git tag | None |
| Live2D Cubism SDK | Submodule | Proprietary | 2 patches (MinGW, GLEW) |

### Vendored Code
| Library | Location | License |
|---------|----------|---------|
| miniz | `thirdparty/miniz/` | Public domain / Unlicense |

### Gateway
- Zero npm dependencies — pure Node.js built-ins
- `engines.node: >=18`

### Packaging
| Platform | Tool | Output |
|----------|------|--------|
| Windows | Inno Setup + `windeployqt` | `SeelieSetup-<version>.exe` |
| macOS | `macdeployqt` + `hdiutil` + `codesign` | `Seelie-<version>.dmg` |
| Linux | `appimagetool` / `linuxdeploy` | `Seelie-<version>-<arch>.AppImage` |

The Inno Setup script is notably well-crafted: Persona-5-inspired theming, bilingual (EN/ZH), per-user/per-machine install choice.

---

## 6. CI/CD — 2/10

**No GitHub Actions workflows exist.** The `.github/` directory contains only OpenSpec skill definitions and prompt templates.

### What's Missing
- No automated builds on push/PR
- No cross-platform test execution
- No release artifact publishing
- No dependency caching
- No static analysis or linting

### What Exists (Manual)
- `scripts/build_release.py` (966 lines) — master build driver, auto-discovers Qt/cmake/ninja
- 14 test binaries runnable via `ctest`
- Platform-specific packaging scripts (Inno, DMG, AppImage)

### Recommendation
Add GitHub Actions with at minimum:
1. `ctest` on every push/PR (Ubuntu runner with Qt6)
2. Release workflow running `build_release.py` on Windows/macOS/Linux runners
3. Qt dependency caching (the ~2 GB install is the bottleneck)

---

## 7. i18n — 7/10

### Coverage
- 180 `tr()` calls across `src/`
- `Seelie_zh_CN.ts` — 772 lines covering ECG, MainWindow, PackCategories, Settings, SystemTray, TipsCatalog
- Bilingual README (EN + ZH)

### Risk
- `TipsEngine.cpp` tip strings (lines 95-211) are `tr()`-wrapped but may not appear in `.ts` if `lupdate` doesn't scan lambda bodies. **Verify with `lupdate`** — Chinese users may see untranslated TipsEngine tips while TipsCatalog tips are translated.

### Hardcoded Strings (Acceptable)
- `"HarmonyOS Sans SC"` — font family name
- `"Seelie"` — app name for QSettings identifiers
- `"Ctrl+Shift+O"` — default key sequence

---

## 8. Project Hygiene — 8/10

| Item | Status |
|------|--------|
| `.gitignore` | Comprehensive (111 lines), covers build artifacts, IDE files, Python bytecode, 16 GB upstream archive |
| `.gitmodules` | 2 submodules (CubismNativeFramework, CubismNativeSamples), correctly registered |
| LICENSE | MIT, Copyright 2026 |
| README | 532 lines, bilingual, well-structured with build instructions for all platforms |
| CONTRIBUTING.md | 96 lines, covers setup, code style, clang-format config, PR guidelines |
| `clang-format` | Configured (4-space indent, Allman braces, 100-char limit) |

### Minor Gaps
- `.vscode/` and `.idea/` not in `.gitignore`
- `seelie_debug.log` (macOS runtime log) not in `.gitignore`
- OpenSpec workflow not documented in README or CONTRIBUTING

---

## 9. AI Persona Layer Design — 4/10

Separate detailed audit in `audit-ai-persona-layer.md`. Summary:

### Critical (P0)
1. **Main thread blocking** — `resolve()` is synchronous but proposes 5s LLM calls for on-demand events
2. **Thread safety** — PersonaPool SQLite writes may happen from network-reply thread

### High Priority (P1)
3. Settings tab naming collision (existing "AI" tab hosts TTS)
4. `milestone.*` events undefined in pipeline
5. JSON parse failure handling incomplete
6. Integration wiring not specified

### YAGNI Cuts
- Three protocols → ship OpenAI Chat only
- `featureAssignments` map → simple string
- Statistics dialog → defer to v1.1
- `extraHeaders` / `systemPrefix` → defer
- Token tracking → defer

---

## Priority Action Items

### Immediate (P0)
1. **Add GitHub Actions CI** — the single biggest gap. At minimum: `ctest` on push/PR
2. **Add compiler warnings** — `-Wall -Wextra -Wpedantic` to CMake build
3. **Fix AI Persona Layer design** — resolve threading issues before implementation
4. **Integrate OS keychain** — move API keys and TTS tokens out of plaintext INI

### Short-term (P1)
5. **Add tests for LottieAnimationEngine** — zero coverage on complex rlottie integration
6. **Add tests for UpdateChecker** — zero coverage on binary UDP protocol
7. **Verify TipsEngine strings in `.ts`** — run `lupdate`, confirm Chinese translation coverage
8. **Remove dead `git_commands` matcher** — `TipsEngine.cpp:199-213`
9. **Add file-size validation before Lottie loading** — prevent allocation bombs
10. **Rename existing "AI" tab to "Voice"** — resolve naming collision for persona feature

### Medium-term (P2)
11. Extract `PackLoadCoordinator` from `MainWindow::onActivePackChanged()`
12. Extract `AnimationDispatcher` for 3-engine fallback chain
13. Add tests for TipsCatalog, CharacterPackLoader, SettingsPanelWidget
14. Migrate update check to HTTPS
15. Add HMAC to UDP IPC protocol
16. Extract `StyleUtils::makeFont()` from 5 repeated font factory patterns
17. Add `QScopedPointer` for top-level widgets in MainWindow
