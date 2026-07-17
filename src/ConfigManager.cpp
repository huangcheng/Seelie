#include "ConfigManager.h"

#include "AutoStartManager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>

QString ConfigManager::defaultEndpoint()
{
    return QStringLiteral("127.0.0.1:52847");
}

QString ConfigManager::defaultUpdateEndpoint()
{
    // Default host:port comes from SEELIE_DEFAULT_UPDATE_ENDPOINT, baked in at
    // CMake-configure time (see CMakeLists.txt). Override at runtime via the
    // `updateServerEndpoint` key in QSettings to point at a local server
    // during development without rebuilding.
    return QStringLiteral(SEELIE_DEFAULT_UPDATE_ENDPOINT);
}

ConfigManager::ConfigManager(QObject *parent)
    : QObject(parent)
    , m_settings(QSettings::IniFormat, QSettings::UserScope,
                 QCoreApplication::organizationName().isEmpty()
                     ? QStringLiteral("Seelie")
                     : QCoreApplication::organizationName(),
                 QCoreApplication::applicationName().isEmpty()
                     ? QStringLiteral("Seelie")
                     : QCoreApplication::applicationName())
    , m_ipcEndpoint(defaultEndpoint())
    , m_updateServerEndpoint(defaultUpdateEndpoint())
{
    // Save debounce: a window drag fires positionChanged on every pixel,
    // which previously meant a full QSettings::sync() per frame. 500 ms is
    // imperceptible to the user but collapses ~60 writes/sec into one. The
    // destructor calls flush() so a pending save lands before exit. M9.
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(500);
    connect(&m_saveTimer, &QTimer::timeout, this, &ConfigManager::flush);
    // Layer 1: in-memory defaults (above).
    // Layer 2: portable Seelie.ini next to the exe — shipped by the installer
    //          so a built distribution can pre-set updateServerEndpoint /
    //          ipcEndpoint / language without forcing every user to edit
    //          their roaming config. Read-only here; save() never writes
    //          to it (user changes always land in the user-scope file).
    // Layer 3: user-scope ~/AppData/Roaming/Seelie/Seelie.ini — applied by load()
    //          after construction, wins over portable values.
    const QString portablePath = QCoreApplication::applicationDirPath() + "/Seelie.ini";
    if (QFile::exists(portablePath)) {
        QSettings portable(portablePath, QSettings::IniFormat);
        if (const auto v = portable.value("updateServerEndpoint").toString(); !v.isEmpty()) {
            m_updateServerEndpoint = v;
        }
        if (const auto v = portable.value("ipcEndpoint").toString(); !v.isEmpty()) {
            m_ipcEndpoint = v;
        }
        if (const auto v = portable.value("language").toString(); !v.isEmpty()) {
            m_language = v;
        }
        // autoStart intentionally NOT read from portable INI — the OS owns
        // that state (HKCU\...\Run / launchd plist / XDG .desktop). See
        // AutoStartManager.h and ConfigManager::load().
        qDebug() << "ConfigManager: portable defaults loaded from" << portablePath;
    }
}

