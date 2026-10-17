#include "Pairing.h"

#include <cstring>

#include "Parameters.h"

namespace UR44::Pairing
{

namespace
{
uint16_t id(const char* name)
{
    return Parameters::instance()[name].id;
}

// Linked while paired, and copied to the right channel when pairing (MemAPI's pairItemTbl, and the device)
const char* const LINKED[] = {
    "ChHpfOnf",           "ChHpfFreq",        "ChSendMixBus1SoloOnf", "ChSendMixBus2SoloOnf", "ChSendMixBus1Onf",
    "ChSendMixBus2Onf",   "ChSendFxBusOnf",   "ChSendFxBusLevel",     "ChFaderLinkOnf",
};
// Linked while the fader link is on (faderLinkItemTbl)
const char* const FADER_LINKED[] = {"ChSendMixBus1Level", "ChSendMixBus2Level"};

bool isOneOf(uint16_t parameter, const char* const* names, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i)
    {
        if (parameter == id(names[i]))
        {
            return true;
        }
    }
    return false;
}

/// The strip parameters the firmware mirrors to the other strip of a stereo strip: the strip's settings (4
/// instances), not its Sweet Spot data (MorphSystemId/Name, the Morph* tables) or the link itself
bool isMirroredStripParameter(uint16_t parameter)
{
    const Parameter* p = Parameters::instance().byId(parameter);
    return p && p->instances == 4 && std::strncmp(p->name, "Fx", 2) == 0 && std::strncmp(p->name, "FxRev", 5) != 0 &&
           parameter != id("FxStereoLinkOnf");
}

bool isOn(const Lookup& state, const char* name, uint16_t index)
{
    return state(id(name), index).value_or(0) != 0;
}
}

bool isPaired(const Lookup& state, uint16_t channel)
{
    return isOn(state, "ChPairOnf", channel);
}

bool isStereoStrip(const Lookup& state, uint16_t strip)
{
    return isOn(state, "FxStereoLinkOnf", strip);
}

QList<Change> followUps(const Lookup& state, uint16_t parameter, uint16_t index, int32_t value)
{
    QList<Change> changes;
    if (isMirroredStripParameter(parameter))
    {
        if (isStereoStrip(state, index))
        {
            changes.append({parameter, uint16_t(index ^ 1), value});
        }
        return changes;
    }
    const Parameter* p = Parameters::instance().byId(parameter);
    if (!p || p->instances != 6 || index > 5)
    {
        return changes;
    }
    const uint16_t other = partner(index);
    const uint16_t l = left(index), r = uint16_t(l + 1);
    auto copyLevels = [&]() {
        for (const char* name : FADER_LINKED)
        {
            if (auto v = state(id(name), l))
            {
                changes.append({id(name), r, *v});
            }
        }
    };

    if (parameter == id("ChPairOnf"))
    {
        changes.append({parameter, other, value});
        if (value && !isPaired(state, index))
        {
            // the left channel's settings to the right one
            for (const char* name : LINKED)
            {
                if (auto v = state(id(name), l))
                {
                    changes.append({id(name), r, *v});
                }
            }
            if (isOn(state, "ChFaderLinkOnf", l))
            {
                copyLevels();
            }
        }
        return changes;
    }
    if (!isPaired(state, index))
    {
        return changes;
    }
    if (isOneOf(parameter, LINKED, std::size(LINKED)))
    {
        changes.append({parameter, other, value});
        if (parameter == id("ChFaderLinkOnf") && value && !isOn(state, "ChFaderLinkOnf", index))
        {
            // switching the link on copies the left channel's levels to the right one
            copyLevels();
        }
    }
    else if (isOneOf(parameter, FADER_LINKED, std::size(FADER_LINKED)) && isOn(state, "ChFaderLinkOnf", index))
    {
        changes.append({parameter, other, value});
    }
    return changes;
}

bool needsResync(uint16_t parameter)
{
    return parameter == id("ChPairOnf");
}

}
