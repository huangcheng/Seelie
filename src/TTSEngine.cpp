#include "TTSEngine.h"
#include "ConfigManager.h"

#include <QAudioDecoder>
#include <QBuffer>
#include <QDateTime>
#include <QDebug>
#include <QLoggingCategory>
#include <QMediaDevices>
#include <QNetworkAccessManager>
#include <QRegularExpression>

Q_LOGGING_CATEGORY(lcTts, "seelie.tts")

using namespace seelie::tts;

namespace {

// Redact known sensitive patterns from error messages before logging.
// Prevents accidental leakage of API tokens / subscription keys into logs.
QString redactSensitiveInfo(const QString &msg)
{
    QString result = msg;
    result.replace(QRegularExpression(
        QStringLiteral(R"(Bearer\s+\S+)")), QStringLiteral("Bearer [REDACTED]"));
    result.replace(QRegularExpression(
        QStringLiteral(R"(Ocp-Apim-Subscription-Key:\s*\S+)")),
        QStringLiteral("Ocp-Apim-Subscription-Key: [REDACTED]"));
    return result;
}

} // namespace

TTSEngine::TTSEngine(ConfigManager *config, QObject *parent)
    : QObject(parent), m_config(config)
{
    m_thread = new QThread(this);
    moveToThread(m_thread);

    connect(m_thread, &QThread::started, this, &TTSEngine::initOnThread);

    if (m_config) {
        connect(m_config, &ConfigManager::ttsActiveProviderChanged,
                this, &TTSEngine::onActiveProviderChanged,
                Qt::QueuedConnection);
        connect(m_config, &ConfigManager::ttsProviderFieldChanged,
                this, &TTSEngine::onProviderFieldChanged,
                Qt::QueuedConnection);
    }
}

TTSEngine::~TTSEngine()
{
    // Engine has worker-thread affinity (moveToThread in the ctor). If we
    // reach the destructor without stop() having returned the engine to the
    // main thread, fall back to a best-effort stop here. After stop()
    // returns, this object's affinity is the main thread, so ~QObject and
    // ~unique_ptr<TtsVoiceCache> destroying their children on the main
    // thread is correct.
    stop();
}

void TTSEngine::start()
{
    if (m_thread && !m_thread->isRunning())
        m_thread->start();
}

void TTSEngine::stop()
{
    if (!m_thread || !m_thread->isRunning()) return;

    // Cleanup must happen on the engine thread (m_audioSink and friends
    // were created there and ~QAudioSink touches CoreAudio state that's
    // pinned to that thread). Queue the cleanup, then post quit() — both
    // events drain in FIFO order, so the event loop exits AFTER cleanup
    // runs.
    //
    // We deliberately do NOT use BlockingQueuedConnection. If the CoreAudio
    // backend wedges inside m_audioSink->stop() — observed on Qt 6.11.1 /
    // macOS during shutdown while playback is active — a blocking invoke
    // hangs the GUI thread indefinitely (see spindump, 62s+ at QLatch::wait
    // on app exit).
    QMetaObject::invokeMethod(this, [this]() {
        resetAudio();
        if (m_inFlight && m_provider) {
            m_provider->cancel(m_inFlight);
            m_inFlight = 0;
        }
        m_provider.reset();
        // Destroy m_nam on the worker thread where it was created (audit M18).
        // If we let it survive until after moveToThread back to the main thread,
        // its internal QThread affinity would be violated and its event processing
        // could produce use-after-free on the worker's event dispatcher.
        delete m_nam;
        m_nam = nullptr;
    }, Qt::QueuedConnection);

    m_thread->quit();
    if (m_thread->wait(2000)) {
        // Worker stopped cleanly. Move the engine (and m_voiceCache, which
        // is a child via QObject parent) back to the main thread so the
        // forthcoming destructor — running on the main thread — destroys
        // children on the right thread instead of UB cross-thread delete.
        moveToThread(QThread::currentThread());
        if (m_voiceCache) m_voiceCache->moveToThread(QThread::currentThread());
    } else {
        // Do NOT call terminate(). Killing a thread that may be inside a
        // CoreAudio render callback leaves OS audio locks held and can
        // wedge subsequent processes that try to open the device.
        // Instead, intentionally leak: the OS reclaims everything at
        // process exit (we are on the shutdown path), and the user sees
        // the app close cleanly rather than freeze indefinitely.
        qCWarning(lcTts) << "TTS thread did not finish in 2s; leaking thread "
                            "rather than terminate() to avoid leaving CoreAudio locks held";
        // Detach m_thread so the destructor doesn't try to wait on it again.
        m_thread->setParent(nullptr);
        m_thread = nullptr;
    }
}

