#include "SayingPool.h"
#include "IdlePicker.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QDebug>

SayingPool::SayingPool()
    : m_rng([] { return QRandomGenerator::global()->generateDouble(); })
{
}

bool SayingPool::load(const QString &locale)
{
    m_categories.clear();
    const QString path = QStringLiteral(":/i18n/i18n/tips.%1.json").arg(locale);
    if (!loadFile(path)) {
        if (locale == QLatin1String("en")) {
            qWarning() << "SayingPool: no sayings in en bundle — sayings disabled";
            return false;
        }
        return loadFile(QStringLiteral(":/i18n/i18n/tips.en.json"));
    }
    return !isEmpty();
}

bool SayingPool::isEmpty() const
{
    return size() == 0;
}

int SayingPool::size() const
{
    int n = 0;
    for (const auto &c : m_categories) n += c.sayings.size();
    return n;
}

SayingPool::Saying SayingPool::pick()
{
    if (m_categories.isEmpty()) return {};

    QVector<int> catWeights;
    for (const auto &c : m_categories) catWeights.append(c.weight);
    const int ci = IdlePicker::pickWeighted(catWeights, -1, m_rng());
    if (ci < 0) return {};

    Category &cat = m_categories[ci];
    QVector<int> sayWeights(cat.sayings.size(), 1);
    const int exclude = cat.sayings.size() > 1 ? cat.lastIndex : -1;
    int si = IdlePicker::pickWeighted(sayWeights, exclude, m_rng());
    if (si < 0) si = 0;
    cat.lastIndex = si;
    return cat.sayings.at(si);
}

bool SayingPool::loadFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    // Same sanity cap as TipsCatalog (M13) — refuse oversized files.
    constexpr qint64 kMaxBytes = 1 * 1024 * 1024;
    if (f.size() > kMaxBytes) {
        qWarning() << "SayingPool: refusing oversized file" << path;
        return false;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (!doc.isObject()) {
        qWarning() << "SayingPool: parse error in" << path << ":" << err.errorString();
        return false;
    }
    const QJsonObject sayings = doc.object().value(QStringLiteral("sayings")).toObject();
    if (sayings.isEmpty()) return false;

    const QVector<QPair<QString, int>> table = {
        {QStringLiteral("humor"), 3},
        {QStringLiteral("encouragement"), 3},
        {QStringLiteral("coding_wisdom"), 2},
        {QStringLiteral("observation"), 2},
    };
    for (const auto &[name, weight] : table) {
        const QJsonArray arr = sayings.value(name).toArray();
        if (arr.isEmpty()) continue;
        Category cat;
        cat.name = name;
        cat.weight = weight;
        for (const QJsonValue &v : arr) {
            const QJsonObject o = v.toObject();
            const QString body = o.value(QStringLiteral("body")).toString();
            if (body.isEmpty()) continue;
            cat.sayings.append({o.value(QStringLiteral("title")).toString(), body});
        }
        if (!cat.sayings.isEmpty()) m_categories.append(cat);
    }
    return !m_categories.isEmpty();
}
