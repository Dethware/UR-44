#pragma once

class QWidget;

/*
 * The About dialog: version, license, and the credits for what this is built on. The UR44's protocol was reverse
 * engineered from Yamaha/Steinberg's TOOLS for UR44, and the built-in Sweet Spot presets are theirs; that origin is
 * stated here, not left implicit.
 */
namespace AboutDialog
{
void show(QWidget* parent);
}
