#ifndef STATISTICS_DIALOG_H
#define STATISTICS_DIALOG_H

#include <QDialog>

class MemoryManager;
class TTSEngine;
class EventRouter;
class IpcServer;
class PersonaEngine;
class QTimer;

class StatisticsDialog : public QDialog
{
    Q_OBJECT
public:
    StatisticsDialog(MemoryManager *memory,
                     TTSEngine *tts,
                     EventRouter *events,
                     IpcServer *ipc,
                     PersonaEngine *persona,
                     QWidget *parent = nullptr);

public slots:
    void refresh();
    void resetStats();

private:
    MemoryManager *m_memory;
    TTSEngine *m_tts;
    EventRouter *m_events;
    IpcServer *m_ipc;
    PersonaEngine *m_persona;
    QTimer *m_refreshTimer;
};

#endif
