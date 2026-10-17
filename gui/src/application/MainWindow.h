#pragma once
#include <QIcon>
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

    /// Shows the window and brings it to the front (from the tray, or when started a second time)
    void showWindow();
    /// Whether the window's geometry is saved to the settings (not for development screenshots)
    void setRememberGeometry(bool remember) { m_rememberGeometry = remember; }
    /// Whether the window should stay hidden at startup ("Start without a window")
    static bool startsHidden();
    /// The application icon: the installed theme icon, or the built-in one
    static QIcon appIcon();

signals:
    /// The device state was loaded (and the pages show it)
    void deviceLoaded();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    /// PHONES 2 source: MIX 1 or MIX 2
    QWidget* makePhones2Selector(QWidget* parent);
    void updateDeviceStatus();
    /// The status bar's activity section: idle, or writing to the device
    void updateActivityStatus();
    /// Points the device's DAW / master meters at the visible mix
    void pointMetersAtVisibleMix();
    void showMeters(const QList<uint32_t>& frame);
    /// Creates the tray icon, once the desktop has a system tray (checking again for a while if it hasn't yet)
    void updateTray();
    void toggleWindow();
    void quit();
    /// Stores the window's size, position and state in the settings
    void saveWindowGeometry();

private:
    const std::unique_ptr<Ui::MainWindow> m_ui;
    UR44::Device* m_device = nullptr;
    QList<MixPage*> m_mixPages;

    // Device status, in the status bar
    class QLabel* m_deviceStatusLabel = nullptr;
    class QLabel* m_activityLabel = nullptr;

    class QSystemTrayIcon* m_tray = nullptr;
    class QAction* m_showAction = nullptr;
    /// Checks for a system tray: every 2 s for a minute (at login the panel may come up after us)
    static constexpr int TRAY_CHECK_MS = 2000;
    static constexpr int TRAY_CHECKS = 30;
    int m_trayChecks = 0;
    /// Quitting for real (from the tray menu): closing doesn't hide to the tray then
    bool m_quitting = false;
    /// The window has been on screen (so its geometry is its own, not just the default)
    bool m_wasShown = false;
    bool m_rememberGeometry = true;
};
