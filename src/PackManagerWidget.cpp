#include "PackManagerWidget.h"
#include "CharacterPackManager.h"
#include "StyledAlertWidget.h"
#include "StyleUtils.h"

#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QFileDialog>
#include <QPropertyAnimation>
#include <QGuiApplication>
#include <QScreen>
#include <QShowEvent>
#include <QStyle>
#include <QStandardPaths>
#include <QWindow>

#include "PlatformWindow.h"

static QFont harmonyFont(int pointSize, QFont::Weight weight = QFont::Normal)
{
    QFont f(QStringLiteral("HarmonyOS Sans SC"), pointSize, weight);
    f.setStyleStrategy(QFont::PreferAntialias);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

PackManagerWidget::PackManagerWidget(CharacterPackManager *manager, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_packManager(manager)
{
    setWindowFlags(
        Qt::FramelessWindowHint |
        Qt::WindowStaysOnTopHint |
        Qt::WindowDoesNotAcceptFocus
    );
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
#ifdef Q_OS_MAC
    setAttribute(Qt::WA_MacAlwaysShowToolWindow, true);
#endif

    setFixedSize(PANEL_WIDTH + SHADOW_BLUR * 2, PANEL_HEIGHT + SHADOW_BLUR * 2);

    setupUi();

    if (m_packManager) {
        connect(m_packManager, &CharacterPackManager::packListChanged,
                this, &PackManagerWidget::refreshPackList);
    }
    refreshPackList();
}

void PackManagerWidget::setupUi()
{
    m_contentWidget = new QWidget(this);
    m_contentWidget->setObjectName(QStringLiteral("packManagerContent"));
    m_contentWidget->setGeometry(SHADOW_BLUR, SHADOW_BLUR, PANEL_WIDTH, PANEL_HEIGHT);
    // Scope this rule to the content widget only. An unscoped "background:
    // transparent" cascades to every child via Qt's widget-stylesheet
    // inheritance and silently defeats the global QPushButton:hover
    // background swap — buttons would change text color on hover but stay
    // white. The #packManagerContent selector pins the rule to this one
    // widget so children keep their app-level styling.
    m_contentWidget->setStyleSheet(QStringLiteral(
        "QWidget#packManagerContent { background: transparent; }"));

    QVBoxLayout *mainLayout = new QVBoxLayout(m_contentWidget);
    mainLayout->setContentsMargins(PADDING, PADDING, PADDING, PADDING);
    mainLayout->setSpacing(VERTICAL_SPACING);

    QHBoxLayout *titleRow = new QHBoxLayout();
    titleRow->setSpacing(4);

    m_titleLabel = new QLabel(tr("Models Management"), m_contentWidget);
    m_titleLabel->setFont(harmonyFont(10, QFont::Bold));
    m_titleLabel->setStyleSheet("color: black; background: transparent;");
    m_titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_closeButton = new QPushButton(tr("\u00D7"), m_contentWidget);
    m_closeButton->setFont(harmonyFont(12, QFont::Bold));
    m_closeButton->setFixedSize(22, 22);
    m_closeButton->setCursor(Qt::PointingHandCursor);
    StyleUtils::setVariant(m_closeButton, "icon-only");
    connect(m_closeButton, &QPushButton::clicked, this, &PackManagerWidget::onCloseClicked);

    titleRow->addWidget(m_titleLabel, 1);
    titleRow->addWidget(m_closeButton);

    m_listWidget = new QListWidget(m_contentWidget);
    m_listWidget->setFont(harmonyFont(10));
    m_listWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    // QListWidget styling lives in the global stylesheet
    // (:/styles/styles/seelie.qss) — sharp 3px corners, 2px black border,
    // Persona orange selection, light-orange hover, minimal scrollbar.

    QHBoxLayout *buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(8);

    m_addButton = new QPushButton(tr("Add"), m_contentWidget);
    m_addButton->setFont(harmonyFont(10));
    m_addButton->setFixedHeight(28);
    m_addButton->setMinimumWidth(60);
    m_addButton->setCursor(Qt::PointingHandCursor);
    // Default global QPushButton style — no variant needed.
    connect(m_addButton, &QPushButton::clicked, this, &PackManagerWidget::onAddClicked);

    m_deleteButton = new QPushButton(tr("Delete"), m_contentWidget);
    m_deleteButton->setFont(harmonyFont(10));
    m_deleteButton->setFixedHeight(28);
    m_deleteButton->setMinimumWidth(60);
    m_deleteButton->setCursor(Qt::PointingHandCursor);
    // Default global QPushButton style — :disabled handled there too.
    connect(m_deleteButton, &QPushButton::clicked, this, &PackManagerWidget::onDeleteClicked);

    buttonRow->addStretch(1);
    buttonRow->addWidget(m_addButton);
    buttonRow->addWidget(m_deleteButton);

    mainLayout->addLayout(titleRow);
    mainLayout->addWidget(m_listWidget, 1);
    mainLayout->addLayout(buttonRow);
}

void PackManagerWidget::ensureAlertDialog()
{
    if (!m_alertDialog) {
        m_alertDialog = new StyledAlertWidget(nullptr);
        // WA_DeleteOnClose means the user closing the dialog frees it.
        // QPointer<> in the header tracks that destruction, so a future
        // showAlert call hits this lazy-create path again instead of a
        // dangling pointer. Audit M7/M8.
        m_alertDialog->setAttribute(Qt::WA_DeleteOnClose);
        m_alertDialog->setPetWindow(m_petWindow);
    }
}

void PackManagerWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF body(SHADOW_BLUR, SHADOW_BLUR, PANEL_WIDTH, PANEL_HEIGHT);
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

    // Shadow
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

    // Orange accent stripe at top
    painter.save();
    painter.setClipPath(panelPath);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0xF3, 0x6F, 0x1A));
    painter.drawRect(QRectF(body.left(), body.top(), body.width() + sk, 4));
    painter.restore();
}

void PackManagerWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    PlatformWindow::applyDwmFramelessAttributes(this);
    if (m_petWindow) {
        positionRelativeTo(m_petWindow);
    } else {
        positionCentered();
    }
}

void PackManagerWidget::positionCentered()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) return;

    QRect screenRect = screen->availableGeometry();
    int x = screenRect.center().x() - (PANEL_WIDTH + SHADOW_BLUR * 2) / 2;
    int y = screenRect.center().y() - (PANEL_HEIGHT + SHADOW_BLUR * 2) / 2;

    move(x, y);
}

void PackManagerWidget::positionRelativeTo(const QWidget *pet)
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

    int panelX = petCenterX - PANEL_WIDTH / 2;
    int panelY = petTop - PANEL_HEIGHT - 5;

    QScreen *screen = QGuiApplication::screenAt(QPoint(petCenterX, petTop));
    if (screen) {
        QRect screenRect = screen->availableGeometry();

        if (panelY < screenRect.top()) {
            panelY = petTop + pet->height() + 5;
        }

        panelX = qBound(screenRect.left(), panelX, screenRect.right() - PANEL_WIDTH);
        panelY = qBound(screenRect.top(), panelY, screenRect.bottom() - PANEL_HEIGHT);
    }

    move(panelX - SHADOW_BLUR, panelY - SHADOW_BLUR);
}

