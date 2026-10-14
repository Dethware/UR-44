#include <QtTest>

#include "BulkDump.h"
#include "Device.h"
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
            emit received(Protocol::bulkDump(Protocol::DumpType::All, readData("dump_all.bin")));
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
            emit received(Protocol::setParameter(id, index, v));
        }
    }

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

    void fxAssign_readsInsertParameters()
    {
        makeDevice();
        connectReady();
        QVERIFY(m_device->assignFx(Protocol::FxType::ChannelStrip, 0, Protocol::InsertPoint::Post));
        QCOMPARE(m_fake->written.last(), Protocol::fxAssign(Protocol::FxType::ChannelStrip, 0, Protocol::InsertPoint::Post));
        QTRY_COMPARE(m_fake->count(Protocol::getParameter(P("ChFxInsPostPatch").id, 0)), 1);
    }
};

QTEST_GUILESS_MAIN(DeviceTest)
#include "tst_Device.moc"
