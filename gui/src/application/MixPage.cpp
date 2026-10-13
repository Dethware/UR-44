#include "MixPage.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "ChannelStrip.h"
#include "Device.h"
#include "LevelMeter.h"
#include "Parameters.h"
#include "Protocol.h"

using namespace UR44;

namespace
{
QString channelSolo(int bus)
{
    return QString("ChSendMixBus%1SoloOnf").arg(bus);
}

QString dawSolo(int bus)
{
    return QString("DawSendMixBus%1SoloOnf").arg(bus);
}
}

MixPage::MixPage(int bus, Device* device, QWidget* parent) : QWidget(parent), m_bus(bus), m_device(device)
{
    auto* pageLayout = new QVBoxLayout(this);

    // Solo status: soloing any channel mutes the rest of this mix, which is easy to forget
    auto* soloRow = new QHBoxLayout;
    soloRow->addStretch();
    m_soloIndicator = new QLabel(tr("SOLO"), this);
    m_soloIndicator->setAlignment(Qt::AlignCenter);
    m_soloIndicator->setMargin(3);
    m_soloIndicator->setMinimumWidth(m_soloIndicator->fontMetrics().horizontalAdvance("SOLO") * 2);
    soloRow->addWidget(m_soloIndicator);
    m_clearSolo = new QPushButton(tr("Clear solo"), this);
    m_clearSolo->setToolTip(tr("Turn off every solo in this mix"));
    connect(m_clearSolo, &QPushButton::clicked, this, &MixPage::clearSolo);
    soloRow->addWidget(m_clearSolo);
    pageLayout->addLayout(soloRow);

    auto* layout = new QHBoxLayout;
    pageLayout->addLayout(layout, 1);
    const QString b = QString::number(bus);

    for (uint16_t ch = 0; ch < 6; ++ch)
    {
        ChannelStrip::Spec spec;
        spec.title = tr("IN %1").arg(ch + 1);
        spec.index = ch;
        spec.on = "ChSendMixBus" + b + "Onf";
        spec.level = "ChSendMixBus" + b + "Level";
        spec.pan = "ChSendMixBus" + b + "Pan";
        spec.solo = "ChSendMixBus" + b + "SoloOnf";
        auto* strip = new ChannelStrip(spec, device, this);
        m_inputs.append(strip);
        layout->addWidget(strip);
    }

    auto separator = [this, layout]() {
        auto* line = new QFrame(this);
        line->setFrameShape(QFrame::VLine);
        line->setFrameShadow(QFrame::Sunken);
        layout->addWidget(line);
    };
    separator();

    ChannelStrip::Spec daw;
    // MIX 1 plays DAW 1/2 (PipeWire "Line1"), MIX 2 plays DAW 3/4 ("Line2")
    daw.title = bus == 1 ? tr("DAW 1/2") : tr("DAW 3/4");
    daw.on = "DawSendMixBus" + b + "Onf";
    daw.level = "DawSendMixBus" + b + "Level";
    daw.pan = "DawSendMixBus" + b + "Bal";
    daw.solo = "DawSendMixBus" + b + "SoloOnf";
    daw.meterChannels = 2;
    m_daw = new ChannelStrip(daw, device, this);
    layout->addWidget(m_daw);

    ChannelStrip::Spec rev;
    rev.title = tr("REV-X");
    rev.on = "FxRevMixBus" + b + "Onf";
    rev.level = "FxRevBus" + b + "Level";
    rev.meterChannels = 0;
    layout->addWidget(new ChannelStrip(rev, device, this));

    separator();

    ChannelStrip::Spec master;
    master.title = tr("MIX %1").arg(bus);
    master.on = "MasterMixBus" + b + "Onf";
    master.level = "MasterMixBus" + b + "Level";
    master.pan = "MasterMixBus" + b + "Bal";
    master.meterChannels = 2;
    m_master = new ChannelStrip(master, device, this);
    layout->addWidget(m_master);

    const Parameters& params = Parameters::instance();
    const uint16_t chSoloId = params[channelSolo(bus)].id;
    const uint16_t dawSoloId = params[dawSolo(bus)].id;
    connect(device, &Device::stateChanged, this, &MixPage::updateSolo);
    connect(device, &Device::stateLoaded, this, &MixPage::updateSolo);
    connect(device, &Device::parameterChanged, this, [this, chSoloId, dawSoloId](uint16_t id) {
        if (id == chSoloId || id == dawSoloId)
        {
            updateSolo();
        }
    });
    updateSolo();
}

void MixPage::updateSolo()
{
    QStringList soloed;
    for (uint16_t ch = 0; ch < 6; ++ch)
    {
        if (m_device->value(channelSolo(m_bus), ch).value_or(0))
        {
            soloed.append(tr("IN %1").arg(ch + 1));
        }
    }
    if (m_device->value(dawSolo(m_bus)).value_or(0))
    {
        soloed.append(tr("DAW"));
    }

    bool active = m_device->isReady() && !soloed.isEmpty();
    m_soloIndicator->setEnabled(active);
    m_soloIndicator->setStyleSheet(active ? "QLabel { background: #d08000; color: black; font-weight: bold; }"
                                          : "QLabel { color: palette(mid); }");
    m_soloIndicator->setToolTip(active ? tr("Soloed: %1. The other channels of this mix are muted.")
                                             .arg(soloed.join(", "))
                                       : tr("Nothing is soloed in this mix"));
    m_clearSolo->setEnabled(active);

    if (active != m_soloActive)
    {
        m_soloActive = active;
        emit soloActiveChanged(active);
    }
}

void MixPage::clearSolo()
{
    for (uint16_t ch = 0; ch < 6; ++ch)
    {
        if (m_device->value(channelSolo(m_bus), ch).value_or(0))
        {
            m_device->setParameter(channelSolo(m_bus), ch, 0);
        }
    }
    if (m_device->value(dawSolo(m_bus)).value_or(0))
    {
        m_device->setParameter(dawSolo(m_bus), 0, 0);
    }
}

void MixPage::showMeters(const QList<uint32_t>& frame)
{
    if (frame.size() < Meters::PairCount * 2)
    {
        return;
    }
    auto level = [&frame](int pair) { return Meters::levelDb(frame.at(pair * 2)); };
    auto hold = [&frame](int pair) { return Meters::levelDb(frame.at(pair * 2 + 1)); };

    for (int ch = 0; ch < m_inputs.size(); ++ch)
    {
        m_inputs[ch]->meter()->setLevels({level(Meters::Input1 + ch)}, {hold(Meters::Input1 + ch)});
    }
    // The DAW / master meters show whichever mix they're pointed at
    if (m_device->value("PatchDawMeter") == m_bus - 1)
    {
        m_daw->meter()->setLevels({level(Meters::DawLeft), level(Meters::DawRight)},
                                  {hold(Meters::DawLeft), hold(Meters::DawRight)});
    }
    else
    {
        m_daw->meter()->clear();
    }
    if (m_device->value("PatchMasterMeter") == m_bus - 1)
    {
        m_master->meter()->setLevels({level(Meters::MasterLeft), level(Meters::MasterRight)},
                                     {hold(Meters::MasterLeft), hold(Meters::MasterRight)});
    }
    else
    {
        m_master->meter()->clear();
    }
}

void MixPage::clearMeters()
{
    for (auto* strip : std::as_const(m_inputs))
    {
        strip->meter()->clear();
    }
    m_daw->meter()->clear();
    m_master->meter()->clear();
}
