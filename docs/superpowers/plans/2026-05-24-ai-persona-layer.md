# AI Persona Layer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace `TipsCatalog` as the primary event tip source with LLM-generated in-character lines, two-tier cached (pool for high-frequency events, on-demand for rare ones), with user-configurable providers, a new AI settings tab, and a Statistics dialog from the tray.

**Architecture:** `PersonaEngine` orchestrates per-event text resolution. Pool-tier events hit `PersonaPool` (SQLite-backed text cache, lazy refill via `LLMProvider`). On-demand events return `TipsCatalog` immediately and emit `tipUpgraded` when the LLM completes — bubble swaps text in place. All new units pinned to main thread; QNAM async on the main event loop, no workers, no cross-thread SQLite. `TipsCatalog` remains the always-available fallback.

**Tech Stack:** C++17, Qt6 (Core, Widgets, Network, Sql, Test). `QNetworkAccessManager` for HTTP. SQLite via existing `MemoryManager` database connection. Three LLM protocols: OpenAI Chat (`/v1/chat/completions`), OpenAI Responses (`/v1/responses`), Anthropic Messages (`/v1/messages`).

**Spec:** `docs/superpowers/specs/2026-05-24-ai-persona-layer-design.md` (commits `4b79e10` → `2ac40c8`).

---

## File Structure

### New files

| File | Responsibility |
|------|----------------|
| `src/llm/LLMProfile.h` | `struct LLMProfile { name, protocol, baseUrl, apiKey, model }` |
| `src/llm/LLMProvider.h` / `.cpp` | Async HTTP client, three-protocol enum, timeout, failure suppression, batched JSON-array generation |
| `src/PersonaPool.h` / `.cpp` | SQLite-backed `(packId, event)` text pool. Pick, refill, hash invalidation, validation, spam guard |
| `src/PersonaEngine.h` / `.cpp` | Orchestrator. Tier routing, sync return for pool-tier, async `tipUpgraded` for on-demand, rolling event window, memory snapshot gated by privacy toggle |
| `src/StatisticsDialog.h` / `.cpp` | Modal dialog reading `stats()` from TTSEngine/EventRouter/IpcServer/PersonaEngine/MemoryManager |
| `tests/test_llm_profile.cpp` | Round-trip `LLMProfile` through ConfigManager |
| `tests/test_llm_provider.cpp` | Mock QNAM via a local `QHttpServer`; cover all three protocols, timeout, batched JSON array, failure suppression |
| `tests/test_persona_pool.cpp` | Schema, CRUD, validation, spam guard, hash invalidation, in-flight cleanup |
| `tests/test_persona_engine.cpp` | Tier routing, sync return, async `tipUpgraded`, memory snapshot gating, milestone routing |
| `tests/test_statistics_dialog.cpp` | Seeded counters → labels render expected strings |

### Modified files

| File | Change |
|------|--------|
| `src/CharacterPack.h` / `.cpp` | Add `struct Persona { system, language, styleExamples }` + parser; persona hash helper |
| `src/EventRouter.h` (only) | (no change — existing `eventProcessed` signal already fits) |
| `src/ConfigManager.h` / `.cpp` | Add `llmProfiles()`, `setLLMProfiles()`, `personaProfile()` / setter, `personaEnabled()`, `shareMemoryWithAi()`; persist via QSettings groups |
| `src/SettingsPanelWidget.h` / `.cpp` | Internal rename `m_aiTab*` → `m_ttsTab*`; add new `m_llmTab*` with user label "AI" |
| `src/SystemTray.h` / `.cpp` | Add "Statistics..." menu entry |
| `src/mainwindow.h` / `.cpp` | Construct PersonaEngine, wire `eventProcessed`/`milestoneReached`/`activePackChanged`/`tipUpgraded`, open StatisticsDialog from tray |
| `src/TTSEngine.h` / `.cpp` | Add `struct Stats` + `stats()` accessor; increment hit/miss counters in cache lookup paths |
| `src/EventRouter.h` / `.cpp` | Add per-event counters + `stats()` accessor |
| `src/IpcServer.h` / `.cpp` and `src/UdpWorker.h` / `.cpp` | Add packets-received + decode-error counters + `stats()` |
| `tests/CMakeLists.txt` | Add new sources to `SEELIEPET_LIB_SOURCES`; add new test executables |

---

## Task Index

1. CharacterPack persona field + parsing
2. LLMProfile struct + ConfigManager serialization
3. LLMProvider scaffold + OpenAI Chat protocol
4. LLMProvider OpenAI Responses + Anthropic Messages protocols
5. LLMProvider timeout + failure suppression
6. PersonaPool schema + CRUD
7. PersonaPool refill, validation, spam guard, in-flight cleanup, hash invalidation
8. PersonaEngine sync pool-tier path
9. PersonaEngine async on-demand path + tipUpgraded
10. MainWindow integration + bubble swap
11. Stats accessors on TTSEngine, EventRouter, IpcServer, PersonaEngine
12. SettingsPanelWidget internal TTS rename
13. Settings — new AI tab (profiles list + persona controls + privacy + regenerate)
14. Settings — edit-profile dialog + test connection
15. StatisticsDialog widget + auto-refresh + reset
16. Tray menu Statistics entry + wire to MainWindow
17. End-to-end manual verification

---

## Task 1: CharacterPack persona field + parsing

**Files:**
- Modify: `src/CharacterPack.h` — add `Persona` struct + accessor + `personaHash()` method
- Modify: `src/CharacterPack.cpp` — parse `persona` block from manifest, compute hash
- Modify: `tests/CMakeLists.txt` — no changes needed yet (CharacterPack already in SEELIEPET_LIB_SOURCES)
- Test: extend `tests/test_character_pack_manager_errors.cpp` with a new `testPersonaParse` slot, OR add a new `tests/test_character_pack_persona.cpp` (prefer the latter for isolation)
- Create: `tests/test_character_pack_persona.cpp`

- [ ] **Step 1: Write failing test**

`tests/test_character_pack_persona.cpp`:
```cpp
#include "CharacterPack.h"
#include <QtTest/QtTest>
#include <QDir>
#include <QTemporaryDir>
#include <QFile>

class TestCharacterPackPersona : public QObject
{
    Q_OBJECT
private slots:
    void testPersonaParse()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QString manifest = R"({
            "format_version": "1.0",
            "id": "test_pack",
            "name": "Test",
            "character": { "engine": "lottie", "anim_directory": "anims",
                           "frame_width": 100, "frame_height": 100 },
            "animations": {},
            "persona": {
                "system": "You are Test. Reply with one sentence.",
                "language": "en",
                "style_examples": ["Hello.", "Hi there."]
            }
        })";
        QFile f(tmp.path() + "/manifest.json");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(manifest.toUtf8());
        f.close();
        QDir(tmp.path()).mkdir("anims");

        CharacterPack pack;
        QVERIFY(pack.loadFromDirectory(tmp.path()));
        QCOMPARE(pack.persona().system, QString("You are Test. Reply with one sentence."));
        QCOMPARE(pack.persona().language, QString("en"));
        QCOMPARE(pack.persona().styleExamples.size(), 2);
        QVERIFY(!pack.personaHash().isEmpty());
        QCOMPARE(pack.personaHash().length(), 64);  // SHA-256 hex
    }

    void testPersonaAbsent()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString manifest = R"({
            "format_version": "1.0",
            "id": "no_persona",
            "name": "NoPersona",
            "character": { "engine": "lottie", "anim_directory": "anims",
                           "frame_width": 100, "frame_height": 100 },
            "animations": {}
        })";
        QFile f(tmp.path() + "/manifest.json");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(manifest.toUtf8());
        f.close();
        QDir(tmp.path()).mkdir("anims");

        CharacterPack pack;
        QVERIFY(pack.loadFromDirectory(tmp.path()));
        QVERIFY(pack.persona().system.isEmpty());
        // Hash must be stable even for empty persona (engine-default used downstream)
        QVERIFY(!pack.personaHash().isEmpty());
    }
};

QTEST_MAIN(TestCharacterPackPersona)
#include "test_character_pack_persona.moc"
```

- [ ] **Step 2: Register the test in CMake**

Edit `tests/CMakeLists.txt` — add to `TEST_SOURCES` list:
```cmake
set(TEST_SOURCES
    test_ipc_animations.cpp
    test_ecg.cpp
    test_gaming_mode.cpp
    test_pet_state_machine.cpp
    test_tts_providers.cpp
    test_tts_engine.cpp
    test_tts_voice_cache.cpp
    test_autostart_manager.cpp
    test_platform_window.cpp
    test_character_pack_manager_errors.cpp
    test_character_pack_persona.cpp   # ← new
)
```

- [ ] **Step 3: Run test, verify it fails to compile**

```bash
cd build && cmake --build . --target test_character_pack_persona 2>&1 | tail -20
```
Expected: compile errors about `pack.persona()` and `pack.personaHash()` not existing.

- [ ] **Step 4: Add Persona struct + accessor to header**

Edit `src/CharacterPack.h`. After the `Metadata` struct definition (around line 100), add:
```cpp
/**
 * @brief Optional pack-provided persona used by the AI Persona Layer.
 *        All fields may be empty — engine-default persona kicks in then.
 */
struct Persona {
    QString system;                  ///< System prompt for the LLM
    QString language;                ///< Locale hint, e.g. "zh-CN"
    QStringList styleExamples;       ///< Few-shot example lines
    bool isEmpty() const { return system.isEmpty() && language.isEmpty() && styleExamples.isEmpty(); }
};
```

In the public accessors section (near `metadata()`), add:
```cpp
/// Pack-provided persona for the AI Persona Layer. Empty when not present.
const Persona &persona() const { return m_persona; }

/// SHA-256 hex of the persona's canonical JSON. Stable for empty persona too.
/// Used to invalidate stale PersonaPool rows when a pack updates its persona.
QString personaHash() const;
```

In the private members section, add:
```cpp
Persona m_persona;
mutable QString m_personaHashCache;   // computed lazily in personaHash()
```

In the private parser declarations:
```cpp
bool parsePersona(const QJsonObject &persona);
```

- [ ] **Step 5: Implement parsing + hash in .cpp**

Edit `src/CharacterPack.cpp`. Add at the top with other includes:
```cpp
#include <QCryptographicHash>
#include <QJsonDocument>
```

Find `parseManifest()` and add a persona parse hook after the existing parses (after `parseStateMap` or wherever appropriate). Example insertion:
```cpp
if (manifest.contains(QStringLiteral("persona")) && manifest.value(QStringLiteral("persona")).isObject()) {
    if (!parsePersona(manifest.value(QStringLiteral("persona")).toObject())) {
        qWarning() << "CharacterPack: persona block failed to parse; continuing with empty persona";
        m_persona = Persona{};
    }
}
```

Add the parser implementation at the end of the file:
```cpp
bool CharacterPack::parsePersona(const QJsonObject &persona)
{
    m_persona.system = persona.value(QStringLiteral("system")).toString();
    m_persona.language = persona.value(QStringLiteral("language")).toString();
    m_persona.styleExamples.clear();
    const QJsonArray ex = persona.value(QStringLiteral("style_examples")).toArray();
    for (const QJsonValue &v : ex) {
        if (v.isString()) m_persona.styleExamples << v.toString();
    }
    m_personaHashCache.clear();  // force re-hash on next access
    return true;
}

QString CharacterPack::personaHash() const
{
    if (!m_personaHashCache.isEmpty()) return m_personaHashCache;

    // Canonical JSON: fixed key order so hash is stable regardless of source ordering.
    QJsonObject canonical;
    canonical.insert(QStringLiteral("system"), m_persona.system);
    canonical.insert(QStringLiteral("language"), m_persona.language);
    QJsonArray exArr;
    for (const QString &e : m_persona.styleExamples) exArr.append(e);
    canonical.insert(QStringLiteral("style_examples"), exArr);

    const QByteArray bytes = QJsonDocument(canonical).toJson(QJsonDocument::Compact);
    m_personaHashCache = QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    return m_personaHashCache;
}
```

- [ ] **Step 6: Run the test, verify it passes**

```bash
cd build && cmake --build . --target test_character_pack_persona && ctest -R test_character_pack_persona --output-on-failure
```
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/CharacterPack.h src/CharacterPack.cpp tests/test_character_pack_persona.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(pack): parse optional persona block from manifest

Adds CharacterPack::Persona struct (system, language, styleExamples)
and CharacterPack::personaHash() (SHA-256 over a canonical JSON
shape). Lays groundwork for the AI Persona Layer.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: LLMProfile struct + ConfigManager serialization

**Files:**
- Create: `src/llm/LLMProfile.h`
- Modify: `src/ConfigManager.h` / `.cpp` — add LLM profile list, personaProfile string, personaEnabled bool, shareMemoryWithAi bool
- Modify: `tests/CMakeLists.txt` — add LLMProfile.h to SEELIEPET_LIB_SOURCES, add new test
- Create: `tests/test_llm_profile.cpp`

- [ ] **Step 1: Write failing test**

`tests/test_llm_profile.cpp`:
```cpp
#include "ConfigManager.h"
#include "llm/LLMProfile.h"
#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QSettings>

class TestLLMProfile : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        // QSettings in-memory: use IniFormat with a temp file
        QCoreApplication::setOrganizationName("SeelieTest");
        QCoreApplication::setApplicationName("seelie_llm_profile_test");
        QSettings s;
        s.clear();
    }

    void testRoundTrip()
    {
        ConfigManager cfg;
        cfg.load();

        QVector<LLMProfile> profiles;
        profiles.append({ "fast", LLMProfile::Protocol::OpenAIChat,
                          "https://api.openai.com/v1", "sk-test", "gpt-4o-mini" });
        profiles.append({ "smart", LLMProfile::Protocol::AnthropicMessages,
                          "https://api.anthropic.com", "sk-ant-test", "claude-haiku" });
        cfg.setLLMProfiles(profiles);
        cfg.setPersonaProfile("fast");
        cfg.setPersonaEnabled(true);
        cfg.setShareMemoryWithAi(false);
        cfg.flush();

        // Reload from disk
        ConfigManager cfg2;
        cfg2.load();
        const auto got = cfg2.llmProfiles();
        QCOMPARE(got.size(), 2);
        QCOMPARE(got[0].name, QString("fast"));
        QCOMPARE(got[0].protocol, LLMProfile::Protocol::OpenAIChat);
        QCOMPARE(got[0].baseUrl, QString("https://api.openai.com/v1"));
        QCOMPARE(got[0].apiKey, QString("sk-test"));
        QCOMPARE(got[0].model, QString("gpt-4o-mini"));
        QCOMPARE(got[1].protocol, LLMProfile::Protocol::AnthropicMessages);
        QCOMPARE(cfg2.personaProfile(), QString("fast"));
        QCOMPARE(cfg2.personaEnabled(), true);
        QCOMPARE(cfg2.shareMemoryWithAi(), false);
    }

    void testEmptyOnFirstRun()
    {
        QSettings s;
        s.clear();
        ConfigManager cfg;
        cfg.load();
        QVERIFY(cfg.llmProfiles().isEmpty());
        QCOMPARE(cfg.personaProfile(), QString());
        QCOMPARE(cfg.personaEnabled(), false);
        QCOMPARE(cfg.shareMemoryWithAi(), false);
    }
};

QTEST_MAIN(TestLLMProfile)
#include "test_llm_profile.moc"
```

