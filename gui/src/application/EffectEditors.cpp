#include "EffectEditors.h"

#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QStandardItemModel>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "Application.h"
#include "Bindings.h"
#include "Device.h"
#include "Effects.h"
#include "Knob.h"
#include "LevelMeter.h"
#include "Protocol.h"
#include "ReverbGraph.h"
#include "ReverbTime.h"
#include "StripGraphs.h"
#include "SweetSpot.h"
#include "Widgets.h"

using namespace UR44;
using namespace Widgets;

namespace
{
/// The Channel Strip (0-3) inserted on a channel, if any
std::optional<uint16_t> insertedStrip(Device* device, uint16_t channel)
{
    for (const char* name : {"ChFxInsPrePatch", "ChFxInsPostPatch"})
    {
        int32_t v = device->value(name, channel).value_or(0);
        if (v >= 1 && v <= 4)
        {
            return uint16_t(v - 1);
        }
    }
    return std::nullopt;
}

/// Shows the existing window, or creates one with `make`
template<typename Make>
void openOnce(QPointer<QDialog>& slot, Make make)
{
    if (!slot)
    {
        slot = make();
        slot->setAttribute(Qt::WA_DeleteOnClose);
    }
    slot->show();
    slot->raise();
    slot->activateWindow();
}

/// An "ON" toggle for a bypass parameter (lit = not bypassed)
QToolButton* onButton(Device* device, const QString& bypassParameter, uint16_t index, const QColor& color,
                      QWidget* parent)
{
    auto* b = textButton("ON", color, QString(), parent);
    b->setFixedWidth(40);
    Bindings::toggle(b, device, bypassParameter, index, true);
    return b;
}

QComboBox* combo(Device* device, const QString& parameter, uint16_t index, QWidget* parent,
                 Bindings::Setter setter = {})
{
    auto* c = new QComboBox(parent);
    Bindings::combo(c, device, parameter, index, setter);
    return c;
}

/// A titled combo in the knob grid
QWidget* comboControl(const QString& title, QComboBox* c, QWidget* parent)
{
    auto* box = new QWidget(parent);
    auto* l = new QVBoxLayout(box);
    l->setContentsMargins(2, 2, 2, 2);
    l->setSpacing(2);
    l->addWidget(caption(title, box));
    c->setParent(box);
    l->addWidget(c);
    l->addStretch();
    return box;
}

void closeWhenDisconnected(QDialog* dialog, Device* device)
{
    QObject::connect(device, &Device::stateChanged, dialog, [dialog, device]() {
        if (!device->isReady())
        {
            dialog->close();
        }
    });
}
}

namespace EffectEditors
{

void openChannelStrip(Device* device, uint16_t channel, QWidget* parent)
{
    static QPointer<QDialog> windows[6];
    auto strip = insertedStrip(device, channel);
    if (channel >= 6 || !strip)
    {
        return;
    }
    openOnce(windows[channel], [&]() { return new ChannelStripEditor(device, channel, *strip, parent); });
}

void openGuitarAmp(Device* device, QWidget* parent)
{
    static QPointer<QDialog> window;
    openOnce(window, [&]() { return new GuitarAmpEditor(device, parent); });
}

void openReverb(Device* device, QWidget* parent)
{
    static QPointer<QDialog> window;
    openOnce(window, [&]() { return new ReverbEditor(device, parent); });
}

}

// --- Channel Strip -------------------------------------------------------------------------------------------------