void ConfigManager::load()
{
    // Always read LLM settings regardless of whether a full config exists.
    // This ensures testEmptyOnFirstRun (and any cleared-settings scenario)
    // gets empty profiles rather than stale in-memory data, since these keys
    // live in a separate section and must always reflect the on-disk state.
    m_llmProfiles.clear();
    const int llmN = m_settings.beginReadArray(QStringLiteral("llm/profiles"));
    for (int i = 0; i < llmN; ++i) {
        m_settings.setArrayIndex(i);
        LLMProfile p;
        p.name = m_settings.value(QStringLiteral("name")).toString();
        p.protocol = static_cast<LLMProfile::Protocol>(
            m_settings.value(QStringLiteral("protocol"), 0).toInt());
        p.baseUrl = m_settings.value(QStringLiteral("baseUrl")).toString();
        p.apiKey = m_settings.value(QStringLiteral("apiKey")).toString();
        p.model = m_settings.value(QStringLiteral("model")).toString();
        if (!p.name.isEmpty()) m_llmProfiles.append(p);
    }
    m_settings.endArray();
    m_personaProfile = m_settings.value(QStringLiteral("llm/personaProfile")).toString();
    m_personaEnabled = m_settings.value(QStringLiteral("llm/personaEnabled"), false).toBool();
    m_shareMemoryWithAi = m_settings.value(QStringLiteral("llm/shareMemoryWithAi"), false).toBool();

    if (!m_settings.contains("language")) {
        qDebug() << "No config file found, using defaults";
        return;
    }

    // Window position
    m_windowPosition.setX(m_settings.value("windowX", 0).toInt());
    m_windowPosition.setY(m_settings.value("windowY", 0).toInt());

    // Language
    m_language = m_settings.value("language", "en").toString();

    // Auto-start: the OS owns the source of truth (HKCU\...\Run on Windows,
    // ~/Library/LaunchAgents on macOS, ~/.config/autostart on Linux). We
    // used to shadow it in this INI under "autoStart", but that produced a
    // silent disagreement with the Inno installer's "Launch at startup"
    // task — both surfaces wrote the same registry key, but ConfigManager
    // rewrote it from a stale INI on every load. Now we just ask the OS.
    //
    // One-shot migration: if a legacy autoStart=true is present in the INI
    // and the OS state is NOT yet set (e.g. fresh upgrade, registry key
    // cleared somehow), seed the OS state from the INI then drop the key.
    m_autoStart = AutoStartManager::isEnabled();
    if (m_settings.contains("autoStart")) {
        const bool legacy = m_settings.value("autoStart", false).toBool();
        if (legacy && !m_autoStart) {
            AutoStartManager::setEnabled(true);
            m_autoStart = true;
        }
        m_settings.remove("autoStart");
    }

    // IPC endpoint
    QString endpoint = m_settings.value("ipcEndpoint").toString();
    if (!endpoint.isEmpty()) {
        m_ipcEndpoint = endpoint;
    }

    // Update-server endpoint (UDP daemon for version checks)
    QString upd = m_settings.value("updateServerEndpoint").toString();
    if (!upd.isEmpty()) {
        m_updateServerEndpoint = upd;
    }

    // Last-selected character pack
    m_activePackId = m_settings.value("activePackId").toString();

    // Display mode (with migration from old ecgEnabled key)
    if (m_settings.contains("displayMode")) {
        const QString modeStr = m_settings.value("displayMode").toString();
        m_displayMode = (modeStr == QStringLiteral("ecg"))
                        ? DisplayMode::Ecg
                        : DisplayMode::Character;
    } else if (m_settings.contains("ecgEnabled")) {
        // Migrate from old ecgEnabled boolean
        m_displayMode = m_settings.value("ecgEnabled", false).toBool()
                        ? DisplayMode::Ecg
                        : DisplayMode::Character;
        // Write new key and remove old one; persist immediately
        m_settings.setValue("displayMode",
                            m_displayMode == DisplayMode::Ecg
                                ? QStringLiteral("ecg")
                                : QStringLiteral("character"));
        m_settings.remove("ecgEnabled");
        m_settings.sync();
    } else {
        m_displayMode = DisplayMode::Character;
    }

    m_globalShortcut = m_settings.value("globalShortcut", m_globalShortcut).toString();
    m_globalShortcutEnabled = m_settings.value("globalShortcutEnabled", m_globalShortcutEnabled).toBool();
    m_gamingModeEnabled = m_settings.value("gamingMode", false).toBool();
    m_contextSensesEnabled = m_settings.value("contextSensesEnabled", true).toBool();
    m_touchReactionsEnabled = m_settings.value("touchReactions", true).toBool();
    m_tipBubblesEnabled = m_settings.value("tipBubblesEnabled", m_tipBubblesEnabled).toBool();

    m_ttsEnabled = m_settings.value("tts/enabled", false).toBool();

    // One-shot migration: if any flat tts/* keys exist, copy them into
    // tts/providers/stepfun/* and delete the originals. Detect by presence
    // of the legacy baseUrl key, which only ever existed in the StepFun era.
    if (m_settings.contains("tts/baseUrl") ||
        m_settings.contains("tts/token") ||
        m_settings.contains("tts/voice") ||
        m_settings.contains("tts/model") ||
        m_settings.contains("tts/language"))
    {
        const QString legacyBaseUrl = m_settings.value("tts/baseUrl").toString();
        const QString legacyToken   = m_settings.value("tts/token").toString();
        const QString legacyModel   = m_settings.value("tts/model").toString();
        const QString legacyVoice   = m_settings.value("tts/voice").toString();

        m_settings.beginGroup("tts/providers/stepfun");
        if (!legacyBaseUrl.isEmpty()) m_settings.setValue("baseUrl", legacyBaseUrl);
        if (!legacyToken.isEmpty())   m_settings.setValue("token",   legacyToken);
        if (!legacyModel.isEmpty())   m_settings.setValue("model",   legacyModel);
        if (!legacyVoice.isEmpty())   m_settings.setValue("voice",   legacyVoice);
        m_settings.endGroup();

        m_settings.remove("tts/baseUrl");
        m_settings.remove("tts/token");
        m_settings.remove("tts/model");
        m_settings.remove("tts/voice");
        m_settings.remove("tts/language");

        if (!m_settings.contains("tts/activeProvider"))
            m_settings.setValue("tts/activeProvider", QStringLiteral("stepfun"));
    }

    m_ttsActiveProvider =
        m_settings.value("tts/activeProvider", QStringLiteral("stepfun")).toString();

    m_ttsProviders.clear();
    m_settings.beginGroup("tts/providers");
    const QStringList providerIds = m_settings.childGroups();
    for (const QString& providerId : providerIds) {
        m_settings.beginGroup(providerId);
        QHash<QString, QString> fields;
        for (const QString& key : m_settings.childKeys())
            fields.insert(key, m_settings.value(key).toString());
        m_ttsProviders.insert(providerId, fields);
        m_settings.endGroup();
    }
    m_settings.endGroup();

    qDebug() << "Config loaded from:" << m_settings.fileName();
}

