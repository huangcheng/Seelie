# Pet Aliveness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Seelie feel alive between events — random idle sayings (canned pools + occasional opt-in LLM quips) and livelier idle animation rotation (variable timing, anti-repeat) across all three animation engines.

**Architecture:** A new `IdleBehaviorEngine` (owned/wired in `MainWindow`, constructed in `main.cpp`) schedules saying bubbles behind strict idle gates; a new `SayingPool` serves categorized canned lines from the existing i18n tip bundles; occasional LLM quips ride the existing `PersonaEngine` OnDemand path via an internal `idle.quip` pseudo-event. Animation rotation upgrades (shared `IdlePicker` helpers: weighted pick with exclusion, uniform 1000–4000ms timeout) live inside each engine.

**Tech Stack:** C++17, Qt6 (Core/Gui/Widgets/Test), QSettings, qrc-bundled JSON.

**Spec:** `docs/superpowers/specs/2026-07-20-pet-aliveness-design.md`

**Recon corrections to the spec (verified in code, spec still directionally right):**
- Sprite idle pool (`SpriteAnimationEngine.cpp:152-179`) **already includes** the 5 `Idle*` variants, diagonal looks, `Hearing_1`, `CheckingSomething` — the "surprise pick" semantics already exist via weight-1 entries. Remaining sprite work: `buildNameMap()` +12 names, add `EmptyTrash` to the pool, variable timing, anti-repeat. No separate surprise mechanism is built.
- Live2D already has jittered idle timing (`Live2DAnimationEngine.cpp:923-929`) — only anti-repeat is added there.
- `m_activeBubbleRequestId` in MainWindow is never reset to 0, so it cannot serve as an "LLM upgrade in flight" gate. The saying gate uses: window visible + tip widget not visible + not suppressed + FSM state == Idle.
- Saying content ships at 5 entries per category per locale (spec said ~10; 5 is enough for anti-repeat to be meaningful and keeps bundles reviewable).

---

### Task 1: IdlePicker shared helpers

**Files:**
- Create: `src/IdlePicker.h`
- Test: `tests/test_idle_behavior.cpp` (created here, grown in later tasks)

- [ ] **Step 1: Write the failing test**

Create `tests/test_idle_behavior.cpp`:

```cpp
#include <QtTest>
#include "IdlePicker.h"

class TestIdleBehavior : public QObject
{
    Q_OBJECT
private slots:
    // --- IdlePicker ---
    void pickWeighted_respectsWeights();
    void pickWeighted_excludesIndex();
    void pickWeighted_zeroWeightsReturnMinus1();
    void idleTimeoutMs_bounds();
};

void TestIdleBehavior::pickWeighted_respectsWeights()
{
    // weights {1, 0, 3}: index 1 can never be picked; r in [0,0.25) → 0, else 2
    const QVector<int> w = {1, 0, 3};
    QCOMPARE(IdlePicker::pickWeighted(w, -1, 0.0), 0);
    QCOMPARE(IdlePicker::pickWeighted(w, -1, 0.24), 0);
    QCOMPARE(IdlePicker::pickWeighted(w, -1, 0.25), 2);
    QCOMPARE(IdlePicker::pickWeighted(w, -1, 0.999), 2);
}

void TestIdleBehavior::pickWeighted_excludesIndex()
{
    const QVector<int> w = {1, 1};
    // Excluding 0 must always yield 1 and vice versa.
    QCOMPARE(IdlePicker::pickWeighted(w, 0, 0.0), 1);
    QCOMPARE(IdlePicker::pickWeighted(w, 0, 0.99), 1);
    QCOMPARE(IdlePicker::pickWeighted(w, 1, 0.0), 0);
    // Excluding the only positive-weight entry → -1
    const QVector<int> single = {5};
    QCOMPARE(IdlePicker::pickWeighted(single, 0, 0.5), -1);
}

void TestIdleBehavior::pickWeighted_zeroWeightsReturnMinus1()
{
    const QVector<int> w = {0, 0, 0};
    QCOMPARE(IdlePicker::pickWeighted(w, -1, 0.5), -1);
    QCOMPARE(IdlePicker::pickWeighted(QVector<int>{}, -1, 0.5), -1);
}

void TestIdleBehavior::idleTimeoutMs_bounds()
{
    QCOMPARE(IdlePicker::idleTimeoutMs(0.0), 1000);
    QVERIFY(IdlePicker::idleTimeoutMs(0.999999) <= 4000);
    for (double r = 0.0; r < 1.0; r += 0.01) {
        const int t = IdlePicker::idleTimeoutMs(r);
        QVERIFY2(t >= 1000 && t <= 4000, qPrintable(QString::number(t)));
    }
    // Out-of-range r is clamped, never out of bounds.
    QVERIFY(IdlePicker::idleTimeoutMs(-0.5) >= 1000);
    QVERIFY(IdlePicker::idleTimeoutMs(1.5) <= 4000);
}

QTEST_MAIN(TestIdleBehavior)
#include "test_idle_behavior.moc"
```

- [ ] **Step 2: Register the test and verify it fails to build**

In `tests/CMakeLists.txt`, add `test_idle_behavior.cpp` to `TEST_SOURCES` (after `test_persona_engine.cpp`, line 127):

```cmake
    test_persona_engine.cpp
    test_idle_behavior.cpp
)
```

Run:
```bash
cd build && cmake .. -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)" && cmake --build . --target test_idle_behavior
```
Expected: FAIL — `IdlePicker.h: No such file or directory`.

- [ ] **Step 3: Implement IdlePicker.h**

Create `src/IdlePicker.h`:

```cpp
#ifndef IDLEPICKER_H
#define IDLEPICKER_H

#include <QVector>

// Shared idle-rotation helpers used by all three animation engines and the
// SayingPool. Pure functions so tests never wait on real timers.
namespace IdlePicker {

// Weighted pick from `weights`, skipping index `exclude` (pass -1 for no
// exclusion). `r` is uniform in [0,1). Returns -1 when no candidate has
// positive weight.
inline int pickWeighted(const QVector<int> &weights, int exclude, double r)
{
    int total = 0;
    for (int i = 0; i < weights.size(); ++i)
        if (i != exclude) total += weights.at(i);
    if (total <= 0) return -1;
    if (r < 0.0) r = 0.0;
    if (r >= 1.0) r = 0.999999999;
    const int roll = static_cast<int>(r * total);
    int cumulative = 0;
    for (int i = 0; i < weights.size(); ++i) {
        if (i == exclude) continue;
        cumulative += weights.at(i);
        if (roll < cumulative) return i;
    }
    return -1;
}

// Uniform idle gap in [1000, 4000] ms. `r` uniform in [0,1), clamped.
inline int idleTimeoutMs(double r)
{
    if (r < 0.0) r = 0.0;
    if (r >= 1.0) r = 0.999999;
    return 1000 + static_cast<int>(r * 3000);
}

} // namespace IdlePicker

#endif // IDLEPICKER_H
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd build && cmake --build . --target test_idle_behavior && ./tests/test_idle_behavior`
Expected: PASS, 4 test functions.

- [ ] **Step 5: Commit**

```bash
git add src/IdlePicker.h tests/test_idle_behavior.cpp tests/CMakeLists.txt
git commit -m "feat(idle): IdlePicker shared weighted-pick + idle-timeout helpers"
```

---

### Task 2: SayingPool + English sayings bundle

**Files:**
- Create: `src/SayingPool.h`, `src/SayingPool.cpp`
- Modify: `assets/i18n/tips.en.json` (add `"sayings"` section)
- Modify: `CMakeLists.txt` (add SayingPool.cpp to the shared lib sources — find the list that `SEELIEPET_LIB_SOURCES` is built from; add `src/SayingPool.cpp` next to `src/TipsCatalog.cpp`)
- Test: `tests/test_idle_behavior.cpp`

- [ ] **Step 1: Add sayings content to the en bundle**

In `assets/i18n/tips.en.json`, add a top-level `"sayings"` key (sibling of `"events"`, `"greetings"`, `"messages"`, `"touch"` — keep valid JSON, mind the comma after the previous last key):

