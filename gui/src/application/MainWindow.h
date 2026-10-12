#pragma once
#include <QMainWindow>

#include <memory>

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    const std::unique_ptr<Ui::MainWindow> m_ui;

    // Device status, in the status bar
    class QLabel* m_deviceStatusLabel = nullptr;
};
