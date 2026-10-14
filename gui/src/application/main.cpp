#include <QCommandLineParser>
#include <QDialog>
#include <QTimer>

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
                                        "strip:<channel 1-6>. A screenshot then shows the editor.", "name");
    parser.addOption(screenshot);
    parser.addOption(tab);
    parser.addOption(editor);
    parser.process(app);

    MainWindow w;
    w.showTab(parser.value(tab).toInt());
    w.show();

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