void TTSEngine::initOnThread()
{
    m_nam = new QNetworkAccessManager(this);
    m_audioBuffer = new QBuffer(this);
    // Decoder is built lazily in startDecode() — see header for why.

    m_retryTimer = new QTimer(this);
    m_retryTimer->setSingleShot(true);
    connect(m_retryTimer, &QTimer::timeout, this, [this]() {
        doSynthesize(m_pendingText, m_pendingOptions);
    });

    m_voiceCache = std::make_unique<seelie::tts::TtsVoiceCache>();
    if (m_config) {
        connect(m_config, &ConfigManager::ttsCacheInvalidated,
                m_voiceCache.get(), &seelie::tts::TtsVoiceCache::wipeAll,
                Qt::QueuedConnection);
    }

    qCInfo(lcTts) << "engine init on thread, active provider ="
                  << (m_config ? m_config->ttsActiveProvider() : QStringLiteral("<no config>"));
    rebuildProvider();
}

void TTSEngine::rebuildProvider()
{
    if (!m_config) return;
    if (m_inFlight && m_provider) {
        m_provider->cancel(m_inFlight);
        m_inFlight = 0;
    }
    // Defer destruction of the old provider until after any synchronous
    // finished() lambda triggered by the cancel above has unwound. The
    // cancel path already erases the in-flight entry before calling
    // abort(), but the std::function callbacks captured via SynthesisRequest
    // can hold references that briefly outlive the cancel call. Move the
    // unique_ptr into a local — it destructs at scope exit, after all
    // synchronous work is complete. Audit H8.
    std::unique_ptr<seelie::tts::ITtsProvider> oldProvider = std::move(m_provider);
    (void)oldProvider;  // RAII: destructs at end of this function

    m_currentProviderStableId.clear();

    const QString stableId = m_config->ttsActiveProvider();
    const ProviderDescriptor* desc =
        TtsProviderRegistry::findByStableId(stableId);
    if (!desc) {
        emit error(tr("Unknown TTS provider: %1").arg(stableId));
        return;
    }

    ProviderConfig cfg{ m_config->ttsProviderConfig(stableId) };
    try {
        m_provider = desc->factory(cfg, m_nam);
    } catch (const std::exception& e) {
        qCWarning(lcTts) << "factory threw for" << stableId << ":" << e.what();
        emit error(tr("Failed to construct TTS provider: %1")
                       .arg(QString::fromUtf8(e.what())));
        return;
    }
    m_currentProviderStableId = stableId;
    qCInfo(lcTts) << "provider built:" << stableId;
}

void TTSEngine::onActiveProviderChanged(const QString &)
{
    rebuildProvider();
}

void TTSEngine::onProviderFieldChanged(const QString &providerId,
                                        const QString &,
                                        const QString &)
{
    if (providerId == m_currentProviderStableId)
        rebuildProvider();
}

void TTSEngine::speak(const QString &text)
{
    speakWithOptions(text, SpeakOptions{});
}

void TTSEngine::testSpeak(const QString &text)
{
    if (!m_config || !m_config->ttsEnabled() || text.isEmpty()) {
        qCDebug(lcTts) << "testSpeak() ignored — enabled="
                       << (m_config && m_config->ttsEnabled())
                       << "textEmpty=" << text.isEmpty();
        return;
    }

    QMetaObject::invokeMethod(this, [this, text]() {
        if (m_speaking || m_inFlight) {
            qCDebug(lcTts) << "testSpeak() dropped — busy (speaking=" << m_speaking
                           << " inFlight=" << m_inFlight << ")";
            return;
        }
        qCInfo(lcTts) << "testSpeak: bypassing voice cache, hitting provider —"
                      << text.left(60);
        m_retryTimer->stop();
        m_retryCount = 0;
        m_pendingText = text;
        m_pendingOptions = SpeakOptions{};
        doSynthesize(text, m_pendingOptions, /*bypassCacheRead=*/true);
    }, Qt::QueuedConnection);
}

