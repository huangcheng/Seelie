#ifndef SEELIE_TTS_OPENAITTSPROVIDER_H
#define SEELIE_TTS_OPENAITTSPROVIDER_H

#include "ITtsProvider.h"
#include "ProviderConfig.h"

#include <QHash>
#include <QObject>
#include <QPointer>

class QNetworkAccessManager;
class QNetworkReply;

namespace seelie::tts {

class OpenAiTtsProvider : public QObject, public ITtsProvider {
    Q_OBJECT
public:
    OpenAiTtsProvider(ProviderConfig cfg, QNetworkAccessManager* nam,
                     QObject* parent = nullptr);

    RequestHandle synthesize(
        const SynthesisRequest& req,
        std::function<void(SynthesisResult)> onSuccess,
        std::function<void(TtsError)> onError) override;

    void cancel(RequestHandle handle) override;

private:
    struct InFlight {
        QPointer<QNetworkReply> reply;
        std::function<void(SynthesisResult)> onSuccess;
        std::function<void(TtsError)> onError;
    };
    ProviderConfig m_cfg;
    QNetworkAccessManager* m_nam;
    QHash<RequestHandle, InFlight> m_inFlight;
    RequestHandle m_nextHandle = 1;
};

} // namespace seelie::tts
#endif
