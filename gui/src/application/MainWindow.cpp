#include <QLabel>

#include "MainWindow.h"
#include "ui_MainWindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_ui(new Ui::MainWindow)
{
    m_ui->setupUi(this);

    m_deviceStatusLabel = new QLabel(tr("Device: not connected"), this);
    m_ui->statusBar->addWidget(m_deviceStatusLabel, 1);
}

MainWindow::~MainWindow() = default;

#include "moc_MainWindow.cpp"
