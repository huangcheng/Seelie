#ifndef SYSTEMTRAY_H
#define SYSTEMTRAY_H

#include <QObject>
#include <QPointer>
#include <QSystemTrayIcon>

class QSystemTrayIcon;
class QMenu;
class QWidget;
class QAction;
class QActionGroup;
class CharacterPackManager;
class UpdateChecker;
class ConfigManager;
class PackManagerWidget;
class TipWidget;

class SystemTray : public QObject
{
    Q_OBJECT

public:
    explicit SystemTray(QWidget *mainWindow, ConfigManager *config, QObject *parent = nullptr);
    ~SystemTray() override;

    void show();
    void retranslateUi();

    // Set sprite pack manager for pack submenu
    void setCharacterPackManager(CharacterPackManager *manager);

    // Set update checker
    void setUpdateChecker(UpdateChecker *checker);

    // Set tip widget used for surfacing update-check results.
    // Routing through the tip bubble (rather than the OS tray balloon)
    // is more reliable across Windows configs (Focus Assist, missing
    // app-user-model-id, notification permission revoked, etc.) and
    // hooks into the existing TTS readback automatically — MainWindow
    // already wires TipWidget::bubbleRequested into TTSEngine::speak.
    void setTipWidget(TipWidget *tipWidget);

signals:
    void statisticsTriggered();
    void exportConfigTriggered();
    void importConfigTriggered();

private slots:
    void onActivated(QSystemTrayIcon::ActivationReason reason);
    void onPackActionTriggered();
    void onManageModelsClicked();
    void onUpdateAvailable(const QString &current, const QString &latest, const QString &url);
    void onNoUpdateAvailable(const QString &current);
    void onUpdateCheckFailed(const QString &error);

private:
    void setupMenu();
    void refreshPackMenu();

    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu *m_trayMenu = nullptr;
    QMenu *m_packMenu = nullptr;
    QActionGroup *m_packActionGroup = nullptr;
    QWidget *m_mainWindow = nullptr;
    QAction *m_toggleAction = nullptr;
    QAction *m_gamingModeAction = nullptr;
    QAction *m_quitAction = nullptr;
    QAction *m_checkUpdateAction = nullptr;
    QAction *m_manageModelsAction = nullptr;
    QAction *m_statsAction = nullptr;
    QMenu   *m_configMenu = nullptr;
    QAction *m_exportConfigAction = nullptr;
    QAction *m_importConfigAction = nullptr;
    CharacterPackManager *m_packManager = nullptr;
    UpdateChecker *m_updateChecker = nullptr;
    ConfigManager *m_config = nullptr;
    TipWidget *m_tipWidget = nullptr;
    QPointer<PackManagerWidget> m_packManagerDialog;
};

#endif // SYSTEMTRAY_H