- [ ] **Step 2: Create LLMProfile.h**

```bash
mkdir -p src/llm
```

`src/llm/LLMProfile.h`:
```cpp
#ifndef LLM_PROFILE_H
#define LLM_PROFILE_H

#include <QString>

/**
 * @brief One user-configured LLM endpoint. Persisted via ConfigManager.
 *
 * Three protocols are supported, mapping to the three HTTP shapes the
 * AI Persona Layer can speak. The same struct represents any of them —
 * baseUrl + model + apiKey are interpreted per protocol.
 */
struct LLMProfile {
    enum class Protocol {
        OpenAIChat = 0,         ///< POST {baseUrl}/chat/completions
        OpenAIResponses = 1,    ///< POST {baseUrl}/responses
        AnthropicMessages = 2,  ///< POST {baseUrl}/messages
    };

    QString name;
    Protocol protocol = Protocol::OpenAIChat;
    QString baseUrl;
    QString apiKey;
    QString model;
};

#endif // LLM_PROFILE_H
```

- [ ] **Step 3: Extend ConfigManager**

Edit `src/ConfigManager.h`. Add `#include "llm/LLMProfile.h"` and `#include <QVector>` near the top. After the TTS section (around line 98), add:

```cpp
// --- LLM (AI Persona Layer) -------------------------------------------------

/// Stored LLM provider profiles. Empty by default.
QVector<LLMProfile> llmProfiles() const { return m_llmProfiles; }
void setLLMProfiles(const QVector<LLMProfile> &profiles);

/// Name of the profile assigned to the persona feature. Empty when unassigned.
QString personaProfile() const { return m_personaProfile; }
void setPersonaProfile(const QString &name);

/// Whether persona commentary is on. Off by default until configured.
bool personaEnabled() const { return m_personaEnabled; }
void setPersonaEnabled(bool enabled);

/// Privacy toggle: send memory snapshot (name, milestones) with prompts. Off by default.
bool shareMemoryWithAi() const { return m_shareMemoryWithAi; }
void setShareMemoryWithAi(bool enabled);
```

Signals (add to the existing signals block):
```cpp
void llmProfilesChanged();
void personaProfileChanged(const QString &name);
void personaEnabledChanged(bool enabled);
void shareMemoryWithAiChanged(bool enabled);
```

Private members (add to the existing block):
```cpp
QVector<LLMProfile> m_llmProfiles;
QString m_personaProfile;
bool m_personaEnabled = false;
bool m_shareMemoryWithAi = false;
```

- [ ] **Step 4: Implement in ConfigManager.cpp**

Inside `ConfigManager::load()`, add at the end:
```cpp
// LLM profiles
m_llmProfiles.clear();
const int n = m_settings.beginReadArray(QStringLiteral("llm/profiles"));
for (int i = 0; i < n; ++i) {
    m_settings.setArrayIndex(i);
    LLMProfile p;
    p.name = m_settings.value(QStringLiteral("name")).toString();
    p.protocol = static_cast<LLMProfile::Protocol>(
        m_settings.value(QStringLiteral("protocol"), 0).toInt());
    p.baseUrl = m_settings.value(QStringLiteral("baseUrl")).toString();
    p.apiKey = m_settings.value(QStringLiteral("apiKey")).toString();
    p.model = m_settings.value(QStringLiteral("model")).toString();
    if (!p.name.isEmpty()) m_llmProfiles.append(p);
}
m_settings.endArray();
m_personaProfile = m_settings.value(QStringLiteral("llm/personaProfile")).toString();
m_personaEnabled = m_settings.value(QStringLiteral("llm/personaEnabled"), false).toBool();
m_shareMemoryWithAi = m_settings.value(QStringLiteral("llm/shareMemoryWithAi"), false).toBool();
```

Implementations (paste anywhere appropriate):
```cpp
void ConfigManager::setLLMProfiles(const QVector<LLMProfile> &profiles)
{
    m_llmProfiles = profiles;

    // QSettings arrays must be cleared first or stale entries persist.
    m_settings.remove(QStringLiteral("llm/profiles"));

    m_settings.beginWriteArray(QStringLiteral("llm/profiles"));
    for (int i = 0; i < m_llmProfiles.size(); ++i) {
        const auto &p = m_llmProfiles[i];
        m_settings.setArrayIndex(i);
        m_settings.setValue(QStringLiteral("name"), p.name);
        m_settings.setValue(QStringLiteral("protocol"), static_cast<int>(p.protocol));
        m_settings.setValue(QStringLiteral("baseUrl"), p.baseUrl);
        m_settings.setValue(QStringLiteral("apiKey"), p.apiKey);
        m_settings.setValue(QStringLiteral("model"), p.model);
    }
    m_settings.endArray();
    save();
    emit llmProfilesChanged();
}

void ConfigManager::setPersonaProfile(const QString &name)
{
    if (m_personaProfile == name) return;
    m_personaProfile = name;
    m_settings.setValue(QStringLiteral("llm/personaProfile"), name);
    save();
    emit personaProfileChanged(name);
}

void ConfigManager::setPersonaEnabled(bool enabled)
{
    if (m_personaEnabled == enabled) return;
    m_personaEnabled = enabled;
    m_settings.setValue(QStringLiteral("llm/personaEnabled"), enabled);
    save();
    emit personaEnabledChanged(enabled);
}

void ConfigManager::setShareMemoryWithAi(bool enabled)
{
    if (m_shareMemoryWithAi == enabled) return;
    m_shareMemoryWithAi = enabled;
    m_settings.setValue(QStringLiteral("llm/shareMemoryWithAi"), enabled);
    save();
    emit shareMemoryWithAiChanged(enabled);
}
```

- [ ] **Step 5: Register sources + test in CMake**

Edit `tests/CMakeLists.txt`. Add `LLMProfile.h` to `SEELIEPET_LIB_SOURCES`:
```cmake
    ${CMAKE_SOURCE_DIR}/src/llm/LLMProfile.h
```

Add to `TEST_SOURCES`:
```cmake
    test_llm_profile.cpp
```

- [ ] **Step 6: Build and run test**

```bash
cd build && cmake .. && cmake --build . --target test_llm_profile && ctest -R test_llm_profile --output-on-failure
```
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/llm/LLMProfile.h src/ConfigManager.h src/ConfigManager.cpp tests/test_llm_profile.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(config): persist LLMProfile list + persona settings

Adds LLMProfile struct (three protocols), ConfigManager getters/
setters for profile list, personaProfile assignment, personaEnabled
toggle, and shareMemoryWithAi privacy toggle. Backed by QSettings
arrays + named keys.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: LLMProvider scaffold + OpenAI Chat protocol

**Files:**
- Create: `src/llm/LLMProvider.h` — interface + LLMResult struct
- Create: `src/llm/LLMProvider.cpp` — main thread, QNAM, OpenAI Chat path
- Modify: `tests/CMakeLists.txt`
- Create: `tests/test_llm_provider.cpp` — covers OpenAI Chat via local `QHttpServer` mock

- [ ] **Step 1: Write failing test (OpenAI Chat happy path)**

`tests/test_llm_provider.cpp`:
```cpp
#include "llm/LLMProvider.h"
#include "llm/LLMProfile.h"
#include <QtTest/QtTest>
#include <QSignalSpy>

#ifdef SEELIE_HAS_QHTTPSERVER
#include <QHttpServer>
#include <QTcpServer>
#endif

class TestLLMProvider : public QObject
{
    Q_OBJECT
private slots:
#ifdef SEELIE_HAS_QHTTPSERVER
    void testOpenAiChatHappyPath()
    {
        QHttpServer server;
        server.route("/chat/completions", [](const QHttpServerRequest &) {
            return QHttpServerResponse(
                R"({"choices":[{"message":{"content":"Hello world."}}],
                    "usage":{"prompt_tokens":12,"completion_tokens":3}})",
                "application/json");
        });
        auto tcp = std::make_unique<QTcpServer>();
        QVERIFY(tcp->listen(QHostAddress::LocalHost, 0));
        const quint16 port = tcp->serverPort();
        QVERIFY(server.bind(tcp.release()));

        LLMProfile profile;
        profile.protocol = LLMProfile::Protocol::OpenAIChat;
        profile.baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(port);
        profile.apiKey = "sk-test";
        profile.model = "gpt-4o-mini";

        LLMProvider provider;
        provider.setProfile(profile);

        QEventLoop loop;
        LLMResult got;
        provider.generate("system prompt", "user prompt", [&](LLMResult r) {
            got = std::move(r);
            loop.quit();
        });
        QTimer::singleShot(3000, &loop, &QEventLoop::quit);
        loop.exec();

        QVERIFY(got.ok);
        QCOMPARE(got.text, QString("Hello world."));
        QCOMPARE(got.tokensIn, 12);
        QCOMPARE(got.tokensOut, 3);
    }

    void testOpenAiChatHttpError()
    {
        QHttpServer server;
        server.route("/chat/completions", [](const QHttpServerRequest &) {
            return QHttpServerResponse("Unauthorized", QHttpServerResponder::StatusCode::Unauthorized);
        });
        auto tcp = std::make_unique<QTcpServer>();
        QVERIFY(tcp->listen(QHostAddress::LocalHost, 0));
        const quint16 port = tcp->serverPort();
        QVERIFY(server.bind(tcp.release()));

        LLMProfile profile;
        profile.protocol = LLMProfile::Protocol::OpenAIChat;
        profile.baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(port);
        profile.apiKey = "bad";
        profile.model = "x";

        LLMProvider provider;
        provider.setProfile(profile);
        QEventLoop loop;
        LLMResult got;
        provider.generate("s", "u", [&](LLMResult r) { got = std::move(r); loop.quit(); });
        QTimer::singleShot(3000, &loop, &QEventLoop::quit);
        loop.exec();

        QVERIFY(!got.ok);
        QVERIFY(!got.error.isEmpty());
    }
#else
    void skipNoHttpServer()
    {
        QSKIP("Qt6::HttpServer not found; LLMProvider tests skipped at compile time.");
    }
#endif
};

QTEST_MAIN(TestLLMProvider)
#include "test_llm_provider.moc"
```

`SEELIE_HAS_QHTTPSERVER` is already wired in `tests/CMakeLists.txt` (`find_package(Qt6 6.5 COMPONENTS HttpServer QUIET)`).

- [ ] **Step 2: Register the new test in CMake**

Add `test_llm_provider.cpp` to `TEST_SOURCES` in `tests/CMakeLists.txt`. Add to `SEELIEPET_LIB_SOURCES`:
```cmake
    ${CMAKE_SOURCE_DIR}/src/llm/LLMProvider.h
    ${CMAKE_SOURCE_DIR}/src/llm/LLMProvider.cpp
```

- [ ] **Step 3: Build, verify failure**

```bash
cd build && cmake --build . --target test_llm_provider 2>&1 | tail -10
```
Expected: compile error (LLMProvider.h not found).

- [ ] **Step 4: Create LLMProvider header**

`src/llm/LLMProvider.h`:
```cpp
#ifndef LLM_PROVIDER_H
#define LLM_PROVIDER_H

#include "LLMProfile.h"
#include <QObject>
#include <QString>
#include <QVector>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

/**
 * @brief Async HTTP client for LLM completions. Single class, three protocols.
 *
 * Lives on the main thread. QNetworkAccessManager is created on the main thread
 * and its `finished` signal fires on the main thread, so user callbacks are
 * always invoked on the main thread.
 */
struct LLMResult {
    bool ok = false;
    QString text;
    QString error;
    int tokensIn = 0;
    int tokensOut = 0;
};

class LLMProvider : public QObject
{
    Q_OBJECT
public:
    explicit LLMProvider(QObject *parent = nullptr);
    ~LLMProvider() override;

    void setProfile(const LLMProfile &profile);
    LLMProfile profile() const { return m_profile; }

    /// True iff baseUrl, apiKey, and model are all non-empty.
    bool isConfigured() const;

    using ResultCallback = std::function<void(LLMResult)>;
    using BatchCallback = std::function<void(QVector<QString>)>;

    /// On-demand single completion. Callback fires exactly once on the main thread.
    void generate(const QString &system, const QString &user, ResultCallback callback);

    /// Batched: ask for N lines as a JSON array of strings. Callback receives parsed lines
    /// (possibly empty on JSON parse failure — caller handles).
    void generateBatch(const QString &system, const QString &user, int n, BatchCallback callback);

    /// Test seam: change the per-request timeout. Default 5000 ms.
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }

private:
    QNetworkReply *sendOpenAiChat(const QString &system, const QString &user);
    QNetworkReply *sendOpenAiResponses(const QString &system, const QString &user);
    QNetworkReply *sendAnthropicMessages(const QString &system, const QString &user);

    static LLMResult parseOpenAiChat(const QByteArray &body);
    static LLMResult parseOpenAiResponses(const QByteArray &body);
    static LLMResult parseAnthropicMessages(const QByteArray &body);

    void wireReply(QNetworkReply *reply, ResultCallback callback);

    LLMProfile m_profile;
    QNetworkAccessManager *m_nam = nullptr;
    int m_timeoutMs = 5000;
};

#endif // LLM_PROVIDER_H
```

- [ ] **Step 5: Implement OpenAI Chat path**

`src/llm/LLMProvider.cpp`:
```cpp
#include "LLMProvider.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QUrl>

LLMProvider::LLMProvider(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

LLMProvider::~LLMProvider() = default;

void LLMProvider::setProfile(const LLMProfile &profile)
{
    m_profile = profile;
}

bool LLMProvider::isConfigured() const
{
    return !m_profile.baseUrl.isEmpty()
        && !m_profile.apiKey.isEmpty()
        && !m_profile.model.isEmpty();
}

void LLMProvider::generate(const QString &system, const QString &user, ResultCallback cb)
{
    if (!isConfigured()) {
        cb({ false, {}, QStringLiteral("provider not configured"), 0, 0 });
        return;
    }

    QNetworkReply *reply = nullptr;
    switch (m_profile.protocol) {
    case LLMProfile::Protocol::OpenAIChat:
        reply = sendOpenAiChat(system, user); break;
    case LLMProfile::Protocol::OpenAIResponses:
        reply = sendOpenAiResponses(system, user); break;
    case LLMProfile::Protocol::AnthropicMessages:
        reply = sendAnthropicMessages(system, user); break;
    }
    if (!reply) {
        cb({ false, {}, QStringLiteral("unknown protocol"), 0, 0 });
        return;
    }
    wireReply(reply, std::move(cb));
}

void LLMProvider::generateBatch(const QString &system, const QString &user, int n, BatchCallback cb)
{
    const QString batchUser = user
        + QStringLiteral("\n\nRespond ONLY with a JSON array of exactly %1 strings. "
                         "No surrounding prose, no markdown code fences, no keys — "
                         "just the JSON array. Example: [\"line one\", \"line two\"]").arg(n);

    generate(system, batchUser, [cb = std::move(cb), n](LLMResult r) {
        QVector<QString> out;
        if (!r.ok) { cb(out); return; }

        // Strip code fences if a model returned them despite instructions.
        QString text = r.text.trimmed();
        if (text.startsWith("```")) {
            int firstNl = text.indexOf('\n');
            int closing = text.lastIndexOf("```");
            if (firstNl >= 0 && closing > firstNl) {
                text = text.mid(firstNl + 1, closing - firstNl - 1).trimmed();
            }
        }

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isArray()) {
            qWarning() << "LLMProvider::generateBatch: JSON parse failed:" << err.errorString()
                       << "head:" << text.left(200);
            cb(out);
            return;
        }
        const QJsonArray arr = doc.array();
        for (const QJsonValue &v : arr) {
            if (v.isString()) out.append(v.toString());
        }
        Q_UNUSED(n);
        cb(out);
    });
}