ChannelStripEditor::ChannelStripEditor(Device* device, uint16_t channel, uint16_t strip, QWidget* parent)
    : QDialog(parent), m_device(device), m_channel(channel), m_strip(strip)
{
    // a paired input's strip is a stereo strip: two strips for two inputs
    auto updateTitle = [this, device, channel, strip]() {
        if (device->value("FxStereoLinkOnf", strip).value_or(0) && device->value("ChPairOnf", channel).value_or(0))
        {
            const int left = channel & ~1;
            const int first = strip & ~1;
            setWindowTitle(
                tr("Channel Strip %1/%2 (IN %3/%4)").arg(first + 1).arg(first + 2).arg(left + 1).arg(left + 2));
        }
        else
        {
            setWindowTitle(tr("Channel Strip %1 (IN %2)").arg(strip + 1).arg(channel + 1));
        }
    };
    Bindings::follow(this, device, "FxStereoLinkOnf", strip, [updateTitle](int32_t) { updateTitle(); });
    Bindings::follow(this, device, "ChPairOnf", channel, [updateTitle](int32_t) { updateTitle(); });
    auto* layout = new QVBoxLayout(this);

    // Morph, Sweet Spot Data, total gain, output meter
    auto* top = new QHBoxLayout;
    Knob* morphKnob = nullptr;
    QLabel* morphValue = nullptr;
    top->addWidget(knobControl(tr("MORPH"), device, QString(), strip, STRIP_COLOR, this, &morphKnob, &morphValue));
    // MORPH moves the compressor and EQ through the Sweet Spot Data's points: the host computes the values
    Bindings::slider(morphKnob, device, "FxMorphValue", strip, morphValue,
                     [device, strip](int32_t v) { Effects::setMorph(*device, strip, v); });
    Bindings::describe(morphKnob, tr("Morph"), tr("Morphs the compressor and EQ between the Sweet Spot Data's settings."));

    // Sweet Spot Data: the built-in presets and those in the configured folder; the device's current one is selected
    auto* sweetSpot = new QComboBox(this);
    sweetSpot->setMinimumWidth(220);
    auto presets = std::make_shared<QList<SweetSpotPreset>>(SweetSpotPreset::available(APPLICATION->sweetSpotFolder()));
    auto fillSweetSpot = [sweetSpot, device, strip, presets]() {
        QSignalBlocker blocker(sweetSpot);
        sweetSpot->clear();
        auto text = [](QByteArray a) { return QString::fromLatin1(a.left(a.indexOf('\0') >= 0 ? a.indexOf('\0') : a.size())); };
        const QString currentName = text(device->array("MorphSystemName", strip));
        const QByteArray currentId = device->array("MorphSystemId", strip);
        int current = -1;
        QString group;
        for (int i = 0; i < presets->size(); ++i)
        {
            const SweetSpotPreset& p = presets->at(i);
            if (p.group != group)
            {
                group = p.group;
                sweetSpot->addItem(group);
                auto* model = qobject_cast<QStandardItemModel*>(sweetSpot->model());
                model->item(sweetSpot->count() - 1)->setEnabled(false); // a heading
            }
            sweetSpot->addItem("    " + p.name, i);
            if (p.id == currentId && p.name == currentName)
            {
                current = sweetSpot->count() - 1;
            }
        }
        if (current < 0)
        {
            // what the device has isn't one of the presets (edited in dspMixFx, say): show it anyway
            sweetSpot->insertItem(0, currentName.isEmpty() ? tr("(unknown)") : currentName, -1);
            current = 0;
        }
        sweetSpot->setCurrentIndex(current);
        sweetSpot->setEnabled(device->isReady());
        sweetSpot->setToolTip(tr("Sweet Spot Data: the morph table MORPH moves through"));
    };
    connect(sweetSpot, &QComboBox::activated, this, [sweetSpot, device, strip, presets](int i) {
        int preset = sweetSpot->itemData(i).toInt();
        if (preset >= 0 && preset < presets->size())
        {
            Effects::loadSweetSpot(*device, strip, presets->at(preset));
        }
    });
    connect(device, &Device::arrayChanged, sweetSpot, [fillSweetSpot, strip](uint16_t, uint16_t index) {
        if (index == strip)
        {
            fillSweetSpot();
        }
    });
    connect(device, &Device::stateLoaded, sweetSpot, fillSweetSpot);
    fillSweetSpot();
    top->addWidget(caption(tr("SWEET SPOT DATA"), this));
    top->addWidget(sweetSpot);
    top->addStretch();
    top->addWidget(knobControl(tr("TOTAL GAIN"), tr("Total Gain"), tr("Adjusts the overall gain of the Channel Strip."),
                               device, "FxEqPeqGain", strip, STRIP_COLOR, this));
    m_output = new LevelMeter(1, this);
    m_output->setFixedHeight(70);
    m_output->setToolTip(tr("Channel Strip output"));
    top->addWidget(m_output);
    layout->addLayout(top);

    // Compressor
    auto* comp = new QGroupBox(tr("COMPRESSOR"), this);
    auto* cl = new QGridLayout(comp);
    cl->addWidget(onButton(device, "FxCompBypass", strip, STRIP_COLOR, comp), 0, 0, Qt::AlignLeft);
    // gain reduction, next to the knobs: 0 dB at the top, like the level meters' 0 dBFS
    m_gainReduction = new LevelMeter(1, comp, LevelMeter::GainReduction);
    m_gainReduction->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred); // as tall as the knob rows
    m_gainReduction->setToolTip(tr("Compressor gain reduction (0 .. 20 dB)"));
    m_gainReductionLabel = caption(tr("GR"), comp);
    m_gainReductionLabel->setMinimumWidth(m_gainReductionLabel->fontMetrics().horizontalAdvance(tr("GR %1 dB").arg("20.0")));
    cl->addWidget(new CompressorGraph(device, strip, comp), 0, 5, 3, 1);
    cl->addWidget(m_gainReductionLabel, 0, 6, Qt::AlignHCenter | Qt::AlignBottom);
    cl->addWidget(m_gainReduction, 1, 6, 2, 1, Qt::AlignHCenter);
    int col = 0;
    struct Control
    {
        QString title;
        QString name;
        QString description;
        const char* parameter;
    };
    for (const Control& c :
         {Control{tr("DRIVE"), tr("Compressor Drive"),
                  tr("Adjusts how strongly the compressor works. The higher the value, the stronger the effect."),
                  "FxCompDrive"},
          Control{tr("ATTACK"), tr("Compressor Attack"),
                  tr("Adjusts how fast the compressor reacts once the signal gets loud."), "FxCompAttack"},
          Control{tr("RELEASE"), tr("Compressor Release"),
                  tr("Adjusts how fast the compressor lets go once the signal gets quieter."), "FxCompRelease"},
          Control{tr("RATIO"), tr("Compressor Ratio"),
                  tr("Adjusts how much the signal above the threshold is reduced."), "FxCompRatio"}})
    {
        cl->addWidget(knobControl(c.title, c.name, c.description, device, c.parameter, strip, STRIP_COLOR, comp), 1,
                      col++);
    }
    cl->addWidget(comboControl(tr("KNEE"), combo(device, "FxCompKnee", strip, comp), comp), 1, col++);
    col = 0;
    for (const Control& c :
         {Control{tr("SIDE CHAIN Q"), tr("Side Chain Q"),
                  tr("Adjusts the bandwidth of the side chain filter, which shapes what the compressor reacts to."),
                  "FxCompScQual"},
          Control{tr("SIDE CHAIN F"), tr("Side Chain Frequency"),
                  tr("Adjusts the centre frequency of the side chain filter."), "FxCompScFreq"},
          Control{tr("SIDE CHAIN G"), tr("Side Chain Gain"),
                  tr("Adjusts the gain of the side chain filter: boosting a range makes the compressor react more "
                     "to it."),
                  "FxCompScGain"}})
    {
        cl->addWidget(knobControl(c.title, c.name, c.description, device, c.parameter, strip, STRIP_COLOR, comp), 2,
                      col++);
    }
    layout->addWidget(comp);

    // Equalizer
    auto* eq = new QGroupBox(tr("EQUALIZER"), this);
    auto* el = new QGridLayout(eq);
    el->addWidget(onButton(device, "FxEqBypass", strip, STRIP_COLOR, eq), 0, 0, Qt::AlignLeft);
    struct Band
    {
        QString title;
        QString name;
        QString kind; // what the band does, for the tooltips
        const char* q;
        const char* f;
        const char* g;
    };
    col = 0;
    for (const Band& band :
         {Band{tr("LOW"), tr("EQ Low"), tr("low shelving band (boosts or cuts everything below its frequency)"),
               nullptr, "FxEqLsvFreq", "FxEqLsvGain"},
          Band{tr("MID"), tr("EQ Mid"), tr("mid peaking band (boosts or cuts around its frequency)"), "FxEqMidQual",
               "FxEqMidFreq", "FxEqMidGain"},
          Band{tr("HIGH"), tr("EQ High"), tr("high shelving band (boosts or cuts everything above its frequency)"),
               nullptr, "FxEqHsvFreq", "FxEqHsvGain"}})
    {
        auto* box = new QGroupBox(band.title, eq);
        auto* bl = new QHBoxLayout(box);
        if (band.q)
        {
            bl->addWidget(knobControl("Q", tr("%1 Q").arg(band.name),
                                      tr("Adjusts the bandwidth of the %1: higher is narrower.").arg(band.kind),
                                      device, band.q, strip, STRIP_COLOR, box));
        }
        bl->addWidget(knobControl("F", tr("%1 Frequency").arg(band.name),
                                  tr("Adjusts the frequency of the %1.").arg(band.kind), device, band.f, strip,
                                  STRIP_COLOR, box));
        bl->addWidget(knobControl("G", tr("%1 Gain").arg(band.name), tr("Adjusts the gain of the %1.").arg(band.kind),
                                  device, band.g, strip, STRIP_COLOR, box));
        el->addWidget(box, 1, col++);
    }
    el->addWidget(new EqGraph(device, strip, eq), 2, 0, 1, col);
    el->setRowStretch(2, 1);
    // the EQ graph takes any extra height, the knobs stay together
    layout->addWidget(eq, 1);

    // Bound to this strip: close if the channel's insert changes to another (or none)
    for (const char* name : {"ChFxInsPrePatch", "ChFxInsPostPatch"})
    {
        Bindings::follow(this, device, name, channel, [this, device, channel](int32_t) {
            if (insertedStrip(device, channel) != m_strip)
            {
                close();
            }
        });
    }
    closeWhenDisconnected(this, device);
    connect(device, &Device::meterFrame, this, &ChannelStripEditor::showMeters);
}

