#include "Bindings.h"

#include <QAbstractButton>
#include <QAbstractSlider>
#include <QComboBox>
#include <QDebug>
#include <QEvent>
#include <QHelpEvent>
#include <QLabel>
#include <QToolTip>

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

/// The full text of a value: label and unit, with -INF as "-∞"
QString fullText(const Parameter* p, int32_t v)
{
    return *p->unit ? p->label(v) + ' ' + QLatin1String(p->unit) : p->label(v);
}

/// describe()'s text, kept on the slider
class SliderInfo : public QObject
{
public:
    explicit SliderInfo(QObject* parent) : QObject(parent) {}
    QString title;
    QString description;
    Format format;
};

SliderInfo* infoOf(const QObject* slider)
{
    for (QObject* child : slider->children())
    {
        if (auto* info = dynamic_cast<SliderInfo*>(child))
        {
            return info;
        }
    }
    return nullptr;
}

/// Shows a bound slider's tooltip (on the slider and its value label), composed when it's asked for
class SliderTip : public QObject
{
public:
    SliderTip(QAbstractSlider* slider, const Parameter* p) : QObject(slider), m_slider(slider), m_p(p) {}

    QString text() const
    {
        const SliderInfo* info = infoOf(m_slider);
        auto value = [this, info](int32_t v) { return info && info->format ? info->format(v) : fullText(m_p, v); };
        QStringList lines;
        if (info && !info->title.isEmpty())
        {
            lines << "<b>" + info->title.toHtmlEscaped() + "</b>";
        }
        if (info && !info->description.isEmpty())
        {
            lines << info->description.toHtmlEscaped();
        }
        lines << QObject::tr("Range: %1 \u2013 %2").arg(value(m_p->min), value(m_p->max)).toHtmlEscaped();
        if (m_slider->isEnabled())
        {
            lines << QObject::tr("Value: %1").arg(value(m_slider->value())).toHtmlEscaped();
        }
        lines << "<i>" + QObject::tr("Double-click to reset to %1").arg(value(m_p->defaultValue)).toHtmlEscaped() + "</i>";
        return "<p style='white-space:pre'>" + lines.join("<br>") + "</p>";
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::ToolTip)
        {
            auto* help = static_cast<QHelpEvent*>(event);
            QToolTip::showText(help->globalPos(), text(), static_cast<QWidget*>(watched));
            return true;
        }
        return false;
    }

private:
    QAbstractSlider* m_slider;
    const Parameter* m_p;
};

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

void slider(QAbstractSlider* slider, Device* device, const QString& name, uint16_t index, QLabel* valueLabel,
            Setter setter, bool withUnit)
{
    const Parameter* p = Parameters::instance().byName(name);
    if (p)
    {
        slider->setRange(p->min, p->max);
        slider->setPageStep(qMax(1, (p->max - p->min) / 10));
        slider->installEventFilter(new DoubleClickReset(slider, p->defaultValue));
        auto* tip = new SliderTip(slider, p);
        slider->installEventFilter(tip);
        if (valueLabel)
        {
            valueLabel->installEventFilter(tip);
        }
    }
    auto show = [slider, valueLabel, p, withUnit](int32_t v) {
        if (!p)
        {
            return;
        }
        if (valueLabel)
        {
            valueLabel->setText(withUnit ? fullText(p, v) : p->label(v));
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
    QObject::connect(slider, &QAbstractSlider::valueChanged, slider, [device, p, index, show, setter](int v) {
        if (setter)
        {
            setter(v);
        }
        else
        {
            device->setParameter(p->id, index, v);
        }
        show(v);
    });
}

void describe(QAbstractSlider* slider, const QString& title, const QString& description, Format format)
{
    SliderInfo* info = infoOf(slider);
    if (!info)
    {
        info = new SliderInfo(slider);
    }
    info->title = title;
    info->description = description;
    info->format = std::move(format);
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

void combo(QComboBox* combo, Device* device, const QString& name, uint16_t index, Setter setter)
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
    QObject::connect(combo, &QComboBox::activated, combo, [combo, device, p, index, setter](int i) {
        int32_t v = combo->itemData(i).toInt();
        if (setter)
        {
            setter(v);
        }
        else
        {
            device->setParameter(p->id, index, v);
        }
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
