#pragma once

#include <QColor>
#include <QAbstractSlider>
#include <QPoint>

/*
 * A rotary control in the style of dspMixFx: a coloured cap with a pointer, sweeping 270 degrees.
 * Bipolar ranges (min < 0 < max, e.g. pan) draw the arc from the centre.
 *
 * Dragged linearly: up or right increases. Wheel and keyboard work as for any QAbstractSlider.
 *
 * Deliberately not a QDial: Breeze (Plasma 6) puts a QFocusFrame on keyboard-focused QDials, an overlay that
 * draws a ring where *its* dial handle would be, regardless of how the widget paints itself.
 */
class Knob : public QAbstractSlider
{
    Q_OBJECT
public:
    explicit Knob(const QColor& color, QWidget* parent = nullptr);

    QSize sizeHint() const override { return QSize(64, 64); }
    QSize minimumSizeHint() const override { return QSize(28, 28); }

    /// Pixels of mouse travel for the whole range
    static constexpr int DRAG_PIXELS = 150;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QColor m_color;
    QPoint m_dragStart;
    int m_dragStartValue = 0;
};
