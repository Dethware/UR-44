#include "LevelMeter.h"

#include <QLinearGradient>
#include <QPainter>

#include "Protocol.h"

namespace
{
constexpr double FLOOR = UR44::Meters::FLOOR_DB;
// Meter scale: the top 40 dB get most of the height, like most hardware meters
constexpr double KNEE_DB = -40.0;
constexpr double KNEE_POS = 0.25;

double position(double db)
{
    if (db <= FLOOR)
    {
        return 0.0;
    }
    if (db >= 0.0)
    {
        return 1.0;
    }
    if (db < KNEE_DB)
    {
        return KNEE_POS * (db - FLOOR) / (KNEE_DB - FLOOR);
    }
    return KNEE_POS + (1.0 - KNEE_POS) * (db - KNEE_DB) / -KNEE_DB;
}
}

LevelMeter::LevelMeter(int channels, QWidget* parent) : QWidget(parent), m_channels(channels)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    clear();
}

void LevelMeter::setLevels(const QList<double>& levels, const QList<double>& holds)
{
    m_levels = levels;
    m_holds = holds;
    update();
}

void LevelMeter::clear()
{
    m_levels = QList<double>(m_channels, FLOOR);
    m_holds = QList<double>(m_channels, FLOOR);
    update();
}

QSize LevelMeter::sizeHint() const
{
    return QSize(6 * m_channels + 4, 200);
}

QSize LevelMeter::minimumSizeHint() const
{
    return QSize(6 * m_channels + 4, 60);
}

void LevelMeter::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    QRect area = rect().adjusted(1, 1, -1, -1);
    p.fillRect(rect(), palette().color(QPalette::Base).darker(isEnabled() ? 150 : 110));

    QLinearGradient gradient(area.bottomLeft(), area.topLeft());
    gradient.setColorAt(0.0, QColor(30, 140, 60));
    gradient.setColorAt(position(-18.0), QColor(60, 190, 60));
    gradient.setColorAt(position(-6.0), QColor(230, 200, 40));
    gradient.setColorAt(position(-1.0), QColor(230, 60, 40));
    gradient.setColorAt(1.0, QColor(230, 30, 30));

    int width = qMax(1, (area.width() - (m_channels - 1)) / m_channels);
    for (int c = 0; c < m_channels; ++c)
    {
        QRect bar(area.left() + c * (width + 1), area.top(), width, area.height());
        double level = c < m_levels.size() ? m_levels.at(c) : FLOOR;
        double hold = c < m_holds.size() ? m_holds.at(c) : FLOOR;

        int h = int(bar.height() * position(level));
        p.fillRect(QRect(bar.left(), bar.bottom() - h + 1, bar.width(), h), gradient);
        if (hold > FLOOR)
        {
            int y = bar.bottom() - int(bar.height() * position(hold));
            p.fillRect(QRect(bar.left(), y, bar.width(), 2), hold >= -1.0 ? QColor(255, 60, 40) : QColor(220, 220, 220));
        }
    }

    // ticks at 0, -6, -18, -40 dB
    p.setPen(palette().color(QPalette::Mid));
    for (double db : {0.0, -6.0, -18.0, -40.0})
    {
        int y = area.bottom() - int(area.height() * position(db));
        p.drawLine(area.left(), y, area.left() + 1, y);
        p.drawLine(area.right() - 1, y, area.right(), y);
    }
}
