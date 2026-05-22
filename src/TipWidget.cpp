#include "TipWidget.h"
#include "MacFocusFix.h"

#include <QPainter>
#include <QFont>
#include <QFontMetrics>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QPaintEvent>
#include <QShowEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QRect>
#include <QPainterPath>
#include <QWindow>

#include "PlatformWindow.h"

TipWidget::TipWidget(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(
        Qt::FramelessWindowHint |
        Qt::WindowStaysOnTopHint |
        Qt::Tool |                       // No taskbar entry; on macOS, prevents app activation on show
        Qt::WindowDoesNotAcceptFocus
    );
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    // Same Win11 DWM workaround as MainWindow — without WA_NoSystemBackground
    // the system fills the bubble's window with white before paintEvent runs,
    // and DWM then draws rounded corners + shadow + Mica around it.
    setAttribute(Qt::WA_NoSystemBackground, true);
#ifdef Q_OS_MAC
    setAttribute(Qt::WA_MacAlwaysShowToolWindow, true);
#endif

    m_opacity = 1.0;

    // Connect dismiss timer
    connect(&m_dismissTimer, &QTimer::timeout, this, &TipWidget::hideBubble);
}

TipWidget::~TipWidget()
{
    if (m_opacityAnim) {
        m_opacityAnim->stop();
        delete m_opacityAnim;
        m_opacityAnim = nullptr;
    }
    if (m_slideAnim) {
        m_slideAnim->stop();
        delete m_slideAnim;
        m_slideAnim = nullptr;
    }
}

void TipWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!m_nativeHandleFixed) {
        // Promote the underlying NSWindow to a non-activating panel so showing
        // the bubble never steals keyboard focus from the user's foreground app.
        MacFocusFix::makeNonActivating(this);
        m_nativeHandleFixed = true;
    }
    refreshDwmAttributes();
}

void TipWidget::refreshDwmAttributes()
{
    PlatformWindow::applyDwmFramelessAttributes(this);
    // Force Windows to re-evaluate the window's composition surface.
    // Without this, DWM can cache stale chrome and leave the bubble
    // invisible after display sleep/wake or DWM restart.
    PlatformWindow::refreshComposition(this);
}

void TipWidget::anchorTo(const QWidget *petWidget)
{
    m_anchoredPet = petWidget;
    if (petWidget) {
        positionRelativeTo(petWidget);
    }
}

void TipWidget::showBubble(const QString &title, const QString &message, BubbleType type, const QString &source, bool bypassUserSuppression)
{
    // Fire the request signal regardless of suppression — listeners like TTS
    // should react to the *intent* to show a tip, not the rendered visibility.
    emit bubbleRequested(title, message, type);

    // Check suppression: mode suppression always blocks, but user suppression can be bypassed
    if (m_suppressedByMode || (m_suppressedByUser && !bypassUserSuppression)) return;

    bool alreadyVisible = isVisible() && m_opacity > 0.5;
    bool sameContent = (m_title == title && m_message == message && m_source == source);

    m_title = title;
    m_message = message;
    m_source = source;
    m_type = type;

    emit bubbleShown(title, message, type);

    // If already showing the same content, just reset the dismiss timer
    if (alreadyVisible && sameContent) {
        m_dismissTimer.stop();
        int dismissMs = (type == StatusBubble) ? STATUS_DISMISS_MS : TIP_DISMISS_MS;
        m_dismissTimer.start(dismissMs);
        return;
    }

    // Calculate text layout and bubble dimensions
    calculateTextLayout();

    // Set widget size: bubble + tail + shadow margin on all sides
    int totalWidth = m_bubbleRect.width() + SHADOW_BLUR * 2;
    int totalHeight = m_bubbleRect.height() + TAIL_HEIGHT + SHADOW_BLUR * 2;
    setFixedSize(totalWidth, totalHeight);

    // Position relative to pet
    if (m_anchoredPet) {
        positionRelativeTo(m_anchoredPet);
    } else {
        // Default position if no pet anchored. primaryScreen() can be null
        // during a headless run or if the only display is being unplugged.
        if (QScreen *primary = QGuiApplication::primaryScreen()) {
            QRect screenRect = primary->availableGeometry();
            move(screenRect.center().x() - width() / 2,
                 screenRect.center().y() - height() / 2);
        }
    }

    if (alreadyVisible) {
        // Content changed while visible — update text without re-fading
        update();
    } else {
        // First show — fade in
        m_opacity = 0.0;
        startEnterAnimation();
        QWidget::show();
        // Note: do not call raise() here — on macOS it can activate the app
        // and steal keyboard focus from whatever the user is typing in.
        // Qt::WindowStaysOnTopHint already keeps the bubble above other windows.
    }

    // Reset dismiss timer
    m_dismissTimer.stop();
    int dismissMs = (type == StatusBubble) ? STATUS_DISMISS_MS : TIP_DISMISS_MS;
    m_dismissTimer.start(dismissMs);
}

