#include "SystemTray.h"
#include "mainwindow.h"
#include "CharacterPackManager.h"
#include "CharacterPack.h"
#include "UpdateChecker.h"
#include "ConfigManager.h"
#include "PackManagerWidget.h"
#include "TipWidget.h"
#include "TipsCatalog.h"

#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QPainter>
#include <QDebug>
#include <QPixmap>
#include <QImage>
#include <QTimer>

SystemTray::SystemTray(QWidget *mainWindow, ConfigManager *config, QObject *parent)
    : QObject(parent)
    , m_mainWindow(mainWindow)
    , m_config(config)
{
    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setToolTip(tr("Seelie Desktop Pet"));

    // Use the application icon scaled for the system tray
    QIcon appIcon = qApp->windowIcon();
    if (!appIcon.isNull()) {
        // macOS menu bar expects ~22px icons; scale from app icon
        QPixmap trayPix = appIcon.pixmap(128, 128)
                              .scaled(44, 44, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QIcon trayIcon;
        trayIcon.addPixmap(trayPix);
        m_trayIcon->setIcon(trayIcon);
    } else {
        // Fallback: simple colored circle
        QPixmap pixmap(64, 64);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(QColor(0, 120, 215));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(8, 8, 48, 48);
        m_trayIcon->setIcon(QIcon(pixmap));
    }

    setupMenu();

    connect(m_trayIcon, &QSystemTrayIcon::activated,
            this, &SystemTray::onActivated);
}

SystemTray::~SystemTray()
{
    // m_trayMenu was allocated with no parent (QMenu requires a QWidget parent,
    // but SystemTray is a QObject). setContextMenu() does not take ownership.
    // Delete explicitly to avoid the leak (H3).
    delete m_trayMenu;
}

void SystemTray::show()
{
    m_trayIcon->show();
}

void SystemTray::onActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger) {
        // Toggle window visibility on click
        if (m_mainWindow) {
            if (m_mainWindow->isVisible()) {
                m_mainWindow->hide();
            } else {
                m_mainWindow->show();
                m_mainWindow->raise();
            }
        }
    }
}

void SystemTray::setupMenu()
{
    // Platform-specific font size: macOS uses 13, Windows uses 10
    int menuFontSize = 10;
#ifdef Q_OS_MAC
    menuFontSize = 13;
#endif
    QFont menuFont("HarmonyOS Sans SC", menuFontSize);
    menuFont.setStyleStrategy(QFont::PreferAntialias);
    // CJK strokes look mangled with default hinting on Windows; let the
    // rasterizer place glyphs by AA only.
    menuFont.setHintingPreference(QFont::PreferNoHinting);

    m_trayMenu = new QMenu();
    m_trayMenu->setFont(menuFont);

    // Ambient peek: disabled, non-clickable status line shown at the top
    // of the menu. Hidden until setMoodStatus() pushes the first text
    // (Task 7 wiring refreshes it on tier/bond changes).
    m_moodStatusAction = m_trayMenu->addAction(QString());
    m_moodStatusAction->setEnabled(false);   // informational, not clickable
    m_moodStatusAction->setVisible(false);   // shown once mood text arrives
    m_trayMenu->addSeparator();

    m_toggleAction = m_trayMenu->addAction(tr("Show/Hide"));
    connect(m_toggleAction, &QAction::triggered, this, [this]() {
        if (m_mainWindow) {
            if (m_mainWindow->isVisible()) {
                m_mainWindow->hide();
            } else {
                m_mainWindow->show();
                m_mainWindow->raise();
            }
        }
    });

    // Gaming Mode — checkable toggle
    m_gamingModeAction = m_trayMenu->addAction(tr("Gaming Mode"));
    m_gamingModeAction->setCheckable(true);
    m_gamingModeAction->setChecked(m_config ? m_config->gamingModeEnabled() : false);
    connect(m_gamingModeAction, &QAction::toggled, this, [this](bool checked) {
        if (m_config) m_config->setGamingModeEnabled(checked);
    });
    if (m_config) {
        connect(m_config, &ConfigManager::gamingModeEnabledChanged,
                this, [this](bool enabled) {
            QSignalBlocker blocker(m_gamingModeAction);
            m_gamingModeAction->setChecked(enabled);
        });
    }

    m_trayMenu->addSeparator();

    // Pack submenu
    m_packMenu = m_trayMenu->addMenu(tr("Model"));
    m_packMenu->setFont(menuFont);

    m_trayMenu->addSeparator();

    // Manage Models dialog
    m_manageModelsAction = m_trayMenu->addAction(tr("Manage Models"));
    connect(m_manageModelsAction, &QAction::triggered, this, &SystemTray::onManageModelsClicked);

    // Statistics dialog
    m_statsAction = m_trayMenu->addAction(tr("Statistics..."));
    connect(m_statsAction, &QAction::triggered, this, &SystemTray::statisticsTriggered);

    // Recall UI ("What do you remember?")
    m_recallAction = m_trayMenu->addAction(tr("What do you remember?"));
    connect(m_recallAction, &QAction::triggered, this, &SystemTray::recallTriggered);

    // Config submenu (Export / Import)
    m_configMenu = m_trayMenu->addMenu(tr("Config"));
    m_configMenu->setFont(menuFont);
    m_exportConfigAction = m_configMenu->addAction(tr("Export..."));
    m_importConfigAction = m_configMenu->addAction(tr("Import..."));
    connect(m_exportConfigAction, &QAction::triggered, this, &SystemTray::exportConfigTriggered);
    connect(m_importConfigAction, &QAction::triggered, this, &SystemTray::importConfigTriggered);

    m_trayMenu->addSeparator();

    // Check for updates
    m_checkUpdateAction = m_trayMenu->addAction(tr("Check for Updates"));
    connect(m_checkUpdateAction, &QAction::triggered, this, [this]() {
        if (m_updateChecker) {
            // Pass true so the response surfaces feedback whether or not
            // an update is actually available — user clicked, user gets
            // an answer.
            m_updateChecker->checkForUpdates(true);
        }
    });

    m_trayMenu->addSeparator();

    m_quitAction = m_trayMenu->addAction(tr("Quit"));
    connect(m_quitAction, &QAction::triggered, qApp, &QApplication::quit);

    m_trayIcon->setContextMenu(m_trayMenu);
}

