#include <QtTest>

#include "Parameters.h"
#include "ReverbTime.h"

#include <cstring>

using namespace UR44;

class ParametersTest : public QObject
{
    Q_OBJECT

private slots:
    void table_isComplete()
    {
        const Parameters& p = Parameters::instance();
        QCOMPARE(p.size(), std::size_t(177));
        for (int id = 0; id <= 0xb0; ++id)
        {
            QVERIFY2(p.byId(uint16_t(id)), qPrintable(QString::number(id)));
            QCOMPARE(p.byId(uint16_t(id))->id, uint16_t(id));
        }
        QVERIFY(!p.byId(0xb1));
    }

    void lookup_byNameAndAlias()
    {
        const Parameters& p = Parameters::instance();
        QCOMPARE(p["ChSendMixBus2Level"].id, uint16_t(0x0d));
        QCOMPARE(p["ChSendMixBus2Level"].instances, 6);
        QCOMPARE(p["MorphCompAttack"].instances, 36);
        // macOS MemAPI 2.3.0 names
        QCOMPARE(p.byName("FxRevMixBus1Level"), p.byName("FxRevBus1Level"));
        QCOMPARE(p.byName("GtrFxCrunchHidMaxOut")->id, uint16_t(0x8a));
        QVERIFY(!p.byName("NoSuchParameter"));
    }

    void text_usesLabels()
    {
        const Parameters& p = Parameters::instance();
        const Parameter& level = p["MasterMixBus1Level"];
        QCOMPARE(level.text(103), QString("0.00 dB"));
        QCOMPARE(level.text(127), QString("6.00 dB"));
        QCOMPARE(level.text(0), QString("-INF dB"));

        const Parameter& pan = p["ChSendMixBus1Pan"];
        QCOMPARE(pan.min, -16);
        QCOMPARE(pan.text(-16), QString("L16"));
        QCOMPARE(pan.text(0), QString("C"));
        QCOMPARE(pan.text(16), QString("R16"));

        QCOMPARE(p["SysFsCode"].text(1), QString("48k Hz"));
        QCOMPARE(p["HaInputGain"].text(1), QString("-10dBV"));
        QCOMPARE(p["PatchAuxPhoneOutput"].text(1), QString("MIX 2 L/R"));
        // a parameter without labels shows the number
        QCOMPARE(p["FxRevRoomSize"].text(7), QString("7"));
    }

    void label_isCompact()
    {
        const Parameters& p = Parameters::instance();
        QCOMPARE(p["MasterMixBus1Level"].label(103), QString("0.00"));
        QCOMPARE(p["MasterMixBus1Level"].label(0), QString::fromUtf8("-\u221E"));
        QCOMPARE(p["ChSendMixBus1Pan"].label(0), QString("C"));
    }

    // The ranges the operation manual gives for REV-X time (page 13)
    void reverbTime_matchesManual()
    {
        using namespace UR44::ReverbTime;
        QCOMPARE(text(0, 0, 0), QString("0.103"));  // Hall minimum
        QCOMPARE(text(0, 31, 69), QString("31.0")); // Hall maximum
        QCOMPARE(text(1, 0, 0), QString("0.152"));  // Room
        QCOMPARE(text(1, 31, 69), QString("45.3"));
        QCOMPARE(text(2, 0, 0), QString("0.176"));  // Plate
        QCOMPARE(text(2, 31, 69), QString("52.0"));
        // at the reference room size the base table applies unchanged
        QCOMPARE(seconds(0, 30, 7), 1.0);
        QCOMPARE(seconds(0, 0, 70), 0.0);
    }

    void clamp_andWire()
    {
        const Parameter& pan = Parameters::instance()["ChSendMixBus1Pan"];
        QCOMPARE(pan.clamp(-100), -16);
        QCOMPARE(pan.clamp(100), 16);
        QCOMPARE(pan.fromWire(uint32_t(-5)), -5);
    }

    void nameIndex_isSortedAndComplete()
    {
        const Parameters& p = Parameters::instance();
        using UR44::Generated::NAME_INDEX;
        using UR44::Generated::NAME_INDEX_COUNT;
        for (std::size_t i = 1; i < NAME_INDEX_COUNT; ++i)
        {
            QVERIFY2(std::strcmp(NAME_INDEX[i - 1].name, NAME_INDEX[i].name) < 0, NAME_INDEX[i].name);
        }
        // every parameter is found by its own name, both as const char* and QString
        for (const Parameter& param : p)
        {
            QCOMPARE(p.byName(param.name), &param);
            QCOMPARE(p.byName(QString::fromLatin1(param.name)), &param);
        }
        QCOMPARE(NAME_INDEX_COUNT, p.size() + 3); // + the three macOS aliases
    }

    void ranges_areConsistent()
    {
        for (const Parameter& param : Parameters::instance())
        {
            QVERIFY(param.max >= param.min);
            QVERIFY(param.instances >= 1);
            QVERIFY(param.isValid(param.defaultValue));
            // (the label count per range is checked by the generator, where a mismatch can be detected)
        }
    }
};

QTEST_GUILESS_MAIN(ParametersTest)
#include "tst_Parameters.moc"
