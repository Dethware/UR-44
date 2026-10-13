#pragma once

#include <QList>
#include <QWidget>

#include <cstdint>

class ChannelStrip;
class QLabel;
class QPushButton;

namespace UR44
{
class Device;
}

/*
 * One of the two DSP mixes (MIX 1: MAIN / LINE 1/2 / PHONES 1, MIX 2: LINE 3/4, PHONES 2 optionally):
 * the six inputs, the DAW return, the REV-X return and the master.
 *
 * The device meters the DAW and master of one mix at a time (PatchDawMeter / PatchMasterMeter); this page shows
 * them only while they point at its mix. MainWindow points them at the visible mix.
 */
class MixPage : public QWidget
{
    Q_OBJECT
public:
    /// `bus` is 1 or 2
    MixPage(int bus, UR44::Device* device, QWidget* parent = nullptr);

    int bus() const { return m_bus; }

    void showMeters(const QList<uint32_t>& frame);
    void clearMeters();

    /// Whether any channel of this mix is soloed (the others are then muted)
    bool isSoloActive() const { return m_soloActive; }

signals:
    void soloActiveChanged(bool active);

private:
    void updateSolo();
    void clearSolo();

    int m_bus;
    UR44::Device* m_device;
    QList<ChannelStrip*> m_inputs;
    ChannelStrip* m_daw = nullptr;
    ChannelStrip* m_master = nullptr;

    QLabel* m_soloIndicator = nullptr;
    QPushButton* m_clearSolo = nullptr;
    bool m_soloActive = false;
};
