#include "ReverbShape.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

#include "ChannelStripCurves.h"
#include "Device.h"
#include "Parameters.h"
#include "ReverbTime.h"

namespace UR44::ReverbShape
{

namespace
{
constexpr double PI = 3.14159265358979323846;
constexpr double SAMPLE_RATE = 44100.0;

// The decay envelopes (libRevHall_UR44.dylib _HallAttackCurve .. _RoomReleaseCurve): a linear attack over
// `attackSamples`, then exponential segments of 4000 samples, each from `amplitude` at its own rate (per sample)
constexpr double SEGMENT_SAMPLES = 4000.0;

struct Segment
{
    double amplitude;
    double rate;
};

constexpr Segment HALL_RELEASE[] = {
    {21268, -8.585911e-05}, {15086, -5.561609e-05}, {12077, -3.675003e-05}, {10426, -2.859895e-05},
    {9299, -9.654691e-05},  {6320, -0.0001023373}, {4197, -7.413425e-05},  {3120, -0.000141996},
    {1768, -0.0001444703},  {992, -3.194689e-05},  {873, -8.717215e-05},   {616, -3.38471e-05},
    {538, -6.976134e-05},   {407, -6.090552e-05},  {319, -2.296696e-05},   {291, -5.450054e-05},
    {234, -1.770651e-05},   {218, -8.159232e-06},  {211, -2.620852e-05},
};
constexpr Segment ROOM_RELEASE[] = {
    {24015, -0.000244}, {9051, -0.000323}, {2489, -0.00032}, {691, -0.000324}, {189, -0.000318},
};
constexpr Segment PLATE_RELEASE[] = {
    {19763, -9.93e-05}, {13284, -9.64e-05}, {9033, -7.37e-05}, {6726, -6.61e-05}, {5162, -8.21e-05},
    {3716, -7.45e-05},  {2757, -5.86e-05},  {2180, -5.95e-05}, {1717, -4.51e-05}, {1433, -6.58e-05},
    {1101, -5.76e-05},  {874, -4.96e-05},   {716, -5.45e-05},  {575, -4.87e-05},  {473, -6.03e-05},
    {371, -3.59e-05},   {321, -5.68e-05},   {255, -3.09e-05},  {225, -4.64e-05},  {186, -1.49e-05},
    {175, -1.62e-05},   {163, -3.74e-05},   {140, -2.2e-05},
};
// The envelopes are in dB above -96 (16 bit amplitudes)
constexpr double FLOOR_DB = -96.0;
// The decay setting scales the early part exponentially, by this per step from the type's own (RevLine)
constexpr double DECAY_STEP = 0.04400934651494026;
// The early part takes at most this share of the reverb time
constexpr double MAX_EARLY = 0.4;

}

struct Curve
{
    long attackSamples;
    double attackSlope;
    const Segment* release;
    int segments;
    // The settings the envelope is drawn for (RevShape's constructor: SetBaseRevTime, SetBaseDecayInit)
    int baseRoomSize;
    int baseTime;
    int baseDecay;

    // RevLine::SetEnvelope, SetAtkMaxAdress, SetReleaseRate
    std::vector<float> table; // dB above -96, per ms
    double attackMs = 0.0;
    double releasePerMs = 0.0; // ln units
    double baseReverbTimeMs = 0.0;