```json
  "sayings": {
    "humor": [
      {"title": "Hmm", "body": "I counted your semicolons today. Impressive. Slightly alarming, but impressive."},
      {"title": "Psst", "body": "If I had a coin for every build, I'd have a very strange wallet."},
      {"title": "Fun fact", "body": "My favorite debugging technique is staring at you until the fix appears."},
      {"title": "Note", "body": "I did NOT touch your code while you weren't looking. Probably."},
      {"title": "Report", "body": "Status: sitting here, looking productive. One of us has to."}
    ],
    "encouragement": [
      {"title": "Hey", "body": "You've shipped harder things than this. Keep going."},
      {"title": "Reminder", "body": "Small steps still count as progress."},
      {"title": "For you", "body": "The bug is more afraid of you than you are of it."},
      {"title": "Cheering", "body": "Quiet focus looks good on you."},
      {"title": "Trust me", "body": "Future you is already grateful for this refactor."}
    ],
    "coding_wisdom": [
      {"title": "Wisdom", "body": "Make it work, make it right, make it fast — in that order."},
      {"title": "Wisdom", "body": "The best line of code is the one you didn't have to write."},
      {"title": "Wisdom", "body": "Name things like the next reader is you at 3 AM."},
      {"title": "Wisdom", "body": "If it's hard to test, it's probably doing too much."},
      {"title": "Wisdom", "body": "Commit early. Regret less."}
    ],
    "observation": [
      {"title": "Observing", "body": "It's been quiet. Suspiciously quiet."},
      {"title": "Observing", "body": "Your cursor has been very thoughtful lately."},
      {"title": "Observing", "body": "I watered your desktop icons while you were away. You're welcome."},
      {"title": "Observing", "body": "Somewhere out there, a CI pipeline is green because of you."},
      {"title": "Observing", "body": "This corner of the screen has excellent views."}
    ]
  }
```

- [ ] **Step 2: Write the failing tests**

Add to `tests/test_idle_behavior.cpp` (includes + slots; keep the existing IdlePicker slots):

```cpp
#include "SayingPool.h"
#include <QSettings>
```

New test slots:

```cpp
    // --- SayingPool ---
    void sayingPool_loadsEnBundle();
    void sayingPool_fallsBackToEn();
    void sayingPool_antiRepeat();
    void sayingPool_emptyIsSafe();
```

Implementations:

```cpp
void TestIdleBehavior::sayingPool_loadsEnBundle()
{
    SayingPool pool;
    QVERIFY(pool.load(QStringLiteral("en")));
    QVERIFY(!pool.isEmpty());
    QCOMPARE(pool.size(), 20);   // 4 categories x 5 sayings
    const SayingPool::Saying s = pool.pick();
    QVERIFY(!s.body.isEmpty());
    QVERIFY(!s.title.isEmpty());
}

void TestIdleBehavior::sayingPool_fallsBackToEn()
{
    SayingPool pool;
    // A locale with no bundled file must fall back to en, not come up empty.
    QVERIFY(pool.load(QStringLiteral("xx_YY")));
    QVERIFY(!pool.isEmpty());
}

void TestIdleBehavior::sayingPool_antiRepeat()
{
    SayingPool pool;
    QVERIFY(pool.load(QStringLiteral("en")));
    // Scripted RNG: always picks the last category (observation) and a fixed
    // position in it — two consecutive picks must differ.
    double r = 0.999;
    pool.setRngFn([&r] { return r; });
    const SayingPool::Saying a = pool.pick();
    const SayingPool::Saying b = pool.pick();
    QVERIFY(a.body != b.body);
}

void TestIdleBehavior::sayingPool_emptyIsSafe()
{
    SayingPool pool;   // never loaded
    QVERIFY(pool.isEmpty());
    const SayingPool::Saying s = pool.pick();   // must not crash
    QVERIFY(s.body.isEmpty());
}
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cd build && cmake --build . --target test_idle_behavior && ./tests/test_idle_behavior`
Expected: FAIL — `SayingPool.h: No such file or directory`.

- [ ] **Step 4: Implement SayingPool**

Create `src/SayingPool.h`:

```cpp
#ifndef SAYINGPOOL_H
#define SAYINGPOOL_H

#include <QString>
#include <QVector>
#include <functional>

// Categorized canned idle sayings, loaded from the "sayings" section of the
// existing i18n tip bundles (:/i18n/i18n/tips.<locale>.json). Weighted
// category draw + per-category anti-repeat. Locale files that lack the
// "sayings" key fall back to the en bundle.
class SayingPool
{
public:
    struct Saying { QString title; QString body; };

    SayingPool();

    // Returns true when at least one saying was loaded.
    bool load(const QString &locale);

    bool isEmpty() const;
    int size() const;   // total sayings across categories

    Saying pick();

    // Test seam: deterministic RNG in [0,1).
    void setRngFn(std::function<double()> fn) { m_rng = std::move(fn); }

private:
    struct Category {
        QString name;
        int weight = 1;
        QVector<Saying> sayings;
        int lastIndex = -1;
    };
    bool loadFile(const QString &path);

    QVector<Category> m_categories;
    std::function<double()> m_rng;
};

#endif // SAYINGPOOL_H
```

Create `src/SayingPool.cpp`:

```cpp
#include "SayingPool.h"
#include "IdlePicker.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QDebug>

SayingPool::SayingPool()
    : m_rng([] { return QRandomGenerator::global()->generateDouble(); })
{
}

bool SayingPool::load(const QString &locale)
{
    m_categories.clear();
    const QString path = QStringLiteral(":/i18n/i18n/tips.%1.json").arg(locale);
    if (!loadFile(path)) {
        if (locale == QLatin1String("en")) {
            qWarning() << "SayingPool: no sayings in en bundle — sayings disabled";
            return false;
        }
        return loadFile(QStringLiteral(":/i18n/i18n/tips.en.json"));
    }
    return !isEmpty();
}

bool SayingPool::isEmpty() const
{
    return size() == 0;
}

int SayingPool::size() const
{
    int n = 0;
    for (const auto &c : m_categories) n += c.sayings.size();
    return n;
}

SayingPool::Saying SayingPool::pick()
{
    if (m_categories.isEmpty()) return {};

    QVector<int> catWeights;
    for (const auto &c : m_categories) catWeights.append(c.weight);
    const int ci = IdlePicker::pickWeighted(catWeights, -1, m_rng());
    if (ci < 0) return {};

    Category &cat = m_categories[ci];
    QVector<int> sayWeights(cat.sayings.size(), 1);
    const int exclude = cat.sayings.size() > 1 ? cat.lastIndex : -1;
    int si = IdlePicker::pickWeighted(sayWeights, exclude, m_rng());
    if (si < 0) si = 0;
    cat.lastIndex = si;
    return cat.sayings.at(si);
}

bool SayingPool::loadFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    // Same sanity cap as TipsCatalog (M13) — refuse oversized files.
    constexpr qint64 kMaxBytes = 1 * 1024 * 1024;
    if (f.size() > kMaxBytes) {
        qWarning() << "SayingPool: refusing oversized file" << path;
        return false;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (!doc.isObject()) {
        qWarning() << "SayingPool: parse error in" << path << ":" << err.errorString();
        return false;
    }
    const QJsonObject sayings = doc.object().value(QStringLiteral("sayings")).toObject();
    if (sayings.isEmpty()) return false;

    const QVector<QPair<QString, int>> table = {
        {QStringLiteral("humor"), 3},
        {QStringLiteral("encouragement"), 3},
        {QStringLiteral("coding_wisdom"), 2},
        {QStringLiteral("observation"), 2},
    };
    for (const auto &[name, weight] : table) {
        const QJsonArray arr = sayings.value(name).toArray();
        if (arr.isEmpty()) continue;
        Category cat;
        cat.name = name;
        cat.weight = weight;
        for (const QJsonValue &v : arr) {
            const QJsonObject o = v.toObject();
            const QString body = o.value(QStringLiteral("body")).toString();
            if (body.isEmpty()) continue;
            cat.sayings.append({o.value(QStringLiteral("title")).toString(), body});
        }
        if (!cat.sayings.isEmpty()) m_categories.append(cat);
    }
    return !m_categories.isEmpty();
}
```

