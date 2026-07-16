#ifndef LLM_PROVIDER_H
#define LLM_PROVIDER_H

#include "LLMProfile.h"
#include <QObject>
#include <QString>
#include <QVector>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

/**
 * @brief Async HTTP client for LLM completions. Single class, three protocols.
 *
 * Lives on the main thread. QNetworkAccessManager is created on the main thread
 * and its `finished` signal fires on the main thread, so user callbacks are
 * always invoked on the main thread.
 */
struct LLMResult {
    bool ok = false;
    QString text;
    QString error;
    int tokensIn = 0;
    int tokensOut = 0;
};

class LLMProvider : public QObject
{
    Q_OBJECT
public:
    explicit LLMProvider(QObject *parent = nullptr);
    ~LLMProvider() override;

    void setProfile(const LLMProfile &profile);
    LLMProfile profile() const { return m_profile; }

    /// True iff baseUrl, apiKey, and model are all non-empty.
    bool isConfigured() const;

    using ResultCallback = std::function<void(LLMResult)>;
    using BatchCallback = std::function<void(QVector<QString>)>;

    /// On-demand single completion. Callback fires exactly once on the main thread.
    void generate(const QString &system, const QString &user, ResultCallback callback);

    /// Batched: ask for N lines as a JSON array of strings. Callback receives parsed lines
    /// (possibly empty on JSON parse failure — caller handles).
    void generateBatch(const QString &system, const QString &user, int n, BatchCallback callback);

    /// Test seam: change the per-request timeout. Default 5000 ms.
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }

    /// Cooldown window after 3 consecutive failures. Default 60000 ms.
    void setCooldownMs(int ms) { m_cooldownMs = ms; }

    /// Last error string from the most recent failed call. For Settings UI.
    QString lastError() const { return m_lastError; }

    /// Blocking embeddings call (OpenAI-compatible /embeddings only).
    /// Safe to call from a worker thread: creates its own QNetworkAccessManager
    /// local to the calling thread. Returns an empty vector on any failure
    /// (errOut, if set, is filled with a short reason). Anthropic and OpenAI
    /// Responses protocols are unsupported (embeddings endpoint is OpenAI-chat
    /// only) and return empty. Not unit-tested (network) — production glue.
    static QVector<float> embedTextSync(const LLMProfile &profile, const QString &text,
                                        QString *errorOut = nullptr);

private:
    QNetworkReply *sendOpenAiChat(const QString &system, const QString &user);
    QNetworkReply *sendOpenAiResponses(const QString &system, const QString &user);
    QNetworkReply *sendAnthropicMessages(const QString &system, const QString &user);

    static LLMResult parseOpenAiChat(const QByteArray &body);
    static LLMResult parseOpenAiResponses(const QByteArray &body);
    static LLMResult parseAnthropicMessages(const QByteArray &body);

    void wireReply(QNetworkReply *reply, ResultCallback callback);

    LLMProfile m_profile;
    QNetworkAccessManager *m_nam = nullptr;
    int m_timeoutMs = 5000;
    int m_consecutiveFailures = 0;
    qint64 m_cooldownUntilMs = 0;     // 0 = no cooldown active
    int m_cooldownMs = 60000;
    QString m_lastError;
};

#endif // LLM_PROVIDER_H
