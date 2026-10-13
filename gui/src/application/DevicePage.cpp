#include "DevicePage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

#include "Bindings.h"
#include "Device.h"

using UR44::Device;

DevicePage::DevicePage(Device* device, QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);

    auto* routing = new QGroupBox(tr("Routing"), this);
    auto* routingForm = new QFormLayout(routing);
    auto* phones2 = new QComboBox(routing);
    Bindings::combo(phones2, device, "PatchAuxPhoneOutput", 0);
    routingForm->addRow(tr("PHONES 2 source:"), phones2);
    auto* fxSend = new QComboBox(routing);
    Bindings::combo(fxSend, device, "PatchFxSend", 0);
    fxSend->setToolTip(tr("Which mix the channels' REV-X sends belong to"));
    routingForm->addRow(tr("REV-X send from:"), fxSend);
    auto* loopback = new QCheckBox(tr("Loopback"), routing);
    loopback->setToolTip(tr("Mix MIX 1 into the signal recorded by the computer (for streaming)"));
    Bindings::toggle(loopback, device, "PatchLoopbackMode", 0);
    routingForm->addRow(QString(), loopback);
    layout->addWidget(routing);

    auto* inputs = new QGroupBox(tr("Inputs"), this);
    auto* inputsForm = new QFormLayout(inputs);
    auto* line56 = new QComboBox(inputs);
    Bindings::combo(line56, device, "HaInputGain", 2);
    inputsForm->addRow(tr("LINE INPUT 5/6 level:"), line56);
    auto* phantom12 = new QLabel(inputs);
    Bindings::label(phantom12, device, "HaP48VOnf", 0);
    phantom12->setToolTip(tr("Set with the +48V switch on the front panel"));
    inputsForm->addRow(tr("+48V (MIC/HI-Z 1/2):"), phantom12);
    auto* phantom34 = new QLabel(inputs);
    Bindings::label(phantom34, device, "HaP48VOnf", 1);
    phantom34->setToolTip(tr("Set with the +48V switch on the front panel"));
    inputsForm->addRow(tr("+48V (MIC/LINE 3/4):"), phantom34);
    layout->addWidget(inputs);

    auto* status = new QGroupBox(tr("Device"), this);
    auto* statusForm = new QFormLayout(status);
    auto* rate = new QLabel(status);
    Bindings::label(rate, device, "SysFsCode", 0);
    rate->setToolTip(tr("Set by the computer's audio stream"));
    statusForm->addRow(tr("Sample rate:"), rate);
    auto* firmware = new QLabel(status);
    auto* port = new QLabel(status);
    statusForm->addRow(tr("Firmware:"), firmware);
    statusForm->addRow(tr("MIDI port:"), port);
    auto updateStatus = [device, firmware, port]() {
        firmware->setText(device->isReady() ? device->firmwareVersion() : tr("-"));
        port->setText(device->port().isEmpty() ? tr("-") : device->port());
    };
    connect(device, &Device::stateChanged, this, updateStatus);
    updateStatus();
    layout->addWidget(status);

    auto* note = new QLabel(
        tr("The UR44 stores its settings when it's switched to standby with its power switch. Disconnecting the "
           "power while it's on loses the changes made since it was last switched on."),
        this);
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addStretch();
}
