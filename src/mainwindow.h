#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QWidget>
#include <QPoint>
#include <QPointer>
#include <QTimer>
#include <QMenu>

#include "AnimationEngine.h"
#include "CharacterPack.h"
#include "ConfigManager.h"
#include "StrokeDetector.h"

class MemoryManager;
class SpriteAnimationEngine;
#if SEELIE_LOTTIE_ENABLED
class LottieAnimationEngine;
#endif
class Model3DEngine;
class Live2DAnimationEngine;  // forward-declared even when SEELIE_LIVE2D_SUPPORT
                              // is off so the m_live2dEngine pointer-member
                              // stays declared and accessor short-circuits
                              // resolve to nullptr without #ifdef noise. M17.
#ifdef SEELIE_TTS_ENABLED
class TTSEngine;
#endif
class TipWidget;
class SettingsPanelWidget;
class ECGWidget;
class CharacterPackManager;
class EventRouter;
class FullscreenWatcher;
class IPCServer;
class PetStateMachine;
class IdleBehaviorEngine;
class DesktopMotionController;
class GlobalShortcutManager;
class PersonaEngine;
class EmbeddingService;

class QTranslator;
class SystemTray;
class StatisticsDialog;
class RecallDialog;

class MainWindow : public QWidget
{
    Q_OBJECT

public:
    explicit MainWindow(ConfigManager *config, QTranslator *translator, QWidget *parent = nullptr);
    ~MainWindow() override;

    SpriteAnimationEngine *animationEngine() const { return m_engine; }
#if SEELIE_LOTTIE_ENABLED
    LottieAnimationEngine *lottieEngine() const { return m_lottieEngine; }
#endif
    /// Returns nullptr when the build is configured without SEELIE_LIVE2D_SUPPORT.
    /// Callers should null-check (most already do via `if (e && e->...)`).
    Live2DAnimationEngine *live2dEngine() const { return m_live2dEngine; }
#ifdef SEELIE_TTS_ENABLED
    TTSEngine *ttsEngine() const { return m_ttsEngine; }
#endif
    TipWidget *tipWidget() const { return m_tipWidget; }
    ECGWidget *ecgWidget() const { return m_ecgWidget; }
    SettingsPanelWidget *settingsPanel() const { return m_settingsPanel; }

    void setSystemTray(SystemTray *tray);
    void setCharacterPackManager(CharacterPackManager *manager);
    void setGlobalShortcutManager(GlobalShortcutManager *manager);

    /** Owned FullscreenWatcher (Gaming Mode) — shared with SystemContextEngine
        for the context.gaming welcome-back event. */
    FullscreenWatcher *fullscreenWatcher() const { return m_fullscreenWatcher; }

    void setEventRouter(EventRouter *router);
    void setIPCServer(IPCServer *ipc) { m_ipcServer = ipc; }
    void setStateMachine(PetStateMachine *sm);
    void setIdleBehaviorEngine(IdleBehaviorEngine *engine);
    void setMemoryManager(MemoryManager *memory);
    void setPersonaEngine(PersonaEngine *engine);
    void setEmbeddingService(EmbeddingService *s);

    /// Ambient peek: hover tooltip mirroring the tray mood line.
    void setMoodPeekText(const QString &text);

    /// Fan out a named animation through Live2D > Lottie > Sprite engines.
    void dispatchAnimation(const QString &anim,
                           AnimationEngine::Priority priority = AnimationEngine::NormalPriority);

    /// Fan out an animation chain. Live2D understands the full chain;
    /// Lottie and Sprite fall back to chain.first().
    void dispatchAnimationChain(const QStringList &chain,
                                AnimationEngine::Priority priority);

signals:
    void positionChanged(const QPoint &pos);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
    /// Re-apply DWM frameless attributes + WA_NoSystemBackground + composition
    /// refresh for this window AND any sibling top-level widgets (tip
    /// bubble) whose attributes can degrade independently. Called from a
    /// 30s timer and from WM_DISPLAYCHANGE. The audit (H2) wanted this
    /// extracted into a DwmRefreshManager class; a single private method
    /// gets the same readability win without the lifetime/ownership noise
    /// of yet another QObject child.
    void refreshAllDwmAttributes();
#endif

public slots:
    void retranslateUi();
    void onLanguageChanged(const QString &lang);
    void showContextMenu(const QPoint &globalPos);

private slots:
    void toggleVisibility();
    void openSettings();
    void onShowStatistics();
    void showRecallDialog();
    void onExportConfig();
    void onImportConfig();
    void onActivePackChanged();
    void onDisplayModeChanged(ConfigManager::DisplayMode mode);
    void onFullscreenStarted();
    void onFullscreenStopped();
    void onTipUpgraded(quint64 requestId, const QString &newText);
    void onTipUpgradeFailed(quint64 requestId);
    void onEventForMemory(const QString &eventName);

private:
    void setupWindowFlags();
    void reloadTranslator(const QString &lang);
    void showRandomGreeting();
    /// Random canned line for a touch gesture ("pet"/"toss") via TipsCatalog.
    void showTouchBubble(const QString &gesture);
    /// Spec 4: session-end bubble — template body now; LLM summary upgrades
    /// it via the activeBubble machinery when persona is configured.
    void showSessionSummaryBubble(const QString &statsLine);
    void tryRecordPoke();
    /// Spec 3: one pet pulse from the StrokeDetector. Task 5 fires the FSM
    /// overlay; Task 6 adds memory + canned lines.
    void onPetStroke();
    /// Spec 3: release velocity exceeded TOSS_SPEED_PX_PER_SEC at drag end.
    void onTossDetected();
    // Wire the EventRouter::eventProcessed → onEventForMemory connection.
    // Idempotent: connect exactly once via m_memoryEventWired (lambdas can't
    // use Qt::UniqueConnection reliably). Called from both setEventRouter()
    // and setMemoryManager() so memory bookkeeping works regardless of
    // whether a PersonaEngine is set.
    void wireMemoryEventConnect();
    void updateDesktopMotion();

