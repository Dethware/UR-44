#include <QtTest>

#include "AlsaTransport.h"
#include "Protocol.h"

using namespace UR44;
using namespace UR44::Protocol;

namespace
{
QByteArray hex(const char* s)
{
    return QByteArray::fromHex(QByteArray(s).replace(' ', ""));
}
}

class ProtocolTest : public QObject
{
    Q_OBJECT

private slots:
    void controlPort_byUsbId()
    {
        // Class Compliant mode, [CC MODE] off, and anything else (successors aren't supported)
        QCOMPARE(AlsaTransport::controlPortSuffix("0499:1704"), QString(",0,1"));
        QCOMPARE(AlsaTransport::controlPortSuffix("0499:150e"), QString(",1,0"));
        QVERIFY(AlsaTransport::controlPortSuffix("0499:1730").isEmpty()); // UR44C
        QVERIFY(AlsaTransport::controlPortSuffix("").isEmpty());
    }

    // Byte sequences confirmed against the device (docs/PROTOCOL.md)
    void encode_matchesKnownMessages()
    {
        QCOMPARE(getParameter(0x0d, 0), hex("F0 43 30 3E 14 01 01 00 00 0D 00 00 00 F7"));
        QCOMPARE(setParameter(0x0d, 0, 100), hex("F0 43 10 3E 14 01 01 00 00 0D 00 00 00 00 00 00 00 64 F7"));
        QCOMPARE(getVersion(), hex("F0 43 30 3E 14 00 70 02 F7"));
        QCOMPARE(meters(true), hex("F0 43 30 3E 14 02 02 32 7F F7"));
        QCOMPARE(meters(false), hex("F0 43 30 3E 14 02 02 32 00 F7"));
        QCOMPARE(dumpRequest(DumpType::All), hex("F0 43 20 3E 14 01 01 00 00 00 00 03 F7"));
        QCOMPARE(fxAssign(FxType::ChannelStrip, 0, InsertPoint::Post), hex("F0 43 10 3E 14 03 08 01 00 00 00 00 01 F7"));
        QCOMPARE(fxUnassign(FxType::GuitarAmp, 0), hex("F0 43 10 3E 14 03 08 02 01 00 00 00 00 F7"));
    }

    void encode_multiByteNumbers()
    {
        // id 0xb0 = 1 0110000 -> 00 01 30; negative values go out as u32 two's complement
        QCOMPARE(setParameter(0xb0, 35, uint32_t(-16)), hex("F0 43 10 3E 14 01 01 00 01 30 00 00 23 0F 7F 7F 7F 70 F7"));
    }

    void parse_parameterValue()
    {
        auto m = parse(hex("F0 43 10 3E 14 01 01 00 00 28 00 00 01 00 00 00 00 01 F7"));
        auto* pv = std::get_if<ParameterValue>(&m);
        QVERIFY(pv);
        QCOMPARE(pv->id, uint16_t(0x28));
        QCOMPARE(pv->index, uint16_t(1));
        QCOMPARE(pv->value, uint32_t(1));

        // A set message decodes the same way (round trip)
        auto m2 = parse(setParameter(0x10, 5, uint32_t(-3)));
        auto* pv2 = std::get_if<ParameterValue>(&m2);
        QVERIFY(pv2);
        QCOMPARE(int32_t(pv2->value), -3);
    }

    void parse_version()
    {
        auto m = parse(hex("F0 43 10 3E 14 00 70 02 00 42 4F 4F 54 00 00 00 00 56 32 2E 31 32 20 00 00 "
                           "4D 41 49 4E 00 00 00 00 56 32 2E 31 32 20 00 00 F7"));
        auto* v = std::get_if<Version>(&m);
        QVERIFY(v);
        QCOMPARE(v->components.size(), 2);
        QCOMPARE(v->components[0].first, QString("BOOT"));
        QCOMPARE(v->components[0].second, QString("V2.12"));
        QCOMPARE(v->components[1].first, QString("MAIN"));
    }

    void parse_meterFrame()
    {
        QByteArray m = hex("F0 43 10 3E 14 02 02");
        for (uint32_t v : {0u, 3160u, 40959u, 8192u})
        {
            m.append(char((v >> 14) & 0x7F));
            m.append(char((v >> 7) & 0x7F));
            m.append(char(v & 0x7F));
        }
        m.append(char(0xF7));
        auto parsed = parse(m);
        auto* f = std::get_if<MeterFrame>(&parsed);
        QVERIFY(f);
        QCOMPARE(f->values, (QList<uint32_t>{0, 3160, 40959, 8192}));
    }

    void meters_scale()
    {
        QCOMPARE(Meters::levelDb(8192), 0.0);
        QCOMPARE(Meters::levelDb(0), Meters::FLOOR_DB);
        QVERIFY(qAbs(Meters::levelDb(6126) - -20.18) < 0.01);
        QCOMPARE(Meters::gainReductionDb(0x9FFF), 0.0);
        QVERIFY(qAbs(Meters::gainReductionDb(0x1DF3) - (8191 - 0x1DF3) / 102.4) < 1e-9);
    }

    void pack7_roundTrip()
    {
        QByteArray data;
        for (int i = 0; i < 300; ++i)
        {
            data.append(char((i * 37) & 0xFF));
        }
        QByteArray packed = pack7(data);
        for (char c : packed)
        {
            QVERIFY(!(uint8_t(c) & 0x80));
        }
        QCOMPARE(packed.size() % 8, 0);
        QCOMPARE(unpack7(packed).left(data.size()), data);
    }

    void bulkDump_roundTrip()
    {
        QByteArray payload("\x80\x01\xff\x00 some payload \xfe", 18);
        QByteArray message = bulkDump(DumpType::CurrentScene, payload);
        auto m = parse(message);
        auto* d = std::get_if<Dump>(&m);
        QVERIFY(d);
        QCOMPARE(d->type, uint32_t(1));
        QCOMPARE(d->payload.left(payload.size()), payload);

        // A corrupted checksum is rejected
        message[message.size() - 2] = char(message.at(message.size() - 2) ^ 1);
        QVERIFY(std::holds_alternative<Unknown>(parse(message)));
    }

    void parse_rejectsOtherMessages()
    {
        QVERIFY(std::holds_alternative<Unknown>(parse(hex("F0 7E 7F 06 02 F7"))));
        QVERIFY(std::holds_alternative<Unknown>(parse(hex("F0 43 10 3E 15 01 01 00 00 00 00 00 00 00 00 00 00 00 F7"))));
        QVERIFY(std::holds_alternative<Unknown>(parse(QByteArray())));
    }

    void splitter_reassemblesChunks()
    {
        SysExSplitter s;
        QByteArray a = getVersion();
        QByteArray b = meters(true);
        QByteArray stream = QByteArray("\x90\x40\x7f", 3) + a + b; // a note-on before the SysEx is dropped
        QList<QByteArray> out;
        for (int i = 0; i < stream.size(); i += 3)
        {
            out += s.feed(stream.mid(i, 3));
        }
        QCOMPARE(out, (QList<QByteArray>{a, b}));
    }

    void splitter_dropsAbortedMessages()
    {
        SysExSplitter s;
        QByteArray stream = hex("F0 43 10 90") + getVersion();
        QCOMPARE(s.feed(stream), QList<QByteArray>{getVersion()});
    }
};

QTEST_GUILESS_MAIN(ProtocolTest)
#include "tst_Protocol.moc"
