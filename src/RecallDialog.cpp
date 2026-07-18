#include "RecallDialog.h"

#include "EmbeddingService.h"
#include "MemoryManager.h"
#include "RecallFilter.h"
#include "StyleUtils.h"

#include <QDateTime>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>

RecallDialog::RecallDialog(MemoryManager *memory, EmbeddingService *embed,
                           const QString &locale, QWidget *parent)
    : PersonaDialog(tr("What do you remember?"), 480, 560, parent)
    , m_memory(memory)
    , m_embed(embed)
    , m_locale(locale)
{
    setStyleSheet(StyleUtils::personaDialogQss());

    auto *root = new QVBoxLayout(contentWidget());

    m_headerLabel = new QLabel(headerText(), contentWidget());
    m_headerLabel->setObjectName(QStringLiteral("recallHeader"));
    m_headerLabel->setWordWrap(true);
    root->addWidget(m_headerLabel);

    m_searchEdit = new QLineEdit(contentWidget());
    m_searchEdit->setObjectName(QStringLiteral("recallSearch"));
    m_searchEdit->setPlaceholderText(tr("Search memories…"));
    m_searchEdit->setClearButtonEnabled(true);
    root->addWidget(m_searchEdit);

    m_list = new QListWidget(contentWidget());
    m_list->setObjectName(QStringLiteral("recallList"));
    m_list->setUniformItemSizes(true);   // cheap rows; cap is 100
    root->addWidget(m_list, /*stretch*/ 1);

    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    auto *closeBtn = new QPushButton(tr("Close"), contentWidget());
    closeBtn->setStyleSheet(StyleUtils::personaButtonQss());
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(DEBOUNCE_MS);
    connect(m_debounce, &QTimer::timeout, this, &RecallDialog::onDebounceTimeout);

    m_fallback = new QTimer(this);
    m_fallback->setSingleShot(true);
    m_fallback->setInterval(FALLBACK_MS);
    connect(m_fallback, &QTimer::timeout, this, &RecallDialog::onFallbackTimeout);

    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &RecallDialog::onSearchTextChanged);

    if (m_embed) {
        connect(m_embed, &EmbeddingService::queryEmbeddingReady,
                this, &RecallDialog::onQueryEmbeddingReady);
    }

    renderDefault();
}

QString RecallDialog::headerText() const
{
    if (!m_memory || !m_memory->isValid()) {
        return tr("Memory unavailable.");
    }
    const int count = m_memory->episodeCount();
    const int bond = m_memory->bondLevel();
    const int aff = m_memory->affection();
    const QString name = m_memory->effectiveName();
    if (name.isEmpty()) {
        return tr("Bond L%1 · Affection %2/100 · %3 memories")
            .arg(bond).arg(aff).arg(count);
    }
    return tr("Known %1 for %2 days · Bond L%3 · Affection %4/100 · %5 memories")
        .arg(name).arg(m_memory->daysMet()).arg(bond).arg(aff).arg(count);
}

void RecallDialog::renderDefault()
{
    if (!m_memory || !m_memory->isValid()) {
        renderEmpty(tr("No memories yet — chat with me more!"));
        return;
    }
    const auto episodes = m_memory->recentEpisodes(DEFAULT_LIMIT);
    if (episodes.isEmpty()) {
        renderEmpty(tr("No memories yet — chat with me more!"));
        return;
    }
    renderEpisodes(episodes);
}

void RecallDialog::renderEpisodes(const QVector<Episode> &episodes)
{
    m_list->clear();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    for (const Episode &e : episodes) {
        m_list->addItem(QStringLiteral("[%1] %2 — %3")
            .arg(e.kind, e.text,
                 RecallFilter::relativeTime(e.ts, nowMs, m_locale)));
    }
}

void RecallDialog::renderEmpty(const QString &text)
{
    m_list->clear();
    m_list->addItem(text);
}

// Task 4 fills these in (search wiring).
void RecallDialog::onSearchTextChanged(const QString &text)
{
    m_pendingQuery = text.trimmed();
    if (m_pendingQuery.isEmpty()) {
        // Close the stale-result window immediately: without this, a late
        // queryEmbeddingReady with the still-current key could render stale
        // results into an empty-search UI before the debounce fires.
        m_currentQueryKey.clear();
    } else {
        // Placeholder, not blank — eliminates the 400ms flash while keeping
        // the row count deterministic (1) so content-based QTRYs are stable.
        renderEmpty(tr("Searching…"));
    }
    m_fallback->stop();
    m_debounce->start();   // restart-on-typing debounce
}

void RecallDialog::onDebounceTimeout()
{
    const QString query = m_pendingQuery;
    if (query.isEmpty()) {
        m_currentQueryKey.clear();
        renderDefault();
        return;
    }
    if (m_embed) {
        // Semantic path: embed the query, render on queryEmbeddingReady.
        // The fallback timer covers slow/failed embeds with substring results.
        ++m_querySeq;
        m_currentQueryKey = QStringLiteral("recall-%1").arg(m_querySeq);
        m_embed->enqueueQuery(m_currentQueryKey, query);
        m_fallback->start();
        return;
    }
    runSubstringSearch(query);
}

void RecallDialog::onFallbackTimeout()
{
    // Semantic path didn't answer in time — show substring results now; a
    // late queryEmbeddingReady still upgrades the list to semantic ranking.
    if (!m_pendingQuery.isEmpty()) {
        runSubstringSearch(m_pendingQuery);
    }
}

void RecallDialog::onQueryEmbeddingReady(const QString &key, const QVector<float> &vec)
{
    // Free the stored copy on delivery — the vector arrived in the signal, so
    // MemoryManager's m_queryEmbeds entry is redundant from here on (stale or
    // not). Without this, every semantic search leaks ~6KB for the app's life.
    if (m_memory) m_memory->clearQueryEmbedding(key);
    if (key != m_currentQueryKey) return;   // stale query — a newer one is pending
    m_fallback->stop();
    if (!m_memory || !m_memory->isValid() || vec.isEmpty()) return;
    const auto results = m_memory->recallByVector(vec, RESULT_LIMIT);
    if (results.isEmpty()) {
        renderEmpty(tr("Nothing like that yet."));
        return;
    }
    renderEpisodes(results);
}

void RecallDialog::runSubstringSearch(const QString &query)
{
    if (!m_memory || !m_memory->isValid()) return;
    const auto pool = m_memory->recentEpisodes(SUBSTRING_POOL);
    const auto hits = RecallFilter::contains(pool, query);
    const auto capped = hits.mid(0, RESULT_LIMIT);
    if (capped.isEmpty()) {
        renderEmpty(tr("Nothing like that yet."));
        return;
    }
    renderEpisodes(capped);
}