    void build(int type)
    {
        long length = long((attackSamples + segments * SEGMENT_SAMPLES) / SAMPLE_RATE * 1000.0);
        table.assign(std::max(length, 3L), 0.0f);
        for (long ms = 1; ms < length; ++ms)
        {
            long sample = long(ms * SAMPLE_RATE * 0.001);
            double amplitude;
            if (sample < attackSamples)
            {
                amplitude = sample * attackSlope;
            }
            else
            {
                long offset = sample - attackSamples;
                long segment = std::min(long(offset / SEGMENT_SAMPLES), long(segments - 1));
                amplitude = release[segment].amplitude *
                            std::exp(double(long(offset - segment * SEGMENT_SAMPLES)) * release[segment].rate);
            }
            table[ms] = float(20.0 * std::log10(amplitude));
        }
        table[0] = table[1] - (table[2] - table[1]);
        attackMs = attackSamples / SAMPLE_RATE * 1000.0;
        releasePerMs = release[segments - 1].rate * SAMPLE_RATE * 0.001;
        baseReverbTimeMs = ReverbTime::seconds(type, baseRoomSize, baseTime) * 1000.0;
    }
};

namespace
{
const Curve& curve(int type)
{
    static Curve curves[3] = {
        {8199, 2.593975, HALL_RELEASE, int(std::size(HALL_RELEASE)), 30, 26, 51, {}},
        {2808, 8.55235, ROOM_RELEASE, int(std::size(ROOM_RELEASE)), 19, 2, 26, {}},
        {1219, 16.212469, PLATE_RELEASE, int(std::size(PLATE_RELEASE)), 15, 25, 0, {}},
    };
    static const bool built = [] {
        for (int t = 0; t < 3; ++t)
        {
            curves[t].build(t);
        }
        return true;
    }();
    Q_UNUSED(built);
    return curves[std::clamp(type, 0, 2)];
}

double number(const char* parameter, int32_t value)
{
    return ChannelStripCurves::valueNumber(parameter, value);
}

int32_t valueOr(const Device& device, const char* name, int32_t fallback)
{
    return Parameters::instance()[name].clamp(device.value(name).value_or(fallback));
}
}

Settings settings(const Device& device)
{
    Settings s;
    s.type = valueOr(device, "FxRevType", s.type);
    s.time = valueOr(device, "FxRevTime", s.time);
    s.initialDelay = valueOr(device, "FxRevIniDly", s.initialDelay);
    s.decay = valueOr(device, "FxRevDecay", s.decay);
    s.roomSize = valueOr(device, "FxRevRoomSize", s.roomSize);
    s.diffusion = valueOr(device, "FxRevDiff", s.diffusion);
    s.hpf = valueOr(device, "FxRevHpf", s.hpf);
    s.lpf = valueOr(device, "FxRevLpf", s.lpf);
    s.highRatio = valueOr(device, "FxRevHighRatio", s.highRatio);
    s.lowRatio = valueOr(device, "FxRevLowRatio", s.lowRatio);
    s.lowFreq = valueOr(device, "FxRevLowFreq", s.lowFreq);
    return s;
}

int autoZoom(double ms)
{
    // YSReverbGraph::DoAutozoom
    for (int i = 0; i < int(std::size(TIME_RANGES_MS)); ++i)
    {
        if (TIME_RANGES_MS[i] * 0.95 >= ms)
        {
            return i;
        }
    }
    return int(std::size(TIME_RANGES_MS)) - 1;
}

double Shape::Shelf::powerDb(double hz) const
{
    // FILTER::power: |H(e^jw)|² in dB
    const std::complex<double> z1 = std::polar(1.0, -2.0 * PI * hz / SAMPLE_RATE);
    std::complex<double> h = (b0 + b1 * z1) / (1.0 - c1 * z1);
    double power = std::norm(h);
    return power > 0.0 ? 10.0 * std::log10(power) : -100.0;
}

Shape::Shape(const Settings& s) : m_curve(&curve(s.type)), m_diffusion(s.diffusion)
{
    const Curve& c = *m_curve;
    m_reverbTime = ReverbTime::seconds(s.type, s.roomSize, s.time) * 1000.0;
    m_initialDelay = number("FxRevIniDly", s.initialDelay);

    // HPF and LPF: one pole (FILTER::setDcfHPF, setDcfLPF)
    {
        double e = 1.0 - std::exp(-2.0 * PI * number("FxRevHpf", s.hpf) / SAMPLE_RATE);
        double k = 1.0 - 0.5 * e;
        m_hpf = {k, -k, k};
        double a = 1.0 - std::exp(-2.0 * PI * number("FxRevLpf", s.lpf) / SAMPLE_RATE);
        m_lpf = {a, 0.0, 1.0 - a};
    }
    // The decay ratios as shelves: high above 4 kHz (FILTER::setCoefHIGS_B), low below the low frequency
    // (setCoefLOWS_A), with the ratio as their gain
    auto ratioDb = [](int32_t value) { return value > 0 ? 20.0 * std::log10(value * 0.1) : -100.0; };
    {
        double g = ratioDb(s.highRatio);
        double a = std::pow(10.0, std::abs(g) / 20.0);
        double t = std::tan(PI * 4000.0 / SAMPLE_RATE);
        double at = a * t;
        if (g >= 0.0)
        {
            double d = at + 1.0;
            m_highRatio = {a * (t + 1.0) / d, -a * (1.0 - t) / d, (1.0 - at) / d};
        }
        else
        {
            double d = t + 1.0;
            m_highRatio = {(at + 1.0) / (a * d), -(1.0 - at) / (a * d), (1.0 - t) / d};
        }
    }
    {
        double g = ratioDb(s.lowRatio);
        double a = std::pow(10.0, std::abs(g) / 20.0);
        double t = std::tan(PI * number("FxRevLowFreq", s.lowFreq) / SAMPLE_RATE);
        double at = a * t;
        if (g >= 0.0)
        {
            double d = t + 1.0;
            m_lowRatio = {(1.0 + at) / d, -(1.0 - at) / d, (1.0 - t) / d};
        }
        else
        {
            double d = at + 1.0;
            m_lowRatio = {(t + 1.0) / d, -(1.0 - t) / d, (1.0 - at) / d};
        }
    }

    // RevLine::CalcRatio: the early part ends at `m_knee` (the decay setting moves it), the tail stretches to the
    // reverb time, and by the ratio at each line's frequency
    double early = c.attackMs / c.baseReverbTimeMs;
    double share = std::min(double(float(std::exp(float(s.decay - c.baseDecay) * float(DECAY_STEP)))) * early,
                            MAX_EARLY);
    double scale = (1.0 - early) * (c.baseReverbTimeMs / m_reverbTime) / (1.0 - share);
    m_knee = share * m_reverbTime;
    m_attackScale = c.attackMs / m_knee;
    for (int line = 0; line < 3; ++line)
    {
        double r = ratio(LINE_FREQUENCIES[line]);
        m_slope[line] = r > 0.0 ? scale / r : 0.0;
        m_offset[line] = c.attackMs - m_slope[line] * m_knee;
    }
}

double Shape::ratio(double hz) const
{
    // RevShape::SetRevTimeRatio
    double db = m_highRatio.powerDb(hz) + m_lowRatio.powerDb(hz);
    return db == -100.0 ? 0.0 : std::pow(10.0, db / 20.0);
}

double Shape::envelope(int line, double ms) const
{
    // RevLine::GetEnvelope: graph time to the envelope's own time, then its table (the original's interpolation
    // comes down to the entry itself) or, past it, the last segment's rate
    const Curve& c = *m_curve;
    double x = ms <= m_knee ? ms * m_attackScale : ms * m_slope[line] + m_offset[line];
    const long length = long(c.table.size());
    if (x <= double(length - 1))
    {
        return c.table[std::max(long(x), 0L)];
    }
    return c.table[length - 1] + (x - length) * c.releasePerMs * (20.0 / std::log(10.0));
}

double Shape::filterDb(double hz) const
{
    // RevShape::GetFilterCurve
    return std::max(m_hpf.powerDb(hz) + m_lpf.powerDb(hz), FLOOR_DB);
}

double Shape::levelDb(int line, double ms) const
{
    // RevShape::GetEnvelopeLine
    line = std::clamp(line, 0, 2);
    double level = 0.0;
    if (ms >= m_initialDelay)
    {
        level = std::max(envelope(line, ms - m_initialDelay), 0.0);
    }
    return level + FLOOR_DB + m_hpf.powerDb(LINE_FREQUENCIES[line]) + m_lpf.powerDb(LINE_FREQUENCIES[line]);
}

double Shape::endMs(double hz) const
{
    // RevShape::GetReverbTimeCurve
    return ratio(hz) * m_reverbTime + m_initialDelay;
}

}
