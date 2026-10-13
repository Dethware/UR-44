#include "ChannelStrip.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include "Bindings.h"
#include "LevelMeter.h"

ChannelStrip::ChannelStrip(const Spec& spec, UR44::Device* device, QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);

    auto* title = new QLabel(spec.title, this);
    title->setAlignment(Qt::AlignCenter);
    QFont bold = title->font();
    bold.setBold(true);
    title->setFont(bold);
    layout->addWidget(title);

    if (!spec.on.isEmpty())
    {
        auto* on = new QPushButton(this);
        on->setToolTip(tr("Whether this channel is sent to the mix"));
        Bindings::toggle(on, device, spec.on, spec.index, tr("On"), tr("Off"));
        layout->addWidget(on);
    }
    if (!spec.solo.isEmpty())
    {
        auto* solo = new QPushButton(tr("Solo"), this);
        Bindings::toggle(solo, device, spec.solo, spec.index);
        layout->addWidget(solo);
    }

    if (!spec.pan.isEmpty())
    {
        auto* pan = new QSlider(Qt::Horizontal, this);
        auto* panLabel = new QLabel(this);
        panLabel->setAlignment(Qt::AlignCenter);
        Bindings::slider(pan, device, spec.pan, spec.index, panLabel);
        pan->setToolTip(tr("Pan"));
        layout->addWidget(pan);
        layout->addWidget(panLabel);
    }

    auto* faderRow = new QHBoxLayout;
    faderRow->addStretch();
    if (spec.meterChannels > 0)
    {
        m_meter = new LevelMeter(spec.meterChannels, this);
        faderRow->addWidget(m_meter);
    }
    auto* levelLabel = new QLabel(this);
    levelLabel->setAlignment(Qt::AlignCenter);
    levelLabel->setMinimumWidth(levelLabel->fontMetrics().horizontalAdvance("-00.00 dB"));
    if (!spec.level.isEmpty())
    {
        auto* fader = new QSlider(Qt::Vertical, this);
        fader->setMinimumHeight(150);
        Bindings::slider(fader, device, spec.level, spec.index, levelLabel);
        faderRow->addWidget(fader);
    }
    faderRow->addStretch();
    layout->addLayout(faderRow, 1);
    layout->addWidget(levelLabel);
}
