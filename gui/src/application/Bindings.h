#pragma once

#include <QString>

#include <cstdint>

class QAbstractButton;
class QAbstractSlider;
class QComboBox;
class QLabel;

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

/// A slider over the parameter's range. `valueLabel` (optional) shows the value's text, e.g. "-6.00 dB".
void slider(QAbstractSlider* slider, UR44::Device* device, const QString& parameter, uint16_t index,
            QLabel* valueLabel = nullptr);

/// A checkable button for an on/off parameter (0/1). If `onText` / `offText` are given, the button's text follows
/// the state, so it always says what the state is (not what clicking does).
void toggle(QAbstractButton* button, UR44::Device* device, const QString& parameter, uint16_t index,
            const QString& onText = QString(), const QString& offText = QString());

/// A combo box listing the parameter's labels
void combo(QComboBox* combo, UR44::Device* device, const QString& parameter, uint16_t index);

/// A read-only label showing the value's text. `format` may contain %1 for it.
void label(QLabel* label, UR44::Device* device, const QString& parameter, uint16_t index,
           const QString& format = "%1");

}
