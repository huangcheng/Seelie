#include "mainwindow.h"
#include "GlobalShortcutManager.h"
#include "SpriteAnimationEngine.h"
#include "LottieAnimationEngine.h"
#ifdef SEELIE_LIVE2D_SUPPORT
#include "Live2DAnimationEngine.h"
#endif
#ifdef SEELIE_TTS_ENABLED
#include "TTSEngine.h"
#endif
#include "CharacterPackManager.h"
#include "CharacterPack.h"
#include "PackDropHandler.h"
#include "ConfigManager.h"
#include "TipWidget.h"
#include "SettingsPanelWidget.h"
#include "ECGWidget.h"
#include "SystemTray.h"
#include "EventRouter.h"
#include "TipsCatalog.h"
#include "FullscreenWatcher.h"
#include "PetStateMachine.h"
#include "MemoryManager.h"
#include "PersonaEngine.h"
#include "StatisticsDialog.h"

#include <QPainter>
#include <QRegularExpression>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include "StyledAlertWidget.h"
#include <QAction>
#include <QApplication>
#include <QRandomGenerator>
#include <QTranslator>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEnterEvent>
#include <QMimeData>
#include <QUrl>
#include <QTimer>
#include <QShowEvent>
#include <QStandardPaths>
#include <QDir>
#include <algorithm>

#ifdef Q_OS_WIN
// windows.h stays because we handle WM_DISPLAYCHANGE in nativeEvent() and
// reach into MSG. The DWM-specific stuff (dwmapi.h + DWMWA_* fallbacks)
// moved into src/PlatformWindow.cpp — see PlatformWindow::applyDwmFramelessAttributes.
#include <windows.h>
#endif
#include "PlatformWindow.h"

