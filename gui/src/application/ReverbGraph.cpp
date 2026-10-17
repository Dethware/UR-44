#include "ReverbGraph.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

#include "Bindings.h"
#include "Device.h"
#include "Widgets.h"

using namespace UR44;
using namespace UR44::ReverbShape;

namespace
{
// dspMixFx's layout (YSReverbGraph): a time axis of 280 px; the depth, 20 Hz .. 20 kHz, shifts by 90 px right and
// up; the level, -60 .. 0 dB, is 84 px high
constexpr double TIME_SPAN = 280.0;
constexpr double DEPTH_SPAN = 90.0;
constexpr double LEVEL_SPAN = 84.0;
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

QString timeLabel(double ms)
{
    return ms >= 1000.0 ? QString("%1 s").arg(ms / 1000.0) : QString("%1 ms").arg(ms);
}
}

ReverbGraph::ReverbGraph(Device* device, QWidget* parent) : QWidget(parent), m_device(device)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setToolTip(tr("The reverb's decay at 100 Hz, 1 kHz and 10 kHz (front to back), filled brighter with more "
                  "diffusion. On the floor: when the reverb ends at each frequency; on the left wall: the HPF and "
                  "LPF. The time range follows the reverb time."));
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
    m_shape = Shape(settings(*m_device));
    m_rangeMs = TIME_RANGES_MS[autoZoom(m_shape.reverbTimeMs())];
    update();
}

QPointF ReverbGraph::project(double time, double depth, double level) const
{
    const double width = TIME_SPAN + DEPTH_SPAN;
    const double height = DEPTH_SPAN + LEVEL_SPAN;
    return QPointF(m_area.left() + m_area.width() * (TIME_SPAN * time + DEPTH_SPAN * depth) / width,
                   m_area.bottom() - m_area.height() * (DEPTH_SPAN * depth + LEVEL_SPAN * level) / height);
}

void ReverbGraph::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QRectF panel = Widgets::beginDisplayPanel(p, rect(), isEnabled());
    m_area = panel.adjusted(LABEL_MARGIN + 6, 8, -8, -LABEL_MARGIN);
    p.setRenderHint(QPainter::Antialiasing);
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.75);
    p.setFont(small);

    auto levelOf = [](double db) { return std::clamp((db - MIN_LEVEL_DB) / -MIN_LEVEL_DB, 0.0, 1.0); };
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

    Widgets::endDisplayPanel(p, rect());
}
