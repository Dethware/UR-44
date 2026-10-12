#include "MainWindow.h"

#include "Application.h"

int main(int argc, char *argv[])
{
    Application app(argc, argv);

    MainWindow w;
    w.show();

    return app.exec();
}