MainWindow::MainWindow(ConfigManager *config, QTranslator *translator, QWidget *parent)
    : QWidget(parent)
    , m_config(config)
    , m_translator(translator)
{
    setupWindowFlags();

    // Enable drag-and-drop
    setAcceptDrops(true);

    // Receive mouseMoveEvent without a button held, so Live2D models can
    // track the cursor (head/eyes follow pointer).
    setMouseTracking(true);

    // Window is taller than the pet so the speech bubble fits above it
    setFixedSize(124, 200);

    // Initialize subsystems
    m_engine = new SpriteAnimationEngine(this);
    m_lottieEngine = new LottieAnimationEngine(this);
#ifdef SEELIE_LIVE2D_SUPPORT
    m_live2dEngine = new Live2DAnimationEngine(this);
#endif

    // Create floating widgets
    m_tipWidget = new TipWidget(nullptr); // no parent — separate top-level widget
    m_tipWidget->setAnchorRect(petRect());
    m_tipWidget->anchorTo(this);

    m_settingsPanel = new SettingsPanelWidget(m_config, nullptr);
    m_settingsPanel->setAnchorRect(petRect());
    m_settingsPanel->hide();

#ifdef SEELIE_TTS_ENABLED
    m_ttsEngine = new TTSEngine(m_config, this);
    m_ttsEngine->start();

    connect(m_tipWidget, &TipWidget::bubbleRequested,
            this, [this](const QString &title, const QString &message, TipWidget::BubbleType type) {
        Q_UNUSED(title)
        if (type != TipWidget::TipBubble) return;
        if (!m_ttsEngine || !m_config->ttsEnabled()) return;
        // ECG mode hides the pet entirely — speaking would be an out-of-context
        // surprise. Match the visual tip suppression in onDisplayModeChanged().
        if (m_config->displayMode() == ConfigManager::DisplayMode::Ecg) return;
        // If persona is active, the queued personaEngine path is about to
        // replace the body. Defer TTS to that path so audio matches the
        // final visible text instead of speaking stale catalog body.
        const bool personaActive = m_personaEngine
            && m_config->personaEnabled()
            && !m_config->personaProfile().isEmpty();
        if (personaActive) return;
        m_ttsEngine->speak(message);
    });

    connect(m_ttsEngine, &TTSEngine::authFailed,
            m_settingsPanel, &SettingsPanelWidget::showAuthFailedHint);

    // Test button works in any mode — the user explicitly asked for it.
    // Use testSpeak() (not speak()) so the provider HTTP layer is exercised
    // even when the canned test phrase is already in the voice cache.
    connect(m_settingsPanel, &SettingsPanelWidget::testTtsRequested,
            m_ttsEngine, &TTSEngine::testSpeak);
    connect(m_settingsPanel, &SettingsPanelWidget::clearVoiceCacheRequested,
            m_ttsEngine, &TTSEngine::clearVoiceCache);
#endif

    m_ecgWidget = new ECGWidget(nullptr); // top-level, like the tip bubble
    m_ecgWidget->setAnchorRect(petRect());
    m_ecgWidget->anchorTo(this);

    // Wire ECG chassis drag so MainWindow tracks the same delta
    connect(m_ecgWidget, &ECGWidget::dragMoved, this, [this](QPoint delta) {
        move(pos() + delta);
        emit positionChanged(pos());
    });

    connect(m_ecgWidget, &ECGWidget::contextMenuRequested,
            this, &MainWindow::showContextMenu);

    connect(m_config, &ConfigManager::displayModeChanged,
            this, &MainWindow::onDisplayModeChanged);

    // Sync initial state once everything is constructed
    onDisplayModeChanged(m_config->displayMode());

    // Connect position change for config persistence
    connect(this, &MainWindow::positionChanged, m_config, &ConfigManager::setWindowPosition);

    // Reposition floating widgets when pet moves
    connect(this, &MainWindow::positionChanged, this, [this](const QPoint &) {
        m_tipWidget->setAnchorRect(petRect());
        m_tipWidget->anchorTo(this);
        if (m_settingsPanel->isVisible()) {
            m_settingsPanel->setAnchorRect(petRect());
            m_settingsPanel->anchorTo(this);
        }
        // Note: ECG widget intentionally NOT re-anchored here. In ECG mode
        // the widget owns its own position via chassis drag, and the
        // MainWindow position follows the ECG (not the other way around).
        // Re-anchoring would create a feedback loop that pins ECG in place.
    });

    // Connect effect triggers from animation engines
    // (Effects removed - using sprite animations only)

    // Repaint widget whenever animation engine advances a frame
    connect(m_engine, &SpriteAnimationEngine::frameChanged,
            this, QOverload<>::of(&QWidget::update));
    connect(m_lottieEngine, &LottieAnimationEngine::frameChanged,
            this, QOverload<>::of(&QWidget::update));
#ifdef SEELIE_LIVE2D_SUPPORT
    connect(m_live2dEngine, &Live2DAnimationEngine::frameChanged,
            this, QOverload<>::of(&QWidget::update));
#endif

#ifdef Q_OS_WIN
    // Refresh DWM attributes every 30s — display sleep/wake or DWM restart
    // can drop the corner-preference / backdrop / NC-rendering settings.
    // We also force a window-style refresh (SWP_FRAMECHANGED) so Windows
    // re-evaluates the composition surface, and queue a repaint so Qt
    // re-composites the widget.  The bubble and ECG widgets get the same
    // treatment so they don't disappear while the pet stays visible.
    m_dwmRefreshTimer = new QTimer(this);
    m_dwmRefreshTimer->setInterval(30000);
    connect(m_dwmRefreshTimer, &QTimer::timeout,
            this, &MainWindow::refreshAllDwmAttributes);
    m_dwmRefreshTimer->start();
#endif

    // Gaming Mode: start fullscreen watcher if enabled at launch
    m_fullscreenWatcher = new FullscreenWatcher(this);
    connect(m_fullscreenWatcher, &FullscreenWatcher::fullscreenAppStarted,
            this, &MainWindow::onFullscreenStarted);
    connect(m_fullscreenWatcher, &FullscreenWatcher::fullscreenAppStopped,
            this, &MainWindow::onFullscreenStopped);
    if (m_config->gamingModeEnabled())
        m_fullscreenWatcher->start();

    connect(m_config, &ConfigManager::gamingModeEnabledChanged,
            this, [this](bool enabled) {
        if (enabled) {
            m_fullscreenWatcher->start();
        } else {
            m_fullscreenWatcher->stop();
            // Restore windows if they were hidden by Gaming Mode
            if (m_hiddenByGamingMode) {
                m_hiddenByGamingMode = false;
                if (m_visible)
                    onDisplayModeChanged(m_config->displayMode());
            }
        }
    });
}

MainWindow::~MainWindow()
{
    // Tear down the three top-level widgets (constructed with nullptr parent
    // so they're separate windows, but tracked here as raw pointer members).
    // Stop the ECG widget first to halt its timers + audio, otherwise a tick
    // fired between hide() and delete can touch a widget that's mid-destruct.
    if (m_ecgWidget) {
        m_ecgWidget->stop();
        delete m_ecgWidget;
        m_ecgWidget = nullptr;
    }
    if (m_settingsPanel) {
        delete m_settingsPanel;
        m_settingsPanel = nullptr;
    }
#ifdef SEELIE_TTS_ENABLED
    // m_ttsEngine is parented to `this` (constructed with `this` as parent
    // at line 86). Qt's parent-child mechanism deletes it automatically when
    // ~MainWindow() runs — an explicit `delete` here would double-free.
    // Call stop() to halt audio output; the actual destruction is left to Qt.
    if (m_ttsEngine) {
        m_ttsEngine->stop();
    }
#endif
    if (m_tipWidget) {
        delete m_tipWidget;
        m_tipWidget = nullptr;
    }
}

// ── Animation dispatch (was free functions in main.cpp) ──────────────────
// Centralises the Live2D > Lottie > Sprite fallback chain so call sites
// don't duplicate it. Audit H3.

