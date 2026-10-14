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

    void showTab(int index);
    UR44::Device* device() const { return m_device; }

signals:
    /// The device state was loaded (and the pages show it)
    void deviceLoaded();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void updateDeviceStatus();
    /// The status bar's activity section: idle, or writing to the device
    void updateActivityStatus();
    /// Points the device's DAW / master meters at the visible mix
    void pointMetersAtVisibleMix();
    void showMeters(const QList<uint32_t>& frame);

private:
    const std::unique_ptr<Ui::MainWindow> m_ui;
    UR44::Device* m_device = nullptr;
    QList<MixPage*> m_mixPages;

    // Device status, in the status bar
    class QLabel* m_deviceStatusLabel = nullptr;
    class QLabel* m_activityLabel = nullptr;
};
