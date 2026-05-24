#include "PersonaDialog.h"

#include <QPainter>
#include <QPainterPath>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMouseEvent>
#include <QFont>
#include <QGuiApplication>
#include <QScreen>

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
    setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_NoSystemBackground, true);

    // Window size: body + shadow margins on all sides (mirrors Settings)
    setFixedSize(bodyW + SHADOW_BLUR * 2, bodyH + SHADOW_BLUR * 2);

    // Single panel widget spans the full body
    m_panelWidget = new QWidget(this);
    m_panelWidget->setGeometry(SHADOW_BLUR, SHADOW_BLUR, bodyW, bodyH);
    m_panelWidget->setStyleSheet(QStringLiteral("background: transparent;"));

    auto *mainLayout = new QVBoxLayout(m_panelWidget);
    mainLayout->setContentsMargins(PADDING, PADDING, PADDING, PADDING);
    mainLayout->setSpacing(VERTICAL_SPACING);

    // Title row
    auto *titleRow = new QHBoxLayout;
    titleRow->setSpacing(4);

    m_titleLabel = new QLabel(title, m_panelWidget);
    m_titleLabel->setFont(personaFont(10, QFont::Bold));
    m_titleLabel->setStyleSheet(QStringLiteral("color: black; background: transparent;"));
    m_titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_closeButton = new QPushButton(tr("×"), m_panelWidget);
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

    titleRow->addWidget(m_titleLabel, 1);
    titleRow->addWidget(m_closeButton);
    mainLayout->addLayout(titleRow);

    // Separator line — QFrame widget, NOT painted
    m_separator = new QFrame(m_panelWidget);
    m_separator->setFrameShape(QFrame::HLine);
    m_separator->setFrameShadow(QFrame::Plain);
    m_separator->setStyleSheet(QStringLiteral("border: none; border-top: 2px solid black; background: transparent;"));
    m_separator->setFixedHeight(1);
    mainLayout->addWidget(m_separator);

    // Content area below separator (this is what subclasses fill)
    m_contentArea = new QWidget(m_panelWidget);
    m_contentArea->setStyleSheet(QStringLiteral("background: transparent;"));
    mainLayout->addWidget(m_contentArea, 1);  // takes remaining space

    // Center on primary screen (keep existing behavior)
    if (auto *screen = QGuiApplication::primaryScreen()) {
        const QRect avail = screen->availableGeometry();
        move(avail.center().x() - width() / 2,
             avail.center().y() - height() / 2);
    }
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

    const QRectF body(SHADOW_BLUR, SHADOW_BLUR, m_bodyWidth, m_bodyHeight);
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
}

void PersonaDialog::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // Drag region: top 32px of the panel where the title row lives.
        const QRect dragZone(SHADOW_BLUR, SHADOW_BLUR, m_bodyWidth, 32);
        if (dragZone.contains(event->pos())) {
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
