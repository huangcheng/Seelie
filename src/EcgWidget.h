#ifndef ECGWIDGET_H
#define ECGWIDGET_H

#include <QWidget>
#include <QTimer>
#include <QVector>
#include <QRect>
#include <QJsonObject>
#include <QPointer>

class QSoundEffect;
class QTemporaryFile;

class EcgWidget : public QWidget
{
    Q_OBJECT

public:
    explicit EcgWidget(QWidget *parent = nullptr);
    ~EcgWidget() override;

    void anchorTo(const QWidget *petWidget);
    void setAnchorRect(const QRect &rect) { m_anchorRect = rect; }

    void start();
    void stop();

    static double ecgSample(double phase);
    static QByteArray synthesizeBeepWav();
    static QByteArray synthesizeFlatlineWav();

    double phase() const { return m_phase; }
    int sampleCount() const { return m_samples.size(); }

    // Exposed for tests.
    bool powerOn() const { return m_powerOn; }
    bool muted() const { return m_muted; }
    int hrIndex() const { return m_hrIndex; }
    double volume() const { return m_volume; }
    double currentBpm() const {
        return m_eventBpm > 0.0 ? m_eventBpm : HR_BPM_OPTIONS[m_hrIndex];
    }

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

    // Synthesize the same action a real mouse press/release would, without
    // needing a visible window. Used by tests; not part of the public API.
    void pressControlAt(QPoint p);
    void releaseControlAt(QPoint p);

    friend class TestEcg;

signals:
    void dragMoved(QPoint deltaGlobal);
    void contextMenuRequested(QPoint globalPos);

public slots:
    // Drive heart-rate / alarm reactions from gateway events.
    void onEvent(const QJsonObject &event);

private slots:
    void onTick();

private:
    void positionRelativeTo(const QWidget *pet);
    void initAudio();
    void recomputeLayout();
    void applyVolumeFromSliderX(int globalX);

    enum class PressedControl { None, Pwr, Alm, Mode, SliderDrag };

    QTimer m_tickTimer;
    QVector<double> m_samples;
    int m_writeHead = 0;
    double m_phase = 0.0;
    double m_prevPhase = 0.0;

    bool   m_powerOn  = true;
    bool   m_muted    = false;
    int    m_hrIndex  = 1;
    double m_volume   = 0.4;
    qreal  m_ledPulse = 1.0;

    PressedControl m_pressed = PressedControl::None;

    // Chassis drag state
    QPoint m_dragLastGlobal;
    bool m_isChassisDragging = false;

    // Event reactivity: a gateway event temporarily overrides the user's
    // manual BPM and may raise an alarm flash. m_eventBpm > 0 means the
    // override is active. Both decay back to baseline via QTimers.
    double m_eventBpm = 0.0;
    bool   m_alarmActive = false;
    QTimer m_eventDecayTimer;
    QTimer m_alarmDecayTimer;
    QTimer m_alarmFlashTimer;
    bool   m_alarmFlashOn = false;

    // Flatline / asystole: after IDLE_TIMEOUT_MS of no events the trace
    // goes flat, beep stops, readout shows "ASYSTOLE". Any new event
    // exits this state and restarts the idle countdown.
    bool   m_flatlined = false;
    QTimer m_idleTimer;
    static constexpr int IDLE_TIMEOUT_MS = 60 * 1000;

    QSoundEffect *m_beep = nullptr;
    QTemporaryFile *m_beepFile = nullptr;
    QSoundEffect *m_flatlineBeep = nullptr;
    QTemporaryFile *m_flatlineBeepFile = nullptr;

    QPointer<const QWidget> m_anchoredPet = nullptr;
    QRect m_anchorRect;
    bool m_nativeHandleFixed = false;  // M21: one-shot guard for MacFocusFix

    // Control rects in widget-local coordinates (include SHADOW_BLUR offset).
    QRect m_pwrRect;
    QRect m_almRect;
    QRect m_modeRect;
    QRect m_sliderRect;

    static constexpr double HR_BPM_OPTIONS[3] = { 60.0, 72.0, 90.0 };

    static constexpr int PANEL_WIDTH       = 220;
    static constexpr int PANEL_HEIGHT      = 140;
    static constexpr int SHADOW_BLUR       = 10;
    static constexpr int CORNER_RADIUS     = 6;
    static constexpr int BORDER_WIDTH      = 3;
    static constexpr int LCD_PADDING       = 8;
    static constexpr int TICK_INTERVAL_MS  = 33;

    static constexpr int TITLE_HEIGHT      = 18;
    static constexpr int LCD_HEIGHT        = 60;
    static constexpr int READOUT_HEIGHT    = 16;
    static constexpr int CONTROL_HEIGHT    = PANEL_HEIGHT - TITLE_HEIGHT - LCD_HEIGHT - READOUT_HEIGHT; // = 46

    static constexpr int BUTTON_W          = 38;
    static constexpr int BUTTON_H          = 22;
    static constexpr int BUTTON_GAP        = 4;
    static constexpr int SLIDER_W          = 60;
    static constexpr int SLIDER_H          = 14;
    static constexpr int SLIDER_THUMB_W    = 10;

    static constexpr double R_PEAK_PHASE   = 0.30;

    static constexpr int BEEP_SAMPLE_RATE  = 22050;
    static constexpr int BEEP_FREQ_HZ      = 880;
    static constexpr int BEEP_DURATION_MS  = 60;
    static constexpr int BEEP_FADE_MS      = 3;

    // Flatline alarm: ~2 s 1000 Hz tone played once when asystole starts.
    // 2 s × 1000 Hz = 2000 whole cycles, so the tail crosses zero.
    static constexpr int FLATLINE_FREQ_HZ     = 1000;
    static constexpr int FLATLINE_DURATION_MS = 2000;
};

#endif // ECGWIDGET_H
