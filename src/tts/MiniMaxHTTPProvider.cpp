#include "MiniMaxHTTPProvider.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace seelie::tts {

namespace {

const char* emotionToString(Emotion e)
{
    switch (e) {
        case Emotion::Happy:   return "happy";
        case Emotion::Sad:     return "sad";
        case Emotion::Angry:   return "angry";
        case Emotion::Calm:    return "calm";
        case Emotion::Whisper: return "whisper";
        case Emotion::Neutral: return nullptr;  // omit
    }
    return nullptr;
}

QUrl buildUrl(const ProviderConfig& cfg)
{
    // baseUrl is used verbatim. If the user's account requires a GroupId
    // query parameter, they include it directly in the URL — same model
    // as StepFun / OpenAI.
    return QUrl(cfg.get("baseUrl", "https://api.minimaxi.com/v1/t2a_v2"));
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

MiniMaxHTTPProvider::MiniMaxHTTPProvider(ProviderConfig cfg,
                                         QNetworkAccessManager* nam,
                                         QObject* parent)
    : QObject(parent), m_cfg(std::move(cfg)), m_nam(nam) {}

RequestHandle MiniMaxHTTPProvider::synthesize(
    const SynthesisRequest& req,
    std::function<void(SynthesisResult)> onSuccess,
    std::function<void(TTSError)> onError)
{
    QJsonObject body;
    body["model"]    = m_cfg.get("model", "speech-02-hd");
    body["text"]     = req.text;
    QJsonObject voiceSetting;
    voiceSetting["voice_id"] = m_cfg.get("voice");
    if (req.options.rate.has_value())
        voiceSetting["speed"] = *req.options.rate;
    body["voice_setting"] = voiceSetting;
    if (req.options.emotion.has_value()) {
        if (const char* s = emotionToString(*req.options.emotion))
            body["emotion"] = QString::fromUtf8(s);
    }

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

        QJsonDocument doc = QJsonDocument::fromJson(r->readAll());
        if (!doc.isObject()) {
            inflight.onError({TTSErrorKind::Unknown, status,
                              QStringLiteral("invalid JSON envelope")});
            return;
        }
        QJsonObject root = doc.object();
        const int respCode =
            root.value("base_resp").toObject().value("status_code").toInt();
        if (respCode != 0) {
            inflight.onError({TTSErrorKind::BadRequest, status,
                              root.value("base_resp").toObject()
                                  .value("status_msg").toString()});
            return;
        }
        const QString hexAudio =
            root.value("data").toObject().value("audio").toString();
        const QByteArray audio = QByteArray::fromHex(hexAudio.toLatin1());
        inflight.onSuccess({audio, "audio/mpeg", 0});
    });

    return handle;
}

void MiniMaxHTTPProvider::cancel(RequestHandle handle)
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