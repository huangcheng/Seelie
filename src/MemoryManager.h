#ifndef MEMORYMANAGER_H
#define MEMORYMANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <QString>

class MemoryManager : public QObject
{
    Q_OBJECT
public:
    explicit MemoryManager(const QString &dbPath, QObject *parent = nullptr);
    ~MemoryManager() override;

    bool isValid() const { return m_valid; }

    /// Borrow the underlying QSqlDatabase. Returned by value (QSqlDatabase is a handle).
    /// PersonaPool uses this so all SQLite access lives on the same connection / thread.
    QSqlDatabase database() const { return m_db; }

    QString value(const QString &key, const QString &defaultValue = QString()) const;
    bool setValue(const QString &key, const QString &value);

    int increment(const QString &key, int delta = 1);

    QString lastGreeting() const;
    bool setLastGreeting(const QString &text);

    QString userName() const;
    void setUserName(const QString &name);
    QString displayName() const;
    void setDisplayName(const QString &name);

    bool hasMilestone(const QString &key) const;
    bool setMilestone(const QString &key);
    void checkMilestone(const QString &key, const QString &title, const QString &body);

    QString effectiveName() const;

signals:
    void userNameChanged(const QString &name);
    void milestoneReached(const QString &title, const QString &body);

private:
    QSqlDatabase m_db;
    QString m_connectionName;
    bool m_valid = false;
};

#endif // MEMORYMANAGER_H
