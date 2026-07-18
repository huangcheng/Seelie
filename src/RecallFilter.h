#ifndef RECALLFILTER_H
#define RECALLFILTER_H

#include "MemoryManager.h"

#include <QDateTime>
#include <QString>

/**
 * Pure helpers for the Recall UI ("What do you remember?"). Header-only so
 * the unit tests link nothing beyond MemoryManager's header. No QObject, no
 * Qt global state — deterministic and trivially testable.
 */
namespace RecallFilter {

/// Case-insensitive substring filter over episodes. Empty query returns the
/// input unchanged (caller's default-view path).
inline QVector<Episode> contains(const QVector<Episode> &episodes, const QString &query)
{
    if (query.isEmpty()) return episodes;
    QVector<Episode> out;
    for (const Episode &e : episodes) {
        if (e.text.contains(query, Qt::CaseInsensitive)) out.append(e);
    }
    return out;
}

/// Relative timestamp for episode rows. `locale` follows ConfigManager's
/// Qt-locale code ("en", "zh_CN", ...) — zh strings when it starts with "zh".
/// Future/skewed timestamps clamp to the "just now" bucket; ≥7 days falls
/// back to the absolute ISO date (relative weeks read badly).
inline QString relativeTime(qint64 ts, qint64 nowMs, const QString &locale)
{
    const bool zh = locale.startsWith(QLatin1String("zh"), Qt::CaseInsensitive);
    qint64 diff = nowMs - ts;
    if (diff < 0) diff = 0;  // clock skew → just now
    const qint64 min = diff / 60000;
    if (min < 1)  return zh ? QStringLiteral("刚刚") : QStringLiteral("just now");
    if (min < 60) return zh ? QStringLiteral("%1 分钟前").arg(min)
                            : QStringLiteral("%1m ago").arg(min);
    const qint64 h = min / 60;
    if (h < 24)   return zh ? QStringLiteral("%1 小时前").arg(h)
                            : QStringLiteral("%1h ago").arg(h);
    const qint64 d = h / 24;
    if (d < 7)    return zh ? QStringLiteral("%1 天前").arg(d)
                            : QStringLiteral("%1d ago").arg(d);
    return QDateTime::fromMSecsSinceEpoch(ts).date().toString(Qt::ISODate);
}

} // namespace RecallFilter

#endif // RECALLFILTER_H