void MainWindow::dispatchAnimation(const QString &anim,
                                   AnimationEngine::Priority priority)
{
    if (anim.isEmpty()) return;
#ifdef SEELIE_LIVE2D_SUPPORT
    if (m_live2dEngine && m_live2dEngine->hasAnimations()) {
        m_live2dEngine->playAnimation(anim, priority);
        return;
    }
#endif
    if (m_lottieEngine && m_lottieEngine->hasAnimations()) {
        m_lottieEngine->playAnimation(anim, priority);
        return;
    }
    if (m_engine && m_engine->hasAnimations()) {
        m_engine->playAnimation(anim, priority);
    }
}

void MainWindow::dispatchAnimationChain(const QStringList &chain,
                                        AnimationEngine::Priority priority)
{
    if (chain.isEmpty()) return;
#ifdef SEELIE_LIVE2D_SUPPORT
    if (m_live2dEngine && m_live2dEngine->hasAnimations()) {
        m_live2dEngine->playAnimationChain(chain, priority);
        return;
    }
#endif
    if (m_lottieEngine && m_lottieEngine->hasAnimations()) {
        m_lottieEngine->playAnimation(chain.first(), priority);
        return;
    }
    if (m_engine && m_engine->hasAnimations()) {
        m_engine->playAnimation(chain.first(), priority);
    }
}

void MainWindow::setupWindowFlags()
{
    setWindowFlags(
        Qt::FramelessWindowHint        // No window frame
        | Qt::WindowStaysOnTopHint     // Always on top
        | Qt::Tool                     // No taskbar entry
        | Qt::WindowDoesNotAcceptFocus // Don't steal focus
    );
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    // Windows DWM otherwise paints a default white background BEFORE
    // paintEvent runs (TranslucentBackground alone isn't enough on Win32),
    // and once it sees an opaque rectangle it adds a drop shadow + light
    // edge — visually a frame around the pet. Suppressing the system
    // background makes the window genuinely transparent on every platform.
    setAttribute(Qt::WA_NoSystemBackground, true);
#ifdef Q_OS_MAC
    // macOS: tool windows are hidden when the app is not active.
    // This keeps the pet visible at all times.
    setAttribute(Qt::WA_MacAlwaysShowToolWindow, true);
#endif
}

void MainWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // Windows 11's DWM auto-applies rounded corners, a drop shadow, and a
    // Mica/Acrylic backdrop tint to top-level windows by default — even
    // frameless tool windows with TranslucentBackground+NoSystemBackground
    // get the chrome. Opt out per-window via the DWM API. winId() is valid
    // by showEvent() because the native window has just been realised.
    PlatformWindow::applyDwmFramelessAttributes(this);
}

#ifdef Q_OS_WIN
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    if (eventType == "windows_generic_MSG") {
        MSG *msg = static_cast<MSG *>(message);
        if (msg->message == WM_DISPLAYCHANGE) {
            // Display resolution/depth changed (display sleep/wake, monitor
            // connect/disconnect, RDP session).  DWM may have restarted, so
            // re-apply attributes immediately instead of waiting for the
            // 30-second timer.
            refreshAllDwmAttributes();
        }
    }
    return QWidget::nativeEvent(eventType, message, result);
}

void MainWindow::refreshAllDwmAttributes()
{
    PlatformWindow::applyDwmFramelessAttributes(this);
    // Re-apply the Qt attribute that suppresses the system background
    // paint — DWM restart can silently drop it, leaving a white rect.
    setAttribute(Qt::WA_NoSystemBackground, true);
    PlatformWindow::refreshComposition(this);

    // Keep the floating widgets in sync — they have their own native
    // windows and their DWM attributes can degrade independently.
    if (m_tipWidget) m_tipWidget->refreshDwmAttributes();
}
#endif

void MainWindow::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const QRect pet = petRect();

    // Draw character animation (Live2D, Lottie, or sprite sheet).
    // On Windows, Live2D's OpenGL context can be invalidated by DWM restart
    // or GPU power-state changes. Fall back through engines if the current
    // one fails to produce a frame.
#ifdef SEELIE_LIVE2D_SUPPORT
    if (m_live2dEngine && m_live2dEngine->isPlaying()) {
        if (m_live2dEngine->lastPaintSuccessful()) {
            m_live2dEngine->paint(&painter, pet);
        } else if (m_lottieEngine && m_lottieEngine->isPlaying()) {
            m_lottieEngine->paint(&painter, pet);
        } else if (m_engine) {
            m_engine->paint(&painter, pet);
        }
    } else
#endif
    if (m_lottieEngine && m_lottieEngine->isPlaying()) {
        m_lottieEngine->paint(&painter, pet);
    } else if (m_engine) {
        m_engine->paint(&painter, pet);
    }

    // (Speech bubbles are now shown via TipWidget)
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && isInPetRect(event->pos())) {
        m_dragStartPos = event->globalPosition().toPoint();
        m_dragWindowPos = pos();
        m_dragging = false;
    }
    QWidget::mousePressEvent(event);
}

