#include "Autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

namespace Autostart
{

namespace
{
/// A desktop entry Exec argument: quoted, with the characters the specification reserves inside quotes escaped
QString quoteExec(const QString& argument)
{
    QString out;
    for (QChar c : argument)
    {
        if (c == '"' || c == '`' || c == '$' || c == '\\')
        {
            out += '\\';
        }
        out += c;
    }
    return '"' + out + '"';
}
}

QString entryPath()
{
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    return QDir(config).filePath(QStringLiteral("autostart/" UR44_APP_ID ".desktop"));
}

bool isEnabled()
{
    return QFileInfo::exists(entryPath());
}

bool setEnabled(bool enabled, QString* error)
{
    const QString path = entryPath();
    if (!enabled)
    {
        if (QFileInfo::exists(path) && !QFile::remove(path))
        {
            if (error)
            {
                *error = QCoreApplication::translate("Autostart", "Couldn't remove %1").arg(path);
            }
            return false;
        }
        return true;
    }
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
    {
        if (error)
        {
            *error = QCoreApplication::translate("Autostart", "Couldn't create %1").arg(QFileInfo(path).absolutePath());
        }
        return false;
    }
    const QString entry = QStringLiteral("[Desktop Entry]\n"
                                         "Type=Application\n"
                                         "Name=UR44 Control\n"
                                         "Comment=Configure the DSP mixer and effects of the Steinberg UR44\n"
                                         "Exec=%1 --background\n"
                                         "Icon=" UR44_APP_ID "\n"
                                         "Terminal=false\n"
                                         "X-GNOME-Autostart-enabled=true\n")
                              .arg(quoteExec(QCoreApplication::applicationFilePath()));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(entry.toUtf8()) < 0 || !file.commit())
    {
        if (error)
        {
            *error = QCoreApplication::translate("Autostart", "Couldn't write %1: %2").arg(path, file.errorString());
        }
        return false;
    }
    return true;
}

}