void TipWidget::hideBubble()
{
    m_dismissTimer.stop();
    startExitAnimation();
}

void TipWidget::setOpacity(qreal o)
{
    m_opacity = o;
    update();
}

void TipWidget::setSlideOffset(qreal o)
{
    m_slideOffset = o;
    move(m_targetPos.x(), m_targetPos.y() + static_cast<int>(o));
}

void TipWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing |
                           QPainter::TextAntialiasing |
                           QPainter::SmoothPixmapTransform, true);

    // Bubble body rect offset by shadow margin
    const QRectF body(SHADOW_BLUR, SHADOW_BLUR,
                      m_bubbleRect.width(), m_bubbleRect.height());
    const qreal r = CORNER_RADIUS;
    const qreal sk = SKEW_PX;  // parallelogram skew

    // Build skewed bubble path (parallelogram with slight rounded corners)
    // Top edge shifted right by sk, bottom edge stays — gives a dynamic lean
    QPainterPath bubblePath;
    bubblePath.moveTo(body.left() + sk + r, body.top());
    bubblePath.lineTo(body.right() + sk - r, body.top());
    bubblePath.quadTo(body.right() + sk, body.top(), body.right() + sk, body.top() + r);
    bubblePath.lineTo(body.right(), body.bottom() - r);
    bubblePath.quadTo(body.right(), body.bottom(), body.right() - r, body.bottom());
    bubblePath.lineTo(body.left() + r, body.bottom());
    bubblePath.quadTo(body.left(), body.bottom(), body.left(), body.bottom() - r);
    bubblePath.lineTo(body.left() + sk, body.top() + r);
    bubblePath.quadTo(body.left() + sk, body.top(), body.left() + sk + r, body.top());
    bubblePath.closeSubpath();

    // Tail X: prefer the pet-anchored value computed by positionRelativeTo
    // (so the tail still points at the pet after the bubble is horizontally
    // clamped to the screen). Fall back to bubble center if no anchor was
    // ever set (e.g. unanchored center-of-screen mode).
    int tailBaseX = (m_tailAnchorX >= 0) ? m_tailAnchorX : int(body.center().x());
    QPainterPath tailPath;
    if (m_tailDown) {
        tailPath.moveTo(tailBaseX - TAIL_WIDTH / 2.0, body.bottom());
        tailPath.lineTo(tailBaseX + 2, body.bottom() + TAIL_HEIGHT);  // tip slightly right
        tailPath.lineTo(tailBaseX + TAIL_WIDTH / 2.0, body.bottom());
    } else {
        tailPath.moveTo(tailBaseX - TAIL_WIDTH / 2.0, body.top());
        tailPath.lineTo(tailBaseX + 2, body.top() - TAIL_HEIGHT);
        tailPath.lineTo(tailBaseX + TAIL_WIDTH / 2.0, body.top());
    }
    tailPath.closeSubpath();

    QPainterPath combined = bubblePath.united(tailPath);

    // Draw bold shadow: dark, offset down-right for a punchy "pop" effect
    painter.save();
    painter.setOpacity(m_opacity * 0.35);
    painter.setPen(Qt::NoPen);
    QPainterPath shadowPath = combined;
    shadowPath.translate(3, 4);
    painter.setBrush(Qt::black);
    painter.drawPath(shadowPath);
    painter.restore();

    // Draw bubble: white fill + thick black border
    painter.save();
    painter.setOpacity(m_opacity);
    painter.setPen(QPen(Qt::black, BORDER_WIDTH, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    painter.setBrush(Qt::white);
    painter.drawPath(combined);
    painter.restore();

    // Red accent stripe at top of bubble
    painter.save();
    painter.setOpacity(m_opacity);
    painter.setClipPath(combined);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0xF3, 0x6F, 0x1A));  // Persona orange
    painter.drawRect(QRectF(body.left(), body.top(), body.width() + sk, 4));
    painter.restore();

    // Offset for text drawing (shifted by shadow margin)
    const int ox = SHADOW_BLUR;
    const int oy = SHADOW_BLUR;

    // Draw title text — bold, black
    if (!m_title.isEmpty()) {
        painter.save();
        painter.setOpacity(m_opacity);
        painter.setFont(makeTitleFont());
        painter.setPen(Qt::black);
        painter.drawText(m_titleRect.translated(ox, oy),
                         Qt::AlignLeft | Qt::AlignVCenter, m_title);
        painter.restore();
    }

    // Draw message text
    if (!m_message.isEmpty()) {
        painter.save();
        painter.setOpacity(m_opacity);
        painter.setFont(makeMessageFont());
        painter.setPen(QColor(0x1A, 0x1A, 0x1A));
        // Word-wrap, no elide. calculateTextLayout() already sized
        // m_messageRect to fit the wrapped text height.
        painter.drawText(m_messageRect.translated(ox, oy),
                         Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_message);
        painter.restore();
    }

    // Draw source label — subtle gray
    if (!m_source.isEmpty()) {
        painter.save();
        painter.setOpacity(m_opacity);
        painter.setFont(makeSourceFont());
        painter.setPen(QColor(0x88, 0x88, 0x88));
        painter.drawText(m_sourceRect.translated(ox, oy),
                         Qt::AlignLeft | Qt::AlignVCenter, m_source);
        painter.restore();
    }
}

