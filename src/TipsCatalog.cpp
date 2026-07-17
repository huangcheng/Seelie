#include "TipsCatalog.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRandomGenerator>
#include <QDebug>

namespace {
// Single hook for all string substitutions surfaced by the catalog.
// Today only {version} is recognised; add more keys here as needed
// (build_date, author, etc.) and they show up everywhere automatically.
TipsCatalog::Tip substitute(TipsCatalog::Tip t)
{
    static const QString kVersionMarker = QStringLiteral("{version}");
    static const QString kVersionValue  = QStringLiteral(PROJECT_VERSION);
    if (t.title.contains(kVersionMarker)) t.title.replace(kVersionMarker, kVersionValue);
    if (t.body.contains(kVersionMarker))  t.body.replace(kVersionMarker, kVersionValue);
    return t;
}
} // namespace

TipsCatalog &TipsCatalog::instance()
{
    static TipsCatalog s_inst;
    return s_inst;
}

TipsCatalog::TipsCatalog()
    : m_locale(QStringLiteral("en"))
{
    // Always pre-load English so callers have a fallback even before the
    // first setLocale() call.
    m_bundles.insert(QStringLiteral("en"), loadBundle(QStringLiteral("en")));
}

void TipsCatalog::setLocale(const QString &locale)
{
    const QString loc = locale.isEmpty() ? QStringLiteral("en") : locale;
    if (loc == m_locale && m_bundles.contains(loc)) {
        return;
    }
    if (!m_bundles.contains(loc)) {
        m_bundles.insert(loc, loadBundle(loc));
    }
    m_locale = loc;
}

TipsCatalog::Tip TipsCatalog::eventTip(const QString &eventName) const
{
    const Bundle &active = activeBundle();
    if (auto it = active.events.constFind(eventName); it != active.events.constEnd()) {
        return substitute(it.value());
    }
    // Fall back to English for events the active locale didn't translate.
    const Bundle &fb = fallbackBundle();
    if (auto it = fb.events.constFind(eventName); it != fb.events.constEnd()) {
        return substitute(it.value());
    }
    return {};
}

TipsCatalog::Tip TipsCatalog::randomGreeting() const
{
    const Bundle &active = activeBundle();
    if (!active.greetings.isEmpty()) {
        const int idx = QRandomGenerator::global()->bounded(active.greetings.size());
        return substitute(active.greetings.at(idx));
    }
    const Bundle &fb = fallbackBundle();
    if (!fb.greetings.isEmpty()) {
        const int idx = QRandomGenerator::global()->bounded(fb.greetings.size());
        return substitute(fb.greetings.at(idx));
    }
    return {};
}

TipsCatalog::Tip TipsCatalog::touchLine(const QString &gesture) const
{
    const Bundle &active = activeBundle();
    if (auto it = active.touch.constFind(gesture);
        it != active.touch.constEnd() && !it.value().isEmpty()) {
        return substitute(it.value().at(
            QRandomGenerator::global()->bounded(it.value().size())));
    }
    const Bundle &fb = fallbackBundle();
    if (auto it = fb.touch.constFind(gesture);
        it != fb.touch.constEnd() && !it.value().isEmpty()) {
        return substitute(it.value().at(
            QRandomGenerator::global()->bounded(it.value().size())));
    }
    return {};
}

TipsCatalog::Tip TipsCatalog::message(const QString &id) const
{
    const Bundle &active = activeBundle();
    if (auto it = active.messages.constFind(id); it != active.messages.constEnd()) {
        return substitute(it.value());
    }
    const Bundle &fb = fallbackBundle();
    if (auto it = fb.messages.constFind(id); it != fb.messages.constEnd()) {
        return substitute(it.value());
    }
    return {};
}

const TipsCatalog::Bundle &TipsCatalog::activeBundle() const
{
    if (auto it = m_bundles.constFind(m_locale); it != m_bundles.constEnd()) {
        return it.value();
    }
    return fallbackBundle();
}

const TipsCatalog::Bundle &TipsCatalog::fallbackBundle() const
{
    static const Bundle empty;
    auto it = m_bundles.constFind(QStringLiteral("en"));
    return it != m_bundles.constEnd() ? it.value() : empty;
}

TipsCatalog::Bundle TipsCatalog::loadBundle(const QString &locale)
{
    Bundle b;
    const QString path = QStringLiteral(":/i18n/i18n/tips.%1.json").arg(locale);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (locale != QLatin1String("en")) {
            qDebug() << "TipsCatalog: no bundled tips file for locale" << locale
                     << "— will fall back to en for missing keys";
        }
        return b;
    }
    // Sanity cap. Tips files are < 100 KB today and live in qrc:/, but a
    // future change could read from disk; with no upper bound, a corrupted
    // or hostile file could OOM the GUI thread on startup. M13.
    constexpr qint64 kMaxTipsBytes = 1 * 1024 * 1024;
    if (f.size() > kMaxTipsBytes) {
        qWarning() << "TipsCatalog: refusing oversized tips file" << path
                   << "size=" << f.size();
        return b;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (doc.isNull() || !doc.isObject()) {
        qWarning() << "TipsCatalog: parse error in" << path << ":" << err.errorString();
        return b;
    }
    const QJsonObject root = doc.object();
    const QJsonObject events = root.value(QStringLiteral("events")).toObject();
    for (auto it = events.begin(); it != events.end(); ++it) {
        const QJsonObject e = it.value().toObject();
        b.events.insert(it.key(),
                        Tip{e.value(QStringLiteral("title")).toString(),
                            e.value(QStringLiteral("body")).toString()});
    }
    const QJsonArray greetings = root.value(QStringLiteral("greetings")).toArray();
    for (const QJsonValue &v : greetings) {
        const QJsonObject g = v.toObject();
        b.greetings.append(Tip{g.value(QStringLiteral("title")).toString(),
                                g.value(QStringLiteral("body")).toString()});
    }
    const QJsonObject messages = root.value(QStringLiteral("messages")).toObject();
    for (auto it = messages.begin(); it != messages.end(); ++it) {
        const QJsonObject m = it.value().toObject();
        b.messages.insert(it.key(),
                          Tip{m.value(QStringLiteral("title")).toString(),
                              m.value(QStringLiteral("body")).toString()});
    }
    const QJsonObject touch = root.value(QStringLiteral("touch")).toObject();
    for (auto it = touch.begin(); it != touch.end(); ++it) {
        QVector<Tip> lines;
        const QJsonArray arr = it.value().toArray();
        for (const QJsonValue &v : arr) {
            const QJsonObject t = v.toObject();
            lines.append(Tip{t.value(QStringLiteral("title")).toString(),
                             t.value(QStringLiteral("body")).toString()});
        }
        b.touch.insert(it.key(), lines);
    }
    qDebug() << "TipsCatalog: loaded" << b.events.size() << "event tips +"
             << b.greetings.size() << "greetings +"
             << b.messages.size() << "messages for locale" << locale;
    return b;
}
