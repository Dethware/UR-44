#pragma once

#include <cstdint>

/*
 * The REV-X decay graph's model, as the dspMixFx REV-X editor draws it (macOS libRevHall_UR44.dylib: RevShape,
 * RevLine and the FILTER functions they use). Like the Channel Strip's curves, the editor computes this itself, at
 * 44.1 kHz; the device isn't asked.
 *
 * The graph shows how the reverb's level decays over time at 100 Hz, 1 kHz and 10 kHz (a waterfall, the low
 * frequencies in front), the decay's end per frequency (the reverb time, scaled by the low and high ratios), and the
 * HPF/LPF response.
 *
 * The values are the wire values of the FxRev* parameters.
 */
namespace UR44
{
class Device;
}

namespace UR44::ReverbShape
{

struct Settings
{
    int32_t type = 0;          // FxRevType: 0 Hall, 1 Room, 2 Plate
    int32_t time = 23;         // FxRevTime, 0..69
    int32_t initialDelay = 2;  // FxRevIniDly, 0..127
    int32_t decay = 27;        // FxRevDecay, 0..63
    int32_t roomSize = 29;     // FxRevRoomSize, 0..31
    int32_t diffusion = 10;    // FxRevDiff, 0..10
    int32_t hpf = 4;           // FxRevHpf, 0..52
    int32_t lpf = 50;          // FxRevLpf, 34..60
    int32_t highRatio = 8;     // FxRevHighRatio, 1..10 (0.1 .. 1.0)
    int32_t lowRatio = 12;     // FxRevLowRatio, 1..14 (0.1 .. 1.4)
    int32_t lowFreq = 32;      // FxRevLowFreq, 1..59
};

/// The REV-X's current settings (defaults for values the device hasn't reported)
Settings settings(const Device& device);

/// The frequencies of the graph's three decay lines, front to back
constexpr double LINE_FREQUENCIES[3] = {100.0, 1000.0, 10000.0};
/// The graph's level range: the lines show -60 .. 0 dB
constexpr double MIN_LEVEL_DB = -60.0;

/// The graph's time ranges (zoom levels), in ms from 0
constexpr double TIME_RANGES_MS[9] = {500, 1000, 2000, 3000, 4000, 5000, 10000, 25000, 50000};
/// The auto zoom: the first range whose 95 % holds `ms` (the end of the reverb), else the last
int autoZoom(double ms);

class Shape
{
public:
    explicit Shape(const Settings& settings);

    /// The level of decay line `line` (0..2, see LINE_FREQUENCIES) at `ms` after the input, in dB, with the
    /// filters; -96 dB is silence
    double levelDb(int line, double ms) const;
    /// The filters' (HPF and LPF) response at `hz`, in dB (from -96)
    double filterDb(double hz) const;
    /// When the reverb ends at `hz`: the reverb time, scaled by the low and high ratios, after the initial delay
    double endMs(double hz) const;

    double initialDelayMs() const { return m_initialDelay; }
    /// Where the decay turns from the early part to the tail (the editor's decay handle), after the initial delay
    double decayMs() const { return m_knee + m_initialDelay; }
    /// The reverb time's end at 1 kHz (the editor's reverb time handle, and what the auto zoom follows)
    double reverbTimeMs() const { return m_reverbTime + m_initialDelay; }
    int diffusion() const { return m_diffusion; }

private:
    struct Shelf
    {
        // y = b0 x + b1 x[-1] + c1 y[-1]
        double b0 = 1.0, b1 = 0.0, c1 = 0.0;
        double powerDb(double hz) const;
    };
    /// The decay ratio's shelves at `hz`: the reverb time there over the reverb time
    double ratio(double hz) const;
    /// RevLine::GetEnvelope: the decay envelope (in dB above -96) `ms` after the initial delay
    double envelope(int line, double ms) const;

    const struct Curve* m_curve;
    double m_reverbTime = 0.0;   // ms
    double m_initialDelay = 0.0; // ms
    double m_knee = 0.0;         // ms after the initial delay
    double m_attackScale = 1.0;  // envelope ms per graph ms, before the knee
    double m_slope[3] = {};      // and after it, per line
    double m_offset[3] = {};
    int m_diffusion = 10;
    Shelf m_hpf, m_lpf, m_highRatio, m_lowRatio;
};

}
