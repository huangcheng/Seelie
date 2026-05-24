#include "StatisticsDialog.h"
#include "StyleUtils.h"
#include "MemoryManager.h"
#include "TTSEngine.h"
#include "EventRouter.h"
#include "IPCServer.h"
#include "PersonaEngine.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QMessageBox>
#include <QSqlQuery>

namespace {
QLabel *mkVal(const QString &objectName, QWidget *parent)
{
    auto *l = new QLabel(QStringLiteral("—"), parent);
    l->setObjectName(objectName);
    l->setStyleSheet(QStringLiteral("font-family: monospace;"));
    return l;
}
}  // namespace

StatisticsDialog::StatisticsDialog(MemoryManager *m, TTSEngine *t,
                                   EventRouter *e, IPCServer *i,
                                   PersonaEngine *p, QWidget *parent)
    : PersonaDialog(tr("Statistics"), 480, 660, parent),
      m_memory(m), m_tts(t), m_events(e), m_ipc(i), m_persona(p),
      m_refreshTimer(new QTimer(this))
{
    setStyleSheet(StyleUtils::personaDialogQss());

    auto *root = new QVBoxLayout(contentWidget());

    auto mkSection = [&](const QString &title) {
        auto *g = new QGroupBox(title, contentWidget());
        auto *f = new QFormLayout(g);
        root->addWidget(g);
        return f;
    };

    auto *tts = mkSection(tr("TTS Cache"));
    tts->addRow(tr("Requests:"), mkVal(QStringLiteral("ttsRequestsLabel"), this));
    tts->addRow(tr("Hits:"),     mkVal(QStringLiteral("ttsHitsLabel"), this));

    auto *persona = mkSection(tr("AI Persona"));
    persona->addRow(tr("Refills ok / fail:"),    mkVal(QStringLiteral("personaRefillsLabel"), this));
    persona->addRow(tr("On-demand ok / fail:"),  mkVal(QStringLiteral("personaOndemandLabel"), this));
    persona->addRow(tr("Tokens in / out:"),      mkVal(QStringLiteral("personaTokensLabel"), this));
    persona->addRow(tr("Last LLM error:"),       mkVal(QStringLiteral("personaLastErrorLabel"), this));

    auto *events = mkSection(tr("Events"));
    events->addRow(tr("Total received:"), mkVal(QStringLiteral("eventsTotalLabel"), this));
    events->addRow(tr("Last event:"),     mkVal(QStringLiteral("eventsLastLabel"), this));

    auto *ipc = mkSection(tr("IPC"));
    ipc->addRow(tr("Packets received:"), mkVal(QStringLiteral("ipcPacketsLabel"), this));
    ipc->addRow(tr("Decode errors:"),    mkVal(QStringLiteral("ipcErrorsLabel"), this));

    auto *btnRow = new QHBoxLayout;
    auto *refreshBtn = new QPushButton(tr("Refresh"), contentWidget());
    auto *resetBtn   = new QPushButton(tr("Reset stats"), contentWidget());
    auto *closeBtn   = new QPushButton(tr("Close"), contentWidget());
    btnRow->addWidget(refreshBtn);
    btnRow->addWidget(resetBtn);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);

    connect(refreshBtn, &QPushButton::clicked, this, &StatisticsDialog::refresh);
    connect(resetBtn,   &QPushButton::clicked, this, &StatisticsDialog::resetStats);
    connect(closeBtn,   &QPushButton::clicked, this, &QDialog::accept);

    m_refreshTimer->setInterval(2000);
    connect(m_refreshTimer, &QTimer::timeout, this, &StatisticsDialog::refresh);
    m_refreshTimer->start();

    refresh();
}

void StatisticsDialog::refresh()
{
    auto val = [this](const char *key) -> QString {
        return m_memory ? m_memory->value(key, QStringLiteral("0")) : QStringLiteral("0");
    };

    // TTS — live from TTSEngine (worker thread; safe to read since stats struct
    // is a value copy)
    if (auto *l = findChild<QLabel*>(QStringLiteral("ttsRequestsLabel"))) {
        l->setText(m_tts ? QString::number(m_tts->stats().sessionRequests)
                         : QStringLiteral("0"));
    }
    if (auto *l = findChild<QLabel*>(QStringLiteral("ttsHitsLabel"))) {
        l->setText(m_tts ? QString::number(m_tts->stats().sessionHits)
                         : QStringLiteral("0"));
    }

    // Persona — prefer live counts so refresh shows current session activity
    // even before lifetime values land in MemoryManager.
    if (auto *l = findChild<QLabel*>(QStringLiteral("personaRefillsLabel"))) {
        if (m_persona) {
            const auto s = m_persona->stats();
            l->setText(QStringLiteral("%1 / %2").arg(s.refillsOk).arg(s.refillsFail));
        } else {
            l->setText(QStringLiteral("0 / 0"));
        }
    }
    if (auto *l = findChild<QLabel*>(QStringLiteral("personaOndemandLabel"))) {
        if (m_persona) {
            const auto s = m_persona->stats();
            l->setText(QStringLiteral("%1 / %2").arg(s.ondemandOk).arg(s.ondemandFail));
        } else {
            l->setText(QStringLiteral("0 / 0"));
        }
    }
    if (auto *l = findChild<QLabel*>(QStringLiteral("personaTokensLabel"))) {
        if (m_persona) {
            const auto s = m_persona->stats();
            l->setText(QStringLiteral("%1 / %2").arg(s.tokensIn).arg(s.tokensOut));
        } else {
            l->setText(QStringLiteral("0 / 0"));
        }
    }
    if (auto *l = findChild<QLabel*>(QStringLiteral("personaLastErrorLabel"))) {
        const QString err = m_persona ? m_persona->stats().lastError : QString();
        l->setText(err.isEmpty() ? QStringLiteral("—") : err);
    }

    // Events — live from EventRouter
    if (auto *l = findChild<QLabel*>(QStringLiteral("eventsTotalLabel"))) {
        l->setText(m_events ? QString::number(m_events->stats().total)
                            : QStringLiteral("0"));
    }
    if (auto *l = findChild<QLabel*>(QStringLiteral("eventsLastLabel"))) {
        const QString last = m_events ? m_events->stats().lastEventName : QString();
        l->setText(last.isEmpty() ? QStringLiteral("—") : last);
    }

    // IPC — live from IPCServer
    if (auto *l = findChild<QLabel*>(QStringLiteral("ipcPacketsLabel"))) {
        l->setText(m_ipc ? QString::number(m_ipc->stats().packets)
                         : QStringLiteral("0"));
    }
    if (auto *l = findChild<QLabel*>(QStringLiteral("ipcErrorsLabel"))) {
        l->setText(m_ipc ? QString::number(m_ipc->stats().decodeErrors)
                         : QStringLiteral("0"));
    }

    Q_UNUSED(val)  // kept for potential future MemoryManager fallback fields
}

void StatisticsDialog::resetStats()
{
    if (QMessageBox::question(this, tr("Reset stats?"),
        tr("This clears only the stats counters. Milestones, name, and other "
           "memory data are preserved. Continue?")) != QMessageBox::Yes) return;

    if (m_memory) {
        QSqlQuery q(m_memory->database());
        q.exec(QStringLiteral("DELETE FROM memory WHERE key LIKE 'stats.%'"));
    }
    refresh();
}
