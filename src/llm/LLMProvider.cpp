#include "LLMProvider.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QUrl>
#include <memory>

LLMProvider::LLMProvider(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

LLMProvider::~LLMProvider() = default;

void LLMProvider::setProfile(const LLMProfile &profile)
{
    m_profile = profile;
}

bool LLMProvider::isConfigured() const
{
    return !m_profile.baseUrl.isEmpty()
        && !m_profile.apiKey.isEmpty()
        && !m_profile.model.isEmpty();
}

void LLMProvider::generate(const QString &system, const QString &user, ResultCallback cb)
{
    if (!isConfigured()) {
        cb({ false, {}, QStringLiteral("provider not configured"), 0, 0 });
        return;
    }

    QNetworkReply *reply = nullptr;
    switch (m_profile.protocol) {
    case LLMProfile::Protocol::OpenAIChat:
        reply = sendOpenAiChat(system, user); break;
    case LLMProfile::Protocol::OpenAIResponses:
        reply = sendOpenAiResponses(system, user); break;
    case LLMProfile::Protocol::AnthropicMessages:
        reply = sendAnthropicMessages(system, user); break;
    }
    if (!reply) {
        cb({ false, {}, QStringLiteral("unknown protocol"), 0, 0 });
        return;
    }
    wireReply(reply, std::move(cb));
}

void LLMProvider::generateBatch(const QString &system, const QString &user, int n, BatchCallback cb)
{
    const QString batchUser = user
        + QStringLiteral("\n\nRespond ONLY with a JSON array of exactly %1 strings. "
                         "No surrounding prose, no markdown code fences, no keys — "
                         "just the JSON array. Example: [\"line one\", \"line two\"]").arg(n);

    generate(system, batchUser, [cb = std::move(cb), n](LLMResult r) {
        QVector<QString> out;
        if (!r.ok) { cb(out); return; }

        // Strip code fences if a model returned them despite instructions.
        QString text = r.text.trimmed();
        if (text.startsWith("```")) {
            int firstNl = text.indexOf('\n');
            int closing = text.lastIndexOf("```");
            if (firstNl >= 0 && closing > firstNl) {
                text = text.mid(firstNl + 1, closing - firstNl - 1).trimmed();
            }
        }

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isArray()) {
            qWarning() << "LLMProvider::generateBatch: JSON parse failed:" << err.errorString()
                       << "head:" << text.left(200);
            cb(out);
            return;
        }
        const QJsonArray arr = doc.array();
        for (const QJsonValue &v : arr) {
            if (v.isString()) out.append(v.toString());
        }
        Q_UNUSED(n);
        cb(out);
    });
}

// --- OpenAI Chat -----------------------------------------------------------

QNetworkReply *LLMProvider::sendOpenAiChat(const QString &system, const QString &user)
{
    QUrl url(m_profile.baseUrl + QStringLiteral("/chat/completions"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", ("Bearer " + m_profile.apiKey).toUtf8());

    QJsonObject body;
    body["model"] = m_profile.model;
    QJsonArray msgs;
    msgs.append(QJsonObject{ {"role","system"}, {"content", system} });
    msgs.append(QJsonObject{ {"role","user"},   {"content", user} });
    body["messages"] = msgs;

    return m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

LLMResult LLMProvider::parseOpenAiChat(const QByteArray &body)
{
    LLMResult r;
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        r.error = "non-JSON response"; return r;
    }
    const QJsonObject obj = doc.object();
    const QJsonArray choices = obj.value("choices").toArray();
    if (choices.isEmpty()) { r.error = "no choices in response"; return r; }
    const QJsonObject first = choices.first().toObject();
    if (!first.contains(QStringLiteral("message")) || !first.value(QStringLiteral("message")).isObject()) {
        r.error = "missing message field in choice";
        return r;
    }
    r.text = first.value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString();
    if (r.text.isEmpty()) { r.error = "empty content"; return r; }

    const QJsonObject usage = obj.value("usage").toObject();
    r.tokensIn = usage.value("prompt_tokens").toInt();
    r.tokensOut = usage.value("completion_tokens").toInt();
    r.ok = true;
    return r;
}

// --- Stubs for the other two protocols (filled in Task 4) -----------------

QNetworkReply *LLMProvider::sendOpenAiResponses(const QString &, const QString &) { return nullptr; }
QNetworkReply *LLMProvider::sendAnthropicMessages(const QString &, const QString &) { return nullptr; }
LLMResult LLMProvider::parseOpenAiResponses(const QByteArray &) { return {}; }
LLMResult LLMProvider::parseAnthropicMessages(const QByteArray &) { return {}; }

// --- Common reply wiring ---------------------------------------------------

void LLMProvider::wireReply(QNetworkReply *reply, ResultCallback cb)
{
    auto *timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    timeout->setInterval(m_timeoutMs);

    auto protocol = m_profile.protocol;
    auto fired = std::make_shared<bool>(false);

    connect(timeout, &QTimer::timeout, reply, [reply, cb, fired]() {
        if (*fired) return;
        *fired = true;
        reply->abort();
        cb({ false, {}, QStringLiteral("timeout"), 0, 0 });
        reply->deleteLater();
    });

    connect(reply, &QNetworkReply::finished, this, [reply, cb, fired, protocol]() {
        if (*fired) return;
        *fired = true;
        if (reply->error() != QNetworkReply::NoError) {
            cb({ false, {}, reply->errorString(), 0, 0 });
            reply->deleteLater();
            return;
        }
        const QByteArray body = reply->readAll();
        LLMResult r;
        switch (protocol) {
        case LLMProfile::Protocol::OpenAIChat:        r = parseOpenAiChat(body); break;
        case LLMProfile::Protocol::OpenAIResponses:   r = parseOpenAiResponses(body); break;
        case LLMProfile::Protocol::AnthropicMessages: r = parseAnthropicMessages(body); break;
        }
        cb(std::move(r));
        reply->deleteLater();
    });

    timeout->start();
}
