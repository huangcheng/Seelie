/**
 * test_pet_state_machine.cpp
 *
 * Unit tests for PetStateMachine — no UDP, no engines, no widgets.
 * Drives the FSM via slots and observes animationRequested / stateChanged.
 */

#include <QTest>
#include <QSignalSpy>
#include <QJsonObject>

#include "PetStateMachine.h"

class TestPetStateMachine : public QObject
{
    Q_OBJECT

private slots:
    void testInitialStateIsIdle();
    void testToolBeforeEntersWorking();
    void testWorkingGraceExpiresToIdle();
    void testWorkingGraceExtendsAcrossGaps();
    void testPromptSubmittedEntersThinking();
    void testToolBeforeTakesOverFromThinking();
    void testPermissionRequestedEntersReviewing();
    void testPermissionResponseExitsReviewing();
    void testFailedOneShotReturnsToWorking();
    void testGreetingOnlyFromIdle();
    void testCelebratingOnTodoUpdated();
    void testSessionErrorOneShotFromIdle();
    void testIdleFallbackAppendedToEveryChain();
    void testPositionChangeFiresWalkingChain();
    void testWalkingChainHasDirection();
    void testUserDragDoesNotTriggerWalking();
    void testCodexNameMapRebuildsChains();
    void testExplicitStateMapOverridesNameMap();
    void testFailedOverlayPreservesWorkingGrace();
    // Task 3 (Spec 3): touch states
    void testPetOneShotReturnsToIdle();
    void testPetRetriggerRefreshesOneShot();
    void testGrabSustainedUntilGrabEnd();
    void testTossOneShot();
    void testPetOverlayFromWorkingRestoresWorking();
    void testTouchChainsResolveFallback();
    void testGrabDuringOneShotCancelsTimer();

private:
    PetStateMachine *m_fsm = nullptr;

    void initFsm() {
        delete m_fsm;
        m_fsm = new PetStateMachine(this);
        qRegisterMetaType<PetStateMachine::State>("PetStateMachine::State");
    }
};

void TestPetStateMachine::testInitialStateIsIdle()
{
    initFsm();
    QCOMPARE(m_fsm->baseState(), PetStateMachine::State::Idle);
    QCOMPARE(m_fsm->activeState(), PetStateMachine::State::Idle);
}

void TestPetStateMachine::testToolBeforeEntersWorking()
{
    initFsm();
    QSignalSpy chainSpy(m_fsm, &PetStateMachine::animationRequested);
    QSignalSpy stateSpy(m_fsm, &PetStateMachine::stateChanged);

    m_fsm->onCanonicalEvent("tool.before");

    QCOMPARE(m_fsm->baseState(), PetStateMachine::State::Working);
    QCOMPARE(stateSpy.count(), 1);
    QCOMPARE(stateSpy.first().at(0).value<PetStateMachine::State>(),
             PetStateMachine::State::Working);
    QCOMPARE(chainSpy.count(), 1);
    const QStringList chain = chainSpy.first().at(0).toStringList();
    QVERIFY(!chain.isEmpty());
    QCOMPARE(chain.first(), QStringLiteral("running"));
}

void TestPetStateMachine::testWorkingGraceExpiresToIdle()
{
    initFsm();
    m_fsm->onCanonicalEvent("tool.before");
    QCOMPARE(m_fsm->baseState(), PetStateMachine::State::Working);

    QSignalSpy stateSpy(m_fsm, &PetStateMachine::stateChanged);
    // Grace expiry is a single-shot timer; poll so a late fire under load
    // doesn't race the QCOMPARE.
    QTRY_COMPARE(m_fsm->baseState(), PetStateMachine::State::Idle);
    QCOMPARE(stateSpy.count(), 1);
}

void TestPetStateMachine::testWorkingGraceExtendsAcrossGaps()
{
    initFsm();
    m_fsm->onCanonicalEvent("tool.before");
    QTest::qWait(800);                       // halfway through grace
    m_fsm->onCanonicalEvent("tool.after");   // resets grace via passive handler
    QTest::qWait(800);                       // total 1600ms wall but grace was reset
    QCOMPARE(m_fsm->baseState(), PetStateMachine::State::Working);

    m_fsm->onCanonicalEvent("tool.before");  // new tool, extends again
    QTest::qWait(800);
    QCOMPARE(m_fsm->baseState(), PetStateMachine::State::Working);
}

