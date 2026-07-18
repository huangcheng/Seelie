# AI-Native Commentary (Spec 4) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Persona prompts gain the memory digest; context/touch events get LLM-backed lines via the pool; sessions ≥30 min end with an LLM summary (template fallback) plus one digest embedding for future similarity recall — all degrading to existing canned behavior offline.

**Architecture:** `PersonaEngine` grows digest injection + a deduplicated `fireOnDemand` helper + `requestSessionSummary`; pool tier expands to context/touch events; `fallbackTip` becomes touch-aware; MainWindow wires touch resolve + the session-end summary/embedding. Spec: `docs/superpowers/specs/2026-07-17-ai-commentary-design.md`.

**Tech Stack:** C++17, Qt6, QHttpServer test mocks (existing `SEELIE_HAS_QHTTPSERVER` pattern). No new config keys, no new files except docs.

**Conventions (binding):** `QStringLiteral`; UPPERCASE acronyms; reason-comments; conventional commits; TDD. Offline/toggle-off behavior must remain available everywhere (canned = fallback, never removed).

---

### Task 1: fallbackTip touch-awareness

**Files:**
- Modify: `src/PersonaEngine.cpp` (`fallbackTip`, ~line 86-89)
- Test: `tests/test_persona_engine.cpp`

- [ ] **Step 1: Write the failing test**

Add to `tests/test_persona_engine.cpp`:

```cpp
    void testFallbackTouchEventsUseTouchPool()
    {
        // Persona disabled → resolve() returns {fallbackTip, 0}; user.* touch
        // events must fall back to the Spec-3 touch line pools (qrc-bundled).
        MemoryManager mm(":memory:");
        ConfigManager cfg;
        cfg.load();
        cfg.setPersonaEnabled(false);
        PersonaEngine engine(&mm, &cfg);

        QVERIFY(!engine.resolve("user.pet", {}).text.isEmpty());
        QVERIFY(!engine.resolve("user.toss", {}).text.isEmpty());
        // hover stays silent (spec: hover never bubbles)
        QVERIFY(engine.resolve("user.hover", {}).text.isEmpty());
    }
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_persona_engine && ./build/tests/test_persona_engine`
Expected: FAIL — `user.pet` returns empty (eventTip has no user.* entries).

- [ ] **Step 3: Implement**

`src/PersonaEngine.cpp` — replace `fallbackTip`:

```cpp
QString PersonaEngine::fallbackTip(const QString &eventName) const
{
    // Spec 4: touch events fall back to the Spec-3 canned touch pools
    // (gesture key = event name suffix). Everything else uses event tips.
    if (eventName == QLatin1String("user.pet")) {
        return TipsCatalog::instance().touchLine(QStringLiteral("pet")).body;
    }
    if (eventName == QLatin1String("user.toss")) {
        return TipsCatalog::instance().touchLine(QStringLiteral("toss")).body;
    }
    return TipsCatalog::instance().eventTip(eventName).body;
}
```

- [ ] **Step 4: Run test to verify it passes**

Expected: PASS (all slots). Note: test binaries bundle the tips qrc since Spec 2 — the touch pool resolves for real.

- [ ] **Step 5: Commit**

```bash
git add src/PersonaEngine.cpp tests/test_persona_engine.cpp
git commit -m "feat(persona): touch-aware fallbackTip (user.pet/toss → touch pools)"
```

---

### Task 2: Pool-tier expansion (context + touch events)

**Files:**
- Modify: `src/PersonaEngine.cpp` (`poolTierEvents()`, lines 10-20)
- Test: `tests/test_persona_engine.cpp`

- [ ] **Step 1: Write the failing test**

Extend `testTierClassification`:

```cpp
        // Spec 4: context.* and user.pet/toss are pool-tier (auto-seeded).
        for (const char *name : {"context.latenight", "context.longsession",
                                 "context.idle", "context.away", "context.gaming",
                                 "context.lowbattery", "context.timeofday",
                                 "user.pet", "user.toss"}) {
            QVERIFY(PersonaEngine::tierFor(name) == PersonaEngine::Tier::Pool);
        }
        // hover stays out of the bubble pipeline entirely.
        QVERIFY(PersonaEngine::tierFor("user.hover") == PersonaEngine::Tier::OnDemand);
```

