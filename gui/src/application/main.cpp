#include <QCommandLineParser>
#include <QTimer>

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
    parser.addOption(screenshot);
    parser.addOption(tab);
    parser.process(app);

    MainWindow w;
    w.showTab(parser.value(tab).toInt());
    w.show();

    if (parser.isSet(screenshot))
    {
        const QString file = parser.value(screenshot);
        auto save = [&w, file]() {
            // let pending layout and paint events settle first
            QTimer::singleShot(300, &w, [&w, file]() {
                bool ok = w.grab().save(file);
                qDebug() << (ok ? "Saved screenshot to" : "Couldn't save screenshot to") << file;
                QCoreApplication::exit(ok ? 0 : 1);
            });
        };
        QObject::connect(&w, &MainWindow::deviceLoaded, &w, save);
        QTimer::singleShot(5000, &w, save);
    }

    return app.exec();
}
