#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <QHash>
#include <QObject>
#include <QPoint>
#include <QString>
#include <QSettings>
#include <QTimer>
#include <QVector>
#include "llm/LLMProfile.h"

class ConfigManager : public QObject
{
    Q_OBJECT

public:
    enum class DisplayMode { Character, Ecg };
    enum class SayingFrequency { Never = 0, Rarely, Sometimes, Often };
    Q_ENUM(SayingFrequency)

    explicit ConfigManager(QObject *parent = nullptr);
    ~ConfigManager() override;

    void load();
    /**
     * Schedule a write to disk. Called from every setter — debounced via
     * a 500 ms timer so a window drag (positionChanged on every pixel)
     * causes one disk write instead of hundreds. Use `flush()` to force
     * an immediate sync (e.g. on shutdown).
     */
    void save();
    /** Force any pending debounced save() to flush immediately. */
    void flush();

    QPoint windowPosition() const { return m_windowPosition; }
    void setWindowPosition(const QPoint &pos);

    QString language() const { return m_language; }
    void setLanguage(const QString &lang);

    bool autoStart() const { return m_autoStart; }
    void setAutoStart(bool enabled);

    /** Pack ID of the last-selected character pack, or empty on first run. */
    QString activePackId() const { return m_activePackId; }
    void setActivePackId(const QString &packId);

    /** Current display mode: Character (animated pet) or Ecg (ICU monitor widget). */
    DisplayMode displayMode() const { return m_displayMode; }
    void setDisplayMode(DisplayMode mode);

    /**
     * Returns the TCP endpoint for IPC.
     * Format: "host:port", e.g. "127.0.0.1:52847"
     */
    QString ipcEndpoint() const { return m_ipcEndpoint; }

    /**
     * Extract just the port number from the current endpoint.
     */
    quint16 ipcPort() const;

    /**
     * Set a new IPC port (keeps host as 127.0.0.1).
     * Saves config and emits ipcEndpointChanged if the value changed.
     */
    void setIpcPort(quint16 port);

    /** Global shortcut key sequence (e.g. "Ctrl+Shift+O"). */
    QString globalShortcut() const { return m_globalShortcut; }
    void setGlobalShortcut(const QString &shortcut);

    /** Whether the global shortcut is enabled. */
    bool globalShortcutEnabled() const { return m_globalShortcutEnabled; }
    void setGlobalShortcutEnabled(bool enabled);

    /** Whether Gaming Mode (auto-hide when a fullscreen app is active) is enabled. */
    bool gamingModeEnabled() const { return m_gamingModeEnabled; }
    void setGamingModeEnabled(bool enabled);

    /** Whether ContextSenses synthetic context events are emitted. Default true. */
    bool contextSensesEnabled() const { return m_contextSensesEnabled; }
    void setContextSensesEnabled(bool enabled);

    /** Whether TouchReactions mouse gestures (pet/grab/toss/hover) are active. Default true. */
    bool touchReactionsEnabled() const { return m_touchReactionsEnabled; }
    void setTouchReactionsEnabled(bool enabled);

    /** Whether tip bubbles surface above the pet. Default true. */
    bool tipBubblesEnabled() const { return m_tipBubblesEnabled; }
    void setTipBubblesEnabled(bool enabled);

    /** Whether TTS (Text-to-Speech) is enabled. Default false. */
    bool ttsEnabled() const { return m_ttsEnabled; }
    void setTtsEnabled(bool enabled);

    /** Stable ID of the active provider ("stepfun", "minimax", "azure", "openai"). */
    QString ttsActiveProvider() const { return m_ttsActiveProvider; }
    void setTtsActiveProvider(const QString &stableId);

    /** Read/write a single field for a given provider stable ID. */
    QString ttsProviderField(const QString &providerId, const QString &field) const;
    void setTtsProviderField(const QString &providerId,
                             const QString &field,
                             const QString &value);

    /** Read all fields for a given provider stable ID. */
    QHash<QString, QString> ttsProviderConfig(const QString &providerId) const;

    // --- LLM (AI Persona Layer) -------------------------------------------------

