#pragma once

#include <QColor>
#include <QAbstractSlider>
#include <QPoint>

/*
 * A rotary control in the style of hardware mixer software: a coloured cap with a pointer, sweeping 270 degrees.
 * Bipolar ranges (min < 0 < max, e.g. pan) draw the arc from the centre.
 *
 * Dragged linearly: up or right increases. Wheel and keyboard work as for any QAbstractSlider.
 * While dragging, the pointer is hidden and held in place, so a drag never runs into the screen edge. That needs
 * pointer warping, which Wayland doesn't allow: there the pointer just moves as usual.
 *
 * Deliberately not a QDial: Breeze (Plasma 6) puts a QFocusFrame on keyboard-focused QDials, an overlay that
 * draws a ring where *its* dial handle would be, regardless of how the widget paints itself.
 */
class Knob : public QAbstractSlider
{
    Q_OBJECT
public:
    explicit Knob(const QColor& color, QWidget* parent = nullptr);

    /// Shorter than wide: the sweep is open at the bottom, so the space below it is cut off
    QSize sizeHint() const override { return QSize(64, 56); }
    QSize minimumSizeHint() const override { return QSize(28, 25); }

    /// The height the knob needs, as a fraction of its diameter
    static constexpr double HEIGHT_RATIO = 0.87;

    /// Pixels of mouse travel for the whole range
    static constexpr int DRAG_PIXELS = 150;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QColor m_color;
    /// Global position of the last mouse event; with the pointer captured, the press position it's warped back to
    QPointF m_dragLastSeen;
    QPointF m_dragPressedAt;
    bool m_pointerCaptured = false;
    int m_dragStartValue = 0;
    double m_dragSteps = 0.0;
};
