#include "ECGWidget.h"
#include "CanonicalEvents.h"
#include "MacFocusFix.h"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QShowEvent>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QWindow>
#include <QSoundEffect>
#include <QTemporaryFile>
#include <QDir>
#include <QUrl>
#include <QtMath>
#include <QJsonObject>
#include <cmath>

#include "PlatformWindow.h"

namespace {

static QFont harmonyFont(int pointSize, QFont::Weight weight = QFont::Normal)
{
    QFont f(QStringLiteral("HarmonyOS Sans SC"), pointSize, weight);
    f.setStyleStrategy(QFont::PreferAntialias);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

} // namespace

// Linker definition for constexpr arrays declared in the header.
constexpr double ECGWidget::HR_BPM_OPTIONS[3];

ECGWidget::ECGWidget(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(
        Qt::FramelessWindowHint |
        Qt::WindowStaysOnTopHint |
        Qt::Tool |
        Qt::WindowDoesNotAcceptFocus
    );
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
#ifdef Q_OS_MAC
    setAttribute(Qt::WA_MacAlwaysShowToolWindow, true);
#endif

    setFixedSize(PANEL_WIDTH + SHADOW_BLUR * 2, PANEL_HEIGHT + SHADOW_BLUR * 2);

    const int lcdW = PANEL_WIDTH - LCD_PADDING * 2;
    m_samples.resize(lcdW);
    m_samples.fill(0.0);

    recomputeLayout();

    m_tickTimer.setInterval(TICK_INTERVAL_MS);
    connect(&m_tickTimer, &QTimer::timeout, this, &ECGWidget::onTick);

    m_eventDecayTimer.setSingleShot(true);
    connect(&m_eventDecayTimer, &QTimer::timeout, this, [this]() {
        m_eventBpm = 0.0;
        update();
    });

    m_alarmDecayTimer.setSingleShot(true);
    connect(&m_alarmDecayTimer, &QTimer::timeout, this, [this]() {
        m_alarmActive = false;
        m_alarmFlashOn = false;
        m_alarmFlashTimer.stop();
        update();
    });

    m_alarmFlashTimer.setInterval(300);
    connect(&m_alarmFlashTimer, &QTimer::timeout, this, [this]() {
        m_alarmFlashOn = !m_alarmFlashOn;
        update();
    });

    m_idleTimer.setSingleShot(true);
    m_idleTimer.setInterval(IDLE_TIMEOUT_MS);
    connect(&m_idleTimer, &QTimer::timeout, this, [this]() {
        m_flatlined = true;
        m_samples.fill(0.0);
        if (m_beep) m_beep->stop();
        if (m_flatlineBeep && !m_muted && m_powerOn && m_flatlineBeep->isLoaded()) {
            m_flatlineBeep->play();
        }
        update();
    });
}

ECGWidget::~ECGWidget()
{
    m_tickTimer.stop();
    // All four audio objects (QSoundEffect and QTemporaryFile) are
    // parented to `this` in initAudio() — Qt's parent-child ownership
    // destroys them automatically when ~QObject runs.
}

void ECGWidget::recomputeLayout()
{
    // All rects in widget-local coordinates (origin = top-left of the full
    // widget including the SHADOW_BLUR margin).
    const int panelX = SHADOW_BLUR;
    const int panelY = SHADOW_BLUR;

    const int controlTop = panelY + TITLE_HEIGHT + LCD_HEIGHT + READOUT_HEIGHT;
    const int ctrlCenterY = controlTop + CONTROL_HEIGHT / 2;

    const int btnY = ctrlCenterY - BUTTON_H / 2;

    m_pwrRect  = QRect(panelX + 8,                            btnY, BUTTON_W, BUTTON_H);
    m_almRect  = QRect(m_pwrRect.left() + BUTTON_W + BUTTON_GAP,  btnY, BUTTON_W, BUTTON_H);
    m_modeRect = QRect(m_almRect.left() + BUTTON_W + BUTTON_GAP,  btnY, BUTTON_W, BUTTON_H);

    const int sliderX = panelX + PANEL_WIDTH - 8 - SLIDER_W;
    const int sliderY = ctrlCenterY - SLIDER_H / 2;
    m_sliderRect = QRect(sliderX, sliderY, SLIDER_W, SLIDER_H);
}

void ECGWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!m_nativeHandleFixed) {
        MacFocusFix::makeNonActivating(this);
        m_nativeHandleFixed = true;
    }
    PlatformWindow::applyDwmFramelessAttributes(this);
}

