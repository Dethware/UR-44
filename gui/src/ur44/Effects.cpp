#include "Effects.h"

#include <QString>

#include <cmath>

#include "Device.h"
#include "Parameters.h"

namespace UR44::Effects
{

const char* const MORPHED_PARAMETERS[17] = {
    "FxCompAttack", "FxCompRelease", "FxCompRatio", "FxCompKnee", "FxCompThreshOfst", "FxCompMakeupOfst",
    "FxCompScFreq", "FxCompScGain", "FxCompScQual", "FxEqHsvFreq", "FxEqHsvGain", "FxEqMidFreq",
    "FxEqMidGain", "FxEqMidQual", "FxEqLsvFreq", "FxEqLsvGain", "FxEqPeqGain",
};

const char* const MORPH_TABLE_PARAMETERS[17] = {
    "MorphCompAttack", "MorphCompRelease", "MorphCompRatio", "MorphCompKnee", "MorphCompThreshOfst",
    "MorphCompMakeupOfst", "MorphCompScFreq", "MorphCompScGain", "MorphCompScQual", "MorphEqHsvFreq",
    "MorphEqHsvGain", "MorphEqMidFreq", "MorphEqMidGain", "MorphEqMidQual", "MorphEqLsvFreq",
    "MorphEqLsvGain", "MorphEqPeqGain",
};

namespace
{
/// The morph point index pair and fraction for a morph value (0 is exactly point 0)
void morphPosition(int32_t morphValue, int& lower, int& upper, double& fraction)
{
    double position = (morphValue & 0xFF) / MORPH_STEP;
    lower = int(position);
    fraction = position - lower;
    upper = lower + 1;
    if (lower > MORPH_POINTS - 2)
    {
        lower = upper = MORPH_POINTS - 1;
    }
    if (morphValue == 0)
    {
        lower = upper = 0;
    }
}
}

MorphPoint interpolate(const MorphPoint& from, const MorphPoint& to, int32_t morphValue)
{
    int lower, upper;
    double fraction;
    morphPosition(morphValue, lower, upper, fraction);
    MorphPoint out{};
    const Parameters& params = Parameters::instance();
    for (size_t i = 0; i < out.size(); ++i)
    {
        // (b - a) * f + a, converted to an integer by truncation, then clipped (CsmdLib::PrmClip)
        int32_t v = int32_t((to[i] - from[i]) * fraction + from[i]);
        out[i] = params[MORPHED_PARAMETERS[i]].clamp(v);
    }
    return out;
}

bool setMorph(Device& device, uint16_t strip, int32_t morphValue)
{
    const Parameters& params = Parameters::instance();
    const Parameter& morph = params["FxMorphValue"];
    if (!device.isReady() || strip >= morph.instances)
    {
        return false;
    }
    morphValue = morph.clamp(morphValue);
    int lower, upper;
    double fraction;
    morphPosition(morphValue, lower, upper, fraction);

    MorphPoint from{}, to{};
    for (size_t i = 0; i < from.size(); ++i)
    {
        auto a = device.value(MORPH_TABLE_PARAMETERS[i], uint16_t(strip * MORPH_POINTS + lower));
        auto b = device.value(MORPH_TABLE_PARAMETERS[i], uint16_t(strip * MORPH_POINTS + upper));
        if (!a || !b)
        {
            return false;
        }
        from[i] = *a;
        to[i] = *b;
    }
    // interpolate() derives the position from the value itself; pass the values for the two points it picks
    MorphPoint values = interpolate(from, to, morphValue);

    device.setParameter(morph.id, strip, morphValue);
    for (size_t i = 0; i < values.size(); ++i)
    {
        if (device.value(MORPHED_PARAMETERS[i], strip) != values[i])
        {
            device.setParameter(MORPHED_PARAMETERS[i], strip, values[i]);
        }
    }
    return true;
}

bool setReverbType(Device& device, int32_t type)
{
    if (!device.isReady() || type < 0 || type > 2)
    {
        return false;
    }
    // Per-type defaults: Hall, Room, Plate (FxRevBal is left alone, like the original)
    struct Default
    {
        const char* name;
        int32_t values[3];
    };
    static const Default DEFAULTS[] = {
        {"FxRevTime", {23, 6, 21}},     {"FxRevDiff", {10, 8, 8}},      {"FxRevIniDly", {2, 2, 2}},
        {"FxRevHpf", {4, 6, 12}},       {"FxRevLpf", {50, 47, 52}},     {"FxRevRoomSize", {29, 15, 18}},
        {"FxRevHighRatio", {8, 7, 9}},  {"FxRevLowRatio", {12, 11, 10}}, {"FxRevDecay", {27, 15, 5}},
        {"FxRevLowFreq", {32, 32, 32}},
    };
    if (device.value("FxRevType") == type)
    {
        return true; // like the original, choosing the current type changes nothing
    }
    device.setParameter("FxRevType", 0, type);
    for (const Default& d : DEFAULTS)
    {
        device.setParameter(d.name, 0, d.values[type]);
    }
    return true;
}

bool setGuitarAmpType(Device& device, int32_t type)
{
    if (!device.isReady() || type < 0 || type > 3)
    {
        return false;
    }
    if (device.value("GtrFxType") == type)
    {
        return true;
    }
    // The new model's parameters (GtrFx<Model>*) go back to their defaults
    static const char* const PREFIXES[] = {"GtrFxClean", "GtrFxCrunch", "GtrFxLead", "GtrFxDrive"};
    const QString prefix = PREFIXES[type];
    device.setParameter("GtrFxType", 0, type);
    for (const Parameter& p : Parameters::instance())
    {
        QString name = QString::fromLatin1(p.name);
        // GtrFxnCrunchHidMaxOut is the Windows name's typo for GtrFxCrunchHidMaxOut
        if (name.startsWith(prefix) || (type == 1 && name == "GtrFxnCrunchHidMaxOut"))
        {
            if (device.value(p.id, 0) != p.defaultValue)
            {
                device.setParameter(p.id, 0, p.defaultValue);
            }
        }
    }
    return true;
}

}