void TipWidget::positionRelativeTo(const QWidget *pet)
{
    if (!pet)
        return;

    QRect anchor = m_anchorRect.isValid() ? m_anchorRect : QRect(0, 0, pet->width(), pet->height());

    // On macOS with Qt::Tool frameless windows, both mapToGlobal() and pos()
    // can return stale/incorrect coordinates. Use the native QWindow position
    // which tracks the actual OS window frame.
    QPoint petGlobalPos;
    if (QWindow *w = pet->windowHandle()) {
        petGlobalPos = w->position();
    } else {
        petGlobalPos = pet->pos();
    }
    QPoint petCenter(petGlobalPos.x() + anchor.x() + anchor.width() / 2,
                     petGlobalPos.y() + anchor.y() + anchor.height() / 2);
    int petCenterX = petCenter.x();
    int petTop      = petGlobalPos.y() + anchor.y();
    int petBottom   = petGlobalPos.y() + anchor.y() + anchor.height();

    // Calculate bubble position: center bubble horizontally on pet center
    int bubbleX = petCenterX - width() / 2;

    // Overlap the pet slightly so the tail feels attached, not floating
    constexpr int PET_OVERLAP = 4;
    int bubbleY = petTop - m_bubbleRect.height() - TAIL_HEIGHT - SHADOW_BLUR + PET_OVERLAP;

    m_tailDown = true; // default: tail points down

    // Check if not enough room above
    QScreen *screen = QGuiApplication::screenAt(petCenter);
    if (screen) {
        QRect screenRect = screen->availableGeometry();
        if (bubbleY < screenRect.top()) {
            // Not enough room above — place below pet with same overlap
            bubbleY = petBottom + TAIL_HEIGHT + SHADOW_BLUR - PET_OVERLAP;
            m_tailDown = false;
        }
    }

    // Update tail polygon for new position

    // Clamp to screen boundaries
    if (screen) {
        QRect screenRect = screen->availableGeometry();
        bubbleX = qBound(screenRect.left(), bubbleX, screenRect.right() - width());
        bubbleY = qBound(screenRect.top(), bubbleY, screenRect.bottom() - height());
    }

    m_targetPos = QPoint(bubbleX, bubbleY);
    move(m_targetPos);

    // Anchor the tail to the pet's horizontal center, in widget-local
    // coordinates. If the bubble was clamped to a screen edge, the tail
    // still points at the pet — without this, paintEvent draws the tail
    // at the bubble center, which can drift far from the pet when the
    // bubble is wide or near a screen edge. Clamp the tail X so it stays
    // visually inside the bubble's body (with a margin for the curved
    // corners) — pointing past the edge looks broken.
    //
    // Guard against tiny bubbles where 2*margin > body width (would make
    // qBound's min > max and assert). Falls back to body center, which
    // is fine for a bubble that small.
    const int localAnchor = petCenterX - bubbleX;
    const int margin = TAIL_WIDTH / 2 + CORNER_RADIUS;
    const int minX = SHADOW_BLUR + margin;
    const int maxX = SHADOW_BLUR + m_bubbleRect.width() - margin;
    m_tailAnchorX = (maxX >= minX)
                        ? qBound(minX, localAnchor, maxX)
                        : SHADOW_BLUR + m_bubbleRect.width() / 2;
    update();
}