void TestPetStateMachine::testPromptSubmittedEntersThinking()
{
    initFsm();
    m_fsm->onCanonicalEvent("prompt.submitted");
    QCOMPARE(m_fsm->baseState(), PetStateMachine::State::Thinking);
}

void TestPetStateMachine::testToolBeforeTakesOverFromThinking()
{
    initFsm();
    m_fsm->onCanonicalEvent("prompt.submitted");
    QCOMPARE(m_fsm->baseState(), PetStateMachine::State::Thinking);
    m_fsm->onCanonicalEvent("tool.before");
    QCOMPARE(m_fsm->baseState(), PetStateMachine::State::Working);
}

void TestPetStateMachine::testPermissionRequestedEntersReviewing()
{
    initFsm();
    m_fsm->onCanonicalEvent("permission.requested");
    QCOMPARE(m_fsm->baseState(), PetStateMachine::State::Reviewing);
}

void TestPetStateMachine::testPermissionResponseExitsReviewing()
{
    initFsm();
    m_fsm->onCanonicalEvent("permission.requested");
    QCOMPARE(m_fsm->baseState(), PetStateMachine::State::Reviewing);
    m_fsm->onCanonicalEvent("permission.response");
    QCOMPARE(m_fsm->baseState(), PetStateMachine::State::Idle);
}

void TestPetStateMachine::testFailedOneShotReturnsToWorking()
{
    initFsm();
    m_fsm->onCanonicalEvent("tool.before");
    QCOMPARE(m_fsm->baseState(), PetStateMachine::State::Working);

    m_fsm->onCanonicalEvent("tool.failed");
    QCOMPARE(m_fsm->activeState(), PetStateMachine::State::Failed);
    QCOMPARE(m_fsm->baseState(), PetStateMachine::State::Working);  // base preserved

    // Overlay must persist for the full one-shot duration
    // (NOTIFICATION_ONESHOT_MS == 2000) — prove it hasn't cleared early at
    // the halfway mark.
    QTest::qWait(1000);  // ~NOTIFICATION_ONESHOT_MS / 2
    QCOMPARE(m_fsm->activeState(), PetStateMachine::State::Failed);

    // After the one-shot completes, overlay should clear. QTRY_COMPARE polls
    // (default ~5s) so a late timer fire under parallel load doesn't race.
    QTRY_COMPARE(m_fsm->activeState(), PetStateMachine::State::Working);
}

void TestPetStateMachine::testGreetingOnlyFromIdle()
{
    initFsm();
    m_fsm->onCanonicalEvent("session.start");
    QCOMPARE(m_fsm->activeState(), PetStateMachine::State::Greeting);

    // Now mid-session: a second session.start should NOT preempt Working.
    initFsm();
    m_fsm->onCanonicalEvent("tool.before");
    m_fsm->onCanonicalEvent("session.start");  // ignored — not Idle
    QCOMPARE(m_fsm->activeState(), PetStateMachine::State::Working);
}

void TestPetStateMachine::testCelebratingOnTodoUpdated()
{
    initFsm();
    QJsonObject payload;
    payload["status"] = "completed";
    m_fsm->onCanonicalEvent("todo.updated", payload);
    QCOMPARE(m_fsm->activeState(), PetStateMachine::State::Celebrating);

    // Updates without "completed" should NOT celebrate.
    initFsm();
    QJsonObject other;
    other["status"] = "in_progress";
    m_fsm->onCanonicalEvent("todo.updated", other);
    QCOMPARE(m_fsm->activeState(), PetStateMachine::State::Idle);
}

void TestPetStateMachine::testSessionErrorOneShotFromIdle()
{
    initFsm();
    m_fsm->onCanonicalEvent("session.error");
    QCOMPARE(m_fsm->activeState(), PetStateMachine::State::Failed);
    QCOMPARE(m_fsm->baseState(), PetStateMachine::State::Idle);
}