    QVector<LLMProfile> llmProfiles() const { return m_llmProfiles; }
    void setLLMProfiles(const QVector<LLMProfile> &profiles);

    QString personaProfile() const { return m_personaProfile; }
    void setPersonaProfile(const QString &name);

    bool personaEnabled() const { return m_personaEnabled; }
    void setPersonaEnabled(bool enabled);

    bool shareMemoryWithAi() const { return m_shareMemoryWithAi; }
    void setShareMemoryWithAi(bool enabled);

    /** Idle-sayings cadence. Default Sometimes. */
    SayingFrequency sayingFrequency() const { return m_sayingFrequency; }
    void setSayingFrequency(SayingFrequency freq);

    /** Whether idle sayings may occasionally be LLM-generated. Default false (cost opt-in). */
    bool llmIdleQuipsEnabled() const { return m_llmIdleQuipsEnabled; }
    void setLLMIdleQuipsEnabled(bool enabled);

    /**
     * Returns the UDP endpoint for the version-check / update server.
     * Format: "host:port". Stored under the `updateServerEndpoint` key in
     * QSettings; falls back to defaultUpdateEndpoint() (compiled in from
     * the SEELIE_DEFAULT_UPDATE_ENDPOINT CMake cache variable) when unset, so
     * production stays operable without forcing every user to write a
     * config file.
     */
    QString updateServerEndpoint() const { return m_updateServerEndpoint; }
    void setUpdateServerEndpoint(const QString &endpoint);

    /** Default IPC endpoint (used when config has no override). */
    static QString defaultEndpoint();

    /** Default update-server endpoint (compiled in from SEELIE_DEFAULT_UPDATE_ENDPOINT). */
    static QString defaultUpdateEndpoint();

    /** Directory containing the config INI file (e.g. ~/.config/Seelie). */
    QString configDir() const;

signals:
    void languageChanged(const QString &lang);
    void ipcEndpointChanged(const QString &endpoint);
    void updateServerEndpointChanged(const QString &endpoint);
    void displayModeChanged(DisplayMode mode);
    void globalShortcutChanged(const QString &shortcut);
    void gamingModeEnabledChanged(bool enabled);
    void contextSensesEnabledChanged(bool enabled);
    void touchReactionsEnabledChanged(bool enabled);
    void tipBubblesEnabledChanged(bool enabled);
    void ttsEnabledChanged(bool enabled);
    void ttsActiveProviderChanged(const QString &stableId);
    void ttsProviderFieldChanged(const QString &providerId,
                                 const QString &field,
                                 const QString &value);
    void ttsCacheInvalidated();
    void llmProfilesChanged();
    void personaProfileChanged(const QString &name);
    void personaEnabledChanged(bool enabled);
    void shareMemoryWithAiChanged(bool enabled);
    void sayingFrequencyChanged(SayingFrequency freq);
    void llmIdleQuipsEnabledChanged(bool enabled);

private:
    QSettings m_settings;
    QTimer m_saveTimer;  // debounces save() into flush()

    QPoint m_windowPosition;
    QString m_language = "en";
    bool m_autoStart = false;
    QString m_ipcEndpoint;
    QString m_updateServerEndpoint;
    QString m_activePackId;
    DisplayMode m_displayMode = DisplayMode::Character;
    QString m_globalShortcut = QStringLiteral("Ctrl+Shift+O");
    bool m_globalShortcutEnabled = true;
    bool m_gamingModeEnabled = false;
    bool m_contextSensesEnabled = true;
    bool m_touchReactionsEnabled = true;
    bool m_tipBubblesEnabled = true;
    bool m_ttsEnabled = false;
    QString m_ttsActiveProvider = QStringLiteral("stepfun");
    QHash<QString, QHash<QString, QString>> m_ttsProviders;  // providerId -> field -> value

    QVector<LLMProfile> m_llmProfiles;
    QString m_personaProfile;
    bool m_personaEnabled = false;
    bool m_shareMemoryWithAi = false;

    SayingFrequency m_sayingFrequency = SayingFrequency::Sometimes;
    bool m_llmIdleQuipsEnabled = false;

};

#endif // CONFIGMANAGER_H
