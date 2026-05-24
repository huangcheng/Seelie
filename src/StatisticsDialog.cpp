#include "StatisticsDialog.h"
#include "StyleUtils.h"
#include "MemoryManager.h"
#include "TTSEngine.h"
#include "EventRouter.h"
#include "IpcServer.h"
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
                                   EventRouter *e, IpcServer *i,
                                   PersonaEngine *p, QWidget *parent)
    : QDialog(parent), m_memory(m), m_tts(t), m_events(e), m_ipc(i), m_persona(p),
      m_refreshTimer(new QTimer(this))
{
    setWindowTitle(tr("Statistics"));
    setFixedSize(480, 620);
    setStyleSheet(StyleUtils::personaDialogQss());

    auto *root = new QVBoxLayout(this);

    auto mkSection = [&](const QString &title) {
        auto *g = new QGroupBox(title, this);
        auto *f = new QFormLayout(g);
        root->addWidget(g);
        return f;
    };

    auto *tts = mkSection(tr("TTS Cache"));
    tts->addRow(tr("Requests (lifetime):"), mkVal(QStringLiteral("ttsRequestsLabel"), this));
    tts->addRow(tr("Hits (lifetime):"),     mkVal(QStringLiteral("ttsHitsLabel"), this));

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
    auto *refreshBtn = new QPushButton(tr("Refresh"), this);
    auto *resetBtn   = new QPushButton(tr("Reset stats"), this);
    auto *closeBtn   = new QPushButton(tr("Close"), this);
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

    if (auto *l = findChild<QLabel*>(QStringLiteral("ttsRequestsLabel"))) l->setText(val("stats.tts.requests"));
    if (auto *l = findChild<QLabel*>(QStringLiteral("ttsHitsLabel")))     l->setText(val("stats.tts.hits"));

    if (auto *l = findChild<QLabel*>(QStringLiteral("personaRefillsLabel"))) {
        l->setText(QStringLiteral("%1 / %2").arg(val("stats.persona.refills.ok"),
                                                  val("stats.persona.refills.fail")));
    }
    if (auto *l = findChild<QLabel*>(QStringLiteral("personaOndemandLabel"))) {
        l->setText(QStringLiteral("%1 / %2").arg(val("stats.persona.ondemand.ok"),
                                                  val("stats.persona.ondemand.fail")));
    }
    if (auto *l = findChild<QLabel*>(QStringLiteral("personaTokensLabel"))) {
        l->setText(QStringLiteral("%1 / %2").arg(val("stats.persona.tokens.in"),
                                                  val("stats.persona.tokens.out")));
    }
    if (auto *l = findChild<QLabel*>(QStringLiteral("personaLastErrorLabel"))) {
        const QString err = m_persona ? m_persona->stats().lastError : QString();
        l->setText(err.isEmpty() ? QStringLiteral("—") : err);
    }

    if (auto *l = findChild<QLabel*>(QStringLiteral("eventsTotalLabel"))) l->setText(val("stats.events.total"));
    if (auto *l = findChild<QLabel*>(QStringLiteral("eventsLastLabel"))) {
        l->setText(m_events ? m_events->stats().lastEventName : QStringLiteral("—"));
    }

    if (auto *l = findChild<QLabel*>(QStringLiteral("ipcPacketsLabel"))) {
        l->setText(m_ipc ? QString::number(m_ipc->stats().packets) : QStringLiteral("0"));
    }
    if (auto *l = findChild<QLabel*>(QStringLiteral("ipcErrorsLabel"))) {
        l->setText(m_ipc ? QString::number(m_ipc->stats().decodeErrors) : QStringLiteral("0"));
    }
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
