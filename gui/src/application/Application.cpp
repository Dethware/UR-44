#include "Application.h"

#include <iostream>

#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QStringList>
#include <QDebug>
#include <QStandardPaths>

#include "FileSystem.h"
#include "CustomMessageBox.h"

#if defined Q_OS_WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdio.h>
#endif

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

namespace {
void appDebugOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    const char *levels = "DWCFIS";
    const QString format("%1 %2 %3\n");

    qint64 msecstotal = APPLICATION->timeSinceStart();
    qint64 seconds = msecstotal / 1000;
    qint64 msecs = msecstotal % 1000;
    QString foo;
    char buf[1025] = {0};
    ::snprintf(buf, 1024, "%5lld.%03lld", seconds, msecs);

    QString out = format.arg(buf).arg(levels[type]).arg(msg);

    APPLICATION->logFile->write(out.toUtf8());
    APPLICATION->logFile->flush();
    QTextStream(stderr) << out.toLocal8Bit();
    fflush(stderr);
}
}

Application::Application(int &argc, char **argv) : QApplication(argc, argv)
{
#if defined Q_OS_WIN32
    // attach the parent console
    if(AttachConsole(ATTACH_PARENT_PROCESS))
    {
        // if attach succeeds, reopen and sync all the i/o
        if(freopen("CON", "w", stdout))
        {
            std::cout.sync_with_stdio();
        }
        if(freopen("CON", "w", stderr))
        {
            std::cerr.sync_with_stdio();
        }
        if(freopen("CON", "r", stdin))
        {
            std::cin.sync_with_stdio();
        }
        auto out = GetStdHandle (STD_OUTPUT_HANDLE);
        DWORD written;
        const char * endline = "\n";
        WriteConsole(out, endline, strlen(endline), &written, NULL);
        consoleAttached = true;
    }
#endif
    setOrganizationName("Dethware");
    setOrganizationDomain("dethware.org");
    setApplicationName("UR44Control");
    setApplicationDisplayName("UR44 Control");
    setApplicationVersion("0.1.0");

    startTime = QDateTime::currentDateTime();

    QString configPath;
    QStringList configLocations = QStandardPaths::standardLocations(QStandardPaths::AppConfigLocation);
    if (configLocations.isEmpty()) {
        qWarning() << "Could not find a standard configuration path!";
        // Fallback: use the current working directory or a hardcoded path
        // This is not ideal, but it's better than crashing!
        configDir = QDir::current();
        configPath = configDir.filePath("config.json");
    } else {
        // Use the first available path
        configDir = QDir(configLocations.first());
        configPath = configDir.filePath("config.json");
    }
    FS::ensureFolderPathExists(configDir.absolutePath());

    settings.setPath(configPath);
    if (QFileInfo::exists(configPath))
    {
        settings.load();
    }

    QString binPath = applicationDirPath();

    QStringList localDataLocations = QStandardPaths::standardLocations(QStandardPaths::AppLocalDataLocation);
    if (localDataLocations.isEmpty()) {
        qWarning() << "Could not find a standard local data path!";
        // Fallback: use the current working directory or a hardcoded path
        // This is not ideal, but it's better than crashing!
        localDataDir = QDir::current();
    } else {
        // Use the first available path
        localDataDir = QDir(localDataLocations.first());
    }
    FS::ensureFolderPathExists(localDataDir.absolutePath());

    QStringList dataLocations = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation);
    if (dataLocations.isEmpty()) {
        qWarning() << "Could not find a standard data path!";
        // Fallback: use the current working directory or a hardcoded path
        // This is not ideal, but it's better than crashing!
        dataDir = QDir::current();
    } else {
        // Use the first available path
        dataDir = QDir(dataLocations.first());
    }
    FS::ensureFolderPathExists(dataDir.absolutePath());

    // init the logger
    {
        static const QString logBase = localDataDir.filePath("UR44Control-%0.log");
        auto moveFile = [](const QString &oldName, const QString &newName)
        {
            QFile::remove(newName);
            QFile::copy(oldName, newName);
            QFile::remove(oldName);
        };

        moveFile(logBase.arg(3), logBase.arg(4));
        moveFile(logBase.arg(2), logBase.arg(3));
        moveFile(logBase.arg(1), logBase.arg(2));
        moveFile(logBase.arg(0), logBase.arg(1));

        logFile = std::unique_ptr<QFile>(new QFile(logBase.arg(0)));
        if(!logFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        {
            showFatalErrorMessage(
                "The local data folder is not writable!",
                QString(
                    "Couldn't create a log file - the data folder is not writable.\n"
                    "\n"
                    "Make sure you have write permissions to the local data folder.\n"
                    "(%1)"
                ).arg(localDataDir.absolutePath())
            );
            return;
        }
        qInstallMessageHandler(appDebugOutput);
        qDebug() << "<> Log initialized.";
    }

    // Set up paths
    {
        qDebug() << applicationDisplayName() << ", (c) 2026-2026 " << organizationName();
        qDebug() << "Version                    : " << applicationVersion();
        qDebug() << "Work path                  : " << QDir::currentPath();
        qDebug() << "Config path                : " << configDir.absolutePath();
        qDebug() << "Data path                  : " << dataDir.absolutePath();
        qDebug() << "Local data                 : " << localDataDir.absolutePath();
        qDebug() << "Binary path                : " << binPath;
        qDebug() << "<> Paths set.";
    }

    connect(this, &Application::aboutToQuit, [this](){
        settings.flush();
        if(logFile)
        {
            logFile->flush();
            logFile->close();
        }
    });
}


void Application::showFatalErrorMessage(const QString& title, const QString& content)
{
    auto dialog = CustomMessageBox::selectable(nullptr, title, content, QMessageBox::Critical);
    dialog->exec();
}

Application::~Application()
{
    // Shut down logger by setting the logger function to nothing
    qInstallMessageHandler(nullptr);

#if defined Q_OS_WIN32
    // Detach from Windows console
    if(consoleAttached)
    {
        fclose(stdout);
        fclose(stdin);
        fclose(stderr);
        FreeConsole();
    }
#endif
}
