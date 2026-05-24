#ifndef STATISTICS_DIALOG_H
#define STATISTICS_DIALOG_H

#include "PersonaDialog.h"

class MemoryManager;
class TTSEngine;
class EventRouter;
class IPCServer;
class PersonaEngine;
class QTimer;

class StatisticsDialog : public PersonaDialog
{
    Q_OBJECT
public:
    StatisticsDialog(MemoryManager *memory,
                     TTSEngine *tts,
                     EventRouter *events,
                     IPCServer *ipc,
                     PersonaEngine *persona,
                     QWidget *parent = nullptr);

public slots:
    void refresh();
    void resetStats();

private:
    MemoryManager *m_memory;
    TTSEngine *m_tts;
    EventRouter *m_events;
    IPCServer *m_ipc;
    PersonaEngine *m_persona;
    QTimer *m_refreshTimer;
};

#endif
