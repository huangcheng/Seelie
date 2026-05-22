#ifndef PACKMANAGERWIDGET_H
#define PACKMANAGERWIDGET_H

#include <QWidget>
#include <QPointer>
#include <QString>

class CharacterPackManager;
class StyledAlertWidget;
class QListWidget;
class QPushButton;
class QLabel;
class QPropertyAnimation;

/**
 * @brief Dialog for managing user-installed character packs.
 *
 * Provides a list view of all user packs with multi-selection support,
 * plus Add and Delete buttons for installing/removing packs in batch.
 * Styled to match SettingsPanelWidget (frameless, translucent, skewed border).
 */
class PackManagerWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal panelScale READ panelScale WRITE setPanelScale)
    Q_PROPERTY(qreal panelOpacity READ panelOpacity WRITE setPanelOpacity)

public:
    explicit PackManagerWidget(CharacterPackManager *manager, QWidget *parent = nullptr);

    void showAnimated();
    void hideAnimated();

    /** Position the dialog at top-center of the pet window, like SettingsPanelWidget. */
    void positionRelativeTo(const QWidget *pet);

    /** Set the pet window reference for positioning. */
    void setPetWindow(QWidget *pet) { m_petWindow = pet; }

    qreal panelScale() const { return m_scale; }
    void setPanelScale(qreal s);
    qreal panelOpacity() const { return m_panelOpacity; }
    void setPanelOpacity(qreal o);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void onCloseClicked();
    void onAddClicked();
    void onDeleteClicked();
    void refreshPackList();

private:
    void setupUi();
    void positionCentered();
    void ensureAlertDialog();

    CharacterPackManager *m_packManager = nullptr;

    QWidget *m_contentWidget = nullptr;
    QLabel *m_titleLabel = nullptr;
    QPushButton *m_closeButton = nullptr;
    QListWidget *m_listWidget = nullptr;
    QPushButton *m_addButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
    // QPointer auto-nulls on the dialog's destruction, so a dialog closed
    // by the user (via WA_DeleteOnClose) leaves m_alertDialog as nullptr
    // and ensureAlertDialog() lazily re-creates one on the next showAlert.
    QPointer<StyledAlertWidget> m_alertDialog;

    qreal m_scale = 1.0;
    qreal m_panelOpacity = 1.0;
    QPropertyAnimation *m_scaleAnim = nullptr;
    QPropertyAnimation *m_opacityAnim = nullptr;
    QPointer<QWidget> m_petWindow;  // H4: QPointer guards against the pet
                                     // window being destroyed while the
                                     // dialog is open.

    static constexpr int PADDING = 14;
    static constexpr int VERTICAL_SPACING = 12;
    static constexpr int SHADOW_BLUR = 10;
    static constexpr int CORNER_RADIUS = 4;
    static constexpr int BORDER_WIDTH = 3;
    static constexpr int SKEW_PX = 4;
    static constexpr int PANEL_WIDTH = 320;
    static constexpr int PANEL_HEIGHT = 400;
};

#endif // PACKMANAGERWIDGET_H
