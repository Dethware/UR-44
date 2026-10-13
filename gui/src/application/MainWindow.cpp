#include <QLabel>
#include <QTabWidget>

#include "AlsaTransport.h"
#include "Application.h"
#include "Device.h"
#include "DevicePage.h"
#include "MixPage.h"

#include "MainWindow.h"
#include "ui_MainWindow.h"

using UR44::Device;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_ui(new Ui::MainWindow)
{
    m_ui->setupUi(this);

    m_device = new Device(new UR44::AlsaTransport, APPLICATION->settings.midiPort, this);

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
    m_ui->tabs->setTabToolTip(0, tr("MAIN OUTPUT, LINE OUTPUT 1/2, PHONES 1"));
    m_ui->tabs->setTabToolTip(1, tr("LINE OUTPUT 3/4, and PHONES 2 if selected"));

    m_deviceStatusLabel = new QLabel(this);
    m_ui->statusBar->addWidget(m_deviceStatusLabel, 1);

    connect(m_device, &Device::stateChanged, this, &MainWindow::updateDeviceStatus);
    connect(m_device, &Device::stateLoaded, this, &MainWindow::pointMetersAtVisibleMix);
    connect(m_device, &Device::meterFrame, this, &MainWindow::showMeters);
    connect(m_ui->tabs, &QTabWidget::currentChanged, this, &MainWindow::pointMetersAtVisibleMix);

    updateDeviceStatus();
    m_device->start();
}

MainWindow::~MainWindow()
{
    // The device would otherwise be destroyed last, after the pages and status bar it notifies
    delete m_device;
    m_device = nullptr;
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    m_device->setMetersEnabled(true);
}

void MainWindow::hideEvent(QHideEvent* event)
{
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
    if (!m_device->isReady())
    {
        for (auto* page : std::as_const(m_mixPages))
        {
            page->clearMeters();
        }
    }
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
