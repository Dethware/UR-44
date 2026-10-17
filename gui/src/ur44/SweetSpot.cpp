#include "SweetSpot.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

#include "Device.h"
#include "Pairing.h"
#include "Parameters.h"

namespace UR44
{

namespace
{
constexpr int FILE_SIZE = 512;
constexpr int POINTS_OFFSET = 0x20;
constexpr int POINT_SIZE = 40; // 20 u16

uint16_t readU16BE(const QByteArray& d, int at)
{
    return uint16_t((uint8_t(d.at(at)) << 8) | uint8_t(d.at(at + 1)));
}
}

std::optional<SweetSpotPreset> SweetSpotPreset::read(const QString& path, QString* error)
{
    auto fail = [error](const QString& why) -> std::optional<SweetSpotPreset> {
        if (error)
        {
            *error = why;
        }
        return std::nullopt;
    };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return fail(file.errorString());
    }
    QByteArray d = file.read(FILE_SIZE);
    if (d.size() < POINTS_OFFSET + POINT_SIZE * Effects::MORPH_POINTS || !d.startsWith("SSMD"))
    {
        return fail("not a Sweet Spot Data (.ssd) file");
    }

    SweetSpotPreset preset;
    QFileInfo info(path);
    preset.name = info.completeBaseName();
    preset.group = info.dir().dirName().trimmed();
    preset.path = path;
    QByteArray id = d.mid(16, 16);
    preset.id = id.left(id.indexOf('\0') >= 0 ? id.indexOf('\0') : id.size());

    const Parameters& params = Parameters::instance();
    for (int p = 0; p < Effects::MORPH_POINTS; ++p)
    {
        for (int i = 0; i < 17; ++i)
        {
            int32_t v = readU16BE(d, POINTS_OFFSET + p * POINT_SIZE + i * 2);
            const Parameter& param = params[Effects::MORPH_TABLE_PARAMETERS[i]];
            if (!param.isValid(v))
            {
                return fail(QString("%1 out of range at point %2").arg(param.name).arg(p + 1));
            }
            preset.points[p][i] = v;
        }
    }
    return preset;
}

QList<SweetSpotPreset> SweetSpotPreset::scan(const QString& folder)
{
    QList<SweetSpotPreset> presets;
    if (folder.isEmpty())
    {
        return presets;
    }
    QDirIterator it(folder, {"*.ssd"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        if (auto preset = read(it.next()))
        {
            presets.append(*preset);
        }
    }
    std::sort(presets.begin(), presets.end(), [](const SweetSpotPreset& a, const SweetSpotPreset& b) {
        return a.id != b.id ? a.id < b.id : a.name < b.name;
    });
    return presets;
}

const QList<SweetSpotPreset>& SweetSpotPreset::builtIn()
{
    static const QList<SweetSpotPreset> presets = [] {
        QList<SweetSpotPreset> list;
        for (std::size_t i = 0; i < Generated::SWEET_SPOT_PRESET_COUNT; ++i)
        {
            const Generated::SweetSpotData& data = Generated::SWEET_SPOT_PRESETS[i];
            SweetSpotPreset preset;
            preset.name = QString::fromLatin1(data.name);
            preset.id = QByteArray(data.id);
            preset.group = QString::fromLatin1(data.group);
            for (int p = 0; p < Effects::MORPH_POINTS; ++p)
            {
                std::copy(std::begin(data.points[p]), std::end(data.points[p]), preset.points[p].begin());
            }
            list.append(preset);
        }
        return list;
    }();
    return presets;
}

QString SweetSpotPreset::builtInNotice()
{
    return QString::fromLatin1(Generated::SWEET_SPOT_NOTICE);
}

QList<SweetSpotPreset> SweetSpotPreset::available(const QString& folder)
{
    QList<SweetSpotPreset> presets = builtIn();
    for (const SweetSpotPreset& extra : scan(folder))
    {
        auto copy = [&extra](const SweetSpotPreset& p) { return p.sameData(extra); };
        if (std::none_of(builtIn().begin(), builtIn().end(), copy))
        {
            presets.append(extra);
        }
    }
    return presets;
}

namespace Effects
{
bool loadSweetSpot(Device& device, uint16_t strip, const SweetSpotPreset& preset)
{
    QHash<uint16_t, QList<int32_t>> points;
    const Parameters& params = Parameters::instance();
    for (int i = 0; i < 17; ++i)
    {
        QList<int32_t>& list = points[params[MORPH_TABLE_PARAMETERS[i]].id];
        for (int p = 0; p < MORPH_POINTS; ++p)
        {
            list.append(preset.points[p][i]);
        }
    }
    if (!device.writeMorphTable(strip, preset.id, preset.name.toLatin1(), points))
    {
        return false;
    }
    // A stereo strip (a paired input's) is two strips: the firmware mirrors their settings, but not the Sweet Spot
    // data, so the other one gets the table too (it keeps the preset when the pair is split)
    const auto state = [&device](uint16_t id, uint16_t index) { return device.value(id, index); };
    if (Pairing::isStereoStrip(state, strip) &&
        !device.writeMorphTable(uint16_t(strip ^ 1), preset.id, preset.name.toLatin1(), points))
    {
        return false;
    }
    // the strip's compressor and EQ follow the new table at the current MORPH position
    return setMorph(device, strip, device.value("FxMorphValue", strip).value_or(0));
}
}

}
