#pragma once

#include <QString>

/*
 * Starting at login, through the freedesktop autostart standard: a desktop entry in $XDG_CONFIG_HOME/autostart
 * (usually ~/.config/autostart), named after the application ID. The entry itself is the setting: there's nothing
 * else to keep in step with it, and desktops' own autostart settings show and change the same file.
 */
namespace Autostart
{
/// ~/.config/autostart/<app id>.desktop
QString entryPath();
bool isEnabled();
/// Creates or removes the entry; it starts this executable (the absolute path of the running one) with --background,
/// so a login never opens the window
bool setEnabled(bool enabled, QString* error = nullptr);
}