QRect MainWindow::petRect() const
{
    int y = height() - m_petSize.height();
    if (y < 0) y = 0;
    return QRect(0, y, m_petSize.width(), m_petSize.height());
}

bool MainWindow::isInPetRect(const QPoint &pos) const
{
    return petRect().contains(pos);
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    // Feed the Live2D drag manager so the head/eyes track the cursor.
    // Map pointer position inside petRect to normalized (-1..+1), Y-up
    // (Cubism convention). Skip for clicks that are starting a window drag.
#ifdef SEELIE_LIVE2D_SUPPORT
    if (m_live2dEngine && isInPetRect(event->pos())) {
        const QRect pet = petRect();
        // Defensive guard: a malformed manifest can set frameWidth /
        // frameHeight to 0, which would propagate through petRect() as a
        // zero-size rect and divide-by-zero on the next two lines.
        if (pet.width() > 0 && pet.height() > 0) {
            const float nx = 2.0f * (event->pos().x() - pet.x()) / float(pet.width())  - 1.0f;
            const float ny = 1.0f - 2.0f * (event->pos().y() - pet.y()) / float(pet.height());
            m_live2dEngine->setPointerTarget(nx, ny);
        }
    }
#endif

    if (event->buttons() & Qt::LeftButton) {
        const QPoint delta = event->globalPosition().toPoint() - m_dragStartPos;

        if (!m_dragging && delta.manhattanLength() > DRAG_THRESHOLD) {
            m_dragging = true;
            // Only sprite packs ship a 'gesture_down' animation. For Live2D
            // the drag reaction is already provided by pointer tracking.
            if (m_engine->hasAnimations()) {
                m_engine->playAnimation("gesture_down", SpriteAnimationEngine::HighPriority);
            }
        }

        if (m_dragging) {
            move(m_dragWindowPos + delta);
        }
    }
    QWidget::mouseMoveEvent(event);
}

void MainWindow::leaveEvent(QEvent *event)
{
#ifdef SEELIE_LIVE2D_SUPPORT
    if (m_live2dEngine) m_live2dEngine->setPointerTarget(0.0f, 0.0f);
#endif
    if (m_stateMachine) {
        m_stateMachine->onSyntheticEvent(QStringLiteral("user.hoverLeave"));
    }
    QWidget::leaveEvent(event);
}

void MainWindow::enterEvent(QEnterEvent *event)
{
#ifdef SEELIE_LIVE2D_SUPPORT
    if (m_live2dEngine) {
        const QRect pr = petRect();
        const QPointF center = pr.center();
        m_live2dEngine->setPointerTarget(
            static_cast<float>(center.x()),
            static_cast<float>(center.y()));
    }
#endif
    if (m_stateMachine) {
        m_stateMachine->onSyntheticEvent(QStringLiteral("user.hoverEnter"));
    }
    QWidget::enterEvent(event);
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_dragging) {
            m_dragging = false;
            if (m_engine->hasAnimations()) {
                m_engine->playAnimation("lookdown", SpriteAnimationEngine::HighPriority);
                m_engine->playAnimation("rest", SpriteAnimationEngine::NormalPriority);
            }
            emit positionChanged(pos());
        } else if (isInPetRect(event->pos())) {
            // Route mouse-click through FSM so the state machine handles
            // user interaction and can trigger the appropriate animation chain.
            if (m_stateMachine) {
                m_stateMachine->onSyntheticEvent(QStringLiteral("user.click"));
                showRandomGreeting();
                QWidget::mouseReleaseEvent(event);
                return;
            }
            // Fallback for sprite packs without an event router wired.
            const QStringList clickAnims = {"click1", "click2"};
            const QString anim = clickAnims.at(QRandomGenerator::global()->bounded(clickAnims.size()));
            m_engine->playAnimation(anim, SpriteAnimationEngine::HighPriority);
            showRandomGreeting();
        }
    }
    QWidget::mouseReleaseEvent(event);
}

void MainWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && isInPetRect(event->pos())) {
        if (m_stateMachine) {
            m_stateMachine->onSyntheticEvent(QStringLiteral("user.doubleclick"));
            showRandomGreeting();
            QWidget::mouseDoubleClickEvent(event);
            return;
        }
        const QStringList dblAnims = {"doubleclick1", "doubleclick2"};
        const QString anim = dblAnims.at(QRandomGenerator::global()->bounded(dblAnims.size()));
        m_engine->playAnimation(anim, SpriteAnimationEngine::HighPriority);
        showRandomGreeting();
    }
    QWidget::mouseDoubleClickEvent(event);
}

void MainWindow::contextMenuEvent(QContextMenuEvent *event)
{
    showContextMenu(event->globalPos());
}

