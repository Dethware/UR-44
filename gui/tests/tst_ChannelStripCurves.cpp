#include <QtTest>

#include "ChannelStripCurves.h"
#include "Parameters.h"

using namespace UR44;
using namespace UR44::ChannelStripCurves;

namespace
{
/// The wire value whose label is `label`
int32_t valueOf(const char* name, const char* label)
{
    const Parameter& p = Parameters::instance()[name];
    for (int32_t v = p.min; v <= p.max; ++v)
    {
        if (qstrcmp(p.labels[v - p.min], label) == 0)
        {
            return v;
        }
    }
    qFatal("no label %s for %s", label, name);
}

int32_t gain(double db)
{
    return int32_t(std::lround(db * 10.0 + 180.0));
}

bool near(double a, double b, double tolerance = 1e-6)
{
    return std::abs(a - b) <= tolerance;
}
}

class ChannelStripCurvesTest : public QObject
{
    Q_OBJECT

private slots:
    void compressor_threshold()
    {
        // the drive sets the threshold, 0 .. -40 dB; the offset (100 = 0 dB) moves it
        QCOMPARE(Compressor({0, 0, 2, 100}).thresholdDb(), 0.0);
        QCOMPARE(Compressor({100, 0, 2, 100}).thresholdDb(), -20.0);
        QCOMPARE(Compressor({200, 0, 2, 100}).thresholdDb(), -40.0);
        QVERIFY(near(Compressor({100, 0, 2, 150}).thresholdDb(), -10.0));
        QVERIFY(near(Compressor({200, 0, 2, 0}).thresholdDb(), -54.0)); // clamped
        // a lowering offset fades out as the threshold nears 0 dB: -2 + -10 = -12, scaled by 2 / 6.2
        QVERIFY(near(Compressor({10, 0, 2, 50}).thresholdDb(), -2.0 * -12.0 / -6.2));
    }

    void compressor_hardKnee()
    {
        Compressor c({100, valueOf("FxCompRatio", "4.00"), 2, 100});
        QCOMPARE(c.outputDb(-40.0), -40.0);
        QCOMPARE(c.outputDb(-20.0), -20.0);
        QVERIFY(near(c.outputDb(-8.0), -20.0 + 12.0 / 4.0));
        // INF holds the output near the threshold
        Compressor limiter({100, valueOf("FxCompRatio", "INF"), 2, 100});
        QVERIFY(near(limiter.outputDb(0.0), -20.0, 0.01));
    }

    void compressor_softKnees()
    {
        int32_t ratio = valueOf("FxCompRatio", "2.00");
        Compressor middle({100, ratio, 1, 100});
        Compressor soft({100, ratio, 0, 100});
        // at the threshold: the knee's quadratic, coefficient * width² * slope (in 20 dB units)
        QVERIFY(near(middle.outputDb(-20.0), -20.0 - 20.0 * 0.5 * 0.25 * 0.5));
        QVERIFY(near(soft.outputDb(-20.0), -20.0 - 20.0 * 0.19231 * 1.69 * 0.5));
        // untouched below the knee (10 dB below for Middle), the ratio above it
        QCOMPARE(middle.outputDb(-31.0), -31.0);
        QVERIFY(near(middle.outputDb(-5.0), -20.0 + 15.0 / 2.0));
        // continuous at the knee's ends (Soft's 0.19231 is the original's rounding of 1 / (4 * 1.3))
        QVERIFY(near(middle.outputDb(-10.0 - 1e-9), middle.outputDb(-10.0 + 1e-9), 1e-6));
        QVERIFY(near(soft.outputDb(6.0 - 1e-9), soft.outputDb(6.0 + 1e-9), 1e-3));
        // no compression without a threshold, whatever the knee
        Compressor none({0, ratio, 0, 100});
        QCOMPARE(none.outputDb(-1.0), -1.0);
    }

    void eq_flatAtZeroGain()
    {
        Eq eq(EqSettings{});
        for (double f : {20.0, 100.0, 1000.0, 10000.0, 20000.0})
        {
            QVERIFY2(near(eq.responseDb(f), 0.0, 1e-9), qPrintable(QString::number(f)));
        }
    }

