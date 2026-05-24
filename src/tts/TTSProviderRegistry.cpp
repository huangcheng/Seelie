#include "TTSProviderRegistry.h"
#include "StepFunHTTPProvider.h"
#include "MiniMaxHTTPProvider.h"
#include "AzureSpeechProvider.h"
#include "OpenAITTSProvider.h"

#include <QCoreApplication>

namespace seelie::tts {

namespace {

const QList<ProviderDescriptor>& builtInDescriptors()
{
    static const QList<ProviderDescriptor> kDescriptors = {
        ProviderDescriptor{
            TTSProviderId::StepFun,
            QStringLiteral("stepfun"),
            QStringLiteral("StepFun"),
            QStringList{QStringLiteral("token"), QStringLiteral("voice")},
            QStringList{QStringLiteral("baseUrl"), QStringLiteral("model")},
            {
                {QStringLiteral("cixingnansheng"),
                 QCoreApplication::translate("Tts", "Cixingnansheng (male)"),
                 QStringLiteral("zh-CN")},
                {QStringLiteral("linjiajiejie"),
                 QCoreApplication::translate("Tts", "Linjiajiejie (female)"),
                 QStringLiteral("zh-CN")},
            },
            {Emotion::Neutral, Emotion::Happy, Emotion::Sad,
             Emotion::Angry, Emotion::Calm, Emotion::Whisper},
            // Factory wired in Task 5.
            [](const ProviderConfig& cfg, QNetworkAccessManager* nam)
                -> std::unique_ptr<ITTSProvider> {
                return std::make_unique<StepFunHTTPProvider>(cfg, nam);
            },
        },
        ProviderDescriptor{
            TTSProviderId::MiniMax,
            QStringLiteral("minimax"),
            QStringLiteral("MiniMax"),
            QStringList{QStringLiteral("token"),
                        QStringLiteral("voice")},
            QStringList{QStringLiteral("baseUrl"), QStringLiteral("model")},
            {
                {QStringLiteral("female-shaonv"),
                 QCoreApplication::translate("Tts", "Female young (shaonv)"),
                 QStringLiteral("zh-CN")},
                {QStringLiteral("male-qn-qingse"),
                 QCoreApplication::translate("Tts", "Male qingse"),
                 QStringLiteral("zh-CN")},
            },
            {Emotion::Neutral, Emotion::Happy, Emotion::Sad,
             Emotion::Angry, Emotion::Calm, Emotion::Whisper},
            [](const ProviderConfig& cfg, QNetworkAccessManager* nam)
                -> std::unique_ptr<ITTSProvider> {
                return std::make_unique<MiniMaxHTTPProvider>(cfg, nam);
            },
        },
        ProviderDescriptor{
            TTSProviderId::Azure,
            QStringLiteral("azure"),
            QStringLiteral("Azure Speech"),
            QStringList{QStringLiteral("key"),
                        QStringLiteral("region"),
                        QStringLiteral("voice")},
            QStringList{QStringLiteral("baseUrl")},
            {
                {QStringLiteral("zh-CN-XiaoxiaoNeural"),
                 QCoreApplication::translate("Tts", "Xiaoxiao (zh-CN, female)"),
                 QStringLiteral("zh-CN")},
                {QStringLiteral("en-US-JennyNeural"),
                 QCoreApplication::translate("Tts", "Jenny (en-US, female)"),
                 QStringLiteral("en-US")},
            },
            {Emotion::Neutral, Emotion::Happy, Emotion::Sad,
             Emotion::Angry, Emotion::Calm, Emotion::Whisper},
            [](const ProviderConfig& cfg, QNetworkAccessManager* nam)
                -> std::unique_ptr<ITTSProvider> {
                return std::make_unique<AzureSpeechProvider>(cfg, nam);
            },
        },
        ProviderDescriptor{
            TTSProviderId::OpenAI,
            QStringLiteral("openai"),
            QStringLiteral("OpenAI"),
            QStringList{QStringLiteral("token"), QStringLiteral("voice")},
            QStringList{QStringLiteral("baseUrl"), QStringLiteral("model")},
            {
                {QStringLiteral("alloy"),    QStringLiteral("Alloy"),   QStringLiteral("en")},
                {QStringLiteral("nova"),     QStringLiteral("Nova"),    QStringLiteral("en")},
                {QStringLiteral("shimmer"),  QStringLiteral("Shimmer"), QStringLiteral("en")},
                {QStringLiteral("echo"),     QStringLiteral("Echo"),    QStringLiteral("en")},
            },
            {Emotion::Neutral},
            [](const ProviderConfig& cfg, QNetworkAccessManager* nam)
                -> std::unique_ptr<ITTSProvider> {
                return std::make_unique<OpenAITTSProvider>(cfg, nam);
            },
        },
    };
    return kDescriptors;
}

} // namespace

const QList<ProviderDescriptor>& TTSProviderRegistry::descriptors()
{
    return builtInDescriptors();
}

const ProviderDescriptor* TTSProviderRegistry::find(TTSProviderId id)
{
    for (const auto& d : descriptors())
        if (d.id == id) return &d;
    return nullptr;
}

const ProviderDescriptor* TTSProviderRegistry::findByStableId(const QString& stableId)
{
    for (const auto& d : descriptors())
        if (d.stableId == stableId) return &d;
    return nullptr;
}

} // namespace seelie::tts
