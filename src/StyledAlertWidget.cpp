#include "StyledAlertWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QPropertyAnimation>
#include <QGuiApplication>
#include <QScreen>
#include <QShowEvent>
#include <QEventLoop>
#include <QWindow>

#include "PlatformWindow.h"
#include "StyleUtils.h"

static QFont harmonyFont(int pointSize, QFont::Weight weight = QFont::Normal)
{
    QFont f(QStringLiteral("HarmonyOS Sans SC"), pointSize, weight);
    f.setStyleStrategy(QFont::PreferAntialias);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

StyledAlertWidget::StyledAlertWidget(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setWindowFlags(
        Qt::FramelessWindowHint |
        Qt::WindowStaysOnTopHint
    );
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
#ifdef Q_OS_MAC
    setAttribute(Qt::WA_MacAlwaysShowToolWindow, true);
#endif

    setFixedSize(PANEL_WIDTH + SHADOW_BLUR * 2, PANEL_HEIGHT + SHADOW_BLUR * 2);

    setupUi();
}

void StyledAlertWidget::setupUi()
{
    m_contentWidget = new QWidget(this);
    m_contentWidget->setObjectName(QStringLiteral("styledAlertContent"));
    m_contentWidget->setGeometry(SHADOW_BLUR, SHADOW_BLUR, PANEL_WIDTH, PANEL_HEIGHT);
    // Scope this rule to the content widget only. An unscoped "background:
    // transparent" cascades to every child via Qt's widget-stylesheet
    // inheritance and silently defeats the global QPushButton:hover
    // background swap — buttons would change text color on hover but stay
    // white. The #styledAlertContent selector pins the rule to this one
    // widget so children keep their app-level styling.
    m_contentWidget->setStyleSheet(QStringLiteral(
        "QWidget#styledAlertContent { background: transparent; }"));

    QVBoxLayout *mainLayout = new QVBoxLayout(m_contentWidget);
    mainLayout->setContentsMargins(PADDING, PADDING, PADDING, PADDING);
    mainLayout->setSpacing(VERTICAL_SPACING);

    QHBoxLayout *titleRow = new QHBoxLayout();
    titleRow->setSpacing(4);

    m_titleLabel = new QLabel(m_contentWidget);
    m_titleLabel->setFont(harmonyFont(13, QFont::Bold));
    m_titleLabel->setStyleSheet("color: black; background: transparent;");
    m_titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_titleLabel->setWordWrap(true);

    m_closeButton = new QPushButton(QStringLiteral("\u00D7"), m_contentWidget);
    m_closeButton->setFont(harmonyFont(14, QFont::Bold));
    m_closeButton->setFixedSize(24, 24);
    m_closeButton->setCursor(Qt::PointingHandCursor);
    StyleUtils::setVariant(m_closeButton, "icon-only");
    connect(m_closeButton, &QPushButton::clicked, this, &StyledAlertWidget::onCloseClicked);

    titleRow->addWidget(m_titleLabel, 1);
    titleRow->addWidget(m_closeButton);

    m_bodyLabel = new QLabel(m_contentWidget);
    m_bodyLabel->setFont(harmonyFont(12));
    m_bodyLabel->setStyleSheet("color: #2C2C2E; background: transparent;");
    m_bodyLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_bodyLabel->setWordWrap(true);
    // Rich-text body + clickable links. About dialog uses <a href> for
    // website / mailto; mode is AutoText so plain-string callers still
    // render plainly (Qt detects the absence of HTML markup).
    m_bodyLabel->setTextFormat(Qt::AutoText);
    m_bodyLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_bodyLabel->setOpenExternalLinks(true);

    QHBoxLayout *buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(8);

    m_okButton = new QPushButton(tr("OK"), m_contentWidget);
    m_okButton->setFont(harmonyFont(12));
    m_okButton->setFixedHeight(30);
    m_okButton->setMinimumWidth(68);
    m_okButton->setCursor(Qt::PointingHandCursor);
    // OK button uses the default global QPushButton style (white BG, 2px
    // black border, Persona-orange hover). No per-widget setStyleSheet —
    // the global stylesheet at :/styles/styles/seelie.qss owns it.
    connect(m_okButton, &QPushButton::clicked, this, &StyledAlertWidget::onOkClicked);

    m_cancelButton = new QPushButton(tr("Cancel"), m_contentWidget);
    m_cancelButton->setFont(harmonyFont(12));
    m_cancelButton->setFixedHeight(30);
    m_cancelButton->setMinimumWidth(68);
    m_cancelButton->setCursor(Qt::PointingHandCursor);
    StyleUtils::setVariant(m_cancelButton, "secondary");
    connect(m_cancelButton, &QPushButton::clicked, this, &StyledAlertWidget::onCancelClicked);
    m_cancelButton->hide();

    buttonRow->addStretch(1);
    buttonRow->addWidget(m_cancelButton);
    buttonRow->addWidget(m_okButton);

    mainLayout->addLayout(titleRow);
    mainLayout->addWidget(m_bodyLabel, 1);
    mainLayout->addLayout(buttonRow);
}

void StyledAlertWidget::showAlert(const QString &title, const QString &body,
                                   const QString &buttonText)
{
    m_titleLabel->setText(title);
    m_bodyLabel->setText(body);
    if (!buttonText.isEmpty()) {
        m_okButton->setText(buttonText);
    } else {
        m_okButton->setText(tr("OK"));
    }
    m_cancelButton->hide();
    m_inConfirmMode = false;

    fitToContent();
    showAnimated();
}

bool StyledAlertWidget::execConfirm(const QString &title, const QString &body)
{
    if (m_inConfirmMode) return false;

    m_titleLabel->setText(title);
    m_bodyLabel->setText(body);
    m_okButton->setText(tr("Yes"));
    m_cancelButton->setText(tr("No"));
    m_cancelButton->show();
    m_inConfirmMode = true;
    m_confirmResult = false;

    fitToContent();
    showAnimated();

    QEventLoop loop;
    connect(this, &StyledAlertWidget::dismissed, &loop, &QEventLoop::quit);
    connect(this, &StyledAlertWidget::destroyed, &loop, &QEventLoop::quit);
    loop.exec();

    m_cancelButton->hide();
    m_okButton->setText(tr("OK"));
    m_inConfirmMode = false;

    return m_confirmResult;
}

void StyledAlertWidget::fitToContent()
{
    // Force the content widget to the target width so the layout engine
    // can compute an accurate height for word-wrapped labels.
    m_contentWidget->setFixedWidth(PANEL_WIDTH);

    const int labelMaxW = PANEL_WIDTH - PADDING * 2;

    // QLabel::heightForWidth() gives the exact wrapped height for rich
    // text and plain text alike, unlike sizeHint() which can under-report
    // before the widget has been shown or laid out.
    int titleHeight = m_titleLabel->heightForWidth(labelMaxW);
    int bodyHeight  = m_bodyLabel->heightForWidth(labelMaxW);

    // Guard against zero-height if labels are empty
    if (titleHeight <= 0)
        titleHeight = m_titleLabel->fontMetrics().height();
    if (bodyHeight <= 0)
        bodyHeight = m_bodyLabel->fontMetrics().height();

    int buttonHeight = m_okButton->sizeHint().height();

    int contentHeight = PADDING * 2
                      + titleHeight
                      + VERTICAL_SPACING
                      + bodyHeight
                      + VERTICAL_SPACING
                      + buttonHeight;

    int newPanelHeight = qMax(PANEL_HEIGHT, contentHeight);
    int newWindowHeight = newPanelHeight + SHADOW_BLUR * 2;

    setFixedSize(PANEL_WIDTH + SHADOW_BLUR * 2, newWindowHeight);
    m_contentWidget->setFixedSize(PANEL_WIDTH, newPanelHeight);
}

void StyledAlertWidget::showAnimated()
{
    m_scale = 0.9;
    m_panelOpacity = 0.0;
    setWindowOpacity(0.0);
    QWidget::show();
    raise();
    activateWindow();

    if (m_scaleAnim) {
        m_scaleAnim->stop();
        delete m_scaleAnim;
        m_scaleAnim = nullptr;
    }
    if (m_opacityAnim) {
        m_opacityAnim->stop();
        delete m_opacityAnim;
        m_opacityAnim = nullptr;
    }

    m_scaleAnim = new QPropertyAnimation(this, "panelScale", this);
    m_scaleAnim->setDuration(300);
    m_scaleAnim->setStartValue(0.9);
    m_scaleAnim->setEndValue(1.0);
    m_scaleAnim->setEasingCurve(QEasingCurve::OutBack);
    m_scaleAnim->start();

    m_opacityAnim = new QPropertyAnimation(this, "panelOpacity", this);
    m_opacityAnim->setDuration(250);
    m_opacityAnim->setStartValue(0.0);
    m_opacityAnim->setEndValue(1.0);
    m_opacityAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_opacityAnim->start();
}

void StyledAlertWidget::hideAnimated()
{
    if (m_scaleAnim) {
        m_scaleAnim->stop();
        delete m_scaleAnim;
        m_scaleAnim = nullptr;
    }
    if (m_opacityAnim) {
        m_opacityAnim->stop();
        delete m_opacityAnim;
        m_opacityAnim = nullptr;
    }

    m_scaleAnim = new QPropertyAnimation(this, "panelScale", this);
    m_scaleAnim->setDuration(200);
    m_scaleAnim->setStartValue(1.0);
    m_scaleAnim->setEndValue(0.9);
    m_scaleAnim->setEasingCurve(QEasingCurve::InCubic);
    m_scaleAnim->start();

    m_opacityAnim = new QPropertyAnimation(this, "panelOpacity", this);
    m_opacityAnim->setDuration(200);
    m_opacityAnim->setStartValue(m_panelOpacity);
    m_opacityAnim->setEndValue(0.0);
    m_opacityAnim->setEasingCurve(QEasingCurve::InCubic);
    connect(m_opacityAnim, &QPropertyAnimation::finished, this, [this]() {
        QWidget::hide();
        emit dismissed();
    });
    m_opacityAnim->start();
}

void StyledAlertWidget::setPanelScale(qreal s)
{
    m_scale = s;
    qreal cx = SHADOW_BLUR + PANEL_WIDTH / 2.0;
    qreal cy = SHADOW_BLUR + m_contentWidget->height() / 2.0;
    m_contentWidget->setGeometry(
        SHADOW_BLUR + static_cast<int>(cx * (1.0 - s)),
        SHADOW_BLUR + static_cast<int>(cy * (1.0 - s)),
        static_cast<int>(PANEL_WIDTH * s),
        static_cast<int>(m_contentWidget->height() * s));
    update();
}

void StyledAlertWidget::setPanelOpacity(qreal o)
{
    m_panelOpacity = o;
    setWindowOpacity(o);
}

void StyledAlertWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const int panelH = m_contentWidget->height();
    const QRectF body(SHADOW_BLUR, SHADOW_BLUR, PANEL_WIDTH, panelH);
    const qreal r = CORNER_RADIUS;
    const qreal sk = SKEW_PX;

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

    painter.save();
    painter.setOpacity(0.35);
    painter.setPen(Qt::NoPen);
    QPainterPath shadowPath = panelPath;
    shadowPath.translate(3, 4);
    painter.setBrush(Qt::black);
    painter.drawPath(shadowPath);
    painter.restore();

    painter.setPen(QPen(Qt::black, BORDER_WIDTH, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    painter.setBrush(Qt::white);
    painter.drawPath(panelPath);

    painter.save();
    painter.setClipPath(panelPath);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0xF3, 0x6F, 0x1A));
    painter.drawRect(QRectF(body.left(), body.top(), body.width() + sk, ACCENT_HEIGHT));
    painter.restore();
}

void StyledAlertWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    PlatformWindow::applyDwmFramelessAttributes(this);
    if (m_petWindow) {
        positionRelativeTo(m_petWindow);
    } else {
        positionCentered();
    }
}

void StyledAlertWidget::positionCentered()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) return;

    QRect screenRect = screen->availableGeometry();
    int x = screenRect.center().x() - width() / 2;
    int y = screenRect.center().y() - height() / 2;

    move(x, y);
}

void StyledAlertWidget::positionRelativeTo(const QWidget *pet)
{
    if (!pet) return;

    QPoint petGlobalPos;
    if (QWindow *w = pet->windowHandle()) {
        petGlobalPos = w->position();
    } else {
        petGlobalPos = pet->mapToGlobal(QPoint(0, 0));
    }
    int petCenterX = petGlobalPos.x() + pet->width() / 2;
    int petTop = petGlobalPos.y();

    int panelW = PANEL_WIDTH;
    int panelH = m_contentWidget->height();
    int panelX = petCenterX - panelW / 2;
    int panelY = petTop - panelH - 5;

    QScreen *screen = QGuiApplication::screenAt(QPoint(petCenterX, petTop));
    if (screen) {
        QRect screenRect = screen->availableGeometry();

        if (panelY < screenRect.top()) {
            panelY = petTop + pet->height() + 5;
        }

        panelX = qBound(screenRect.left(), panelX, screenRect.right() - panelW);
        panelY = qBound(screenRect.top(), panelY, screenRect.bottom() - panelH);
    }

    move(panelX - SHADOW_BLUR, panelY - SHADOW_BLUR);
}

void StyledAlertWidget::onOkClicked()
{
    if (m_inConfirmMode) {
        m_confirmResult = true;
    }
    hideAnimated();
}

void StyledAlertWidget::onCancelClicked()
{
    m_confirmResult = false;
    hideAnimated();
}

void StyledAlertWidget::onCloseClicked()
{
    if (m_inConfirmMode) {
        m_confirmResult = false;
    }
    hideAnimated();
}
