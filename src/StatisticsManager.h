#ifndef STATISTICS_MANAGER_H
#define STATISTICS_MANAGER_H

#include "StatisticsPersistence.h"
#include <QMutex>
#include <QObject>
#include <QTimer>
#include <functional>
#include <QVector>

/**
 * @brief Thread-safe application singleton for persisting statistics.
 *
 * Components from any thread register their load/save callbacks.
 * The manager orchestrates periodic auto-save (main thread) and
 * provides thread-safe access to save/load/reset from anywhere.
 *
 * Usage:
 *   // Register from any thread (typically main thread during startup):
 *   StatisticsManager::instance().registerComponent("persona",
 *       [&]() { personaEngine.loadStats(dir); },
 *       [&]() { personaEngine.saveStats(dir); });
 *
 *   // Start auto-save from main thread:
 *   StatisticsManager::instance().startAutoSave(dir);
 *
 *   // Trigger save from any thread:
 *   StatisticsManager::instance().saveAll();
 */
class StatisticsManager : public QObject
{
    Q_OBJECT
public:
    /// Global instance accessor. Returns the instance created by
    /// createInstance() in main.cpp. Do NOT call before createInstance().
    static StatisticsManager *instance();

    /// Create the app-scope singleton, parented to the QApplication.
    /// Call once from main() before registering any components.
    static void createInstance(const QString &configDir, QObject *parent = nullptr);

    /// Register a component's load/save callbacks. Thread-safe.
    void registerComponent(const QString &name,
                           std::function<void()> loader,
                           std::function<void()> saver);

    /// Load all registered components. Thread-safe.
    void loadAll();

    /// Save all registered components. Thread-safe.
    void saveAll();

    /// Start periodic auto-save timer. Must be called from the thread
    /// that owns the QTimer event loop (typically main thread).
    void startAutoSave(int intervalMs = 60000);

    /// Stop the auto-save timer.
    void stopAutoSave();

    /// Reset all stats (delete statistics.json and reload defaults).
    void resetAll();

    /// Access the underlying persistence for direct use (e.g. reset in dialog).
    StatisticsPersistence *persistence() { return m_persistence; }

private slots:
    void onAutoSave();

private:
    explicit StatisticsManager(const QString &configDir, QObject *parent = nullptr);
    ~StatisticsManager() override;
    Q_DISABLE_COPY(StatisticsManager)

    mutable QMutex m_mutex;
    StatisticsPersistence *m_persistence;
    QTimer *m_autoSaveTimer = nullptr;

    struct Component {
        QString name;
        std::function<void()> loader;
        std::function<void()> saver;
    };
    QVector<Component> m_components;
};

#endif // STATISTICS_MANAGER_H
