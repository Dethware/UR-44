#pragma once

#include <QList>

#include <cstdint>
#include <functional>
#include <optional>

/*
 * Input pairing (ChPairOnf): inputs 1/2, 3/4 and 5/6 as stereo pairs. The firmware links a pair itself, but reports
 * none of it, so a client has to apply the same rules to its own copy of the state. These are those rules, as
 * tested on the device (docs/PROTOCOL.md "Input pairing"):
 *
 *   - ChPairOnf on either channel sets both. Pairing copies the left channel's settings to the right one (the linked
 *     parameters below, and the MIX levels while the fader link is on); phase and the pans stay per channel.
 *   - While paired, a change to a linked parameter of either channel is copied to the other. The MIX 1/2 levels are
 *     linked while ChFaderLinkOnf is on (itself linked); switching it on copies the left channel's levels.
 *   - A channel strip on a pair is a stereo strip: two strips (0/1 or 2/3) with FxStereoLinkOnf on, whose settings
 *     are mirrored (but not their Sweet Spot morph tables).
 *
 * Pairing also moves the strips around and takes the guitar amp off; those need a resync (needsResync()).
 */
namespace UR44::Pairing
{

struct Change
{
    uint16_t id;
    uint16_t index;
    int32_t value;
};

/// Looks up a value in the client's state
using Lookup = std::function<std::optional<int32_t>(uint16_t id, uint16_t index)>;

/// The other channel of `channel`'s pair (0..5), and the pair's left channel
constexpr uint16_t partner(uint16_t channel)
{
    return channel ^ 1;
}
constexpr uint16_t left(uint16_t channel)
{
    return channel & ~1;
}

/// Whether `channel` is paired
bool isPaired(const Lookup& state, uint16_t channel);
/// Whether channel strip `strip` (0..3) is half of a stereo strip
bool isStereoStrip(const Lookup& state, uint16_t strip);

/// The changes the firmware makes by itself when `id[index]` is set to `value`, with `state` as it was before the
/// set (the set itself isn't included)
QList<Change> followUps(const Lookup& state, uint16_t id, uint16_t index, int32_t value);

/// Whether setting `id` makes the device rearrange things a client can only learn from a state dump (the strips and
/// the guitar amp, when pairing or unpairing)
bool needsResync(uint16_t id);

}
