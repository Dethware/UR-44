#include <QtTest>

#include "Parameters.h"

using namespace UR44;

class ParametersTest : public QObject
{
    Q_OBJECT

private slots:
    void table_isComplete()
    {
        const Parameters& p = Parameters::instance();
        QCOMPARE(p.all().size(), 177);
        for (int id = 0; id <= 0xb0; ++id)
        {
            QVERIFY2(p.byId(uint16_t(id)), qPrintable(QString::number(id)));
        }
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

    void clamp_andWire()
    {
        const Parameter& pan = Parameters::instance()["ChSendMixBus1Pan"];
        QCOMPARE(pan.clamp(-100), -16);
        QCOMPARE(pan.clamp(100), 16);
        QCOMPARE(pan.fromWire(uint32_t(-5)), -5);
    }

    void fromJson_rejectsInvalid()
    {
        QString error;
        QVERIFY(Parameters::fromJson("not json", &error).isEmpty());
        QVERIFY(!error.isEmpty());
        // label count must match the range
        QVERIFY(Parameters::fromJson(R"({"parameters": [{"id": 1, "name": "X", "min": 0, "max": 2, "labels": ["a"]}]})")
                    .isEmpty());
    }
};

QTEST_GUILESS_MAIN(ParametersTest)
#include "tst_Parameters.moc"