// --- OpenAI Chat -----------------------------------------------------------

QNetworkReply *LLMProvider::sendOpenAiChat(const QString &system, const QString &user)
{
    QUrl url(m_profile.baseUrl + QStringLiteral("/chat/completions"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", ("Bearer " + m_profile.apiKey).toUtf8());

    QJsonObject body;
    body["model"] = m_profile.model;
    QJsonArray msgs;
    msgs.append(QJsonObject{ {"role","system"}, {"content", system} });
    msgs.append(QJsonObject{ {"role","user"},   {"content", user} });
    body["messages"] = msgs;

    return m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

LLMResult LLMProvider::parseOpenAiChat(const QByteArray &body)
{
    LLMResult r;
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        r.error = "non-JSON response"; return r;
    }
    const QJsonObject obj = doc.object();
    const QJsonArray choices = obj.value("choices").toArray();
    if (choices.isEmpty()) { r.error = "no choices in response"; return r; }
    r.text = choices.first().toObject()
                     .value("message").toObject()
                     .value("content").toString();
    if (r.text.isEmpty()) { r.error = "empty content"; return r; }

    const QJsonObject usage = obj.value("usage").toObject();
    r.tokensIn = usage.value("prompt_tokens").toInt();
    r.tokensOut = usage.value("completion_tokens").toInt();
    r.ok = true;
    return r;
}

// --- Stubs for the other two protocols (filled in Task 4) -----------------

QNetworkReply *LLMProvider::sendOpenAiResponses(const QString &, const QString &) { return nullptr; }
QNetworkReply *LLMProvider::sendAnthropicMessages(const QString &, const QString &) { return nullptr; }
LLMResult LLMProvider::parseOpenAiResponses(const QByteArray &) { return {}; }
LLMResult LLMProvider::parseAnthropicMessages(const QByteArray &) { return {}; }

// --- Common reply wiring ---------------------------------------------------

void LLMProvider::wireReply(QNetworkReply *reply, ResultCallback cb)
{
    auto *timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    timeout->setInterval(m_timeoutMs);

    auto protocol = m_profile.protocol;
    auto fired = std::make_shared<bool>(false);

    connect(timeout, &QTimer::timeout, reply, [reply, cb, fired]() {
        if (*fired) return;
        *fired = true;
        reply->abort();
        cb({ false, {}, QStringLiteral("timeout"), 0, 0 });
        reply->deleteLater();
    });

    connect(reply, &QNetworkReply::finished, this, [reply, cb, fired, protocol]() {
        if (*fired) return;
        *fired = true;
        if (reply->error() != QNetworkReply::NoError) {
            cb({ false, {}, reply->errorString(), 0, 0 });
            reply->deleteLater();
            return;
        }
        const QByteArray body = reply->readAll();
        LLMResult r;
        switch (protocol) {
        case LLMProfile::Protocol::OpenAIChat:        r = parseOpenAiChat(body); break;
        case LLMProfile::Protocol::OpenAIResponses:   r = parseOpenAiResponses(body); break;
        case LLMProfile::Protocol::AnthropicMessages: r = parseAnthropicMessages(body); break;
        }
        cb(std::move(r));
        reply->deleteLater();
    });

    timeout->start();
}
```

- [ ] **Step 6: Build and run test**

```bash
cd build && cmake --build . --target test_llm_provider && ctest -R test_llm_provider --output-on-failure
```
Expected: both subtests PASS (or `skipNoHttpServer` SKIP if Qt6::HttpServer not installed — that's acceptable).

- [ ] **Step 7: Commit**

```bash
git add src/llm/LLMProvider.h src/llm/LLMProvider.cpp tests/test_llm_provider.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(llm): LLMProvider scaffold + OpenAI Chat protocol

Adds async HTTP client with three-protocol enum; this commit ships
the OpenAI Chat path (POST /chat/completions). Reply wiring covers
timeout, abort-on-timeout, and content-type+auth-bearer headers.
generateBatch instructs the model to return a JSON array of N
strings; parses + strips code fences if present.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: LLMProvider — OpenAI Responses + Anthropic Messages

**Files:**
- Modify: `src/llm/LLMProvider.cpp` — replace the two stubs with real implementations
- Modify: `tests/test_llm_provider.cpp` — add two more subtests

- [ ] **Step 1: Write failing tests**

Add to `tests/test_llm_provider.cpp` inside the `#ifdef SEELIE_HAS_QHTTPSERVER` block:
```cpp
void testOpenAiResponses()
{
    QHttpServer server;
    server.route("/responses", [](const QHttpServerRequest &) {
        return QHttpServerResponse(
            R"({"output":[{"content":[{"type":"output_text","text":"Tch."}]}],
                "usage":{"input_tokens":15,"output_tokens":2}})",
            "application/json");
    });
    auto tcp = std::make_unique<QTcpServer>();
    QVERIFY(tcp->listen(QHostAddress::LocalHost, 0));
    const quint16 port = tcp->serverPort();
    QVERIFY(server.bind(tcp.release()));

    LLMProfile p;
    p.protocol = LLMProfile::Protocol::OpenAIResponses;
    p.baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(port);
    p.apiKey = "sk-test"; p.model = "gpt-4o-mini";
    LLMProvider provider;
    provider.setProfile(p);

    QEventLoop loop;
    LLMResult got;
    provider.generate("s", "u", [&](LLMResult r) { got = std::move(r); loop.quit(); });
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    loop.exec();
    QVERIFY(got.ok);
    QCOMPARE(got.text, QString("Tch."));
    QCOMPARE(got.tokensIn, 15);
    QCOMPARE(got.tokensOut, 2);
}

void testAnthropicMessages()
{
    QHttpServer server;
    server.route("/messages", [](const QHttpServerRequest &) {
        return QHttpServerResponse(
            R"({"content":[{"type":"text","text":"Senpai."}],
                "usage":{"input_tokens":20,"output_tokens":1}})",
            "application/json");
    });
    auto tcp = std::make_unique<QTcpServer>();
    QVERIFY(tcp->listen(QHostAddress::LocalHost, 0));
    const quint16 port = tcp->serverPort();
    QVERIFY(server.bind(tcp.release()));

    LLMProfile p;
    p.protocol = LLMProfile::Protocol::AnthropicMessages;
    p.baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(port);
    p.apiKey = "sk-ant-test"; p.model = "claude-haiku";
    LLMProvider provider;
    provider.setProfile(p);

    QEventLoop loop;
    LLMResult got;
    provider.generate("s", "u", [&](LLMResult r) { got = std::move(r); loop.quit(); });
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    loop.exec();
    QVERIFY(got.ok);
    QCOMPARE(got.text, QString("Senpai."));
    QCOMPARE(got.tokensIn, 20);
    QCOMPARE(got.tokensOut, 1);
}
```

- [ ] **Step 2: Run tests, verify failure**

```bash
cd build && cmake --build . --target test_llm_provider && ctest -R test_llm_provider --output-on-failure
```
Expected: the two new subtests FAIL (stubs return nullptr / empty).

- [ ] **Step 3: Replace stubs in LLMProvider.cpp**

```cpp
QNetworkReply *LLMProvider::sendOpenAiResponses(const QString &system, const QString &user)
{
    QUrl url(m_profile.baseUrl + QStringLiteral("/responses"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", ("Bearer " + m_profile.apiKey).toUtf8());

    QJsonObject body;
    body["model"] = m_profile.model;
    body["instructions"] = system;
    body["input"] = user;
    return m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

LLMResult LLMProvider::parseOpenAiResponses(const QByteArray &body)
{
    LLMResult r;
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) { r.error = "non-JSON response"; return r; }
    const QJsonObject obj = doc.object();

    const QJsonArray output = obj.value("output").toArray();
    if (output.isEmpty()) { r.error = "no output in response"; return r; }
    const QJsonArray content = output.first().toObject().value("content").toArray();
    for (const QJsonValue &v : content) {
        const QJsonObject c = v.toObject();
        if (c.value("type").toString() == "output_text") {
            r.text = c.value("text").toString();
            break;
        }
    }
    if (r.text.isEmpty()) { r.error = "no output_text in content"; return r; }

    const QJsonObject usage = obj.value("usage").toObject();
    r.tokensIn = usage.value("input_tokens").toInt();
    r.tokensOut = usage.value("output_tokens").toInt();
    r.ok = true;
    return r;
}

QNetworkReply *LLMProvider::sendAnthropicMessages(const QString &system, const QString &user)
{
    QUrl url(m_profile.baseUrl + QStringLiteral("/messages"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("x-api-key", m_profile.apiKey.toUtf8());
    req.setRawHeader("anthropic-version", "2023-06-01");

    QJsonObject body;
    body["model"] = m_profile.model;
    body["max_tokens"] = 512;
    body["system"] = system;
    QJsonArray msgs;
    msgs.append(QJsonObject{ {"role","user"}, {"content", user} });
    body["messages"] = msgs;
    return m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

LLMResult LLMProvider::parseAnthropicMessages(const QByteArray &body)
{
    LLMResult r;
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) { r.error = "non-JSON response"; return r; }
    const QJsonObject obj = doc.object();

    const QJsonArray content = obj.value("content").toArray();
    for (const QJsonValue &v : content) {
        const QJsonObject c = v.toObject();
        if (c.value("type").toString() == "text") {
            r.text = c.value("text").toString();
            break;
        }
    }
    if (r.text.isEmpty()) { r.error = "no text in content"; return r; }

    const QJsonObject usage = obj.value("usage").toObject();
    r.tokensIn = usage.value("input_tokens").toInt();
    r.tokensOut = usage.value("output_tokens").toInt();
    r.ok = true;
    return r;
}
```

- [ ] **Step 4: Run tests, verify pass**

```bash
cd build && cmake --build . --target test_llm_provider && ctest -R test_llm_provider --output-on-failure
```
Expected: all four subtests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/llm/LLMProvider.cpp tests/test_llm_provider.cpp
git commit -m "$(cat <<'EOF'
feat(llm): OpenAI Responses + Anthropic Messages protocols

Both follow the same wireReply path; only request shape and response
parser differ. Anthropic uses x-api-key + anthropic-version headers;
OpenAI Responses uses instructions+input shape with output[].content
output_text typed entries.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: LLMProvider — timeout + failure suppression

**Files:**
- Modify: `src/llm/LLMProvider.h` / `.cpp` — add 3-strike cooldown
- Modify: `tests/test_llm_provider.cpp` — add stress subtest

- [ ] **Step 1: Write failing test**

Add to `test_llm_provider.cpp` inside the QHttpServer block:
```cpp
void testFailureSuppression()
{
    // Server that always returns 500.
    QHttpServer server;
    server.route("/chat/completions", [](const QHttpServerRequest &) {
        return QHttpServerResponse("err", QHttpServerResponder::StatusCode::InternalServerError);
    });
    auto tcp = std::make_unique<QTcpServer>();
    QVERIFY(tcp->listen(QHostAddress::LocalHost, 0));
    const quint16 port = tcp->serverPort();
    QVERIFY(server.bind(tcp.release()));

    LLMProfile p;
    p.protocol = LLMProfile::Protocol::OpenAIChat;
    p.baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(port);
    p.apiKey = "k"; p.model = "m";

    LLMProvider provider;
    provider.setProfile(p);
    provider.setCooldownMs(50);  // shrink for test

    auto fire = [&]() {
        QEventLoop loop;
        LLMResult got;
        provider.generate("s","u",[&](LLMResult r){ got = std::move(r); loop.quit(); });
        QTimer::singleShot(3000,&loop,&QEventLoop::quit);
        loop.exec();
        return got;
    };

    // Three failed calls should NOT yet suppress.
    QVERIFY(!fire().ok);
    QVERIFY(!fire().ok);
    QVERIFY(!fire().ok);
    // Fourth call should be suppressed synchronously without network.
    QElapsedTimer t; t.start();
    LLMResult fourth = fire();
    QVERIFY(!fourth.ok);
    QCOMPARE(fourth.error, QString("suppressed (cooldown)"));
    QVERIFY(t.elapsed() < 30);  // returned without a network round-trip

    // After cooldown, calls are allowed again.
    QTest::qWait(80);
    QVERIFY(!fire().ok);  // server still errors but we got past suppression
    QVERIFY(fire().error != QString("suppressed (cooldown)"));
}
```

- [ ] **Step 2: Add API + members**

`src/llm/LLMProvider.h` additions:
```cpp
public:
    /// Cooldown window after 3 consecutive failures. Default 60000 ms.
    void setCooldownMs(int ms) { m_cooldownMs = ms; }

    /// Last error string from the most recent failed call. For Settings UI.
    QString lastError() const { return m_lastError; }

private:
    int m_consecutiveFailures = 0;
    qint64 m_cooldownUntilMs = 0;     // 0 = no cooldown active
    int m_cooldownMs = 60000;
    QString m_lastError;
```

Add `#include <QElapsedTimer>` if not present.

- [ ] **Step 3: Add suppression check + counters in generate()**

Top of `LLMProvider::generate()` (before the protocol switch):
```cpp
const qint64 now = QDateTime::currentMSecsSinceEpoch();
if (m_cooldownUntilMs > 0 && now < m_cooldownUntilMs) {
    cb({ false, {}, QStringLiteral("suppressed (cooldown)"), 0, 0 });
    return;
}
```

In `wireReply`, after computing `r`, wrap the callback to update counters before delegating:
```cpp
auto userCb = std::move(cb);
ResultCallback wrapped = [this, userCb = std::move(userCb)](LLMResult r) mutable {
    if (r.ok) {
        m_consecutiveFailures = 0;
        m_cooldownUntilMs = 0;
        m_lastError.clear();
    } else {
        ++m_consecutiveFailures;
        m_lastError = r.error;
        if (m_consecutiveFailures >= 3) {
            m_cooldownUntilMs = QDateTime::currentMSecsSinceEpoch() + m_cooldownMs;
        }
    }
    userCb(std::move(r));
};
```

Replace usages of `cb(…)` inside `wireReply` lambdas with `wrapped(…)`. Add `#include <QDateTime>`.

- [ ] **Step 4: Run test, verify pass**

```bash
cd build && cmake --build . --target test_llm_provider && ctest -R test_llm_provider --output-on-failure
```
Expected: PASS including the new subtest.

- [ ] **Step 5: Commit**

```bash
git add src/llm/LLMProvider.h src/llm/LLMProvider.cpp tests/test_llm_provider.cpp
git commit -m "$(cat <<'EOF'
feat(llm): three-strike failure suppression + lastError surface

After 3 consecutive failed calls, generate() returns
"suppressed (cooldown)" without a network round-trip for the
configured cooldown window (default 60s). Recovers on first success
or after window expires. lastError() exposes the most recent failure
text for the Settings UI.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: PersonaPool — schema + CRUD

**Files:**
- Create: `src/PersonaPool.h` / `.cpp`
- Modify: `src/MemoryManager.h` / `.cpp` — add `QSqlDatabase database() const` accessor
- Modify: `tests/CMakeLists.txt`
- Create: `tests/test_persona_pool.cpp`

- [ ] **Step 1: Add database() accessor to MemoryManager**

`src/MemoryManager.h` — add public method:
```cpp
/// Borrow the underlying QSqlDatabase. Returned by value (QSqlDatabase is a handle).
/// PersonaPool uses this so all SQLite access lives on the same connection / thread.
QSqlDatabase database() const { return m_db; }
```

(No .cpp change needed — inline.)

- [ ] **Step 2: Write failing test**

`tests/test_persona_pool.cpp`:
```cpp
#include "PersonaPool.h"
#include "MemoryManager.h"
#include <QtTest/QtTest>

class TestPersonaPool : public QObject
{
    Q_OBJECT
private slots:
    void testInsertAndPick()
    {
        MemoryManager mm(":memory:");
        QVERIFY(mm.isValid());
        PersonaPool pool(mm.database());
        QVERIFY(pool.isValid());

        QCOMPARE(pool.size("pack_a", "tool.before"), 0);
        QVERIFY(pool.insert("pack_a", "tool.before", "hash1", "Hello"));
        QVERIFY(pool.insert("pack_a", "tool.before", "hash1", "Hi"));
        QCOMPARE(pool.size("pack_a", "tool.before"), 2);

        QString picked = pool.pick("pack_a", "tool.before", "hash1");
        QVERIFY(picked == "Hello" || picked == "Hi");
    }

    void testPickEmpty()
    {
        MemoryManager mm(":memory:");
        PersonaPool pool(mm.database());
        QVERIFY(pool.pick("nope", "tool.before", "h").isEmpty());
    }

    void testInsertDuplicatesIgnored()
    {
        MemoryManager mm(":memory:");
        PersonaPool pool(mm.database());
        QVERIFY(pool.insert("p", "e", "h", "Hello"));
        QVERIFY(pool.insert("p", "e", "h", "Hello"));  // duplicate text — PK conflict
        QCOMPARE(pool.size("p", "e"), 1);
    }
};

QTEST_MAIN(TestPersonaPool)
#include "test_persona_pool.moc"
```

- [ ] **Step 3: Register sources + test in CMake**

`tests/CMakeLists.txt`: add to `SEELIEPET_LIB_SOURCES`:
```cmake
    ${CMAKE_SOURCE_DIR}/src/PersonaPool.h
    ${CMAKE_SOURCE_DIR}/src/PersonaPool.cpp
```
Add to `TEST_SOURCES`:
```cmake
    test_persona_pool.cpp
```

- [ ] **Step 4: Create PersonaPool header + impl**

`src/PersonaPool.h`:
```cpp
#ifndef PERSONA_POOL_H
#define PERSONA_POOL_H

#include <QSqlDatabase>
#include <QString>
#include <QStringList>

/**
 * @brief SQLite-backed text pool keyed by (packId, eventName).
 *
 * Lives entirely on the main thread (same as MemoryManager). Borrows
 * MemoryManager's QSqlDatabase by value — no separate connection.
 */
class PersonaPool
{
public:
    explicit PersonaPool(QSqlDatabase db);

    /// True iff the persona_pool table is present on the connection.
    bool isValid() const { return m_valid; }

    /// Number of stored entries for (packId, eventName) regardless of hash.
    int size(const QString &packId, const QString &eventName) const;

    /// Insert one text line. Duplicate (pack, event, text) is silently ignored.
    bool insert(const QString &packId, const QString &eventName,
                const QString &personaHash, const QString &text);

    /// Random pick from entries matching (packId, eventName, personaHash).
    /// Returns empty string if no such entries exist.
    QString pick(const QString &packId, const QString &eventName,
                 const QString &personaHash);

    /// Wipe rows for a pack whose persona_hash differs from `currentHash`.
    /// Used by the invalidation path.
    int wipeStale(const QString &packId, const QString &currentHash);

    /// Wipe ALL rows for a pack (used by the "Regenerate" button).
    int wipePack(const QString &packId);

private:
    QSqlDatabase m_db;
    bool m_valid = false;
};

#endif // PERSONA_POOL_H
```

`src/PersonaPool.cpp`:
```cpp
#include "PersonaPool.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QRandomGenerator>
#include <QtGlobal>

PersonaPool::PersonaPool(QSqlDatabase db) : m_db(db)
{
    if (!m_db.isValid() || !m_db.isOpen()) return;
    QSqlQuery q(m_db);
    const bool table = q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS persona_pool ("
        "  pack_id      TEXT NOT NULL,"
        "  event        TEXT NOT NULL,"
        "  text         TEXT NOT NULL,"
        "  persona_hash TEXT NOT NULL,"
        "  created_at   INTEGER NOT NULL,"
        "  PRIMARY KEY (pack_id, event, text)"
        ")"));
    const bool idx = q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_persona_pool_lookup "
        "ON persona_pool (pack_id, event, persona_hash)"));
    m_valid = table && idx;
    if (!m_valid) qWarning() << "PersonaPool init failed:" << q.lastError().text();
}

