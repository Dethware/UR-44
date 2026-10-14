#pragma once

#include <array>
#include <cstdint>

/*
 * The parameter relations dspMixFx implements on the host side (macOS MemAPI smdSpecialRelationTbl), for the
 * effects. The device doesn't do these itself, so a client changing these parameters has to.
 */
namespace UR44
{
class Device;

namespace Effects
{

// --- Sweet Spot Morphing Channel Strip (smd_SetPrmRelMorph) ------------------------------------------------------

/// The 17 compressor/EQ values a morph table point holds, in the order of the Morph* parameters (0x53..0x63)
/// and of their Fx* counterparts.
using MorphPoint = std::array<int32_t, 17>;
/// The Fx* parameters (instance = strip) that FxMorphValue drives, in MorphPoint order
extern const char* const MORPHED_PARAMETERS[17];
/// The Morph* table parameters (instance = strip * 9 + point), in MorphPoint order
extern const char* const MORPH_TABLE_PARAMETERS[17];
constexpr int MORPH_POINTS = 9;
/// FxMorphValue (0..120) per morph table point
constexpr double MORPH_STEP = 15.0;

/// The values for a morph position: linear interpolation between points value/15 and value/15 + 1, truncated and
/// clamped to each parameter's range like the original
MorphPoint interpolate(const MorphPoint& from, const MorphPoint& to, int32_t morphValue);

/// Sets FxMorphValue[strip] and the interpolated compressor/EQ values from the strip's morph table.
/// Returns false if the device isn't ready or the table isn't known.
bool setMorph(Device& device, uint16_t strip, int32_t morphValue);

// --- REV-X (smd_SetPrmRelRevType) --------------------------------------------------------------------------------

/// Sets FxRevType and resets the reverb parameters to that type's defaults
bool setReverbType(Device& device, int32_t type);

// --- Guitar Amp Classics (smd_SetPrmRelGtrFxType) ------------------------------------------------------------------

/// Sets GtrFxType and resets the selected amp model's parameters to their defaults
bool setGuitarAmpType(Device& device, int32_t type);

}
}
