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
 * Paired inputs (1/2, 3/4, 5/6: the chain between their captions) show their shared controls once, across both columns: HPF, the effect, the
 * REV-X send, mute/solo, and the fader while the fader link is on (the lock between the names). Phase, the meters and
 * the pans stay per channel.
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
    /// The pair's LINK button between the connector captions, its fader link between the names, and +48V
    void addPairHeader(class QGridLayout* grid, uint16_t leftChannel);
    /// Lays out a pair's columns as one (paired) or two
    void updatePair(uint16_t leftChannel);
    void addDawColumn(class QGridLayout* grid, int column);
    void addMasterColumn(class QGridLayout* grid, int column);
    void updateSolo();
    /// Ctrl+Shift+click on `mute` / `solo` applies to all the channels of the mix
    void connectAllChannels(class QToolButton* mute, class QToolButton* solo);

    int m_bus;
    UR44::Device* m_device;
    QList<LevelMeter*> m_inputMeters;
    LevelMeter* m_dawMeter = nullptr;
    LevelMeter* m_masterMeter = nullptr;

    class QGridLayout* m_grid = nullptr;
    /// Per input: the controls a pair shares (the left channel's span both columns, the right channel's hide)
    struct Shared
    {
        QWidget* widget;
        int row;
        Qt::Alignment alignment;
        bool fader; // shared only while the fader link is on
    };
    QList<Shared> m_shared[6];
    /// Per input: per-channel parts of a shared row that hide on the right channel (HPF)
    QList<QWidget*> m_hiddenWhenRight[6];
    /// Per pair (by its left channel): the fader link button
    QWidget* m_faderLinks[6] = {};
    QList<int> m_stripColumns;

    bool m_soloActive = false;
};