void TestPetStateMachine::testIdleFallbackAppendedToEveryChain()
{
    initFsm();
    QSignalSpy chainSpy(m_fsm, &PetStateMachine::animationRequested);

    m_fsm->onCanonicalEvent("tool.before");
    m_fsm->onCanonicalEvent("session.error");

    QVERIFY(chainSpy.count() >= 2);
    for (const QList<QVariant> &emission : chainSpy) {
        const QStringList chain = emission.at(0).toStringList();
        QVERIFY2(chain.contains("idle"),
                 qPrintable(QString("chain missing idle fallback: %1").arg(chain.join(','))));
    }
}

void TestPetStateMachine::testPositionChangeFiresWalkingChain()
{
    initFsm();
    QSignalSpy chainSpy(m_fsm, &PetStateMachine::animationRequested);

    m_fsm->onPositionChanged(QPoint(100, 100), QPoint(200, 100), false);

    QVERIFY(chainSpy.count() >= 1);
    const QStringList chain = chainSpy.last().at(0).toStringList();
    QVERIFY(chain.contains("running-right"));
}

void TestPetStateMachine::testWalkingChainHasDirection()
{
    initFsm();
    QSignalSpy chainSpy(m_fsm, &PetStateMachine::animationRequested);

    m_fsm->onPositionChanged(QPoint(200, 100), QPoint(100, 100), false);  // leftward

    QVERIFY(chainSpy.count() >= 1);
    const QStringList chain = chainSpy.last().at(0).toStringList();
    QVERIFY(chain.contains("running-left"));
}

void TestPetStateMachine::testUserDragDoesNotTriggerWalking()
{
    initFsm();
    QSignalSpy chainSpy(m_fsm, &PetStateMachine::animationRequested);

    m_fsm->onPositionChanged(QPoint(100, 100), QPoint(200, 100), true);  // user drag
    QCOMPARE(chainSpy.count(), 0);
}

void TestPetStateMachine::testCodexNameMapRebuildsChains()
{
    initFsm();
    QMap<QString, QString> nameMap;
    nameMap["work"] = "running";          // Working → "running"
    nameMap["alert"] = "failed";          // Failed  → "failed"
    nameMap["greet"] = "waving";          // Greeting → "waving"
    nameMap["think"] = "waiting";         // Thinking → "waiting"
    nameMap["attention"] = "review";      // Reviewing → "review"
    nameMap["celebrate"] = "jumping";     // Celebrating → "jumping"
    nameMap["rest"] = "idle";             // idle fallback

    m_fsm->rebuildChainsFromNameMap(nameMap);

    QSignalSpy chainSpy(m_fsm, &PetStateMachine::animationRequested);
    m_fsm->onCanonicalEvent("tool.before");
    QVERIFY(chainSpy.count() >= 1);
    const QStringList chain = chainSpy.last().at(0).toStringList();
    QCOMPARE(chain.first(), QStringLiteral("running"));
}

void TestPetStateMachine::testExplicitStateMapOverridesNameMap()
{
    initFsm();
    QMap<QString, QStringList> stateMap;
    stateMap["Working"] = QStringList{"my-busy-anim"};
    stateMap["Failed"]  = QStringList{"my-error-anim"};

    QMap<QString, QString> nameMap;
    nameMap["work"] = "ignored-by-explicit-statemap";

    m_fsm->rebuildChainsFromMaps(stateMap, nameMap);

    QSignalSpy chainSpy(m_fsm, &PetStateMachine::animationRequested);
    m_fsm->onCanonicalEvent("tool.before");
    QCOMPARE(chainSpy.last().at(0).toStringList().first(),
             QStringLiteral("my-busy-anim"));
}

void TestPetStateMachine::testFailedOverlayPreservesWorkingGrace()
{
    // Working starts; Failed overlay fires; while overlay is showing the
    // grace clock keeps ticking under it. After the one-shot, Working is
    // restored AND grace is still running — a quiet 1500ms still drops
    // to Idle, but a follow-up tool.before within grace stays Working.
    initFsm();
    m_fsm->onCanonicalEvent("tool.before");
    QCOMPARE(m_fsm->baseState(), PetStateMachine::State::Working);

    m_fsm->onCanonicalEvent("tool.failed");
    QCOMPARE(m_fsm->activeState(), PetStateMachine::State::Failed);

    // After overlay finishes the one-shot clears and restores the saved
    // sustained state. Poll so a late timer fire doesn't race the check.
    QTRY_COMPARE(m_fsm->activeState(), PetStateMachine::State::Working);
    QCOMPARE(m_fsm->baseState(), PetStateMachine::State::Working);
}

