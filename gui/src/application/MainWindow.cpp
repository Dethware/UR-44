#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>

#include "AboutDialog.h"
#include "AlsaTransport.h"
#include "Application.h"
#include "Bindings.h"
#include "Device.h"
#include "DevicePage.h"
#include "MixPage.h"
#include "ScenesPanel.h"
#include "SettingsPage.h"
#include "Widgets.h"

#include "MainWindow.h"
#include "ui_MainWindow.h"

using UR44::Device;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_ui(new Ui::MainWindow)
{
    m_ui->setupUi(this);

    m_device = new Device(new UR44::AlsaTransport, APPLICATION->settings.ur44.midiPort, this);

    for (int bus : {1, 2})
    {
        auto* page = new MixPage(bus, m_device, m_ui->tabs);
        m_mixPages.append(page);
        m_ui->tabs->addTab(page, tr("MIX %1").arg(bus));
        // Show solos on the tab too, so they're noticed from the other pages
        connect(page, &MixPage::soloActiveChanged, this, [this, page, bus](bool active) {
            int index = m_ui->tabs->indexOf(page);
            m_ui->tabs->setTabText(index, active ? tr("MIX %1 (SOLO)").arg(bus) : tr("MIX %1").arg(bus));
        });
    }
    m_ui->tabs->addTab(new DevicePage(m_device, m_ui->tabs), tr("Device"));
    auto* settingsPage = new SettingsPage(m_ui->tabs);
    m_ui->tabs->addTab(settingsPage, tr("Settings"));
    m_ui->tabs->setTabToolTip(m_ui->tabs->count() - 1, tr("UR44 Control's own options: startup and the tray"));
    setWindowIcon(appIcon());
    m_ui->tabs->setTabToolTip(0, tr("MAIN OUTPUT, LINE OUTPUT 1/2, PHONES 1"));
    m_ui->tabs->setTabToolTip(1, tr("LINE OUTPUT 3/4, and PHONES 2 if selected"));

    // no menu bar: PHONES 2 and About sit in the free space next to the tabs
    auto* corner = new QWidget(m_ui->tabs);
    auto* cornerLayout = new QHBoxLayout(corner);
    cornerLayout->setContentsMargins(0, 0, 4, 0);
    cornerLayout->setSpacing(4);
    cornerLayout->addWidget(makePhones2Selector(corner));
    cornerLayout->addSpacing(12);
    auto* about = new QToolButton(corner);
    about->setText(tr("About"));
    about->setAutoRaise(true);
    about->setToolTip(tr("Version, license and credits"));
    connect(about, &QToolButton::clicked, this, [this]() { AboutDialog::show(this); });
    cornerLayout->addWidget(about);
    m_ui->tabs->setCornerWidget(corner, Qt::TopRightCorner);

    // prepared scenes, next to the pages
    auto* scenes = new ScenesPanel(m_device, m_ui->centralWidget);
    scenes->setFixedWidth(190);
    m_ui->gridLayout->addWidget(scenes, 0, 1);

    m_deviceStatusLabel = new QLabel(this);
    m_ui->statusBar->addWidget(m_deviceStatusLabel, 1);
    m_activityLabel = new QLabel(this);
    m_activityLabel->setToolTip(tr("Scenes and Sweet Spot Data take the device about 0.3 s each to apply; "
                                   "changes made meanwhile are applied after them"));
    m_ui->statusBar->addPermanentWidget(m_activityLabel);

    connect(m_device, &Device::stateChanged, this, &MainWindow::updateDeviceStatus);
    connect(m_device, &Device::lastErrorChanged, this, &MainWindow::updateDeviceStatus);
    connect(m_device, &Device::busyChanged, this, &MainWindow::updateActivityStatus);
    connect(m_device, &Device::stateChanged, this, &MainWindow::updateActivityStatus);
    connect(m_device, &Device::stateLoaded, this, &MainWindow::pointMetersAtVisibleMix);
    connect(m_device, &Device::stateLoaded, this, &MainWindow::deviceLoaded);
    connect(m_device, &Device::meterFrame, this, &MainWindow::showMeters);
    connect(m_ui->tabs, &QTabWidget::currentChanged, this, &MainWindow::pointMetersAtVisibleMix);

    // where it was last time (before the window is first shown; with Wayland, the compositor places it)
    const QByteArray geometry = QByteArray::fromBase64(APPLICATION->settings.windowGeometry.toLatin1());
    if (!geometry.isEmpty())
    {
        restoreGeometry(geometry);
    }

    updateDeviceStatus();
    updateActivityStatus();
    updateTray();
    m_device->start();
}