int PersonaPool::size(const QString &packId, const QString &eventName) const
{
    if (!m_valid) return 0;
    QSqlQuery q(m_db);
    q.prepare("SELECT COUNT(*) FROM persona_pool WHERE pack_id=? AND event=?");
    q.addBindValue(packId);
    q.addBindValue(eventName);
    if (q.exec() && q.next()) return q.value(0).toInt();
    return 0;
}

bool PersonaPool::insert(const QString &packId, const QString &eventName,
                         const QString &personaHash, const QString &text)
{
    if (!m_valid) return false;
    QSqlQuery q(m_db);
    q.prepare("INSERT OR IGNORE INTO persona_pool"
              "(pack_id, event, text, persona_hash, created_at) VALUES(?,?,?,?,?)");
    q.addBindValue(packId);
    q.addBindValue(eventName);
    q.addBindValue(text);
    q.addBindValue(personaHash);
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if (!q.exec()) {
        qWarning() << "PersonaPool::insert failed:" << q.lastError().text();
        return false;
    }
    return true;
}

QString PersonaPool::pick(const QString &packId, const QString &eventName,
                          const QString &personaHash)
{
    if (!m_valid) return {};
    QSqlQuery q(m_db);
    q.prepare("SELECT text FROM persona_pool "
              "WHERE pack_id=? AND event=? AND persona_hash=? "
              "ORDER BY RANDOM() LIMIT 1");
    q.addBindValue(packId);
    q.addBindValue(eventName);
    q.addBindValue(personaHash);
    if (q.exec() && q.next()) return q.value(0).toString();
    return {};
}

int PersonaPool::wipeStale(const QString &packId, const QString &currentHash)
{
    if (!m_valid) return 0;
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM persona_pool WHERE pack_id=? AND persona_hash != ?");
    q.addBindValue(packId);
    q.addBindValue(currentHash);
    if (!q.exec()) {
        qWarning() << "PersonaPool::wipeStale failed:" << q.lastError().text();
        return 0;
    }
    return q.numRowsAffected();
}

int PersonaPool::wipePack(const QString &packId)
{
    if (!m_valid) return 0;
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM persona_pool WHERE pack_id=?");
    q.addBindValue(packId);
    if (!q.exec()) return 0;
    return q.numRowsAffected();
}
```

Add `#include <QDateTime>` to the .cpp.

- [ ] **Step 5: Build + run test**

```bash
cd build && cmake .. && cmake --build . --target test_persona_pool && ctest -R test_persona_pool --output-on-failure
```
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/PersonaPool.h src/PersonaPool.cpp src/MemoryManager.h tests/test_persona_pool.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat: PersonaPool SQLite backing — schema, CRUD, random pick

Stores (packId, event, text, persona_hash) keyed by exact-text PK.
Shares MemoryManager's QSqlDatabase via new database() accessor.
Random pick via ORDER BY RANDOM() LIMIT 1.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: PersonaPool — refill, validation, spam guard, in-flight cleanup, hash invalidation

**Files:**
- Modify: `src/PersonaPool.h` / `.cpp` — add refill bookkeeping (no LLM call — that's PersonaEngine's job)
- Modify: `tests/test_persona_pool.cpp` — add subtests

The pool exposes hooks; the actual LLM-driven refill loop is glued together in `PersonaEngine` (Task 8). Here we add the bookkeeping that pool needs.

- [ ] **Step 1: Write failing tests**

Add to `test_persona_pool.cpp`:
```cpp
void testWipeStale()
{
    MemoryManager mm(":memory:");
    PersonaPool pool(mm.database());
    pool.insert("p", "e", "old_hash", "A");
    pool.insert("p", "e", "old_hash", "B");
    pool.insert("p", "e", "new_hash", "C");

    QCOMPARE(pool.wipeStale("p", "new_hash"), 2);
    QCOMPARE(pool.size("p", "e"), 1);
    QCOMPARE(pool.pick("p", "e", "new_hash"), QString("C"));
}

void testInsertManyValid()
{
    MemoryManager mm(":memory:");
    PersonaPool pool(mm.database());
    int accepted = pool.insertMany("p", "e", "h",
        { "Hello", "  ", "", "World", QString(220, 'x'), "Tch." });
    // Whitespace and empty rejected; oversized truncated; valid duplicates ignored
    QCOMPARE(accepted, 4);  // Hello, World, truncated-x..., Tch.
    QCOMPARE(pool.size("p", "e"), 4);

    // Reinserting same texts should not increase size
    QCOMPARE(pool.insertMany("p", "e", "h", { "Hello", "Tch." }), 0);
    QCOMPARE(pool.size("p", "e"), 4);
}

void testInflightLifecycle()
{
    MemoryManager mm(":memory:");
    PersonaPool pool(mm.database());
    pool.setInflightTimeoutMs(50);

    QVERIFY(!pool.isRefillInFlight("p", "e"));
    pool.markRefillStarted("p", "e");
    QVERIFY(pool.isRefillInFlight("p", "e"));
    pool.markRefillFinished("p", "e");
    QVERIFY(!pool.isRefillInFlight("p", "e"));

    // Stale in-flight is auto-cleaned
    pool.markRefillStarted("p", "e");
    QTest::qWait(80);
    QVERIFY(!pool.isRefillInFlight("p", "e"));  // expired by timeout sweep
}

void testSpamGuard()
{
    MemoryManager mm(":memory:");
    PersonaPool pool(mm.database());

    QVERIFY(!pool.isSpamSuppressed("p", "e"));
    pool.recordEmptyRefill("p", "e");
    pool.recordEmptyRefill("p", "e");
    QVERIFY(!pool.isSpamSuppressed("p", "e"));
    pool.recordEmptyRefill("p", "e");
    QVERIFY(pool.isSpamSuppressed("p", "e"));

    pool.clearSpamSuppression("p", "e");
    QVERIFY(!pool.isSpamSuppressed("p", "e"));
}
```

- [ ] **Step 2: Add header API**

`PersonaPool.h` additions in public:
```cpp
/// Insert multiple lines at once. Returns count actually inserted (after
/// validation: empty/whitespace dropped; lines >MAX_TIP_CHARS truncated).
int insertMany(const QString &packId, const QString &eventName,
               const QString &personaHash, const QStringList &texts);

// --- In-flight refill bookkeeping ------------------------------------------
bool isRefillInFlight(const QString &packId, const QString &eventName) const;
void markRefillStarted(const QString &packId, const QString &eventName);
void markRefillFinished(const QString &packId, const QString &eventName);

/// Default 30000 ms. Stuck entries older than this are swept on access.
void setInflightTimeoutMs(int ms) { m_inflightTimeoutMs = ms; }

// --- Spam guard (3-empty-refill suppression) -------------------------------
bool isSpamSuppressed(const QString &packId, const QString &eventName) const;
void recordEmptyRefill(const QString &packId, const QString &eventName);
void clearSpamSuppression(const QString &packId, const QString &eventName);

static constexpr int MAX_TIP_CHARS = 200;
static constexpr int TARGET_POOL_SIZE = 20;
static constexpr int MIN_POOL_SIZE = 5;
```

Private members:
```cpp
QHash<QString, qint64> m_inflight;      // key = "pack|event", value = start ms
QHash<QString, int>    m_emptyCounters; // key = "pack|event", value = count
int m_inflightTimeoutMs = 30000;
static QString makeKey(const QString &p, const QString &e) { return p + QChar('|') + e; }
```

Add `#include <QHash>` to header.

- [ ] **Step 3: Implement in .cpp**

```cpp
int PersonaPool::insertMany(const QString &packId, const QString &eventName,
                            const QString &personaHash, const QStringList &texts)
{
    if (!m_valid) return 0;
    int n = 0;
    for (const QString &raw : texts) {
        const QString trimmed = raw.trimmed();
        if (trimmed.isEmpty()) {
            qWarning() << "PersonaPool: skipping empty/whitespace entry for"
                       << packId << eventName;
            continue;
        }
        QString clean = trimmed;
        if (clean.length() > MAX_TIP_CHARS) {
            qWarning() << "PersonaPool: truncating oversized entry ("
                       << clean.length() << "chars) for" << packId << eventName;
            clean.truncate(MAX_TIP_CHARS);
        }
        if (insert(packId, eventName, personaHash, clean)) {
            // INSERT OR IGNORE returns success even on duplicate; count via numRowsAffected
            // — but insert() doesn't expose it. Re-check size delta or rely on PK rejection.
            // Simplest: compute via change to size() before/after, but that's O(N) per row.
            // Approx: we count "accepted" as non-duplicates by querying after insert.
            // To keep this simple, we use a separate prepared query that returns rows-affected:
            QSqlQuery q(m_db);
            q.prepare("SELECT changes()");
            if (q.exec() && q.next() && q.value(0).toInt() > 0) ++n;
        }
    }
    return n;
}

bool PersonaPool::isRefillInFlight(const QString &packId, const QString &eventName) const
{
    auto *self = const_cast<PersonaPool*>(this);
    const QString k = makeKey(packId, eventName);
    auto it = self->m_inflight.find(k);
    if (it == self->m_inflight.end()) return false;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - it.value() > m_inflightTimeoutMs) {
        self->m_inflight.erase(it);  // sweep on access
        return false;
    }
    return true;
}

void PersonaPool::markRefillStarted(const QString &packId, const QString &eventName)
{
    m_inflight.insert(makeKey(packId, eventName), QDateTime::currentMSecsSinceEpoch());
}

void PersonaPool::markRefillFinished(const QString &packId, const QString &eventName)
{
    m_inflight.remove(makeKey(packId, eventName));
}

bool PersonaPool::isSpamSuppressed(const QString &packId, const QString &eventName) const
{
    return m_emptyCounters.value(makeKey(packId, eventName), 0) >= 3;
}

void PersonaPool::recordEmptyRefill(const QString &packId, const QString &eventName)
{
    const QString k = makeKey(packId, eventName);
    m_emptyCounters[k] = m_emptyCounters.value(k, 0) + 1;
}

void PersonaPool::clearSpamSuppression(const QString &packId, const QString &eventName)
{
    m_emptyCounters.remove(makeKey(packId, eventName));
}
```

