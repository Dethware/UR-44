#include "ReverbGraph.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

#include "Bindings.h"
#include "ChannelStripCurves.h"
#include "Device.h"
#include "Parameters.h"
#include "ReverbTime.h"
#include "Widgets.h"

using namespace UR44;
using namespace UR44::ReverbShape;

namespace
{
// The 3D layout. The depth axis (20 Hz .. 20 kHz) runs up and to the right at this angle, whatever the size; it takes
// DEPTH_SHARE of the height and the level (-60 .. 0 dB) the rest, and the time axis the width it leaves. dspMixFx's
// graph (YSReverbGraph) is 45°, with the depth 90 px up and the level 84 px.
constexpr double DEPTH_ANGLE_DEGREES = 45.0;
constexpr double DEPTH_SHARE = 90.0 / (90.0 + 84.0);
// The time axis keeps at least this share of the width (the depth gets shorter, at the same angle, if it must)
constexpr double MIN_TIME_SHARE = 0.4;
constexpr double MIN_HZ = 20.0;
constexpr double DECADES = 3.0;
// The curves' resolution: at least the editor's (280 points per line, 90 across the frequencies)
constexpr int LINE_POINTS = 280;
constexpr int FREQUENCY_POINTS = 90;
// Room for the labels under the floor and left of the wall
constexpr double LABEL_MARGIN = 14.0;

double depthOf(double hz)
{
    return std::log10(hz / MIN_HZ) / DECADES;
}

// The decay lines' colours: dspMixFx's hues (blue, azure, sky, front to back), brighter for contrast on the dark
// panel (the originals, 0 0 255 and on, are too dark there). The fill is opaque, so the lines in front hide the ones
// behind, and brighter with more diffusion, like dspMixFx's (kGraph_PolyColors)
const QColor LINE_COLORS[3] = {QColor(100, 130, 255), QColor(50, 170, 255), QColor(90, 220, 255)};

QColor mix(const QColor& a, const QColor& b, double t)
{
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t), float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t));
}

QColor fillColor(int line, int diffusion, bool enabled)
{
    return mix(Widgets::displayBackground(enabled), LINE_COLORS[line], 0.22 + 0.033 * diffusion);
}

QColor netColor(int line, int diffusion, bool enabled)
{
    return mix(Widgets::displayBackground(enabled), LINE_COLORS[line], 0.42 + 0.033 * diffusion);
}

const QColor TIME_COLOR(255, 200, 60);   // the reverb's end, on the floor
const QColor FILTER_COLOR(120, 220, 120); // the filters, on the wall

double levelOf(double db)
{
    return std::clamp((db - MIN_LEVEL_DB) / -MIN_LEVEL_DB, 0.0, 1.0);
}

double cutoff(const char* parameter, int32_t value)
{
    return ChannelStripCurves::valueNumber(parameter, value);
}

const QColor HANDLE_COLOR(235, 235, 235); // the 1 kHz line's: initial delay, decay, reverb time
constexpr double HANDLE_RADIUS = 4.0;
constexpr double GRAB_RADIUS = 8.0;

QString timeLabel(double ms)
{
    return ms >= 1000.0 ? QString("%1 s").arg(ms / 1000.0) : QString("%1 ms").arg(ms);
}
}

ReverbGraph::ReverbGraph(Device* device, QWidget* parent) : QWidget(parent), m_device(device)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    for (const char* name : {"FxRevType", "FxRevTime", "FxRevIniDly", "FxRevDecay", "FxRevRoomSize", "FxRevDiff",
                             "FxRevHpf", "FxRevLpf", "FxRevHighRatio", "FxRevLowRatio", "FxRevLowFreq"})
    {
        Bindings::follow(this, device, name, 0, [this](int32_t) { refresh(); });
    }
    refresh();
}

QSize ReverbGraph::sizeHint() const
{
    return QSize(480, 220);
}

QSize ReverbGraph::minimumSizeHint() const
{
    return QSize(320, 160);
}

void ReverbGraph::refresh()
{
    m_settings = settings(*m_device);
    m_shape = Shape(m_settings);
    applyZoom();
    update();
}

void ReverbGraph::applyZoom()
{
    // the zoom holds still while dragging (dspMixFx's edit zoom), so the handle stays under the mouse
    if (m_drag.handle != Handle::None)
    {
        return;
    }
    if (m_autoZoom)
    {
        m_zoom = autoZoom(m_shape.reverbTimeMs());
    }
    m_rangeMs = TIME_RANGES_MS[m_zoom];
}

