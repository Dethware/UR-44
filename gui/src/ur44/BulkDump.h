#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

#include <cstdint>
#include <optional>

/*
 * Decoder for bulk dump payloads (after 7-bit unpacking), see docs/PROTOCOL.md "Bulk dump encoding".
 *
 * A payload is two chunks, each a 32-byte header (16-byte name, ..., u16 BE body length at 30) and a body:
 *   F_<name>  format: groups of  80 01 <instances:u16>  followed by items  <id:u16> <flags> <size>
 *             (flags 01 unsigned, 00 signed, 40 byte array followed by <count:u16>)
 *   D_<name>  values in format order, item-major (every instance of an item in turn), big-endian
 */
namespace UR44
{

struct BulkDump
{
    struct Item
    {
        uint16_t id;
        uint8_t flags;
        uint8_t size;
        uint16_t count; // array elements (1 for scalars)
        bool isArray() const { return flags & 0x40; }
        bool isSigned() const { return (flags & 0x7F) == 0; }
    };
    struct Group
    {
        uint16_t instances;
        QList<Item> items;
    };

    /// "CurrentScene" or "All"
    QString name;
    QList<Group> groups;
    /// Scalar values: id -> one value per instance (signed values sign-extended)
    QHash<uint16_t, QList<int32_t>> values;
    /// Byte arrays (e.g. MorphSystemName): id -> one array per instance
    QHash<uint16_t, QList<QByteArray>> arrays;

    /// Decodes a payload; nullopt (with the reason in `error`) if it's malformed
    static std::optional<BulkDump> decode(const QByteArray& payload, QString* error = nullptr);
};

}