void ChannelStripEditor::showMeters(const QList<uint32_t>& frame)
{
    if (frame.size() < Meters::PairCount * 2)
    {
        return;
    }
    int out = (Meters::StripOutput1 + m_strip) * 2;
    m_output->setLevels({Meters::levelDb(frame.at(out))}, {Meters::levelDb(frame.at(out + 1))});
    double gr = Meters::gainReductionDb(frame.at((Meters::GainReduction1 + m_strip) * 2));
    m_gainReduction->setGainReduction({gr});
    m_gainReductionLabel->setText(tr("GR %1 dB").arg(gr, 0, 'f', 1));
}

// --- Guitar Amp Classics -------------------------------------------------------------------------------------------

GuitarAmpEditor::GuitarAmpEditor(Device* device, QWidget* parent) : QDialog(parent), m_device(device)
{
    setWindowTitle(tr("Guitar Amp Classics"));
    auto* layout = new QVBoxLayout(this);

    auto* top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Amp type:"), this));
    // Choosing a model resets its controls to their defaults, like dspMixFx
    top->addWidget(combo(device, "GtrFxType", 0, this, [device](int32_t v) { Effects::setGuitarAmpType(*device, v); }));
    top->addStretch();
    m_input = new LevelMeter(2, this);
    m_input->setFixedHeight(60);
    m_input->setToolTip(tr("Amp input"));
    m_output = new LevelMeter(2, this);
    m_output->setFixedHeight(60);
    m_output->setToolTip(tr("Amp output"));
    top->addWidget(caption(tr("IN"), this));
    top->addWidget(m_input);
    top->addWidget(caption(tr("OUT"), this));
    top->addWidget(m_output);
    layout->addLayout(top);

    auto* pages = new QStackedWidget(this);
    struct Control
    {
        QString title;
        const char* parameter;
        bool isChoice = false;
    };
    // what each knob does; the same knob title means the same thing on every model
    const QHash<QString, QString> descriptions{
        {tr("VOLUME"), tr("Adjusts the input level of the amp.")},
        {tr("DISTORTION"), tr("Adjusts the amount of distortion.")},
        {tr("GAIN"), tr("Adjusts the pre-amp gain: the amount of distortion.")},
        {tr("MASTER"), tr("Adjusts the level going from the pre-amp to the power amp: more adds power amp "
                          "distortion.")},
        {tr("TREBLE"), tr("Adjusts the tone of the high range.")},
        {tr("MIDDLE"), tr("Adjusts the tone of the middle range.")},
        {tr("BASS"), tr("Adjusts the tone of the low range.")},
        {tr("PRESENCE"), tr("Adjusts the tone of the highest range, for brilliance and edge.")},
        {tr("SPEED"), tr("Adjusts the speed of the chorus or vibrato.")},
        {tr("DEPTH"), tr("Adjusts the depth of the chorus or vibrato.")},
        {tr("BLEND"), tr("Adjusts the balance between the dry sound and the chorus or vibrato.")},
        {tr("OUTPUT"), tr("Adjusts the output level of the amp.")},
    };
    auto page = [&](const QString& model, const QList<Control>& controls) {
        auto* w = new QWidget(pages);
        auto* l = new QHBoxLayout(w);
        for (const Control& c : controls)
        {
            l->addWidget(c.isChoice ? comboControl(c.title, combo(device, c.parameter, 0, w), w)
                                    : knobControl(c.title, tr("%1 %2").arg(model, c.title), descriptions.value(c.title),
                                                  device, c.parameter, 0, AMP_COLOR, w));
        }
        l->addStretch();
        pages->addWidget(w);
    };
    // the controls each model's panel has (operation manual pages 21-23)
    page(tr("Clean"), {{tr("VOLUME"), "GtrFxCleanVolume"}, {tr("DISTORTION"), "GtrFxCleanDistortion"},
          {tr("TREBLE"), "GtrFxCleanTreble"}, {tr("MIDDLE"), "GtrFxCleanMiddle"}, {tr("BASS"), "GtrFxCleanBass"},
          {tr("PRESENCE"), "GtrFxCleanHiTreble"}, {tr("Cho/OFF/Vib"), "GtrFxCleanChorus", true},
          {tr("SPEED"), "GtrFxCleanVibSpeed"}, {tr("DEPTH"), "GtrFxCleanVibDepth"}, {tr("BLEND"), "GtrFxCleanBlend"},
          {tr("OUTPUT"), "GtrFxCleanOutput"}});
    page(tr("Crunch"), {{tr("Normal/Bright"), "GtrFxCrunchType", true}, {tr("GAIN"), "GtrFxCrunchVolume"},
          {tr("TREBLE"), "GtrFxCrunchTreble"}, {tr("MIDDLE"), "GtrFxCrunchMiddle"}, {tr("BASS"), "GtrFxCrunchBass"},
          {tr("PRESENCE"), "GtrFxCrunchPresence"}, {tr("OUTPUT"), "GtrFxCrunchOutput"}});
    page(tr("Lead"), {{tr("High/Low"), "GtrFxLeadSensitivity", true}, {tr("GAIN"), "GtrFxLeadPreAmp"},
          {tr("MASTER"), "GtrFxLeadMaster"}, {tr("TREBLE"), "GtrFxLeadTreble"}, {tr("MIDDLE"), "GtrFxLeadMiddle"},
          {tr("BASS"), "GtrFxLeadBass"}, {tr("PRESENCE"), "GtrFxLeadPresence"}, {tr("OUTPUT"), "GtrFxLeadOutput"}});
    page(tr("Drive"), {{tr("AMP TYPE"), "GtrFxDriveType", true}, {tr("GAIN"), "GtrFxDriveGain"}, {tr("MASTER"), "GtrFxDriveMaster"},
          {tr("TREBLE"), "GtrFxDriveTreble"}, {tr("MIDDLE"), "GtrFxDriveMiddle"}, {tr("BASS"), "GtrFxDriveBass"},
          {tr("PRESENCE"), "GtrFxDrivePresence"}, {tr("OUTPUT"), "GtrFxDriveOutput"}});
    Bindings::follow(pages, device, "GtrFxType", 0, [pages](int32_t type) { pages->setCurrentIndex(type); });
    layout->addWidget(pages);

    closeWhenDisconnected(this, device);
    connect(device, &Device::meterFrame, this, &GuitarAmpEditor::showMeters);
}