void MainWindow::showContextMenu(const QPoint &globalPos)
{
    QMenu menu(this);
    int menuFontSize = 10;
#ifdef Q_OS_MAC
    menuFontSize = 13;
#endif
    QFont menuFont("HarmonyOS Sans SC", menuFontSize);
    menuFont.setStyleStrategy(QFont::PreferAntialias);
    menu.setFont(menuFont);

    QAction *toggleAction = menu.addAction(m_visible ? tr("Hide") : tr("Show"));
    connect(toggleAction, &QAction::triggered, this, &MainWindow::toggleVisibility);

    menu.addSeparator();

    QAction *settingsAction = menu.addAction(tr("Settings"));
    connect(settingsAction, &QAction::triggered, this, &MainWindow::openSettings);

    QAction *aboutAction = menu.addAction(tr("About"));
    connect(aboutAction, &QAction::triggered, this, [this]() {
        const auto t = TipsCatalog::instance().message(QStringLiteral("about"));
        // Always use the styled modal — the About body contains HTML <a>
        // links which the TipBubble's QPainter::drawText path can't render
        // (links would appear as literal markup). The dialog uses a QLabel
        // with rich-text + setOpenExternalLinks(true), so https:// and
        // mailto: hand off to the OS.
        StyledAlertWidget *dialog = new StyledAlertWidget(nullptr);
        // dismissed → deleteLater handles cleanup. Do NOT also set
        // WA_DeleteOnClose — that creates two independent deletion paths
        // (dismissed→deleteLater AND Qt's close-event→delete) which can
        // double-free. M25 fix.
        dialog->setPetWindow(this);
        connect(dialog, &StyledAlertWidget::dismissed, dialog, &QObject::deleteLater);
        dialog->showAlert(t.title, t.body);
    });

    menu.addSeparator();

    QAction *quitAction = menu.addAction(tr("Quit"));
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

    menu.exec(globalPos);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    PackDropHandler::handleDragEnter(event);
}

void MainWindow::dropEvent(QDropEvent *event)
{
    PackDropHandler::handleDrop(event, m_packManager, m_tipWidget);
}

void MainWindow::onDisplayModeChanged(ConfigManager::DisplayMode mode)
{
    if (mode == ConfigManager::DisplayMode::Ecg) {
        m_tipWidget->hideBubble();
        m_tipWidget->setSuppressed(true);
        // No new TTS will start — see the bubbleRequested guard above. Any
        // utterance already mid-stream finishes naturally; stopping it would
        // require a heavier teardown than the gain warrants.
        hide();
        if (m_ecgWidget) {
            m_ecgWidget->setAnchorRect(petRect());
            m_ecgWidget->anchorTo(this);
            m_ecgWidget->start();
        }
    } else {
        if (m_ecgWidget) m_ecgWidget->stop();
        m_tipWidget->setSuppressed(false);
        show();
    }
}

void MainWindow::onFullscreenStarted()
{
    // Only hide if the pet is currently visible and not already hidden by us
    if (!m_hiddenByGamingMode && m_visible) {
        m_hiddenByGamingMode = true;
        hide();
        if (m_tipWidget) {
            m_tipWidget->hideBubble();
            m_tipWidget->setSuppressed(true);
        }
        if (m_ecgWidget && m_ecgWidget->isVisible())
            m_ecgWidget->hide();
        qDebug() << "MainWindow: Gaming Mode — hiding pet (fullscreen app detected)";
    }
}

void MainWindow::onFullscreenStopped()
{
    if (m_hiddenByGamingMode) {
        m_hiddenByGamingMode = false;
        if (m_visible) {
            onDisplayModeChanged(m_config->displayMode());
            if (m_ecgWidget && !m_ecgWidget->isVisible()
                    && m_config->displayMode() == ConfigManager::DisplayMode::Ecg)
                m_ecgWidget->show();
        }
        qDebug() << "MainWindow: Gaming Mode — restoring pet (fullscreen app gone)";
    }
}

void MainWindow::toggleVisibility()
{
    m_visible = !m_visible;
    if (m_visible) {
        // Restore to current mode
        onDisplayModeChanged(m_config->displayMode());
        if (m_config->displayMode() == ConfigManager::DisplayMode::Character) {
            m_engine->playAnimation("wave", SpriteAnimationEngine::HighPriority);
        }
    } else {
        hide();
        m_tipWidget->hideBubble();
        m_tipWidget->setSuppressed(true);
        m_settingsPanel->hideAnimated();
        if (m_ecgWidget) m_ecgWidget->stop();
    }
}

void MainWindow::openSettings()
{
    if (m_shortcutManager) {
        m_shortcutManager->setEnabled(false);
    }

    // In ECG mode this MainWindow is hidden, so anchoring Settings to its
    // petRect() lands the panel wherever MainWindow's last known coordinates
    // were — which is wrong if the user has dragged the ECG away. Anchor to
    // the ECG widget itself when it's the active display.
    if (m_ecgWidget && m_ecgWidget->isVisible()) {
        m_settingsPanel->setAnchorRect(QRect(0, 0, m_ecgWidget->width(), m_ecgWidget->height()));
        m_settingsPanel->anchorTo(m_ecgWidget);
    } else {
        m_settingsPanel->setAnchorRect(petRect());
        m_settingsPanel->anchorTo(this);
    }
    m_settingsPanel->showAnimated();
}