void ECGWidget::anchorTo(const QWidget *petWidget)
{
    m_anchoredPet = petWidget;
    if (petWidget && isVisible()) {
        positionRelativeTo(petWidget);
    }
}

void ECGWidget::start()
{
    initAudio();
    m_prevPhase = m_phase;
    m_powerOn = true;
    m_tickTimer.start();
    m_idleTimer.start();
    if (m_anchoredPet) {
        positionRelativeTo(m_anchoredPet);
    }
    show();
}

void ECGWidget::stop()
{
    m_tickTimer.stop();
    m_idleTimer.stop();
    m_alarmFlashTimer.stop();
    m_alarmDecayTimer.stop();
    m_eventDecayTimer.stop();
    if (m_beep) m_beep->stop();
    if (m_flatlineBeep) m_flatlineBeep->stop();
    m_flatlined = false;
    m_alarmActive = false;
    m_eventBpm = 0.0;
    hide();
}

void ECGWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF body(SHADOW_BLUR, SHADOW_BLUR, PANEL_WIDTH, PANEL_HEIGHT);

    // --- Drop shadow (straight rect, 35% opacity) ---
    {
        QPainterPath shadowPath;
        shadowPath.addRoundedRect(body.translated(3, 4), CORNER_RADIUS, CORNER_RADIUS);
        p.save();
        p.setOpacity(0.35);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::black);
        p.drawPath(shadowPath);
        p.restore();
    }

    // --- Chassis outer rounded rect ---
    QPainterPath chassisPath;
    chassisPath.addRoundedRect(body, CORNER_RADIUS, CORNER_RADIUS);

    p.setPen(QPen(Qt::black, BORDER_WIDTH, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    p.setBrush(Qt::white);
    p.drawPath(chassisPath);

    p.save();
    p.setClipPath(chassisPath);

    const QRectF titleBar(body.left(), body.top(), body.width(), TITLE_HEIGHT);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xF3, 0x6F, 0x1A));
    p.drawRect(titleBar);

    p.setPen(Qt::white);
    p.setFont(harmonyFont(9, QFont::Bold));
    QRectF titleTextRect = titleBar.adjusted(10, 0, -30, 0);
    p.drawText(titleTextRect, Qt::AlignVCenter | Qt::AlignLeft, tr("ECG MONITOR"));

    {
        const qreal ledCx  = body.left() + PANEL_WIDTH - 16;
        const qreal ledCy  = body.top() + TITLE_HEIGHT / 2.0;
        const qreal coreR  = 4.0;                  // 8 px diameter LED core
        const qreal glowR  = 6.0 * m_ledPulse;     // 12 px glow at rest, 16.8 px at peak

        if (m_powerOn) {
            p.save();
            p.setPen(Qt::NoPen);
            QRadialGradient glow(ledCx, ledCy, glowR);
            glow.setColorAt(0, QColor(0x4F, 0xFF, 0x7A, 160));
            glow.setColorAt(1, QColor(0x4F, 0xFF, 0x7A, 0));
            p.setBrush(glow);
            p.drawEllipse(QPointF(ledCx, ledCy), glowR, glowR);

            p.setBrush(QColor(0x4F, 0xFF, 0x7A));
            p.drawEllipse(QPointF(ledCx, ledCy), coreR, coreR);
            p.restore();
        } else {
            p.save();
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0x40, 0x40, 0x40));
            p.drawEllipse(QPointF(ledCx, ledCy), coreR, coreR);
            p.restore();
        }
    }

    p.setPen(QPen(Qt::black, 1));
    p.drawLine(QPointF(body.left(), body.top() + TITLE_HEIGHT),
               QPointF(body.right(), body.top() + TITLE_HEIGHT));

    const QRect lcd(
        static_cast<int>(body.left()) + LCD_PADDING,
        static_cast<int>(body.top()) + TITLE_HEIGHT + LCD_PADDING / 2,
        PANEL_WIDTH - LCD_PADDING * 2,
        LCD_HEIGHT - LCD_PADDING
    );

    p.setPen(Qt::NoPen);
    p.setBrush(m_powerOn ? QColor(0x05, 0x1F, 0x0A) : QColor(0x08, 0x08, 0x08));
    p.drawRect(lcd);

    if (m_powerOn) {
        // Grid
        p.setRenderHint(QPainter::Antialiasing, false);
        for (int x = lcd.left(); x <= lcd.right(); x += 10) {
            const bool major = ((x - lcd.left()) % 50 == 0);
            p.setPen(QPen(major ? QColor(0x2D, 0x7A, 0x4A)
                                : QColor(0x15, 0x4D, 0x2E), 1));
            p.drawLine(x, lcd.top(), x, lcd.bottom());
        }
        for (int y = lcd.top(); y <= lcd.bottom(); y += 10) {
            const bool major = ((y - lcd.top()) % 20 == 0);
            p.setPen(QPen(major ? QColor(0x2D, 0x7A, 0x4A)
                                : QColor(0x15, 0x4D, 0x2E), 1));
            p.drawLine(lcd.left(), y, lcd.right(), y);
        }
        p.setRenderHint(QPainter::Antialiasing, true);

        // Trace
        if (!m_samples.isEmpty()) {
            const double midY  = lcd.center().y();
            const double scale = lcd.height() * 0.40;
            QPainterPath trace;
            for (int i = 0; i < m_samples.size(); ++i) {
                const int idx = (m_writeHead + i) % m_samples.size();
                const double v = m_samples[idx];
                const double tx = lcd.left() + i;
                const double ty = midY - v * scale;
                if (i == 0) trace.moveTo(tx, ty);
                else        trace.lineTo(tx, ty);
            }
            p.setPen(QPen(QColor(0x4F, 0xFF, 0x7A, 90), 4));
            p.drawPath(trace);
            p.setPen(QPen(QColor(0x4F, 0xFF, 0x7A), 1.6));
            p.drawPath(trace);
        }
    }

    const QRect readout(
        static_cast<int>(body.left()),
        static_cast<int>(body.top()) + TITLE_HEIGHT + LCD_HEIGHT,
        PANEL_WIDTH,
        READOUT_HEIGHT
    );

    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    p.drawRect(readout);

    p.setFont(harmonyFont(10, QFont::Bold));
    if (!m_powerOn) {
        p.setPen(QColor(0x80, 0x80, 0x80));
        p.drawText(readout.adjusted(10, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   tr("STANDBY"));
    } else if (m_flatlined) {
        p.setPen(QColor(0xC0, 0x40, 0x40));
        p.drawText(readout.adjusted(10, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   tr("ASYSTOLE"));
    } else {
        const int bpm = static_cast<int>(std::round(currentBpm()));
        const QString bpmText = tr("HR %1 BPM").arg(bpm);

        p.setPen(QColor(0x1A, 0x6A, 0x2A));
        p.drawText(readout.adjusted(10, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, bpmText);

        if (m_muted) {
            QFontMetrics fm(p.font());
            const int bpmW = fm.horizontalAdvance(bpmText);
            p.setPen(QColor(0xC0, 0x40, 0x40));
            p.drawText(readout.adjusted(10 + bpmW, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft,
                       tr(" (MUTED)"));
        }
    }

    p.setPen(QPen(Qt::black, 1));
    p.drawLine(readout.topLeft(), readout.topRight());

    const QRect ctrlPanel(
        static_cast<int>(body.left()),
        static_cast<int>(body.top()) + TITLE_HEIGHT + LCD_HEIGHT + READOUT_HEIGHT,
        PANEL_WIDTH,
        CONTROL_HEIGHT
    );

    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    p.drawRect(ctrlPanel);

    p.setPen(QPen(Qt::black, 1));
    p.drawLine(ctrlPanel.topLeft(), ctrlPanel.topRight());

    auto drawButton = [&](const QRect &r, const QString &label, bool isOff,
                          bool isPressed, bool isMuted = false)
    {
        const int yShift = isPressed ? 1 : 0;
        QRect br = r.translated(0, yShift);

        // Button face
        QColor fillColor = isOff ? QColor(0xA0, 0xA0, 0xA0)
                                 : QColor(0xD0, 0xD0, 0xD0);
        if (isPressed) fillColor = QColor(0x90, 0x90, 0x90);

        p.save();
        p.setPen(QPen(Qt::black, 1.5));
        p.setBrush(fillColor);
        p.drawRoundedRect(br, 3, 3);

        // Button label
        p.setPen(Qt::black);
        p.setFont(harmonyFont(9, QFont::Bold));
        p.drawText(br, Qt::AlignCenter, label);

        // ALM mute indicator: diagonal red bar
        if (isMuted) {
            p.setPen(QPen(QColor(0xC0, 0x40, 0x40), 3));
            p.drawLine(br.topLeft() + QPoint(3, 3),
                       br.bottomRight() - QPoint(3, 3));
        }

        p.restore();
    };

    const bool pwrPressed  = (m_pressed == PressedControl::Pwr);
    const bool almPressed  = (m_pressed == PressedControl::Alm);
    const bool modePressed = (m_pressed == PressedControl::Mode);

    drawButton(m_pwrRect,  tr("PWR"),  !m_powerOn, pwrPressed);
    drawButton(m_almRect,  tr("ALM"),  false,       almPressed, m_muted);
    const QString modeLabel = QString::number(static_cast<int>(std::round(currentBpm())));
    drawButton(m_modeRect, modeLabel,  false,       modePressed);

    {
        const QRect &sr = m_sliderRect;
        const int trackH = 4;
        const int trackY = sr.top() + (sr.height() - trackH) / 2;
        const QRect track(sr.left(), trackY, sr.width(), trackH);

        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x20, 0x20, 0x20));
        p.drawRoundedRect(track, 2, 2);

        const int thumbX = sr.left() + static_cast<int>((sr.width() - SLIDER_THUMB_W) * m_volume);
        const int fillW  = thumbX + SLIDER_THUMB_W / 2 - sr.left();
        if (fillW > 0) {
            p.setBrush(QColor(0xF3, 0x6F, 0x1A));
            p.drawRoundedRect(QRect(sr.left(), trackY, fillW, trackH), 2, 2);
        }

        const QRect thumb(thumbX, sr.top(), SLIDER_THUMB_W, sr.height());
        p.setPen(QPen(Qt::black, 1));
        p.setBrush(QColor(0xE0, 0xE0, 0xE0));
        p.drawRoundedRect(thumb, 2, 2);

        p.restore();
    }

    p.restore(); // end clip

    if (m_alarmActive && m_alarmFlashOn) {
        QPainterPath alarmRing;
        alarmRing.addRoundedRect(body, CORNER_RADIUS, CORNER_RADIUS);
        p.setPen(QPen(QColor(0xC0, 0x40, 0x40), BORDER_WIDTH,
                      Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(alarmRing);
    }
}

void ECGWidget::positionRelativeTo(const QWidget *pet)
{
    if (!pet) return;

    QRect anchor = m_anchorRect.isValid()
                   ? m_anchorRect
                   : QRect(0, 0, pet->width(), pet->height());

    QPoint petGlobalPos;
    if (QWindow *w = pet->windowHandle()) {
        petGlobalPos = w->position();
    } else {
        petGlobalPos = pet->pos();
    }
    const int petCenterX = petGlobalPos.x() + anchor.x() + anchor.width() / 2;
    const int petTop     = petGlobalPos.y() + anchor.y();

    int wx = petCenterX - width() / 2;
    int wy = petTop - height();

    QScreen *screen = QGuiApplication::screenAt(QPoint(petCenterX, petTop));
    if (screen) {
        QRect g = screen->availableGeometry();
        wx = qBound(g.left(), wx, g.right() - width());
        wy = qBound(g.top(), wy, g.bottom() - height());
    }
    move(wx, wy);
}

void ECGWidget::onTick()
{
    if (!m_powerOn) return;

    if (m_flatlined) {
        m_samples[m_writeHead] = 0.0;
        m_writeHead = (m_writeHead + 1) % m_samples.size();
        update();
        return;
    }

    const double periodMs = 60.0 * 1000.0 / currentBpm();
    const double dPhase   = static_cast<double>(TICK_INTERVAL_MS) / periodMs;

    m_prevPhase = m_phase;
    m_phase    += dPhase;
    if (m_phase >= 1.0) m_phase -= 1.0;

    m_samples[m_writeHead] = ecgSample(m_phase);
    m_writeHead = (m_writeHead + 1) % m_samples.size();

    m_ledPulse = qMax(1.0, m_ledPulse - 0.13);

    auto crossedR = [&]() {
        if (m_prevPhase <= m_phase)
            return m_prevPhase < R_PEAK_PHASE && m_phase >= R_PEAK_PHASE;
        return R_PEAK_PHASE > m_prevPhase || R_PEAK_PHASE <= m_phase;
    };

    if (crossedR()) {
        m_ledPulse = 1.4;
        if (!m_muted && m_beep && m_beep->isLoaded()) {
            m_beep->play();
        }
    }

    update();
}

void ECGWidget::applyVolumeFromSliderX(int widgetX)
{
    const int trackW = m_sliderRect.width() - SLIDER_THUMB_W;
    const int rel    = widgetX - m_sliderRect.left() - SLIDER_THUMB_W / 2;
    m_volume = qBound(0.0, static_cast<double>(rel) / trackW, 1.0);
    if (m_beep) m_beep->setVolume(static_cast<float>(m_volume));
    if (m_flatlineBeep) m_flatlineBeep->setVolume(static_cast<float>(m_volume));
}

void ECGWidget::pressControlAt(QPoint p)
{
    if (m_pwrRect.contains(p))       m_pressed = PressedControl::Pwr;
    else if (m_almRect.contains(p))  m_pressed = PressedControl::Alm;
    else if (m_modeRect.contains(p)) m_pressed = PressedControl::Mode;
    else if (m_sliderRect.contains(p)) {
        m_pressed = PressedControl::SliderDrag;
        applyVolumeFromSliderX(p.x());
    } else {
        m_pressed = PressedControl::None;
    }
    update();
}

void ECGWidget::releaseControlAt(QPoint p)
{
    switch (m_pressed) {
    case PressedControl::Pwr:
        if (m_pwrRect.contains(p)) {
            m_powerOn = !m_powerOn;
            if (m_powerOn) {
                // Reset every transient flag so power-cycling out of a
                // flatline / alarm state actually returns to a clean
                // baseline. Without this, the trace stays flat or the
                // border keeps flashing until a gateway event arrives.
                m_flatlined = false;
                m_alarmActive = false;
                m_alarmFlashOn = false;
                m_alarmFlashTimer.stop();
                m_alarmDecayTimer.stop();
                m_eventBpm = 0.0;
                m_eventDecayTimer.stop();
                m_prevPhase = m_phase;
                m_idleTimer.start();
                m_tickTimer.start();
            } else {
                m_tickTimer.stop();
                m_idleTimer.stop();
                m_alarmFlashTimer.stop();
                m_alarmDecayTimer.stop();
                m_eventDecayTimer.stop();
                if (m_beep) m_beep->stop();
                if (m_flatlineBeep) m_flatlineBeep->stop();
            }
        }
        break;
    case PressedControl::Alm:
        if (m_almRect.contains(p)) {
            m_muted = !m_muted;
            if (m_flatlineBeep) {
                if (m_muted) {
                    m_flatlineBeep->stop();
                } else if (m_flatlined && m_powerOn && m_flatlineBeep->isLoaded()) {
                    m_flatlineBeep->play();
                }
            }
        }
        break;
    case PressedControl::Mode:
        if (m_modeRect.contains(p)) m_hrIndex = (m_hrIndex + 1) % 3;
        break;
    case PressedControl::SliderDrag:
        break;
    case PressedControl::None:
        break;
    }
    m_pressed = PressedControl::None;
    update();
}

void ECGWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const QPoint p = event->pos();
        // Check if press lands on a control first
        if (m_pwrRect.contains(p) || m_almRect.contains(p)
                || m_modeRect.contains(p) || m_sliderRect.contains(p)) {
            pressControlAt(p);
        } else {
            // Chassis area drag
            const QRect chassis(SHADOW_BLUR, SHADOW_BLUR, PANEL_WIDTH, PANEL_HEIGHT);
            if (chassis.contains(p)) {
                m_isChassisDragging = true;
                m_dragLastGlobal = event->globalPosition().toPoint();
                setCursor(Qt::ClosedHandCursor);
            } else {
                pressControlAt(p);
            }
        }
    }
    event->accept();
}

void ECGWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_pressed == PressedControl::SliderDrag) {
        applyVolumeFromSliderX(event->pos().x());
        update();
    } else if (m_isChassisDragging) {
        const QPoint globalPos = event->globalPosition().toPoint();
        const QPoint delta = globalPos - m_dragLastGlobal;
        m_dragLastGlobal = globalPos;
        move(pos() + delta);
        emit dragMoved(delta);
    }
    event->accept();
}

void ECGWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_isChassisDragging) {
        m_isChassisDragging = false;
        unsetCursor();
        event->accept();
        return;
    }
    releaseControlAt(event->pos());
    event->accept();
}

void ECGWidget::contextMenuEvent(QContextMenuEvent *event)
{
    emit contextMenuRequested(event->globalPos());
    event->accept();
}

void ECGWidget::onEvent(const QJsonObject &event)
{
    // Stay completely silent when not the active display: in Character mode
    // the widget is hidden but the IPC connection is still live, and processing
    // events here would restart the idle timer and eventually leak a flatline
    // tone with no widget on screen.
    if (!isVisible() || !m_powerOn) return;

    const QString name = event.value(QStringLiteral("event")).toString();
    if (name.isEmpty()) return;

    // Any event resets the idle countdown and revives a flatline.
    const bool wasFlatlined = m_flatlined;
    m_flatlined = false;
    m_idleTimer.start();
    if (wasFlatlined && m_flatlineBeep) m_flatlineBeep->stop();

    // Negative bpm means "no override" (settle to user's manual BPM).
    double bpm = -1.0;
    bool alarm = false;
    int decayMs = 4000;

    namespace CE = CanonicalEvents;
    if      (name == CE::SessionStart)        { bpm = 90;  decayMs = 5000; }
    else if (name == CE::SessionEnd)          { bpm = 60;  decayMs = 5000; }
    else if (name == CE::SessionIdle)         { bpm = 60;  decayMs = 8000; }
    else if (name == CE::SessionError)        { bpm = 130; alarm = true; decayMs = 6000; }
    else if (name == CE::PromptSubmitted)     { bpm = 95;  decayMs = 3000; }
    else if (name == CE::ToolBefore)          { bpm = 85;  decayMs = 4000; }
    else if (name == CE::ToolAfter)           { decayMs = 1000; }
    else if (name == CE::ToolFailed)          { bpm = 120; alarm = true; decayMs = 5000; }
    else if (name == CE::PermissionRequested) { bpm = 100; alarm = true; decayMs = 5000; }
    else if (name == CE::PermissionDenied)    { bpm = 120; alarm = true; decayMs = 5000; }
    else if (name == CE::SubagentStarted)     { bpm = 90;  decayMs = 4000; }
    else if (name == CE::SubagentStopped)     { bpm = 72;  decayMs = 2000; }
    else return;

    if (bpm > 0) {
        m_eventBpm = bpm;
        m_eventDecayTimer.start(decayMs);
    } else {
        m_eventBpm = 0.0;
        m_eventDecayTimer.stop();
    }

    if (alarm) {
        m_alarmActive = true;
        m_alarmFlashOn = true;
        m_alarmFlashTimer.start();
        m_alarmDecayTimer.start(decayMs);
    }

    // Bump the LED on every event so users get instant visual feedback
    // independent of the next R-peak crossing.
    m_ledPulse = 1.4;
    update();
}

