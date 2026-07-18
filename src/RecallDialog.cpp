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
void RecallDialog::onSearchTextChanged(const QString &) {}
void RecallDialog::onDebounceTimeout() {}
void RecallDialog::onFallbackTimeout() {}
void RecallDialog::onQueryEmbeddingReady(const QString &, const QVector<float> &) {}
void RecallDialog::runSubstringSearch(const QString &) {}
