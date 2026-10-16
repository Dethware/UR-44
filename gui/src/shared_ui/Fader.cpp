#include "Fader.h"

#include <QPainter>
#include <QStyle>
#include <QStyleOptionSlider>

#include <algorithm>
#include <cstdlib>

namespace
{
constexpr int MARK = 4;       // a mark's length on each side of the track
constexpr int MAJOR_MARK = 7;
constexpr int GAP = 3;        // between the track and the marks
constexpr int TRACK_HALF = 4; // the track's half width the marks keep clear of
}

Fader::Fader(QWidget* parent) : QSlider(Qt::Vertical, parent)
{
}

void Fader::setMarks(const QList<Mark>& marks)
{
    m_marks = marks;
    updateGeometry();
    update();
}

int Fader::labelWidth() const
{
    QFont f = font();
    f.setPointSizeF(f.pointSizeF() * 0.7);
    QFontMetrics fm(f);
    int width = 0;
    for (const Mark& m : m_marks)
    {
        width = std::max(width, fm.horizontalAdvance(m.label));
    }
    return width ? width + 2 : 0;
}

QSize Fader::sizeHint() const
{
    QSize size = QSlider::sizeHint();
    // room for the marks on both sides (and the labels on the left), keeping the track in the middle
    const int side = labelWidth() + GAP + MAJOR_MARK + TRACK_HALF;
    return QSize(std::max(size.width(), 2 * side), size.height());
}

QSize Fader::minimumSizeHint() const
{
    QSize size = QSlider::minimumSizeHint();
    return QSize(sizeHint().width(), size.height());
}

int Fader::yForValue(int v) const
{
    QStyleOptionSlider opt;
    initStyleOption(&opt);
    const QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
    const QRect handle = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);
    const int span = groove.height() - handle.height();
    // opt.upsideDown already says which way the slider runs (a vertical one has its minimum at the bottom)
    const int offset = QStyle::sliderPositionFromValue(minimum(), maximum(), v, span, opt.upsideDown);
    return groove.top() + offset + handle.height() / 2;
}

void Fader::paintEvent(QPaintEvent* event)
{
    if (!m_marks.isEmpty())
    {
        QPainter p(this);
        const QColor color = palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::Mid);
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() * 0.7);
        p.setFont(f);
        const int centre = width() / 2;
        const int textHeight = p.fontMetrics().height();
        // labels that would overlap one already drawn are left out (the marks stay); major ones first
        QList<int> labelled;
        auto fits = [&labelled, textHeight](int y) {
            return std::none_of(labelled.cbegin(), labelled.cend(),
                                [y, textHeight](int other) { return std::abs(other - y) < textHeight - 2; });
        };
        QList<Mark> ordered = m_marks;
        std::stable_partition(ordered.begin(), ordered.end(), [](const Mark& m) { return m.major; });
        for (const Mark& m : std::as_const(ordered))
        {
            const int y = yForValue(m.value);
            const int length = m.major ? MAJOR_MARK : MARK;
            p.setPen(QPen(color, m.major ? 2 : 1));
            const int inner = TRACK_HALF + GAP;
            p.drawLine(centre - inner - length, y, centre - inner, y);
            p.drawLine(centre + inner, y, centre + inner + length, y);
            if (!m.label.isEmpty() && fits(y))
            {
                labelled.append(y);
                const int right = centre - inner - MAJOR_MARK - 2;
                const QRect box(0, y - p.fontMetrics().height() / 2, right, p.fontMetrics().height());
                p.setPen(palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::PlaceholderText));
                p.drawText(box, Qt::AlignRight | Qt::AlignVCenter, m.label);
            }
        }
    }
    // the track and handle over the marks
    QSlider::paintEvent(event);
}
