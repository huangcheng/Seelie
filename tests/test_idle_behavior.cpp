#include <QtTest>
#include "IdlePicker.h"
#include "SayingPool.h"
#include "ConfigManager.h"
#include <QSettings>
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

QTEST_MAIN(TestIdleBehavior)
#include "test_idle_behavior.moc"