MainWindow::~MainWindow()
{
    saveWindowGeometry();
    // The device would otherwise be destroyed last, after the pages and status bar it notifies
    delete m_device;
    m_device = nullptr;
}

QWidget* MainWindow::makePhones2Selector(QWidget* parent)
{
    // PHONES 1 always plays MIX 1; PHONES 2 plays either mix (PatchAuxPhoneOutput: 0 = MIX 1, 1 = MIX 2)
    auto* box = new QWidget(parent);
    auto* layout = new QHBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(Widgets::caption(tr("PHONES 2"), box));
    layout->addSpacing(4);
    Device* device = m_device;
    QList<QToolButton*> buttons;
    for (int bus : {1, 2})
    {
        auto* b = Widgets::textButton(tr("MIX %1").arg(bus), QColor(70, 200, 90),
                                      tr("PHONES 2 plays MIX %1").arg(bus), box);
        b->setFixedWidth(48);
        layout->addWidget(b);
        buttons.append(b);
    }
    auto refresh = [buttons, device]() {
        const auto source = device->value("PatchAuxPhoneOutput");
        for (int i = 0; i < buttons.size(); ++i)
        {
            QSignalBlocker blocker(buttons[i]);
            buttons[i]->setChecked(source == i);
            buttons[i]->setEnabled(device->isReady());
        }
    };
    for (int i = 0; i < buttons.size(); ++i)
    {
        // like radio buttons: clicking the lit one keeps it lit
        connect(buttons[i], &QToolButton::clicked, box, [device, i, refresh]() {
            device->setParameter("PatchAuxPhoneOutput", 0, i);
            refresh();
        });
    }
    Bindings::follow(box, device, "PatchAuxPhoneOutput", 0, [refresh](int32_t) { refresh(); });
    connect(device, &Device::stateChanged, box, refresh);
    refresh();
    box->setToolTip(tr("What PHONES 2 plays. PHONES 1 always plays MIX 1."));
    return box;
}

QIcon MainWindow::appIcon()
{
    QIcon builtIn;
    for (int size : {16, 22, 24, 32, 48, 64, 128, 256})
    {
        builtIn.addFile(QString(":/icons/ur44control-%1.png").arg(size), QSize(size, size));
    }
    return QIcon::fromTheme(UR44_APP_ID, builtIn);
}

bool MainWindow::startsHidden()
{
    return APPLICATION->settings.startHidden;
}

void MainWindow::updateTray()
{
    if (m_tray)
    {
        return;
    }
    if (!QSystemTrayIcon::isSystemTrayAvailable())
    {
        // started at login, the desktop's panel (and its tray) may not be up yet: keep looking for a while
        if (m_trayChecks++ < TRAY_CHECKS)
        {
            QTimer::singleShot(TRAY_CHECK_MS, this, &MainWindow::updateTray);
        }
        return;
    }
    m_tray = new QSystemTrayIcon(appIcon(), this);
    auto* menu = new QMenu(this);
    m_showAction = menu->addAction(tr("Show UR44 Control"), this, &MainWindow::toggleWindow);
    menu->addSeparator();
    menu->addAction(tr("Quit"), this, &MainWindow::quit);
    connect(menu, &QMenu::aboutToShow, this, [this]() {
        m_showAction->setText(isVisible() ? tr("Hide UR44 Control") : tr("Show UR44 Control"));
    });
    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger)
        {
            toggleWindow();
        }
    });
    updateDeviceStatus(); // the tooltip
    m_tray->show();
}

