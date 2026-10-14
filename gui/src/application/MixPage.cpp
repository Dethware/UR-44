#include "MixPage.h"

#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <memory>

#include "Bindings.h"
#include "Device.h"
#include "Knob.h"
#include "LevelMeter.h"
#include "Parameters.h"
#include "Protocol.h"
#include "ReverbTime.h"

using namespace UR44;

namespace
{
// dspMixFx colours: REV-X is light blue, pan green, the REV-X return orange
const QColor REVX_COLOR(64, 200, 232);
const QColor PAN_COLOR(70, 170, 70);
const QColor RETURN_COLOR(230, 130, 40);

// Rows shared by all columns
enum Row
{
    InputTypeRow,
    MeterRow,
    FxLocationRow,
    FxSwitchRow,
    FxTypeRow,
    SendRow,
    SendLabelRow,
    PanRow,
    PanLabelRow,
    MuteSoloRow,
    PhantomRow,
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

/// A small square toggle like dspMixFx's, lit in `color` when checked
QToolButton* squareButton(const QString& text, const QColor& color, const QString& tooltip, QWidget* parent)
{
    auto* b = new QToolButton(parent);
    b->setText(text);
    b->setToolTip(tooltip);
    b->setCheckable(true);
    b->setFixedSize(26, 22);
    b->setStyleSheet(QString("QToolButton { background: #3a3a3a; color: #d0d0d0; border: 1px solid #1e1e1e;"
                             " border-radius: 3px; font-weight: bold; }"
                             "QToolButton:checked { background: %1; color: black; }"
                             "QToolButton:disabled { background: #2e2e2e; color: #6a6a6a; }"
                             // read-only indicators (PHONES 1, channel link) still show their state
                             "QToolButton:checked:disabled { background: %2; color: #202020; }")
                         .arg(color.name(), color.darker(150).name()));
    return b;
}

/// A text toggle (INS.FX, Ch.Strip, SEND) in the same style, but wider
QToolButton* textButton(const QString& text, const QColor& color, const QString& tooltip, QWidget* parent)
{
    auto* b = squareButton(text, color, tooltip, parent);
    b->setFixedSize(QSize(64, 20));
    return b;
}

QLabel* caption(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setAlignment(Qt::AlignCenter);
    QFont f = l->font();
    f.setPointSizeF(f.pointSizeF() * 0.85);
    l->setFont(f);
    return l;
}

void addCentered(QGridLayout* grid, QWidget* w, int row, int column, int columnSpan = 1)
{
    grid->addWidget(w, row, column, 1, columnSpan, Qt::AlignHCenter);
}

QSlider* fader(QWidget* parent)
{
    auto* f = new QSlider(Qt::Vertical, parent);
    f->setMinimumHeight(140);
    f->setToolTip(QObject::tr("Double-click for 0 dB"));
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
    // columns 6 and 8 are gaps between the input, DAW and master groups
    grid->setColumnMinimumWidth(6, 4);
    addDawColumn(grid, 7);
    grid->setColumnMinimumWidth(8, 4);
    addMasterColumn(grid, 9);
    m_grid = grid;
    m_stripColumns = {0, 1, 2, 3, 4, 5, 7, 9};
    grid->setRowStretch(FaderRow, 1);

    outer->addWidget(makeSidePanel());

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
    }
    auto* phase = squareButton(QString::fromUtf8("Ø"), QColor(80, 160, 255), tr("Phase invert"), meterBox);
    phase->setFixedWidth(34);
    Bindings::toggle(phase, device, "ChPhaseOnf", ch);
    switches->addWidget(phase);
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
    auto* edit = squareButton("e", QColor(200, 200, 200), tr("Effect editor (not available yet)"), this);
    edit->setCheckable(false);
    edit->setEnabled(false);
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
    auto refresh = [=]() {
        bool ready = device->isReady();
        auto now = assigned();
        FxChoice shown = now.value_or(*preferred);
        bool ins = shown.point == Protocol::InsertPoint::Pre;
        location->setText(ins ? "INS.FX" : "MON.FX");
        location->setToolTip(ins ? tr("INS.FX: the effect applies to the monitor and the recorded signal")
                                 : tr("MON.FX: the effect applies to the monitor signal only"));
        type->setText(shown.type == Protocol::FxType::ChannelStrip ? "Ch.Strip" : "Guitar Amp");
        type->setToolTip(tr("Effect type: Channel Strip or Guitar Amp Classics (click to switch)"));
        QSignalBlocker blocker(power);
        power->setChecked(now.has_value());
        power->setToolTip(now ? tr("Effect on (click to remove it)") : tr("Effect off (click to insert it)"));
        for (QWidget* w : {static_cast<QWidget*>(location), static_cast<QWidget*>(power), static_cast<QWidget*>(type)})
        {
            w->setEnabled(ready);
        }
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
    connect(power, &QToolButton::clicked, this, [=](bool on) {
        apply(*preferred, on);
        refresh();
    });
    for (const char* name : {"ChFxInsPrePatch", "ChFxInsPostPatch", "ChGtrFxInsPrePatch", "ChGtrFxInsPostPatch"})
    {
        Bindings::follow(power, device, name, ch, [refresh](int32_t) { refresh(); });
    }
    connect(device, &Device::stateChanged, power, refresh);
    refresh();

    addCentered(grid, location, FxLocationRow, column);
    auto* fxSwitches = new QWidget(this);
    auto* fxSwitchLayout = new QHBoxLayout(fxSwitches);
    fxSwitchLayout->setContentsMargins(0, 0, 0, 0);
    fxSwitchLayout->setSpacing(4);
    fxSwitchLayout->addWidget(power);
    fxSwitchLayout->addWidget(edit);
    addCentered(grid, fxSwitches, FxSwitchRow, column);
    addCentered(grid, type, FxTypeRow, column);

    // REV-X send (one per channel, shared by both mixes)
    auto* send = new Knob(REVX_COLOR, this);
    auto* sendLabel = caption(QString(), this);
    Bindings::slider(send, device, "ChSendFxBusLevel", ch, sendLabel);
    addCentered(grid, send, SendRow, column);
    addCentered(grid, sendLabel, SendLabelRow, column);

    auto* pan = new Knob(PAN_COLOR, this);
    auto* panLabel = caption(QString(), this);
    Bindings::slider(pan, device, "ChSendMixBus" + b + "Pan", ch, panLabel);
    addCentered(grid, pan, PanRow, column);
    addCentered(grid, panLabel, PanLabelRow, column);

    auto* muteSolo = new QWidget(this);
    auto* msLayout = new QHBoxLayout(muteSolo);
    msLayout->setContentsMargins(0, 0, 0, 0);
    msLayout->setSpacing(4);
    auto* mute = squareButton("M", QColor(240, 130, 40), tr("Mute in MIX %1").arg(m_bus), muteSolo);
    Bindings::toggle(mute, device, "ChSendMixBus" + b + "Onf", ch, true);
    auto* solo = squareButton("S", QColor(240, 200, 40), tr("Solo in MIX %1").arg(m_bus), muteSolo);
    Bindings::toggle(solo, device, channelSolo(m_bus), ch);
    msLayout->addWidget(mute);
    msLayout->addWidget(solo);
    addCentered(grid, muteSolo, MuteSoloRow, column);

    if (ch < 4)
    {
        // +48V is switched on the front panel, per input pair
        auto* phantom = caption(QString(), this);
        phantom->setToolTip(tr("Phantom power, switched on the front panel"));
        Bindings::follow(phantom, device, "HaP48VOnf", uint16_t(ch / 2), [phantom](int32_t v) {
            phantom->setText(v ? QString::fromUtf8("<span style='color:#ff4030'>●</span> +48V")
                               : QString::fromUtf8("<span style='color:#606060'>●</span> +48V"));
        });
        addCentered(grid, phantom, PhantomRow, column);
    }

    auto* level = fader(this);
    auto* levelLabel = caption(QString(), this);
    Bindings::slider(level, device, "ChSendMixBus" + b + "Level", ch, levelLabel);
    addCentered(grid, level, FaderRow, column);
    addCentered(grid, levelLabel, LevelLabelRow, column);
    addCentered(grid, caption(tr("IN %1").arg(ch + 1), this), NameRow, column);
}

void MixPage::addDawColumn(QGridLayout* grid, int column)
{
    const QString b = QString::number(m_bus);
    m_dawMeter = new LevelMeter(2, this);
    m_dawMeter->setFixedHeight(90);
    addCentered(grid, m_dawMeter, MeterRow, column);

    auto* pan = new Knob(PAN_COLOR, this);
    auto* panLabel = caption(QString(), this);
    Bindings::slider(pan, m_device, "DawSendMixBus" + b + "Bal", 0, panLabel);
    addCentered(grid, pan, PanRow, column);
    addCentered(grid, panLabel, PanLabelRow, column);

    auto* muteSolo = new QWidget(this);
    auto* msLayout = new QHBoxLayout(muteSolo);
    msLayout->setContentsMargins(0, 0, 0, 0);
    msLayout->setSpacing(4);
    auto* mute = squareButton("M", QColor(240, 130, 40), tr("Mute in MIX %1").arg(m_bus), muteSolo);
    Bindings::toggle(mute, m_device, "DawSendMixBus" + b + "Onf", 0, true);
    auto* solo = squareButton("S", QColor(240, 200, 40), tr("Solo in MIX %1").arg(m_bus), muteSolo);
    Bindings::toggle(solo, m_device, dawSolo(m_bus), 0);
    msLayout->addWidget(mute);
    msLayout->addWidget(solo);
    addCentered(grid, muteSolo, MuteSoloRow, column);

    auto* level = fader(this);
    auto* levelLabel = caption(QString(), this);
    Bindings::slider(level, m_device, "DawSendMixBus" + b + "Level", 0, levelLabel);
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

    // Inferred from the manual ("turns REV-X on/off; can be on for either MIX 1 or MIX 2"): lit when REV-X isn't
    // bypassed and the channels' REV-X sends belong to this mix (PatchFxSend)
    auto* sendOn = textButton("SEND", REVX_COLOR, QString(), revx);
    sendOn->setFixedWidth(48);
    auto* edit = squareButton("e", QColor(200, 200, 200), tr("REV-X editor (not available yet)"), revx);
    edit->setCheckable(false);
    edit->setEnabled(false);
    auto sendActive = [device, bus]() {
        return !device->value("FxRevBypass").value_or(1) && device->value("PatchFxSend").value_or(-1) == bus - 1;
    };
    auto refreshSend = [sendOn, sendActive, device, bus]() {
        sendOn->setEnabled(device->isReady());
        QSignalBlocker blocker(sendOn);
        sendOn->setChecked(sendActive());
        sendOn->setToolTip(sendActive() ? tr("REV-X is on, fed by the MIX %1 channel sends (click to turn off)").arg(bus)
                                        : tr("Click to feed REV-X from the MIX %1 channel sends").arg(bus));
    };
    connect(sendOn, &QToolButton::clicked, this, [=]() {
        if (sendActive())
        {
            device->setParameter("FxRevBypass", 0, 1);
        }
        else
        {
            device->setParameter("PatchFxSend", 0, bus - 1);
            device->setParameter("FxRevBypass", 0, 0);
        }
        refreshSend();
    });
    Bindings::follow(sendOn, device, "FxRevBypass", 0, [refreshSend](int32_t) { refreshSend(); });
    Bindings::follow(sendOn, device, "PatchFxSend", 0, [refreshSend](int32_t) { refreshSend(); });
    connect(device, &Device::stateChanged, sendOn, refreshSend);
    refreshSend();
    rl->addWidget(sendOn, 1, 0, Qt::AlignHCenter);
    rl->addWidget(edit, 1, 1, Qt::AlignHCenter);

    auto* type = new QComboBox(revx);
    Bindings::combo(type, device, "FxRevType", 0);
    rl->addWidget(type, 2, 0, 1, 2);

    auto* time = new Knob(QColor(60, 90, 200), revx);
    auto* timeLabel = caption(QString(), revx);
    Bindings::slider(time, device, "FxRevTime", 0);
    // the time depends on the type and room size (dspMixFx computes it the same way)
    auto showTime = [device, timeLabel, time]() {
        QString t = ReverbTime::text(device->value("FxRevType").value_or(0), device->value("FxRevRoomSize").value_or(0),
                                     device->value("FxRevTime").value_or(0));
        timeLabel->setText(t);
        time->setToolTip(tr("Reverb time: %1 s").arg(t));
    };
    for (const char* name : {"FxRevType", "FxRevRoomSize", "FxRevTime"})
    {
        Bindings::follow(timeLabel, device, name, 0, [showTime](int32_t) { showTime(); });
    }
    rl->addWidget(caption(tr("TIME"), revx), 3, 0);
    rl->addWidget(caption(tr("RTN"), revx), 3, 1);
    rl->addWidget(time, 4, 0, Qt::AlignHCenter);
    auto* rtn = new Knob(RETURN_COLOR, revx);
    auto* rtnLabel = caption(QString(), revx);
    Bindings::slider(rtn, device, "FxRevBus" + b + "Level", 0, rtnLabel);
    rl->addWidget(rtn, 4, 1, Qt::AlignHCenter);
    rl->addWidget(timeLabel, 5, 0);
    rl->addWidget(rtnLabel, 5, 1);
    grid->addWidget(revx, FxLocationRow, column, SendLabelRow - FxLocationRow + 1, 1, Qt::AlignHCenter);

    auto* bal = new Knob(PAN_COLOR, this);
    auto* balLabel = caption(QString(), this);
    Bindings::slider(bal, device, "MasterMixBus" + b + "Bal", 0, balLabel);
    addCentered(grid, bal, PanRow, column);
    addCentered(grid, balLabel, PanLabelRow, column);

    auto* mute = squareButton("M", QColor(240, 130, 40), tr("Mute MIX %1").arg(m_bus), this);
    Bindings::toggle(mute, device, "MasterMixBus" + b + "Onf", 0, true);
    addCentered(grid, mute, MuteSoloRow, column);

    auto* level = fader(this);
    auto* levelLabel = caption(QString(), this);
    Bindings::slider(level, device, "MasterMixBus" + b + "Level", 0, levelLabel);
    addCentered(grid, level, FaderRow, column);
    addCentered(grid, levelLabel, LevelLabelRow, column);
    addCentered(grid, caption(tr("MASTER"), this), NameRow, column);
}

QWidget* MixPage::makeSidePanel()
{
    auto* panel = new QWidget(this);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);