QRectF ReverbGraph::plotArea() const
{
    return Widgets::displayPanelArea(rect()).adjusted(LABEL_MARGIN + 6, 8, -8, -LABEL_MARGIN);
}

QRectF ReverbGraph::autoBadge() const
{
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.75);
    QFontMetricsF metrics(small);
    QRectF panel = Widgets::displayPanelArea(rect());
    double w = metrics.horizontalAdvance("AUTO") + 10, h = metrics.height() + 4;
    return QRectF(panel.right() - w - 4, panel.top() + 4, w, h);
}

QPointF ReverbGraph::project(double time, double depth, double level) const
{
    const QRectF area = plotArea();
    const double angle = DEPTH_ANGLE_DEGREES * 3.14159265358979323846 / 180.0;
    // the depth axis' rise and run, at the angle
    double rise = area.height() * DEPTH_SHARE;
    double run = rise / std::tan(angle);
    if (run > area.width() * (1.0 - MIN_TIME_SHARE))
    {
        run = area.width() * (1.0 - MIN_TIME_SHARE);
        rise = run * std::tan(angle);
    }
    const double timeLength = area.width() - run;
    const double levelHeight = area.height() * (1.0 - DEPTH_SHARE);
    return QPointF(area.left() + timeLength * time + run * depth,
                   area.bottom() - rise * depth - levelHeight * level);
}

// --- handles

namespace
{
struct HandleInfo
{
    const char* name;
    const char* parameter;
    int32_t Settings::*field;
    bool alongTime; // else along the depth
};

HandleInfo info(int handle)
{
    switch (handle)
    {
    case 1:
        return {QT_TRANSLATE_NOOP("ReverbGraph", "Initial Delay"), "FxRevIniDly", &Settings::initialDelay, true};
    case 2:
        return {QT_TRANSLATE_NOOP("ReverbGraph", "Decay"), "FxRevDecay", &Settings::decay, true};
    case 3:
        return {QT_TRANSLATE_NOOP("ReverbGraph", "Reverb Time"), "FxRevTime", &Settings::time, true};
    case 4:
        return {QT_TRANSLATE_NOOP("ReverbGraph", "Low Ratio"), "FxRevLowRatio", &Settings::lowRatio, true};
    case 5:
        return {QT_TRANSLATE_NOOP("ReverbGraph", "High Ratio"), "FxRevHighRatio", &Settings::highRatio, true};
    case 6:
        return {QT_TRANSLATE_NOOP("ReverbGraph", "Low Frequency"), "FxRevLowFreq", &Settings::lowFreq, false};
    case 7:
        return {QT_TRANSLATE_NOOP("ReverbGraph", "HPF"), "FxRevHpf", &Settings::hpf, false};
    default:
        return {QT_TRANSLATE_NOOP("ReverbGraph", "LPF"), "FxRevLpf", &Settings::lpf, false};
    }
}

const double DEPTH_100 = depthOf(LINE_FREQUENCIES[0]);
const double DEPTH_1K = depthOf(LINE_FREQUENCIES[1]);
}

ReverbGraph::Point ReverbGraph::handlePoint(Handle handle, const Shape& shape, const Settings& s) const
{
    switch (handle)
    {
    case Handle::InitialDelay:
        // shared by all three lines: on the front one, where nothing covers it
        return {shape.initialDelayMs() / m_rangeMs, DEPTH_100, 0.0};
    case Handle::Decay:
        return {shape.decayMs() / m_rangeMs, DEPTH_1K, levelOf(shape.levelDb(1, shape.decayMs()))};
    case Handle::ReverbTime:
        return {shape.reverbTimeMs() / m_rangeMs, DEPTH_1K, 0.0};
    case Handle::LowRatio:
        return {shape.endMs(MIN_HZ) / m_rangeMs, 0.0, 0.0};
    case Handle::HighRatio:
        return {shape.endMs(MIN_HZ * std::pow(10.0, DECADES)) / m_rangeMs, 1.0, 0.0};
    case Handle::LowFreq:
    {
        double hz = cutoff("FxRevLowFreq", s.lowFreq);
        return {shape.endMs(hz) / m_rangeMs, depthOf(hz), 0.0};
    }
    case Handle::Hpf:
    case Handle::Lpf:
    {
        double hz = handle == Handle::Hpf ? cutoff("FxRevHpf", s.hpf) : cutoff("FxRevLpf", s.lpf);
        return {0.0, depthOf(hz), levelOf(shape.filterDb(hz))};
    }
    case Handle::None:
        break;
    }
    return {0, 0, 0};
}

