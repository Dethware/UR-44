#pragma once

#include <QList>
#include <QWidget>

#include <cstdint>

class LevelMeter;

namespace UR44
{
class Device;
}

/*
 * One of the two DSP mixes (MIX 1: MAIN / LINE 1/2 / PHONES 1, MIX 2: LINE 3/4, PHONES 2 optionally), laid out
 * like dspMixFx: one column per input, the DAW return and the master (with REV-X), and the PHONES selection.
 * Controls of the same kind share a row.
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

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void addInputColumn(class QGridLayout* grid, int column, uint16_t ch);
    void addDawColumn(class QGridLayout* grid, int column);
    void addMasterColumn(class QGridLayout* grid, int column);
    void updateSolo();

    int m_bus;
    UR44::Device* m_device;
    QList<LevelMeter*> m_inputMeters;
    LevelMeter* m_dawMeter = nullptr;
    LevelMeter* m_masterMeter = nullptr;

    class QGridLayout* m_grid = nullptr;
    QList<int> m_stripColumns;

    bool m_soloActive = false;
};
