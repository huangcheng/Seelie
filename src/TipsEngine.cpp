#include "TipsEngine.h"
#include "CanonicalEvents.h"
#include "TipWidget.h"
#include "MemoryManager.h"

#include <QJsonObject>
#include <QDateTime>
#include <QDebug>

namespace CE = CanonicalEvents;

TipsEngine::TipsEngine(QObject *parent)
    : QObject(parent)
{
    initMatchers();
}

void TipsEngine::processEvent(const QString &eventName, const QJsonObject &eventData)
{
    // Add to event window
    EventEntry entry;
    entry.name = eventName;
    entry.timestamp = QDateTime::currentDateTime();
    entry.data = eventData;
    m_eventWindow.append(entry);

    // Trim old events (outside 30-second window)
    const QDateTime cutoff = entry.timestamp.addSecs(-m_windowDurationSec);
    while (!m_eventWindow.isEmpty() && m_eventWindow.first().timestamp < cutoff) {
        m_eventWindow.removeFirst();
    }

    // Trim to max window size
    while (m_eventWindow.size() > m_windowSize) {
        m_eventWindow.removeFirst();
    }

    // Run pattern matchers
    for (const PatternMatcher &matcher : m_matchers) {
        if (isInCooldown(matcher.name)) {
            continue;
        }

        // Build event list for matcher
        QVector<QPair<QString, QDateTime>> events;
        for (const EventEntry &e : m_eventWindow) {
            events.append({e.name, e.timestamp});
        }

        if (matcher.matcher(events)) {
            // M4: Use qint64 msecs since epoch (less sensitive to wall-clock
            // jumps than QDateTime::currentDateTime()).
            m_lastTriggered[matcher.name] = QDateTime::currentMSecsSinceEpoch();

            if (m_tipWidget) {
                m_tipWidget->showBubble(matcher.tipTitle, matcher.tipBody,
                                        TipWidget::TipBubble);
            }
            if (!matcher.animation.isEmpty()) {
                // Engine-agnostic dispatch: MainWindow listens and routes
                // through the same Live2D > Lottie > Sprite priority chain
                // EventRouter uses. Previously this called directly into
                // SpriteAnimationEngine, which meant tip animations were
                // silently dropped for Lottie and Live2D packs (audit H1).
                emit animationRequested(matcher.animation);
            }

            // first_tip milestone — fires once, the first time any tip is shown
            if (m_memory) {
                m_memory->checkMilestone(QStringLiteral("first_tip"),
                                          tr("Seelie noticed something!"),
                                          tr("I'll try to give helpful tips while you work."));
            }

            qDebug() << "TipsEngine: Pattern matched:" << matcher.name;
            break; // Only trigger one tip per event
        }
    }
}

void TipsEngine::initMatchers()
{
    // 1. Repeated errors (3+ errors in 30 seconds)
    m_matchers.append({
        "repeated_errors",
        [](const QVector<QPair<QString, QDateTime>> &events) {
            int errorCount = 0;
            for (const auto &e : events) {
                if (e.first == CE::SessionError || e.first == CE::ToolFailed) {
                    errorCount++;
                }
            }
            return errorCount >= 3;
        },
        tr("Having trouble?"),
        tr("It looks like you're running into repeated errors. Try checking the error messages carefully."),
        "alert"
    });

    // 2. Test file activity
    m_matchers.append({
        "test_file_activity",
        [](const QVector<QPair<QString, QDateTime>> &events) {
            for (const auto &e : events) {
                if (e.first == CE::FileEdited || e.first == CE::FileWatched) {
                    return true;
                }
            }
            return false;
        },
        tr("Working on tests?"),
        tr("It looks like you're editing test files. Remember to run your tests after making changes!"),
        "explain"
    });

    // 3. Config editing
    m_matchers.append({
        "config_editing",
        [](const QVector<QPair<QString, QDateTime>> &events) {
            int configEdits = 0;
            for (const auto &e : events) {
                if (e.first == CE::FileEdited) {
                    configEdits++;
                }
            }
            return configEdits >= 2;
        },
        tr("Updating configuration?"),
        tr("It looks like you're making configuration changes. Don't forget to restart your services if needed!"),
        "sendmail"
    });

    // 4. First edit after start
    m_matchers.append({
        "first_edit",
        [](const QVector<QPair<QString, QDateTime>> &events) {
            bool hasStart = false;
            bool hasEdit = false;
            for (const auto &e : events) {
                if (e.first == CE::SessionStart) hasStart = true;
                if (e.first == CE::FileEdited) hasEdit = true;
            }
            return hasStart && hasEdit;
        },
        tr("Getting started!"),
        tr("It looks like you're making your first edit in this session. Happy coding!"),
        "wave"
    });

    // 5. Rapid changes (5+ edits in 30 seconds)
    m_matchers.append({
        "rapid_changes",
        [](const QVector<QPair<QString, QDateTime>> &events) {
            int editCount = 0;
            for (const auto &e : events) {
                if (e.first == CE::FileEdited) {
                    editCount++;
                }
            }
            return editCount >= 5;
        },
        tr("Making lots of changes?"),
        tr("It looks like you're making rapid edits. Consider committing your work to save progress!"),
        "congratulate"
    });

    // 6. Idle after start
    m_matchers.append({
        "idle_after_start",
        [](const QVector<QPair<QString, QDateTime>> &events) {
            if (events.size() < 2) return false;
            // Check if first event is session.start and last is session.idle
            return events.first().first == CE::SessionStart &&
                   events.last().first == CE::SessionIdle;
        },
        tr("Taking a break?"),
        tr("It looks like you've been idle. Let me know when you're ready to continue!"),
        "getattentionyawn"
    });

    // 7. Permission denials
    m_matchers.append({
        "permission_denials",
        [](const QVector<QPair<QString, QDateTime>> &events) {
            int denialCount = 0;
            for (const auto &e : events) {
                if (e.first == CE::PermissionDenied) {
                    denialCount++;
                }
            }
            return denialCount >= 2;
        },
        tr("Permission issues?"),
        tr("It looks like you've denied permissions multiple times. You can configure auto-approval in your tool settings."),
        "explain"
    });

    // 8. Git commands (check tool names)
    m_matchers.append({
        "git_commands",
        [](const QVector<QPair<QString, QDateTime>> &events) {
            for (const auto &e : events) {
                if (e.first == CE::ToolBefore || e.first == CE::ToolAfter) {
                    // Would need to check tool name in data
                    return false; // Simplified for now
                }
            }
            return false;
        },
        tr("Using git?"),
        tr("It looks like you're working with git. Remember to pull before pushing!"),
        "explain"
    });
}

void TipsEngine::retranslateUi()
{
    m_matchers.clear();
    initMatchers();
}

bool TipsEngine::isInCooldown(const QString &patternName) const
{
    if (!m_lastTriggered.contains(patternName)) {
        return false;
    }

    // M4: Use monotonic msecs since epoch instead of QDateTime wall-clock
    // comparison. currentMSecsSinceEpoch() is less affected by NTP jumps
    // and DST changes than QDateTime::currentDateTime().
    const qint64 lastTimeMs = m_lastTriggered.value(patternName);
    return (QDateTime::currentMSecsSinceEpoch() - lastTimeMs) < m_cooldownMinMs;
}