QPointF ReverbGraph::handlePosition(Handle handle) const
{
    Point at = handlePoint(handle, m_shape, m_settings);
    return project(std::clamp(at.time, 0.0, 1.0), at.depth, at.level);
}

ReverbGraph::Handle ReverbGraph::handleAt(const QPointF& pos) const
{
    Handle best = Handle::None;
    double bestDistance = GRAB_RADIUS;
    for (int h = int(Handle::InitialDelay); h <= int(Handle::Lpf); ++h)
    {
        double d = QLineF(pos, handlePosition(Handle(h))).length();
        if (d <= bestDistance)
        {
            best = Handle(h);
            bestDistance = d;
        }
    }
    return best;
}

void ReverbGraph::moveHandle(Handle handle, const QPointF& pos)
{
    const HandleInfo h = info(int(handle));
    const Point at = handlePoint(handle, m_shape, m_settings);
    // the target along the handle's axis, through where it is
    double target;
    if (h.alongTime)
    {
        QPointF origin = project(0, at.depth, at.level);
        QPointF axis = project(1, at.depth, at.level) - origin;
        target = QPointF::dotProduct(pos - origin, axis) / QPointF::dotProduct(axis, axis);
    }
    else
    {
        QPointF origin = project(at.time, 0, at.level);
        QPointF axis = project(at.time, 1, at.level) - origin;
        target = QPointF::dotProduct(pos - origin, axis) / QPointF::dotProduct(axis, axis);
    }
    // the parameter's value that puts the handle nearest
    const Parameter& p = Parameters::instance()[h.parameter];
    int32_t best = m_settings.*h.field;
    double bestDistance = 1e300;
    for (int32_t v = p.min; v <= p.max; ++v)
    {
        Settings s = m_settings;
        s.*h.field = v;
        Point candidate = handlePoint(handle, Shape(s), s);
        double d = std::abs((h.alongTime ? candidate.time : candidate.depth) - target);
        if (d < bestDistance)
        {
            best = v;
            bestDistance = d;
        }
    }
    if (best != m_settings.*h.field)
    {
        m_device->setParameter(h.parameter, 0, best);
    }
}

void ReverbGraph::showValues(Handle handle, const QPoint& globalPos)
{
    if (handle == Handle::None)
    {
        QToolTip::hideText();
        return;
    }
    const HandleInfo h = info(int(handle));
    int32_t value = m_settings.*h.field;
    QString text = handle == Handle::ReverbTime
                       ? ReverbTime::text(m_settings.type, m_settings.roomSize, value) + " s"
                       : Parameters::instance()[h.parameter].text(value);
    QToolTip::showText(globalPos, tr("%1: %2").arg(tr(h.name), text), this);
}

// --- events

void ReverbGraph::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
    {
        QWidget::mousePressEvent(event);
        return;
    }
    if (autoBadge().contains(event->position()))
    {
        m_autoZoom = !m_autoZoom;
        applyZoom();
        update();
        return;
    }
    Handle handle = handleAt(event->position());
    if (handle == Handle::None)
    {
        QWidget::mousePressEvent(event);
        return;
    }
    m_drag = Drag{handle, event->position(), handlePosition(handle), bool(event->modifiers() & Qt::ShiftModifier)};
    showValues(handle, event->globalPosition().toPoint());
    update();
}

void ReverbGraph::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF pos = event->position();
    if (m_drag.handle == Handle::None)
    {
        Handle hover = handleAt(pos);
        bool badge = hover == Handle::None && autoBadge().contains(pos);
        if (hover != m_hover || badge != m_hoverBadge)
        {
            m_hover = hover;
            m_hoverBadge = badge;
            if (badge)
            {
                setCursor(Qt::PointingHandCursor);
                QToolTip::showText(event->globalPosition().toPoint(), badgeToolTip(), this);
            }
            else if (hover == Handle::None)
            {
                unsetCursor();
                QToolTip::hideText();
            }
            else
            {
                setCursor(info(int(hover)).alongTime ? Qt::SizeHorCursor : Qt::SizeBDiagCursor);
                showValues(hover, event->globalPosition().toPoint());
            }
            update();
        }
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
    moveHandle(m_drag.handle, m_drag.origin + (pos - m_drag.press) * (fine ? 0.25 : 1.0));
    showValues(m_drag.handle, event->globalPosition().toPoint());
}

