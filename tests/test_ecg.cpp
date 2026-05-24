#include <QTest>
#include <QApplication>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QDir>
#include <QCoreApplication>
#include <cmath>

#include "ConfigManager.h"
#include "ECGWidget.h"

class TestEcg : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void configDisplayModeDefaultsCharacter();
    void configDisplayModeRoundTrips();
    void configDisplayModeEmitsSignal();
    void configMigratesFromOldEcgEnabledKey();

    void ecgSampleHasRPeakNear030();
    void ecgSampleBaselineAwayFromComplex();

    void onTickAdvancesPhaseAndShiftsBuffer();

    void synthesizeBeepWavHasValidRiffHeader();

    // New redesign tests
    void pwrToggleStopsTimer();
    void almToggleSilencesBeep();
    void modeButtonCyclesBpm();
    void sliderPressUpdatesVolume();
    void sliderMoveUpdatesVolume();
};

static void removeTestConfig()
{
    {
        QSettings s(QSettings::IniFormat, QSettings::UserScope,
                    QStringLiteral("SeelieTests"), QStringLiteral("SeelieTests-Ecg"));
        s.clear();
        s.sync();
    }
    const QString cfgDir = QStandardPaths::writableLocation(
        QStandardPaths::AppConfigLocation);
    QDir(cfgDir).removeRecursively();
    QFile::remove(cfgDir + ".ini");
}

void TestEcg::initTestCase()
{
    QCoreApplication::setOrganizationName("SeelieTests");
    QCoreApplication::setApplicationName("SeelieTests-Ecg");
    removeTestConfig();
}

void TestEcg::cleanupTestCase()
{
    removeTestConfig();
}

void TestEcg::configDisplayModeDefaultsCharacter()
{
    removeTestConfig();
    ConfigManager cfg;
    cfg.load();
    QCOMPARE(cfg.displayMode(), ConfigManager::DisplayMode::Character);
}

void TestEcg::configDisplayModeRoundTrips()
{
    removeTestConfig();
    {
        ConfigManager cfg;
        cfg.load();
        cfg.setDisplayMode(ConfigManager::DisplayMode::Ecg);
        cfg.save();
    }
    {
        ConfigManager cfg2;
        cfg2.load();
        QCOMPARE(cfg2.displayMode(), ConfigManager::DisplayMode::Ecg);
    }
}

void TestEcg::configDisplayModeEmitsSignal()
{
    removeTestConfig();
    ConfigManager cfg;
    cfg.load();
    // Ensure we start in Character mode
    cfg.setDisplayMode(ConfigManager::DisplayMode::Character);

    QSignalSpy spy(&cfg, &ConfigManager::displayModeChanged);
    cfg.setDisplayMode(ConfigManager::DisplayMode::Ecg);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<ConfigManager::DisplayMode>(),
             ConfigManager::DisplayMode::Ecg);
    // Idempotent: setting same value again must not emit
    cfg.setDisplayMode(ConfigManager::DisplayMode::Ecg);
    QCOMPARE(spy.count(), 1);
}

void TestEcg::configMigratesFromOldEcgEnabledKey()
{
    removeTestConfig();

    // Write the old-style key directly via QSettings
    {
        QSettings s(QSettings::IniFormat, QSettings::UserScope,
                    QStringLiteral("SeelieTests"), QStringLiteral("SeelieTests-Ecg"));
        s.setValue("language", "en"); // ensure the file exists (load() short-circuits otherwise)
        s.setValue("ecgEnabled", true);
        s.sync();
    }

    // Load through ConfigManager — should migrate
    {
        ConfigManager cfg;
        cfg.load();
        QCOMPARE(cfg.displayMode(), ConfigManager::DisplayMode::Ecg);

        // Verify old key was removed and new key written
        QSettings s(QSettings::IniFormat, QSettings::UserScope,
                    QStringLiteral("SeelieTests"), QStringLiteral("SeelieTests-Ecg"));
        QVERIFY(!s.contains("ecgEnabled"));
        QVERIFY(s.contains("displayMode"));
        QCOMPARE(s.value("displayMode").toString(), QStringLiteral("ecg"));
    }

    // Second load should read the new key cleanly (no old key)
    {
        ConfigManager cfg2;
        cfg2.load();
        QCOMPARE(cfg2.displayMode(), ConfigManager::DisplayMode::Ecg);
    }
}

void TestEcg::ecgSampleHasRPeakNear030()
{
    const double rPeak = ECGWidget::ecgSample(0.30);
    QVERIFY(rPeak > 0.8);

    for (double p : {0.05, 0.20, 0.40, 0.60, 0.80, 0.95}) {
        QVERIFY2(ECGWidget::ecgSample(p) < rPeak - 0.3,
                 qPrintable(QString("phase %1 should be far below R peak").arg(p)));
    }
}

void TestEcg::ecgSampleBaselineAwayFromComplex()
{
    QVERIFY(std::abs(ECGWidget::ecgSample(0.85)) < 0.05);
    QVERIFY(std::abs(ECGWidget::ecgSample(0.95)) < 0.05);
}

void TestEcg::onTickAdvancesPhaseAndShiftsBuffer()
{
    ECGWidget w;
    const double startPhase = w.phase();
    const int n = w.sampleCount();
    QVERIFY(n > 100);

    QMetaObject::invokeMethod(&w, "onTick");
    for (int i = 0; i < 29; ++i) {
        QMetaObject::invokeMethod(&w, "onTick");
    }
    QVERIFY(w.phase() != startPhase);
    // After ~1 second of ticks at 72 BPM, phase should have advanced ~1.2 cycles.
    const double elapsedCycles = (30 * 33.0 / 1000.0) * (72.0 / 60.0);
    QVERIFY(std::abs((w.phase() - startPhase) - (elapsedCycles - std::floor(elapsedCycles))) < 0.05
            || std::abs(w.phase() - startPhase) < 1.0);
}

