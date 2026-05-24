#ifndef TIPWIDGET_H
#define TIPWIDGET_H

#include <QWidget>
#include <QString>
#include <QTimer>
#include <QPropertyAnimation>
#include <QRect>
#include <QPointer>

class TipWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity)
    Q_PROPERTY(qreal slideOffset READ slideOffset WRITE setSlideOffset)

public:
    enum BubbleType { StatusBubble = 0, TipBubble = 1 };

    explicit TipWidget(QWidget *parent = nullptr);
    ~TipWidget();

    // Position relative to the pet widget
    void anchorTo(const QWidget *petWidget);
    void setAnchorRect(const QRect &rect) { m_anchorRect = rect; }

    // Show bubble with title + message, optionally showing source label
    // bypassUserSuppression: if true, ignores m_suppressedByUser (for system notifications like pack installation)
    void showBubble(const QString &title, const QString &message, BubbleType type = TipBubble, const QString &source = "", bool bypassUserSuppression = false);

    // Hide with exit animation
    void hideBubble();

    // Update message text in-place while the bubble is visible.
    // Recalculates layout and repaints; does not restart the dismiss timer
    // or trigger the enter animation. No-op if the new text equals the current.
    void updateMessage(const QString &message);

    // Suppression has two independent channels OR'd together: mode (ECG /
    // gaming auto-hide) and user preference (settings toggle). Either one
    // turning on hides the bubble; both must be off for it to show again.
    void setSuppressed(bool s) { setSuppressedByMode(s); }
    void setSuppressedByMode(bool s) {
        m_suppressedByMode = s;
        if (isSuppressed() && isVisible()) hideBubble();
    }
    void setSuppressedByUser(bool s) {
        m_suppressedByUser = s;
        if (isSuppressed() && isVisible()) hideBubble();
    }
    bool isSuppressed() const { return m_suppressedByMode || m_suppressedByUser; }

    // Re-apply DWM attributes (Windows). Called periodically by MainWindow
    // to combat DWM attribute loss after long-running sessions.
    void refreshDwmAttributes();

    qreal opacity() const { return m_opacity; }
    void setOpacity(qreal o);
    qreal slideOffset() const { return m_slideOffset; }
    void setSlideOffset(qreal o);

    // --- Test accessors ------------------------------------------------------
    QString title() const { return m_title; }
    QString message() const { return m_message; }
    BubbleType bubbleType() const { return m_type; }

signals:
    // Fires every time a bubble is requested, BEFORE suppression is checked.
    // Use this for side-effects that should be independent of bubble visibility
    // (e.g. TTS — speaking shouldn't be gated on the visual tip toggle).
    void bubbleRequested(const QString &title, const QString &message, BubbleType type);

    // Fires only when the bubble actually shows (after suppression check).
    void bubbleShown(const QString &title, const QString &message, BubbleType type);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void positionRelativeTo(const QWidget *pet);
    void startEnterAnimation();
    void startExitAnimation();
    void calculateTextLayout();

    // Font factories — single source of truth so paintEvent and
    // calculateTextLayout never disagree on weight/strategy/hinting.
    static QFont makeTitleFont();
    static QFont makeMessageFont();
    static QFont makeSourceFont();

    QString m_title;
    QString m_message;
    QString m_source;
    BubbleType m_type = TipBubble;

    // Suppression flags — either OR'd channel hides the bubble. mode is
    // driven by ECG / gaming auto-hide; user is driven by the settings toggle.
    bool m_suppressedByMode = false;
    bool m_suppressedByUser = false;

    // Layout
    QRect m_bubbleRect;      // The rounded rectangle part (excludes tail)
    QRect m_titleRect;       // Where title text is drawn
    QRect m_messageRect;      // Where message text is drawn
    QRect m_sourceRect;       // Where source label is drawn
    bool m_tailDown = true;   // true = tail points down (bubble above pet)

    // X-coordinate (in this widget's local coords) where the tail's tip
    // should land. Set by positionRelativeTo() to the pet center; if the
    // bubble was clamped horizontally to fit on screen, this stays
    // anchored to the pet so the tail still points at it. -1 == "no
    // anchor known yet, fall back to bubble center".
    int m_tailAnchorX = -1;

    // Animation
    qreal m_opacity = 1.0;
    qreal m_slideOffset = 0.0;   // vertical slide offset in pixels
    QPropertyAnimation *m_opacityAnim = nullptr;
    QPropertyAnimation *m_slideAnim = nullptr;
    QPoint m_targetPos;          // final resting position

    // Auto-dismiss
    QTimer m_dismissTimer;
    static constexpr int STATUS_DISMISS_MS = 6000;
    static constexpr int TIP_DISMISS_MS = 12000;

    // Styling
    static constexpr int PADDING_H = 14;       // horizontal padding
    static constexpr int PADDING_V = 10;        // vertical padding
    static constexpr int CORNER_RADIUS = 4;     // sharp-ish corners (P5 feel)
    static constexpr int TAIL_WIDTH = 16;       // base of tail triangle
    static constexpr int TAIL_HEIGHT = 10;      // height of tail triangle
    static constexpr int MAX_BUBBLE_WIDTH = 220;
    static constexpr int SHADOW_BLUR = 10;      // shadow spread
    static constexpr int BORDER_WIDTH = 3;      // bold border
    static constexpr int SKEW_PX = 4;           // parallelogram skew offset

    QPointer<const QWidget> m_anchoredPet = nullptr;
    QRect m_anchorRect;  // rect within the anchored widget to anchor to (empty = full widget)
    bool m_nativeHandleFixed = false;  // M21: one-shot guard for MacFocusFix
};

#endif // TIPWIDGET_H