void TTSEngine::clearVoiceCache()
{
    // Hop to the engine thread; m_voiceCache is constructed there in
    // initOnThread() and not safe to touch from elsewhere.
    QMetaObject::invokeMethod(this, [this]() {
        if (m_voiceCache) m_voiceCache->wipeAll();
    }, Qt::QueuedConnection);
}

void TTSEngine::speakWithOptions(const QString &text, SpeakOptions opts)
{
    if (!m_config || !m_config->ttsEnabled() || text.isEmpty()) {
        qCDebug(lcTts) << "speak() ignored — enabled=" << (m_config && m_config->ttsEnabled())
                       << "textEmpty=" << text.isEmpty();
        return;
    }

    QMetaObject::invokeMethod(this, [this, text, opts]() {
        // Debounce: if we're already speaking (or have a request in flight),
        // drop the new one rather than racing the audio pipeline through a
        // teardown. Rapid clicks on the pet stay responsive visually; only
        // the speech is gated.
        if (m_speaking || m_inFlight) {
            qCDebug(lcTts) << "speak() dropped — busy (speaking=" << m_speaking
                           << " inFlight=" << m_inFlight << ")";
            return;
        }
        qCInfo(lcTts) << "speak:" << text.left(60);
        m_retryTimer->stop();
        m_retryCount = 0;
        m_pendingText = text;
        m_pendingOptions = opts;
        doSynthesize(text, opts);
    }, Qt::QueuedConnection);
}

void TTSEngine::doSynthesize(const QString &text, SpeakOptions opts,
                              bool bypassCacheRead)
{
    if (!m_provider) {
        qCWarning(lcTts) << "doSynthesize: no provider";
        emit error(tr("No TTS provider configured"));
        return;
    }

    // Required-field precheck. Without this a missing token / voice goes
    // straight to the provider HTTP layer and surfaces as a cryptic 401
    // / 400 several seconds later. Audit L6.
    if (m_config) {
        const QString providerStableId = m_config->ttsActiveProvider();
        const auto *desc = seelie::tts::TtsProviderRegistry::findByStableId(providerStableId);
        if (desc) {
            QStringList missing;
            for (const QString &field : desc->requiredFields) {
                if (m_config->ttsProviderField(providerStableId, field).trimmed().isEmpty())
                    missing.append(field);
            }
            if (!missing.isEmpty()) {
                qCWarning(lcTts) << "doSynthesize: missing required fields"
                                 << missing << "for provider" << providerStableId;
                emit error(tr("TTS provider \"%1\" is missing required fields: %2. "
                              "Open Settings → TTS to configure.")
                               .arg(desc->displayName, missing.join(QStringLiteral(", "))));
                return;
            }
        }
    }

    if (m_voiceCache && m_config) {
        const QString providerId = m_config->ttsActiveProvider();
        const QString voiceId = m_config->ttsProviderField(providerId, QStringLiteral("voice"));
        const QString modelId = m_config->ttsProviderField(providerId, QStringLiteral("model"));
        m_pendingCacheKey = seelie::tts::TtsVoiceCache::cacheKey(
            providerId, voiceId, modelId, opts, text);

        ++m_stats.sessionRequests;
        if (!bypassCacheRead && m_voiceCache->hasCachedAudio(m_pendingCacheKey)) {
            QByteArray cachedAudio = m_voiceCache->getCachedAudio(m_pendingCacheKey);
            if (!cachedAudio.isEmpty()) {
                qCInfo(lcTts) << "cache hit, returning cached audio for key:"
                              << m_pendingCacheKey;
                ++m_stats.sessionHits;
                SynthesisResult result;
                result.audio = std::move(cachedAudio);
                QString cachedMime = m_voiceCache->getCachedMimeType(m_pendingCacheKey);
                result.mimeType = cachedMime.isEmpty()
                    ? QStringLiteral("audio/mpeg")
                    : cachedMime;
                onSynthesisSuccess(std::move(result));
                return;
            }
        }
        // Cache miss — record for diagnostics.
        m_stats.lastMissMs   = QDateTime::currentMSecsSinceEpoch();
        m_stats.lastMissText = text;
    } else {
        m_pendingCacheKey.clear();
    }

    qCDebug(lcTts) << "synthesize via" << m_currentProviderStableId;
    SynthesisRequest req{text, opts};
    m_inFlight = m_provider->synthesize(req,
        [this](SynthesisResult r) { onSynthesisSuccess(std::move(r)); },
        [this](TtsError e)        { onSynthesisError(std::move(e)); });
}

