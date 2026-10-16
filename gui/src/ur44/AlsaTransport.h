#pragma once

#include <QList>

#include "Transport.h"

class QSocketNotifier;
typedef struct _snd_rawmidi snd_rawmidi_t;

/*
 * Transport over an ALSA rawmidi port, e.g. "hw:1,0,1". Rawmidi ports are exclusive: while this is open, no other
 * program (such as tools/ur44.py) can use the port.
 */
namespace UR44
{

class AlsaTransport : public Transport
{
    Q_OBJECT
public:
    static constexpr int WRITE_TIMEOUT_MS = 1000;

    using Transport::Transport;
    ~AlsaTransport() override;

    /// The control port of the first ALSA card named exactly "Steinberg UR44" (not its successors), e.g. "hw:1,0,1"
    QString detect() override;
    /// Where the control port is, by the card's USB id (/proc/asound/cardN/usbid): ",0,1" (the second subdevice of
    /// MIDI device 0) in Class Compliant mode (0499:1704), ",1,0" (MIDI device 1) with [CC MODE] off (0499:150e).
    /// Empty for anything else.
    static QString controlPortSuffix(const QByteArray& usbId);
    /// The ALSA library version in use, e.g. "1.2.16"
    static QString alsaVersion();
    bool open(const QString& port, QString* error) override;
    void close() override;
    bool isOpen() const override { return m_in != nullptr; }
    bool write(const QByteArray& bytes) override;
    bool hasFxBus() const override { return m_usbId == "0499:150e"; }

private:
    void readAvailable();
    void fail(const QString& reason);

    snd_rawmidi_t* m_in = nullptr;
    snd_rawmidi_t* m_out = nullptr;
    QList<QSocketNotifier*> m_notifiers;
    /// The open card's USB id, e.g. "0499:1704"
    QByteArray m_usbId;
};

}
