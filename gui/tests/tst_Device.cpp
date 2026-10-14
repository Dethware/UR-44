#include <QtTest>

#include "BulkDump.h"
#include "Device.h"
#include "Effects.h"
#include "SweetSpot.h"
#include "Parameters.h"
#include "Protocol.h"
#include "Transport.h"

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

/*
 * Behaves like the UR44 on its control port: answers the version request, sends the captured "All" dump, keeps
 * parameter registers for get/set, and streams meter frames on request. Replies are asynchronous, like real I/O.
 */
class FakeUR44 : public Transport
{
    Q_OBJECT
public:
    QString detectResult = "hw:9,0,1";
    bool openFails = false;
    bool answers = true;
    /// Values the device "insists on": a set of these is ignored
    QHash<uint32_t, uint32_t> forced;
    QList<QByteArray> written;
    QHash<uint32_t, uint32_t> registers;
    bool meters = false;
    int opens = 0;
    int dumpRequests = 0;
    int morphTableWrites = 0;
    /// Byte-array parameters written by the host (others come from the captured dump)
    QHash<uint32_t, QByteArray> arrays;
    /// ChFxInsPrePatch, ChFxInsPostPatch, ChGtrFxInsPrePatch, ChGtrFxInsPostPatch
    const QList<uint16_t> INSERT_IDS{0x05, 0x06, 0xaf, 0xb0};

    FakeUR44()
    {
        auto dump = BulkDump::decode(readData("dump_all.bin"));
        for (auto it = dump->values.constBegin(); it != dump->values.constEnd(); ++it)
        {
            for (int i = 0; i < it.value().size(); ++i)
            {
                registers.insert(key(it.key(), i), uint32_t(it.value().at(i)));
            }
        }
    }

    static uint32_t key(uint16_t id, int index) { return (uint32_t(id) << 16) | uint16_t(index); }

    QString detect() override { return detectResult; }
    bool open(const QString& port, QString* error) override
    {
        ++opens;
        if (openFails)
        {
            *error = port + " is in use by another program";
            return false;
        }
        m_open = true;
        return true;
    }
    void close() override { m_open = false; }
    bool isOpen() const override { return m_open; }
    bool write(const QByteArray& bytes) override
    {
        written.append(bytes);
        if (answers)
        {
            QTimer::singleShot(0, this, [this, bytes]() { respond(bytes); });
        }
        return true;
    }

    /// Simulates a device-side message (e.g. a hardware switch)
    void deviceSends(const QByteArray& message) { emit received(message); }
    void unplug() { m_open = false; emit failed("read failed: No such device"); }

