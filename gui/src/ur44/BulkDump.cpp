#include "BulkDump.h"

namespace UR44
{

namespace
{
constexpr int HEADER_SIZE = 32;

uint32_t readBE(const QByteArray& data, int at, int size)
{
    uint32_t v = 0;
    for (int i = 0; i < size; ++i)
    {
        v = (v << 8) | uint8_t(data.at(at + i));
    }
    return v;
}

struct Chunk
{
    QString name;
    QByteArray body;
};

bool splitChunks(const QByteArray& payload, QList<Chunk>& chunks, QString& error)
{
    int at = 0;
    while (at + HEADER_SIZE <= payload.size())
    {
        QByteArray rawName = payload.mid(at, 16);
        int nul = rawName.indexOf('\0');
        QString name = QString::fromLatin1(nul >= 0 ? rawName.left(nul) : rawName);
        int size = int(readBE(payload, at + 30, 2));
        if (at + HEADER_SIZE + size > payload.size())
        {
            error = QString("chunk %1 is truncated").arg(name);
            return false;
        }
        chunks.append({name, payload.mid(at + HEADER_SIZE, size)});
        at += HEADER_SIZE + size;
    }
    return true;
}
}

std::optional<BulkDump> BulkDump::decode(const QByteArray& payload, QString* errorOut)
{
    QString error;
    auto fail = [&](const QString& why) -> std::optional<BulkDump> {
        if (errorOut)
        {
            *errorOut = why;
        }
        return std::nullopt;
    };

    QList<Chunk> chunks;
    if (!splitChunks(payload, chunks, error))
    {
        return fail(error);
    }
    const Chunk* format = nullptr;
    const Chunk* data = nullptr;
    for (const Chunk& c : chunks)
    {
        if (c.name.startsWith("F_") && !format)
        {
            format = &c;
        }
    }
    if (!format)
    {
        return fail("no format chunk");
    }
    BulkDump dump;
    dump.name = format->name.mid(2);
    for (const Chunk& c : chunks)
    {
        if (c.name == "D_" + dump.name)
        {
            data = &c;
        }
    }
    if (!data)
    {
        return fail(QString("no data chunk for %1").arg(dump.name));
    }

    // Format: groups and items
    const QByteArray& f = format->body;
    for (int i = 0; i + 4 <= f.size();)
    {
        if (uint8_t(f.at(i)) == 0x80)
        {
            dump.groups.append(Group{uint16_t(readBE(f, i + 2, 2)), {}});
            i += 4;
            continue;
        }
        if (dump.groups.isEmpty())
        {
            return fail("format item before the first group");
        }
        Item item{uint16_t(readBE(f, i, 2)), uint8_t(f.at(i + 2)), uint8_t(f.at(i + 3)), 1};
        i += 4;
        if (item.isArray())
        {
            if (i + 2 > f.size())
            {
                return fail("truncated array item");
            }
            item.count = uint16_t(readBE(f, i, 2));
            i += 2;
        }
        if (item.size == 0 || item.size > 4)
        {
            return fail(QString("unsupported item size %1 for id %2").arg(item.size).arg(item.id));
        }
        dump.groups.last().items.append(item);
    }

    // Data: item-major
    const QByteArray& d = data->body;
    int at = 0;
    for (const Group& group : std::as_const(dump.groups))
    {
        for (const Item& item : group.items)
        {
            int needed = group.instances * item.count * item.size;
            if (at + needed > d.size())
            {
                return fail(QString("data too short at id %1").arg(item.id));
            }
            if (item.isArray())
            {
                auto& list = dump.arrays[item.id];
                for (int inst = 0; inst < group.instances; ++inst)
                {
                    list.append(d.mid(at, item.count * item.size));
                    at += item.count * item.size;
                }
                continue;
            }
            auto& list = dump.values[item.id];
            for (int inst = 0; inst < group.instances; ++inst)
            {
                uint32_t raw = readBE(d, at, item.size);
                at += item.size;
                int32_t v = int32_t(raw);
                if (item.isSigned() && item.size < 4 && (raw & (1u << (item.size * 8 - 1))))
                {
                    v = int32_t(raw | ~((1u << (item.size * 8)) - 1));
                }
                list.append(v);
            }
        }
    }
    if (at != d.size())
    {
        return fail(QString("%1 data bytes left over").arg(d.size() - at));
    }
    return dump;
}

}
