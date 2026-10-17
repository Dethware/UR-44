#include "StripGraphs.h"

#include <QPainter>
#include <QPainterPath>

#include <cmath>

#include "Bindings.h"
#include "Device.h"
#include "Widgets.h"

using namespace UR44;
using namespace UR44::ChannelStripCurves;

namespace
{
// The editor's scales: the compressor -60 .. 0 dB on both axes, the EQ 20 Hz .. 20 kHz and -20 .. +20 dB
constexpr double COMP_RANGE_DB = 60.0;
constexpr double EQ_MIN_HZ = 20.0;
constexpr double EQ_DECADES = 3.0;
constexpr double EQ_RANGE_DB = 20.0;

QColor curveColor(const QColor& color, bool bypassed)
{
    return bypassed ? QColor(140, 140, 140) : color;
}

void followAll(QWidget* context, Device* device, uint16_t strip, std::initializer_list<const char*> names,
               const std::function<void()>& refresh)
{
    for (const char* name : names)
    {
        Bindings::follow(context, device, name, strip, [refresh](int32_t) { refresh(); });
    }
}
}

// --- Compressor ---------------------------------------------------------------------------------------------------

CompressorGraph::CompressorGraph(Device* device, uint16_t strip, QWidget* parent)
    : QWidget(parent), m_device(device), m_strip(strip)
{
    // as tall as the compressor's rows (it doesn't ask for more), and square: resizeEvent() follows the height
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    setToolTip(tr("Compressor curve: output level (up) for the input level (across), -60 .. 0 dB"));
    followAll(this, device, strip, {"FxCompDrive", "FxCompRatio", "FxCompKnee", "FxCompThreshOfst", "FxCompBypass"},
              [this] { refresh(); });
    refresh();
}

QSize CompressorGraph::sizeHint() const
{
    return QSize(height() > 0 ? height() : 132, 132);
}

QSize CompressorGraph::minimumSizeHint() const
{
    return QSize(96, 96);
}

void CompressorGraph::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (width() != height())
    {
        setFixedWidth(height());
    }
}

void CompressorGraph::refresh()
{
    m_compressor = Compressor(compressorSettings(*m_device, m_strip));
    m_bypassed = m_device->value("FxCompBypass", m_strip).value_or(0) != 0;
    update();
}

void CompressorGraph::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QRectF area = Widgets::beginDisplayPanel(p, rect(), isEnabled());
    auto x = [&](double db) { return area.left() + area.width() * (db + COMP_RANGE_DB) / COMP_RANGE_DB; };
    auto y = [&](double db) { return area.bottom() - area.height() * (db + COMP_RANGE_DB) / COMP_RANGE_DB; };

    // a grid every 10 dB, and the 1:1 line
    for (int db = -50; db < 0; db += 10)
    {
        p.setPen(Widgets::displayGrid(db % 20 == 0));
        p.drawLine(QPointF(x(db), area.top()), QPointF(x(db), area.bottom()));
        p.drawLine(QPointF(area.left(), y(db)), QPointF(area.right(), y(db)));
    }
    p.setPen(QPen(Widgets::displayGrid(true), 1, Qt::DashLine));
    p.drawLine(QPointF(x(-COMP_RANGE_DB), y(-COMP_RANGE_DB)), QPointF(x(0), y(0)));

    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath curve;
    const int steps = int(area.width());
    for (int i = 0; i <= steps; ++i)
    {
        double in = -COMP_RANGE_DB + COMP_RANGE_DB * i / steps;
        double out = std::clamp(m_compressor.outputDb(in), -COMP_RANGE_DB, 0.0);
        i == 0 ? curve.moveTo(x(in), y(out)) : curve.lineTo(x(in), y(out));
    }
    p.setClipRect(area, Qt::IntersectClip);
    p.setPen(QPen(curveColor(Widgets::STRIP_COLOR, m_bypassed), 2));
    p.drawPath(curve);

    // the threshold
    if (m_compressor.thresholdDb() < 0.0)
    {
        double t = m_compressor.thresholdDb();
        p.setPen(Qt::NoPen);
        p.setBrush(curveColor(Widgets::STRIP_COLOR, m_bypassed));
        p.drawEllipse(QPointF(x(t), y(m_compressor.outputDb(t))), 3, 3);
    }
    Widgets::endDisplayPanel(p, rect());
}

// --- EQ -----------------------------------------------------------------------------------------------------------

EqGraph::EqGraph(Device* device, uint16_t strip, QWidget* parent) : QWidget(parent), m_device(device), m_strip(strip)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setToolTip(tr("EQ response: 20 Hz .. 20 kHz, -20 .. +20 dB (without the total gain)"));
    followAll(this, device, strip,
              {"FxEqLsvFreq", "FxEqLsvGain", "FxEqMidFreq", "FxEqMidGain", "FxEqMidQual", "FxEqHsvFreq",
               "FxEqHsvGain", "FxEqBypass"},
              [this] { refresh(); });
    refresh();
}

