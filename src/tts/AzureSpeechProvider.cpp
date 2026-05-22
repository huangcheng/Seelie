#include "AzureSpeechProvider.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace seelie::tts {

namespace {

const char* emotionToStyle(Emotion e)
{
    switch (e) {
        case Emotion::Happy:   return "cheerful";
        case Emotion::Sad:     return "sad";
        case Emotion::Angry:   return "angry";
        case Emotion::Calm:    return "calm";
        case Emotion::Whisper: return "whispering";
        case Emotion::Neutral: return nullptr;
    }
    return nullptr;
}

QUrl buildUrl(const ProviderConfig& cfg)
{
    // User-provided baseUrl wins (used verbatim). Otherwise derive from region.
    const QString baseUrl = cfg.get("baseUrl");
    if (!baseUrl.isEmpty()) return QUrl(baseUrl);

    // Test hook: takes precedence only when no real baseUrl is set.
    const QString override_ = cfg.get("azureEndpointOverride");
    if (!override_.isEmpty()) {
        QString base = override_;
        if (base.endsWith('/')) base.chop(1);
        return QUrl(base + "/cognitiveservices/v1");
    }
    const QString region = cfg.get("region", "eastus");
    return QUrl(QStringLiteral("https://%1.tts.speech.microsoft.com/cognitiveservices/v1")
                .arg(region));
}

QString xmlEscape(const QString& s)
{
    QString out = s;
    out.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;");
    return out;
}

QString buildSsml(const QString& text, const QString& voice,
                   const QString& langHint, std::optional<Emotion> emotion)
{
    const QString lang = xmlEscape(langHint.isEmpty()
        ? QStringLiteral("en-US") : langHint);
    const QString voiceName = xmlEscape(voice);
    QString inner = xmlEscape(text);
    if (emotion.has_value()) {
        if (const char* style = emotionToStyle(*emotion)) {
            inner = QStringLiteral(
                "<mstts:express-as style=\"%1\">%2</mstts:express-as>"
            ).arg(QString::fromLatin1(style), inner);
        }
    }
    return QStringLiteral(
        "<speak version=\"1.0\" xmlns=\"http://www.w3.org/2001/10/synthesis\""
        " xmlns:mstts=\"https://www.w3.org/2001/mstts\""
        " xml:lang=\"%1\">"
        "<voice name=\"%2\">%3</voice>"
        "</speak>"
    ).arg(lang, voiceName, inner);
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

AzureSpeechProvider::AzureSpeechProvider(ProviderConfig cfg,
                                          QNetworkAccessManager* nam,
                                          QObject* parent)
    : QObject(parent), m_cfg(std::move(cfg)), m_nam(nam) {}

RequestHandle AzureSpeechProvider::synthesize(
    const SynthesisRequest& req,
    std::function<void(SynthesisResult)> onSuccess,
    std::function<void(TtsError)> onError)
{
    const QString ssml = buildSsml(req.text,
                                   m_cfg.get("voice"),
                                   req.options.languageHint,
                                   req.options.emotion);

    QNetworkRequest qreq(buildUrl(m_cfg));
    qreq.setRawHeader("Ocp-Apim-Subscription-Key", m_cfg.get("key").trimmed().toUtf8());
    qreq.setRawHeader("Content-Type", "application/ssml+xml");
    qreq.setRawHeader("X-Microsoft-OutputFormat", "audio-24khz-48kbitrate-mono-mp3");
    qreq.setRawHeader("User-Agent", "Seelie");
    qreq.setTransferTimeout(30000);

    QNetworkReply* reply = m_nam->post(qreq, ssml.toUtf8());
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
            inflight.onError({TtsErrorKind::Network, 0, r->errorString()});
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

void AzureSpeechProvider::cancel(RequestHandle handle)
{
    auto it = m_inFlight.find(handle);
    if (it == m_inFlight.end()) return;
    QNetworkReply *reply = it.value().reply;
    // Erase before abort: see StepFunHttpProvider::cancel for rationale (H8).
    m_inFlight.erase(it);
    if (reply) {
        reply->abort();
        reply->deleteLater();
    }
}

} // namespace seelie::tts
