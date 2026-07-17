#ifndef TIPSCATALOG_H
#define TIPSCATALOG_H

#include <QHash>
#include <QString>
#include <QVector>

/**
 * @brief Loads per-locale tip text from `:/i18n/tips.<locale>.json`.
 *
 * Tip strings (event titles/bodies + random greeting pool) live in plain
 * JSON files under `assets/i18n/`, bundled via `assets/tips.qrc`. Non-
 * engineers can add a language by dropping in another `tips.<locale>.json`
 * — no .ts editing or `lupdate` run needed.
 *
 * Falls back to English for missing keys / missing locales. Singleton
 * because two different code paths (EventRouter + MainWindow's random
 * greeting) need the same loaded state and the resources are tiny.
 */
class TipsCatalog
{
public:
    struct Tip {
        QString title;
        QString body;
    };

    static TipsCatalog &instance();

    /// Switch active locale. Re-reads from the corresponding resource file
    /// and falls back to en if the file isn't bundled.
    void setLocale(const QString &locale);

    /// Resolve an event tip. Empty Tip if the event has no entry in any locale.
    Tip eventTip(const QString &eventName) const;

    /// Pick one greeting at random from the active locale's pool. Empty Tip
    /// if the pool is empty.
    Tip randomGreeting() const;

    /// Pick one touch-reaction line at random for `gesture` ("pet", "toss").
    /// Empty Tip if the gesture has no lines in active or fallback locale.
    Tip touchLine(const QString &gesture) const;

    /// Resolve an interaction message (about box, pack-install result, etc.)
    /// by id. Empty Tip if no entry exists in active or fallback locale.
    Tip message(const QString &id) const;

private:
    TipsCatalog();
    ~TipsCatalog() = default;
    TipsCatalog(const TipsCatalog &) = delete;
    TipsCatalog &operator=(const TipsCatalog &) = delete;

    struct Bundle {
        QHash<QString, Tip> events;
        QVector<Tip> greetings;
        QHash<QString, Tip> messages;
        QHash<QString, QVector<Tip>> touch;   // gesture → line pool
    };

    static Bundle loadBundle(const QString &locale);
    const Bundle &activeBundle() const;
    const Bundle &fallbackBundle() const;

    QString m_locale;
    QHash<QString, Bundle> m_bundles;   // keyed by locale code, "en" always loaded
};

#endif // TIPSCATALOG_H