void ReverbGraph::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_drag.handle == Handle::None || event->button() != Qt::LeftButton)
    {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    m_drag = Drag{};
    applyZoom(); // what the auto zoom held back while dragging
    m_hover = handleAt(event->position());
    if (m_hover == Handle::None)
    {
        unsetCursor();
        QToolTip::hideText();
    }
    update();
}

void ReverbGraph::wheelEvent(QWheelEvent* event)
{
    if (m_drag.handle != Handle::None)
    {
        event->accept();
        return;
    }
    // whole notches; a touchpad's small steps add up. Up zooms in (a shorter range).
    m_wheelRemainder += event->angleDelta().y();
    int notches = m_wheelRemainder / 120;
    m_wheelRemainder %= 120;
    if (notches != 0)
    {
        m_autoZoom = false;
        m_zoom = std::clamp(m_zoom - notches, 0, int(std::size(TIME_RANGES_MS)) - 1);
        applyZoom();
        update();
    }
    event->accept();
}

QString ReverbGraph::badgeToolTip() const
{
    return m_autoZoom ? tr("The time range follows the reverb time; the wheel zooms by hand")
                      : tr("Follow the reverb time again");
}

bool ReverbGraph::event(QEvent* event)
{
    // Tooltips only for what's under the mouse (a handle, the AUTO badge), none for the graph as a whole
    if (event->type() == QEvent::ToolTip)
    {
        auto* help = static_cast<QHelpEvent*>(event);
        if (m_hoverBadge)
        {
            QToolTip::showText(help->globalPos(), badgeToolTip(), this);
        }
        else
        {
            showValues(m_drag.handle != Handle::None ? m_drag.handle : m_hover, help->globalPos());
        }
        return true;
    }
    return QWidget::event(event);
}

void ReverbGraph::leaveEvent(QEvent* event)
{
    if (m_drag.handle == Handle::None)
    {
        m_hover = Handle::None;
        m_hoverBadge = false;
        unsetCursor();
        update();
    }
    QWidget::leaveEvent(event);
}

