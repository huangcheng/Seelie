#include "GlobalShortcutManager.h"
#include <QHotkey>
#include <QDebug>

GlobalShortcutManager::GlobalShortcutManager(QObject *parent)
    : QObject(parent)
    , m_shortcut(QStringLiteral("Ctrl+Shift+O"))
{
    m_hotkey = new QHotkey(QKeySequence(m_shortcut), true, this);
    connect(m_hotkey, &QHotkey::activated, this, &GlobalShortcutManager::activated);
    if (!m_hotkey->isRegistered()) {
        qWarning() << "GlobalShortcutManager: default shortcut" << m_shortcut
                   << "failed to register — another application may hold it";
    }
}

GlobalShortcutManager::~GlobalShortcutManager() = default;

void GlobalShortcutManager::setShortcut(const QString &shortcut)
{
    if (m_shortcut == shortcut) return;
    if (m_hotkey) {
        if (m_hotkey->setShortcut(QKeySequence(shortcut), false)) {
            m_shortcut = shortcut;
            if (m_enabled) {
                m_hotkey->setRegistered(true);
            }
            emit shortcutChanged(shortcut);
        } else {
            qWarning() << "GlobalShortcutManager: failed to register shortcut" << shortcut;
        }
    }
}

QString GlobalShortcutManager::shortcut() const { return m_shortcut; }

void GlobalShortcutManager::setEnabled(bool enabled)
{
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    if (m_hotkey) {
        m_hotkey->setRegistered(enabled);
    }
}

bool GlobalShortcutManager::enabled() const { return m_enabled; }
