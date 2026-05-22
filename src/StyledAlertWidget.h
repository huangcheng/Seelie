#ifndef STYLEDALERTWIDGET_H
#define STYLEDALERTWIDGET_H

#include <QWidget>
#include <QString>
#include <QPropertyAnimation>

class QLabel;
class QPushButton;

/**
 * @brief Reusable styled alert dialog matching the app's skewed-panel visual style.
 *
 * Displays a title, body text, and a single OK button inside a frameless,
 * translucent, skewed-parallelogram-bordered panel — identical to
 * SettingsPanelWidget and PackManagerWidget.
 */
class StyledAlertWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal panelScale READ panelScale WRITE setPanelScale)
    Q_PROPERTY(qreal panelOpacity READ panelOpacity WRITE setPanelOpacity)

public:
    explicit StyledAlertWidget(QWidget *parent = nullptr);

    /**
     * @brief Show the alert dialog with the given title and body text.
     * @param title   Bold title text at the top of the dialog.
     * @param body   Body text below the title.
     * @param buttonText  Custom label for the OK button (defaults to "OK").
     */
    void showAlert(const QString &title, const QString &body,
                   const QString &buttonText = QString());

    /**
     * @brief Show a confirmation dialog with Yes/No buttons and block until dismissed.
     * @param title   Bold title text.
     * @param body   Body text.
     * @return true if user clicked Yes, false if No or closed.
     */
    bool execConfirm(const QString &title, const QString &body);

    void showAnimated();

    /** Animated hide (fade-out + scale-down). */
    void hideAnimated();

    /** Position the dialog at top-center of the pet window, like SettingsPanelWidget. */
    void positionRelativeTo(const QWidget *pet);

    /** Set the pet window reference for positioning. */
    void setPetWindow(QWidget *pet) { m_petWindow = pet; }

    qreal panelScale() const { return m_scale; }
    void setPanelScale(qreal s);
    qreal panelOpacity() const { return m_panelOpacity; }
    void setPanelOpacity(qreal o);

signals:
    /** Emitted when the dialog is dismissed (OK or close). */
    void dismissed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void onOkClicked();
    void onCancelClicked();
    void onCloseClicked();

private:
    void setupUi();
    void positionCentered();
    void fitToContent();

    QWidget *m_contentWidget = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_bodyLabel = nullptr;
    QPushButton *m_closeButton = nullptr;
    QPushButton *m_okButton = nullptr;
    QPushButton *m_cancelButton = nullptr;

    qreal m_scale = 1.0;
    qreal m_panelOpacity = 1.0;
    QPropertyAnimation *m_scaleAnim = nullptr;
    QPropertyAnimation *m_opacityAnim = nullptr;

    bool m_confirmResult = false;
    bool m_inConfirmMode = false;
    QWidget *m_petWindow = nullptr;

    static constexpr int PADDING = 14;
    static constexpr int VERTICAL_SPACING = 12;
    static constexpr int SHADOW_BLUR = 10;
    static constexpr int CORNER_RADIUS = 4;
    static constexpr int BORDER_WIDTH = 3;
    static constexpr int SKEW_PX = 4;
    static constexpr int ACCENT_HEIGHT = 4;
    static constexpr int PANEL_WIDTH = 320;
    static constexpr int PANEL_HEIGHT = 200;
};

#endif // STYLEDALERTWIDGET_H