void ECGWidget::initAudio()
{
    if (m_beep) return;

    // Parent the QTemporaryFiles to `this` so Qt cleans them up on
    // widget destruction (and setAutoRemove(true) deletes the actual
    // file on disk). The QSoundEffects below already have `this` as
    // parent — no manual delete needed in ~ECGWidget.
    m_beepFile = new QTemporaryFile(
        QDir::tempPath() + QStringLiteral("/seelie_ecg_beep_XXXXXX.wav"),
        this);
    m_beepFile->setAutoRemove(true);
    if (!m_beepFile->open()) {
        m_beepFile->deleteLater();
        m_beepFile = nullptr;
        return;
    }
    m_beepFile->write(synthesizeBeepWav());
    if (!m_beepFile->flush()) {
        qWarning() << "ECGWidget: failed to flush beep WAV — discarding incomplete audio";
        m_beepFile->close();
        m_beepFile->deleteLater();
        m_beepFile = nullptr;
        return;
    }
    const QString path = m_beepFile->fileName();
    m_beepFile->close();

    m_beep = new QSoundEffect(this);
    m_beep->setSource(QUrl::fromLocalFile(path));
    m_beep->setVolume(static_cast<float>(m_volume));

    m_flatlineBeepFile = new QTemporaryFile(
        QDir::tempPath() + QStringLiteral("/seelie_ecg_flatline_XXXXXX.wav"),
        this);
    m_flatlineBeepFile->setAutoRemove(true);
    if (m_flatlineBeepFile->open()) {
        m_flatlineBeepFile->write(synthesizeFlatlineWav());
        if (!m_flatlineBeepFile->flush()) {
            qWarning() << "ECGWidget: failed to flush flatline WAV — discarding incomplete audio";
            m_flatlineBeepFile->close();
            m_flatlineBeepFile->deleteLater();
            m_flatlineBeepFile = nullptr;
        } else {
            const QString flatPath = m_flatlineBeepFile->fileName();
            m_flatlineBeepFile->close();

            m_flatlineBeep = new QSoundEffect(this);
            m_flatlineBeep->setSource(QUrl::fromLocalFile(flatPath));
            m_flatlineBeep->setVolume(static_cast<float>(m_volume));
        }
    }
}

