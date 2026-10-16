#pragma once

#include <QString>

/*
 * REV-X reverb time, as dspMixFx displays it (macOS MemAPI prmTable::getActualReverbTime / float2RevTimeStr).
 *
 * FxRevTime (0..69) indexes a table of base times (0.3 .. 30 s); the actual time also depends on the reverb type
 * (FxRevType: 0 Hall, 1 Room, 2 Plate) and FxRevRoomSize (0..31).
 */
namespace UR44::ReverbTime
{
/// Seconds, rounded to milliseconds like the original; 0 for an invalid time index
double seconds(int type, int roomSize, int time);
/// "0.780", "2.47", "31.0"
QString text(int type, int roomSize, int time);
/// The range of reverb times: at `roomSize`, and over all room sizes (what the manual lists), e.g.
/// "0.300 – 30.0 s at this room size (Plate: 0.176 – 52.0 s over all room sizes)" for Plate at room size 15
QString rangeText(int type, int roomSize);
}
