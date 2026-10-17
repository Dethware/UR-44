#pragma once

#include <QColor>
#include <QString>

#include <cstdint>

class QLabel;
class QPainter;
class QRectF;
class QToolButton;
class QWidget;
class Knob;

namespace UR44
{
class Device;
}

/*
 * Small widgets in the dspMixFx style, shared by the mix pages and the effect editors.
 */
namespace Widgets
{
// dspMixFx colours: REV-X is light blue, pan green, the REV-X return orange
const QColor REVX_COLOR(64, 200, 232);
const QColor PAN_COLOR(70, 170, 70);
const QColor RETURN_COLOR(230, 130, 40);
const QColor STRIP_COLOR(230, 160, 50);
const QColor AMP_COLOR(200, 70, 60);

// The meters' and graphs' display colours: always dark, whatever the theme, for the contrast (like dspMixFx's)
inline QColor displayBackground(bool enabled)
{
    return enabled ? QColor(12, 13, 15) : QColor(24, 26, 28);
}
inline QColor displayGrid(bool major)
{
    return QColor(255, 255, 255, major ? 60 : 24);
}
inline QColor displayLabel()
{
    return QColor(255, 255, 255, 140);
}

/// A sunken display panel with rounded corners, filling `rect`: paints the background and clips the painter to it.
/// Returns the area to draw in. Finish with endDisplayPanel().
QRectF beginDisplayPanel(QPainter& p, const QRectF& rect, bool enabled);
/// The panel's sunken edge, over what was drawn; resets the clip
void endDisplayPanel(QPainter& p, const QRectF& rect);

/// A small square toggle, lit in `color` when checked
QToolButton* squareButton(const QString& text, const QColor& color, const QString& tooltip, QWidget* parent);
/// A wider text toggle in the same style (INS.FX, Ch.Strip, SEND, ON)
QToolButton* textButton(const QString& text, const QColor& color, const QString& tooltip, QWidget* parent);
/// A centred label in a slightly smaller font
QLabel* caption(const QString& text, QWidget* parent);

/*
 * A titled knob with its value (with unit) below, bound to a parameter. Returns the container; `knobOut` gets the
 * knob, e.g. to show a computed value instead.
 */
QWidget* knobControl(const QString& title, UR44::Device* device, const QString& parameter, uint16_t index,
                     const QColor& color, QWidget* parent, Knob** knobOut = nullptr, QLabel** valueOut = nullptr);

/// A knobControl() whose tooltip says what it does: `name` ("Compressor Attack") and `description`
QWidget* knobControl(const QString& title, const QString& name, const QString& description, UR44::Device* device,
                     const QString& parameter, uint16_t index, const QColor& color, QWidget* parent);
}
