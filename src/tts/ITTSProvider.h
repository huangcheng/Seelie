#ifndef SEELIE_ITTSPROVIDER_H
#define SEELIE_ITTSPROVIDER_H

#include <QByteArray>
#include <QString>
#include <functional>
#include <memory>
#include <optional>

namespace seelie::tts {

enum class Emotion {
    Neutral,
    Happy,
    Sad,
    Angry,
    Calm,
    Whisper,
};

struct SpeakOptions {
    std::optional<Emotion> emotion;
    std::optional<float>   rate;          // 0.5 .. 2.0
    QString                languageHint;  // BCP-47, e.g. "zh-CN"
};

struct SynthesisRequest {
    QString      text;
    SpeakOptions options;
};

struct SynthesisResult {
    QByteArray audio;
    QString    mimeType;     // "audio/mpeg", "audio/wav", "audio/x-pcm"
    int        sampleRate;   // 0 = inferred from container
};

enum class TtsErrorKind {
    Network,        // transport / DNS / TLS / 5xx after retries
    AuthFailed,     // 401 / 403
    BadRequest,     // 400 / 422 — payload rejected
    RateLimited,    // 429 (after we honored Retry-After)
    Cancelled,      // request was superseded
    Unsupported,    // adapter cannot service request (e.g. missing field)
    Unknown,
};

struct TtsError {
    TtsErrorKind kind = TtsErrorKind::Unknown;
    int          httpStatus = 0;        // 0 if not HTTP
    QString      message;               // human-readable
};

using RequestHandle = quint64;          // 0 = invalid; provider-assigned otherwise

class ITtsProvider {
public:
    virtual ~ITtsProvider() = default;

    // Must be non-blocking. Exactly one of onSuccess/onError must fire on
    // the engine's thread, UNLESS cancel() is called first — after cancel(),
    // neither callback fires for that handle. Implementations achieve this
    // by clearing callback pointers before aborting the underlying QNetworkReply.
    virtual RequestHandle synthesize(
        const SynthesisRequest& req,
        std::function<void(SynthesisResult)> onSuccess,
        std::function<void(TtsError)> onError) = 0;

    virtual void cancel(RequestHandle handle) = 0;
};

} // namespace seelie::tts

#endif