void ConfigManager::save()
{
    // Schedule; coalesces bursts of setters into one disk write.
    m_saveTimer.start();
}

void ConfigManager::flush()
{
    m_saveTimer.stop();
    m_settings.setValue("windowX", m_windowPosition.x());
    m_settings.setValue("windowY", m_windowPosition.y());
    m_settings.setValue("language", m_language);
    // autoStart is NOT persisted to QSettings — the OS owns that state.
    // See setAutoStart() / load() and AutoStartManager.
    m_settings.setValue("ipcEndpoint", m_ipcEndpoint);
    m_settings.setValue("updateServerEndpoint", m_updateServerEndpoint);
    m_settings.setValue("activePackId", m_activePackId);
    m_settings.setValue("displayMode",
                        m_displayMode == DisplayMode::Ecg
                            ? QStringLiteral("ecg")
                            : QStringLiteral("character"));
    m_settings.setValue("globalShortcut", m_globalShortcut);
    m_settings.setValue("globalShortcutEnabled", m_globalShortcutEnabled);
    m_settings.setValue("gamingMode", m_gamingModeEnabled);
    m_settings.setValue("contextSensesEnabled", m_contextSensesEnabled);
    m_settings.setValue("touchReactions", m_touchReactionsEnabled);
    m_settings.setValue("tipBubblesEnabled", m_tipBubblesEnabled);
    m_settings.setValue("tts/enabled", m_ttsEnabled);
    m_settings.setValue("tts/activeProvider", m_ttsActiveProvider);

    // Re-write all known provider subtrees. Removing the parent group first
    // makes the on-disk state match the in-memory map exactly (handles
    // deletions of keys that were cleared in-memory).
    m_settings.remove("tts/providers");
    for (auto pit = m_ttsProviders.cbegin(); pit != m_ttsProviders.cend(); ++pit) {
        m_settings.beginGroup(QStringLiteral("tts/providers/") + pit.key());
        for (auto fit = pit.value().cbegin(); fit != pit.value().cend(); ++fit)
            m_settings.setValue(fit.key(), fit.value());
        m_settings.endGroup();
    }
    m_settings.sync();
}