Add `src/SayingPool.cpp` to the library sources in the top-level `CMakeLists.txt` (the source list feeding `SEELIEPET_LIB_SOURCES`; add it adjacent to `src/TipsCatalog.cpp`).

- [ ] **Step 5: Run test to verify it passes**

Run: `cd build && cmake .. -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)" && cmake --build . --target test_idle_behavior && ./tests/test_idle_behavior`
Expected: PASS, 8 test functions.

- [ ] **Step 6: Commit**

```bash
git add src/SayingPool.h src/SayingPool.cpp assets/i18n/tips.en.json CMakeLists.txt tests/test_idle_behavior.cpp
git commit -m "feat(idle): SayingPool with categorized canned sayings (en)"
```

---

### Task 3: ConfigManager keys

**Files:**
- Modify: `src/ConfigManager.h`
- Modify: `src/ConfigManager.cpp` (load at ~line 168, flush at ~line 248, add setters near other setters)
- Test: `tests/test_idle_behavior.cpp`

- [ ] **Step 1: Write the failing tests**

Add slots to `tests/test_idle_behavior.cpp`:

```cpp
#include "ConfigManager.h"
#include <QTemporaryDir>
```

```cpp
    // --- ConfigManager keys ---
    void config_defaults();
    void config_roundTrip();
```

```cpp
void TestIdleBehavior::config_defaults()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tmp.path());
    ConfigManager cfg;
    cfg.load();
    QCOMPARE(cfg.sayingFrequency(), ConfigManager::SayingFrequency::Sometimes);
    QCOMPARE(cfg.llmIdleQuipsEnabled(), false);
}

void TestIdleBehavior::config_roundTrip()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tmp.path());
    {
        ConfigManager cfg;
        cfg.load();
        cfg.setSayingFrequency(ConfigManager::SayingFrequency::Often);
        cfg.setLLMIdleQuipsEnabled(true);
        cfg.flush();
    }
    {
        ConfigManager cfg2;
        cfg2.load();
        QCOMPARE(cfg2.sayingFrequency(), ConfigManager::SayingFrequency::Often);
        QCOMPARE(cfg2.llmIdleQuipsEnabled(), true);
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd build && cmake --build . --target test_idle_behavior`
Expected: compile FAIL — `sayingFrequency` / `llmIdleQuipsEnabled` / setters not members of `ConfigManager`.

- [ ] **Step 3: Implement ConfigManager changes**

In `src/ConfigManager.h`, after the `DisplayMode` enum (line 18):

```cpp
    enum class SayingFrequency { Never = 0, Rarely, Sometimes, Often };
    Q_ENUM(SayingFrequency)
```

After the `shareMemoryWithAi` accessors (lines 120-121):

```cpp
    /** Idle-sayings cadence. Default Sometimes. */
    SayingFrequency sayingFrequency() const { return m_sayingFrequency; }
    void setSayingFrequency(SayingFrequency freq);

    /** Whether idle sayings may occasionally be LLM-generated. Default false (cost opt-in). */
    bool llmIdleQuipsEnabled() const { return m_llmIdleQuipsEnabled; }
    void setLLMIdleQuipsEnabled(bool enabled);
```

In the `signals:` block, after `shareMemoryWithAiChanged`:

```cpp
    void sayingFrequencyChanged(SayingFrequency freq);
    void llmIdleQuipsEnabledChanged(bool enabled);
```

In the private members, after `m_shareMemoryWithAi`:

```cpp
    SayingFrequency m_sayingFrequency = SayingFrequency::Sometimes;
    bool m_llmIdleQuipsEnabled = false;
```

In `src/ConfigManager.cpp` `load()`, after the `m_tipBubblesEnabled` line (~line 168):

```cpp
    {
        const int f = m_settings.value("sayingFrequency",
                                       static_cast<int>(SayingFrequency::Sometimes)).toInt();
        m_sayingFrequency = static_cast<SayingFrequency>(
            qBound(0, f, static_cast<int>(SayingFrequency::Often)));
    }
    m_llmIdleQuipsEnabled = m_settings.value("llmIdleQuips", false).toBool();
```

In `flush()`, after the `m_settings.setValue("tipBubblesEnabled", ...)` line:

```cpp
    m_settings.setValue("sayingFrequency", static_cast<int>(m_sayingFrequency));
    m_settings.setValue("llmIdleQuips", m_llmIdleQuipsEnabled);
```

Add setters in `ConfigManager.cpp` (place next to `setShareMemoryWithAi`):

```cpp
void ConfigManager::setSayingFrequency(SayingFrequency freq)
{
    if (m_sayingFrequency == freq) return;
    m_sayingFrequency = freq;
    emit sayingFrequencyChanged(freq);
    save();
}

void ConfigManager::setLLMIdleQuipsEnabled(bool enabled)
{
    if (m_llmIdleQuipsEnabled == enabled) return;
    m_llmIdleQuipsEnabled = enabled;
    emit llmIdleQuipsEnabledChanged(enabled);
    save();
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd build && cmake --build . --target test_idle_behavior && ./tests/test_idle_behavior`
Expected: PASS, 10 test functions.

- [ ] **Step 5: Commit**

```bash
git add src/ConfigManager.h src/ConfigManager.cpp tests/test_idle_behavior.cpp
git commit -m "feat(idle): sayingFrequency + llmIdleQuipsEnabled config keys"
```

---

### Task 4: IdleBehaviorEngine — canned sayings path

**Files:**
- Create: `src/IdleBehaviorEngine.h`, `src/IdleBehaviorEngine.cpp`
- Modify: `CMakeLists.txt` (add `src/IdleBehaviorEngine.cpp` next to `src/SayingPool.cpp`)
- Test: `tests/test_idle_behavior.cpp`

- [ ] **Step 1: Write the failing tests**

Add slots:

```cpp
#include "IdleBehaviorEngine.h"
#include <QSignalSpy>
```

```cpp
    // --- IdleBehaviorEngine (canned path) ---
    void engine_noFireBeforeInterval();
    void engine_firesAfterInterval();
    void engine_eventResetsClock();
    void engine_gateBlocksSilently();
    void engine_neverDisables();
```