void MainWindow::setSystemTray(SystemTray *tray)
{
    m_systemTray = tray;
    if (m_systemTray) {
        if (m_packManager) {
            m_systemTray->setCharacterPackManager(m_packManager);
        }
        connect(m_systemTray, &SystemTray::statisticsTriggered,
                this, &MainWindow::onShowStatistics);
    }
}

void MainWindow::onShowStatistics()
{
    if (m_statsDialog) {
        m_statsDialog->raise();
        m_statsDialog->activateWindow();
        return;
    }
#ifdef SEELIE_TTS_ENABLED
    TTSEngine *tts = m_ttsEngine;
#else
    TTSEngine *tts = nullptr;
#endif
    m_statsDialog = new StatisticsDialog(m_memory, tts,
                                         m_eventRouter, m_ipcServer,
                                         m_personaEngine, this);
    m_statsDialog->setAttribute(Qt::WA_DeleteOnClose);
    m_statsDialog->show();
}

void MainWindow::setGlobalShortcutManager(GlobalShortcutManager *manager)
{
    m_shortcutManager = manager;
    if (m_settingsPanel && m_shortcutManager) {
        connect(m_settingsPanel, &SettingsPanelWidget::panelHidden,
                this, [this]() {
            if (m_shortcutManager && m_config && m_config->globalShortcutEnabled()) {
                m_shortcutManager->setEnabled(true);
            }
        });
    }
}

void MainWindow::setCharacterPackManager(CharacterPackManager *manager)
{
    // H8: Disconnect previous manager's signals before wiring the new one.
    if (m_packManager) {
        disconnect(m_packManager, nullptr, this, nullptr);
    }

    m_packManager = manager;
    if (m_packManager) {
        m_packManager->setActiveLocale(m_config ? m_config->language() : QString());

        connect(m_packManager, &CharacterPackManager::activePackChanged,
                this, &MainWindow::onActivePackChanged);

        // Pass to settings panel
        if (m_settingsPanel) {
            m_settingsPanel->setCharacterPackManager(manager);
        }

        // Load active pack immediately (pack may have been loaded before signal was connected)
        if (m_packManager->activePack()) {
            onActivePackChanged();
        }
    }
}

void MainWindow::setMemoryManager(MemoryManager *memory)
{
    m_memory = memory;
    if (m_settingsPanel) {
        m_settingsPanel->setMemoryManager(memory);
    }
    // Wire gaming_mode milestone (deferred until MemoryManager is available)
    if (m_memory && m_config) {
        connect(m_config, &ConfigManager::gamingModeEnabledChanged,
                m_memory, [this](bool enabled) {
            if (enabled) {
                m_memory->checkMilestone(QStringLiteral("gaming_mode"),
                    tr("Gaming Mode activated!"),
                    tr("Seelie will hide when fullscreen apps are detected."));
            }
        });
    }
}

void MainWindow::setPersonaEngine(PersonaEngine *engine)
{
    m_personaEngine = engine;
    if (!m_personaEngine) return;

    // (a) EventRouter → PersonaEngine: resolve event → potentially upgrade bubble text.
    // The existing EventRouter path already shows the TipsCatalog text; this
    // runs in parallel and overwrites with the persona-resolved text (same value
    // when persona is off/no LLM configured, upgraded value when LLM responds).
    if (m_eventRouter) {
        connect(m_eventRouter, &EventRouter::eventProcessed,
                this, [this](const QString &name, const QJsonObject &payload) {
            if (!m_personaEngine) return;
            PersonaEngine::Resolved r = m_personaEngine->resolve(name, payload);
            if (r.text.isEmpty()) return;
            if (!m_tipWidget || !m_tipWidget->isVisible()) return;
            m_activeBubbleRequestId = r.requestId;
            m_tipWidget->updateMessage(r.text);
            // TTS the final body (catalog-driven speak was suppressed earlier
            // when persona is active — see TipWidget::bubbleRequested handler).
            if (m_ttsEngine && m_config && m_config->ttsEnabled()
                && m_config->displayMode() != ConfigManager::DisplayMode::Ecg) {
                m_ttsEngine->speak(r.text);
            }
        }, Qt::QueuedConnection);
    }

    // (b) MemoryManager::milestoneReached → PersonaEngine
    if (m_memory) {
        connect(m_memory, &MemoryManager::milestoneReached,
                this, [this](const QString &title, const QString &body) {
            Q_UNUSED(body)
            if (!m_personaEngine) return;
            PersonaEngine::Resolved r = m_personaEngine->resolve(
                QStringLiteral("milestone.") + title, QJsonObject{});
            if (r.text.isEmpty()) return;
            if (!m_tipWidget || !m_tipWidget->isVisible()) return;
            m_activeBubbleRequestId = r.requestId;
            m_tipWidget->updateMessage(r.text);
        });
    }

    // (c) CharacterPackManager::activePackChanged → refresh pack id + persona hash
    if (m_packManager) {
        connect(m_packManager, &CharacterPackManager::activePackChanged,
                this, [this](CharacterPack *pack) {
            if (!m_personaEngine) return;
            m_personaEngine->setActivePackId(pack ? pack->metadata().id : QString());
            m_personaEngine->setPersonaHash(pack ? pack->personaHash() : QString());
        });
        // Apply current pack immediately
        if (CharacterPack *pack = m_packManager->activePack()) {
            m_personaEngine->setActivePackId(pack->metadata().id);
            m_personaEngine->setPersonaHash(pack->personaHash());
        }
    }

    // (d) PersonaEngine::tipUpgraded → slot
    connect(m_personaEngine, &PersonaEngine::tipUpgraded,
            this, &MainWindow::onTipUpgraded);
}

