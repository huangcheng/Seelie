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
