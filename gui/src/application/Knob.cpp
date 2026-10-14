#include "Knob.h"

#include <QMouseEvent>
#include <QPainter>

#include <cmath>
#include <QtMath>

namespace
{
constexpr double SWEEP = 270.0;
constexpr double START = 225.0; // degrees, counter-clockwise from 3 o'clock: lower left
}

Knob::Knob(const QColor& color, QWidget* parent) : QAbstractSlider(parent), m_color(color)
{
    setFocusPolicy(Qt::WheelFocus);
    setFixedSize(sizeHint());
}

void Knob::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
    {
        QAbstractSlider::mousePressEvent(event);
        return;
    }
    m_dragStart = event->position().toPoint();
    m_dragStartValue = value();
    setSliderDown(true);
    event->accept();
}

void Knob::mouseMoveEvent(QMouseEvent* event)
{
    if (!isSliderDown())
    {
        QAbstractSlider::mouseMoveEvent(event);
        return;
    }
    // up and right increase; Shift for fine control
    QPoint delta = event->position().toPoint() - m_dragStart;
    double pixels = (event->modifiers() & Qt::ShiftModifier) ? DRAG_PIXELS * 4.0 : DRAG_PIXELS;
    double steps = (delta.x() - delta.y()) * (maximum() - minimum()) / pixels;
    setSliderPosition(m_dragStartValue + int(std::lround(steps)));
    event->accept();
}

void Knob::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && isSliderDown())
    {
        setSliderDown(false);
        event->accept();
        return;
    }
    QAbstractSlider::mouseReleaseEvent(event);
}

void Knob::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const qreal size = qMin(width(), height()) - 2;
    const QRectF outer((width() - size) / 2.0, (height() - size) / 2.0, size, size);
    const QRectF cap = outer.adjusted(size * 0.18, size * 0.18, -size * 0.18, -size * 0.18);
    const QColor color = isEnabled() ? m_color : palette().color(QPalette::Disabled, QPalette::Button);

    auto angleOf = [this](int v) {
        double span = maximum() - minimum();
        double t = span > 0 ? (v - minimum()) / span : 0.0;
        return START - t * SWEEP;
    };

    // track and value arc
    QPen track(palette().color(QPalette::Mid), size * 0.08, Qt::SolidLine, Qt::FlatCap);
    p.setPen(track);
    p.drawArc(outer.adjusted(2, 2, -2, -2), int(START * 16), int(-SWEEP * 16));
    int origin = (minimum() < 0 && maximum() > 0) ? 0 : minimum();
    QPen arc(color, size * 0.08, Qt::SolidLine, Qt::FlatCap);
    p.setPen(arc);
    double a0 = angleOf(origin);
    double a1 = angleOf(value());
    p.drawArc(outer.adjusted(2, 2, -2, -2), int(a0 * 16), int((a1 - a0) * 16));

    // cap
    QRadialGradient gradient(cap.center() - QPointF(cap.width() * 0.15, cap.height() * 0.2), cap.width() * 0.7);
    gradient.setColorAt(0.0, color.lighter(140));
    gradient.setColorAt(1.0, color.darker(160));
    p.setPen(QPen(color.darker(220), 1));
    p.setBrush(gradient);
    p.drawEllipse(cap);

    // pointer
    double rad = qDegreesToRadians(a1);
    QPointF c = cap.center();
    QPointF tip(c.x() + std::cos(rad) * cap.width() * 0.45, c.y() - std::sin(rad) * cap.height() * 0.45);
    QPointF base(c.x() + std::cos(rad) * cap.width() * 0.12, c.y() - std::sin(rad) * cap.height() * 0.12);
    p.setPen(QPen(isEnabled() ? Qt::white : palette().color(QPalette::Disabled, QPalette::Text), 2, Qt::SolidLine,
                  Qt::RoundCap));
    p.drawLine(base, tip);

    if (hasFocus())
    {
        p.setPen(QPen(palette().color(QPalette::Highlight), 1, Qt::DotLine));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(outer.adjusted(0.5, 0.5, -0.5, -0.5));
    }
}
