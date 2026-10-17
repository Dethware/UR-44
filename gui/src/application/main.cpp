#include <QCommandLineParser>
#include <QDialog>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>

#include <unistd.h>

#include "AboutDialog.h"
#include "EffectEditors.h"

#include "MainWindow.h"

#include "Application.h"

int main(int argc, char *argv[])
{
    Application app(argc, argv);

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption screenshot("screenshot", "Save a screenshot of the window to <file> once the device state is "
                                                "loaded (or after 5 s), then quit.", "file");
    QCommandLineOption tab("tab", "Show tab <index> first.", "index", "0");
    QCommandLineOption editor("editor", "Open an effect editor once the device state is loaded: reverb, amp, or "
                                        "strip:<channel 1-6>, or about (the About dialog). A screenshot then shows "
                                        "it.", "name");
    QCommandLineOption background("background", "Start without a window (as at login): it opens from the tray icon, "
                                                "or by starting UR44 Control again.");
    parser.addOption(background);
    parser.addOption(screenshot);
    parser.addOption(tab);
    parser.addOption(editor);
    parser.process(app);

    // One instance per user: the device's MIDI port can only be open once. Starting it again (say, from the menu
    // while it sits in the tray) shows the running one's window instead. Screenshots (for development) run alone.
    const QString instanceName = QStringLiteral(UR44_APP_ID "-%1").arg(getuid());
    QLocalServer instanceServer;
    if (!parser.isSet(screenshot))
    {
        QLocalSocket running;
        running.connectToServer(instanceName);
        if (running.waitForConnected(500))
        {
            running.write("show\n");
            running.waitForBytesWritten(500);
            return 0;
        }
        QLocalServer::removeServer(instanceName); // a stale socket from an instance that crashed
        instanceServer.listen(instanceName);
    }

    // The window decides when to quit: with the tray icon, closing it may only hide it
    app.setQuitOnLastWindowClosed(false);

    MainWindow w;
    w.setRememberGeometry(!parser.isSet(screenshot));
    w.showTab(parser.value(tab).toInt());
    QObject::connect(&instanceServer, &QLocalServer::newConnection, &w, [&w, &instanceServer]() {
        while (QLocalSocket* other = instanceServer.nextPendingConnection())
        {
            QObject::connect(other, &QLocalSocket::disconnected, other, &QObject::deleteLater);
            w.showWindow();
        }
    });
    if (parser.isSet(screenshot) || (!parser.isSet(background) && !MainWindow::startsHidden()))
    {
        w.show();
    }

    if (parser.isSet(editor))
    {
        const QString name = parser.value(editor);
        QObject::connect(&w, &MainWindow::deviceLoaded, &w, [&w, name]() {
            if (name == "reverb")
            {
                EffectEditors::openReverb(w.device(), &w);
            }
            else if (name == "amp")
            {
                EffectEditors::openGuitarAmp(w.device(), &w);
            }
            else if (name == "about")
            {
                AboutDialog::show(&w);
            }
            else if (name.startsWith("strip:"))
            {
                EffectEditors::openChannelStrip(w.device(), uint16_t(name.mid(6).toInt() - 1), &w);
            }
        });
    }

    if (parser.isSet(screenshot))
    {
        const QString file = parser.value(screenshot);
        auto save = [&w, file]() {
            // let pending layout and paint events settle first
            QTimer::singleShot(300, &w, [&w, file]() {
                // an open editor, if any, else the main window
                QWidget* target = &w;
                for (QWidget* top : QApplication::topLevelWidgets())
                {
                    if (qobject_cast<QDialog*>(top) && top->isVisible())
                    {
                        target = top;
                    }
                }
                bool ok = target->grab().save(file);
                qDebug() << (ok ? "Saved screenshot to" : "Couldn't save screenshot to") << file;
                QCoreApplication::exit(ok ? 0 : 1);
            });
        };
        QObject::connect(&w, &MainWindow::deviceLoaded, &w, save);
        QTimer::singleShot(5000, &w, save);
    }

    return app.exec();
}
