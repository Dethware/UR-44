#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

/*
 * A bidirectional MIDI byte stream to the UR44's control port. Implementations: AlsaTransport (the real device),
 * and fakes in the tests.
 */
namespace UR44
{

class Transport : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~Transport() override = default;

    /// Finds the device; returns a port name for open(), or an empty string if there's none
    virtual QString detect() = 0;
    virtual bool open(const QString& port, QString* error) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual bool write(const QByteArray& bytes) = 0;
    /// Whether the open device has the FX Bus: two more playback channels that feed REV-X, with the UR44's
    /// [CC MODE] switch off (docs/PROTOCOL.md, "Class Compliant mode off")
    virtual bool hasFxBus() const { return false; }

signals:
    void received(const QByteArray& bytes);
    /// The port stopped working (e.g. the device was unplugged); it has been closed
    void failed(const QString& reason);
};

}