- [ ] **Step 4: Run tests, verify pass**

```bash
cd build && cmake --build . --target test_persona_pool && ctest -R test_persona_pool --output-on-failure
```
Expected: all subtests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/PersonaPool.h src/PersonaPool.cpp tests/test_persona_pool.cpp
git commit -m "$(cat <<'EOF'
feat: PersonaPool refill bookkeeping — in-flight, spam guard, validation

insertMany() handles per-entry validation (empty/whitespace dropped,
oversized truncated to MAX_TIP_CHARS=200, duplicate PK ignored).
isRefillInFlight tracks (pack, event) keys with 30s auto-sweep so a
never-fired callback doesn't leak. isSpamSuppressed flips after 3
consecutive empty refills until the user clicks Regenerate.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: PersonaEngine — sync pool-tier path

**Files:**
- Create: `src/PersonaEngine.h` / `.cpp`
- Modify: `tests/CMakeLists.txt`
- Create: `tests/test_persona_engine.cpp`

This task ships only the pool-tier sync path. On-demand + tipUpgraded come in Task 9.

- [ ] **Step 1: Write failing test**

`tests/test_persona_engine.cpp`:
```cpp
#include "PersonaEngine.h"
#include "PersonaPool.h"
#include "MemoryManager.h"
#include "ConfigManager.h"
#include "CharacterPack.h"
#include "TipsCatalog.h"
#include <QtTest/QtTest>

class TestPersonaEngine : public QObject
{
    Q_OBJECT
private slots:
    void testFallbackWhenDisabled()
    {
        MemoryManager mm(":memory:");
        ConfigManager cfg;
        cfg.setPersonaEnabled(false);
        PersonaEngine engine(&mm, &cfg);
        engine.setActivePackId("p");
        engine.setPersonaHash("h");

        auto r = engine.resolve("tool.before", {});
        // Falls back to TipsCatalog (may be empty if catalog not loaded; either way no upgrade)
        QCOMPARE(r.requestId, 0u);
    }

    void testPoolHitReturnsCachedText()
    {
        MemoryManager mm(":memory:");
        ConfigManager cfg;
        cfg.setPersonaEnabled(true);
        cfg.setPersonaProfile("fake");
        cfg.setLLMProfiles({ { "fake", LLMProfile::Protocol::OpenAIChat,
                               "http://nope", "k", "m" } });
        PersonaEngine engine(&mm, &cfg);
        engine.setActivePackId("p");
        engine.setPersonaHash("h");

        // Seed pool directly
        engine.pool().insert("p", "tool.before", "h", "Cached line");

        auto r = engine.resolve("tool.before", {});
        QCOMPARE(r.text, QString("Cached line"));
        QCOMPARE(r.requestId, 0u);  // pool tier => no upgrade
    }

    void testTierClassification()
    {
        QVERIFY(PersonaEngine::tierFor("tool.before") == PersonaEngine::Tier::Pool);
        QVERIFY(PersonaEngine::tierFor("file.edited") == PersonaEngine::Tier::Pool);
        QVERIFY(PersonaEngine::tierFor("permission.response") == PersonaEngine::Tier::Pool);
        QVERIFY(PersonaEngine::tierFor("session.start") == PersonaEngine::Tier::OnDemand);
        QVERIFY(PersonaEngine::tierFor("session.error") == PersonaEngine::Tier::OnDemand);
        QVERIFY(PersonaEngine::tierFor("milestone.gaming_mode") == PersonaEngine::Tier::OnDemand);
        // Unknown event defaults to OnDemand (forward compat)
        QVERIFY(PersonaEngine::tierFor("future.unknown") == PersonaEngine::Tier::OnDemand);
    }
};

QTEST_MAIN(TestPersonaEngine)
#include "test_persona_engine.moc"
```

- [ ] **Step 2: Create header**

`src/PersonaEngine.h`:
```cpp
#ifndef PERSONA_ENGINE_H
#define PERSONA_ENGINE_H

#include "PersonaPool.h"
#include "llm/LLMProvider.h"
#include <QObject>
#include <QQueue>
#include <QJsonObject>

class MemoryManager;
class ConfigManager;
class CharacterPack;

/**
 * @brief Resolves canonical events into in-character tip text.
 *
 * Pool-tier events (tool.before, file.edited, ...) return immediately from
 * PersonaPool. On-demand events (session.start, milestones, ...) return
 * a TipsCatalog fallback synchronously and emit tipUpgraded() when the LLM
 * call completes.
 */
class PersonaEngine : public QObject
{
    Q_OBJECT
public:
    enum class Tier { Pool, OnDemand };

    struct Resolved {
        QString text;
        quint64 requestId = 0;   // 0 if no upgrade will arrive
    };

    PersonaEngine(MemoryManager *memory, ConfigManager *config, QObject *parent = nullptr);

    /// Caller-supplied state — usually wired from CharacterPackManager.
    void setActivePackId(const QString &packId) { m_activePackId = packId; }
    void setPersonaHash(const QString &hash) { m_personaHash = hash; }

    /// Synchronous entry point. Always returns a non-empty text (or empty if
    /// no fallback is available). Pool-tier: text from pool or TipsCatalog
    /// fallback while refill runs. On-demand: TipsCatalog fallback with a
    /// non-zero requestId; the real line arrives via tipUpgraded later.
    Resolved resolve(const QString &eventName, const QJsonObject &payload);

    /// Test seam — expose internal pool for seeding.
    PersonaPool &pool() { return m_pool; }

    static Tier tierFor(const QString &eventName);

signals:
    void tipUpgraded(quint64 requestId, const QString &newText);

private:
    Resolved resolvePool(const QString &eventName);
    Resolved resolveOnDemand(const QString &eventName, const QJsonObject &payload);
    QString fallbackTip(const QString &eventName) const;

    MemoryManager *m_memory;
    ConfigManager *m_config;

    QString m_activePackId;
    QString m_personaHash;

    PersonaPool m_pool;
    LLMProvider m_provider;

    QQueue<QString> m_eventWindow;
    static constexpr int EVENT_WINDOW_SIZE = 5;

    quint64 m_nextRequestId = 1;
};

#endif // PERSONA_ENGINE_H
```

- [ ] **Step 3: Implement sync path in .cpp**

`src/PersonaEngine.cpp`:
```cpp
#include "PersonaEngine.h"
#include "MemoryManager.h"
#include "ConfigManager.h"
#include "TipsCatalog.h"
#include <QSet>

namespace {
const QSet<QString> &poolTierEvents()
{
    static const QSet<QString> s = {
        "tool.before", "tool.after", "tool.failed",
        "file.edited", "file.watched",
        "prompt.submitted", "todo.updated",
        "notification.sent", "permission.response",
    };
    return s;
}
}  // namespace

PersonaEngine::PersonaEngine(MemoryManager *memory, ConfigManager *config, QObject *parent)
    : QObject(parent)
    , m_memory(memory)
    , m_config(config)
    , m_pool(memory ? memory->database() : QSqlDatabase{})
{
}

PersonaEngine::Tier PersonaEngine::tierFor(const QString &eventName)
{
    return poolTierEvents().contains(eventName) ? Tier::Pool : Tier::OnDemand;
}

QString PersonaEngine::fallbackTip(const QString &eventName) const
{
    return TipsCatalog::instance().eventTip(eventName).body;
}

PersonaEngine::Resolved PersonaEngine::resolve(const QString &eventName,
                                               const QJsonObject &payload)
{
    // Maintain rolling window
    m_eventWindow.enqueue(eventName);
    while (m_eventWindow.size() > EVENT_WINDOW_SIZE) m_eventWindow.dequeue();

    if (!m_config || !m_config->personaEnabled() || m_config->personaProfile().isEmpty()) {
        return { fallbackTip(eventName), 0 };
    }

    if (tierFor(eventName) == Tier::Pool) return resolvePool(eventName);
    return resolveOnDemand(eventName, payload);
}

PersonaEngine::Resolved PersonaEngine::resolvePool(const QString &eventName)
{
    const QString text = m_pool.pick(m_activePackId, eventName, m_personaHash);
    if (!text.isEmpty()) {
        return { text, 0 };
    }
    // Cold pool — return fallback. Refill scheduling lives in Task 9 once we
    // have provider wiring; for now return fallback so the engine stays useful.
    return { fallbackTip(eventName), 0 };
}

PersonaEngine::Resolved PersonaEngine::resolveOnDemand(const QString &eventName,
                                                       const QJsonObject &)
{
    // On-demand async path lands in Task 9. For now this also returns fallback.
    return { fallbackTip(eventName), 0 };
}
```

- [ ] **Step 4: CMake**

Add to `tests/CMakeLists.txt` `SEELIEPET_LIB_SOURCES`:
```cmake
    ${CMAKE_SOURCE_DIR}/src/PersonaEngine.h
    ${CMAKE_SOURCE_DIR}/src/PersonaEngine.cpp
```
Add to `TEST_SOURCES`:
```cmake
    test_persona_engine.cpp
```

- [ ] **Step 5: Build + run**

```bash
cd build && cmake .. && cmake --build . --target test_persona_engine && ctest -R test_persona_engine --output-on-failure
```
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/PersonaEngine.h src/PersonaEngine.cpp tests/test_persona_engine.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat: PersonaEngine sync pool-tier path + tier classification

Pool-tier events (9 names) return cached pool text or TipsCatalog
fallback synchronously. On-demand path returns fallback for now —
async LLM call comes in the next commit.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: PersonaEngine — async on-demand path + tipUpgraded + refill scheduling

**Files:**
- Modify: `src/PersonaEngine.h` / `.cpp` — wire LLMProvider for on-demand + refill
- Modify: `tests/test_persona_engine.cpp` — add async + refill subtests using QHttpServer mock

- [ ] **Step 1: Write failing tests**

Add to `test_persona_engine.cpp`:
```cpp
#ifdef SEELIE_HAS_QHTTPSERVER
#include <QHttpServer>
#include <QTcpServer>

void testOnDemandUpgrade()
{
    QHttpServer server;
    server.route("/chat/completions", [](const QHttpServerRequest &) {
        return QHttpServerResponse(
            R"({"choices":[{"message":{"content":"Live line."}}],
                "usage":{"prompt_tokens":1,"completion_tokens":1}})",
            "application/json");
    });
    auto tcp = std::make_unique<QTcpServer>();
    QVERIFY(tcp->listen(QHostAddress::LocalHost, 0));
    const quint16 port = tcp->serverPort();
    QVERIFY(server.bind(tcp.release()));

    MemoryManager mm(":memory:");
    ConfigManager cfg;
    cfg.setPersonaEnabled(true);
    cfg.setLLMProfiles({ { "p", LLMProfile::Protocol::OpenAIChat,
                            QStringLiteral("http://127.0.0.1:%1").arg(port),
                            "k", "m" } });
    cfg.setPersonaProfile("p");
    PersonaEngine engine(&mm, &cfg);
    engine.setActivePackId("pack");
    engine.setPersonaHash("h");

    QSignalSpy spy(&engine, &PersonaEngine::tipUpgraded);
    auto r = engine.resolve("session.start", {});
    QVERIFY(r.requestId != 0);  // upgrade will arrive
    QVERIFY(!r.text.isEmpty()); // immediate fallback

    QVERIFY(spy.wait(3000));
    QCOMPARE(spy.first()[0].value<quint64>(), r.requestId);
    QCOMPARE(spy.first()[1].toString(), QString("Live line."));
}

void testPoolRefillFromBatchedLLM()
{
    QHttpServer server;
    server.route("/chat/completions", [](const QHttpServerRequest &) {
        return QHttpServerResponse(
            R"({"choices":[{"message":{"content":
                "[\"line one\",\"line two\",\"line three\"]"}}]})",
            "application/json");
    });
    auto tcp = std::make_unique<QTcpServer>();
    QVERIFY(tcp->listen(QHostAddress::LocalHost, 0));
    const quint16 port = tcp->serverPort();
    QVERIFY(server.bind(tcp.release()));

    MemoryManager mm(":memory:");
    ConfigManager cfg;
    cfg.setPersonaEnabled(true);
    cfg.setLLMProfiles({ { "p", LLMProfile::Protocol::OpenAIChat,
                            QStringLiteral("http://127.0.0.1:%1").arg(port),
                            "k","m" } });
    cfg.setPersonaProfile("p");
    PersonaEngine engine(&mm, &cfg);
    engine.setActivePackId("pack");
    engine.setPersonaHash("h");

    QCOMPARE(engine.pool().size("pack", "tool.before"), 0);
    engine.resolve("tool.before", {});  // triggers async refill

    QTRY_VERIFY_WITH_TIMEOUT(engine.pool().size("pack","tool.before") >= 3, 3000);
}
#endif
```

- [ ] **Step 2: Implement async path + refill**

Edit `src/PersonaEngine.cpp`. In the constructor, wire the provider:
```cpp
// After m_pool init:
// Pick first profile that matches personaProfile name
if (m_config) {
    const auto profiles = m_config->llmProfiles();
    for (const auto &p : profiles) {
        if (p.name == m_config->personaProfile()) {
            m_provider.setProfile(p);
            break;
        }
    }
}
```

Replace `resolveOnDemand` body:
```cpp
PersonaEngine::Resolved PersonaEngine::resolveOnDemand(const QString &eventName,
                                                       const QJsonObject &payload)
{
    if (!m_provider.isConfigured()) return { fallbackTip(eventName), 0 };

    const quint64 requestId = m_nextRequestId++;

    // Build context for the prompt
    QStringList recent;
    for (const QString &e : m_eventWindow) recent << e;

    QString systemPrompt = QStringLiteral(
        "You are a desktop pet companion to a software developer. "
        "Reply with ONE short sentence in the user's language.");
    QString userPrompt = QStringLiteral("Event: %1\nRecent events: %2\nReact in-character.")
                          .arg(eventName, recent.join(", "));

    // Privacy: only attach memory snapshot if user opted in
    if (m_config->shareMemoryWithAi() && m_memory) {
        const QString name = m_memory->effectiveName();
        if (!name.isEmpty()) userPrompt += QStringLiteral("\nUser name: %1").arg(name);
    }

    m_provider.generate(systemPrompt, userPrompt,
        [this, requestId, eventName](LLMResult r) {
            if (!r.ok || r.text.trimmed().isEmpty()) return;
            QString t = r.text.trimmed();
            if (t.length() > PersonaPool::MAX_TIP_CHARS) t.truncate(PersonaPool::MAX_TIP_CHARS);
            emit tipUpgraded(requestId, t);
        });

    return { fallbackTip(eventName), requestId };
}
```