void MainWindow::onTipUpgraded(quint64 requestId, const QString &newText)
{
    // Only apply if this upgrade belongs to the currently active bubble request
    // and the bubble is still visible.
    if (requestId != m_activeBubbleRequestId) return;
    if (!m_tipWidget || !m_tipWidget->isVisible()) return;
    m_tipWidget->updateMessage(newText);

    // Re-fire TTS for the upgraded body.
    if (m_ttsEngine && m_config && m_config->ttsEnabled()
        && m_config->displayMode() != ConfigManager::DisplayMode::Ecg) {
        m_ttsEngine->speak(newText);
    }
}

void MainWindow::onActivePackChanged()
{
    qDebug() << "[ONACTIVE] onActivePackChanged called";
    if (!m_packManager) {
        qDebug() << "  no pack manager";
        return;
    }

    CharacterPack *pack = m_packManager->activePack();
    if (!pack) {
        qDebug() << "  no active pack";
        return;
    }

    qDebug() << "  Active pack id:" << m_packManager->activePackId()
             << "name:" << pack->metadata().name
             << "engineType:" << static_cast<int>(pack->characterConfig().engineType)
             << "isValid:" << pack->isValid();

    // Resize window based on pack frame dimensions × the pack's displayScale.
    // Default displayScale is 1.0 (native resolution). Sprite packs can opt
    // into a larger rendered size per-manifest to match Live2D's 300×300,
    // trading sharpness for visual parity — it's a per-pack call, not a
    // global normalization (which blurs low-res ClippyJS art unconditionally).
    int fw = pack->characterConfig().frameWidth;
    int fh = pack->characterConfig().frameHeight;
    const float displayScale = pack->characterConfig().displayScale;
    if (fw > 0 && fh > 0) {
        const int displayW = static_cast<int>(fw * displayScale);
        const int displayH = static_cast<int>(fh * displayScale);
        int tipSpace = height() - petRect().height();
        // Resize window first so height() updates before m_petSize changes,
        // avoiding a transient state where petRect() computes a negative y.
        setFixedSize(displayW, displayH + tipSpace);
        m_petSize = QSize(displayW, displayH);
        qDebug() << "  Window resized to:" << displayW << "x" << displayH;
    }

    if (m_ecgWidget && m_ecgWidget->isVisible()) {
        m_ecgWidget->setAnchorRect(petRect());
        m_ecgWidget->anchorTo(this);
    }

    // Load animations based on pack type.
    // Stop every engine first: all three share the paint path, and Live2D
    // takes priority in paintEvent if isPlaying() is true — without this,
    // switching from a Live2D pack to a sprite pack would leave the old
    // Live2D frame squished into the sprite pack's smaller petRect.
    m_engine->stop();
    m_lottieEngine->stop();
#ifdef SEELIE_LIVE2D_SUPPORT
    m_live2dEngine->stop();
#endif

#ifndef SEELIE_LIVE2D_SUPPORT
    // If Live2D support was not compiled in but the selected pack requires
    // it, warn and auto-skip to the first non-Live2D pack.  Without this
    // fallback the sprite engine fails silently and the pet window renders
    // as a fully transparent rectangle.
    if (pack->characterConfig().engineType == CharacterPack::EngineType::Live2D) {
        qWarning() << "Live2D pack" << pack->metadata().name
                    << "selected but Live2D support not compiled in."
                    << "See CONTRIBUTING.md for Cubism SDK Core setup.";
        // Guard against recursion — switchPack re-emits activePackChanged.
        if (m_skipLive2dFallback) return;
        m_skipLive2dFallback = true;
        if (m_packManager) {
            const auto packs = m_packManager->availablePacks();
            for (const auto &info : packs) {
                if (info.id == pack->metadata().id) continue; // skip the Live2D pack
                m_packManager->switchPack(info.id);
                // onActivePackChanged recurses here; if the new pack loads,
                // one of the engines will be playing and we're done.
                if (m_lottieEngine->isPlaying() || m_engine->isPlaying()) {
                    m_skipLive2dFallback = false;
                    return;
                }
            }
        }
        m_skipLive2dFallback = false;
        qWarning() << "No non-Live2D packs available — pet will be invisible";
        return;
    }
#endif

#ifdef SEELIE_LIVE2D_SUPPORT
    if (pack->characterConfig().engineType == CharacterPack::EngineType::Live2D) {
        m_live2dEngine->loadFromCharacterPack(pack);
    } else
#endif
    if (pack->characterConfig().engineType == CharacterPack::EngineType::Lottie) {
        m_lottieEngine->loadFromCharacterPack(pack);
    } else {
        m_engine->loadFromCharacterPack(pack);
    }

#ifdef SEELIE_LIVE2D_SUPPORT
    // Crop the window to the character's actual silhouette once the Live2D
    // engine has produced a few frames (motion settles ~500ms after load).
    // Without this, Rice / Wanko / etc. render as a small character at the
    // bottom of a 300×300 frame with a huge empty top margin, and the tip
    // bubble anchors above that empty space instead of above the character.
    if (pack->characterConfig().engineType == CharacterPack::EngineType::Live2D) {
        const float displayScale = pack->characterConfig().displayScale;
        // Stamp this load attempt so a rapid re-trigger invalidates the
        // pending crop callback below; only the most recent load applies.
        const int loadId = ++m_packLoadId;
        QTimer::singleShot(500, this, [this, displayScale, loadId]() {
            if (loadId != m_packLoadId) return;        // superseded
            if (!m_live2dEngine) return;
            const QRect b = m_live2dEngine->characterBounds();
            if (b.isNull() || b.isEmpty()) return;
            // Match Live2DAnimationEngine::paint()'s source rect: full frame
            // width + measured height + generous top pad for motion headroom
            // and lighting/glow effects above the character.
            const int padTop = std::max(32, b.height() / 4);
            const int srcH = std::min(m_live2dEngine->renderHeight() - std::max(0, b.y() - padTop),
                                      b.height() + padTop);
            const int displayW = static_cast<int>(m_live2dEngine->renderWidth() * displayScale);
            const int displayH = static_cast<int>(srcH * displayScale);
            const int tipSpace = height() - petRect().height();
            setFixedSize(displayW, displayH + tipSpace);
            m_petSize = QSize(displayW, displayH);
            // Re-anchor floating widgets to the newly sized pet rect.
            m_tipWidget->setAnchorRect(petRect());
            m_tipWidget->anchorTo(this);
            if (m_ecgWidget && m_ecgWidget->isVisible()) {
                m_ecgWidget->setAnchorRect(petRect());
                m_ecgWidget->anchorTo(this);
            }
            update();
        });
    }
#endif
}

