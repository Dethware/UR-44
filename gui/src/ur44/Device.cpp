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

void Device::tryConnect()
{
    if (!m_running)
    {
        return;
    }
    m_currentPort = m_port.isEmpty() ? m_transport->detect() : m_port;
    if (m_currentPort.isEmpty())
    {
        m_lastError = tr("No UR44 found");
        m_retryTimer.start();
        return;
    }
    QString error;
    if (!m_transport->open(m_currentPort, &error))
    {
        m_lastError = error;
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
    m_lastError = error;
    m_replyTimer.stop();
    m_confirmTimer.stop();
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
            loadDump(dump->payload);
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

void Device::loadDump(const QByteArray& payload)
{
    QString error;
    auto dump = BulkDump::decode(payload, &error);
    if (!dump)
    {
        disconnectWithError(tr("Couldn't decode the device state: %1").arg(error));
        return;
    }
    m_replyTimer.stop();
    m_values.clear();
    const Parameters& params = Parameters::instance();
    for (auto it = dump->values.constBegin(); it != dump->values.constEnd(); ++it)
    {
        const Parameter* p = params.byId(it.key());
        if (!p)
        {
            continue;
        }
        for (int i = 0; i < it.value().size() && i < p->instances; ++i)
        {
            storeValue(*p, uint16_t(i), it.value().at(i), false);
        }
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
    // The device updates the insert parameters itself without telling us; ask for them
    for (const char* name : {"ChFxInsPrePatch", "ChFxInsPostPatch", "ChGtrFxInsPrePatch", "ChGtrFxInsPostPatch"})
    {
        m_unconfirmed.insert(key(Parameters::instance()[name].id, channel));
    }
    m_confirmTimer.start();
    return true;
}

bool Device::unassignFx(Protocol::FxType type, uint16_t channel)
{
    if (!isReady())
    {
        return false;
    }
    send(Protocol::fxUnassign(type, channel));
    for (const char* name : {"ChFxInsPrePatch", "ChFxInsPostPatch", "ChGtrFxInsPrePatch", "ChGtrFxInsPostPatch"})
    {
        m_unconfirmed.insert(key(Parameters::instance()[name].id, channel));
    }
    m_confirmTimer.start();
    return true;
}

}
