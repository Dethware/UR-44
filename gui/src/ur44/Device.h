#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

#include <optional>

#include "BulkDump.h"
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
    static constexpr int RESYNC_DELAY_MS = 200;

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
    /// Byte-array parameters (MorphSystemId, MorphSystemName) as loaded from the state dump; empty if unknown.
    /// A per-parameter read only returns their first byte, so these aren't updated by changes.
    QByteArray array(const QString& name, uint16_t index = 0) const;
    std::optional<int32_t> value(const QString& name, uint16_t index = 0) const;

    /// Sends a parameter change (clamped to the parameter's range). Returns false if not Ready or unknown.
    bool setParameter(uint16_t id, uint16_t index, int32_t value);
    bool setParameter(const QString& name, uint16_t index, int32_t value);

    /// Requests meter frames while Ready; they arrive as meterFrame() about 20 times per second
    void setMetersEnabled(bool enabled);

    /// Inserts / removes a channel strip or guitar amp. The device picks the strip (or refuses); the result arrives
    /// as changes of the insert parameters, through a resync.
    bool assignFx(Protocol::FxType type, uint16_t channel, Protocol::InsertPoint point);
    bool unassignFx(Protocol::FxType type, uint16_t channel);

    /// Writes a Sweet Spot morph table to one channel strip (0-3): its id ("0002"), name ("02 Color") and the 9
    /// points of each Morph* parameter (by id, 0x53..0x63). The other strips' tables are sent unchanged. Uses the
    /// format the device described in its last state dump; false if that's unknown or the device isn't ready.
    bool writeMorphTable(uint16_t strip, const QByteArray& id, const QByteArray& name,
                         const QHash<uint16_t, QList<int32_t>>& points);

    /// Re-reads the whole state (a bulk dump) shortly, and announces what changed through parameterChanged.
    /// For changes the device makes without telling us, and values it won't report on their own.
    void resync();

signals:
    void stateChanged(UR44::Device::State state);
    /// lastError() changed, e.g. a retry failed differently (the state stays Disconnected)
    void lastErrorChanged(const QString& error);
    /// The whole state was (re)loaded; every value may have changed
    void stateLoaded();
    void parameterChanged(uint16_t id, uint16_t index, int32_t value);
    /// A byte-array parameter (MorphSystemId, MorphSystemName) changed
    void arrayChanged(uint16_t id, uint16_t index);
    void meterFrame(const QList<uint32_t>& values);

private:
    void setState(State state);
    void setLastError(const QString& error);
    void tryConnect();
    void disconnectWithError(const QString& error);
    void onReceived(const QByteArray& bytes);
    void handle(const Protocol::Message& message);
    void loadDump(const QByteArray& payload, bool merge);
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
    QHash<uint32_t, QByteArray> m_arrays;
    /// The format groups of the last state dump, reused to write parts of the state
    QList<BulkDump::Group> m_dumpFormat;
    QSet<uint32_t> m_unconfirmed;
    // Changes sent after a resync's dump request aren't in that dump; don't let it revert them
    quint64 m_sequence = 0;
    quint64 m_resyncRequestedAt = 0;
    QHash<uint32_t, quint64> m_setAt;
    QTimer m_resyncTimer;
    QTimer m_retryTimer;
    QTimer m_replyTimer;
    QTimer m_confirmTimer;
};

}