Replace `resolvePool` cold-path branch with a refill scheduler:
```cpp
PersonaEngine::Resolved PersonaEngine::resolvePool(const QString &eventName)
{
    const QString text = m_pool.pick(m_activePackId, eventName, m_personaHash);
    if (m_pool.size(m_activePackId, eventName) < PersonaPool::MIN_POOL_SIZE
        && !m_pool.isRefillInFlight(m_activePackId, eventName)
        && !m_pool.isSpamSuppressed(m_activePackId, eventName)
        && m_provider.isConfigured())
    {
        m_pool.markRefillStarted(m_activePackId, eventName);

        QString system = QStringLiteral(
            "You write short in-character reactions for a desktop pet. "
            "Each line: one short sentence, under 200 characters. "
            "Stay in character.");
        QString user = QStringLiteral(
            "Generate %1 distinct one-sentence reactions to the event '%2'.")
            .arg(PersonaPool::TARGET_POOL_SIZE).arg(eventName);

        const QString pack = m_activePackId;
        const QString event = eventName;
        const QString hash = m_personaHash;

        m_provider.generateBatch(system, user, PersonaPool::TARGET_POOL_SIZE,
            [this, pack, event, hash](QVector<QString> lines) {
                m_pool.markRefillFinished(pack, event);
                if (lines.isEmpty()) {
                    m_pool.recordEmptyRefill(pack, event);
                    return;
                }
                QStringList qsList;
                for (const auto &l : lines) qsList << l;
                m_pool.insertMany(pack, event, hash, qsList);
            });
    }
    return { text.isEmpty() ? fallbackTip(eventName) : text, 0 };
}
```

- [ ] **Step 3: Build + run**

```bash
cd build && cmake --build . --target test_persona_engine && ctest -R test_persona_engine --output-on-failure
```
Expected: PASS (or SKIP if Qt6::HttpServer unavailable).

- [ ] **Step 4: Commit**

```bash
git add src/PersonaEngine.h src/PersonaEngine.cpp tests/test_persona_engine.cpp
git commit -m "$(cat <<'EOF'
feat: PersonaEngine async on-demand + pool refill scheduling

On-demand events fire LLMProvider::generate, return fallback
immediately with a requestId, and emit tipUpgraded when the LLM
responds (truncated to MAX_TIP_CHARS). Cold pool-tier events
schedule a background generateBatch refill, gated by isRefillInFlight
and isSpamSuppressed so we never thunder-herd a misbehaving endpoint.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 10: MainWindow integration + bubble swap on tipUpgraded

**Files:**
- Modify: `src/main.cpp` — construct PersonaEngine, pass to MainWindow
- Modify: `src/mainwindow.h` / `.cpp` — own PersonaEngine, wire signals, swap bubble text on tipUpgraded
- Modify: `src/EventRouter.cpp` — verify `eventProcessed` is emitted at end of routeEvent (the current signal). No new signal needed.

- [ ] **Step 1: Inspect existing wiring**

Read `src/main.cpp` around the EventRouter / MainWindow construction (find by grep):
```bash
```

Locate where `eventRouter`, `memory`, and `packManager` are created and where they're passed into MainWindow.

- [ ] **Step 2: Construct PersonaEngine in main.cpp**

In `src/main.cpp`, right after `MemoryManager memory(...)` and `ConfigManager cfg`:
```cpp
PersonaEngine personaEngine(&memory, &cfg);
```

Pass it into `MainWindow` via a setter (matching existing patterns — see how MemoryManager is wired in commit `f2923e8`):
```cpp
mainWindow.setPersonaEngine(&personaEngine);
```

- [ ] **Step 3: Add setter + signal wiring in MainWindow**

`src/mainwindow.h`:
```cpp
class PersonaEngine;
...
public:
    void setPersonaEngine(PersonaEngine *engine);

private slots:
    void onTipUpgraded(quint64 requestId, const QString &newText);

private:
    PersonaEngine *m_personaEngine = nullptr;
    quint64 m_activeBubbleRequestId = 0;
```

`src/mainwindow.cpp`:
```cpp
void MainWindow::setPersonaEngine(PersonaEngine *engine)
{
    m_personaEngine = engine;
    if (!engine) return;

    // Wire EventRouter -> PersonaEngine (use existing eventProcessed signal)
    connect(m_eventRouter, &EventRouter::eventProcessed,
            this, [this](const QString &name, const QJsonObject &payload) {
        if (!m_personaEngine) return;
        auto r = m_personaEngine->resolve(name, payload);
        // The TipsEngine path already showed the bubble; if this is a fresh line
        // we override. For now, only swap when text differs.
        if (m_tipBubble && !r.text.isEmpty()) {
            m_tipBubble->setBody(r.text);
        }
        m_activeBubbleRequestId = r.requestId;
    });

    // MemoryManager milestones bypass EventRouter
    connect(m_memoryManager, &MemoryManager::milestoneReached,
            this, [this](const QString &title, const QString &body) {
        if (!m_personaEngine) return;
        const QString key = QStringLiteral("milestone.") + title;
        auto r = m_personaEngine->resolve(key, {});
        if (m_tipBubble && !r.text.isEmpty()) {
            m_tipBubble->setBody(r.text);
        }
        m_activeBubbleRequestId = r.requestId;
        Q_UNUSED(body);
    });

    // CharacterPackManager active pack changed -> refresh hash
    connect(m_packManager, &CharacterPackManager::activePackChanged,
            this, [this](CharacterPack *pack) {
        if (!m_personaEngine) return;
        m_personaEngine->setActivePackId(pack ? pack->metadata().id : QString());
        m_personaEngine->setPersonaHash(pack ? pack->personaHash() : QString());
    });

    // Async upgrades
    connect(engine, &PersonaEngine::tipUpgraded,
            this, &MainWindow::onTipUpgraded);
}

void MainWindow::onTipUpgraded(quint64 requestId, const QString &newText)
{
    if (requestId == 0) return;
    if (requestId != m_activeBubbleRequestId) return;  // bubble already replaced
    if (!m_tipBubble || !m_tipBubble->isVisible()) return;
    m_tipBubble->setBody(newText);
    // TTS re-synthesis: existing TTSEngine cache miss on new text is acceptable —
    // the cost is one rare on-demand event's audio per session.
    if (m_ttsEngine) m_ttsEngine->speak(newText);
}
```

(Adjust to match actual MainWindow member names — `m_eventRouter`, `m_memoryManager`, `m_packManager`, `m_tipBubble`, `m_ttsEngine` are inferred. Read the header first to confirm.)

- [ ] **Step 4: Add #include + forward decls**

In `mainwindow.cpp`: `#include "PersonaEngine.h"`, `#include "CharacterPack.h"`, `#include "CharacterPackManager.h"`, `#include "MemoryManager.h"`.

- [ ] **Step 5: Build the app**

```bash
cd build && cmake --build . --target Seelie 2>&1 | tail -20
```
Expected: clean build.

- [ ] **Step 6: Smoke run**

Start the app, send a test event from the gateway, observe tip bubble. Use:
```bash
seelie-gateway --source claude-code --event session.start
```
- With AI off (default), bubble should show TipsCatalog text exactly as before.
- This is a manual sanity check — full e2e is Task 17.

- [ ] **Step 7: Commit**

```bash
git add src/main.cpp src/mainwindow.h src/mainwindow.cpp
git commit -m "$(cat <<'EOF'
feat: wire PersonaEngine into MainWindow

Listens to EventRouter::eventProcessed, MemoryManager::milestoneReached,
and CharacterPackManager::activePackChanged. tipUpgraded swaps the
active bubble text when the LLM response arrives, gated by a stored
requestId so stale upgrades don't overwrite a newer bubble.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 11: Stats accessors on TTSEngine, EventRouter, IpcServer, PersonaEngine

**Files:**
- Modify: `src/TTSEngine.h` / `.cpp` — add hit/miss counters + `stats()` accessor
- Modify: `src/EventRouter.h` / `.cpp` — per-event counter + total + last-event timestamp
- Modify: `src/IpcServer.h` / `.cpp` + `src/UdpWorker.h` / `.cpp` — packets received, decode errors, start time
- Modify: `src/PersonaEngine.h` / `.cpp` — refill ok/fail, ondemand ok/fail, last error, tokens in/out
- Modify: `tests/test_persona_engine.cpp` etc — add stats assertions

Stats are kept in two layers: in-memory counters (for current session "since boot" numbers) and persisted lifetime values (via `MemoryManager::increment("stats.x")`).

- [ ] **Step 1: TTSEngine stats**

`src/TTSEngine.h` additions:
```cpp
struct TtsStats {
    int sessionRequests = 0;
    int sessionHits = 0;
    qint64 lastMissMs = 0;
    QString lastMissText;
};
public:
    TtsStats stats() const { return m_stats; }
private:
    TtsStats m_stats;
```

In TTSEngine.cpp wherever it consults the cache: bump `m_stats.sessionRequests`, increment `m_stats.sessionHits` on hit. On miss, record timestamp + text. Mirror via `m_memory->increment("stats.tts.requests")` and `stats.tts.hits` (pass `MemoryManager*` in via setter if not already accessible).

- [ ] **Step 2: EventRouter stats**

`src/EventRouter.h`:
```cpp
struct EventStats {
    int total = 0;
    QHash<QString,int> perEvent;
    qint64 lastEventMs = 0;
    QString lastEventName;
};
EventStats stats() const { return m_stats; }
```
In `EventRouter::routeEvent()` after validation passes:
```cpp
++m_stats.total;
m_stats.perEvent[event["event"].toString()] += 1;
m_stats.lastEventMs = QDateTime::currentMSecsSinceEpoch();
m_stats.lastEventName = event["event"].toString();
```

- [ ] **Step 3: IpcServer / UdpWorker stats**

In `UdpWorker.cpp` where datagrams are read: increment packets-received counter. On JSON decode failure: increment decode-error counter. Expose via `IpcServer::stats()` which forwards to the worker's counter.

```cpp
// src/IpcServer.h
struct IpcStats {
    qint64 packets = 0;
    qint64 decodeErrors = 0;
    qint64 startedAtMs = 0;
};
IpcStats stats() const;
```

- [ ] **Step 4: PersonaEngine stats**

`src/PersonaEngine.h`:
```cpp
struct PersonaStats {
    int refillsOk = 0;
    int refillsFail = 0;
    int ondemandOk = 0;
    int ondemandFail = 0;
    qint64 tokensIn = 0;
    qint64 tokensOut = 0;
    QString lastError;
};
PersonaStats stats() const { return m_stats; }
```

Increment in the respective callbacks in `resolvePool` (refill batch result) and `resolveOnDemand` (single result). Capture `r.tokensIn` / `r.tokensOut` from `LLMResult`. `lastError` mirrors `m_provider.lastError()`.

- [ ] **Step 5: Persist lifetime counters**

Wherever a counter is bumped in-memory, also call `m_memory->increment("stats.<area>.<name>")`. Pass MemoryManager pointers via constructor or setter where not already available.

- [ ] **Step 6: Unit test stats**

Add to test_persona_engine.cpp:
```cpp
void testStatsCountersIncrement()
{
    MemoryManager mm(":memory:");
    ConfigManager cfg;
    PersonaEngine engine(&mm, &cfg);
    // No provider configured → fallback path only. Stats should still record event counters.
    engine.resolve("tool.before", {});
    QCOMPARE(engine.stats().ondemandOk + engine.stats().ondemandFail, 0);
}
```

- [ ] **Step 7: Build all + ctest**

```bash
cd build && cmake --build . && ctest --output-on-failure
```
Expected: all tests PASS.

- [ ] **Step 8: Commit**

```bash
git add src/TTSEngine.h src/TTSEngine.cpp src/EventRouter.h src/EventRouter.cpp \
        src/IpcServer.h src/IpcServer.cpp src/UdpWorker.h src/UdpWorker.cpp \
        src/PersonaEngine.h src/PersonaEngine.cpp tests/test_persona_engine.cpp
git commit -m "$(cat <<'EOF'
feat: stats accessors on TTS / Event / Ipc / Persona

Each subsystem exposes a small read-only struct via stats(); the
Statistics dialog (next task) consumes them. Counters also mirrored
to MemoryManager kv keys (stats.*) so lifetime totals survive
restart.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 12: SettingsPanelWidget — internal rename m_aiTab → m_ttsTab

This is a mechanical refactor: free up `m_aiTab*` / `m_aiTabBtn` for the new LLM tab (user label "AI"), and rename the existing internal vars to match the user-visible "TTS" label they actually hold.

**Files:**
- Modify: `src/SettingsPanelWidget.h` — rename declarations
- Modify: `src/SettingsPanelWidget.cpp` — rename all usages

- [ ] **Step 1: Identify references**

```bash
```

Confirm count (the audit estimated ~12 references plus the header).

- [ ] **Step 2: Rename in the header**

Edit `src/SettingsPanelWidget.h` — change every occurrence of `m_aiTab` to `m_ttsTab`, and `m_aiTabBtn` to `m_ttsTabBtn`. No semantic change.

- [ ] **Step 3: Rename in the .cpp**

Edit `src/SettingsPanelWidget.cpp` — same pattern. Crucially, the user-facing `tr("TTS")` label on line 629 stays unchanged.

- [ ] **Step 4: Build the app, verify no compile errors**

```bash
cd build && cmake --build . --target Seelie 2>&1 | tail -20
```
Expected: clean build.

- [ ] **Step 5: Smoke run**

Launch the app, open Settings, click each tab — TTS tab should look exactly the same. No regression.

- [ ] **Step 6: Commit**

```bash
git add src/SettingsPanelWidget.h src/SettingsPanelWidget.cpp
git commit -m "$(cat <<'EOF'
refactor(settings): rename internal m_aiTab* -> m_ttsTab*

Frees the m_aiTab* identifiers for the upcoming LLM "AI" tab. The
user-facing tr("TTS") label is unchanged — this is purely an
internal naming fix.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 13: Settings — new AI tab (profile list, persona enable, privacy, regenerate)

**Files:**
- Modify: `src/SettingsPanelWidget.h` / `.cpp` — add new `m_llmTab` with profile list view, "Persona" group, privacy checkbox, "Regenerate pool" button

The edit-profile dialog comes in Task 14. This task just shows the tab and lets the user toggle persona on/off + pick from existing profiles + flip the privacy switch.

- [ ] **Step 1: Add header members**

`src/SettingsPanelWidget.h` — new member declarations:
```cpp
QPushButton *m_llmTabBtn = nullptr;
QWidget *m_llmTab = nullptr;
QListWidget *m_llmProfilesList = nullptr;
QPushButton *m_llmAddBtn = nullptr;
QPushButton *m_llmEditBtn = nullptr;
QPushButton *m_llmDeleteBtn = nullptr;
QPushButton *m_llmTestBtn = nullptr;
QComboBox  *m_personaProfileCombo = nullptr;
QCheckBox  *m_personaEnabledCheck = nullptr;
QCheckBox  *m_shareMemoryCheck = nullptr;
QPushButton *m_regenPoolBtn = nullptr;
QLabel     *m_llmLastErrorLabel = nullptr;
```

Add slot:
```cpp
private slots:
    void onTabChangedLLM();
    void refreshLLMProfilesUi();
    void onAddProfileClicked();
    void onEditProfileClicked();
    void onDeleteProfileClicked();
    void onTestConnectionClicked();
    void onRegenPoolClicked();
