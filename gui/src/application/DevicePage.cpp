#include "DevicePage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

#include <QFileDialog>
#include <QHBoxLayout>
#include <QPushButton>

#include "Application.h"
#include "Bindings.h"
#include "Device.h"
#include "SweetSpot.h"

using UR44::Device;

DevicePage::DevicePage(Device* device, QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);

    // PHONES 2 and the REV-X send source are on the mix pages, like in dspMixFx
    auto* routing = new QGroupBox(tr("Routing"), this);
    auto* routingForm = new QFormLayout(routing);
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

    auto* effects = new QGroupBox(tr("Effects"), this);
    auto* effectsForm = new QFormLayout(effects);
    auto* folderRow = new QHBoxLayout;
    auto* folder = new QLabel(effects);
    folder->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto showFolder = [folder]() {
        QString path = APPLICATION->sweetSpotFolder();
        int builtIn = UR44::SweetSpotPreset::builtIn().size();
        int extra = UR44::SweetSpotPreset::available(path).size() - builtIn;
        folder->setText(tr("%1 built in, %2 more from %3")
                            .arg(builtIn)
                            .arg(extra)
                            .arg(QDir::toNativeSeparators(path)));
        folder->setToolTip(tr("The factory presets from TOOLS for UR44 (%1) are built in. Other .ssd files in the "
                              "folder are listed after them.")
                               .arg(UR44::SweetSpotPreset::builtInNotice()));
    };
    showFolder();
    folderRow->addWidget(folder, 1);
    auto* browse = new QPushButton(tr("Choose..."), effects);
    connect(browse, &QPushButton::clicked, this, [this, showFolder]() {
        QString dir = QFileDialog::getExistingDirectory(this, tr("Sweet Spot Data folder"),
                                                        APPLICATION->sweetSpotFolder());
        if (!dir.isEmpty())
        {
            APPLICATION->settings.ur44.sweetSpotFolder = dir;
            APPLICATION->settings.scheduleSave();
            showFolder();
        }
    });
    folderRow->addWidget(browse);
    effectsForm->addRow(tr("Sweet Spot Data:"), folderRow);
    layout->addWidget(effects);

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
    auto* mode = new QLabel(status);
    mode->setToolTip(tr("Set with the [CC MODE] switch on the back, read when the UR44 is switched on"));
    statusForm->addRow(tr("USB mode:"), mode);
    auto updateStatus = [device, firmware, port, mode]() {
        firmware->setText(device->isReady() ? device->firmwareVersion() : tr("-"));
        port->setText(device->port().isEmpty() ? tr("-") : device->port());
        mode->setText(!device->isReady()  ? tr("-")
                      : device->hasFxBus() ? tr("[CC MODE] off: 6 outputs (MIX 1, MIX 2, FX Bus to REV-X)")
                                           : tr("Class Compliant: 4 outputs (MIX 1, MIX 2)"));
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
