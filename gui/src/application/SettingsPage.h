#pragma once

#include <QWidget>

class QCheckBox;
class QLabel;

/*
 * The application's own options (not the device's): starting at login and starting without a window. The tray icon
 * is always there (when the desktop has a system tray), and closing the window hides it to the tray.
 */
class SettingsPage : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsPage(QWidget* parent = nullptr);

private:
    QCheckBox* m_autostart = nullptr;
    QCheckBox* m_startHidden = nullptr;
};
