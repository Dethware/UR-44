#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QTimer>

/// A prepared scene: the mixer/effect state to recall (UR44::Scene), kept in the settings
struct StoredScene
{
    QString name;
    /// The scene payload (device byte order), base64
    QString data;

    QJsonObject extra;

    template<typename S> void serialize(S& fp)
    {
        fp.property("name", name);
        fp.property("data", data);
    }
};

/// Settings for the UR44 (the device support may grow other models with their own settings)
struct UR44Settings
{
    /// ALSA rawmidi port of the UR44's control interface (its second MIDI port, e.g. "hw:1,0,1").
    /// Empty means auto-detect.
    QString midiPort;

    /// Folder with Sweet Spot Data presets (.ssd, searched recursively). Empty means <data dir>/sweet-spot.
    QString sweetSpotFolder;

    /// Prepared scenes, in the order shown
    QList<StoredScene> scenes;

    QJsonObject extra;

    template<typename S> void serialize(S& fp)
    {
        fp.property("midi_port", midiPort);
        fp.property("sweet_spot_folder", sweetSpotFolder);
        fp.property("scenes", scenes);
    }
};

/*
 * Application settings, stored as JSON (see JsonSerialization.h). Device settings are grouped per model ("ur44").
 *
 * Change the members directly, then call scheduleSave(). Saving is throttled: the first change starts a timer, and
 * everything changed until it fires is written at once. This keeps the number of writes bounded even if values
 * change constantly (like window geometry while resizing). Pending changes are written on destruction.
 */
struct Settings
{
    static constexpr int SAVE_DELAY_MS = 1000;

    Settings();
    ~Settings();
    Settings(const Settings&) = delete;
    Settings& operator=(const Settings&) = delete;

    /// Sets the file used by load() and save()
    void setPath(const QString& path);

    /// Loads settings from the file. Missing or invalid values keep their defaults.
    /// Returns false (and leaves the settings untouched) if the file can't be read or parsed.
    bool load();

    /// Writes the settings to the file now.
    bool save();

    /// Writes the settings to the file within SAVE_DELAY_MS.
    void scheduleSave();

    /// Writes the settings now, if there's a scheduled save.
    void flush();

    /// Device settings, per model
    UR44Settings ur44;

    /// Start without showing the window (it opens from the tray icon). Starts at login always are.
    bool startHidden = false;
    /// The main window's size, position and state (QWidget::saveGeometry(), base64)
    QString windowGeometry;
    // (Starting at login isn't stored here: the autostart entry itself is the setting, see Autostart.h.)

    QJsonObject extra;

    template<typename S> void serialize(S& fp)
    {
        fp.property("ur44", ur44);
        fp.property("start_hidden", startHidden);
        fp.property("window_geometry", windowGeometry);
    }

private:
    QString m_path;
    QTimer m_saveTimer;
};