void SystemTray::setCharacterPackManager(CharacterPackManager *manager)
{
    // H8: Disconnect previous manager's signals before wiring the new one.
    if (m_packManager) {
        disconnect(m_packManager, nullptr, this, nullptr);
    }

    m_packManager = manager;
    if (m_packManager) {
        connect(m_packManager, &CharacterPackManager::packListChanged,
                this, &SystemTray::refreshPackMenu);
        refreshPackMenu();
    }

    if (m_packManagerDialog) {
        // QPointer + WA_DeleteOnClose: closing the dialog auto-deletes it
        // and nulls m_packManagerDialog. Active dialogs explicitly destroyed
        // here use deleteLater() so a queued mouse/key event targeted at
        // the dialog cannot land after `delete`. M7.
        m_packManagerDialog->deleteLater();
        m_packManagerDialog = nullptr;
    }
}

void SystemTray::setUpdateChecker(UpdateChecker *checker)
{
    // H8: Disconnect previous checker's signals before wiring the new one.
    if (m_updateChecker) {
        disconnect(m_updateChecker, nullptr, this, nullptr);
    }

    m_updateChecker = checker;
    if (m_updateChecker) {
        connect(m_updateChecker, &UpdateChecker::updateAvailable,
                this, &SystemTray::onUpdateAvailable);
        connect(m_updateChecker, &UpdateChecker::noUpdateAvailable,
                this, &SystemTray::onNoUpdateAvailable);
        connect(m_updateChecker, &UpdateChecker::checkFailed,
                this, &SystemTray::onUpdateCheckFailed);
    }
}

void SystemTray::setTipWidget(TipWidget *tipWidget)
{
    m_tipWidget = tipWidget;
}

void SystemTray::onUpdateAvailable(const QString &current, const QString &latest, const QString &url)
{
    Q_UNUSED(url);
    // Always surface — auto + manual both deserve it. Route via the tip
    // bubble (and let it cascade into TTS via MainWindow's bubbleRequested
    // wiring) instead of the OS tray balloon, which is unreliable on
    // Windows when Focus Assist is on / notification permission is
    // missing. bypassUserSuppression=true so users who muted tip bubbles
    // still see the update notice — it's important system feedback, not
    // chatter.
    if (!m_tipWidget) {
        qWarning() << "UpdateChecker: update available but no TipWidget wired";
        return;
    }
    const auto t = TipsCatalog::instance().message(QStringLiteral("update.available"));
    const QString body = t.body.isEmpty()
        ? tr("Version %1 is available (current: %2)").arg(latest, current)
        : QString(t.body).replace(QStringLiteral("{latest}"), latest)
                         .replace(QStringLiteral("{current}"), current);
    m_tipWidget->showBubble(t.title.isEmpty() ? tr("Update Available") : t.title,
                            body, TipWidget::TipBubble, QString(), /*bypassUserSuppression=*/true);
}

