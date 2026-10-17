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
 */
class ReverbGraph : public QWidget
{
    Q_OBJECT
public:
    explicit ReverbGraph(UR44::Device* device, QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void refresh();
    /// The projection: time (0..1 of the range), depth (0..1: 20 Hz .. 20 kHz on a log scale) and level (0..1:
    /// -60 .. 0 dB) to the widget
    QPointF project(double time, double depth, double level) const;

    UR44::Device* m_device;
    UR44::ReverbShape::Shape m_shape{{}};
    double m_rangeMs = 1000.0;
    QRectF m_area;
};