QSize EqGraph::sizeHint() const
{
    return QSize(480, 132);
}

QSize EqGraph::minimumSizeHint() const
{
    return QSize(240, 132);
}

QColor EqGraph::bandColor(Eq::Band band)
{
    switch (band)
    {
    case Eq::Low:
        return QColor(80, 150, 240);
    case Eq::Mid:
        return QColor(90, 190, 90);
    case Eq::High:
        return QColor(220, 90, 80);
    case Eq::All:
        break;
    }
    return Widgets::STRIP_COLOR;
}

void EqGraph::refresh()
{
    m_eq = Eq(eqSettings(*m_device, m_strip));
    m_bypassed = m_device->value("FxEqBypass", m_strip).value_or(0) != 0;
    update();
}

void EqGraph::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QRectF area = Widgets::beginDisplayPanel(p, rect(), isEnabled());
    auto x = [&](double hz) { return area.left() + area.width() * std::log10(hz / EQ_MIN_HZ) / EQ_DECADES; };
    auto y = [&](double db) { return area.center().y() - area.height() / 2 * db / EQ_RANGE_DB; };
    auto frequencyAt = [&](double px) { return EQ_MIN_HZ * std::pow(10.0, EQ_DECADES * (px - area.left()) / area.width()); };

    // the grid: 100 Hz, 1 kHz, 10 kHz with the steps between; 0 and ±6, ±12, ±18 dB
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.75);
    p.setFont(small);
    for (double decade : {10.0, 100.0, 1000.0, 10000.0})
    {
        for (int step = 1; step <= 9; ++step)
        {
            double hz = decade * step;
            if (hz <= EQ_MIN_HZ || hz >= EQ_MIN_HZ * std::pow(10.0, EQ_DECADES))
            {
                continue;
            }
            p.setPen(Widgets::displayGrid(step == 1));
            p.drawLine(QPointF(x(hz), area.top()), QPointF(x(hz), area.bottom()));
            if (step == 1)
            {
                p.setPen(Widgets::displayLabel());
                QString label = hz >= 1000.0 ? QString("%1k").arg(hz / 1000.0) : QString::number(hz);
                p.drawText(QPointF(x(hz) + 3, area.bottom() - 3), label);
            }
        }
    }
    for (int db = -18; db <= 18; db += 6)
    {
        p.setPen(Widgets::displayGrid(db == 0));
        p.drawLine(QPointF(area.left(), y(db)), QPointF(area.right(), y(db)));
        if (db != 0 && std::abs(db) != 18)
        {
            p.setPen(Widgets::displayLabel());
            p.drawText(QPointF(area.left() + 3, y(db) - 2), QString::asprintf("%+d", db));
        }
    }

    p.setRenderHint(QPainter::Antialiasing);
    p.setClipRect(area, Qt::IntersectClip);
    auto curve = [&](Eq::Band band) {
        QPainterPath path;
        for (int px = int(area.left()); px <= int(area.right()) + 1; ++px)
        {
            double db = std::clamp(m_eq.responseDb(frequencyAt(px), band), -EQ_RANGE_DB - 1, EQ_RANGE_DB + 1);
            px == int(area.left()) ? path.moveTo(px, y(db)) : path.lineTo(px, y(db));
        }
        return path;
    };

    // each band thin, the total over them, filled from 0 dB
    for (Eq::Band band : {Eq::Low, Eq::Mid, Eq::High})
    {
        QColor c = curveColor(bandColor(band), m_bypassed);
        c.setAlpha(170);
        p.setPen(QPen(c, 1));
        p.drawPath(curve(band));
    }
    QPainterPath total = curve(Eq::All);
    QPainterPath fill = total;
    fill.lineTo(area.right() + 1, y(0));
    fill.lineTo(area.left(), y(0));
    fill.closeSubpath();
    QColor c = curveColor(Widgets::STRIP_COLOR, m_bypassed);
    QColor translucent = c;
    translucent.setAlpha(50);
    p.fillPath(fill, translucent);
    p.setPen(QPen(c, 2));
    p.drawPath(total);

    // the bands' points: frequency and gain
    p.setPen(QPen(Widgets::displayBackground(isEnabled()), 1));
    for (Eq::Band band : {Eq::Low, Eq::Mid, Eq::High})
    {
        p.setBrush(curveColor(bandColor(band), m_bypassed));
        p.drawEllipse(QPointF(x(m_eq.frequency(band)), y(m_eq.gainDb(band))), 4, 4);
    }
    Widgets::endDisplayPanel(p, rect());
}
