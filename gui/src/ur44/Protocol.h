#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>
#include <variant>

/*
 * The UR44 control protocol: Yamaha-style SysEx on the second USB-MIDI port.
 * See docs/PROTOCOL.md for the derivation. Pure encoding/decoding, no I/O.
 *
 * Numbers in messages are big-endian 7-bit groups: u16 as 3 bytes, u32 as 5 bytes.
 */
namespace UR44::Protocol
{

// --- host -> device ---------------------------------------------------------------------------------------------

/// F0 43 30 3E 14 01 01 <id> <index> F7
QByteArray getParameter(uint16_t id, uint16_t index);
/// F0 43 10 3E 14 01 01 <id> <index> <value> F7
QByteArray setParameter(uint16_t id, uint16_t index, uint32_t value);
/// F0 43 30 3E 14 00 70 02 F7
QByteArray getVersion();
/// F0 43 30 3E 14 02 02 32 {7F|00} F7
QByteArray meters(bool on);

enum class DumpType : uint32_t
{
    CurrentScene = 1,
    MorphTable = 2,
    All = 3,
};
/// F0 43 20 3E 14 01 01 <type:u32> F7
QByteArray dumpRequest(DumpType type);

enum class FxType : uint8_t
{
    ChannelStrip = 0,
    GuitarAmp = 1,
};
enum class InsertPoint : uint8_t
{
    Pre = 0,
    Post = 1,
};
/// F0 43 10 3E 14 03 08 01 <type> <channel:u16> <point> F7
QByteArray fxAssign(FxType type, uint16_t channel, InsertPoint point);
/// F0 43 10 3E 14 03 08 02 <type> <channel:u16> 00 F7
QByteArray fxUnassign(FxType type, uint16_t channel);

// --- device -> host ---------------------------------------------------------------------------------------------

/// A parameter value: the reply to a get, or an unsolicited change
struct ParameterValue
{
    uint16_t id;
    uint16_t index;
    uint32_t value;
};

/// Firmware component versions, e.g. {"BOOT", "V2.12"}, {"MAIN", "V2.12"}
struct Version
{
    QList<std::pair<QString, QString>> components;
};

/// One meter frame: 22 (level, peak hold) pairs. See Meters below for the layout.
struct MeterFrame
{
    QList<uint32_t> values;
};

/// A bulk dump: the type and the unpacked (8-bit) payload. Decode the payload with BulkDump.
struct Dump
{
    uint32_t type;
    QByteArray payload;
};

/// The device finished storing the scene
struct StoreCompleted
{
};

struct Unknown
{
    QByteArray message;
};

using Message = std::variant<ParameterValue, Version, MeterFrame, Dump, StoreCompleted, Unknown>;

/// Decodes one complete SysEx message (F0 ... F7)
Message parse(const QByteArray& sysex);

/*
 * Splits a raw MIDI byte stream into complete SysEx messages. Bytes outside F0..F7 are dropped.
 */
class SysExSplitter
{
public:
    /// Adds received bytes; returns the messages they completed
    QList<QByteArray> feed(const QByteArray& bytes);
    void clear() { m_buffer.clear(); m_inMessage = false; }

private:
    QByteArray m_buffer;
    bool m_inMessage = false;
};

// --- helpers (exposed for tests) --------------------------------------------------------------------------------

void appendU16(QByteArray& out, uint16_t v);
void appendU32(QByteArray& out, uint32_t v);
uint32_t readBE7(const char* data, int count);

/// 8 -> 7 bit packing used by bulk dumps: groups of 7 bytes' low 7 bits, then one byte with their MSBs
QByteArray pack7(const QByteArray& data);
QByteArray unpack7(const QByteArray& data);
/// Yamaha checksum: -(sum) & 0x7F
uint8_t checksum(const QByteArray& data);
/// Builds a bulk dump message (device -> host format; also what the host sends to write state)
QByteArray bulkDump(DumpType type, const QByteArray& payload);

}

/*
 * Meter frame layout and scale (docs/PROTOCOL.md, "Meter frame").
 */
namespace UR44::Meters
{
enum Pair
{
    Input1 = 0, // .. Input6 = 5
    DawLeft = 6,
    DawRight = 7,
    MasterLeft = 8,
    MasterRight = 9,
    GainReduction1 = 10, // .. 4 = 13; channel strips
    StripOutput1 = 14,   // .. 4 = 17
    GuitarInLeft = 18,
    GuitarInRight = 19,
    GuitarOutLeft = 20,
    GuitarOutRight = 21,
    PairCount = 22,
};

constexpr double FLOOR_DB = -80.0;

/// Level meter value -> dBFS (FLOOR_DB at 0, 0 dBFS at 8192)
inline double levelDb(uint32_t v)
{
    return v == 0 ? FLOOR_DB : v / 102.4 + FLOOR_DB;
}

/// Gain reduction meter value -> dB of reduction (0 = none)
inline double gainReductionDb(uint32_t v)
{
    int level = int(v & 0x1FFF);
    return level >= 8191 ? 0.0 : (8191 - level) / 102.4;
}
}