void MainWindow::showWindow()
{
    if (isMinimized())
    {
        showNormal();
    }
    show();
    raise();
    activateWindow();
}

void MainWindow::toggleWindow()
{
    if (isVisible())
    {
        hide();
    }
    else
    {
        showWindow();
    }
}

void MainWindow::quit()
{
    saveWindowGeometry();
    m_quitting = true;
    QApplication::quit();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!m_quitting && m_tray)
    {
        // keep running in the tray
        hide();
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
    m_quitting = true;
    QApplication::quit();
}

void MainWindow::showTab(int index)
{
    if (index >= 0 && index < m_ui->tabs->count())
    {
        m_ui->tabs->setCurrentIndex(index);
    }
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    m_wasShown = true;
    m_device->setMetersEnabled(true);
}

void MainWindow::saveWindowGeometry()
{
    // only once the window has been shown: a window started in the tray has no geometry of its own yet
    if (!m_wasShown || !m_rememberGeometry)
    {
        return;
    }
    const QString geometry = QString::fromLatin1(saveGeometry().toBase64());
    if (geometry != APPLICATION->settings.windowGeometry)
    {
        APPLICATION->settings.windowGeometry = geometry;
        APPLICATION->settings.scheduleSave();
    }
}

void MainWindow::hideEvent(QHideEvent* event)
{
    // closing to the tray hides the window: keep where it was
    saveWindowGeometry();
    QMainWindow::hideEvent(event);
    if (m_device)
    {
        m_device->setMetersEnabled(false);
    }
}

void MainWindow::updateDeviceStatus()
{
    QString text;
    switch (m_device->state())
    {
        case Device::State::Disconnected:
            text = tr("Not connected: %1 (retrying)").arg(m_device->lastError());
            break;
        case Device::State::Connecting:
            text = tr("Connecting to %1...").arg(m_device->port());
            break;
        case Device::State::Loading:
            text = tr("Loading the device state...");
            break;
        case Device::State::Ready:
            text = tr("Connected to UR44 on %1 (%2)").arg(m_device->port(), m_device->firmwareVersion());
            break;
    }
    m_deviceStatusLabel->setText(text);
    if (m_tray)
    {
        m_tray->setToolTip(tr("UR44 Control\n%1").arg(text));
    }
    if (!m_device->isReady())
    {
        for (auto* page : std::as_const(m_mixPages))
        {
            page->clearMeters();
        }
    }
}

void MainWindow::updateActivityStatus()
{
    // what the device is doing with what we sent: only shown while connected
    const int pending = m_device->pendingWrites();
    QString text;
    if (m_device->isReady())
    {
        text = pending == 0   ? tr("Ready")
               : pending == 1 ? tr("Writing...")
                              : tr("Writing... (%1 queued)").arg(pending - 1);
    }
    m_activityLabel->setText(text);
}

void MainWindow::pointMetersAtVisibleMix()
{
    auto* page = qobject_cast<MixPage*>(m_ui->tabs->currentWidget());
    if (!page || !m_device->isReady())
    {
        return;
    }
    int source = page->bus() - 1; // MIXBUS 1/2, DAW 1/2 / 3/4
    for (const char* name : {"PatchMasterMeter", "PatchDawMeter"})
    {
        if (m_device->value(name) != source)
        {
            m_device->setParameter(name, 0, source);
        }
    }
}

void MainWindow::showMeters(const QList<uint32_t>& frame)
{
    if (auto* page = qobject_cast<MixPage*>(m_ui->tabs->currentWidget()))
    {
        page->showMeters(frame);
    }
}

#include "moc_MainWindow.cpp"
