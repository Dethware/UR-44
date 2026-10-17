#pragma once

#include <QDialog>

#include <cstdint>

class LevelMeter;
class QLabel;

namespace UR44
{
class Device;
}

/*
 * Editor windows for the DSP effects, laid out after the dspMixFx windows (operation manual pages 18-23).
 * Each is non-modal and exists once per target: opening it again raises the existing window.
 */
namespace EffectEditors
{
/// The Channel Strip inserted on `channel` (0-5); does nothing if none is
void openChannelStrip(UR44::Device* device, uint16_t channel, QWidget* parent);
/// Guitar Amp Classics (there is one)
void openGuitarAmp(UR44::Device* device, QWidget* parent);
/// REV-X
void openReverb(UR44::Device* device, QWidget* parent);
}

/*
 * Sweet Spot Morphing Channel Strip: morph, total gain, compressor, 3-band EQ. Bound to the strip (0-3) that was
 * inserted on the channel when it opened; closes when the channel's strip changes.
 */
class ChannelStripEditor : public QDialog
{
    Q_OBJECT
public:
    ChannelStripEditor(UR44::Device* device, uint16_t channel, uint16_t strip, QWidget* parent = nullptr);

    uint16_t channel() const { return m_channel; }

private:
    void showMeters(const QList<uint32_t>& frame);

    UR44::Device* m_device;
    uint16_t m_channel;
    uint16_t m_strip;
    LevelMeter* m_output = nullptr;
    LevelMeter* m_gainReduction = nullptr;
    QLabel* m_gainReductionLabel = nullptr;
};

/*
 * Guitar Amp Classics: the amp model (Clean, Crunch, Lead, Drive) and its controls.
 */
class GuitarAmpEditor : public QDialog
{
    Q_OBJECT
public:
    explicit GuitarAmpEditor(UR44::Device* device, QWidget* parent = nullptr);

private:
    void showMeters(const QList<uint32_t>& frame);

    UR44::Device* m_device;
    LevelMeter* m_input = nullptr;
    LevelMeter* m_output = nullptr;
};

/*
 * REV-X: type and reverb parameters.
 */
class ReverbEditor : public QDialog
{
    Q_OBJECT
public:
    explicit ReverbEditor(UR44::Device* device, QWidget* parent = nullptr);
};
