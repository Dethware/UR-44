#pragma once

#include <QString>

#include <cstdint>
#include <functional>

class QAbstractButton;
class QAbstractSlider;
class QComboBox;
class QLabel;
class QWidget;

namespace UR44
{
class Device;
}

/*
 * Two-way bindings between widgets and one parameter instance on the device.
 *
 * The widget follows the device (on load and on every change, from any source) and user changes go to the
 * device. Bound widgets are disabled while the device isn't ready. The widget is the connections' context, so
 * they go away with it.
 */
namespace Bindings
{

/// What a user change does; by default the parameter is set on the device. A custom setter is for parameters with
/// relations the host has to implement (Effects.h), such as FxMorphValue.
using Setter = std::function<void(int32_t value)>;

/// A slider (fader, knob) over the parameter's range. Double-click resets it to the parameter's default.
/// `valueLabel` (optional) shows the compact value ("0.00", "-∞", "C"), or with `withUnit` the full text
/// ("32 Hz"). The tooltip has the range and the current value, and what describe() adds.
void slider(QAbstractSlider* slider, UR44::Device* device, const QString& parameter, uint16_t index,
            QLabel* valueLabel = nullptr, Setter setter = {}, bool withUnit = false);

/// Formats a value for a tooltip, for sliders whose value text isn't the parameter's own (the reverb time)
using Format = std::function<QString(int32_t value)>;

/// What a bound slider does, for its tooltip: `title` ("REV-X Send for IN 1") and `description` above the range,
/// the current value and the double-click default. `format` replaces the parameter's value text.
/// Can be called before or after slider().
void describe(QAbstractSlider* slider, const QString& title, const QString& description, Format format = {});

/// A checkable button for an on/off parameter (0/1). `inverted`: checked means 0 (a mute button for an "on"
/// parameter). If `onText` / `offText` are given, the text follows the state.
void toggle(QAbstractButton* button, UR44::Device* device, const QString& parameter, uint16_t index,
            bool inverted = false, const QString& onText = QString(), const QString& offText = QString());

/// A combo box listing the parameter's labels
void combo(QComboBox* combo, UR44::Device* device, const QString& parameter, uint16_t index, Setter setter = {});

/// A read-only label showing the value's text. `format` may contain %1 for it.
void label(QLabel* label, UR44::Device* device, const QString& parameter, uint16_t index,
           const QString& format = "%1");

/// Calls `update(value)` on load and on every change of the instance, for as long as `context` lives. Unlike the
/// bindings above, it doesn't touch the context widget's enabled state.
void follow(QWidget* context, UR44::Device* device, const QString& parameter, uint16_t index,
            std::function<void(int32_t)> update);

}