```cpp
// Helper: engine with fake clock/rng, canned-only (persona == nullptr).
// `now` starts at 1'000'000 ms so arithmetic stays positive.
struct EngineFixture {
    QTemporaryDir tmp;
    qint64 now = 1'000'000;
    double rng = 0.5;
    EngineFixture() {
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tmp.path());
    }
};

void TestIdleBehavior::engine_noFireBeforeInterval()
{
    EngineFixture fx;
    ConfigManager cfg; cfg.load();
    IdleBehaviorEngine engine(&cfg, nullptr);
    engine.setNowFn([&fx] { return fx.now; });
    engine.setRngFn([&fx] { return fx.rng; });
    engine.setCanShowGate([] { return true; });
    QVERIFY(engine.loadSayings(QStringLiteral("en")));
    engine.applyConfig();   // Sometimes → interval in [360000, 600000]
    QSignalSpy spy(&engine, &IdleBehaviorEngine::sayingReady);
    engine.tick();
    fx.now += 100'000;    // well under the shortest Sometimes interval
    engine.tick();
    QCOMPARE(spy.count(), 0);
}

void TestIdleBehavior::engine_firesAfterInterval()
{
    EngineFixture fx;
    ConfigManager cfg; cfg.load();
    IdleBehaviorEngine engine(&cfg, nullptr);
    engine.setNowFn([&fx] { return fx.now; });
    engine.setRngFn([&fx] { return fx.rng; });
    engine.setCanShowGate([] { return true; });
    QVERIFY(engine.loadSayings(QStringLiteral("en")));
    engine.applyConfig();
    QSignalSpy spy(&engine, &IdleBehaviorEngine::sayingReady);
    fx.now += 700'000;    // beyond the longest Sometimes interval (600000)
    engine.tick();
    QCOMPARE(spy.count(), 1);
    const QString body = spy.takeFirst().at(1).toString();
    QVERIFY(!body.isEmpty());
}

void TestIdleBehavior::engine_eventResetsClock()
{
    EngineFixture fx;
    ConfigManager cfg; cfg.load();
    IdleBehaviorEngine engine(&cfg, nullptr);
    engine.setNowFn([&fx] { return fx.now; });
    engine.setRngFn([&fx] { return fx.rng; });
    engine.setCanShowGate([] { return true; });
    QVERIFY(engine.loadSayings(QStringLiteral("en")));
    engine.applyConfig();
    QSignalSpy spy(&engine, &IdleBehaviorEngine::sayingReady);
    fx.now += 500'000;
    engine.onEventProcessed();      // event at t=+500s resets the clock
    fx.now += 400'000;              // 400s since the event < 480s interval — not enough
    engine.tick();
    QCOMPARE(spy.count(), 0);
    fx.now += 200'000;              // 600s since the event ≥ 480s — fires
    engine.tick();
    QCOMPARE(spy.count(), 1);
}

void TestIdleBehavior::engine_gateBlocksSilently()
{
    EngineFixture fx;
    ConfigManager cfg; cfg.load();
    IdleBehaviorEngine engine(&cfg, nullptr);
    engine.setNowFn([&fx] { return fx.now; });
    engine.setRngFn([&fx] { return fx.rng; });
    bool gateOpen = false;
    engine.setCanShowGate([&gateOpen] { return gateOpen; });
    QVERIFY(engine.loadSayings(QStringLiteral("en")));
    engine.applyConfig();
    QSignalSpy spy(&engine, &IdleBehaviorEngine::sayingReady);
    fx.now += 700'000;
    engine.tick();                  // gated → silent skip
    QCOMPARE(spy.count(), 0);
    fx.now += 700'000;              // no catch-up burst: still one slot max
    engine.tick();
    QCOMPARE(spy.count(), 0);
    gateOpen = true;
    fx.now += 700'000;
    engine.tick();
    QCOMPARE(spy.count(), 1);
}

void TestIdleBehavior::engine_neverDisables()
{
    EngineFixture fx;
    ConfigManager cfg; cfg.load();
    cfg.setSayingFrequency(ConfigManager::SayingFrequency::Never);
    IdleBehaviorEngine engine(&cfg, nullptr);
    engine.setNowFn([&fx] { return fx.now; });
    engine.setRngFn([&fx] { return fx.rng; });
    engine.setCanShowGate([] { return true; });
    QVERIFY(engine.loadSayings(QStringLiteral("en")));
    engine.applyConfig();
    QSignalSpy spy(&engine, &IdleBehaviorEngine::sayingReady);
    fx.now += 10'000'000;
    engine.tick();
    QCOMPARE(spy.count(), 0);
}
```

(Place the `EngineFixture` struct above the test-class implementation section.)

- [ ] **Step 2: Run test to verify it fails**

Run: `cd build && cmake --build . --target test_idle_behavior`
Expected: compile FAIL — `IdleBehaviorEngine.h: No such file or directory`.

- [ ] **Step 3: Implement IdleBehaviorEngine**

Create `src/IdleBehaviorEngine.h`:

```cpp
#ifndef IDLEBEHAVIORENGINE_H
#define IDLEBEHAVIORENGINE_H

#include <QObject>
#include <QTimer>
#include <functional>

#include "ConfigManager.h"
#include "SayingPool.h"

class PersonaEngine;

/**
 * Schedules idle sayings: canned lines from SayingPool by default, with an
 * occasional LLM quip via PersonaEngine ("idle.quip") when the user opted in.
 *
 * Sayings are the LOWEST bubble priority: a slot only fires when no event
 * occurred within the current interval AND the caller-supplied gate (pet
 * idle, bubble free, window visible) passes. Skipped slots are silent —
 * no catch-up bursts.
 *
 * Test seams mirror SystemContextEngine: injectable clock + RNG + tick().
 */
class IdleBehaviorEngine : public QObject
{
    Q_OBJECT
public:
    using NowFn  = std::function<qint64()>;
    using RngFn  = std::function<double()>;
    using GateFn = std::function<bool()>;

    IdleBehaviorEngine(ConfigManager *config, PersonaEngine *persona,
                       QObject *parent = nullptr);

    /** Combined "pet idle & bubble-free" gate, supplied by MainWindow. */
    void setCanShowGate(GateFn fn) { m_canShow = std::move(fn); }

    /** Call on every EventRouter::eventProcessed — resets the idle clock. */
    void onEventProcessed();

    /** Load sayings for a locale. false → sayings disabled until next load. */
    bool loadSayings(const QString &locale);

    /** Re-read frequency; starts/stops the scheduler. Call after loadSayings
     *  and on sayingFrequencyChanged. */
    void applyConfig();

    // --- Test seams ------------------------------------------------------
    void setNowFn(NowFn fn) { m_now = std::move(fn); }
    void setRngFn(RngFn fn) { m_rng = std::move(fn); }
    void tick();                       // drive one scheduler step manually
    SayingPool &pool() { return m_pool; }

signals:
    void sayingReady(const QString &title, const QString &body);

private slots:
    void onTimer() { attemptSlot(); }

private:
    void armTimer();
    int rollIntervalMs() const;
    void attemptSlot();
    void fireCanned();
    void fireQuip();
    void onQuipUpgraded(quint64 requestId, const QString &text);
    void onQuipFailed(quint64 requestId);

    ConfigManager *m_config;
    PersonaEngine *m_persona;
    SayingPool m_pool;
    GateFn m_canShow;
    NowFn m_now;
    RngFn m_rng;
    QTimer m_timer;
    qint64 m_lastEventAt = 0;
    int m_intervalMs = 0;
    quint64 m_pendingQuipId = 0;
    bool m_sayingsUsable = false;
};

#endif // IDLEBEHAVIORENGINE_H
```

Create `src/IdleBehaviorEngine.cpp`:

