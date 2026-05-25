#include "StatisticsManager.h"
#include <QDebug>
#include <QFile>
#include <QMutexLocker>

static StatisticsManager *g_instance = nullptr;

StatisticsManager *StatisticsManager::instance()
{
    return g_instance;
}

void StatisticsManager::createInstance(const QString &configDir, QObject *parent)
{
    Q_ASSERT(!g_instance);
    g_instance = new StatisticsManager(configDir, parent);
}

StatisticsManager::StatisticsManager(const QString &configDir, QObject *parent)
    : QObject(parent)
    , m_persistence(new StatisticsPersistence(configDir))
{
}

StatisticsManager::~StatisticsManager()
{
    delete m_persistence;
    g_instance = nullptr;
}

void StatisticsManager::registerComponent(const QString &name,
                                           std::function<void()> loader,
                                           std::function<void()> saver)
{
    QMutexLocker lock(&m_mutex);
    m_components.append({name, std::move(loader), std::move(saver)});
}

void StatisticsManager::loadAll()
{
    QMutexLocker lock(&m_mutex);
    for (const auto &c : m_components) {
        if (c.loader) {
            c.loader();
        }
    }
}

void StatisticsManager::saveAll()
{
    QMutexLocker lock(&m_mutex);
    for (const auto &c : m_components) {
        if (c.saver) {
            c.saver();
        }
    }
}

void StatisticsManager::startAutoSave(int intervalMs)
{
    QMutexLocker lock(&m_mutex);
    if (!m_autoSaveTimer) {
        m_autoSaveTimer = new QTimer(this);
        connect(m_autoSaveTimer, &QTimer::timeout, this, &StatisticsManager::onAutoSave);
    }
    m_autoSaveTimer->stop();
    m_autoSaveTimer->setInterval(intervalMs);
    m_autoSaveTimer->start();
}

void StatisticsManager::stopAutoSave()
{
    QMutexLocker lock(&m_mutex);
    if (m_autoSaveTimer) {
        m_autoSaveTimer->stop();
    }
}

void StatisticsManager::onAutoSave()
{
    saveAll();
}

void StatisticsManager::resetAll()
{
    QMutexLocker lock(&m_mutex);

    // Delete the statistics file
    if (m_persistence) {
        QFile::remove(m_persistence->filePath());
    }

    // Reload all components — each component's loadStats will see an empty
    // file and reset to defaults (zeros).
    for (const auto &c : m_components) {
        if (c.loader) {
            c.loader();
        }
    }
}
