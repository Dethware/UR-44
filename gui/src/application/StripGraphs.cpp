#include "StripGraphs.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <QWheelEvent>

#include <cmath>

#include "Bindings.h"
#include "Device.h"
#include "Parameters.h"
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

namespace
{
struct BandParameters
{
    const char* name; // for the tooltips
    const char* frequency;
    const char* gain;
};

const BandParameters BANDS[3] = {
    {QT_TRANSLATE_NOOP("EqGraph", "EQ Low"), "FxEqLsvFreq", "FxEqLsvGain"},
    {QT_TRANSLATE_NOOP("EqGraph", "EQ Mid"), "FxEqMidFreq", "FxEqMidGain"},
    {QT_TRANSLATE_NOOP("EqGraph", "EQ High"), "FxEqHsvFreq", "FxEqHsvGain"},
};
const char* const MID_Q = "FxEqMidQual";

constexpr double HANDLE_RADIUS = 4.0;
constexpr double GRAB_RADIUS = 8.0;
// Ctrl-drag: the movement that decides the direction
constexpr double AXIS_THRESHOLD = 4.0;

Eq::Band bandOf(int handle)
{
    return Eq::Band(handle); // Handle::Low .. High are 1 .. 3, like Eq::Low .. High
}
}

EqGraph::EqGraph(Device* device, uint16_t strip, QWidget* parent) : QWidget(parent), m_device(device), m_strip(strip)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    setToolTip(tr("EQ response: 20 Hz .. 20 kHz, -20 .. +20 dB (without the total gain).\n"
                  "Drag a point to set its band's frequency and gain (Shift: fine, Ctrl: along one axis). The mid "
                  "band's width handles, or the wheel over its point, set its width (Q)."));
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

// --- geometry

QRectF EqGraph::plotArea() const
{
    return Widgets::displayPanelArea(rect());
}

double EqGraph::xForFrequency(double hz) const
{
    QRectF area = plotArea();
    return area.left() + area.width() * std::log10(hz / EQ_MIN_HZ) / EQ_DECADES;
}

double EqGraph::frequencyForX(double x) const
{
    QRectF area = plotArea();
    return EQ_MIN_HZ * std::pow(10.0, EQ_DECADES * (x - area.left()) / area.width());
}

double EqGraph::yForGain(double db) const
{
    QRectF area = plotArea();
    return area.center().y() - area.height() / 2 * db / EQ_RANGE_DB;
}

double EqGraph::gainForY(double y) const
{
    QRectF area = plotArea();
    return (area.center().y() - y) / (area.height() / 2) * EQ_RANGE_DB;
}

QPointF EqGraph::handlePosition(Handle handle) const
{
    switch (handle)
    {
    case Handle::Low:
    case Handle::Mid:
    case Handle::High:
    {
        Eq::Band band = bandOf(int(handle));
        return QPointF(xForFrequency(m_eq.frequency(band)), yForGain(m_eq.gainDb(band)));
    }
    case Handle::MidWidthBelow:
    case Handle::MidWidthAbove:
    {
        // the band's edges at its effective Q, on its curve
        double q = valueNumber(MID_Q, m_device->value(MID_Q, m_strip).value_or(0)) * m_eq.midQScale();
        double half = octavesForQ(q) / 2.0;
        double hz = m_eq.frequency(Eq::Mid) * std::pow(2.0, handle == Handle::MidWidthBelow ? -half : half);
        return QPointF(xForFrequency(hz), yForGain(m_eq.responseDb(hz, Eq::Mid)));
    }
    case Handle::None:
        break;
    }
    return QPointF();
}

EqGraph::Handle EqGraph::handleAt(const QPointF& pos) const
{
    Handle best = Handle::None;
    double bestDistance = GRAB_RADIUS;
    for (Handle h : {Handle::Low, Handle::Mid, Handle::High, Handle::MidWidthBelow, Handle::MidWidthAbove})
    {
        double d = QLineF(pos, handlePosition(h)).length();
        if (d <= bestDistance)
        {
            best = h;
            bestDistance = d;
        }
    }
    return best;
}

// --- changes

void EqGraph::set(const char* parameter, int32_t value)
{
    if (m_device->value(parameter, m_strip) != value)
    {
        m_device->setParameter(parameter, m_strip, value);
    }
}