void SystemTray::onNoUpdateAvailable(const QString &current)
{
    // Only feedback the user if THEY asked. The 5s post-launch auto check
    // stays silent on the happy path so the user doesn't get a tip every
    // single startup confirming they're up to date.
    if (!m_updateChecker || !m_updateChecker->wasUserTriggered()) {
        qDebug() << "UpdateChecker: no-update silent (auto check), current=" << current;
        return;
    }
    if (!m_tipWidget) {
        qWarning() << "UpdateChecker: manual no-update result but no TipWidget wired";
        return;
    }
    const auto t = TipsCatalog::instance().message(QStringLiteral("update.none"));
    const QString body = t.body.isEmpty()
        ? tr("You are running the latest version (%1)").arg(current)
        : QString(t.body).replace(QStringLiteral("{current}"), current);
    m_tipWidget->showBubble(t.title.isEmpty() ? tr("You're up to date") : t.title,
                            body, TipWidget::TipBubble, QString(), /*bypassUserSuppression=*/true);
}

void SystemTray::onUpdateCheckFailed(const QString &error)
{
    // Same rationale as onNoUpdateAvailable — auto checks stay silent on
    // failure (network is flaky, no point bothering the user). Manual
    // checks always surface the error so the user knows their click hit
    // a wall.
    if (!m_updateChecker || !m_updateChecker->wasUserTriggered()) {
        qDebug() << "UpdateChecker: silent failure (auto check):" << error;
        return;
    }
    if (!m_tipWidget) {
        qWarning() << "UpdateChecker: manual check failed but no TipWidget wired:" << error;
        return;
    }
    const auto t = TipsCatalog::instance().message(QStringLiteral("update.failed"));
    const QString body = t.body.isEmpty()
        ? tr("Could not check for updates: %1").arg(error)
        : QString(t.body).replace(QStringLiteral("{error}"), error);
    m_tipWidget->showBubble(t.title.isEmpty() ? tr("Update Check Failed") : t.title,
                            body, TipWidget::TipBubble, QString(), /*bypassUserSuppression=*/true);
}

void SystemTray::onPackActionTriggered()
{
    QAction *action = qobject_cast<QAction*>(sender());
    if (!action || !m_packManager) {
        return;
    }

    QString packId = action->data().toString();
    if (packId.isEmpty()) {
        return;
    }
    // QActionGroup flips the radio indicator on click before the slot runs,
    // so a failed switchPack() leaves the menu lying about which pack is
    // active. Repaint from the manager's truth so the user sees the
    // revert (and gets a chance to notice the failure in the log).
    if (!m_packManager->switchPack(packId)) {
        refreshPackMenu();
    }
}

// Fixed display order for known categories; unknown ones land at the end
// in their raw-id form. QT_TRANSLATE_NOOP marks the labels for lupdate
// against the SystemTray context without running tr() at static-init time.
static const struct {
    const char *id;
    const char *labelEn;
} kCategoryOrder[] = {
    { "originals",       QT_TRANSLATE_NOOP("PackCategories", "Standalone") },
    { "azur_lane",       QT_TRANSLATE_NOOP("PackCategories", "Azur Lane") },
    { "girls_frontline", QT_TRANSLATE_NOOP("PackCategories", "Girls' Frontline") },
    { "idol_dimension",  QT_TRANSLATE_NOOP("PackCategories", "Idol Dimension") },
    { "konosuba",        QT_TRANSLATE_NOOP("PackCategories", "Konosuba") },
    { "live2d_samples",  QT_TRANSLATE_NOOP("PackCategories", "Live2D Samples") },
};

