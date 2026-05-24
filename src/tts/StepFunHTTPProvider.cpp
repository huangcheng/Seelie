#include "StepFunHttpProvider.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace seelie::tts {

namespace {

const char* emotionToInstruction(Emotion e)
{
    switch (e) {
        case Emotion::Happy:   return "\xE8\xAF\xAD\xE6\xB0\x94\xE6\x84\x89\xE5\xBF\xAB"; // 语气愉快
        case Emotion::Sad:     return "\xE8\xAF\xAD\xE6\xB0\x94\xE4\xBD\x8E\xE6\xB2\x89\xEF\xBC\x8C\xE6\x82\xB2\xE4\xBC\xA4"; // 语气低沉，悲伤
        case Emotion::Angry:   return "\xE8\xAF\xAD\xE6\xB0\x94\xE4\xB8\xA5\xE5\x8E\x89"; // 语气严厉
        case Emotion::Calm:    return "\xE8\xAF\xAD\xE6\xB0\x94\xE5\xB9\xB3\xE9\x9D\x99"; // 语气平静
        case Emotion::Whisper: return "\xE4\xBD\x8E\xE5\xA3\xB0\xE8\x80\xB3\xE8\xAF\xAD"; // 低声耳语
        case Emotion::Neutral:
        default:
            return nullptr;
    }
}

QUrl buildUrl(const ProviderConfig& cfg)
{
    // Full endpoint URL, used verbatim. Default is the canonical StepFun
    // endpoint per https://platform.stepfun.com/docs/zh/step-plan/integrations/audio-api
    return QUrl(cfg.get("baseUrl",
        "https://api.stepfun.com/step_plan/v1/audio/speech"));
}

TtsErrorKind classifyHttp(int status)
{
    if (status == 401 || status == 403) return TtsErrorKind::AuthFailed;
    if (status == 429)                  return TtsErrorKind::RateLimited;
    if (status >= 500)                  return TtsErrorKind::Network;
    if (status >= 400)                  return TtsErrorKind::BadRequest;
    return TtsErrorKind::Unknown;
}

} // namespace

StepFunHttpProvider::StepFunHttpProvider(ProviderConfig cfg,
                                         QNetworkAccessManager* nam,
                                         QObject* parent)
    : QObject(parent), m_cfg(std::move(cfg)), m_nam(nam)
{
}

RequestHandle StepFunHttpProvider::synthesize(
    const SynthesisRequest& req,
    std::function<void(SynthesisResult)> onSuccess,
    std::function<void(TtsError)> onError)
{
    QJsonObject body;
    body["model"]           = m_cfg.get("model", "stepaudio-2.5-tts");
    body["input"]           = req.text;
    body["voice"]           = m_cfg.get("voice");
    body["response_format"] = "mp3";
    if (req.options.rate.has_value())
        body["speed_ratio"] = *req.options.rate;
    if (req.options.emotion.has_value()) {
        const char* instr = emotionToInstruction(*req.options.emotion);
        if (instr) body["instruction"] = QString::fromUtf8(instr);
    }

    QNetworkRequest qreq(buildUrl(m_cfg));
    qreq.setRawHeader("Authorization",
                      "Bearer " + m_cfg.get("token").trimmed().toUtf8());
    qreq.setRawHeader("Content-Type", "application/json");
    // Without a transfer timeout the engine debounces all subsequent
    // utterances behind a single hung TLS handshake. 30s is generous —
    // a real synthesis usually replies in under 2s.
    qreq.setTransferTimeout(30000);

    QNetworkReply* reply =
        m_nam->post(qreq, QJsonDocument(body).toJson(QJsonDocument::Compact));
    const RequestHandle handle = m_nextHandle++;
    m_inFlight.insert(handle, {reply, onSuccess, onError});

    connect(reply, &QNetworkReply::finished, this, [this, handle]() {
        auto it = m_inFlight.find(handle);
        if (it == m_inFlight.end()) return;        // cancelled
        InFlight inflight = it.value();
        m_inFlight.erase(it);

        QNetworkReply* r = inflight.reply;
        if (!r) return;
        r->deleteLater();

        const int status = r->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (r->error() != QNetworkReply::NoError && status == 0) {
            inflight.onError({TtsErrorKind::Network, 0, r->errorString()});
            return;
        }
        if (status >= 400) {
            inflight.onError({classifyHttp(status), status,
                              QString::fromUtf8(r->readAll())});
            return;
        }

        QString mime = r->header(QNetworkRequest::ContentTypeHeader).toString();
        if (mime.isEmpty()) mime = "audio/mpeg";
        inflight.onSuccess({r->readAll(), mime, 0});
    });

    return handle;
}

void StepFunHttpProvider::cancel(RequestHandle handle)
{
    auto it = m_inFlight.find(handle);
    if (it == m_inFlight.end()) return;
    QNetworkReply *reply = it.value().reply;
    // Erase BEFORE abort. QNetworkReply::abort() can emit finished()
    // synchronously, re-entering the lambda above; if the entry is still
    // in m_inFlight when the lambda runs, the lambda erases it and our
    // post-abort erase below triggers a use-after-free on the iterator.
    // After erase, the lambda's m_inFlight.find() misses and bails. H8.
    m_inFlight.erase(it);
    if (reply) {
        reply->abort();
        reply->deleteLater();
    }
}

} // namespace seelie::tts