    QRect petRect() const;
    bool isInPetRect(const QPoint &pos) const;

    SpriteAnimationEngine *m_engine;
#if SEELIE_LOTTIE_ENABLED
    LottieAnimationEngine *m_lottieEngine;
#endif
    Model3DEngine *m_model3dEngine = nullptr;
    // Engine that owns the ACTIVE pack. Dispatch routes here first so a
    // stopped-but-still-loaded engine from a previously loaded pack can't
    // swallow events after a pack switch (it would still report
    // hasAnimations()==true).
#if SEELIE_LOTTIE_ENABLED
    CharacterPack::EngineType m_activeEngineType = CharacterPack::EngineType::Lottie;
#else
    CharacterPack::EngineType m_activeEngineType = CharacterPack::EngineType::SpriteSheet;
#endif
    Live2DAnimationEngine *m_live2dEngine = nullptr;  // always null when
                                                      // SEELIE_LIVE2D_SUPPORT off
#ifdef SEELIE_TTS_ENABLED
    TTSEngine *m_ttsEngine = nullptr;
#endif
    ConfigManager *m_config;
    TipWidget *m_tipWidget;
    SettingsPanelWidget *m_settingsPanel;
    ECGWidget *m_ecgWidget = nullptr;
    QTranslator *m_translator;
    SystemTray *m_systemTray = nullptr;
    CharacterPackManager *m_packManager = nullptr;
    EventRouter *m_eventRouter = nullptr;
    IPCServer *m_ipcServer = nullptr;
    PetStateMachine *m_stateMachine = nullptr;
    IdleBehaviorEngine *m_idleEngine = nullptr;
    DesktopMotionController *m_desktopMotion = nullptr;
    bool m_autonomousMove = false;
    GlobalShortcutManager *m_shortcutManager = nullptr;
    MemoryManager *m_memory = nullptr;
    PersonaEngine *m_personaEngine = nullptr;
    EmbeddingService *m_embeddingService = nullptr;
    quint64 m_activeBubbleRequestId = 0;
    // Catalog body for the current bubble — used as TTS fallback if the
    // persona LLM call for this requestId fails. Captured at event-route
    // time so we don't have to re-derive it from the (possibly already
    // overwritten) bubble widget.
    QString m_activeBubbleFallbackBody;
    // Spec 4 (quality review): true between session.end's summary bubble
    // and the next session.start. Gates the (a) persona connect so it does
    // NOT fire a second OnDemand resolve for session.end that would
    // overwrite the summary's requestId + text. Reset on session.start.
    bool m_summaryShownThisSessionEnd = false;
    QPointer<StatisticsDialog> m_statsDialog;
    QPointer<RecallDialog> m_recallDialog;   // raise-or-create singleton (recall UI)

    // Gaming Mode
    FullscreenWatcher *m_fullscreenWatcher = nullptr;
    bool m_hiddenByGamingMode = false;

    // Drag state
    QPoint m_dragStartPos;
    QPoint m_dragWindowPos;
    bool m_dragging = false;
    static constexpr int DRAG_THRESHOLD = 5;

    // Spec 3 (TouchReactions): stroke/drag disambiguation. m_strokeSession is
    // true while a petRect press is being classified (toggle on only).
    StrokeDetector m_strokeDetector;
    bool m_strokeSession = false;

    // Active pack's render size (drives petRect). Defaults to Clippy's
    // historical 124×93; onActivePackChanged() updates it to the pack's
    // frameWidth/frameHeight so Live2D/Lottie packs aren't squished into
    // a Clippy-sized rect at the bottom of a much taller window.
    QSize m_petSize = QSize(124, 93);

    // Visibility
    bool m_visible = true;

    // Guard against infinite recursion when auto-skipping Live2D packs
    // (onActivePackChanged → switchPack → activePackChanged → onActivePackChanged).
    bool m_skipLive2dFallback = false;

    // Monotonic counter for pack load attempts. Each onActivePackChanged()
    // bumps it; the 500 ms post-load crop lambda compares its captured value
    // against the current one and bails if it's been superseded.
    int m_packLoadId = 0;

    // Pet Memory 2.0 (Task 9): session bookkeeping for episode/bond writes.
    qint64 m_sessionStartMs = 0;
    int    m_sessionEventCount = 0;
    qint64 m_lastPokeWriteMs = 0;   // shared throttle across click + dblclick
    qint64 m_lastPetWriteMs = 0;    // pet affection throttle (2s, spec §3)
    qint64 m_lastHoverWriteMs = 0;  // hover affection throttle (60s, spec §3)
    // Connect-once guard for the EventRouter → onEventForMemory wiring.
    // See wireMemoryEventConnect(); lambdas can't rely on Qt::UniqueConnection.
    bool m_memoryEventWired = false;

#ifdef Q_OS_WIN
    // Windows DWM can lose window attributes after long-running sessions
    // (display sleep/wake, DWM restart). Refresh periodically.
    QTimer *m_dwmRefreshTimer = nullptr;
#endif
};

#endif // MAINWINDOW_H