void TipWidget::startEnterAnimation()
{
    if (m_opacityAnim) {
        m_opacityAnim->stop();
        delete m_opacityAnim;
        m_opacityAnim = nullptr;
    }
    if (m_slideAnim) {
        m_slideAnim->stop();
        delete m_slideAnim;
        m_slideAnim = nullptr;
    }

    // Slide direction: bubble above pet slides up from below, below pet slides down from above
    int slideDistance = m_tailDown ? 20 : -20;

    // Fade in
    m_opacityAnim = new QPropertyAnimation(this, "opacity", this);
    m_opacityAnim->setDuration(300);
    m_opacityAnim->setStartValue(0.0);
    m_opacityAnim->setEndValue(1.0);
    m_opacityAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_opacityAnim->start();

    // Slide with overshoot bounce
    m_slideAnim = new QPropertyAnimation(this, "slideOffset", this);
    m_slideAnim->setDuration(350);
    m_slideAnim->setStartValue(static_cast<qreal>(slideDistance));
    m_slideAnim->setEndValue(0.0);
    m_slideAnim->setEasingCurve(QEasingCurve::OutBack);
    m_slideAnim->start();
}

void TipWidget::startExitAnimation()
{
    if (m_opacityAnim) {
        m_opacityAnim->stop();
        delete m_opacityAnim;
        m_opacityAnim = nullptr;
    }
    if (m_slideAnim) {
        m_slideAnim->stop();
        delete m_slideAnim;
        m_slideAnim = nullptr;
    }

    int slideDistance = m_tailDown ? 12 : -12;

    // Fade out
    m_opacityAnim = new QPropertyAnimation(this, "opacity", this);
    m_opacityAnim->setDuration(200);
    m_opacityAnim->setStartValue(m_opacity);
    m_opacityAnim->setEndValue(0.0);
    m_opacityAnim->setEasingCurve(QEasingCurve::InCubic);
    connect(m_opacityAnim, &QPropertyAnimation::finished, this, &QWidget::hide);
    m_opacityAnim->start();

    // Slide away
    m_slideAnim = new QPropertyAnimation(this, "slideOffset", this);
    m_slideAnim->setDuration(200);
    m_slideAnim->setStartValue(0.0);
    m_slideAnim->setEndValue(static_cast<qreal>(slideDistance));
    m_slideAnim->setEasingCurve(QEasingCurve::InCubic);
    m_slideAnim->start();
}

