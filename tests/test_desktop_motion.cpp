#include <QtTest>
#include "DesktopMotionController.h"
#include "DesktopGeometry.h"

class TestDesktopMotion : public QObject
{
    Q_OBJECT
private slots:
    void disabledTickStaysIdle();
    void wanderAfterCooldownPicksWalkClip();
    void perchTimerHopOffReturnsIdle();
    void perchedWindowMoveFallsToShelf();
    void workingCancelsWanderNotFall();
    void shelfFromGeometriesUsesTaskbarTop();
};

struct MotionFixture {
    qint64 now = 1'000'000;
    double rng = 0.5;
    int rngCall = 0;
    DesktopMotionController ctrl;
    QRect screen{0, 0, 1920, 1080};
    DesktopMotionController::WindowGeom activeWindow;

    MotionFixture()
    {
        ctrl.setNowFn([this] { return now; });
        ctrl.setRngFn([this] {
            ++rngCall;
            return rng;
        });
        ctrl.setScreenFn([this] { return screen; });
        ctrl.setActiveWindowFn([this] { return activeWindow; });
        ctrl.setShelfFn([this](const QRect &) {
            return DesktopMotionController::ShelfTarget{QPoint(960, 1000)};
        });
        ctrl.setPetRect(QRect(500, 400, 124, 200));
        ctrl.onFsmState(QStringLiteral("Idle"));
    }

    void advanceWanderCooldown()
    {
        now += 25'000;
        ctrl.tick();
    }
};

void TestDesktopMotion::disabledTickStaysIdle()
{
    MotionFixture f;
    f.ctrl.tick();
    QCOMPARE(f.ctrl.mode(), DesktopMotionController::Mode::Idle);
    QVERIFY(f.ctrl.requestedClip().isEmpty());
}

void TestDesktopMotion::wanderAfterCooldownPicksWalkClip()
{
    MotionFixture f;
    f.rng = 0.5; // skip perch (needs < 0.08)
    f.ctrl.setEnabled(true);
    f.advanceWanderCooldown();

    QCOMPARE(f.ctrl.mode(), DesktopMotionController::Mode::Wandering);
    QCOMPARE(f.ctrl.requestedClip(), QStringLiteral("walk_right"));
    QVERIFY(f.ctrl.targetPetRect().x() > f.ctrl.petRect().x() - 1);
}

void TestDesktopMotion::perchTimerHopOffReturnsIdle()
{
    MotionFixture f;
    f.activeWindow = {QRect(800, 300, 400, 300), 42};
    f.ctrl.setForcePerch(true);
    f.ctrl.setEnabled(true);
    f.advanceWanderCooldown();

    QCOMPARE(f.ctrl.mode(), DesktopMotionController::Mode::Perched);
    QCOMPARE(f.ctrl.requestedClip(), QStringLiteral("sit"));

    f.now += 25'000;
    f.ctrl.tick();
    QCOMPARE(f.ctrl.mode(), DesktopMotionController::Mode::HoppingOff);
    QCOMPARE(f.ctrl.requestedClip(), QStringLiteral("hop_off"));

    for (int i = 0; i < 200 && f.ctrl.mode() == DesktopMotionController::Mode::HoppingOff; ++i) {
        f.now += 50;
        f.ctrl.tick();
    }
    QCOMPARE(f.ctrl.mode(), DesktopMotionController::Mode::Idle);
}

void TestDesktopMotion::perchedWindowMoveFallsToShelf()
{
    MotionFixture f;
    f.activeWindow = {QRect(800, 300, 400, 300), 42};
    f.ctrl.setForcePerch(true);
    f.ctrl.setEnabled(true);
    f.advanceWanderCooldown();
    QCOMPARE(f.ctrl.mode(), DesktopMotionController::Mode::Perched);

    f.activeWindow.frame = QRect(900, 350, 400, 300);
    f.ctrl.tick();
    QCOMPARE(f.ctrl.mode(), DesktopMotionController::Mode::Falling);
    QCOMPARE(f.ctrl.requestedClip(), QStringLiteral("fall"));

    const int startY = f.ctrl.petRect().y();
    for (int i = 0; i < 300 && f.ctrl.mode() == DesktopMotionController::Mode::Falling; ++i) {
        f.now += 50;
        f.ctrl.tick();
    }
    QCOMPARE(f.ctrl.mode(), DesktopMotionController::Mode::Idle);
    QCOMPARE(f.ctrl.requestedClip(), QStringLiteral("land"));
    QVERIFY(f.ctrl.petRect().y() > startY);
    QCOMPARE(f.ctrl.petRect().y(), 1000 - 200);
}

void TestDesktopMotion::workingCancelsWanderNotFall()
{
    MotionFixture f;
    f.rng = 0.5;
    f.ctrl.setEnabled(true);
    f.advanceWanderCooldown();
    QCOMPARE(f.ctrl.mode(), DesktopMotionController::Mode::Wandering);

    f.ctrl.onFsmState(QStringLiteral("Working"));
    QCOMPARE(f.ctrl.mode(), DesktopMotionController::Mode::Idle);

    f.activeWindow = {QRect(800, 300, 400, 300), 7};
    f.ctrl.setForcePerch(true);
    f.ctrl.onFsmState(QStringLiteral("Idle"));
    f.advanceWanderCooldown();
    QCOMPARE(f.ctrl.mode(), DesktopMotionController::Mode::Perched);

    f.ctrl.notifyPerchedWindowMoved();
    QCOMPARE(f.ctrl.mode(), DesktopMotionController::Mode::Falling);

    f.ctrl.onFsmState(QStringLiteral("Working"));
    QCOMPARE(f.ctrl.mode(), DesktopMotionController::Mode::Falling);
}

void TestDesktopMotion::shelfFromGeometriesUsesTaskbarTop()
{
    const QRect full(0, 0, 1920, 1080);
    const QRect avail(0, 0, 1920, 1040);
    const auto shelf = DesktopGeometry::shelfFromGeometries(full, avail);
    QCOMPARE(shelf.landTopCenter.y(), 1040);
    QCOMPARE(shelf.landTopCenter.x(), 960);
}

QTEST_MAIN(TestDesktopMotion)
#include "test_desktop_motion.moc"