void EqGraph::moveHandle(Handle handle, const QPointF& pos, bool frequency, bool gain)
{
    if (handle == Handle::MidWidthBelow || handle == Handle::MidWidthAbove)
    {
        double octaves = 2.0 * std::abs(std::log2(frequencyForX(pos.x()) / m_eq.frequency(Eq::Mid)));
        set(MID_Q, nearestValue(MID_Q, qForOctaves(std::max(octaves, 0.01)) / m_eq.midQScale()));
        return;
    }
    const BandParameters& band = BANDS[int(handle) - 1];
    if (frequency)
    {
        set(band.frequency, nearestValue(band.frequency, frequencyForX(pos.x())));
    }
    if (gain)
    {
        set(band.gain, nearestValue(band.gain, gainForY(pos.y())));
    }
}

void EqGraph::stepWidth(int steps)
{
    const Parameter& p = Parameters::instance()[MID_Q];
    set(MID_Q, p.clamp(m_device->value(MID_Q, m_strip).value_or(p.defaultValue) + steps));
}

void EqGraph::showValues(Handle handle, const QPoint& globalPos)
{
    if (handle == Handle::None)
    {
        QToolTip::hideText();
        return;
    }
    const Parameters& parameters = Parameters::instance();
    Handle bandHandle = handle == Handle::MidWidthBelow || handle == Handle::MidWidthAbove ? Handle::Mid : handle;
    const BandParameters& band = BANDS[int(bandHandle) - 1];
    auto text = [&](const char* name) {
        const Parameter& p = parameters[name];
        return p.text(m_device->value(name, m_strip).value_or(p.defaultValue));
    };
    QString values = tr("%1: %2, %3").arg(tr(band.name), text(band.frequency), text(band.gain));
    if (bandHandle == Handle::Mid)
    {
        values += tr(", Q %1").arg(text(MID_Q));
    }
    QToolTip::showText(globalPos, values, this);
}

// --- events

void EqGraph::mousePressEvent(QMouseEvent* event)
{
    Handle handle = event->button() == Qt::LeftButton ? handleAt(event->position()) : Handle::None;
    if (handle == Handle::None)
    {
        QWidget::mousePressEvent(event);
        return;
    }
    m_drag = Drag{handle, event->position(), handlePosition(handle),
                  bool(event->modifiers() & Qt::ShiftModifier), event->modifiers() & Qt::ControlModifier ? 0 : -1};
    setCursor(handle == Handle::MidWidthBelow || handle == Handle::MidWidthAbove ? Qt::SizeHorCursor
                                                                                 : Qt::ClosedHandCursor);
    showValues(handle, event->globalPosition().toPoint());
    update();
}

void EqGraph::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF pos = event->position();
    if (m_drag.handle == Handle::None)
    {
        Handle hover = handleAt(pos);
        if (hover != m_hover)
        {
            m_hover = hover;
            if (hover == Handle::None)
            {
                unsetCursor();
            }
            else
            {
                setCursor(hover == Handle::MidWidthBelow || hover == Handle::MidWidthAbove ? Qt::SizeHorCursor
                                                                                          : Qt::OpenHandCursor);
            }
            showValues(hover, event->globalPosition().toPoint());
        }
        update(); // the width handles show while the mouse is over the graph
        return;
    }

    // Shift changes mid-drag: carry on from where the handle is
    bool fine = event->modifiers() & Qt::ShiftModifier;
    if (fine != m_drag.fine)
    {
        m_drag.press = pos;
        m_drag.origin = handlePosition(m_drag.handle);
        m_drag.fine = fine;
    }
    QPointF delta = (pos - m_drag.press) * (fine ? 0.25 : 1.0);
    if (m_drag.axis == 0 && std::max(std::abs(delta.x()), std::abs(delta.y())) >= AXIS_THRESHOLD)
    {
        m_drag.axis = std::abs(delta.x()) >= std::abs(delta.y()) ? 1 : 2;
    }
    if (m_drag.axis != 0)
    {
        moveHandle(m_drag.handle, m_drag.origin + delta, m_drag.axis != 2, m_drag.axis != 1);
    }
    showValues(m_drag.handle, event->globalPosition().toPoint());
}