```cpp
#include "IdleBehaviorEngine.h"
#include "PersonaEngine.h"

#include <QDateTime>
#include <QRandomGenerator>

IdleBehaviorEngine::IdleBehaviorEngine(ConfigManager *config, PersonaEngine *persona,
                                       QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_persona(persona)
    , m_now([] { return QDateTime::currentMSecsSinceEpoch(); })
    , m_rng([] { return QRandomGenerator::global()->generateDouble(); })
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &IdleBehaviorEngine::onTimer);

    if (m_persona) {
        connect(m_persona, &PersonaEngine::tipUpgraded,
                this, &IdleBehaviorEngine::onQuipUpgraded);
        connect(m_persona, &PersonaEngine::tipUpgradeFailed,
                this, &IdleBehaviorEngine::onQuipFailed);
    }
}

bool IdleBehaviorEngine::loadSayings(const QString &locale)
{
    m_sayingsUsable = m_pool.load(locale);
    return m_sayingsUsable;
}

void IdleBehaviorEngine::applyConfig()
{
    m_timer.stop();
    if (!m_config || !m_sayingsUsable) return;
    if (m_config->sayingFrequency() == ConfigManager::SayingFrequency::Never) return;
    m_lastEventAt = m_now();
    m_intervalMs = rollIntervalMs();
    armTimer();
}

void IdleBehaviorEngine::onEventProcessed()
{
    m_lastEventAt = m_now();
    // Any real event also cancels a pending idle quip — the event's own
    // bubble wins.
    m_pendingQuipId = 0;
    if (m_timer.isActive()) {
        m_intervalMs = rollIntervalMs();
        armTimer();
    }
}

void IdleBehaviorEngine::tick()
{
    attemptSlot();
}

void IdleBehaviorEngine::armTimer()
{
    if (m_intervalMs > 0) m_timer.start(m_intervalMs);
}

int IdleBehaviorEngine::rollIntervalMs() const
{
    const double r = m_rng();
    switch (m_config ? m_config->sayingFrequency() : ConfigManager::SayingFrequency::Never) {
    case ConfigManager::SayingFrequency::Rarely:    // 12–20 min
        return static_cast<int>((12 * 60 + r * 8 * 60) * 1000);
    case ConfigManager::SayingFrequency::Sometimes: // 6–10 min
        return static_cast<int>((6 * 60 + r * 4 * 60) * 1000);
    case ConfigManager::SayingFrequency::Often:     // 2.5–4 min
        return static_cast<int>((150 + r * 90) * 1000);
    case ConfigManager::SayingFrequency::Never:
        break;
    }
    return 0;
}

void IdleBehaviorEngine::attemptSlot()
{
    if (!m_config || !m_sayingsUsable) return;
    if (m_config->sayingFrequency() == ConfigManager::SayingFrequency::Never) return;
    if (m_intervalMs <= 0) return;
    if (m_now() - m_lastEventAt < m_intervalMs) return;  // not idle long enough

    // Re-arm before the gate so a skipped slot can't stall the schedule.
    m_intervalMs = rollIntervalMs();
    armTimer();

    if (m_canShow && !m_canShow()) return;   // silent skip, no catch-up
    if (m_pool.isEmpty()) return;

    const bool quipAllowed = m_config->llmIdleQuipsEnabled() && m_persona;
    if (quipAllowed && m_rng() < 0.15) {
        fireQuip();
        return;
    }
    fireCanned();
}

void IdleBehaviorEngine::fireCanned()
{
    const SayingPool::Saying s = m_pool.pick();
    if (!s.body.isEmpty()) emit sayingReady(s.title, s.body);
}

void IdleBehaviorEngine::fireQuip()
{
    const PersonaEngine::Resolved r =
        m_persona->resolve(QStringLiteral("idle.quip"), QJsonObject{});
    if (r.requestId == 0) {
        // Persona disabled / no provider configured — stay canned.
        fireCanned();
        return;
    }
    m_pendingQuipId = r.requestId;
}

void IdleBehaviorEngine::onQuipUpgraded(quint64 requestId, const QString &text)
{
    if (requestId != m_pendingQuipId) return;
    m_pendingQuipId = 0;
    // Re-check the gate at delivery time — an event may have arrived while
    // the LLM call was in flight.
    if (m_canShow && !m_canShow()) return;
    if (!text.trimmed().isEmpty())
        emit sayingReady(tr("Idle musing"), text.trimmed());
}

void IdleBehaviorEngine::onQuipFailed(quint64 requestId)
{
    if (requestId != m_pendingQuipId) return;
    m_pendingQuipId = 0;
    // Silent fallback to a canned line, still behind the gate.
    if (m_canShow && !m_canShow()) return;
    fireCanned();
}
```

Add `src/IdleBehaviorEngine.cpp` to the library sources in `CMakeLists.txt`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cd build && cmake --build . --target test_idle_behavior && ./tests/test_idle_behavior`
Expected: PASS, 15 test functions.

- [ ] **Step 5: Commit**

```bash
git add src/IdleBehaviorEngine.h src/IdleBehaviorEngine.cpp CMakeLists.txt tests/test_idle_behavior.cpp
git commit -m "feat(idle): IdleBehaviorEngine scheduler with canned sayings path"
```

---

### Task 5: PersonaEngine `idle.quip` prompt

**Files:**
- Modify: `src/PersonaEngine.cpp` (`resolveOnDemand`, lines ~195-201)
- Test: `tests/test_idle_behavior.cpp`

- [ ] **Step 1: Write the failing test**

Add slots:

```cpp
#include "PersonaEngine.h"
```

```cpp
    // --- PersonaEngine idle.quip ---
    void persona_idleQuipIsOnDemand();
    void persona_idleQuipNoProfile();
```

```cpp
void TestIdleBehavior::persona_idleQuipIsOnDemand()
{
    QCOMPARE(PersonaEngine::tierFor(QStringLiteral("idle.quip")),
             PersonaEngine::Tier::OnDemand);
}

