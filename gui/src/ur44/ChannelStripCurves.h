#pragma once

#include <cstdint>

/*
 * The Channel Strip's compressor transfer curve and EQ response, as the dspMixFx Channel Strip editor draws them
 * (macOS libChannelStrip_UR44.dylib: Channel_Strip::setParameter, GetCompCurve and filterPower, and the FILTER2PEQ
 * coefficient functions). The editor computes them from its own model of the strip, at 44.1 kHz; the device isn't
 * asked.
 *
 * The values are the wire values of the strip's Fx* parameters.
 */
namespace UR44
{
class Device;
}

namespace UR44::ChannelStripCurves
{

struct CompressorSettings
{
    int32_t drive = 100;           // FxCompDrive, 0..200
    int32_t ratio = 0;             // FxCompRatio, 0..120 (1.00 .. INF)
    int32_t knee = 0;              // FxCompKnee: 0 Soft, 1 Middle, 2 Hard
    int32_t thresholdOffset = 100; // FxCompThreshOfst, 0..200 (-20 .. +20 dB)
};

struct EqSettings
{
    int32_t lowFreq = 4;    // FxEqLsvFreq
    int32_t lowGain = 180;  // FxEqLsvGain, 0..360 (-18 .. +18 dB)
    int32_t midFreq = 4;    // FxEqMidFreq
    int32_t midGain = 180;  // FxEqMidGain
    int32_t midQ = 0;       // FxEqMidQual
    int32_t highFreq = 60;  // FxEqHsvFreq
    int32_t highGain = 180; // FxEqHsvGain
};

/// The strip's current settings (defaults for values the device hasn't reported)
CompressorSettings compressorSettings(const Device& device, uint16_t strip);
EqSettings eqSettings(const Device& device, uint16_t strip);

/*
 * The compressor's static curve: output level for an input level, both in dB (the editor's graph shows
 * -60 .. 0 dB on both axes). Without the makeup gain, like the editor.
 */
class Compressor
{
public:
    explicit Compressor(const CompressorSettings& settings);

    /// The threshold in dB (-54 .. 0); 0 is "no compression"
    double thresholdDb() const { return m_threshold; }
    double outputDb(double inputDb) const;

private:
    double m_threshold = 0.0;
    double m_slope = 0.0; // 1 - 1/ratio
    int m_knee = 0;
};

/*
 * The 3-band EQ's magnitude response in dB (the editor's graph shows 20 Hz .. 20 kHz, -20 .. +20 dB). Without the
 * total gain, like the editor.
 */
class Eq
{
public:
    enum Band
    {
        All,
        Low,
        Mid,
        High,
    };

    explicit Eq(const EqSettings& settings, double sampleRate = 44100.0);

    double responseDb(double frequency, Band band = All) const;

    /// The band's centre (Mid) or corner (Low, High) frequency in Hz, and its gain in dB
    double frequency(Band band) const;
    double gainDb(Band band) const;

private:
    struct Section
    {
        // y = b0 x + b1 x[-1] + b2 x[-2] + c1 y[-1] + c2 y[-2]
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, c1 = 0.0, c2 = 0.0;
        double powerDb(double frequency, double sampleRate) const;
    };

    double m_sampleRate;
    double m_frequency[3];
    double m_gain[3];
    Section m_sections[3];
};

}