    int count(const QByteArray& message) const { return int(written.count(message)); }

private:
    void respond(const QByteArray& request)
    {
        if (!m_open)
        {
            return;
        }
        if (request == Protocol::getVersion())
        {
            // the reply captured from the real device
            emit received(QByteArray::fromHex("F043103E1400700200"
                                              "424F4F540000000056322E3132200000"
                                              "4D41494E0000000056322E3132200000F7"));
        }
        else if (request == Protocol::dumpRequest(Protocol::DumpType::All))
        {
            ++dumpRequests;
            QByteArray reply = Protocol::bulkDump(Protocol::DumpType::All, currentDump());
            if (holdDumps)
            {
                m_heldDumps.append(reply);
            }
            else
            {
                emit received(reply);
            }
        }
        else if (request.size() == 14 && request.startsWith(QByteArray::fromHex("F043103E140308")))
        {
            // FX assign / unassign: like the real device, take strip 1 (or clear the channel's inserts)
            bool assign = request.at(7) == 1;
            bool guitar = request.at(8) == 1;
            uint16_t ch = uint16_t(Protocol::readBE7(request.constData() + 9, 3));
            bool post = request.at(12) == 1;
            for (uint16_t id : INSERT_IDS)
            {
                registers[key(id, ch)] = 0;
            }
            if (assign)
            {
                uint16_t id = guitar ? (post ? 0xb0 : 0xaf) : (post ? 0x06 : 0x05);
                registers[key(id, ch)] = 1;
            }
        }
        else if (auto m = Protocol::parse(request); std::holds_alternative<Protocol::Dump>(m))
        {
            // a morph table write: apply it and acknowledge like the real device
            const auto& dump = std::get<Protocol::Dump>(m);
            if (dump.type == uint32_t(Protocol::DumpType::MorphTable))
            {
                ++morphTableWrites;
                auto table = BulkDump::decode(dump.payload);
                for (auto it = table->values.constBegin(); it != table->values.constEnd(); ++it)
                {
                    for (int i = 0; i < it.value().size(); ++i)
                    {
                        registers[key(it.key(), i)] = uint32_t(it.value().at(i));
                    }
                }
                for (auto it = table->arrays.constBegin(); it != table->arrays.constEnd(); ++it)
                {
                    for (int i = 0; i < it.value().size(); ++i)
                    {
                        arrays[key(it.key(), i)] = it.value().at(i);
                    }
                }
                emit received(QByteArray::fromHex("F043103E1403020200000000 02F7").replace(' ', ""));
            }
        }
        else if (request == Protocol::meters(true))
        {
            meters = true;
        }
        else if (request == Protocol::meters(false))
        {
            meters = false;
        }
        else if (auto m = Protocol::parse(request); auto* pv = std::get_if<Protocol::ParameterValue>(&m))
        {
            // a set (same layout as a reply)
            uint32_t k = key(pv->id, pv->index);
            if (!forced.contains(k))
            {
                registers[k] = pv->value;
            }
        }
        else if (request.size() == 14 && request.startsWith(QByteArray::fromHex("F043303E140101")))
        {
            uint16_t id = uint16_t(Protocol::readBE7(request.constData() + 7, 3));
            uint16_t index = uint16_t(Protocol::readBE7(request.constData() + 10, 3));
            uint32_t k = key(id, index);
            uint32_t v = forced.value(k, registers.value(k, 0x040480C0)); // out of range: neighbouring memory
            if (INSERT_IDS.contains(id) && v != 0)
            {
                return; // firmware quirk: no reply for an insert parameter while an effect is assigned
            }
            emit received(Protocol::setParameter(id, index, v));
        }
    }

public:
    /// The captured dump with its values replaced by the current registers
    QByteArray currentDump() const
    {
        QByteArray payload = readData("dump_all.bin");
        auto dump = BulkDump::decode(payload);
        // the data chunk is the second one: its body starts after both headers and the format body
        int formatSize = (uint8_t(payload.at(30)) << 8) | uint8_t(payload.at(31));
        int at = 32 + formatSize + 32;
        for (const auto& group : dump->groups)
        {
            for (const auto& item : group.items)
            {
                for (int inst = 0; inst < group.instances; ++inst)
                {
                    for (int e = 0; e < item.count; ++e)
                    {
                        if (!item.isArray())
                        {
                            uint32_t v = registers.value(key(item.id, inst));
                            for (int b = 0; b < item.size; ++b)
                            {
                                payload[at + b] = char(v >> (8 * (item.size - 1 - b)));
                            }
                        }
                        else if (arrays.contains(key(item.id, inst)))
                        {
                            payload[at] = arrays.value(key(item.id, inst)).at(e);
                        }
                        at += item.size;
                    }
                }
            }
        }
        return payload;
    }

    /// Dumps are captured when requested; with `holdDumps` they're delivered only by releaseDumps()
    bool holdDumps = false;
    void releaseDumps()
    {
        for (const QByteArray& d : std::exchange(m_heldDumps, {}))
        {
            emit received(d);
        }
    }

private:
    QList<QByteArray> m_heldDumps;
    bool m_open = false;
};

const Parameter& P(const char* name)
{
    return Parameters::instance()[name];
}
}

class DeviceTest : public QObject
{
    Q_OBJECT

private:
    FakeUR44* m_fake = nullptr;
    std::unique_ptr<Device> m_device;

    void makeDevice(const QString& port = QString())
    {
        m_fake = new FakeUR44;
        m_device = std::make_unique<Device>(m_fake, port);
    }