void PackManagerWidget::showAnimated()
{
    m_scale = 0.9;
    m_panelOpacity = 0.0;
    setWindowOpacity(0.0);
    QWidget::show();
    raise();

    delete m_scaleAnim;
    delete m_opacityAnim;

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

void PackManagerWidget::hideAnimated()
{
    delete m_scaleAnim;
    delete m_opacityAnim;

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
    connect(m_opacityAnim, &QPropertyAnimation::finished, this, &QWidget::hide);
    m_opacityAnim->start();
}

void PackManagerWidget::setPanelScale(qreal s)
{
    m_scale = s;
    QTransform t;
    qreal cx = SHADOW_BLUR + PANEL_WIDTH / 2.0;
    qreal cy = SHADOW_BLUR + PANEL_HEIGHT / 2.0;
    t.translate(cx, cy);
    t.scale(s, s);
    t.translate(-cx, -cy);
    m_contentWidget->setGeometry(
        SHADOW_BLUR + static_cast<int>(cx * (1.0 - s)),
        SHADOW_BLUR + static_cast<int>(cy * (1.0 - s)),
        static_cast<int>(PANEL_WIDTH * s),
        static_cast<int>(PANEL_HEIGHT * s));
    update();
}

void PackManagerWidget::setPanelOpacity(qreal o)
{
    m_panelOpacity = o;
    setWindowOpacity(o);
}

void PackManagerWidget::onCloseClicked()
{
    hideAnimated();
}

void PackManagerWidget::onAddClicked()
{
    if (!m_packManager) return;

    QStringList filters;
    filters << tr("Pack files (*.spk *.codex-pet)")
            << tr("SPK files (*.spk)")
            << tr("Codex Pet files (*.codex-pet)")
            << tr("All files (*)");

    QStringList files = QFileDialog::getOpenFileNames(
        this,
        tr("Select Pack Files to Install"),
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
        filters.join(";;")
    );

    if (files.isEmpty()) return;

    int successCount = 0;
    int failCount = 0;
    QStringList failedFiles;
    // Capture per-file failure reasons so we can show *why* an install
    // failed instead of a generic "installation failed". H17.
    QStringList failureReasons;

    for (const QString &file : files) {
        if (m_packManager->installPack(file)) {
            ++successCount;
        } else {
            ++failCount;
            const QString name = QFileInfo(file).fileName();
            failedFiles.append(name);
            const QString why = m_packManager->lastError();
            if (!why.isEmpty()) {
                failureReasons.append(QStringLiteral("%1: %2").arg(name, why));
            }
        }
    }

    auto formatFailureDetail = [&]() {
        return failureReasons.isEmpty()
            ? failedFiles.join(QStringLiteral(", "))
            : failureReasons.join(QLatin1Char('\n'));
    };

    if (successCount > 0) {
        QString msg = tr("Successfully installed %1 pack(s).").arg(successCount);
        if (failCount > 0) {
            msg += QLatin1Char('\n') + tr("Failed to install %1 file(s):").arg(failCount)
                 + QLatin1Char('\n') + formatFailureDetail();
        }
        ensureAlertDialog();
        m_alertDialog->showAlert(tr("Installation Complete"), msg);
    } else if (failCount > 0) {
        ensureAlertDialog();
        m_alertDialog->showAlert(tr("Installation Failed"), formatFailureDetail());
    }
}

void PackManagerWidget::onDeleteClicked()
{
    if (!m_packManager) return;

    QList<QListWidgetItem *> selected = m_listWidget->selectedItems();
    if (selected.isEmpty()) {
        ensureAlertDialog();
        m_alertDialog->showAlert(tr("No Selection"),
            tr("Please select one or more packs to delete."));
        return;
    }

    QStringList packNames;
    QStringList packIds;
    for (QListWidgetItem *item : selected) {
        packIds.append(item->data(Qt::UserRole).toString());
        packNames.append(item->text());
    }

    QString activePackId = m_packManager->activePackId();
    bool hasActivePack = packIds.contains(activePackId);

    if (hasActivePack) {
        ensureAlertDialog();
        CharacterPackManager::PackInfo activeInfo = m_packManager->packInfo(activePackId);
        QString activeName = activeInfo.displayName(m_packManager->activeLocale());
        m_alertDialog->showAlert(
            tr("Cannot Delete Active Pet"),
            tr("\"%1\" is currently in use and cannot be deleted.\n\nPlease switch to another pet first.")
                .arg(activeName)
        );
        return;
    }

    StyledAlertWidget confirmDialog(nullptr);
    bool confirmed = confirmDialog.execConfirm(
        tr("Delete Packs"),
        tr("Are you sure you want to delete the following %1 pack(s)?\n\n%2\n\nThis action cannot be undone.")
            .arg(packIds.size())
            .arg(packNames.join("\n"))
    );

    if (!confirmed) return;

    int successCount = 0;
    int failCount = 0;
    QStringList failedNames;
    QStringList failureReasons;

    for (const QString &packId : packIds) {
        CharacterPackManager::PackInfo info = m_packManager->packInfo(packId);
        if (m_packManager->uninstallPack(packId)) {
            ++successCount;
        } else {
            ++failCount;
            const QString name = info.displayName(m_packManager->activeLocale());
            failedNames.append(name);
            const QString why = m_packManager->lastError();
            if (!why.isEmpty()) {
                failureReasons.append(QStringLiteral("%1: %2").arg(name, why));
            }
        }
    }

    if (successCount > 0 && failCount == 0) {
        ensureAlertDialog();
        m_alertDialog->showAlert(tr("Delete Complete"),
            tr("Successfully deleted %1 pack(s).").arg(successCount));
    } else if (failCount > 0) {
        QString msg = tr("Successfully deleted %1 pack(s).").arg(successCount);
        msg += QLatin1Char('\n') + (failureReasons.isEmpty()
                                    ? tr("Failed to delete: %1").arg(failedNames.join(", "))
                                    : tr("Failed to delete:\n%1").arg(failureReasons.join("\n")));
        ensureAlertDialog();
        m_alertDialog->showAlert(tr("Delete Partial"), msg);
    }
}

void PackManagerWidget::refreshPackList()
{
    if (!m_listWidget || !m_packManager) return;

    m_listWidget->clear();

    const auto packs = m_packManager->availablePacks();
    const QString locale = m_packManager->activeLocale();

    for (const auto &pack : packs) {
        if (pack.source != CharacterPackManager::PackSource::User) {
            qDebug() << "PackManagerWidget: Filtering out built-in pack:" << pack.id << pack.name;
            continue;
        }

        QListWidgetItem *item = new QListWidgetItem(pack.displayName(locale));
        item->setData(Qt::UserRole, pack.id);
        item->setToolTip(tr("ID: %1\nPath: %2").arg(pack.id, pack.path));
        m_listWidget->addItem(item);
        qDebug() << "PackManagerWidget: Showing user pack:" << pack.id << pack.name;
    }

    // Update delete button state
    m_deleteButton->setEnabled(m_listWidget->count() > 0);
}
