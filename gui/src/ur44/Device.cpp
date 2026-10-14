#include "Device.h"

#include <QDebug>

#include "BulkDump.h"
#include "Parameters.h"
#include "Transport.h"

namespace UR44
{

Device::Device(Transport* transport, const QString& port, QObject* parent)
    : QObject(parent), m_transport(transport), m_port(port)
{
    m_transport->setParent(this);
    connect(m_transport, &Transport::received, this, &Device::onReceived);
    connect(m_transport, &Transport::failed, this, [this](const QString& reason) { disconnectWithError(reason); });

    m_retryTimer.setSingleShot(true);
    m_retryTimer.setInterval(RETRY_MS);
    connect(&m_retryTimer, &QTimer::timeout, this, &Device::tryConnect);

    m_replyTimer.setSingleShot(true);
    m_replyTimer.setInterval(REPLY_TIMEOUT_MS);
    connect(&m_replyTimer, &QTimer::timeout, this, [this]() {
        disconnectWithError(m_state == State::Connecting ? tr("The device doesn't answer")
                                                         : tr("The device didn't send its state"));
    });

    m_confirmTimer.setSingleShot(true);
    m_confirmTimer.setInterval(CONFIRM_DELAY_MS);
    connect(&m_confirmTimer, &QTimer::timeout, this, &Device::sendConfirmations);

    m_resyncTimer.setSingleShot(true);
    m_resyncTimer.setInterval(RESYNC_DELAY_MS);
    connect(&m_resyncTimer, &QTimer::timeout, this, [this]() {
        if (isReady())
        {
            m_resyncRequestedAt = ++m_sequence;
            send(Protocol::dumpRequest(Protocol::DumpType::All));
        }
    });
}

Device::~Device()
{
    // Close the port (stopping the meter stream), but don't notify anyone: they may be half destroyed already
    QSignalBlocker blocker(this);
    stop();
}

void Device::start()
{
    m_running = true;
    if (m_state == State::Disconnected)
    {
        tryConnect();
    }
}

void Device::stop()
{
    m_running = false;
    m_retryTimer.stop();
    m_replyTimer.stop();
    m_confirmTimer.stop();
    m_resyncTimer.stop();
    if (m_transport->isOpen())
    {
        // The port is exclusive: whoever opens it next would get our meter stream otherwise
        send(Protocol::meters(false));
        m_transport->close();
    }
    setState(State::Disconnected);
}

void Device::setPort(const QString& port)
{
    if (port == m_port)
    {
        return;
    }
    m_port = port;
    if (m_running)
    {
        stop();
        start();
    }
}

void Device::setState(State state)
{
    if (state == m_state)
    {
        return;
    }
    m_state = state;
    qDebug() << "Device state:" << state;
    emit stateChanged(state);
}

void Device::setLastError(const QString& error)
{
    if (error != m_lastError)
    {
        m_lastError = error;
        emit lastErrorChanged(error);
    }
}

void Device::tryConnect()
{
    if (!m_running)
    {
        return;
    }
    m_currentPort = m_port.isEmpty() ? m_transport->detect() : m_port;
    if (m_currentPort.isEmpty())
    {
        setLastError(tr("No UR44 found"));
        m_retryTimer.start();
        return;
    }
    QString error;
    if (!m_transport->open(m_currentPort, &error))
    {
        setLastError(error);
        m_retryTimer.start();
        return;
    }
    m_splitter.clear();
    m_values.clear();
    m_unconfirmed.clear();
    m_version.clear();
    setState(State::Connecting);
    // A meter stream left running by a previous owner of the port would flood us
    send(Protocol::meters(false));
    send(Protocol::getVersion());
    m_replyTimer.start();
}

void Device::disconnectWithError(const QString& error)
{
    qWarning() << "Device disconnected:" << error;
    setLastError(error);
    m_replyTimer.stop();
    m_confirmTimer.stop();
    m_resyncTimer.stop();
    m_transport->close();
    setState(State::Disconnected);
    if (m_running)
    {
        m_retryTimer.start();
    }
}

void Device::send(const QByteArray& message)
{
    if (m_transport->isOpen())
    {
        m_transport->write(message);
    }
    else
    {
        qWarning() << "Transport not open on send()";
    }
}

void Device::onReceived(const QByteArray& bytes)
{
    for (const QByteArray& message : m_splitter.feed(bytes))
    {
        handle(Protocol::parse(message));
    }
}

void Device::handle(const Protocol::Message& message)
{
    if (auto* version = std::get_if<Protocol::Version>(&message))
    {
        QStringList parts;
        for (const auto& [name, ver] : version->components)
        {
            parts.append(name + ' ' + ver);
        }
        m_version = parts.join(", ");
        if (m_state == State::Connecting)
        {
            setState(State::Loading);
            send(Protocol::dumpRequest(Protocol::DumpType::All));
            m_replyTimer.start();
        }
    }
    else if (auto* dump = std::get_if<Protocol::Dump>(&message))
    {
        if (dump->type == uint32_t(Protocol::DumpType::All))
        {
            if (m_state == State::Loading)
            {
                loadDump(dump->payload, false);
            }
            else if (m_state == State::Ready)
            {
                loadDump(dump->payload, true);
            }
        }
    }
    else if (auto* pv = std::get_if<Protocol::ParameterValue>(&message))
    {
        if (m_state != State::Ready)
        {
            return;
        }
        const Parameter* p = Parameters::instance().byId(pv->id);
        if (!p || pv->index >= p->instances)
        {
            return; // out-of-range reads return neighbouring memory; never cache those
        }
        m_unconfirmed.remove(key(pv->id, pv->index));
        storeValue(*p, pv->index, p->fromWire(pv->value), true);
    }
    else if (auto* frame = std::get_if<Protocol::MeterFrame>(&message))
    {
        if (m_state == State::Ready && m_metersWanted)
        {
            emit meterFrame(frame->values);
        }
    }
}

void Device::loadDump(const QByteArray& payload, bool merge)
{
    QString error;
    auto dump = BulkDump::decode(payload, &error);
    if (!dump)
    {
        if (merge)
        {
            qWarning() << "Ignoring an undecodable state dump:" << error;
            return;
        }
        disconnectWithError(tr("Couldn't decode the device state: %1").arg(error));
        return;
    }
    if (!merge)
    {
        m_replyTimer.stop();
        m_values.clear();
        m_arrays.clear();
        m_setAt.clear();
    }
    m_dumpFormat = dump->groups;
    for (auto it = dump->arrays.constBegin(); it != dump->arrays.constEnd(); ++it)
    {
        for (int i = 0; i < it.value().size(); ++i)
        {
            uint32_t k = key(it.key(), uint16_t(i));
            bool changed = m_arrays.value(k) != it.value().at(i);
            m_arrays.insert(k, it.value().at(i));
            if (merge && changed)
            {
                emit arrayChanged(it.key(), uint16_t(i));
            }
        }
    }
    const Parameters& params = Parameters::instance();
    int changed = 0;
    for (auto it = dump->values.constBegin(); it != dump->values.constEnd(); ++it)
    {
        const Parameter* p = params.byId(it.key());
        if (!p)
        {
            continue;
        }
        for (int i = 0; i < it.value().size() && i < p->instances; ++i)
        {
            uint32_t k = key(p->id, uint16_t(i));
            if (merge && m_setAt.value(k) > m_resyncRequestedAt)
            {
                continue; // changed by us after the dump was requested; the dump has the older value
            }
            if (merge && m_values.value(k, it.value().at(i) + 1) != it.value().at(i))
            {
                ++changed;
            }
            storeValue(*p, uint16_t(i), it.value().at(i), merge);
        }
    }
    if (merge)
    {
        qDebug() << "Resynced" << dump->name << ":" << changed << "values changed";
        return;
    }
    qDebug() << "Loaded" << dump->name << "with" << m_values.size() << "values";
    setState(State::Ready);
    emit stateLoaded();
    if (m_metersWanted)
    {
        send(Protocol::meters(true));
    }
}

void Device::storeValue(const Parameter& p, uint16_t index, int32_t value, bool notify)
{
    uint32_t k = key(p.id, index);
    auto it = m_values.find(k);
    if (it != m_values.end() && *it == value)
    {
        return;
    }
    m_values.insert(k, value);
    if (notify)
    {
        emit parameterChanged(p.id, index, value);
    }
}

std::optional<int32_t> Device::value(uint16_t id, uint16_t index) const
{
    auto it = m_values.constFind(key(id, index));
    if (it == m_values.constEnd())
    {
        return std::nullopt;
    }
    return *it;
}

QByteArray Device::array(const QString& name, uint16_t index) const
{
    const Parameter* p = Parameters::instance().byName(name);
    return p ? m_arrays.value(key(p->id, index)) : QByteArray();
}

std::optional<int32_t> Device::value(const QString& name, uint16_t index) const
{
    const Parameter* p = Parameters::instance().byName(name);
    return p ? value(p->id, index) : std::nullopt;
}

bool Device::setParameter(uint16_t id, uint16_t index, int32_t v)
{
    const Parameter* p = Parameters::instance().byId(id);
    if (!isReady() || !p || index >= p->instances)
    {
        return false;
    }
    v = p->clamp(v);
    send(Protocol::setParameter(id, index, uint32_t(v)));
    m_setAt.insert(key(id, index), ++m_sequence);
    storeValue(*p, index, v, true);
    m_unconfirmed.insert(key(id, index));
    m_confirmTimer.start();
    return true;
}

bool Device::setParameter(const QString& name, uint16_t index, int32_t v)
{
    const Parameter* p = Parameters::instance().byName(name);
    return p && setParameter(p->id, index, v);
}

void Device::sendConfirmations()
{
    for (uint32_t k : std::as_const(m_unconfirmed))
    {
        send(Protocol::getParameter(uint16_t(k >> 16), uint16_t(k & 0xFFFF)));
    }
    m_unconfirmed.clear();
}

void Device::setMetersEnabled(bool enabled)
{
    if (enabled == m_metersWanted)
    {
        return;
    }
    m_metersWanted = enabled;
    if (isReady())
    {
        send(Protocol::meters(enabled));
    }
}

bool Device::assignFx(Protocol::FxType type, uint16_t channel, Protocol::InsertPoint point)
{
    if (!isReady())
    {
        return false;
    }
    send(Protocol::fxAssign(type, channel, point));
    // The device updates the insert parameters itself without telling us, and doesn't answer reads of them while
    // an effect is assigned: only the state dump has them
    resync();
    return true;
}

bool Device::unassignFx(Protocol::FxType type, uint16_t channel)
{
    if (!isReady())
    {
        return false;
    }
    send(Protocol::fxUnassign(type, channel));
    resync();
    return true;
}

bool Device::writeMorphTable(uint16_t strip, const QByteArray& id, const QByteArray& name,
                             const QHash<uint16_t, QList<int32_t>>& points)
{
    constexpr uint16_t SYSTEM_ID = 0x51, SYSTEM_NAME = 0x52, FIRST_MORPH = 0x53;
    constexpr int POINTS = 9;
    if (!isReady())
    {
        return false;
    }
    BulkDump table;
    table.name = "MorphingTable";
    for (const BulkDump::Group& g : std::as_const(m_dumpFormat))
    {
        if (!g.items.isEmpty() && (g.items.first().id == SYSTEM_ID || g.items.first().id == FIRST_MORPH))
        {
            table.groups.append(g);
        }
    }
    if (table.groups.size() != 2)
    {
        return false;
    }
    // the current table, with the strip replaced
    for (const BulkDump::Group& g : std::as_const(table.groups))
    {
        for (const BulkDump::Item& item : g.items)
        {
            for (int inst = 0; inst < g.instances; ++inst)
            {
                if (item.isArray())
                {
                    QByteArray a = m_arrays.value(key(item.id, uint16_t(inst)));
                    if (inst == strip && item.id == SYSTEM_ID)
                    {
                        a = id;
                    }
                    if (inst == strip && item.id == SYSTEM_NAME)
                    {
                        a = name;
                    }
                    table.arrays[item.id].append(a.leftJustified(item.count * item.size, '\0', true));
                }
                else
                {
                    int32_t v = value(item.id, uint16_t(inst)).value_or(0);
                    if (inst / POINTS == strip && points.value(item.id).size() == POINTS)
                    {
                        v = points.value(item.id).at(inst % POINTS);
                    }
                    table.values[item.id].append(v);
                }
            }
        }
    }
    send(Protocol::bulkDump(Protocol::DumpType::MorphTable, table.encode()));

    // the cache follows right away (setMorph() reads it); the resync confirms
    const Parameters& params = Parameters::instance();
    for (auto it = table.values.constBegin(); it != table.values.constEnd(); ++it)
    {
        if (const Parameter* p = params.byId(it.key()))
        {
            for (int inst = strip * POINTS; inst < (strip + 1) * POINTS; ++inst)
            {
                storeValue(*p, uint16_t(inst), it.value().at(inst), true);
                m_setAt.insert(key(p->id, uint16_t(inst)), ++m_sequence);
            }
        }
    }
    for (uint16_t arrayId : {SYSTEM_ID, SYSTEM_NAME})
    {
        m_arrays.insert(key(arrayId, strip), table.arrays.value(arrayId).value(strip));
        emit arrayChanged(arrayId, strip);
    }
    resync();
    return true;
}

void Device::resync()
{
    if (isReady())
    {
        m_resyncTimer.start();
    }
}

}