double ECGWidget::ecgSample(double phase)
{
    phase = phase - std::floor(phase);

    auto gauss = [](double x, double mu, double sigma) {
        const double z = (x - mu) / sigma;
        return std::exp(-0.5 * z * z);
    };

    double v = 0.0;
    v += 0.15 * gauss(phase, 0.15, 0.025); // P wave
    v -= 0.10 * gauss(phase, 0.28, 0.012); // Q dip
    v += 1.00 * gauss(phase, 0.30, 0.012); // R spike
    v -= 0.20 * gauss(phase, 0.32, 0.018); // S dip
    v += 0.30 * gauss(phase, 0.50, 0.040); // T wave
    return v;
}

QByteArray ECGWidget::synthesizeBeepWav()
{
    const int    sr   = BEEP_SAMPLE_RATE;
    const int    n    = sr * BEEP_DURATION_MS / 1000;
    const int    fade = sr * BEEP_FADE_MS / 1000;
    const double w    = 2.0 * M_PI * BEEP_FREQ_HZ / double(sr);

    QByteArray pcm;
    pcm.reserve(n * 2);
    for (int i = 0; i < n; ++i) {
        double env = 1.0;
        if (i < fade)          env = double(i) / fade;
        else if (i > n - fade) env = double(n - i) / fade;
        const double s = std::sin(w * i) * env * 0.6;
        const qint16 v = static_cast<qint16>(qBound(-32767.0, s * 32767.0, 32767.0));
        pcm.append(static_cast<char>(v & 0xFF));
        pcm.append(static_cast<char>((v >> 8) & 0xFF));
    }

    QByteArray wav;
    wav.reserve(44 + pcm.size());

    auto putU32 = [&](quint32 v) {
        wav.append(char(v & 0xFF));
        wav.append(char((v >> 8) & 0xFF));
        wav.append(char((v >> 16) & 0xFF));
        wav.append(char((v >> 24) & 0xFF));
    };
    auto putU16 = [&](quint16 v) {
        wav.append(char(v & 0xFF));
        wav.append(char((v >> 8) & 0xFF));
    };

    wav.append("RIFF");
    putU32(36 + pcm.size());
    wav.append("WAVE");
    wav.append("fmt ");
    putU32(16);
    putU16(1);
    putU16(1);
    putU32(sr);
    putU32(sr * 2);
    putU16(2);
    putU16(16);
    wav.append("data");
    putU32(pcm.size());
    wav.append(pcm);
    return wav;
}

