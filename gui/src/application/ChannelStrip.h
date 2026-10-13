#pragma once

#include <QString>
#include <QWidget>

#include <cstdint>

class LevelMeter;

namespace UR44
{
class Device;
}

/*
 * One mixer channel: meter, fader, pan, on and solo, each bound to a device parameter.
 * Controls whose parameter name is empty aren't shown (e.g. the REV-X return has no pan).
 */
class ChannelStrip : public QWidget
{
    Q_OBJECT
public:
    struct Spec
    {
        QString title;
        uint16_t index = 0;
        QString on;
        QString level;
        QString pan;
        QString solo;
        /// 0 = no meter, 1 = mono, 2 = stereo
        int meterChannels = 1;
    };

    ChannelStrip(const Spec& spec, UR44::Device* device, QWidget* parent = nullptr);

    /// nullptr if the strip has no meter
    LevelMeter* meter() const { return m_meter; }

private:
    LevelMeter* m_meter = nullptr;
};