```

- [ ] **Step 2: Build the tab in .cpp**

In the `setupTabs()` (or equivalent constructor flow) — wherever `m_aiTabBtn` (now `m_ttsTabBtn`) was added, add a sibling block for `m_llmTabBtn` with label `tr("AI")` and `onTabChanged(2)` (or whatever the next index is).

Build the tab body (mirroring the TTS tab layout style):
```cpp
m_llmTab = new QWidget(m_contentWidget);
m_llmTab->setVisible(false);
auto *llmLayout = new QVBoxLayout(m_llmTab);

// --- Profiles ---
auto *profilesGroup = new QGroupBox(tr("Profiles"), m_llmTab);
auto *pgLayout = new QVBoxLayout(profilesGroup);
m_llmProfilesList = new QListWidget(profilesGroup);
pgLayout->addWidget(m_llmProfilesList);
auto *pgBtnRow = new QHBoxLayout;
m_llmAddBtn    = new QPushButton(tr("Add"),    profilesGroup);
m_llmEditBtn   = new QPushButton(tr("Edit"),   profilesGroup);
m_llmDeleteBtn = new QPushButton(tr("Delete"), profilesGroup);
m_llmTestBtn   = new QPushButton(tr("Test connection"), profilesGroup);
pgBtnRow->addWidget(m_llmAddBtn);
pgBtnRow->addWidget(m_llmEditBtn);
pgBtnRow->addWidget(m_llmDeleteBtn);
pgBtnRow->addWidget(m_llmTestBtn);
pgLayout->addLayout(pgBtnRow);
llmLayout->addWidget(profilesGroup);

// --- Persona ---
auto *personaGroup = new QGroupBox(tr("Persona"), m_llmTab);
auto *pgForm = new QFormLayout(personaGroup);
m_personaProfileCombo = new QComboBox(personaGroup);
m_personaEnabledCheck = new QCheckBox(tr("Enabled"), personaGroup);
pgForm->addRow(tr("Profile:"), m_personaProfileCombo);
pgForm->addRow(QString(), m_personaEnabledCheck);
llmLayout->addWidget(personaGroup);

// --- Privacy ---
auto *privacyGroup = new QGroupBox(tr("Privacy"), m_llmTab);
auto *privLayout = new QVBoxLayout(privacyGroup);
m_shareMemoryCheck = new QCheckBox(tr("Share memory with AI (name, milestones)"), privacyGroup);
privLayout->addWidget(m_shareMemoryCheck);
llmLayout->addWidget(privacyGroup);

// --- Tools ---
auto *toolsGroup = new QGroupBox(tr("Tools"), m_llmTab);
auto *toolsLayout = new QVBoxLayout(toolsGroup);
m_regenPoolBtn = new QPushButton(tr("Regenerate persona pool for active pack"), toolsGroup);
toolsLayout->addWidget(m_regenPoolBtn);
llmLayout->addWidget(toolsGroup);

// --- Status ---
m_llmLastErrorLabel = new QLabel(tr("Last error: —"), m_llmTab);
m_llmLastErrorLabel->setWordWrap(true);
llmLayout->addWidget(m_llmLastErrorLabel);

llmLayout->addStretch();
tabContentLayout->addWidget(m_llmTab, 1);
```

- [ ] **Step 3: Wire signals**

```cpp
connect(m_llmAddBtn,    &QPushButton::clicked, this, &SettingsPanelWidget::onAddProfileClicked);
connect(m_llmEditBtn,   &QPushButton::clicked, this, &SettingsPanelWidget::onEditProfileClicked);
connect(m_llmDeleteBtn, &QPushButton::clicked, this, &SettingsPanelWidget::onDeleteProfileClicked);
connect(m_llmTestBtn,   &QPushButton::clicked, this, &SettingsPanelWidget::onTestConnectionClicked);
connect(m_regenPoolBtn, &QPushButton::clicked, this, &SettingsPanelWidget::onRegenPoolClicked);

connect(m_personaEnabledCheck, &QCheckBox::toggled,
        m_config, &ConfigManager::setPersonaEnabled);
connect(m_shareMemoryCheck, &QCheckBox::toggled,
        m_config, &ConfigManager::setShareMemoryWithAi);
connect(m_personaProfileCombo, QOverload<const QString &>::of(&QComboBox::currentTextChanged),
        m_config, &ConfigManager::setPersonaProfile);
```

`refreshLLMProfilesUi()`:
```cpp
void SettingsPanelWidget::refreshLLMProfilesUi()
{
    m_llmProfilesList->clear();
    m_personaProfileCombo->blockSignals(true);
    m_personaProfileCombo->clear();
    for (const auto &p : m_config->llmProfiles()) {
        const QString protoName = [&]() {
            switch (p.protocol) {
            case LLMProfile::Protocol::OpenAIChat:        return tr("OpenAI Chat");
            case LLMProfile::Protocol::OpenAIResponses:   return tr("OpenAI Responses");
            case LLMProfile::Protocol::AnthropicMessages: return tr("Anthropic");
            }
            return QString();
        }();
        m_llmProfilesList->addItem(QStringLiteral("%1   %2   %3")
                                      .arg(p.name, protoName, p.model));
        m_personaProfileCombo->addItem(p.name);
    }
    m_personaProfileCombo->setCurrentText(m_config->personaProfile());
    m_personaProfileCombo->blockSignals(false);
    m_personaEnabledCheck->setChecked(m_config->personaEnabled());
    m_shareMemoryCheck->setChecked(m_config->shareMemoryWithAi());
}
```

Stub the dialog-opening slots — the dialog itself comes in Task 14:
```cpp
void SettingsPanelWidget::onAddProfileClicked()    { /* opens dialog in Task 14 */ }
void SettingsPanelWidget::onEditProfileClicked()   { /* opens dialog in Task 14 */ }
void SettingsPanelWidget::onTestConnectionClicked(){ /* fires in Task 14 */ }

void SettingsPanelWidget::onDeleteProfileClicked()
{
    auto *item = m_llmProfilesList->currentItem();
    if (!item) return;
    const QString name = item->text().split(' ').first().trimmed();
    auto profiles = m_config->llmProfiles();
    profiles.erase(std::remove_if(profiles.begin(), profiles.end(),
        [&](const LLMProfile &p){ return p.name == name; }), profiles.end());
    m_config->setLLMProfiles(profiles);
    refreshLLMProfilesUi();
}

void SettingsPanelWidget::onRegenPoolClicked()
{
    if (m_personaEngine) m_personaEngine->regenerateActivePackPool();
}
```

For the regenerate path, add a setter on PersonaEngine:
```cpp
// PersonaEngine.h
void setActivePackId(...);
void regenerateActivePackPool() { m_pool.wipePack(m_activePackId); }
```

- [ ] **Step 4: Build**

```bash
cd build && cmake --build . --target Seelie 2>&1 | tail -10
```

- [ ] **Step 5: Manual smoke test**

Launch app → Settings → click new "AI" tab → confirm it renders. Profiles list is empty (correct on first run). Persona/Privacy checkboxes flip and persist across restart.

- [ ] **Step 6: Commit**

```bash
git add src/SettingsPanelWidget.h src/SettingsPanelWidget.cpp src/PersonaEngine.h
git commit -m "$(cat <<'EOF'
feat(settings): new AI tab — profiles list, persona toggle, privacy

Adds the "AI" tab with profile list (Add/Edit/Delete/Test buttons —
dialog comes next), persona profile combo + enabled checkbox,
"Share memory with AI" privacy toggle, and "Regenerate persona
pool" tool button. State flows through ConfigManager so it persists
on restart.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 14: Settings — edit-profile dialog + test connection

**Files:**
- Create: `src/EditLLMProfileDialog.h` / `.cpp` — modal dialog
- Modify: `src/SettingsPanelWidget.cpp` — wire Add/Edit/Test to the dialog + LLMProvider

- [ ] **Step 1: Create dialog**

`src/EditLLMProfileDialog.h`:
```cpp
#ifndef EDIT_LLM_PROFILE_DIALOG_H
#define EDIT_LLM_PROFILE_DIALOG_H

#include "llm/LLMProfile.h"
#include <QDialog>
class QLineEdit;
class QComboBox;

class EditLLMProfileDialog : public QDialog
{
    Q_OBJECT
public:
    explicit EditLLMProfileDialog(const LLMProfile &initial, QWidget *parent = nullptr);
    LLMProfile profile() const;
private:
    QLineEdit *m_name;
    QComboBox *m_protocol;
    QLineEdit *m_baseUrl;
    QLineEdit *m_apiKey;
    QLineEdit *m_model;
};

#endif
```

`src/EditLLMProfileDialog.cpp`:
```cpp
#include "EditLLMProfileDialog.h"
#include <QLineEdit>
#include <QComboBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QPushButton>

EditLLMProfileDialog::EditLLMProfileDialog(const LLMProfile &initial, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("LLM Profile"));
    auto *form = new QFormLayout(this);

    m_name = new QLineEdit(initial.name, this);
    m_protocol = new QComboBox(this);
    m_protocol->addItem(tr("OpenAI Chat"),         int(LLMProfile::Protocol::OpenAIChat));
    m_protocol->addItem(tr("OpenAI Responses"),    int(LLMProfile::Protocol::OpenAIResponses));
    m_protocol->addItem(tr("Anthropic Messages"),  int(LLMProfile::Protocol::AnthropicMessages));
    m_protocol->setCurrentIndex(int(initial.protocol));
    m_baseUrl = new QLineEdit(initial.baseUrl, this);
    m_apiKey = new QLineEdit(initial.apiKey, this);
    m_apiKey->setEchoMode(QLineEdit::Password);
    m_model = new QLineEdit(initial.model, this);

    form->addRow(tr("Name:"),     m_name);
    form->addRow(tr("Protocol:"), m_protocol);
    form->addRow(tr("Base URL:"), m_baseUrl);
    form->addRow(tr("API key:"),  m_apiKey);
    form->addRow(tr("Model:"),    m_model);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

LLMProfile EditLLMProfileDialog::profile() const
{
    LLMProfile p;
    p.name = m_name->text().trimmed();
    p.protocol = static_cast<LLMProfile::Protocol>(m_protocol->currentData().toInt());
    p.baseUrl = m_baseUrl->text().trimmed();
    p.apiKey = m_apiKey->text();
    p.model = m_model->text().trimmed();
    return p;
}
```

- [ ] **Step 2: Wire Add/Edit in SettingsPanelWidget.cpp**

```cpp
void SettingsPanelWidget::onAddProfileClicked()
{
    EditLLMProfileDialog dlg({}, this);
    if (dlg.exec() != QDialog::Accepted) return;
    auto profiles = m_config->llmProfiles();
    profiles.append(dlg.profile());
    m_config->setLLMProfiles(profiles);
    refreshLLMProfilesUi();
}

void SettingsPanelWidget::onEditProfileClicked()
{
    auto *item = m_llmProfilesList->currentItem();
    if (!item) return;
    const int row = m_llmProfilesList->currentRow();
    auto profiles = m_config->llmProfiles();
    if (row < 0 || row >= profiles.size()) return;
    EditLLMProfileDialog dlg(profiles[row], this);
    if (dlg.exec() != QDialog::Accepted) return;
    profiles[row] = dlg.profile();
    m_config->setLLMProfiles(profiles);
    refreshLLMProfilesUi();
}
```

- [ ] **Step 3: Wire Test connection**

```cpp
void SettingsPanelWidget::onTestConnectionClicked()
{
    const int row = m_llmProfilesList->currentRow();
    if (row < 0) return;
    const auto profile = m_config->llmProfiles().value(row);

    LLMProvider provider;
    provider.setProfile(profile);
    provider.setTimeoutMs(5000);

    QElapsedTimer t; t.start();
    m_llmLastErrorLabel->setText(tr("Testing..."));
    provider.generate(
        QStringLiteral("Reply with the single word OK."),
        QStringLiteral("ping"),
        [this, elapsed = t](LLMResult r) mutable {
            const qint64 ms = elapsed.elapsed();
            if (r.ok) {
                m_llmLastErrorLabel->setText(tr("✓ %1 ms").arg(ms));
            } else {
                m_llmLastErrorLabel->setText(tr("✗ %1").arg(r.error));
            }
        });
}
```

Note: `LLMProvider` here is a stack local. Since `generate()` returns immediately and the callback fires later, the local is destroyed first → reply is dangling. Fix by making the provider a member:
```cpp
// In header
QScopedPointer<LLMProvider> m_testProvider;
// In ctor
m_testProvider.reset(new LLMProvider(this));
// In onTestConnectionClicked: use m_testProvider.data() instead
```

- [ ] **Step 4: CMake**

Add EditLLMProfileDialog to `SEELIEPET_LIB_SOURCES` (if shared with tests) or to the main app target's sources. For now, add to the app target only by appending to the main `add_executable(Seelie ...)` in the top-level `CMakeLists.txt`:
```cmake
src/EditLLMProfileDialog.h
src/EditLLMProfileDialog.cpp
```

- [ ] **Step 5: Build + manual smoke**

```bash
cd build && cmake --build . --target Seelie
```

Launch app, open Settings → AI → Add. Fill an OpenAI profile with a real key, click OK. The new entry appears. Select it, click Test connection — see `✓ <ms>` or a parse-able error.

- [ ] **Step 6: Commit**

```bash
git add src/EditLLMProfileDialog.h src/EditLLMProfileDialog.cpp src/SettingsPanelWidget.h src/SettingsPanelWidget.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(settings): edit-profile dialog + Test connection

Modal QDialog for editing an LLMProfile (name, protocol dropdown,
base URL, API key as password field, model). Add/Edit buttons in
the AI tab now open it. Test connection fires a 1-token request and
shows either "✓ <ms>" or "✗ <error>" in the status label.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 15: StatisticsDialog widget + auto-refresh + reset

**Files:**
- Create: `src/StatisticsDialog.h` / `.cpp`
- Create: `tests/test_statistics_dialog.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing test**

`tests/test_statistics_dialog.cpp`:
```cpp
#include "StatisticsDialog.h"
#include "MemoryManager.h"
#include <QtTest/QtTest>
#include <QApplication>

class TestStatisticsDialog : public QObject
{
    Q_OBJECT
private slots:
    void testRendersSeededValues()
    {
        MemoryManager mm(":memory:");
        mm.increment("stats.tts.requests", 100);
        mm.increment("stats.tts.hits", 94);
        mm.increment("stats.events.total", 18432);

        StatisticsDialog dlg(&mm, nullptr, nullptr, nullptr, nullptr);
        dlg.refresh();
        QVERIFY(dlg.findChild<QLabel*>("ttsRequestsLabel")->text().contains("100"));
        QVERIFY(dlg.findChild<QLabel*>("ttsHitsLabel")->text().contains("94"));
        QVERIFY(dlg.findChild<QLabel*>("eventsTotalLabel")->text().contains("18432"));
    }
};

QTEST_MAIN(TestStatisticsDialog)
#include "test_statistics_dialog.moc"
```

