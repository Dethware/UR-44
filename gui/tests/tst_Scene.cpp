#include <QtTest>

#include "BulkDump.h"
#include "Effects.h"
#include "Parameters.h"
#include "Scene.h"
#include "SweetSpot.h"

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

uint16_t id(const char* name)
{
    return Parameters::instance()[name].id;
}

/// The captured scene dump, without the few bytes of 7-bit padding the device adds at the end
QByteArray capturedScene()
{
    return BulkDump::decode(readData("dump_currentscene.bin"))->encode();
}
}

// The dumps were captured from a real UR44 (docs/data). A scene is the "CurrentScene" dump: the state dump's format
// without the system/routing items, which the real device confirms by its own CurrentScene dump.
class SceneTest : public QObject
{
    Q_OBJECT

private slots:
    void format_isTheDevicesSceneFormat()
    {
        auto all = BulkDump::decode(readData("dump_all.bin"));
        auto scene = BulkDump::decode(readData("dump_currentscene.bin"));
        const auto format = Scenes::format(all->groups);
        QCOMPARE(format.size(), scene->groups.size());
        for (int g = 0; g < format.size(); ++g)
        {
            QCOMPARE(format[g].instances, scene->groups[g].instances);
            QCOMPARE(format[g].items.size(), scene->groups[g].items.size());
            for (int i = 0; i < format[g].items.size(); ++i)
            {
                QCOMPARE(format[g].items[i].id, scene->groups[g].items[i].id);
            }
        }
        QVERIFY(!Scenes::isSceneItem(id("SysFsCode")));
        QVERIFY(!Scenes::isSceneItem(id("PatchAuxPhoneOutput")));
        QVERIFY(Scenes::isSceneItem(id("PatchFxSend")));
        QVERIFY(Scenes::isSceneItem(id("ChFxInsPostPatch")));
    }

    void normalize_convertsHostOrder()
    {
        const QByteArray device = capturedScene();
        QByteArray host = Scenes::toHostOrder(device);
        QCOMPARE(host.size(), device.size());
        QVERIFY(host != device);
        // dspMixFx's form: version 0x2774 and the size as little-endian u32s
        QCOMPARE(host.mid(24, 4), QByteArray::fromHex("74270000"));
        QCOMPARE(Scenes::normalize(host).value_or(QByteArray()), device);
        QCOMPARE(Scenes::normalize(device).value_or(QByteArray()), device);
    }

    void normalize_rejectsOtherData()
    {
        QString error;
        QVERIFY(!Scenes::normalize(QByteArray("junk"), &error));
        QVERIFY(!error.isEmpty());
        // a state dump isn't a scene
        QVERIFY(!Scenes::normalize(BulkDump::decode(readData("dump_all.bin"))->encode(), &error));
        QVERIFY(error.contains("All"));
        // truncated
        QVERIFY(!Scenes::normalize(capturedScene().chopped(10)));
    }

    void defaults_areFactoryState()
    {
        auto all = BulkDump::decode(readData("dump_all.bin"));
        auto scene = BulkDump::decode(Scenes::defaults(Scenes::format(all->groups)));
        QVERIFY(scene);
        QCOMPARE(scene->name, QString("CurrentScene"));
        QCOMPARE(scene->values[id("ChSendMixBus1Onf")], QList<int32_t>(6, 1));
        QCOMPARE(scene->values[id("ChSendMixBus1Level")], QList<int32_t>(6, Parameters::instance()["ChSendMixBus1Level"].defaultValue));
        QCOMPARE(scene->values[id("ChFxInsPrePatch")], QList<int32_t>(6, 0));
        QCOMPARE(scene->values[id("ChFxInsPostPatch")], QList<int32_t>(6, 0));
        QVERIFY(!scene->values.contains(id("SysFsCode")));

        // every strip: "01 Basic", with the compressor/EQ on its first point (MORPH at 0)
        const SweetSpotPreset& basic = SweetSpotPreset::builtIn().first();
        for (int strip = 0; strip < 4; ++strip)
        {
            QCOMPARE(scene->arrays[id("MorphSystemName")].at(strip).left(8), QByteArray("01 Basic"));
            QCOMPARE(scene->arrays[id("MorphSystemId")].at(strip), QByteArray("0001"));
            for (size_t f = 0; f < 17; ++f)
            {
                QCOMPARE(scene->values[id(Effects::MORPH_TABLE_PARAMETERS[f])].at(strip * 9 + 4), basic.points[4][f]);
                QCOMPARE(scene->values[id(Effects::MORPHED_PARAMETERS[f])].at(strip), basic.points[0][f]);
            }
        }
    }

    void file_roundTrips()
    {
        const QByteArray a = capturedScene();
        auto decoded = BulkDump::decode(a);
        decoded->values[id("ChSendMixBus2Pan")][5] = 7;
        const QByteArray b = decoded->encode();

        QByteArray xml = Scenes::writeFile({{"Vocals", a}, {"Ünïcödé & <stuff>", b}}, a);
        QVERIFY(xml.contains("<!DOCTYPE UR44>"));
        QVERIFY(xml.contains("<Scene No=\"2\""));

        QString error;
        auto file = Scenes::readFile(xml, &error);
        QVERIFY2(file, qPrintable(error));
        QCOMPARE(file->scenes.size(), 2);
        QCOMPARE(file->scenes[0].name, QString("Vocals"));
        QCOMPARE(file->scenes[0].data, a);
        QCOMPARE(file->scenes[1].name, QString("Ünïcödé & <stuff>"));
        QCOMPARE(file->scenes[1].data, b);
        QCOMPARE(file->current.value_or(QByteArray()), a);
    }

    void file_readsDspMixFxLayout()
    {
        // as dspMixFx writes it: slots out of order and with gaps, UI memory elements inside the scenes
        const QByteArray host = Scenes::toHostOrder(capturedScene()).toBase64();
        QByteArray xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE UR44>\n<UR44File>\n"
                         "<Header>UR44 File</Header><Version>1.0.0</Version><SceneData>\n"
                         "<Scene No=\"7\" name=\"Late\" sz=\"1\" cvtSz=\"1\" IO=\"0\">\n" + host +
                         "\n<MuteStatus ChSz=\"6\" OutChSz=\"2\">AAAA</MuteStatus>"
                         "<FxInsPosStatus ChSz=\"6\">AAAA</FxInsPosStatus></Scene>\n"
                         "<Scene No=\"2\" name=\"Early\">" + host + "</Scene>\n"
                         "</SceneData><CurrentScene>2</CurrentScene></UR44File>\n";
        QString error;
        auto file = Scenes::readFile(xml, &error);
        QVERIFY2(file, qPrintable(error));
        QCOMPARE(file->scenes.size(), 2);
        QCOMPARE(file->scenes[0].name, QString("Early"));
        QCOMPARE(file->scenes[1].name, QString("Late"));
        QCOMPARE(file->scenes[1].data, capturedScene());
        QVERIFY(!file->current);
    }

    void file_rejectsBadInput()
    {
        QString error;
        QVERIFY(!Scenes::readFile("<html/>", &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!Scenes::readFile("<UR44File><SceneData><Scene No=\"1\" name=\"x\">bm90IGEgc2NlbmU=</Scene>"
                                  "</SceneData></UR44File>",
                                  &error));
        QVERIFY(error.contains("x"));
    }
};

QTEST_GUILESS_MAIN(SceneTest)
#include "tst_Scene.moc"
