#include "AlsaTransport.h"

#include <QDebug>
#include <QSocketNotifier>

#include <alsa/asoundlib.h>
#include <poll.h>

namespace UR44
{

AlsaTransport::~AlsaTransport()
{
    close();
}

QString AlsaTransport::detect()
{
    int card = -1;
    while (snd_card_next(&card) == 0 && card >= 0)
    {
        QString hw = QString("hw:%1").arg(card);
        snd_ctl_t* ctl = nullptr;
        if (snd_ctl_open(&ctl, hw.toLatin1().constData(), 0) < 0)
        {
            continue;
        }
        snd_ctl_card_info_t* info;
        snd_ctl_card_info_alloca(&info);
        bool isUR44 = snd_ctl_card_info(ctl, info) == 0 && QString(snd_ctl_card_info_get_name(info)).contains("UR44");

        // The control port is subdevice 1 of rawmidi device 0
        bool hasPort = false;
        if (isUR44)
        {
            snd_rawmidi_info_t* rinfo;
            snd_rawmidi_info_alloca(&rinfo);
            snd_rawmidi_info_set_device(rinfo, 0);
            snd_rawmidi_info_set_stream(rinfo, SND_RAWMIDI_STREAM_INPUT);
            hasPort = snd_ctl_rawmidi_info(ctl, rinfo) == 0 && snd_rawmidi_info_get_subdevices_count(rinfo) >= 2;
        }
        snd_ctl_close(ctl);
        if (hasPort)
        {
            return hw + ",0,1";
        }
    }
    return QString();
}

bool AlsaTransport::open(const QString& port, QString* error)
{
    close();
    int r = snd_rawmidi_open(&m_in, &m_out, port.toLatin1().constData(), SND_RAWMIDI_NONBLOCK);
    if (r < 0)
    {
        m_in = m_out = nullptr;
        if (error)
        {
            *error = r == -EBUSY ? QString("%1 is in use by another program").arg(port)
                                 : QString("%1: %2").arg(port, snd_strerror(r));
        }
        return false;
    }
    // Input and output share one file descriptor, so both stay non-blocking (write() waits for space itself)

    int count = snd_rawmidi_poll_descriptors_count(m_in);
    QList<pollfd> fds(count);
    snd_rawmidi_poll_descriptors(m_in, fds.data(), count);
    for (const pollfd& fd : fds)
    {
        auto* notifier = new QSocketNotifier(fd.fd, QSocketNotifier::Read, this);
        connect(notifier, &QSocketNotifier::activated, this, &AlsaTransport::readAvailable);
        m_notifiers.append(notifier);
    }
    qDebug() << "Opened" << port;
    return true;
}

void AlsaTransport::close()
{
    qDeleteAll(m_notifiers);
    m_notifiers.clear();
    if (m_in)
    {
        snd_rawmidi_close(m_in);
        m_in = nullptr;
    }
    if (m_out)
    {
        snd_rawmidi_close(m_out);
        m_out = nullptr;
    }
}

bool AlsaTransport::write(const QByteArray& bytes)
{
    if (!m_out)
    {
        return false;
    }
    const char* data = bytes.constData();
    size_t left = size_t(bytes.size());
    while (left > 0)
    {
        ssize_t r = snd_rawmidi_write(m_out, data, left);
        if (r > 0)
        {
            data += r;
            left -= size_t(r);
            continue;
        }
        if (r == -EAGAIN)
        {
            // The output buffer is full (only for large messages); wait for room, but not forever
            int count = snd_rawmidi_poll_descriptors_count(m_out);
            QList<pollfd> fds(count);
            snd_rawmidi_poll_descriptors(m_out, fds.data(), count);
            if (poll(fds.data(), nfds_t(count), WRITE_TIMEOUT_MS) > 0)
            {
                continue;
            }
            fail(QString("write timed out"));
            return false;
        }
        fail(QString("write failed: %1").arg(snd_strerror(int(r))));
        return false;
    }
    return true;
}

void AlsaTransport::readAvailable()
{
    char buffer[1024];
    QByteArray bytes;
    while (m_in)
    {
        ssize_t r = snd_rawmidi_read(m_in, buffer, sizeof(buffer));
        if (r > 0)
        {
            bytes.append(buffer, int(r));
            continue;
        }
        if (r == -EAGAIN || r == 0)
        {
            break;
        }
        // -ENODEV and friends: the device is gone
        if (!bytes.isEmpty())
        {
            emit received(bytes);
        }
        fail(QString("read failed: %1").arg(snd_strerror(int(r))));
        return;
    }
    if (!bytes.isEmpty())
    {
        emit received(bytes);
    }
}

void AlsaTransport::fail(const QString& reason)
{
    qWarning() << "MIDI port failed:" << reason;
    close();
    emit failed(reason);
}

}
