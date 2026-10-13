#pragma once

#include <QString>

#include <algorithm>
#include <cstddef>
#include <cstdint>

/*
 * The 177 DSP parameters, recovered from memapi_ur44.dll. Ids and ranges are the wire values; labels are what
 * dspMixFx displays.
 *
 * The data is constant, in static memory: ParameterTable.cpp is generated at build time by gen_parameter_table.py
 * from tools/ur44_params.py and tools/ur44_labels.json.
 */
namespace UR44
{

struct Parameter
{
    uint16_t id;
    const char* name;
    int32_t min;
    int32_t max;
    int32_t defaultValue;
    /// Valid instance indices: 0 .. instances-1 (channels, input pairs, strips, ...)
    int instances;
    /// "dB", "Hz", ... or ""
    const char* unit;
    /// Display labels for min..max (labels[0] is the label of min), or nullptr for plain numbers
    const char* const* labels;

    bool isSigned() const { return min < 0; }
    bool isValid(int32_t value) const { return value >= min && value <= max; }
    int32_t clamp(int32_t value) const { return std::clamp(value, min, max); }
    /// "0.00 dB", "L16", "On", ...
    QString text(int32_t value) const;
    /// Wire value (u32) -> value, sign-extended for signed parameters
    int32_t fromWire(uint32_t raw) const { return int32_t(raw); }
};

namespace Generated
{
struct NameIndexEntry
{
    const char* name;
    uint16_t id;
};

/// Indexed by id
extern const Parameter PARAMETERS[];
extern const std::size_t PARAMETER_COUNT;
/// Every name and alias, sorted by name (strcmp order)
extern const NameIndexEntry NAME_INDEX[];
extern const std::size_t NAME_INDEX_COUNT;
}

/*
 * Lookups in the parameter table.
 */
class Parameters
{
public:
    static const Parameters& instance();

    const Parameter* byId(uint16_t id) const;
    /// By name, including the macOS MemAPI 2.3.0 aliases
    const Parameter* byName(const char* name) const;
    const Parameter* byName(const QString& name) const;
    /// byName() that must succeed (asserts in debug builds)
    const Parameter& operator[](const char* name) const;
    const Parameter& operator[](const QString& name) const;

    std::size_t size() const { return Generated::PARAMETER_COUNT; }
    const Parameter* begin() const { return Generated::PARAMETERS; }
    const Parameter* end() const { return Generated::PARAMETERS + Generated::PARAMETER_COUNT; }
};

}
