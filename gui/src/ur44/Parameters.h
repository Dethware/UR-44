#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cstdint>

/*
 * The 177 DSP parameters, generated from memapi_ur44.dll (re/gen_gui_params.py -> parameters.json, embedded as
 * :/ur44/parameters.json). Ids and ranges are the wire values; labels are what dspMixFx displays.
 */
namespace UR44
{

struct Parameter
{
    uint16_t id = 0;
    QString name;
    int32_t min = 0;
    int32_t max = 0;
    int32_t defaultValue = 0;
    /// Valid instance indices: 0 .. instances-1 (channels, input pairs, strips, ...)
    int instances = 1;
    QString unit;
    /// Display labels for min..max (labels[0] is min); empty for plain numbers
    QStringList labels;

    bool isSigned() const { return min < 0; }
    bool isValid(int32_t value) const { return value >= min && value <= max; }
    int32_t clamp(int32_t value) const { return std::clamp(value, min, max); }
    /// "0.00 dB", "L16", "On", ...
    QString text(int32_t value) const;
    /// Wire value (u32) -> value, sign-extended for signed parameters
    int32_t fromWire(uint32_t raw) const { return int32_t(raw); }
};

class Parameters
{
public:
    /// The table embedded in the binary
    static const Parameters& instance();

    /// Parses a parameters.json document; empty on error
    static Parameters fromJson(const QByteArray& json, QString* error = nullptr);

    const Parameter* byId(uint16_t id) const;
    /// By name, including the macOS MemAPI 2.3.0 aliases
    const Parameter* byName(const QString& name) const;
    /// byName() that must succeed (asserts in debug builds)
    const Parameter& operator[](const QString& name) const;

    const QList<Parameter>& all() const { return m_parameters; }
    bool isEmpty() const { return m_parameters.isEmpty(); }

private:
    QList<Parameter> m_parameters;
    QHash<uint16_t, int> m_byId;
    QHash<QString, int> m_byName;
};

}