ConfigManager::~ConfigManager()
{
    // Guarantee any pending debounced write lands before the QSettings is
    // destroyed.
    if (m_saveTimer.isActive()) flush();
}

QString ConfigManager::configDir() const
{
    return QFileInfo(m_settings.fileName()).absolutePath();
}

void ConfigManager::setWindowPosition(const QPoint &pos)
{
    if (m_windowPosition != pos) {
        m_windowPosition = pos;
        save();
    }
}

void ConfigManager::setLanguage(const QString &lang)
{
    if (m_language != lang) {
        m_language = lang;
        save();
        emit languageChanged(lang);
    }
}

void ConfigManager::setAutoStart(bool enabled)
{
    if (m_autoStart != enabled) {
        m_autoStart = enabled;
        // No save() — autoStart lives in the OS, not QSettings. The setter
        // here exists so the in-memory mirror stays consistent for the
        // settings panel and any signal listeners.
        AutoStartManager::setEnabled(enabled);
    }
}


void ConfigManager::setActivePackId(const QString &packId)
{
    if (m_activePackId != packId) {
        m_activePackId = packId;
        save();
    }
}

quint16 ConfigManager::ipcPort() const
{
    constexpr quint16 kDefaultPort = 52847;
    int colonPos = m_ipcEndpoint.lastIndexOf(':');
    if (colonPos <= 0) return kDefaultPort;

    bool ok = false;
    const uint parsed = m_ipcEndpoint.mid(colonPos + 1).toUInt(&ok);
    // Reject parse failures and out-of-range values rather than silently
    // truncating via the quint16 cast (toUInt('99999')→99999→34464).
    if (!ok || parsed == 0 || parsed > 65535) {
        return kDefaultPort;
    }
    return static_cast<quint16>(parsed);
}

void ConfigManager::setIpcPort(quint16 port)
{
    const QString newEndpoint = QStringLiteral("127.0.0.1:") + QString::number(port);
    if (m_ipcEndpoint != newEndpoint) {
        m_ipcEndpoint = newEndpoint;
        save();
        emit ipcEndpointChanged(m_ipcEndpoint);
    }
}

void ConfigManager::setUpdateServerEndpoint(const QString &endpoint)
{
    if (m_updateServerEndpoint != endpoint) {
        m_updateServerEndpoint = endpoint;
        save();
        emit updateServerEndpointChanged(m_updateServerEndpoint);
    }
}

void ConfigManager::setDisplayMode(DisplayMode mode)
{
    if (m_displayMode != mode) {
        m_displayMode = mode;
        save();
        emit displayModeChanged(mode);
    }
}

void ConfigManager::setGlobalShortcut(const QString &shortcut)
{
    if (m_globalShortcut == shortcut) return;
    m_globalShortcut = shortcut;
    save();
    emit globalShortcutChanged(shortcut);
}

void ConfigManager::setGlobalShortcutEnabled(bool enabled)
{
    if (m_globalShortcutEnabled == enabled) return;
    m_globalShortcutEnabled = enabled;
    save();
}

void ConfigManager::setGamingModeEnabled(bool enabled)
{
    if (m_gamingModeEnabled == enabled) return;
    m_gamingModeEnabled = enabled;
    save();
    emit gamingModeEnabledChanged(enabled);
}

void ConfigManager::setContextSensesEnabled(bool enabled)
{
    if (m_contextSensesEnabled == enabled) return;
    m_contextSensesEnabled = enabled;
    save();
    emit contextSensesEnabledChanged(enabled);
}

void ConfigManager::setTouchReactionsEnabled(bool enabled)
{
    if (m_touchReactionsEnabled == enabled) return;
    m_touchReactionsEnabled = enabled;
    save();
    emit touchReactionsEnabledChanged(enabled);
}

