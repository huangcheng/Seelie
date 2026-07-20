#ifndef SAYINGPOOL_H
#define SAYINGPOOL_H

#include <QString>
#include <QVector>
#include <functional>

// Categorized canned idle sayings, loaded from the "sayings" section of the
// existing i18n tip bundles (:/i18n/i18n/tips.<locale>.json). Weighted
// category draw + per-category anti-repeat. Locale files that lack the
// "sayings" key fall back to the en bundle.
class SayingPool
{
public:
    struct Saying { QString title; QString body; };

    SayingPool();

    // Returns true when at least one saying was loaded.
    bool load(const QString &locale);

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
