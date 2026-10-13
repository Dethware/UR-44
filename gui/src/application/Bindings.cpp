#include "Bindings.h"

#include <QAbstractButton>
#include <QAbstractSlider>
#include <QComboBox>
#include <QDebug>
#include <QLabel>

#include "Device.h"
#include "Parameters.h"

using UR44::Device;
using UR44::Parameter;
using UR44::Parameters;

namespace Bindings
{

namespace
{
/*
 * Calls `update(value)` whenever the bound instance changes (or the state loads), and keeps `widget` enabled only
 * while the device is ready and has a value for it.
 */
template<typename Update>
const Parameter* watch(QWidget* widget, Device* device, const QString& name, uint16_t index, Update update)
{
    const Parameter* p = Parameters::instance().byName(name);
    if (!p)
    {
        qCritical() << "Binding to unknown parameter" << name;
        widget->setEnabled(false);
        return nullptr;
    }

    auto refresh = [widget, device, p, index, update]() {
        auto v = device->value(p->id, index);
        widget->setEnabled(device->isReady() && v.has_value());
        if (v)
        {
            update(*v);
        }
    };
    QObject::connect(device, &Device::stateLoaded, widget, refresh);
    QObject::connect(device, &Device::stateChanged, widget, refresh);
    QObject::connect(device, &Device::parameterChanged, widget,
                     [p, index, update](uint16_t id, uint16_t i, int32_t value) {
                         if (id == p->id && i == index)
                         {
                             update(value);
                         }
                     });
    refresh();
    return p;
}
}

void slider(QAbstractSlider* slider, Device* device, const QString& name, uint16_t index, QLabel* valueLabel)
{
    const Parameter* p = Parameters::instance().byName(name);
    if (p)
    {
        slider->setRange(p->min, p->max);
        slider->setPageStep(qMax(1, (p->max - p->min) / 10));
    }
    auto show = [valueLabel, p](int32_t v) {
        if (valueLabel && p)
        {
            valueLabel->setText(p->text(v));
        }
    };
    p = watch(slider, device, name, index, [slider, show](int32_t v) {
        QSignalBlocker blocker(slider);
        slider->setValue(v);
        show(v);
    });
    if (!p)
    {
        return;
    }
    QObject::connect(slider, &QAbstractSlider::valueChanged, slider, [device, p, index, show](int v) {
        device->setParameter(p->id, index, v);
        show(v);
    });
}

void toggle(QAbstractButton* button, Device* device, const QString& name, uint16_t index, const QString& onText,
            const QString& offText)
{
    button->setCheckable(true);
    auto showState = [button, onText, offText](bool checked) {
        if (!onText.isEmpty() || !offText.isEmpty())
        {
            button->setText(checked ? onText : offText);
        }
    };
    showState(button->isChecked());
    const Parameter* p = watch(button, device, name, index, [button, showState](int32_t v) {
        QSignalBlocker blocker(button);
        button->setChecked(v != 0);
        showState(v != 0);
    });
    if (!p)
    {
        return;
    }
    QObject::connect(button, &QAbstractButton::toggled, button, [device, p, index, showState](bool checked) {
        device->setParameter(p->id, index, checked ? 1 : 0);
        showState(checked);
    });
}

void combo(QComboBox* combo, Device* device, const QString& name, uint16_t index)
{
    const Parameter* p = Parameters::instance().byName(name);
    if (p)
    {
        combo->clear();
        for (int32_t v = p->min; v <= p->max; ++v)
        {
            combo->addItem(p->text(v), v);
        }
    }
    p = watch(combo, device, name, index, [combo](int32_t v) {
        QSignalBlocker blocker(combo);
        combo->setCurrentIndex(combo->findData(v));
    });
    if (!p)
    {
        return;
    }
    QObject::connect(combo, &QComboBox::activated, combo, [combo, device, p, index](int i) {
        device->setParameter(p->id, index, combo->itemData(i).toInt());
    });
}

void label(QLabel* label, Device* device, const QString& name, uint16_t index, const QString& format)
{
    const Parameter* p = Parameters::instance().byName(name);
    watch(label, device, name, index, [label, p, format](int32_t v) { label->setText(format.arg(p->text(v))); });
}

}
