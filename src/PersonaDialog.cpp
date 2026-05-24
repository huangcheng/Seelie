#include "PersonaDialog.h"

#include <QPainter>
#include <QPainterPath>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMouseEvent>
#include <QFont>

static QFont personaFont(int pointSize, QFont::Weight weight = QFont::Normal)
{
    QFont f(QStringLiteral("HarmonyOS Sans SC"), pointSize, weight);
    f.setStyleStrategy(QFont::PreferAntialias);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

PersonaDialog::PersonaDialog(const QString &title, int bodyW, int bodyH, QWidget *parent)
    : QDialog(parent)
    , m_bodyWidth(bodyW)
    , m_bodyHeight(bodyH)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowSystemMenuHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);

    // Total window size: body + shadow margins on all sides + title bar on top
    setFixedSize(bodyW + SHADOW_BLUR * 2,
                 bodyH + TITLE_BAR_HEIGHT + SHADOW_BLUR * 2);

    // --- Title bar (drawn over the painted chrome, at the top of the body rect) ---
    m_titleBar = new QWidget(this);
    m_titleBar->setGeometry(SHADOW_BLUR, SHADOW_BLUR, bodyW, TITLE_BAR_HEIGHT);
    m_titleBar->setStyleSheet(QStringLiteral("background: transparent;"));
    m_titleBar->setAttribute(Qt::WA_TransparentForMouseEvents, false);

    auto *titleBarLayout = new QHBoxLayout(m_titleBar);
    titleBarLayout->setContentsMargins(10, 0, 4, 0);
    titleBarLayout->setSpacing(4);

    m_titleLabel = new QLabel(title, m_titleBar);
    m_titleLabel->setFont(personaFont(10, QFont::Bold));
    m_titleLabel->setStyleSheet(QStringLiteral("color: black; background: transparent;"));
    m_titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_closeButton = new QPushButton(QStringLiteral("\xC3\x97"), m_titleBar); // UTF-8 ×
    m_closeButton->setFont(personaFont(12, QFont::Bold));
    m_closeButton->setFixedSize(22, 22);
    m_closeButton->setCursor(Qt::PointingHandCursor);
    m_closeButton->setStyleSheet(QStringLiteral(R"(
        QPushButton {
            background: transparent;
            border: none;
            border-radius: 3px;
            color: #888;
            padding: 0px;
        }
        QPushButton:hover {
            background: #F36F1A;
            color: white;
        }
    )"));
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::reject);

    titleBarLayout->addWidget(m_titleLabel, 1);
    titleBarLayout->addWidget(m_closeButton);

    // --- Content widget (sits below the title bar, inside the shadow margins) ---
    m_contentWidget = new QWidget(this);
    m_contentWidget->setGeometry(SHADOW_BLUR,
                                  SHADOW_BLUR + TITLE_BAR_HEIGHT,
                                  bodyW,
                                  bodyH);
    m_contentWidget->setStyleSheet(QStringLiteral("background: white;"));
}

void PersonaDialog::setPersonaTitle(const QString &title)
{
    if (m_titleLabel)
        m_titleLabel->setText(title);
}

void PersonaDialog::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // The body rect spans the full height including title bar + content.
    const qreal totalBodyH = m_bodyHeight + TITLE_BAR_HEIGHT;
    const QRectF body(SHADOW_BLUR, SHADOW_BLUR, m_bodyWidth, totalBodyH);
    const qreal r  = CORNER_RADIUS;
    const qreal sk = SKEW_PX;

    // Build skewed panel path (matching SettingsPanelWidget's parallelogram)
    QPainterPath panelPath;
    panelPath.moveTo(body.left() + sk + r, body.top());
    panelPath.lineTo(body.right() + sk - r, body.top());
    panelPath.quadTo(body.right() + sk, body.top(), body.right() + sk, body.top() + r);
    panelPath.lineTo(body.right(), body.bottom() - r);
    panelPath.quadTo(body.right(), body.bottom(), body.right() - r, body.bottom());
    panelPath.lineTo(body.left() + r, body.bottom());
    panelPath.quadTo(body.left(), body.bottom(), body.left(), body.bottom() - r);
    panelPath.lineTo(body.left() + sk, body.top() + r);
    panelPath.quadTo(body.left() + sk, body.top(), body.left() + sk + r, body.top());
    panelPath.closeSubpath();

    // Bold shadow
    painter.save();
    painter.setOpacity(0.35);
    painter.setPen(Qt::NoPen);
    QPainterPath shadowPath = panelPath;
    shadowPath.translate(3, 4);
    painter.setBrush(Qt::black);
    painter.drawPath(shadowPath);
    painter.restore();

    // White fill + thick black border
    painter.setPen(QPen(Qt::black, BORDER_WIDTH, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    painter.setBrush(Qt::white);
    painter.drawPath(panelPath);

    // Red accent stripe at top (same orange-red as SettingsPanelWidget)
    painter.save();
    painter.setClipPath(panelPath);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0xF3, 0x6F, 0x1A));
    painter.drawRect(QRectF(body.left(), body.top(), body.width() + sk, 4));
    painter.restore();

    // Separator line between title bar and content
    painter.save();
    painter.setClipPath(panelPath);
    painter.setPen(QPen(Qt::black, 2));
    const qreal sepY = body.top() + TITLE_BAR_HEIGHT;
    painter.drawLine(QPointF(body.left(), sepY), QPointF(body.right() + sk, sepY));
    painter.restore();
}

void PersonaDialog::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // Allow dragging from the title bar area
        const QRect titleBarRect(SHADOW_BLUR, SHADOW_BLUR, m_bodyWidth, TITLE_BAR_HEIGHT);
        if (titleBarRect.contains(event->pos())) {
            m_dragging = true;
            m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
            event->accept();
            return;
        }
    }
    QDialog::mousePressEvent(event);
}

void PersonaDialog::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - m_dragOffset);
        event->accept();
        return;
    }
    QDialog::mouseMoveEvent(event);
}

void PersonaDialog::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        event->accept();
        return;
    }
    QDialog::mouseReleaseEvent(event);
}
