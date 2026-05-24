#ifndef PERSONA_DIALOG_H
#define PERSONA_DIALOG_H

#include <QDialog>

class QLabel;
class QPushButton;
class QFrame;
class QVBoxLayout;

/**
 * @brief QDialog subclass that paints the Persona 5 Royal panel chrome
 *        (frameless, drop shadow, black border, drawn title bar with
 *        close button). Mirrors SettingsPanelWidget's visual language.
 *
 * Usage:
 *   class MyDialog : public PersonaDialog {
 *   public:
 *       MyDialog() : PersonaDialog(QStringLiteral("My Title"), 420, 520) {
 *           auto *layout = new QVBoxLayout(contentWidget());
 *           // ... add child widgets to contentWidget(), not 'this' ...
 *       }
 *   };
 */
class PersonaDialog : public QDialog
{
    Q_OBJECT
public:
    /// title — drawn in the dialog's title row.
    /// bodyW / bodyH — the FULL panel dimensions (excluding shadow margins
    ///   on all four sides). Matches SettingsPanelWidget's PANEL_WIDTH ×
    ///   PANEL_HEIGHT semantics.
    PersonaDialog(const QString &title, int bodyW, int bodyH, QWidget *parent = nullptr);

    /// Container for dialog content (below the separator). Add a layout to
    /// this, not to `this`.
    QWidget *contentWidget() const { return m_contentArea; }

    /// Re-emit the title (used if subclass changes its own title).
    void setPersonaTitle(const QString &title);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;    // drag-to-move
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QLabel      *m_titleLabel  = nullptr;
    QPushButton *m_closeButton = nullptr;
    QFrame      *m_separator   = nullptr;
    QWidget     *m_panelWidget = nullptr;   // spans the full body (transparent bg)
    QWidget     *m_contentArea = nullptr;   // child of m_panelWidget, below separator

    int m_bodyWidth;
    int m_bodyHeight;

    // Drag state
    QPoint m_dragOffset;
    bool m_dragging = false;

    static constexpr int SHADOW_BLUR   = 10;
    static constexpr int CORNER_RADIUS = 4;
    static constexpr int BORDER_WIDTH  = 3;
    static constexpr int SKEW_PX       = 4;
    static constexpr int PADDING       = 14;   // match SettingsPanelWidget
    static constexpr int VERTICAL_SPACING = 12; // match SettingsPanelWidget
};

#endif // PERSONA_DIALOG_H
