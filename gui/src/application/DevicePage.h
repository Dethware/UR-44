#pragma once

#include <QWidget>

namespace UR44
{
class Device;
}

/*
 * Device-wide settings: loopback, LINE 5/6 level, plus read-only status (+48V switches, sample rate, firmware,
 * port). Routing that dspMixFx shows per mix (PHONES 2, REV-X send) is on the mix pages.
 */
class DevicePage : public QWidget
{
    Q_OBJECT
public:
    explicit DevicePage(UR44::Device* device, QWidget* parent = nullptr);
};
