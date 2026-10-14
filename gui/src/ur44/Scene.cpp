#include "Scene.h"

#include <QObject>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <algorithm>

#include "Effects.h"
#include "Parameters.h"
#include "SweetSpot.h"

namespace UR44
{
namespace Scenes
{

namespace
{
constexpr int HEADER_SIZE = 32;
const char* const SCENE_NAME = "CurrentScene";

/// The state dump items that a scene leaves out
const char* const SYSTEM_ITEMS[] = {"PatchAuxPhoneOutput", "PatchDawMeter", "PatchMasterMeter", "SysFsCode",
                                    "SysSoloOnf",          "HaP48VOnf",     "HaInputGain",      "PatchLoopbackMode"};

uint32_t readU32(const QByteArray& d, int at, bool bigEndian)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
    {
        int byte = bigEndian ? at + i : at + 3 - i;
        v = (v << 8) | uint8_t(d.at(byte));
    }
    return v;
}

void appendU32(QByteArray& d, uint32_t v, bool bigEndian)
{
    for (int i = 0; i < 4; ++i)
    {
        int shift = bigEndian ? 24 - 8 * i : 8 * i;
        d.append(char((v >> shift) & 0xFF));
    }
}

enum class Order
{
    Device,
    Host,
    Unknown,
};

Order headerOrder(const QByteArray& d, int at)
{
    // device: ... 27 74 <size u32 BE>; dspMixFx files: ... 74 27 00 00 <size u32 LE>
    if (uint8_t(d.at(at + 26)) == 0x27 && uint8_t(d.at(at + 27)) == 0x74)
    {
        return Order::Device;
    }
    if (uint8_t(d.at(at + 24)) == 0x74 && uint8_t(d.at(at + 25)) == 0x27 && d.at(at + 26) == 0 && d.at(at + 27) == 0)
    {
        return Order::Host;
    }
    return Order::Unknown;
}

/// Rewrites every chunk header in the given byte order; nullopt if a header isn't recognised
std::optional<QByteArray> convert(const QByteArray& data, Order target)
{
    QByteArray out;
    int at = 0;
    while (at + HEADER_SIZE <= data.size())
    {
        Order order = headerOrder(data, at);
        if (order == Order::Unknown)
        {
            return std::nullopt;
        }
        uint32_t size = readU32(data, at + 28, order == Order::Device);
        if (at + HEADER_SIZE + qsizetype(size) > data.size())
        {
            return std::nullopt;
        }
        out.append(data.mid(at, 16));
        if (target == Order::Device)
        {
            out.append(QByteArray::fromHex("010000000000000000002774"));
            appendU32(out, size, true);
        }
        else
        {
            out.append(QByteArray(8, '\0'));
            out.append(QByteArray::fromHex("74270000"));
            appendU32(out, size, false);
        }
        out.append(data.mid(at + HEADER_SIZE, size));
        at += HEADER_SIZE + int(size);
    }
    if (at != data.size() || out.isEmpty())
    {
        return std::nullopt;
    }
    return out;
}

void fail(QString* error, const QString& why)
{
    if (error)
    {
        *error = why;
    }
}
}

bool isSceneItem(uint16_t id)
{
    const Parameters& params = Parameters::instance();
    return std::none_of(std::begin(SYSTEM_ITEMS), std::end(SYSTEM_ITEMS),
                        [&](const char* name) { return params[name].id == id; });
}

QList<BulkDump::Group> format(const QList<BulkDump::Group>& stateFormat)
{
    QList<BulkDump::Group> groups;
    for (const BulkDump::Group& g : stateFormat)
    {
        BulkDump::Group scene{g.instances, {}};
        for (const BulkDump::Item& item : g.items)
        {
            if (isSceneItem(item.id))
            {
                scene.items.append(item);
            }
        }
        if (!scene.items.isEmpty())
        {
            groups.append(scene);
        }
    }
    return groups;
}

std::optional<QByteArray> normalize(const QByteArray& data, QString* error)
{
    auto device = convert(data, Order::Device);
    if (!device)
    {
        fail(error, QObject::tr("not a scene (unrecognised data)"));
        return std::nullopt;
    }
    QString why;
    auto dump = BulkDump::decode(*device, &why);
    if (!dump)
    {
        fail(error, QObject::tr("not a scene (%1)").arg(why));
        return std::nullopt;
    }
    if (dump->name != QLatin1String(SCENE_NAME))
    {
        fail(error, QObject::tr("not a scene (%1 data)").arg(dump->name));
        return std::nullopt;
    }
    return device;
}

QByteArray toHostOrder(const QByteArray& deviceData)
{
    return convert(deviceData, Order::Host).value_or(QByteArray());
}

QByteArray defaults(const QList<BulkDump::Group>& format)
{
    const Parameters& params = Parameters::instance();
    BulkDump scene;
    scene.name = SCENE_NAME;
    scene.groups = format;
    for (const BulkDump::Group& g : format)
    {
        for (const BulkDump::Item& item : g.items)
        {
            const Parameter* p = params.byId(item.id);
            for (int inst = 0; inst < g.instances; ++inst)
            {
                if (item.isArray())
                {
                    scene.arrays[item.id].append(QByteArray());
                }
                else
                {
                    scene.values[item.id].append(p ? p->defaultValue : 0);
                }
            }
        }
    }

    // the morph tables: "01 Basic" on every strip, and the strips' compressor/EQ at their MORPH position on it
    const SweetSpotPreset& basic = SweetSpotPreset::builtIn().first();
    const uint16_t idId = params["MorphSystemId"].id, nameId = params["MorphSystemName"].id;
    for (QByteArray& a : scene.arrays[idId])
    {
        a = basic.id;
    }
    for (QByteArray& a : scene.arrays[nameId])
    {
        a = basic.name.toLatin1();
    }
    for (size_t f = 0; f < std::size(Effects::MORPH_TABLE_PARAMETERS); ++f)
    {
        QList<int32_t>& table = scene.values[params[Effects::MORPH_TABLE_PARAMETERS[f]].id];
        for (int inst = 0; inst < table.size(); ++inst)
        {
            table[inst] = basic.points[size_t(inst % Effects::MORPH_POINTS)][f];
        }
    }
    QList<int32_t>& morph = scene.values[params["FxMorphValue"].id];
    for (int strip = 0; strip < morph.size(); ++strip)
    {
        int lower = std::clamp(int(morph[strip] / Effects::MORPH_STEP), 0, Effects::MORPH_POINTS - 1);
        int upper = std::min(lower + 1, Effects::MORPH_POINTS - 1);
        Effects::MorphPoint values = Effects::interpolate(basic.points[size_t(lower)], basic.points[size_t(upper)],
                                                          morph[strip]);
        for (size_t f = 0; f < values.size(); ++f)
        {
            QList<int32_t>& v = scene.values[params[Effects::MORPHED_PARAMETERS[f]].id];
            if (strip < v.size())
            {
                v[strip] = values[f];
            }
        }
    }
    return scene.encode();
}

// --- *.UR44 ------------------------------------------------------------------------------------------------------

namespace
{
/// The base64 text directly inside the current element; skips (and ends after) its child elements
QByteArray elementData(QXmlStreamReader& xml)
{
    QByteArray text;
    int depth = 1;
    while (depth > 0 && !xml.atEnd())
    {
        switch (xml.readNext())
        {
            case QXmlStreamReader::StartElement:
                ++depth;
                break;
            case QXmlStreamReader::EndElement:
                --depth;
                break;
            case QXmlStreamReader::Characters:
                if (depth == 1)
                {
                    text.append(xml.text().toLatin1());
                }
                break;
            default:
                break;
        }
    }
    return QByteArray::fromBase64(text.trimmed());
}
}

std::optional<File> readFile(const QByteArray& data, QString* error)
{
    QXmlStreamReader xml(data);
    if (!xml.readNextStartElement() || xml.name() != QLatin1String("UR44File"))
    {
        fail(error, QObject::tr("not a dspMixFx UR44 scene file"));
        return std::nullopt;
    }
    File file;
    QList<std::pair<int, Scene>> numbered;
    while (xml.readNextStartElement())
    {
        if (xml.name() == QLatin1String("SceneData"))
        {
            while (xml.readNextStartElement())
            {
                if (xml.name() != QLatin1String("Scene"))
                {
                    xml.skipCurrentElement();
                    continue;
                }
                int no = xml.attributes().value("No").toInt();
                QString name = xml.attributes().value("name").toString();
                QString why;
                auto scene = normalize(elementData(xml), &why);
                if (!scene)
                {
                    fail(error, QObject::tr("scene %1 (%2): %3").arg(no).arg(name, why));
                    return std::nullopt;
                }
                numbered.append({no, Scene{name, *scene}});
            }
        }
        else if (xml.name() == QLatin1String("CurrentData"))
        {
            if (auto current = normalize(elementData(xml)))
            {
                file.current = current;
            }
        }
        else
        {
            xml.skipCurrentElement();
        }
    }
    if (xml.hasError())
    {
        fail(error, xml.errorString());
        return std::nullopt;
    }
    std::stable_sort(numbered.begin(), numbered.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [no, scene] : numbered)
    {
        file.scenes.append(scene);
    }
    return file;
}

QByteArray writeFile(const QList<Scene>& scenes, const QByteArray& current)
{
    // the layout dspMixFx writes (docs/PROTOCOL.md)
    QByteArray out;
    QXmlStreamWriter xml(&out);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeDTD("<!DOCTYPE UR44>");
    xml.writeStartElement("UR44File");
    xml.writeTextElement("Header", "UR44 File");
    xml.writeTextElement("Version", "1.0.0");
    xml.writeStartElement("SceneData");
    for (int i = 0; i < std::min<qsizetype>(scenes.size(), FILE_SLOTS); ++i)
    {
        QByteArray host = toHostOrder(scenes[i].data);
        QByteArray base64 = host.toBase64();
        xml.writeStartElement("Scene");
        xml.writeAttribute("No", QString::number(i + 1));
        xml.writeAttribute("name", scenes[i].name);
        xml.writeAttribute("sz", QString::number(host.size()));
        xml.writeAttribute("cvtSz", QString::number(base64.size()));
        xml.writeCharacters(QString::fromLatin1(base64));
        xml.writeEndElement();
    }
    xml.writeEndElement();
    xml.writeTextElement("CurrentScene", scenes.isEmpty() ? "0" : "1");
    QByteArray host = toHostOrder(current);
    QByteArray base64 = host.toBase64();
    xml.writeStartElement("CurrentData");
    xml.writeAttribute("sz", QString::number(host.size()));
    xml.writeAttribute("cvtSz", QString::number(base64.size()));
    xml.writeAttribute("IO", "0");
    xml.writeAttribute("ChStripPatch", "0");
    xml.writeAttribute("ExtFX", "0");
    xml.writeCharacters(QString::fromLatin1(base64));
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndDocument();
    return out;
}

}
}
