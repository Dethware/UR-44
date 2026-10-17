#include "SettingsPage.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QSystemTrayIcon>
#include <QVBoxLayout>

#include "Application.h"
#include "Autostart.h"

SettingsPage::SettingsPage(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    Settings& settings = APPLICATION->settings;

    auto* startup = new QGroupBox(tr("Startup"), this);
    auto* startupLayout = new QVBoxLayout(startup);
    m_autostart = new QCheckBox(tr("Start when I log in"), startup);
    m_autostart->setToolTip(tr("Adds UR44 Control to the desktop's autostart (%1). It starts in the background, "
                               "without a window.")
                                .arg(Autostart::entryPath()));
    m_autostart->setChecked(Autostart::isEnabled());
    connect(m_autostart, &QCheckBox::toggled, this, [this](bool on) {
        QString error;
        if (!Autostart::setEnabled(on, &error))
        {
            QMessageBox::warning(this, tr("Start when I log in"), error);
        }
        // the entry is the setting: show what's really there
        QSignalBlocker blocker(m_autostart);
        m_autostart->setChecked(Autostart::isEnabled());
    });
    startupLayout->addWidget(m_autostart);
    m_startHidden = new QCheckBox(tr("Start without a window (in the tray)"), startup);
    m_startHidden->setToolTip(tr("Also when started by hand; the window opens from the tray icon, or by starting "
                                 "UR44 Control again. (Started at login, it always starts without a window.)"));
    m_startHidden->setChecked(settings.startHidden);
    connect(m_startHidden, &QCheckBox::toggled, this, [](bool on) {
        APPLICATION->settings.startHidden = on;
        APPLICATION->settings.scheduleSave();
    });
    startupLayout->addWidget(m_startHidden);
    layout->addWidget(startup);

    // the window closes to the tray icon; without a system tray, closing it quits
    auto* note = new QLabel(QSystemTrayIcon::isSystemTrayAvailable()
                                ? tr("Closing the window keeps UR44 Control running in the system tray. Quit from "
                                     "the tray icon's menu.")
                                : tr("This desktop has no system tray (yet): closing the window quits UR44 Control."),
                            this);
    note->setWordWrap(true);
    note->setEnabled(false);
    layout->addWidget(note);

    layout->addStretch();
}