    void eq_shelves()
    {
        EqSettings s;
        s.lowFreq = valueOf("FxEqLsvFreq", "200.0");
        s.lowGain = gain(12.0);
        s.highFreq = valueOf("FxEqHsvFreq", "5.00k");
        s.highGain = gain(-9.0);
        Eq eq(s);
        QCOMPARE(eq.frequency(Eq::Low), 200.0);
        QCOMPARE(eq.gainDb(Eq::High), -9.0);
        // the shelves reach their gain at DC and Nyquist, and leave the other end alone
        QVERIFY(near(eq.responseDb(0.001, Eq::Low), 12.0, 1e-3));
        QVERIFY(near(eq.responseDb(20000.0, Eq::Low), 0.0, 0.1));
        QVERIFY(near(eq.responseDb(20.0, Eq::High), 0.0, 0.01));
        QVERIFY(eq.responseDb(20000.0, Eq::High) < -8.0);
        // the sum of the bands
        QVERIFY(near(eq.responseDb(1000.0), eq.responseDb(1000.0, Eq::Low) + eq.responseDb(1000.0, Eq::Mid) +
                                                eq.responseDb(1000.0, Eq::High)));
    }

    void eq_peak()
    {
        EqSettings s;
        s.midFreq = valueOf("FxEqMidFreq", "1.00k");
        s.midGain = gain(6.0);
        s.midQ = valueOf("FxEqMidQual", "2.00");
        Eq boost(s);
        // exactly the gain at the centre (bilinear, prewarped), falling off either side
        QVERIFY(near(boost.responseDb(1000.0, Eq::Mid), 6.0, 1e-9));
        QVERIFY(boost.responseDb(500.0, Eq::Mid) < 6.0);
        QVERIFY(boost.responseDb(2000.0, Eq::Mid) < 6.0);
        QVERIFY(near(boost.responseDb(20.0, Eq::Mid), 0.0, 0.1));
        // below 18 dB the band is wider than its nominal Q (and the same either way)
        QVERIFY(near(boost.midQScale(), std::pow(10.0, (6.0 - 18.0) / 20.0 * 0.7), 1e-9));
        // a cut mirrors the boost
        s.midGain = gain(-6.0);
        Eq cut(s);
        for (double f : {200.0, 700.0, 1000.0, 1500.0, 5000.0})
        {
            QVERIFY(near(cut.responseDb(f, Eq::Mid), -boost.responseDb(f, Eq::Mid), 1e-9));
        }
    }

    void values_numbersAndBack()
    {
        QCOMPARE(valueNumber("FxEqMidFreq", valueOf("FxEqMidFreq", "1.06k")), 1060.0);
        QCOMPARE(valueNumber("FxEqMidGain", gain(-4.5)), -4.5);
        QCOMPARE(valueNumber("FxCompRatio", valueOf("FxCompRatio", "INF")), 3000.0);
        // nearest: on a log scale for frequencies, clamped to the range
        QCOMPARE(nearestValue("FxEqMidFreq", 1030.0), valueOf("FxEqMidFreq", "1.06k"));
        QCOMPARE(nearestValue("FxEqMidFreq", 5.0), valueOf("FxEqMidFreq", "20.0"));
        QCOMPARE(nearestValue("FxEqLsvFreq", 20000.0), Parameters::instance()["FxEqLsvFreq"].max);
        QCOMPARE(nearestValue("FxEqMidGain", 30.0), gain(18.0));
        QCOMPARE(nearestValue("FxEqMidGain", 2.04), gain(2.0));
        QCOMPARE(nearestValue("FxEqMidQual", 2.0), valueOf("FxEqMidQual", "2.00"));
    }

    void bandwidth_octavesAndQ()
    {
        // Q 1.41 is one octave, Q 0.67 two
        QVERIFY(near(qForOctaves(1.0), 1.4142, 1e-3));
        QVERIFY(near(qForOctaves(2.0), 0.6667, 1e-3));
        for (double q : {0.5, 1.0, 4.0, 16.0})
        {
            QVERIFY(near(qForOctaves(octavesForQ(q)), q, 1e-9));
        }
    }
};

QTEST_GUILESS_MAIN(ChannelStripCurvesTest)
#include "tst_ChannelStripCurves.moc"