void SystemTray::refreshPackMenu()
{
    if (!m_packMenu) {
        qDebug() << "SystemTray::refreshPackMenu — no packMenu";
        return;
    }

    m_packMenu->clear();
    delete m_packActionGroup;
    m_packActionGroup = new QActionGroup(this);
    m_packActionGroup->setExclusive(true);

    if (!m_packManager) {
        qDebug() << "SystemTray::refreshPackMenu — no packManager";
        return;
    }

    const auto packs = m_packManager->availablePacks();
    const QString activeId = m_packManager->activePackId();
    const QString locale = m_packManager->activeLocale();

    // Group packs by category (parser already supplied a default for legacy
    // manifests via author-string heuristics, so `category` is always set).
    QMap<QString, QVector<CharacterPackManager::PackInfo>> grouped;
    for (const auto &pack : packs) {
        const QString cat = pack.category.isEmpty()
                                ? QStringLiteral("originals") : pack.category;
        grouped[cat].append(pack);
    }

    auto addToSubmenu = [this, &activeId, &locale](QMenu *menu,
                                                    const QVector<CharacterPackManager::PackInfo> &list) {
        for (const auto &pack : list) {
            QAction *action = menu->addAction(pack.displayName(locale));
            action->setData(pack.id);
            action->setCheckable(true);
            action->setChecked(pack.id == activeId);
            m_packActionGroup->addAction(action);
            connect(action, &QAction::triggered, this, &SystemTray::onPackActionTriggered);
        }
    };

    QSet<QString> seen;
    for (const auto &c : kCategoryOrder) {
        const QString id = QString::fromLatin1(c.id);
        if (!grouped.contains(id)) continue;
        QMenu *sub = m_packMenu->addMenu(QCoreApplication::translate("PackCategories", c.labelEn));
        sub->setFont(m_packMenu->font());
        addToSubmenu(sub, grouped[id]);
        seen.insert(id);
    }
    // Future-proof: any category we haven't enumerated lands at the end
    // with the raw id as the menu label.
    for (auto it = grouped.constBegin(); it != grouped.constEnd(); ++it) {
        if (seen.contains(it.key())) continue;
        QMenu *sub = m_packMenu->addMenu(it.key());
        sub->setFont(m_packMenu->font());
        addToSubmenu(sub, it.value());
    }

    qDebug() << "SystemTray::refreshPackMenu — populated"
             << packs.size() << "packs across" << grouped.size() << "categories";
}

void SystemTray::retranslateUi()
{
    m_trayIcon->setToolTip(tr("Seelie Desktop Pet"));
    if (m_toggleAction) {
        m_toggleAction->setText(tr("Show/Hide"));
    }
    if (m_gamingModeAction) {
        m_gamingModeAction->setText(tr("Gaming Mode"));
    }
    if (m_packMenu) {
        m_packMenu->setTitle(tr("Model"));
    }
    if (m_checkUpdateAction) {
        m_checkUpdateAction->setText(tr("Check for Updates"));
    }
    if (m_quitAction) {
        m_quitAction->setText(tr("Quit"));
    }
    if (m_manageModelsAction) {
        m_manageModelsAction->setText(tr("Manage Models"));
    }
    if (m_statsAction) {
        m_statsAction->setText(tr("Statistics..."));
    }
    if (m_recallAction) {
        m_recallAction->setText(tr("What do you remember?"));
    }
    if (m_configMenu) {
        m_configMenu->setTitle(tr("Config"));
    }
    if (m_exportConfigAction) {
        m_exportConfigAction->setText(tr("Export..."));
    }
    if (m_importConfigAction) {
        m_importConfigAction->setText(tr("Import..."));
    }
    refreshPackMenu();
}

void SystemTray::onManageModelsClicked()
{
    if (!m_packManagerDialog) {
        m_packManagerDialog = new PackManagerWidget(m_packManager, nullptr);
        m_packManagerDialog->setAttribute(Qt::WA_DeleteOnClose);
        m_packManagerDialog->setPetWindow(m_mainWindow);
    }
    // Defer show until the tray menu has fully closed — on macOS showing a
    // window while the menu is still the key window prevents it from appearing.
    QTimer::singleShot(50, m_packManagerDialog, [this]() {
        m_packManagerDialog->showAnimated();
    });
}

void SystemTray::setMoodStatus(const QString &text)
{
    if (!m_moodStatusAction) return;
    m_moodStatusAction->setText(text);
    m_moodStatusAction->setVisible(!text.isEmpty());
}
