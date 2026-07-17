#include "mainwindow.h"
#include "ConfigManager.h"
#include "IPCServer.h"
#include "EventRouter.h"
#include "StatisticsManager.h"
#ifdef SEELIE_TTS_ENABLED
#include "TTSEngine.h"
#endif
#include "SpriteAnimationEngine.h"
#include "LottieAnimationEngine.h"
#ifdef SEELIE_LIVE2D_SUPPORT
#include "Live2DAnimationEngine.h"
#endif
#include "CharacterPackManager.h"
#include "CharacterPack.h"
#include "TipWidget.h"
#include "ECGWidget.h"
#include "TipsEngine.h"
#include "SystemTray.h"
#include "SystemContextEngine.h"
#include "UpdateChecker.h"
#include "GlobalShortcutManager.h"
#include "PetStateMachine.h"

#include <QApplication>
#include <QLocale>
#include <QLockFile>
#include <QTranslator>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QScreen>
#include <QDebug>
#include <QFile>
#include <QFontDatabase>
#include <QImageReader>
#include <QMutex>
#include <QTextStream>
#include <QTimer>
#include <memory>
#include <optional>

#include "TipsCatalog.h"
#include "MemoryManager.h"
#include "PersonaEngine.h"
#include "EmbeddingService.h"
#include "llm/LLMProvider.h"
#include "llm/LLMProfile.h"
#include "SettingsPanelWidget.h"

static QString configDir() {
    // QStandardPaths::ConfigLocation already resolves to <APPDATA>/<Org>/<App>
    // on Windows and the equivalent on macOS/Linux — no extra suffix needed.
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
}

static QString dataDir() {
    // AppLocalDataLocation is for user data (packs, logs), not config.
    // macOS: ~/Library/Application Support/Seelie/
    // Linux: ~/.local/share/Seelie/
    // Windows: %LOCALAPPDATA%/Seelie/
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}

