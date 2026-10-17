#include <QtTest>

#include "ReverbShape.h"
#include "ReverbTime.h"

using namespace UR44;
using namespace UR44::ReverbShape;

class ReverbShapeTest : public QObject
{
    Q_OBJECT

private slots:
    void autoZoom_firstRangeThatHoldsTheReverb()
    {
        QCOMPARE(autoZoom(100.0), 0);   // 500 ms
        QCOMPARE(autoZoom(475.0), 0);   // 95 % of it
        QCOMPARE(autoZoom(476.0), 1);   // 1 s
        QCOMPARE(autoZoom(5180.0), 6);  // 10 s: 5 s holds only 4.75 s
        QCOMPARE(autoZoom(60000.0), 8); // the last
    }

    void times_followTheSettings()
    {
        Settings s; // Hall
        s.highRatio = 10;
        s.lowRatio = 10; // both 1.0: the same reverb time everywhere
        Shape shape(s);
        const double rt = ReverbTime::seconds(0, s.roomSize, s.time) * 1000.0;
        QCOMPARE(shape.initialDelayMs(), 3.2);
        QCOMPARE(shape.reverbTimeMs(), rt + 3.2);
        for (double hz : {20.0, 100.0, 1000.0, 10000.0, 20000.0})
        {
            QVERIFY2(std::abs(shape.endMs(hz) - (rt + 3.2)) < 1.0, qPrintable(QString::number(hz)));
        }
        // the ratios shorten the ends of the range (first order shelves at the low frequency and 4 kHz: they
        // overlap, the middle doesn't get the whole reverb time either)
        s.highRatio = 5;
        s.lowRatio = 5;
        Shape shorter(s);
        QVERIFY(shorter.endMs(20000.0) < shorter.endMs(4000.0) * 0.7);
        QVERIFY(shorter.endMs(20.0) < shorter.endMs(4000.0) * 0.7);
    }

    void levels_decayFromThePeak()
    {
        for (int type : {0, 1, 2})
        {
            Settings s;
            s.type = type;
            s.highRatio = 10;
            s.lowRatio = 10;
            Shape shape(s);
            // silent before the initial delay, then up, then down for good
            QVERIFY(shape.levelDb(1, 1.0) < -90.0);
            double peak = -200.0, previous = 0.0, peakMs = 0.0;
            for (double ms = 4.0; ms < 4000.0; ms += 1.0)
            {
                double level = shape.levelDb(1, ms);
                if (level > peak)
                {
                    peak = level;
                    peakMs = ms;
                }
                if (ms > peakMs + 50.0)
                {
                    QVERIFY2(level <= previous + 1e-9, qPrintable(QString("type %1 at %2 ms").arg(type).arg(ms)));
                }
                previous = level;
            }
            QVERIFY2(peak > -15.0 && peak < -5.0, qPrintable(QString::number(peak)));
            // about 50 dB down from the peak by the end of the reverb time
            double atEnd = shape.levelDb(1, shape.reverbTimeMs());
            QVERIFY2(atEnd < peak - 40.0 && atEnd > peak - 60.0,
                     qPrintable(QString("type %1: %2 dB at the end, peak %3").arg(type).arg(atEnd).arg(peak)));
        }
    }

    void filters_cutTheEnds()
    {
        Settings s;
        s.hpf = 26; // 400 Hz
        s.lpf = 40; // 2.0 kHz
        Shape shape(s);
        QVERIFY(shape.filterDb(1000.0) > -3.0);
        QVERIFY(shape.filterDb(50.0) < -10.0);
        QVERIFY(shape.filterDb(16000.0) < -10.0);
        // the decay lines carry them: 100 Hz and 10 kHz are quieter than 1 kHz
        QVERIFY(shape.levelDb(0, 100.0) < shape.levelDb(1, 100.0) - 5.0);
        QVERIFY(shape.levelDb(2, 100.0) < shape.levelDb(1, 100.0) - 5.0);
    }
};

QTEST_GUILESS_MAIN(ReverbShapeTest)
#include "tst_ReverbShape.moc"
