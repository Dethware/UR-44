#include "MixPage.h"

#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <memory>

#include "Bindings.h"
#include "Device.h"
#include "EffectEditors.h"
#include "Effects.h"
#include "Fader.h"
#include "Knob.h"
#include "LevelMeter.h"
#include "Parameters.h"
#include "Protocol.h"
#include "ReverbTime.h"
#include "Widgets.h"

using namespace UR44;
using namespace Widgets;

namespace
{
// Rows shared by all columns. The *Gap rows are empty space between sections, with a separator line in the middle.
enum Row
{
    InputTypeRow,
    MeterRow,
    MeterGap,
    FxLocationRow,
    FxSwitchRow,
    FxTypeRow,
    FxGap,
    SendRow,
    SendLabelRow,
    SendGap,
    PanRow,
    PanLabelRow,
    PanGap,
    MuteSoloRow,
    PhantomRow,
    PhantomGap,
    FaderRow,
    LevelLabelRow,
    NameRow,
};

QString channelSolo(int bus)
{
    return QString("ChSendMixBus%1SoloOnf").arg(bus);
}

QString dawSolo(int bus)
{
    return QString("DawSendMixBus%1SoloOnf").arg(bus);
}

/// Ctrl+Shift (Cmd+Shift on macOS): dspMixFx's modifier for changing all the channels of a mix at once
bool allChannelsModifier()
{
    const auto all = Qt::ControlModifier | Qt::ShiftModifier;
    return (QGuiApplication::keyboardModifiers() & all) == all;
}

/// A small square toggle like dspMixFx's, lit in `color` when checked

/// A text toggle (INS.FX, Ch.Strip, SEND) in the same style, but wider


/// A chain link, for the pairs' LINK buttons: two interlocked rounded links
QIcon chainIcon()
{
    QPixmap pixmap(QSize(18, 12) * 2);
    pixmap.setDevicePixelRatio(2);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(215, 215, 215), 1.6));
    p.drawRoundedRect(QRectF(1.5, 3, 9, 6), 3, 3);
    p.drawRoundedRect(QRectF(7.5, 3, 9, 6), 3, 3);
    return QIcon(pixmap);
}

/// A padlock, closed or open, for the pairs' fader link buttons
QIcon lockIcon()
{
    auto draw = [](bool closed) {
        QPixmap pixmap(QSize(12, 14) * 2);
        pixmap.setDevicePixelRatio(2);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor color(215, 215, 215);
        p.setPen(QPen(color, 1.5));
        // the shackle: down into the body when closed, lifted and to the side when open
        // (the lifted one still clear of the top: the arc's top is at 1 then, the pen's half width above it)
        QPainterPath shackle;
        const double lift = closed ? 0.0 : 2.0;
        shackle.moveTo(3.5, 8 - lift);
        shackle.lineTo(3.5, 6 - lift);
        shackle.arcTo(QRectF(3.5, 3 - lift, 5, 6), 180, -180);
        shackle.lineTo(8.5, closed ? 8 : 6.5 - lift);
        p.drawPath(shackle);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRoundedRect(QRectF(1.5, 7.5, 9, 6), 1.5, 1.5);
        return pixmap;
    };
    QIcon icon;
    icon.addPixmap(draw(false), QIcon::Normal, QIcon::Off);
    icon.addPixmap(draw(true), QIcon::Normal, QIcon::On);
    return icon;
}

void addCentered(QGridLayout* grid, QWidget* w, int row, int column, int columnSpan = 1)
{
    grid->addWidget(w, row, column, 1, columnSpan, Qt::AlignHCenter);
}

/// A knob and its value label below it, kept together when the rows get taller (the REV-X block spans them)
void addKnob(QGridLayout* grid, QWidget* knob, QWidget* label, int row, int column)
{
    grid->addWidget(knob, row, column, Qt::AlignHCenter | Qt::AlignBottom);
    grid->addWidget(label, row + 1, column, Qt::AlignHCenter | Qt::AlignTop);
}

/// dspMixFx's fader scale: notches at these levels, the ends drawn heavier. Only some are labelled: the scale is
/// linear in the device's 0..127 steps, so the low end is crowded.
const double FADER_MARKS_DB[] = {6, 0, -6, -12, -18, -30, -40, -50, -60};
const double FADER_LABELS_DB[] = {6, 0, -12, -30};

