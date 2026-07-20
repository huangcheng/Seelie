#ifndef SAYINGPOOL_H
#define SAYINGPOOL_H

#include <QString>
#include <QVector>
#include <functional>

// Categorized canned idle sayings. Loading tries, in order, the first source
// that yields >=1 saying (no merging):
//   1. <overrideDir>/sayings.<locale>.json   (external override, bare format)
//   2. <overrideDir>/sayings.en.json         (external en fallback)
//   3. :/i18n/i18n/tips.<locale>.json        (qrc bundle, "sayings" key)
//   4. :/i18n/i18n/tips.en.json              (qrc en fallback, "sayings" key)
//
// External override files use the friendlier BARE categories format:
//   { "humor": [{"title": "...", "body": "..."}, ...], ... }
// qrc bundles use the full tips document with a top-level "sayings" key.
// The parser auto-detects: if the JSON root contains a "sayings" key, it
// parses inside it; otherwise the root itself is treated as the categories
// map. Weighted category draw + per-category anti-repeat. Files that yield
// zero sayings fall through to the next source.
class SayingPool
{
public:
    struct Saying { QString title; QString body; };

    SayingPool();

    // Loads per the chain above. When overrideDir is empty, steps 1-2 are
    // skipped (qrc-only — the historical behaviour). Returns true when at
    // least one saying was loaded.
    bool load(const QString &locale, const QString &overrideDir = QString());

    bool isEmpty() const;
    int size() const;   // total sayings across categories

    Saying pick();

    // Test seam: deterministic RNG in [0,1).
    void setRngFn(std::function<double()> fn) { m_rng = std::move(fn); }

private:
    struct Category {
        QString name;
        int weight = 1;
        QVector<Saying> sayings;
        int lastIndex = -1;
    };
    bool loadFile(const QString &path);

    QVector<Category> m_categories;
    std::function<double()> m_rng;
};

#endif // SAYINGPOOL_H
