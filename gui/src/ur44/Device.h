#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

#include <optional>

#include "Protocol.h"

namespace UR44
{

class Transport;
struct Parameter;

/*
 * The connection to the UR44 and a cache of its DSP state.
 *
 * Lifecycle: Disconnected -> Connecting (version request) -> Loading (bulk dump "All") -> Ready. Any failure
 * (no device, port busy, timeout, unplug) goes back to Disconnected and retries every RETRY_MS.
 *
 * While Ready, values() mirror the device: they are loaded from the dump, updated by unsolicited parameter changes
 * (e.g. the +48V switches) and by setParameter(). setParameter() updates the cache immediately and asks the device
 * for the value shortly afterwards, so a rejected change is corrected.
 */
class Device : public QObject
{
    Q_OBJECT
public:
    enum class State
    {
        Disconnected,
        Connecting,
        Loading,
        Ready,
    };
    Q_ENUM(State)

    static constexpr int RETRY_MS = 2000;
    static constexpr int REPLY_TIMEOUT_MS = 1500;
    static constexpr int CONFIRM_DELAY_MS = 150;

    /// Takes ownership of the transport. `port` is the port to open; empty means transport->detect().
    explicit Device(Transport* transport, const QString& port = QString(), QObject* parent = nullptr);
    ~Device() override;

    /// Starts connecting (and reconnecting)
    void start();
    /// Disconnects and stops reconnecting
    void stop();
    void setPort(const QString& port);

    State state() const { return m_state; }
    bool isReady() const { return m_state == State::Ready; }
    /// The port in use (or last tried), e.g. "hw:1,0,1"
    QString port() const { return m_currentPort; }
    /// Why the last connection attempt failed
    QString lastError() const { return m_lastError; }
    /// e.g. "BOOT V2.12, MAIN V2.12"
    QString firmwareVersion() const { return m_version; }

    std::optional<int32_t> value(uint16_t id, uint16_t index = 0) const;
    std::optional<int32_t> value(const QString& name, uint16_t index = 0) const;

    /// Sends a parameter change (clamped to the parameter's range). Returns false if not Ready or unknown.
    bool setParameter(uint16_t id, uint16_t index, int32_t value);
    bool setParameter(const QString& name, uint16_t index, int32_t value);

    /// Requests meter frames while Ready; they arrive as meterFrame() about 20 times per second
    void setMetersEnabled(bool enabled);

    /// Inserts / removes a channel strip or guitar amp. The device picks the strip; the insert parameters update.
    bool assignFx(Protocol::FxType type, uint16_t channel, Protocol::InsertPoint point);
    bool unassignFx(Protocol::FxType type, uint16_t channel);

signals:
    void stateChanged(UR44::Device::State state);
    /// lastError() changed, e.g. a retry failed differently (the state stays Disconnected)
    void lastErrorChanged(const QString& error);
    /// The whole state was (re)loaded; every value may have changed
    void stateLoaded();
    void parameterChanged(uint16_t id, uint16_t index, int32_t value);
    void meterFrame(const QList<uint32_t>& values);

private:
    void setState(State state);
    void setLastError(const QString& error);
    void tryConnect();
    void disconnectWithError(const QString& error);
    void onReceived(const QByteArray& bytes);
    void handle(const Protocol::Message& message);
    void loadDump(const QByteArray& payload);
    void storeValue(const Parameter& p, uint16_t index, int32_t value, bool notify);
    void sendConfirmations();
    void send(const QByteArray& message);

    static uint32_t key(uint16_t id, uint16_t index) { return (uint32_t(id) << 16) | index; }

    Transport* m_transport;
    QString m_port;
    QString m_currentPort;
    State m_state = State::Disconnected;
    bool m_running = false;
    bool m_metersWanted = false;
    QString m_lastError;
    QString m_version;
    Protocol::SysExSplitter m_splitter;
    QHash<uint32_t, int32_t> m_values;
    QSet<uint32_t> m_unconfirmed;
    QTimer m_retryTimer;
    QTimer m_replyTimer;
    QTimer m_confirmTimer;
};

}