void TTSEngine::onSynthesisSuccess(SynthesisResult result)
{
    m_inFlight = 0;
    m_retryCount = 0;
    qCInfo(lcTts) << "synthesis ok:" << result.audio.size() << "bytes"
                  << "mime=" << result.mimeType;
    if (result.audio.isEmpty()) {
        qCWarning(lcTts) << "empty audio buffer — nothing to play";
        emit speakingFinished();
        return;
    }

    if (m_voiceCache && !m_pendingCacheKey.isEmpty()) {
        m_voiceCache->writeCachedAudio(m_pendingCacheKey, result.audio, result.mimeType);
    }

    m_speaking = true;
    startDecode(result.audio, result.mimeType);
    emit speakingStarted();
}

void TTSEngine::onSynthesisError(TtsError err)
{
    m_inFlight = 0;
    if (err.kind == TtsErrorKind::Cancelled) {
        qCDebug(lcTts) << "synthesis cancelled";
        m_speaking = false;
        return;
    }
    qCWarning(lcTts) << "synthesis error kind=" << int(err.kind)
                     << "http=" << err.httpStatus
                     << "msg=" << redactSensitiveInfo(err.message);
    if (err.kind == TtsErrorKind::AuthFailed) {
        m_speaking = false;
        emit authFailed(m_currentProviderStableId);
        emit error(tr("TTS authentication failed (HTTP %1)").arg(err.httpStatus));
        return;
    }
    if ((err.kind == TtsErrorKind::Network || err.kind == TtsErrorKind::RateLimited)
        && m_retryCount < kMaxRetries) {
        scheduleRetry();
        return;
    }
    m_speaking = false;
    emit error(err.message.isEmpty()
                 ? tr("TTS request failed")
                 : err.message);
}

void TTSEngine::scheduleRetry()
{
    const int delays[] = {250, 1000};
    m_retryTimer->start(delays[m_retryCount]);
    ++m_retryCount;
}

void TTSEngine::onDecoderBufferReady()
{
    if (!m_decoder) return;
    // Cap accumulated PCM so a malicious / malformed audio header that
    // claims an extreme duration cannot OOM the process. 50 MB is ≈ 4
    // minutes of 48kHz/16-bit stereo PCM — more than enough for any
    // legitimate TTS utterance.
    static constexpr qsizetype kMaxPcmBytes = 50 * 1024 * 1024;
    while (m_decoder->bufferAvailable()) {
        QAudioBuffer buf = m_decoder->read();
        if (!buf.isValid()) break;
        if (!m_pcmFormat.isValid()) {
            m_pcmFormat = buf.format();
            qCInfo(lcTts) << "decoded audio format:" << m_pcmFormat;
        }
        if (m_pcm.size() + buf.byteCount() > kMaxPcmBytes) {
            qCWarning(lcTts) << "PCM cap reached (" << kMaxPcmBytes
                             << " bytes); aborting decode to avoid OOM";
            m_decoder->stop();
            m_speaking = false;
            emit speakingFinished();
            return;
        }
        m_pcm.append(buf.constData<char>(), buf.byteCount());
    }
}

