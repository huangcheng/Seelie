#include <QtTest>
#include "IdlePicker.h"
#include "SayingPool.h"
#include "ConfigManager.h"
#include "IdleBehaviorEngine.h"
#include "PersonaEngine.h"
#include "SpriteAnimationEngine.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>

class TestIdleBehavior : public QObject
{
    Q_OBJECT
private slots:
    // --- IdlePicker ---
    void pickWeighted_respectsWeights();
    void pickWeighted_excludesIndex();
    void pickWeighted_zeroWeightsReturnMinus1();
    void idleTimeoutMs_bounds();
    // --- SayingPool ---
    void sayingPool_loadsEnBundle();
    void sayingPool_fallsBackToEn();
    void sayingPool_antiRepeat();
    void sayingPool_emptyIsSafe();
    // --- ConfigManager keys ---
    void config_defaults();
    void config_roundTrip();
    // --- IdleBehaviorEngine (canned path) ---
    void engine_noFireBeforeInterval();
    void engine_firesAfterInterval();
    void engine_eventResetsClock();
    void engine_gateBlocksSilently();
    void engine_neverDisables();
    // --- PersonaEngine idle.quip ---
    void persona_idleQuipIsOnDemand();
    void persona_idleQuipNoProfile();
    // --- Sprite engine name map ---
    void sprite_newNamesResolve();
};

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
    QCOMPARE(IdlePicker::idleTimeoutMs(0.999999), 4000);
    QCOMPARE(IdlePicker::idleTimeoutMs(1.0), 4000);
    for (double r = 0.0; r < 1.0; r += 0.01) {
        const int t = IdlePicker::idleTimeoutMs(r);
        QVERIFY2(t >= 1000 && t <= 4000, qPrintable(QString::number(t)));
    }
    // Out-of-range r is clamped, never out of bounds.
    QVERIFY(IdlePicker::idleTimeoutMs(-0.5) >= 1000);
    QVERIFY(IdlePicker::idleTimeoutMs(1.5) <= 4000);
}

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

void TestIdleBehavior::sprite_newNamesResolve()
{
    // Locate sprite assets mirroring test_ipc_animations.cpp's findAssetsDir():
    // assets/ → SOURCE_DIR/assets → assets/packs/<name>/sprites/. The legacy
    // top-level assets/map.png was removed; the sheet now ships inside packs.
    auto findSpriteRoot = []() -> QString {
        const auto hasAssets = [](const QString &dir) {
            return QFile::exists(dir + QStringLiteral("/map.png"))
                && QFile::exists(dir + QStringLiteral("/animations.json"));
        };
        QDir dir(QCoreApplication::applicationDirPath());
        for (int i = 0; i < 6; ++i) {
            const QString candidate = dir.absoluteFilePath(QStringLiteral("assets"));
            if (hasAssets(candidate)) return candidate;
            if (!dir.cdUp()) break;
        }
        const QString srcAssets =
            QDir(QStringLiteral(SOURCE_DIR)).absoluteFilePath(QStringLiteral("assets"));
        if (hasAssets(srcAssets)) return srcAssets;
        QDir packsDir(srcAssets + QStringLiteral("/packs"));
        if (packsDir.exists()) {
            const QStringList packs = packsDir.entryList(
                QStringList(), QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            for (const QString &pack : packs) {
                const QString sprites =
                    packsDir.absoluteFilePath(pack + QStringLiteral("/sprites"));
                if (hasAssets(sprites)) return sprites;
            }
        }
        return QString();
    };

    const QString assetsDir = findSpriteRoot();
    QVERIFY2(!assetsDir.isEmpty(), "Could not locate sprite assets directory");

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

QTEST_MAIN(TestIdleBehavior)
#include "test_idle_behavior.moc"
