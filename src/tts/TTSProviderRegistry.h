#ifndef SEELIE_TTS_PROVIDERREGISTRY_H
#define SEELIE_TTS_PROVIDERREGISTRY_H

#include "ITtsProvider.h"
#include "ProviderConfig.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>

class QNetworkAccessManager;

namespace seelie::tts {

enum class TtsProviderId {
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
    TtsProviderId      id;
    QString            stableId;          // "stepfun" — used in QSettings keys
    QString            displayName;       // "StepFun"
    QStringList        requiredFields;    // {"token", "voice"}
    QStringList        optionalFields;    // {"baseUrl", "model"}
    QList<VoicePreset> voiceCatalog;
    QList<Emotion>     supportedEmotions;
    std::function<std::unique_ptr<ITtsProvider>(
        const ProviderConfig&,
        QNetworkAccessManager*)> factory;
};

// All four adapters are registered at static-init time.
class TtsProviderRegistry {
public:
    static const QList<ProviderDescriptor>& descriptors();
    static const ProviderDescriptor* find(TtsProviderId id);
    static const ProviderDescriptor* findByStableId(const QString& stableId);
};

} // namespace seelie::tts

#endif
