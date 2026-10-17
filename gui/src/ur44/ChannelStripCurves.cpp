#include "ChannelStripCurves.h"

#include <QString>

#include <algorithm>
#include <cmath>
#include <complex>

#include "Device.h"
#include "Parameters.h"

namespace UR44::ChannelStripCurves
{

namespace
{
constexpr double PI = 3.14159265358979323846;

int32_t valueOr(const Device& device, const char* name, uint16_t strip, int32_t fallback)
{
    return Parameters::instance()[name].clamp(device.value(name, strip).value_or(fallback));
}

/// The number a parameter's label shows: "1.06k" is 1060; "INF" is the plug-in's 3000 (the ratio table's end)
double labelNumber(const char* name, int32_t value)
{
    const Parameter& p = Parameters::instance()[name];
    if (!p.labels)
    {
        return value;
    }
    QString label = QString::fromLatin1(p.labels[p.clamp(value) - p.min]);
    if (label == QLatin1String("INF"))
    {
        return 3000.0;
    }
    double scale = 1.0;
    if (label.endsWith('k'))
    {
        label.chop(1);
        scale = 1000.0;
    }
    return label.toDouble() * scale;
}

/// The gains of the EQ bands: 0..360 is -18 .. +18 dB (Table2Gain)
double bandGainDb(int32_t value)
{
    return value * 0.1 - 18.0;
}

double db2amp(double db)
{
    return std::pow(10.0, db / 20.0);
}

// Shelf and peaking filter shape constants (Channel_Strip's constructor: Table2Option(0x20), Table2Option(0x46), 18)
constexpr double SHELF_SLOPE = 0.32;
constexpr double PEAK_Q_SCALING = 0.70;
constexpr double SHAPE_REFERENCE_DB = 18.0;
}

CompressorSettings compressorSettings(const Device& device, uint16_t strip)
{
    CompressorSettings s;
    s.drive = valueOr(device, "FxCompDrive", strip, s.drive);
    s.ratio = valueOr(device, "FxCompRatio", strip, s.ratio);
    s.knee = valueOr(device, "FxCompKnee", strip, s.knee);
    s.thresholdOffset = valueOr(device, "FxCompThreshOfst", strip, s.thresholdOffset);
    return s;
}

EqSettings eqSettings(const Device& device, uint16_t strip)
{
    EqSettings s;
    s.lowFreq = valueOr(device, "FxEqLsvFreq", strip, s.lowFreq);
    s.lowGain = valueOr(device, "FxEqLsvGain", strip, s.lowGain);
    s.midFreq = valueOr(device, "FxEqMidFreq", strip, s.midFreq);
    s.midGain = valueOr(device, "FxEqMidGain", strip, s.midGain);
    s.midQ = valueOr(device, "FxEqMidQual", strip, s.midQ);
    s.highFreq = valueOr(device, "FxEqHsvFreq", strip, s.highFreq);
    s.highGain = valueOr(device, "FxEqHsvGain", strip, s.highGain);
    return s;
}

// --- Compressor -------------------------------------------------------------------------------------------------

Compressor::Compressor(const CompressorSettings& settings)
{
    // Channel_Strip::setParameter 0 and 5 (Table2Threshold, Table2ThresholdOffset), CalcThreData: the drive sets the
    // threshold (0 .. -40 dB), the offset moves it; a lowering offset fades out as the threshold nears 0 dB
    double threshold = -settings.drive * 0.2;
    double offset = settings.thresholdOffset * 0.2 - 20.0;
    double t = threshold + offset;
    if (offset < 0.0 && threshold >= -6.2)
    {
        t = threshold * t / -6.2;
    }
    m_threshold = std::clamp(t, -54.0, 0.0);
    m_slope = 1.0 - 1.0 / labelNumber("FxCompRatio", settings.ratio);
    m_knee = settings.knee;
}

double Compressor::outputDb(double inputDb) const
{
    // Channel_Strip::GetCompCurve; the level above the threshold in log10 units (20 dB), from -30 dB below it
    double over = std::max(inputDb - m_threshold, -30.0) / 20.0;
    double gain = 0.0; // log10 of the gain
    // The knee's half width (in 20 dB units) and quadratic coefficient: Soft ±26 dB, Middle ±10 dB, else hard
    double halfWidth = m_knee == 0 ? 1.3 : 0.5;
    double coefficient = m_knee == 0 ? 0.19231 : 0.5;
    if (m_threshold != 0.0 && (m_knee == 0 || m_knee == 1))
    {
        if (over > halfWidth)
        {
            gain = -m_slope * over;
        }
        else if (over > -halfWidth)
        {
            gain = -coefficient * (over + halfWidth) * (over + halfWidth) * m_slope;
        }
    }
    else if (over > 0.0)
    {
        gain = -m_slope * over;
    }
    return inputDb + 20.0 * gain;
}

// --- EQ ---------------------------------------------------------------------------------------------------------

Eq::Eq(const EqSettings& settings, double sampleRate) : m_sampleRate(sampleRate)
{
    const double fs = sampleRate;
    // Above Nyquist (only possible below 44.1 kHz) a band is flat
    auto beyondNyquist = [fs](double f) { return fs < 44100.0 && f > 0.5 * fs - 10.0; };

    // Low: first order shelf (FILTER2PEQ::setCoefLowShelf_C)
    {
        double f = labelNumber("FxEqLsvFreq", settings.lowFreq);
        double g = bandGainDb(settings.lowGain);
        m_frequency[0] = f;
        m_gain[0] = g;
        Section& s = m_sections[0];
        if (!beyondNyquist(f))
        {
            double a = db2amp(std::abs(g));
            double w = std::pow(db2amp(SHAPE_REFERENCE_DB), SHELF_SLOPE) * std::tan(PI * f / fs) *
                       std::pow(a, 1.0 - SHELF_SLOPE);
            if (g >= 0.0)
            {
                double d = a + w;
                s = {a * (1.0 + w) / d, -a * (1.0 - w) / d, 0.0, (a - w) / d, 0.0};
            }
            else
            {
                double d = 1.0 + w;
                s = {(1.0 + w / a) / d, -(1.0 - w / a) / d, 0.0, (1.0 - w) / d, 0.0};
            }
        }
    }

    // Mid: peaking (FILTER2PEQ::setCoefPEQ_C); the Q narrows towards high frequencies and with the gain
    {
        double f = labelNumber("FxEqMidFreq", settings.midFreq);
        double g = bandGainDb(settings.midGain);
        double q = labelNumber("FxEqMidQual", settings.midQ);
        m_frequency[1] = f;
        m_gain[1] = g;
        Section& s = m_sections[1];
        if (!beyondNyquist(f))
        {
            struct Correction
            {
                double limit, logSlope, offset, slope;
            };
            const Correction c = fs < 48000.0   ? Correction{2000.0, -0.0301, 1.25513, -4.817e-05}
                                 : fs < 88200.0 ? Correction{2000.0, -0.02143, 1.25686, -4.103e-05}
                                 : fs < 96000.0 ? Correction{4000.0, -0.0317, 1.29486, -2.462e-05}
                                                : Correction{4000.0, -0.02382, 1.21531, -2.004e-05};
            if (f > c.limit)
            {
                double factor = std::log2(q) * c.logSlope + c.offset + c.slope * f;
                if (factor <= 1.0)
                {
                    q = std::max(q * factor, 0.1);
                }
            }
            double a = db2amp(std::abs(g));
            double k = (1.0 / q) / std::pow(db2amp(std::abs(g) - SHAPE_REFERENCE_DB), PEAK_Q_SCALING);
            double t = std::tan(PI * f / fs);
            double t2 = t * t;
            double numerator = g >= 0.0 ? a * t * k : t * k;
            double denominator = g >= 0.0 ? t * k : a * t * k;
            double a0 = 1.0 + t2 + denominator;
            s = {(1.0 + t2 + numerator) / a0, 2.0 * (t2 - 1.0) / a0, (1.0 + t2 - numerator) / a0,
                 -2.0 * (t2 - 1.0) / a0, (denominator - (1.0 + t2)) / a0};
        }
    }

    // High: first order shelf (FILTER2PEQ::setCoefHiShelf_C)
    {
        double f = labelNumber("FxEqHsvFreq", settings.highFreq);
        double g = bandGainDb(settings.highGain);
        m_frequency[2] = f;
        m_gain[2] = g;
        Section& s = m_sections[2];
        if (!beyondNyquist(f))
        {
            double a = db2amp(g); // below 1 for a cut
            double t = std::tan(PI * f / fs) * std::pow(db2amp(SHAPE_REFERENCE_DB), -SHELF_SLOPE);
            if (g >= 0.0)
            {
                double w = t * std::pow(a, -(1.0 - SHELF_SLOPE));
                double d = a * w + 1.0;
                s = {(w + 1.0) * a / d, -a * (1.0 - w) / d, 0.0, (1.0 - a * w) / d, 0.0};
            }
            else
            {
                double w = t * std::pow(a, 1.0 - SHELF_SLOPE);
                double d = w + 1.0;
                s = {(a + w) / d, (w - a) / d, 0.0, (1.0 - w) / d, 0.0};
            }
        }
    }
}

double Eq::Section::powerDb(double frequency, double sampleRate) const
{
    // FILTER2PEQ::power: |H(e^jw)|² in dB
    const std::complex<double> z1 = std::polar(1.0, -2.0 * PI * frequency / sampleRate);
    const std::complex<double> z2 = z1 * z1;
    std::complex<double> h = (b0 + b1 * z1 + b2 * z2) / (1.0 - c1 * z1 - c2 * z2);
    double power = std::norm(h);
    return power > 0.0 ? 10.0 * std::log10(power) : -100.0;
}

double Eq::responseDb(double frequency, Band band) const
{
    // Channel_Strip::filterPower: up to 0.48 fs
    double f = std::min(frequency, 0.48 * m_sampleRate);
    if (band != All)
    {
        return m_sections[band - 1].powerDb(f, m_sampleRate);
    }
    double sum = 0.0;
    for (const Section& s : m_sections)
    {
        sum += s.powerDb(f, m_sampleRate);
    }
    return sum;
}

double Eq::frequency(Band band) const
{
    return band == All ? 0.0 : m_frequency[band - 1];
}

double Eq::gainDb(Band band) const
{
    return band == All ? 0.0 : m_gain[band - 1];
}

}