void EqGraph::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_drag.handle == Handle::None || event->button() != Qt::LeftButton)
    {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    m_drag = Drag{};
    m_hover = handleAt(event->position());
    if (m_hover == Handle::None)
    {
        unsetCursor();
        QToolTip::hideText();
    }
    else
    {
        setCursor(m_hover == Handle::MidWidthBelow || m_hover == Handle::MidWidthAbove ? Qt::SizeHorCursor
                                                                                      : Qt::OpenHandCursor);
    }
    update();
}

void EqGraph::wheelEvent(QWheelEvent* event)
{
    // Only the mid point takes the wheel, for its width: it stays put under the mouse. A point whose frequency or
    // gain changed would run away from it (and so would the width handles).
    if (handleAt(event->position()) != Handle::Mid)
    {
        event->ignore();
        return;
    }
    // whole notches; a touchpad's small steps add up
    m_wheelRemainder += event->angleDelta().y();
    int notches = m_wheelRemainder / 120;
    m_wheelRemainder %= 120;
    if (notches != 0)
    {
        stepWidth(notches);
        showValues(Handle::Mid, event->globalPosition().toPoint());
    }
    event->accept();
}

void EqGraph::leaveEvent(QEvent* event)
{
    if (m_drag.handle == Handle::None)
    {
        m_hover = Handle::None;
        unsetCursor();
    }
    update();
    QWidget::leaveEvent(event);
}

// --- painting

void EqGraph::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QRectF area = Widgets::beginDisplayPanel(p, rect(), isEnabled());
    auto x = [this](double hz) { return xForFrequency(hz); };
    auto y = [this](double db) { return yForGain(db); };

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
            double db = std::clamp(m_eq.responseDb(frequencyForX(px), band), -EQ_RANGE_DB - 1, EQ_RANGE_DB + 1);
            px == int(area.left()) ? path.moveTo(px, y(db)) : path.lineTo(px, y(db));
        }
        return path;
    };

    // each band thin, the total over them, filled from 0 dB
    const bool midActive = m_hover == Handle::Mid || m_hover == Handle::MidWidthBelow ||
                           m_hover == Handle::MidWidthAbove || m_drag.handle == Handle::Mid ||
                           m_drag.handle == Handle::MidWidthBelow || m_drag.handle == Handle::MidWidthAbove;
    for (Eq::Band band : {Eq::Low, Eq::Mid, Eq::High})
    {
        QColor c = curveColor(bandColor(band), m_bypassed);
        bool active = int(m_drag.handle == Handle::None ? m_hover : m_drag.handle) == int(band) ||
                      (band == Eq::Mid && midActive);
        c.setAlpha(active ? 255 : 170);
        p.setPen(QPen(c, active ? 1.5 : 1));
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

    // the mid band's width handles, while the mouse is over the graph
    if (underMouse() || m_drag.handle != Handle::None)
    {
        QColor mid = curveColor(bandColor(Eq::Mid), m_bypassed);
        for (Handle h : {Handle::MidWidthBelow, Handle::MidWidthAbove})
        {
            QPointF at = handlePosition(h);
            bool active = m_hover == h || m_drag.handle == h;
            p.setPen(QPen(mid, active ? 2.5 : 1.5, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(at - QPointF(0, active ? 7 : 5), at + QPointF(0, active ? 7 : 5));
        }
    }

    // the bands' points: frequency and gain; the one under the mouse or being dragged bigger, with a ring
    for (Eq::Band band : {Eq::Low, Eq::Mid, Eq::High})
    {
        Handle h = Handle(int(band));
        bool active = m_hover == h || m_drag.handle == h;
        QPointF at = handlePosition(h);
        p.setPen(active ? QPen(QColor(255, 255, 255, 220), 1.5) : QPen(Widgets::displayBackground(isEnabled()), 1));
        p.setBrush(curveColor(bandColor(band), m_bypassed));
        p.drawEllipse(at, active ? HANDLE_RADIUS + 1.5 : HANDLE_RADIUS, active ? HANDLE_RADIUS + 1.5 : HANDLE_RADIUS);
    }
    Widgets::endDisplayPanel(p, rect());
}
