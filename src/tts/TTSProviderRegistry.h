#ifndef SEELIE_TTS_PROVIDER_REGISTRY_H
#define SEELIE_TTS_PROVIDER_REGISTRY_H

#include "ITTSProvider.h"
#include "ProviderConfig.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>

class QNetworkAccessManager;

namespace seelie::tts {

enum class TTSProviderId {
    StepFun,
    MiniMax,
    Azure,
    OpenAI,
};

struct VoicePreset {
    QString id;            // "cixingnansheng"
    QString displayName;   // "Cixingnansheng (male)"
    QString language;      // "zh-CN"
};

struct ProviderDescriptor {
    TTSProviderId      id;
    QString            stableId;          // "stepfun" — used in QSettings keys
    QString            displayName;       // "StepFun"
    QStringList        requiredFields;    // {"token", "voice"}
    QStringList        optionalFields;    // {"baseUrl", "model"}
    QList<VoicePreset> voiceCatalog;
    QList<Emotion>     supportedEmotions;
    std::function<std::unique_ptr<ITTSProvider>(
        const ProviderConfig&,
        QNetworkAccessManager*)> factory;
};

// All four adapters are registered at static-init time.
class TTSProviderRegistry {
public:
    static const QList<ProviderDescriptor>& descriptors();
    static const ProviderDescriptor* find(TTSProviderId id);
    static const ProviderDescriptor* findByStableId(const QString& stableId);
};

} // namespace seelie::tts

#endif // SEELIE_TTS_PROVIDER_REGISTRY_H