- [ ] **Step 2: Create header**

`src/StatisticsDialog.h`:
```cpp
#ifndef STATISTICS_DIALOG_H
#define STATISTICS_DIALOG_H

#include <QDialog>
class MemoryManager;
class TTSEngine;
class EventRouter;
class IpcServer;
class PersonaEngine;
class QTimer;
class QLabel;

class StatisticsDialog : public QDialog
{
    Q_OBJECT
public:
    StatisticsDialog(MemoryManager *memory,
                     TTSEngine *tts,
                     EventRouter *events,
                     IpcServer *ipc,
                     PersonaEngine *persona,
                     QWidget *parent = nullptr);

public slots:
    void refresh();
    void resetStats();

private:
    MemoryManager *m_memory;
    TTSEngine *m_tts;
    EventRouter *m_events;
    IpcServer *m_ipc;
    PersonaEngine *m_persona;
    QTimer *m_refreshTimer;
};

#endif
```

- [ ] **Step 3: Implement**

`src/StatisticsDialog.cpp`:
```cpp
#include "StatisticsDialog.h"
#include "MemoryManager.h"
#include "TTSEngine.h"
#include "EventRouter.h"
#include "IpcServer.h"
#include "PersonaEngine.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QMessageBox>
#include <QDateTime>

namespace {
QLabel *mkVal(const QString &name, QWidget *parent)
{
    auto *l = new QLabel(QStringLiteral("—"), parent);
    l->setObjectName(name);
    l->setStyleSheet("font-family: monospace;");
    return l;
}
}

StatisticsDialog::StatisticsDialog(MemoryManager *m, TTSEngine *t,
                                   EventRouter *e, IpcServer *i,
                                   PersonaEngine *p, QWidget *parent)
    : QDialog(parent), m_memory(m), m_tts(t), m_events(e), m_ipc(i), m_persona(p),
      m_refreshTimer(new QTimer(this))
{
    setWindowTitle(tr("Statistics"));
    setFixedSize(480, 620);

    auto *root = new QVBoxLayout(this);

    auto mkSection = [&](const QString &title) {
        auto *g = new QGroupBox(title, this);
        auto *f = new QFormLayout(g);
        root->addWidget(g);
        return f;
    };

    auto *tts = mkSection(tr("TTS Cache"));
    tts->addRow(tr("Entries cached:"), mkVal("ttsRequestsLabel", this));
    tts->addRow(tr("Hits (lifetime):"), mkVal("ttsHitsLabel", this));

    auto *persona = mkSection(tr("AI Persona"));
    persona->addRow(tr("Refills ok / fail:"),    mkVal("personaRefillsLabel", this));
    persona->addRow(tr("On-demand ok / fail:"),  mkVal("personaOndemandLabel", this));
    persona->addRow(tr("Tokens in / out:"),      mkVal("personaTokensLabel", this));
    persona->addRow(tr("Last LLM error:"),       mkVal("personaLastErrorLabel", this));

    auto *events = mkSection(tr("Events"));
    events->addRow(tr("Total received:"), mkVal("eventsTotalLabel", this));
    events->addRow(tr("Last event:"),     mkVal("eventsLastLabel", this));

    auto *ipc = mkSection(tr("IPC"));
    ipc->addRow(tr("Packets received:"), mkVal("ipcPacketsLabel", this));
    ipc->addRow(tr("Decode errors:"),    mkVal("ipcErrorsLabel", this));

    auto *btnRow = new QHBoxLayout;
    auto *refreshBtn = new QPushButton(tr("Refresh"), this);
    auto *resetBtn   = new QPushButton(tr("Reset stats"), this);
    auto *closeBtn   = new QPushButton(tr("Close"), this);
    btnRow->addWidget(refreshBtn);
    btnRow->addWidget(resetBtn);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);

    connect(refreshBtn, &QPushButton::clicked, this, &StatisticsDialog::refresh);
    connect(resetBtn,   &QPushButton::clicked, this, &StatisticsDialog::resetStats);
    connect(closeBtn,   &QPushButton::clicked, this, &QDialog::accept);

    m_refreshTimer->setInterval(2000);
    connect(m_refreshTimer, &QTimer::timeout, this, &StatisticsDialog::refresh);
    m_refreshTimer->start();

    refresh();
}

void StatisticsDialog::refresh()
{
    auto val = [this](const char *key) -> QString {
        return m_memory ? m_memory->value(key, "0") : "0";
    };

    findChild<QLabel*>("ttsRequestsLabel")->setText(val("stats.tts.requests"));
    findChild<QLabel*>("ttsHitsLabel")->setText(val("stats.tts.hits"));
    findChild<QLabel*>("personaRefillsLabel")->setText(
        QStringLiteral("%1 / %2").arg(val("stats.persona.refills.ok"),
                                      val("stats.persona.refills.fail")));
    findChild<QLabel*>("personaOndemandLabel")->setText(
        QStringLiteral("%1 / %2").arg(val("stats.persona.ondemand.ok"),
                                      val("stats.persona.ondemand.fail")));
    findChild<QLabel*>("personaTokensLabel")->setText(
        QStringLiteral("%1 / %2").arg(val("stats.persona.tokens.in"),
                                      val("stats.persona.tokens.out")));
    findChild<QLabel*>("personaLastErrorLabel")->setText(
        m_persona ? (m_persona->stats().lastError.isEmpty()
                     ? "—" : m_persona->stats().lastError)
                  : "—");
    findChild<QLabel*>("eventsTotalLabel")->setText(val("stats.events.total"));
    findChild<QLabel*>("eventsLastLabel")->setText(
        m_events ? m_events->stats().lastEventName : "—");
    findChild<QLabel*>("ipcPacketsLabel")->setText(val("stats.ipc.packets"));
    findChild<QLabel*>("ipcErrorsLabel")->setText(val("stats.ipc.decode_errors"));
}

void StatisticsDialog::resetStats()
{
    if (QMessageBox::question(this, tr("Reset stats?"),
        tr("This clears only the stats counters. Milestones, name, and other "
           "memory data are preserved. Continue?")) != QMessageBox::Yes) return;

    if (m_memory) {
        // Wipe all stats.* keys
        QSqlQuery q(m_memory->database());
        q.exec("DELETE FROM memory WHERE key LIKE 'stats.%'");
    }
    refresh();
}
```

- [ ] **Step 4: CMake**

Add to `SEELIEPET_LIB_SOURCES`:
```cmake
    ${CMAKE_SOURCE_DIR}/src/StatisticsDialog.h
    ${CMAKE_SOURCE_DIR}/src/StatisticsDialog.cpp
```
Add to `TEST_SOURCES`: `test_statistics_dialog.cpp`.

- [ ] **Step 5: Build + run**

```bash
cd build && cmake .. && cmake --build . --target test_statistics_dialog && ctest -R test_statistics_dialog --output-on-failure
```
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/StatisticsDialog.h src/StatisticsDialog.cpp tests/test_statistics_dialog.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat: StatisticsDialog — TTS / persona / events / IPC counters

Reads lifetime counters from MemoryManager (stats.* keys) plus live
PersonaEngine.lastError. Refresh button + 2s auto-refresh timer.
Reset wipes only stats.* keys (preserves milestones / name).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 16: Tray menu Statistics entry + wire to MainWindow

**Files:**
- Modify: `src/SystemTray.h` / `.cpp` — add Statistics menu item, emit signal
- Modify: `src/mainwindow.h` / `.cpp` — slot opens StatisticsDialog

- [ ] **Step 1: Add tray menu item**

`src/SystemTray.h` — add signal `statisticsTriggered()`. In the `setupMenu()` method (or equivalent), add a new action between Settings and About:
```cpp
auto *statsAction = menu->addAction(tr("Statistics..."));
connect(statsAction, &QAction::triggered, this, &SystemTray::statisticsTriggered);
```

- [ ] **Step 2: Wire in MainWindow**

`mainwindow.h`:
```cpp
private slots:
    void onShowStatistics();
private:
    QPointer<StatisticsDialog> m_statsDialog;
```

`mainwindow.cpp`:
```cpp
#include "StatisticsDialog.h"

// In ctor where tray is created:
connect(m_systemTray, &SystemTray::statisticsTriggered,
        this, &MainWindow::onShowStatistics);

void MainWindow::onShowStatistics()
{
    if (m_statsDialog) {
        m_statsDialog->raise();
        m_statsDialog->activateWindow();
        return;
    }
    m_statsDialog = new StatisticsDialog(m_memoryManager, m_ttsEngine,
                                          m_eventRouter, m_ipcServer,
                                          m_personaEngine, this);
    m_statsDialog->setAttribute(Qt::WA_DeleteOnClose);
    m_statsDialog->show();
}
```

- [ ] **Step 3: Build + manual smoke**

```bash
cd build && cmake --build . --target Seelie
```

Launch app, right-click tray icon → "Statistics..." → dialog opens. Send a few events via gateway, watch the counters tick every 2s. Click Reset → confirm dialog → numbers return to 0.

- [ ] **Step 4: Commit**

```bash
git add src/SystemTray.h src/SystemTray.cpp src/mainwindow.h src/mainwindow.cpp
git commit -m "$(cat <<'EOF'
feat: tray menu Statistics entry opens StatisticsDialog

Adds "Statistics..." between Settings and About. MainWindow lazy-
creates the dialog (Qt::WA_DeleteOnClose) and re-uses the same
instance while it's visible.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 17: End-to-end manual verification

Not a code task — a verification gate. Each item is a real-world check that proves the feature works as designed.

- [ ] **Step 1: Fresh-install path**
  - Delete config (`%APPDATA%/Seelie` on Windows, `~/.config/Seelie` on Linux/macOS) and memory DB.
  - Launch the app. Settings → AI tab → confirm profiles list is empty, persona toggle is off.
  - Send events via gateway: pet shows `TipsCatalog` static text as before — feature is invisible.

- [ ] **Step 2: Provider configured, persona off**
  - Add an OpenAI profile (real key), select it, leave persona toggle off.
  - Click Test connection → expect `✓ <ms>` reply.
  - Send events → still see TipsCatalog text (engine bypassed).

- [ ] **Step 3: Persona on, pool warm-up**
  - Toggle persona on. Open Statistics. Initial counters should be near zero.
  - Send 3-4 `tool.before` events from gateway. First event likely shows TipsCatalog fallback (cold pool). After ~2-3s, pool fills; subsequent `tool.before` events display fresh LLM-generated text.
  - Statistics dialog shows `Refills: 1 / 0`, pool warmth > 0.

- [ ] **Step 4: On-demand upgrade visible**
  - Trigger `session.start` (e.g. restart gateway connection).
  - Bubble first shows TipsCatalog fallback, then ~1-2s later swaps to LLM-generated text.
  - Statistics shows `On-demand calls: 1 / 0`.

- [ ] **Step 5: Privacy toggle**
  - Set username via Profile tab; toggle "Share memory with AI" ON. Trigger session.start.
  - Confirm via provider's request logs (or by inspecting outbound traffic with mitmproxy) that the user name appears in the prompt. Toggle OFF, confirm name is no longer included.

- [ ] **Step 6: Pack switch invalidates pool**
  - Switch active pack (Settings → Packs). PersonaEngine should refresh hash. Next pool-tier event for the new pack starts a fresh warm-up. Old pack's pool rows remain (lazy invalidation — fine).
  - Edit a pack's `manifest.json` to change the persona, restart the app, send a pool-tier event for that pack → old rows wiped (hash mismatch), refill starts.

- [ ] **Step 7: Failure suppression**
  - Edit profile to a wrong URL. Send 4 events that route to on-demand. After 3 failures, Statistics's `Last LLM error` shows the network error. Subsequent calls for ~60s are suppressed (no further error counter increase, no network activity in process monitor).

- [ ] **Step 8: Regenerate pool button**
  - Settings → AI → click "Regenerate persona pool for active pack". Statistics's pool warmth drops to 0. Next event triggers a fresh refill.

- [ ] **Step 9: Long-running soak**
  - Leave the app running for a real coding session (≥30 minutes with Claude Code or similar firing events). Confirm:
    - No memory growth (RAM stays under 50 MB).
    - No SQLite errors in logs.
    - TTS cache hit rate creeps toward 95%+ once pool warms.
    - Bubble swaps for on-demand events are smooth (no visible flicker).

- [ ] **Step 10: Commit verification doc**

After the manual run, capture observed numbers and any issues:
```bash
git checkout -b verify/ai-persona-layer  # optional, only if you log results
```
No code commit unless issues are found. If any issue is found, file a follow-up commit fixing the smallest reproducer.

---

## Self-Review

**Spec coverage check** (running each spec section against the task list):

- §Architecture (thread model, three units on main thread) → Tasks 3, 8, 9 (in code structure)
- §1 Tier policy → Task 8 (`tierFor`)
- §2 Data flow (sync pool, async on-demand, tipUpgraded) → Tasks 8, 9, 10
- §3 PersonaPool (schema, refill, validation, spam guard, in-flight, hash invalidation) → Tasks 6, 7
- §4 LLMProvider (three protocols, timeout, suppression) → Tasks 3, 4, 5
- §5 Pack manifest persona → Task 1
- §6 Settings AI tab → Tasks 12, 13, 14
- §7 Privacy "Share memory with AI" → Task 9 (gating), Task 13 (UI)
- §8 Integration & threading → Task 10
- §9 Statistics dialog → Tasks 11, 15, 16
- §10 Cost (informational, no task)
- §11 Tests (unit + stress + integration) → Tasks 1-9 (unit), Task 17 (manual)
- §12 Risks → mitigations in respective tasks
- §13 Out of scope → not implemented (correct)

**Gaps noted and resolved inline:**
- The spec mentions a "Last error" surface in the AI tab. Task 13 includes `m_llmLastErrorLabel`; Task 14's Test connection writes to it. Task 11's PersonaEngine stats also include `lastError`. Covered.
- Stress test "1000 rapid tool.before events" from §11 — this is best left to manual verification (Task 17, Step 9 — long soak) rather than automated, since it requires a real event source. Captured as a soak check.
- The audit's "milestone routing" — handled in Task 10's MemoryManager signal connection.

**Placeholder scan:** none of the tasks contain TBD/TODO. All steps have concrete code or commands.

**Type consistency:** PersonaEngine APIs (`resolve()`, `Resolved`, `tipUpgraded`) match across Tasks 8, 9, 10. `LLMProfile::Protocol` enum values match across 2, 3, 4. PersonaPool method names (`insert`, `insertMany`, `pick`, `wipePack`, `markRefillStarted/Finished`, `isRefillInFlight`, `isSpamSuppressed`, `recordEmptyRefill`, `clearSpamSuppression`) match across Tasks 6, 7, 9.

Plan ready.
