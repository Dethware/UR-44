#include "Bindings.h"

#include <QAbstractButton>
#include <QAbstractSlider>
#include <QComboBox>
#include <QDebug>
#include <QEvent>
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
const Parameter* watch(QWidget* widget, Device* device, const QString& name, uint16_t index, Update update,
                       bool manageEnabled = true)
{
    const Parameter* p = Parameters::instance().byName(name);
    if (!p)
    {
        qCritical() << "Binding to unknown parameter" << name;
        widget->setEnabled(false);
        return nullptr;
    }

    auto refresh = [widget, device, p, index, update, manageEnabled]() {
        auto v = device->value(p->id, index);
        if (manageEnabled)
        {
            widget->setEnabled(device->isReady() && v.has_value());
        }
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

/// Resets a slider to a value on double-click
class DoubleClickReset : public QObject
{
public:
    DoubleClickReset(QAbstractSlider* slider, int value) : QObject(slider), m_value(value) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::MouseButtonDblClick)
        {
            static_cast<QAbstractSlider*>(watched)->setValue(m_value);
            return true;
        }
        return false;
    }

private:
    int m_value;
};
}

void slider(QAbstractSlider* slider, Device* device, const QString& name, uint16_t index, QLabel* valueLabel)
{
    const Parameter* p = Parameters::instance().byName(name);
    if (p)
    {
        slider->setRange(p->min, p->max);
        slider->setPageStep(qMax(1, (p->max - p->min) / 10));
        slider->installEventFilter(new DoubleClickReset(slider, p->defaultValue));
    }
    auto show = [slider, valueLabel, p](int32_t v) {
        if (!p)
        {
            return;
        }
        slider->setToolTip(p->text(v));
        if (valueLabel)
        {
            valueLabel->setText(p->label(v));
            valueLabel->setToolTip(p->text(v));
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

void toggle(QAbstractButton* button, Device* device, const QString& name, uint16_t index, bool inverted,
            const QString& onText, const QString& offText)
{
    button->setCheckable(true);
    auto showState = [button, onText, offText](bool checked) {
        if (!onText.isEmpty() || !offText.isEmpty())
        {
            button->setText(checked ? onText : offText);
        }
    };
    showState(button->isChecked());
    const Parameter* p = watch(button, device, name, index, [button, showState, inverted](int32_t v) {
        bool checked = (v != 0) != inverted;
        QSignalBlocker blocker(button);
        button->setChecked(checked);
        showState(checked);
    });
    if (!p)
    {
        return;
    }
    QObject::connect(button, &QAbstractButton::toggled, button, [device, p, index, showState, inverted](bool checked) {
        device->setParameter(p->id, index, checked != inverted ? 1 : 0);
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
            combo->addItem(p->label(v), v);
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

void follow(QWidget* context, Device* device, const QString& name, uint16_t index, std::function<void(int32_t)> update)
{
    watch(context, device, name, index, update, false);
}

}