void ReverbGraph::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QRectF panel = Widgets::beginDisplayPanel(p, rect(), isEnabled());
    p.setRenderHint(QPainter::Antialiasing);
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.75);
    p.setFont(small);

    auto timeOf = [this](double ms) { return std::clamp(ms / m_rangeMs, 0.0, 1.0); };

    // The floor: the time grid across the depth, the decay lines' frequencies along the time
    QPolygonF floor{project(0, 0, 0), project(1, 0, 0), project(1, 1, 0), project(0, 1, 0)};
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 10));
    p.drawPolygon(floor);
    for (int i = 0; i <= 10; ++i)
    {
        p.setPen(Widgets::displayGrid(i % 5 == 0));
        p.drawLine(project(i / 10.0, 0, 0), project(i / 10.0, 1, 0));
    }
    p.setPen(Widgets::displayGrid(false));
    p.drawLine(project(0, 0, 0), project(1, 0, 0));
    p.drawLine(project(0, 1, 0), project(1, 1, 0));
    // The left wall, with -20 and -40 dB
    p.setPen(Widgets::displayGrid(true));
    p.drawPolyline(QPolygonF{project(0, 0, 0), project(0, 0, 1), project(0, 1, 1), project(0, 1, 0)});
    p.setPen(Widgets::displayGrid(false));
    for (double db : {-20.0, -40.0})
    {
        p.drawLine(project(0, 0, levelOf(db)), project(0, 1, levelOf(db)));
    }

    // The labels: time along the front, the lines' frequencies, the levels on the wall
    p.setPen(Widgets::displayLabel());
    for (int i = 0; i <= 2; ++i)
    {
        QPointF at = project(i / 2.0, 0, 0);
        QString text = timeLabel(m_rangeMs * i / 2.0);
        double w = p.fontMetrics().horizontalAdvance(text);
        double x = i == 0 ? at.x() : i == 2 ? at.x() - w : at.x() - w / 2;
        p.drawText(QPointF(x, at.y() + p.fontMetrics().ascent() + 2), text);
    }
    const char* const LINE_LABELS[3] = {"100", "1k", "10k"};
    for (int line = 0; line < 3; ++line)
    {
        QPointF at = project(1, depthOf(LINE_FREQUENCIES[line]), 0);
        p.drawText(at + QPointF(3, 3), LINE_LABELS[line]);
    }
    for (double db : {0.0, -20.0, -40.0})
    {
        QString text = QString::number(db);
        QPointF at = project(0, 0, levelOf(db));
        p.drawText(at + QPointF(-p.fontMetrics().horizontalAdvance(text) - 3, p.fontMetrics().ascent() / 2.0 - 1),
                   text);
    }

    // The decay lines, back to front: filled from the floor (brighter with more diffusion), hatched, outlined
    // Sampled from 0 at the time axis' resolution (at least the editor's 280 points), with the onset (the end of
    // the initial delay) and the decay's knee exactly: so the rise from the floor and the corner always show. (The
    // editor samples in step with the knee instead, from up to a step after 0, which can skip the rise.)
    const double axisPixels = QLineF(project(0, 0, 0), project(1, 0, 0)).length();
    const int points = std::max(LINE_POINTS, int(axisPixels));
    QList<double> times;
    for (int i = 0; i <= points; ++i)
    {
        times << m_rangeMs * i / points;
    }
    for (double ms : {m_shape.initialDelayMs(), m_shape.decayMs()})
    {
        if (ms < m_rangeMs)
        {
            times << ms;
        }
    }
    std::sort(times.begin(), times.end());
    const int diffusion = std::clamp(m_shape.diffusion(), 0, 10);
    for (int line = 2; line >= 0; --line)
    {
        const double depth = depthOf(LINE_FREQUENCIES[line]);
        QPolygonF curve;
        for (double ms : times)
        {
            curve << project(ms / m_rangeMs, depth, levelOf(m_shape.levelDb(line, ms)));
        }
        QPolygonF area = curve;
        area.prepend(project(0, depth, 0));
        area << project(1, depth, 0);
        p.setPen(Qt::NoPen);
        p.setBrush(fillColor(line, diffusion, isEnabled()));
        p.drawPolygon(area);
        p.setPen(QPen(netColor(line, diffusion, isEnabled()), 1));
        for (int i = (line + 1) % 3; i < curve.size(); i += 3)
        {
            QPointF base(curve[i].x(), project(0, depth, 0).y());
            p.drawLine(base, curve[i]);
        }
        p.setPen(QPen(LINE_COLORS[line], 2));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(curve);
    }

    // The reverb's end per frequency, on the floor
    QPolygonF end;
    for (int i = 0; i < FREQUENCY_POINTS; ++i)
    {
        double depth = double(i) / FREQUENCY_POINTS;
        end << project(timeOf(m_shape.endMs(MIN_HZ * std::pow(10.0, DECADES * depth))), depth, 0);
    }
    p.setPen(QPen(TIME_COLOR, 1.5));
    p.drawPolyline(end);

    // The filters, on the left wall
    QPolygonF filter;
    for (int i = 0; i < FREQUENCY_POINTS; ++i)
    {
        double depth = double(i) / FREQUENCY_POINTS;
        filter << project(0, depth, levelOf(m_shape.filterDb(MIN_HZ * std::pow(10.0, DECADES * depth))));
    }
    p.setPen(QPen(FILTER_COLOR, 1.5));
    p.drawPolyline(filter);

    // The handles, the one under the mouse or being dragged bigger, with a ring
    for (int h = int(Handle::InitialDelay); h <= int(Handle::Lpf); ++h)
    {
        Handle handle = Handle(h);
        QColor color = h <= int(Handle::ReverbTime) ? HANDLE_COLOR : h <= int(Handle::LowFreq) ? TIME_COLOR : FILTER_COLOR;
        bool active = m_hover == handle || m_drag.handle == handle;
        double r = active ? HANDLE_RADIUS + 1.5 : HANDLE_RADIUS;
        p.setPen(active ? QPen(QColor(255, 255, 255, 220), 1.5) : QPen(Widgets::displayBackground(isEnabled()), 1));
        p.setBrush(color);
        p.drawEllipse(handlePosition(handle), r, r);
    }

    // The AUTO zoom badge: lit while the range follows the reverb time
    {
        QRectF badge = autoBadge();
        QColor lit = Widgets::REVX_COLOR;
        p.setPen(QPen(m_autoZoom ? lit : Widgets::displayGrid(true), 1));
        p.setBrush(m_autoZoom ? QColor(lit.red(), lit.green(), lit.blue(), 60) : QColor(0, 0, 0, 0));
        p.drawRoundedRect(badge, 3, 3);
        QColor text = m_autoZoom ? lit : Widgets::displayLabel();
        if (m_hoverBadge)
        {
            text = text.lighter(130);
        }
        p.setPen(text);
        p.drawText(badge, Qt::AlignCenter, "AUTO");
    }

    Widgets::endDisplayPanel(p, rect());
}
