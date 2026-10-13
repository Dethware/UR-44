#pragma once
#include <QMainWindow>

#include <memory>

namespace Ui {
class MainWindow;
}

namespace UR44
{
class Device;
}

class MixPage;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void updateDeviceStatus();
    /// Points the device's DAW / master meters at the visible mix
    void pointMetersAtVisibleMix();
    void showMeters(const QList<uint32_t>& frame);

private:
    const std::unique_ptr<Ui::MainWindow> m_ui;
    UR44::Device* m_device = nullptr;
    QList<MixPage*> m_mixPages;

    // Device status, in the status bar
    class QLabel* m_deviceStatusLabel = nullptr;
};
