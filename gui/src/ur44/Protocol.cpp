#include "Protocol.h"

namespace UR44::Protocol
{

namespace
{
constexpr char SOX = char(0xF0);
constexpr char EOX = char(0xF7);
constexpr uint8_t YAMAHA = 0x43;
constexpr uint8_t GROUP = 0x3E;
constexpr uint8_t MODEL = 0x14;

enum Type : uint8_t
{
    BulkDumpType = 0x00,
    ParameterChange = 0x10,
    DumpRequest = 0x20,
    ParameterRequest = 0x30,
};

QByteArray header(Type type)
{
    QByteArray out;
    out.append(SOX);
    out.append(char(YAMAHA));
    out.append(char(type));
    out.append(char(GROUP));
    out.append(char(MODEL));
    return out;
}

QByteArray message(Type type, std::initializer_list<uint8_t> body)
{
    QByteArray out = header(type);
    for (uint8_t b : body)
    {
        out.append(char(b));
    }
    return out;
}

uint8_t byteAt(const QByteArray& data, int i)
{
    return uint8_t(data.at(i));
}
}

void appendU16(QByteArray& out, uint16_t v)
{
    out.append(char((v >> 14) & 0x03));
    out.append(char((v >> 7) & 0x7F));
    out.append(char(v & 0x7F));
}

void appendU32(QByteArray& out, uint32_t v)
{
    out.append(char((v >> 28) & 0x0F));
    out.append(char((v >> 21) & 0x7F));
    out.append(char((v >> 14) & 0x7F));
    out.append(char((v >> 7) & 0x7F));
    out.append(char(v & 0x7F));
}

uint32_t readBE7(const char* data, int count)
{
    uint32_t v = 0;
    for (int i = 0; i < count; ++i)
    {
        v = (v << 7) | (uint8_t(data[i]) & 0x7F);
    }
    return v;
}

QByteArray getParameter(uint16_t id, uint16_t index)
{
    QByteArray out = message(ParameterRequest, {0x01, 0x01});
    appendU16(out, id);
    appendU16(out, index);
    out.append(EOX);
    return out;
}

QByteArray setParameter(uint16_t id, uint16_t index, uint32_t value)
{
    QByteArray out = message(ParameterChange, {0x01, 0x01});
    appendU16(out, id);
    appendU16(out, index);
    appendU32(out, value);
    out.append(EOX);
    return out;
}

QByteArray getVersion()
{
    QByteArray out = message(ParameterRequest, {0x00, 0x70, 0x02});
    out.append(EOX);
    return out;
}

QByteArray meters(bool on)
{
    QByteArray out = message(ParameterRequest, {0x02, 0x02, 0x32, uint8_t(on ? 0x7F : 0x00)});
    out.append(EOX);
    return out;
}

QByteArray dumpRequest(DumpType type)
{
    QByteArray out = message(DumpRequest, {0x01, 0x01});
    appendU32(out, uint32_t(type));
    out.append(EOX);
    return out;
}

QByteArray fxAssign(FxType type, uint16_t channel, InsertPoint point)
{
    QByteArray out = message(ParameterChange, {0x03, 0x08, 0x01, uint8_t(type)});
    appendU16(out, channel);
    out.append(char(point));
    out.append(EOX);
    return out;
}

QByteArray fxUnassign(FxType type, uint16_t channel)
{
    QByteArray out = message(ParameterChange, {0x03, 0x08, 0x02, uint8_t(type)});
    appendU16(out, channel);
    out.append(char(0));
    out.append(EOX);
    return out;
}

QByteArray pack7(const QByteArray& data)
{
    QByteArray out;
    for (int g = 0; g < data.size(); g += 7)
    {
        uint8_t msb = 0;
        for (int k = 0; k < 7; ++k)
        {
            uint8_t b = g + k < data.size() ? uint8_t(data.at(g + k)) : 0;
            out.append(char(b & 0x7F));
            msb |= uint8_t((b & 0x80) >> (k + 1));
        }
        out.append(char(msb));
    }
    return out;
}

QByteArray unpack7(const QByteArray& data)
{
    QByteArray out;
    for (int g = 0; g + 8 <= data.size(); g += 8)
    {
        uint8_t msb = uint8_t(data.at(g + 7));
        for (int k = 0; k < 7; ++k)
        {
            out.append(char(uint8_t(data.at(g + k)) | ((msb << (k + 1)) & 0x80)));
        }
    }
    return out;
}

uint8_t checksum(const QByteArray& data)
{
    uint8_t sum = 0;
    for (char c : data)
    {
        sum += uint8_t(c);
    }
    return uint8_t(-sum) & 0x7F;
}

QByteArray bulkDump(DumpType type, const QByteArray& payload)
{
    // F0 43 00 3E <lenH> <lenL> 14 | 01 01 <type:u32> <data7> | <cs> F7, where len and cs cover the middle part
    QByteArray body;
    body.append(char(0x01));
    body.append(char(0x01));
    appendU32(body, uint32_t(type));
    body.append(pack7(payload));

    QByteArray out;
    out.append(SOX);
    out.append(char(YAMAHA));
    out.append(char(BulkDumpType));
    out.append(char(GROUP));
    out.append(char((body.size() >> 7) & 0x7F));
    out.append(char(body.size() & 0x7F));
    out.append(char(MODEL));
    out.append(body);
    out.append(char(checksum(body)));
    out.append(EOX);
    return out;
}

Message parse(const QByteArray& m)
{
    auto unknown = [&m]() { return Unknown{m}; };
    if (m.size() < 6 || m.front() != SOX || m.back() != EOX || byteAt(m, 1) != YAMAHA)
    {
        return unknown();
    }

    uint8_t type = byteAt(m, 2) & 0xF0;
    if (type == BulkDumpType)
    {
        // F0 43 00 3E lenH lenL 14 01 01 <type:5> <data7...> cs F7
        if (m.size() < 16 || byteAt(m, 3) != GROUP || byteAt(m, 6) != MODEL)
        {
            return unknown();
        }
        QByteArray body = m.mid(7, m.size() - 9);
        int length = (byteAt(m, 4) << 7) | byteAt(m, 5);
        if (length != body.size() || checksum(body) != byteAt(m, m.size() - 2) || byteAt(m, 7) != 0x01 ||
            byteAt(m, 8) != 0x01)
        {
            return unknown();
        }
        return Dump{readBE7(m.constData() + 9, 5), unpack7(body.mid(7))};
    }

    if (type != ParameterChange || m.size() < 8 || byteAt(m, 3) != GROUP || byteAt(m, 4) != MODEL)
    {
        return unknown();
    }

    uint8_t cls = byteAt(m, 5);
    uint8_t sub = byteAt(m, 6);
    if (cls == 0x01 && m.size() == 19)
    {
        return ParameterValue{uint16_t(readBE7(m.constData() + 7, 3)), uint16_t(readBE7(m.constData() + 10, 3)),
                              readBE7(m.constData() + 13, 5)};
    }
    if (cls == 0x00 && sub == 0x70 && m.size() > 9 && byteAt(m, 7) == 0x02)
    {
        // 00 70 02 00, then pairs of 8-byte name and 8-byte version, NUL padded
        Version version;
        QByteArray body = m.mid(9, m.size() - 10);
        for (int i = 0; i + 16 <= body.size(); i += 16)
        {
            auto field = [&body](int at) {
                QByteArray f = body.mid(at, 8);
                int nul = f.indexOf('\0');
                return QString::fromLatin1(nul >= 0 ? f.left(nul) : f).trimmed();
            };
            version.components.append({field(i), field(i + 8)});
        }
        return version;
    }
    if (cls == 0x02 && sub == 0x02)
    {
        MeterFrame frame;
        for (int i = 7; i + 3 <= m.size() - 1; i += 3)
        {
            frame.values.append(readBE7(m.constData() + i, 3));
        }
        return frame;
    }
    if (cls == 0x03 && sub == 0x02 && m.size() > 8 && byteAt(m, 7) == 0x01)
    {
        return StoreCompleted{};
    }
    return unknown();
}

QList<QByteArray> SysExSplitter::feed(const QByteArray& bytes)
{
    QList<QByteArray> out;
    for (char c : bytes)
    {
        if (c == SOX)
        {
            m_buffer.clear();
            m_buffer.append(c);
            m_inMessage = true;
        }
        else if (m_inMessage)
        {
            if (uint8_t(c) >= 0xF8)
            {
                continue; // real-time messages may be interleaved
            }
            m_buffer.append(c);
            if (c == EOX)
            {
                out.append(m_buffer);
                m_buffer.clear();
                m_inMessage = false;
            }
            else if (uint8_t(c) & 0x80)
            {
                // another status byte aborts the SysEx
                m_buffer.clear();
                m_inMessage = false;
            }
        }
    }
    return out;
}

}
