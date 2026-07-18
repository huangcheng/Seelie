#ifndef RECALLDIALOG_H
#define RECALLDIALOG_H

#include "PersonaDialog.h"

#include <QVector>

class EmbeddingService;
class MemoryManager;
class QLabel;
class QLineEdit;
class QListWidget;
class QTimer;
struct Episode;

/**
 * "What do you remember?" — the Recall UI. StatisticsDialog-style chrome via
 * PersonaDialog: a relationship header (days/bond/affection/count), a search
 * box (Task 4 wires the two search paths), and a QListWidget of episode rows
 * ("[kind] text — 2h ago"). Read-only; no editing/deleting in v1.
 */
class RecallDialog : public PersonaDialog
{
    Q_OBJECT

public:
    RecallDialog(MemoryManager *memory, EmbeddingService *embed,
                 const QString &locale, QWidget *parent = nullptr);

private slots:
    void onSearchTextChanged(const QString &text);
    void onDebounceTimeout();
    void onFallbackTimeout();
    void onQueryEmbeddingReady(const QString &key, const QVector<float> &vec);

private:
    void renderDefault();
    void renderEpisodes(const QVector<Episode> &episodes);
    void renderEmpty(const QString &text);
    void runSubstringSearch(const QString &query);
    QString headerText() const;

    MemoryManager *m_memory;
    EmbeddingService *m_embed;
    QString m_locale;

    QLabel *m_headerLabel = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QListWidget *m_list = nullptr;
    QTimer *m_debounce = nullptr;
    QTimer *m_fallback = nullptr;
    QString m_pendingQuery;
    QString m_currentQueryKey;
    quint64 m_querySeq = 0;

    static constexpr int DEFAULT_LIMIT = 100;
    static constexpr int RESULT_LIMIT = 20;
    static constexpr int SUBSTRING_POOL = 500;
    static constexpr int DEBOUNCE_MS = 400;
    static constexpr int FALLBACK_MS = 800;
};

#endif // RECALLDIALOG_H