void ConfigManager::setTipBubblesEnabled(bool enabled)
{
    if (m_tipBubblesEnabled == enabled) return;
    m_tipBubblesEnabled = enabled;
    save();
    emit tipBubblesEnabledChanged(enabled);
}

void ConfigManager::setTtsEnabled(bool enabled)
{
    if (m_ttsEnabled == enabled) return;
    m_ttsEnabled = enabled;
    save();
    emit ttsEnabledChanged(enabled);
}

void ConfigManager::setTtsActiveProvider(const QString &stableId)
{
    if (m_ttsActiveProvider == stableId) return;
    m_ttsActiveProvider = stableId;
    save();
    emit ttsActiveProviderChanged(stableId);
    emit ttsCacheInvalidated();
}

QString ConfigManager::ttsProviderField(const QString &providerId,
                                        const QString &field) const
{
    auto pit = m_ttsProviders.constFind(providerId);
    if (pit == m_ttsProviders.constEnd()) return QString();
    auto fit = pit->constFind(field);
    return fit == pit->constEnd() ? QString() : *fit;
}

void ConfigManager::setTtsProviderField(const QString &providerId,
                                        const QString &field,
                                        const QString &value)
{
    // Trim whitespace — provider tokens and URLs are frequently pasted with
    // a leading/trailing space or tab, which makes the resulting Authorization
    // header malformed and the request unauthorized for opaque reasons.
    const QString trimmed = value.trimmed();
    QHash<QString, QString>& fields = m_ttsProviders[providerId];
    if (fields.value(field) == trimmed) return;
    fields.insert(field, trimmed);
    save();
    emit ttsProviderFieldChanged(providerId, field, trimmed);
    // Voice and model are part of the cache fingerprint; changing them must
    // invalidate cached audio for the active provider, otherwise we serve
    // audio synthesized under the previous voice/model selection.
    if (field == QStringLiteral("voice") || field == QStringLiteral("model"))
        emit ttsCacheInvalidated();
}

QHash<QString, QString> ConfigManager::ttsProviderConfig(const QString &providerId) const
{
    return m_ttsProviders.value(providerId);
}

void ConfigManager::setLLMProfiles(const QVector<LLMProfile> &profiles)
{
    m_llmProfiles = profiles;

    // QSettings arrays must be cleared first or stale entries persist.
    m_settings.remove(QStringLiteral("llm/profiles"));

    m_settings.beginWriteArray(QStringLiteral("llm/profiles"));
    for (int i = 0; i < m_llmProfiles.size(); ++i) {
        const auto &p = m_llmProfiles[i];
        m_settings.setArrayIndex(i);
        m_settings.setValue(QStringLiteral("name"), p.name);
        m_settings.setValue(QStringLiteral("protocol"), static_cast<int>(p.protocol));
        m_settings.setValue(QStringLiteral("baseUrl"), p.baseUrl);
        m_settings.setValue(QStringLiteral("apiKey"), p.apiKey);
        m_settings.setValue(QStringLiteral("model"), p.model);
    }
    m_settings.endArray();
    save();
    emit llmProfilesChanged();
}

void ConfigManager::setPersonaProfile(const QString &name)
{
    if (m_personaProfile == name) return;
    m_personaProfile = name;
    m_settings.setValue(QStringLiteral("llm/personaProfile"), name);
    save();
    emit personaProfileChanged(name);
}

void ConfigManager::setPersonaEnabled(bool enabled)
{
    if (m_personaEnabled == enabled) return;
    m_personaEnabled = enabled;
    m_settings.setValue(QStringLiteral("llm/personaEnabled"), enabled);
    save();
    emit personaEnabledChanged(enabled);
}

void ConfigManager::setShareMemoryWithAi(bool enabled)
{
    if (m_shareMemoryWithAi == enabled) return;
    m_shareMemoryWithAi = enabled;
    m_settings.setValue(QStringLiteral("llm/shareMemoryWithAi"), enabled);
    save();
    emit shareMemoryWithAiChanged(enabled);
}