    // Solo status: soloing any channel mutes the rest of this mix, which is easy to forget
    m_soloIndicator = new QLabel(tr("SOLO"), panel);
    m_soloIndicator->setAlignment(Qt::AlignCenter);
    m_soloIndicator->setMargin(3);
    layout->addWidget(m_soloIndicator);
    m_clearSolo = new QPushButton(tr("Clear solo"), panel);
    m_clearSolo->setToolTip(tr("Turn off every solo in this mix"));
    connect(m_clearSolo, &QPushButton::clicked, this, &MixPage::clearSolo);
    layout->addWidget(m_clearSolo);
    layout->addStretch();

    // PHONES: 1 always plays MIX 1; 2 plays the MIX selected for it
    auto* phonesRow = new QHBoxLayout;
    auto* phones1 = squareButton("1", QColor(70, 200, 90), tr("PHONES 1 always plays MIX 1"), panel);
    phones1->setChecked(m_bus == 1);
    phones1->setEnabled(false);
    auto* phones2 = squareButton("2", QColor(70, 200, 90), QString(), panel);
    const int bus = m_bus;
    Device* device = m_device;
    Bindings::follow(phones2, device, "PatchAuxPhoneOutput", 0, [phones2, bus](int32_t v) {
        QSignalBlocker blocker(phones2);
        phones2->setChecked(v == bus - 1);
        phones2->setToolTip(v == bus - 1 ? tr("PHONES 2 plays MIX %1").arg(bus)
                                         : tr("Click to play MIX %1 on PHONES 2").arg(bus));
    });
    connect(phones2, &QToolButton::clicked, this, [phones2, device, bus]() {
        device->setParameter("PatchAuxPhoneOutput", 0, bus - 1);
        phones2->setChecked(device->value("PatchAuxPhoneOutput") == bus - 1);
    });
    connect(device, &Device::stateChanged, phones2, [phones2, device]() { phones2->setEnabled(device->isReady()); });
    phones2->setEnabled(device->isReady());
    phonesRow->addWidget(phones1);
    phonesRow->addWidget(phones2);
    layout->addLayout(phonesRow);
    layout->addWidget(caption(tr("PHONES"), panel));
    return panel;
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
    for (int column : std::as_const(m_stripColumns))
    {
        QRect strip;
        for (int row = InputTypeRow; row <= NameRow; ++row)
        {
            strip |= m_grid->cellRect(row, column);
        }
        strip.adjust(-5, -4, 5, 4);
        p.setPen(Qt::NoPen);
        p.setBrush(panel);
        p.drawRoundedRect(strip, 4, 4);

        // section boundaries; skip the ones that would only frame empty space (DAW) or cut the REV-X block (master)
        QList<int> boundaries{MeterRow, FxTypeRow, SendLabelRow, PanLabelRow, PhantomRow};
        if (column == 7)
        {
            boundaries.removeAll(FxTypeRow);
        }
        if (column == 9)
        {
            boundaries.removeAll(FxTypeRow);
        }
        p.setPen(QPen(line, 1));
        for (int row : std::as_const(boundaries))
        {
            int y = (m_grid->cellRect(row, column).bottom() + m_grid->cellRect(row + 1, column).top() + 1) / 2;
            p.drawLine(QPointF(strip.left() + 3, y + 0.5), QPointF(strip.right() - 3, y + 0.5));
        }
    }
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