    void connectReady()
    {
        m_device->start();
        QTRY_COMPARE(m_device->state(), Device::State::Ready);
    }

private slots:
    void cleanup()
    {
        m_device.reset();
    }

    void connect_loadsState()
    {
        makeDevice();
        QSignalSpy states(m_device.get(), &Device::stateChanged);
        QSignalSpy loaded(m_device.get(), &Device::stateLoaded);
        connectReady();

        QCOMPARE(m_device->port(), QString("hw:9,0,1"));
        QCOMPARE(m_device->firmwareVersion(), QString("BOOT V2.12, MAIN V2.12"));
        QCOMPARE(loaded.size(), 1);
        QCOMPARE(states.size(), 3); // Connecting, Loading, Ready
        // a stale meter stream is stopped before anything else
        QCOMPARE(m_fake->written.first(), Protocol::meters(false));

        QCOMPARE(m_device->value("ChSendMixBus1Onf", 0), std::optional<int32_t>(0));
        QCOMPARE(m_device->value("ChSendMixBus1Level", 0), std::optional<int32_t>(127));
        QCOMPARE(m_device->value("HaInputGain", 2), std::optional<int32_t>(1));
        QCOMPARE(m_device->value("MorphCompAttack", 35).has_value(), true);
        QCOMPARE(m_device->array("MorphSystemName", 3).left(8), QByteArray("01 Basic"));
        QCOMPARE(m_device->array("MorphSystemId", 0), QByteArray("0001"));
        // not part of the dump, and out of range
        QCOMPARE(m_device->value("DawSendFxBusLevel", 0), std::nullopt);
        QCOMPARE(m_device->value("ChSendMixBus1Onf", 6), std::nullopt);
    }

    void connect_usesConfiguredPort()
    {
        makeDevice("hw:3,0,1");
        m_fake->detectResult.clear();
        connectReady();
        QCOMPARE(m_device->port(), QString("hw:3,0,1"));
    }

    void connect_retriesWhenMissingOrBusy()
    {
        makeDevice();
        QSignalSpy errors(m_device.get(), &Device::lastErrorChanged);
        m_fake->detectResult.clear();
        m_device->start();
        QCOMPARE(m_device->state(), Device::State::Disconnected);
        QCOMPARE(m_device->lastError(), QString("No UR44 found"));
        QCOMPARE(errors.size(), 1);

        m_fake->detectResult = "hw:9,0,1";
        m_fake->openFails = true;
        QTRY_VERIFY_WITH_TIMEOUT(m_fake->opens >= 1, Device::RETRY_MS * 2);
        QVERIFY(m_device->lastError().contains("in use"));
        // the state stayed Disconnected, but the new reason is announced
        QCOMPARE(errors.size(), 2);

        m_fake->openFails = false;
        QTRY_COMPARE_WITH_TIMEOUT(m_device->state(), Device::State::Ready, Device::RETRY_MS * 2);
    }

    void connect_timesOutAndRetries()
    {
        makeDevice();
        m_fake->answers = false;
        m_device->start();
        QCOMPARE(m_device->state(), Device::State::Connecting);
        QTRY_COMPARE_WITH_TIMEOUT(m_device->state(), Device::State::Disconnected, Device::REPLY_TIMEOUT_MS * 2);
        QVERIFY(!m_fake->isOpen());

        m_fake->answers = true;
        QTRY_COMPARE_WITH_TIMEOUT(m_device->state(), Device::State::Ready, Device::RETRY_MS * 2);
    }

    void unplug_reconnects()
    {
        makeDevice();
        connectReady();
        m_fake->unplug();
        QCOMPARE(m_device->state(), Device::State::Disconnected);
        QVERIFY(m_device->lastError().contains("No such device"));
        QTRY_COMPARE_WITH_TIMEOUT(m_device->state(), Device::State::Ready, Device::RETRY_MS * 2);
    }