// Mirror qDebug/qWarning/qInfo to a file next to the executable so a crashed
// /SUBSYSTEM:WINDOWS run still leaves a trace. Default Qt handler writes to
// OutputDebugString which is invisible without a debugger attached.
static void fileMessageHandler(QtMsgType type, const QMessageLogContext &,
                               const QString &msg)
{
    // Owned by std::unique_ptr (was raw `QFile *` — L7) so the file handle
    // and OS resources are released on process exit even though the static
    // lifetime means the OS would also reclaim them. Cleaner ownership.
    static std::unique_ptr<QFile> logFile;
    static QMutex mtx;
    static const QString LOG_NAME = QStringLiteral("seelie_debug.log");
    static const qint64 MAX_SIZE = 5 * 1024 * 1024;
    static const int MAX_ARCHIVES = 10;

    QMutexLocker lock(&mtx);

    if (!logFile) {
        // Write to a user-writable location, NOT next to the executable:
        // on macOS the executable lives inside Contents/MacOS/, and any extra
        // file there invalidates the codesignature ("code object is not signed
        // at all"), causing LaunchServices to refuse to launch the bundle.
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        QDir().mkpath(dir);
        const QString path = dir + "/" + LOG_NAME;
        logFile = std::make_unique<QFile>(path);
        // Failure handled by the isOpen() check below; the cast acknowledges
        // QFile::open's [[nodiscard]] without obscuring the control flow.
        (void)logFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate);
    }

    // Rotate if current log exceeds MAX_SIZE.
    if (logFile->size() >= MAX_SIZE) {
        // Timestamp suffix down to the second so a chatty crash loop doesn't
        // overwrite earlier same-day archives.
        const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
        const QString logDir = QFileInfo(*logFile).absolutePath();
        const QString rotatedPath = logDir + "/" + LOG_NAME + "." + stamp;
        logFile->close();
        QFile::remove(rotatedPath);
        QFile::rename(logDir + "/" + LOG_NAME, rotatedPath);
        QStringList oldLogs;
        const QDir logsDir(logDir);
        for (const QFileInfo &fi : logsDir.entryInfoList({LOG_NAME + ".*"}, QDir::Files)) {
            const QString fn = fi.fileName();
            if (fn != LOG_NAME) {
                oldLogs.append(fi.absoluteFilePath());
            }
        }
        if (oldLogs.size() > MAX_ARCHIVES) {
            QStringList sorted = oldLogs;
            sorted.sort();
            for (int i = 0; i < sorted.size() - MAX_ARCHIVES; ++i) {
                QFile::remove(sorted[i]);
            }
        }
        (void)logFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate);
    }

    if (!logFile->isOpen()) return;
    const char *level = "DEBUG";
    bool flushImmediately = false;
    switch (type) {
        case QtWarningMsg:  level = "WARN";  break;
        case QtCriticalMsg: level = "CRIT";  flushImmediately = true; break;
        case QtFatalMsg:    level = "FATAL"; flushImmediately = true; break;
        case QtInfoMsg:     level = "INFO";  break;
        default: break;
    }
    QTextStream(logFile.get()) << '[' << level << "] " << msg << '\n';
    // Coalesced flushing: previously every message did a synchronous flush()
    // which serialized every logging thread on a disk fsync. The OS already
    // buffers writes; we only force a flush for messages that signal an
    // imminent crash (Critical / Fatal) so the tail of the log survives.
    // Routine debug/info messages get flushed at process exit via the
    // qInstallMessageHandler cleanup path. Audit M2.
    if (flushImmediately) logFile->flush();
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    qInstallMessageHandler(fileMessageHandler);  // after QApplication so applicationDirPath() is valid
    a.setApplicationName("Seelie");
    // Intentionally not setOrganizationName — QStandardPaths::ConfigLocation
    // is <APPDATA>/<Org>/<App> on Windows, so setting Org=App="Seelie" produces
    // a redundant Local/Seelie/Seelie/ nesting. With only the application name set,
    // we get a clean Local/Seelie/. QSettings paths are unaffected because
    // ConfigManager constructs QSettings with an explicit (org, app) tuple.
    a.setWindowIcon(QIcon(":/icons/seelie.png"));
    a.setQuitOnLastWindowClosed(false); // system tray keeps it alive

    // --- Single instance enforcement -----------------------------------------
    const QString lockDir = configDir();
    QDir().mkpath(lockDir);
    QLockFile lockFile(lockDir + "/Seelie.lock");
    lockFile.setStaleLockTime(300000); // L2: 5 minutes — 30s was too short for debug sessions
    if (!lockFile.tryLock(100)) {
        qInfo() << "Seelie is already running. Bringing existing instance to front.";
        return 0;
    }

    // --- Config --------------------------------------------------------------
    ConfigManager config;
    config.load();

    // --- Translations --------------------------------------------------------
    QTranslator translator;
    QString lang = config.language();
    // Bootstrap the JSON tip catalog before MainWindow / EventRouter are
    // built so the very first bubble (e.g. session.start announce) already
    // has localized text.
    TipsCatalog::instance().setLocale(lang.isEmpty() ? QStringLiteral("en") : lang);
    if (!lang.isEmpty() && lang != "en") {
        const QString baseName = "Seelie_" + lang;
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
        }
    } else if (lang.isEmpty()) {
        // No preference saved yet — fall back to system locale on first launch.
        const QStringList uiLanguages = QLocale::system().uiLanguages();
        for (const QString &locale : uiLanguages) {
            const QString baseName = "Seelie_" + QLocale(locale).name();
            if (translator.load(":/i18n/" + baseName)) {
                a.installTranslator(&translator);
                break;
            }
        }
    }
    // If lang == "en", install no translator: Qt falls through to source strings.

    // --- Locate assets directory ----------------------------------------------
    // Searches upward from the executable to find the assets folder,
    // handling macOS app bundles, various build dir layouts, and source-tree runs.
    auto findAssetsDir = []() -> QString {
        QDir dir(QApplication::applicationDirPath());

#ifdef Q_OS_MAC
        // macOS app bundle: assets live in Contents/Resources/assets
        // (searched first so deployed bundles win over dev-tree fallbacks).
        QDir bundleDir(dir);
        if (bundleDir.cdUp() && bundleDir.cd("Resources") && bundleDir.cd("assets")) {
            QString candidate = bundleDir.absolutePath();
            if (QFile::exists(candidate + "/animations.json")) {
                return candidate;
            }
        }
#endif

        for (int i = 0; i < 6; ++i) {
            QString candidate = dir.absoluteFilePath("assets");
            // Use animations.json as the sentinel (always present at assets root);
            // map.png was moved to packs-disabled/ and no longer exists here.
            if (QFile::exists(candidate + "/animations.json")) {
                return candidate;
            }
            if (!dir.cdUp()) break;
        }
#if defined(SEELIE_SOURCE_DIR) && !defined(NDEBUG)
        // Debug-build fallback: use the compile-time source directory so IDE
        // builds whose CWD is far from the source tree still find assets.
        // INTENTIONALLY disabled in Release: an installed build that fails the
        // application-dir search must NOT silently leak the developer's source
        // tree into the user's running app (audit fix — that's what loaded all
        // 117 source packs from F:/Oai/assets in production).
        QString sourceAssets = QStringLiteral(SEELIE_SOURCE_DIR) + "/assets";
        if (QFile::exists(sourceAssets + "/animations.json")) {
            return sourceAssets;
        }
#endif
        return QString();
    };

    const QString assetsDir = findAssetsDir();
    if (!assetsDir.isEmpty()) {
        qDebug() << "Assets loaded from:" << assetsDir;
    } else {
        qWarning() << "Assets not found. Searched from:" << QApplication::applicationDirPath();
    }

    if (QImageReader::supportedImageFormats().contains("webp")) {
        qDebug() << "WEBP image format: supported (required for Codex pets)";
    } else {
        qWarning() << "WEBP image format: NOT supported. Codex pets will fail to load. "
                      "On macOS: brew install qtimageformats, then run macdeployqt to bundle the plugin.";
    }

    // --- Font loading (HarmonyOS Sans SC) ------------------------------------
    // Load from filesystem (assets/fonts/) for cross-platform consistency.
    // Must run before MainWindow creation so QApplication::setFont() propagates.
    {
        const QStringList fontFiles = {
            (!assetsDir.isEmpty()) ? assetsDir + "/fonts/HarmonyOS_Sans_SC_Bold.ttf" : QString(),
            (!assetsDir.isEmpty()) ? assetsDir + "/fonts/HarmonyOS_Sans_SC_Regular.ttf" : QString(),
        };
        QString registeredFamily;
        for (const QString &path : fontFiles) {
            if (path.isEmpty() || !QFile::exists(path)) continue;
            int id = QFontDatabase::addApplicationFont(path);
            if (id >= 0) {
                QStringList families = QFontDatabase::applicationFontFamilies(id);
                if (!families.isEmpty() && registeredFamily.isEmpty()) {
                    registeredFamily = families.first();
                    qDebug() << "Font loaded:" << path << "→" << registeredFamily;
                }
            } else {
                qWarning() << "Failed to load font:" << path;
            }
        }
        if (!registeredFamily.isEmpty()) {
            QFont appFont(registeredFamily, 9);
            appFont.setStyleStrategy(QFont::PreferAntialias);
            QApplication::setFont(appFont);
            qDebug() << "Global app font set to:" << registeredFamily;
        } else {
            qWarning() << "HarmonyOS Sans SC not loaded — using system default font";
        }
    }

    // --- Global stylesheet (Persona 5 design system) -------------------------
    // Applied to qApp so QMenu / QToolTip / QPushButton / QLineEdit /
    // QCheckBox / QComboBox inherit cartoon styling everywhere — most
    // importantly the tray menu and pet context menu, which previously
    // looked Windows-native. Widgets with their own setStyleSheet still
    // override this (Qt QSS specificity rule).
    {
        QFile qss(QStringLiteral(":/styles/styles/seelie.qss"));
        if (qss.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qApp->setStyleSheet(QString::fromUtf8(qss.readAll()));
            qDebug() << "Global stylesheet loaded:" << qss.fileName()
                     << "(" << qss.size() << "bytes )";
        } else {
            qWarning() << "Could not load global stylesheet" << qss.fileName()
                       << "—" << qss.errorString();
        }
    }

    // --- Memory manager ------------------------------------------------------
    MemoryManager memory(configDir() + "/memory.db");
    if (!memory.isValid()) {
        qWarning() << "MemoryManager: database could not be opened — pet memory features disabled";
    }

    // --- Persona engine ------------------------------------------------------
    PersonaEngine personaEngine(&memory, &config);

    // --- Main window ---------------------------------------------------------
    MainWindow w(&config, &translator);
    w.setMemoryManager(&memory);

    // --- Sprite pack manager -------------------------------------------------
    CharacterPackManager packManager;
    const QString builtInPacksDir = assetsDir + "/packs";
    const QString userPacksDir = dataDir() + "/packs";
    packManager.initialize(builtInPacksDir, userPacksDir, config.activePackId());
    w.setCharacterPackManager(&packManager);

    // Persist the user's pack choice so the next launch restores it
    QObject::connect(&packManager, &CharacterPackManager::activePackChanged,
                     &config, [&config, &packManager](CharacterPack *) {
        config.setActivePackId(packManager.activePackId());
    });

    // --- Position window ----------------------------------------------------
    // Clamp to screen bounds to handle: resolution changes, disconnected monitors,
    // or window size changes from sprite pack loading.
    const QSize winSize = w.size();
    const int posMargin = 8;

    // Helper: clamp a position so the entire window fits inside a screen rect
    auto clampedPos = [&](const QPoint &pos, const QRect &geo) -> QPoint {
        int x = qBound(geo.left() + posMargin, pos.x(), geo.right() - winSize.width() - posMargin);
        int y = qBound(geo.top() + posMargin, pos.y(), geo.bottom() - winSize.height() - posMargin);
        return QPoint(x, y);
    };

    // Helper: find the screen whose geometry has the largest overlap with a rect
    auto bestScreenFor = [&](const QRect &rect) -> const QScreen * {
        const QScreen *best = nullptr;
        int bestArea = -1;
        for (const QScreen *screen : QApplication::screens()) {
            QRect inter = screen->availableGeometry().intersected(rect);
            int area = inter.width() * inter.height();
            if (area > bestArea) {
                bestArea = area;
                best = screen;
            }
        }
        return best ? best : QApplication::primaryScreen();
    };

    QPoint savedPos = config.windowPosition();
    QPoint targetPos;
    if (!savedPos.isNull()) {
        // Restore saved position, clamped to the best-matching screen
        const QScreen *screen = bestScreenFor(QRect(savedPos, winSize));
        targetPos = clampedPos(savedPos, screen->availableGeometry());
    } else {
        // Default: bottom-right corner of primary screen.
        // primaryScreen() can return null on a headless / unplug-mid-session
        // edge case; fall back to (0, 0) rather than dereferencing.
        const QScreen *primary = QApplication::primaryScreen();
        const QRect screen = primary ? primary->availableGeometry() : QRect(0, 0, 1280, 800);
        targetPos = QPoint(screen.right() - winSize.width() - posMargin,
                           screen.bottom() - winSize.height() - posMargin);
    }
    w.move(targetPos);

    // --- Load animation assets (legacy fallback) -----------------------------
    // If no sprite pack is loaded, fall back to hardcoded assets
    if (!packManager.activePack()) {
        QString spritePath, animJsonPath;
        if (!assetsDir.isEmpty()) {
            spritePath = assetsDir + "/map.png";
            animJsonPath = assetsDir + "/animations.json";
        }

        if (!spritePath.isEmpty()) {
            w.animationEngine()->loadAssets(spritePath, animJsonPath);
        }
    }

    // --- Tips engine ---------------------------------------------------------
    TipsEngine tipsEngine;
    tipsEngine.setTipWidget(w.tipWidget());
    tipsEngine.setMemoryManager(&memory);

    // TipsEngine is engine-agnostic — fan out across active engines in the
    // Live2D > Lottie > Sprite priority chain. Audit H1.
    QObject::connect(&tipsEngine, &TipsEngine::animationRequested,
                     &w, [&w](const QString &anim) {
        w.dispatchAnimation(anim);
    });

    // --- Event router --------------------------------------------------------
    EventRouter eventRouter;
    eventRouter.setTipWidget(w.tipWidget());
    eventRouter.setTipsEngine(&tipsEngine);
    eventRouter.setMemoryManager(&memory);
    // MainWindow uses the FSM (via setStateMachine) for mouse-driven synthetic
    // events; EventRouter no longer owns animation dispatch.
    w.setEventRouter(&eventRouter);
    w.setPersonaEngine(&personaEngine);
    if (w.settingsPanel()) w.settingsPanel()->setPersonaEngine(&personaEngine);

    // --- System context engine (ContextSenses, Spec 2) -----------------------
    // Emits synthetic context.* events through the same EventRouter pipeline.
    // start/stop follows the contextSensesEnabled toggle live, mirroring the
    // Gaming Mode wiring pattern in MainWindow.
    SystemContextEngine contextEngine(&eventRouter, &config);
    contextEngine.setFullscreenWatcher(w.fullscreenWatcher());
    if (config.contextSensesEnabled())
        contextEngine.start();
    QObject::connect(&config, &ConfigManager::contextSensesEnabledChanged,
                     &contextEngine, [&contextEngine](bool enabled) {
        if (enabled) contextEngine.start(); else contextEngine.stop();
    });

    // Tip-bubble user toggle: apply current value, then track changes live.
    w.tipWidget()->setSuppressedByUser(!config.tipBubblesEnabled());
    QObject::connect(&config, &ConfigManager::tipBubblesEnabledChanged,
                     w.tipWidget(),
                     [bubble = w.tipWidget()](bool enabled) {
                         bubble->setSuppressedByUser(!enabled);
                     });

    // --- IPC server ----------------------------------------------------------
    IPCServer ipcServer;

    // IPC events → EventRouter
    QObject::connect(&ipcServer, &IPCServer::eventReceived,
                     &eventRouter, &EventRouter::routeEvent);

    // IPC events → ECGWidget for heart-rate / alarm reactions
    QObject::connect(&ipcServer, &IPCServer::eventReceived,
                     w.ecgWidget(), &ECGWidget::onEvent);

    // IPC tips → TipWidget directly
    QObject::connect(&ipcServer, &IPCServer::tipReceived,
                     &w, [&w](const QJsonObject &tip) {
        const QString title = tip.value("title").toString("Tip");
        const QString body = tip.value("body").toString();
        const QString anim = tip.value("animation").toString();

        w.tipWidget()->showBubble(title, body, TipWidget::TipBubble);
        w.dispatchAnimation(anim);
    });

    ipcServer.start(config.ipcEndpoint());
    w.setIPCServer(&ipcServer);

    // Milestone achievements → tip bubble
    QObject::connect(&memory, &MemoryManager::milestoneReached,
                     &w, [&w](const QString &title, const QString &body) {
        w.tipWidget()->showBubble(title, body, TipWidget::TipBubble);
    });

    // Bond level ups surface as tip bubbles
    QObject::connect(&memory, &MemoryManager::bondLevelChanged,
                     &w, [&w](int level) {
        w.tipWidget()->showBubble(QObject::tr("Bond level up!"),
            QObject::tr("Seelie feels closer to you (Lv %1).").arg(level),
            TipWidget::TipBubble);
    });

    // Embedding service (gated: persona AI + memory sharing).
    // Resolves the active LLM profile exactly how
    // PersonaEngine::refreshActiveProfile does (config.personaProfile() →
    // match by name in config.llmProfiles()). Only stands up the worker for
    // OpenAI-Chat endpoints with a full config, since embedTextSync returns
    // empty for Anthropic / OpenAI Responses (the embeddings endpoint is
    // OpenAI-chat only). Episode enqueue jobs are wired up just below.
    EmbeddingService *embedSvc = nullptr;
    if (config.personaEnabled() && config.shareMemoryWithAi()) {
        LLMProfile prof;
        const QString profName = config.personaProfile();
        if (!profName.isEmpty()) {
            for (const auto &p : config.llmProfiles()) {
                if (p.name == profName) { prof = p; break; }
            }
        }
        if (prof.protocol == LLMProfile::Protocol::OpenAIChat
            && !prof.baseUrl.isEmpty() && !prof.apiKey.isEmpty()
            && !prof.model.isEmpty()) {
            embedSvc = new EmbeddingService(&memory,
                [prof](const QString &text, QString *err) {
                    return LLMProvider::embedTextSync(prof, text, err);
                }, &a);
        }
    }
    // Milestones also become episodes — placed AFTER embedSvc construction so
    // the capture can enqueue an embedding job for the new episode row.
    // (Was previously before embedSvc was declared; moved here in Task 10.)
    QObject::connect(&memory, &MemoryManager::milestoneReached,
                     &memory, [&memory, embedSvc](const QString &title, const QString &) {
        const qint64 id = memory.recordEpisode(QStringLiteral("milestone"), title);
        if (embedSvc && id >= 0) embedSvc->enqueueEpisode(id, title);
    });

    // Wire the embedding service into MainWindow so session.end episodes can
    // enqueue too. Setter stores the pointer as-is (nullptr when ungated).
    w.setEmbeddingService(embedSvc);

    // Restart IPC server when port changes
    QObject::connect(&config, &ConfigManager::ipcEndpointChanged,
                     &ipcServer, [&ipcServer](const QString &endpoint) {
        qDebug() << "IPC: Restarting server on new endpoint:" << endpoint;
        ipcServer.restart(endpoint);
    });

    // --- System tray ---------------------------------------------------------
    SystemTray tray(&w, &config);
    tray.setTipWidget(w.tipWidget());  // route update-check results through the bubble
    tray.show();
    w.setSystemTray(&tray);

    // --- Update checker -------------------------------------------------------
    UpdateChecker updateChecker(&config);
    tray.setUpdateChecker(&updateChecker);

    // Check for updates on startup (delayed to not block UI). userTriggered=false
    // keeps the result silent on the happy path — only an `updateAvailable`
    // surfaces a notification.
    QTimer::singleShot(5000, &updateChecker, [&updateChecker]() {
        updateChecker.checkForUpdates(false);
    });

    // --- Language switching --------------------------------------------------
    QObject::connect(&config, &ConfigManager::languageChanged,
                     &w, &MainWindow::onLanguageChanged);
    QObject::connect(&config, &ConfigManager::languageChanged,
                     &eventRouter, &EventRouter::retranslateUi);
    QObject::connect(&config, &ConfigManager::languageChanged,
                     &tipsEngine, &TipsEngine::retranslateUi);

    // --- Global shortcut ------------------------------------------------------
    GlobalShortcutManager shortcutManager;
    shortcutManager.setShortcut(config.globalShortcut());
    shortcutManager.setEnabled(config.globalShortcutEnabled());
    QObject::connect(&shortcutManager, &GlobalShortcutManager::activated,
                     &w, [&w]() { w.setVisible(!w.isVisible()); });
    QObject::connect(&shortcutManager, &GlobalShortcutManager::shortcutChanged,
                     &config, [&config](const QString &s) { config.setGlobalShortcut(s); });
    QObject::connect(&config, &ConfigManager::globalShortcutChanged,
                     &shortcutManager, [&shortcutManager](const QString &s) { shortcutManager.setShortcut(s); });
    w.setGlobalShortcutManager(&shortcutManager);

    // --- Pet state machine ----------------------------------------------------
    PetStateMachine stateMachine;

    // Wire FSM into MainWindow so synthetic events (user.click etc.) reach it.
    w.setStateMachine(&stateMachine);

    // Gateway events flow EventRouter → FSM. EventRouter still owns tip-text.
    QObject::connect(&eventRouter, &EventRouter::eventProcessed,
                     &stateMachine,
                     [&stateMachine](const QString &name, const QJsonObject &payload) {
                         stateMachine.onCanonicalEvent(name, payload);
                     });

    // Window-position deltas drive the Walking overlay.
    QObject::connect(&w, &MainWindow::positionChanged,
                     &stateMachine,
                     [&stateMachine, lastPos = std::optional<QPoint>{}](const QPoint &p) mutable {
                         if (lastPos.has_value()) {
                             stateMachine.onPositionChanged(*lastPos, p, /*isUserDrag=*/false);
                         }
                         lastPos = p;
                     });

    // Active pack changes rebuild per-state chains.
    QObject::connect(&packManager, &CharacterPackManager::activePackChanged,
                     &stateMachine,
                     [&stateMachine, &packManager]() {
                         stateMachine.onActivePackChanged(packManager.activePack());
                     });

    // FSM-emitted chain → engine fan-out.
    QObject::connect(&stateMachine, &PetStateMachine::animationRequested,
                     &w,
                     [&w](const QStringList &chain, int priority) {
        const auto p = (priority == PetStateMachine::HighPriority)
                           ? AnimationEngine::HighPriority
                           : AnimationEngine::NormalPriority;
        w.dispatchAnimationChain(chain, p);
    });

    if (config.displayMode() == ConfigManager::DisplayMode::Character) {
        w.show();
        w.raise();
#ifdef Q_OS_MAC
        w.activateWindow();
#endif
    }
    // In ECG mode the MainWindow stays hidden — MainWindow's constructor
    // already started the ECG widget via onDisplayModeChanged().

    qDebug() << "Seelie started — window at" << w.pos() << "size" << w.size();

    // --- Statistics persistence -----------------------------------------------
    const QString statsDir = configDir();
    StatisticsManager::createInstance(statsDir, &a);

    StatisticsManager::instance()->registerComponent("persona",
        [&]() { personaEngine.loadStats(statsDir); },
        [&]() { personaEngine.saveStats(statsDir); });
    StatisticsManager::instance()->registerComponent("events",
        [&]() { eventRouter.loadStats(statsDir); },
        [&]() { eventRouter.saveStats(statsDir); });
    StatisticsManager::instance()->registerComponent("ipc",
        [&]() { ipcServer.loadStats(statsDir); },
        [&]() { ipcServer.saveStats(statsDir); });
#ifdef SEELIE_TTS_ENABLED
    StatisticsManager::instance()->registerComponent("tts",
        [&]() { if (w.ttsEngine()) w.ttsEngine()->loadStats(statsDir); },
        [&]() { if (w.ttsEngine()) w.ttsEngine()->saveStats(statsDir); });
#endif

    StatisticsManager::instance()->loadAll();
    StatisticsManager::instance()->startAutoSave(60000);

    QObject::connect(&a, &QCoreApplication::aboutToQuit, []() {
        StatisticsManager::instance()->saveAll();
    });

    const int rc = a.exec();
    // Rider A (Task 9 review I-1): tear down the embedding worker BEFORE the
    // MemoryManager stack object goes out of scope. EmbeddingService::~ stops
    // its worker thread (quit + wait) and the worker's pending results were
    // already delivered back to the main thread; deleting here avoids a
    // use-after-free when `memory` is destroyed on function return.
    delete embedSvc;
    embedSvc = nullptr;
    return rc;
}