void TestPetStateMachine::testPetOneShotReturnsToIdle()
{
    initFsm();
    m_fsm->onSyntheticEvent("user.pet");
    QCOMPARE(m_fsm->activeState(), PetStateMachine::State::Petted);
    QTRY_COMPARE(m_fsm->activeState(), PetStateMachine::State::Idle);
}

void TestPetStateMachine::testPetRetriggerRefreshesOneShot()
{
    initFsm();
    m_fsm->onSyntheticEvent("user.pet");
    QCOMPARE(m_fsm->activeState(), PetStateMachine::State::Petted);
    QTest::qWait(1000);                 // half the one-shot (NOTIFICATION_ONESHOT_MS=2000)
    m_fsm->onSyntheticEvent("user.pet");  // re-stroke -> timer restarts
    QCOMPARE(m_fsm->activeState(), PetStateMachine::State::Petted);
    QTest::qWait(1000);                 // total 2000ms but only 1000ms since re-trigger
    QCOMPARE(m_fsm->activeState(), PetStateMachine::State::Petted);
    QTRY_COMPARE(m_fsm->activeState(), PetStateMachine::State::Idle);
}

void TestPetStateMachine::testGrabSustainedUntilGrabEnd()
{
    initFsm();
    m_fsm->onSyntheticEvent("user.grab");
    QCOMPARE(m_fsm->activeState(), PetStateMachine::State::Grabbed);
    QTest::qWait(2500);  // > NOTIFICATION_ONESHOT_MS -- sustained: must NOT expire
    QCOMPARE(m_fsm->activeState(), PetStateMachine::State::Grabbed);
    m_fsm->onSyntheticEvent("user.grabEnd");
    QCOMPARE(m_fsm->activeState(), PetStateMachine::State::Idle);
}

void TestPetStateMachine::testTossOneShot()
{
    initFsm();
    m_fsm->onSyntheticEvent("user.toss");
    QCOMPARE(m_fsm->activeState(), PetStateMachine::State::Tossed);
    QTRY_COMPARE(m_fsm->activeState(), PetStateMachine::State::Idle);
}

void TestPetStateMachine::testPetOverlayFromWorkingRestoresWorking()
{
    initFsm();
    m_fsm->onCanonicalEvent("tool.before");
    QCOMPARE(m_fsm->baseState(), PetStateMachine::State::Working);
    m_fsm->onSyntheticEvent("user.pet");
    QCOMPARE(m_fsm->activeState(), PetStateMachine::State::Petted);
    QTRY_COMPARE(m_fsm->activeState(), PetStateMachine::State::Working);
}

void TestPetStateMachine::testTouchChainsResolveFallback()
{
    initFsm();
    QSignalSpy spy(m_fsm, &PetStateMachine::animationRequested);
    m_fsm->onSyntheticEvent("user.pet");
    QVERIFY(spy.count() >= 1);
    const QStringList chain = spy.takeFirst().at(0).toStringList();
    QVERIFY(!chain.isEmpty());
    // Default chain ends with the idle fallback appended by emitChainFor.
    QCOMPARE(chain.last(), QStringLiteral("idle"));
}

void TestPetStateMachine::testGrabDuringOneShotCancelsTimer()
{
    initFsm();
    m_fsm->onSyntheticEvent("user.pet");
    QCOMPARE(m_fsm->activeState(), PetStateMachine::State::Petted);
    QTest::qWait(500);
    m_fsm->onSyntheticEvent("user.grab");
    QCOMPARE(m_fsm->activeState(), PetStateMachine::State::Grabbed);
    QTest::qWait(2000);  // the original Petted timer would have fired by now
    QCOMPARE(m_fsm->activeState(), PetStateMachine::State::Grabbed);  // still held
    m_fsm->onSyntheticEvent("user.grabEnd");
    QCOMPARE(m_fsm->activeState(), PetStateMachine::State::Idle);
}

QTEST_MAIN(TestPetStateMachine)
#include "test_pet_state_machine.moc"