- [ ] **Step 2: Run test to verify it fails**

Expected: FAIL — new names return OnDemand.

- [ ] **Step 3: Implement**

`src/PersonaEngine.cpp` — extend the static set:

```cpp
    static const QSet<QString> s = {
        QStringLiteral("tool.before"), QStringLiteral("tool.after"),
        QStringLiteral("tool.failed"),
        QStringLiteral("file.edited"), QStringLiteral("file.watched"),
        QStringLiteral("prompt.submitted"), QStringLiteral("todo.updated"),
        QStringLiteral("notification.sent"), QStringLiteral("permission.response"),
        // Spec 4: context senses + touch reactions get pool-tier canned lines
        // (auto-seeded via generateBatch on first low-water access).
        QStringLiteral("context.latenight"), QStringLiteral("context.longsession"),
        QStringLiteral("context.idle"), QStringLiteral("context.away"),
        QStringLiteral("context.gaming"), QStringLiteral("context.lowbattery"),
        QStringLiteral("context.timeofday"),
        QStringLiteral("user.pet"), QStringLiteral("user.toss"),
    };
```

- [ ] **Step 4: Run test to verify it passes**

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/PersonaEngine.cpp tests/test_persona_engine.cpp
git commit -m "feat(persona): pool-tier lines for context.* and user.pet/toss"
```

---

### Task 3: Memory digest injection into OnDemand prompts

**Files:**
- Modify: `src/PersonaEngine.cpp` (`resolveOnDemand` shareMemory block, ~lines 187-199)
- Test: `tests/test_persona_engine.cpp`

- [ ] **Step 1: Write the failing test**

Add (inside the `#ifdef SEELIE_HAS_QHTTPSERVER` block):

```cpp
    void testDigestInjectionInOnDemandPrompt()
    {
        QByteArray capturedBody;
        QHttpServer server;
        server.route("/chat/completions", [&capturedBody](const QHttpServerRequest &req) {
            capturedBody = req.body();
            return QHttpServerResponse(QJsonDocument::fromJson(
                R"({"choices":[{"message":{"content":"ok"}}],"usage":{"prompt_tokens":1,"completion_tokens":1}})").object());
        });
        auto tcp = std::make_unique<QTcpServer>();
        QVERIFY(tcp->listen(QHostAddress::LocalHost, 0));
        const quint16 port = tcp->serverPort();
        QVERIFY(server.bind(tcp.release()));

        MemoryManager mm(":memory:");
        QVERIFY(mm.isValid());
        QVERIFY(mm.recordEpisode(QStringLiteral("session"), QStringLiteral("episodetext-xyz")) >= 0);

        ConfigManager cfg;
        cfg.load();
        cfg.setPersonaEnabled(true);
        cfg.setShareMemoryWithAi(true);
        cfg.setLLMProfiles({ { "p", LLMProfile::Protocol::OpenAIChat,
                               QStringLiteral("http://127.0.0.1:%1").arg(port), "k", "m" } });
        cfg.setPersonaProfile("p");
        PersonaEngine engine(&mm, &cfg);
        engine.setActivePackId("pack");
        engine.setPersonaHash("h");

        QSignalSpy spy(&engine, &PersonaEngine::tipUpgraded);
        engine.resolve("session.start", {});
        QVERIFY(spy.wait(3000));
        QVERIFY(capturedBody.contains("episodetext-xyz"));   // digest made it into the prompt

        // Gate off: digest must NOT be sent.
        capturedBody.clear();
        cfg.setShareMemoryWithAi(false);
        engine.resolve("session.start", {});
        QVERIFY(spy.wait(3000));
        QVERIFY(!capturedBody.contains("episodetext-xyz"));
    }
```

Note: `MemoryManager::recordEpisode` returns the row id (check signature — `qint64`); `setShareMemoryWithAi` exists on ConfigManager.

- [ ] **Step 2: Run test to verify it fails**

Expected: FAIL — prompt lacks the digest.

- [ ] **Step 3: Implement**

`src/PersonaEngine.cpp` — in `resolveOnDemand`, extend the `shareMemory && m_memory` block (after the bio append):

