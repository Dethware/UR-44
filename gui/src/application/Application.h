#pragma once

#include <QApplication>
#include <memory>
#include <QDebug>
#include <QFlag>
#include <QIcon>
#include <QDateTime>
#include <QFile>
#include <QDir>

#include "Settings.h"

#if defined(APPLICATION)
#undef APPLICATION
#endif
#define APPLICATION (static_cast<Application *>(QCoreApplication::instance()))

class Application : public QApplication
{
    Q_OBJECT

public:
    Application(int &argc, char **argv);
    virtual ~Application();

    /// Where Sweet Spot Data presets are looked for (the setting, or <data dir>/sweet-spot)
    QString sweetSpotFolder() const {
        return settings.sweetSpotFolder.isEmpty() ? dataDir.filePath("sweet-spot") : settings.sweetSpotFolder;
    }

    qint64 timeSinceStart() const {
        return startTime.msecsTo(QDateTime::currentDateTime());
    }

private:
    // sets the fatal error message and m_status to Failed.
    void showFatalErrorMessage(const QString& title, const QString& content);

private:
    QDateTime startTime;

#if defined Q_OS_WIN32
    // used on Windows to attach the standard IO streams
    bool consoleAttached = false;
#endif

public:
    QDir configDir;
    QDir localDataDir;
    QDir dataDir;

    Settings settings;
    std::unique_ptr<QFile> logFile;
};