void TestIdleBehavior::persona_idleQuipNoProfile()
{
    EngineFixture fx;
    ConfigManager cfg; cfg.load();   // persona disabled, no profile
    PersonaEngine persona(nullptr, &cfg);
    const PersonaEngine::Resolved r =
        persona.resolve(QStringLiteral("idle.quip"), QJsonObject{});
    // No LLM call may be fired; caller falls back to canned.
    QCOMPARE(r.requestId, quint64(0));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd build && cmake --build . --target test_idle_behavior && ./tests/test_idle_behavior -functions`
Expected: the two new slots FAIL to compile or link (PersonaEngine usage is fine, but verify `persona_idleQuipIsOnDemand` currently passes — `tierFor` is automatic since `idle.quip` is not in `poolTierEvents()`. If both pass already, keep them as pinning tests and proceed.)

- [ ] **Step 3: Add the dedicated idle prompt**

In `src/PersonaEngine.cpp`, `resolveOnDemand()` (lines ~195-201), replace:

```cpp
    QString userPrompt;
    if (shareMemory) {
        userPrompt = QStringLiteral("Event: %1\nRecent events: %2\nReact in-character.")
                      .arg(eventName, recent.join(QStringLiteral(", ")));
    } else {
        userPrompt = QStringLiteral("Event: %1\nReact in-character.").arg(eventName);
    }
```

with:

```cpp
    QString userPrompt;
    if (eventName == QLatin1String("idle.quip")) {
        // Ambient one-liner, not an event reaction. The memory/name/bio
        // blocks below still apply behind the shareMemoryWithAi gate.
        userPrompt = QStringLiteral(
            "You're idling on the user's desktop. Say one short ambient "
            "in-character line — an observation, gentle humor, or quiet "
            "encouragement. No questions, no exclamation marks.");
    } else if (shareMemory) {
        userPrompt = QStringLiteral("Event: %1\nRecent events: %2\nReact in-character.")
                      .arg(eventName, recent.join(QStringLiteral(", ")));
    } else {
        userPrompt = QStringLiteral("Event: %1\nReact in-character.").arg(eventName);
    }
```

- [ ] **Step 4: Run tests**

Run: `cd build && cmake --build . --target test_idle_behavior && ./tests/test_idle_behavior && ./tests/test_persona_engine`
Expected: PASS (both suites — the prompt change must not break existing persona tests).

- [ ] **Step 5: Commit**

```bash
git add src/PersonaEngine.cpp tests/test_idle_behavior.cpp
git commit -m "feat(idle): dedicated idle.quip prompt in PersonaEngine"
```

---

### Task 6: Sprite engine — name map, EmptyTrash, variable timing, anti-repeat

**Files:**
- Modify: `src/SpriteAnimationEngine.h` (add `m_lastIdleAnim` member near line 113)
- Modify: `src/SpriteAnimationEngine.cpp` (`buildNameMap` lines 64-80, idle pool line 178, `startNextAnimation` line 483, `startIdleAnimation` lines 502-528)
- Test: `tests/test_idle_behavior.cpp`

- [ ] **Step 1: Write the failing test**

Add slot:

```cpp
#include "SpriteAnimationEngine.h"
#include <QDir>
```

```cpp
    // --- Sprite engine name map ---
    void sprite_newNamesResolve();
```

```cpp
void TestIdleBehavior::sprite_newNamesResolve()
{
    const QString assetsDir =
        QDir(QStringLiteral(SOURCE_DIR)).absoluteFilePath(QStringLiteral("assets"));
    SpriteAnimationEngine engine;
    QVERIFY(engine.loadAssets(assetsDir + QStringLiteral("/map.png"),
                              assetsDir + QStringLiteral("/animations.json")));

    const QVector<QPair<QString, QString>> cases = {
        {QStringLiteral("idle_head_scratch"), QStringLiteral("IdleHeadScratch")},
        {QStringLiteral("idle_finger_tap"),   QStringLiteral("IdleFingerTap")},
        {QStringLiteral("idle_eyebrow_raise"),QStringLiteral("IdleEyeBrowRaise")},
        {QStringLiteral("idle_rope_pile"),    QStringLiteral("IdleRopePile")},
        {QStringLiteral("idle_snooze"),       QStringLiteral("IdleSnooze")},
        {QStringLiteral("checking"),          QStringLiteral("CheckingSomething")},
        {QStringLiteral("empty_trash"),       QStringLiteral("EmptyTrash")},
        {QStringLiteral("hearing"),           QStringLiteral("Hearing_1")},
        {QStringLiteral("look_down_left"),    QStringLiteral("LookDownLeft")},
        {QStringLiteral("look_down_right"),   QStringLiteral("LookDownRight")},
        {QStringLiteral("look_up_left"),      QStringLiteral("LookUpLeft")},
        {QStringLiteral("look_up_right"),     QStringLiteral("LookUpRight")},
    };
    for (const auto &[publicName, internal] : cases) {
        // HighPriority preempts whatever is playing — no stop() needed
        // (stop() clears loaded state and would break subsequent plays).
        engine.playAnimation(publicName, SpriteAnimationEngine::HighPriority);
        QCOMPARE(engine.currentAnimation(), internal);
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd build && cmake --build . --target test_idle_behavior && ./tests/test_idle_behavior sprite_newNamesResolve`
Expected: FAIL — `currentAnimation()` is empty or a fallback for each new name.

- [ ] **Step 3: Implement sprite engine changes**

In `src/SpriteAnimationEngine.h`, after `int m_idleTimeoutMs = 3000;` (line 113):

```cpp
    QString m_lastIdleAnim;   // anti-repeat for idle picks
```

In `src/SpriteAnimationEngine.cpp` `buildNameMap()`, after the `m_nameMap["lookup"]` line (line 79):

```cpp
    // Personality / look-around animations — previously unreachable by name.
    m_nameMap["idle_head_scratch"]  = "IdleHeadScratch";
    m_nameMap["idle_finger_tap"]    = "IdleFingerTap";
    m_nameMap["idle_eyebrow_raise"] = "IdleEyeBrowRaise";
    m_nameMap["idle_rope_pile"]     = "IdleRopePile";
    m_nameMap["idle_snooze"]        = "IdleSnooze";
    m_nameMap["checking"]           = "CheckingSomething";
    m_nameMap["empty_trash"]        = "EmptyTrash";
    m_nameMap["hearing"]            = "Hearing_1";
    m_nameMap["look_down_left"]     = "LookDownLeft";
    m_nameMap["look_down_right"]    = "LookDownRight";
    m_nameMap["look_up_left"]       = "LookUpLeft";
    m_nameMap["look_up_right"]      = "LookUpRight";
```

In the hardcoded idle pool (after `{"Searching", 1},` at line 178), add:

```cpp
        {"EmptyTrash",        1},
```

In `startNextAnimation()` (lines 481-484), replace:

```cpp
    } else {
        m_playing = false;
        m_idleTimer.start();
    }
```

with:

```cpp
    } else {
        m_playing = false;
        // Variable 1–4s gap so idle motion doesn't feel metronomic.
        m_idleTimer.setInterval(IdlePicker::idleTimeoutMs(
            QRandomGenerator::global()->generateDouble()));
        m_idleTimer.start();
    }
```

Replace the weighted-draw body of `startIdleAnimation()` (lines 514-528) with:

```cpp
    const int exclude = m_idleAnims.size() > 1
                        ? m_idleAnims.indexOf(m_lastIdleAnim) : -1;
    int idx = IdlePicker::pickWeighted(m_idleWeights, exclude,
                                       QRandomGenerator::global()->generateDouble());
    if (idx < 0) idx = 0;
    m_lastIdleAnim = m_idleAnims.at(idx);
    playAnimation(m_lastIdleAnim, HighPriority);
```

(Keep the `m_idleAnims.isEmpty()` fallback block above it unchanged.) Add `#include "IdlePicker.h"` at the top of the file.

- [ ] **Step 4: Run tests**

Run: `cd build && cmake --build . --target test_idle_behavior && ./tests/test_idle_behavior && ./tests/test_ipc_animations`
Expected: PASS (both suites).

- [ ] **Step 5: Commit**

```bash
git add src/SpriteAnimationEngine.h src/SpriteAnimationEngine.cpp tests/test_idle_behavior.cpp
git commit -m "feat(idle): sprite engine — 12 new public names, EmptyTrash, variable timing, anti-repeat"
```

---

### Task 7: Lottie engine — variable timing + anti-repeat

**Files:**
- Modify: `src/LottieAnimationEngine.h` (add `m_lastIdleAnim` near line 103)
- Modify: `src/LottieAnimationEngine.cpp` (`startIdleAnimation` lines 275-298, idle re-arm at line 271)

- [ ] **Step 1: Inspect current code**

Read `src/LottieAnimationEngine.cpp` lines 250-300. Confirm: line 271's `m_idleTimer.start()` sits in the else-branch where the queue is empty (the re-arm point), and `startIdleAnimation()` does the weighted draw.

- [ ] **Step 2: Implement**

In `src/LottieAnimationEngine.h`, after `int m_idleTimeoutMs = 3000;`:

```cpp
    QString m_lastIdleAnim;   // anti-repeat for idle picks
```

In `src/LottieAnimationEngine.cpp`: add `#include "IdlePicker.h"` at the top.

At the re-arm site (line ~271), replace `m_idleTimer.start();` with:

```cpp
        m_idleTimer.setInterval(IdlePicker::idleTimeoutMs(
            QRandomGenerator::global()->generateDouble()));
        m_idleTimer.start();
```

Replace the weighted-draw body of `startIdleAnimation()` (the totalWeight/roll/cumulative loop) with:

```cpp
    const int exclude = m_idleAnims.size() > 1
                        ? m_idleAnims.indexOf(m_lastIdleAnim) : -1;
    int idx = IdlePicker::pickWeighted(m_idleWeights, exclude,
                                       QRandomGenerator::global()->generateDouble());
    if (idx < 0) idx = 0;
    m_lastIdleAnim = m_idleAnims.at(idx);
    playAnimation(m_lastIdleAnim, HighPriority);
```

(Keep the `m_idleAnims.isEmpty()` early-return above it unchanged.)

- [ ] **Step 3: Build and run tests**

Run: `cd build && cmake --build . && ctest -R "idle_behavior|ipc_animations" --output-on-failure`
Expected: PASS.

- [ ] **Step 4: Commit**

```bash
git add src/LottieAnimationEngine.h src/LottieAnimationEngine.cpp
git commit -m "feat(idle): Lottie engine — variable idle timing + anti-repeat"
```

---

### Task 8: Live2D engine — anti-repeat

**Files:**
- Modify: `src/Live2DAnimationEngine.h` (add `m_lastIdleAnim` near line 158)
- Modify: `src/Live2DAnimationEngine.cpp` (`startIdleAnimation` lines 933-951)

(Timing already jittered at lines 923-929 — leave it.)

- [ ] **Step 1: Implement**

In `src/Live2DAnimationEngine.h`, after `int m_idleTimeoutMs = 3000;`:

```cpp
    QString m_lastIdleAnim;   // anti-repeat for idle picks
```

In `src/Live2DAnimationEngine.cpp`: add `#include "IdlePicker.h"` at the top (inside the `#ifdef SEELIE_LIVE2D_SUPPORT` region, next to the other project includes).

Replace the body of `startIdleAnimation()` (lines 933-951) with:

```cpp
void Live2DAnimationEngine::startIdleAnimation()
{
    if (m_idleAnims.isEmpty()) return;

    const int exclude = m_idleAnims.size() > 1
                        ? m_idleAnims.indexOf(m_lastIdleAnim) : -1;
    int idx = IdlePicker::pickWeighted(m_idleWeights, exclude,
                                       QRandomGenerator::global()->generateDouble());
    if (idx < 0) return;
    m_lastIdleAnim = m_idleAnims.at(idx);
    playAnimation(m_lastIdleAnim, NormalPriority);
}
```

- [ ] **Step 2: Build and run tests**

Run: `cd build && cmake --build . && ctest --output-on-failure`
Expected: PASS, all suites.

- [ ] **Step 3: Commit**

```bash
git add src/Live2DAnimationEngine.h src/Live2DAnimationEngine.cpp
git commit -m "feat(idle): Live2D engine — idle anti-repeat"
```

---

### Task 9: MainWindow + main.cpp wiring

**Files:**
- Modify: `src/MainWindow.h` (member + setter, near `setPersonaEngine`)
- Modify: `src/mainwindow.cpp` (new `setIdleBehaviorEngine()`, after `setPersonaEngine` ~line 1093)
- Modify: `src/main.cpp` (construct after `personaEngine` at line 310, wire after line 408)

- [ ] **Step 1: Implement MainWindow side**

In `src/MainWindow.h`: add forward declaration `class IdleBehaviorEngine;` (next to `class PetStateMachine;` line 31), a public setter next to `setStateMachine` (line 71):

```cpp
    void setIdleBehaviorEngine(IdleBehaviorEngine *engine);
```

and a private member next to `m_stateMachine` (line 173):

```cpp
    IdleBehaviorEngine *m_idleEngine = nullptr;
```

In `src/mainwindow.cpp`, after `setPersonaEngine()` (ends ~line 1093), add:

```cpp
void MainWindow::setIdleBehaviorEngine(IdleBehaviorEngine *engine)
{
    m_idleEngine = engine;
    if (!m_idleEngine) return;

    // Saying gate: pet truly idle and bubble-free. Sayings are the lowest
    // bubble priority — any event bubble wins by last-write-wins.
    m_idleEngine->setCanShowGate([this] {
        if (!isVisible()) return false;
        if (m_tipWidget
            && (m_tipWidget->isVisible() || m_tipWidget->isSuppressed())) return false;
        if (m_stateMachine
            && m_stateMachine->activeState() != PetStateMachine::State::Idle) return false;
        return true;
    });

    if (m_eventRouter) {
        connect(m_eventRouter, &EventRouter::eventProcessed,
                m_idleEngine, [this](const QString &, const QJsonObject &) {
            if (m_idleEngine) m_idleEngine->onEventProcessed();
        });
    }

    connect(m_idleEngine, &IdleBehaviorEngine::sayingReady,
            this, [this](const QString &title, const QString &body) {
        if (!m_tipWidget) return;
        // StatusBubble: 6s auto-dismiss — ambient speech shouldn't linger
        // as long as actionable tips (12s).
        m_tipWidget->showBubble(title, body, TipWidget::StatusBubble);
    });

    if (m_config) {
        connect(m_config, &ConfigManager::sayingFrequencyChanged,
                m_idleEngine, [this](ConfigManager::SayingFrequency) {
            if (m_idleEngine) m_idleEngine->applyConfig();
        });
        connect(m_config, &ConfigManager::languageChanged,
                m_idleEngine, [this](const QString &lang) {
            if (m_idleEngine) {
                m_idleEngine->loadSayings(lang);
                m_idleEngine->applyConfig();
            }
        });
    }
}
```

Add `#include "IdleBehaviorEngine.h"` at the top of `mainwindow.cpp`.

- [ ] **Step 2: Implement main.cpp side**

In `src/main.cpp`, after line 310 (`PersonaEngine personaEngine(&memory, &config);`):

```cpp
    IdleBehaviorEngine idleEngine(&config, &personaEngine);
    idleEngine.loadSayings(config.language());
    idleEngine.applyConfig();
```

After line 408 (`w.setPersonaEngine(&personaEngine);`):

```cpp
    w.setIdleBehaviorEngine(&idleEngine);
```

Add `#include "IdleBehaviorEngine.h"` near line 46.

- [ ] **Step 3: Build and run full test suite**

Run: `cd build && cmake --build . && ctest --output-on-failure`
Expected: PASS, all suites (wiring is glue; existing suites must stay green).

- [ ] **Step 4: Manual smoke check**

Run: `open build/Seelie.app` (macOS). With default settings (Sometimes), leave the pet untouched ~7 minutes and confirm an idle saying bubble appears; trigger `seelie-gateway --source claude-code --event session.start` and confirm event bubbles are unaffected. Then quit the app.

- [ ] **Step 5: Commit**

```bash
git add src/MainWindow.h src/mainwindow.cpp src/main.cpp
git commit -m "feat(idle): wire IdleBehaviorEngine into MainWindow and main"
```

---

### Task 10: Settings UI

**Files:**
- Modify: `src/SettingsPanelWidget.h` (members; add `#include <QComboBox>` if not present)
- Modify: `src/SettingsPanelWidget.cpp` (Interaction group after Touch Reactions row ~line 654; LLM privacy group after line 883; retranslate ~line 1536)

- [ ] **Step 1: Sayings frequency combo (General → Interaction group)**

In `src/SettingsPanelWidget.h`, next to the other Interaction-group members:

```cpp
    QLabel *m_sayingsLabel = nullptr;
    QComboBox *m_sayingsCombo = nullptr;
```

In `src/SettingsPanelWidget.cpp`, after the Touch Reactions `addWidget` calls (~line 654-655):

```cpp
    m_sayingsLabel = new QLabel(tr("Idle Sayings"), m_contentWidget);
    m_sayingsLabel->setFont(harmonyFont(10));
    m_sayingsLabel->setStyleSheet("color: black; background: transparent;");
    m_sayingsLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_sayingsCombo = new QComboBox(m_contentWidget);
    m_sayingsCombo->addItem(tr("Never"),     static_cast<int>(ConfigManager::SayingFrequency::Never));
    m_sayingsCombo->addItem(tr("Rarely"),    static_cast<int>(ConfigManager::SayingFrequency::Rarely));
    m_sayingsCombo->addItem(tr("Sometimes"), static_cast<int>(ConfigManager::SayingFrequency::Sometimes));
    m_sayingsCombo->addItem(tr("Often"),     static_cast<int>(ConfigManager::SayingFrequency::Often));
    m_sayingsCombo->setCurrentIndex(static_cast<int>(m_config->sayingFrequency()));
    connect(m_sayingsCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        m_config->setSayingFrequency(static_cast<ConfigManager::SayingFrequency>(
            m_sayingsCombo->itemData(idx).toInt()));
    });
    connect(m_config, &ConfigManager::sayingFrequencyChanged,
            this, [this](ConfigManager::SayingFrequency f) {
        QSignalBlocker blocker(m_sayingsCombo);
        m_sayingsCombo->setCurrentIndex(static_cast<int>(f));
    });

    interactGrid->addWidget(m_sayingsLabel, 3, 0, Qt::AlignLeft | Qt::AlignVCenter);
    interactGrid->addWidget(m_sayingsCombo, 3, 1, Qt::AlignLeft | Qt::AlignVCenter);
```

- [ ] **Step 2: LLM idle quips checkbox (LLM tab privacy group)**

In `src/SettingsPanelWidget.h`, next to `m_shareMemoryCheck`:

```cpp
    CheckMarkBox *m_llmIdleQuipsCheck = nullptr;
```

In `src/SettingsPanelWidget.cpp`, immediately after `privLayout->addWidget(m_shareMemoryCheck);` (line 883):

```cpp
        m_llmIdleQuipsCheck = new CheckMarkBox(tr("Occasional AI idle quips"), privacyGroup);
        m_llmIdleQuipsCheck->setToolTip(tr("When the pet is idle, it may occasionally send a short prompt (plus your memory digest if 'Share memory with AI' is on) to the configured AI provider to generate a fresh quip."));
        m_llmIdleQuipsCheck->setStyleSheet(m_autoStartCheck->styleSheet());
        m_llmIdleQuipsCheck->setChecked(m_config->llmIdleQuipsEnabled());
        privLayout->addWidget(m_llmIdleQuipsCheck);
        connect(m_llmIdleQuipsCheck, &QCheckBox::toggled,
                this, [this](bool on) { m_config->setLLMIdleQuipsEnabled(on); });
        connect(m_config, &ConfigManager::llmIdleQuipsEnabledChanged,
                this, [this](bool on) {
            QSignalBlocker blocker(m_llmIdleQuipsCheck);
            m_llmIdleQuipsCheck->setChecked(on);
        });
```

In the retranslate block (next to the `m_shareMemoryCheck` re-set at ~line 1536), add:

```cpp
    if (m_llmIdleQuipsCheck) {
        m_llmIdleQuipsCheck->setText(tr("Occasional AI idle quips"));
        m_llmIdleQuipsCheck->setToolTip(tr("When the pet is idle, it may occasionally send a short prompt (plus your memory digest if 'Share memory with AI' is on) to the configured AI provider to generate a fresh quip."));
    }
    if (m_sayingsLabel) m_sayingsLabel->setText(tr("Idle Sayings"));
```

(Note: combo items are populated at construction; on language switch they keep the old locale until restart — same limitation as other enum combos in this panel. Acceptable; do not add re-population logic.)

- [ ] **Step 3: Build and run full test suite**

Run: `cd build && cmake --build . && ctest --output-on-failure`
Expected: PASS.

- [ ] **Step 4: Commit**

```bash
git add src/SettingsPanelWidget.h src/SettingsPanelWidget.cpp
git commit -m "feat(idle): settings UI — sayings frequency combo + AI idle quips toggle"
```

---

### Task 11: zh_CN sayings + translation strings

**Files:**
- Modify: `assets/i18n/tips.zh_CN.json` (add `"sayings"` section)
- Modify: `Seelie_zh_CN.ts` (regenerate + translate new strings)
- Test: `tests/test_idle_behavior.cpp` (add zh bundle load test)

- [ ] **Step 1: Add zh_CN sayings**

In `assets/i18n/tips.zh_CN.json`, add a top-level `"sayings"` key (same position as in the en file):

```json
  "sayings": {
    "humor": [
      {"title": "嗯", "body": "我数了数你今天写的分号。很厉害,也有点吓人。"},
      {"title": "嘘", "body": "如果每次编译我都能得到一块钱,那我的钱包一定很奇怪。"},
      {"title": "冷知识", "body": "我最拿手的调试方法是盯着你看,直到答案出现。"},
      {"title": "声明", "body": "你不在的时候我没有动过你的代码。大概吧。"},
      {"title": "汇报", "body": "当前状态:坐在这里假装很忙。总得有人装一下。"}
    ],
    "encouragement": [
      {"title": "嘿", "body": "你搞定过比这更难的事。继续。"},
      {"title": "提醒", "body": "小步走也算前进。"},
      {"title": "送给你", "body": "这个 bug 怕你的程度,超过你怕它。"},
      {"title": "加油", "body": "专注的你看起来状态很好。"},
      {"title": "相信我", "body": "未来的你会感谢这次重构。"}
    ],
    "coding_wisdom": [
      {"title": "心得", "body": "先能跑,再写好,最后写快。顺序别乱。"},
      {"title": "心得", "body": "最好的代码,是那行你不用写的代码。"},
      {"title": "心得", "body": "起名字的时候,想象读者是凌晨三点的自己。"},
      {"title": "心得", "body": "难测试的代码,多半是职责太多了。"},
      {"title": "心得", "body": "早提交,少后悔。"}
    ],
    "observation": [
      {"title": "观察中", "body": "好安静。安静得有点可疑。"},
      {"title": "观察中", "body": "你的光标最近沉思的时间变多了。"},
      {"title": "观察中", "body": "你离开的时候我给你的桌面图标浇了水。不客气。"},
      {"title": "观察中", "body": "在世界的某个角落,有一条 CI 因你而绿。"},
      {"title": "观察中", "body": "屏幕这个角落的风景真不错。"}
    ]
  }
```

- [ ] **Step 2: Add a zh bundle load test**

Add slot to `tests/test_idle_behavior.cpp`:

```cpp
    void sayingPool_loadsZhBundle();
```

```cpp
void TestIdleBehavior::sayingPool_loadsZhBundle()
{
    SayingPool pool;
    QVERIFY(pool.load(QStringLiteral("zh_CN")));
    QCOMPARE(pool.size(), 20);
    const SayingPool::Saying s = pool.pick();
    QVERIFY(!s.body.isEmpty());
}
```

Run: `cd build && cmake --build . --target test_idle_behavior && ./tests/test_idle_behavior`
Expected: PASS, 19 test functions.

- [ ] **Step 3: Regenerate and translate .ts**

Run:
```bash
cd build && cmake --build . --target update_translations 2>/dev/null || lupdate ../src -ts ../Seelie_zh_CN.ts
```

Open `Seelie_zh_CN.ts` and provide zh_CN translations for the new strings: `"Idle Sayings"`, `"Never"`, `"Rarely"`, `"Sometimes"`, `"Often"`, `"Occasional AI idle quips"`, its tooltip, and `"Idle musing"`. Suggested:

| Source | Translation |
|---|---|
| Idle Sayings | 闲聊语录 |
| Never | 从不 |
| Rarely | 很少 |
| Sometimes | 偶尔 |
| Often | 经常 |
| Occasional AI idle quips | 偶尔让 AI 即兴发挥 |
| When the pet is idle, it may occasionally send a short prompt… | 宠物空闲时,可能会偶尔向已配置的 AI 服务商发送一条短提示(若开启了"与 AI 共享记忆"还会附带记忆摘要),用于生成一句新的闲聊。 |
| Idle musing | 随想 |

Rebuild: `cd build && cmake --build .` (the .qm is regenerated by the build).

- [ ] **Step 4: Commit**

```bash
git add assets/i18n/tips.zh_CN.json Seelie_zh_CN.ts tests/test_idle_behavior.cpp
git commit -m "feat(idle): zh_CN sayings bundle + UI translations"
```

---

### Task 12: Final verification + openspec archive

**Files:**
- Modify: `TODO.md` (note the change shipped)
- Archive: `openspec/changes/random-sayings`, `openspec/changes/random-idle-animations`

- [ ] **Step 1: Full suite green**

Run: `cd build && ctest --output-on-failure`
Expected: all suites PASS.

- [ ] **Step 2: Manual smoke (per Task 9 Step 4 if not already done)**

Frequency = Often (settings → Idle Sayings), confirm a saying appears within ~4 minutes of no events; enable "Occasional AI idle quips" with a configured LLM profile and confirm quips also arrive (they will be rare — ~15% of slots — so this check can be deferred to real usage; note it in TODO.md).

- [ ] **Step 3: Archive absorbed openspec changes**

Both proposals are fully absorbed by this change (with the persona-era amendments recorded in the spec). Follow the openspec archive workflow (`openspec-archive-change` skill) for `random-sayings` and `random-idle-animations`.

- [ ] **Step 4: Update TODO.md**

Add a short entry: Pet Aliveness shipped (sayings + idle rotation), with the deferred check "smoke-test LLM idle quips with a real profile" next to the existing embedding smoke-test item.

- [ ] **Step 5: Commit**

```bash
git add TODO.md openspec/
git commit -m "docs: pet aliveness shipped; archive absorbed openspec proposals"
```

---

## Self-review notes

- **Spec coverage:** all spec sections map to tasks — SayingPool (T2), config keys (T3), engine scheduler + gates + LLM roll (T4, T5), sprite work (T6), Lottie (T7), Live2D (T8), settings UI (T10), zh_CN (T11), error handling (SayingPool fallback T2, no-profile fallback T4/T5, quip-failure canned fallback T4, config defaults T3), testing (per-task). Preemption-by-last-write-wins is documented in T9 rather than implemented (it's existing behavior).
- **Deviations from spec** (recon-verified, listed in the header): no separate surprise mechanism (weight-1 pool entries already provide it), Live2D timing untouched (already jittered), no "upgrade in flight" gate (no reliable flag; visibility gate covers the common case), 5 sayings/category instead of ~10.
- **Type consistency:** `SayingPool::Saying{title, body}`, `ConfigManager::SayingFrequency`, `IdleBehaviorEngine::{setCanShowGate, onEventProcessed, loadSayings, applyConfig, tick, sayingReady}`, `IdlePicker::{pickWeighted, idleTimeoutMs}` are used identically across tasks.
