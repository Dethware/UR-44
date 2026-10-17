#include <QtTest>

#include "Pairing.h"
#include "Parameters.h"

#include <QHash>

using namespace UR44;
using namespace UR44::Pairing;

namespace
{
uint16_t id(const char* name)
{
    return Parameters::instance()[name].id;
}

/// A client's state: unset values are 0
struct State
{
    QHash<QPair<uint16_t, uint16_t>, int32_t> values;

    void set(const char* name, uint16_t index, int32_t value) { values[{id(name), index}] = value; }
    int32_t get(const char* name, uint16_t index) const { return values.value({id(name), index}, 0); }
    Lookup lookup() const
    {
        return [this](uint16_t i, uint16_t n) { return std::optional<int32_t>(values.value({i, n}, 0)); };
    }
    /// A set, with the firmware's follow-ups, like Device
    void apply(const char* name, uint16_t index, int32_t value)
    {
        const QList<Change> changes = followUps(lookup(), id(name), index, value);
        set(name, index, value);
        for (const Change& c : changes)
        {
            values[{c.id, c.index}] = c.value;
        }
    }
};
}

class PairingTest : public QObject
{
    Q_OBJECT

private slots:
    void partners()
    {
        QCOMPARE(partner(2), uint16_t(3));
        QCOMPARE(partner(3), uint16_t(2));
        QCOMPARE(left(5), uint16_t(4));
    }

    void unpaired_changesStayPut()
    {
        State s;
        s.apply("ChHpfOnf", 2, 1);
        s.apply("ChSendMixBus1Level", 2, 50);
        QCOMPARE(s.get("ChHpfOnf", 3), 0);
        QCOMPARE(s.get("ChSendMixBus1Level", 3), 0);
    }

    void pairing_copiesLeftToRight()
    {
        State s;
        s.set("ChFaderLinkOnf", 2, 1);
        s.set("ChFaderLinkOnf", 3, 1);
        s.set("ChHpfOnf", 2, 1);
        s.set("ChSendMixBus2Onf", 2, 1);
        s.set("ChSendMixBus2Level", 2, 90);
        s.set("ChSendMixBus2Level", 3, 70);
        s.set("ChSendMixBus2Pan", 3, 5);
        s.set("ChPhaseOnf", 2, 1);
        // either channel's switch pairs both, and the left channel's settings win
        s.apply("ChPairOnf", 3, 1);
        QCOMPARE(s.get("ChPairOnf", 2), 1);
        QCOMPARE(s.get("ChHpfOnf", 3), 1);
        QCOMPARE(s.get("ChSendMixBus2Onf", 3), 1);
        QCOMPARE(s.get("ChSendMixBus2Level", 3), 90);
        // the right one keeps its pans and phase
        QCOMPARE(s.get("ChSendMixBus2Pan", 3), 5);
        QCOMPARE(s.get("ChPhaseOnf", 3), 0);
        // the other pairs aren't touched
        QCOMPARE(s.get("ChPairOnf", 4), 0);
    }

    void pairing_withoutFaderLink_keepsTheLevels()
    {
        State s;
        s.set("ChSendMixBus1Level", 0, 90);
        s.set("ChSendMixBus1Level", 1, 70);
        s.apply("ChPairOnf", 0, 1);
        QCOMPARE(s.get("ChSendMixBus1Level", 1), 70);
    }

    void paired_linkedChangesReachThePartner()
    {
        State s;
        s.apply("ChPairOnf", 4, 1);
        for (const char* name : {"ChHpfFreq", "ChSendMixBus1Onf", "ChSendFxBusOnf", "ChSendFxBusLevel",
                                 "ChSendMixBus2SoloOnf"})
        {
            s.apply(name, 5, 3);
            QVERIFY2(s.get(name, 4) == 3, name);
        }
        s.apply("ChSendMixBus1Pan", 4, -16);
        QCOMPARE(s.get("ChSendMixBus1Pan", 5), 0);
        s.apply("ChPhaseOnf", 5, 1);
        QCOMPARE(s.get("ChPhaseOnf", 4), 0);
    }

    void faderLink_decidesTheLevels()
    {
        State s;
        s.set("ChFaderLinkOnf", 2, 1);
        s.set("ChFaderLinkOnf", 3, 1);
        s.apply("ChPairOnf", 2, 1);
        s.apply("ChSendMixBus1Level", 3, 60);
        QCOMPARE(s.get("ChSendMixBus1Level", 2), 60);

        // off: on both, and the levels are separate (the FX Bus level stays linked)
        s.apply("ChFaderLinkOnf", 2, 0);
        QCOMPARE(s.get("ChFaderLinkOnf", 3), 0);
        s.apply("ChSendMixBus1Level", 3, 40);
        QCOMPARE(s.get("ChSendMixBus1Level", 2), 60);
        s.apply("ChSendFxBusLevel", 3, 40);
        QCOMPARE(s.get("ChSendFxBusLevel", 2), 40);

        // on again (from either channel): the left channel's levels to the right one
        s.apply("ChFaderLinkOnf", 3, 1);
        QCOMPARE(s.get("ChFaderLinkOnf", 2), 1);
        QCOMPARE(s.get("ChSendMixBus1Level", 3), 60);
    }

    void stereoStrip_mirrorsSettingsNotSweetSpotData()
    {
        State s;
        s.set("FxStereoLinkOnf", 0, 1);
        s.set("FxStereoLinkOnf", 1, 1);
        s.apply("FxCompDrive", 1, 150);
        QCOMPARE(s.get("FxCompDrive", 0), 150);
        s.apply("FxEqBypass", 0, 1);
        QCOMPARE(s.get("FxEqBypass", 1), 1);
        // not the morph tables, nor strips that aren't linked
        s.apply("MorphCompAttack", 9, 5);
        QCOMPARE(s.get("MorphCompAttack", 0), 0);
        s.apply("FxCompDrive", 2, 10);
        QCOMPARE(s.get("FxCompDrive", 3), 0);
        // nor the reverb (one instance)
        QVERIFY(followUps(s.lookup(), id("FxRevTime"), 0, 10).isEmpty());
    }

    void resync_onlyForPairing()
    {
        QVERIFY(needsResync(id("ChPairOnf")));
        QVERIFY(!needsResync(id("ChHpfOnf")));
    }
};

QTEST_GUILESS_MAIN(PairingTest)
#include "tst_Pairing.moc"
