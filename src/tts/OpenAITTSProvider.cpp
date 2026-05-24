#include "OpenAITTSProvider.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace seelie::tts {

namespace {

QUrl buildUrl(const ProviderConfig& cfg)
{
    return QUrl(cfg.get("baseUrl", "https://api.openai.com/v1/audio/speech"));
}

TTSErrorKind classifyHttp(int status)
{
    if (status == 401 || status == 403) return TTSErrorKind::AuthFailed;
    if (status == 429)                  return TTSErrorKind::RateLimited;
    if (status >= 500)                  return TTSErrorKind::Network;
    if (status >= 400)                  return TTSErrorKind::BadRequest;
    return TTSErrorKind::Unknown;
}

} // namespace

OpenAITTSProvider::OpenAITTSProvider(ProviderConfig cfg,
                                     QNetworkAccessManager* nam,
                                     QObject* parent)
    : QObject(parent), m_cfg(std::move(cfg)), m_nam(nam) {}

RequestHandle OpenAITTSProvider::synthesize(
    const SynthesisRequest& req,
    std::function<void(SynthesisResult)> onSuccess,
    std::function<void(TTSError)> onError)
{
    QJsonObject body;
    body["model"]           = m_cfg.get("model", "gpt-4o-mini-tts");
    body["input"]           = req.text;
    body["voice"]           = m_cfg.get("voice");
    body["response_format"] = "mp3";
    if (req.options.rate.has_value())
        body["speed"] = *req.options.rate;
    // emotion intentionally dropped — OpenAI tts-1 family doesn't expose it.

    QNetworkRequest qreq(buildUrl(m_cfg));
    qreq.setRawHeader("Authorization",
                      "Bearer " + m_cfg.get("token").trimmed().toUtf8());
    qreq.setRawHeader("Content-Type", "application/json");
    qreq.setTransferTimeout(30000);

    QNetworkReply* reply =
        m_nam->post(qreq, QJsonDocument(body).toJson(QJsonDocument::Compact));
    const RequestHandle handle = m_nextHandle++;
    m_inFlight.insert(handle, {reply, onSuccess, onError});

    connect(reply, &QNetworkReply::finished, this, [this, handle]() {
        auto it = m_inFlight.find(handle);
        if (it == m_inFlight.end()) return;
        InFlight inflight = it.value();
        m_inFlight.erase(it);

        QNetworkReply* r = inflight.reply;
        if (!r) return;
        r->deleteLater();

        const int status = r->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (r->error() != QNetworkReply::NoError && status == 0) {
            inflight.onError({TTSErrorKind::Network, 0, r->errorString()});
            return;
        }
        if (status >= 400) {
            inflight.onError({classifyHttp(status), status,
                              QString::fromUtf8(r->readAll())});
            return;
        }
        inflight.onSuccess({r->readAll(), "audio/mpeg", 0});
    });

    return handle;
}

void OpenAITTSProvider::cancel(RequestHandle handle)
{
    auto it = m_inFlight.find(handle);
    if (it == m_inFlight.end()) return;
    QNetworkReply *reply = it.value().reply;
    // Erase before abort: see StepFunHTTPProvider::cancel for rationale (H8).
    m_inFlight.erase(it);
    if (reply) {
        reply->abort();
        reply->deleteLater();
    }
}

} // namespace seelie::tts