/// A level fader with the console scale. All the level parameters share one scale (-INF .. +6 dB, 0..127).
Fader* fader(QWidget* parent)
{
    static const QList<Fader::Mark> marks = [] {
        const Parameter& level = Parameters::instance()["ChSendMixBus1Level"];
        QList<Fader::Mark> list;
        for (double db : FADER_MARKS_DB)
        {
            // the value whose label is this level, e.g. "-6.00"
            for (int v = level.min; v <= level.max; ++v)
            {
                bool ok = false;
                if (std::abs(QString(level.label(v)).toDouble(&ok) - db) < 0.001 && ok)
                {
                    const bool labelled = std::find(std::begin(FADER_LABELS_DB), std::end(FADER_LABELS_DB), db) !=
                                          std::end(FADER_LABELS_DB);
                    const QString text = db > 0 ? QString("+%1").arg(db) : QString::number(db);
                    list.append({v, labelled ? text : QString(), db == 6});
                    break;
                }
            }
        }
        list.append({level.min, QStringLiteral("-\u221E"), true});
        return list;
    }();
    auto* f = new Fader(parent);
    f->setMarks(marks);
    f->setMinimumHeight(140);
    return f;
}
}

MixPage::MixPage(int bus, Device* device, QWidget* parent) : QWidget(parent), m_bus(bus), m_device(device)
{
    auto* outer = new QHBoxLayout(this);
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(3);
    grid->setContentsMargins(6, 6, 6, 6);
    outer->addLayout(grid, 1);

    for (uint16_t ch = 0; ch < 6; ++ch)
    {
        addInputColumn(grid, ch, ch);
    }
    m_grid = grid;
    for (uint16_t l : {0, 2, 4})
    {
        addPairHeader(grid, l);
        for (const char* name : {"ChPairOnf", "ChFaderLinkOnf"})
        {
            Bindings::follow(this, device, name, l, [this, l](int32_t) { updatePair(l); });
        }
        updatePair(l);
    }
    // columns 6 and 8 are gaps between the input, DAW and master groups
    grid->setColumnMinimumWidth(6, 4);
    addDawColumn(grid, 7);
    grid->setColumnMinimumWidth(8, 4);
    addMasterColumn(grid, 9);
    m_stripColumns = {0, 1, 2, 3, 4, 5, 7, 9};
    grid->setRowStretch(FaderRow, 1);
    for (int gap : {MeterGap, FxGap, SendGap, PanGap, PhantomGap})
    {
        grid->setRowMinimumHeight(gap, 5);
    }


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

void MixPage::addInputColumn(QGridLayout* grid, int column, uint16_t ch)
{
    const QString b = QString::number(m_bus);
    Device* device = m_device;

    // the front/back panel connector
    static const char* const INPUT_TYPES[] = {"MIC/HI-Z", "MIC/HI-Z", "MIC/LINE", "MIC/LINE", "LINE", "LINE"};
    addCentered(grid, caption(INPUT_TYPES[ch], this), InputTypeRow, column);

    // meter, with HPF and phase next to it
    auto* meterBox = new QWidget(this);
    auto* meterLayout = new QHBoxLayout(meterBox);
    meterLayout->setContentsMargins(0, 0, 0, 0);
    auto* meter = new LevelMeter(1, meterBox);
    meter->setFixedHeight(90);
    m_inputMeters.append(meter);
    meterLayout->addWidget(meter);
    auto* switches = new QVBoxLayout;
    switches->setSpacing(3);
    auto* phase = squareButton(QString::fromUtf8("Ø"), QColor(80, 160, 255), tr("Phase invert"), meterBox);
    phase->setFixedWidth(34);
    Bindings::toggle(phase, device, "ChPhaseOnf", ch);
    switches->addWidget(phase);
    if (ch < 4) // there's no high pass filter on LINE INPUT 5/6
    {
        auto* hpf = squareButton("HPF", QColor(80, 160, 255), QString(), meterBox);
        hpf->setFixedWidth(34);
        Bindings::toggle(hpf, device, "ChHpfOnf", ch);
        // the cutoff (dspMixFx sets it in its Setup window): right-click menu
        hpf->setContextMenuPolicy(Qt::CustomContextMenu);
        const Parameter& freq = Parameters::instance()["ChHpfFreq"];
        Bindings::follow(hpf, device, "ChHpfFreq", ch, [hpf, &freq](int32_t v) {
            hpf->setToolTip(tr("High pass filter at %1 (right-click to change)").arg(freq.text(v)));
        });
        connect(hpf, &QWidget::customContextMenuRequested, hpf, [hpf, device, ch, &freq](const QPoint& pos) {
            QMenu menu;
            int32_t current = device->value(freq.id, ch).value_or(-1);
            for (int32_t v = freq.min; v <= freq.max; ++v)
            {
                QAction* a = menu.addAction(freq.text(v));
                a->setCheckable(true);
                a->setChecked(v == current);
                connect(a, &QAction::triggered, hpf, [device, &freq, ch, v]() { device->setParameter(freq.id, ch, v); });
            }
            menu.exec(hpf->mapToGlobal(pos));
        });
        switches->addWidget(hpf);
        m_hiddenWhenRight[ch].append(hpf);
    }
    switches->addStretch();
    meterLayout->addLayout(switches);
    addCentered(grid, meterBox, MeterRow, column);

    // effect: insertion location, on/off (assignment), type
    struct FxChoice
    {
        Protocol::FxType type = Protocol::FxType::ChannelStrip;
        Protocol::InsertPoint point = Protocol::InsertPoint::Pre;
    };
    auto preferred = std::make_shared<FxChoice>();
    auto* location = textButton("INS.FX", QColor(230, 120, 60), QString(), this);
    location->setCheckable(false);
    auto* power = squareButton(QString::fromUtf8("⏻"), QColor(70, 200, 90), QString(), this);
    QFont powerFont = power->font();
    powerFont.setPointSizeF(powerFont.pointSizeF() * 1.4);
    power->setFont(powerFont);
    auto* edit = squareButton("e", QColor(200, 200, 200), tr("Edit the effect"), this);
    edit->setCheckable(false);
    auto* type = textButton("Ch.Strip", QColor(230, 120, 60), QString(), this);
    type->setCheckable(false);

    // What's inserted now: from the insert parameters (ChFxIns* = strip 1..4, ChGtrFxIns* = 0/1)
    auto assigned = [device, ch]() -> std::optional<FxChoice> {
        auto v = [device, ch](const char* name) { return device->value(name, ch).value_or(0); };
        if (v("ChFxInsPrePatch"))
            return FxChoice{Protocol::FxType::ChannelStrip, Protocol::InsertPoint::Pre};
        if (v("ChFxInsPostPatch"))
            return FxChoice{Protocol::FxType::ChannelStrip, Protocol::InsertPoint::Post};
        if (v("ChGtrFxInsPrePatch"))
            return FxChoice{Protocol::FxType::GuitarAmp, Protocol::InsertPoint::Pre};
        if (v("ChGtrFxInsPostPatch"))
            return FxChoice{Protocol::FxType::GuitarAmp, Protocol::InsertPoint::Post};
        return std::nullopt;
    };
    // a pair takes the channel strip (as a stereo strip); the guitar amp is mono, and pairing takes it off
    auto paired = [device, ch]() { return device->value("ChPairOnf", ch).value_or(0) != 0; };
    auto refresh = [=]() {
        bool ready = device->isReady();
        auto now = assigned();
        FxChoice shown = now.value_or(*preferred);
        if (paired() && !now)
        {
            shown.type = Protocol::FxType::ChannelStrip;
        }
        bool ins = shown.point == Protocol::InsertPoint::Pre;
        location->setText(ins ? "INS.FX" : "MON.FX");
        location->setToolTip(ins ? tr("INS.FX: the effect applies to the monitor and the recorded signal")
                                 : tr("MON.FX: the effect applies to the monitor signal only"));
        type->setText(shown.type == Protocol::FxType::ChannelStrip ? "Ch.Strip" : "Guitar Amp");
        type->setToolTip(paired() ? tr("Effect type: a pair takes the Channel Strip, as a stereo strip")
                                  : tr("Effect type: Channel Strip or Guitar Amp Classics (click to switch)"));
        QSignalBlocker blocker(power);
        power->setChecked(now.has_value());
        power->setToolTip(now ? tr("Effect on (click to remove it)") : tr("Effect off (click to insert it)"));
        for (QWidget* w : {static_cast<QWidget*>(location), static_cast<QWidget*>(power), static_cast<QWidget*>(type)})
        {
            w->setEnabled(ready);
        }
        type->setEnabled(ready && !paired());
        edit->setEnabled(ready && now.has_value());
    };
    auto apply = [=](FxChoice choice, bool on) {
        if (auto now = assigned())
        {
            device->unassignFx(now->type, ch);
        }
        if (on)
        {
            device->assignFx(choice.type, ch, choice.point);
        }
    };
    connect(location, &QToolButton::clicked, this, [=]() {
        FxChoice next = assigned().value_or(*preferred);
        next.point = next.point == Protocol::InsertPoint::Pre ? Protocol::InsertPoint::Post : Protocol::InsertPoint::Pre;
        preferred->point = next.point;
        if (assigned())
        {
            apply(next, true);
        }
        refresh();
    });
    connect(type, &QToolButton::clicked, this, [=]() {
        FxChoice next = assigned().value_or(*preferred);
        next.type = next.type == Protocol::FxType::ChannelStrip ? Protocol::FxType::GuitarAmp
                                                                 : Protocol::FxType::ChannelStrip;
        preferred->type = next.type;
        if (assigned())
        {
            apply(next, true);
        }
        refresh();
    });
    connect(edit, &QToolButton::clicked, this, [=]() {
        if (auto now = assigned())
        {
            if (now->type == Protocol::FxType::ChannelStrip)
            {
                EffectEditors::openChannelStrip(device, ch, window());
            }
            else
            {
                EffectEditors::openGuitarAmp(device, window());
            }
        }
    });
    connect(power, &QToolButton::clicked, this, [=](bool on) {
        FxChoice choice = *preferred;
        if (paired())
        {
            choice.type = Protocol::FxType::ChannelStrip;
        }
        apply(choice, on);
        refresh();
    });
    for (const char* name :
         {"ChFxInsPrePatch", "ChFxInsPostPatch", "ChGtrFxInsPrePatch", "ChGtrFxInsPostPatch", "ChPairOnf"})
    {
        Bindings::follow(power, device, name, ch, [refresh](int32_t) { refresh(); });
    }
    connect(device, &Device::stateChanged, power, refresh);
    refresh();

    addCentered(grid, location, FxLocationRow, column);
    m_shared[ch].append({location, FxLocationRow, Qt::AlignHCenter, false});
    auto* fxSwitches = new QWidget(this);
    auto* fxSwitchLayout = new QHBoxLayout(fxSwitches);
    fxSwitchLayout->setContentsMargins(0, 0, 0, 0);
    fxSwitchLayout->setSpacing(4);
    fxSwitchLayout->addWidget(power);
    fxSwitchLayout->addWidget(edit);
    addCentered(grid, fxSwitches, FxSwitchRow, column);
    addCentered(grid, type, FxTypeRow, column);
    m_shared[ch].append({fxSwitches, FxSwitchRow, Qt::AlignHCenter, false});
    m_shared[ch].append({type, FxTypeRow, Qt::AlignHCenter, false});

    // REV-X send (one per channel, shared by both mixes)
    auto* send = new Knob(REVX_COLOR, this);
    auto* sendLabel = caption(QString(), this);
    Bindings::slider(send, device, "ChSendFxBusLevel", ch, sendLabel, {}, true);
    Bindings::describe(send, tr("REV-X Send for IN %1").arg(ch + 1),
                       tr("Adjusts the signal level which is sent to REV-X.\n"
                          "One send per input: it feeds REV-X from the mix selected with SEND."));
    addKnob(grid, send, sendLabel, SendRow, column);
    m_shared[ch].append({send, SendRow, Qt::AlignHCenter | Qt::AlignBottom, false});
    m_shared[ch].append({sendLabel, SendLabelRow, Qt::AlignHCenter | Qt::AlignTop, false});

    auto* pan = new Knob(PAN_COLOR, this);
    auto* panLabel = caption(QString(), this);
    Bindings::slider(pan, device, "ChSendMixBus" + b + "Pan", ch, panLabel);
    Bindings::describe(pan, tr("Pan for IN %1").arg(ch + 1),
                       tr("Places the signal between the left and right of MIX %1.").arg(m_bus));
    addKnob(grid, pan, panLabel, PanRow, column);

    auto* muteSolo = new QWidget(this);
    auto* msLayout = new QHBoxLayout(muteSolo);
    msLayout->setContentsMargins(0, 0, 0, 0);
    msLayout->setSpacing(4);
    auto* mute = squareButton("M", QColor(240, 130, 40),
                              tr("Mute in MIX %1 (Ctrl+Shift+click: all channels)").arg(m_bus), muteSolo);
    Bindings::toggle(mute, device, "ChSendMixBus" + b + "Onf", ch, true);
    auto* solo = squareButton("S", QColor(240, 200, 40),
                              tr("Solo in MIX %1 (Ctrl+Shift+click: all channels)").arg(m_bus), muteSolo);
    Bindings::toggle(solo, device, channelSolo(m_bus), ch);
    connectAllChannels(mute, solo);
    msLayout->addWidget(mute);
    msLayout->addWidget(solo);
    addCentered(grid, muteSolo, MuteSoloRow, column);
    m_shared[ch].append({muteSolo, MuteSoloRow, Qt::AlignHCenter, false});

    auto* level = fader(this);
    auto* levelLabel = caption(QString(), this);
    // Ctrl+Shift (Cmd+Shift on macOS) + drag moves all the input faders of this mix to the same level, like dspMixFx
    const QString levelName = "ChSendMixBus" + b + "Level";
    Bindings::slider(level, device, levelName, ch, levelLabel,
                     [device, levelName, ch](int32_t v) {
                         if (allChannelsModifier())
                         {
                             device->setParameterAllInstances(levelName, v);
                         }
                         else
                         {
                             device->setParameter(levelName, ch, v);
                         }
                     },
                     true);
    Bindings::describe(level, tr("Level for IN %1").arg(ch + 1),
                       tr("Adjusts the level of the input in MIX %1.\n"
                          "Ctrl+Shift+drag moves all six input faders together.")
                           .arg(m_bus));
    addCentered(grid, level, FaderRow, column);
    addCentered(grid, levelLabel, LevelLabelRow, column);
    m_shared[ch].append({level, FaderRow, Qt::AlignHCenter, true});
    m_shared[ch].append({levelLabel, LevelLabelRow, Qt::AlignHCenter, true});
    addCentered(grid, caption(tr("IN %1").arg(ch + 1), this), NameRow, column);
}

void MixPage::addPairHeader(QGridLayout* grid, uint16_t l)
{
    // the pair's link: a small chain between the two connector captions
    auto* link = squareButton(QString(), QColor(80, 160, 255), QString(), this);
    link->setFixedSize(24, 18);
    link->setIcon(chainIcon());
    link->setIconSize(QSize(18, 12));
    link->setToolTip(tr("Link IN %1 and IN %2 as a stereo pair: they share their settings, except phase and pan")
                         .arg(l + 1)
                         .arg(l + 2));
    Bindings::toggle(link, m_device, "ChPairOnf", l);
    grid->addWidget(link, InputTypeRow, l, 1, 2, Qt::AlignHCenter);

    // the fader link, between the channel names while the pair is linked: one fader for both, or one each
    auto* faderLink = squareButton(QString(), QColor(80, 160, 255), QString(), this);
    faderLink->setFixedSize(22, 20);
    faderLink->setIcon(lockIcon());
    faderLink->setIconSize(QSize(12, 14));
    faderLink->setToolTip(tr("Fader link: IN %1 and IN %2 share one fader per mix (off: a fader each)")
                              .arg(l + 1)
                              .arg(l + 2));
    Bindings::toggle(faderLink, m_device, "ChFaderLinkOnf", l);
    grid->addWidget(faderLink, NameRow, l, 1, 2, Qt::AlignHCenter);
    m_faderLinks[l] = faderLink;

    // +48V is switched on the front panel, per input pair (1/2, 3/4): between the two channels, as a read-only
    // indicator (a disabled button still shows its state, like PHONES 1)
    if (l < 4)
    {
        auto* phantom = textButton("+48V", QColor(255, 64, 48), QString(), this);
        phantom->setFixedWidth(48);
        phantom->setEnabled(false);
        // brighter than a read-only button's usual darkened colour: phantom power on is worth noticing
        phantom->setStyleSheet(phantom->styleSheet() +
                               "QToolButton:checked:disabled { background: #e8402c; color: #1a1a1a; }");
        phantom->setToolTip(tr("Phantom power for IN %1 and IN %2, switched on the front panel").arg(l + 1).arg(l + 2));
        Bindings::follow(phantom, m_device, "HaP48VOnf", uint16_t(l / 2),
                         [phantom](int32_t v) { phantom->setChecked(v != 0); });
        grid->addWidget(phantom, PhantomRow, l, 1, 2, Qt::AlignHCenter);
    }
}

void MixPage::updatePair(uint16_t l)
{
    const bool paired = m_device->value("ChPairOnf", l).value_or(0) != 0;
    const bool fadersLinked = m_device->value("ChFaderLinkOnf", l).value_or(1) != 0;
    const uint16_t r = uint16_t(l + 1);
    for (int i = 0; i < m_shared[l].size(); ++i)
    {
        const Shared& left = m_shared[l].at(i);
        const bool shared = paired && (!left.fader || fadersLinked);
        m_grid->removeWidget(left.widget);
        m_grid->addWidget(left.widget, left.row, l, 1, shared ? 2 : 1, left.alignment);
        m_shared[r].at(i).widget->setVisible(!shared);
    }
    for (QWidget* w : std::as_const(m_hiddenWhenRight[r]))
    {
        w->setVisible(!paired);
    }
    m_faderLinks[l]->setVisible(paired);
    update(); // the column backgrounds (paintEvent) follow the layout
}

void MixPage::addDawColumn(QGridLayout* grid, int column)
{
    const QString b = QString::number(m_bus);
    auto* source = caption(tr("DAW"), this);
    // MIX 1 plays DAW 1/2, MIX 2 plays DAW 3/4 (see the name below)
    source->setToolTip(tr("Playback from the computer"));
    addCentered(grid, source, InputTypeRow, column);
    m_dawMeter = new LevelMeter(2, this);
    m_dawMeter->setFixedHeight(90);
    addCentered(grid, m_dawMeter, MeterRow, column);

    // With the [CC MODE] switch off the computer has a third output, the FX Bus, which goes straight into REV-X
    auto* fxBus = caption(tr("FX BUS\n\u2192 REV-X"), this);
    fxBus->setToolTip(tr("The computer's \"FX Bus\" output goes straight into REV-X, at a fixed level.\n"
                         "Hear it through REV-X's RTN in MIX 1 or MIX 2."));
    addCentered(grid, fxBus, FxSwitchRow, column);
    auto showFxBus = [fxBus, device = m_device]() { fxBus->setVisible(device->isReady() && device->hasFxBus()); };
    connect(m_device, &Device::stateChanged, fxBus, showFxBus);
    showFxBus();

    auto* pan = new Knob(PAN_COLOR, this);
    auto* panLabel = caption(QString(), this);
    Bindings::slider(pan, m_device, "DawSendMixBus" + b + "Bal", 0, panLabel);
    Bindings::describe(pan, tr("Balance for DAW"),
                       tr("Adjusts the left/right balance of the computer playback in MIX %1.").arg(m_bus));
    addKnob(grid, pan, panLabel, PanRow, column);

    auto* muteSolo = new QWidget(this);
    auto* msLayout = new QHBoxLayout(muteSolo);
    msLayout->setContentsMargins(0, 0, 0, 0);
    msLayout->setSpacing(4);
    auto* mute = squareButton("M", QColor(240, 130, 40),
                              tr("Mute in MIX %1 (Ctrl+Shift+click: all channels)").arg(m_bus), muteSolo);
    Bindings::toggle(mute, m_device, "DawSendMixBus" + b + "Onf", 0, true);
    auto* solo = squareButton("S", QColor(240, 200, 40),
                              tr("Solo in MIX %1 (Ctrl+Shift+click: all channels)").arg(m_bus), muteSolo);
    Bindings::toggle(solo, m_device, dawSolo(m_bus), 0);
    connectAllChannels(mute, solo);
    msLayout->addWidget(mute);
    msLayout->addWidget(solo);
    addCentered(grid, muteSolo, MuteSoloRow, column);

    auto* level = fader(this);
    auto* levelLabel = caption(QString(), this);
    Bindings::slider(level, m_device, "DawSendMixBus" + b + "Level", 0, levelLabel, {}, true);
    Bindings::describe(level, tr("Level for DAW"),
                       tr("Adjusts the level of the computer playback in MIX %1.").arg(m_bus));
    addCentered(grid, level, FaderRow, column);
    addCentered(grid, levelLabel, LevelLabelRow, column);
    // MIX 1 plays DAW 1/2 (PipeWire "Line1"), MIX 2 plays DAW 3/4 ("Line2")
    addCentered(grid, caption(m_bus == 1 ? tr("DAW 1/2") : tr("DAW 3/4"), this), NameRow, column);
}

void MixPage::addMasterColumn(QGridLayout* grid, int column)
{
    const QString b = QString::number(m_bus);
    Device* device = m_device;
    const int bus = m_bus;

    // where the mix goes: the outputs, and with loopback (MIX 1 only) back to the computer as well
    auto* destination = caption(tr("OUT"), this);
    const QString outputs = bus == 1 ? tr("MAIN OUTPUT, LINE OUTPUT 1/2 and PHONES 1 (and PHONES 2 if it plays MIX 1)")
                                     : tr("LINE OUTPUT 3/4 (and PHONES 2 if it plays MIX 2)");
    destination->setToolTip(tr("MIX %1 goes to %2").arg(bus).arg(outputs));
    if (bus == 1)
    {
        Bindings::follow(destination, device, "PatchLoopbackMode", 0, [destination, outputs](int32_t loopback) {
            destination->setText(loopback ? tr("OUT + DAW") : tr("OUT"));
            destination->setToolTip(loopback ? tr("MIX 1 goes to %1, and back to the computer (loopback is on)")
                                                   .arg(outputs)
                                             : tr("MIX 1 goes to %1").arg(outputs));
        });
    }
    addCentered(grid, destination, InputTypeRow, column);
    m_masterMeter = new LevelMeter(2, this);
    m_masterMeter->setFixedHeight(90);
    addCentered(grid, m_masterMeter, MeterRow, column);

    // REV-X: send on/off for this mix, type, time, return level
    auto* revx = new QFrame(this);
    revx->setFrameShape(QFrame::StyledPanel);
    auto* rl = new QGridLayout(revx);
    rl->setContentsMargins(4, 4, 4, 4);
    rl->setSpacing(3);
    auto* title = caption("REV-X", revx);
    rl->addWidget(title, 0, 0, 1, 2);

    // One REV-X: PatchFxSend picks the mix whose channel sends feed it (SEND), and each mix has its own return
    // switch, FxRevMixBus<n>Onf (RTN). Both found by ear; both returns can be on at once.
    auto* sendOn = textButton("SEND", REVX_COLOR, QString(), revx);
    sendOn->setFixedWidth(48);
    auto* edit = squareButton("e", QColor(200, 200, 200), tr("Edit REV-X"), revx);
    edit->setCheckable(false);
    connect(edit, &QToolButton::clicked, this, [this, device]() { EffectEditors::openReverb(device, window()); });
    connect(device, &Device::stateChanged, edit, [edit, device]() { edit->setEnabled(device->isReady()); });
    edit->setEnabled(device->isReady());
    auto isSource = [device, bus]() { return device->value("PatchFxSend").value_or(-1) == bus - 1; };
    auto refreshSend = [sendOn, isSource, device, bus]() {
        sendOn->setEnabled(device->isReady());
        QSignalBlocker blocker(sendOn);
        sendOn->setChecked(isSource());
        sendOn->setToolTip(isSource()
                               ? tr("REV-X is fed from MIX %1: its channels' REV-X sends").arg(bus)
                               : tr("Click to feed REV-X from MIX %1 instead of MIX %2").arg(bus).arg(3 - bus));
    };
    connect(sendOn, &QToolButton::clicked, this, [=]() {
        // like a radio button: one of the mixes is always the source
        if (!isSource())
        {
            device->setParameter("PatchFxSend", 0, bus - 1);
        }
        refreshSend();
    });
    Bindings::follow(sendOn, device, "PatchFxSend", 0, [refreshSend](int32_t) { refreshSend(); });
    connect(device, &Device::stateChanged, sendOn, refreshSend);
    refreshSend();
    rl->addWidget(sendOn, 1, 0, Qt::AlignHCenter);
    rl->addWidget(edit, 1, 1, Qt::AlignHCenter);

    auto* type = new QComboBox(revx);
    // choosing a type resets the reverb parameters to the type's defaults, like dspMixFx (and the REV-X editor)
    Bindings::combo(type, device, "FxRevType", 0, [device](int32_t v) { Effects::setReverbType(*device, v); });
    rl->addWidget(type, 2, 0, 1, 2);

    auto* time = new Knob(QColor(60, 90, 200), revx);
    auto* timeLabel = caption(QString(), revx);
    Bindings::slider(time, device, "FxRevTime", 0);
    Bindings::describe(time, tr("REV-X Reverb Time"),
                       tr("Adjusts the reverb time. The range depends on the reverb type and room size."),
                       [device](int32_t v) {
                           return ReverbTime::text(device->value("FxRevType").value_or(0),
                                                   device->value("FxRevRoomSize").value_or(0), v) + " s";
                       },
                       [device]() {
                           return ReverbTime::rangeText(device->value("FxRevType").value_or(0),
                                                        device->value("FxRevRoomSize").value_or(0));
                       });
    // the time depends on the type and room size (dspMixFx computes it the same way)
    auto showTime = [device, timeLabel, time]() {
        QString t = ReverbTime::text(device->value("FxRevType").value_or(0), device->value("FxRevRoomSize").value_or(0),
                                     device->value("FxRevTime").value_or(0));
        timeLabel->setText(t + " s");
    };
    for (const char* name : {"FxRevType", "FxRevRoomSize", "FxRevTime"})
    {
        Bindings::follow(timeLabel, device, name, 0, [showTime](int32_t) { showTime(); });
    }
    rl->addWidget(caption(tr("TIME"), revx), 3, 0);
    auto* returnOn = textButton(tr("RTN"), RETURN_COLOR, tr("REV-X return into MIX %1 on/off").arg(bus), revx);
    Bindings::toggle(returnOn, device, "FxRevMixBus" + b + "Onf", 0);
    rl->addWidget(returnOn, 3, 1, Qt::AlignHCenter);
    rl->addWidget(time, 4, 0, Qt::AlignHCenter);
    auto* rtn = new Knob(RETURN_COLOR, revx);
    auto* rtnLabel = caption(QString(), revx);
    Bindings::slider(rtn, device, "FxRevBus" + b + "Level", 0, rtnLabel, {}, true);
    Bindings::describe(rtn, tr("REV-X Return for MIX %1").arg(m_bus),
                       tr("Adjusts the level of the reverb signal returned to MIX %1 (switched with RTN).")
                           .arg(m_bus));
    rl->addWidget(rtn, 4, 1, Qt::AlignHCenter);
    rl->addWidget(timeLabel, 5, 0);
    rl->addWidget(rtnLabel, 5, 1);
    grid->addWidget(revx, FxLocationRow, column, SendLabelRow - FxLocationRow + 1, 1, Qt::AlignHCenter);

    auto* bal = new Knob(PAN_COLOR, this);
    auto* balLabel = caption(QString(), this);
    Bindings::slider(bal, device, "MasterMixBus" + b + "Bal", 0, balLabel);
    Bindings::describe(bal, tr("Balance for MIX %1").arg(m_bus),
                       tr("Adjusts the left/right balance of the MIX %1 output.").arg(m_bus));
    addKnob(grid, bal, balLabel, PanRow, column);

    auto* mute = squareButton("M", QColor(240, 130, 40), tr("Mute MIX %1").arg(m_bus), this);
    Bindings::toggle(mute, device, "MasterMixBus" + b + "Onf", 0, true);
    addCentered(grid, mute, MuteSoloRow, column);

    auto* level = fader(this);
    auto* levelLabel = caption(QString(), this);
    Bindings::slider(level, device, "MasterMixBus" + b + "Level", 0, levelLabel, {}, true);
    Bindings::describe(level, tr("Master level for MIX %1").arg(m_bus),
                       tr("Adjusts the output level of MIX %1.").arg(m_bus));
    addCentered(grid, level, FaderRow, column);
    addCentered(grid, levelLabel, LevelLabelRow, column);
    addCentered(grid, caption(tr("MASTER"), this), NameRow, column);
}

void MixPage::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);
    if (!m_grid)
    {
        return;
    }
    // Strip panels behind the controls, with lines between the sections. Colours derive from the palette, so this
    // works with light and dark themes.
    const QColor window = palette().color(QPalette::Window);
    const bool dark = window.lightness() < 128;
    const QColor panel = dark ? window.lighter(135) : window.darker(108);
    const QColor line = dark ? panel.lighter(135) : panel.darker(118);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    // a pair of inputs is one strip
    auto paired = [this](int column) {
        return column < 6 && m_device->value("ChPairOnf", uint16_t(column)).value_or(0) != 0;
    };
    for (int column : std::as_const(m_stripColumns))
    {
        if (column < 6 && column % 2 == 1 && paired(column - 1))
        {
            continue; // drawn with the left one
        }
        const int last = column % 2 == 0 && paired(column) ? column + 1 : column;
        QRect strip;
        for (int row = InputTypeRow; row <= NameRow; ++row)
        {
            strip |= m_grid->cellRect(row, column);
            strip |= m_grid->cellRect(row, last);
        }
        strip.adjust(-5, -4, 5, 4);
        p.setPen(Qt::NoPen);
        p.setBrush(panel);
        p.drawRoundedRect(strip, 4, 4);

        // section boundaries; skip the ones that would only frame empty space (DAW) or cut the REV-X block (master)
        QList<int> boundaries{MeterGap, FxGap, SendGap, PanGap, PhantomGap};
        if (column == 7 || column == 9)
        {
            boundaries.removeAll(FxGap);
        }
        p.setPen(QPen(line, 1));
        for (int row : std::as_const(boundaries))
        {
            int y = m_grid->cellRect(row, column).center().y();
            p.drawLine(QPointF(strip.left() + 3, y + 0.5), QPointF(strip.right() - 3, y + 0.5));
        }
    }
}