void GuitarAmpEditor::showMeters(const QList<uint32_t>& frame)
{
    if (frame.size() < Meters::PairCount * 2)
    {
        return;
    }
    auto level = [&frame](int pair) { return Meters::levelDb(frame.at(pair * 2)); };
    auto hold = [&frame](int pair) { return Meters::levelDb(frame.at(pair * 2 + 1)); };
    m_input->setLevels({level(Meters::GuitarInLeft), level(Meters::GuitarInRight)},
                       {hold(Meters::GuitarInLeft), hold(Meters::GuitarInRight)});
    m_output->setLevels({level(Meters::GuitarOutLeft), level(Meters::GuitarOutRight)},
                        {hold(Meters::GuitarOutLeft), hold(Meters::GuitarOutRight)});
}

// --- REV-X ---------------------------------------------------------------------------------------------------------

ReverbEditor::ReverbEditor(Device* device, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("REV-X"));
    auto* layout = new QVBoxLayout(this);

    auto* top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Type:"), this));
    // Choosing a type resets the reverb parameters to the type's defaults, like dspMixFx
    top->addWidget(combo(device, "FxRevType", 0, this, [device](int32_t v) { Effects::setReverbType(*device, v); }));
    top->addStretch();
    layout->addLayout(top);
    // the graph takes any extra height
    layout->addWidget(new ReverbGraph(device, this), 1);

    auto* grid = new QGridLayout;
    Knob* time = nullptr;
    QLabel* timeValue = nullptr;
    grid->addWidget(knobControl(tr("REVERB TIME"), device, QString(), 0, REVX_COLOR, this, &time, &timeValue), 0, 0);
    Bindings::slider(time, device, "FxRevTime", 0);
    Bindings::describe(time, tr("REV-X Reverb Time"),
                       tr("Adjusts how long the reverb lasts. The range depends on the reverb type and room size."),
                       [device](int32_t v) {
                           return ReverbTime::text(device->value("FxRevType").value_or(0),
                                                   device->value("FxRevRoomSize").value_or(0), v) + " s";
                       },
                       [device]() {
                           return ReverbTime::rangeText(device->value("FxRevType").value_or(0),
                                                        device->value("FxRevRoomSize").value_or(0));
                       });
    // the time depends on the type and room size (computed like dspMixFx)
    auto showTime = [device, timeValue, time]() {
        QString t = ReverbTime::text(device->value("FxRevType").value_or(0), device->value("FxRevRoomSize").value_or(0),
                                     device->value("FxRevTime").value_or(0));
        timeValue->setText(t + " s");
    };
    for (const char* name : {"FxRevType", "FxRevRoomSize", "FxRevTime"})
    {
        Bindings::follow(timeValue, device, name, 0, [showTime](int32_t) { showTime(); });
    }

    int i = 1;
    struct Control
    {
        QString title;
        QString name;
        QString description;
        const char* parameter;
    };
    for (const Control& c : {
             Control{tr("INITIAL DELAY"), tr("Initial Delay"),
                     tr("Adjusts the time between the direct sound and the start of the reverb."), "FxRevIniDly"},
             Control{tr("DECAY"), tr("Decay"), tr("Adjusts how the reverb fades out (its envelope)."), "FxRevDecay"},
             Control{tr("ROOM SIZE"), tr("Room Size"),
                     tr("Adjusts the size of the simulated room. It also scales the reverb time."), "FxRevRoomSize"},
             Control{tr("DIFFUSION"), tr("Diffusion"), tr("Adjusts how much the reverb spreads out."), "FxRevDiff"},
             Control{tr("HPF"), tr("High Pass Filter"), tr("Cuts the reverb below this frequency."), "FxRevHpf"},
             Control{tr("LPF"), tr("Low Pass Filter"), tr("Cuts the reverb above this frequency."), "FxRevLpf"},
             Control{tr("HI RATIO"), tr("High Ratio"),
                     tr("Adjusts how long the high range reverberates, relative to the reverb time."),
                     "FxRevHighRatio"},
             Control{tr("LOW RATIO"), tr("Low Ratio"),
                     tr("Adjusts how long the low range reverberates, relative to the reverb time."), "FxRevLowRatio"},
             Control{tr("LOW FREQ"), tr("Low Frequency"), tr("Sets the frequency below which LOW RATIO applies."),
                     "FxRevLowFreq"}})
    {
        grid->addWidget(knobControl(c.title, tr("REV-X %1").arg(c.name), c.description, device, c.parameter, 0,
                                    REVX_COLOR, this),
                        i / 5, i % 5);
        ++i;
    }
    layout->addLayout(grid);
    closeWhenDisconnected(this, device);
}
