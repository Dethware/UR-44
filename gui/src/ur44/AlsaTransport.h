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

    /// The second MIDI port of the first ALSA card whose name contains "UR44"
    QString detect() override;
    bool open(const QString& port, QString* error) override;
    void close() override;
    bool isOpen() const override { return m_in != nullptr; }
    bool write(const QByteArray& bytes) override;

private:
    void readAvailable();
    void fail(const QString& reason);

    snd_rawmidi_t* m_in = nullptr;
    snd_rawmidi_t* m_out = nullptr;
    QList<QSocketNotifier*> m_notifiers;
};

}