void MixPage::connectAllChannels(QToolButton* mute, QToolButton* solo)
{
    // Ctrl+Shift+click on a channel's M or S: the same state for the six inputs and the DAW channel, like dspMixFx
    auto apply = [this](const QString& channelParameter, const QString& dawParameter, int32_t value) {
        m_device->setParameterAllInstances(channelParameter, value);
        m_device->setParameter(dawParameter, 0, value);
    };
    const QString b = QString::number(m_bus);
    connect(mute, &QToolButton::clicked, this, [apply, b](bool muted) {
        if (allChannelsModifier())
        {
            apply("ChSendMixBus" + b + "Onf", "DawSendMixBus" + b + "Onf", muted ? 0 : 1); // "on" is not muted
        }
    });
    connect(solo, &QToolButton::clicked, this, [this, apply](bool soloed) {
        if (allChannelsModifier())
        {
            apply(channelSolo(m_bus), dawSolo(m_bus), soloed ? 1 : 0);
        }
    });
}

void MixPage::updateSolo()
{
    // soloing any channel mutes the rest of this mix, which is easy to forget: the tab says so (MainWindow)
    bool active = m_device->isReady() && m_device->value(dawSolo(m_bus)).value_or(0);
    for (uint16_t ch = 0; ch < 6 && !active; ++ch)
    {
        active = m_device->isReady() && m_device->value(channelSolo(m_bus), ch).value_or(0);
    }
    if (active != m_soloActive)
    {
        m_soloActive = active;
        emit soloActiveChanged(active);
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

    for (int ch = 0; ch < m_inputMeters.size(); ++ch)
    {
        m_inputMeters[ch]->setLevels({level(Meters::Input1 + ch)}, {hold(Meters::Input1 + ch)});
    }
    // The DAW / master meters show whichever mix they're pointed at
    if (m_device->value("PatchDawMeter") == m_bus - 1)
    {
        m_dawMeter->setLevels({level(Meters::DawLeft), level(Meters::DawRight)},
                              {hold(Meters::DawLeft), hold(Meters::DawRight)});
    }
    else
    {
        m_dawMeter->clear();
    }
    if (m_device->value("PatchMasterMeter") == m_bus - 1)
    {
        m_masterMeter->setLevels({level(Meters::MasterLeft), level(Meters::MasterRight)},
                                 {hold(Meters::MasterLeft), hold(Meters::MasterRight)});
    }
    else
    {
        m_masterMeter->clear();
    }
}

void MixPage::clearMeters()
{
    for (auto* meter : std::as_const(m_inputMeters))
    {
        meter->clear();
    }
    m_dawMeter->clear();
    m_masterMeter->clear();
}