```cpp
        // Spec 4: the pet's memory digest (bond, affection, similarity-ranked
        // episodes) joins the prompt behind the same opt-in gate.
        if (m_memory->isValid()) {
            const QString digest = m_memory->memoryDigest();
            if (!digest.isEmpty()) {
                userPrompt += QStringLiteral("\nMemory:\n%1").arg(digest);
            }
        }
```

- [ ] **Step 4: Run test to verify it passes**

Expected: PASS (both gated directions).

- [ ] **Step 5: Commit**

```bash
git add src/PersonaEngine.cpp tests/test_persona_engine.cpp
git commit -m "feat(persona): inject memoryDigest into on-demand prompts (opt-in)"
```

---

### Task 4: Session-end summary + digest embedding

**Files:**
- Modify: `src/PersonaEngine.h` (+ `fireOnDemand`, `requestSessionSummary`)
- Modify: `src/PersonaEngine.cpp` (extract `fireOnDemand` from `resolveOnDemand`'s generate+callback; add `requestSessionSummary`)
- Modify: `src/mainwindow.h` (+ `showSessionSummaryBubble`)
- Modify: `src/mainwindow.cpp` (session.end branch; `showSessionSummaryBubble`)
- Modify: `assets/i18n/tips.en.json`, `assets/i18n/tips.zh_CN.json` (`session.summary` message)
- Test: `tests/test_persona_engine.cpp`

- [ ] **Step 1: Write the failing tests**

Add (QHttpServer block):

```cpp
    void testSessionSummaryPromptAndUpgrade()
    {
        QByteArray capturedBody;
        QHttpServer server;
        server.route("/chat/completions", [&capturedBody](const QHttpServerRequest &req) {
            capturedBody = req.body();
            return QHttpServerResponse(QJsonDocument::fromJson(
                R"({"choices":[{"message":{"content":"2 hours, 42 edits, one heroic save."}}],"usage":{"prompt_tokens":1,"completion_tokens":1}})").object());
        });
        auto tcp = std::make_unique<QTcpServer>();
        QVERIFY(tcp->listen(QHostAddress::LocalHost, 0));
        const quint16 port = tcp->serverPort();
        QVERIFY(server.bind(tcp.release()));

        MemoryManager mm(":memory:");
        ConfigManager cfg;
        cfg.load();
        cfg.setPersonaEnabled(true);
        cfg.setShareMemoryWithAi(false);   // summary must not require memory sharing
        cfg.setLLMProfiles({ { "p", LLMProfile::Protocol::OpenAIChat,
                               QStringLiteral("http://127.0.0.1:%1").arg(port), "k", "m" } });
        cfg.setPersonaProfile("p");
        PersonaEngine engine(&mm, &cfg);
        engine.setActivePackId("pack");
        engine.setPersonaHash("h");

        QSignalSpy spy(&engine, &PersonaEngine::tipUpgraded);
        const quint64 id = engine.requestSessionSummary(QStringLiteral("2h 5m, 42 events"));
        QVERIFY(id != 0);
        QVERIFY(spy.wait(3000));
        QVERIFY(capturedBody.contains("2h 5m, 42 events"));
        QVERIFY(capturedBody.contains("Summarize"));
        QCOMPARE(spy.first()[0].value<quint64>(), id);
        QCOMPARE(spy.first()[1].toString(), QString("2 hours, 42 edits, one heroic save."));
    }

    void testSessionSummaryOfflineReturnsZero()
    {
        MemoryManager mm(":memory:");
        ConfigManager cfg;
        cfg.load();
        cfg.setPersonaEnabled(false);   // persona off → no LLM call, id 0
        PersonaEngine engine(&mm, &cfg);
        QCOMPARE(engine.requestSessionSummary(QStringLiteral("2h 5m, 42 events")), quint64(0));
    }

    void testSessionSummaryCatalogEntry()
    {
        // The offline template must exist in the bundled catalog.
        const auto tip = TipsCatalog::instance().message(QStringLiteral("session.summary"));
        QVERIFY(!tip.title.isEmpty());
        QVERIFY(tip.body.contains(QStringLiteral("{summary}")));
    }
```

- [ ] **Step 2: Run test to verify it fails**

Expected: COMPILE FAIL (`requestSessionSummary` missing) / catalog FAIL.

- [ ] **Step 3a: Implement — fireOnDemand extraction**

`src/PersonaEngine.h` — private section, after `resolveOnDemand`:

```cpp
    /// Shared OnDemand lifecycle: requestId allocation, generate call, and
    /// the stale/fail/truncate callback. Used by resolveOnDemand and
    /// requestSessionSummary (Spec 4 dedup).
    quint64 fireOnDemand(const QString &systemPrompt, const QString &userPrompt);
```

Public section, after `resolve`:

```cpp
    /// Spec 4: fire an LLM session summary. Returns requestId (tipUpgraded
    /// will carry it), or 0 when persona/provider is off (caller shows the
    /// deterministic template instead).
    quint64 requestSessionSummary(const QString &statsLine);
```

`src/PersonaEngine.cpp` — extract the generate+callback from `resolveOnDemand` into `fireOnDemand` (move the requestId allocation + `m_provider.generate(...)` + the entire callback lambda body verbatim), so `resolveOnDemand` ends with:

```cpp
    return { fallbackTip(eventName), fireOnDemand(systemPrompt, userPrompt) };
```

New method:

```cpp
quint64 PersonaEngine::requestSessionSummary(const QString &statsLine)
{
    if (!m_config || !m_config->personaEnabled()
        || m_config->personaProfile().isEmpty() || !m_provider.isConfigured()) {
        return 0;
    }
    const QString lang = localeToHuman(m_config->language());
    QString userPrompt = QStringLiteral("Session: %1\n").arg(statsLine);
    if (m_config->shareMemoryWithAi()) {
        QStringList recent;
        for (const QString &e : m_eventWindow) recent << e;
        if (!recent.isEmpty()) {
            userPrompt += QStringLiteral("Recent events: %1\n")
                              .arg(recent.join(QStringLiteral(", ")));
        }
        if (m_memory && m_memory->isValid()) {
            const QString digest = m_memory->memoryDigest();
            if (!digest.isEmpty()) {
                userPrompt += QStringLiteral("Memory:\n%1\n").arg(digest);
            }
        }
    }
    userPrompt += QStringLiteral("Summarize this work session in-character.");
    const QString systemPrompt = QStringLiteral(
        "You are a desktop pet companion to a software developer. "
        "Reply with ONE short sentence in %1. Do not add quotes, "
        "translation, or commentary — just the sentence itself.").arg(lang);
    return fireOnDemand(systemPrompt, userPrompt);
}

quint64 PersonaEngine::fireOnDemand(const QString &systemPrompt, const QString &userPrompt)
{
    const quint64 requestId = m_nextRequestId++;
    // Capture current pack/hash by value so we can detect stale callbacks
    // if the user switches packs while the LLM request is in flight.
    const QString capturedPackId = m_activePackId;
    const QString capturedHash   = m_personaHash;

    m_provider.generate(systemPrompt, userPrompt,
        [this, requestId, capturedPackId, capturedHash](LLMResult r) {
            // Bail silently if the pack or persona hash changed mid-flight.
            if (capturedPackId != m_activePackId || capturedHash != m_personaHash) {
                ++m_stats.ondemandStale;
                return;
            }

            if (!r.ok || r.text.trimmed().isEmpty()) {
                ++m_stats.ondemandFail;
                m_stats.lastError = r.error;
                if (m_memory) m_memory->increment(QStringLiteral("stats.persona.ondemand.fail"));
                emit tipUpgradeFailed(requestId);
                return;
            }
            ++m_stats.ondemandOk;
            m_stats.tokensIn  += r.tokensIn;
            m_stats.tokensOut += r.tokensOut;
            m_stats.lastError.clear();
            if (m_memory) {
                m_memory->increment(QStringLiteral("stats.persona.ondemand.ok"));
                m_memory->increment(QStringLiteral("stats.persona.tokens.in"),  r.tokensIn);
                m_memory->increment(QStringLiteral("stats.persona.tokens.out"), r.tokensOut);
            }
            QString t = r.text.trimmed();
            if (t.length() > PersonaPool::MAX_TIP_CHARS) t.truncate(PersonaPool::MAX_TIP_CHARS);
            emit tipUpgraded(requestId, t);
        });

    return requestId;
}
```

(This is the exact generate+callback code moved out of `resolveOnDemand` — requestId allocation, stale capture, and the full callback body, byte-identical to what was there.)

- [ ] **Step 3b: Implement — MainWindow session.end**

In `onEventForMemory`'s session.end branch, inside the `≥30 min` block after the episode recording:

```cpp
            // Spec 4: spoken session summary — deterministic template now,
            // LLM upgrade async; plus one digest embedding so FUTURE
            // memoryDigest() calls rank episodes by similarity to this one.
            showSessionSummaryBubble(text);
            if (m_embeddingService) {
                m_embeddingService->requestDigestEmbedding(
                    text + QLatin1Char('\n') + m_memory->memoryDigest());
            }
```

(`text` is the existing episode stats line — reuse it.)

`src/mainwindow.h` — private methods, near `showTouchBubble`:

```cpp
    /// Spec 4: session-end bubble — template body now; LLM summary upgrades
    /// it via the activeBubble machinery when persona is configured.
    void showSessionSummaryBubble(const QString &statsLine);
```

`src/mainwindow.cpp` — new method after `showTouchBubble`:

```cpp
void MainWindow::showSessionSummaryBubble(const QString &statsLine)
{
    if (!m_tipWidget) return;
    const auto entry = TipsCatalog::instance().message(QStringLiteral("session.summary"));
    QString body = entry.body;
    if (body.isEmpty()) {
        body = QStringLiteral("Session wrapped: {summary}");  // catalog-missing fallback
    }
    body.replace(QStringLiteral("{summary}"), statsLine);
    const QString title = entry.title.isEmpty()
        ? QStringLiteral("Session ended") : entry.title;

    quint64 requestId = 0;
    if (m_personaEngine && m_config && m_config->personaEnabled()) {
        requestId = m_personaEngine->requestSessionSummary(statsLine);
    }
    m_activeBubbleRequestId = requestId;
    m_activeBubbleFallbackBody = body;
    m_tipWidget->showBubble(title, body, TipWidget::TipBubble);

    // TTS policy mirrors the event-route listener: speak now only when no
    // upgrade is in flight; otherwise onTipUpgraded/Failed handles it.
    const bool ttsReady = m_ttsEngine && m_config && m_config->ttsEnabled()
        && m_config->displayMode() != ConfigManager::DisplayMode::Ecg;
    if (ttsReady && requestId == 0) {
        m_ttsEngine->speak(body);
    }
}
```

- [ ] **Step 3c: Implement — JSON entries**

`assets/i18n/tips.en.json`, inside `"messages"`:

```json
    "session.summary":     {"title": "Session ended",       "body": "Session wrapped: {summary}"}
```

`assets/i18n/tips.zh_CN.json`, inside `"messages"`:

```json
    "session.summary":     {"title": "会话结束",             "body": "本次会话：{summary}"}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build --target test_persona_engine && ./build/tests/test_persona_engine`
Expected: PASS all. Also full build + `ctest --test-dir build 2>&1 | tail -3` (19/19).

- [ ] **Step 5: Commit**

```bash
git add src/PersonaEngine.h src/PersonaEngine.cpp src/mainwindow.h src/mainwindow.cpp assets/i18n/tips.en.json assets/i18n/tips.zh_CN.json tests/test_persona_engine.cpp
git commit -m "feat(persona): session-end LLM summary + template fallback + digest embedding"
```

---

### Task 5: Touch bubbles persona-aware

**Files:**
- Modify: `src/mainwindow.cpp` (`showTouchBubble`)
- Test: none new (widget wiring; suites must stay green)

- [ ] **Step 1: Implement — replace showTouchBubble body**

```cpp
void MainWindow::showTouchBubble(const QString &gesture)
{
    if (!m_tipWidget) return;
    // Title stays from the canned pool (per-gesture flavor); body prefers a
    // persona-resolved line (pool hit) or an OnDemand upgrade in flight.
    const auto canned = TipsCatalog::instance().touchLine(gesture);
    QString body = canned.body;
    quint64 requestId = 0;
    if (m_personaEngine && m_config && m_config->personaEnabled()) {
        PersonaEngine::Resolved r = m_personaEngine->resolve(
            QStringLiteral("user.") + gesture, QJsonObject{});
        if (!r.text.isEmpty()) body = r.text;
        requestId = r.requestId;
    }
    if (body.isEmpty()) return;
    m_activeBubbleRequestId = requestId;
    m_activeBubbleFallbackBody = body;
    m_tipWidget->showBubble(canned.title, body, TipWidget::TipBubble);

    const bool ttsReady = m_ttsEngine && m_config && m_config->ttsEnabled()
        && m_config->displayMode() != ConfigManager::DisplayMode::Ecg;
    if (ttsReady && requestId == 0) {
        m_ttsEngine->speak(body);
    }
}
```

Notes: `user.pet`/`user.toss` are pool-tier after Task 2 — `resolve()` returns a pool line or the touch-aware fallbackTip (same content stream as the old direct `touchLine`, so offline behavior is unchanged). An empty canned title is impossible (pools have titles); no extra fallback needed.

- [ ] **Step 2: Build + verify**

Run: `cmake --build build -j 10` (clean) && `ctest --test-dir build 2>&1 | tail -3` (19/19).

- [ ] **Step 3: Commit**

```bash
git add src/mainwindow.cpp
git commit -m "feat(touch): persona-resolved touch bubble bodies with canned fallback"
```

---

### Task 6: Full verification + TODO.md + errata

**Files:**
- Modify: `TODO.md`
- Modify: `docs/superpowers/plans/2026-07-17-ai-commentary.md` (append errata)
- Test: whole suite

- [ ] **Step 1: Full build + suite**

Run: `cmake --build build -j 10 && ctest --test-dir build`
Expected: clean; 19/19. Named pre-existing flakes (test_system_context UDP, test_pet_state_machine timing) → rerun once if hit (`ctest --test-dir build --rerun-failed --output-on-failure`); anything else = STOP, report BLOCKED.

- [ ] **Step 2: TODO.md**

§2 Spec 4 bullet → `[x]` SHIPPED 2026-07-17 on branch `ai-commentary` (pending merge): digest injection behind shareMemoryWithAi, pool-tier for context/touch, touch bubble upgrades, session-end LLM summary with deterministic template fallback, digest embedding 1/session. Program memory→senses→touch→commentary complete. Suite green. Parked: recall UI, habit learning, RemoteMemoryBackend, momentum glide, real-endpoint smoke.

Also update §1's embedding smoke-test bullet to note Spec 4 wired `requestDigestEmbedding` (production smoke still pending user's profile/quota).