QByteArray ECGWidget::synthesizeFlatlineWav()
{
    const int    sr = BEEP_SAMPLE_RATE;
    const int    n  = sr * FLATLINE_DURATION_MS / 1000;
    const double w  = 2.0 * M_PI * FLATLINE_FREQ_HZ / double(sr);

    QByteArray pcm;
    pcm.reserve(n * 2);
    for (int i = 0; i < n; ++i) {
        const double s = std::sin(w * i) * 0.5;
        const qint16 v = static_cast<qint16>(qBound(-32767.0, s * 32767.0, 32767.0));
        pcm.append(static_cast<char>(v & 0xFF));
        pcm.append(static_cast<char>((v >> 8) & 0xFF));
    }

    QByteArray wav;
    wav.reserve(44 + pcm.size());

    auto putU32 = [&](quint32 v) {
        wav.append(char(v & 0xFF));
        wav.append(char((v >> 8) & 0xFF));
        wav.append(char((v >> 16) & 0xFF));
        wav.append(char((v >> 24) & 0xFF));
    };
    auto putU16 = [&](quint16 v) {
        wav.append(char(v & 0xFF));
        wav.append(char((v >> 8) & 0xFF));
    };

    wav.append("RIFF");
    putU32(36 + pcm.size());
    wav.append("WAVE");
    wav.append("fmt ");
    putU32(16);
    putU16(1);
    putU16(1);
    putU32(sr);
    putU32(sr * 2);
    putU16(2);
    putU16(16);
    wav.append("data");
    putU32(pcm.size());
    wav.append(pcm);
    return wav;
}