void TTSEngine::onDecoderFinished()
{
    qCInfo(lcTts) << "decoder finished, pcm size=" << m_pcm.size();
    if (m_pcm.isEmpty() || !m_pcmFormat.isValid()) {
        qCWarning(lcTts) << "no PCM to play";
        m_speaking = false;
        emit speakingFinished();
        return;
    }

    // Hand the assembled PCM to the sink in pull mode. The sink reads at
    // its own pace; no short-write loss.
    m_pcmBuffer = new QBuffer(this);
    m_pcmBuffer->setData(m_pcm);
    m_pcmBuffer->open(QIODevice::ReadOnly);

    m_audioSink = new QAudioSink(QMediaDevices::defaultAudioOutput(),
                                  m_pcmFormat, this);
    connect(m_audioSink, &QAudioSink::stateChanged, this,
            [this](QAudio::State s) {
                qCDebug(lcTts) << "sink state ->" << s;
                // IdleState after Active means the source ran out — playback
                // is done. StoppedState means we called stop() ourselves.
                if (s == QAudio::IdleState) {
                    m_speaking = false;
                    emit speakingFinished();
                }
            });

    qCInfo(lcTts) << "sink start (pull mode), device="
                  << QMediaDevices::defaultAudioOutput().description();
    m_audioSink->start(m_pcmBuffer);
    // QAudioSink::start() in pull mode returns void. A failed start
    // transitions immediately to StoppedState — the existing stateChanged
    // handler covers that path. As a safety net, poll the state after
    // starting: if it's already stopped, the sink never began playback.
    if (m_audioSink->state() == QAudio::StoppedState) {
        qCWarning(lcTts) << "QAudioSink::start() immediately entered StoppedState";
        m_speaking = false;
        emit error(tr("Failed to start audio playback"));
        emit speakingFinished();
    }
}

void TTSEngine::startDecode(const QByteArray &audio, const QString &mimeType)
{
    // Recreate the decoder every time. Reusing a single QAudioDecoder across
    // utterances is unreliable on Qt 6.11's WMF backend (Windows): once a
    // decode completes or errors, setSourceDevice() + start() on the same
    // instance frequently produces no bufferReady signals. A fresh decoder
    // sidesteps that entirely.
    resetAudio();

    m_audioBuffer->setData(audio);
    m_audioBuffer->open(QIODevice::ReadOnly);

    m_decoder = new QAudioDecoder(this);
    m_decoder->setSourceDevice(m_audioBuffer);

    connect(m_decoder, &QAudioDecoder::bufferReady,
            this, &TTSEngine::onDecoderBufferReady);
    connect(m_decoder, QOverload<>::of(&QAudioDecoder::finished),
            this, &TTSEngine::onDecoderFinished);
    connect(m_decoder, QOverload<QAudioDecoder::Error>::of(&QAudioDecoder::error), this,
            [this](QAudioDecoder::Error e) {
                qCWarning(lcTts) << "QAudioDecoder error" << e
                                 << m_decoder->errorString();
                m_speaking = false;
                emit speakingFinished();
            });

    qCDebug(lcTts) << "decoder start, audio bytes=" << audio.size()
                   << "mime hint=" << mimeType;
    m_decoder->start();
    // QAudioDecoder::start() is asynchronous; errors surface via the
    // error signal connected above. No synchronous return value to check.
}

void TTSEngine::resetAudio()
{
    if (m_decoder) {
        // Disconnect first: deleteLater() defers destruction past the next
        // event-loop iteration, and any queued bufferReady/finished signals
        // already in the queue would fire on the soon-to-be-stale instance.
        // After disconnect they're dropped, even if Qt has already routed
        // them. Audit H6.
        m_decoder->disconnect(this);
        m_decoder->stop();
        m_decoder->deleteLater();
        m_decoder = nullptr;
    }
    if (m_audioSink) {
        m_audioSink->disconnect(this);
        m_audioSink->stop();
        m_audioSink->deleteLater();
        m_audioSink = nullptr;
    }
    if (m_pcmBuffer) {
        m_pcmBuffer->close();
        m_pcmBuffer->deleteLater();
        m_pcmBuffer = nullptr;
    }
    if (m_audioBuffer) {
        m_audioBuffer->close();
        m_audioBuffer->setData(QByteArray());
    }
    m_pcm.clear();
    m_pcmFormat = QAudioFormat{};
}