    void unsolicitedChange_isReported()
    {
        makeDevice();
        connectReady();
        QSignalSpy changes(m_device.get(), &Device::parameterChanged);
        uint16_t p48 = P("HaP48VOnf").id;

        m_fake->deviceSends(Protocol::setParameter(p48, 1, 1));
        QCOMPARE(changes.size(), 1);
        QCOMPARE(changes.at(0).at(0).value<uint16_t>(), p48);
        QCOMPARE(changes.at(0).at(1).value<uint16_t>(), uint16_t(1));
        QCOMPARE(changes.at(0).at(2).value<int32_t>(), 1);
        QCOMPARE(m_device->value(p48, 1), std::optional<int32_t>(1));

        // the same value again isn't a change; an out-of-range index is ignored
        m_fake->deviceSends(Protocol::setParameter(p48, 1, 1));
        m_fake->deviceSends(Protocol::setParameter(p48, 3, 1));
        QCOMPARE(changes.size(), 1);
    }

    void setParameter_sendsAndConfirms()
    {
        makeDevice();
        connectReady();
        QSignalSpy changes(m_device.get(), &Device::parameterChanged);
        const Parameter& pan = P("ChSendMixBus2Pan");

        QVERIFY(m_device->setParameter(pan.id, 3, -7));
        QCOMPARE(m_fake->written.last(), Protocol::setParameter(pan.id, 3, uint32_t(-7)));
        QCOMPARE(m_device->value(pan.id, 3), std::optional<int32_t>(-7));
        QCOMPARE(changes.size(), 1);

        // the read-back comes shortly after and agrees, so nothing else changes
        QTRY_COMPARE(m_fake->count(Protocol::getParameter(pan.id, 3)), 1);
        QTest::qWait(50);
        QCOMPARE(changes.size(), 1);

        // out-of-range values are clamped, unknown parameters and indices refused
        QVERIFY(m_device->setParameter(pan.id, 3, 99));
        QCOMPARE(m_device->value(pan.id, 3), std::optional<int32_t>(16));
        QVERIFY(!m_device->setParameter(pan.id, 6, 0));
        QVERIFY(!m_device->setParameter("NoSuchParameter", 0, 0));
    }

    void setParameter_correctedByDevice()
    {
        makeDevice();
        connectReady();
        const Parameter& onf = P("ChSendMixBus1Onf");
        m_fake->forced.insert(FakeUR44::key(onf.id, 0), 0); // input 1 stays off in MIX 1
        QSignalSpy changes(m_device.get(), &Device::parameterChanged);

        QVERIFY(m_device->setParameter(onf.id, 0, 1));
        QCOMPARE(m_device->value(onf.id, 0), std::optional<int32_t>(1));
        QTRY_COMPARE(m_device->value(onf.id, 0), std::optional<int32_t>(0));
        QCOMPARE(changes.size(), 2);
    }

    void setParameter_refusedWhenNotReady()
    {
        makeDevice();
        QVERIFY(!m_device->setParameter("MasterMixBus1Level", 0, 103));
        QVERIFY(m_fake->written.isEmpty());
    }

    void meters_followReadyState()
    {
        makeDevice();
        m_device->setMetersEnabled(true);
        QVERIFY(m_fake->written.isEmpty()); // not connected yet
        connectReady();
        QTRY_VERIFY(m_fake->meters);

        QSignalSpy frames(m_device.get(), &Device::meterFrame);
        QByteArray frame = QByteArray::fromHex("F043103E140202") + QByteArray(44 * 3, 0) + QByteArray::fromHex("F7");
        m_fake->deviceSends(frame);
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames.at(0).at(0).value<QList<uint32_t>>().size(), 44);

        m_device->setMetersEnabled(false);
        QTRY_VERIFY(!m_fake->meters);
        m_fake->deviceSends(frame);
        QCOMPARE(frames.size(), 1);