void TestEcg::synthesizeBeepWavHasValidRiffHeader()
{
    QByteArray wav = ECGWidget::synthesizeBeepWav();
    QVERIFY(wav.size() > 44);
    QCOMPARE(QByteArray(wav.constData(), 4),      QByteArray("RIFF"));
    QCOMPARE(QByteArray(wav.constData() + 8, 4),  QByteArray("WAVE"));
    QCOMPARE(QByteArray(wav.constData() + 12, 4), QByteArray("fmt "));

    quint16 fmt = static_cast<quint8>(wav[20])
                | (static_cast<quint8>(wav[21]) << 8);
    QCOMPARE(int(fmt), 1);

    quint16 ch = static_cast<quint8>(wav[22])
               | (static_cast<quint8>(wav[23]) << 8);
    QCOMPARE(int(ch), 1);

    quint32 sr = static_cast<quint8>(wav[24])
               | (static_cast<quint8>(wav[25]) << 8)
               | (static_cast<quint8>(wav[26]) << 16)
               | (static_cast<quint8>(wav[27]) << 24);
    QCOMPARE(int(sr), 22050);
}

// -----------------------------------------------------------------------
// New tests for the ICU monitor redesign
// -----------------------------------------------------------------------

void TestEcg::pwrToggleStopsTimer()
{
    ECGWidget w;
    // start() makes the widget visible and starts the timer; skip show() to
    // avoid needing a display — use the tick/phase interface instead.
    // Drive a few ticks to confirm phase advances when power is on.
    QVERIFY(w.powerOn()); // default on

    const double phaseA = w.phase();
    QMetaObject::invokeMethod(&w, "onTick");
    const double phaseB = w.phase();
    QVERIFY(phaseB != phaseA); // ticking advances phase while powered on

    // PWR button center: x=SHADOW_BLUR+8+BUTTON_W/2=37, y≈ctrl center.
    const QPoint pwrCenter(37, 127);
    w.pressControlAt(pwrCenter);
    w.releaseControlAt(pwrCenter);

    QVERIFY(!w.powerOn());

    // After power-off, onTick should be a no-op for phase.
    const double phaseAfterOff = w.phase();
    QMetaObject::invokeMethod(&w, "onTick");
    QCOMPARE(w.phase(), phaseAfterOff); // phase must not advance
}

void TestEcg::almToggleSilencesBeep()
{
    ECGWidget w;
    QVERIFY(!w.muted()); // default not muted

    // Press + release ALM button.
    // ALM button: x = SHADOW_BLUR + 8 + BUTTON_W + BUTTON_GAP + BUTTON_W/2
    //           = 10 + 8 + 38 + 4 + 19 = 79
    // y ≈ ctrl panel center = SHADOW_BLUR + TITLE_HEIGHT + LCD_HEIGHT + READOUT_HEIGHT + CONTROL_HEIGHT/2
    //                       = 10 + 18 + 60 + 16 + 23 = 127
    const QPoint almCenter(79, 127);
    w.pressControlAt(almCenter);
    w.releaseControlAt(almCenter);

    QVERIFY(w.muted());

    // Second click toggles back.
    w.pressControlAt(almCenter);
    w.releaseControlAt(almCenter);

    QVERIFY(!w.muted());
}

void TestEcg::modeButtonCyclesBpm()
{
    ECGWidget w;
    // Default hrIndex=1 → 72 BPM.
    QCOMPARE(w.currentBpm(), 72.0);

    // MODE button center: x = SHADOW_BLUR + 8 + 2*(BUTTON_W+BUTTON_GAP) + BUTTON_W/2
    //                       = 10 + 8 + 2*(38+4) + 19 = 10 + 8 + 84 + 19 = 121
    // y ≈ 127 (same ctrl center)
    const QPoint modeCenter(121, 127);

    w.pressControlAt(modeCenter);
    w.releaseControlAt(modeCenter);
    QCOMPARE(w.currentBpm(), 90.0); // 1→2

    w.pressControlAt(modeCenter);
    w.releaseControlAt(modeCenter);
    QCOMPARE(w.currentBpm(), 60.0); // 2→0

    w.pressControlAt(modeCenter);
    w.releaseControlAt(modeCenter);
    QCOMPARE(w.currentBpm(), 72.0); // 0→1, back to start
}

void TestEcg::sliderPressUpdatesVolume()
{
    // Slider rect: x = SHADOW_BLUR + PANEL_WIDTH - 8 - SLIDER_W = 162, width = 60.
    // For volume=0.5: rel = 0.5 * (SLIDER_W - SLIDER_THUMB_W) = 25; x = 162+5+25 = 192.
    ECGWidget w;
    w.pressControlAt(QPoint(192, 127));
    QVERIFY(std::abs(w.volume() - 0.5) < 0.05);
    w.releaseControlAt(QPoint(192, 127));
}

void TestEcg::sliderMoveUpdatesVolume()
{
    ECGWidget w;
    w.pressControlAt(QPoint(192, 127)); // start at midpoint, ~0.5
    QVERIFY(std::abs(w.volume() - 0.5) < 0.05);

    QMouseEvent move(QEvent::MouseMove, QPoint(167, 127), w.mapToGlobal(QPoint(167, 127)),
                     Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&w, &move);
    QVERIFY(w.volume() < 0.05);

    w.releaseControlAt(QPoint(167, 127));
}

QTEST_MAIN(TestEcg)
#include "test_ecg.moc"
