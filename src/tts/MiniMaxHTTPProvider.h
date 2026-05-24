#ifndef SEELIE_TTS_MINIMAX_HTTP_PROVIDER_H
#define SEELIE_TTS_MINIMAX_HTTP_PROVIDER_H

#include "ITTSProvider.h"
#include "ProviderConfig.h"

#include <QHash>
#include <QObject>
#include <QPointer>

class QNetworkAccessManager;
class QNetworkReply;

namespace seelie::tts {

class MiniMaxHTTPProvider : public QObject, public ITTSProvider {
    Q_OBJECT
public:
    MiniMaxHTTPProvider(ProviderConfig cfg, QNetworkAccessManager* nam,
                        QObject* parent = nullptr);

    RequestHandle synthesize(
        const SynthesisRequest& req,
        std::function<void(SynthesisResult)> onSuccess,
        std::function<void(TTSError)> onError) override;

    void cancel(RequestHandle handle) override;

private:
    struct InFlight {
        QPointer<QNetworkReply> reply;
        std::function<void(SynthesisResult)> onSuccess;
        std::function<void(TTSError)> onError;
    };

    ProviderConfig m_cfg;
    QNetworkAccessManager* m_nam;
    QHash<RequestHandle, InFlight> m_inFlight;
    RequestHandle m_nextHandle = 1;
};

} // namespace seelie::tts

#endif // SEELIE_TTS_MINIMAX_HTTP_PROVIDER_H
