#include "ReverbTime.h"

#include <QObject>

#include <cmath>
#include <iterator>

namespace UR44::ReverbTime
{

namespace
{
// ReverbTimeTable in MemAPI (identical in Windows memapi_ur44.dll at 0x10019120)
constexpr double BASE_TIMES[] = {
    0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0, 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8, 1.9, 2.0,
    2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7, 2.8, 2.9, 3.0, 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8,
    3.9, 4.0, 4.1, 4.2, 4.3, 4.4, 4.5, 4.6, 4.7, 4.8, 4.9, 5.0, 5.5, 6.0, 6.5, 7.0, 7.5, 8.0,
    8.5, 9.0, 9.5, 10.0, 11.0, 12.0, 13.0, 14.0, 15.0, 16.0, 17.0, 18.0, 19.0, 20.0, 25.0, 30.0,
};
}

double seconds(int type, int roomSize, int time)
{
    if (time < 0 || time >= int(std::size(BASE_TIMES)))
    {
        return 0.0;
    }
    // The room size the base times are given for: Hall 30, Room 19, Plate 15
    int reference = type == 1 ? 19 : type == 2 ? 15 : 30;
    int diff = reference - roomSize;
    // computed in float like the original (cvtsd2ss), then rounded to milliseconds
    float factor = float(diff > 0 ? std::pow(0.965, diff) : std::pow(1.035, -diff));
    return std::trunc(double(factor) * BASE_TIMES[time] * 1000.0 + 0.5) * 0.001;
}

QString rangeText(int type, int roomSize)
{
    constexpr int LAST_TIME = int(std::size(BASE_TIMES)) - 1;
    constexpr int LAST_ROOM_SIZE = 31;
    static const char* const TYPES[] = {QT_TRANSLATE_NOOP("ReverbTime", "Hall"), QT_TRANSLATE_NOOP("ReverbTime", "Room"),
                                        QT_TRANSLATE_NOOP("ReverbTime", "Plate")};
    const QString name = type >= 0 && type < 3 ? QObject::tr(TYPES[type]) : QString::number(type);
    return QObject::tr("%1 \u2013 %2 s at this room size (%3: %4 \u2013 %5 s over all room sizes)")
        .arg(text(type, roomSize, 0), text(type, roomSize, LAST_TIME), name, text(type, 0, 0),
             text(type, LAST_ROOM_SIZE, LAST_TIME));
}

QString text(int type, int roomSize, int time)
{
    float s = float(seconds(type, roomSize, time));
    int decimals = s < 1.0f ? 3 : s < 10.0f ? 2 : 1;
    return QString::number(s, 'f', decimals);
}

}