void TipWidget::calculateTextLayout()
{
    const QFont titleFont   = makeTitleFont();
    const QFont msgFont     = makeMessageFont();
    const QFont sourceFont  = makeSourceFont();

    QFontMetrics titleFm(titleFont);
    QFontMetrics msgFm(msgFont);
    QFontMetrics sourceFm(sourceFont);

    // Calculate title height
    int titleHeight = m_title.isEmpty() ? 0 : titleFm.height();

    // Calculate message width with word wrap (no elide — long text grows
    // the bubble vertically rather than being truncated to "...").
    int textWidth = MAX_BUBBLE_WIDTH - (PADDING_H * 2);

    // Calculate message bounding rect
    QRect msgBounding = msgFm.boundingRect(0, 0, textWidth, 1000,
                                            Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                                            m_message);
    int msgHeight = msgBounding.height();

    // Calculate source label height
    int sourceHeight = m_source.isEmpty() ? 0 : sourceFm.height();

    // Calculate total bubble dimensions
    int contentWidth = qMax(titleFm.horizontalAdvance(m_title), msgBounding.width());
    if (!m_source.isEmpty()) {
        contentWidth = qMax(contentWidth, sourceFm.horizontalAdvance(m_source));
    }
    int bubbleWidth = qMin(MAX_BUBBLE_WIDTH, contentWidth + PADDING_H * 2);
    int bubbleHeight = titleHeight + msgHeight + sourceHeight + PADDING_V * 2;

    // Ensure minimum height
    if (!m_title.isEmpty() && msgHeight > 0) {
        bubbleHeight += 4; // Spacing between title and message
    }
    if (sourceHeight > 0) {
        bubbleHeight += 4; // Spacing before source label
    }

    m_bubbleRect.setWidth(bubbleWidth);
    m_bubbleRect.setHeight(bubbleHeight);

    // Calculate title rect
    int currentY = PADDING_V;
    if (!m_title.isEmpty()) {
        m_titleRect = QRect(PADDING_H, currentY,
                             bubbleWidth - PADDING_H * 2, titleHeight);
        currentY += titleHeight;
    } else {
        m_titleRect = QRect();
    }

    // Calculate message rect
    if (!m_message.isEmpty()) {
        m_messageRect = QRect(PADDING_H, currentY,
                              bubbleWidth - PADDING_H * 2, msgHeight);
        currentY += msgHeight;
    } else {
        m_messageRect = QRect();
    }

    // Calculate source rect
    if (!m_source.isEmpty()) {
        currentY += 4; // spacing
        m_sourceRect = QRect(PADDING_H, currentY,
                             bubbleWidth - PADDING_H * 2, sourceHeight);
    } else {
        m_sourceRect = QRect();
    }
}

// Translucent windows on Windows can't use ClearType subpixel AA, so the
// goal here is the cleanest grayscale rendering possible: explicit AA
// strategy + no hinting (avoids stroke-snapping that mangles CJK glyphs)
// + a real Bold weight (Black is synthesized for HarmonyOS Sans SC and
// looks grainy at 12pt).
QFont TipWidget::makeTitleFont()
{
    QFont f(QStringLiteral("HarmonyOS Sans SC"), 12, QFont::Bold);
    f.setStyleStrategy(QFont::PreferAntialias);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

QFont TipWidget::makeMessageFont()
{
    QFont f(QStringLiteral("HarmonyOS Sans SC"), 12);
    f.setStyleStrategy(QFont::PreferAntialias);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

QFont TipWidget::makeSourceFont()
{
    QFont f(QStringLiteral("HarmonyOS Sans SC"), 10);
    f.setStyleStrategy(QFont::PreferAntialias);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}