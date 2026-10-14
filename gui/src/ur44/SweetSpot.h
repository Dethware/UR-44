#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "Effects.h"

/*
 * Sweet Spot Data: the morph tables of the Sweet Spot Morphing Channel Strip, as .ssd preset files.
 *
 * The 34 factory presets are Yamaha's and come with TOOLS for UR44 (Copyright(C)2008 Yamaha Corporation). They're
 * built in: re/extract_sweet_spot.py turned them into tools/ur44_sweet_spot.py, and the build generates
 * SweetSpotTable.cpp from that. More .ssd files can be read from a folder.
 *
 * .ssd (512 bytes): "SSMD", version u32 1 at 8, the id ("0002") at 0x10 (16 bytes, NUL padded), then 9 points of
 * 20 big-endian u16 at 0x20; the first 17 of each point are the Morph* fields in parameter order. The name is
 * the file name ("02 Color.ssd").
 */
namespace UR44
{

struct SweetSpotPreset
{
    /// "02 Color": what the device stores as MorphSystemName
    QString name;
    /// "0002": MorphSystemId
    QByteArray id;
    /// The folder it came from, e.g. "Yamaha Preset"
    QString group;
    /// The file it was read from; empty for a built-in preset
    QString path;
    std::array<Effects::MorphPoint, Effects::MORPH_POINTS> points{};

    static std::optional<SweetSpotPreset> read(const QString& path, QString* error = nullptr);
    /// All presets below `folder`, ordered by id (which keeps the factory groups together)
    static QList<SweetSpotPreset> scan(const QString& folder);

    /// The factory presets that come with TOOLS for UR44, ordered by id
    static const QList<SweetSpotPreset>& builtIn();
    /// The copyright notice the factory presets carry
    static QString builtInNotice();
    /// The built-in presets, then those in `folder` that aren't copies of a built-in one
    static QList<SweetSpotPreset> available(const QString& folder);

    bool isBuiltIn() const { return path.isEmpty(); }
    bool sameData(const SweetSpotPreset& other) const { return id == other.id && name == other.name && points == other.points; }
};

namespace Generated
{
struct SweetSpotData
{
    const char* group;
    const char* name;
    const char* id;
    uint16_t points[9][17];
};
// SweetSpotTable.cpp, generated during the build from tools/ur44_sweet_spot.py
extern const SweetSpotData SWEET_SPOT_PRESETS[];
extern const std::size_t SWEET_SPOT_PRESET_COUNT;
extern const char* const SWEET_SPOT_NOTICE;
}

namespace Effects
{
/// Loads a preset into a channel strip (0-3) and re-applies the strip's MORPH position to the new table, like
/// dspMixFx. The device keeps it (until the next standby: stored like the rest of the state).
bool loadSweetSpot(Device& device, uint16_t strip, const SweetSpotPreset& preset);
}

}
