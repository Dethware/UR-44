#pragma once

#include <QPointF>
#include <QWidget>

#include "ReverbShape.h"

namespace UR44
{
class Device;
}

/*
 * The REV-X editor's graph, like dspMixFx's: a waterfall of the reverb's decay at 100 Hz, 1 kHz and 10 kHz (the low
 * frequencies in front), filled brighter with more diffusion; the reverb's end per frequency on the floor, and the
 * HPF/LPF response on the left wall. The time range follows the reverb time (dspMixFx's auto zoom).
 *
 * It's also a control. dspMixFx's handles sit on rulers around its graph; here they sit on what they set: the initial
 * delay on the front line's start (it's every line's), the decay and reverb time on the 1 kHz line (its peak and
 * end), the ratios at the ends of the floor's curve (20 Hz and 20 kHz) with the low frequency on it, and the HPF and
 * LPF on the wall's. Drag them (Shift: fine); the zoom holds still while dragging. The wheel zooms (the 9 ranges);
 * the AUTO badge goes back to the auto zoom.
 */
class ReverbGraph : public QWidget
{
    Q_OBJECT
public:
    explicit ReverbGraph(UR44::Device* device, QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    enum class Handle
    {
        None,
        InitialDelay,
        Decay,
        ReverbTime,
        LowRatio,
        HighRatio,
        LowFreq,
        Hpf,
        Lpf,
    };
    struct Point
    {
        double time;  // 0..1 of the range (may lie beyond it)
        double depth; // 0..1
        double level; // 0..1
    };

    void refresh();
    void applyZoom();
    QRectF plotArea() const;
    QRectF autoBadge() const;
    /// Where a handle is, in the graph's coordinates, for the current settings or others
    Point handlePoint(Handle handle, const UR44::ReverbShape::Shape& shape,
                      const UR44::ReverbShape::Settings& settings) const;
    QPointF handlePosition(Handle handle) const;
    Handle handleAt(const QPointF& pos) const;
    /// Sets the handle's parameter to the value that puts it nearest `pos`
    void moveHandle(Handle handle, const QPointF& pos);
    void showValues(Handle handle, const QPoint& globalPos);
    QString badgeToolTip() const;
    /// The projection: time (0..1 of the range), depth (0..1: 20 Hz .. 20 kHz on a log scale) and level (0..1:
    /// -60 .. 0 dB) to the widget
    QPointF project(double time, double depth, double level) const;

    UR44::Device* m_device;
    UR44::ReverbShape::Settings m_settings;
    UR44::ReverbShape::Shape m_shape{{}};
    double m_rangeMs = 1000.0;
    bool m_autoZoom = true;
    int m_zoom = 1;

    Handle m_hover = Handle::None;
    bool m_hoverBadge = false;
    struct Drag
    {
        Handle handle = Handle::None;
        QPointF press;
        QPointF origin;
        bool fine = false;
    } m_drag;
    int m_wheelRemainder = 0;
};