        // stop() turns the stream off even if meters were on
        m_device->setMetersEnabled(true);
        QTRY_VERIFY(m_fake->meters);
        m_device->stop();
        QCOMPARE(m_fake->written.last(), Protocol::meters(false));
        QVERIFY(!m_fake->isOpen());
    }

    // --- effect relations (Effects.h) ---------------------------------------------------------------------------

    Effects::MorphPoint morphPoint(uint16_t strip, int point)
    {
        Effects::MorphPoint p{};
        for (size_t i = 0; i < p.size(); ++i)
        {
            p[i] = *m_device->value(Effects::MORPH_TABLE_PARAMETERS[i], uint16_t(strip * Effects::MORPH_POINTS + point));
        }
        return p;
    }

    Effects::MorphPoint morphedValues(uint16_t strip)
    {
        Effects::MorphPoint p{};
        for (size_t i = 0; i < p.size(); ++i)
        {
            p[i] = *m_device->value(Effects::MORPHED_PARAMETERS[i], strip);
        }
        return p;
    }

    void morph_landsOnTablePoints()
    {
        makeDevice();
        connectReady();
        const uint16_t strip = 2;
        for (auto [value, point] : {std::pair{0, 0}, {15, 1}, {60, 4}, {120, 8}})
        {
            QVERIFY(Effects::setMorph(*m_device, strip, value));
            QCOMPARE(m_device->value("FxMorphValue", strip), std::optional<int32_t>(value));
            QCOMPARE(morphedValues(strip), morphPoint(strip, point));
        }
        // the values went to the device as well
        QTRY_COMPARE(m_fake->registers.value(FakeUR44::key(P("FxCompAttack").id, strip)),
                     uint32_t(morphPoint(strip, 8)[0]));
    }

    void morph_interpolatesBetweenPoints()
    {
        makeDevice();
        connectReady();
        // 22 / 15 = point 1 + 0.4667
        QVERIFY(Effects::setMorph(*m_device, 0, 22));
        Effects::MorphPoint a = morphPoint(0, 1), b = morphPoint(0, 2), got = morphedValues(0);
        for (size_t i = 0; i < got.size(); ++i)
        {
            int32_t expected = int32_t((b[i] - a[i]) * (22 / 15.0 - 1) + a[i]);
            QCOMPARE(got[i], expected);
        }
        // the factory table's points differ, so this really is in between
        QVERIFY(a != b);
    }

    void reverbType_setsTypeDefaults()
    {
        makeDevice();
        connectReady();
        QVERIFY(Effects::setReverbType(*m_device, 2)); // Plate
        QCOMPARE(m_device->value("FxRevType"), std::optional<int32_t>(2));
        QCOMPARE(m_device->value("FxRevTime"), std::optional<int32_t>(21));
        QCOMPARE(m_device->value("FxRevRoomSize"), std::optional<int32_t>(18));
        QCOMPARE(m_device->value("FxRevDecay"), std::optional<int32_t>(5));

        // choosing the current type again keeps the user's settings
        m_device->setParameter("FxRevTime", 0, 40);
        QVERIFY(Effects::setReverbType(*m_device, 2));
        QCOMPARE(m_device->value("FxRevTime"), std::optional<int32_t>(40));
        QVERIFY(!Effects::setReverbType(*m_device, 3));
    }

    void guitarAmpType_resetsTheNewModel()
    {
        makeDevice();
        connectReady();
        QVERIFY(Effects::setGuitarAmpType(*m_device, 0)); // already Clean in the captured state: no change
        m_device->setParameter("GtrFxCleanBass", 0, 99);
        m_device->setParameter("GtrFxDriveGain", 0, 5);

        QVERIFY(Effects::setGuitarAmpType(*m_device, 3)); // Drive
        QCOMPARE(m_device->value("GtrFxType"), std::optional<int32_t>(3));
        QCOMPARE(m_device->value("GtrFxDriveGain"), std::optional<int32_t>(P("GtrFxDriveGain").defaultValue));
        // the other models keep their settings
        QCOMPARE(m_device->value("GtrFxCleanBass"), std::optional<int32_t>(99));
    }

    // --- Sweet Spot Data -------------------------------------------------------------------------------------------

    /// A synthetic .ssd file: point p, field i = base + p * 3 + i,
    /// kept inside each field's range
    QString makeSsd(QTemporaryDir& dir, const QString& name, const QByteArray& id, int base)
    {
        // "SSMD", zeros, version 1 at 8, zeros, the id at 16
        QByteArray d("SSMD", 4);
        d.append(QByteArray::fromHex("000000000100000000000000"));
        d.append(id.leftJustified(16, '\0'));
        for (int p = 0; p < 9; ++p)
        {
            for (int i = 0; i < 20; ++i)
            {
                int v = 0;
                if (i < 17)
                {
                    const Parameter& param = P(Effects::MORPH_TABLE_PARAMETERS[i]);
                    v = param.clamp(param.min + (base + p * 3 + i) % (param.max - param.min + 1));
                }
                d.append(char(v >> 8));
                d.append(char(v & 0xFF));
            }
        }
        d = d.leftJustified(512, '\0');
        QString path = dir.filePath(name + ".ssd");
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(d) != d.size())
        {
            qFatal("can't write %s", qPrintable(path));
        }
        return path;
    }

    void sweetSpot_readsAndScans()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("Yamaha Preset");
        QTemporaryDir* d = &dir;
        makeSsd(*d, "Yamaha Preset/02 Second", "0002", 5);
        makeSsd(*d, "Yamaha Preset/01 First", "0001", 1);
        QFile junk(dir.filePath("junk.ssd"));
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write("not a preset");
        junk.close();

        auto presets = SweetSpotPreset::scan(dir.path());
        QCOMPARE(presets.size(), 2); // the junk file is skipped
        QCOMPARE(presets[0].name, QString("01 First"));
        QCOMPARE(presets[0].id, QByteArray("0001"));
        QCOMPARE(presets[0].group, QString("Yamaha Preset"));
        QCOMPARE(presets[1].name, QString("02 Second"));
        QString error;
        QVERIFY(!SweetSpotPreset::read(dir.filePath("junk.ssd"), &error));
        QVERIFY(!error.isEmpty());
    }

    void sweetSpot_builtIn()
    {
        const auto& presets = SweetSpotPreset::builtIn();
        QCOMPARE(presets.size(), 34);
        for (int i = 0; i < presets.size(); ++i)
        {
            QCOMPARE(presets[i].id, QByteArray::number(i + 1).rightJustified(4, '0'));
            QVERIFY(presets[i].isBuiltIn());
            QCOMPARE(presets[i].group, QString(i < 6 ? "Yamaha Preset" : "Engineers' Preset"));
            for (const auto& point : presets[i].points)
            {
                for (int f = 0; f < 17; ++f)
                {
                    QVERIFY(P(Effects::MORPH_TABLE_PARAMETERS[f]).isValid(point[f]));
                }
            }
        }
        QCOMPARE(presets[0].name, QString("01 Basic"));
        QVERIFY(SweetSpotPreset::builtInNotice().contains("Yamaha"));

        // "01 Basic" is the factory table: the one the captured device has in strip 1
        auto dump = BulkDump::decode(readData("dump_all.bin"));
        QVERIFY(dump);
        for (int f = 0; f < 17; ++f)
        {
            const QList<int32_t>& table = dump->values[P(Effects::MORPH_TABLE_PARAMETERS[f]).id];
            for (int p = 0; p < Effects::MORPH_POINTS; ++p)
            {
                QCOMPARE(presets[0].points[p][f], table[p]);
            }
        }
    }

    void sweetSpot_availableSkipsCopies()
    {
        QTemporaryDir dir;
        makeSsd(dir, "07 Mine", "0035", 4);
        // a copy of a built-in preset, as the vendor files would be
        const SweetSpotPreset& basic = SweetSpotPreset::builtIn().first();
        QByteArray d("SSMD", 4);
        d.append(QByteArray::fromHex("000000000100000000000000"));
        d.append(basic.id.leftJustified(16, '\0'));
        for (const auto& point : basic.points)
        {
            for (int i = 0; i < 20; ++i)
            {
                int v = i < 17 ? point[i] : 0;
                d.append(char(v >> 8)).append(char(v & 0xFF));
            }
        }
        QFile f(dir.filePath(basic.name + ".ssd"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(d.leftJustified(512, '\0'));
        f.close();

        auto presets = SweetSpotPreset::available(dir.path());
        QCOMPARE(presets.size(), 35);
        QCOMPARE(presets.last().name, QString("07 Mine"));
        QVERIFY(!presets.last().isBuiltIn());
        QCOMPARE(SweetSpotPreset::available(QString()).size(), 34);
    }

    void sweetSpot_loadsIntoStrip()
    {
        makeDevice();
        connectReady();
        QTemporaryDir dir;
        auto preset = SweetSpotPreset::read(makeSsd(dir, "07 Test", "0007", 2));
        QVERIFY(preset);
        const uint16_t strip = 3;
        m_device->setParameter("FxMorphValue", strip, 30); // point 2 exactly
        QSignalSpy arrays(m_device.get(), &Device::arrayChanged);

        QVERIFY(Effects::loadSweetSpot(*m_device, strip, *preset));
        QTRY_COMPARE(m_fake->morphTableWrites, 1);
        // the cache has the new table right away, and the strip follows it at its MORPH position
        QCOMPARE(m_device->array("MorphSystemName", strip).left(7), QByteArray("07 Test"));
        QCOMPARE(m_device->array("MorphSystemId", strip), QByteArray("0007"));
        QCOMPARE(arrays.size(), 2);
        QCOMPARE(morphPoint(strip, 0), preset->points[0]);
        QCOMPARE(morphedValues(strip), preset->points[2]);
        // the other strips' tables are untouched
        QCOMPARE(m_device->array("MorphSystemName", 0).left(8), QByteArray("01 Basic"));

        // the device has it too: after the resync, nothing differs
        QTRY_COMPARE(m_fake->dumpRequests, 2);
        QCOMPARE(m_fake->arrays.value(FakeUR44::key(0x52, strip)).left(7), QByteArray("07 Test"));
        QCOMPARE(morphPoint(strip, 8), preset->points[8]);
    }

    void fakeDump_reflectsRegisters()
    {
        // sanity check for the fake itself: an unchanged register set reproduces the captured payload
        FakeUR44 fake;
        QCOMPARE(fake.currentDump(), readData("dump_all.bin"));
    }

    void fxAssign_resyncsInsertParameters()
    {
        makeDevice();
        connectReady();
        QSignalSpy changes(m_device.get(), &Device::parameterChanged);
        const uint16_t post = P("ChFxInsPostPatch").id;

        QVERIFY(m_device->assignFx(Protocol::FxType::ChannelStrip, 0, Protocol::InsertPoint::Post));
        QCOMPARE(m_fake->written.last(), Protocol::fxAssign(Protocol::FxType::ChannelStrip, 0, Protocol::InsertPoint::Post));
        // the device won't answer a read of the insert parameter now, so the state comes from a dump
        QTRY_COMPARE(m_device->value(post, 0), std::optional<int32_t>(1));
        QCOMPARE(m_fake->dumpRequests, 2); // the initial load, and the resync
        QCOMPARE(changes.size(), 1);
        QCOMPARE(changes.at(0).at(0).value<uint16_t>(), post);

        QVERIFY(m_device->unassignFx(Protocol::FxType::ChannelStrip, 0));
        QTRY_COMPARE(m_device->value(post, 0), std::optional<int32_t>(0));
    }

    void resync_keepsNewerLocalChanges()
    {
        makeDevice();
        connectReady();
        const uint16_t level = P("MasterMixBus1Level").id;
        QCOMPARE(m_device->value(level, 0), std::optional<int32_t>(103));

        m_fake->holdDumps = true;
        m_device->resync();
        QTRY_COMPARE(m_fake->dumpRequests, 2); // requested; the reply (with 103) is in flight
        m_device->setParameter(level, 0, 80);
        m_fake->releaseDumps();
        // the older dump doesn't revert the newer change, and the read-back agrees
        QCOMPARE(m_device->value(level, 0), std::optional<int32_t>(80));
        QTest::qWait(Device::CONFIRM_DELAY_MS * 2);
        QCOMPARE(m_device->value(level, 0), std::optional<int32_t>(80));
    }
};

QTEST_GUILESS_MAIN(DeviceTest)
#include "tst_Device.moc"
