#ifndef TTSENGINE_H
#define TTSENGINE_H

#include "tts/ITTSProvider.h"
#include "tts/TTSProviderRegistry.h"
#include "tts/TTSVoiceCache.h"

#include <QAudioDecoder>
#include <QAudioFormat>
#include <QAudioSink>
#include <QBuffer>
#include <QObject>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <memory>

class ConfigManager;
class QNetworkAccessManager;

// NOTE: TTS counters are in-memory only (no MemoryManager persistence).
// TTSEngine runs on a worker thread; calling SQLite from the worker would
// violate the single-connection thread invariant. Per-session hit rate is
// sufficient — a TTSEngine restart resets the counters intentionally.
struct TTSStats {
    int sessionRequests = 0;
    int sessionHits     = 0;
    qint64 lastMissMs   = 0;
    QString lastMissText;
};

class TTSEngine : public QObject
{
    Q_OBJECT

public:
    explicit TTSEngine(ConfigManager *config, QObject *parent = nullptr);
    ~TTSEngine() override;

    void start();
    void stop();

    TTSStats stats() const { return m_stats; }

public slots:
    void speak(const QString &text);
    void speakWithOptions(const QString &text, seelie::tts::SpeakOptions opts);
    // Same as speak() but bypasses the voice-cache lookup so the provider
    // HTTP layer is always exercised. Used by Settings → AI → "Test" — a
    // cache hit on the canned test phrase would falsely indicate provider
    // success when in fact only playback was tested. Cache write still
    // happens so the test result serves any later tip with the same text.
    void testSpeak(const QString &text);
    // Wipe the on-disk voice cache. Safe to call from any thread.
    void clearVoiceCache();

signals:
    void speakingStarted();
    void speakingFinished();
    void error(const QString &message);
    void authFailed(QString providerStableId);

private slots:
    void onActiveProviderChanged(const QString &stableId);
    void onProviderFieldChanged(const QString &providerId,
                                const QString &field,
                                const QString &value);
    void onDecoderBufferReady();
    void onDecoderFinished();

private:
    void initOnThread();           // runs on m_thread after start()
    void rebuildProvider();        // tear down, re-instantiate from current config
    void doSynthesize(const QString &text, seelie::tts::SpeakOptions opts,
                      bool bypassCacheRead = false);
    void onSynthesisSuccess(seelie::tts::SynthesisResult result);
    void onSynthesisError(seelie::tts::TTSError err);
    void scheduleRetry();
    void resetAudio();
    void startDecode(const QByteArray &audio, const QString &mimeType);

    ConfigManager *m_config = nullptr;
    QThread *m_thread = nullptr;

    QNetworkAccessManager *m_nam = nullptr;
    std::unique_ptr<seelie::tts::ITTSProvider> m_provider;
    std::unique_ptr<seelie::tts::TTSVoiceCache> m_voiceCache;
    QString m_currentProviderStableId;

    // Active request bookkeeping.
    seelie::tts::RequestHandle m_inFlight = 0;
    QString m_pendingText;
    seelie::tts::SpeakOptions m_pendingOptions;
    // Snapshot of the cache fingerprint at request time. Captured here so the
    // success path writes under the same key the lookup used, even if config
    // changes mid-flight (rebuildProvider() cancels in-flight requests, but
    // this insulates the cache from any future cancellation regression).
    QString m_pendingCacheKey;
    int m_retryCount = 0;
    QTimer *m_retryTimer = nullptr;

    // Audio pipeline. Decoder is recreated per-utterance (Qt 6.11 WMF
    // backend stops emitting bufferReady on a reused instance after the
    // first decode). The sink runs in pull mode against m_pcmBuffer: we
    // accumulate decoded PCM into the buffer as bufferReady fires, then
    // hand the buffer to QAudioSink::start() once the decoder finishes.
    // Pull mode lets the sink read at its own pace and eliminates the
    // short-write loss that push mode produced on Windows.
    QBuffer *m_audioBuffer = nullptr;        // MP3 input to the decoder
    QAudioDecoder *m_decoder = nullptr;
    QByteArray m_pcm;                        // accumulated decoded PCM
    QAudioFormat m_pcmFormat;                // format of m_pcm
    QBuffer *m_pcmBuffer = nullptr;          // pull-mode source for the sink
    QAudioSink *m_audioSink = nullptr;

    // True between speakingStarted and speakingFinished. Rapid speak() calls
    // arriving while busy are dropped (debounce). Set/cleared on the engine
    // thread only.
    bool m_speaking = false;

    static constexpr int kMaxRetries = 2;

    TTSStats m_stats;
};

#endif
