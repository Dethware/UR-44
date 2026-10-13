#include <QtTest>

#include "BulkDump.h"

using namespace UR44;

namespace
{
QByteArray readData(const char* name)
{
    QFile f(QString(TEST_DATA_DIR) + "/" + name);
    if (!f.open(QIODevice::ReadOnly))
    {
        qFatal("missing test data %s", name);
    }
    return f.readAll();
}

QByteArray chunk(const QByteArray& name, const QByteArray& body)
{
    QByteArray header = name.leftJustified(16, '\0');
    header.append(QByteArray::fromHex("0001000000000000000000002774"));
    header.append(char((body.size() >> 8) & 0xFF));
    header.append(char(body.size() & 0xFF));
    return header + body;
}
}

// The dumps were captured from a real UR44 (docs/data); expected values come from tools/ur44_dump.py, which was
// verified against per-parameter reads of the same device.
class BulkDumpTest : public QObject
{
    Q_OBJECT

private slots:
    void decode_all()
    {
        QString error;
        auto dump = BulkDump::decode(readData("dump_all.bin"), &error);
        QVERIFY2(dump, qPrintable(error));
        QCOMPARE(dump->name, QString("All"));
        QCOMPARE(dump->groups.size(), 11);
        int items = 0;
        for (const auto& g : dump->groups)
        {
            items += g.items.size();
        }
        QCOMPARE(items, 174);
        QCOMPARE(dump->values.size(), 172);
        QCOMPARE(dump->arrays.size(), 2);

        QCOMPARE(dump->values[0x09], (QList<int32_t>{0, 1, 1, 1, 1, 1})); // ChSendMixBus1Onf
        QCOMPARE(dump->values[0x0c], (QList<int32_t>{127, 103, 103, 103, 103, 103})); // ChSendMixBus1Level
        QCOMPARE(dump->values[0x26], QList<int32_t>{1}); // SysFsCode = 48k
        QCOMPARE(dump->values[0x28], (QList<int32_t>{1, 0, 0})); // HaP48VOnf
        QCOMPARE(dump->values[0x29], (QList<int32_t>{0, 0, 1})); // HaInputGain
        QCOMPARE(dump->values[0x53].size(), 36); // MorphCompAttack: 4 strips x 9 points
        QCOMPARE(dump->values[0x53].mid(0, 4), (QList<int32_t>{184, 165, 147, 181}));
        QCOMPARE(dump->arrays[0x52].at(0).left(8), QByteArray("01 Basic")); // MorphSystemName
    }

    void decode_currentScene()
    {
        auto dump = BulkDump::decode(readData("dump_currentscene.bin"));
        QVERIFY(dump);
        QCOMPARE(dump->name, QString("CurrentScene"));
        QCOMPARE(dump->groups.size(), 9);
        QVERIFY(!dump->values.contains(0x26)); // system settings aren't part of a scene
        QCOMPARE(dump->values[0x0c], (QList<int32_t>{127, 103, 103, 103, 103, 103}));
    }

    void decode_signedValues()
    {
        // one group of 2 instances: id 0x0f (pan, signed, 1 byte) and id 0x0c (level, unsigned, 1 byte)
        QByteArray format = QByteArray::fromHex("80010002" "000f0001" "000c0101");
        QByteArray data = QByteArray::fromHex("f010" "7f80");
        auto dump = BulkDump::decode(chunk("F_Test", format) + chunk("D_Test", data));
        QVERIFY(dump);
        QCOMPARE(dump->values[0x0f], (QList<int32_t>{-16, 16}));
        QCOMPARE(dump->values[0x0c], (QList<int32_t>{127, 128}));
    }

    void decode_rejectsMalformed()
    {
        QString error;
        QVERIFY(!BulkDump::decode(QByteArray(), &error));
        QVERIFY(!error.isEmpty());

        QByteArray format = QByteArray::fromHex("80010002" "000c0101");
        // too little data, too much data, missing data chunk
        QVERIFY(!BulkDump::decode(chunk("F_T", format) + chunk("D_T", QByteArray(1, 0))));
        QVERIFY(!BulkDump::decode(chunk("F_T", format) + chunk("D_T", QByteArray(3, 0))));
        QVERIFY(!BulkDump::decode(chunk("F_T", format)));
        // truncated chunk
        QVERIFY(!BulkDump::decode(chunk("F_T", format).left(40)));
    }
};

QTEST_GUILESS_MAIN(BulkDumpTest)
#include "tst_BulkDump.moc"