- [ ] **Step 3: Errata + commit**

Append an "Execution Errata" section to this plan doc recording anything that deviated during execution (fill at the end — see Self-Review Notes below for the seed entries).

```bash
git add TODO.md docs/superpowers/plans/2026-07-17-ai-commentary.md
git commit -m "docs: mark Spec 4 (AI commentary) shipped — program complete"
```

---

## Self-Review Notes (seed for the errata section)

- **Spec coverage:** fallbackTip touch-aware (T1) · pool expansion (T2) · digest injection (T3) · session summary + embedding (T4) · touch bubbles (T5) · verification (T6). user.hover silent by MainWindow never calling resolve for it (Spec 3 emits no bubble path) ✓.
- **Type consistency:** `PersonaEngine::Resolved{text, requestId}` reused; `fireOnDemand` returns quint64 requestId; `requestSessionSummary(const QString &) -> quint64`; `showSessionSummaryBubble(const QString &)`; `showTouchBubble(const QString &)` unchanged signature.
- **Deliberate deviations from spec text:** (a) fireOnDemand extraction added for DRY (spec didn't name it; the generate+callback would otherwise be duplicated); (b) summary TTS policy mirrors the event-route listener exactly (spec said "like any other tip upgrade" — this is the concrete shape); (c) digest-embedding context text = episode stats line + current digest (spec §5 said statsLine + memoryDigest()).
- **Known untested-by-automation areas:** MainWindow bubble wiring (T4/T5 widget-level; persona-engine level covered by QHttpServer tests).