void MainWindow::retranslateUi()
{
    // Context menu is ephemeral — it will pick up translations on next show.
    // Nothing persistent to update here except the About bubble text which is also ephemeral.
}

void MainWindow::reloadTranslator(const QString &lang)
{
    QApplication *app = qApp;
    app->removeTranslator(m_translator);

    if (!lang.isEmpty() && lang != "en") {
        const QString baseName = "Seelie_" + lang;
        if (m_translator->load(":/i18n/" + baseName)) {
            app->installTranslator(m_translator);
        }
    }
}

void MainWindow::onLanguageChanged(const QString &lang)
{
    reloadTranslator(lang);
    TipsCatalog::instance().setLocale(lang);
    if (m_packManager) {
        m_packManager->setActiveLocale(lang);
    }
    retranslateUi();
    m_settingsPanel->retranslateUi();
    if (m_systemTray) {
        m_systemTray->retranslateUi();
    }
}

void MainWindow::showRandomGreeting()
{
    if (!m_tipWidget) return;

    const auto g = TipsCatalog::instance().randomGreeting();
    if (g.title.isEmpty()) return;

    QString title = g.title;
    QString body  = g.body;

    // Greeting dedup + {name} substitution (only if MemoryManager is wired)
    if (m_memory && m_memory->isValid()) {
        // Skip greeting if it's the same as last time
        if (g.title == m_memory->lastGreeting()) {
            const auto g2 = TipsCatalog::instance().randomGreeting();
            if (!g2.title.isEmpty()) {
                title = g2.title;
                body  = g2.body;
            }
        }
        m_memory->setLastGreeting(title);

        // {name} substitution
        const QString name = m_memory->effectiveName();
        if (!name.isEmpty()) {
            title.replace(QStringLiteral("{name}"), name.toHtmlEscaped());
            body.replace(QStringLiteral("{name}"),  name.toHtmlEscaped());
        } else {
            static const QRegularExpression strip(QStringLiteral(",?\\s*\\{name\\}"));
            title.replace(strip, QString());
            body.replace(strip,  QString());
        }
    }

    m_tipWidget->showBubble(title, body, TipWidget::TipBubble);
}
