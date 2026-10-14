#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

#include "BulkDump.h"

/*
 * Scenes: the mixer and effect state dspMixFx stores and recalls (docs/PROTOCOL.md, "Scenes and presets").
 *
 * A scene is a "CurrentScene" payload: the state dump's format without the system and routing settings (sample rate,
 * PHONES 2 source, meter sources, loopback, solo flag, +48V, input gains). Recalling one is a type 1 bulk dump; the
 * device applies all of it, effect inserts included, and acknowledges with a Function 03 02 02 message.
 *
 * Payloads here are always in the device's form. dspMixFx stores them with little-endian chunk headers; normalize()
 * and toHostOrder() convert.
 */
namespace UR44
{

struct Scene
{
    QString name;
    /// The CurrentScene payload, device byte order
    QByteArray data;
};

namespace Scenes
{

/// Whether a state dump item belongs to a scene (everything but the system and routing settings)
bool isSceneItem(uint16_t id);

/// The scene's format: the state dump's groups without the items that aren't part of a scene
QList<BulkDump::Group> format(const QList<BulkDump::Group>& stateFormat);

/// Checks that `data` is a scene and returns it in device byte order (converting dspMixFx's host order)
std::optional<QByteArray> normalize(const QByteArray& data, QString* error = nullptr);

/// The payload with dspMixFx's little-endian chunk headers
QByteArray toHostOrder(const QByteArray& deviceData);

/// The factory default mixer state, in `format` (from scene format()): every parameter at its default, the
/// morph tables "01 Basic" with the strips following them at their default MORPH position, no effect inserted
QByteArray defaults(const QList<BulkDump::Group>& format);

// --- dspMixFx scene files (*.UR44) --------------------------------------------------------------------------------

struct File
{
    /// The stored scenes, by slot (up to 20)
    QList<Scene> scenes;
    /// The state dspMixFx had when it saved the file (CurrentData), if any
    std::optional<QByteArray> current;
};

constexpr int FILE_SLOTS = 20;

std::optional<File> readFile(const QByteArray& xml, QString* error = nullptr);

/// Writes up to FILE_SLOTS scenes (the rest are left out), and `current` as the file's current state.
/// dspMixFx's UI memory (MuteStatus, FxInsPosStatus, ChFxTypeStatus) isn't written: it's optional in the format.
QByteArray writeFile(const QList<Scene>& scenes, const QByteArray& current);

}
}
